/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏扫描时序：按像素钟解出行/帧参数与帧频。
 *
 * 一帧要给每条栅极开门一次，开门期间把整行源极数据移进去，所以
 *
 *     L_DL   = H / p                                （每钟送 p 个像素）
 *     H_all  = L_SL + L_BL + L_DL + L_EL            （一行的总钟数）
 *     T_line = H_all / f                            （f 单位 MHz，得微秒）
 *     T_frame= T_line · V_all,  V_all = F_SL+F_BL+V+F_EL
 *     ν      = 10^6 / T_frame                       （帧频 Hz）
 *     T_refresh = N · T_frame                       （N 是波形相数）
 *
 * 帧频只由几何与时钟决定，和 LUT 里的电压码无关：LUT 决定扫几帧，不决定一帧多久。
 * 源极电压只在 L_DL 有效，CKV 必须盖住这段。多出来的行时间堆到 L_EL，不能堆到
 * L_BL。厂家表 LGONL≈13.4µs。消隐下界：T(L_SL)≥0.3µs、T(L_BL)≥0.8µs、L_EL≥4 钟。
 * 这些下界都是时间，换算成钟数就随 f 变，所以每次改像素钟都要重新解一遍。
 *
 * EPD scan timing: solve line/frame parameters and frame rate from the pixel clock.
 *
 * Each frame opens every gate once and shifts a full source line while it is
 * open, so
 *
 *     L_DL   = H / p                                (p pixels per clock)
 *     H_all  = L_SL + L_BL + L_DL + L_EL            (clocks per line)
 *     T_line = H_all / f                            (f in MHz, result in µs)
 *     T_frame= T_line · V_all,  V_all = F_SL+F_BL+V+F_EL
 *     ν      = 10^6 / T_frame                       (frame rate in Hz)
 *     T_refresh = N · T_frame                       (N is waveform phases)
 *
 * Frame rate is geometry and clock only; LUT voltage codes pick how many
 * frames, not how long one frame lasts. Source voltages are valid only
 * during L_DL, so CKV must cover that window. Extra line time goes to
 * L_EL, never L_BL. Vendor LGONL≈13.4µs. Blanking floors: T(L_SL)≥0.3µs,
 * T(L_BL)≥0.8µs, L_EL≥4 clocks. Those floors are times, so clock counts
 * change with f and must be re-solved whenever the pixel clock changes.
 */

#pragma once

#include "epd_lcd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 板级支持的像素钟区间。上限受 DMA 供数能力限制，不是面板规格限制。
/// / Board pixel-clock range. The cap is DMA feed, not a panel spec.
#define READ_PICO_EPD_PCLK_MIN_MHZ 12
#define READ_PICO_EPD_PCLK_MAX_MHZ 24

/// 扫描档位。灰阶刷新按原厂算法文档的设计点（ν≈90Hz）配帧周期；自制的跟随 DU 与
/// 连续 DU 是按最短行标定的，跟着拉长只会白白降帧率。实测在 7~18ms 之间改帧周期对
/// GC16 的成像看不出差别，所以这里只是各自守住自己的标定点，不是靠它调驱动量。
/// 切换只是几个寄存器加一次 RMT 重建，放在两次刷新之间做。
/// / Scan profile. Gray refresh uses the vendor design point (ν≈90 Hz).
/// Follow and continuous DU are calibrated at the shortest line; stretching
/// that only drops frame rate. Bench GC16 looks the same from 7–18 ms, so
/// each profile keeps its own setpoint rather than using it as drive.
/// Switching is a few registers plus one RMT rebuild, between refreshes.
typedef enum {
    READ_PICO_EPD_SCAN_FULL = 0,  ///< 行周期补到波形标定的帧周期 / Pad the line to the waveform frame period
    READ_PICO_EPD_SCAN_FAST,      ///< 行周期取规格允许的最短值 / Shortest legal line period
} read_pico_epd_scan_profile_t;

