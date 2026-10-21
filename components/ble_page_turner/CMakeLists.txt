# BLE HID 主机（蓝牙翻页器）。依赖 NimBLE 主机与 BT 控制器，都是 IDF 自带组件。
# BLE HID host (Bluetooth page-turner). Needs the NimBLE host and the BT controller, both
# bundled with ESP-IDF.
idf_component_register(
    SRCS "src/ble_page_turner.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_timer read_pico push_sink
)
