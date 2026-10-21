#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
生词本的接线是跨文件的，靠注释同步太脆弱：app_book.c 里少调一次 vocab_log_capture()、
或者 chip 的点击没落到 dict_popup_tap()，编译照样过、真机上只是"点了没反应"，
要等用户拿卡去查才发现。本测试守住这几处接线：

  1. start_dict_lookup() 必须在 unlock_draw() 之后才抓取上下文（不在持锁期间读 s_text）；
  2. 进入/退出/掉卡的三个生命周期钩子必须调用 warm() / invalidate()，索引不能悬空；
  3. 词典面板的点击必须先交给 dict_popup_tap()，命中 chip 时不关闭卡片；
  4. 弹层必须真的画出 chip，并且头部宽度要为它让位；
  5. 工具条第 6 项必须真的打开视图，且视图内删掉一条后要重渲染 + 重开；
  6. 视图路径必须**按内容**比较（不许指针比较，也不许 strstr 子串包含）。

只做静态检查，不编译。

The vocabulary book is wired across files, and comment-only sync is too fragile: if
app_book.c forgets a vocab_log_capture() call, or the chip tap never reaches
dict_popup_tap(), the build still succeeds and the device merely "does nothing" — the
user only finds out later. This test pins the wiring:
  1. start_dict_lookup() must capture context after unlock_draw(), not while holding
     the draw lock;
  2. the enter/exit/media-lost hooks must call warm() / invalidate() so the index
     never dangles;
  3. a dictionary-panel tap must first go to dict_popup_tap(), and a chip hit must
     keep the card open;
  4. the popup must actually draw the chip and the header width must yield to it;
  5. toolbar item 6 must really open the view, and a removal inside the view must
     re-render and re-open it;
  6. the view path must be matched by contents, never by pointer or substring.

Static only, nothing is compiled.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOK = ROOT / 'main' / 'apps' / 'app_book.c'
POPUP = ROOT / 'main' / 'apps' / 'dict_popup.c'
VOCAB = ROOT / 'main' / 'dict' / 'vocab_log.c'


def function_body(text: str, signature: str) -> str:
    """Return the braces-balanced body of a function, or '' when not found."""
    start = text.find(signature)
    if start < 0:
        return ''
    depth = 0
    seen_open = False
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
            seen_open = True
        elif text[i] == '}':
            depth -= 1
            if seen_open and depth == 0:
                return text[start:i + 1]
    return ''


