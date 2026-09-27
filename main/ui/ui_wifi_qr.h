/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 热点连接二维码缓存与绘制。/ Hotspot connection QR cache and drawing.
 * 冻结：编码与绘制分离，不在render中分配或访问网络。/ Frozen: encode separately; render never allocates or accesses networking.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

/// UI线程预编码标准WiFi载荷；失败清空旧码。/ Pre-encode standard WiFi payload on UI thread; failure clears old code.
bool ui_wifi_qr_prepare(const char* ssid, const char* password);
/// 预编码设备HTTP IPv4网址；失败清空旧码。/ Pre-encode the device HTTP IPv4 URL; failure clears old code.
bool ui_wifi_qr_prepare_url(const char* url);
/// 丢弃缓存，供网络停止或切换时调用。/ Discard cached code when networking stops or changes.
void ui_wifi_qr_clear(void);
/// 纯绘制，整数缩放且保留四模块静区。/ Paint only, with integer scaling and a four-module quiet zone.
void ui_wifi_qr_draw(uint8_t* fb, EpdRect area);
