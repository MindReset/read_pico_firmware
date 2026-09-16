idf_component_register(
    SRCS "read_pico_pmu.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES freertos esp_rom
)
