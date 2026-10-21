/* SPDX-License-Identifier: Apache-2.0
 * 默认构造器在内存不足时会断言；传输入口须返回错误，句柄交给调用方清理。
 * Default WiFi constructors assert on allocation/attach failure. Transfer is a
 * recoverable UI action: return the error and leave any handle to caller cleanup.
 */
#pragma once
#include <stdbool.h>
#include "esp_netif.h"
#include "esp_wifi_default.h"

static esp_err_t transfer_create_netif(bool ap, esp_netif_t **out) {
    esp_netif_config_t cfg = ap ? (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_AP()
                                : (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_STA();
    *out = esp_netif_new(&cfg);
    if (!*out) return ESP_ERR_NO_MEM;
    esp_err_t err = ap ? esp_netif_attach_wifi_ap(*out) : esp_netif_attach_wifi_station(*out);
    if (err != ESP_OK) return err;
    return ap ? esp_wifi_set_default_wifi_ap_handlers() : esp_wifi_set_default_wifi_sta_handlers();
}
