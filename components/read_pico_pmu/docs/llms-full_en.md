# Read Pico power management · host integration (firmware 1.0.8 / protocol 1.1)

Read Pico is an ESP32-S3 development board with a 4.7" monochrome e-paper panel, made by Shenzhen MindReset Technology Co., Ltd. for developers who want to write open-source firmware for e-paper devices. This file is the host-side note for the on-board power-management chip, read_pico_pmu.

Companions in this folder: `pmu_protocol_en.h`, `pmu_registers_en.json`. Print: `read_pico_pmu_datasheet_v1.0.8_en.pdf`.

## Summary

- Chip read_pico_pmu, on Read Pico. Silicon: Wuhan Xinyuan CW32L010F8U6, QFN20 3.0×3.0 mm. OTP, no OTA / IAP.
- I2C 7-bit 0x2A (write 0x54, read 0x55). 400 kHz, 100 kHz compatible. Little-endian.
- device_id 0x3201. IDENTITY magic ASCII "PMU1". Protocol 1.1. Firmware 1.0.8. build_hash 20260905. hw_revision 1.
- Registers: 0x00 IDENTITY 32R; 0x20 STATUS 64R; 0x60 DIAG 32R; 0x80 CMD 64W; 0x81 RESP 64R; 0x82 PEEK 16R; 0x83 ACK 4W; 0x84 COUNT 1R; 0x85 QUICK_BATTERY 8R.
- Command and response are 64 bytes. Payload max 44. magic 0xA5. session_id must equal current boot_id. sequence must not be 0.
- CRC16-CCITT-FALSE: poly 0x1021, init 0xFFFF, refin/refout false, xorout 0. Vector "123456789" = 0x29B1. Covers bytes before crc.
- Event FIFO depth 8, 16 bytes each. CW_INT open-drain, low = unacked events. PEEK does not pop; ACK = event_id u16 + crc16 over those 2 bytes.
- Power state is STATUS.power_state, not the command return code. Charge state is charge_state, not voltage.
- Off / reset / download use PREPARE(0x0300)+COMMIT(0x0301). Token ~2 s. COMMIT delay cap 5 s.
- I2C does not respond in DeepSleep. After logical off or soft sleep the slave stops until the host is powered again. After cold start, a short press wakes a 15 s window while the slave still answers.
- VCOM_GET 0x0510 / VCOM_SET 0x0511. Absolute mV, 500–2500, 10 mV aligned. Uncalibrated valid=0, mv=0. Illegal → INVALID_ARGUMENT. FACTORY_DEFAULT restores RAM switches only.
- Readout lock via ISP. MAINT_GET_INFO (0x05F2) reads UID, cycles, replace count, repair codes, and lock counters.
- Host boot: read IDENTITY → store boot_id → consume HOST_STARTED → HOST_READY(boot_reason) → TIME_SYNC. sequence starts at last_command_sequence+1.

## Roles

read_pico_pmu stays powered on the board: power sequencing, battery, charge detect, key. ESP32-S3 host runs only when ESP_EN=1: app, UI, storage, commands, events, time, VCOM. Host reads results; it does not convert voltage or decode the charger pin.

## Pins (QFN20 top, counter-clockwise, pin-1 dot)

1 PB07/NRST; 2 PA00 ESP_EN active-high out; 3 PA01 CW_INT OD active-low; 4 VSS; 5 Vcore (100–470 nF to GND, do not supply); 6 VDD ~3.3 V; 7 PA02 BAT_MEASURE_EN; 8 PB00 CHARGE_STATUS; 9 PB01 unused; 10 PA03 BAT_ADC; 11 PA04 ESP_BOOT; 12 PA05 LED_RED PWM; 13 PA06 LED_WHITE PWM; 14 PA07 SWDIO / UART1_RXD (ISP); 15 PA08 SWCLK / UART1_TXD (ISP); 16 PB02 POWER_KEY active-low; 17 PB03 unused; 18 PB04 unused; 19 PB05 I2C_SDA; 20 PB06 I2C_SCL.

## Electrical and clocks

VDD 1.62–5.5 V, 3.3 V on this board. −40–85 °C. System clock internal HSI/6 = 8 MHz. LSI ~32 kHz for IWDT and RTC. No crystal. Flash 64 KB, last page 0xFE00. SRAM 4 KB. Silicon DeepSleep 0.3–1.2 µA typical. GPIO VIH 0.7 VDD, VIL 0.3 VDD.

