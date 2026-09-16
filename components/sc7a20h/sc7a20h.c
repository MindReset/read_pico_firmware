/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SC7A20H 主机：读数、电源、拿起唤醒，以及实验室助手。
 *
 * SC7A20H host: read, power, pickup-to-wake, plus lab helpers.
 */

#include "sc7a20h_lab.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SC7A20H_REG_WHO_AM_I 0x0F
#define SC7A20H_REG_CTRL0 0x1F
#define SC7A20H_REG_CTRL1 0x20
#define SC7A20H_REG_CTRL2 0x21
#define SC7A20H_REG_CTRL3 0x22
#define SC7A20H_REG_CTRL4 0x23
#define SC7A20H_REG_CTRL5 0x24
#define SC7A20H_REG_CTRL6 0x25
#define SC7A20H_REG_STATUS 0x27
#define SC7A20H_REG_OUT_X_L 0x28
#define SC7A20H_REG_FIFO_CTRL 0x2E
#define SC7A20H_REG_FIFO_SRC 0x2F
#define SC7A20H_REG_AOI1_CFG 0x30
#define SC7A20H_REG_AOI1_SRC 0x31
#define SC7A20H_REG_AOI2_CFG 0x34
#define SC7A20H_REG_AOI2_SRC 0x35
#define SC7A20H_REG_CLICK_CTRL 0x38
#define SC7A20H_REG_CLICK_SRC 0x39
#define SC7A20H_REG_CLICK_C1 0x3A
#define SC7A20H_REG_CLICK_C2 0x3B
#define SC7A20H_REG_CLICK_C3 0x3C
#define SC7A20H_REG_CLICK_C4 0x3D
#define SC7A20H_REG_OUT_X_NEW 0x61
#define SC7A20H_REG_SOFT_RESET 0x68
#define SC7A20H_REG_FIFO_DATA 0x69
#define SC7A20H_REG_VERSION 0x70

#define CTRL5_FIFO_EN 0x40
#define CTRL5_LIR_INT1 0x08
#define CTRL5_D4D_INT1 0x04
#define CTRL5_BOOT 0x80
#define CTRL3_FIFO_8BIT 0x01
#define CTRL2_FDS 0x08
#define CTRL2_HP_RESET 0x04
#define CTRL2_HPIS1 0x01
#define CTRL2_HPIS2 0x02
#define CTRL4_BDU 0x80
#define CTRL4_ST0 0x02

#define I2C_TIMEOUT_MS 100
#define I2C_HZ 400000

static const char* TAG = "sc7a20h";

struct sc7a20h_dev_t {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    gpio_num_t int1_gpio;
    sc7a20h_sensor_config_t cfg;
    uint8_t mg_lsb;
    uint8_t ctrl3;
    uint8_t ctrl5;
    bool fifo_8bit;
    bool powered;
    bool int1_isr;
    volatile uint32_t int1_count;
};

static bool gpio_ok(gpio_num_t gpio) {
    return gpio != GPIO_NUM_NC && GPIO_IS_VALID_GPIO(gpio);
}

static void int1_detach_isr(sc7a20h_handle_t h) {
    if (h->int1_isr && gpio_ok(h->int1_gpio)) {
        gpio_isr_handler_remove(h->int1_gpio);
        gpio_set_intr_type(h->int1_gpio, GPIO_INTR_DISABLE);
        h->int1_isr = false;
    }
}

