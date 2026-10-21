/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define FF_FS_REENTRANT 1
#define FR_OK 0
typedef uint8_t BYTE;
typedef struct {uint32_t free_clst; BYTE ldrv;} FATFS;
typedef struct {struct {FATFS *fs;}obj;} FF_DIR;
int f_opendir(FF_DIR *root,const char *path);
int f_closedir(FF_DIR *root);
int ff_mutex_take(int drive);
void ff_mutex_give(int drive);
