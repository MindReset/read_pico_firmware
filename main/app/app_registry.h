/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 正式产品页面的清单。菜单、主循环和开机页都从这里取。
 *
 * Registry of production pages used by the menu, app loop, and boot route.
 */

#pragma once

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

int app_count(void);
const app_desc_t* app_at(int index);
/// 找不到时返回 -1。/ Returns -1 when not found.
int app_index_of(const app_desc_t* app);

/// 开机默认产品页。/ Default production page at boot.
const app_desc_t* app_home_page(void);

#ifdef __cplusplus
}
#endif
