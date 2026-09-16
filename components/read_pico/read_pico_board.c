/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 板级 I2C、FCA9555、SY7636A 与 EPD 电源轨。
 *
 * Board I2C, FCA9555, SY7636A, and EPD rails.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "epd_board.h"
#include "epd_lcd.h"
#include "epdiy.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fca9555.h"
#include "read_pico_board.h"
#include "read_pico_epd_timing.h"
#include "sdkconfig.h"
#include "sy7636a.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
// 避开 hal/lcd_periph.h（私有 esp_hal_i2s 依赖）；S3 上 DE == LCD_H_ENABLE。
// / Skip hal/lcd_periph.h (private esp_hal_i2s dep); DE == LCD_H_ENABLE on S3.
#include <soc/gpio_sig_map.h>
#define BOARD_LCD_DE_SIG LCD_H_ENABLE_IDX
#elif ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 2)
#include <soc/lcd_periph.h>
#define BOARD_LCD_DE_SIG lcd_periph_signals.panels[0].de_sig
#else
#include <soc/lcd_periph.h>
#define BOARD_LCD_DE_SIG lcd_periph_rgb_signals.panels[0].de_sig
#endif

#define TAG "read_pico"

#define EPD_I2C_SCL ((gpio_num_t)CONFIG_READ_PICO_I2C_SCL_GPIO)
#define EPD_I2C_SDA ((gpio_num_t)CONFIG_READ_PICO_I2C_SDA_GPIO)

#define IOE_MODE (1U << 0)
#define IOE_XOE (1U << 1)
#define IOE_CW_INT (1U << 2)
#define IOE_SY_EN (1U << 3)
#define IOE_SY_VCOM_EN (1U << 4)
#define IOE_SY_PGOOD (1U << 5)
#define IOE_SD_CD (1U << 6)
#define IOE_TP_RST (1U << 7)

// P0.2 CW_INT / P0.5 PGOOD / P0.6 SD_CD 为输入，其余为输出。
// / P0.2 CW_INT / P0.5 PGOOD / P0.6 SD_CD are inputs; the rest are outputs.
#define IOE_CONFIG_PORT0 0x64
#define IOE_REG_COUNT 8

#define D0 GPIO_NUM_4
#define D1 GPIO_NUM_5
#define D2 GPIO_NUM_6
#define D3 GPIO_NUM_7
#define D4 GPIO_NUM_8
#define D5 GPIO_NUM_9
#define D6 GPIO_NUM_10
#define D7 GPIO_NUM_11
#define D8 GPIO_NUM_12
#define D9 GPIO_NUM_13
#define D10 GPIO_NUM_14
#define D11 GPIO_NUM_15
#define D12 GPIO_NUM_16
#define D13 GPIO_NUM_17
#define D14 GPIO_NUM_18
#define D15 GPIO_NUM_45

#define EPD_XLE GPIO_NUM_3
#define EPD_XSTL GPIO_NUM_46
#define EPD_XCL GPIO_NUM_21
#define EPD_SPV GPIO_NUM_47
#define EPD_CKV GPIO_NUM_48

static uint8_t ioe_output;
static bool rails_on;
static i2c_master_bus_handle_t s_i2c_bus;
static sy7636a_handle_t s_sy;
static fca9555_handle_t s_ioe;

static esp_err_t ioe_set(uint8_t mask, bool high);

i2c_master_bus_handle_t read_pico_i2c_bus(void) {
    return s_i2c_bus;
}

