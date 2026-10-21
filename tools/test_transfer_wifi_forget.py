#!/usr/bin/env python3
"""运行生产遗忘入口，复用 NVS 故障替身验证失败重试与资源保护。
Exercise the production forget API with existing NVS fault shims and resource guards.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
production = (ROOT / "components/read_pico_transfer/read_pico_transfer.c").read_text()


def function(name):
    match = re.search(r"(?m)^(?:static )?[^\n;]*\b" + name + r"\([^)]*\)\s*\{", production)
    assert match, name
    opening = production.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (production[end] == "{") - (production[end] == "}")
        end += 1
    return production[match.start():end]


# 复用真实凭据读写与已有故障替身，只替换平台边界，不复制遗忘实现。
# Reuse real credential I/O and existing fault shims; replace platform boundaries, not forget logic.
harness = (ROOT / "tools/transfer_wifi_host_test.c").read_text()
harness = harness.replace("int main(void)", "static void policy_test(void)")
harness = harness.replace('"../components/', '"' + str(ROOT / "components") + '/')
harness += r'''
enum {READ_PICO_TRANSFER_MODE_AP, READ_PICO_TRANSFER_MODE_STA};
enum {READ_PICO_TRANSFER_STOPPED, READ_PICO_TRANSFER_STARTING, READ_PICO_TRANSFER_READY,
      READ_PICO_TRANSFER_UPLOADING, READ_PICO_TRANSFER_ERROR};
typedef struct {int mode,state;bool wifi_configured;char wifi_ssid[33];} read_pico_transfer_status_t;
static read_pico_transfer_status_t s_status;
static bool s_wifi,s_started,s_config_busy;
static bool cancel_time_sync(bool automatic_only){(void)automatic_only;return true;}
static void *s_http,*s_netif;
#define portENTER_CRITICAL(lock) ((void)0)
#define portEXIT_CRITICAL(lock) ((void)0)
#define ESP_LOGI(...) ((void)0)
'''
harness += "\n".join(function(name) for name in (
    "read_pico_transfer_get_status", "claim_config", "release_config",
    "publish_credentials", "read_pico_transfer_forget_wifi"))
harness += r'''
static void saved_network(int mode, int state) {
    fail_open=fail_write=fail_commit=0;
    s_wifi=s_started=s_config_busy=false;s_http=s_netif=NULL;
    s_status=(read_pico_transfer_status_t){.mode=mode,.state=state};
    transfer_credentials_t c;
    assert(credentials_from_text("saved", "12345678", &c));
    assert(store_credentials(&c)==ESP_OK);publish_credentials(&c);
    clear_secret(&c,sizeof(c));
}
int main(void) {
    policy_test();
    // 已清理的失败连接可遗忘；NVS 提交失败保留配置与公开状态，下一次确认可重试。
    // A cleaned-up failed connection can be forgotten; failed commits retain credentials and allow retry.
    saved_network(READ_PICO_TRANSFER_MODE_STA, READ_PICO_TRANSFER_ERROR);
    fail_commit=1;
    assert(read_pico_transfer_forget_wifi()==ESP_FAIL && present && s_status.wifi_configured);
    assert(!s_config_busy && !handles && !strcmp(s_status.wifi_ssid,"saved"));
    fail_commit=0;
    assert(read_pico_transfer_forget_wifi()==ESP_OK && !present && !s_status.wifi_configured);
    assert(!s_config_busy && !handles && !s_status.wifi_ssid[0]);
    transfer_credentials_t check;
    assert(load_credentials(&check)==ESP_OK && !check.version);
    assert(read_pico_transfer_forget_wifi()==ESP_OK);
    saved_network(READ_PICO_TRANSFER_MODE_STA, READ_PICO_TRANSFER_ERROR);
    fail_open=1;
    assert(read_pico_transfer_forget_wifi()==ESP_FAIL && present && s_status.wifi_configured);
    fail_open=0;s_config_busy=true;
    assert(read_pico_transfer_forget_wifi()==ESP_ERR_INVALID_STATE && present);
    s_config_busy=false;
    // 任一 STA 资源仍活跃都拒绝；热点遗忘只删保存的 STA，不中断热点。
    // Reject any live STA resource; forgetting saved STA credentials leaves an AP intact.
    for(int resource=0;resource<4;++resource){
        saved_network(READ_PICO_TRANSFER_MODE_STA, READ_PICO_TRANSFER_ERROR);
        if(resource==0)s_wifi=true;if(resource==1)s_started=true;
        if(resource==2)s_http=&s_status;if(resource==3)s_netif=&s_status;
        assert(read_pico_transfer_forget_wifi()==ESP_ERR_INVALID_STATE && present);
    }
    saved_network(READ_PICO_TRANSFER_MODE_AP, READ_PICO_TRANSFER_READY);
    s_wifi=s_started=true;s_http=s_netif=&s_status;
    assert(read_pico_transfer_forget_wifi()==ESP_OK && !present && s_wifi && s_started && s_http && s_netif);
    for(int mode=0;mode<2;++mode){
        saved_network(mode, READ_PICO_TRANSFER_UPLOADING);
        assert(read_pico_transfer_forget_wifi()==ESP_ERR_INVALID_STATE && present);
    }
    puts("transfer WiFi forget: failed-start recovery, persistence faults, busy/upload and resource guards PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="kiikoread-wifi-forget-") as directory:
    work = Path(directory)
    source = work / "test.c"
    source.write_text(harness)
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    if not (cjson / "cJSON.c").exists():
        import os
        cjson = Path(os.environ["KIIKO_CJSON_DIR"])
    # -D_GNU_SOURCE：transfer_policy.h 用 strnlen，-std=c11 严格模式下需要它才可见。
    # -D_GNU_SOURCE keeps strnlen visible in transfer_policy.h under strict -std=c11.
    subprocess.run(["cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                    "-Wno-deprecated-declarations",
                    "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(cjson), str(source), str(cjson / "cJSON.c"), "-lm",
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
