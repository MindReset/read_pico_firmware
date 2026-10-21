import sys
from pathlib import Path
ROOT = Path('/tmp/rpr')

DUMP_FUNC = r'''
// 【临时 instrumentation，第①层真机验证用，验证后整段删除】
// 只读回读，不写任何寄存器：证明布防"返回 ESP_OK 且寄存器内容正确"，证伪
// "返回 OK 但阈值/时长没落盘"这类静默失效。判据是区间断言：mg 等效 >= 950
// 且 duration == 5——1000mg 在量化公式下编码为 992mg，寄存器原值会随驱动
// 量化参数微调，钉死原值会误报，故只卡 mg 等效下界与 duration 精确值。
// / TEMPORARY instrumentation for real-hardware layer-1 verification; delete
// after sign-off. Read-only: proves the arm returned ESP_OK AND the register
// contents are right, falsifying silent "OK but threshold/duration never
// landed" failures. Verdict is a range assert: mg equivalent >= 950 and
// duration == 5 (1000mg encodes to 992mg under quantization; the raw register
// value shifts with driver quantization params, so pinning it would false-
// alarm — only the mg-equivalent floor and the exact duration are asserted).
esp_err_t sc7a20h_arm_verify_dump(sc7a20h_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t cfg = 0;
    uint8_t ths = 0;
    uint8_t dur = 0;
    esp_err_t err = read_regs(h, SC7A20H_REG_AOI1_CFG, &cfg, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "arm verify: AOI1 cfg read failed: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = read_regs(h, (uint8_t)(SC7A20H_REG_AOI1_CFG + 2), &ths, 1)) != ESP_OK) {
        ESP_LOGE(TAG, "arm verify: AOI1 ths read failed: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = read_regs(h, (uint8_t)(SC7A20H_REG_AOI1_CFG + 3), &dur, 1)) != ESP_OK) {
        ESP_LOGE(TAG, "arm verify: AOI1 dur read failed: %s", esp_err_to_name(err));
        return err;
    }
    // 反解码必须用布防写入时同一个 fs：arm 末尾已把 h->cfg 还原为 saved，
    // 这里读到的就是写入量化的那个 fs。/ Decode with the same fs the write
    // used: the arm tail restores h->cfg to saved, which is the fs the AOI
    // threshold was quantized with.
    const uint16_t mg = sc7a20h_ths_mg_fs(h->cfg.fs, ths);
    // 判据常量与方案文档《横屏双栏排版-做法方案》§13「睡眠/拿起唤醒」条目
    // 逐字对应——这里是那份判据在固件里的副本，改任一边必须同步另一边，
    // 防将来有人把这段临时代码当生产逻辑独立调参。
    // The verdict constants mirror §13 "sleep / pickup-to-wake" in the plan
    // doc verbatim: this is that criterion's in-firmware copy, and either
    // side must be changed in lockstep — guards against a future reader
    // tuning this temporary code as if it were production logic.
    const bool pass = mg >= 950 && dur == 5;
    ESP_LOGI(
        TAG, "arm verify: AOI1 cfg=0x%02X ths=%u(%u mg @ %s) dur=%u -> %s",
        cfg, ths, mg, sc7a20h_fs_name(h->cfg.fs), dur, pass ? "PASS" : "FAIL"
    );
    return pass ? ESP_OK : ESP_ERR_INVALID_STATE;
}

'''

HDR_DECL = '''/// 【临时】布防后只读回读 AOI1 阈值/时长并打日志，第①层真机验证后删除。
/// 判据常量与方案文档 §13 逐字对应，改任一边必须同步另一边。
/// / TEMP: read-only AOI1 threshold/duration readback after arm, logged for
/// real-hardware layer-1 verification; removed after sign-off. The verdict
/// constants mirror §13 of the plan doc verbatim — change in lockstep.
esp_err_t sc7a20h_arm_verify_dump(sc7a20h_handle_t h);
'''

SLEEP_CALL = '''        // 【临时 instrumentation，第①层真机验证用，验证后删除】只读回读，
        // 纯观察不改布防决策：verdict 只在日志里，返回值故意丢弃。
        // / TEMP instrumentation for real-hardware layer-1 verification;
        // removed after sign-off. Read-only and purely observational: the
        // verdict lives in the log, the return value is deliberately dropped.
        (void)sc7a20h_arm_verify_dump(acc);
'''

mode = sys.argv[1]
c = ROOT / 'components/sc7a20h/sc7a20h.c'
h = ROOT / 'components/sc7a20h/include/sc7a20h.h'
s = ROOT / 'main/sleep.c'
if mode == 'apply':
    t = c.read_text(encoding='utf-8')
    anchor = 'esp_err_t sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t h) {'
    assert t.count(anchor) == 1, 'c anchor'
    c.write_text(t.replace(anchor, DUMP_FUNC.lstrip('\n') + anchor, 1), encoding='utf-8')
    t = h.read_text(encoding='utf-8')
    anchor = 'esp_err_t sc7a20h_arm_pickup_wake(sc7a20h_handle_t h, const sc7a20h_motion_cfg_t* cfg);'
    assert t.count(anchor) == 1, 'h anchor'
    h.write_text(t.replace(anchor, anchor + '\n' + HDR_DECL, 1), encoding='utf-8')
    t = s.read_text(encoding='utf-8')
    anchor = '        acc_armed = true;\n'
    assert t.count(anchor) == 1, 'sleep anchor'
    s.write_text(t.replace(anchor, anchor + SLEEP_CALL, 1), encoding='utf-8')
    print('instrumentation applied')
else:
    import subprocess
    subprocess.run(['git','checkout','--',
                    'components/sc7a20h/sc7a20h.c',
                    'components/sc7a20h/include/sc7a20h.h',
                    'main/sleep.c'], cwd=str(ROOT), check=True)
    print('instrumentation reverted')
