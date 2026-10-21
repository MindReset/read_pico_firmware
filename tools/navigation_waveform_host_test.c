/* SPDX-License-Identifier: Apache-2.0
 * 中文：检查实际短表每对灰阶只向目标驱动，不能把它当作实屏灰阶或残影验证。
 * English: Check that every actual short-table gray pair drives only toward the target; this does not validate optical grays or ghosting.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "e0470_epaper_waveform.h"
#include "display_main_waveform.h"
#include "ui/ui_image_dither.h"

int main(void) {
    e0470_waveform_init();
    const EpdWaveformPhases* short_table = e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM, 1);
    const EpdWaveformPhases* gray_table = e0470_waveform_phases(&E0470_WAVEFORM, 5);
    assert(short_table && gray_table && short_table->phases == 8);
    const EpdWaveformPhases* original_bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
    uint8_t saved_bw[E0470_FULL_DU_FRAMES * 16 * 4], saved_gray[E0470_GL16_FRAMES * 16 * 4];
    assert(original_bw && original_bw->phases == E0470_FULL_DU_FRAMES);
    memcpy(saved_bw, original_bw->luts, sizeof(saved_bw));
    memcpy(saved_gray, gray_table->luts, sizeof(saved_gray));
    int gray_reverse_dark = 0;
    for (int from = 0; from < 16; ++from) {
        for (int to = 0; to < 16; ++to) {
            int pushes = 0;
            for (int f = 0; f < short_table->phases; ++f) {
                const int action = e0470_phase_action(short_table, f, to, from);
                if (from == to) assert(action == 0);
                else if (action) {
                    assert(action == (to > from ? 2 : 1));
                    ++pushes;
                }
            }
            if (from != to) assert(pushes > 0 && pushes <= 8);
            if (from == 0 && to == 15) assert(pushes == 8);
            if (from == 15 && to == 0) assert(pushes == 7);
            // GL16 中部分变浅迁移含反向压黑；短表所有对应迁移均已检查无反向动作。
            // Some GL16 brightening pairs include reverse darkening; every corresponding short-table pair above has no reverse action.
            if (to > from) {
                for (int f = 0; f < gray_table->phases; ++f)
                    gray_reverse_dark += e0470_phase_action(gray_table, f, to, from) == 1;
            }
        }
    }
    assert(gray_reverse_dark > 0);

    // 局部灰阶不裁短任何变色迁移，只有等灰动作清零；原始校准表不得被原地改写。
    // Local grays do not truncate changed transitions: only equal-gray actions are cleared; the original calibration table must not be modified in place.
    const EpdWaveform* local = display_main_gray_waveform();
    const EpdWaveformPhases* bands = e0470_waveform_phases(local, MODE_GL16);
    const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
    assert(local != &E0470_WAVEFORM && local->temp_intervals == E0470_WAVEFORM.temp_intervals);
    assert(bands && bands->phases == gray_table->phases && bands->phase_times == gray_table->phase_times);
    assert(bw && bw->phases == 20);
    for (int f = 0; f < bw->phases; ++f) {
        assert(e0470_phase_action(bw, f, 0, 0) == 0);
        assert(e0470_phase_action(bw, f, 15, 15) == 0);
    }
    int original_diagonal = 0;
    for (int f = 0; f < bands->phases; ++f)
        for (int from = 0; from < 16; ++from)
            for (int to = 0; to < 16; ++to) {
                int action = e0470_phase_action(bands, f, to, from);
                int original = e0470_phase_action(gray_table, f, to, from);
                if (from == to) {assert(action == 0); original_diagonal += original != 0;}
                else assert(action == original);
            }
    assert(original_diagonal > 0 && display_main_gray_waveform() == local);
    // 书架退出完整复用DU并给变白像素补四相，禁止中灰到白反向压黑。
    // Shelf exit retains full DU plus four whitening phases, forbidding reverse darkening on gray-to-white.
    const EpdWaveform* exit_wave=display_main_shelf_exit_waveform();
    const EpdWaveformPhases* exit_table=e0470_waveform_phases(exit_wave,MODE_DU);
    assert(exit_wave!=&E0470_WAVEFORM && exit_wave->temp_intervals==E0470_WAVEFORM.temp_intervals);
    assert(exit_table && exit_table->phases==24 && !exit_table->phase_times);
    int white_pushes=0;
    for(int f=0;f<exit_table->phases;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        int expected=from==to?0:f<bw->phases?e0470_phase_action(bw,f,to,from):to==15?2:0;
        int action=e0470_phase_action(exit_table,f,to,from);
        assert(action==expected);
        if(to==15)assert(action==0 || action==2);
        if(to==0)assert(action==0 || action==1);
        if(from==0 && to==15)white_pushes+=action==2;
    }
    assert(white_pushes==22 && display_main_shelf_exit_waveform()==exit_wave);
    // 柔和水波纹只改黑白端点和等灰，中间灰完整保留37相，防止丢失灰阶细节。
    // Soft water changes only monochrome endpoints and equal grays, retaining all 37 midgray phases to preserve detail.
    const EpdWaveform* water_wave=display_main_water_waveform();
    const EpdWaveformPhases* water_table=e0470_waveform_phases(water_wave,MODE_GL16);
    assert(water_wave!=&E0470_WAVEFORM && water_wave->temp_intervals==E0470_WAVEFORM.temp_intervals);
    assert(water_table && water_table->phases==37 && !water_table->phase_times);
    for(int f=0;f<water_table->phases;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        int expected=from==to?0:to==0 || to==15?
            (f<bw->phases?e0470_phase_action(bw,f,to,from):to==15 && f<24?2:0):
            e0470_phase_action(gray_table,f,to,from);
        int action=e0470_phase_action(water_table,f,to,from);
        assert(action==expected);
        if(to==15)assert(action==0 || action==2);
        if(to==0)assert(action==0 || action==1);
    }
    assert(display_main_water_waveform()==water_wave);
    const EpdWaveformPhases* first_table=e0470_waveform_phases(display_main_bw_waveform(),MODE_DU);
    assert(first_table && first_table->phases == 20);
    for (int from=0;from<16;++from) for (int to=0;to<16;++to) for (int f=0;f<20;++f)
        assert(e0470_phase_action(first_table,f,to,from)==(from==to?0:e0470_phase_action(bw,f,to,from)));
    assert(!memcmp(saved_bw,bw->luts,sizeof(saved_bw)) && !memcmp(saved_gray,gray_table->luts,sizeof(saved_gray)));
    const EpdWaveform *normal_wave=display_main_normal_waveform();
    const EpdWaveformPhases *normal_table=e0470_waveform_phases(normal_wave,MODE_GL16);
    assert(normal_table&&normal_table->phases==water_table->phases&&display_main_normal_waveform()==normal_wave);
    int softened=0;
    for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        int old_counts[4]={0},new_counts[4]={0},differences=0,first=-1,last=-1;
        for(int phase=0;phase<water_table->phases;++phase){
            int a=e0470_phase_action(water_table,phase,to,from),b=e0470_phase_action(normal_table,phase,to,from);
            ++old_counts[a];++new_counts[b];differences+=a!=b;
            if(a){if(first<0)first=phase;last=phase;}
            if(to==from||to==0||to==15||a==0||a==3)assert(a==b);
        }
        assert(!memcmp(old_counts,new_counts,sizeof(old_counts))&&differences<=4);
        if(first>=0){assert(e0470_phase_action(water_table,first,to,from)==e0470_phase_action(normal_table,first,to,from));
            assert(e0470_phase_action(water_table,last,to,from)==e0470_phase_action(normal_table,last,to,from));}
        softened+=differences>0;
    }
    assert(softened>0&&!memcmp(saved_gray,gray_table->luts,sizeof(saved_gray)));
    printf("PASS: ordinary main %d softened gray-edge paths retain all37 phases, counts, holds, endpoints; ripple and vendor outputs preserved\n",softened);
    // 点阵亮度随输入单调增加且稳定，不引入中灰；阅读插图原16灰输出仍保留。
    // Dot brightness increases monotonically and remains stable with no intermediate grays; existing 16-gray reader output is retained.
    int previous_white = 0, previous_cover_white = 0;
    for (int gray = 0; gray <= 255; ++gray) {
        int white = 0, cover_white = 0;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            int v = ui_image_dither_bw((uint8_t)gray, x, y);
            assert(v == 0 || v == 255);
            assert(v == ui_image_dither_bw((uint8_t)gray, x + 4, y + 4));
            assert(v == ui_image_dither_bw((uint8_t)gray, x - 4, y - 4));
            white += v == 255;
            int cover=ui_image_dither_cover_bw((uint8_t)gray,x,y);
            assert(cover==0 || cover==255);
            assert(cover==ui_image_dither_cover_bw((uint8_t)gray,x+4,y+4));
            assert(cover==ui_image_dither_cover_bw((uint8_t)gray,x-4,y-4));
            assert(cover>=v);
            cover_white+=cover==255;
        }
        assert(white >= previous_white); previous_white = white;
        assert(cover_white>=previous_cover_white); previous_cover_white=cover_white;
        if (gray == 0) assert(white == 0 && cover_white==1);
        if (gray == 128) assert(white == 8 && cover_white==8);
    }
    assert(previous_white == 16 && previous_cover_white==16 && ui_image_dither_gray(119, 0, 0) == 119);
    puts("navigation waveform: calibrated GL16, unchanged grays, shelf whitespace erase, original water midgrays, vendor tables and lighter covers passed; optical behavior requires hardware");
    return 0;
}
