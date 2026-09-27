/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
const char* esp_err_to_name(int err);
typedef void* cst836u_handle_t;
typedef void* sc7a20h_handle_t;
#define CST836U_MAX_POINTS 2
typedef struct { bool active; uint16_t x,y; } cst836u_point_t;
typedef struct { bool touched; uint8_t count; uint16_t x,y; cst836u_point_t points[2]; uint8_t raw[15]; } cst836u_touch_t;
typedef struct { int unused; } cst836u_info_t;
int cst836u_read(void*, cst836u_touch_t*);
int cst836u_get_info(void*, cst836u_info_t*);
typedef struct { int unused; } EpdiyHighlevelState;
typedef struct { int x,y,width,height; } EpdRect;
enum EpdDrawError { EPD_DRAW_SUCCESS };
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
#define MODE_DU 1
#define MODE_GL16 2
#define MODE_GC16 3
extern int E0470_WAVEFORM;
bool continuous_du_init(void);
int64_t esp_timer_get_time(void);
void vTaskDelay(int);
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
void guard_draw_result(EpdiyHighlevelState*, enum EpdDrawError);
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*, const void*, int, EpdRect);
enum EpdDrawError update_display_full(EpdiyHighlevelState*);
enum EpdDrawError update_display_mode(EpdiyHighlevelState*, int);
enum EpdDrawError update_display_white(EpdiyHighlevelState*);
bool display_take_white_exit(void);
void rails_idle_check(int64_t);
bool read_pico_pmu_ready(void);
bool read_pico_pmu_take_key_short(void);
void enter_lock_and_sleep(EpdiyHighlevelState*, int64_t*, void*);
const char* app_settings_font_path(void);
bool ttf_font_path_is_builtin(const char*);
bool ttf_font_ready(void);
bool ttf_font_is_builtin(void);
const char* ttf_font_path(void);
int ttf_font_open(const char*);
int ttf_font_open_builtin(void);
typedef struct { bool mounted; } read_pico_sd_info_t;
int read_pico_sd_get_info(read_pico_sd_info_t*);
void read_pico_sd_start_probe(void);

int epd_rotated_display_width(void);
void epd_fill_triangle(int,int,int,int,int,int,uint8_t,uint8_t*);
