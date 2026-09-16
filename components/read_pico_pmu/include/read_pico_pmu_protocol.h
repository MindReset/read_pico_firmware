/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 与 CW32 `shared/pmu_protocol.h` 保持一致。线格式，不是主机实现。
 *
 * Must match CW32 `shared/pmu_protocol.h`. Wire format only.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PMU_I2C_ADDR               0x2A
#define PMU_PROTOCOL_MAJOR         1
#define PMU_PROTOCOL_MINOR         1
#define PMU_FRAME_SIZE             64
#define PMU_FRAME_PAYLOAD_SIZE     44
#define PMU_IDENTITY_SIZE          32
#define PMU_STATUS_SIZE            64
#define PMU_DIAGNOSTICS_SIZE       32
#define PMU_EVENT_SIZE             16
#define PMU_EVENT_FIFO_DEPTH       8
#define PMU_RESULT_CACHE_DEPTH     4

#define PMU_REG_IDENTITY           0x00
#define PMU_REG_STATUS             0x20
#define PMU_REG_DIAGNOSTICS        0x60
#define PMU_REG_COMMAND            0x80
#define PMU_REG_RESPONSE           0x81
#define PMU_REG_EVENT_PEEK         0x82
#define PMU_REG_EVENT_ACK          0x83
#define PMU_REG_EVENT_COUNT        0x84
/// QUICK_BATTERY：8 字节轻量电池快照，免帧协议直接读。
/// / 8-byte battery snapshot, no frame protocol.
/// [0..1] battery_mv u16 LE
/// [2..3] soc_permille u16 LE（0~1000，0xFFFF=未知 / unknown）
/// [4] charge_state u8（pmu_charge_state）
/// [5] flags u8（bit0=battery_valid bit1=soc_valid bit2=charging_active）
/// [6..7] crc16 u16 LE（CRC16-CCITT-FALSE，覆盖 [0..5] / covers [0..5]）
#define PMU_REG_QUICK_BATTERY      0x85
#define PMU_QUICK_BATTERY_SIZE     8

#define PMU_FW_MAJOR               1
#define PMU_FW_MINOR               0
/// 与 CW32 固件 PMU_FW_PATCH 对齐。/ Must match CW32 firmware.
#define PMU_FW_PATCH               8
#define PMU_HW_REVISION            1
#define PMU_DEVICE_ID              0x3201U

#define PMU_FRAME_MAGIC            0xA5U
#define PMU_FRAME_KIND_REQ         0U
#define PMU_FRAME_KIND_RESP        1U

#define PMU_CAP_BATTERY_ADC        (1u << 0)
#define PMU_CAP_CHARGE_STATUS      (1u << 1)
#define PMU_CAP_KEY_EVENTS         (1u << 2)
#define PMU_CAP_LED_CONTROL        (1u << 3)
#define PMU_CAP_HOST_EN_CONTROL    (1u << 4)
#define PMU_CAP_HOST_BOOT_CONTROL  (1u << 5)

#define PMU_STATUS_BATTERY_VALID     (1u << 0)
#define PMU_STATUS_BATTERY_LOW       (1u << 1)
#define PMU_STATUS_BATTERY_CRITICAL  (1u << 2)
#define PMU_STATUS_CHARGING_ACTIVE   (1u << 3)
#define PMU_STATUS_CHARGE_PIN_HIGH   (1u << 4)
#define PMU_STATUS_KEY_PRESSED       (1u << 5)
#define PMU_STATUS_HOST_EN_HIGH      (1u << 6)
#define PMU_STATUS_HOST_BOOT_LOW     (1u << 7)
#define PMU_STATUS_EVENT_PENDING     (1u << 8)
#define PMU_STATUS_COMMAND_BUSY      (1u << 9)
#define PMU_STATUS_CONFIG_VALID      (1u << 10)
#define PMU_STATUS_CW_INT_ASSERTED   (1u << 13)