## power_state

0 OFF, EN=0. 1 POWERING_ON. 2 BOOT_WAIT for HOST_READY; 15 s timeout only sets fault_flags bit0. 3 RUNNING. 4 SHUTDOWN_PENDING, force off after 10 s. 5 RESETTING, EN low 250 ms. 6 DOWNLOAD_MODE, BOOT=0 then reset, release BOOT after ~500 ms; HOST_READY returns to RUNNING. 7 FAULT reserved. 8 SOFT_SLEEP, STATUS first, EN=0 after ~100 ms. Next short press only raises EN.

### Soft sleep

RUNNING only. HOST_READY cancels the drop while EN is still high. HOST_READY after EN is low is discarded. Key or alarm power-on clears a pending request. DeepSleep is allowed after EN=0. Alarm power-on, wake_reason=6.

### Two-phase action

1 LOGICAL_OFF → SHUTDOWN_PENDING. 2 HARD_RESET. 3 NORMAL_RESTART, same as hard reset, BOOT=1. 4 ENTER_DOWNLOAD. 5 EXIT_DOWNLOAD then hard reset. 6 POWER_ON, OFF only, wake_reason=3. 7 SOFT_SLEEP, RUNNING only.

PREPARE: action u8 + delay_ms u16 + reason u16, at least 5 bytes. Response token u32 + expires_in_ms=2000 + echoed delay. Execution delay cap 5000 ms.

Convenience: 0x0310 soft sleep; 0x0311 off (force u8 + reason u16); 0x0312 reset (reason u16). Still idempotent.

Cooperative off: on SHUTDOWN_REQUESTED flush, send SHUTDOWN_READY(event_id), EN=0 ~100 ms later. Forced: hold 10 s or force=1. Battery ≤3050 mV starts cooperative off, reason=13.

IWDT 1 s overflow powers the host again, wake_reason=4. RTC keeps time; alarms and RAM config are lost.

last_action_reason: 10 = 10 s key; 11 = cooperative timeout; 12 = SHUTDOWN_READY done; 13 = low battery; 20 = key download.

## Key

Debounce 30 ms. EN=0 hold ≥1 s powers on; valid voltage ≤3050 mV and not charging refuses, red blinks twice. EN=0 in SOFT_SLEEP (or RUNNING with EN already low): short press powers on. EN=1 hold ≥10 s forced off. EN=1 two shorts (gap ≤1.2 s) then hold ≥3 s enters download; the two shorts still report KEY_SHORT; the third does not report KEY_LONG. Short / 1.5 s / 8 s are events only. KEY_DOWN / KEY_UP can be disabled with 0x0503.

key_state: bit0 pressed; bit1 past 1.5 s; bit2 past 8 s; bit3 download combo has two shorts.

## Battery

Divider 33 kΩ / 100 kΩ, ratio 1.33. Wait 10 ms after BAT_MEASURE_EN. VDDA from internal bandgap. Valid 2700–4400 mV. Three failures clear BATTERY_VALID. Period: ~30 s running, ~120 s off idle, ~60 s off charging. First valid voltage within ~800 ms. Immediate sample 0x0100 ACCEPTED then event 0x11.

Thresholds: low 3500 mV event 0x12; critical 3300 mV event 0x13; forced off 3050 mV; hysteresis 100 mV. CONFIG full_battery_mv=4150 is not used as full. On-chip temp 0.01 °C, −40–125 °C.

SOC table (permille): 4200=1000, 4100=960, 4000=900, 3900=780, 3800=620, 3700=450, 3650=330, 3600=200, 3550=110, 3450=70, 3300=50, 3150=20, 3050=0, linear interpolate. Monotonic down while not charging. Hold pre-charge value while charging; 1000 when full inferred. Rebase: charge end or jump >500 mV. About ±10 points, for a battery bar. Low-SOC notify off by default, 50‰, 30‰ recovery hysteresis. Equivalent cycle +1 per 1000‰ discharged.

## Charge LY4566-420