static void read_pico_i2c_deinit(void) {
    if (s_sy) {
        sy7636a_deinit(s_sy);
        s_sy = NULL;
    }
    if (s_ioe) {
        fca9555_deinit(s_ioe);
        s_ioe = NULL;
    }
    if (s_i2c_bus != NULL) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

static lcd_bus_config_t lcd_config = {
    .clock = EPD_XCL,
    .ckv = EPD_CKV,
    .leh = EPD_XLE,
    .start_pulse = EPD_XSTL,
    .stv = EPD_SPV,
    .data = {
        D0, D1, D2, D3, D4, D5, D6, D7,
        D8, D9, D10, D11, D12, D13, D14, D15,
    },
};

static const char* i2c_dev_name(uint8_t addr) {
    switch (addr) {
        case 0x15: return "CST836U";
        case 0x19: return "SC7A20H";
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27: return "FCA9555?";
        case 0x2A: return "CW32";
        case 0x62: return "SY7636A";
        default: return NULL;
    }
}

static esp_err_t i2c_probe(uint8_t addr) {
    return i2c_master_probe(s_i2c_bus, addr, 50);
}

// USB/软件复位不会给从机掉电。CST836U / CW32 若卡在应答中间，会把 SDA 拉死，
// 新 I2C 驱动的 probe 就会对每个地址都 TIMEOUT（不是 NACK）。
// / USB/soft reset does not power-cycle slaves. A stuck CST836U / CW32 holds
// SDA; the new I2C probe then TIMEs OUT on every address (not NACK).
static void i2c_bus_recover(void) {
    gpio_hold_dis(EPD_I2C_SDA);
    gpio_hold_dis(EPD_I2C_SCL);
    gpio_sleep_sel_dis(EPD_I2C_SDA);
    gpio_sleep_sel_dis(EPD_I2C_SCL);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << EPD_I2C_SDA) | (1ULL << EPD_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(EPD_I2C_SDA, 1);
    gpio_set_level(EPD_I2C_SCL, 1);
    esp_rom_delay_us(20);

    int sda = gpio_get_level(EPD_I2C_SDA);
    int scl = gpio_get_level(EPD_I2C_SCL);
    if (sda && scl) {
        ESP_LOGI(TAG, "I2C lines idle SDA=%d SCL=%d", sda, scl);
    } else {
        ESP_LOGW(TAG, "I2C bus stuck SDA=%d SCL=%d, clocking recover", sda, scl);
    }

    for (int i = 0; i < 9; ++i) {
        if (gpio_get_level(EPD_I2C_SDA)) break;
        gpio_set_level(EPD_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(EPD_I2C_SCL, 1);
        esp_rom_delay_us(5);
    }

    gpio_set_level(EPD_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(EPD_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(EPD_I2C_SDA, 1);
    esp_rom_delay_us(5);

    sda = gpio_get_level(EPD_I2C_SDA);
    scl = gpio_get_level(EPD_I2C_SCL);
    if (!sda || !scl) {
        ESP_LOGE(TAG, "I2C recover failed SDA=%d SCL=%d (need power cycle)", sda, scl);
    }
}

static void i2c_scan_bus(void) {
    int found = 0;
    ESP_LOGI(TAG, "I2C scan SCL=%d SDA=%d 400kHz", EPD_I2C_SCL, EPD_I2C_SDA);
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        esp_err_t err = i2c_probe(addr);
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2C hung at 0x%02X, reset bus", addr);
            i2c_master_bus_reset(s_i2c_bus);
            err = i2c_probe(addr);
            if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "I2C still hung, abort scan");
                break;
            }
        }
        if (err != ESP_OK) continue;
        const char* name = i2c_dev_name(addr);
        if (name != NULL) {
            ESP_LOGI(TAG, "  ACK 0x%02X %s", addr, name);
        } else {
            ESP_LOGI(TAG, "  ACK 0x%02X", addr);
        }
        found++;
    }
    if (found == 0) {
        ESP_LOGE(TAG, "I2C scan: no devices (SDA/SCL wiring or pull-up?)");
    } else {
        ESP_LOGI(TAG, "I2C scan: %d device(s)", found);
    }
}

static esp_err_t fca_expect_reg(uint8_t reg, uint8_t expected, const char* name) {
    uint8_t actual = 0;
    esp_err_t err = fca9555_read_reg(s_ioe, reg, &actual);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FCA9555 %s read 0x%02X failed: %s", name, reg, esp_err_to_name(err));
        return err;
    }
    if (actual != expected) {
        ESP_LOGE(TAG, "FCA9555 %s mismatch: wrote 0x%02X, read 0x%02X", name, expected, actual);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "FCA9555 %s OK: 0x%02X", name, actual);
    return ESP_OK;
}

