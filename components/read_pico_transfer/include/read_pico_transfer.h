/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 双模式传书生命周期和跨任务状态；存储策略由调用方注入。
 * AP/STA transfer lifecycle and cross-task status; caller supplies storage policy.
 * 冻结：图书 TXT/EPUB；完整字体部署使用独立 TF 目录的 TTF；不依赖页面或图书实现。
 * Frozen: TXT/EPUB books and complete TTF fonts in a separate TF directory; no page or book implementation dependency.
 * 冻结：热点网页或停服后的设备触屏可配网；已有WiFi模式不接受远程修改凭据。
 * Frozen: AP webpage or stopped-service device UI may provision; STA rejects remote credential changes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "read_pico_transfer_network.h"

#define READ_PICO_TRANSFER_PASSWORD "readpico"
#define READ_PICO_TRANSFER_URL "http://192.168.4.1"

typedef enum {
    READ_PICO_TRANSFER_MODE_AP = 0, ///< 设备热点 / Device hotspot
    READ_PICO_TRANSFER_MODE_STA, ///< 已有 WiFi / Existing WiFi
} read_pico_transfer_mode_t;

typedef enum {
    READ_PICO_TRANSFER_STOPPED, ///< 已停止 / Stopped
    READ_PICO_TRANSFER_STARTING, ///< 启动中 / Starting
    READ_PICO_TRANSFER_READY, ///< 等待上传 / Ready
    READ_PICO_TRANSFER_UPLOADING, ///< 接收中 / Receiving
    READ_PICO_TRANSFER_ERROR, ///< 网络或请求失败 / Network or request failed
} read_pico_transfer_state_t;

typedef struct {
    read_pico_transfer_mode_t mode; ///< 网络模式，默认热点 / Network mode, default AP
    const char *root_dir; ///< 已挂载根目录，start 内复制 / Mounted root, copied by start
    const char *font_dir; ///< 可选 TF 字体目录；调用方须暂停 SD 字体读取至 stop 返回 / Optional TF font directory; caller must suspend SD font reads until stop returns
    bool is_flash; ///< 内置存储标志 / Internal storage flag
    size_t file_limit; ///< 单文件上限，零表示不限 / Per-file limit, zero means unlimited
    uint64_t (*free_bytes_cb)(void *ctx); ///< 查询可用字节 / Query available bytes
    void *free_bytes_ctx; ///< 回调上下文，stop 前有效 / Callback context valid until stop
    esp_err_t (*file_changed_cb)(const char *path); ///< 文件提交或删除后清除进度；失败不回滚文件 / Clear progress after commit or deletion; failure never rolls back the file
} read_pico_transfer_cfg_t;

typedef struct {
    read_pico_transfer_state_t state; ///< 当前状态 / Current state
    read_pico_transfer_mode_t mode; ///< 网络模式 / Network mode
    char url[64]; ///< 可访问地址，未联网为空 / Reachable address, empty when offline
    char wifi_ssid[33]; ///< 已保存网络名称，不含密码 / Saved network name, never a password
    bool wifi_configured; ///< 存在有效凭据 / Valid credentials saved
    bool network_ready; ///< 已取得可用网络地址 / Usable network address acquired
    unsigned sta_count; ///< 连接数 / Station count
    char ssid[33]; ///< 热点名称 / AP name
    char cur_name[121]; ///< 完整 UTF-8 文件名 / Complete UTF-8 filename
    size_t cur_bytes; ///< 已接收字节 / Received bytes
    size_t cur_total; ///< 本次总字节 / Request total bytes
    unsigned done_count; ///< 成功提交数 / Successfully committed files
    unsigned changed_count; ///< 提交、删除或进度清理重试成功的次数 / Commits, deletions, or successful progress-cleanup retries
    esp_err_t last_error; ///< 最近错误 / Last error
} read_pico_transfer_status_t;

/// 同一控制任务串行启动/停止；重复启动返回状态错误。/ Serialize start/stop on one owner task; repeated start fails.
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg);
/// 等待请求退出并释放本组件资源；可重复调用。/ Join requests and release owned resources; repeatable.
void read_pico_transfer_stop(void);
/// 原子拒绝文件操作期间的停止；成功则已停服，由同一控制任务调用。/ Atomically refuse stopping during file operations; true means stopped. Owner task only.
bool read_pico_transfer_try_stop_if_idle(void);
/// 跨任务复制一致状态快照。/ Copy a consistent snapshot across tasks.
void read_pico_transfer_get_status(read_pico_transfer_status_t *out);
/// 控制任务每500ms驱动有界连接重试与15秒超时。/ Owner task polls every 500 ms for bounded retries and a 15 s timeout.
void read_pico_transfer_service_poll(void);
/// 只读已保存SSID；未配置仍返回ESP_OK。/ Read only the saved SSID; unconfigured still returns ESP_OK.
esp_err_t read_pico_transfer_get_saved_wifi(char ssid[33], bool *configured);
/// 仅热点或停止状态可遗忘；上传中拒绝。/ Forget only while AP or stopped; rejected during upload.
esp_err_t read_pico_transfer_forget_wifi(void);
/// 停服后同步扫描2.4GHz网络；最多16个去重SSID，按信号降序，所有临时资源均释放。/ Scan synchronously while stopped; up to 16 unique SSIDs by descending RSSI; release all temporary resources.
esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX], size_t *count);
/// 停服后保存设备输入的凭据，不自动连接；密码校验与网页相同。/ Save device-entered credentials while stopped without connecting; validation matches the webpage.
esp_err_t read_pico_transfer_save_wifi(const char *ssid, const char *password);
