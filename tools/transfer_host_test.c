/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 传书公共实现的主机回归。/ Host regression of the shared transfer implementation.
 */
#define READ_PICO_TRANSFER_HOST_TEST
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
static char stat_alias[448], stat_target[448];
static int test_stat(const char *path, struct stat *out) {
    return stat(stat_alias[0] && !strcmp(path, stat_alias) ? stat_target : path, out);
}
static int fail_write, fail_close, fatfs_replace, fail_commit, fail_remove, fail_restore;
static int test_remove(const char *path) {
    if (fail_remove) { errno = EACCES; return -1; }
    return remove(path);
}
static size_t test_write(const void *p, size_t size, size_t n, FILE *f) {
    if (fail_write) return 0;
    return fwrite(p, size, n, f);
}
static int test_close(FILE *f) {
    int result = fclose(f);
    return fail_close ? EOF : result;
}
static int test_rename(const char *from, const char *to) {
    if (fail_restore && strstr(from, ".rename-backup")) { errno = EIO; return -1; }
    if (strstr(from, ".part")) {
        FILE *old = fopen(to, "rb");
        if (old) { fclose(old); if (fatfs_replace) { errno = EEXIST; return -1; } }
        if (fail_commit) { errno = EIO; return -1; }
    }
    return rename(from, to);
}
#define fwrite test_write
#define fclose test_close
#define rename test_rename
#define remove test_remove
#define stat(path, out) test_stat(path, out)
#include "../components/read_pico_transfer/read_pico_transfer.c"
#undef fwrite
#undef fclose
#undef rename
#undef remove
#undef stat
#include <assert.h>
#include <unistd.h>

static int remaining, fail_recv, fail_after, receive_calls;
static int receive(void *ctx, char *buf, size_t n) {
    (void)ctx;
    if (fail_recv) return -1;
    if (fail_after && ++receive_calls == fail_after) return -1;
    if (n > (size_t)remaining) n = remaining;
    memset(buf, 'a', n); remaining -= n; return (int)n;
}
static void write_fixture(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text)); assert(fclose(f) == 0);
}
static int cleanup_calls, cleanup_failure;
static char cleaned_path[448];
static int change_callback(const char *path) {
    assert(path[0]); snprintf(cleaned_path, sizeof(cleaned_path), "%s", path);
    ++cleanup_calls; return cleanup_failure;
}
static int receive_font(void *ctx, char *buf, size_t n) { return (int)fread(buf, 1, n, ctx); }

static void test_font_uploads(void) {
    char name[121];
    assert(decode_font_name("Chinese%20Font.TTF", name));
    assert(!strcmp(name, "Chinese Font.TTF"));
    assert(!decode_name("font.ttf", name) && !decode_font_name("book.epub", name));
    assert(!decode_font_name("font.otf", name) && !decode_font_name("font.ttc", name));
    assert(!decode_font_name("..%2Ffont.ttf", name) && !decode_font_name("%2Ffont.ttf", name));
    assert(!decode_font_name("bad%00.ttf", name));
    assert(temporary_basename("Font.TTF.part", ".part", name));
    assert(temporary_basename("Font.TTF.rename-backup", ".rename-backup", name));

    char root[] = "/tmp/transfer-font-XXXXXX", path[288], part[296], backup[320];
    assert(mkdtemp(root));
    snprintf(path, sizeof(path), "%s/Font.ttf", root);
    snprintf(part, sizeof(part), "%s.part", path);
    snprintf(backup, sizeof(backup), "%s.rename-backup", path);
    assert(valid_ttf("main/assets/builtin.ttf"));
    FILE *source = fopen("main/assets/builtin.ttf", "rb"); assert(source);
    assert(!fseek(source, 0, SEEK_END)); size_t size = (size_t)ftell(source); rewind(source);
    char *buf = malloc(16384); assert(buf);
    file_result_t op = upload_managed(path, part, size, TRANSFER_FONT_MAX, UINT64_MAX, false,
        buf, 16384, receive_font, source, NULL, NULL);
    assert(op.status == 200 && op.changed && valid_ttf(path) && !cleanup_calls);
    struct stat before, after; assert(!stat(path, &before));
    remaining = 32;
    op = upload_managed(path, part, 32, TRANSFER_FONT_MAX, UINT64_MAX, false, buf, 16384, receive, NULL, NULL, NULL);
    assert(op.status == 409 && remaining == 32);
    op = upload_managed(path, part, 32, TRANSFER_FONT_MAX, UINT64_MAX, true, buf, 16384, receive, NULL, NULL, NULL);
    assert(op.status == 422 && !op.changed && valid_ttf(path) && access(part, F_OK));
    assert(!stat(path, &after) && before.st_size == after.st_size);
    remaining = 40000; receive_calls = 0; fail_after = 2;
    op = upload_managed(path, part, 40000, TRANSFER_FONT_MAX, UINT64_MAX, true, buf, 16384, receive, NULL, NULL, NULL);
    assert(op.status == 408 && !op.changed && valid_ttf(path) && access(part, F_OK));
    fail_after = 0;
    op = upload_managed(path, part, TRANSFER_FONT_MAX + 1U, TRANSFER_FONT_MAX, UINT64_MAX, true, buf, 16384, receive, NULL, NULL, NULL);
    assert(op.status == 413 && !op.changed);
    op = upload_managed(path, part, size, TRANSFER_FONT_MAX, 0, true, buf, 16384, receive_font, source, NULL, NULL);
    assert(op.status == 507 && !op.changed);
    rewind(source); fatfs_replace = true;
    op = upload_managed(path, part, size, TRANSFER_FONT_MAX, UINT64_MAX, true, buf, 16384, receive_font, source, NULL, NULL);
    assert(op.status == 200 && op.changed && valid_ttf(path) && access(backup, F_OK));
    fatfs_replace = false;
    assert(!fclose(source)); free(buf);
    assert(!rename(path, backup)); write_fixture(part, "unfinished");
    unsigned removed = 0, restored = 0;
    assert(!cleanup_interrupted(root, &removed, &restored));
    assert(removed == 1 && restored == 1 && valid_ttf(path) && access(part, F_OK));
    unlink(path); rmdir(root);
}