// 上电自检：ACK、8 寄存器可读、CFG/OUT 回读。不拉 SY_EN / VCOM，避免误开高压。
// / Power-on check: ACK, 8 readable regs, CFG/OUT readback. Leave SY_EN / VCOM
// low so HV does not come up by accident.
static esp_err_t fca9555_selftest(void) {
    static const char* const reg_names[IOE_REG_COUNT] = {
        "IN0", "IN1", "OUT0", "OUT1", "INV0", "INV1", "CFG0", "CFG1",
    };
    uint8_t regs[IOE_REG_COUNT] = { 0 };

    ESP_LOGI(TAG, "FCA9555 self-test start, addr=0x%02X (A2=1 A1=0 A0=0)", FCA9555_ADDR_DEFAULT);
    i2c_scan_bus();

    esp_err_t err = i2c_probe(FCA9555_ADDR_DEFAULT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FCA9555 NACK at 0x%02X: %s", FCA9555_ADDR_DEFAULT, esp_err_to_name(err));
        for (uint8_t addr = 0x20; addr <= 0x27; ++addr) {
            if (addr == FCA9555_ADDR_DEFAULT) continue;
            if (i2c_probe(addr) == ESP_OK) {
                ESP_LOGW(TAG, "FCA9555-like ACK at 0x%02X (A2A1A0=%d%d%d)",
                         addr, (addr >> 2) & 1, (addr >> 1) & 1, addr & 1);
            }
        }
        return err;
    }
    ESP_LOGI(TAG, "FCA9555 ACK OK");

    for (uint8_t i = 0; i < IOE_REG_COUNT; ++i) {
        err = fca9555_read_reg(s_ioe, i, &regs[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "FCA9555 %s read failed: %s", reg_names[i], esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(
        TAG,
        "FCA9555 regs IN=%02X%02X OUT=%02X%02X INV=%02X%02X CFG=%02X%02X",
        regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]
    );

    err = fca9555_set_config(s_ioe, 0, IOE_CONFIG_PORT0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FCA9555 CFG0 write failed: %s", esp_err_to_name(err));
        return err;
    }
    err = fca_expect_reg(6, IOE_CONFIG_PORT0, "CFG0");
    if (err != ESP_OK) return err;

    err = fca9555_set_config(s_ioe, 1, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FCA9555 CFG1 write failed: %s", esp_err_to_name(err));
        return err;
    }
    err = fca_expect_reg(7, 0xFF, "CFG1");
    if (err != ESP_OK) return err;

    // 只翻转 MODE / TP_RST，SY_EN 和 VCOM 保持低。
    // / Toggle MODE / TP_RST only; keep SY_EN and VCOM low.
    const uint8_t out_a = IOE_MODE | IOE_TP_RST;
    const uint8_t out_b = IOE_MODE;
    err = fca9555_set_output(s_ioe, 0, out_a);
    if (err != ESP_OK) return err;
    err = fca_expect_reg(2, out_a, "OUT0");
    if (err != ESP_OK) return err;
    err = fca9555_set_output(s_ioe, 0, out_b);
    if (err != ESP_OK) return err;
    err = fca_expect_reg(2, out_b, "OUT0 toggle");
    if (err != ESP_OK) return err;
    err = fca9555_set_output(s_ioe, 0, out_a);
    if (err != ESP_OK) return err;

    uint8_t in0 = 0;
    err = fca9555_read_reg(s_ioe, 0, &in0);
    if (err != ESP_OK) return err;
    ESP_LOGI(
        TAG,
        "FCA9555 IN0=0x%02X CW_INT=%d PGOOD=%d SD_CD=%s",
        in0,
        (in0 & IOE_CW_INT) ? 1 : 0,
        (in0 & IOE_SY_PGOOD) ? 1 : 0,
        (in0 & IOE_SD_CD) ? "absent" : "present"
    );
    ESP_LOGI(TAG, "FCA9555 self-test PASS");
    return ESP_OK;
}

static esp_err_t ioe_commit(void) {
    return fca9555_set_output(s_ioe, 0, ioe_output);
}

static esp_err_t ioe_set(uint8_t mask, bool high) {
    if (high) {
        ioe_output |= mask;
    } else {
        ioe_output &= (uint8_t)~mask;
    }
    return ioe_commit();
}

static esp_err_t board_sy_en(bool on) {
    return ioe_set(IOE_SY_EN, on);
}

static esp_err_t board_sy_vcom_en(bool on) {
    return ioe_set(IOE_SY_VCOM_EN, on);
}

static int board_sy_pgood(void) {
    uint8_t in0 = 0;
    if (s_ioe == NULL || fca9555_read_input(s_ioe, 0, &in0) != ESP_OK) return 0;
    return (in0 & IOE_SY_PGOOD) ? 1 : 0;
}

static void board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    bool changed = false;
    if (mask->ep_mode) {
        if (state->ep_mode) ioe_output |= IOE_MODE;
        else ioe_output &= (uint8_t)~IOE_MODE;
        changed = true;
    }
    if (mask->ep_output_enable) {
        if (state->ep_output_enable) ioe_output |= IOE_XOE;
        else ioe_output &= (uint8_t)~IOE_XOE;
        changed = true;
    }
    if (changed) {
        // I2C 挂了不能 abort，否则自检中断后刷屏会把整机打回主界面。
        // / Do not abort on a dead I2C; a self-test interrupt mid-refresh would
        // bounce the device back to home.
        esp_err_t err = ioe_commit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ioe_commit %s", esp_err_to_name(err));
        }
    }
}

