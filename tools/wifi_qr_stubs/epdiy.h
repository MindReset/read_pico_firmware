/* 中文：二维码光栅测试边界。/ English: QR raster test boundary. */
#pragma once
#include <stdint.h>
typedef struct {int x,y,width,height;} EpdRect;
void epd_fill_rect(EpdRect area,uint8_t color,uint8_t* fb);