charge_state: 0 UNKNOWN; 1 NOT_CHARGING; 2 CHARGING (present, edge within 2 s); 3 FULL_INFERRED (present, no edge for 3 s); 4 FAULT reserved. Sample 20 ms; 100 ms when off and unplugged. Unplug threshold = clamp(last high half×1.5+200, 1100, 1800) ms, else 1400. No unplug for 2.5 s after EN fall; no full for 2.5 s after EN rise. Event 0x10 arg0=new, arg1=old. 0x0501 enables auto power-on from OFF, wake_reason=5. RAM only, default off.

## LED (high to low priority)

Download: 0.15 s × 7, off-white-off-red-off-white-off. Shutdown: red blink 150 ms × 4. Power-on: red 150, off 150, white 150, off. Host LED_SET default 5 s, or boot white 250 ms / reset red 250 ms. Charging: red breathe 2.6 s, dark 0.3 s. Full: white solid (hidden in BOOT_WAIT). Key held: red solid. EN=0 and not charging: off. 0x0504=0 disables charge LED. LED_SET: color 0/1/2/3, brightness 0 off else on; 2-byte fade 400 ms; 4-byte fade_ms, 0 instant, max 4000. led_state: bit0 red, bit1 white, bit2 breathe, bit3 host override, bit4 charge policy. Charge LED can cover LED_SET.

## Low power

Normal Sleep: I2C responds. DeepSleep: power_state OFF or SOFT_SLEEP, EN=0, not charging, key and LED idle, no pending command, ≥15 s since last command and I2C. Wake: key edge, charger insert, RTC alarm. Invalid wake sleeps again. IWDT paused. DIAG offset 14 is DeepSleep entries.

No I2C in DeepSleep. After cold start, before the slave stops, a short press wakes a 15 s window. After logical off, soft sleep, or a reset pulse the slave stops; it answers again after the host is powered.

## Time

TIME_SYNC 0x0005: UTC unix seconds 946684800–4102444799. Does not stretch SCL. TIME_GET 0x0007: unix u32 (0 if never synced) + millis u16 (always 0) + synced u8 + reserved. synced=0 after reset. Alarm remaining seconds stay when the target is moved. ALARM_SET: mode 0/1/2 + seconds 10–2678400. ALARM_GET: 10 bytes. RAM only. RTC_RAW 0x000A: 9×u32. Event timestamp_ms is PMU uptime, not UTC. LSI about ±1–2%; sync regularly.

## I2C

Read: START + 0x54 + REG + RESTART + 0x55 + N + STOP. Write-then-stop then read also works. Write: START + 0x54 + REG + PAYLOAD + STOP. Timeout 20 ms, backoff 2 / 5 / 20 ms, 3 tries. Stuck bus: up to 9 SCL then STOP. Slave self-recovers after ~100 ms idle. Serialize PMU access in one task; do not split a frame.

## IDENTITY 0x00 (32)

0 u8[4] PMU1; 4 major=1; 5 minor=1; 6 fw 1; 7 fw 0; 8 patch 8; 9 hw 1; 10 u16 device 0x3201; 12 u32 boot_id (never 0); 16 u32 caps bits 0–5; 20 max_frame=64; 21 fifo=8; 22 u16 build_number = low 16 of 20260905 (0x4F09); 24 u32 build_hash=20260905; 28 u16 0; 30 crc16 over 0–29.

boot_id changes on PMU reset, not on ESP32-only reset. Drop saved sequence and token; re-read IDENTITY, STATUS, and events.

## STATUS 0x20 (64)

0 u16 generation; 2 u8 power_state; 3 u8 wake_reason; 4 u32 flags; 8 u16 battery_mv; 10 u16 adc_raw; 12 u16 soc_permille; 14 i16 temp_centi; 16 u8 charge_state; 17 u8 key_state; 18 u8 led_state; 19 u8 host_state (0 unknown, 1 off, 2 starting, 3 ready, 4 soft sleep, 5 shutting down, 6 fault); 20 u32 fault_flags bit0 BOOT_WAIT timeout sticky; 24 u8 pending_events; 25 u8 cw_reset_reason; 26 u8 0; 27 u8 last_command_status; 28 u32 uptime_ms; 32 u32 0; 36 u32 config_generation; 40 u16 last_command_sequence; 42 u16 last_event_id; 44 u32 last_action_reason; 48–61 0; 62 crc16 over 0–61.