static esp_err_t int1_as_input(sc7a20h_handle_t h) {
    if (!gpio_ok(h->int1_gpio)) return ESP_ERR_INVALID_ARG;
    int1_detach_isr(h);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->int1_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static esp_err_t write_reg(sc7a20h_handle_t h, uint8_t reg, uint8_t value) {
    const uint8_t data[] = { reg, value };
    return i2c_master_transmit(h->dev, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t read_regs(sc7a20h_handle_t h, uint8_t reg, uint8_t* data, size_t length) {
    if (length > 1) reg |= 0x80;
    return i2c_master_transmit_receive(h->dev, &reg, 1, data, length, I2C_TIMEOUT_MS);
}

static bool odr_ok(sc7a20h_odr_t odr) {
    return odr >= SC7A20H_ODR_12_5 && odr <= SC7A20H_ODR_800;
}

static bool fs_ok(sc7a20h_fs_t fs) {
    return fs <= SC7A20H_FS_16G;
}

static bool cfg_ok(const sc7a20h_sensor_config_t* cfg) {
    return cfg != NULL && odr_ok(cfg->odr) && fs_ok(cfg->fs)
        && cfg->mode <= SC7A20H_MODE_ENHANCED && cfg->osr <= SC7A20H_OSR_32
        && cfg->dlpf <= SC7A20H_DLPF_STRONG && cfg->hpf <= SC7A20H_HPF_FAST
        && (cfg->axis_mask & SC7A20H_AXIS_XYZ) != 0;
}

static void fill_sample(
    sc7a20h_handle_t h, sc7a20h_sample_t* sample,
    int16_t x, int16_t y, int16_t z, uint8_t status
) {
    sample->x_raw = (int16_t)(x >> 4);
    sample->y_raw = (int16_t)(y >> 4);
    sample->z_raw = (int16_t)(z >> 4);
    sample->x_mg = (int16_t)(sample->x_raw * h->mg_lsb);
    sample->y_mg = (int16_t)(sample->y_raw * h->mg_lsb);
    sample->z_mg = (int16_t)(sample->z_raw * h->mg_lsb);
    sample->status = status;
}

void sc7a20h_aoi_decode(uint8_t raw, sc7a20h_aoi_src_t* src) {
    if (src == NULL) return;
    src->src = raw;
    src->ia = (raw & 0x40) != 0;
    src->zh = (raw & 0x20) != 0;
    src->zl = (raw & 0x10) != 0;
    src->yh = (raw & 0x08) != 0;
    src->yl = (raw & 0x04) != 0;
    src->xh = (raw & 0x02) != 0;
    src->xl = (raw & 0x01) != 0;
}

static esp_err_t write_ctrl5(sc7a20h_handle_t h, uint8_t value) {
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL5, value);
    if (err == ESP_OK) h->ctrl5 = value;
    return err;
}

uint16_t sc7a20h_odr_hz(sc7a20h_odr_t odr) {
    static const uint16_t hz[] = { 0, 2, 13, 25, 50, 100, 200, 400, 800 };
    return odr <= SC7A20H_ODR_800 ? hz[odr] : 0;
}

uint8_t sc7a20h_ths_lsb_fs(sc7a20h_fs_t fs, uint16_t ths_mg) {
    uint16_t lsb = (uint16_t)(16u << (unsigned)fs);
    uint16_t v = ths_mg / lsb;
    return v > 127 ? 127 : (uint8_t)v;
}

uint16_t sc7a20h_ths_mg_fs(sc7a20h_fs_t fs, uint8_t lsb) {
    return (uint16_t)lsb * (uint16_t)(16u << (unsigned)fs);
}

uint8_t sc7a20h_ths_lsb(sc7a20h_handle_t h, uint16_t ths_mg) {
    if (h == NULL) return 0;
    return sc7a20h_ths_lsb_fs(h->cfg.fs, ths_mg);
}

uint16_t sc7a20h_ths_mg(sc7a20h_handle_t h, uint8_t lsb) {
    if (h == NULL) return 0;
    return sc7a20h_ths_mg_fs(h->cfg.fs, lsb);
}

const sc7a20h_sensor_config_t* sc7a20h_get_config(sc7a20h_handle_t h) {
    return h == NULL ? NULL : &h->cfg;
}

esp_err_t sc7a20h_apply_config(sc7a20h_handle_t h, const sc7a20h_sensor_config_t* config) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    if (!cfg_ok(config)) return ESP_ERR_INVALID_ARG;

    const bool hr =
        config->mode == SC7A20H_MODE_HR || config->mode == SC7A20H_MODE_ENHANCED;
    const bool lpen =
        config->mode == SC7A20H_MODE_LP || config->mode == SC7A20H_MODE_ENHANCED;
    const uint8_t ctrl0 =
        (uint8_t)(((uint8_t)config->osr << 4) | ((config->dlpf & 2) ? 0x02 : 0x00) | (hr ? 0x01 : 0x00));
    const uint8_t ctrl4 = (uint8_t)(
        CTRL4_BDU | ((uint8_t)config->fs << 4) | ((config->dlpf & 1) << 3)
    );
    const uint8_t ctrl1 = (uint8_t)(
        ((uint8_t)config->odr << 4) | (lpen ? 0x08 : 0x00) | (config->axis_mask & 0x07)
    );
    uint8_t ctrl2 = 0;
    if (config->hpf > 0) {
        ctrl2 = (uint8_t)(CTRL2_FDS | (((config->hpf - 1) & 3) << 4));
    }

    esp_err_t err;
    if ((err = write_reg(h, SC7A20H_REG_CTRL0, ctrl0)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CTRL2, ctrl2)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CTRL4, ctrl4)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CTRL1, ctrl1)) != ESP_OK) return err;

    h->cfg = *config;
    h->mg_lsb = (uint8_t)(1u << (uint8_t)config->fs);
    h->powered = true;
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t sc7a20h_power_down(sc7a20h_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL1, 0x00);
    if (err != ESP_OK) return err;
    h->powered = false;
    return ESP_OK;
}

esp_err_t sc7a20h_power_up(sc7a20h_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_STATE;
    return sc7a20h_apply_config(h, &h->cfg);
}

bool sc7a20h_powered(sc7a20h_handle_t h) {
    return h != NULL && h->powered;
}

static esp_err_t bring_up(sc7a20h_handle_t h, const sc7a20h_sensor_config_t* cfg) {
    // 士兰 12.34：0x68=0xA5 复位整个模拟/数字块。
    // / Silan 12.34: 0x68 = 0xA5 resets the whole analog/digital block.
    esp_err_t err = write_reg(h, SC7A20H_REG_SOFT_RESET, 0xA5);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t chip_id = 0;
    uint8_t version = 0;
    err = read_regs(h, SC7A20H_REG_WHO_AM_I, &chip_id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(err));
        return err;
    }
    read_regs(h, SC7A20H_REG_VERSION, &version, 1);
    if (chip_id != SC7A20H_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X, expected 0x11", chip_id);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = write_reg(h, SC7A20H_REG_CTRL5, CTRL5_BOOT);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    h->ctrl3 = 0;
    h->ctrl5 = 0;
    h->fifo_8bit = false;
    err = sc7a20h_apply_config(h, cfg);
    if (err != ESP_OK) return err;

    ESP_LOGI(
        TAG, "Ready: WHO_AM_I=0x%02X VER=0x%02X mode=%s odr=%s lsb=%umg",
        chip_id, version, sc7a20h_mode_name(cfg->mode),
        sc7a20h_odr_name(cfg->odr), (unsigned)h->mg_lsb
    );
    return ESP_OK;
}

