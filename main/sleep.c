/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 * 用户修订：票根从任意页面进入均用系统字体；浅睡唤醒恢复此前阅读字体，深睡由启动检查点恢复。
 * User revision: tickets use the system face from every page; light wake restores the previous reader face, while deep wake uses the startup checkpoint.
 * 用户修订：其他样式/自动锁屏先准备完整前缓冲，再清屏并做原 GC16 输出，缩短白屏等待。
 * User revision: other styles and idle locks prepare the complete front buffer before the original clear/GC16 output, shortening white-screen waits.
 * 用户修订：拼贴的新整帧缓存移到完整显示之后写入，密码层覆盖前提交。
 * User revision: persist new collage frames after complete presentation and before PIN overlays.
 * 用户修订：主动拼贴锁屏立即清白并显示圆锁，随后只用一次原GC16输出完整拼贴；取消占位和四步显影。
 * User revision: manual collage locks promptly present white and a round lock, then present the complete collage once with original GC16; remove placeholders and four-pass reveal.
 * 用户修订：合入官方 a2a63a6 的外设停用、IOE 收口与 CPU 浅睡保持；保留十分钟浅睡、密码和阅读检查点。
 * User revision: port official a2a63a6 peripheral/IOE parking and CPU light-sleep retention while preserving ten-minute light sleep, PIN and reader checkpoints.
 * 冻结：沿用实际后缓冲，不把圆锁当白色；不增加帧副本，自动休眠/其他样式/失败回退保留原流程。
 * Frozen: Retain true history including the badge, add no frame copy and preserve idle/other-style/failure recovery.
 */
#include "ui_avatar.h"
#include "book_cover_jobs.h"

#include "sleep.h"
#include "boot_state.h"
#include "lock_pin.h"
#include "lock_screen.h"
#include "read_pico_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_font_context.h"
#include "book_cover.h"
#include "book_lock_collage.h"
#include "book_ticket.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "ota_online.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_private/sleep_cpu.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_buzzer.h"
#include "ble_page_turner.h"
#include "push_sink.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "ui_wallpaper.h"
#include "ui_lock_feedback.h"

static const char* TAG = "read_pico";
static bool s_lock_feedback_visible;

#define APP_LOCK_LIGHT_SLEEP_MS (10U * 60U * 1000U)
#define APP_LOCK_KEY_POLL_US (2LL * 1000 * 1000)
#define APP_LOCK_STUCK_IRQ_US (3LL * 1000 * 1000)
#define APP_LOCK_SLEEP_ERROR_LIMIT 5
// PMU 确认后仍未掉 EN 时恢复调用方，禁止无限等待。/ Restore the caller if EN stays high after acknowledgement instead of waiting forever.
#define HOST_EN_DROP_WAIT_MS 5000

extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");

void app_lock_feedback(EpdiyHighlevelState *hl) {
    s_lock_feedback_visible = false;
    if (!hl || app_settings_lock_style() != 2 || pico_online_busy()) return;
    read_pico_transfer_status_t transfer = {0};
    read_pico_transfer_get_status(&transfer);
    if (transfer.state == READ_PICO_TRANSFER_UPLOADING) return;
    uint8_t *framebuffer = epd_hl_get_framebuffer(hl);
    if (!framebuffer || epd_rotated_display_width() < 97 || epd_rotated_display_height() < 97) return;
    display_main_transition_cancel();
    // 原GC16清白适用于任意旧灰阶，不用只保证黑白端点的短DU擦整屏，无额外定时停留。
    // Calibrated GC16 whitens any prior gray; endpoint-only short DU must not erase a whole gray page.
    epd_hl_set_all_white(hl);
    if (update_display_full(hl) != EPD_DRAW_SUCCESS) return;
    EpdRect area = ui_lock_feedback_draw(framebuffer);
    s_lock_feedback_visible = update_display_area_diff_with(hl, &E0470_FOLLOW_WAVEFORM,
        MODE_DU, area) == EPD_DRAW_SUCCESS;
}