enum pmu_power_state {
    PMU_PWR_OFF = 0,
    PMU_PWR_POWERING_ON = 1,
    PMU_PWR_BOOT_WAIT = 2,
    PMU_PWR_RUNNING = 3,
    PMU_PWR_SHUTDOWN_PENDING = 4,
    PMU_PWR_RESETTING = 5,
    PMU_PWR_DOWNLOAD_MODE = 6,
    PMU_PWR_FAULT = 7,
    PMU_PWR_SOFT_SLEEP = 8
};

enum pmu_charge_state {
    PMU_CHARGE_UNKNOWN = 0,
    PMU_CHARGE_NOT_CHARGING = 1,
    PMU_CHARGE_CHARGING = 2,
    PMU_CHARGE_FULL_INFERRED = 3,
    PMU_CHARGE_FAULT = 4
};

enum pmu_command {
    PMU_CMD_PING                    = 0x0001,
    PMU_CMD_GET_EXTENDED_INFO       = 0x0002,
    PMU_CMD_HOST_READY              = 0x0004,
    /// payload: unix_seconds:u32 LE（UTC）。校准 CW 本地时间基准。
    /// / unix_seconds:u32 LE (UTC). Calibrates the CW local clock.
    PMU_CMD_TIME_SYNC               = 0x0005,
    PMU_CMD_CLEAR_DIAGNOSTICS       = 0x0006,
    /// 响应 8B：unix_seconds:u32（0=未校准）+ millis:u16 + synced:u8 + rsvd:u8。
    /// / 8B: unix_seconds:u32 (0=unset) + millis:u16 + synced:u8 + rsvd:u8.
    PMU_CMD_TIME_GET                = 0x0007,
    /// payload: mode:u8（0=关 1=单次 2=循环）+ seconds:u32 LE（10s..31天）。
    /// 到点推 ALARM_FIRED；主机关机则自动开机（wake_reason=6）。
    /// RTC 走时，CW 深睡不影响；CW 断电/复位后闹钟丢失。
    /// / mode:u8 (0=off 1=once 2=repeat) + seconds:u32 LE (10s..31d).
    /// Fires ALARM_FIRED; powers the host on if it is off (wake_reason=6).
    /// RTC keeps time through CW deep sleep; lost on CW power-cut/reset.
    PMU_CMD_ALARM_SET               = 0x0008,
    /// 响应 10B：mode:u8 + rsvd:u8 + remaining_sec:u32 + target_unix:u32。
    /// / 10B: mode:u8 + rsvd:u8 + remaining_sec:u32 + target_unix:u32.
    PMU_CMD_ALARM_GET               = 0x0009,
    /// 排障用。响应 36B：9×u32 LE，依次为 RTC_CR0、RTC_CR1、RTC_CR2、RTC_PSC、
    /// RTC_DATE、RTC_TIME、RTC_SSCNT、SYSCTRL_CR1、SYSCTRL_LSI。
    /// / Debug. 36B: 9×u32 LE of those raw registers.
    PMU_CMD_RTC_RAW                 = 0x000A,
    PMU_CMD_BATTERY_SAMPLE          = 0x0100,
    PMU_CMD_BATTERY_SET_PROFILE     = 0x0101,
    /// LED_SET: color:u8, brightness:u8, [fade_ms:u16 LE]。
    /// 2 字节默认淡入淡出；4 字节 fade_ms=0 立刻切。brightness 0=灭。
    /// / color:u8, brightness:u8, [fade_ms:u16 LE].
    /// 2-byte payload uses the default fade; 4-byte fade_ms=0 is instant. brightness 0=off.
    PMU_CMD_LED_SET                 = 0x0200,
    PMU_CMD_LED_OVERRIDE_CLEAR      = 0x0203,
    PMU_CMD_ACTION_PREPARE          = 0x0300,
    PMU_CMD_ACTION_COMMIT           = 0x0301,
    PMU_CMD_ACTION_CANCEL           = 0x0302,
    PMU_CMD_SHUTDOWN_READY          = 0x0303,
    PMU_CMD_HOST_SOFT_SLEEP         = 0x0310,
    PMU_CMD_HOST_REQUEST_OFF        = 0x0311,
    PMU_CMD_HOST_REQUEST_RESET      = 0x0312,
    PMU_CMD_EVENTS_CLEAR_ALL        = 0x0313,
    PMU_CMD_CONFIG_GET              = 0x0500,
    /// payload: enable:u8（0/1）。RAM 保存，CW 复位后恢复默认关闭。
    /// / enable:u8 (0/1). RAM only; CW reset restores the default off.
    PMU_CMD_CONFIG_SET_WAKE_ON_CHARGE = 0x0501,
    /// payload: enable:u8 [threshold_permille:u16 LE]。默认关。
    /// SOC 低于阈值且未充电时推送 BATTERY_SOC_LOW；HOST_READY 时若仍低会补发。
    /// / enable:u8 [threshold_permille:u16 LE]. Off by default.
    /// Fires BATTERY_SOC_LOW when SOC is under the threshold and not charging;
    /// HOST_READY re-sends if still low.
    PMU_CMD_CONFIG_SET_LOW_SOC_NOTIFY = 0x0502,
    /// payload: enable:u8。0=不上报 KEY_DOWN/KEY_UP（SHORT/LONG/FORCE_OFF 不受影响）。默认开。
    /// / enable:u8. 0 hides KEY_DOWN/KEY_UP. SHORT/LONG/FORCE_OFF stay. Default on.
    PMU_CMD_CONFIG_SET_KEY_EVENTS   = 0x0503,
    /// payload: enable:u8。0=关闭充电呼吸灯与充满常亮。默认开。
    /// / enable:u8. 0 disables charge-breathing and full-on LED. Default on.
    PMU_CMD_CONFIG_SET_CHARGE_LED   = 0x0504,
    PMU_CMD_CONFIG_FACTORY_DEFAULT  = 0x0505,
    /// 面板 VCOM（绝对值 mV）。未标定 GET 的 valid=0。
    /// GET 响应 4B：vcom_mv:u16 LE + valid:u8（1=500..2500）+ rsvd:u8。
    /// SET payload：mv:u16 LE，500..2500 且整十。只在开机标定页写一次。
    /// / Panel VCOM (absolute mV). GET valid=0 when unset.
    /// GET 4B: vcom_mv:u16 LE + valid:u8 (1=500..2500) + rsvd:u8.
    /// SET: mv:u16 LE, 500..2500 and a multiple of 10. Factory setup page only.
    PMU_CMD_VCOM_GET                = 0x0510,
    PMU_CMD_VCOM_SET                = 0x0511,
    /// 公开只读。响应至少 10B：芯片出厂 UID[10]，可当设备序列号。
    /// / Public read. At least 10B: factory UID[10], usable as a serial.
    PMU_CMD_MAINT_GET_INFO          = 0x05F2
};