esp_err_t sc7a20h_init(i2c_master_bus_handle_t bus_handle, const sc7a20h_config_t* config,
                       sc7a20h_handle_t* handle) {
    if (bus_handle == NULL || config == NULL || handle == NULL) return ESP_ERR_INVALID_ARG;

    sc7a20h_sensor_config_t cfg = config->sensor;
    if (cfg.axis_mask == 0 && cfg.odr == 0) cfg = (sc7a20h_sensor_config_t)SC7A20H_CONFIG_IDLE;
    if (!cfg_ok(&cfg)) return ESP_ERR_INVALID_ARG;

    sc7a20h_handle_t h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;

    h->bus = bus_handle;
    h->int1_gpio = config->int1_gpio;
    h->cfg = cfg;
    h->mg_lsb = 1;

    uint8_t addr = config->i2c_addr ? config->i2c_addr : SC7A20H_ADDR_DEFAULT;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    err = bring_up(h, &cfg);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return err;
    }

    if (gpio_ok(h->int1_gpio)) {
        err = int1_as_input(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INT1 GPIO%d input failed: %s", (int)h->int1_gpio, esp_err_to_name(err));
        }
    }

    *handle = h;
    return ESP_OK;
}

esp_err_t sc7a20h_deinit(sc7a20h_handle_t h) {
    if (h == NULL) return ESP_OK;
    int1_detach_isr(h);
    if (h->dev) {
        write_reg(h, SC7A20H_REG_CTRL1, 0x00);
        i2c_master_bus_rm_device(h->dev);
    }
    free(h);
    return ESP_OK;
}

esp_err_t sc7a20h_read(sc7a20h_handle_t h, sc7a20h_sample_t* sample) {
    if (h == NULL || sample == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t status = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_STATUS, &status, 1);
    if (err != ESP_OK) return err;

    uint8_t data[6];
    err = read_regs(h, SC7A20H_REG_OUT_X_L, data, sizeof(data));
    if (err != ESP_OK) return err;

    int16_t x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
    int16_t y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
    int16_t z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
    fill_sample(h, sample, x, y, z, status);
    return ESP_OK;
}

esp_err_t sc7a20h_read_new(sc7a20h_handle_t h, sc7a20h_sample_t* sample) {
    if (h == NULL || sample == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t status = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_STATUS, &status, 1);
    if (err != ESP_OK) return err;

    uint8_t data[6];
    err = read_regs(h, SC7A20H_REG_OUT_X_NEW, data, sizeof(data));
    if (err != ESP_OK) return err;

    int16_t x = (int16_t)((uint16_t)data[0] << 8 | data[1]);
    int16_t y = (int16_t)((uint16_t)data[2] << 8 | data[3]);
    int16_t z = (int16_t)((uint16_t)data[4] << 8 | data[5]);
    fill_sample(h, sample, x, y, z, status);
    return ESP_OK;
}

static uint8_t motion_ctrl2(sc7a20h_handle_t h, uint8_t hpis) {
    uint8_t hpcf = h->cfg.hpf > 0 ? (uint8_t)((h->cfg.hpf - 1) & 3) : 1;
    return (uint8_t)(CTRL2_FDS | (hpcf << 4) | hpis);
}

// 手册 CTRL2.HP_RESET：先吸住当前重力当参考再松开。软复位后第一次开 HPIS 不复位，桌上也会 AOI。
// / Datasheet CTRL2.HP_RESET: latch current gravity as the reference, then
// release. The first HPIS after soft reset without this trips AOI on the table.
static esp_err_t write_ctrl2_hpf(sc7a20h_handle_t h, uint8_t ctrl2) {
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL2, (uint8_t)(ctrl2 | CTRL2_HP_RESET));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(160));
    return write_reg(h, SC7A20H_REG_CTRL2, ctrl2);
}

esp_err_t sc7a20h_enable_motion(sc7a20h_handle_t h, const sc7a20h_motion_cfg_t* cfg) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_motion_cfg_t def = SC7A20H_MOTION_DEFAULT();
    if (cfg == NULL) cfg = &def;
    uint8_t axes = cfg->axis_mask & SC7A20H_AXIS_XYZ;
    if (axes == 0) axes = SC7A20H_AXIS_XYZ;

    uint8_t int_cfg = 0;
    if (axes & SC7A20H_AXIS_X) int_cfg |= SC7A20H_AOI_XH;
    if (axes & SC7A20H_AXIS_Y) int_cfg |= SC7A20H_AOI_YH;
    if (axes & SC7A20H_AXIS_Z) int_cfg |= SC7A20H_AOI_ZH;
    sc7a20h_aoi_cfg_t aoi = {
        .cfg = int_cfg,
        .ths_mg = cfg->ths_mg,
        .duration = cfg->duration,
    };
    uint8_t ctrl2 = motion_ctrl2(h, CTRL2_HPIS1);
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL2, (uint8_t)(ctrl2 | CTRL2_HP_RESET));
    if (err != ESP_OK) return err;
    err = sc7a20h_aoi_config(h, SC7A20H_AOI1, &aoi);
    if (err != ESP_OK) return err;
    err = write_ctrl5(h, (uint8_t)((h->ctrl5 & CTRL5_FIFO_EN) | CTRL5_LIR_INT1));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(160));
    err = write_reg(h, SC7A20H_REG_CTRL2, ctrl2);
    if (err != ESP_OK) return err;
    return sc7a20h_ack_int(h);
}

