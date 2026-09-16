/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SY7636A 主机：EN 控制、I2C 读写、轨上电/掉电。
 *
 * SY7636A host: EN control, I2C read/write, rail power-on/off.
 */

#include "sy7636a.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REG_OPERATION 0x00
#define REG_VCOM_LSB 0x01
#define REG_VCOM_MSB 0x02
#define REG_VLDO 0x03
#define REG_DELAY 0x06
#define REG_FAULT 0x07
#define REG_TEMP 0x08

#define OP_ON 0x80
#define OP_VCOMCTL 0x40
#define VLDO_RESERVED 0x06
#define I2C_TIMEOUT_MS 100
#define I2C_RETRY 5

static const char* TAG = "sy7636a";

struct sy7636a_dev_t {
    i2c_master_dev_handle_t dev;
    gpio_num_t en_gpio;
    gpio_num_t vcom_en_gpio;
    gpio_num_t pgood_gpio;
    esp_err_t (*en_fn)(bool on);
    esp_err_t (*vcom_en_fn)(bool on);
    int (*pgood_fn)(void);
    uint16_t en_settle_ms;
    uint16_t pgood_timeout_ms;
    uint16_t poweroff_hold_ms;
    sy7636a_power_config_t cfg;
    sy7636a_status_t last;
    bool last_ok;
    bool rails;
    bool en_on;
};

static bool gpio_ok(gpio_num_t gpio) {
    return gpio != GPIO_NUM_NC && GPIO_IS_VALID_GPIO(gpio);
}

static int dly_enc(uint8_t ms) {
    switch (ms) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 4: return 3;
        default: return -1;
    }
}

static uint8_t dly_ms(uint8_t code) {
    static const uint8_t ms[4] = { 0, 1, 2, 4 };
    return ms[code & 0x03];
}

static bool cfg_ok(const sy7636a_power_config_t* cfg) {
    if (cfg->vcom_mv < 0 || cfg->vcom_mv > SY7636A_VCOM_MAX_MV) return false;
    if (cfg->vldo < SY7636A_VLDO_1525 || cfg->vldo > SY7636A_VLDO_1425) return false;
    if (cfg->discharge & (uint8_t)~SY7636A_DISCHG_MASK) return false;
    for (int i = 0; i < 4; i++) {
        if (dly_enc(cfg->dly_ms[i]) < 0) return false;
    }
    return true;
}

static uint8_t pack_delay(const uint8_t dly[4]) {
    return (uint8_t)(
        (dly_enc(dly[3]) << 6) | (dly_enc(dly[2]) << 4)
        | (dly_enc(dly[1]) << 2) | dly_enc(dly[0])
    );
}

static uint8_t pack_vldo(sy7636a_vldo_t vldo) {
    return (uint8_t)(((uint8_t)vldo << 5) | VLDO_RESERVED);
}

static uint8_t pack_op(const sy7636a_power_config_t* cfg, bool on) {
    uint8_t op = (uint8_t)(cfg->discharge & SY7636A_DISCHG_MASK);
    if (on) op |= OP_ON;
    if (cfg->vcom_manual) op |= OP_VCOMCTL;
    return op;
}

static void unpack_delay(uint8_t reg, uint8_t out[4]) {
    out[0] = dly_ms(reg);
    out[1] = dly_ms((uint8_t)(reg >> 2));
    out[2] = dly_ms((uint8_t)(reg >> 4));
    out[3] = dly_ms((uint8_t)(reg >> 6));
}

static sy7636a_vldo_t unpack_vldo(uint8_t reg) {
    return (sy7636a_vldo_t)((reg >> 5) & 0x07);
}

