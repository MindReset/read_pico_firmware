/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the firmware backup format and validation against a temporary card. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __APPLE__
/* Some host libc versions predate strlcpy, which ESP-IDF provides. */
size_t strlcpy(char *dst, const char *src, size_t cap) {
    size_t length = strlen(src);
    if (cap) {
        size_t copied = length < cap - 1 ? length : cap - 1;
        memcpy(dst, src, copied);
        dst[copied] = 0;
    }
    return length;
}
#endif

#define APP_SETTINGS_BACKUP_ROOT "build/book-tests/settings-card"
#include "../main/settings.c"

static read_pico_transfer_wifi_backup_t saved_wifi = {
    .configured = 1, .ssid = "Home_2.4G", .password = "password123"
};
static unsigned wifi_imports;
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out) {
    *out = saved_wifi;
    return ESP_OK;
}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *backup) {
    if (!backup || backup->configured > 1) return false;
    if (!backup->configured) return !backup->ssid[0] && !backup->password[0];
    return backup->ssid[0] &&
           strnlen(backup->ssid, sizeof(backup->ssid)) < sizeof(backup->ssid) &&
           strnlen(backup->password, sizeof(backup->password)) < sizeof(backup->password);
}
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *backup) {
    if (!read_pico_transfer_wifi_backup_valid(backup)) return ESP_ERR_INVALID_ARG;
    saved_wifi = *backup;
    ++wifi_imports;
    return ESP_OK;
}

static unsigned history_saves, history_restores;
esp_err_t book_history_backup_write(FILE *file) {
    ++history_saves;
    return fwrite("RPHIST1", 1, 8, file) == 8 ? ESP_OK : ESP_FAIL;
}
bool book_history_backup_validate(FILE *file) {
    char marker[8];
    return fread(marker, 1, 8, file) == 8 && !memcmp(marker, "RPHIST1", 8) && fgetc(file) == EOF;
}
esp_err_t book_history_backup_restore(FILE *file) {
    ++history_restores;
    return book_history_backup_validate(file) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool card_mounted = true;
static bool commit_fails;
static int commit_count;
static uint8_t test_loaded_system_size;
static uint8_t test_loaded_fast;
static uint8_t loaded_main_mode;
static bool has_main_mode;
static uint8_t loaded_hold=APP_READER_KEY_REFRESH;
static bool has_hold;
static bool keys_v2;
static uint8_t loaded_weight=2;
static bool has_weight;
static uint8_t loaded_fine=32;static bool has_fine;
static uint8_t loaded_aa=1;
static bool has_aa;
static uint8_t loaded_adjust=20;
static bool has_adjust;
static uint8_t loaded_keys[3];
static bool has_keys[3];
static int loaded_shelf=-1, loaded_lock=-1,loaded_home=-1;
static bool loaded_shelf_v22;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) { info->mounted = card_mounted; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; (void)mode; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) { (void)h; if(!strcmp(key,NVS_KEY_BOOK_WEIGHT_FINE)&&has_fine){*value=loaded_fine;return ESP_OK;} if(!strcmp(key,NVS_KEY_HOME_COMPONENT)&&loaded_home>=0){*value=(uint8_t)loaded_home;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_AA)&&has_aa){*value=loaded_aa;return ESP_OK;} if(!strcmp(key,NVS_KEY_READER_KEYS_V2)&&keys_v2){*value=1;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_WEIGHT)&&has_weight){*value=loaded_weight;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_INDENT_ADJUST)&&has_adjust){*value=loaded_adjust;return ESP_OK;} if(!strcmp(key,NVS_KEY_SHELF_STYLE)&&loaded_shelf>=0){*value=(uint8_t)loaded_shelf;return ESP_OK;} if(!strcmp(key,NVS_KEY_SHELF_V22)&&loaded_shelf_v22){*value=1;return ESP_OK;} if(!strcmp(key,NVS_KEY_LOCK_STYLE)&&loaded_lock>=0){*value=(uint8_t)loaded_lock;return ESP_OK;} if(!strcmp(key,NVS_KEY_HOLD_ACTION)&&has_hold){*value=loaded_hold;return ESP_OK;} for(unsigned i=0;i<3;++i) if(!strcmp(key,s_reader_key_names[i])&&has_keys[i]){*value=loaded_keys[i];return ESP_OK;} if (!strcmp(key, NVS_KEY_MAIN_REFRESH) && has_main_mode) { *value=loaded_main_mode;return ESP_OK; } if (!strcmp(key, "ui_fast")) { *value = test_loaded_fast; return ESP_OK; } if (!strcmp(key, NVS_KEY_SYS_SIZE) && test_loaded_system_size) { *value = test_loaded_system_size; return ESP_OK; } return ESP_FAIL; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) { (void)h; if(!strcmp(key,NVS_KEY_BOOK_WEIGHT_FINE)){loaded_fine=value;has_fine=true;} if(!strcmp(key,NVS_KEY_BOOK_AA)){loaded_aa=value;has_aa=true;} if(!strcmp(key,NVS_KEY_READER_KEYS_V2))keys_v2=value==1; if(!strcmp(key,NVS_KEY_BOOK_WEIGHT)){loaded_weight=value;has_weight=true;} if(!strcmp(key,NVS_KEY_BOOK_INDENT_ADJUST)){loaded_adjust=value;has_adjust=true;} if(!strcmp(key,NVS_KEY_SHELF_STYLE))loaded_shelf=value; if(!strcmp(key,NVS_KEY_SHELF_V22))loaded_shelf_v22=value==1; if(!strcmp(key,NVS_KEY_LOCK_STYLE))loaded_lock=value; if(!strcmp(key,NVS_KEY_HOLD_ACTION)){loaded_hold=value;has_hold=true;} for(unsigned i=0;i<3;++i) if(!strcmp(key,s_reader_key_names[i])){loaded_keys[i]=value;has_keys[i]=true;} if (!strcmp(key, NVS_KEY_MAIN_REFRESH)) {has_main_mode=true;loaded_main_mode=value;} if (!strcmp(key, "ui_fast")) test_loaded_fast=value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size) { (void)h; (void)key; (void)value; (void)size; return ESP_FAIL; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) { (void)h; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; (void)key; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; ++commit_count; return commit_fails ? ESP_FAIL : ESP_OK; }

