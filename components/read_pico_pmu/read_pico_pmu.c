/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CW32 PMU 主机：64 字节带 CRC 的帧、序号恢复、超时后复位 I2C。
 *
 * CW32 PMU host: 64-byte CRC frames, sequence recovery, I2C reset on timeout.
 */

#include "read_pico_pmu.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "pmu"

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static pmu_snapshot_t s_snap;
static uint16_t s_seq = 1;
static bool s_ready;
static uint8_t s_last_payload[PMU_FRAME_PAYLOAD_SIZE];
static uint8_t s_last_plen;

/* ---- 帧收发 / Frame I/O ---- */
uint16_t pmu_crc16_ccitt_false(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    while (length--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool crc_ok(const uint8_t* data, size_t cover, uint16_t expect) {
    return pmu_crc16_ccitt_false(data, (uint16_t)cover) == expect;
}

// I2C 超时后复位总线，避免 SCL 被钳死。/ Reset the bus after I2C timeout so SCL is not held.
static void pmu_bus_recover(esp_err_t err) {
    if (err != ESP_ERR_TIMEOUT) return;
    if (s_bus != NULL) {
        (void)i2c_master_bus_reset(s_bus);
    }
}

// CW32 写寄存器地址后 STOP/Repeated START 都会锁存 s_reg。
// After a register write, both STOP and Repeated START latch s_reg on the CW32.
static esp_err_t pmu_read_reg(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 100);
    if (err == ESP_OK) return ESP_OK;
    pmu_bus_recover(err);
    err = i2c_master_transmit(s_dev, &reg, 1, 100);
    if (err != ESP_OK) {
        pmu_bus_recover(err);
        return err;
    }
    esp_rom_delay_us(50);
    err = i2c_master_receive(s_dev, data, len, 100);
    if (err != ESP_OK) pmu_bus_recover(err);
    return err;
}

static esp_err_t pmu_write_reg(uint8_t reg, const uint8_t* data, size_t len) {
    uint8_t buf[1 + PMU_FRAME_SIZE];
    if (len > PMU_FRAME_SIZE) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    if (len > 0 && data != NULL) memcpy(&buf[1], data, len);
    esp_err_t err = i2c_master_transmit(s_dev, buf, 1 + len, 100);
    if (err != ESP_OK) pmu_bus_recover(err);
    return err;
}

static bool parse_identity(const uint8_t* raw) {
    if (raw[0] != 'P' || raw[1] != 'M' || raw[2] != 'U' ||
        !crc_ok(raw, 30, rd16(&raw[30]))) {
        return false;
    }
    s_snap.proto_major = raw[4];
    s_snap.proto_minor = raw[5];
    s_snap.fw_major = raw[6];
    s_snap.fw_minor = raw[7];
    s_snap.fw_patch = raw[8];
    s_snap.hw_rev = raw[9];
    s_snap.device_id = rd16(&raw[10]);
    s_snap.boot_id = rd32(&raw[12]);
    s_snap.caps = rd32(&raw[16]);
    s_snap.build_number = rd16(&raw[22]);
    s_snap.build_hash = rd32(&raw[24]);
    return true;
}

// CRC 不对则不改快照：STATUS 影子更新时 I2C 会读到半新半旧。
// Leave the snapshot alone on a bad CRC: STATUS updates can tear mid-read.
static bool parse_status(const uint8_t* raw) {
    if (!crc_ok(raw, 62, rd16(&raw[62]))) return false;
    s_snap.generation = rd16(&raw[0]);
    s_snap.power_state = raw[2];
    s_snap.wake_reason = raw[3];
    s_snap.flags = rd32(&raw[4]);
    s_snap.battery_mv = rd16(&raw[8]);
    s_snap.battery_raw = rd16(&raw[10]);
    s_snap.soc_permille = rd16(&raw[12]);
    s_snap.mcu_temp_centi = (int16_t)rd16(&raw[14]);
    s_snap.charge_state = raw[16];
    s_snap.key_state = raw[17];
    s_snap.led_state = raw[18];
    s_snap.host_state = raw[19];
    s_snap.fault_flags = rd32(&raw[20]);
    s_snap.pending_events = raw[24];
    s_snap.reset_reason = raw[25];
    s_snap.last_cmd_status = raw[27];
    s_snap.uptime_ms = rd32(&raw[28]);
    s_snap.config_generation = rd32(&raw[36]);
    s_snap.last_cmd_seq = rd16(&raw[40]);
    s_snap.last_event_id = rd16(&raw[42]);
    s_snap.last_action_reason = rd32(&raw[44]);
    return true;
}

static void parse_diag(const uint8_t* raw) {
    s_snap.diag_ok = crc_ok(raw, 30, rd16(&raw[30]));
    s_snap.diag_i2c_rx = rd16(&raw[0]);
    s_snap.diag_i2c_recov = rd16(&raw[2]);
    s_snap.diag_crc = rd16(&raw[4]);
    s_snap.diag_bad_len = rd16(&raw[6]);
    s_snap.diag_unknown = rd16(&raw[8]);
    s_snap.diag_overflow = rd16(&raw[12]);
    s_snap.diag_sleep = rd16(&raw[14]);
    s_snap.diag_host_rst = rd16(&raw[16]);
    s_snap.diag_adc = rd16(&raw[20]);
}

static void parse_quick(const uint8_t* raw) {
    if (!crc_ok(raw, 6, rd16(&raw[6]))) return;
    s_snap.qb_mv = rd16(&raw[0]);
    s_snap.qb_soc = rd16(&raw[2]);
    s_snap.qb_charge = raw[4];
    s_snap.qb_flags = raw[5];
}

static void parse_event(const uint8_t* raw) {
    memcpy(&s_snap.event, raw, sizeof(s_snap.event));
    s_snap.event_ok =
        s_snap.event.event_id != 0 &&
        crc_ok(raw, 14, s_snap.event.crc16);
}

static void parse_config(const uint8_t* p) {
    s_snap.config.low_mv = rd16(&p[0]);
    s_snap.config.critical_mv = rd16(&p[2]);
    s_snap.config.key_long_ms = rd16(&p[4]);
    s_snap.config.key_force_event_ms = rd16(&p[6]);
    s_snap.config.key_power_on_ms = rd16(&p[8]);
    s_snap.config.key_force_off_ms = rd16(&p[10]);
    s_snap.config.full_mv = rd16(&p[12]);
    s_snap.config.generation = rd32(&p[14]);
    s_snap.config.forced_off_mv = rd16(&p[18]);
    s_snap.config.hysteresis_mv = rd16(&p[20]);
    s_snap.config.key_debounce_ms = rd16(&p[22]);
    s_snap.config.host_boot_timeout_ms = rd16(&p[24]);
    s_snap.config.shutdown_timeout_ms = rd16(&p[26]);
    s_snap.config.led_override_timeout_ms = rd16(&p[28]);
    s_snap.config.wake_on_charge = p[30];
    s_snap.config.key_raw_events = p[31];
    s_snap.config.charge_led = p[32];
    s_snap.config.low_soc_enable = p[33];
    s_snap.config.low_soc_threshold = rd16(&p[34]);
    s_snap.config_ok = true;
}

// USB 只复位 ESP 时 boot_id 不变，本地 seq 从 last_command_sequence+1 起。
// A USB reset of the ESP keeps boot_id; resume seq from last_command_sequence+1.
static void recover_seq(void) {
    uint8_t ident[PMU_IDENTITY_SIZE];
    uint8_t status[PMU_STATUS_SIZE];
    if (pmu_read_reg(PMU_REG_IDENTITY, ident, sizeof(ident)) == ESP_OK) {
        parse_identity(ident);
        s_snap.present = true;
    }
    if (pmu_read_reg(PMU_REG_STATUS, status, sizeof(status)) == ESP_OK) {
        parse_status(status);
    }
    uint32_t next = (uint32_t)s_snap.last_cmd_seq + 1U;
    if (next == 0 || next > 0xFFFF) next = 1;
    if (s_seq != (uint16_t)next) {
        ESP_LOGI(
            TAG, "seq recover %u -> %u last=%u pwr=%s",
            (unsigned)s_seq, (unsigned)next, (unsigned)s_snap.last_cmd_seq,
            pmu_power_name(s_snap.power_state)
        );
        s_seq = (uint16_t)next;
    }
}

// 填请求帧并推进序号（跳过 0）。/ Fill a request and bump the sequence (skip 0).
static void fill_req(pmu_frame_t* req, uint16_t code, const uint8_t* payload, uint8_t plen) {
    memset(req, 0, sizeof(*req));
    req->magic = PMU_FRAME_MAGIC;
    req->header_version = 1;
    req->kind = PMU_FRAME_KIND_REQ;
    req->protocol_major = PMU_PROTOCOL_MAJOR;
    req->protocol_minor = PMU_PROTOCOL_MINOR;
    req->sequence = s_seq++;
    if (s_seq == 0) s_seq = 1;
    req->code = code;
    req->payload_length = plen;
    req->session_id = s_snap.boot_id;
    if (payload != NULL && plen > 0) memcpy(req->payload, payload, plen);
    req->crc16 = pmu_crc16_ccitt_false((const uint8_t*)req, 62);
}

static esp_err_t read_response(pmu_frame_t* resp) {
    uint8_t raw[PMU_FRAME_SIZE];
    esp_err_t err = pmu_read_reg(PMU_REG_RESPONSE, raw, sizeof(raw));
    if (err != ESP_OK) return err;
    memcpy(resp, raw, sizeof(*resp));
    if (resp->magic != PMU_FRAME_MAGIC) return ESP_ERR_INVALID_RESPONSE;
    if (!crc_ok(raw, 62, resp->crc16)) return ESP_ERR_INVALID_CRC;
    return ESP_OK;
}

// 等到对应序号的响应，最多 5 次。/ Wait for the matching sequence, up to 5 reads.
static esp_err_t read_response_for(uint16_t seq, pmu_frame_t* resp) {
    for (int i = 0; i < 5; i++) {
        esp_err_t err = read_response(resp);
        if (err != ESP_OK) return err;
        if (resp->sequence == seq) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_INVALID_RESPONSE;
}

// 发一帧命令。序号冲突或过期会话就恢复序号再试。
// Send one command. Recover seq and retry on conflict or a stale session.
esp_err_t read_pico_pmu_cmd(uint16_t code, const uint8_t* payload, uint8_t plen) {
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;

    pmu_frame_t req;
    pmu_frame_t resp;
    memset(&resp, 0, sizeof(resp));
    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) recover_seq();

        fill_req(&req, code, payload, plen);
        err = pmu_write_reg(PMU_REG_COMMAND, (const uint8_t*)&req, sizeof(req));
        s_snap.last_err = err;
        s_snap.last_op_code = code;
        if (err != ESP_OK) return err;

        vTaskDelay(pdMS_TO_TICKS(30));
        err = read_response_for(req.sequence, &resp);
        if (err != ESP_OK) {
            s_snap.last_err = err;
            if (err == ESP_ERR_TIMEOUT) break;
            continue;
        }
        if (resp.status == PMU_STATUS_STALE_SESSION ||
            resp.status == PMU_STATUS_SEQUENCE_CONFLICT) {
            s_snap.last_op_status = resp.status;
            ESP_LOGW(
                TAG, "cmd 0x%04X %s seq=%u, retry",
                (unsigned)code, pmu_status_name(resp.status), (unsigned)req.sequence
            );
            continue;
        }
        break;
    }

    s_snap.last_err = err;
    if (err != ESP_OK) return err;
    s_snap.last_op_status = resp.status;
    s_last_plen = resp.payload_length;
    if (s_last_plen > sizeof(s_last_payload)) s_last_plen = sizeof(s_last_payload);
    memcpy(s_last_payload, resp.payload, s_last_plen);

    if (code == PMU_CMD_CONFIG_GET && resp.status == PMU_STATUS_OK) {
        parse_config(resp.payload);
    } else if (code == PMU_CMD_TIME_GET && resp.status == PMU_STATUS_OK) {
        s_snap.unix_sec = rd32(&resp.payload[0]);
        s_snap.time_synced = resp.payload[6];
        s_snap.time_ok = true;
    } else if (code == PMU_CMD_ALARM_GET && resp.status == PMU_STATUS_OK) {
        s_snap.alarm_mode = resp.payload[0];
        s_snap.alarm_remain = rd32(&resp.payload[2]);
        s_snap.alarm_target = rd32(&resp.payload[6]);
        s_snap.alarm_ok = true;
    } else if (code == PMU_CMD_RTC_RAW && resp.status == PMU_STATUS_OK && resp.payload_length >= 36) {
        for (int i = 0; i < 9; i++) s_snap.rtc_raw[i] = rd32(&resp.payload[i * 4]);
        s_snap.rtc_raw_ok = true;
    } else if (code == PMU_CMD_MAINT_GET_INFO && resp.status == PMU_STATUS_OK
               && resp.payload_length >= PMU_CHIP_UID_LEN) {
        memcpy(s_snap.uid, resp.payload, PMU_CHIP_UID_LEN);
        s_snap.uid_ok = true;
    }
    return (resp.status == PMU_STATUS_OK || resp.status == PMU_STATUS_ACCEPTED)
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t read_pico_pmu_vcom_get(int* out_mv) {
    if (out_mv != NULL) *out_mv = 0;
    if (!s_ready) return ESP_ERR_NOT_FOUND;
    if (read_pico_pmu_cmd(PMU_CMD_VCOM_GET, NULL, 0) != ESP_OK) return ESP_ERR_NOT_FOUND;
    if (s_last_plen < 4 || s_last_payload[2] == 0) return ESP_ERR_NOT_FOUND;
    int mv = (int)rd16(s_last_payload);
    if (mv < 500 || mv > 2500 || (mv % 10) != 0) return ESP_ERR_NOT_FOUND;
    if (out_mv != NULL) *out_mv = mv;
    return ESP_OK;
}

esp_err_t read_pico_pmu_vcom_set(int mv) {
    if (!s_ready) return ESP_ERR_NOT_FOUND;
    if (mv < 500 || mv > 2500 || (mv % 10) != 0) return ESP_ERR_INVALID_ARG;
    uint8_t buf[2];
    wr16(buf, (uint16_t)mv);
    return read_pico_pmu_cmd(PMU_CMD_VCOM_SET, buf, sizeof(buf));
}

esp_err_t read_pico_pmu_uid_get(uint8_t out[PMU_CHIP_UID_LEN]) {
    if (out != NULL) memset(out, 0, PMU_CHIP_UID_LEN);
    if (!s_ready) return ESP_ERR_NOT_FOUND;
    if (s_snap.uid_ok) {
        if (out != NULL) memcpy(out, s_snap.uid, PMU_CHIP_UID_LEN);
        return ESP_OK;
    }
    if (read_pico_pmu_cmd(PMU_CMD_MAINT_GET_INFO, NULL, 0) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_snap.uid_ok) return ESP_ERR_NOT_FOUND;
    if (out != NULL) memcpy(out, s_snap.uid, PMU_CHIP_UID_LEN);
    return ESP_OK;
}

esp_err_t read_pico_pmu_event_ack(uint16_t event_id) {
    uint8_t buf[4];
    wr16(&buf[0], event_id);
    wr16(&buf[2], pmu_crc16_ccitt_false(buf, 2));
    esp_err_t err = pmu_write_reg(PMU_REG_EVENT_ACK, buf, sizeof(buf));
    s_snap.last_err = err;
    s_snap.last_op_code = PMU_REG_EVENT_ACK;
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    return read_pico_pmu_poll();
}

void read_pico_pmu_drain_events(void) {
    for (int i = 0; i < PMU_EVENT_FIFO_DEPTH; i++) {
        if (read_pico_pmu_poll() != ESP_OK) break;
        if (!s_snap.event_ok || s_snap.pending_events == 0) break;
        uint16_t id = s_snap.event.event_id;
        if (id == 0) break;
        ESP_LOGI(TAG, "drain %s id=%u", pmu_event_name(s_snap.event.type), (unsigned)id);
        if (read_pico_pmu_event_ack(id) != ESP_OK) break;
    }
}

bool read_pico_pmu_take_key_short(void) {
    bool lock = false;
    for (int i = 0; i < PMU_EVENT_FIFO_DEPTH; i++) {
        if (read_pico_pmu_poll() != ESP_OK) break;
        if (!s_snap.event_ok || s_snap.pending_events == 0) break;
        uint8_t type = s_snap.event.type;
        uint16_t id = s_snap.event.event_id;
        if (type == PMU_EVT_KEY_SHORT) {
            lock = true;
            ESP_LOGI(TAG, "KEY_SHORT id=%u held=%u", (unsigned)id, (unsigned)s_snap.event.arg0);
        }
        if (id == 0) break;
        if (read_pico_pmu_event_ack(id) != ESP_OK) break;
    }
    return lock;
}

bool read_pico_pmu_take_key_wakeup(void) {
    bool wake = false;
    for (int i = 0; i < PMU_EVENT_FIFO_DEPTH; i++) {
        if (read_pico_pmu_poll() != ESP_OK) break;
        if (!s_snap.event_ok || s_snap.pending_events == 0) break;
        uint8_t type = s_snap.event.type;
        uint16_t id = s_snap.event.event_id;
        if (type == PMU_EVT_KEY_DOWN || type == PMU_EVT_KEY_SHORT) {
            wake = true;
            ESP_LOGI(TAG, "key wake %s id=%u", pmu_event_name(type), (unsigned)id);
        }
        if (id == 0) break;
        if (read_pico_pmu_event_ack(id) != ESP_OK) break;
    }
    return wake;
}

// 命令 OK/ACCEPTED 只表示受理，以 STATUS power_state 为准。
// OK/ACCEPTED means accepted; the real state is STATUS power_state.
static esp_err_t wait_power_state(uint8_t want, int timeout_ms) {
    int steps = timeout_ms / 20;
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; i++) {
        if (read_pico_pmu_poll() == ESP_OK && s_snap.power_state == want) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(
        TAG, "wait %s timeout, pwr=%s",
        pmu_power_name(want), pmu_power_name(s_snap.power_state)
    );
    return ESP_ERR_TIMEOUT;
}

esp_err_t read_pico_pmu_report_ready(void) {
    uint8_t reason = 0;
    (void)read_pico_pmu_poll();
    if (s_snap.power_state == PMU_PWR_RUNNING) return ESP_OK;

    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_HOST_READY, &reason, 1);
    ESP_LOGI(
        TAG, "HOST_READY %s st=%s pwr=%s",
        esp_err_to_name(err), pmu_status_name(s_snap.last_op_status),
        pmu_power_name(s_snap.power_state)
    );
    if (err != ESP_OK) return err;
    return wait_power_state(PMU_PWR_RUNNING, 1000);
}

