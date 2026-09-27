/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 在内存 FAT 盘验证真实 IDF FatFs 中文长文件名转换与打开。
 * Test Chinese long-name conversion and opening on a RAM disk using real IDF FatFs.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"

#define DISK_BYTES (4 * 1024 * 1024)
static BYTE disk[DISK_BYTES];
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {1, 0}};
static const char* names[] = {"中文书名.txt", "GBK内容测试.txt", "日本語.epub", "ASCII.txt"};

void* ff_memalloc(size_t bytes) { return malloc(bytes); }
void ff_memfree(void* p) { free(p); }
int ff_mutex_create(int volume) { (void)volume; return 1; }
void ff_mutex_delete(int volume) { (void)volume; }
int ff_mutex_take(int volume) { (void)volume; return 1; }
void ff_mutex_give(int volume) { (void)volume; }
DWORD get_fattime(void) { return (46U << 25) | (9U << 21) | (26U << 16); }
DSTATUS disk_initialize(BYTE drive) { (void)drive; return 0; }
DSTATUS disk_status(BYTE drive) { (void)drive; return 0; }
DRESULT disk_read(BYTE drive, BYTE* out, LBA_t sector, UINT count) {
    (void)drive;
    if (sector >= DISK_BYTES / 512 || count > DISK_BYTES / 512 - sector) return RES_PARERR;
    memcpy(out, disk + sector * 512, count * 512); return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE* data, LBA_t sector, UINT count) {
    (void)drive;
    if (sector >= DISK_BYTES / 512 || count > DISK_BYTES / 512 - sector) return RES_PARERR;
    memcpy(disk + sector * 512, data, count * 512); return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE cmd, void* out) {
    (void)drive;
    if (cmd == GET_SECTOR_COUNT) *(LBA_t*)out = DISK_BYTES / 512;
    else if (cmd == GET_SECTOR_SIZE) *(WORD*)out = 512;
    else if (cmd == GET_BLOCK_SIZE) *(DWORD*)out = 1;
    else if (cmd != CTRL_SYNC) return RES_PARERR;
    return RES_OK;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    FATFS fs;
    FIL file;
    UINT count;
    if (!strcmp(argv[1], "create")) {
        assert(FF_LFN_UNICODE == 2);
        BYTE work[4096];
        MKFS_PARM config = {.fmt = FM_FAT | FM_SFD};
        assert(f_mkfs("0:", &config, work, sizeof(work)) == FR_OK);
        assert(f_mount(&fs, "0:", 1) == FR_OK);
        for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
            assert(f_open(&file, names[i], FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
            assert(f_write(&file, "sample", 6, &count) == FR_OK && count == 6);
            assert(f_close(&file) == FR_OK);
        }
        assert(f_mount(NULL, "0:", 0) == FR_OK);
        FILE* image = fopen(argv[2], "wb");
        assert(image && fwrite(disk, 1, sizeof(disk), image) == sizeof(disk));
        assert(fclose(image) == 0);
        puts("Created UTF-16 LFN FAT image with Chinese, Japanese and ASCII filenames");
        return 0;
    }
    FILE* image = fopen(argv[2], "rb");
    assert(image && fread(disk, 1, sizeof(disk), image) == sizeof(disk));
    assert(fclose(image) == 0);
    assert(f_mount(&fs, "0:", 1) == FR_OK);
    FF_DIR dir;
    FILINFO info;
    assert(f_opendir(&dir, "0:") == FR_OK);
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        assert(f_readdir(&dir, &info) == FR_OK && info.fname[0]);
        if (FF_LFN_UNICODE == 2 || i == 3) assert(!strcmp(info.fname, names[i]));
        else assert(strcmp(info.fname, names[i]));
        printf("API=%s name=%s\n", FF_LFN_UNICODE == 2 ? "UTF8" : "OEM437", info.fname);
        assert(f_open(&file, info.fname, FA_READ) == FR_OK);
        char text[8] = {0};
        assert(f_read(&file, text, sizeof(text), &count) == FR_OK && count == 6);
        assert(!strcmp(text, "sample"));
        assert(f_close(&file) == FR_OK);
    }
    assert(f_closedir(&dir) == FR_OK);
    if (FF_LFN_UNICODE == 2) {
        assert(f_open(&file, names[0], FA_READ) == FR_OK);
        assert(f_close(&file) == FR_OK);
    } else assert(f_open(&file, names[0], FA_READ) == FR_NO_FILE);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    puts("Filename encoding regression passed");
}
