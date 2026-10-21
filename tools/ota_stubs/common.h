/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffffu
#define pdPASS 1
#define pdTRUE 1
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffeu
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_CHIP_ID_ESP32S3 9
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
#define READ_PICO_TRANSFER_MODE_STA 1
#define READ_PICO_TRANSFER_ERROR 3
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_HASH_OPERATION_INIT {0}
typedef void *SemaphoreHandle_t;
typedef unsigned esp_ota_handle_t;
typedef struct {uint32_t magic_word,secure_version,reserv1[2];char version[32],project_name[32];uint8_t unused[176];} esp_app_desc_t;
typedef struct {uint8_t magic;uint8_t unused[11];uint16_t chip_id;uint8_t tail[10];} esp_image_header_t;
typedef struct {uint32_t address,length;} esp_image_segment_header_t;
typedef struct {uint32_t address,size;} esp_partition_t;
typedef struct FakeHttp *esp_http_client_handle_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {int event_id; void *user_data; char *header_key, *header_value;} esp_http_client_event_t;
typedef struct {const char *url;int timeout_ms,buffer_size,buffer_size_tx;int (*crt_bundle_attach)(void*);bool disable_auto_redirect,keep_alive_enable; int (*event_handler)(esp_http_client_event_t*);void *user_data;} esp_http_client_config_t;
typedef int wifi_ps_type_t;
#define WIFI_PS_MIN_MODEM 1
#define WIFI_PS_NONE 0
esp_err_t esp_wifi_get_ps(wifi_ps_type_t*);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t);
typedef struct {int mode,state;bool network_ready;} read_pico_transfer_status_t;
typedef struct {int mode;bool network_only;} read_pico_transfer_cfg_t;
typedef struct {size_t bytes;} psa_hash_operation_t;
void *heap_caps_malloc(size_t,int);
void *heap_caps_calloc(size_t,size_t,int);
void heap_caps_free(void*);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
int xSemaphoreTakeRecursive(SemaphoreHandle_t,uint32_t);
int xSemaphoreGiveRecursive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t,uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelete(void*);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
const esp_app_desc_t *esp_app_get_description(void);
const esp_partition_t *esp_partition_find_first(int,int,const char*);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(void*);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
void read_pico_transfer_get_status(read_pico_transfer_status_t*);
esp_err_t read_pico_transfer_get_saved_wifi(char*,bool*);
void read_pico_transfer_stop(void);
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t*);
void read_pico_transfer_service_poll(void);
esp_err_t read_pico_transfer_sync_time_online(uint32_t*);
int esp_crt_bundle_attach(void*);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int);
int esp_http_client_read(esp_http_client_handle_t,char*,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
int psa_crypto_init(void);
int psa_hash_setup(psa_hash_operation_t*,int);
int psa_hash_update(psa_hash_operation_t*,const void*,size_t);
int psa_hash_finish(psa_hash_operation_t*,uint8_t*,size_t,size_t*);
int psa_hash_abort(psa_hash_operation_t*);
