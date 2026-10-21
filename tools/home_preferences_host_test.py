"""Exercise native Home preference validation, commit failures and backup block integrity.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'main/settings.c').read_text()
functions=source[source.index('app_home_component_t app_settings_home_component'):source.index('bool app_settings_home_full_refresh')]
backup=source[source.index('typedef struct { uint8_t component, minutes, checksum[4];'):source.index('static unsigned backup_version')]
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef enum {APP_HOME_FOLDER,APP_HOME_CLOCK,APP_HOME_ASTERISK=2,APP_HOME_MOON=4,APP_HOME_KEHAI=5,APP_HOME_DALI=6,APP_HOME_CAT=7,APP_HOME_HOUSE=8,APP_HOME_COMPONENT_COUNT} app_home_component_t;
enum{ESP_OK,ESP_ERR_INVALID_ARG,ESP_FAIL,NVS_READWRITE};
#define NVS_NS "read_pico"
#define NVS_KEY_HOME_COMPONENT "home_widget"
#define NVS_KEY_HOME_GOAL "home_goal"
static app_home_component_t s_home_component=APP_HOME_FOLDER;
static uint8_t s_home_goal_minutes=30;
static bool fail_open,fail_set,fail_commit;
static int writes,commits;
static esp_err_t nvs_open(const char *ns,int mode,int *h){assert(!strcmp(ns,NVS_NS)&&mode==NVS_READWRITE);*h=1;return fail_open?ESP_FAIL:ESP_OK;}
static esp_err_t nvs_set_u8(int h,const char *key,uint8_t value){assert(h==1&&key&&value<=180);++writes;return fail_set?ESP_FAIL:ESP_OK;}
static esp_err_t nvs_commit(int h){assert(h==1);++commits;return fail_commit?ESP_FAIL:ESP_OK;}
static void nvs_close(int h){assert(h==1);}
'''+functions+backup+r'''
int main(void){
 assert(app_settings_home_component()==APP_HOME_FOLDER&&app_settings_home_goal_minutes()==30);
 assert(app_settings_set_home_component(APP_HOME_COMPONENT_COUNT)==ESP_ERR_INVALID_ARG&&writes==0);
 assert(app_settings_set_home_goal_minutes(0)==ESP_ERR_INVALID_ARG);
 assert(app_settings_set_home_goal_minutes(181)==ESP_ERR_INVALID_ARG);
 assert(app_settings_set_home_goal_minutes(16)==ESP_ERR_INVALID_ARG&&writes==0);
 fail_open=true;assert(app_settings_set_home_component(APP_HOME_CLOCK)!=ESP_OK&&s_home_component==APP_HOME_FOLDER);fail_open=false;
 fail_commit=true;assert(app_settings_set_home_component(APP_HOME_CLOCK)!=ESP_OK&&s_home_component==APP_HOME_FOLDER);
 assert(app_settings_set_home_goal_minutes(60)!=ESP_OK&&s_home_goal_minutes==30);fail_commit=false;
 fail_set=true;int before=commits;assert(app_settings_set_home_goal_minutes(15)!=ESP_OK&&commits==before&&s_home_goal_minutes==30);fail_set=false;
 assert(app_settings_set_home_component(APP_HOME_CLOCK)==ESP_OK&&s_home_component==APP_HOME_CLOCK);
 assert(app_settings_set_home_component(3)==ESP_ERR_INVALID_ARG);
 for(unsigned i=APP_HOME_ASTERISK;i<APP_HOME_COMPONENT_COUNT;++i)if(i!=3)assert(app_settings_set_home_component(i)==ESP_OK&&(unsigned)s_home_component==i);
 assert(app_settings_set_home_goal_minutes(5)==ESP_OK&&s_home_goal_minutes==5);
 assert(app_settings_set_home_goal_minutes(180)==ESP_OK&&s_home_goal_minutes==180);
 settings_backup_home_t v={.component=APP_HOME_CLOCK,.minutes=60};uint32_t h=backup_home_checksum(&v);
 for(unsigned i=0;i<4;++i)v.checksum[i]=h>>(8*i);
 assert(backup_home_valid(&v));v.minutes=30;assert(!backup_home_valid(&v));v.minutes=60;
 v.component=APP_HOME_COMPONENT_COUNT;assert(!backup_home_valid(&v));v.component=APP_HOME_CLOCK;v.checksum[0]^=1;assert(!backup_home_valid(&v));
 puts("PASS: native Home defaults, value bounds, NVS open/write/commit failure preservation and independent backup checksum");
}
'''
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'preferences.c';exe=c.with_suffix('');c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