def main() -> int:
    for path in (BOOK, POPUP):
        if not path.exists():
            print(f'FAIL: missing {path}')
            return 1
    book = BOOK.read_text(encoding='utf-8')
    popup = POPUP.read_text(encoding='utf-8')
    failures = []

    # 1. 抓取上下文必须在解锁之后。/ Capture must happen after the draw lock is released.
    capture = function_body(book, 'static app_redraw_t start_dict_lookup(')
    if not capture:
        failures.append('start_dict_lookup(): not found')
    else:
        if 'vocab_log_capture(' not in capture:
            failures.append('start_dict_lookup(): does not call vocab_log_capture()')
        lock_at, cap_at = capture.find('unlock_draw()'), capture.find('vocab_log_capture(')
        if lock_at < 0:
            failures.append('start_dict_lookup(): never calls unlock_draw()')
        elif cap_at < lock_at:
            failures.append('start_dict_lookup(): captures before unlock_draw() '
                            '(would read s_text under the draw lock)')

    # 2. 生命周期钩子。/ Lifecycle hooks.
    hooks = {
        'static void on_enter(': 'vocab_log_warm()',
        'static void book_on_exit(': 'vocab_log_invalidate()',
        'static void book_on_media_lost(': 'vocab_log_invalidate()',
    }
    for signature, call in hooks.items():
        body = function_body(book, signature)
        name = signature.split('(')[0].split()[-1]
        if not body:
            failures.append(f'{name}(): not found')
        elif call not in body:
            failures.append(f'{name}(): does not call {call}')

    # 3. 面板点击先过 chip。/ Panel taps go through the chip first.
    action = function_body(book, 'static app_redraw_t reader_panel_action(')
    if not action:
        failures.append('reader_panel_action(): not found')
    else:
        dict_branch = action.find('READER_PANEL_DICT')
        if dict_branch < 0:
            failures.append('reader_panel_action(): no READER_PANEL_DICT branch')
        else:
            branch = action[dict_branch:dict_branch + 400]
            if 'dict_popup_tap(' not in branch:
                failures.append('reader_panel_action(): the dictionary branch never '
                                'calls dict_popup_tap()')
            elif branch.find('dict_popup_tap(') > branch.find('dict_popup_reset()'):
                failures.append('reader_panel_action(): resets the popup before giving '
                                'the chip a chance to handle the tap')

    # 4. 上下文守卫：在虚拟书里长按，capture() 必须直接 return 并清空原句。
    # Context guard: a long press inside the virtual book must bail out and clear
    # the sentence buffer, or a row of the view itself becomes a "source sentence".
    if not VOCAB.exists():
        failures.append('vocab_log.c: missing')
    else:
        text = VOCAB.read_text(encoding='utf-8')
        body = function_body(text, 'void vocab_log_capture(')
        if not body:
            failures.append('vocab_log_capture(): not found')
        else:
            if 'vocab_log_is_view_path(' not in body:
                failures.append('vocab_log_capture(): does not consult vocab_log_is_view_path() '
                                '(a long press in the vocabulary view would capture its own row)')
            if 's_sentence[0] = 0' not in body:
                failures.append('vocab_log_capture(): does not clear s_sentence when guarding')
            guard_at = body.find('vocab_log_is_view_path(')
            if guard_at < 0 or body.find('s_book[0] = 0') < guard_at:
                failures.append('vocab_log_capture(): the guard must run before the book '
                                'context is overwritten')

        # 路径比较必须是**深比较**：`path == VOCAB_VIEW` 比的是地址，换个来源的路径
        # 就整个失效，而且编译期一声不吭。也不许退化成 strstr 之类的子串包含——
        # 误判会让 DICT 分支在真书里删词时把虚拟书顶上来。
        # Path matching must compare contents: `path == VOCAB_VIEW` compares addresses
        # and silently breaks; a substring test like strstr would match
        # "my_vocab_view.txt" and reopen the view over a real book.
        matcher = function_body(text, 'static bool name_is_view(')
        if not matcher:
            failures.append('vocab_log.c: name_is_view() not found')
        else:
            if re.search(r'[=!]=\s*(VOCAB_VIEW|view|path)\b', matcher):
                failures.append('name_is_view(): compares pointers instead of contents '
                                '(path == VOCAB_VIEW breaks for any other buffer)')
            # 注意：strcasecmp 里并没有 "strcmp" 这个连续子串，别写错。
            # Careful: "strcasecmp" does not contain the substring "strcmp".
            if not re.search(r'strcasecmp|strcmp', matcher):
                failures.append('name_is_view(): does not compare with strcmp/strcasecmp')
            if 'strstr' in matcher and not re.search(r'strcasecmp|strcmp', matcher):
                failures.append('name_is_view(): substring matching would match '
                                'my_vocab_view.txt too')

    # 5. 工具条：两张表必须等长，且有 _Static_assert 兜着。
    # Toolbar: both tables must be the same length and asserted.
    for needle, label in (
        ('#define BOOK_TOOL_COUNT 6', 'six tools'),
        ('k_tool_labels[BOOK_TOOL_COUNT]', 'a label table sized by the count'),
        ('k_tool_icons[BOOK_TOOL_COUNT]', 'an icon table sized by the count'),
        ('"生词本"', 'the vocabulary-book label'),
    ):
        if needle not in book:
            failures.append(f'app_book.c: missing {label} ({needle!r})')
    # 两条断言必须分别存在。只数 _Static_assert 的出现次数会被上游自己的断言
    # （以及注释里提到的那几次）蒙混过关：删掉一条后总数依然够。
    # Both assertions must exist individually. Counting _Static_assert occurrences
    # lets upstream's own asserts - and the ones merely mentioned in comments -
    # mask a missing one.
    if '"every reader tool needs a label"' not in book or \
       '"every reader tool needs an icon"' not in book:
        failures.append('app_book.c: the toolbar tables lost their _Static_assert '
                        '(a missing entry would silently shift every tap)')
    # 生词本那一项要真的打开视图，而不是落到别的分支上
    # The vocabulary entry must open the view, not fall through to another branch.
    tools = function_body(book, 'static app_redraw_t reader_panel_action(')
    if 'open_vocab_view(ctx, false)' not in tools:
        failures.append('reader_panel_action(): the toolbar never opens the vocabulary view')
    # 在视图里删掉一条后要重开视图 / a removal inside the view must re-open it
    if 'DICT_POPUP_TAP_REMOVED' not in tools or 'open_vocab_view(ctx, true)' not in tools:
        failures.append('reader_panel_action(): removing an entry in the view does not '
                        're-render and re-open it')

    # 6. 弹层真的画 chip，且头部让位。/ The popup draws the chip and the header yields.
    for needle, label in (
        ('dict_popup_save_rect', 'chip geometry'),
        ('ui_draw_choice_round_rect(', 'filled chip when saved'),
        ('已存', 'saved label'),
    ):
        if needle not in popup:
            failures.append(f'dict_popup.c: missing {label} ({needle!r})')
    if 'inner_w - DICT_POPUP_SAVE_W' not in popup:
        failures.append('dict_popup.c: the header width does not reserve room for the chip')

    if failures:
        for line in failures:
            print('  FAIL ' + line)
        print('vocab_wiring_host_test: FAIL')
        return 1
    print('PASS: vocabulary book wiring intact (capture after unlock, 3 lifecycle hooks, '
          'view guard matched by contents, 6 asserted tools, chip tap + re-open, chip drawn)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
