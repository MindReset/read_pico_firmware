/* SPDX-License-Identifier: Apache-2.0
 * 中文：S3 RTC 离线走时；在线每小时校准，离线每六小时短连已保存 WiFi。
 * English: S3 RTC keeps offline time; calibrate hourly online or briefly every six hours offline.
 */
#include "time_service.h"
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "read_pico_transfer.h"
#include "push_sink.h"
#include "sleep.h"

#define CLOCK_VALID_MIN 1704067200ULL
#define CLOCK_VALID_MAX 4102444799ULL
#define CLOCK_ONLINE_INTERVAL 3600ULL
// gen22：PMU 内部 RC 每天快几分钟，离线 6 小时才补一次会让误差累积到肉眼可见；
// 缩到 2 小时（仅在已保存 WiFi 时短连，耗电可控）。
// gen22: the PMU's internal RC gains minutes a day; a six-hour offline cadence lets
// the error grow visible. Two hours (short join only when WiFi is saved, power-wise OK).
#define CLOCK_OFFLINE_INTERVAL (2ULL * 3600)
#define CLOCK_RETRY_US (5LL * 60 * 1000000)
static atomic_bool s_running;
static bool s_loaded;
static uint64_t s_last_sync;
static int64_t s_next_attempt_us = 15000000;
// gen22 漂移账本：PMU 的本地秒走得快（内部 RC），用「上次校时的真实 UTC + 上次校时的
// 本地读数 + 学习到的 ppm」在没有网络时把时间拉回来。
// gen22 drift ledger: the PMU's local seconds run fast (internal RC); with no network,
// pull time back using "true UTC at last sync + local reading at last sync + learned ppm".
static uint32_t s_last_local;
static int32_t s_drift_ppm;
static bool s_drift_valid;

#define DRIFT_PPM_MAX 20000   // 2%：超过这个量当成异常样本丢弃 / beyond 2% treat the sample as bogus
#define DRIFT_MIN_ELAPSED 3600u  // 少于 1 小时的样本噪声太大 / under an hour, the sample is mostly noise

static void ensure_state(void) {
    if (s_loaded) return;
    nvs_handle_t handle;
    if (nvs_open("kiiko_clock", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u64(handle, "last_ntp", &s_last_sync) != ESP_OK) s_last_sync = 0;
        uint32_t local = 0; int32_t ppm = 0;
        if (nvs_get_u32(handle, "last_local", &local) == ESP_OK) s_last_local = local;
        if (nvs_get_i32(handle, "drift_ppm", &ppm) == ESP_OK) s_drift_ppm = ppm;
        nvs_close(handle);
    }
    if (s_last_sync < CLOCK_VALID_MIN || s_last_sync > CLOCK_VALID_MAX) s_last_sync = 0;
    s_drift_valid = s_last_sync != 0 && s_last_local >= CLOCK_VALID_MIN &&
                    s_last_local <= CLOCK_VALID_MAX && s_drift_ppm > -DRIFT_PPM_MAX &&
                    s_drift_ppm < DRIFT_PPM_MAX;
    s_loaded = true;
}

void app_time_service_compensate(uint32_t *utc_sec) {
    if (!utc_sec) return;
    ensure_state();
    if (!s_drift_valid) return;                     // 没有可靠样本就不猜 / no sample, no guess
    uint32_t local = *utc_sec;
    if (local < CLOCK_VALID_MIN || local > CLOCK_VALID_MAX) return;
    if (local <= s_last_local) return;              // 时间倒退（换电池/重置）不补偿 / went backwards: skip
    uint64_t elapsed = (uint64_t)local - s_last_local;
    // 本地每走 1e6，真实只走 (1e6 - ppm)；把本地增量换算回真实增量。
    // For every 1e6 the local clock advanced, real time advanced (1e6 - ppm).
    uint64_t real = elapsed * (uint64_t)(1000000 - s_drift_ppm) / 1000000u;
    uint64_t corrected = (uint64_t)s_last_sync + real;
    if (corrected > CLOCK_VALID_MAX) return;
    int64_t delta = (int64_t)corrected - (int64_t)local;
    if (delta > 24L * 3600 || delta < -24L * 3600) return;  // 跨度过大说明账本已失效 / ledger stale
    ESP_LOGI("clock", "drift compensation: local=%lu -> %lu (ppm=%ld, held %llu s)",
             (unsigned long)local, (unsigned long)corrected, (long)s_drift_ppm,
             (unsigned long long)elapsed);
    *utc_sec = (uint32_t)corrected;
}