#define PMU_CHIP_UID_LEN 10

enum pmu_status_code {
    PMU_STATUS_OK                   = 0x0000,
    PMU_STATUS_ACCEPTED             = 0x0001,
    PMU_STATUS_BUSY                 = 0x0002,
    PMU_STATUS_BAD_CRC              = 0x0010,
    PMU_STATUS_BAD_MAGIC            = 0x0011,
    PMU_STATUS_BAD_LENGTH           = 0x0012,
    PMU_STATUS_UNSUPPORTED_VERSION  = 0x0013,
    PMU_STATUS_UNKNOWN_COMMAND      = 0x0014,
    PMU_STATUS_INVALID_ARGUMENT     = 0x0015,
    PMU_STATUS_SEQUENCE_CONFLICT    = 0x0016,
    PMU_STATUS_STALE_SESSION        = 0x0017,
    PMU_STATUS_NOT_SUPPORTED        = 0x0018,
    PMU_STATUS_INVALID_STATE        = 0x0020,
    PMU_STATUS_NOT_ARMED            = 0x0021,
    PMU_STATUS_TOKEN_EXPIRED        = 0x0022,
    PMU_STATUS_PERMISSION_DENIED    = 0x0023,
    PMU_STATUS_CONFIG_INVALID       = 0x0030,
    PMU_STATUS_NVM_FAILURE          = 0x0031,
    PMU_STATUS_ADC_FAILURE          = 0x0040,
    PMU_STATUS_HOST_TIMEOUT         = 0x0041,
    PMU_STATUS_INTERNAL_ERROR       = 0x00FF
};

