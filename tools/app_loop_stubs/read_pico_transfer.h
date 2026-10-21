/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
static inline void read_pico_transfer_service_poll(void) {}

#include "common.h"
#define READ_PICO_TRANSFER_STOPPED 0
#define READ_PICO_TRANSFER_MODE_STA 1
#define READ_PICO_TRANSFER_MODE_AP 2
typedef struct {int state,mode;bool network_ready;} read_pico_transfer_status_t;
typedef struct {int mode;bool network_only;} read_pico_transfer_cfg_t;
void read_pico_transfer_get_status(read_pico_transfer_status_t *s);
int read_pico_transfer_get_saved_wifi(char *ssid,bool *configured);
int read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg);
bool read_pico_transfer_try_stop_if_idle(void);
