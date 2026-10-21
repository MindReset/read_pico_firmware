/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * Preserve the source on copy failure; never overwrite an existing target.
 */
#include "file_tree.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define TREE_DEPTH_MAX 8u
#define TREE_NODES_MAX 1000u
#define TREE_PATH_MAX 288u

static void *tree_buffer(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

bool file_tree_same_or_below(const char *path, const char *directory) {
    if (!path || !directory || !*directory) return false;
    size_t n = strlen(directory);
    return !strncmp(path, directory, n) && (!path[n] || path[n] == '/');
}

static bool child_path(const char *parent, const char *name, char out[TREE_PATH_MAX]) {
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/')) return false;
    int n = snprintf(out, TREE_PATH_MAX, "%s/%s", parent, name);
    return n > 0 && (unsigned)n < TREE_PATH_MAX;
}

static esp_err_t inspect_at(const char *path, unsigned depth, file_tree_info_t *info) {
    if (depth > TREE_DEPTH_MAX || ++info->nodes > TREE_NODES_MAX) return ESP_ERR_INVALID_SIZE;
    struct stat st;
    if (stat(path, &st)) return ESP_FAIL;
    if (S_ISREG(st.st_mode)) {
        if (st.st_size < 0 || UINT64_MAX - info->bytes < (uint64_t)st.st_size) return ESP_ERR_INVALID_SIZE;
        info->bytes += (uint64_t)st.st_size;
        return ESP_OK;
    }
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    DIR *dir = opendir(path);
    if (!dir) return ESP_FAIL;
    char *child = tree_buffer(TREE_PATH_MAX);
    if (!child) { closedir(dir); return ESP_ERR_NO_MEM; }
    esp_err_t err = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!child_path(path, entry->d_name, child)) { err = ESP_ERR_INVALID_SIZE; break; }
        err = inspect_at(child, depth + 1, info);
        if (err != ESP_OK) break;
    }
    free(child);
    if (closedir(dir)) err = ESP_FAIL;
    return err;
}

esp_err_t file_tree_inspect(const char *path, file_tree_info_t *info) {
    if (!path || !*path || !info) return ESP_ERR_INVALID_ARG;
    *info = (file_tree_info_t){0};
    return inspect_at(path, 0, info);
}

static esp_err_t delete_at(const char *path, unsigned depth,
                           file_tree_deleted_cb_t callback, void *ctx) {
    if (depth > TREE_DEPTH_MAX) return ESP_ERR_INVALID_SIZE;
    struct stat st;
    if (stat(path, &st)) return ESP_FAIL;
    if (S_ISREG(st.st_mode)) {
        if (unlink(path)) return ESP_FAIL;
        if (callback) callback(path, false, ctx);
        return ESP_OK;
    }
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    DIR *dir = opendir(path);
    if (!dir) return ESP_FAIL;
    char *child = tree_buffer(TREE_PATH_MAX);
    if (!child) { closedir(dir); return ESP_ERR_NO_MEM; }
    esp_err_t err = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!child_path(path, entry->d_name, child)) { err = ESP_ERR_INVALID_SIZE; break; }
        err = delete_at(child, depth + 1, callback, ctx);
        if (err != ESP_OK) break;
    }
    free(child);
    if (closedir(dir)) err = ESP_FAIL;
    if (err == ESP_OK && rmdir(path)) err = ESP_FAIL;
    if (err == ESP_OK && callback) callback(path, true, ctx);
    return err;
}

esp_err_t file_tree_delete(const char *path, file_tree_deleted_cb_t callback, void *ctx) {
    file_tree_info_t info;
    esp_err_t err = file_tree_inspect(path, &info);
    return err == ESP_OK ? delete_at(path, 0, callback, ctx) : err;
}

static esp_err_t copy_file(const char *source, const char *target) {
    unsigned char *buffer = tree_buffer(4096);
    if (!buffer) return ESP_ERR_NO_MEM;
    FILE *in = fopen(source, "rb");
    if (!in) { free(buffer); return ESP_FAIL; }
    FILE *out = fopen(target, "wb");
    if (!out) { fclose(in); free(buffer); return ESP_FAIL; }
    esp_err_t err = ESP_OK;
    for (;;) {
        size_t n = fread(buffer, 1, 4096, in);
        if (n && fwrite(buffer, 1, n, out) != n) { err = ESP_FAIL; break; }
        if (n < 4096) { if (ferror(in)) err = ESP_FAIL; break; }
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
    if (fclose(in)) err = ESP_FAIL;
    if (fclose(out)) err = ESP_FAIL;
    free(buffer);
    if (err != ESP_OK) (void)unlink(target);
    return err;
}

static esp_err_t copy_at(const char *source, const char *target, unsigned depth) {
    if (depth > TREE_DEPTH_MAX) return ESP_ERR_INVALID_SIZE;
    struct stat st;
    if (stat(source, &st)) return ESP_FAIL;
    if (S_ISREG(st.st_mode)) return copy_file(source, target);
    if (!S_ISDIR(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    if (mkdir(target, 0775)) return ESP_FAIL;
    DIR *dir = opendir(source);
    if (!dir) { (void)rmdir(target); return ESP_FAIL; }
    char *paths = tree_buffer(TREE_PATH_MAX * 2);
    if (!paths) { closedir(dir); (void)rmdir(target); return ESP_ERR_NO_MEM; }
    char *from = paths, *to = paths + TREE_PATH_MAX;
    esp_err_t err = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!child_path(source, entry->d_name, from) || !child_path(target, entry->d_name, to)) {
            err = ESP_ERR_INVALID_SIZE; break;
        }
        err = copy_at(from, to, depth + 1);
        if (err != ESP_OK) break;
    }
    free(paths);
    if (closedir(dir)) err = ESP_FAIL;
    if (err != ESP_OK) (void)delete_at(target, depth, NULL, NULL);
    return err;
}

static esp_err_t target_available(const char *source, const char *target) {
    if (!source || !target || !*source || !*target ||
        strlen(source) >= TREE_PATH_MAX || strlen(target) >= TREE_PATH_MAX ||
        !strcmp(source, target) || file_tree_same_or_below(target, source)) return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (!stat(target, &st)) return ESP_ERR_INVALID_STATE;
    if (errno != ENOENT) return ESP_FAIL;
    char parent[TREE_PATH_MAX];
    strcpy(parent, target);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) return ESP_ERR_INVALID_ARG;
    *slash = 0;
    if (stat(parent, &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

esp_err_t file_tree_copy(const char *source, const char *target, uint64_t free_bytes) {
    esp_err_t err = target_available(source, target);
    if (err != ESP_OK) return err;
    file_tree_info_t info;
    err = file_tree_inspect(source, &info);
    if (err != ESP_OK) return err;
    if (info.bytes > free_bytes) return ESP_ERR_INVALID_SIZE;
    return copy_at(source, target, 0);
}

esp_err_t file_tree_move(const char *source, const char *target) {
    esp_err_t err = target_available(source, target);
    if (err != ESP_OK) return err;
    // A same-volume rename is atomic and needs no recursive traversal, even
    // for a large folder. The caller updates path-based metadata afterward.
    struct stat st;
    if (stat(source, &st)) return ESP_ERR_NOT_FOUND;
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return ESP_ERR_NOT_SUPPORTED;
    return rename(source, target) ? ESP_FAIL : ESP_OK;
}
