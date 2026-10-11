/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 */

#include "sleep.h"

#include "app.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_private/sleep_cpu.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_buzzer.h"
#include "read_pico_pmu.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"

static const char* TAG = "read_pico";

// 主机下电后等 EN 落下的上限；PMU 确认后约 100 ms 拉低，超时视为失败。
// Upper bound for EN to fall after power-down; the PMU drops it about 100 ms after acknowledging.
#define HOST_EN_DROP_WAIT_MS 5000

extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");

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

bool app_lock_powers_down(void) {
    return app_settings_sleep_mode() != APP_SLEEP_LIGHT;
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

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    periph_suspend(false);
    lock_arm_ioe_wakeup();
    if (pickup) {
        sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
        motion.ths_mg = 350;
        motion.duration = 3;
        sc7a20h_arm_pickup_wake(acc, &motion);
        acc_armed = true;
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
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", READ_PICO_IOE_INT_GPIO);
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    bool slept = false;
    for (;;) {
        read_pico_clear_ioe_int();
        if (!slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            read_pico_pmu_drain_events();
            read_pico_clear_ioe_int();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            if (read_pico_pmu_take_key_wakeup()) {
                wake = APP_WAKE_KEY;
                break;
            }
            read_pico_clear_ioe_int();
            continue;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_light_sleep_start();
        slept = true;
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
    }

    gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    periph_resume();
    app_settings_set_last_wake((uint8_t)wake);
    return wake;
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    periph_suspend(true);
    pmu_selftest_prepare_powerdown();
    esp_err_t err = mode == APP_SLEEP_OFF
        ? read_pico_pmu_power_off()
        : read_pico_pmu_report_sleep();
    ESP_LOGI(TAG, "%s %s", mode == APP_SLEEP_OFF ? "power off" : "SOFT_SLEEP", esp_err_to_name(err));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
        err = ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "EN still high after PMU ack");
    }
    // PMU 没把 EN 拉下来：恢复外设交还调用方，不能停在这里空转耗电。
    // The PMU did not drop EN: restore peripherals and return instead of idling at full power.
    periph_resume();
    return err;
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
) {
    // 乐曲的 32 kHz 采样中断和线圈电流会干扰锁屏整屏刷新，先停再上屏轨。
    // The score's 32 kHz sample ISR and coil current disturb the lock-face full refresh; stop it before the rails come up.
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    ui_draw_full_image(framebuffer, lock_4bpp_bin_start);
    epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    app_lock_wait_key_idle(800);
    app_sleep_mode_t mode = app_settings_sleep_mode();
    ESP_LOGI(TAG, "lock %s", app_sleep_mode_name(mode));

    // 主机下电只在失败时返回，退到浅睡，至少外设已停、按键可唤醒。
    // Host power-down returns only on failure; fall back to light sleep with peripherals parked and the key as wake.
    if (mode == APP_SLEEP_OFF || mode == APP_SLEEP_DEEP) {
        ESP_LOGW(TAG, "host sleep failed: %s, light sleep", esp_err_to_name(app_enter_host_sleep(mode)));
    }
    epd_poweroff();
    app_light_sleep_wait(acc);

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
