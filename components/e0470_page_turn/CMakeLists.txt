idf_component_register(
    SRCS "e0470_page_turn.c"
    INCLUDE_DIRS "include"
    REQUIRES epdiy e0470_epaper_waveform
    PRIV_REQUIRES log esp_timer
)
