#pragma once
#include "epdiy.h"
extern const EpdWaveform E0470_WAVEFORM;
extern const EpdWaveform E0470_APPLY_WAVEFORM;
const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode);