esp_err_t sc7a20h_arm_pickup_wake(sc7a20h_handle_t h, const sc7a20h_motion_cfg_t* cfg) {
    if (h == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_sensor_config_t saved = h->cfg;
    sc7a20h_sensor_config_t idle = SC7A20H_CONFIG_IDLE;
    idle.fs = saved.fs;
    idle.axis_mask = saved.axis_mask ? saved.axis_mask : SC7A20H_AXIS_XYZ;
    // 先断开 INT1，等高通复位完成再接到脚上，避免上电第一次把重力锁到引脚。
    // / Detach INT1 until HPF reset finishes, so the first gravity latch does not
    // stick on the pin.
    esp_err_t err = sc7a20h_int_route(h, 0);
    if (err != ESP_OK) return err;
    err = sc7a20h_apply_config(h, &idle);
    if (err != ESP_OK) return err;
    h->cfg = saved;
    err = sc7a20h_enable_motion(h, cfg);
    if (err != ESP_OK) return err;
    err = sc7a20h_ack_int(h);
    if (err != ESP_OK) return err;
    err = sc7a20h_int_route(h, SC7A20H_INT1_AOI1);
    if (err != ESP_OK) return err;
    return sc7a20h_ack_int(h);
}

esp_err_t sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t h) {
    if (h == NULL || !gpio_ok(h->int1_gpio)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = int1_as_input(h);
    if (err != ESP_OK) return err;
    err = gpio_wakeup_enable(h->int1_gpio, GPIO_INTR_HIGH_LEVEL);
    if (err != ESP_OK) return err;
    return esp_sleep_enable_gpio_wakeup();
}

esp_err_t sc7a20h_ack_int(sc7a20h_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t discard = 0;
    esp_err_t err;
    if ((err = read_regs(h, SC7A20H_REG_CLICK_SRC, &discard, 1)) != ESP_OK) return err;
    if ((err = read_regs(h, SC7A20H_REG_AOI1_SRC, &discard, 1)) != ESP_OK) return err;
    if ((err = read_regs(h, SC7A20H_REG_AOI2_SRC, &discard, 1)) != ESP_OK) return err;
    return ESP_OK;
}

esp_err_t sc7a20h_config_ext1_wakeup(sc7a20h_handle_t h) {
    if (h == NULL || !gpio_ok(h->int1_gpio)) return ESP_ERR_INVALID_ARG;
    if (!rtc_gpio_is_valid_gpio(h->int1_gpio)) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = rtc_gpio_pulldown_en(h->int1_gpio);
    if (err != ESP_OK) return err;
    err = rtc_gpio_pullup_dis(h->int1_gpio);
    if (err != ESP_OK) return err;
    return esp_sleep_enable_ext1_wakeup_io(
        1ULL << h->int1_gpio, ESP_EXT1_WAKEUP_ANY_HIGH
    );
}

esp_err_t sc7a20h_soft_reset(sc7a20h_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    return bring_up(h, &h->cfg);
}

esp_err_t sc7a20h_version(sc7a20h_handle_t h, uint8_t* whoami, uint8_t* version) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t id = 0;
    uint8_t ver = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_WHO_AM_I, &id, 1);
    if (err != ESP_OK) return err;
    err = read_regs(h, SC7A20H_REG_VERSION, &ver, 1);
    if (err != ESP_OK) return err;
    if (whoami) *whoami = id;
    if (version) *version = ver;
    return ESP_OK;
}

esp_err_t sc7a20h_dump_regs(sc7a20h_handle_t h, uint8_t* out, size_t length) {
    if (h == NULL || out == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    static const uint8_t addrs[SC7A20H_DUMP_LEN] = {
        0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27,
        0x2E, 0x2F, 0x30, 0x31, 0x38, 0x39, 0x0F, 0x70,
    };
    size_t n = length < SC7A20H_DUMP_LEN ? length : SC7A20H_DUMP_LEN;
    for (size_t i = 0; i < n; i++) {
        esp_err_t err = read_regs(h, addrs[i], &out[i], 1);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t sc7a20h_click_config(
    sc7a20h_handle_t h, uint8_t axis_mask, sc7a20h_click_ths_t ths
) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t ths_step = (uint8_t)ths;
    if (ths_step > SC7A20H_CLICK_THS_MAX) ths_step = SC7A20H_CLICK_THS_MAX;
    // 38h：B2=X、B1=Y、B0=Z，和 AXIS_X=bit0 的顺序相反。B3=LIR，读 SRC 才清。
    // / 38h: B2=X, B1=Y, B0=Z (opposite of AXIS_X=bit0). B3=LIR; clear by reading SRC.
    uint8_t en = 0;
    if (axis_mask & SC7A20H_AXIS_X) en |= 0x04;
    if (axis_mask & SC7A20H_AXIS_Y) en |= 0x02;
    if (axis_mask & SC7A20H_AXIS_Z) en |= 0x01;
    const uint8_t ctrl = (uint8_t)(0x08 | en);
    // 3Ah：PRE_QT=00（按 200Hz）、PRE_NTH=14LSB 手持也算平静、SCTH1=ths。
    // / 3Ah: PRE_QT=00 (at 200 Hz), PRE_NTH=14 LSB so handheld still counts quiet, SCTH1=ths.
    const uint8_t c1 = (uint8_t)((0 << 6) | (5 << 3) | (ths_step & 7));
    // 3Bh：QT_MT=4/200Hz，SCTH2 略高于 SCTH1，脉宽上限 8/ODR。
    // / 3Bh: QT_MT=4/200 Hz, SCTH2 a step above SCTH1, pulse-width cap 8/ODR.
    const uint8_t scth2 = ths_step > 6 ? 7 : (uint8_t)(ths_step + 1);
    const uint8_t c2 = (uint8_t)((1 << 6) | (scth2 << 3) | 2);
    const uint8_t c3 = (uint8_t)((1 << 3) | 3);
    const uint8_t c4 = 0x53;
    esp_err_t err;
    if ((err = write_reg(h, SC7A20H_REG_CLICK_CTRL, ctrl)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CLICK_C1, c1)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CLICK_C2, c2)) != ESP_OK) return err;
    if ((err = write_reg(h, SC7A20H_REG_CLICK_C3, c3)) != ESP_OK) return err;
    return write_reg(h, SC7A20H_REG_CLICK_C4, c4);
}

