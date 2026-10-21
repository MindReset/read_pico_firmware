#!/usr/bin/env python3
"""中文：实际油量表解析与无效读数回归。/ English: Real gauge parser and invalid sample regressions."""
from pathlib import Path
import subprocess, tempfile, re
root=Path(__file__).resolve().parents[1]
source=(root/'components/read_pico_pmu/read_pico_pmu.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m, name
    n=m.end();level=1
    while level:
        if source[n]=='{':level+=1
        elif source[n]=='}':level-=1
        n+=1
    return source[m.start():n]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'driver').mkdir()
    (p/'driver/i2c_master.h').write_text('#pragma once\ntypedef void *i2c_master_bus_handle_t;\n')
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    code='#include "read_pico_pmu.h"\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\nstatic pmu_snapshot_t s_snap;\n'
    code+='\n'.join(extract(f) for f in ['pmu_crc16_ccitt_false','rd16','rd32','crc_ok','parse_status','parse_quick'])
    code+='''
static void crc(uint8_t *b,int n) {unsigned c=pmu_crc16_ccitt_false(b,n);b[n]=c;b[n+1]=c>>8;}
int main(void) {
 assert(pmu_battery_percent(&s_snap)==-1);
 uint8_t raw[64]={0};raw[4]=PMU_STATUS_BATTERY_VALID;raw[12]=0x0b;raw[13]=3;crc(raw,62);
 s_snap.present=true;s_snap.status_ok=parse_status(raw);assert(pmu_battery_percent(&s_snap)==78);
 uint8_t quick[8]={0};quick[2]=0x67;quick[3]=3;quick[5]=3;crc(quick,6);parse_quick(quick);
 assert(pmu_battery_percent(&s_snap)==87);
 quick[2]=0xe8;quick[3]=3;quick[5]=7;crc(quick,6);parse_quick(quick);
 assert(pmu_battery_percent(&s_snap)==100 && pmu_battery_charging(&s_snap));
 quick[2]=255;quick[3]=255;crc(quick,6);parse_quick(quick);assert(pmu_battery_percent(&s_snap)==-1);
 quick[2]=0;quick[3]=0;quick[5]=1;crc(quick,6);parse_quick(quick);assert(pmu_battery_percent(&s_snap)==-1);
 quick[5]=3;crc(quick,6);parse_quick(quick);assert(pmu_battery_percent(&s_snap)==0);
 quick[2]=50;quick[6]^=1;s_snap.quick_ok=false;parse_quick(quick);assert(!s_snap.quick_ok && pmu_battery_percent(&s_snap)==78);
 s_snap.status_ok=false;assert(pmu_battery_percent(&s_snap)==-1);
 s_snap.status_ok=true;s_snap.flags=0;assert(pmu_battery_percent(&s_snap)==-1);
 puts("PASS: PMU software gauge, CRC rejection, quick/status fallback, validity flags, 0/100%, unknown and charging");
}
'''
    (p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+tmp,'-I'+str(root/'components/read_pico_pmu/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