enum pmu_action {
    PMU_ACTION_HOST_LOGICAL_OFF         = 1,
    PMU_ACTION_HOST_HARD_RESET          = 2,
    PMU_ACTION_HOST_NORMAL_RESTART      = 3,
    PMU_ACTION_HOST_ENTER_DOWNLOAD_MODE = 4,
    PMU_ACTION_HOST_EXIT_DOWNLOAD_MODE  = 5,
    PMU_ACTION_HOST_POWER_ON            = 6,
    PMU_ACTION_HOST_SOFT_SLEEP          = 7
};

enum pmu_event_type {
    PMU_EVT_KEY_DOWN              = 0x01,
    PMU_EVT_KEY_UP                = 0x02,
    PMU_EVT_KEY_SHORT             = 0x03,
    PMU_EVT_KEY_LONG              = 0x04,
    PMU_EVT_KEY_FORCE_OFF         = 0x05,
    PMU_EVT_CHARGE_STATE_CHANGED  = 0x10,
    PMU_EVT_BATTERY_SAMPLE_READY  = 0x11,
    PMU_EVT_BATTERY_LOW           = 0x12,
    PMU_EVT_BATTERY_CRITICAL      = 0x13,
    ///< arg0=当前 soc_permille，arg1=阈值。/ arg0=soc_permille, arg1=threshold.
    PMU_EVT_BATTERY_SOC_LOW       = 0x14,
    ///< RTC 闹钟到点。arg0=mode（1=单次已停 2=循环已重排）。/ Alarm. arg0=mode (1=once 2=repeat).
    PMU_EVT_ALARM_FIRED           = 0x15,
    PMU_EVT_HOST_STARTED          = 0x20,
    PMU_EVT_HOST_READY            = 0x21,
    PMU_EVT_SHUTDOWN_REQUESTED    = 0x22,
    PMU_EVT_HOST_RESET_PERFORMED  = 0x24,
    PMU_EVT_COMMAND_COMPLETED     = 0x30,
    PMU_EVT_CONFIG_RECOVERED      = 0x40,
    PMU_EVT_CW_RESET              = 0x41,
    PMU_EVT_OVERFLOW              = 0x7F
};

enum pmu_severity {
    PMU_SEV_INFO = 0,
    PMU_SEV_NOTICE = 1,
    PMU_SEV_WARNING = 2,
    PMU_SEV_CRITICAL = 3
};

enum pmu_led_color {
    PMU_LED_OFF = 0,
    PMU_LED_RED = 1,
    PMU_LED_WHITE = 2,
    PMU_LED_RED_WHITE = 3
};

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t header_version;
    uint8_t kind;
    uint8_t flags;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint16_t sequence;
    uint16_t code;
    uint16_t status;
    uint8_t payload_length;
    uint8_t reserved;
    uint32_t session_id;
    uint8_t payload[44];
    uint16_t crc16;
} pmu_frame_t;

typedef struct __attribute__((packed)) {
    uint16_t event_id;
    uint8_t type;
    uint8_t severity;
    uint32_t timestamp_ms;
    uint32_t arg0;
    uint16_t arg1;
    uint16_t crc16;
} pmu_event_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(pmu_frame_t) == 64, "pmu_frame_t size");
_Static_assert(sizeof(pmu_event_t) == 16, "pmu_event_t size");
#endif

uint16_t pmu_crc16_ccitt_false(const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif
