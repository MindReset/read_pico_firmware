/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"

/// 普通纯文字翻页：白底到中灰的少量回浅脉冲提前，其余迁移保留原表。
/// Plain text turns: advance a few gray-edge settling pulses; retain all other transitions.
const EpdWaveform* display_reader_text_waveform(void);