esp_err_t sc7a20h_click_read(sc7a20h_handle_t h, sc7a20h_click_src_t* src) {
    if (h == NULL || src == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t raw = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_CLICK_SRC, &raw, 1);
    if (err != ESP_OK) return err;
    src->src = raw;
    src->count = raw & 0x0F;
    return ESP_OK;
}

esp_err_t sc7a20h_aoi_config(sc7a20h_handle_t h, sc7a20h_aoi_t unit, const sc7a20h_aoi_cfg_t* cfg) {
    if (h == NULL || cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t base = unit == SC7A20H_AOI2 ? SC7A20H_REG_AOI2_CFG : SC7A20H_REG_AOI1_CFG;
    esp_err_t err;
    if ((err = write_reg(h, base, cfg->cfg)) != ESP_OK) return err;
    if ((err = write_reg(h, (uint8_t)(base + 2), sc7a20h_ths_lsb(h, cfg->ths_mg))) != ESP_OK) {
        return err;
    }
    return write_reg(h, (uint8_t)(base + 3), (uint8_t)(cfg->duration & 0x7F));
}

esp_err_t sc7a20h_aoi_read(sc7a20h_handle_t h, sc7a20h_aoi_t unit, sc7a20h_aoi_src_t* src) {
    if (h == NULL || src == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t raw = 0;
    uint8_t reg = unit == SC7A20H_AOI2 ? SC7A20H_REG_AOI2_SRC : SC7A20H_REG_AOI1_SRC;
    esp_err_t err = read_regs(h, reg, &raw, 1);
    if (err != ESP_OK) return err;
    sc7a20h_aoi_decode(raw, src);
    return ESP_OK;
}

esp_err_t sc7a20h_orientation_arm(
    sc7a20h_handle_t h, bool enable_4d, uint16_t ths_mg, uint8_t duration
) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_aoi_cfg_t cfg = {
        .cfg = (uint8_t)(SC7A20H_AOI_AND | SC7A20H_AOI_6D | SC7A20H_AOI_XYZ),
        .ths_mg = ths_mg,
        .duration = duration,
    };
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL2, 0x00);
    if (err != ESP_OK) return err;
    err = sc7a20h_aoi_config(h, SC7A20H_AOI1, &cfg);
    if (err != ESP_OK) return err;
    uint8_t ctrl5 = (uint8_t)((h->ctrl5 & CTRL5_FIFO_EN) | CTRL5_LIR_INT1);
    if (enable_4d) ctrl5 |= CTRL5_D4D_INT1;
    return write_ctrl5(h, ctrl5);
}

sc7a20h_orient_t sc7a20h_orientation(const sc7a20h_aoi_src_t* src) {
    if (src == NULL || !src->ia) return SC7A20H_ORIENT_UNKNOWN;
    if (src->zh) return SC7A20H_ORIENT_PZ;
    if (src->zl) return SC7A20H_ORIENT_NZ;
    if (src->xh) return SC7A20H_ORIENT_PX;
    if (src->xl) return SC7A20H_ORIENT_NX;
    if (src->yh) return SC7A20H_ORIENT_PY;
    if (src->yl) return SC7A20H_ORIENT_NY;
    return SC7A20H_ORIENT_UNKNOWN;
}

esp_err_t sc7a20h_freefall_config(sc7a20h_handle_t h, uint16_t ths_mg, uint8_t duration) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_aoi_cfg_t cfg = {
        .cfg = (uint8_t)(SC7A20H_AOI_AND | SC7A20H_AOI_6D | SC7A20H_AOI_XL
            | SC7A20H_AOI_YL | SC7A20H_AOI_ZL),
        .ths_mg = ths_mg,
        .duration = duration,
    };
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL2, 0x00);
    if (err != ESP_OK) return err;
    err = sc7a20h_aoi_config(h, SC7A20H_AOI2, &cfg);
    if (err != ESP_OK) return err;
    return write_ctrl5(h, (uint8_t)((h->ctrl5 & ~0x20) | CTRL5_LIR_INT1 | (h->ctrl5 & CTRL5_FIFO_EN)));
}

