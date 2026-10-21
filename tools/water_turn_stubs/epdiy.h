#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { int phases; const int* phase_times; const uint8_t* luts; } EpdWaveformPhases;
typedef struct { int id; } EpdWaveform;
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT,
                   EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
enum EpdDrawMode { MODE_DU = 1, MODE_GL16 = 5, MODE_PACKING_1PPB_DIFFERENCE = 0x100 };
enum EpdDrawError { EPD_DRAW_SUCCESS = 0, EPD_DRAW_NO_PHASES_AVAILABLE = 1,
                    EPD_DRAW_INVALID_CROP = 2, EPD_DRAW_OTHER_ERROR = 4 };
int epd_width(void);
int epd_height(void);
enum EpdRotation epd_get_rotation(void);
EpdRect epd_full_screen(void);
void epd_poweron(void);
void epd_build_1ppB_lut_1k(uint8_t* lut, const EpdWaveformPhases* phases, int frame);
EpdRect epd_difference_image_cropped(const uint8_t* to, const uint8_t* from, EpdRect area,
                                      uint8_t* difference, bool* dirty_lines, uint8_t* dirty_columns);
void epd_set_line_phase_luts(const uint8_t* const* phase_luts, const int8_t* line_phase);
void epd_set_col_phase_luts(const uint8_t* const* phase_luts, const int* x0, const int* x1,
                            const int8_t* phase, int nbands);
void epd_clear_phase_luts(void);
enum EpdDrawError epd_draw_base(EpdRect area, const uint8_t* data, EpdRect crop_to,
                                 enum EpdDrawMode mode, int temperature, const bool* drawn_lines,
                                 const uint8_t* drawn_columns, const EpdWaveform* waveform);
