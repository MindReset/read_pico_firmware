/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file_tree.h"

static unsigned deleted_files, deleted_dirs;
static void deleted(const char *path, bool directory, void *context) {
    (void)path; (void)context;
    if (directory) ++deleted_dirs; else ++deleted_files;
}
static void make_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(!fclose(file));
}
static bool exists(const char *path) { struct stat st; return !stat(path, &st); }

int main(void) {
    char root[] = "/tmp/pico-file-tree-XXXXXX";
    assert(mkdtemp(root));
    char src[288], sub[288], file[288], copy[288], moved[288], nested[288];
    snprintf(src, sizeof(src), "%s/books", root);
    snprintf(sub, sizeof(sub), "%s/books/nested", root);
    snprintf(file, sizeof(file), "%s/books/nested/test.epub", root);
    snprintf(copy, sizeof(copy), "%s/books-copy", root);
    snprintf(moved, sizeof(moved), "%s/books-moved", root);
    snprintf(nested, sizeof(nested), "%s/books/nested/deeper", root);
    assert(!mkdir(src, 0700));
    assert(!mkdir(sub, 0700));
    make_file(file, "epub-payload");
    file_tree_info_t info;
    assert(file_tree_inspect(src, &info) == ESP_OK && info.nodes == 3 && info.bytes == 12);
    assert(file_tree_copy(src, copy, 11) == ESP_ERR_INVALID_SIZE && !exists(copy));
    assert(file_tree_copy(src, copy, 12) == ESP_OK && exists(copy));
    assert(file_tree_copy(src, copy, 12) == ESP_ERR_INVALID_STATE);
    assert(file_tree_move(src, nested) == ESP_ERR_INVALID_ARG && exists(src));
    assert(file_tree_move(src, copy) == ESP_ERR_INVALID_STATE && exists(src));
    assert(file_tree_move(src, moved) == ESP_OK && !exists(src) && exists(moved));
    assert(file_tree_same_or_below("/sdcard/books/a.epub", "/sdcard/books"));
    assert(!file_tree_same_or_below("/sdcard/books2/a.epub", "/sdcard/books"));
    assert(file_tree_delete(moved, deleted, NULL) == ESP_OK);
    assert(file_tree_delete(copy, deleted, NULL) == ESP_OK);
    assert(deleted_files == 2 && deleted_dirs == 4);
    assert(!rmdir(root));
    puts("file tree tests passed");
    return 0;
}
