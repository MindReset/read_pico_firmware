/* SPDX-License-Identifier: Apache-2.0
 * 中文：密码设置的验证/确认事务，取消不保存。/ English: Verified PIN setup transactions; cancellation never saves.
 */
#pragma once
#include "lock_pin.h"
typedef enum { PIN_FLOW_OLD, PIN_FLOW_NEW, PIN_FLOW_CONFIRM, PIN_FLOW_DONE } lock_pin_step_t;
typedef struct { char old[5], next[5];lock_pin_step_t step;bool disable; } lock_pin_flow_t;
/// 启用需新密码与确认；已开启的修改/关闭先验证旧密码。/ New PINs need confirmation; enabled changes/removal first verify the old one.
void lock_pin_flow_begin(lock_pin_flow_t *flow,bool disable);
/// 接受完整四位输入；只在确认/旧密码关闭后提交。/ Accept four digits; commit only at confirmation or verified removal.
esp_err_t lock_pin_flow_input(lock_pin_flow_t *flow,const char pin[5]);
/// 当前步骤的系统字体标题。/ System-font title for the current step.
const char *lock_pin_flow_title(const lock_pin_flow_t *flow);
/// 清零全部暂存，不修改NVS。/ Wipe staging without changing NVS.
void lock_pin_flow_end(lock_pin_flow_t *flow);