static void board_init(uint32_t epd_row_width) {
    i2c_bus_recover();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = READ_PICO_I2C_PORT,
        .sda_io_num = EPD_I2C_SDA,
        .scl_io_num = EPD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    fca9555_config_t ioe_config = FCA9555_CONFIG_DEFAULT();
    ioe_config.i2c_addr = FCA9555_ADDR_DEFAULT;
    ioe_config.int_gpio = (gpio_num_t)READ_PICO_IOE_INT_GPIO;
    ESP_ERROR_CHECK(fca9555_init(s_i2c_bus, &ioe_config, &s_ioe));

    esp_err_t fca_err = fca9555_selftest();
    if (fca_err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "FCA9555 self-test timeout, reset bus and retry");
        i2c_master_bus_reset(s_i2c_bus);
        fca_err = fca9555_selftest();
    }
    if (fca_err != ESP_OK) {
        ESP_LOGE(TAG, "FCA9555 self-test FAILED: %s, stop before EPD init", esp_err_to_name(fca_err));
    }
    ESP_ERROR_CHECK(fca_err);

    // P0.7 作为 CST836U 的低有效复位输出。
    // / P0.7 is the CST836U active-low reset output.
    ESP_ERROR_CHECK(fca9555_set_config(s_ioe, 0, IOE_CONFIG_PORT0));
    ioe_output = IOE_MODE | IOE_TP_RST;
    ESP_ERROR_CHECK(ioe_commit());

    ESP_ERROR_CHECK(ioe_set(IOE_TP_RST, false));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(ioe_set(IOE_TP_RST, true));
    vTaskDelay(pdMS_TO_TICKS(50));

    sy7636a_config_t sy = SY7636A_CONFIG_DEFAULT();
    sy.power.vcom_mv = 1290;
    sy.en_fn = board_sy_en;
    sy.vcom_en_fn = board_sy_vcom_en;
    sy.pgood_fn = board_sy_pgood;
    ESP_ERROR_CHECK(sy7636a_init(s_i2c_bus, &sy, &s_sy));

    const EpdDisplay_t* display = epd_get_display();
    const int init_pclk_mhz = display->bus_speed * 8 / display->bus_width;
    read_pico_epd_scan_t scan;
    read_pico_epd_scan(init_pclk_mhz, READ_PICO_EPD_SCAN_FULL, &scan);
    read_pico_epd_init_lcd(&lcd_config, display->bus_width, display->width, display->height, &scan);
    ESP_LOGI(TAG, "EPD bus initialized, SY7636A address 0x%02X", SY7636A_ADDR_DEFAULT);
}

static void board_poweron(epd_ctrl_state_t* state) {
    if (rails_on) return;

    esp_rom_gpio_connect_out_signal(EPD_XSTL, BOARD_LCD_DE_SIG, false, false);

    state->ep_output_enable = false;
    state->ep_mode = true;
    epd_ctrl_state_t ctrl_mask = {
        .ep_output_enable = true,
        .ep_mode = true,
    };
    board_set_ctrl(state, &ctrl_mask);

    if (sy7636a_power_on(s_sy) != ESP_OK) return;

    state->ep_output_enable = true;
    board_set_ctrl(state, &ctrl_mask);
    rails_on = true;

    sy7636a_status_t st;
    if (sy7636a_read(s_sy, &st) == ESP_OK) {
        ESP_LOGI(
            TAG, "SY7636A rails ready, VCOM=-%dmV, temperature=%dC",
            st.vcom_mv, st.temperature_c
        );
    }
}

