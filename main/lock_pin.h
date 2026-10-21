/* SPDX-License-Identifier: Apache-2.0
 * 中文：四位锁屏密码的原子NVS凭据；不存明文、不覆盖普通配置备份。
 * English: Atomic NVS credentials for a four-digit screen lock; no plaintext or ordinary-backup overwrite.
 * 冻结：旧凭据验证后才能修改/关闭；读取损坏时保持锁定。
 * Frozen: Verify existing credentials before replacement/removal; corrupted reads remain locked.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
/// 默认关闭；存储错误按开启处理，不能绕过验证。/ Default off; storage errors require authentication.
bool lock_pin_enabled(void);
/// 凭据是否可验证；不可靠存储不会退回关闭。/ Whether credentials are usable; bad storage never disables protection.
bool lock_pin_available(void);
/// 校验四位数字，常数时间比较摘要。/ Validate four digits with a constant-time digest comparison.
bool lock_pin_verify(const char pin[5]);
/// 未开启时old为NULL；开启时必须正确；new为NULL代表关闭。/ Null old enables from off; enabled changes require the old PIN; null new disables.
esp_err_t lock_pin_replace(const char old_pin[5], const char new_pin[5]);
/// 清零短暂密码工作区；禁止把其内容写进日志。/ Wipe temporary credentials; never log their contents.
void lock_pin_wipe(void *bytes, unsigned length);