esp_err_t sc7a20h_activity_config(sc7a20h_handle_t h, uint16_t ths_mg, uint8_t duration) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_aoi_cfg_t cfg = {
        .cfg = (uint8_t)(SC7A20H_AOI_XH | SC7A20H_AOI_YH | SC7A20H_AOI_ZH),
        .ths_mg = ths_mg,
        .duration = duration,
    };
    uint8_t ctrl2 = motion_ctrl2(h, CTRL2_HPIS2);
    esp_err_t err = write_ctrl2_hpf(h, ctrl2);
    if (err != ESP_OK) return err;
    err = sc7a20h_aoi_config(h, SC7A20H_AOI2, &cfg);
    if (err != ESP_OK) return err;
    return write_ctrl5(h, (uint8_t)((h->ctrl5 & ~0x20) | CTRL5_LIR_INT1 | (h->ctrl5 & CTRL5_FIFO_EN)));
}

esp_err_t sc7a20h_fifo_config(sc7a20h_handle_t h, const sc7a20h_fifo_cfg_t* cfg) {
    if (h == NULL || cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t wtm = cfg->watermark > 31 ? 31 : cfg->watermark;
    uint8_t fifo_ctrl = (uint8_t)(((uint8_t)cfg->mode << 6) | (cfg->trigger_aoi2 ? 0x20 : 0) | wtm);
    h->fifo_8bit = cfg->bit8;
    h->ctrl3 = (uint8_t)((h->ctrl3 & ~CTRL3_FIFO_8BIT) | (cfg->bit8 ? CTRL3_FIFO_8BIT : 0));
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL3, h->ctrl3);
    if (err != ESP_OK) return err;
    if (cfg->mode == SC7A20H_FIFO_BYPASS) {
        err = write_reg(h, SC7A20H_REG_FIFO_CTRL, fifo_ctrl);
        if (err != ESP_OK) return err;
        return write_ctrl5(h, (uint8_t)(h->ctrl5 & ~CTRL5_FIFO_EN));
    }
    err = write_ctrl5(h, (uint8_t)(h->ctrl5 | CTRL5_FIFO_EN));
    if (err != ESP_OK) return err;
    return write_reg(h, SC7A20H_REG_FIFO_CTRL, fifo_ctrl);
}

esp_err_t sc7a20h_fifo_status(sc7a20h_handle_t h, sc7a20h_fifo_status_t* status) {
    if (h == NULL || status == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t raw = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_FIFO_SRC, &raw, 1);
    if (err != ESP_OK) return err;
    status->raw = raw;
    status->wtm = (raw & 0x80) != 0;
    status->overrun = (raw & 0x40) != 0;
    status->empty = (raw & 0x20) != 0;
    status->fss = raw & 0x1F;
    return ESP_OK;
}

esp_err_t sc7a20h_fifo_read(
    sc7a20h_handle_t h, sc7a20h_sample_t* samples, size_t max, size_t* out_n
) {
    if (h == NULL || samples == NULL || out_n == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_fifo_status_t st;
    esp_err_t err = sc7a20h_fifo_status(h, &st);
    if (err != ESP_OK) return err;
    // FSS 只有 5 位：满 32 组时为 0，EMPTY=0。不能把 0 当成没数据。
    // / FSS is 5 bits: full 32 samples reads 0 with EMPTY=0. Do not treat 0 as empty.
    size_t n = st.empty ? 0 : (st.fss == 0 ? (size_t)SC7A20H_FIFO_MAX : st.fss);
    if (n > max) n = max;
    if (n > SC7A20H_FIFO_MAX) n = SC7A20H_FIFO_MAX;
    *out_n = 0;
    const int bps = h->fifo_8bit ? 3 : 6;
    for (size_t i = 0; i < n; i++) {
        uint8_t data[6] = { 0 };
        for (int b = 0; b < bps; b++) {
            err = read_regs(h, SC7A20H_REG_FIFO_DATA, &data[b], 1);
            if (err != ESP_OK) return err;
        }
        int16_t x, y, z;
        if (h->fifo_8bit) {
            x = (int16_t)((int8_t)data[0] << 8);
            y = (int16_t)((int8_t)data[1] << 8);
            z = (int16_t)((int8_t)data[2] << 8);
        } else {
            x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
            y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
            z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
        }
        fill_sample(h, &samples[i], x, y, z, 0);
        (*out_n)++;
    }
    return ESP_OK;
}

esp_err_t sc7a20h_fifo_clear(sc7a20h_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_STATE;
    sc7a20h_fifo_status_t st;
    esp_err_t st_err = sc7a20h_fifo_status(h, &st);
    if (st_err != ESP_OK) return st_err;
    uint8_t saved = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_FIFO_CTRL, &saved, 1);
    if (err != ESP_OK) return err;
    err = write_reg(h, SC7A20H_REG_FIFO_CTRL, 0x00);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2));
    return write_reg(h, SC7A20H_REG_FIFO_CTRL, saved);
}

