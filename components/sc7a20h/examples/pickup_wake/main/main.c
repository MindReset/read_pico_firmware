/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 拿起唤醒：初始化 I2C，读几组样，武装运动，浅睡。
 * 引脚：idf.py menuconfig → SC7A20H pickup_wake
 *
 * Pickup-to-wake: init I2C, read a few samples, arm motion, light sleep.
 * Pins: idf.py menuconfig → SC7A20H pickup_wake
 */

#include "sc7a20h.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "pickup_wake";

void app_main(void) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_t)CONFIG_SC7A20H_EXAMPLE_I2C_PORT,
        .sda_io_num = (gpio_num_t)CONFIG_SC7A20H_EXAMPLE_SDA,
        .scl_io_num = (gpio_num_t)CONFIG_SC7A20H_EXAMPLE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    sc7a20h_handle_t acc = NULL;
    sc7a20h_config_t config = SC7A20H_DEVICE_CONFIG_DEFAULT();
    config.int1_gpio = (gpio_num_t)CONFIG_SC7A20H_EXAMPLE_INT1;
    if (sc7a20h_init(bus, &config, &acc) != ESP_OK) {
        ESP_LOGE(TAG, "sensor init failed");
        return;
    }

    for (int i = 0; i < 5; i++) {
        sc7a20h_sample_t s;
        if (sc7a20h_read(acc, &s) == ESP_OK) {
            ESP_LOGI(TAG, "X=%dmg Y=%dmg Z=%dmg", s.x_mg, s.y_mg, s.z_mg);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_ERROR_CHECK(sc7a20h_arm_pickup_wake(acc, NULL));
    ESP_ERROR_CHECK(sc7a20h_config_light_sleep_wakeup(acc));
    ESP_LOGI(TAG, "light sleep — pick up the device to wake");
    esp_light_sleep_start();

    sc7a20h_ack_int(acc);
    ESP_LOGI(TAG, "woke, INT1=%d", sc7a20h_int1_level(acc));
    sc7a20h_power_down(acc);
    sc7a20h_deinit(acc);
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}
