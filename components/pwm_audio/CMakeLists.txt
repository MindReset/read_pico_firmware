idf_component_register(
    SRCS "pwm_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_gptimer esp_driver_ledc
    PRIV_REQUIRES heap
)
