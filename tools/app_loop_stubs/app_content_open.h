#pragma once
#include <stdbool.h>

static inline bool app_book_request_open(const char *path) { (void)path; return true; }
static inline bool app_image_request_open(const char *path) { (void)path; return true; }
static inline void app_files_request_folder(int folder) { (void)folder; }
static inline void app_book_request_manage(void) {}
static inline bool app_book_reader_body_visible(void) { return false; }

#include <stddef.h>
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen);

void app_book_cover_mode_changed(void);
void app_home_cover_mode_changed(void);
