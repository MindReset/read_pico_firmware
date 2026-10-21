/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实刷新调度，验证完整黑白、条带灰阶、固定底栏、内存及输出失败恢复。
 * English: Run the real presenter to verify complete black/white, gray bands, fixed navigation, allocation and output failure recovery.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "app_config.h"
#include "settings.h"
#include "ui/ui_nav_layout.h"
#include "ui/ui_image_dither.h"

#define W 1216
#define H 684
#define BYTES (W * H / 2)
const EpdWaveform E0470_WAVEFORM = {0}, E0470_FOLLOW_WAVEFORM = {1},
    E0470_FULL_WAVEFORM = {2}, E0470_GRAY8_WAVEFORM = {3};
static const EpdWaveform main_gray = {4}, main_exit = {5}, main_water = {6}, main_bw = {7}, main_acrylic = {8}, main_acrylic_clean = {9};
const EpdWaveform* display_reader_text_waveform(void) {return &E0470_WAVEFORM;}
static app_main_refresh_mode_t main_mode=APP_MAIN_REFRESH_FAST;
app_main_refresh_mode_t app_settings_main_refresh_mode(void) {return main_mode;}
static int exit_calls;
bool app_settings_main_fast_refresh(void) {return main_mode==APP_MAIN_REFRESH_FAST;}
const EpdWaveform* display_main_shelf_exit_waveform(void) {return &main_exit;}
const EpdWaveform* display_main_gray_waveform(void) {return &main_gray;}
const EpdWaveform* display_main_water_waveform(void) {return &main_water;}
const EpdWaveform* display_main_normal_waveform(void) { return display_main_water_waveform(); }
const EpdWaveform* display_main_bw_waveform(void) {return &main_bw;}
const EpdWaveform* display_main_acrylic_waveform(void) {return &main_acrylic;}
const EpdWaveform* display_main_acrylic_clean_waveform(void) {return &main_acrylic_clean;}
static uint8_t panel[BYTES], front[BYTES], back[BYTES], final[BYTES];
static EpdiyHighlevelState hl = {.front_fb = front, .back_fb = back, .waveform = &E0470_WAVEFORM};
static int64_t now_us;
static int calls, bw_calls, gray_calls, gc_calls, full_calls, clears, scan, prefill;
static int failures;
static int fail_after;
static enum EpdDrawError fail_error;
static int allocations, frees, complete_bw_calls;
static size_t allocated_bytes;
static bool alloc_fail;
static const EpdRect acrylic[] = {{0,360,H,90},{0,642,H,90},{0,924,H,90}};
static EpdRect last_area;
static enum EpdDrawMode last_mode;
static const EpdWaveform *last_wave;
static char owner_a, owner_b;
static enum EpdRotation rotation = EPD_ROT_INVERTED_PORTRAIT;
static int nav_draws, water_calls, water_fails;
static EpdRect last_water_area;
static int clean_calls;
// 所有真实推屏和欠载恢复都必须持有 OTA 互斥锁，包括递归兜底。
// Every physical scan and underrun recovery must hold OTA exclusion, including recursive fallback.
static unsigned ota_display_depth;
bool pico_online_display_begin(void) { ++ota_display_depth; return true; }
void pico_online_display_end(bool held) { assert(held && ota_display_depth); --ota_display_depth; }
void e0470_page_turn_release(void) {}

void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps) {
    assert(alignment == 16 && caps == 3);
    ++allocations; allocated_bytes = bytes;
    return alloc_fail ? NULL : malloc(bytes);
}
void heap_caps_free(void* allocation) {assert(allocation); ++frees; free(allocation);}
int epd_width(void) {return W;}
int epd_height(void) {return H;}
enum EpdRotation epd_get_rotation(void) {return rotation;}
int epd_rotated_display_width(void) {return H;}
int epd_rotated_display_height(void) {return W;}
int64_t esp_timer_get_time(void) {return now_us;}
// gen19 双档策略：会话 idle 恢复默认档(18)，推送会话活跃或真欠载回落安全档(12)。/ gen19 dual-clock policy: idle restores the default, live push sessions or true underruns fall back to safe.
void read_pico_epd_set_pclk(int mhz) {assert(mhz == DISPLAY_PCLK_SAFE_MHZ || mhz == DISPLAY_PCLK_DEFAULT_MHZ);}
void read_pico_epd_use_scan(read_pico_epd_scan_t mode) {scan = mode;}
void epd_lcd_set_prefill_lines(int lines) {prefill = lines;}
void epd_poweron(void) {}
bool read_pico_epd_release_drive(void) {return true;}
void epd_poweroff(void) {}
void epd_clear(void) { assert(ota_display_depth);++clears; memset(panel, 255, BYTES);}
void epd_hl_set_all_white(EpdiyHighlevelState* state) {memset(state->front_fb, 255, BYTES);}
void epd_hl_waveform(EpdiyHighlevelState* state, const EpdWaveform* waveform) {state->waveform = waveform;}
enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* state, EpdRect area, e0470_turn_dir_t dir) {
    assert(ota_display_depth);
    (void)dir; ++water_calls;
    last_water_area=area;
    assert(area.y>=0 && area.y%32==0 && area.y+area.height==UI_NAV_REFRESH_END && scan==READ_PICO_EPD_SCAN_FAST);
    if(water_fails)return (enum EpdDrawError)water_fails;
    const size_t stride=W/2;
    for(int y=0;y<H;++y){memcpy(panel+y*stride+area.y/2,state->front_fb+y*stride+area.y/2,area.height/2);memcpy(state->back_fb+y*stride+area.y/2,state->front_fb+y*stride+area.y/2,area.height/2);}
    return EPD_DRAW_SUCCESS;
}
static int turn_tick_us=21000;
void e0470_page_turn_set_tick_us(int us){turn_tick_us=us;}
int e0470_page_turn_tick_us(void){return turn_tick_us;}
enum EpdDrawError e0470_page_turn_with_waveform_compact(EpdiyHighlevelState* state, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned bands) {
    assert(ota_display_depth);
    assert(waveform==&main_water && bands==E0470_TURN_FAST_BANDS && turn_tick_us==E0470_TURN_FAST_TICK_US);
    return e0470_page_turn(state,area,dir);
}