static void board_poweroff(epd_ctrl_state_t* state) {
    state->ep_output_enable = false;
    state->ep_mode = true;
    epd_ctrl_state_t ctrl_mask = {
        .ep_output_enable = true,
        .ep_mode = true,
    };
    board_set_ctrl(state, &ctrl_mask);

    vTaskDelay(pdMS_TO_TICKS(1));
    (void)sy7636a_power_off(s_sy);
    rails_on = false;
}

static void board_deinit(void) {
    if (rails_on) {
        board_poweroff(epd_ctrl_state());
    }
    epd_lcd_deinit();
    read_pico_i2c_deinit();
}

static void board_measure_vcom(epd_ctrl_state_t* state) {
    board_poweron(state);
}

static float board_temperature(void) {
    if (!rails_on || s_sy == NULL) return 25.0f;
    sy7636a_status_t st;
    if (sy7636a_read(s_sy, &st) != ESP_OK) return 25.0f;
    return (float)st.temperature_c;
}

static void board_set_vcom(int value) {
    if (s_sy) (void)sy7636a_set_vcom(s_sy, value);
}

static void status_from_cfg(sy7636a_status_t* st, const sy7636a_power_config_t* cfg, bool on) {
    st->on = on;
    st->vcom_manual = cfg->vcom_manual;
    st->discharge = (uint8_t)(cfg->discharge & SY7636A_DISCHG_MASK);
    st->vcom_mv = cfg->vcom_mv;
    st->vldo = cfg->vldo;
    memcpy(st->dly_ms, cfg->dly_ms, sizeof(st->dly_ms));
}

sy7636a_handle_t read_pico_sy7636a(void) {
    return s_sy;
}

fca9555_handle_t read_pico_fca9555(void) {
    return s_ioe;
}

static read_pico_i2c_census_t s_census;

static const struct {
    uint8_t addr;
    const char* name;
} s_census_map[READ_PICO_I2C_DEV_N] = {
    { 0x15, "CST836U" },
    { 0x19, "SC7A20H" },
    { FCA9555_ADDR_DEFAULT, "FCA9555" },
    { 0x2A, "CW32" },
    { SY7636A_ADDR_DEFAULT, "SY7636A" },
};

void read_pico_i2c_census_take(void) {
    s_census = (read_pico_i2c_census_t){ 0 };
    s_census.all_online = true;
    for (int i = 0; i < READ_PICO_I2C_DEV_N; i++) {
        s_census.dev[i].name = s_census_map[i].name;
        s_census.dev[i].addr = s_census_map[i].addr;
        if (s_i2c_bus == NULL) {
            s_census.dev[i].online = false;
        } else {
            esp_err_t err = i2c_probe(s_census_map[i].addr);
            if (err == ESP_ERR_TIMEOUT) {
                i2c_master_bus_reset(s_i2c_bus);
                err = i2c_probe(s_census_map[i].addr);
            }
            s_census.dev[i].online = (err == ESP_OK);
        }
        if (s_census.dev[i].online) {
            ESP_LOGI(TAG, "I2C online 0x%02X %s", s_census.dev[i].addr, s_census.dev[i].name);
        } else {
            s_census.offline_n++;
            s_census.all_online = false;
            ESP_LOGW(TAG, "I2C offline 0x%02X %s", s_census.dev[i].addr, s_census.dev[i].name);
        }
    }
}

const read_pico_i2c_census_t* read_pico_i2c_census(void) {
    return &s_census;
}

int read_pico_ioe_int_level(void) {
    return fca9555_int_level(s_ioe);
}

static void status_from_map(read_pico_status_t* status, const fca9555_map_t* map) {
    status->ioe_input = map->input;
    status->ioe_output = map->output;
    status->ioe_invert = map->invert;
    status->ioe_config = map->config;
}