static bool draw_wallpaper(uint8_t *framebuffer) {
    const char *path = app_settings_wallpaper_path();
    return app_settings_lock_style() == 1 &&
        ui_wallpaper_prepare_lock(framebuffer,path);
}

static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;

void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc) {
    s_tp = tp;
    s_acc = acc;
    // 没开 PM 时 IDF 不分配 CPU 保存区，浅睡里 CPU 不会掉电；这里手动申请一次。
    // Without PM, IDF never allocates CPU retention, so light sleep keeps the CPU powered; request it once here.
    esp_err_t err = sleep_cpu_configure(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "CPU light-sleep power-down off: %s", esp_err_to_name(err));
}

// 触摸、加速度计都在常开 3V3 上，ESP 浅睡或 EN 拉低都不会断它们的电，必须主动送进最低功耗态。
// Touch and accelerometer sit on the always-on 3V3; neither light sleep nor EN low cuts them, so park them explicitly.
static void periph_suspend(bool host_off) {
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    esp_err_t ioe = read_pico_ioe_sleep(host_off);
    if (ioe != ESP_OK) ESP_LOGW(TAG, "ioe sleep %s", esp_err_to_name(ioe));
    s_acc_was_on = sc7a20h_powered(s_acc);
    if (s_acc_was_on) (void)sc7a20h_power_down(s_acc);
    if (s_tp != NULL && cst836u_get_mode(s_tp) != CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_set_mode(s_tp, CST836U_MODE_DEEPSLEEP);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch deepsleep %s", esp_err_to_name(err));
    }
}

// 深睡的触摸只认 TP_RST 复位；加速度计按睡前的配置重新上电。
// Deep-sleeping touch only wakes via TP_RST; the accelerometer comes back with its pre-sleep config.
static void periph_resume(void) {
    (void)read_pico_ioe_wake();
    if (s_tp != NULL && cst836u_get_mode(s_tp) == CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_wake(s_tp);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch wake %s", esp_err_to_name(err));
    }
    if (s_acc_was_on) (void)sc7a20h_power_up(s_acc);
    s_acc_was_on = false;
}

static void lock_arm_ioe_wakeup(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << READ_PICO_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    read_pico_clear_ioe_int();
    gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        read_pico_pmu_drain_events();
        read_pico_clear_ioe_int();
        bool held = false;
        if (read_pico_pmu_poll() == ESP_OK) {
            held = (read_pico_pmu_get()->key_state & 0x01) != 0;
        }
        bool int_high = gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) != 0;
        if (!held && int_high) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= 150) return;
        } else {
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}

static void pickup_ack(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_read_events(acc, &ev);
    sc7a20h_ack_int(acc);
}

static bool pickup_ia(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_aoi_src_t aoi;
    if (sc7a20h_read_events(acc, &ev) != ESP_OK) return false;
    sc7a20h_aoi_decode(ev.aoi1_src, &aoi);
    return aoi.ia;
}