// panel 独立于 back；失败只改变物理屏，绝不假装回写已经成功。
// Keep panel independent from back; failed output changes only the panel, never a falsely successful baseline.
static enum EpdDrawError draw(EpdiyHighlevelState* state, enum EpdDrawMode mode, bool full, const EpdRect* area) {
    assert(ota_display_depth);
    assert(!memcmp(panel, state->back_fb, BYTES));
    // 模拟 highlevel 的倒置竖屏及32列对齐，包含相邻半字节与固定底栏边界。
    // Model highlevel's inverted portrait and 32-column expansion, including neighboring nibbles and fixed navigation.
    const int x0 = area ? area->y & ~31 : 0;
    const int x1 = area ? (area->y + area->height + 31) & ~31 : W;
    assert(x0 >= 0 && x1 <= W);
    bool changed = full;
    for (int y = 0; y < H && !changed; ++y) {
        size_t offset = (size_t)y * W / 2 + x0 / 2;
        changed = memcmp(state->front_fb + offset, state->back_fb + offset, (x1 - x0) / 2) != 0;
    }
    if (!changed) return EPD_DRAW_SUCCESS;
    ++calls; full_calls += full;
    bool nav_driven = full && x1 > UI_NAV_REFRESH_END;
    for (int y = 0; y < H; ++y) for (int x = UI_NAV_REFRESH_END; x < x1; ++x) {
        const size_t at = (size_t)y * W / 2 + x / 2;
        const unsigned shift = (x & 1) ? 4 : 0;
        const bool delta = ((state->front_fb[at] ^ state->back_fb[at]) >> shift) & 15;
        nav_driven |= delta;
        // 合并快刷只能驱动内容区以外的五行横条；真实波形另验证等灰完全保持。
        // Fused BW scans may drive only the five marker rows beyond the body.
        // Actual waveform tests separately prove every equal pair has zero drive.
        if (!full && (state->waveform == &main_bw || state->waveform == &main_exit))
            assert(!delta || (x >= UI_NAV_MARKER_TOP && x < UI_NAV_MARKER_BOTTOM));
    }
    nav_draws += nav_driven;
    last_area = area ? *area : (EpdRect){0, 0, H, W};
    last_mode = mode; last_wave=state->waveform;
    if (mode == MODE_DU) {
        ++bw_calls;
        if (state->waveform == &E0470_FOLLOW_WAVEFORM) assert(scan == READ_PICO_EPD_SCAN_FAST);
        else {
            assert((state->waveform == &main_bw || state->waveform == &main_exit || state->waveform == &E0470_WAVEFORM) && scan == READ_PICO_EPD_SCAN_FULL);
            ++complete_bw_calls; exit_calls += state->waveform == &main_exit;
            for (int y = 0; y < H; ++y)
                for (int x = x0 / 2; x < x1 / 2; ++x) {
                    uint8_t v = state->front_fb[(size_t)y * W / 2 + x];
                    uint8_t old=state->back_fb[(size_t)y*W/2+x];
                    assert((v & 15) == 0 || (v & 15) == 15 || (v & 15)==(old & 15));
                    assert((v >> 4) == 0 || (v >> 4) == 15 || (v >> 4)==(old >> 4));
                    if(state->waveform==&main_bw || state->waveform==&main_exit)
                        for(unsigned b=0;b<3;++b)if(x>=acrylic[b].y/2 && x<(acrylic[b].y+acrylic[b].height)/2 &&
                            state->front_fb[(size_t)y*W/2+x]!=final[(size_t)y*W/2+x])
                            assert(v==old);
                }
        }
    } else {
        ++gray_calls; gc_calls += mode == MODE_GC16;
        assert(scan == READ_PICO_EPD_SCAN_FULL);
        if (state->waveform==&main_acrylic_clean) {
            ++clean_calls;
            assert(full && mode==MODE_GL16 && x1<=UI_NAV_REFRESH_END);
        }
    }
    const bool fail = failures > 0 && fail_after == 0;
    if (fail_after > 0) --fail_after;
    if (fail) --failures;
    for (int y = 0; y < (fail ? H / 2 : H); ++y) {
        size_t offset = (size_t)y * W / 2 + x0 / 2;
        size_t bytes = (size_t)(x1 - x0) / 2;
        memcpy(panel + offset, state->front_fb + offset, bytes);
        if (!fail) memcpy(state->back_fb + offset, state->front_fb + offset, bytes);
    }
    return fail ? fail_error : EPD_DRAW_SUCCESS;
}
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature) {
    assert(temperature == 25); return draw(state, mode, false, NULL);
}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature) {
    assert(temperature == 25); return draw(state, mode, true, NULL);
}
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area) {
    assert(temperature == 25); return draw(state, mode, false, &area);
}
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area) {
    assert(temperature == 25); return draw(state, mode, true, &area);
}
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature) {
    memset(state->back_fb, 255, BYTES);
    return epd_hl_update_screen_full(state, mode, temperature);
}
static void reset(void) {
    display_main_transition_cancel(); main_mode=APP_MAIN_REFRESH_FAST; exit_calls=water_calls=water_fails=0; failures = fail_after = 0;
    allocations = frees = complete_bw_calls = 0; alloc_fail = false; now_us = 1000000;
    rotation = EPD_ROT_INVERTED_PORTRAIT;
    memset(front, 255, BYTES); memset(back, 255, BYTES); memset(panel, 255, BYTES);
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    calls = bw_calls = gray_calls = gc_calls = full_calls = clears = nav_draws = clean_calls = 0;
}
static void target(unsigned seed) {
    for (size_t i = 0; i < BYTES; ++i) {
        // 两个半字节分别混合黑白；灰阶条带另行覆盖全部16级。
        // Mix both black/white nibbles independently; separate band cases cover all 16 gray levels.
        static const unsigned char values[] = {0x00, 0xff, 0x0f, 0xf0};
        int x = (int)(i % (W / 2)) * 2;
        final[i] = x < UI_NAV_REFRESH_END ? values[(i + seed) % sizeof(values)] : back[i];
    }
    memcpy(front, final, BYTES);
}
static void begin(void* owner) {
    display_main_transition_arm(owner, false, false);
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    display_main_transition_disarm();
    assert(!memcmp(front, final, BYTES));
}
static void assert_final(void) {
    assert(!memcmp(panel, final, BYTES) && !memcmp(back, final, BYTES));
    assert(!memcmp(front, final, BYTES));
}
// 按像素生成预期固定底栏，独立核对半字节边界及灰阶前态。
// Build expected navigation pixel by pixel to independently check nibble edges and the actual gray baseline.
static void expect_navigation(bool marker) {
    for (int y = 0; y < H; ++y) for (int x = UI_NAV_REFRESH_END; x < W; ++x) {
        if (marker && x >= UI_NAV_MARKER_TOP && x < UI_NAV_MARKER_BOTTOM) continue;
        const size_t at = (size_t)y * W / 2 + x / 2;
        const unsigned mask = 15u << ((x & 1) ? 4 : 0);
        final[at] = (final[at] & ~mask) | (back[at] & mask);
    }
}