void app_time_service_note_sync(uint32_t true_utc, uint32_t local_before) {
    if (true_utc < CLOCK_VALID_MIN || true_utc > CLOCK_VALID_MAX) return;
    ensure_state();
    if (s_drift_valid && local_before > s_last_local) {
        uint64_t elapsed = (uint64_t)local_before - s_last_local;
        if (elapsed >= DRIFT_MIN_ELAPSED) {
            int64_t error = (int64_t)local_before - (int64_t)true_utc;  // 正 = 本地偏快 / positive = running fast
            int64_t instant = error * 1000000LL / (int64_t)elapsed;
            if (instant > -DRIFT_PPM_MAX && instant < DRIFT_PPM_MAX) {
                // 指数平滑：老样本权重 3、新样本 1，避免单次抖动主导。
                // EMA: old samples weight 3, new sample 1, so one outlier cannot dominate.
                int64_t blended = ((int64_t)s_drift_ppm * 3 + instant) / 4;
                s_drift_ppm = (int32_t)blended;
                ESP_LOGI("clock", "drift sample: %ld ppm over %llu s -> %ld ppm",
                         (long)instant, (unsigned long long)elapsed, (long)s_drift_ppm);
            }
        }
    }
    s_last_sync = true_utc;
    s_last_local = true_utc;   // 校正后本地读数等于真实 UTC / after correction the local reading equals true UTC
    s_drift_valid = true;
    nvs_handle_t handle;
    if (nvs_open("kiiko_clock", NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_set_u64(handle, "last_ntp", s_last_sync);
        (void)nvs_set_u32(handle, "last_local", s_last_local);
        (void)nvs_set_i32(handle, "drift_ppm", s_drift_ppm);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
}

static void clock_worker(void *arg) {
    (void)arg;
    // gen22：SNTP 成功会直接 settimeofday 覆盖系统时间，所以「漂移前的本地读数」
    // 必须在发起对时之前抓；事后读到的已经是真值，学不到漂移。
    // gen22: a successful SNTP settimeofday overwrites system time, so the "local
    // reading before drift correction" must be captured BEFORE the sync - reading it
    // afterwards yields the truth and teaches us nothing.
    uint32_t local_before = 0;
    {
        time_t now = time(NULL);
        if (now >= (time_t)CLOCK_VALID_MIN && now <= (time_t)CLOCK_VALID_MAX)
            local_before = (uint32_t)now;
    }
    uint32_t utc = 0;
    esp_err_t err = read_pico_transfer_sync_time_auto(&utc);
    ESP_LOGI("clock", "background calibration: %s", esp_err_to_name(err));
    if (err == ESP_OK && local_before) app_time_service_note_sync(utc, local_before);
    atomic_store(&s_running, false);
    vTaskDelete(NULL);
}

void app_time_service_poll(bool idle) {
    if (!idle) return;
    ensure_state();
    // gen22 蓝牙校时：手机带来的真值优先登记（无 Wi-Fi 时的主力校时通道）。
    // 系统时钟已在 push_sink 收到时写好，这里补漂移样本与 PMU 同步。
    // gen22 time-over-BLE: prefer the phone's truth (the main path with no WiFi).
    // The system clock was already set on receipt; record the sample and sync the PMU.
    uint32_t phone_utc = 0;
    if (push_sink_take_time_sync(&phone_utc)) {
        time_t local = time(NULL);
        uint32_t local_before = (local >= (time_t)CLOCK_VALID_MIN && local <= (time_t)CLOCK_VALID_MAX)
                                ? (uint32_t)local : phone_utc;
        app_time_service_note_sync(phone_utc, local_before);
        (void)app_sync_time_to_pmu();
    }
    uint32_t received;
    if (read_pico_transfer_take_time_sync(&received)) {
        // 只有真实网络成功才持久化；普通重启/手动设时间不能伪装成网络校准。
        // Persist only a real network success; reboot/manual edits do not count as calibration.
        // 前台传书路径的 SNTP 已经把系统时间改成真值，拿不到漂移前的读数，
        // 因此只刷新基准、不参与漂移学习（漂移由后台 clock_worker 采样）。
        // The foreground transfer path already applied SNTP to system time, so the
        // pre-drift reading is gone: refresh the anchor only, leave drift sampling to
        // the background clock_worker.
        app_time_service_note_sync(received, received);
        (void)app_sync_time_to_pmu();
    }
    const int64_t monotonic = esp_timer_get_time();
    if (atomic_load(&s_running) || monotonic < s_next_attempt_us) return;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    bool online = status.state == READ_PICO_TRANSFER_READY &&
                  status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready;
    if (!online && status.state != READ_PICO_TRANSFER_STOPPED) return;
    time_t now = time(NULL);
    uint64_t interval = online ? CLOCK_ONLINE_INTERVAL : CLOCK_OFFLINE_INTERVAL;
    if (now >= (time_t)s_last_sync && s_last_sync && (uint64_t)now - s_last_sync < interval) return;
    s_next_attempt_us = monotonic + CLOCK_RETRY_US;
    if (!online) {
        char ssid[33]; bool configured = false;
        if (read_pico_transfer_get_saved_wifi(ssid, &configured) != ESP_OK || !configured) return;
    }
    atomic_store(&s_running, true);
    // 仅对时期间保留任务栈；不创建常驻无线任务或页面缓存。
    // Keep a task stack only during calibration; no permanent radio task or page cache.
    if (xTaskCreate(clock_worker, "rtc_sync", 6144, NULL, 1, NULL) != pdPASS) {
        atomic_store(&s_running, false);
        ESP_LOGW("clock", "background calibration deferred: no task memory");
    }
}
