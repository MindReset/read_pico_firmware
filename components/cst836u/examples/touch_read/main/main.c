/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 轮询 CST836U 触点。接了 RST 则演示深睡再唤醒。
 * 引脚：idf.py menuconfig → CST836U touch_read
 *
 * Poll CST836U points. If RST is wired, demo deep sleep then wake.
 * Pins: idf.py menuconfig → CST836U touch_read
 */

#include "cst836u.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "touch_read";

void app_main(void) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_t)CONFIG_CST836U_EXAMPLE_I2C_PORT,
        .sda_io_num = (gpio_num_t)CONFIG_CST836U_EXAMPLE_SDA,
        .scl_io_num = (gpio_num_t)CONFIG_CST836U_EXAMPLE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    cst836u_handle_t tp = NULL;
    cst836u_config_t config = CST836U_CONFIG_DEFAULT();
    config.int_gpio = (gpio_num_t)CONFIG_CST836U_EXAMPLE_INT;
    if (CONFIG_CST836U_EXAMPLE_RST >= 0) {
        config.rst_gpio = (gpio_num_t)CONFIG_CST836U_EXAMPLE_RST;
    }
    if (cst836u_init(bus, &config, &tp) != ESP_OK) {
        ESP_LOGE(TAG, "touch init failed");
        return;
    }

    cst836u_info_t info;
    if (cst836u_get_info(tp, &info) == ESP_OK) {
        ESP_LOGI(TAG, "FW=0x%02X ID=0x%02X type=0x%04X", info.fw, info.id, info.type);
    }

    for (int i = 0; i < 80; i++) {
        cst836u_touch_t t;
        if (cst836u_read(tp, &t) == ESP_OK && t.touched) {
            ESP_LOGI(
                TAG, "irq=%d n=%u P1 %s %u,%u P2 %s %u,%u",
                cst836u_int_asserted(tp) ? 1 : 0, t.count,
                cst836u_event_name(t.points[0].event), t.points[0].x, t.points[0].y,
                cst836u_event_name(t.points[1].event), t.points[1].x, t.points[1].y
            );
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (CONFIG_CST836U_EXAMPLE_RST >= 0) {
        ESP_LOGI(TAG, "deep sleep 2s then RST wake");
        ESP_ERROR_CHECK(cst836u_set_mode(tp, CST836U_MODE_DEEPSLEEP));
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_ERROR_CHECK(cst836u_wake(tp));
        ESP_LOGI(TAG, "awake, mode=%s", cst836u_mode_name(cst836u_get_mode(tp)));
    }
    cst836u_deinit(tp);
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}
