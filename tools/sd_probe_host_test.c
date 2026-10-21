/*
 * 首次无卡启动后插卡应可挂载；拔出已挂载卡仍须显式重挂。
 * A first insertion after empty boot mounts; removal of a mounted card still requires explicit remount.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "freertos/task.h"

static bool test_present, test_exfat, test_raw_error;
static bool detect_failure;
static int mount_calls, unmount_calls, test_fail_above_khz;
static esp_err_t test_mount_error = ESP_ERR_TIMEOUT;
static int clock_history[64], delays;
static uint64_t test_total=1024*512,test_free=768*512;
static esp_err_t test_info_error=ESP_OK;
static unsigned space_calls, invalidations;
static bool fat_locked;

static sdmmc_card_t test_card = {
    .cid.name = "TEST64G", .csd.capacity = 1024, .csd.sector_size = 512,
};

#define mkdir(path, mode) (0)
#include "../components/read_pico/read_pico_sd.c"
#undef mkdir

static FATFS test_fat={.free_clst=1};
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *c){assert(c==&test_card);return 0;}
int f_opendir(FF_DIR *root,const char *path){assert(!strcmp(path,"0:"));root->obj.fs=&test_fat;return FR_OK;}
int f_closedir(FF_DIR *root){assert(root->obj.fs==&test_fat);return FR_OK;}
int ff_mutex_take(int drive){assert(drive==0&&!fat_locked);fat_locked=true;return 1;}
void ff_mutex_give(int drive){assert(drive==0&&fat_locked&&test_fat.free_clst==UINT32_MAX);fat_locked=false;++invalidations;}
esp_err_t test_host_init(void){return ESP_OK;}
esp_err_t test_host_deinit(void){return ESP_OK;}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){(void)slot;(void)config;return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *out){(void)host;*out=test_card;return test_raw_error?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *c,void *buffer,size_t start,size_t count){(void)c;(void)start;assert(count==1);if(test_raw_error)return ESP_ERR_TIMEOUT;memset(buffer,0x5a,512);if(test_exfat)memcpy((char*)buffer+3,"EXFAT   ",8);return ESP_OK;}
bool read_pico_sd_present(void) { return test_present; }
esp_err_t read_pico_sd_detect(bool* present) {
    if (detect_failure) return ESP_ERR_TIMEOUT;
    *present = test_present;
    return ESP_OK;
}
const char* esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
BaseType_t xTaskCreate(void (*fn)(void*), const char* name, int stack,
                       void* arg, int priority, void* handle) {
    (void)name; (void)stack; (void)priority; (void)handle;
    fn(arg);
    return pdPASS;
}
void vTaskDelay(int ticks) { assert(ticks == 250); ++delays; }
void vTaskDelete(void* handle) { (void)handle; }
esp_err_t esp_vfs_fat_sdmmc_mount(const char* path, const sdmmc_host_t* host,
                                  const sdmmc_slot_config_t* slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t* config,
                                  sdmmc_card_t** out) {
    (void)path; (void)host; (void)slot; (void)config;
    assert(mount_calls < 64);
    clock_history[mount_calls++] = host->max_freq_khz;
    assert(test_present);
    assert(!config->format_if_mount_failed);
    if(test_fail_above_khz && host->max_freq_khz > test_fail_above_khz){*out=NULL;return test_mount_error;}
    *out = &test_card;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char* path, sdmmc_card_t* mounted) {
    (void)path;
    assert(mounted == &test_card);
    ++unmount_calls;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_format(const char* path, sdmmc_card_t* mounted) {
    (void)path; (void)mounted;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_info(const char* path, uint64_t* total, uint64_t* free_bytes) {
    (void)path;
    ++space_calls;
    assert(!fat_locked);
    // 模拟真实 f_getfree：作废 FSInfo 后扫描，后续直接复用随写入变化的计数。
    // Like f_getfree, unknown FSInfo scans once; later counts follow allocation changes.
    if (test_fat.free_clst==UINT32_MAX) test_fat.free_clst=123;
    *total=test_total;*free_bytes=test_free;
    return test_info_error;
}

int main(void) {
    read_pico_sd_info_t info = {0};
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FOUND);
    assert(read_pico_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    assert(!info.present && !info.mounted && mount_calls == 0);

    test_present = true;
    assert(read_pico_sd_get_info(&info) == ESP_ERR_INVALID_STATE);
    assert(info.present && !info.mounted);
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info) == ESP_OK);
    assert(info.present && info.mounted && mount_calls == 1);

    detect_failure = true;
    assert(read_pico_sd_get_info(&info) == ESP_OK && info.mounted && info.capacity_bytes);
    assert(read_pico_sd_start_probe() == ESP_OK && mount_calls == 1);
    detect_failure = false;

    test_present = false;
    assert(read_pico_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    test_present = true;
    assert(read_pico_sd_start_probe() == ESP_ERR_INVALID_STATE);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info) == ESP_OK);
    assert(info.mounted && mount_calls == 2 && unmount_calls == 1);
    test_card.csd.capacity = 536870912u; // 256 GiB / 512 byte sectors, exceeds 32-bit byte range.
    test_fail_above_khz=10000;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted);
    assert(info.capacity_bytes==256ull*1024*1024*1024);
    assert(mount_calls==6 && delays==3);
    assert(clock_history[2]==40000 && clock_history[3]==40000 && clock_history[4]==20000 && clock_history[5]==10000);

    // CMD6 忙不能终止降速；默认频率跳过高速切换。/ CMD6 busy must fall back; default speed skips HS negotiation.
    test_mount_error=ESP_ERR_INVALID_STATE;
    test_fail_above_khz=20000;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && mount_calls==9);
    assert(clock_history[6]==40000 && clock_history[7]==40000 && clock_history[8]==20000);

    // 资源错误立即失败，初始化失败不建议格式化。/ Resource errors fail immediately; init failures never suggest format.
    test_mount_error=ESP_ERR_NO_MEM;
    test_fail_above_khz=1;
    int before=mount_calls, previous_delays=delays;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NO_MEM && !info.mounted && !info.needs_format);
    assert(mount_calls==before+1 && delays==previous_delays);
    test_mount_error=ESP_ERR_INVALID_STATE;
    before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.needs_format);
    assert(mount_calls==before+4 && delays==previous_delays+3);
    test_mount_error=ESP_FAIL;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && !info.needs_format);

    test_exfat=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && info.needs_format);
    test_raw_error=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && !info.needs_format);
    test_raw_error=test_exfat=false;

    // CD 通信未知时仍尝试实际挂载，不能假设拔卡。/ Unknown CD still permits an actual mount, not assumed removal.
    detect_failure=true;
    test_fail_above_khz=0;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.present);
    detect_failure=false;
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted);
    assert(info.space_valid && info.filesystem_bytes==test_total && info.free_bytes==test_free);
    assert(invalidations>0);
    // 多分区卡只统计挂载分区；256GiB卡不能截断，挂载状态与统计错误独立。
    test_total=128ull*1024*1024*1024; test_free=100ull*1024*1024*1024;
    unsigned before_space=space_calls;
    assert(read_pico_sd_refresh_space()==ESP_OK && space_calls==before_space+1);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.capacity_bytes==256ull*1024*1024*1024 &&
           info.filesystem_bytes==test_total && info.filesystem_bytes-info.free_bytes==28ull*1024*1024*1024);
    test_free-=512*1024;
    assert(read_pico_sd_refresh_space()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.free_bytes==test_free);
    test_info_error=ESP_FAIL;
    assert(read_pico_sd_refresh_space()==ESP_FAIL);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted&&!info.space_valid&&info.space_error==ESP_FAIL&&info.free_bytes==0);
    test_info_error=ESP_OK;test_free=test_total+1;
    assert(read_pico_sd_refresh_space()==ESP_FAIL);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&!info.space_valid);
    test_free=0;
    assert(read_pico_sd_refresh_space()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.space_valid&&info.free_bytes==0);
    test_present=false;before_space=space_calls;
    assert(read_pico_sd_refresh_space()==ESP_ERR_NOT_FOUND&&space_calls==before_space);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND&&!info.mounted&&!info.space_valid);
    puts("sd_probe: empty boot/insertion, checked CD, bounded settle/fallback, CMD6 busy, OOM, no format suggestion, 256 GiB passed");
    return 0;
}