// 第一次低电平不够：高通余波还会再拉高。要连续安静一段时间才进浅睡。
// A first low is not enough: high-pass ringing can rise again. Wait for a quiet stretch before light sleep.
static bool pickup_wait_quiet(sc7a20h_handle_t acc, int quiet_ms, int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t low_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        if (sc7a20h_int1_level(acc) == 0) {
            if (low_from == 0) low_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - low_from >= quiet_ms) return true;
        } else {
            pickup_ack(acc);
            low_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc, uint32_t timeout_ms) {
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return APP_WAKE_KEY;
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    const int64_t deadline_us = timeout_ms
        ? esp_timer_get_time() + (int64_t)timeout_ms * 1000 : 0;
    periph_suspend(false);
    lock_arm_ioe_wakeup();
    if (pickup) {
        sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
        // 拿起去抖：指尖点屏/桌面震动/放机落桌余震都是 <200ms 的瞬态，真实抓握
        // 拿起加速度过 1g 且持续数百 ms。1000mg + 400ms(5 拍 @12.5Hz)双条件，
        // 只放行真实拿起。误醒仍频繁就继续加 ths_mg；拿起不灵就往回收。/ Debounce
        // pickup: taps and desk settles are sub-200ms transients, a real pickup
        // sustains >1g for hundreds of ms. 1000mg + 400ms (5 samples @12.5Hz)
        // passes real pickups only. Tune on real hardware after flashing.
        motion.ths_mg = 1000;
        motion.duration = 5;
        esp_err_t arm_err = sc7a20h_arm_pickup_wake(acc, &motion);
        if (arm_err != ESP_OK) {
            // 偶发 I2C 抖动重试一次；仍失败才降级。/ Retry once for a transient I2C glitch.
            arm_err = sc7a20h_arm_pickup_wake(acc, &motion);
        }
        // 成败都记账：失败时 IMU 可能已被部分上电，出口善后需按 armed 收起防漏电。
        // Account as armed either way: a failed arm may have partially powered the
        // IMU, and exit cleanup must park it to avoid a half-armed drain.
        acc_armed = true;
        if (arm_err != ESP_OK) {
            // 失效方向是误醒不是不醒：驱动默认 250mg/2 比我方 1000mg/5 灵敏，静默漏防
            // 表现为费电而非唤醒失灵，必须显式记录。/ The failure direction is false
            // wakeups, not missed ones: the driver default 250mg/2 is more sensitive
            // than our 1000mg/5, so a silent miss reads as battery drain, not a broken
            // wake; log it explicitly instead of swallowing it.
            pickup = false;
            ESP_LOGW(TAG, "pickup arm failed: %s; key only", esp_err_to_name(arm_err));
        } else {
            pickup_ack(acc);
            if (!pickup_wait_quiet(acc, 400, 2000)) {
                pickup = false;
                ESP_LOGW(TAG, "pickup INT1 stuck high, key only");
            } else {
                sc7a20h_config_light_sleep_wakeup(acc);
                if (!pickup_wait_quiet(acc, 400, 1500)) {
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    pickup = false;
                    ESP_LOGW(TAG, "pickup INT1 re-asserted, key only");
                } else {
                    // 安静窗口里若 INT1 闪过，ESP 会留下高电平唤醒挂起，关掉再打开清掉。
                    // If INT1 glitched in the quiet window, ESP keeps a high-level wake pending; disable then re-enable to clear it.
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    sc7a20h_config_light_sleep_wakeup(acc);
                    if (sc7a20h_int1_level(acc) > 0) {
                        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                        pickup = false;
                        ESP_LOGW(TAG, "pickup INT1 high at sleep, key only");
                    } else {
                        ESP_LOGI(
                            TAG, "wait key GPIO%d or pickup GPIO%d",
                            READ_PICO_IOE_INT_GPIO, (int)sc7a20h_int1_gpio(acc)
                        );
                    }
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", READ_PICO_IOE_INT_GPIO);
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    int64_t ioe_low_since_us = 0;
    unsigned sleep_errors = 0;
    esp_err_t last_sleep_error = ESP_OK;
    int64_t last_sleep_error_log_ms = 0;
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        // Check the deadline before every interrupt recovery path. A stuck-low
        // IO expander must never bypass the ten-minute PMU handoff forever.
        // 每次处理中断前先检查期限，避免 IO 扩展器低电平让票根永远停住。
        if (deadline_us && now_us >= deadline_us) {
            wake = APP_WAKE_TIMEOUT;
            ESP_LOGI(TAG, "lock light sleep timeout after %u ms", (unsigned)timeout_ms);
            break;
        }
        // The PMU event is authoritative even if the IOE edge was missed.
        // Timer wakeups below provide a bounded fallback poll for that case.
        // 即使漏掉 IOE 中断，也由定时唤醒轮询 PMU 按键事件。
        if (read_pico_pmu_take_key_wakeup()) {
            wake = APP_WAKE_KEY;
            break;
        }
        read_pico_clear_ioe_int();
        if (gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            if (!ioe_low_since_us) ioe_low_since_us = now_us;
            if (now_us - ioe_low_since_us >= APP_LOCK_STUCK_IRQ_US) {
                // Return to the active page instead of leaving an inert lock
                // image when a non-key IOE interrupt cannot be cleared.
                // 非按键中断一直清不掉时，退回可操作页面，避免像死机一样停在锁屏。
                ESP_LOGW(TAG, "lock IOE_INT stuck low; recover active page");
                wake = APP_WAKE_KEY;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        ioe_low_since_us = 0;
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        int64_t sleep_us = APP_LOCK_KEY_POLL_US;
        if (deadline_us) {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            if (remaining_us < sleep_us) sleep_us = remaining_us;
        }
        if (sleep_us <= 0) continue;
        esp_err_t timer_error = esp_sleep_enable_timer_wakeup((uint64_t)sleep_us);
        if (timer_error != ESP_OK) {
            ESP_LOGW(TAG, "lock sleep timer rejected: %s", esp_err_to_name(timer_error));
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                wake = APP_WAKE_KEY;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_err_t sleep_error = esp_light_sleep_start();
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        read_pico_clear_ioe_int();
        if (read_pico_pmu_take_key_wakeup()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            if (dt_ms < 80) {
                ESP_LOGW(TAG, "pickup ignored, sleep %lld ms", (long long)dt_ms);
                pickup_ack(acc);
                pickup_wait_quiet(acc, 400, 1500);
                continue;
            }
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
        }
        if (sleep_error != ESP_OK) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (sleep_error != last_sleep_error || now_ms - last_sleep_error_log_ms >= 30000) {
                ESP_LOGW(TAG, "lock light sleep rejected: %s, IOE_INT=%d",
                         esp_err_to_name(sleep_error),
                         gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO));
                last_sleep_error_log_ms = now_ms;
            }
            last_sleep_error = sleep_error;
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                ESP_LOGE(TAG, "lock light sleep repeatedly rejected; recover active page");
                wake = APP_WAKE_KEY;
                break;
            }
            // A rejected sleep must not spin at full CPU speed behind the static e-paper image.
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            last_sleep_error = ESP_OK;
            sleep_errors = 0;
        }
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    periph_resume();
    app_settings_set_last_wake((uint8_t)(wake == APP_WAKE_TIMEOUT ? APP_WAKE_NONE : wake));
    return wake;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    return app_light_sleep_wait_timed(acc, 0);
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    if (mode != APP_SLEEP_OFF && mode != APP_SLEEP_DEEP) return ESP_ERR_INVALID_ARG;
    // 产品页用 on_before_lock 保存并收齐后台；保留阅读内存以便交接失败仍可回到原页。
    // Product pages checkpoint/join in on_before_lock; retain reader state so a failed handoff can resume the same page.
    if (pico_online_busy() || !(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000)) || !read_pico_transfer_pause_for_sleep()) return ESP_ERR_INVALID_STATE;
    // gen18：深睡/关机没有 BLE 唤醒源，武装中的推流必须在此让位并留痕——
    // 「armed canceled by power_off」。关机交接失败回到原页时，状态机是诚实的
    // IDLE，不会对着一个已拆的栈继续倒数（覆盖表第 4 行）。
    // gen18: deep sleep / power off has no BLE wake source, so an open push
    // arm must yield here with a trace - "armed canceled by power_off". When a
    // failed shutdown returns to the operable page the machine is an honest
    // IDLE instead of counting down against a torn-down stack (row 4).
    push_sink_cancel_why("关机/深睡，接收结束");
    (void)ble_pt_stop(2000);
    (void)app_sync_time_to_pmu();
    // 必须先断面板轨，再拉低 MODE，避免常开逻辑仍耗电。/ Drop panel rails before parking MODE on the always-on supply.
    pmu_selftest_prepare_powerdown();
    periph_suspend(true);
    esp_err_t err = ESP_FAIL;
    const unsigned attempts = mode == APP_SLEEP_OFF ? 1 : 3;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
        err = mode == APP_SLEEP_OFF ? read_pico_pmu_power_off() : read_pico_pmu_report_sleep();
        ESP_LOGI(TAG, "%s attempt %u: %s", mode == APP_SLEEP_OFF ? "power off" : "SOFT_SLEEP",
                 attempt + 1, esp_err_to_name(err));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
            err = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "EN still high after PMU ack");
            break;
        }
        if (attempt + 1 < attempts) vTaskDelay(pdMS_TO_TICKS(200));
    }
    // 失败不复位重试；恢复外设，交还锁屏/密码层，阅读恢复检查点仍由原入口处理。
    // Do not reset/retry on failure; restore peripherals to the lock/PIN caller, which retains checkpoint ownership.
    periph_resume();
    read_pico_transfer_resume_after_sleep();
    return err;
}

