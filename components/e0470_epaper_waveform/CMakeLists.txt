idf_component_register(
    SRCS "e0470_epaper_waveform.c" "e0470_waveform_trim.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "waveforms"
    REQUIRES epdiy
)