int main(void) {
    test_font_uploads();
    char alias_root[] = "/tmp/transfer-alias-XXXXXX", canonical[448], requested[448];
    assert(mkdtemp(alias_root));
    snprintf(canonical, sizeof(canonical), "%s/Book.txt", alias_root);
    write_fixture(canonical, "original");
    assert(resolve_mutation_path(alias_root, "book.txt", requested, sizeof(requested)) == 0);
    assert(!strcmp(canonical, requested));
    char flash_root[] = "/tmp/transfer-flash-XXXXXX", flash_canonical[448];
    assert(mkdtemp(flash_root));
    snprintf(flash_canonical, sizeof(flash_canonical), "%s/FlashBook.txt", flash_root);
    write_fixture(flash_canonical, "flash");
    assert(resolve_mutation_path(flash_root, "flashbook.TXT", requested, sizeof(requested)) == 0);
    assert(!strcmp(flash_canonical, requested));
    unlink(flash_canonical); rmdir(flash_root);
    snprintf(stat_alias, sizeof(stat_alias), "%s/BOOK~1.TXT", alias_root);
    strcpy(stat_target, canonical);
    assert(resolve_mutation_path(alias_root, "BOOK~1.TXT", requested, sizeof(requested)) == 400);
    snprintf(stat_alias, sizeof(stat_alias), "%s/BÖÖK.TXT", alias_root);
    assert(resolve_mutation_path(alias_root, "BÖÖK.TXT", requested, sizeof(requested)) == 400);
    stat_alias[0] = 0;
    assert(resolve_mutation_path(alias_root, "book.txt", requested, sizeof(requested)) == 0);
    assert(!strcmp(canonical, requested));
    char alias_backup[480]; snprintf(alias_backup, sizeof(alias_backup), "%s.rename-backup", canonical);
    assert(rename(canonical, alias_backup) == 0);
    assert(resolve_mutation_path(alias_root, "BOOK.TXT", requested, sizeof(requested)) == 0);
    assert(!strcmp(canonical, requested));
    snprintf(stat_alias, sizeof(stat_alias), "%s/BOOK~1.TXT.rename-backup", alias_root);
    strcpy(stat_target, alias_backup);
    assert(resolve_mutation_path(alias_root, "BOOK~1.TXT", requested, sizeof(requested)) == 400);
    stat_alias[0] = 0;
    unlink(alias_backup); rmdir(alias_root);
    assert(name_contains("红楼梦.txt", "hlm"));
    assert(name_contains("重庆故事.epub", "chongqing"));
    assert(name_contains("Harry Potter.txt", "hp"));
    assert(!name_contains("红楼梦.txt", "txt"));
    char name[121], root[] = "/tmp/transfer-test-XXXXXX", path[256], part[264], buf[16];
    assert(mkdtemp(root));
    assert(decode_name("%E4%B8%AD%E6%96%87.epub", name));
    assert(!strcmp(name, "中文.epub"));
    const char *bad[] = {"../a.txt", "%2e%2e.txt", "a%2fb.txt", "a%5cb.txt", "a%00.txt", "a%ff.txt", "a%C0%AF.txt", "a%ED%A0%80.txt", "a%F4%90%80%80.txt", "a%.txt", "a.pdf", "a.txt.", ".txt", "a:book.txt"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) assert(!decode_name(bad[i], name));
    char longname[128]; memset(longname, 'a', 117); strcpy(longname + 117, ".txt");
    assert(!decode_name(longname, name));
    memset(longname, 'a', 116); strcpy(longname + 116, ".TXT");
    assert(decode_name(longname, name) && strlen(name) == 120);
    assert(check_length(1048577, 1048576, UINT64_MAX) == 413);
    assert(check_length(17, 0, 16) == 507);
    assert(check_length(0, 0, 100) == 400);
    assert(check_length(1048576, 1048576, 1048576) == 0);
    snprintf(path, sizeof(path), "%s/book.txt", root);
    snprintf(part, sizeof(part), "%s.part", path);
    write_fixture(path, "original"); remaining = 50;
    transfer_book_page_t listing;
    assert(list_books(root, "BOOK", 0, &listing) == 0 && listing.total == 1 && listing.items[0].size == 8);
    assert(list_books(root, "missing", 0, &listing) == 0 && listing.total == 0);
    file_result_t op = upload_managed(path, part, 50, 0, UINT64_MAX, false, buf, sizeof(buf), receive, NULL, NULL, change_callback);
    assert(op.status == 409 && !op.changed && remaining == 50 && cleanup_calls == 0);
    fail_recv = 1;
    op = upload_managed(path, part, 50, 0, UINT64_MAX, true, buf, sizeof(buf), receive, NULL, NULL, change_callback);
    assert(op.status == 408 && !op.changed && cleanup_calls == 0); fail_recv = 0;
    cleanup_failure = 1; remaining = 50;
    op = upload_managed(path, part, 50, 0, UINT64_MAX, true, buf, sizeof(buf), receive, NULL, NULL, change_callback);
    assert(op.status == 500 && op.changed && op.progress_cleanup_failed && cleanup_calls == 1);
    op = delete_managed(path, change_callback);
    assert(op.status == 500 && op.changed && op.progress_cleanup_failed && access(path, F_OK) != 0);
    cleanup_failure = 0;
    assert(change_callback(path) == 0);
    op = delete_managed(path, change_callback); assert(op.status == 404 && !op.changed);
    remaining = 50;
    assert(receive_file(path, part, 50, buf, sizeof(buf), receive, NULL, NULL) == 0);
    FILE *f = fopen(path, "rb"); assert(f); fseek(f, 0, SEEK_END); assert(ftell(f) == 50); fclose(f);
    remaining = 20;
    assert(receive_file(path, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 0);
    fail_recv = 1;
    assert(receive_file(path, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 408);
    assert(access(part, F_OK) != 0);
    fail_recv = 0; remaining = 20; fail_write = 1;
    assert(receive_file(path, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 507);
    assert(access(part, F_OK) != 0); fail_write = 0;
    remaining = 20; fail_close = 1;
    assert(receive_file(path, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 507);
    assert(access(part, F_OK) != 0); fail_close = 0;
    fatfs_replace = 1; remaining = 30;
    assert(receive_file(path, part, 30, buf, sizeof(buf), receive, NULL, NULL) == 0);
    fail_commit = 1; remaining = 40;
    assert(receive_file(path, part, 40, buf, sizeof(buf), receive, NULL, NULL) == 507);
    f = fopen(path, "rb"); assert(f); fseek(f, 0, SEEK_END); assert(ftell(f) == 30); fclose(f);
    fail_commit = 0;
    char backup[320]; snprintf(backup, sizeof(backup), "%s.rename-backup", path);
    f = fopen(backup, "wb"); assert(f); fclose(f); remaining = 40;
    assert(receive_file(path, part, 40, buf, sizeof(buf), receive, NULL, NULL) == 507);
    assert(access(backup, F_OK) == 0); unlink(backup);
    f = fopen(path, "rb"); assert(f); fseek(f, 0, SEEK_END); assert(ftell(f) == 30); fclose(f);
    fail_recv = 0; remaining = 0;
    assert(receive_file(path, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 408);
    remaining = 20;
    assert(receive_file(root, part, 20, buf, sizeof(buf), receive, NULL, NULL) == 507);
    assert(access(part, F_OK) != 0);
    assert(receive_file(path, "/nonexistent/part", 20, buf, sizeof(buf), receive, NULL, NULL) == 507);
    unlink(path);
    char orphan[320], saved[320], kept[320], unknown[320], directory[320];
    snprintf(orphan, sizeof(orphan), "%s/orphan.epub.part", root);
    snprintf(saved, sizeof(saved), "%s/orphan.epub.rename-backup", root);
    snprintf(kept, sizeof(kept), "%s/orphan.epub", root);
    snprintf(unknown, sizeof(unknown), "%s/notes.pdf.part", root);
    snprintf(directory, sizeof(directory), "%s/directory.txt.part", root);
    write_fixture(orphan, "incomplete"); write_fixture(saved, "old complete book");
    write_fixture(unknown, "not ours"); assert(mkdir(directory, 0700) == 0);
    unsigned removed = 0, restored = 0;
    assert(cleanup_interrupted(root, &removed, &restored) == 0 && removed == 1 && restored == 1);
    assert(access(orphan, F_OK) != 0 && access(saved, F_OK) != 0);
    f = fopen(kept, "rb"); assert(f); char content[32] = {0}; assert(fread(content, 1, 31, f) == 17); fclose(f);
    assert(!strcmp(content, "old complete book"));
    assert(access(unknown, F_OK) == 0 && access(directory, F_OK) == 0);
    write_fixture(saved, "second complete book");
    assert(cleanup_interrupted(root, &removed, &restored) == 0 && removed == 0 && restored == 0);
    assert(access(saved, F_OK) == 0 && access(kept, F_OK) == 0);
    char percent[320], invalid[320];
    snprintf(percent, sizeof(percent), "%s/100%%.txt.part", root);
    snprintf(invalid, sizeof(invalid), "%s/bad..txt.part", root);
    write_fixture(percent, "incomplete"); write_fixture(invalid, "not ours");
    fail_remove = 1;
    assert(cleanup_interrupted(root, &removed, &restored) != 0 && access(percent, F_OK) == 0);
    fail_remove = 0;
    assert(cleanup_interrupted(root, &removed, &restored) == 0 && removed == 1);
    assert(access(percent, F_OK) != 0 && access(invalid, F_OK) == 0);
    assert(cleanup_interrupted("/nonexistent/cleanup", &removed, &restored) != 0);
    unlink(saved); unlink(kept); unlink(unknown); unlink(invalid); rmdir(directory);
    char long_name[256], managed_path[448], decoded[256];
    memset(long_name, 'x', 251); strcpy(long_name + 251, ".txt");
    assert(raw_book_name(long_name, decoded, sizeof(decoded)) && strlen(decoded) == 255);
    assert(!decode_name(long_name, name));
    snprintf(managed_path, sizeof(managed_path), "%s/%s", root, long_name); write_fixture(managed_path, "long");
    for (int i = 0; i < 65; ++i) { snprintf(path, sizeof(path), "%s/pg%02d.txt", root, i); write_fixture(path, "page"); }
    assert(list_books(root, "", 0, &listing) == 0 && listing.total == 66 && listing.count == 16);
    assert(list_books(root, "", 4, &listing) == 0 && listing.total == 66 && listing.count == 2);
    assert(list_books(root, "PG0", 0, &listing) == 0 && listing.total == 10);
    assert(delete_managed(managed_path, change_callback).changed);
    memset(long_name, 'x', 196); strcpy(long_name + 196, ".txt");
    snprintf(managed_path, sizeof(managed_path), "%s/%s", root, long_name);
    snprintf(backup, sizeof(backup), "%s/%s.rename-backup", root, long_name);
    write_fixture(managed_path, "long main"); write_fixture(backup, "long backup");
    assert(delete_managed(managed_path, change_callback).changed);
    assert(access(managed_path, F_OK) != 0 && access(backup, F_OK) != 0);
    assert(delete_managed(root, change_callback).status == 400);
    for (int i = 0; i < 65; ++i) { snprintf(path, sizeof(path), "%s/pg%02d.txt", root, i); unlink(path); }
    snprintf(path, sizeof(path), "%s/large.epub", root); snprintf(part, sizeof(part), "%s.part", path);
    char *large_buf = malloc(16384); assert(large_buf);
    remaining = 50 * 1024 * 1024;
    op = upload_managed(path, part, (size_t)remaining, 0, UINT64_MAX, false, large_buf, 16384, receive, NULL, NULL, change_callback);
    assert(op.status == 200 && op.changed && remaining == 0);
    fail_after = 3; receive_calls = 0; remaining = 1024 * 1024;
    op = upload_managed(path, part, (size_t)remaining, 0, UINT64_MAX, true, large_buf, 16384, receive, NULL, NULL, change_callback);
    assert(op.status == 408 && !op.changed && remaining < 1024 * 1024 && access(part, F_OK) != 0);
    struct stat large_stat; assert(stat(path, &large_stat) == 0 && large_stat.st_size == 50 * 1024 * 1024);
    free(large_buf); unlink(path);
    snprintf(path, sizeof(path), "%s/backup-warning.txt", root); snprintf(part, sizeof(part), "%s.part", path);
    snprintf(backup, sizeof(backup), "%s.rename-backup", path); write_fixture(path, "old");
    fail_after = 0; fail_remove = 1; remaining = 16; int before_callback = cleanup_calls;
    op = upload_managed(path, part, 16, 0, UINT64_MAX, true, buf, sizeof(buf), receive, NULL, NULL, change_callback);
    assert(op.status == 200 && op.changed && cleanup_calls == before_callback + 1);
    assert(op.storage_cleanup_failed);
    assert(stat(path, &large_stat) == 0 && large_stat.st_size == 16 && access(backup, F_OK) == 0);
    fail_remove = 0; unlink(path); unlink(backup);
    write_fixture(path, "new complete"); write_fixture(backup, "old complete");
    op = delete_managed(path, change_callback);
    assert(op.status == 200 && op.changed && access(path, F_OK) != 0 && access(backup, F_OK) != 0);
    assert(cleanup_interrupted(root, &removed, &restored) == 0 && restored == 0);
    write_fixture(path, "main stays"); write_fixture(backup, "old stays"); fail_remove = 1;
    op = delete_managed(path, change_callback);
    assert(op.status == 507 && !op.changed && access(path, F_OK) == 0 && access(backup, F_OK) == 0);
    fail_remove = 0; unlink(path); fail_restore = 1;
    op = delete_managed(path, change_callback);
    assert(op.status == 507 && !op.changed && access(path, F_OK) != 0 && access(backup, F_OK) == 0);
    fail_restore = 0; fail_remove = 1;
    op = delete_managed(path, change_callback);
    assert(op.status == 507 && !op.changed && access(path, F_OK) == 0 && access(backup, F_OK) != 0);
    fail_remove = 0; unlink(path); write_fixture(backup, "recover then delete");
    op = delete_managed(path, change_callback);
    assert(op.status == 200 && op.changed && access(path, F_OK) != 0 && access(backup, F_OK) != 0);
    unsigned changes = 0;
    record_file_change(&changes, (file_result_t){.status = 200}, true); assert(changes == 1);
    record_file_change(&changes, (file_result_t){.status = 500}, true); assert(changes == 1);
    record_file_change(&changes, (file_result_t){.status = 500, .changed = true}, false); assert(changes == 2);
    // 别名覆盖和删除都必须清真实路径，覆盖不改变目录拼写。/ Alias replacement and deletion clear the actual path and preserve directory spelling.
    snprintf(path, sizeof(path), "%s/Book.txt", root); write_fixture(path, "old data");
    assert(resolve_mutation_path(root, "book.txt", requested, sizeof(requested)) == 0);
    snprintf(part, sizeof(part), "%s.part", path); remaining = 8; cleanup_failure = 0;
    op = upload_managed(requested, part, 8, 0, UINT64_MAX, true, buf, sizeof(buf), receive, NULL, NULL, change_callback);
    assert(op.status == 200 && op.changed && !strcmp(cleaned_path, path));
    assert(access(path, F_OK) == 0);
    assert(resolve_mutation_path(root, "BOOK.TXT", requested, sizeof(requested)) == 0);
    cleanup_failure = 1;
    op = delete_managed(requested, change_callback);
    assert(op.status == 500 && op.changed && op.progress_cleanup_failed && !strcmp(cleaned_path, path));
    assert(access(path, F_OK) != 0);
    cleanup_failure = 0;
    assert(resolve_mutation_path(root, "Book.txt", requested, sizeof(requested)) == 0);
    op = changed_result(requested, change_callback);
    assert(op.status == 200 && !strcmp(cleaned_path, path));
    rmdir(root); puts("transfer host tests passed");
}