flags: 0 BATTERY_VALID; 1 LOW; 2 CRITICAL; 3 CHARGING_ACTIVE (charge_state==2); 4 CHARGE_PIN_HIGH; 5 KEY; 6 EN_HIGH; 7 BOOT_LOW; 8 EVENT_PENDING; 9 COMMAND_BUSY; 10 CONFIG_VALID (always 1); 13 CW_INT_ASSERTED.

## QUICK_BATTERY 0x85 (8)

0 u16 mv; 2 u16 soc, 0xFFFF if invalid; 4 u8 charge_state; 5 flags: bit0 valid, bit1 soc_valid, bit2 charging; 6 crc16 over 0–5.

## DIAGNOSTICS 0x60 (32)

u16 fields, saturate at 0xFFFF. CLEAR_DIAGNOSTICS zeros first 30 bytes. 0 i2c_rx; 2 bus recovery; 4 crc; 6 bad_len; 8 unknown_cmd; 10 reserved 0; 12 overflow; 14 deepsleep; 16 host_reset; 18 reserved 0 (IWDT is STATUS.cw_reset_reason==4); 20 adc; 22 0; 24 reserved 0; 26 reserved 0; 28 0; 30 crc16 over 0–29.

## Command frame (64)

0 magic 0xA5; 1 header_version 1; 2 kind request 0 / response 1; 3 flags; 4 proto major; 5 proto minor; 6 u16 sequence; 8 u16 opcode; 10 u16 status (0 in request); 12 u8 payload_length 0–44; 13 reserved; 14 u32 session_id; 18 payload[44]; 62 crc16 over 0–61.

Idempotency (session_id, sequence). Same request returns cache. Same key, different opcode/payload → SEQUENCE_CONFLICT. Cache 4. Cached HOST_READY still runs if not RUNNING. Off/reset/download are not re-executed. Lost response: same sequence again.

## Status codes

0x0000 OK. 0x0001 ACCEPTED, then STATUS or event. 0x0002 BUSY reserved. 0x0010 BAD_CRC. 0x0011 BAD_MAGIC. 0x0012 BAD_LENGTH. 0x0013 UNSUPPORTED_VERSION. 0x0014 UNKNOWN_COMMAND. 0x0015 INVALID_ARGUMENT. 0x0016 SEQUENCE_CONFLICT. 0x0017 STALE_SESSION. 0x0018 NOT_SUPPORTED. 0x0020 INVALID_STATE. 0x0021 NOT_ARMED. 0x0022 TOKEN_EXPIRED. 0x0023 PERMISSION_DENIED. 0x0030 CONFIG_INVALID. 0x0031 NVM_FAILURE. 0x0040 ADC_FAILURE. 0x0041 HOST_TIMEOUT. 0x00FF INTERNAL_ERROR.

Sample failure still ACCEPTED; result is an event. BOOT_WAIT timeout only sets the fault bit.

## Events (16)

0 u16 event_id from 1, skip 0; 2 type; 3 severity 0 INFO, 1 NOTICE, 2 WARNING, 3 CRITICAL; 4 u32 timestamp_ms; 8 u32 arg0; 12 u16 arg1; 14 crc16 over 0–13.

Full FIFO drops INFO then NOTICE. ACK one by one; do not use EVENTS_CLEAR_ALL day-to-day.

0x01 KEY_DOWN. 0x02 KEY_UP, arg0 hold ms. 0x03 KEY_SHORT. 0x04 KEY_LONG. 0x05 KEY_FORCE_OFF (no auto off). 0x10 CHARGE, new / old. 0x11 SAMPLE, mV / raw. 0x12 LOW. 0x13 CRITICAL. 0x14 SOC_LOW, ‰ / threshold. 0x15 ALARM, mode. 0x20 HOST_STARTED, wake_reason. 0x21 HOST_READY, ms / boot_reason. 0x22 SHUTDOWN_REQUESTED, reason / deadline_ms. 0x24 HOST_RESET, reason / count. 0x30 COMMAND_COMPLETED, sequence / status. 0x40 CONFIG_RECOVERED, generation / reason. 0x41 CW_RESET, reset_reason / boot_id low 16. 0x7F OVERFLOW, lost.

Collect: CW_INT low → read 0x84 → PEEK, handle, ACK until count=0. Still read STATUS once per second.

## Commands

