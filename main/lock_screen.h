/* SPDX-License-Identifier: Apache-2.0
 * 中文：密码锁独占输入，只有验证成功返回。/ English: Password lock owns input; return only after successful verification.
 */
#pragma once
#include "epd_highlevel.h"
#include "cst836u.h"
#include "sc7a20h.h"
/// 仅恢复匹配当前样式/壁纸的实际锁屏图，失败不影响密码保护。/ Restore actual lock art only for the current style/wallpaper; failure never removes protection.
bool lock_screen_restore(uint8_t *frame);
/// 普通锁屏先等待唤醒，开机直接验证；错误/取消不能返回主循环。/ Normal locks wait for wake; boot verifies immediately; errors/cancel cannot return to the app loop.
void lock_screen_authenticate(EpdiyHighlevelState *hl,cst836u_handle_t touch,sc7a20h_handle_t acc,bool boot);
