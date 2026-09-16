/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * EN 为高时 I2C 才有应答。本例把 EN 接到 GPIO，读一遍状态后关轨。
 *
 * I2C ACKs only while EN is high. This example wires EN to a GPIO, reads
 * status once, then drops the rails.
 */

#include "sy7636a.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char* TAG = "sy7636a_ex";

#ifndef CONFIG_EXAMPLE_I2C_SDA
#define CONFIG_EXAMPLE_I2C_SDA 39
#define CONFIG_EXAMPLE_I2C_SCL 40
#define CONFIG_EXAMPLE_SY_EN 0
#endif

void app_main(void) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_EXAMPLE_I2C_SDA,
        .scl_io_num = CONFIG_EXAMPLE_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    sy7636a_handle_t pmic = NULL;
    sy7636a_config_t config = SY7636A_CONFIG_DEFAULT();
    config.en_gpio = CONFIG_EXAMPLE_SY_EN;
    if (sy7636a_init(bus, &config, &pmic) != ESP_OK) {
        ESP_LOGE(TAG, "init failed");
        return;
    }

    if (sy7636a_power_on(pmic) != ESP_OK) {
        ESP_LOGE(TAG, "power on failed (EN / PGOOD?)");
        sy7636a_deinit(pmic);
        return;
    }

    sy7636a_status_t st;
    if (sy7636a_read(pmic, &st) == ESP_OK) {
        ESP_LOGI(
            TAG, "on=%d VCOM=-%d mV %s T=%dC fault=%s",
            st.on, st.vcom_mv, sy7636a_vldo_name(st.vldo),
            st.temperature_c, sy7636a_fault_name(st.fault)
        );
    }

    sy7636a_power_off(pmic);
    sy7636a_deinit(pmic);
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}
