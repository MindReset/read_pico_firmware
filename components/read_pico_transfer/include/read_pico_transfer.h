/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 双模式传书生命周期和跨任务状态；存储策略由调用方注入。
 * AP/STA transfer lifecycle and cross-task status; caller supplies storage policy.
 * 冻结：图书仅接收 TXT/EPUB；字体与图片只进入各自固定目录；不依赖页面或图书实现。
 * Frozen: book uploads accept TXT/EPUB only; font and image uploads use fixed directories; no page or book implementation dependency.
 * 冻结：热点网页或停服后的设备触屏可配网；已有WiFi模式不接受远程修改凭据。
 * Frozen: AP webpage or stopped-service device UI may provision; STA rejects remote credential changes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "read_pico_transfer_network.h"

#define READ_PICO_TRANSFER_PASSWORD "kiikoread"
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
    bool network_only; ///< 仅连接已有 WiFi，不启动上传服务或访问存储 / Connect saved WiFi without upload server or storage
    const char *root_dir; ///< 已挂载根目录，start 内复制 / Mounted root, copied by start
    bool is_flash; ///< 内置存储标志 / Internal storage flag
    size_t file_limit; ///< 单文件上限，零表示不限 / Per-file limit, zero means unlimited
    uint64_t (*free_bytes_cb)(void *ctx); ///< 查询可用字节 / Query available bytes
    void *free_bytes_ctx; ///< 回调上下文，stop 前有效 / Callback context valid until stop
    bool (*title_get_cb)(const char *path, char *out, size_t cap); ///< 读取显示书名覆盖 / Read title override
    esp_err_t (*title_set_cb)(const char *path, const char *title); ///< 保存显示书名覆盖 / Save title override
    esp_err_t (*file_changed_cb)(const char *path); ///< 文件提交或删除后清除进度；失败不回滚文件 / Clear progress after commit or deletion; failure never rolls back the file
    void (*file_deleted_cb)(const char *path); ///< 网页文件管理删除后的元数据清理 / Metadata cleanup after web file deletion
    void (*file_moved_cb)(const char *old_path, const char *new_path, uint32_t size); ///< 网页重命名后的进度与字体路径迁移 / Migrate progress and font selection after rename
    void (*directory_deleted_cb)(const char *path); ///< 删除目录后清理目录设置 / Clear directory settings after deletion
    void (*directory_moved_cb)(const char *old_path, const char *new_path); ///< 重命名目录后更新目录设置 / Update directory settings after rename
    bool (*signature_get_cb)(char *out, size_t cap); ///< 复制当前签名，不依赖页面 / Copy current signature without page dependencies
    esp_err_t (*signature_set_cb)(const char *text); ///< 持久保存签名，空串清除 / Persist a signature; empty clears it
    bool (*wallpaper_set_cb)(const char *path); ///< 图片上传完成后设为锁屏壁纸 / Select an uploaded image as lock wallpaper
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
    char cur_name[241]; ///< 完整 UTF-8 文件名 / Complete UTF-8 filename
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
/// 锁屏浅睡前停用射频；唤醒后按原会话配置恢复。上传中返回 false。
bool read_pico_transfer_pause_for_sleep(void);
void read_pico_transfer_resume_after_sleep(void);
/// 飞行模式闸门：置位后 start() 一律拒绝，已运行的会话由调用方 stop()。
/// 只挡射频，不动 NVS 里的凭据，解除后可直连原网络。
/// Flight-mode gate: while set, start() is refused and the caller stops any live session.
/// Radios only; NVS credentials are untouched so the saved network works again once cleared.
void read_pico_transfer_set_rf_blocked(bool blocked);
bool read_pico_transfer_rf_blocked(void);
/// 跨任务复制一致状态快照。/ Copy a consistent snapshot across tasks.
void read_pico_transfer_get_status(read_pico_transfer_status_t *out);
/// 控制任务每500ms驱动有界连接重试与15秒超时。/ Owner task polls every 500 ms for bounded retries and a 15 s timeout.
void read_pico_transfer_service_poll(void);
/// 只读已保存SSID；未配置仍返回ESP_OK。/ Read only the saved SSID; unconfigured still returns ESP_OK.
esp_err_t read_pico_transfer_get_saved_wifi(char ssid[33], bool *configured);
typedef struct {
    uint8_t configured; ///< 是否已配网 / Whether STA credentials are configured
    char ssid[33]; ///< 已保存的网络名称 / Saved network name
    char password[65]; ///< 明文密码，调用者使用后必须清零 / Plaintext secret; caller must erase after use
} read_pico_transfer_wifi_backup_t;
/// 仅供用户主动保存配置到 TF 卡；返回明文凭据，调用者负责清零。
/// Export credentials only for user-requested TF backup; caller must erase the plaintext.
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out);
/// 恢复前校验凭据格式；不读写设备。/ Validate a backup record without accessing the device.
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *backup);
/// 恢复单个 NVS 凭据；已运行的 WiFi 会话在下次重连时采用新配置。
/// Restore the single NVS record; an active WiFi session adopts it on its next connection.
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *backup);
/// 仅热点或 STA 资源已释放时可遗忘（含启动失败）；上传中拒绝。
/// Forget while AP or after releasing STA resources, including a failed start; rejected during upload.
esp_err_t read_pico_transfer_forget_wifi(void);
/// 停服后同步扫描2.4GHz网络；最多16个去重SSID，按信号降序，临时射频和STA接口均释放。/ Scan synchronously while stopped; up to 16 unique SSIDs by descending RSSI; release temporary radio and STA resources.
esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX], size_t *count);
/// 停服后保存设备输入的凭据，不自动连接；密码校验与网页相同。/ Save device-entered credentials while stopped without connecting; validation matches the webpage.
esp_err_t read_pico_transfer_save_wifi(const char *ssid, const char *password);
/// 已连接 STA 时复用网络；停服时临时联网。成功返回 UTC 秒，保留原本在线状态。
/// Reuse a connected STA or connect briefly while stopped; return UTC seconds and preserve the prior online state.
esp_err_t read_pico_transfer_sync_time(uint32_t *utc_seconds);
/// 已连接 STA 时对时，不断开 WiFi；供 HTTPS 页面校验证书。/ Sync time on connected STA without disconnecting WiFi for HTTPS certificate checks.
esp_err_t read_pico_transfer_sync_time_online(uint32_t *utc_seconds);

/// 后台短时校准，可被手动联网或睡眠取消；不读取 TF 文件。
/// Brief background calibration, cancellable by manual networking or sleep; no TF access.
esp_err_t read_pico_transfer_sync_time_auto(uint32_t *utc_seconds);
/// 主循环消费成功校准事件，用于 PMU/NVS 交接。/ Consume successful sync events on the main loop for PMU/NVS handoff.
bool read_pico_transfer_take_time_sync(uint32_t *utc_seconds);
