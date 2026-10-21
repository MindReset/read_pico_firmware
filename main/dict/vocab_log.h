/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：生词本。长按查词后由用户点卡片上的 chip 手动保存，写 TF 卡 TSV，可直接导入 Anki。
 * English: Vocabulary builder. After a long-press lookup the reader taps a chip on the
 * card to save; entries land on the TF card as TSV, ready for Anki.
 *
 * 冻结：绝不自动记录——保存只由用户点击触发；写失败必须静默，绝不得阻塞 UI。
 * Frozen: never automatic — saving only happens on an explicit tap, and a failed
 * write stays silent instead of blocking the UI.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOCAB_WORD_MAX 64

/// 抓取长按上下文（书名路径、章节原文、词区间）。必须在原文仍有效时调用，
/// 且不要在绘制锁内做——扫描是有界的字符串操作，不该拉长持锁时间。
/// Capture the long-press context while the chapter text is still valid. Call it
/// outside the draw lock: the scan is bounded string work and must not extend
/// the time the lock is held.
void vocab_log_capture(const char* book_path, const char* text, size_t start, size_t end);
/// 词是否已在生词本。索引未就绪时返回 false（chip 显示为未保存），**不读卡**，
/// 所以它永远不会拖慢查词路径。
/// Whether the word is saved. Returns false when the index is unavailable and
/// never touches the card, so it can never slow the lookup path down.
bool vocab_log_contains(const char* word);
/// 追加一条。索引未就绪时改为逐行扫文件去重（只在用户点击时发生，不在热路径上）。
/// Append one entry. Falls back to a file scan when the index is unavailable;
/// that only ever happens on an explicit tap, never on a hot path.
bool vocab_log_save(const char* word, const char* definition);
/// 移除一条（chip 再点一次）。走临时文件重写，中途失败不留半成品。
/// 被删的那一行会被记住：紧接着再点一次「保存」就**原样写回**，所以误删可撤销。
/// Remove one entry through a temp-file rewrite; a failure leaves no half file.
/// The removed row is remembered so an immediate second tap writes it back
/// verbatim — an accidental removal is undoable.
bool vocab_log_remove(const char* word);
/// 把 vocab.txt 渲染成一本排版好的 TXT 虚拟书，供现成阅读器打开。
/// 返回是否成功；*out_count 是词条数（0 表示生词本是空的）。
/// Render vocab.txt into a laid-out TXT virtual book the existing reader can open.
/// Returns success; *out_count is the entry count (0 means the book is empty).
bool vocab_log_render(size_t* out_count);
/// 虚拟书路径，用来打开它。/ The virtual book path, used to open it.
const char* vocab_log_view_path(void);
/// 是不是虚拟书（只比文件名，所以拷到别处再读也认得出）。
/// capture() 用它拒绝抓取上下文，免得把视图自己的一行当成原句写回去。
/// Whether this is the virtual book (matches on the file name, so a copy read from
/// elsewhere is still recognized). capture() uses it to refuse the context, so a
/// row of the view itself can never become a "source sentence".
bool vocab_log_is_view_path(const char* path);
/// 预建去重索引。与 dict_warm() 同处调用，把读卡开销挪出查词路径。
/// Build the index up front; call next to dict_warm() to keep the card read off
/// the lookup path.
void vocab_log_warm(void);
/// 拔卡/退出时释放索引。与 dict_close() 同处调用。
/// Drop the index when the card goes away; call next to dict_close().
void vocab_log_invalidate(void);

#ifdef __cplusplus
}
#endif
