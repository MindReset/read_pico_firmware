#pragma once
#include <stdint.h>
typedef struct { uint8_t* front_fb; uint8_t* back_fb; uint8_t* difference_fb;
                 _Bool* dirty_lines; uint8_t* dirty_columns; } EpdiyHighlevelState;
