/* SPDX-License-Identifier: Apache-2.0
 * 中文：书架一级子目录归类与分页记忆；路径边界严格匹配，不访问存储或UI。
 * English: Direct-child shelf categories and page memory, with strict path boundaries and no I/O or UI.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool book_shelf_child(const char *root, const char *path, char *out, size_t capacity) {
    if (!root || !path || !out || !capacity) return false;
    out[0] = 0;
    size_t n = strlen(root);
    while (n && root[n-1] == '/') --n;
    if (!n || strncmp(root,path,n) || path[n] != '/') return false;
    const char *start = path+n+1, *slash = strchr(start,'/');
    if (!slash || slash==start || start[0]=='.' || (size_t)(slash-start)>=capacity) return false;
    size_t size = (size_t)(slash-start);
    if ((size==2 && !memcmp(start,"..",2)) || (size==1 && start[0]=='.')) return false;
    memcpy(out,start,size); out[size]=0;
    return true;
}
static inline int book_shelf_saved_page(int saved, int count, int rows) {
    if (saved<0 || count<=0 || rows<=0) return 0;
    int last=(count-1)/rows;
    return saved>last ? last : saved;
}