int main(void) {
    loaded_home=3;app_settings_init();assert(app_settings_home_component()==APP_HOME_CLOCK);
    loaded_home=APP_HOME_MOON;app_settings_init();assert(app_settings_home_component()==APP_HOME_MOON);
    loaded_home=-1;keys_v2=false;

    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    app_settings_set_book_margin(0);assert(app_settings_book_margin()==0);
    app_settings_set_book_margin(24);assert(app_settings_book_margin()==24);
    app_settings_set_book_margin(49);assert(app_settings_book_margin()==24);
    assert(app_settings_set_book_background_path("/sdcard/pictures/bg.png"));
    assert(!app_settings_set_book_background_path("/sdcard/pictures/../bad.png"));
    has_hold=true;loaded_hold=APP_READER_KEY_REFRESH;
    has_keys[0]=has_keys[1]=has_keys[2]=true;
    loaded_keys[0]=APP_READER_KEY_PREV;loaded_keys[1]=APP_READER_KEY_TOOLS;loaded_keys[2]=APP_READER_KEY_NEXT;
    app_settings_init();assert(keys_v2&&app_settings_reader_hold_action()==APP_READER_KEY_HOME&&loaded_hold==APP_READER_KEY_HOME);
    // A later explicit refresh mapping survives subsequent starts.
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_REFRESH));
    app_settings_init();assert(app_settings_reader_hold_action()==APP_READER_KEY_REFRESH);
    has_hold=false;
    app_settings_init();assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_book_weight()==0);
    assert(app_settings_set_book_weight(2)&&loaded_weight==4);
    s_book_weight=2;app_settings_init();assert(app_settings_book_weight()==2);
    assert(!app_settings_set_book_weight(3));
    commit_fails=true;assert(!app_settings_set_book_weight(-1)&&app_settings_book_weight()==2);commit_fails=false;
    assert(app_settings_set_book_weight(-1));
    loaded_weight=255;has_fine=false;app_settings_init();assert(app_settings_book_weight()==0);
    assert(app_settings_set_book_weight_fine(1)&&app_settings_book_weight_fine()==1&&loaded_fine==33);
    app_settings_init();assert(app_settings_book_weight_fine()==1);
    assert(!app_settings_set_book_weight_fine(33)&&app_settings_book_weight_fine()==1);
    commit_fails=true;assert(!app_settings_set_book_weight_fine(2)&&app_settings_book_weight_fine()==1);commit_fails=false;
    has_fine=false;loaded_weight=4;app_settings_init();assert(app_settings_book_weight_fine()==32);
    assert(app_settings_set_book_weight(1));
    assert(app_settings_book_antialias());
    assert(app_settings_set_book_antialias(false)&&loaded_aa==0);
    s_book_antialias=true;app_settings_init();assert(!app_settings_book_antialias());
    commit_fails=true;assert(!app_settings_set_book_antialias(true)&&!app_settings_book_antialias());commit_fails=false;
    assert(app_settings_set_book_antialias(true));
    loaded_aa=255;app_settings_init();assert(app_settings_book_antialias());
    assert(app_settings_set_book_antialias(false));
    app_settings_set_reader_key_preset(1);assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_system_contrast() == 100);
    assert(app_settings_book_line_spacing() == 130); /* New installations start at the middle slider stop. */

    assert(app_settings_book_indent_adjust()==0);
    app_settings_set_book_indent_adjust(-20);assert(loaded_adjust==0);
    s_book_indent_adjust=20;app_settings_init();assert(app_settings_book_indent_adjust()==-20);
    app_settings_set_book_indent_adjust(20);app_settings_set_book_indent_adjust(21);assert(loaded_adjust==40);
    loaded_adjust=255;app_settings_init();assert(app_settings_book_indent_adjust()==0);
    app_settings_set_book_indent_adjust(-7);
    has_keys[0]=has_keys[1]=has_keys[2]=true;
    loaded_keys[0]=APP_READER_KEY_PREV;loaded_keys[1]=APP_READER_KEY_HOME;loaded_keys[2]=APP_READER_KEY_NONE;
    app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    loaded_hold=APP_READER_KEY_TOOLS;has_hold=true;
    app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME && app_settings_reader_hold_action()==APP_READER_KEY_TOOLS);
    loaded_hold=APP_READER_KEY_REFRESH;app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    test_loaded_fast = 1;
    app_settings_init();
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_FAST);
    assert(app_settings_main_fast_refresh()&&loaded_main_mode==1);
    s_main_refresh=APP_MAIN_REFRESH_NORMAL;app_settings_init();assert(app_settings_main_fast_refresh());
    loaded_main_mode=3;app_settings_init();assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_WATER);
    s_main_refresh=APP_MAIN_REFRESH_NORMAL;app_settings_init();assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    assert(!app_settings_main_fast_refresh());
    (void)mkdir(APP_SETTINGS_BACKUP_ROOT, 0700);
    (void)remove(BACKUP_FILE);
    (void)remove(BACKUP_PREVIOUS);
    (void)remove(BACKUP_TEMP);
    card_mounted = false;
    assert(app_settings_backup_save() == ESP_ERR_INVALID_STATE);
    card_mounted = true;
    app_settings_set_reader_key_preset(2);
    memset(s_reader_keys,0,sizeof(s_reader_keys));app_settings_init();
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME && app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN && app_settings_reader_key_action(2)==APP_READER_KEY_TOOLS);
    app_settings_set_reader_key_preset(99);assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME);
    app_settings_set_reader_key_action(0,APP_READER_KEY_COUNT);assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME);
    int guard_commits=commit_count;
    assert(app_settings_reader_key_action_allowed(2,APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    assert(commit_count==guard_commits+1&&app_settings_reader_key_action(2)==APP_READER_KEY_REFRESH);
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_HOME));
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_set_reader_key_action(0,APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    commit_fails=true;assert(!app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN);commit_fails=false;
    loaded_keys[1]=APP_READER_KEY_FULLSCREEN;

    assert(app_settings_reader_hold_action()==APP_READER_KEY_REFRESH);
    app_settings_set_reader_hold_action(APP_READER_KEY_HOME);
    s_reader_hold_action=APP_READER_KEY_REFRESH;app_settings_init();
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    s_system_size = 200;
    s_book_px = 62;
    s_book_tracking = 4;
    s_book_indent = 3;
    app_settings_set_book_indent_adjust(-7);
    s_book_rule_offset = 7; /* +6 px */
    s_reader_full_pages = 5;
    s_reader_turn_effect = 1;
    s_reader_power_turn = true;
    s_reader_immersive = true;
    s_shelf_style = 3;
    s_staged_shutdown = true;
    s_auto_lock_minutes = 5;
    s_home_full_refresh = true;
    s_ble_turner = true;
    s_reader_hold_refresh = true;
    app_settings_set_reader_vertical_turn(true);
    assert(app_settings_reader_vertical_turn());

    strlcpy(s_device_name, "Kiiko Pico", sizeof(s_device_name));
    strlcpy(s_status_signature, "今天也要读书", sizeof(s_status_signature));
    strlcpy(s_avatar, "/sdcard/pictures/missing-avatar.jpg", sizeof(s_avatar));
    strlcpy(s_font, "/sdcard/fonts/missing.ttf", sizeof(s_font));
    s_lock_style = 1;
    strlcpy(s_wallpaper, "/sdcard/pictures/missing.jpg", sizeof(s_wallpaper));
    assert(app_settings_backup_save() == ESP_OK);
    assert(history_saves == 1);
    FILE *saved = fopen(BACKUP_FILE, "rb");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7, SEEK_SET) == 0);
    settings_backup_profile_t saved_profile;
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    settings_backup_keys_t saved_keys;
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys) && backup_keys_valid(&saved_keys));
    settings_backup_hold_t saved_hold;
    assert(fread(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold) && backup_hold_valid(&saved_hold));
    settings_backup_indent_adjust_t saved_adjust;
    assert(fread(&saved_adjust,1,sizeof(saved_adjust),saved)==sizeof(saved_adjust)&&backup_indent_adjust_valid(&saved_adjust));
    settings_backup_weight_t saved_weight;
    assert(fread(&saved_weight,1,sizeof(saved_weight),saved)==sizeof(saved_weight)&&backup_weight_valid(&saved_weight));
    assert(saved_weight.encoded==176);
    settings_backup_wifi_t saved_network;
    assert(fread(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    assert(!strcmp(saved_profile.device_name, "Kiiko Pico") &&
           !strcmp(saved_profile.status_signature, "今天也要读书") &&
           !strcmp(saved_profile.avatar, "/sdcard/pictures/missing-avatar.jpg"));
    assert(!(saved_profile.home_full_refresh & 64));
    assert(test_loaded_fast == 1);
    assert(backup_wifi_valid(&saved_network) && saved_network.credentials.configured &&
           !strcmp(saved_network.credentials.ssid, "Home_2.4G") &&
           !strcmp(saved_network.credentials.password, "password123"));
    // 旧备份仍可恢复，但退出的极速测试位被忽略；其他配置与网络继续恢复。
    // Accept old backups while ignoring the retired fast-test bit; restore other settings and WiFi.
    saved = fopen(BACKUP_FILE, "r+b");
    settings_backup_v1_t legacy_header; uint8_t legacy_ext[7];
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    saved_profile.home_full_refresh |= 64;
    uint32_t legacy_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i=0;i<4;++i) saved_profile.checksum[i]=(uint8_t)(legacy_hash>>(8*i));
    assert(fseek(saved, sizeof(legacy_header) + sizeof(legacy_ext), SEEK_SET) == 0);
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fclose(saved) == 0);
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) + sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) + (sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)) +
                            offsetof(settings_backup_wifi_t, credentials.password), SEEK_SET) == 0);
    assert(fputc('X', saved) != EOF && fclose(saved) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(!saved_wifi.configured && wifi_imports == 0);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) + sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) + (sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)), SEEK_SET) == 0);
    assert(fwrite(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    // 映射损坏或截断必须在任何设置/网络/阅读资料写入前拒绝。
    // Reject damaged mappings before writing settings, networks or history.
    long adjust_at=sizeof(settings_backup_v1_t)+7+sizeof(settings_backup_profile_t)+sizeof(settings_backup_keys_t)+sizeof(settings_backup_hold_t);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,adjust_at,SEEK_SET)==0);
    assert(fputc(41,saved)!=EOF&&fclose(saved)==0);
    int before_bad_adjust=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==before_bad_adjust&&wifi_imports==0&&history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,adjust_at,SEEK_SET)==0);
    assert(fwrite(&saved_adjust,1,sizeof(saved_adjust),saved)==sizeof(saved_adjust)&&fclose(saved)==0);
    long weight_at=adjust_at+(long)sizeof(saved_adjust);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,weight_at,SEEK_SET)==0);
    assert(fputc(5,saved)!=EOF&&fclose(saved)==0);
    int before_bad_weight=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==before_bad_weight&&wifi_imports==0&&history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,weight_at,SEEK_SET)==0);
    assert(fwrite(&saved_weight,1,sizeof(saved_weight),saved)==sizeof(saved_weight)&&fclose(saved)==0);
    long keys_at=sizeof(settings_backup_v1_t)+7+sizeof(settings_backup_profile_t);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at,SEEK_SET)==0);
    assert(fputc(APP_READER_KEY_COUNT,saved)!=EOF&&fclose(saved)==0);
    int rejected_commits=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==rejected_commits && wifi_imports==0 && history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at,SEEK_SET)==0);
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys)&&fclose(saved)==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at+(long)sizeof(saved_keys),SEEK_SET)==0);
    assert(fputc(APP_READER_KEY_COUNT,saved)!=EOF&&fclose(saved)==0);
    rejected_commits=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==rejected_commits && wifi_imports==0 && history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at+(long)sizeof(saved_keys),SEEK_SET)==0);
    assert(fwrite(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold)&&fclose(saved)==0);
    s_reader_hold_action=APP_READER_KEY_REFRESH;
    s_book_weight=2;
    s_system_size = 120;
    s_book_px = 48;
    s_book_tracking = 2;
    s_book_indent = 0;
    s_book_indent_adjust = 40;
    s_book_rule_offset = 4;
    s_reader_full_pages = 15;
    s_reader_turn_effect = 0;
    s_reader_power_turn = false;
    s_reader_immersive = false;
    s_shelf_style = 2;
    s_staged_shutdown = false;
    s_auto_lock_minutes = 0;
    s_main_refresh=APP_MAIN_REFRESH_NORMAL;
    s_home_full_refresh = false;
    s_ble_turner = false;
    s_reader_hold_refresh = false;
    app_settings_set_reader_vertical_turn(false);

    strlcpy(s_device_name, "Pico", sizeof(s_device_name));
    s_status_signature[0] = 0;
    s_avatar[0] = 0;
    s_lock_style = 0;
    s_wallpaper[0] = 0;
    app_settings_set_reader_key_preset(1);
    const int prior_restore_commits = commit_count;
    assert(app_settings_backup_restore() == ESP_OK);
    assert(history_restores == 1);
    assert(app_settings_book_weight()==1&&app_settings_book_weight_fine()==16);
    assert(!app_settings_book_antialias()&&loaded_aa==0);
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_TOOLS && app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN && app_settings_reader_key_action(2)==APP_READER_KEY_REFRESH);
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER && loaded_main_mode==2);
    assert(app_settings_system_font_size() == 200);
    assert(wifi_imports == 1 && saved_wifi.configured &&
           !strcmp(saved_wifi.ssid, "Home_2.4G") &&
           !strcmp(saved_wifi.password, "password123"));
    assert(app_settings_book_indent_adjust()==-7&&loaded_adjust==13);
    assert(s_book_px == 62 && s_book_tracking == 4 && s_book_indent == 3 &&
           s_book_rule_offset == 7 && s_reader_full_pages == 5);
    assert(s_reader_turn_effect == 1 && s_reader_power_turn && s_reader_immersive && s_shelf_style == 2);
    assert(s_staged_shutdown&&app_settings_auto_lock_minutes()==5);
    assert(s_ble_turner);
    assert(s_reader_hold_refresh);
    assert(app_settings_reader_vertical_turn());

    assert(s_home_full_refresh && !strcmp(s_device_name, "Kiiko Pico") &&
           !strcmp(s_status_signature, "今天也要读书"));
    assert(!s_avatar[0]); /* Missing avatar falls back to the default mark. */
    assert(!s_font[0]); /* Missing external font falls back to built-in. */
    assert(s_lock_style == 0 && !s_wallpaper[0]); /* Missing wallpaper uses ticket. */
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(commit_count == prior_restore_commits + 1); /* Restore commits exactly once. */
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_FAST);
    assert(app_settings_backup_save()==ESP_OK);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_NORMAL);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_main_fast_refresh());
    // v11 没有微调块；保留映射、网络及阅读记录，微调安全回到0。
    // V11 lacks the adjustment block; retain keys/network/history, defaulting adjustment to zero.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fread(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold));
    long v11_adjust_at=ftell(saved);
    assert(fseek(saved,(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)),SEEK_CUR)==0);
    long v11_network_at=ftell(saved);assert(fseek(saved,0,SEEK_END)==0);long v12_end=ftell(saved);
    size_t v11_tail_bytes=(size_t)(v12_end-v11_network_at);uint8_t *v11_tail=malloc(v11_tail_bytes);assert(v11_tail);
    assert(fseek(saved,v11_network_at,SEEK_SET)==0&&fread(v11_tail,1,v11_tail_bytes,saved)==v11_tail_bytes);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(v11_tail+sizeof(settings_backup_wifi_t), v11_tail+sizeof(settings_backup_wifi_t)+(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)), v11_tail_bytes-sizeof(settings_backup_wifi_t)-(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)));
    v11_tail_bytes-=(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));v12_end-=(long)(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));
    memcpy(legacy_header.magic,"PICOSETB",8);backup_seal(&legacy_header);
    uint32_t v11_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(v11_hash>>(8*i));
    v11_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(v11_hash>>(8*i));
    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fwrite(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold));
    assert(ftell(saved)==v11_adjust_at&&fwrite(v11_tail,1,v11_tail_bytes,saved)==v11_tail_bytes);free(v11_tail);
    assert(fflush(saved)==0&&ftruncate(fileno(saved),v12_end-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0&&fclose(saved)==0);
    app_settings_set_book_indent_adjust(12);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_indent_adjust()==0&&loaded_adjust==20&&app_settings_book_weight()==0&&app_settings_book_antialias());
    assert(app_settings_reader_hold_action()==saved_hold.action&&app_settings_reader_key_action(0)==saved_keys.actions[0]);
    assert(app_settings_backup_save()==ESP_OK);
    // v10 按键/网络/阅读数据不变；缺少长按块时恢复返回首页。
    // Preserve v10 mappings/network/history; absent hold blocks default to home.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    long v10_tail_start=ftell(saved)+(long)sizeof(settings_backup_hold_t)+(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t));
    assert(fseek(saved,0,SEEK_END)==0);long v11_end=ftell(saved);
    size_t v10_tail_size=(size_t)(v11_end-v10_tail_start);uint8_t *v10_tail=malloc(v10_tail_size);assert(v10_tail);
    assert(fseek(saved,v10_tail_start,SEEK_SET)==0&&fread(v10_tail,1,v10_tail_size,saved)==v10_tail_size);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(v10_tail+sizeof(settings_backup_wifi_t), v10_tail+sizeof(settings_backup_wifi_t)+(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)), v10_tail_size-sizeof(settings_backup_wifi_t)-(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)));
    v10_tail_size-=(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));v11_end-=(long)(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));
    saved_keys.actions[0]=APP_READER_KEY_HOME;saved_keys.actions[1]=APP_READER_KEY_FULLSCREEN;saved_keys.actions[2]=APP_READER_KEY_REFRESH;
    uint32_t missing_tools_hash=backup_keys_checksum(&saved_keys);
    for(int i=0;i<4;++i)saved_keys.checksum[i]=(uint8_t)(missing_tools_hash>>(i*8));
    memcpy(legacy_header.magic,"PICOSETA",8);backup_seal(&legacy_header);
    uint32_t v10_ext_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(v10_ext_hash>>(i*8));
    uint32_t v10_profile_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(v10_profile_hash>>(i*8));
    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fwrite(v10_tail,1,v10_tail_size,saved)==v10_tail_size);free(v10_tail);assert(fflush(saved)==0);
    assert(ftruncate(fileno(saved),v11_end-(long)sizeof(settings_backup_hold_t)-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0&&fclose(saved)==0);
    unsigned prior_wifi=wifi_imports,prior_history=history_restores;
    s_reader_hold_action=APP_READER_KEY_HOME;assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME && app_settings_main_fast_refresh() && app_settings_book_indent_adjust()==0);
    assert(wifi_imports==prior_wifi+1&&history_restores==prior_history+1);
    assert(app_settings_reader_key_action(0)==saved_keys.actions[0]&&app_settings_reader_key_action(1)==saved_keys.actions[1]&&app_settings_reader_key_action(2)==saved_keys.actions[2]);
    assert(app_settings_backup_save()==ESP_OK); // Continue v8 conversion from current v11.
    // v8 的旧实验位不启用新模式，既有备份仍可恢复。
    // A v8 retired-test bit cannot enable a new mode; existing backups remain readable.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    memcpy(legacy_header.magic,"PICOSET8",8);backup_seal(&legacy_header);legacy_ext[2]&=7;
    uint32_t ext_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(ext_hash>>(i*8));
    saved_profile.home_full_refresh=(saved_profile.home_full_refresh&127)|64;
    legacy_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(legacy_hash>>(i*8));
    long tail_start=ftell(saved)+(long)sizeof(settings_backup_keys_t)+(long)sizeof(settings_backup_hold_t)+(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t));
    assert(fseek(saved,0,SEEK_END)==0);long file_end=ftell(saved);
    size_t tail_size=(size_t)(file_end-tail_start);uint8_t *tail=malloc(tail_size);assert(tail);
    assert(fseek(saved,tail_start,SEEK_SET)==0&&fread(tail,1,tail_size,saved)==tail_size);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(tail+sizeof(settings_backup_wifi_t), tail+sizeof(settings_backup_wifi_t)+(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)), tail_size-sizeof(settings_backup_wifi_t)-(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t)));
    tail_size-=(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));file_end-=(long)(sizeof(settings_backup_background_t)+sizeof(settings_backup_home_t));
    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(tail,1,tail_size,saved)==tail_size);free(tail);assert(fflush(saved)==0);
    assert(ftruncate(fileno(saved),file_end-(long)sizeof(settings_backup_keys_t)-(long)sizeof(settings_backup_hold_t)-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0);assert(fclose(saved)==0);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL);
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_PREV && app_settings_reader_key_action(1)==APP_READER_KEY_TOOLS && app_settings_reader_key_action(2)==APP_READER_KEY_NEXT);
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    app_settings_set_reader_full_pages(30);
    assert(s_reader_full_pages == 30);
    app_settings_set_reader_full_pages(0);
    assert(s_reader_full_pages == 0);
    app_settings_set_book_reading_line_offset(-8);
    assert(app_settings_book_reading_line_offset() == -8);
    app_settings_set_book_reading_line_offset(7); /* Reject odd-pixel shifts. */
    assert(app_settings_book_reading_line_offset() == -8);

    // 长按是唯一工具栏入口的完整备份必须保持三键自定义，而不能强制覆盖中键短按。
    // A backup with hold as its only toolbar entry must retain all three short actions.
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_backup_save()==ESP_OK);
    app_settings_set_reader_key_preset(1);
    assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME && app_settings_reader_hold_action()==APP_READER_KEY_TOOLS);
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_NONE));
    // 无工具栏按键的映射在保存、恢复和重启后保持不变；固定上滑入口由阅读控制测试覆盖。
    // A mapping without toolbar keys survives save/restore/reboot; reader-control tests cover the invariant swipe entry.
    assert(app_settings_set_reader_key_action(0,APP_READER_KEY_PREV));
    assert(app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_NEXT));
    assert(app_settings_backup_save()==ESP_OK);app_settings_set_reader_key_preset(1);
    assert(app_settings_backup_restore()==ESP_OK);app_settings_init();
    assert(app_settings_reader_hold_action()==APP_READER_KEY_NONE&&app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    assert(!app_settings_reader_key_action_allowed(4,APP_READER_KEY_TOOLS));
    assert(!app_settings_reader_key_action_allowed(0,APP_READER_KEY_COUNT));
    app_settings_set_reader_key_preset(1);
    s_book_px = 70;
    assert(app_settings_backup_save() == ESP_OK); /* Overwrite an existing backup. */
    s_book_px = 48;
    commit_fails = true;
    strlcpy(saved_wifi.ssid, "Other_2.4G", sizeof(saved_wifi.ssid));
    assert(app_settings_backup_restore() == ESP_FAIL);
    assert(s_book_px == 48); /* RAM state is unchanged on commit failure. */
    assert(!strcmp(saved_wifi.ssid, "Other_2.4G"));
    commit_fails = false;
    assert(app_settings_backup_restore() == ESP_OK && s_book_px == 70 &&
           s_reader_full_pages == 0 && app_settings_book_reading_line_offset() == -8 &&
           s_staged_shutdown);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G"));

    /* PICOSET3 backups predate the shutdown choice and restore the safe off default. */
    FILE *file = fopen(BACKUP_FILE, "rb");
    assert(file);
    settings_backup_v1_t legacy;
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET3", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t v3_extension[6] = {3, 4};
    uint32_t v3_hash = backup_rule_offset_checksum(&legacy, v3_extension[0], v3_extension[1]);
    for (int i = 0; i < 4; ++i) v3_extension[i + 2] = (uint8_t)(v3_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(v3_extension, 1, sizeof(v3_extension), file) == sizeof(v3_extension));
    assert(fclose(file) == 0);
    assert(app_settings_backup_restore() == ESP_OK && !s_staged_shutdown);

    assert(!s_home_full_refresh && !strcmp(s_device_name, "kiikoread") && !s_status_signature[0]);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G")); /* Old backups leave network alone. */

    /* Existing PICOSET2 backups remain readable, with centered guide lines. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET2", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t old_extension[5] = {3};
    uint32_t old_hash = backup_indent_checksum(&legacy, old_extension[0]);
    for (int i = 0; i < 4; ++i) old_extension[i + 1] = (uint8_t)(old_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(old_extension, 1, sizeof(old_extension), file) == sizeof(old_extension));
    assert(fclose(file) == 0);
    s_book_rule_offset = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_rule_offset == 4 && !s_reader_hold_refresh);
    assert(!app_settings_reader_vertical_turn());

    /* Existing PICOSET1 backups remain readable and use the two-em default. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET1", sizeof(legacy.magic));
    backup_seal(&legacy);
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    s_book_indent = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_indent == 2);
    /* A backup without a configured network clears an existing saved network. */
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    assert(app_settings_backup_save() == ESP_OK);
    saved_wifi.configured = 1;
    strlcpy(saved_wifi.ssid, "Temporary", sizeof(saved_wifi.ssid));
    strlcpy(saved_wifi.password, "temporary123", sizeof(saved_wifi.password));
    assert(app_settings_backup_restore() == ESP_OK && !saved_wifi.configured);

    file = fopen(BACKUP_FILE, "r+b");
    assert(file);
    assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    s_book_px = 48;
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(s_book_px == 48);
    assert(remove(BACKUP_FILE) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_NOT_FOUND);
    for (int percent = 100; percent <= 200; percent += 10) {
        app_settings_set_system_font_size((uint8_t)percent);
        assert(app_settings_system_font_size() == percent);
    }
    app_settings_set_system_font_size(210);
    app_settings_set_system_font_size(195);
    assert(app_settings_system_font_size() == 200);
    // 保存失败保留当前字体和字号；同步阅读字体只在确认后单独保存。
    // Failed commits retain the current fonts and scale; reading-font sync is a separate confirmed write.
    app_settings_set_system_font_path("/sdcard/fonts/System.ttf");
    app_settings_set_font_path("/sdcard/fonts/Reading.ttf");
    app_settings_set_system_font_size(150);
    commit_fails = true;
    app_settings_set_system_font_path("/sdcard/fonts/Other.ttf");
    app_settings_set_font_path("/sdcard/fonts/Other.ttf");
    app_settings_set_system_font_size(200);
    assert(!strcmp(app_settings_system_font_path(), "/sdcard/fonts/System.ttf"));
    assert(!strcmp(app_settings_font_path(), "/sdcard/fonts/Reading.ttf"));
    assert(app_settings_system_font_size() == 150);
    commit_fails = false;
    char too_long_font[256];
    memset(too_long_font, 'x', sizeof(too_long_font)-1);
    too_long_font[sizeof(too_long_font)-1] = 0;
    app_settings_set_system_font_path(too_long_font);
    app_settings_set_font_path(too_long_font);
    assert(!strcmp(app_settings_system_font_path(), "/sdcard/fonts/System.ttf"));
    assert(!strcmp(app_settings_font_path(), "/sdcard/fonts/Reading.ttf"));
    app_settings_set_system_font_path("");
    app_settings_set_font_path("");
    assert(!app_settings_system_font_path()[0] && !app_settings_font_path()[0]);
    test_loaded_system_size = 180; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 180);
    test_loaded_system_size = 200; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 200);


    // 已删除的3/4样式在旧NVS与旧备份都回退到亚克力，1/2保持原选择。
    // Retired styles 3/4 fall back from old NVS and backups; preserve retained choices 1/2.
    for (int style=1;style<=5;++style) {
        loaded_shelf=style;loaded_shelf_v22=true;s_shelf_style=1;
        app_settings_init();assert(app_settings_shelf_style()==((style==3||style==4)?2:style));
        assert(loaded_shelf==((style==3||style==4)?2:style));
        s_shelf_style=(uint8_t)style;
        assert(app_settings_backup_save()==ESP_OK);
        s_shelf_style=1;assert(app_settings_backup_restore()==ESP_OK);
        assert(app_settings_shelf_style()==((style==3||style==4)?2:style));
    }
    for (unsigned style=0;style<3;++style) {
        app_settings_set_lock_style((uint8_t)style);
        s_lock_style=99;app_settings_init();assert(app_settings_lock_style()==style);
    }
    app_settings_set_lock_style(2);s_wallpaper[0]=0;
    assert(app_settings_backup_save()==ESP_OK);
    s_lock_style=0;assert(app_settings_backup_restore()==ESP_OK&&app_settings_lock_style()==2);
    assert(loaded_lock==2&&!s_wallpaper[0]);
    s_home_component=(app_home_component_t)3;assert(app_settings_backup_save()==ESP_OK);
    s_home_component=APP_HOME_KEHAI;assert(app_settings_backup_restore()==ESP_OK&&app_settings_home_component()==APP_HOME_CLOCK);
    assert(remove(BACKUP_FILE)==0);
    puts("settings backup host test passed (200% sizes, retired shelf migration, collage persistence/restore without wallpaper)");
    return 0;
}
