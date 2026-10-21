/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// 空闲时启动有界后台对时；主循环独占 PMU 与 NVS 的交接。
/// Start bounded background sync while idle; the main loop alone hands time to PMU/NVS.
void app_time_service_poll(bool idle);

/// gen22：对从 PMU 读来的本地秒做漂移补偿后原地写回。
/// 根因——PMU 无外部晶振（文档：LSI ~32kHz 供 RTC），深睡时 ESP 掉电、时间由 PMU 的
/// 内部 RC 走，实测每天快几分钟。没有网络时也要把误差压下来，故按学习到的 ppm 修正。
/// gen22: correct a PMU-sourced local second count by the learned drift, in place.
/// Root cause - the PMU has no crystal (docs: LSI ~32 kHz feeds the RTC) and the ESP
/// rail is dropped in sleep, so the PMU's internal RC carries time and runs minutes
/// fast per day. Compensation must work offline, hence the learned ppm correction.
void app_time_service_compensate(uint32_t *utc_sec);

/// gen22：一次真实校时成功后登记样本（真实 UTC + 校正前的本地秒），更新漂移率并落 NVS。
/// gen22: record a real sync sample (true UTC plus the local second count before
/// correction), refresh the drift estimate and persist it.
void app_time_service_note_sync(uint32_t true_utc, uint32_t local_before);
