/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实 JPEG/PNG 解码、资源引用及失败清理测试。
 * English: Real JPEG/PNG decoding, resource references and failure cleanup tests.
 * 冻结：仅用于宿主测试。/ Frozen: Host tests only.
 */
#include "book_image.h"
#include "book_epub.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int image_fail_after = -1;
static uint8_t* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path,"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>=0); rewind(f);
    uint8_t* data=malloc((size_t)n+1); assert(data);
    assert(fread(data,1,(size_t)n,f)==(size_t)n); fclose(f); *size=(size_t)n; return data;
}
static void check_image(const char* path, bool png) {
    size_t len; uint8_t* data=read_file(path,&len), *pixels=NULL; uint16_t w=0,h=0;
    assert(book_image_decode(data,len,1000000,&pixels,&w,&h));
    assert(w==16 && h==8);
    if (png) { assert(pixels[0]==255 && pixels[1]==127 && pixels[2]==0); }
    else for(size_t i=0;i<(size_t)w*h;++i) assert(pixels[i]>=118 && pixels[i]<=122);
    free(pixels);
    assert(!book_image_decode(data,len,1,&pixels,&w,&h) && !pixels && !w && !h);
    assert(!book_image_decode(data,2u*1024u*1024u+1,1000000,&pixels,&w,&h));
    // 截断和单字节损坏不越界；宽容解码成功时也必须释放。
    // Truncation and single-byte corruption stay bounded; free even tolerated partial decodes.
    for (size_t n=0;n<len && n<256;++n) {
        (void)book_image_decode(data,n,1000000,&pixels,&w,&h); free(pixels);
    }
    for (size_t n=0;n<len;n+=17) {
        data[n]^=0xff;
        (void)book_image_decode(data,len,1000000,&pixels,&w,&h); free(pixels);
        data[n]^=0xff;
    }
    for(int fail=0;fail<40;++fail) {
        image_fail_after=fail;
        (void)book_image_decode(data,len,1000000,&pixels,&w,&h); free(pixels);
        image_fail_after=-1;
        assert(book_image_decode(data,len,1000000,&pixels,&w,&h)); free(pixels);
    }
    free(data);
}
int main(int argc,char** argv) {
    if(argc==4 && !strcmp(argv[1],"--expect")) {
        size_t len,n;uint8_t* data=read_file(argv[2],&len),*expected=read_file(argv[3],&n),*pixels=NULL;uint16_t w,h;
        assert(book_image_decode(data,len,1000000,&pixels,&w,&h));
        assert((size_t)w*h==n && !memcmp(pixels,expected,n));
        free(data);free(expected);free(pixels);return 0;
    }
    if (argc==3 && !strcmp(argv[1],"--inspect")) {
        size_t len; uint8_t* data=read_file(argv[2],&len), *pixels=NULL; uint16_t w,h;
        bool ok=book_image_decode(data,len,2u*1024u*1024u,&pixels,&w,&h);
        printf("image %s: %s %ux%u\n",argv[2],ok?"decoded":"placeholder",w,h);
        if(ok){char path[1024];snprintf(path,sizeof(path),"%s.pgm",argv[2]);FILE* f=fopen(path,"wb");assert(f);fprintf(f,"P5\n%u %u\n255\n",w,h);fwrite(pixels,1,(size_t)w*h,f);fclose(f);}
        free(data);free(pixels);return 0;
    }
    assert(argc==7);
    check_image(argv[1],true);check_image(argv[2],false);
    const unsigned expected[][2]={{648,863},{450,1000},{9,17}};
    for(int i=0;i<3;++i) {
        size_t len;uint8_t* data=read_file(argv[i+4],&len),*pixels=NULL;uint16_t w,h;
        assert(book_image_decode(data,len,1000000,&pixels,&w,&h));
        assert(w==expected[i][0] && h==expected[i][1]);
        for(size_t p=0;p<(size_t)w*h;++p) assert(pixels[p]==128);
        free(data);free(pixels);
    }
    book_epub_t* book=NULL;assert(book_epub_open(argv[3],&book)==ESP_OK);
    html_text_t text={0};assert(book_epub_load(book,0,&text)==ESP_OK);
    size_t decoded=0,refs=0;
    for(size_t i=0;i<text.count;++i){if(text.blocks[i].image)++decoded;if(text.blocks[i].image_src)++refs;}
    assert(refs==7 && decoded==0);
    // 每次只解码明确请求的图片；失败不改正文与块表。/ Decode only the requested image; failures leave text and blocks intact.
    decoded=0;
    for(size_t i=0;i<text.count;++i) if(text.blocks[i].image_src) {
        uint8_t* pixels=NULL;uint16_t w=0,h=0;
        esp_err_t err=book_epub_load_image(book,0,text.blocks[i].image_src,&pixels,&w,&h);
        if(err==ESP_OK){++decoded;assert(pixels && w==16 && h==8);free(pixels);}
        else assert(!pixels && !w && !h);
        assert(!text.blocks[i].image && !text.blocks[i].image_repeated && !text.blocks[i].image_title);
    }
    assert(decoded==2 && strstr(text.utf8,"before") && strstr(text.utf8,"after"));
    uint8_t* pixels=NULL;uint16_t w=0,h=0;
    image_fail_after=0;
    assert(book_epub_load_image(book,0,"../images/gray.jpg",&pixels,&w,&h)!=ESP_OK && !pixels && !w && !h);
    image_fail_after=-1;
    assert(book_epub_load_image(book,0,"../images/gray.jpg",&pixels,&w,&h)==ESP_OK);free(pixels);
    html_text_free(&text);
    assert(book_epub_load(book,1,&text)==ESP_OK && text.count==2);
    for(size_t i=0;i<text.count;++i) assert(!text.blocks[i].image && text.blocks[i].image_repeated && text.blocks[i].image_first_chapter==0);
    html_text_free(&text);
    // 标题前的图独立标记，标题后的图仍是普通图片；同资源引用沿用标记。
    // Mark the opening title image, retain ordinary images after the heading, and recognize the same resource elsewhere.
    assert(book_epub_load(book,2,&text)==ESP_OK && text.count==4);
    assert(text.blocks[0].image_title && text.blocks[0].image_repeated && text.blocks[0].image_first_chapter==0);
    assert(text.blocks[2].image_src && !text.blocks[2].image_title && text.blocks[2].image_repeated);
    html_text_free(&text);
    assert(book_epub_load(book,1,&text)==ESP_OK);
    assert(text.blocks[0].image_title && !text.blocks[1].image_title);
    html_text_free(&text);book_epub_close(book);
    // 从后章续读不能声称它是全书首次；读到前章后更新已读最早位置。
    // Resuming later cannot claim a book-wide first occurrence; visiting an earlier chapter updates the known earliest location.
    assert(book_epub_open(argv[3],&book)==ESP_OK);
    assert(book_epub_load(book,1,&text)==ESP_OK);
    for(size_t i=0;i<text.count;++i) assert(!text.blocks[i].image_repeated && text.blocks[i].image_first_chapter==1 && !text.blocks[i].image_title);
    html_text_free(&text);
    assert(book_epub_load(book,0,&text)==ESP_OK);html_text_free(&text);
    assert(book_epub_load(book,1,&text)==ESP_OK);
    for(size_t i=0;i<text.count;++i) assert(text.blocks[i].image_repeated && text.blocks[i].image_first_chapter==0);
    html_text_free(&text);book_epub_close(book);
    puts("image decode: JPEG/PNG, alpha, budget, corruption/OOM, on-demand loading and visited-chapter repeats passed");
}