esp_err_t read_pico_pmu_report_sleep(void) {
    (void)read_pico_pmu_poll();
    if (s_snap.power_state != PMU_PWR_RUNNING) {
        ESP_LOGW(
            TAG, "SOFT_SLEEP need RUNNING, now %s",
            pmu_power_name(s_snap.power_state)
        );
        esp_err_t err = read_pico_pmu_report_ready();
        if (err != ESP_OK) return err;
    }

    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_HOST_SOFT_SLEEP, NULL, 0);
    ESP_LOGI(
        TAG, "SOFT_SLEEP cmd %s st=%s pwr=%s",
        esp_err_to_name(err), pmu_status_name(s_snap.last_op_status),
        pmu_power_name(s_snap.power_state)
    );
    if (err != ESP_OK) return err;
    if (wait_power_state(PMU_PWR_SOFT_SLEEP, 1000) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "PMU latched SOFT_SLEEP");
    return ESP_OK;
}

esp_err_t read_pico_pmu_power_off(void) {
    (void)read_pico_pmu_poll();

    uint8_t req[3] = { 0, 1, 0 };
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_OFF, req, sizeof(req));
    ESP_LOGI(
        TAG, "REQUEST_OFF %s st=%s pwr=%s",
        esp_err_to_name(err), pmu_status_name(s_snap.last_op_status),
        pmu_power_name(s_snap.power_state)
    );
    if (err != ESP_OK) {
        err = read_pico_pmu_action(PMU_ACTION_HOST_LOGICAL_OFF, 0, 1);
        ESP_LOGI(TAG, "LOGICAL_OFF fallback %s", esp_err_to_name(err));
        if (err != ESP_OK) return err;
    }

    uint16_t event_id = 0;
    bool pending = false;
    for (int i = 0; i < 100; i++) {
        if (read_pico_pmu_poll() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (s_snap.power_state == PMU_PWR_SHUTDOWN_PENDING) pending = true;
        if (s_snap.event_ok && s_snap.pending_events > 0) {
            uint8_t type = s_snap.event.type;
            uint16_t id = s_snap.event.event_id;
            ESP_LOGI(TAG, "off wait %s id=%u pwr=%s",
                     pmu_event_name(type), (unsigned)id,
                     pmu_power_name(s_snap.power_state));
            if (type == PMU_EVT_SHUTDOWN_REQUESTED && id != 0) {
                event_id = id;
                pending = true;
                (void)read_pico_pmu_event_ack(id);
                break;
            }
            if (id != 0) (void)read_pico_pmu_event_ack(id);
            continue;
        }
        if (pending && event_id == 0 && s_snap.last_event_id != 0) {
            event_id = s_snap.last_event_id;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (!pending) {
        ESP_LOGE(
            TAG, "no SHUTDOWN_PENDING, pwr=%s ev=%u",
            pmu_power_name(s_snap.power_state), (unsigned)s_snap.pending_events
        );
        return ESP_ERR_TIMEOUT;
    }
    if (event_id == 0) event_id = s_snap.last_event_id;
    if (event_id == 0) event_id = 1;

    uint8_t ready[2];
    wr16(ready, event_id);
    err = read_pico_pmu_cmd(PMU_CMD_SHUTDOWN_READY, ready, sizeof(ready));
    ESP_LOGI(
        TAG, "SHUTDOWN_READY id=%u %s st=%s",
        (unsigned)event_id, esp_err_to_name(err),
        pmu_status_name(s_snap.last_op_status)
    );
    if (err != ESP_OK) return err;

    for (int i = 0; i < 25; i++) {
        (void)read_pico_pmu_poll();
        if (s_snap.power_state == PMU_PWR_OFF) {
            ESP_LOGI(TAG, "PMU EN off");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, "EN still high, pwr=%s", pmu_power_name(s_snap.power_state));
    return ESP_ERR_TIMEOUT;
}

esp_err_t read_pico_pmu_action(uint8_t action, uint16_t delay_ms, uint16_t reason) {
    uint8_t prep[5];
    prep[0] = action;
    wr16(&prep[1], delay_ms);
    wr16(&prep[3], reason);
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_ACTION_PREPARE, prep, sizeof(prep));
    if (err != ESP_OK || s_last_plen < 4) return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    uint8_t commit[4];
    wr32(commit, rd32(s_last_payload));
    return read_pico_pmu_cmd(PMU_CMD_ACTION_COMMIT, commit, sizeof(commit));
}

// full：身份/状态/诊断/配置/时间/闹钟。否则只刷状态、快照电池和事件计数。
// full: identity, status, diag, config, time, alarm. Else status, quick battery, event count.
static esp_err_t refresh_core(bool full) {
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t ident[PMU_IDENTITY_SIZE];
    uint8_t status[PMU_STATUS_SIZE];
    uint8_t diag[PMU_DIAGNOSTICS_SIZE];
    uint8_t quick[PMU_QUICK_BATTERY_SIZE];
    uint8_t ev[PMU_EVENT_SIZE];
    uint8_t count = 0;

    esp_err_t err = pmu_read_reg(PMU_REG_IDENTITY, ident, sizeof(ident));
    if (err != ESP_OK) {
        s_snap.present = false;
        s_snap.last_err = err;
        return err;
    }
    s_snap.present = true;
    bool id_ok = parse_identity(ident);
    for (int i = 0; !id_ok && i < 2; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (pmu_read_reg(PMU_REG_IDENTITY, ident, sizeof(ident)) == ESP_OK) {
            id_ok = parse_identity(ident);
        }
    }
    s_snap.identity_ok = id_ok;

    bool st_ok = false;
    for (int i = 0; i < 3 && !st_ok; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(20));
        if (pmu_read_reg(PMU_REG_STATUS, status, sizeof(status)) == ESP_OK) {
            st_ok = parse_status(status);
        }
    }
    s_snap.status_ok = st_ok;

    if (pmu_read_reg(PMU_REG_QUICK_BATTERY, quick, sizeof(quick)) == ESP_OK) {
        parse_quick(quick);
    }
    if (pmu_read_reg(PMU_REG_EVENT_COUNT, &count, 1) == ESP_OK) {
        s_snap.pending_events = count;
    }
    if (count != 0 && pmu_read_reg(PMU_REG_EVENT_PEEK, ev, sizeof(ev)) == ESP_OK) {
        parse_event(ev);
    } else {
        s_snap.event_ok = false;
        memset(&s_snap.event, 0, sizeof(s_snap.event));
    }

    if (full) {
        if (pmu_read_reg(PMU_REG_DIAGNOSTICS, diag, sizeof(diag)) == ESP_OK) {
            parse_diag(diag);
        }
        read_pico_pmu_cmd(PMU_CMD_CONFIG_GET, NULL, 0);
        read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
        read_pico_pmu_cmd(PMU_CMD_ALARM_GET, NULL, 0);
    }
    s_snap.last_err = ESP_OK;
    return ESP_OK;
}

esp_err_t read_pico_pmu_refresh(void) {
    return refresh_core(true);
}

esp_err_t read_pico_pmu_poll(void) {
    return refresh_core(false);
}

esp_err_t read_pico_pmu_init(i2c_master_bus_handle_t bus_handle, const read_pico_pmu_config_t* config) {
    memset(&s_snap, 0, sizeof(s_snap));
    if (config == NULL || bus_handle == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t addr = config->i2c_addr ? config->i2c_addr : PMU_I2C_ADDR;
    uint32_t hz = config->scl_speed_hz ? config->scl_speed_hz : READ_PICO_PMU_I2C_HZ_DEFAULT;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    s_bus = bus_handle;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device 0x%02X failed: %s", addr, esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < 5; ++i) {
        uint8_t ident[PMU_IDENTITY_SIZE];
        err = pmu_read_reg(PMU_REG_IDENTITY, ident, sizeof(ident));
        if (err == ESP_OK) {
            parse_identity(ident);
            s_snap.present = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CW32 not responding: %s", esp_err_to_name(err));
        s_snap.last_err = err;
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        s_bus = NULL;
        return err;
    }

    recover_seq();

    uint8_t reason = 0;
    err = read_pico_pmu_cmd(PMU_CMD_HOST_READY, &reason, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HOST_READY failed: %s st=%s",
                 esp_err_to_name(s_snap.last_err), pmu_status_name(s_snap.last_op_status));
    } else if (wait_power_state(PMU_PWR_RUNNING, 1000) != ESP_OK) {
        ESP_LOGW(TAG, "HOST_READY sent, PMU still %s", pmu_power_name(s_snap.power_state));
    }

    s_ready = s_snap.identity_ok;
    ESP_LOGI(
        TAG, "Ready: %s proto %u.%u fw %u.%u.%u boot=%08lX pwr=%s",
        s_snap.identity_ok ? "PMU1" : "?",
        s_snap.proto_major, s_snap.proto_minor,
        s_snap.fw_major, s_snap.fw_minor, s_snap.fw_patch,
        (unsigned long)s_snap.boot_id,
        pmu_power_name(s_snap.power_state)
    );
    if (s_snap.fw_major != PMU_FW_MAJOR ||
        s_snap.fw_minor != PMU_FW_MINOR ||
        s_snap.fw_patch < PMU_FW_PATCH) {
        ESP_LOGW(
            TAG, "CW32 fw %u.%u.%u older than host expect %u.%u.%u",
            s_snap.fw_major, s_snap.fw_minor, s_snap.fw_patch,
            PMU_FW_MAJOR, PMU_FW_MINOR, PMU_FW_PATCH
        );
    }
    if (s_snap.power_state != PMU_PWR_RUNNING) {
        ESP_LOGW(TAG, "handshake not RUNNING after HOST_READY");
    }
    if (s_ready) {
        read_pico_pmu_drain_events();
        return ESP_OK;
    }
    (void)read_pico_pmu_deinit();
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t read_pico_pmu_deinit(void) {
    if (s_dev != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_dev);
        if (err != ESP_OK) return err;
    }
    s_bus = NULL;
    s_dev = NULL;
    s_ready = false;
    memset(&s_snap, 0, sizeof(s_snap));
    return ESP_OK;
}

bool read_pico_pmu_ready(void) {
    return s_ready;
}

const pmu_snapshot_t* read_pico_pmu_get(void) {
    return &s_snap;
}

const char* pmu_power_name(uint8_t state) {
    switch (state) {
        case PMU_PWR_OFF: return "OFF";
        case PMU_PWR_POWERING_ON: return "PWR_ON";
        case PMU_PWR_BOOT_WAIT: return "BOOT_WAIT";
        case PMU_PWR_RUNNING: return "RUNNING";
        case PMU_PWR_SHUTDOWN_PENDING: return "SHUT_PEND";
        case PMU_PWR_RESETTING: return "RESET";
        case PMU_PWR_DOWNLOAD_MODE: return "DOWNLOAD";
        case PMU_PWR_FAULT: return "FAULT";
        case PMU_PWR_SOFT_SLEEP: return "SOFT_SLEEP";
        default: return "?";
    }
}

const char* pmu_charge_name(uint8_t state) {
    switch (state) {
        case PMU_CHARGE_UNKNOWN: return "UNK";
        case PMU_CHARGE_NOT_CHARGING: return "IDLE";
        case PMU_CHARGE_CHARGING: return "CHG";
        case PMU_CHARGE_FULL_INFERRED: return "FULL";
        case PMU_CHARGE_FAULT: return "FAULT";
        default: return "?";
    }
}

const char* pmu_event_name(uint8_t type) {
    switch (type) {
        case PMU_EVT_KEY_DOWN: return "KEY_DOWN";
        case PMU_EVT_KEY_UP: return "KEY_UP";
        case PMU_EVT_KEY_SHORT: return "KEY_SHORT";
        case PMU_EVT_KEY_LONG: return "KEY_LONG";
        case PMU_EVT_KEY_FORCE_OFF: return "KEY_FORCE";
        case PMU_EVT_CHARGE_STATE_CHANGED: return "CHARGE";
        case PMU_EVT_BATTERY_SAMPLE_READY: return "BAT_RDY";
        case PMU_EVT_BATTERY_LOW: return "BAT_LOW";
        case PMU_EVT_BATTERY_CRITICAL: return "BAT_CRIT";
        case PMU_EVT_BATTERY_SOC_LOW: return "SOC_LOW";
        case PMU_EVT_ALARM_FIRED: return "ALARM";
        case PMU_EVT_HOST_STARTED: return "HOST_START";
        case PMU_EVT_HOST_READY: return "HOST_RDY";
        case PMU_EVT_SHUTDOWN_REQUESTED: return "SHUT_REQ";
        case PMU_EVT_HOST_RESET_PERFORMED: return "HOST_RST";
        case PMU_EVT_COMMAND_COMPLETED: return "CMD_DONE";
        case PMU_EVT_CONFIG_RECOVERED: return "CFG_RCV";
        case PMU_EVT_CW_RESET: return "CW_RST";
        case PMU_EVT_OVERFLOW: return "OVERFLOW";
        default: return "EVT";
    }
}

const char* pmu_status_name(uint16_t status) {
    switch (status) {
        case PMU_STATUS_OK: return "OK";
        case PMU_STATUS_ACCEPTED: return "ACCEPTED";
        case PMU_STATUS_BUSY: return "BUSY";
        case PMU_STATUS_BAD_CRC: return "BAD_CRC";
        case PMU_STATUS_BAD_MAGIC: return "BAD_MAGIC";
        case PMU_STATUS_BAD_LENGTH: return "BAD_LEN";
        case PMU_STATUS_UNSUPPORTED_VERSION: return "BAD_VER";
        case PMU_STATUS_UNKNOWN_COMMAND: return "UNK_CMD";
        case PMU_STATUS_INVALID_ARGUMENT: return "BAD_ARG";
        case PMU_STATUS_SEQUENCE_CONFLICT: return "SEQ";
        case PMU_STATUS_STALE_SESSION: return "STALE";
        case PMU_STATUS_NOT_SUPPORTED: return "NOSUP";
        case PMU_STATUS_INVALID_STATE: return "BAD_ST";
        case PMU_STATUS_NOT_ARMED: return "NOARM";
        case PMU_STATUS_TOKEN_EXPIRED: return "EXPIRED";
        case PMU_STATUS_PERMISSION_DENIED: return "DENIED";
        default: return "ERR";
    }
}

const char* pmu_led_name(uint8_t color) {
    switch (color) {
        case PMU_LED_OFF: return "OFF";
        case PMU_LED_RED: return "RED";
        case PMU_LED_WHITE: return "WHT";
        case PMU_LED_RED_WHITE: return "BOTH";
        default: return "?";
    }
}

const char* pmu_action_name(uint8_t action) {
    switch (action) {
        case PMU_ACTION_HOST_LOGICAL_OFF: return "LOG_OFF";
        case PMU_ACTION_HOST_HARD_RESET: return "HRST";
        case PMU_ACTION_HOST_NORMAL_RESTART: return "RESTART";
        case PMU_ACTION_HOST_ENTER_DOWNLOAD_MODE: return "DL";
        case PMU_ACTION_HOST_EXIT_DOWNLOAD_MODE: return "EXIT_DL";
        case PMU_ACTION_HOST_POWER_ON: return "PWR_ON";
        case PMU_ACTION_HOST_SOFT_SLEEP: return "SLEEP";
        default: return "ACT";
    }
}
