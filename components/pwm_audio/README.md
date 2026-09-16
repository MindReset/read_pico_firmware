# pwm_audio

LEDC PWM 音频：环形缓冲加定时器 ISR，把 PCM 写成占空比。本仓库用作蜂鸣器底层之一。

LEDC PWM audio helper: a ring buffer and a timer ISR that writes PCM samples as duty cycles. Used as one of the buzzer backends.

这是 Espressif
[pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio)
的裁剪副本（Apache-2.0）。

This is a trimmed copy of Espressif's
[pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio)
(Apache-2.0).

## 接口 / API

- `pwm_audio_init()` / `pwm_audio_deinit()`
- `pwm_audio_start()` / `pwm_audio_stop()`
- `pwm_audio_write()`：把 PCM 推进环形缓冲。/ Push PCM into the ring buffer.
- `pwm_audio_set_param()` / `pwm_audio_set_sample_rate()` / `pwm_audio_set_volume()`

见 `include/pwm_audio.h`。

See `include/pwm_audio.h`.

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。版权归 Espressif Systems (Shanghai) CO LTD。

Apache-2.0, see [LICENSE](LICENSE). Copyright Espressif Systems (Shanghai) CO LTD.
