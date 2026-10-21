#pragma once
#include <stdbool.h>

static inline bool app_font_activate_system(void) { return true; }
static inline bool app_font_activate_reading(void) { return true; }
static inline bool app_font_retry_active(void) {
    return ttf_font_is_builtin() && !ttf_font_path_is_builtin("saved") &&
           ttf_font_open("saved") == 0;
}
