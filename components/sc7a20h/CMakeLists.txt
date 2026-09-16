idf_component_register(
    SRCS "sc7a20h.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES esp_timer freertos esp_hw_support
)