typedef struct {
    int pclk_mhz;         ///< f
    int px_per_clk;       ///< p，由总线位宽与 2bit/像素得出 / p from bus width and 2 bits/pixel
    int line_start;       ///< L_SL，喂 le_high_time / L_SL → le_high_time
    int line_back_porch;  ///< L_BL，喂 line_front_porch / L_BL → line_front_porch
    int line_data;        ///< L_DL = H / p
    int line_end;         ///< L_EL，凑行长的余量，必须写进 LCD / L_EL slack; must be written to the LCD
    int line_clocks;      ///< H_all
    int line_us;          ///< T_line
    int frame_lines;      ///< V_all
    int frame_us;         ///< T_frame
    int frame_hz_x100;    ///< ν×100，避免为了两位小数引入浮点 / ν×100, no float for two decimals
    int ckv_high_01us;    ///< T(L_GON)，单位 0.1µs / T(L_GON) in 0.1 µs
} read_pico_epd_scan_t;

/// 按当前面板几何、像素钟与档位解一套扫描参数。pclk 会先夹到板级区间里。
/// / Solve scan params from geometry, pixel clock, and profile. pclk is clamped first.
void read_pico_epd_scan(
    int pclk_mhz, read_pico_epd_scan_profile_t profile, read_pico_epd_scan_t* out
);

/// N 相波形的墙钟时间 T_refresh = N · T_frame，单位毫秒。
/// / Wall time for an N-phase waveform: T_refresh = N · T_frame, in ms.
int read_pico_epd_refresh_ms(int pclk_mhz, read_pico_epd_scan_profile_t profile, int phases);

/// 改像素钟：重解行消隐与 CKV 宽度再切频率，只调 epd_set_lcd_pixel_clock_MHz
/// 会让消隐停在上一档的钟数上，高频下 CKV 就顶满整行了。
/// / Change the pixel clock: re-solve blanking and CKV, then switch frequency.
/// Calling epd_set_lcd_pixel_clock_MHz alone leaves the previous clock counts,
/// so CKV fills the whole line at high f.
void read_pico_epd_set_pclk(int pclk_mhz);

/// 当前像素钟。/ Current pixel clock.
int read_pico_epd_pclk_mhz(void);

/// FULL 档要凑到的帧周期（µs）。每相的驱动量正比于它，灰阶梯子的展开程度也就由它
/// 决定：太短压不黑，太长单次推动就饱和、梯子塞到白端。默认取波形头里的标定值，
/// 连调时可以在线改。下界受最短行限制（约 700 行 × 最短行长）。
/// / FULL-profile target frame period (µs). Drive per phase scales with this,
/// and so does the gray ladder: too short never goes black; too long saturates
/// in one push and packs the ladder toward white. Default is the waveform
/// header setpoint; live-tunable. Floor is the shortest line (~700 × min line).
int read_pico_epd_frame_target_us(void);
void read_pico_epd_set_frame_target_us(int us);

/// CKV 高电平上限（0.1µs）。厂家表 LGONL≈13.4µs，默认 134。
/// / CKV high cap (0.1 µs). Vendor LGONL≈13.4 µs; default 134.
int read_pico_epd_ckv_high_max_01us(void);
void read_pico_epd_set_ckv_high_max_01us(int v);

/// 切扫描档位，同档重复调用是空操作。必须在两次刷新之间调。
/// / Switch scan profile; same-profile calls are no-ops. Between refreshes only.
void read_pico_epd_use_scan(read_pico_epd_scan_profile_t profile);

/// 开机：把解出的扫描参数灌进 LCD 并初始化外设。
/// / Boot: push the solved scan into the LCD and init the peripheral.
void read_pico_epd_init_lcd(
    const lcd_bus_config_t* bus, int bus_width, int width, int height,
    const read_pico_epd_scan_t* scan
);

#ifdef __cplusplus
}
#endif