esp_err_t app_sync_time_to_pmu(void) {
    time_t now = time(NULL);
    if (now < 946684800 || now > 4102444799U) {
        ESP_LOGW(TAG, "skip PMU time sync: system clock invalid");
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t seconds = (uint32_t)now;
    uint8_t payload[4] = {
        (uint8_t)(seconds & 0xff),
        (uint8_t)((seconds >> 8) & 0xff),
        (uint8_t)((seconds >> 16) & 0xff),
        (uint8_t)((seconds >> 24) & 0xff),
    };
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    ESP_LOGI(TAG, "PMU time sync before power action: %s", esp_err_to_name(err));
    return err;
}

void app_restart_host(void) {
    (void)app_sync_time_to_pmu();
    pmu_selftest_prepare_powerdown();
    esp_err_t err = read_pico_pmu_action(PMU_ACTION_HOST_NORMAL_RESTART, 250, 0x5052);
    ESP_LOGI(TAG, "normal restart: %s", esp_err_to_name(err));
    if (err != ESP_OK) esp_restart();
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background
) {
    bool feedback_visible = s_lock_feedback_visible;
    s_lock_feedback_visible = false;
    // 推流武装期拒绝锁屏：深睡没有 BLE 唤醒源（sleep.c 的深睡路径连栈都拆了），
    // 睡下去手机推什么都是丢；用户此刻正是在等一条推送。调用方照常重绘原页，
    // 与 transfer busy 的推迟同款处理。/ Refuse locking while a push arm is open:
    // deep sleep has no BLE wake source (the deep-sleep path even tears the stack
    // down), so anything the phone pushes would be lost - and waiting for a push
    // is exactly what the user is doing. The caller repaints as usual, the same
    // way a busy transfer postpones the lock.
    if (push_sink_armed()) {
        ESP_LOGW(TAG, "lock refused: push sink armed");
        return;
    }
    // Refuse to interrupt an active upload. The caller repaints its page when
    // this function returns, and the user can lock again after the transfer.
    bool radio_paused = read_pico_transfer_pause_for_sleep();
    if (!radio_paused) {
        pico_boot_clear_resume();
        ESP_LOGW(TAG, "lock postponed while transfer is busy");
        return;
    }
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    // 主动拼贴在白底圆锁上准备最终画面；其他入口仍保留旧页面，只改RAM。
    // Prepare manual collages behind the white lock badge; other entries keep their prior page while drawing RAM.
    epd_hl_set_all_white(hl);
    uint8_t lock_style = app_settings_lock_style();
    bool ticket = lock_style == 0;
    bool system_lock = ticket || lock_style == 2 || lock_pin_enabled();
    app_lock_font_t lock_font = {0};
    if (system_lock) app_font_begin_lock(&lock_font);
    // 阅读票根独占锁屏画布：始终先画最近书籍封面，再叠票根；自定义壁纸只属于壁纸模式。
    // Ticket mode owns the lock canvas: book cover first, ticket on top. Custom wallpaper is wallpaper-only.
    bool lock_drawn = ticket
        ? book_ticket_draw(framebuffer, reader_background)
        : lock_style == 2 ? book_lock_collage_prepare(framebuffer) : draw_wallpaper(framebuffer);
    if (!lock_drawn) {
        epd_hl_set_all_white(hl);
        ui_draw_full_image(framebuffer, lock_4bpp_bin_start);
    }
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (feedback_visible) result = update_display_full(hl);
    if (!feedback_visible || result != EPD_DRAW_SUCCESS) {
        // 反馈成功时不再重复清白；GC16从真实圆锁历史输出。自动/其他样式/失败保留原清屏。
        // A successful badge needs no second white clear; GC16 uses its true history. Idle/other/fault paths retain original clearing.
        epd_poweron();
        epd_clear();
        result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    }
    if (lock_style == 2 && lock_drawn && result == EPD_DRAW_SUCCESS) book_lock_collage_cache_commit(framebuffer);
    if (lock_style == 1 && lock_drawn && result == EPD_DRAW_SUCCESS) ui_wallpaper_cache_commit(framebuffer);

    app_lock_wait_key_idle(800);
    // 锁屏先浅睡，十分钟后进入深睡；按键唤醒使用已保存的页面检查点。
    // Locks light-sleep first, enter deep sleep after ten minutes and wake through the saved page checkpoint.
    ESP_LOGI(TAG, "lock LIGHT");
    epd_poweroff();
    app_wake_source_t wake = APP_WAKE_KEY;
    if (lock_pin_enabled()) lock_screen_authenticate(hl, touch, acc, false);
    else wake = app_light_sleep_wait_timed(acc, APP_LOCK_LIGHT_SLEEP_MS);
    if (wake == APP_WAKE_TIMEOUT) {
        // PMU SOFT_SLEEP drops the ESP rail while keeping its RTC alive. A short
        // power-key press boots the host; the e-paper keeps this lock image.
        app_enter_host_sleep(APP_SLEEP_DEEP);
    }
    pico_boot_clear_resume();
    if (system_lock) app_font_end_lock(&lock_font);
    read_pico_transfer_resume_after_sleep();

    // 参考帧和屏幕都归零，回到主循环后由当前页自己画一遍，不必知道是哪一页。
    // Zero the reference frame and the panel; the current page redraws after the loop resumes, without knowing which page it is.
    read_pico_pmu_drain_events();
    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    read_pico_pmu_drain_events();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    ESP_LOGI(TAG, "unlocked");
}

void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc) {
    if (!lock_pin_enabled()) return;
    // 卡挂载有界等待仅为了字体/锁屏图；没有卡仍必须验证。
    // Bound mount waiting for the face/art only; absent media still requires authentication.
    read_pico_sd_info_t info;
    for (unsigned i = 0; i < 150 && read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++i)
        vTaskDelay(pdMS_TO_TICKS(20));
    app_lock_font_t saved; app_font_begin_lock(&saved);
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    if (!lock_screen_restore(fb)) {
        bool drawn = app_settings_lock_style() == 0 ? book_ticket_draw(fb, false) :
            app_settings_lock_style() == 2 ? book_lock_collage_draw(fb) : draw_wallpaper(fb);
        if (!drawn) { memset(fb, 255, (size_t)epd_width() * epd_height() / 2); ui_draw_full_image(fb, lock_4bpp_bin_start); }
    }
    lock_screen_authenticate(hl, touch, acc, true);
    app_font_end_lock(&saved);
}
