# 手机 BLE 推流接收端（push sink）。纯核无依赖，胶水层要 NimBLE 主机、NVS 会话
# 簿记与 PMU 电量门禁。/ The phone BLE push receiver. The pure core has no
# dependency; the glue needs the NimBLE host, the NVS session bookkeeping and the
# PMU battery gate.
idf_component_register(
    SRCS
        "src/push_proto.c"
        "src/push_session.c"
        "src/push_sink.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_timer esp_hw_support read_pico_pmu
)
