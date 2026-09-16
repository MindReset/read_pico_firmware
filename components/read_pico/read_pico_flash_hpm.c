/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 覆盖 IDF 的 flash 高性能模式表，补上板上 Zbit ZB25VQ128（0x5E4018）。
 * IDF 只认同厂 8MB 的 0x5E4016，16MB 会被判不支持，120MHz 仍走默认时序。
 *
 * Override the IDF HPM table for the onboard Zbit ZB25VQ128 (0x5E4018).
 * IDF only lists the 8MB 0x5E4016; the 16MB part is treated as unsupported
 * and would run 120 MHz with default timing.
 *
 * 进入方式和 8MB 一样：写状态寄存器 3 的 HPF 位。先发 0x50（volatile SR
 * 写使能），掉电即失效，不改非易失配置。回调在 cache 关闭时跑，函数进
 * IRAM，表和字符串进 DRAM。
 * Same entry as the 8MB part: set HPF in status register 3 after 0x50
 * (write-enable for volatile SR). The bit is lost on power-off. Callbacks
 * run with cache off, so code is IRAM and the table/strings are DRAM.
 */

// spi_flash_override.h 把这张表声明成 weak，直接照着声明定义会连带继承 weak 属性，
// 于是变成两个弱符号竞争，链接器取先遇到的那个（IDF 自己那份），覆盖就白做了。
// 这里把声明改个名字避开属性，下面再定义真正的强符号。改名必须发生在任何头文件
// 引入这个声明之前，所以这一段放在所有 include 最前面。
// The header declares the table weak. Defining it under that name inherits
// weak, so two weaks fight and the linker keeps IDF's copy. Rename the
// declaration first, then define a strong symbol. The rename must happen
// before any header pulls the weak decl in.
#define spi_flash_hpm_enable_list spi_flash_hpm_enable_list_weak_decl
#include "esp_flash_chips/spi_flash_override.h"
#undef spi_flash_hpm_enable_list

#include <stdint.h>

#include "bootloader_flash_priv.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_rom_spiflash.h"

#define ZB_FLASH_ID_8MB 0x5E4016
#define ZB_FLASH_ID_16MB 0x5E4018
// 状态寄存器 3 的 HPF 位（datasheet 记作 bit5，寄存器内偏移 4）。/ HPF in SR3 (datasheet bit5, offset 4).
#define ZB_HPF_BIT (1 << 4)
// IDF 对同厂芯片的判断：超过这个频率就必须进高性能模式。/ IDF: above this MHz the part must enter HPM.
#define ZB_HPM_THRESHOLD_MHZ 104

extern void spi_flash_hpm_get_dummy_generic(spi_flash_hpm_dummy_conf_t* dummy_conf);

static IRAM_ATTR esp_err_t zb_hpm_probe(uint32_t flash_id) {
    switch (flash_id) {
        case ZB_FLASH_ID_8MB:
        case ZB_FLASH_ID_16MB:
            return ESP_OK;
        default:
            return ESP_ERR_NOT_FOUND;
    }
}

static IRAM_ATTR spi_flash_requirement_t zb_hpm_requirement_check(
    uint32_t flash_id, uint32_t freq_mhz, int voltage_mv, int temperature
) {
    (void)flash_id;
    (void)voltage_mv;
    (void)temperature;
    return freq_mhz >= ZB_HPM_THRESHOLD_MHZ ? SPI_FLASH_HPM_WRITE_SR_NEEDED
                                            : SPI_FLASH_HPM_UNNEEDED;
}

static IRAM_ATTR void zb_hpm_enable(void) {
    uint8_t status = bootloader_read_status_8b_rdsr3();
    bootloader_execute_flash_command(CMD_WRENVSR, 0, 0, 0);
    bootloader_write_status_8b_wrsr3(status | ZB_HPF_BIT);
    esp_rom_spiflash_wait_idle(&g_rom_flashchip);
}

static IRAM_ATTR esp_err_t zb_hpm_check(void) {
    return (bootloader_read_status_8b_rdsr3() & ZB_HPF_BIT) != 0 ? ESP_OK : ESP_FAIL;
}

static DRAM_ATTR const char zb_hpm_method[] = "write sr3-bit5 (ZB25VQ128)";
static DRAM_ATTR const char zb_hpm_fallback[] = "NULL";

// 覆盖 IDF 的 weak 表。最后一项 probe 为 NULL，是查找失败时的兜底。
// Strong override of IDF's weak table. A NULL probe is the not-found fallback.
DRAM_ATTR const spi_flash_hpm_info_t spi_flash_hpm_enable_list[] = {
    {
        zb_hpm_method,
        zb_hpm_probe,
        zb_hpm_requirement_check,
        zb_hpm_enable,
        zb_hpm_check,
        spi_flash_hpm_get_dummy_generic,
    },
    {
        zb_hpm_fallback,
        NULL,
        NULL,
        NULL,
        NULL,
        spi_flash_hpm_get_dummy_generic,
    },
};
