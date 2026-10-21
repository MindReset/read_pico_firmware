#pragma once
#include "transfer_ui_test_env.h"
static inline const char *app_settings_books_dir(void) {return "/sdcard/books";}
static inline const char *app_settings_fonts_dir(void) {return "/sdcard/fonts";}
static inline bool app_settings_set_books_dir(const char *path) {(void)path;return true;}
static inline bool app_settings_set_fonts_dir(const char *path) {(void)path;return true;}
static inline const char *app_settings_system_font_path(void) {return "";}
static inline const char *app_settings_font_path(void) {return "";}
static inline const char *app_settings_wallpaper_path(void) {return "";}
static inline void app_settings_set_system_font_path(const char *path) {(void)path;}
static inline void app_settings_set_font_path(const char *path) {(void)path;}
static inline void app_settings_set_wallpaper_path(const char *path) {(void)path;}
static inline void app_settings_set_lock_style(uint8_t style) {(void)style;}
static inline uint8_t app_settings_lock_style(void) {return 1;}