static void gray_target(unsigned seed) {
    target(seed);
    for (unsigned b = 0; b < 3; ++b)
        for (int y = 0; y < H; ++y)
            for (int x = acrylic[b].y / 2; x < (acrylic[b].y + acrylic[b].height) / 2; ++x)
                front[(size_t)y * W / 2 + x] = (uint8_t)(((x + y + seed) % 16) |
                                                        (((x + y + seed + 7) % 16) << 4));
    memcpy(final, front, BYTES);
}

static void gray_begin_for(bool changing_page) {
    display_main_transition_arm(&owner_a, false, changing_page);
    display_main_transition_gray_bands(acrylic, 3);
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert_final();
}
static void gray_begin(void) {gray_begin_for(false);}

int main(void) {
    // 所有256种双半字节都走真实批量规整；不同字节并排验证乘法不会串位，底栏保持原像素。
    // Exercise all 256 nibble pairs through real batch normalization, checking adjacent bytes for carries and preserving navigation.
    reset(); memcpy(final,back,BYTES);
    for (int y=0;y<H;++y) for (int x=0;x<UI_NAV_REFRESH_END/2;++x) {
        size_t at=(size_t)y*W/2+x;unsigned value=(unsigned)(x+y*17)&255u;
        front[at]=(uint8_t)value;
        final[at]=(uint8_t)(((value&15)>=8?15:0)|((value>>4)>=8?240:0));
    }
    begin(&owner_a);assert_final();
    assert(!allocations&&!gray_calls&&complete_bw_calls==1);
    // 黑白内容只推完整DU，连续切换不触发周期全像素清理。
    // Black/white content uses only complete DU without periodic full-pixel cleanup during tab switching.
    reset();
    for (unsigned i = 0; i < 200; ++i) {
        target(i); begin(i & 1 ? &owner_a : &owner_b); assert_final();
        assert(calls == (int)i + 1 && bw_calls == calls && last_mode == MODE_DU);
        assert(complete_bw_calls == calls);
        assert(last_area.y == 0 && last_area.height == UI_NAV_MARKER_SCAN_END);
        assert(last_area.height % 32 == 0 && last_area.height <= UI_NAV_MARKER_SCAN_END);
        assert(!gray_calls && !full_calls && !gc_calls && !clears && !nav_draws && !allocations);
        assert(hl.waveform == &E0470_WAVEFORM && scan == READ_PICO_EPD_SCAN_FULL);
    }
    now_us += 9000000; rails_idle_check(now_us / 1000);
    display_main_transition_cancel(); assert(calls == 200); assert_final();
    begin(&owner_a); assert(calls == 200); assert_final();

    // 已显示灰阶挡板转细点阵时，只推一轮黑白，取消灰区暂存和补刷；分页也相同。
    // Converting displayed gray guards to fine dots uses one BW pass without band staging/completion; paging follows the same route.
    reset(); gray_target(3); gray_begin();
    for (unsigned b = 0; b < 3; ++b)
        for (int y = 0; y < H; ++y)
            for (int x = acrylic[b].y / 2; x < (acrylic[b].y + acrylic[b].height) / 2; ++x) {
                const size_t offset = (size_t)y * W / 2 + x;
                const uint8_t gray = front[offset];
                const unsigned lo = ui_image_dither_acrylic_bw((gray & 15) * 17u, H - y - 1, x * 2) >> 4;
                const unsigned hi = ui_image_dither_acrylic_bw((gray >> 4) * 17u, H - y - 1, x * 2 + 1) >> 4;
                front[offset] = (uint8_t)(lo | (hi << 4));
            }
    memcpy(final, front, BYTES);
    const int old_bw = bw_calls, old_gray = gray_calls, old_allocs = allocations;
    begin(&owner_a); assert_final();
    assert(bw_calls == old_bw + 1 && gray_calls == old_gray && allocations == old_allocs);
    target(11);
    display_main_transition_arm(&owner_a, false, false);
    display_main_transition_shelf_page();
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,196,H,892}) == EPD_DRAW_SUCCESS);
    assert_final();
    assert(bw_calls == old_bw + 2 && gray_calls == old_gray && allocations == old_allocs);
    assert(last_mode == MODE_DU && hl.waveform == &E0470_WAVEFORM && !nav_draws);

    // 横条以外任意重绘差异不能驱动底栏；奇数起点同字节的分隔线也必须保留。
    // Repaint differences outside the marker cannot drive navigation; retain the divider nibble sharing the marker's odd starting byte.
    reset();
    memset(front, 0xee, BYTES); assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    calls = bw_calls = gray_calls = full_calls = gc_calls = nav_draws = 0;
    target(1);
    for (int y = 0; y < H; ++y) {
        size_t offset = (size_t)y * W / 2 + UI_NAV_REFRESH_END / 2;
        memset(front + offset, 0, (W - UI_NAV_REFRESH_END) / 2);
        const size_t row = (size_t)y * W / 2;
        for (int x = UI_NAV_MARKER_TOP; x < UI_NAV_MARKER_BOTTOM; ++x) {
            const size_t at = row + x / 2;
            const unsigned shift = (x & 1) ? 4 : 0;
            front[at] = (front[at] & ~(15u << shift)) | (back[at] & (15u << shift));
        }
    }

    begin(&owner_a); assert_final();
    assert(calls == 1 && !nav_draws && !full_calls && !gc_calls);
    for (int y = 0; y < H; ++y) {
        for (int x = UI_NAV_REFRESH_END / 2; x < W / 2; ++x)
            assert(panel[y * W / 2 + x] == 0xee);
    }

    // 真实四个横条位置循环移动，旧条擦白新条压黑，其他底栏字节不变；无内容变化时只刷横条。
    // Move between all four real marker positions, erasing the old and drawing the new while retaining all other nav bytes; marker-only changes skip content output.
    for (int ordinary = 0; ordinary < 2; ++ordinary) {
    reset(); main_mode = ordinary ? APP_MAIN_REFRESH_NORMAL : APP_MAIN_REFRESH_FAST;
    for (int step = 0; step < 12; ++step) {
        if (step) memcpy(front, back, BYTES);
        else target(2);
        const int center = ((step % 4) * 2 + 1) * H / 8;
        for (int y = 0; y < H; ++y) {
            for (int x = UI_NAV_MARKER_TOP; x < UI_NAV_MARKER_BOTTOM; ++x) {
                const size_t at = (size_t)y * W / 2 + x / 2;
                const unsigned shift = (x & 1) ? 4 : 0;
                const unsigned value = y >= H - center - 28 && y < H - center + 28 ? 0 : 15;
                front[at] = (front[at] & ~(15u << shift)) | (value << shift);
            }
        }
        memcpy(final, front, BYTES);
        const int old_calls = calls;
        begin(&owner_a); assert_final();
        assert(calls == old_calls + (step == 0 && ordinary ? 2 : 1));
        if (step == 0 && !ordinary)
            assert(last_area.y == 0 && last_area.height == UI_NAV_MARKER_SCAN_END);
        else assert(last_area.y == UI_NAV_MARKER_TOP && last_area.height == 5);
        assert(last_mode == MODE_DU && nav_draws == step + 1);
        assert(UI_NAV_MARKER_SCAN_END == 1120 && !full_calls && !gc_calls && !clears);
        for (int y = 0; y < H; ++y) {
            const size_t row = (size_t)y * W / 2;
            assert((panel[row + UI_NAV_MARKER_TOP / 2] & 15) == 15);
            for (int x = UI_NAV_MARKER_BOTTOM / 2; x < W / 2; ++x) assert(panel[row + x] == 255);
        }
    }
    assert(bw_calls == 12 && gray_calls == ordinary && !allocations);
    int unchanged = calls; begin(&owner_b); assert(calls == unchanged); assert_final();
    }
    reset();

    // 后台完成与主页面滚动的局部范围不得因32列对齐越过固定边界。
    // Local background completion and main-page scrolling must not cross the frozen boundary after alignment.
    target(4); begin(&owner_a); int before = calls;
    front[UI_NAV_REFRESH_END / 2 - 1] = final[UI_NAV_REFRESH_END / 2 - 1] = 0x00;
    front[UI_NAV_MARKER_BOTTOM / 2] = 0;
    display_main_transition_arm(&owner_a, false, false);
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0, UI_NAV_REFRESH_END - 11, H, 24}) == EPD_DRAW_SUCCESS);
    assert(last_area.y + last_area.height == UI_NAV_REFRESH_END && calls == before + 1 && !nav_draws);
    assert_final();
    display_main_transition_arm(&owner_a, false, false);
    front[UI_NAV_MARKER_BOTTOM / 2] = 0;
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0, UI_NAV_TOP, H, 24}) == EPD_DRAW_SUCCESS);
    assert(calls == before + 1 && !nav_draws); assert_final();

    // 主页面计数独立：不会积累或消耗其他页面的原有清理次数。
    // Main tabs neither accrue nor consume the original cleanup cadence for other pages.
    reset();
    for (int i = 1; i < APP_UI_FAST_GC16_EVERY; ++i) {
        target((unsigned)i); assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    }
    assert(!gc_calls); int old_full = full_calls;
    for (unsigned i = 0; i < 40; ++i) {target(i); begin(&owner_a); assert_final();}
    assert(!gc_calls && full_calls == old_full);
    target(100); assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(gc_calls == 1 && full_calls == old_full + 1); assert_final();

    // 其他旋转、取消未用许可保持原来整页出口。
    // Other rotations and cancelled permission retain the existing whole-page presenter.
    reset(); rotation = EPD_ROT_LANDSCAPE; target(3); begin(&owner_a);
    assert(last_area.height == W && !bw_calls); assert_final();
    reset(); target(3); display_main_transition_arm(&owner_a, false, false); display_main_transition_cancel();
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS && last_area.height == W); assert_final();

    // 主页面结束后的局部 DU、灰阶差分和强制局部整理保持原路径，不追加刷新。
    // Later local DU, gray difference and forced local cleanup retain their paths without extra output.
    for (int kind = 0; kind < 3; ++kind) {
        reset(); target(0); begin(&owner_a); front[7] = final[7] = 0x30;
        EpdRect area = {0, 1, H, 1};
        enum EpdDrawError result = kind == 0 ? update_display_area_diff_with(&hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area)
            : kind == 1 ? update_display_area_with(&hl, &E0470_WAVEFORM, MODE_GL16, area)
                        : update_display_area_full_with(&hl, &E0470_WAVEFORM, MODE_GL16, area);
        assert(result == EPD_DRAW_SUCCESS && calls == 2); assert_final();
        assert(bw_calls == 1 + (kind == 0) && full_calls == (kind != 0));
    }

    // 显式全刷、图片、从白底和锁屏相关出口保持完整目标；取消没有回放任务。
    // Explicit full, image, from-white and lock-related outputs retain their whole targets; cancel never replays a deferred frame.
    for (int view = 0; view < 4; ++view) {
        reset(); target(0); begin(&owner_a); target(6);
        if (view == 2) {bool held=pico_online_display_begin();epd_clear();pico_online_display_end(held);} // from-white 需要物理白底。/ From-white requires a physically white baseline.
        enum EpdDrawError result = view == 0 ? update_display_full(&hl)
            : view == 1 ? update_display_image_gray(&hl)
            : view == 2 ? update_display_from_white_with(&hl, &E0470_WAVEFORM, MODE_GL16)
                        : update_display_white(&hl);
        assert(result == EPD_DRAW_SUCCESS);
        memcpy(final, front, BYTES); assert_final();
        int count = calls; display_main_transition_cancel(); assert(calls == count);
    }

    // 失败从白底恢复最终图；恢复也失败时，下次局部输出先重建实际基准。
    // Recover failures from white to the final target; a failed recovery rebuilds the actual baseline before later local output.
    for (int error = 1; error <= 2; ++error) {
        reset(); target(0); fail_error = error == 1 ? EPD_DRAW_EMPTY_LINE_QUEUE : EPD_DRAW_OTHER_ERROR;
        failures = 1; begin(&owner_a); assert_final();
        assert(clears == 1 && calls == 2 && gc_calls == 1);
    }
    reset(); target(0); failures = 2; fail_error = EPD_DRAW_OTHER_ERROR;
    display_main_transition_arm(&owner_a, false, false);
    assert(update_display_fast_page(&hl) == EPD_DRAW_OTHER_ERROR);
    assert(memcmp(panel, front, BYTES));
    front[20] = final[20] = 0x22;
    assert(update_display_area_diff_with(&hl, &E0470_FOLLOW_WAVEFORM, MODE_DU,
                                       (EpdRect){0, 2, H, 1}) == EPD_DRAW_SUCCESS);
    assert_final(); assert(clears == 2 && gc_calls == 2 && bw_calls == 1);

    // 内容成功后横条失败，重建最新完整目标；不得继续显示旧横条或半张新页。
    // If the marker fails after successful content, rebuild the latest complete target rather than retaining an old marker or partial new page.
    reset(); main_mode=APP_MAIN_REFRESH_NORMAL; target(5);
    for (int y = 0; y < H; ++y) front[(size_t)y * W / 2 + 550] = 0;
    memcpy(final, front, BYTES);
    failures = 1; fail_after = 1; fail_error = EPD_DRAW_OTHER_ERROR;
    begin(&owner_a); assert_final();
    assert(calls == 3 && clears == 1 && bw_calls == 1 && gc_calls == 1);

    // 灰阶只缓存三条带：首遍保留旧亚克力，第二遍单向提交新UI灰阶；重画同图不做往返驱动。
    // Cache three gray bands: the first pass preserves old acrylic and the second commits directional UI grays; identical repaint does not round-trip pixels.
    reset(); gray_target(1); gray_begin();
    assert(calls == 2 && complete_bw_calls == 1 && gray_calls == 1 && !full_calls && !nav_draws);
    assert(allocations == 1 && frees == 1 && allocated_bytes == 92340);
    assert(last_mode == MODE_GL16 && last_area.y == 360 && last_area.height == 654);
    int gray_unchanged = calls; gray_begin();
    assert(calls == gray_unchanged && allocations == 1 && frees == 1);
    for (unsigned i = 2; i < 20; ++i) {gray_target(i); gray_begin();}
    assert(calls == 38 && complete_bw_calls == 19 && gray_calls == 19 && frees == 19);
    assert(!clears && !gc_calls && !full_calls && !nav_draws);

    // 只有亚克力变化时，黑白首帧完全跳过，不做一次多余的内容输出。
    // Skip the BW pass entirely when only acrylic changes, avoiding a redundant content update.
    reset();
    for(unsigned b=0;b<3;++b)for(int y=0;y<H;++y)
        memset(front+(size_t)y*W/2+acrylic[b].y/2,0x88,(size_t)acrylic[b].height/2);
    memcpy(final,front,BYTES);gray_begin();assert_final();
    assert(calls==1 && gray_calls==1 && !bw_calls && !clears && !gc_calls && allocations==frees);

    // PSRAM不足时一次单向提交最终UI灰阶与完整黑白端点，不丢图、不留异步任务。
    // With insufficient PSRAM submit directional UI grays and complete monochrome endpoints in one pass, without losing the target or queuing stale work.
    reset(); gray_target(2); alloc_fail = true; gray_begin();
    assert(calls == 1 && !complete_bw_calls && gray_calls == 1 && allocations == 1 && !frees);
    assert(last_area.y == 0 && last_area.height == UI_NAV_REFRESH_END && !nav_draws);
    int oom_calls = calls; display_main_transition_cancel(); assert(calls == oom_calls);

    // 黑白遍、灰阶遍、横条遍分别失败均从最新最终图恢复；缓存不泄漏。
    // Fail black/white, gray and marker passes separately; each recovers the latest final target without leaking the cache.
    for (int stage = 0; stage < 3; ++stage) {
        reset(); gray_target(3);
        for (int y = 0; y < H; ++y) front[(size_t)y * W / 2 + 550] = 0;
        memcpy(final, front, BYTES);
        failures = 1; fail_after = stage; fail_error = EPD_DRAW_OTHER_ERROR;
        gray_begin();
        assert(clears == 1 && gc_calls == 1 && calls == stage + 2);
        assert(allocations == 1 && frees == 1);
        int completed = calls; display_main_transition_cancel(); assert(calls == completed);
    }

    // 同页灰阶区域仍走完整灰阶表；其余区域用完整DU，局部出口不会越界到底栏。
    // Same-page gray regions retain the complete gray table; other areas use complete DU without crossing the navigation boundary.
    reset(); gray_target(4); gray_begin();
    front[181] = final[181] = 0x33;
    display_main_transition_arm(&owner_a, false, false); display_main_transition_gray_bands(acrylic, 3);
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,360,H,8}) == EPD_DRAW_SUCCESS);
    assert_final(); assert(last_mode == MODE_GL16 && !nav_draws);

    // 无效条带及取消许可不能泄漏到下一页；包括奇数、重叠和可能溢出的输入。
    // Invalid bands and cancelled permission cannot leak into another page, including odd, overlapping and potentially overflowing input.
    const EpdRect invalid[] = {{0,361,H,90},{1,360,H,90},{0,1000,H,90},
                               {0,360,H,2147483646},{0,-2,H,90}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset(); target(1);
        display_main_transition_arm(&owner_a, false, false); display_main_transition_gray_bands(&invalid[i], 1);
        assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS); assert_final();
        assert(calls == 1 && complete_bw_calls == 1 && !allocations);
    }
    reset(); target(2);
    const EpdRect overlap[] = {{0,360,H,90},{0,420,H,90}};
    display_main_transition_arm(&owner_a, false, false); display_main_transition_gray_bands(overlap, 2);
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS); assert_final(); assert(!allocations);
    reset(); gray_target(5);
    display_main_transition_arm(&owner_a, false, false); display_main_transition_gray_bands(acrylic, 3);
    display_main_transition_cancel();
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS); assert_final(); assert(!allocations);

    // 灰阶迁移仍采用校准GL16；空书位采用加强擦白而非整屏GC16。
    // Retain calibrated GL16 and strengthen empty-slot whitening without whole-screen GC16.
    reset(); gray_target(7); gray_begin_for(true);
    assert(calls==2 && !clean_calls && !full_calls && gray_calls==1 && !gc_calls && !clears && !nav_draws);
    assert(allocations==frees && allocated_bytes==92340);
    int entry_calls=calls; gray_begin_for(true); assert(calls==entry_calls);
    reset(); target(3); begin(&owner_a); target(4);
    display_main_transition_arm(&owner_a,false,false);display_main_transition_shelf_page();
    assert(update_display_area_diff_with(&hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){0,196,H,892})==EPD_DRAW_SUCCESS);
    assert_final();assert(exit_calls==1 && !gc_calls && !clears && !nav_draws);

    // 抗锯齿灰像素只二值化主内容，灰色底栏的真实前态和目标横条仍保持。
    // Antialiased grays are binarized only in main content; the actual gray navigation baseline and target marker are preserved.
    reset(); memset(front, 0x7e, BYTES);
    assert(update_display_full(&hl) == EPD_DRAW_SUCCESS);
    memcpy(final, front, BYTES);
    for (int y = 0; y < H; ++y)
        memset(final + (size_t)y * W / 2, 0x0f, UI_NAV_REFRESH_END / 2);
    begin(&owner_a); assert_final();
    // 普通模式一次输出柔和灰阶，不运行水波纹调度或黑白预刷。
    // Ordinary uses one soft grayscale pass without ripple scheduling or a monochrome prepass.
    reset(); main_mode=APP_MAIN_REFRESH_NORMAL; memset(front,0x73,BYTES); memcpy(final,front,BYTES);
    expect_navigation(true);
    display_main_transition_arm(&owner_a,true,false);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS); assert_final();
    assert(bw_calls==1 && !complete_bw_calls && !exit_calls && !allocations && gray_calls==1 && nav_draws==1);
    // 普通灰阶周期清理、局部滚动与后台封面均止于1088，重复目标不清屏，手动全刷仍含底栏。
    // Ordinary gray cleanup, scrolling and background covers stop at 1088; identical targets skip cleanup and manual full output still includes nav.
    reset(); main_mode=APP_MAIN_REFRESH_NORMAL; memset(front,0x73,BYTES);
    assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);
    calls=gray_calls=gc_calls=full_calls=nav_draws=0;
    for (int step=1;step<=24;++step) {
        gray_target((unsigned)step);begin(&owner_a);assert_final();
        assert(hl.waveform==&E0470_WAVEFORM && !bw_calls && !allocations && !nav_draws);
        assert(last_area.y==0 && last_area.height==UI_NAV_REFRESH_END);
        assert(gray_calls==step && full_calls==step/APP_UI_FAST_GC16_EVERY && !water_calls);
        assert(gc_calls==step/APP_UI_FAST_GC16_EVERY);
        assert(last_wave==(step%APP_UI_FAST_GC16_EVERY ? &main_water : &E0470_WAVEFORM));
        assert(last_mode==(step%APP_UI_FAST_GC16_EVERY ? MODE_GL16 : MODE_GC16));
        for(int y=0;y<H;++y)for(int x=UI_NAV_REFRESH_END/2;x<W/2;++x)assert(back[y*(W/2)+x]==0x73);
    }
    int normal_before=calls;begin(&owner_a);assert(calls==normal_before);
    front[UI_NAV_REFRESH_END/2-1]=final[UI_NAV_REFRESH_END/2-1]=0x28;
    front[UI_NAV_MARKER_BOTTOM/2]=0;
    display_main_transition_arm(&owner_a,false,false);
    assert(update_display_area_diff_with(&hl,&E0470_WAVEFORM,MODE_GL16,
        (EpdRect){0,UI_NAV_REFRESH_END-11,H,24})==EPD_DRAW_SUCCESS);
    assert_final();assert(calls==normal_before+1 && !nav_draws && last_mode==MODE_GL16);
    assert(last_area.y+last_area.height==UI_NAV_REFRESH_END);
    display_main_transition_arm(&owner_a,false,false);
    assert(update_display_area_with(&hl,&E0470_WAVEFORM,MODE_GL16,
        (EpdRect){0,UI_NAV_REFRESH_END-11,H,24})==EPD_DRAW_SUCCESS);
    assert_final();assert(full_calls==2 && !nav_draws && last_wave==&main_water && last_mode==MODE_GL16);
    display_main_transition_arm(&owner_a,false,false);
    assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);assert_final();assert(nav_draws==1);
    // 普通柔和输出在内容或横条扫描失败时从白底重建，绝不提交半帧。
    // Soft ordinary output rebuilds from white after a body or marker scan failure instead of committing an intermediate frame.
    for(int stage=0;stage<2;++stage){
        reset();main_mode=APP_MAIN_REFRESH_NORMAL;gray_target(3);front[550]=0;memcpy(final,front,BYTES);
        failures=1;fail_after=stage;fail_error=EPD_DRAW_OTHER_ERROR;
        display_main_transition_arm(&owner_a,false,true);
        assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
        assert(clears==1&&gc_calls==1&&!allocations&&!water_calls&&hl.waveform==&E0470_WAVEFORM);
    }
    // 普通亚克力题头和内容同次扫描，不能拆成两轮；32列边界不扫入底栏。
    // Ordinary acrylic header and content share one scan; aligned bounds never drive navigation.
    for(int stage=-1;stage<1;++stage){
        reset();main_mode=APP_MAIN_REFRESH_NORMAL;gray_target(3);
        if(stage>=0){failures=1;fail_after=stage;fail_error=EPD_DRAW_OTHER_ERROR;}
        display_main_transition_arm(&owner_a,false,true);display_main_transition_gray_bands(acrylic,3);
        assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
        if(stage<0){assert(!clears&&!gc_calls&&full_calls==1&&calls==1&&!nav_draws&&gray_calls==1);}
        else assert(clears==1&&gc_calls==1);
    }
    reset(); target(2); display_main_transition_arm(&owner_a,true,false);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS); assert_final();
    assert(exit_calls==1 && calls==1 && !allocations && !nav_draws);
    reset(); target(2); display_main_transition_arm(&owner_a,true,false); display_main_transition_cancel();
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS); assert_final(); assert(!exit_calls);
    reset(); gray_target(2); display_main_transition_arm(&owner_a,true,false); display_main_transition_gray_bands(acrylic,3);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS); assert_final();
    assert(exit_calls==1 && gray_calls==1 && allocations==frees && !nav_draws);
    reset(); target(2); display_main_transition_arm(&owner_a,true,false); main_mode=APP_MAIN_REFRESH_NORMAL;
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS); assert_final(); assert(!exit_calls && !bw_calls);
    reset(); main_mode=APP_MAIN_REFRESH_WATER; memset(front,0x73,BYTES); memcpy(final,front,BYTES);
    for(int y=0;y<H;++y)memcpy(final+y*(W/2)+UI_NAV_REFRESH_END/2,back+y*(W/2)+UI_NAV_REFRESH_END/2,(W-UI_NAV_REFRESH_END)/2);
    memcpy(front,final,BYTES);display_main_transition_arm(&owner_a,false,true);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
    assert(water_calls==1 && !calls && !allocations && !bw_calls && turn_tick_us==21000);
    // Entering the shelf: one ripple includes the header and content, stopping before fixed navigation.
    reset();main_mode=APP_MAIN_REFRESH_WATER;gray_target(2);
    display_main_transition_arm(&owner_a,false,true);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
    assert(water_calls==1&&!gray_calls&&last_water_area.y==0&&last_water_area.height==UI_NAV_REFRESH_END);
    assert(!clears&&!allocations&&!bw_calls&&!nav_draws);
    reset();main_mode=APP_MAIN_REFRESH_WATER;gray_target(2);water_fails=EPD_DRAW_OTHER_ERROR;
    display_main_transition_arm(&owner_a,false,true);assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
    assert(water_calls==1 && gc_calls==1 && clears==1);
    reset();main_mode=APP_MAIN_REFRESH_WATER;gray_target(2);water_fails=EPD_DRAW_NO_PHASES_AVAILABLE;
    display_main_transition_arm(&owner_a,false,true);assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();
    assert(water_calls==1 && gray_calls==1 && !bw_calls && !clears);
    reset();main_mode=APP_MAIN_REFRESH_WATER;gray_target(2);display_main_transition_arm(&owner_a,false,false);
    assert(update_display_fast_page(&hl)==EPD_DRAW_SUCCESS);assert_final();assert(!water_calls && gray_calls==1 && !bw_calls);
    // 主循环给首页局部输出加入口许可，短时钟波形仍必须原样通过三种主页模式。
    // Main-loop permission arms local Home output; short clock motion must retain its waveform in all main modes.
    for(int mode=APP_MAIN_REFRESH_NORMAL;mode<=APP_MAIN_REFRESH_WATER;++mode){
        reset();main_mode=mode;
        memset(front,mode==APP_MAIN_REFRESH_FAST?0xff:0x73,BYTES);
        assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);
        calls=bw_calls=gray_calls=gc_calls=full_calls=clears=nav_draws=0;
        for(int step=1;step<=6;++step){
            front[200*(W/2)+700/2]=step%2?0x0f:0xf0;memcpy(final,front,BYTES);
            display_main_transition_arm(&owner_a,false,false);
            assert(update_display_area_diff_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,(EpdRect){0,640,H,256})==EPD_DRAW_SUCCESS);
            assert(last_wave==&E0470_FOLLOW_WAVEFORM&&last_mode==MODE_DU&&scan==READ_PICO_EPD_SCAN_FAST);
            assert(calls==step&&bw_calls==step&&!gray_calls&&!full_calls&&!clears&&!water_calls&&!nav_draws&&!allocations);
            assert_final();
        }
        display_main_transition_arm(&owner_a,false,false);
        assert(update_display_area_diff_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,(EpdRect){0,640,H,256})==EPD_DRAW_SUCCESS);
        assert(calls==6);assert_final();
        front[200*(W/2)+700/2]=0;memcpy(final,front,BYTES);failures=1;fail_error=EPD_DRAW_OTHER_ERROR;
        display_main_transition_arm(&owner_a,false,false);
        assert(update_display_area_diff_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,(EpdRect){0,640,H,256})==EPD_DRAW_SUCCESS);
        assert(clears==1&&gc_calls==1);assert_final();
    }
    puts("PASS: protected Cat/Kehai short motion retains FOLLOW/DU/FAST in normal, fast and ripple; unchanged targets skip scans and failures rebuild the exact frame");
    assert(!ota_display_depth);
    puts("main navigation: 200 complete BW transitions; calibrated gray bands, empty shelf slots, identical-target skips, allocation failure and three output-failure recoveries; moving markers, nibble/32-column boundaries and default outputs passed");
    return 0;
}

// Continuous state is exercised separately with native-size buffers.
void continuous_du_reset(void){}
bool continuous_du_busy(void){return false;}
