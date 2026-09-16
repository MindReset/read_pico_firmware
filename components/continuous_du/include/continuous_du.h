/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 连续 DU：一次扫描一个相位，软件记每像素剩余相位。帧率与黑度解耦，
 * 半程反转可重置。只能驱动 1bit 内容。
 *
 * Continuous DU: one phase per scan, leftover phases kept in software.
 * Frame rate and darkness are independent; a mid-flight reverse resets
 * the count. 1-bit content only.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epd_highlevel.h"
#include "epdiy.h"

/// 连续 DU：一次面板扫描只输出一个 DU 相位，每个像素还需要几个相位由软件状态机
/// 单独记着。这样"画面多久动一次"和"一个像素被驱动多久"就解耦了——常规做法是把
/// N 个相位连在一次刷新里发完，期间画面冻结，于是帧率只能靠砍相位数（砍对比度）
/// 来换；这里每 6.2ms 就出一帧，相位数只决定黑度，不影响帧率。
///
/// 另一半好处是半程反转不再丢进度：常规做法只有 front/back 两个 framebuffer，
/// 记不住"这个像素擦白只擦了一半"，反复半程切换会让粒子停在中间、颜色塌向灰。
/// 这里每像素的剩余相位是显式的，反转时直接重置成完整相位数（early cancellation）。
///
/// 参考 Modos Labs Glider/Caster 与 E0470A01-CF-O 的 continuous DU 实现。
///
/// 限制：difference 只用得上纯黑、纯白和"不驱动"三种取值，所以只能驱动 1bit 内容，
/// 灰度和抗锯齿字形走不了这条路。
///
/// Continuous DU: one panel scan emits a single DU phase; leftover phases
/// per pixel live in a software state machine. How often the picture
/// moves is then independent of how long a pixel is driven. The usual
/// path fires N phases in one refresh while the picture is frozen, so
/// frame rate can only be traded against phase count (contrast). Here a
/// frame lands every 6.2 ms; phase count only sets darkness.
///
/// Mid-flight reverse also keeps progress: a front/back pair cannot
/// remember "this pixel was only half erased", so rapid reverse leaves
/// particles mid-travel and the color collapses toward gray. Leftover
/// phases are explicit here; a reverse resets them to a full count
/// (early cancellation).
///
/// See Modos Labs Glider/Caster and the E0470A01-CF-O continuous DU.
///
/// Limit: difference only encodes black, white, or "do not drive", so
/// this path is 1-bit only. Gray and antialiased glyphs cannot use it.

/// 压黑与擦白各自需要的相位数。参考实现取 6/7，擦白比压黑更费所以多给一相位；这里
/// 在试对称的 6/6，擦白少一相位会让旧圆点退白更快，代价是可能留下更淡的残影。
/// / Dark and light phase counts. The reference uses 6/7 (erase costs more).
/// This tree tries 6/6 so old dots fade faster, at the cost of lighter ghosting.
#define CONTINUOUS_DARK_PHASES 6
#define CONTINUOUS_LIGHT_PHASES 6

int continuous_du_dark_phases(void);
int continuous_du_light_phases(void);

/// 分配每像素状态（PSRAM，epd_width()*epd_height() 字节）。重复调用是安全的。
/// / Allocate per-pixel state (PSRAM, epd_width()*epd_height() bytes). Repeat-safe.
bool continuous_du_init(void);
void continuous_du_deinit(void);

/// 把所有像素的剩余相位清零，等于放弃尚未走完的驱动。
/// / Zero leftover phases; abandon any drive still in flight.
void continuous_du_reset(void);

/// 是否还有像素没走完相位。用来决定手指停下后要不要继续补扫。
/// / True if any pixel still has leftover phases. Used to keep scanning after the finger stops.
bool continuous_du_busy(void);

/// 逻辑坐标（UI 用的旋转后坐标）换成 framebuffer 的物理坐标。
/// / Logical (rotated UI) coordinates to framebuffer physical coordinates.
void continuous_du_from_logical(int lx, int ly, int* px, int* py);

/// 逻辑矩形换成 framebuffer 的物理矩形。
/// / Logical rect to framebuffer physical rect.
EpdRect continuous_du_rect_from_logical(EpdRect logical);

/// 在物理坐标系标记一个实心圆：phases 为正表示压黑，为负表示擦白。
/// 已在反方向途中的像素会被直接重置成新方向的完整相位数。
/// / Mark a filled circle in physical coords: +phases darkens, −phases erases.
/// A pixel already going the other way is reset to a full count in the new direction.
void continuous_du_mark_circle(int cx, int cy, int radius, int phases);

/// 同上，但标记一个物理坐标矩形。用来擦掉上一次定稿留下的圆点编号之类的图形。
/// / Same, but a physical rect. Used to erase last-settle marks such as dot numbers.
void continuous_du_mark_rect(EpdRect area, int phases);

/// 比较 to/from 的 4bpp：目标 <8 压黑，≥8 擦白。invert 则先往反方向推。
/// / Compare 4bpp to/from: dest <8 darkens, ≥8 erases. invert drives the opposite way first.
void continuous_du_mark_diff(
    const uint8_t* to, const uint8_t* from, EpdRect area, int phases, bool invert
);

/// 扫描一个相位，area 为物理坐标矩形。只有剩余相位非零的像素会被驱动。
/// / Scan one phase over a physical rect. Only pixels with leftover phases are driven.
enum EpdDrawError continuous_du_scan(EpdiyHighlevelState* hl, EpdRect area);
