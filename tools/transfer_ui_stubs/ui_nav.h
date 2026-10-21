#pragma once

#define UI_NAV_TOP 1096
#define UI_KEY_2 2

static inline void ui_nav_draw(uint8_t *fb, int active) { (void)fb; (void)active; }
static inline void ui_nav_status(uint8_t *fb) { (void)fb; }
static inline void ui_nav_back(uint8_t *fb, int x, int y) { (void)fb; (void)x; (void)y; }
static inline void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray) { (void)fb; (void)cx; (void)cy; (void)size; (void)gray; }
static int test_nav_index = -1;
static inline int ui_nav_hit(uint16_t x, uint16_t y) { return x < UI_LOCK_WIDTH && y >= UI_NAV_TOP && y < UI_LOCK_HEIGHT ? x * 4 / UI_LOCK_WIDTH : -1; }
static inline void ui_nav_request(app_ctx_t *ctx, int index) { (void)ctx; test_nav_index = index; }