// refresh_cfg：连读 8 寄存器；否则只读 IN/OUT，CFG/INV 用缓存。失败不再补读。
// / refresh_cfg: sequential 8-reg read; else IN/OUT only, CFG/INV from cache.
// No fallback read on failure.
static esp_err_t fill_ioe_status(read_pico_status_t* status, bool refresh_cfg) {
    fca9555_map_t map = { 0 };
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_ioe) {
        err = (refresh_cfg || !fca9555_dir_cached(s_ioe))
            ? fca9555_read_all(s_ioe, &map)
            : fca9555_read_io(s_ioe, &map);
    }

    if (err != ESP_OK) {
        map.input = 0;
        map.output = (uint16_t)ioe_output | 0xFF00;
        if (fca9555_dir_cached(s_ioe)) {
            fca9555_get_dir_cache(s_ioe, &map.invert, &map.config);
        }
    }
    status_from_map(status, &map);
    status->ioe_int_level = read_pico_ioe_int_level();
    status->rails_on = rails_on;
    return err;
}

esp_err_t read_pico_get_status(read_pico_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;

    // SY 页只要 PGOOD（IN0 bit5），不要连读 8 寄存器。
    // / The SY page only needs PGOOD (IN0 bit5); do not sequential-read 8 regs.
    uint8_t in0 = 0;
    if (s_ioe) (void)fca9555_read_reg(s_ioe, 0, &in0);
    status->ioe_input = in0;
    status->ioe_output = (uint16_t)ioe_output | 0xFF00;
    status->ioe_invert = 0;
    status->ioe_config = 0;
    status->ioe_int_level = 1;
    status->rails_on = rails_on;
    status->sy_live = false;
    memset(&status->sy, 0, sizeof(status->sy));
    if (s_sy) status->sy.vcom_mv = sy7636a_get_vcom(s_sy);

    if (s_sy == NULL) return ESP_ERR_INVALID_STATE;

    bool woke = false;
    esp_err_t err = sy7636a_i2c_begin(s_sy, &woke);
    if (err == ESP_OK) err = sy7636a_read(s_sy, &status->sy);
    sy7636a_i2c_end(s_sy, woke);

    if (err == ESP_OK) {
        status->sy_live = true;
    } else {
        const sy7636a_status_t* last = sy7636a_last(s_sy);
        if (last) status->sy = *last;
        ESP_LOGW(TAG, "SY7636A live read failed: %s", esp_err_to_name(err));
    }

    // 轨没开时芯片已被 EN 复位，页面改显示即将写入的缓存。
    // / Rails off means EN already reset the chip; the page shows the pending cache.
    if (!rails_on) {
        const sy7636a_power_config_t* cfg = sy7636a_get_config(s_sy);
        if (cfg) status_from_cfg(&status->sy, cfg, false);
    }
    return err;
}

esp_err_t read_pico_get_ioe_status(read_pico_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    return fill_ioe_status(status, false);
}

esp_err_t read_pico_get_ioe_status_full(read_pico_status_t* status) {
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    return fill_ioe_status(status, true);
}

esp_err_t read_pico_toggle_vcomctl(void) {
    if (s_sy == NULL) return ESP_ERR_INVALID_STATE;
    const sy7636a_power_config_t* cfg = sy7636a_get_config(s_sy);
    bool next = cfg ? !cfg->vcom_manual : true;
    return sy7636a_set_vcom_manual(s_sy, next);
}

bool read_pico_rails_on(void) {
    return rails_on;
}

bool read_pico_sd_present(void) {
    // 卡座 CD 信号低有效，连接到 FCA9555 P0.6。
    // 读失败时不要把低有效 CD 判成“有卡”。
    // / Slot CD is active-low on FCA9555 P0.6. A failed read must not look like "card present".
    uint8_t in0 = 0xFF;
    if (s_ioe == NULL || fca9555_read_reg(s_ioe, 0, &in0) != ESP_OK) return false;
    return (in0 & IOE_SD_CD) == 0;
}

void read_pico_clear_ioe_int(void) {
    (void)fca9555_clear_int(s_ioe);
}

esp_err_t read_pico_tp_rst(bool high) {
    return ioe_set(IOE_TP_RST, high);
}

esp_err_t read_pico_touch_reset(void) {
    // 低有效复位，时序照 board_init 里那次上电复位：拉低 10ms 再放开。
    // / Active-low reset, same as board_init: hold 10 ms then release.
    esp_err_t err = read_pico_tp_rst(false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    return read_pico_tp_rst(true);
}

const EpdBoardDefinition epd_board_read_pico = {
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .poweroff = board_poweroff,
    .measure_vcom = board_measure_vcom,
    .get_temperature = board_temperature,
    .set_vcom = board_set_vcom,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
