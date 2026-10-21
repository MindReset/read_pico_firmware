#pragma once
#include "common.h"
/// 飞行模式：射频被阻断时，升级和联网对时要给出明确提示，桩里也要有这两个声明。
/// Flight mode: updates and online time sync explain themselves while the radio is blocked, so the
/// stubs need both declarations too.
void read_pico_transfer_set_rf_blocked(bool blocked);
bool read_pico_transfer_rf_blocked(void);