static esp_err_t wait_sample(sc7a20h_handle_t h, sc7a20h_sample_t* sample, int timeout_ms) {
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t status = 0;
        esp_err_t err = read_regs(h, SC7A20H_REG_STATUS, &status, 1);
        if (err != ESP_OK) return err;
        if (status & 0x08) return sc7a20h_read(h, sample);
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t sc7a20h_self_test(sc7a20h_handle_t h, sc7a20h_selftest_t* result) {
    if (h == NULL || result == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;

    const int n = 16;
    int32_t off[3] = { 0 };
    int32_t on[3] = { 0 };
    sc7a20h_sample_t s;
    uint8_t ctrl4 = 0;
    int timeout_ms = 4000 / (int)sc7a20h_odr_hz(h->cfg.odr) + 50;
    if (timeout_ms < 120) timeout_ms = 120;
    esp_err_t err = read_regs(h, SC7A20H_REG_CTRL4, &ctrl4, 1);
    if (err != ESP_OK) return err;

    for (int i = 0; i < n; i++) {
        err = wait_sample(h, &s, timeout_ms);
        if (err != ESP_OK) return err;
        off[0] += s.x_mg;
        off[1] += s.y_mg;
        off[2] += s.z_mg;
    }

    err = write_reg(h, SC7A20H_REG_CTRL4, (uint8_t)((ctrl4 & ~0x06) | CTRL4_ST0));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    for (int i = 0; i < n; i++) {
        err = wait_sample(h, &s, timeout_ms);
        if (err != ESP_OK) {
            write_reg(h, SC7A20H_REG_CTRL4, ctrl4);
            return err;
        }
        on[0] += s.x_mg;
        on[1] += s.y_mg;
        on[2] += s.z_mg;
    }
    write_reg(h, SC7A20H_REG_CTRL4, ctrl4);

    int16_t dx = (int16_t)(on[0] / n - off[0] / n);
    int16_t dy = (int16_t)(on[1] / n - off[1] / n);
    int16_t dz = (int16_t)(on[2] / n - off[2] / n);
    result->dx_mg = dx;
    result->dy_mg = dy;
    result->dz_mg = dz;
    int ax = abs(dx);
    int ay = abs(dy);
    int az = abs(dz);
    result->pass_x = ax >= 160 && ax <= 640;
    result->pass_y = ay >= 160 && ay <= 640;
    result->pass_z = az >= 215 && az <= 860;
    return ESP_OK;
}

static uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0;
    uint32_t b = 1u << 30;
    while (b > x) b >>= 2;
    while (b != 0) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

esp_err_t sc7a20h_stats_collect(sc7a20h_handle_t h, uint16_t n, sc7a20h_stats_t* stats) {
    if (h == NULL || stats == NULL || n < 2 || n > 256) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;

    int16_t* xs = calloc((size_t)n * 3, sizeof(int16_t));
    if (xs == NULL) return ESP_ERR_NO_MEM;
    int16_t* ys = xs + n;
    int16_t* zs = ys + n;
    uint16_t overrun = 0;
    int64_t t0 = 0;
    int timeout_ms = 4000 / (int)sc7a20h_odr_hz(h->cfg.odr) + 20;
    if (timeout_ms < 20) timeout_ms = 20;

    for (uint16_t i = 0; i < n; i++) {
        sc7a20h_sample_t s;
        esp_err_t err = wait_sample(h, &s, timeout_ms);
        if (err != ESP_OK) {
            free(xs);
            return err;
        }
        if (i == 0) t0 = esp_timer_get_time();
        if (s.status & 0x80) overrun++;
        xs[i] = s.x_mg;
        ys[i] = s.y_mg;
        zs[i] = s.z_mg;
    }
    int64_t dt = esp_timer_get_time() - t0;
    if (dt < 1) dt = 1;

    int32_t sx = 0, sy = 0, sz = 0;
    int16_t minx = xs[0], maxx = xs[0];
    int16_t miny = ys[0], maxy = ys[0];
    int16_t minz = zs[0], maxz = zs[0];
    for (uint16_t i = 0; i < n; i++) {
        sx += xs[i];
        sy += ys[i];
        sz += zs[i];
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
        if (zs[i] < minz) minz = zs[i];
        if (zs[i] > maxz) maxz = zs[i];
    }
    int16_t mx = (int16_t)(sx / n);
    int16_t my = (int16_t)(sy / n);
    int16_t mz = (int16_t)(sz / n);
    uint32_t vx = 0, vy = 0, vz = 0;
    for (uint16_t i = 0; i < n; i++) {
        int32_t dx = xs[i] - mx;
        int32_t dy = ys[i] - my;
        int32_t dz = zs[i] - mz;
        vx += (uint32_t)(dx * dx);
        vy += (uint32_t)(dy * dy);
        vz += (uint32_t)(dz * dz);
    }
    stats->mean_x = mx;
    stats->mean_y = my;
    stats->mean_z = mz;
    stats->pp_x = (int16_t)(maxx - minx);
    stats->pp_y = (int16_t)(maxy - miny);
    stats->pp_z = (int16_t)(maxz - minz);
    stats->std_x = (uint16_t)isqrt32(vx / n);
    stats->std_y = (uint16_t)isqrt32(vy / n);
    stats->std_z = (uint16_t)isqrt32(vz / n);
    stats->odr_x10 = (uint16_t)((int64_t)(n - 1) * 10000000 / dt);
    stats->overrun = overrun;
    stats->n = n;
    free(xs);
    return ESP_OK;
}

esp_err_t sc7a20h_int_route(sc7a20h_handle_t h, uint8_t mask) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    h->ctrl3 = (uint8_t)((mask & 0xF6) | (h->fifo_8bit ? CTRL3_FIFO_8BIT : 0));
    esp_err_t err = write_reg(h, SC7A20H_REG_CTRL3, h->ctrl3);
    if (err != ESP_OK) return err;
    return write_reg(h, SC7A20H_REG_CTRL6, 0x00);
}

static void IRAM_ATTR int1_isr(void* arg) {
    sc7a20h_handle_t h = (sc7a20h_handle_t)arg;
    if (h) h->int1_count++;
}

esp_err_t sc7a20h_int1_begin(sc7a20h_handle_t h) {
    if (h == NULL || !gpio_ok(h->int1_gpio)) return ESP_ERR_INVALID_ARG;
    int1_detach_isr(h);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->int1_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(h->int1_gpio, int1_isr, h);
    if (err != ESP_OK) return err;
    h->int1_isr = true;
    h->int1_count = 0;
    return ESP_OK;
}

gpio_num_t sc7a20h_int1_gpio(sc7a20h_handle_t h) {
    return h == NULL ? GPIO_NUM_NC : h->int1_gpio;
}

int sc7a20h_int1_level(sc7a20h_handle_t h) {
    if (h == NULL || !gpio_ok(h->int1_gpio)) return -1;
    return gpio_get_level(h->int1_gpio);
}

uint32_t sc7a20h_int1_count(sc7a20h_handle_t h) {
    return h == NULL ? 0 : h->int1_count;
}

esp_err_t sc7a20h_read_events(sc7a20h_handle_t h, sc7a20h_events_t* events) {
    if (h == NULL || events == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    memset(events, 0, sizeof(*events));
    esp_err_t err;
    if ((err = read_regs(h, SC7A20H_REG_CLICK_SRC, &events->click_src, 1)) != ESP_OK) {
        return err;
    }
    if ((err = read_regs(h, SC7A20H_REG_AOI1_SRC, &events->aoi1_src, 1)) != ESP_OK) {
        return err;
    }
    if ((err = read_regs(h, SC7A20H_REG_AOI2_SRC, &events->aoi2_src, 1)) != ESP_OK) {
        return err;
    }
    if ((err = read_regs(h, SC7A20H_REG_FIFO_SRC, &events->fifo_src, 1)) != ESP_OK) {
        return err;
    }
    if ((err = read_regs(h, SC7A20H_REG_STATUS, &events->status, 1)) != ESP_OK) {
        return err;
    }
    events->int1_count = h->int1_count;
    events->int1_level = sc7a20h_int1_level(h);
    return ESP_OK;
}

const char* sc7a20h_odr_name(sc7a20h_odr_t odr) {
    switch (odr) {
        case SC7A20H_ODR_12_5: return "12.5 Hz";
        case SC7A20H_ODR_25: return "25 Hz";
        case SC7A20H_ODR_50: return "50 Hz";
        case SC7A20H_ODR_100: return "100 Hz";
        case SC7A20H_ODR_200: return "200 Hz";
        case SC7A20H_ODR_400: return "400 Hz";
        case SC7A20H_ODR_800: return "800 Hz";
        default: return "--";
    }
}

const char* sc7a20h_fs_name(sc7a20h_fs_t fs) {
    switch (fs) {
        case SC7A20H_FS_2G: return "±2 g";
        case SC7A20H_FS_4G: return "±4 g";
        case SC7A20H_FS_8G: return "±8 g";
        case SC7A20H_FS_16G: return "±16 g";
        default: return "--";
    }
}

const char* sc7a20h_mode_name(sc7a20h_mode_t mode) {
    switch (mode) {
        case SC7A20H_MODE_NORMAL: return "normal";
        case SC7A20H_MODE_LP: return "low-power";
        case SC7A20H_MODE_HR: return "high-res";
        case SC7A20H_MODE_ENHANCED: return "enhanced";
        default: return "--";
    }
}

const char* sc7a20h_orient_name(sc7a20h_orient_t orient) {
    switch (orient) {
        case SC7A20H_ORIENT_PX: return "+X";
        case SC7A20H_ORIENT_NX: return "-X";
        case SC7A20H_ORIENT_PY: return "+Y";
        case SC7A20H_ORIENT_NY: return "-Y";
        case SC7A20H_ORIENT_PZ: return "+Z";
        case SC7A20H_ORIENT_NZ: return "-Z";
        default: return "unknown";
    }
}

const char* sc7a20h_click_name(uint8_t count) {
    switch (count) {
        case 0: return "none";
        case 1: return "single";
        case 2: return "double";
        case 3: return "triple";
        default: return "multi";
    }
}

const char* sc7a20h_fifo_mode_name(sc7a20h_fifo_mode_t mode) {
    switch (mode) {
        case SC7A20H_FIFO_BYPASS: return "bypass";
        case SC7A20H_FIFO_MODE: return "fifo";
        case SC7A20H_FIFO_STREAM: return "stream";
        case SC7A20H_FIFO_TRIGGER: return "trigger";
        default: return "--";
    }
}

const char* sc7a20h_int_route_name(uint8_t mask) {
    if (mask & SC7A20H_INT1_CLICK) return "click";
    if (mask & SC7A20H_INT1_AOI1) return "AOI1";
    if (mask & SC7A20H_INT1_AOI2) return "AOI2";
    if (mask & SC7A20H_INT1_DRDY) return "data-ready";
    if (mask & SC7A20H_INT1_WTM) return "watermark";
    if (mask & SC7A20H_INT1_OVERRUN) return "overrun";
    return "none";
}
