/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { UI_AVATAR_UNAVAILABLE=-1, UI_AVATAR_PENDING, UI_AVATAR_READY } ui_avatar_state_t;
/// 只排队，不访问文件；头像任务不写页面或请求导航。/ Queue only; no file access, framebuffer writes or navigation in the worker.
void ui_avatar_request(const char *path,int side);
/// 主线程复制已完成的像素；不等待解码。/ Main-thread copy of finished pixels, without waiting for decoding.
ui_avatar_state_t ui_avatar_poll(const char *path,int side,uint8_t *pixels);
/// 卡变化时废弃结果，不等正在解码的旧任务。/ Discard stale media results without joining a decoder.
void ui_avatar_invalidate(void);
/// 仅卸载卡或睡眠需要等文件句柄收齐；切页绝不调用。/ Join file ownership only before unmount/sleep, never on navigation.
bool ui_avatar_quiesce(unsigned timeout_ms);