static esp_err_t write_reg(sy7636a_handle_t h, uint8_t reg, uint8_t value) {
    uint8_t data[2] = { reg, value };
    return i2c_master_transmit(h->dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(sy7636a_handle_t h, uint8_t reg, uint8_t* value) {
    // 手册要求先 STOP 再读，不能 Repeated START。
    // / Datasheet requires STOP then read; Repeated START is not allowed.
    esp_err_t err = i2c_master_transmit(h->dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    return i2c_master_receive(h->dev, value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t read_reg_retry(sy7636a_handle_t h, uint8_t reg, uint8_t* value) {
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < I2C_RETRY; i++) {
        err = read_reg(h, reg, value);
        if (err == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return err;
}

static esp_err_t set_en(sy7636a_handle_t h, bool on) {
    if (h->en_fn) return h->en_fn(on);
    if (gpio_ok(h->en_gpio)) return gpio_set_level(h->en_gpio, on ? 1 : 0);
    return ESP_OK;
}

static esp_err_t set_vcom_en(sy7636a_handle_t h, bool on) {
    if (h->vcom_en_fn) return h->vcom_en_fn(on);
    if (gpio_ok(h->vcom_en_gpio)) return gpio_set_level(h->vcom_en_gpio, on ? 1 : 0);
    return ESP_OK;
}

static int read_pgood(sy7636a_handle_t h) {
    if (h->pgood_fn) return h->pgood_fn();
    if (gpio_ok(h->pgood_gpio)) return gpio_get_level(h->pgood_gpio);
    return 1;
}

static esp_err_t cfg_gpio_out(gpio_num_t gpio, int level) {
    if (!gpio_ok(gpio)) return ESP_OK;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    return gpio_set_level(gpio, level);
}

static esp_err_t cfg_gpio_in(gpio_num_t gpio) {
    if (!gpio_ok(gpio)) return ESP_OK;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static esp_err_t write_vcom(sy7636a_handle_t h) {
    int code = h->cfg.vcom_mv / 10;
    if (code < 0) code = 0;
    if (code > 500) code = 500;

    uint8_t msb = 0x14;
    esp_err_t err = read_reg(h, REG_VCOM_MSB, &msb);
    if (err != ESP_OK) return err;
    err = write_reg(h, REG_VCOM_LSB, (uint8_t)code);
    if (err != ESP_OK) return err;
    msb = (uint8_t)((msb & 0x7F) | ((code & 0x100) ? 0x80 : 0));
    return write_reg(h, REG_VCOM_MSB, msb);
}

static esp_err_t write_pending(sy7636a_handle_t h) {
    if (!h->en_on) return ESP_OK;
    esp_err_t err = write_vcom(h);
    if (err != ESP_OK) return err;
    err = write_reg(h, REG_VLDO, pack_vldo(h->cfg.vldo));
    if (err != ESP_OK) return err;
    err = write_reg(h, REG_DELAY, pack_delay(h->cfg.dly_ms));
    if (err != ESP_OK) return err;
    return write_reg(h, REG_OPERATION, pack_op(&h->cfg, h->rails));
}

static void fill_status(
    sy7636a_status_t* s, uint8_t op, uint8_t fault, uint8_t vldo,
    uint8_t delay, int8_t temp, int vcom_mv
) {
    s->on = (op & OP_ON) != 0;
    s->vcom_manual = (op & OP_VCOMCTL) != 0;
    s->discharge = (uint8_t)(op & SY7636A_DISCHG_MASK);
    s->vcom_mv = vcom_mv;
    s->vldo = unpack_vldo(vldo);
    unpack_delay(delay, s->dly_ms);
    s->fault_reg = fault;
    s->fault = sy7636a_fault_from_reg(fault);
    s->pg = (fault & 0x01) != 0;
    s->temperature_c = temp;
}

esp_err_t sy7636a_init(i2c_master_bus_handle_t bus_handle, const sy7636a_config_t* config,
                       sy7636a_handle_t* handle) {
    if (bus_handle == NULL || config == NULL || handle == NULL) return ESP_ERR_INVALID_ARG;

    sy7636a_power_config_t cfg = config->power;
    if (cfg.vcom_mv == 0 && cfg.vldo == 0) cfg = (sy7636a_power_config_t)SY7636A_POWER_CONFIG_DEFAULT();
    if (!cfg_ok(&cfg)) return ESP_ERR_INVALID_ARG;

    sy7636a_handle_t h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;

    h->en_gpio = config->en_gpio;
    h->vcom_en_gpio = config->vcom_en_gpio;
    h->pgood_gpio = config->pgood_gpio;
    h->en_fn = config->en_fn;
    h->vcom_en_fn = config->vcom_en_fn;
    h->pgood_fn = config->pgood_fn;
    h->en_settle_ms = config->en_settle_ms ? config->en_settle_ms : 20;
    h->pgood_timeout_ms = config->pgood_timeout_ms ? config->pgood_timeout_ms : 300;
    h->poweroff_hold_ms = config->poweroff_hold_ms ? config->poweroff_hold_ms : 500;
    h->cfg = cfg;
    h->en_on = !h->en_fn && !gpio_ok(h->en_gpio);

    esp_err_t err = cfg_gpio_out(h->en_gpio, 0);
    if (err == ESP_OK) err = cfg_gpio_out(h->vcom_en_gpio, 0);
    if (err == ESP_OK) err = cfg_gpio_in(h->pgood_gpio);
    if (err != ESP_OK) {
        free(h);
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr ? config->i2c_addr : SY7636A_ADDR_DEFAULT,
        .scl_speed_hz = config->scl_speed_hz ? config->scl_speed_hz : SY7636A_I2C_HZ_DEFAULT,
    };
    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C add failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    *handle = h;
    return ESP_OK;
}

esp_err_t sy7636a_deinit(sy7636a_handle_t h) {
    if (h == NULL) return ESP_OK;
    if (h->rails) (void)sy7636a_power_off(h);
    if (h->dev) i2c_master_bus_rm_device(h->dev);
    free(h);
    return ESP_OK;
}

esp_err_t sy7636a_i2c_begin(sy7636a_handle_t h, bool* woke) {
    if (h == NULL || woke == NULL) return ESP_ERR_INVALID_ARG;
    *woke = false;
    if (h->en_on) return ESP_OK;
    esp_err_t err = set_en(h, true);
    if (err != ESP_OK) return err;
    h->en_on = true;
    *woke = true;
    vTaskDelay(pdMS_TO_TICKS(h->en_settle_ms));
    return ESP_OK;
}

void sy7636a_i2c_end(sy7636a_handle_t h, bool woke) {
    if (h == NULL || !woke || h->rails) return;
    (void)set_en(h, false);
    h->en_on = false;
}

esp_err_t sy7636a_power_on(sy7636a_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    if (h->rails) return ESP_OK;

    bool woke = false;
    esp_err_t err = sy7636a_i2c_begin(h, &woke);
    if (err != ESP_OK) return err;
    (void)set_vcom_en(h, false);

    h->rails = false;
    for (int i = 0; i < I2C_RETRY; i++) {
        err = write_vcom(h);
        if (err == ESP_OK) err = write_reg(h, REG_VLDO, pack_vldo(h->cfg.vldo));
        if (err == ESP_OK) err = write_reg(h, REG_DELAY, pack_delay(h->cfg.dly_ms));
        if (err == ESP_OK) err = write_reg(h, REG_OPERATION, pack_op(&h->cfg, true));
        if (err == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power on failed: %s", esp_err_to_name(err));
        sy7636a_i2c_end(h, true);
        return err;
    }

    bool good = false;
    int64_t deadline = esp_timer_get_time() + (int64_t)h->pgood_timeout_ms * 1000;
    do {
        if (read_pgood(h) > 0) {
            good = true;
            break;
        }
        esp_rom_delay_us(200);
    } while (esp_timer_get_time() < deadline);

    if (!good) {
        uint8_t fault = 0xFF;
        (void)read_reg(h, REG_FAULT, &fault);
        ESP_LOGE(TAG, "PGOOD timeout, fault=0x%02X", fault);
        (void)write_reg(h, REG_OPERATION, 0x00);
        vTaskDelay(pdMS_TO_TICKS(150));
        sy7636a_i2c_end(h, true);
        return ESP_ERR_TIMEOUT;
    }

    err = set_vcom_en(h, true);
    if (err != ESP_OK) {
        (void)write_reg(h, REG_OPERATION, 0x00);
        sy7636a_i2c_end(h, true);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    h->rails = true;
    ESP_LOGI(TAG, "rails on, VCOM=-%d mV", h->cfg.vcom_mv);
    return ESP_OK;
}

esp_err_t sy7636a_power_off(sy7636a_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    (void)set_vcom_en(h, false);
    if (h->en_on) (void)write_reg(h, REG_OPERATION, 0x00);
    vTaskDelay(pdMS_TO_TICKS(h->poweroff_hold_ms));
    (void)set_en(h, false);
    h->en_on = false;
    h->rails = false;
    ESP_LOGI(TAG, "rails off");
    return ESP_OK;
}

bool sy7636a_rails_on(sy7636a_handle_t h) {
    return h != NULL && h->rails;
}

esp_err_t sy7636a_read(sy7636a_handle_t h, sy7636a_status_t* status) {
    if (h == NULL || status == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t op = 0, fault = 0, temp = 0, vldo = 0, delay = 0;
    esp_err_t err = read_reg_retry(h, REG_OPERATION, &op);
    if (err == ESP_OK) err = read_reg_retry(h, REG_FAULT, &fault);
    if (err == ESP_OK) err = read_reg_retry(h, REG_TEMP, &temp);
    if (err == ESP_OK) err = read_reg_retry(h, REG_VLDO, &vldo);
    if (err == ESP_OK) err = read_reg_retry(h, REG_DELAY, &delay);
    if (err != ESP_OK) return err;

    fill_status(status, op, fault, vldo, delay, (int8_t)temp, h->cfg.vcom_mv);
    h->last = *status;
    h->last_ok = true;
    return ESP_OK;
}

const sy7636a_status_t* sy7636a_last(sy7636a_handle_t h) {
    if (h == NULL || !h->last_ok) return NULL;
    return &h->last;
}

const sy7636a_power_config_t* sy7636a_get_config(sy7636a_handle_t h) {
    return h ? &h->cfg : NULL;
}

esp_err_t sy7636a_set_vcom(sy7636a_handle_t h, int mv) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    if (mv < 0) mv = 0;
    if (mv > SY7636A_VCOM_MAX_MV) mv = SY7636A_VCOM_MAX_MV;
    h->cfg.vcom_mv = mv;
    if (!h->en_on) return ESP_OK;
    return write_vcom(h);
}

int sy7636a_get_vcom(sy7636a_handle_t h) {
    return h ? h->cfg.vcom_mv : 0;
}

esp_err_t sy7636a_set_vcom_manual(sy7636a_handle_t h, bool manual) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    h->cfg.vcom_manual = manual;
    if (!h->en_on) return ESP_OK;
    return write_reg(h, REG_OPERATION, pack_op(&h->cfg, h->rails));
}

esp_err_t sy7636a_set_vldo(sy7636a_handle_t h, sy7636a_vldo_t vldo) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    if (vldo < SY7636A_VLDO_1525 || vldo > SY7636A_VLDO_1425) {
        return ESP_ERR_INVALID_ARG;
    }
    h->cfg.vldo = vldo;
    return write_pending(h);
}

esp_err_t sy7636a_set_discharge(sy7636a_handle_t h, uint8_t bits) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    h->cfg.discharge = (uint8_t)(bits & SY7636A_DISCHG_MASK);
    return write_pending(h);
}

esp_err_t sy7636a_set_poweron_delay(
    sy7636a_handle_t h,
    uint8_t dly1_ms, uint8_t dly2_ms, uint8_t dly3_ms, uint8_t dly4_ms
) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t dly[4] = { dly1_ms, dly2_ms, dly3_ms, dly4_ms };
    for (int i = 0; i < 4; i++) {
        if (dly_enc(dly[i]) < 0) return ESP_ERR_INVALID_ARG;
    }
    memcpy(h->cfg.dly_ms, dly, sizeof(dly));
    return write_pending(h);
}

sy7636a_fault_t sy7636a_fault_from_reg(uint8_t fault_reg) {
    switch ((fault_reg >> 1) & 0x0F) {
        case 0x0: return SY7636A_FAULT_NONE;
        case 0x1: return SY7636A_FAULT_UVP_VP;
        case 0x2: return SY7636A_FAULT_UVP_VN;
        case 0x3: return SY7636A_FAULT_UVP_VPOS;
        case 0x4: return SY7636A_FAULT_UVP_VNEG;
        case 0x5: return SY7636A_FAULT_UVP_VDDH;
        case 0x6: return SY7636A_FAULT_UVP_VEE;
        case 0x7: return SY7636A_FAULT_SCP_VP;
        case 0x8: return SY7636A_FAULT_SCP_VN;
        case 0x9: return SY7636A_FAULT_SCP_VPOS;
        case 0xA: return SY7636A_FAULT_SCP_VNEG;
        case 0xB: return SY7636A_FAULT_SCP_VDDH;
        case 0xC: return SY7636A_FAULT_SCP_VEE;
        case 0xD: return SY7636A_FAULT_SCP_VCOM;
        case 0xF: return SY7636A_FAULT_OTP;
        default: return SY7636A_FAULT_UNKNOWN;
    }
}

const char* sy7636a_fault_name(sy7636a_fault_t fault) {
    switch (fault) {
        case SY7636A_FAULT_NONE: return "none";
        case SY7636A_FAULT_UVP_VP: return "UVP VP";
        case SY7636A_FAULT_UVP_VN: return "UVP VN";
        case SY7636A_FAULT_UVP_VPOS: return "UVP VPOS";
        case SY7636A_FAULT_UVP_VNEG: return "UVP VNEG";
        case SY7636A_FAULT_UVP_VDDH: return "UVP VDDH";
        case SY7636A_FAULT_UVP_VEE: return "UVP VEE";
        case SY7636A_FAULT_SCP_VP: return "SCP VP";
        case SY7636A_FAULT_SCP_VN: return "SCP VN";
        case SY7636A_FAULT_SCP_VPOS: return "SCP VPOS";
        case SY7636A_FAULT_SCP_VNEG: return "SCP VNEG";
        case SY7636A_FAULT_SCP_VDDH: return "SCP VDDH";
        case SY7636A_FAULT_SCP_VEE: return "SCP VEE";
        case SY7636A_FAULT_SCP_VCOM: return "SCP VCOM";
        case SY7636A_FAULT_OTP: return "OTP";
        default: return "fault";
    }
}

const char* sy7636a_vldo_name(sy7636a_vldo_t vldo) {
    switch (vldo) {
        case SY7636A_VLDO_1525: return "±15.25 V";
        case SY7636A_VLDO_1500: return "±15.00 V";
        case SY7636A_VLDO_1475: return "±14.75 V";
        case SY7636A_VLDO_1450: return "±14.50 V";
        case SY7636A_VLDO_1425: return "±14.25 V";
        default: return "reserved";
    }
}
