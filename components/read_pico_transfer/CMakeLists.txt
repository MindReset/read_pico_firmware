# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 独立热点传书组件与嵌入页面。/ Standalone hotspot transfer component and embedded page.
idf_component_register(SRCS "read_pico_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES read_pico_search esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs esp_timer espressif__cjson
    EMBED_TXTFILES "upload.html")
