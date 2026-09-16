/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 转储 FCA9555 寄存器并轮询 INT#。
 * 引脚：idf.py menuconfig → FCA9555 read_ports
 *
 * Dump FCA9555 registers and poll INT#.
 * Pins: idf.py menuconfig → FCA9555 read_ports
 */

#include "fca9555.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "read_ports";

void app_main(void) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_t)CONFIG_FCA9555_EXAMPLE_I2C_PORT,
        .sda_io_num = (gpio_num_t)CONFIG_FCA9555_EXAMPLE_SDA,
        .scl_io_num = (gpio_num_t)CONFIG_FCA9555_EXAMPLE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    fca9555_handle_t ioe = NULL;
    fca9555_config_t config = FCA9555_CONFIG_DEFAULT();
    config.i2c_addr = (uint8_t)CONFIG_FCA9555_EXAMPLE_ADDR;
    if (CONFIG_FCA9555_EXAMPLE_INT >= 0) {
        config.int_gpio = (gpio_num_t)CONFIG_FCA9555_EXAMPLE_INT;
    }
    if (fca9555_init(bus, &config, &ioe) != ESP_OK) {
        ESP_LOGE(TAG, "init failed");
        return;
    }

    fca9555_map_t map;
    if (fca9555_read_all(ioe, &map) == ESP_OK) {
        ESP_LOGI(
            TAG, "IN=%04X OUT=%04X INV=%04X CFG=%04X INT=%d",
            map.input, map.output, map.invert, map.config,
            fca9555_int_level(ioe)
        );
    } else {
        ESP_LOGE(TAG, "read_all failed");
    }

    for (int i = 0; i < 40; i++) {
        if (fca9555_int_asserted(ioe) && fca9555_read_io(ioe, &map) == ESP_OK) {
            ESP_LOGI(TAG, "irq IN=%04X OUT=%04X", map.input, map.output);
            (void)fca9555_clear_int(ioe);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    fca9555_deinit(ioe);
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}
