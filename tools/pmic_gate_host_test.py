"""板级实际 VCOM 门控与扫描稳定时间回归。/ Actual board VCOM gating and scan settling regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'components/read_pico/read_pico_board.c').read_text()
def function(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*\)\s*\{',source,re.M)
    assert m,name
    at,depth=m.end(),1
    while depth:
        if source[at]=='{':depth+=1
        elif source[at]=='}':depth-=1
        at+=1
    return source[m.start():at]
unit=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define TAG "test"
#define EPD_XSTL 46
#define BOARD_LCD_DE_SIG 0
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef struct {bool ep_mode,ep_output_enable;} epd_ctrl_state_t;
typedef struct {int vcom_mv,temperature_c;} sy7636a_status_t;
static uint8_t ioe_output,hardware;
static bool rails_on,vcom_gated,s_drive_release_retry;
static epd_ctrl_state_t *current_state;
static epd_ctrl_state_t *epd_ctrl_state(void){assert(current_state);return current_state;}
static void *s_ioe,*s_sy;
static unsigned waits,wait_us,writes;
static bool power_error,commit_error;
static int fca9555_set_output(void *h,int port,uint8_t value){(void)h;assert(!port);++writes;if(commit_error)return -1;hardware=value;return 0;}
static void esp_rom_delay_us(unsigned us){++waits;wait_us+=us;}
static void vTaskDelay(int ticks){(void)ticks;}
static void esp_rom_gpio_connect_out_signal(int pin,int sig,bool invert,bool oen){(void)pin;(void)sig;(void)invert;(void)oen;}
static int sy7636a_read(void *h,sy7636a_status_t *status){(void)h;(void)status;return -1;}
static int sy7636a_power_on(void *h);
static int sy7636a_power_off(void *h);
"""
unit+='\n'.join(re.findall(r'^#define (?:IOE_MODE|IOE_XOE|IOE_SY_VCOM_EN|VCOM_SETTLE_US)\b.*',source,re.M))+'\n'
for name in ['ioe_commit','ioe_set','board_set_ctrl','read_pico_epd_release_drive','board_poweron','board_poweroff']:unit+=function(name)+'\n'
unit+=r"""
static int sy7636a_power_on(void *h){(void)h;if(power_error)return -1;return ioe_set(IOE_SY_VCOM_EN,true);}
static int sy7636a_power_off(void *h){(void)h;return ioe_set(IOE_SY_VCOM_EN,false);}
int main(void){
 epd_ctrl_state_t state={0},mode={.ep_mode=true},output={.ep_output_enable=true};
 current_state=&state;
 state.ep_mode=true;board_set_ctrl(&state,&mode);assert(!vcom_gated&&!(hardware&IOE_SY_VCOM_EN)&&!waits);
 power_error=true;board_poweron(&state);assert(!rails_on&&!vcom_gated&&!waits);power_error=false;
 board_poweron(&state);assert(rails_on&&vcom_gated&&(hardware&IOE_SY_VCOM_EN)&&(hardware&IOE_XOE));
 board_poweron(&state);assert(!waits);
 state.ep_mode=false;board_set_ctrl(&state,&mode);assert(!(hardware&IOE_MODE)&&!(hardware&IOE_SY_VCOM_EN)&&!waits);
 // 未掩码 ep_mode 的输出变更，不能重新开启 VCOM。/ Output-only masks must not raise VCOM.
 state.ep_mode=true;board_set_ctrl(&state,&output);assert(!(hardware&IOE_SY_VCOM_EN)&&!waits);
 board_set_ctrl(&state,&mode);assert((hardware&IOE_SY_VCOM_EN)&&waits==1&&wait_us==2000);
 board_set_ctrl(&state,&mode);assert(waits==1);
 for(int i=0;i<5;++i){state.ep_mode=false;board_set_ctrl(&state,&mode);assert(!(hardware&IOE_SY_VCOM_EN));state.ep_mode=true;board_set_ctrl(&state,&mode);}
 assert(waits==6&&wait_us==12000);
 state.ep_mode=false;board_set_ctrl(&state,&mode);board_poweroff(&state);assert(!rails_on&&!vcom_gated&&!(hardware&IOE_SY_VCOM_EN)&&!(hardware&IOE_XOE)&&waits==6);
 state.ep_mode=true;board_set_ctrl(&state,&mode);assert(!(hardware&IOE_SY_VCOM_EN)&&waits==6);
 // 无变化路径：上电后没有扫描器收尾，统一结束出口仍须关闭驱动。
 // No-change path has no scanner teardown; common completion must still release drive.
 ioe_output|=0x88;
 board_poweron(&state);assert(rails_on&&(hardware&IOE_SY_VCOM_EN));
 assert(read_pico_epd_release_drive());
 assert(!(hardware&(IOE_MODE|IOE_XOE|IOE_SY_VCOM_EN))&&rails_on&&(hardware&0x88)==0x88);
 assert(!state.ep_mode&&!state.ep_output_enable);
 unsigned neutral_writes=writes;
 for(int i=0;i<10000;++i)assert(read_pico_epd_release_drive());
 assert(writes==neutral_writes);
 // 已清零的软件影子不能掩盖最后一次真实 I2C 写入失败。
 // A zeroed software shadow must not hide a failed physical I2C write.
 state.ep_mode=true;state.ep_output_enable=true;epd_ctrl_state_t both={true,true};
 board_set_ctrl(&state,&both);assert(hardware&IOE_SY_VCOM_EN);
 commit_error=true;state.ep_mode=false;state.ep_output_enable=false;board_set_ctrl(&state,&both);
 assert(!(ioe_output&(IOE_MODE|IOE_XOE|IOE_SY_VCOM_EN))&&s_drive_release_retry&&(hardware&IOE_SY_VCOM_EN));
 assert(!read_pico_epd_release_drive()&&s_drive_release_retry);
 commit_error=false;assert(read_pico_epd_release_drive()&&!s_drive_release_retry);
 assert(!(hardware&(IOE_MODE|IOE_XOE|IOE_SY_VCOM_EN)));
 neutral_writes=writes;assert(read_pico_epd_release_drive()&&writes==neutral_writes);
 puts("PASS: idle/no-change drive release, zero redundant I2C and retry after failed neutral hardware writes");
 puts("PASS: VCOM follows masked ep_mode only with powered rails, each rising edge settles for 2 ms, failed power-on and power-off retain gating boundaries");
}
"""
with tempfile.TemporaryDirectory() as folder:
    c,binary=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
