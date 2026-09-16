idf_component_register(
    SRCS "sy7636a.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES esp_timer freertos esp_rom
)