0x0001 PING. 0x0002 GET_EXTENDED_INFO, 6 bytes. 0x0004 HOST_READY, boot_reason u8; from BOOT_WAIT / POWERING_ON / RESETTING / RUNNING / DOWNLOAD_MODE / SOFT_SLEEP while EN high → RUNNING; ignored after EN is low. READY before soft sleep if sent together. 0x0005 TIME_SYNC unix u32. 0x0006 CLEAR_DIAGNOSTICS first 30 bytes. 0x0007 TIME_GET 8 bytes. 0x0008 ALARM_SET. 0x0009 ALARM_GET 10 bytes. 0x000A RTC_RAW 36 bytes.

0x0100 BATTERY_SAMPLE ACCEPTED. 0x0101 BATTERY_SET_PROFILE NOT_SUPPORTED. 0x0200 LED_SET. 0x0203 LED_OVERRIDE_CLEAR.

0x0300 PREPARE. 0x0301 COMMIT token u32 ACCEPTED. 0x0302 CANCEL. 0x0303 SHUTDOWN_READY event_id u16. 0x0310 SOFT_SLEEP, else INVALID_STATE. 0x0311 REQUEST_OFF. 0x0312 REQUEST_RESET. 0x0313 EVENTS_CLEAR_ALL.

0x0500 CONFIG_GET 36 bytes (3500 / 3300 / 1500 / 8000 / 1000 / 10000 / 4150 / generation / 3050 / 100 / 30 / 15000 / 10000 / 5000 + four switches + low-SOC threshold). 0x0501 WAKE_ON_CHARGE. 0x0502 LOW_SOC enable [+ 1–1000]. 0x0503 KEY_EVENTS. 0x0504 CHARGE_LED. 0x0505 FACTORY_DEFAULT restores RAM switches, pushes CONFIG_RECOVERED, does not change VCOM or the page. After reset: wake=0, key_raw=1, charge_led=1, low_soc=0, threshold=50.

0x0510 VCOM_GET 4 bytes: mv + valid + 0. 0x0511 VCOM_SET mv; illegal → INVALID_ARGUMENT, echo 2 bytes. Range 500–2500, 10 mV aligned.

0x05F2 MAINT_GET_INFO 36 bytes: uid[10] + cycle u16 + replace u16 + accum u16 + repair[16] + readout_level + used + remaining + 0.

## Maintenance page (0xFE00, 32-byte record)

0 u32 magic 0x314D5650 ("PVM1"); 4 u16 version=1; 6 cycle; 8 replace; 10 accum 0–999; 12 repair[16]; 28 vcom_mv; 30 crc16 over 0–29. Whole-page erase/write. FACTORY_DEFAULT does not change this page.

## Reset and wake

cw_reset_reason: 0 unknown, 1 POR, 2 brown-out, 3 NRST, 4 IWDT, 5 software, 6 LOCKUP, 7 clock. This firmware: IWDT→4, POR→1, NRST→3, else 0.

wake_reason: 0 unknown, 1 cold-start off, 2 key, 3 host request, 4 IWDT recovery, 5 charger (must enable), 6 alarm.

## Production

Program SWD (PA07 / PA08). Image read_pico_pmu_v1.0.8_build20260905.hex. Power-on: red then white, 0.6 s.

Readout lock via ISP only (CW_Programmer + CW-Writer), level 1. Enter ISP: hold reset, ~50 kHz on SWDIO, release and wait 5 ms. Downgrade is full-chip erase and clears the page.

## Host boot and shutdown

1. Read 0x00, check CRC and "PMU1".
2. session_id = boot_id. sequence = STATUS.last_command_sequence+1 (or 1 if 0).
3. Drain events until count=0.
4. HOST_READY, wait power_state==RUNNING.
5. TIME_SYNC. VCOM_GET if needed.
6. Loop: CW_INT or STATUS once per second; battery 0x85 or 0x20.
7. Shutdown: flush → PREPARE LOGICAL_OFF → COMMIT (optional delay), or REQUEST_OFF → wait EN=0.
8. If boot_id changes, drop cache and return to step 1. Old-session commands return STALE_SESSION.

Hold 8 s is KEY_FORCE_OFF only; hold 10 s forces power-off. TIME_SYNC out of range → INVALID_ARGUMENT.
