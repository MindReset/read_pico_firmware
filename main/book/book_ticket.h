/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：记录实际阅读时长和翻页，并绘制票根锁屏。
 * English: Record actual reading time and turns, and paint the ticket lock face.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t book_ticket_record(uint32_t seconds, uint32_t turns);
/// 近 30 天（最早到今天）的实测阅读秒数；时钟无效时返回 false 且清零。
/// Measured seconds for the last 30 days, oldest to today; false and zeroed without a valid clock.
bool book_ticket_recent_days(uint32_t seconds[30]);
/// 当前月每日真实阅读秒数，兼容旧30日记录；无有效日期返回 false。/ Current-month measured seconds with legacy 30-day migration; false without a valid date.
bool book_ticket_month_days(int year, int month, uint32_t seconds[31]);
bool book_ticket_draw(uint8_t *fb, bool reader_background);
