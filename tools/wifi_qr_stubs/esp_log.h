/* 中文：主机日志替身。/ English: Host log shim. */
#pragma once
typedef int esp_log_level_t;
#define ESP_LOG_WARN 2
static inline esp_log_level_t esp_log_level_get(const char* tag) { (void)tag; return 3; }
static inline void esp_log_level_set(const char* tag, esp_log_level_t level) { (void)tag; (void)level; }
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
