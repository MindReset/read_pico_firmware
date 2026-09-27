/* SPDX-License-Identifier: Apache-2.0
 * 扫描时序替身。/ Scan timing shim.
 */
#pragma once
#define READ_PICO_EPD_PCLK_MIN_MHZ 12
#define READ_PICO_EPD_PCLK_MAX_MHZ 24
typedef enum { READ_PICO_EPD_SCAN_FULL, READ_PICO_EPD_SCAN_FAST } read_pico_epd_scan_t;
void read_pico_epd_set_pclk(int mhz);
void read_pico_epd_use_scan(read_pico_epd_scan_t scan);
