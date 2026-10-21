/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 图书书架、阅读、目录与页内进度；文件解析和排版由book模块负责。
 * Book shelf, reader, TOC and progress; book modules own parsing and pagination.
 *
 * 用户修订：阅读设置采用已确认的半屏双列排版；自动全刷与翻页效果点按轮换，保留按键控制二级页。
 * User revision: use the approved compact two-column reading sheet; tap cycles refresh/effect and key controls retain their secondary page.
 * 冻结：Phase4b统一手势入口并接管三键为上页/工具条/下页；工具条保留强刷。屏幕翻页在抬起提交，不画按下态。
 * 用户修订：图文混排原样采用 PR9 的 book_layout；调用侧只适配图片缓存与上下文生命周期。
 * 晃动默认关；横向左晃上一页、右晃下一页，触摸与回弹不触发；离页恢复加速度配置并休眠。render只绘图。
 * 预渲染与同步推屏重叠、按行可停；推屏返回前交还字体与章节资源。
 * 普通翻页只刷新正文与页脚；手动或周期清残影整屏全刷。
 * 用户修订：阅读手动与周期全刷恢复 rc90 的原表 GC16 目标页输出，不先擦白再补文字。
 * User revision: restore rc90's original-table GC16 target-page output for manual and periodic refresh, without a white pre-clear and text pass.
 * 用户修订：正文中间双击固定切换全屏，单击左右各半或上1/3下2/3翻页，不受按键配置影响。
 * 三键单击及中键长按可自定义，提供翻页/工具栏及首页/全屏/工具栏两套推荐；中键长按默认返回主页，阅读页不画右下角菜单图标。
 * 用户授权基础管理：长按书架先看完整详情，清进度与删文件分别确认；失败保留待重试记录，不自动回收其他书进度。
 * 用户修订：单本管理为书架弹窗；管理页用于批量操作。分页和排序保留勾选，筛选/应用搜索及重扫清除勾选。
 * 失败进度仅按变更路径失效；删除后的清理重试保留到本次开机结束，不随切页释放。
 * 卡失效时先保存进度并关闭阅读资源，再由主循环回退字体；禁止自动续读失效挂载。
 * 用户最新修订：书架只保留深色书轨与亚克力，每页九本；收录导入及读过的书，移出仅隐藏，再读重新上架。
 * 底栏保留首页/书架/文件/设置，设置直接进入设置页。
 * 用户修订：长按图书可用本机拼音输入编辑书名；阅读时长与翻页真实记录，供票根锁屏使用。
 * 用户最新修订：书名编辑可点选插入位置并用左右键微调，支持在文字中间插入和删除。
 * 用户授权新输入法：书名和搜索共用九宫格/全键盘、中英文及离线词语候选，不改正文排版。
 * 用户修订：首页、书架、文件、设置四栏导航；切换界面采用 GL16，章节首页单独排标题。
 * 用户修订：EPUB 章节首页以书内目录标题为准，正文题头仅在相同时折叠，避免引言误判。
 * 用户修订：阅读进度条无外伸刻度并使用圆角；继续阅读区显示最近书籍封面。
 * 用户修订：书架整页读取成品缓存后一次显示；首次生成有界分批处理，但整页封面一起显现，不逐本推屏。
 * User revision: read the page's finished caches before its first frame; cold generation is bounded, but cover artwork is revealed together.
 * 用户修订：字号和间距重排只保存最终进度，字号使用现有闲置灰阶整理。
 * 用户修订：阅读设置滑杆可拖动并在松手后重排；字体选择可纵向翻页；统计入口显示真实明细与近30天数据。
 * 用户修订：前后均为纯文字的普通翻页采用柔和 GL16 差分，仅白底灰边提前少量回浅，不改字形或灰度目标；插图、混排、周期全刷及水波纹保留原规则。
 * User revision: soften ordinary pure-text GL16 turns by advancing a few white-to-gray settling pulses; preserve glyph targets, images, mixed pages, cleanup and ripple.
 * 用户修订：目录由独立模块整页绘制与命中；目录标题清理换行并限制为单行，翻页不再沿用书架的局部刷新。
 * 用户修订：书架封面抽出与取消仅驱动变化像素，保持灰阶，不在点按时强制清屏。
 * 用户修订：列表封面上弹8px，松手先回位再开书；只重画两位置的并集，复用缓存，不触碰文字/边框/分隔线。快刷用跟随DU，其他模式保留灰阶差分。
 * User revision: list covers bounce up 8px and settle before opening; repaint only the position union using cached artwork, preserving text, frames and separators. Fast uses FOLLOW DU; other modes retain gray differentials.
 * 用户确认：列表封面缩至120×156，保留原中心点与右侧文字位置，等比填充并居中裁切。
 * User approval: list covers use 120×156 boxes, retaining their center and right-side text positions with aspect fill and centered cropping.
 * 用户修订：快刷亚克力采用规则一像素棋盘格，与已缓存封面合并输出，不注册灰区或等待框架先显示；未缓存封面继续有界延后解码。普通及水波纹保留灰阶，切页/锁屏取消旧目标。
 * 用户修订：书架读取最多当前整页缓存；未缓存封面限时准备，整组显现后才确认待刷标记，文件间检查导航，不新增整页缓存。
 * 用户修订：阅读背景先列pictures图片、忽略隐藏文件，点开预览上下切换，返回目录；背景按整屏坐标固定，不随文字边距或全屏状态缩放。
 * User revision: fast acrylic uses a regular one-pixel checkerboard in the same output as cached covers, without gray registration or a furniture-first wait; uncached covers retain bounded lazy decoding. Ordinary/water retain grays; page/lock changes cancel stale targets.
 * 用户要求采用PR17的快速水波纹，并最新指定8条带、每拍11090us；每次显式设置，结束恢复先前节拍，避免影响主页动画。全刷、插图及失败恢复保留原优先级。
 * User requests PR17 fast ripple with the latest 8 bands at 11090us; explicitly select pacing per turn and restore it afterward so main animations remain independent. Retain cleanup, image and recovery priority.
 * 用户增加管理/导入按压态：复用现有抬起确认流程，按下灰底加深并下沉2px，只推按钮区域。
 * User adds management/import feedback: retain release-confirmed actions, darken and depress controls 2px on press, presenting only their region.
 * 用户修订：快刷封面保留轻量减黑并按最新要求略增黑点密度，在PSRAM缓存最多九张1位缩略图（约40KiB）；进度未改变时不重复丢弃封面。缓存只画封面，收藏与界面文字实时绘制，开书或卡失效时释放。
 * Frozen: Phase4b uses the shared gesture entry and owns previous/tools/next keys; the toolbar keeps full refresh. Screen turns commit on release without pressed decoration.
 * User revision: adopt PR9's book_layout unchanged; callers only adapt bitmap caching and context lifetimes.
 * Authorized keyboard revision: titles and search share T9/QWERTY, bilingual offline phrase candidates; body layout stays intact.
 * 用户修订：输入不重画题头、封面或正文，只刷新输入变化区域，退出后释放查字缓存。
 * 用户修订：新开书默认全屏；双击中间切换，翻页关闭工具栏并回全屏；重排以已选目标页的文字位置为锚，不跳过未读文字。
 * User revision: new books open full screen; center double taps toggle it, turns dismiss tools and return to full screen; reflow anchors the selected destination before changing geometry so unread text is not skipped.
 * 用户修订：字重用横向减/加胶囊，每次微调1/16像素覆盖率；保持字形比例/字宽，不加载额外整份字库。
 * User revision: a horizontal minus/plus pill adjusts coverage by 1/16 pixel per tap, preserving advances/proportions without another font.
 * 用户确认：字体/字重与书内字体/排版分别并排，移除阅读背景入口；字号和阅读线继续可调。
 * User approval: pair face/weight and embedded-face/typography, removing the background entry while keeping size and reading rules adjustable.
 * 用户增加阅读抗锯齿开关，默认开；书架整页封面合并显示；正文底部上滑固定呼出工具栏，不受翻页/按键设置影响。
 * User adds default-on reading AA and grouped shelf artwork; upward swipes from the body bottom always open tools independently of tap/key modes.
 * 用户修订：底部上滑固定呼出工具栏，不随按键及翻页模式变化；取消短按或长按必须保留工具栏的限制。
 * User revision: bottom-edge swipes always open tools independently of key/tap modes; short and hold actions no longer require a toolbar mapping.
 * User revision: input refreshes changed regions without repainting headers, covers or body; release lookup caches on exit.
 * Shake is off by default: left/right impulses turn back/forward, suppressing touch and rebound; exit restores the sensor and sleeps it. Render only paints.
 * Prefetch overlaps synchronous output and stops between rows; no font/chapter resources survive presentation.
 * Ordinary turns refresh only body and footer; manual and periodic ghost cleanup refresh the entire screen.
 * User revision: center body double taps always toggle full screen; single taps use halves or upper 1/3/lower 2/3 independently of key mappings.
 * Short key actions are configurable with two presets; middle holds are configurable and default to home. The reader has no bottom-right menu icon.
 * User-authorized management shows full details before separate clear/delete confirmations; retain failed saves for retry without pruning other books.
 * User revision: single-book actions use a shelf dialog; full management is for batches. Paging/sorting preserve selection; filtering/applied search and rescanning clear it.
 * Invalidate failed progress only for changed paths; retain deletion cleanup retries across page exits for this boot.
 * Lost media saves progress and closes reader resources before global font fallback; never auto-resume an invalid mount.
 * Latest user revision: the two retained shelf styles hold nine imported or read books; retired styles restore as acrylic, and removal hides until reread.
 * The fourth tab opens Settings directly.
 * User revision: book details lead to an on-device Pinyin title editor; measured reading time and turns feed the ticket lock face.
 * Latest user revision: the title editor can place and move an insertion caret for edits in the middle of text.
 * 用户统一修订：书名与搜索采用共用常亮光标；点文字或箭头定位，按系统字体测量。
 * User-wide revision: title/search share a steady caret, text/arrow positioning and system-font measurement.
 * User revision: home, shelf, files and settings have four-tab navigation; view changes use GL16 and chapter starts have a title lead.
 * User revision: EPUB chapter leads use navigation titles; body headings are folded only when matching, preventing front matter from being mislabeled.
 * User revision: the reader bar is rounded without protruding ticks; continue reading displays the latest cover.
 * User revision: the shelf retains its title layout, reads a complete cached page before painting, and reveals cold artwork only after the page is prepared.
 * User revision: size and spacing reflow saves final progress only; size uses the existing idle grayscale settle.
 * User revision: reader sliders drag and reflow on release; font selection pages vertically; statistics entries show real details and recent-30-day data.
 * User revision: pure-text turns use softened gray-edge differential GL16; images/mixed pages, periodic cleanup and water turns retain their policies.
 * User revision: a standalone module owns full-page TOC rendering and hit testing; normalized single-line titles cannot leak into another row.
 * User revision: cover lift and cancellation drive changed pixels in grayscale, without forced cleanup during a tap.
 * 用户修订：快刷仅常驻黑白缩略图；灰阶解码缓冲转好即释放，灰阶模式按需重建，收藏白边单独记录。
 * User revision: fast mode retains only packed monochrome thumbnails, frees gray decoding buffers immediately after conversion, rebuilds gray on demand, and stores favorite-edge contrast separately.
 * User revision: fast covers retain a light white lift with slightly denser blacks per the latest request and cache at most nine 1-bit thumbnails in PSRAM (about 40 KiB); unchanged progress no longer discards covers. Cache artwork only, draw favorites and UI text live, and release on book opening or media loss.
 * 用户最新修订：阅读书名与章节标题复用当前阅读字体，以原32/48px直接光栅化；首页另用其活动系统字体，不同时加载两份字库。
 * Latest user revision: book/chapter titles reuse the current reader face at the original 32/48px; Home uses its active system face, never retaining both fonts.
 * 用户确认：题头下拉选择 books 子目录，各类别/布局记住页码；去掉题头横线，主体在确认位置基础上下移8px。
 * User approval: choose books subfolders in the title menu, retain per-category/layout pages, remove the header rule and lower the approved body by 8px.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "app.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "app_registry.h"
#include "ble_page_turner.h"
#include "book_layout.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "book_lock_collage.h"
#include "book_epub.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_title.h"
#include "book_ticket.h"
#include "book_excerpt.h"
#include "book_toc.h"
#include "app_font_context.h"
#include "book_source.h"
#include "book_store.h"
#include "book_shelf_categories.h"
#include "dict_lookup.h"
#include "dict_popup.h"
#include "vocab_log.h"
#include "display.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "push_sink.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "read_pico_search.h"
#include "settings.h"
#include "book/book_reader_input.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_image_dither.h"
#include "ui_reader_background.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_click_feedback.h"
#include "app_transfer_mode.h"

// 阅读工具栏图标盒边长；行内图标在 1096..1216 的工具条里以 1138 为中心。
// Reader toolbar icon box; the row centres on y=1138 inside the 1096..1216 bar.
#define READER_TOOL_ICON_PX 40
// 二级面板返回键里的箭头边长；圆底半径 24，原来的箭头是 20 像素高。
// Chevron box inside a secondary panel's back control; the old arrow was 20 px tall.
#define READER_SHEET_BACK_PX 34

#define BOOK_ROWS 13
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define BOOK_PX_MIN 36
#define BOOK_PX_MAX 72
#define BOOK_PX_STEP 4
// 边距取上游 r91 的九档滑杆（0..48）；工具条第 6 项（词典）是我方新增，故计数为 6。
// Margins follow upstream r91's nine-step slider (0..48); the sixth tool (dictionary) is ours,
// hence the count of six.
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define BOOK_MARGIN_CHOICES 9
#define BOOK_TOOL_COUNT 6
#define BOOK_FONT_PAGE 9
#define BOOKMARK_MAX 24
#define BOOKMARK_ROWS 6

typedef enum { SHELF, READING, TOC, MANAGE, BULK, IMPORT, SEARCH, EDIT } book_view_t;
typedef enum {
    READER_PANEL_NONE,
    READER_PANEL_TOOLS,
    READER_PANEL_FONT_SETTINGS,
    READER_PANEL_LAYOUT_SETTINGS,
    READER_PANEL_WEIGHT_SETTINGS,
    READER_PANEL_RULE_SETTINGS,
    READER_PANEL_FONT_PICKER,
    READER_PANEL_BACKGROUND,
    READER_PANEL_BACKGROUND_PREVIEW,
    READER_PANEL_STATS,
    READER_PANEL_BOOKMARKS,
    READER_PANEL_STATS_RECENT,
    READER_PANEL_REFRESH_SETTINGS,
    READER_PANEL_TURN_SETTINGS,
    READER_PANEL_KEY_SETTINGS,
    READER_PANEL_KEY_PICKER,
    READER_PANEL_DICT, ///< 点词查译卡片 / Tap-to-lookup definition card
} reader_panel_t;
typedef struct {
    char name[256];
    char author[128];
    char path[BOOK_STORE_PATH_MAX];
    uint32_t size;
    uint16_t chapter;
    bool is_flash;
    bool has_progress;
    uint8_t pct;
    uint32_t recent;
    bool selected, removed, search_match, favorite;
} shelf_entry_t;

static const char* TAG = "book";
static book_view_t s_view;
static int s_presented_view = -1;
static bool s_reader_image_refresh_pending;
static bool s_reader_turn_pending;
// 只记录成功推屏的纯正文基准，避免跨章或退出插图时误用文字波形。
// Track only successfully presented text-only bodies, protecting chapter and image transitions.
static bool s_reader_text_frame;
static char s_requested_open[BOOK_STORE_PATH_MAX];
static bool s_requested_open_home;
static bool s_requested_fullscreen = true;
static bool s_reader_return_home;
static bool s_requested_manage;
static shelf_entry_t* s_shelf;
static struct {
    int index;
    uint8_t *gray, *fast_bits;
    unsigned width, height;
    uint8_t fast_contrast, fast_phase;
    bool fast_white_edge;
} s_covers[BOOK_ROWS];
static unsigned s_cover_pending_mask, s_cover_decode_mask, s_cover_warm_mask;
static unsigned s_cover_dirty_mask, s_cover_present_mask;
static int64_t s_cover_warm_after_ms;
static size_t s_shelf_capacity;
static int s_count, s_visible_count, s_filter;
static bool s_recent_sort;
// 目录记录有界增长；同一开机周期按完整路径保存两种布局的页码。
// Bounded folder records retain pages by full path for the two layouts during this boot.
#define SHELF_CATEGORY_LIMIT 128
#define SHELF_CATEGORY_ROWS 7
typedef struct { char path[BOOK_STORE_PATH_MAX]; int pages[2], count; bool available; } shelf_category_t;
static shelf_category_t *s_categories;
static int s_category_count, s_category_capacity, s_category_selected, s_category_menu_page;
static bool s_category_menu;
static char s_category_root[BOOK_STORE_PATH_MAX];
static void shelf_category_init(void);
static void shelf_category_remember(app_ctx_t *ctx);
static void shelf_category_restore(app_ctx_t *ctx);
static void shelf_categories_recount(void);
static void draw_category_menu(uint8_t *fb);
static const char *shelf_category_name(int index);
static bool shelf_category_matches(const char *path);
static int shelf_category_add(const char *path);
static app_redraw_t shelf_category_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev);
static bool s_reader_favorite;
static shelf_entry_t s_managed;
static bool s_delete_confirm, s_file_removed;
static char s_shelf_warning[128], s_manage_message[128];
static char s_query[65], s_search_draft[65], s_batch_message[128];
static ui_text_edit_t s_search_input;
static book_view_t s_search_parent;
// 批量操作需要二次确认的三类：删文件、清进度、从书架移除。前两类动数据，后一类只动书架。
// Three batch actions need confirmation: delete files, clear progress, remove from the shelf.
// The first two touch stored data; the last one only touches shelf membership.
typedef enum {
    BATCH_DELETE,
    BATCH_CLEAR,
    BATCH_UNSHELF,
} batch_kind_t;
static bool s_batch_confirm;
static batch_kind_t s_batch_kind;
typedef struct pending_progress {
    char path[BOOK_STORE_PATH_MAX];
    book_progress_t value;
    bool dirty, progress_saved;
    book_progress_watch_t* watch;
    struct pending_progress* next;
} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {
    shelf_entry_t entry;
    struct delete_retry* next;
} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[BOOK_STORE_PATH_MAX];
static char s_editor_title[121], s_editor_notice[96];
static ui_text_edit_t s_editor_input;
static uint8_t* s_editor_cover;
static bool s_save_failed;
static bool s_pending_invalidated;
static int64_t s_save_retry_ms;
static unsigned s_store_revision;
static uint32_t s_progress_revision=UINT32_MAX;
static bool s_shelf_cache_valid, s_cache_sd_present, s_cache_sd_mounted;
static uint8_t s_cache_shelf_style;
static bool s_scan_pending, s_clear_confirm;
static reader_panel_t s_reader_panel;
static bool s_reader_fullscreen;
static int64_t s_reader_clock_minute = -1, s_reader_clock_painted_minute = -1;
static bool s_reader_clock_pending;
static char s_message[128], s_storage[128], s_path[BOOK_STORE_PATH_MAX], s_title[128];
// gen18（18-a）：读 push.txt 期间 sink 后台保活；手机回连时本页自动切回传书页
// 接客。这个标记告诉 book_on_exit「此次离页是交接，不收摊」——摊子留给用户
// 真正离开阅读器时收（裁决：收摊点挂 book_on_exit）。
// gen18 (18-a): the sink stays background-alive while push.txt is read; a
// returning phone bounces this page back to the transfer page to receive. The
// flag tells book_on_exit "this exit is a handoff, do not tear down" - the
// teardown waits for a real reader exit (the ruling: teardown hangs off
// book_on_exit).
static bool s_push_handoff;
static char s_book_title[128];
static char s_chapter_heading_title[128], s_chapter_heading_label[32];
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height;
// 横排题头该占的行高，与当前模式无关地记着：首块是插图时它是 0，
// 切回横排要用它，而不是拿竖排留下的 0 去算。
// The row height a horizontal heading should take, remembered independently of the current mode:
// it is zero when the chapter opens with an illustration, and switching back must use it rather
// than the zero a vertical heading left behind.
static unsigned s_chapter_lead_row_height;
// 竖排首页为题头预留的列宽；横排为 0。/ Columns reserved for a vertical heading; 0 horizontally.
static unsigned s_chapter_lead_width;
static char s_font_path[192];
static char* s_text;
bool app_book_reader_body_visible(void) {
    return s_view == READING && s_text && s_reader_panel == READER_PANEL_NONE && !s_clear_confirm;
}
static blk_t* s_blocks;
// 书内字体的 run 表与块表同生共死：都来自同一次 html_text 解析，排版直接借用。
// The face run table lives and dies with the block table: both come from the same html_text
// parse, and the layout borrows them.
static html_run_t* s_runs;
static size_t s_run_count;
// 上一次排版用的书内字体开关：字体页改过之后，回阅读页要重排当前章才看得出来。
// The embedded-face setting the current layout was built with: after the font page changes
// it, the reader must re-typeset the chapter before the change is visible.
static bool s_book_fonts_applied;
// 字体开关改了之后要重排当前章，但那次重排不能在触摸回调里做：栈太深。
// The font switch needs the current chapter re-typeset, but not from the touch callback: the
// stack there is too deep.
static bool s_fonts_reload_pending;
static char** s_images;
static size_t s_image_count;
// PR #7 的当前页图片集合：只保留显示页的灰阶，不限制为八张。
// PR #7's current-page image set retains visible grayscale only, without an eight-image limit.
typedef struct { uint8_t *gray; int width, height; } reader_image_t;
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_page_images_for = SIZE_MAX;
static uint32_t s_page_images_generation;
static size_t s_block_count;
static size_t s_text_len, s_chapter, s_page;
static size_t s_selected_toc = SIZE_MAX;
static bool s_toc_jump_open, s_toc_jump_drag;
static int s_toc_jump_percent;
static size_t s_jump_offset = SIZE_MAX, s_jump_page;
static uint32_t s_file_size;
static int s_px, s_margin, s_line_spacing, s_turns, s_unsaved;
static int64_t s_poll_ms, s_last_turn_ms;
static int64_t s_stats_last_ms, s_stats_activity_ms;
static uint32_t s_stats_pending_ms, s_stats_pending_turns;
static uint32_t s_session_read_ms, s_session_turns;
static int s_font_page;
static int s_reader_slider = -1;
static bool s_reader_slider_endpoint;
static int s_reader_preview_px, s_reader_preview_margin, s_reader_preview_line, s_reader_preview_para;
static int s_reader_preview_tracking;
static uint32_t s_recent_days[30];
static bool s_recent_days_valid;
static char s_reader_notice[64];
static int64_t s_reader_notice_until;
static int s_bookmark_page;
static bool s_bookmark_edit, s_bookmark_delete_confirm, s_bookmark_delete_error;
static uint32_t s_bookmark_selected;
typedef struct { char path[288]; } reader_background_item_t;
static reader_background_item_t *s_backgrounds;
static int s_background_count, s_background_selected, s_background_page;
#define READER_BACKGROUND_ROWS 7
static bool s_background_ready;
static char s_background_notice[96];
static char s_background_preview_notice[96];
static bool s_background_prepare_pending;
static EpdRect s_area;
static enum EpdDrawMode s_mode = MODE_GL16;
static bool s_input_settle;
static bool s_reader_cleanup;
static bool s_reader_footer_pending;
static bool s_water_turn_pending;
static e0470_turn_dir_t s_water_turn_dir;
static int s_pressed_control = -1;
static bool s_shelf_feedback_pending;
static bool s_cover_batch_present_pending;
static bool s_shelf_page_pending;
static book_reader_tap_t s_reader_tap;
static unsigned s_reader_key_pick;
static char s_reader_key_error[64];
static int64_t s_du_ms;
static unsigned s_du_count;
static EpdRect s_du_area;
static SemaphoreHandle_t s_draw_lock, s_prep_done;
static TaskHandle_t s_prep_task;
static atomic_bool s_prep_stop;
static book_layout_draw_cursor_t s_prep_cursor;
static uint32_t s_next_generation;
static uint8_t* s_next_fb;
static int s_next_page = -1, s_prep_page = -1;

#define s_toolbar (s_reader_panel != READER_PANEL_NONE)

static void render(app_ctx_t* ctx, uint8_t* fb);
static void draw_reader_panel(uint8_t* fb);
static EpdRect reader_slider_rect(int slider);
static void scan_shelf(app_ctx_t* ctx);
static void copy_text(char* dst, size_t cap, const char* src);
static void fit_text(char* text, int px, int width);
static void free_book(void);
static void save_progress(void);
static void invalidate_prep(void);
static void prepare_inline_image(void);
static void release_page_images(void);
static void sort_shelf(app_ctx_t* ctx);
static void prepare_covers(app_ctx_t* ctx);
static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx);
static app_redraw_t toggle_reader_landscape(app_ctx_t* ctx);
bool app_book_request_open(const char* path) {
    if (!path || !path[0] || strnlen(path, sizeof(s_requested_open)) >= sizeof(s_requested_open)) return false;
    copy_text(s_requested_open, sizeof(s_requested_open), path);
    s_requested_open_home = false;
    s_requested_fullscreen = true;
    return true;
}
bool app_book_request_open_from_home(const char* path) {
    if (!app_book_request_open(path)) return false;
    s_requested_open_home = true;
    return true;
}
bool app_book_request_resume(const char *path, bool fullscreen) {
    if (!app_book_request_open_from_home(path)) return false;
    s_requested_fullscreen = fullscreen;
    return true;
}
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen) {
    if (!s_text || !s_path[0] || s_view == SHELF || s_view == MANAGE || s_view == BULK ||
        !path || !fullscreen || strlen(s_path) >= capacity || s_save_failed) return false;
    copy_text(path, capacity, s_path);
    *fullscreen = s_reader_fullscreen;
    return true;
}
void app_book_request_manage(void) { s_requested_manage = true; }
static void draw_control(uint8_t* fb, EpdRect rect, const char* label, int id) {
    if (id == 112 && s_query[0]) {
        ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
        ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2,
                   UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_pressed_control == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, label, false);
}
static void lock_draw(void) { if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY); }
static void unlock_draw(void) { if (s_draw_lock) xSemaphoreGive(s_draw_lock); }
static size_t fb_bytes(void) { return (size_t)epd_width() * epd_height() / 2; }
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
// 正文上方留出一致的呼吸空间，下沿靠近进度区；两种模式共用排版与刷新边界。
// Give the body a consistent top inset and extend it toward the progress strip in both modes.
static EpdRect reader_area(void) {
    const int top = s_reader_fullscreen ? (app_settings_reader_immersive() ? 0 : 80) : 160;
    // 全屏进度线单独差分，翻页时不让正文刷新反复驱动整条横线。
    // Keep the full-screen progress strip out of body updates so turns do not drive the whole bar.
    const int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP : UI_BAR_TOP + 8;
    return (EpdRect){0, top, UI_LOCK_WIDTH, bottom - top};
}
static EpdRect reader_fullscreen_progress_area(void) {
    return (EpdRect){0, READER_FULLSCREEN_PROGRESS_TOP,
                     UI_LOCK_WIDTH, UI_LOCK_HEIGHT - READER_FULLSCREEN_PROGRESS_TOP};
}
static EpdRect body_rect_for_tracking(int tracking_index) {
    int top = s_reader_fullscreen ? (app_settings_reader_immersive() ? 24 : 96) : 188;
    int margin = s_margin;
    // Leave only a narrow strip for the full-book progress line in full screen.
    int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP - 2 : UI_BAR_TOP + 8;
    int tracking = (tracking_index - 2) * 2;
    // 竖排把「边距」放到上下两端，左右只留一条窄边；整字收齐的是列高，不是行宽。
    // Vertical setting moves the margin setting to the top and bottom edges, keeps a narrow side
    // inset, and rounds the column height to whole glyphs instead of rounding a line's width.
    if (app_settings_book_vertical()) {
        EpdRect upright = {24, top + margin, UI_LOCK_WIDTH - 48, bottom - top - 2 * margin};
        EpdRect turned = book_layout_balanced_rect(
            (EpdRect){upright.y, upright.x, upright.height, upright.width}, s_px, tracking);
        return (EpdRect){turned.y, turned.x, turned.height, turned.width};
    }
    EpdRect outer = {margin, top, UI_LOCK_WIDTH - 2 * margin, bottom - top};
    return book_layout_balanced_rect(outer, s_px, tracking);
}
static EpdRect body_rect(void) {
    return body_rect_for_tracking(app_settings_book_tracking());
}
static int reader_margin_width(int margin, int px, int tracking_px) {
    EpdRect outer = {margin, 0, UI_LOCK_WIDTH - 2 * margin, 1};
    return book_layout_balanced_rect(outer, px, tracking_px).width;
}
static int reader_margin_levels(int px, int tracking_px,
                                int widths[BOOK_MARGIN_CHOICES], int values[BOOK_MARGIN_CHOICES]) {
    int count = 0;
    for (int i = 0; i < BOOK_MARGIN_CHOICES; ++i) {
        int value = BOOK_MARGIN_MIN + i * 6;
        int width = reader_margin_width(value, px, tracking_px);
        if (!count || widths[count-1] != width) {
            widths[count] = width;
            values[count++] = value;
        } else if (count > 1 && abs(value - 24) < abs(values[count-1] - 24)) {
            values[count-1] = value;
        }
    }
    // 同字格宽度只显示一个档位；极值仍到0/48，已有边距不因重画滑杆改写。
    // Show one stop per distinct column width, retaining the 0/48 extremes and existing saved margins.
    values[0] = BOOK_MARGIN_MIN;
    if (count > 1) values[count-1] = BOOK_MARGIN_MAX;
    return count;
}
static int reader_margin_level_for(int margin, int px, int tracking_px) {
    if (margin < BOOK_MARGIN_MIN) margin = BOOK_MARGIN_MIN;
    if (margin > BOOK_MARGIN_MAX) margin = BOOK_MARGIN_MAX;
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    int width = reader_margin_width(margin, px, tracking_px);
    for (int i = 0; i < count; ++i) if (width >= widths[i]) return i;
    return count - 1;
}
static int reader_margin_for_level(int level, int current, int px, int tracking_px) {
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    if (level < 0) level = 0;
    if (level >= count) level = count - 1;
    return level == reader_margin_level_for(current, px, tracking_px) ? current : values[level];
}
static EpdRect progress_rect(void) {
    // 阅读页没有右侧菜单按钮；进度信息应和页眉分隔线一样铺满内容宽度。
    // The reader has no right-hand menu button; the footer spans the full content width.
    return (EpdRect){36, UI_BAR_TOP, UI_LOCK_WIDTH - 72, UI_BAR_H};
}
static int shelf_rows(void) { return s_view == SHELF && app_settings_shelf_style() == 5 ? 4 : BOOK_GRID_ROWS; }
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_HEIGHT 214
#define SHELF_FAST_COVER_STRIDE ((SHELF_FAST_COVER_WIDTH + 7) / 8)
#define SHELF_FAST_COVER_BYTES (SHELF_FAST_COVER_STRIDE * SHELF_FAST_COVER_HEIGHT)
#define SHELF_FAST_CACHE_RESERVE (512u * 1024u)
#define SHELF_GUARD_CACHE_BYTES (644u * 90u / 2u)
typedef struct { EpdRect rect; int rotation, contrast; bool binary, valid; } shelf_guard_key_t;
static uint8_t *s_guard_cache;
static shelf_guard_key_t s_guard_keys[3];
static bool s_guard_cache_attempted;
static void release_guard_cache(void) {
    free(s_guard_cache); s_guard_cache=NULL; s_guard_cache_attempted=false;
    memset(s_guard_keys,0,sizeof(s_guard_keys));
}
// 缓存精确的输入和成品；逐行比对底图，任何封面、位置或模式变化均重绘。
// Cache exact inputs and finished pixels; compare every input row so art, position and mode changes redraw.
static void draw_shelf_guard(uint8_t *fb,EpdRect rect,int slot) {
    const bool binary=s_view==SHELF&&app_settings_main_fast_refresh();
    EpdRect p=ui_rotate_rect_to_fb(rect);
    const bool fits=slot>=0&&slot<3&&!(p.x&1)&&!(p.width&1)&&p.x>=0&&p.y>=0&&
        p.x+p.width<=epd_width()&&p.y+p.height<=epd_height()&&
        (size_t)p.width*p.height/2<=SHELF_GUARD_CACHE_BYTES;
    if(fits&&!s_guard_cache&&!s_guard_cache_attempted) {
        s_guard_cache_attempted=true;
        size_t bytes=6u*SHELF_GUARD_CACHE_BYTES;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>=bytes+2u*1024u*1024u)
            s_guard_cache=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    }
    if(!fits||!s_guard_cache){ui_draw_acrylic_guard(fb,rect);if(binary)ui_acrylic_bw_rect(fb,rect);return;}
    shelf_guard_key_t *key=&s_guard_keys[slot];
    size_t stride=(size_t)epd_width()/2,span=(size_t)p.width/2;
    uint8_t *input=s_guard_cache+2u*slot*SHELF_GUARD_CACHE_BYTES,*output=input+SHELF_GUARD_CACHE_BYTES;
    bool same=key->valid&&key->rotation==epd_get_rotation()&&key->contrast==app_settings_system_contrast()&&
        key->binary==binary&&!memcmp(&key->rect,&rect,sizeof(rect));
    for(int y=0;same&&y<p.height;++y)
        same=!memcmp(fb+(size_t)(p.y+y)*stride+p.x/2,input+(size_t)y*span,span);
    if(!same) {
        key->valid=false;
        for(int y=0;y<p.height;++y)memcpy(input+(size_t)y*span,fb+(size_t)(p.y+y)*stride+p.x/2,span);
        ui_draw_acrylic_guard(fb,rect);if(binary)ui_acrylic_bw_rect(fb,rect);
        for(int y=0;y<p.height;++y)memcpy(output+(size_t)y*span,fb+(size_t)(p.y+y)*stride+p.x/2,span);
        *key=(shelf_guard_key_t){.rect=rect,.rotation=epd_get_rotation(),.contrast=app_settings_system_contrast(),.binary=binary,.valid=true};
    } else for(int y=0;y<p.height;++y)memcpy(fb+(size_t)(p.y+y)*stride+p.x/2,output+(size_t)y*span,span);
}
static EpdRect row_rect(int row) {
    if (s_view == SHELF && app_settings_shelf_style() == 5)
        return (EpdRect){36, 184 + row * 197, 612, 197};
    if (s_view != BULK) {
        int col = row % 3, line = row / 3;
        int cell = (ui_content_width() - 2 * UI_GAP) / 3;
        return (EpdRect){UI_MARGIN + col * (cell + UI_GAP), (s_view == SHELF ? 192 : 224) + line * 282, cell, 260};
    }
    return (EpdRect){UI_MARGIN, 308 + row * (UI_BTN_H + UI_GAP), ui_content_width(), UI_BTN_H};
}
static EpdRect shelf_cover_image(int row) {
    EpdRect card = row_rect(row);
    if (s_view != SHELF || app_settings_shelf_style() != 5)
        return (EpdRect){card.x + (card.width - 164) / 2, card.y, 164, 214};
    // 列表所有封面统一尺寸；原图按比例铺满并居中裁切。/ Uniform list boxes use aspect fill and center crop.
    return (EpdRect){71, card.y + (197-156)/2, 120, 156};
}
static unsigned s_cover_generation;
static void invalidate_covers(void) {
    book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;
    release_guard_cache();
    s_cover_pending_mask = s_cover_decode_mask = s_cover_warm_mask = 0;
    s_cover_dirty_mask = s_cover_present_mask = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) {
        free(s_covers[i].gray);
        free(s_covers[i].fast_bits);
        s_covers[i].gray = NULL;
        s_covers[i].fast_bits = NULL;
        s_covers[i].index = -1;
    }
}
static void release_fast_covers(void) {
    for (int i=0;i<BOOK_ROWS;++i) {
        free(s_covers[i].fast_bits);
        s_covers[i].fast_bits=NULL;
    }
}
static bool fast_cover_matches(int row, EpdRect image) {
    return s_covers[row].fast_bits && s_covers[row].fast_contrast==app_settings_system_contrast() &&
        s_covers[row].fast_phase==((image.x&3)|((image.y&3)<<2));
}
static bool cover_favorite_needs_white_edge(const uint8_t *gray, EpdRect image, unsigned sw, unsigned sh);
static void prepare_fast_covers(void) {
    // 转换成功即释放灰阶；不释放仍有效的黑白缓存，避免重复切页重新解码。
    // Release gray immediately after conversion, retaining valid packed artwork across tab changes.
    if (s_view != SHELF || !app_settings_main_fast_refresh()) { release_fast_covers(); return; }
    for (int row = 0; row < BOOK_ROWS; ++row) {
        if (!s_covers[row].gray) continue;
        EpdRect image = shelf_cover_image(row);
        if (row < shelf_rows() && row < BOOK_GRID_ROWS && !fast_cover_matches(row, image)) {
            free(s_covers[row].fast_bits); s_covers[row].fast_bits = NULL;
            // 转换后回收的灰阶空间也计入保留量；内存不足时显示标题，不长期保留灰阶副本。
            // Count reclaimed gray toward the reserve; on OOM show a title rather than retain a gray duplicate.
            size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) + BOOK_COVER_W * BOOK_COVER_H;
            uint8_t *bits = free_after >= SHELF_FAST_CACHE_RESERVE + SHELF_FAST_COVER_BYTES
                ? heap_caps_calloc(1, SHELF_FAST_COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
            if (bits) {
                unsigned sw = app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W;
                unsigned sh = app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H;
                book_crop_t crop = book_cover_crop(sw, sh, image.width, image.height);
                unsigned sample_x[SHELF_FAST_COVER_WIDTH];
                uint8_t tones[256];
                for(int x=0;x<image.width;++x)sample_x[x]=crop.x+(uint64_t)(unsigned)x*crop.width/(unsigned)image.width;
                for(unsigned v=0;v<256;++v)tones[v]=(ui_contrast_gray((uint8_t)v)>>4)*17u;
                for (int y = 0; y < image.height; ++y) {
                    const unsigned sy = crop.y + (uint64_t)(unsigned)y * crop.height / (unsigned)image.height;
                    for (int x = 0; x < image.width; ++x) {
                        const unsigned sx = sample_x[x];
                        uint8_t tone = tones[s_covers[row].gray[sy * sw + sx]];
                        if (ui_image_dither_cover_bw(tone, image.x + x, image.y + y))
                            bits[y * SHELF_FAST_COVER_STRIDE + x / 8] |= (uint8_t)(1u << (x & 7));
                    }
                }
                s_covers[row].fast_white_edge = cover_favorite_needs_white_edge(s_covers[row].gray, image, sw, sh);
                s_covers[row].fast_bits = bits;
                s_covers[row].fast_contrast = app_settings_system_contrast();
                s_covers[row].fast_phase = (image.x & 3) | ((image.y & 3) << 2);
            }
        }
        free(s_covers[row].gray); s_covers[row].gray = NULL;
    }
}
void app_book_cover_mode_changed(void) { invalidate_covers(); }
static uint8_t* load_cover_gray(const char* source, const char* title, const char* author,
                                bool decode, bool* pending) {
    *pending = false;
    if (!pico_boot_asset_allowed(source)) return NULL;
    uint8_t* gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) return NULL;
    if (!book_cover_load_gray(source, title, author, gray, decode, pending)) {
        free(gray); return NULL;
    }
    return gray;
}
static uint8_t *load_shelf_cover_gray(int row, const char *source, const char *title,
                                      const char *author, bool decode, bool *pending) {
    if (s_view != SHELF || app_settings_shelf_style() != 5)
        return load_cover_gray(source, title, author, decode, pending);
    *pending = false;
    if (!pico_boot_asset_allowed(source)) return NULL;
    s_covers[row].width = BOOK_COVER_W; s_covers[row].height = BOOK_COVER_H;
    uint8_t *gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray && !book_cover_load_list_gray(source, title, author, gray, decode, pending,
                                          &s_covers[row].width, &s_covers[row].height)) {
        free(gray); gray = NULL;
    }
    return gray;
}

// 直接读最终点阵：不分配灰阶工作区，也不在每次书架切页重新转换。
// Read the finished raster: no gray workspace or repeated conversion on shelf paging.
static bool load_shelf_cover_fast(int row, int index) {
    if (s_view != SHELF || !app_settings_main_fast_refresh() || row >= BOOK_GRID_ROWS ||
        !pico_boot_asset_allowed(s_shelf[index].path)) return false;
    EpdRect image = shelf_cover_image(row);
    size_t bytes = (size_t)SHELF_FAST_COVER_STRIDE * image.height;
    if (bytes > SHELF_FAST_COVER_BYTES || heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        < SHELF_FAST_CACHE_RESERVE + SHELF_FAST_COVER_BYTES) return false;
    uint8_t *bits = heap_caps_calloc(1, SHELF_FAST_COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bits) return false;
    book_fast_cover_key_t key = {(unsigned)image.width, (unsigned)image.height, SHELF_FAST_COVER_STRIDE,
        app_settings_system_contrast(), (image.x&3)|((image.y&3)<<2)};
    bool edge;
    if (!book_cover_fast_cache_read(s_shelf[index].path, s_shelf[index].name, s_shelf[index].author,
                                   key, bits, &edge)) { free(bits); return false; }
    s_covers[row].fast_bits = bits; s_covers[row].fast_white_edge = edge;
    s_covers[row].fast_contrast = key.contrast; s_covers[row].fast_phase = key.phase;
    return true;
}
static void save_shelf_cover_fast(int row, int index) {
    if (!s_covers[row].fast_bits) return;
    EpdRect image = shelf_cover_image(row);
    book_fast_cover_key_t key = {(unsigned)image.width, (unsigned)image.height, SHELF_FAST_COVER_STRIDE,
        s_covers[row].fast_contrast, s_covers[row].fast_phase};
    book_cover_fast_cache_write(s_shelf[index].path, s_shelf[index].name, s_shelf[index].author,
                                key, s_covers[row].fast_bits, s_covers[row].fast_white_edge);
}

static unsigned prepare_cover_batch(app_ctx_t *ctx,bool cache_only) {
    if((s_view!=SHELF&&s_view!=MANAGE)||(!cache_only&&ctx->touch&&ctx->touch->touched))return 0;
    unsigned changed=0;
    for(int row=0;row<shelf_rows();++row){
        unsigned bit=1u<<row;if(!(s_cover_pending_mask&bit))continue;
        if(s_view==SHELF&&app_poll_main_navigation(ctx))return changed;
        int index=ctx->leaf*shelf_rows()+row;
        if(index<0||index>=s_visible_count||s_covers[row].index!=index||s_shelf[index].removed){s_cover_pending_mask&=~bit;continue;}
        EpdRect image=shelf_cover_image(row);
        book_cover_job_t request={.list=s_view==SHELF&&app_settings_shelf_style()==5,
            .fast=s_view==SHELF&&app_settings_main_fast_refresh(),.width=image.width,.height=image.height,
            .stride=SHELF_FAST_COVER_STRIDE,.contrast=app_settings_system_contrast(),
            .phase=(image.x&3)|((image.y&3)<<2),.x=image.x,.y=image.y};
        copy_text(request.path,sizeof(request.path),s_shelf[index].path);
        copy_text(request.title,sizeof(request.title),s_shelf[index].name);
        copy_text(request.author,sizeof(request.author),s_shelf[index].author);
        if(!s_cover_generation)s_cover_generation=book_cover_jobs_begin();
        if(!book_cover_jobs_submit(s_cover_generation,row,&request)){
            s_cover_generation=book_cover_jobs_begin();
            if(!book_cover_jobs_submit(s_cover_generation,row,&request)){s_cover_pending_mask&=~bit;continue;}
        }
        book_cover_result_t result={0};int state=book_cover_jobs_take(s_cover_generation,row,&result);
        if(!state)continue;
        s_cover_pending_mask&=~bit;s_cover_decode_mask&=~bit;
        if(state<0)continue;
        free(s_covers[row].gray);free(s_covers[row].fast_bits);
        s_covers[row].gray=result.fast?NULL:result.pixels;
        s_covers[row].fast_bits=result.fast?result.pixels:NULL;
        s_covers[row].width=result.width;s_covers[row].height=result.height;
        s_covers[row].fast_white_edge=result.white_edge;
        s_covers[row].fast_contrast=request.contrast;s_covers[row].fast_phase=request.phase;
        changed|=bit;s_cover_dirty_mask|=bit;
    }
    return changed;
}

// 分批只用于准备，不能让几张已解码封面先出现在屏幕或点击反馈中。
// Chunk preparation only: neither page painting nor press feedback reveals an incomplete cover group.
static bool shelf_cover_frame_ready(void) {
    return s_view != SHELF || s_cover_pending_mask == 0;
}
static unsigned shelf_cover_reveal_mask(void) {
    return shelf_cover_frame_ready() ? s_cover_dirty_mask : 0;
}

static void prepare_covers(app_ctx_t* ctx) {
    if (s_view != SHELF && s_view != MANAGE) return;
    int rows = shelf_rows();
    if (s_covers[0].index != ctx->leaf * rows) {
        book_cover_jobs_cancel(s_cover_generation);s_cover_generation=book_cover_jobs_begin();
        s_cover_pending_mask = s_cover_decode_mask = s_cover_warm_mask = 0;
        s_cover_dirty_mask = s_cover_present_mask = 0;
    }
    for (int row = 0; row < rows; ++row) {
        int index = ctx->leaf * rows + row;
        EpdRect image = shelf_cover_image(row);
        bool packed = s_view == SHELF && app_settings_main_fast_refresh();
        if (s_covers[row].index == index &&
            (packed ? fast_cover_matches(row, image) : s_covers[row].gray != NULL)) continue;
        if (s_covers[row].index == index && !s_covers[row].gray && !s_covers[row].fast_bits) continue;
        free(s_covers[row].gray); free(s_covers[row].fast_bits);
        s_covers[row].gray = s_covers[row].fast_bits = NULL;
        s_covers[row].index = index;
        s_cover_dirty_mask &= ~(1u<<row);
        if (index >= s_visible_count || index < 0 || s_shelf[index].removed) continue;
        // Queue each slot, then gather cheap disk-cache hits before painting; decode misses later.
        s_cover_pending_mask |= 1u << row;
        s_cover_warm_mask |= 1u << row;
        s_cover_warm_after_ms = ctx->now_ms + 1500;
    }
    prepare_fast_covers();
    (void)prepare_cover_batch(ctx, true);
}

// 首帧短暂汇合TF缓存命中，让题头、书架和封面一起显示；冷解码不无限阻塞。
// Briefly gather warm TF hits for a complete first frame; never wait indefinitely for cold decoding.
static void gather_cover_entry(app_ctx_t *ctx) {
    if(s_view!=SHELF||!s_cover_pending_mask||ctx->request_app||
       (s_presented_view==(int)SHELF&&!s_shelf_page_pending))return;
    const int64_t deadline=esp_timer_get_time()+60000;
    while(s_cover_pending_mask&&esp_timer_get_time()<deadline){
        if(app_poll_main_navigation(ctx))return;
        vTaskDelay(1);
        (void)prepare_cover_batch(ctx,true);
        if(ctx->request_app)return;
    }
}

// 画面和操作优先，预生成只在封面加载完、无触摸的空闲书架运行一次一本。
// Present and input take priority; warm one cover only on an idle shelf after visible loading completes.
static void warm_shelf_lock_cover(app_ctx_t *ctx) {
    if (s_view != SHELF || app_settings_lock_style() != 2 || s_clear_confirm || s_batch_confirm ||
        s_cover_pending_mask || !s_cover_warm_mask || ctx->now_ms < s_cover_warm_after_ms ||
        (ctx->touch && ctx->touch->touched) || ctx->request_app || ctx->request_return || ctx->request_menu) return;
    if (app_poll_main_navigation(ctx)) return;
    for (int row = 0; row < shelf_rows(); ++row) {
        const unsigned bit = 1u << row;
        if (!(s_cover_warm_mask & bit)) continue;
        s_cover_warm_mask &= ~bit; s_cover_warm_after_ms = ctx->now_ms + 1000;
        int index = ctx->leaf * shelf_rows() + row;
        if (index >= 0 && index < s_visible_count && s_covers[row].index == index && !s_shelf[index].removed)
            (void)book_lock_collage_warm_cover(s_shelf[index].path);
        (void)app_poll_main_navigation(ctx);
        break;
    }
}

static void draw_favorite_icon(uint8_t *fb, int x, int y, int w, int h, bool filled, uint8_t ink) {
    // 只绘制标记本身，封面上不加矩形衬底。/ Draw only the glyph, without a rectangular cover underlay.
    const int notch_y = y + h - 9;
    if (filled) {
        // 把 V 形尾部也填满，避免选中后只剩矩形黑块、尾部仍是空心。
        // Fill the V-shaped tail as well so the selected glyph is solid throughout.
        for (int py = y; py <= y + h; ++py) {
            int arm = py <= notch_y ? w / 2 : (w / 2) * (y + h - py) / 9;
            ui_fill_rect_fast((EpdRect){x, py, arm + 1, 1}, ink, fb);
            ui_fill_rect_fast((EpdRect){x + w - arm, py, arm + 1, 1}, ink, fb);
        }
        return;
    }
    // 空心态使用与阅读工具栏相近的两像素线宽。
    // Keep the outline close to the other reader toolbar icons in visual weight.
    ui_fill_rect_fast((EpdRect){x, y, w + 1, 2}, ink, fb);
    ui_fill_rect_fast((EpdRect){x, y, 2, h - 1}, ink, fb);
    ui_fill_rect_fast((EpdRect){x + w - 1, y, 2, h - 1}, ink, fb);
    epd_draw_line(x, y + h, x + w / 2, notch_y, ink, fb);
    epd_draw_line(x + 1, y + h - 1, x + w / 2, notch_y + 1, ink, fb);
    epd_draw_line(x + w / 2, notch_y, x + w, y + h, ink, fb);
    epd_draw_line(x + w / 2, notch_y + 1, x + w - 1, y + h - 1, ink, fb);
}
static void draw_shelf_favorite_icon(uint8_t *fb, int x, int y, int w, int h, bool white_edge) {
    if (white_edge) {
        // 仅沿书签轮廓扩两像素白边，深封面上也不出现白色矩形底块。
        // Dilate only the bookmark silhouette by two pixels, never a white backing box.
        const int notch_y = y + h - 9;
        for (int py = y; py <= y + h; ++py) {
            int arm = py <= notch_y ? w / 2 : (w / 2) * (y + h - py) / 9;
            ui_fill_rect_fast((EpdRect){x - 2, py - 2, arm + 5, 5}, UI_GRAY_WHITE, fb);
            ui_fill_rect_fast((EpdRect){x + w - arm - 2, py - 2, arm + 5, 5}, UI_GRAY_WHITE, fb);
        }
    }
    draw_favorite_icon(fb, x, y, w, h, true, UI_GRAY_BLACK);
}
static bool cover_favorite_needs_white_edge(const uint8_t *gray, EpdRect image, unsigned sw, unsigned sh) {
    if (!gray || !sw || !sh || image.width <= 0 || image.height <= 0) return false;
    unsigned sum = 0, count = 0;
    const book_crop_t crop = book_cover_crop(sw, sh, image.width, image.height);
    // 只在标记周围采样；图标始终为黑色，深封面才加白边。
    // Sample the badge area; keep the glyph black and outline it only on dark covers.
    for (int y = 9; y < 39; y += 5) {
        for (int x = 11; x < 35; x += 4) {
            unsigned sx = crop.x + (uint64_t)(unsigned)x * crop.width / (unsigned)image.width;
            unsigned sy = crop.y + (uint64_t)(unsigned)y * crop.height / (unsigned)image.height;
            if (sx >= sw) sx = sw - 1;
            if (sy >= sh) sy = sh - 1;
            sum += ui_contrast_gray(gray[sy * sw + sx]);
            ++count;
        }
    }
    return sum < count * 145;
}
static void draw_shelf_cover(uint8_t* fb, EpdRect card, int row, const char* name, bool favorite) {
    EpdRect image = shelf_cover_image(row);
    image.y += card.y - row_rect(row).y;
    const bool artwork = shelf_cover_frame_ready();
    ui_fill_rect_fast(image, UI_GRAY_LIGHT, fb);
    if (artwork && app_settings_main_fast_refresh() && s_view==SHELF && fast_cover_matches(row,image)) {
        ui_blit_mono_bitmap(fb,image,s_covers[row].fast_bits,SHELF_FAST_COVER_STRIDE);
    } else if (artwork && s_covers[row].gray) {

        const uint8_t *gray = s_covers[row].gray;
        // 封面缓冲是 176×240、这一格是 164×214；按长边铺满 + 居中裁剪，避免被压扁。
        // The cover buffer is 176x240 and this frame is 164x214; fill by the longer side and
        // centre-crop so the artwork is not squashed.
        const unsigned frame_width = (unsigned)image.width, frame_height = (unsigned)image.height;
        unsigned sw = s_view == SHELF && app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W;
        unsigned sh = s_view == SHELF && app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H;
        const book_crop_t crop = book_cover_crop(sw, sh, frame_width, frame_height);
        unsigned sample_x[SHELF_FAST_COVER_WIDTH];
        uint8_t tones[256];
        const bool sampled=image.width<=SHELF_FAST_COVER_WIDTH;
        const bool binary=app_settings_main_fast_refresh()&&s_view==SHELF;
        if(sampled)for(int x=0;x<image.width;++x)sample_x[x]=crop.x+(uint64_t)(unsigned)x*crop.width/frame_width;
        for(unsigned v=0;v<256;++v)tones[v]=ui_contrast_gray((uint8_t)v);
        for (int y = 0; y < image.height; ++y) {
            const int sy = (int)(crop.y + (uint64_t)(unsigned)y * crop.height / frame_height);
            for (int x = 0; x < image.width; ++x) {
                const unsigned sx = sampled?sample_x[x]:crop.x+(uint64_t)(unsigned)x*crop.width/frame_width;
                const uint8_t tone = tones[gray[sy * sw + sx]];
                epd_draw_pixel(image.x + x, image.y + y,
                               binary
                                   ? ui_image_dither_cover_bw((tone>>4)*17u, image.x + x, image.y + y)
                                   : ui_image_dither_gray(tone, image.x + x, image.y + y), fb);
            }
        }
    } else {
        char title[80]; copy_text(title, sizeof(title), name);
        char *dot = strrchr(title, '.'); if (dot) *dot = 0;
        fit_text(title, 26, image.width - 22);
        ui_text_vc(fb, image.x + image.width / 2, image.y + image.height / 2, 26,
                   title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, image, 0, UI_GRAY_BLACK);
    if (favorite && image.width >= 28 && image.height >= 42) {
        draw_shelf_favorite_icon(fb, image.x + 10, image.y + 8, 24, 31,
                                 artwork && ((app_settings_main_fast_refresh() && s_view == SHELF && fast_cover_matches(row, image))
                                     ? s_covers[row].fast_white_edge : cover_favorite_needs_white_edge(s_covers[row].gray, image,
                                         app_settings_shelf_style() == 5 ? s_covers[row].width : BOOK_COVER_W,
                                         app_settings_shelf_style() == 5 ? s_covers[row].height : BOOK_COVER_H)));
    }
}
static EpdRect paint_list_cover_feedback(app_ctx_t *ctx, int row) {
    EpdRect image = shelf_cover_image(row);
    EpdRect area = image;
    area.y -= SHELF_LIST_BOUNCE_PX;
    area.height += SHELF_LIST_BOUNCE_PX;
    // 两位置均位于白色列表卡片内；不清整行，也不绘制额外反馈框。
    // Both positions stay within the white list card; never clear a whole row or add a feedback frame.
    ui_fill_rect_fast(area, UI_GRAY_WHITE, ctx->fb);
    EpdRect card = row_rect(row);
    if (s_pressed_control == row) card.y -= SHELF_LIST_BOUNCE_PX;
    int index = ctx->leaf * shelf_rows() + row;
    if (index >= 0 && index < s_visible_count)
        draw_shelf_cover(ctx->fb, card, row, s_shelf[index].name, s_shelf[index].favorite);
    return area;
}
static void draw_list_title(uint8_t *fb, const char *name, int top) {
    char lines[2][128] = {{0}};
    int px = ttf_em_height_px(27);
    const unsigned char *at = (const unsigned char *)name;
    for (int line = 0; line < 2 && *at; ++line) {
        size_t used = 0;
        while (*at && used < sizeof(lines[line])-5) {
            unsigned n = *at < 128 ? 1 : (*at & 0xe0)==0xc0 ? 2 : (*at & 0xf0)==0xe0 ? 3 : 4;
            if (strlen((const char *)at) < n) break;
            memcpy(lines[line]+used, at, n); lines[line][used+n] = 0;
            if (ui_text_fixed_width_px(px, lines[line]) > 378) { lines[line][used] = 0; break; }
            used += n; at += n;
        }
        if (line == 1 && *at) fit_text(lines[line], px, 352);
        ui_text_fixed_vc(fb, 244, top + line*35, px, lines[line], EPD_DRAW_ALIGN_LEFT, false);
    }
}
static void draw_list_row(uint8_t *fb, int row, const shelf_entry_t *item) {
    EpdRect card = row_rect(row);
    draw_list_title(fb, item->name, card.y + 42);
    char author[128]; copy_text(author, sizeof(author), item->author[0] ? item->author : "作者未知");
    int author_px = ttf_em_height_px(21), progress_px = ttf_em_height_px(19);
    fit_text(author, author_px, 378);
    int author_y = ui_text_fixed_width_px(ttf_em_height_px(27), item->name) > 378 ? 106 : 83;
    ui_text_fixed_ink_vc(fb, 244, card.y + author_y, author_px, author, EPD_DRAW_ALIGN_LEFT, 0x60);
    unsigned progress = item->has_progress ? item->pct : 0;
    if (progress > 100) progress = 100;
    char label[48]; snprintf(label, sizeof(label), progress == 100 ? "已读完 · %u%%" :
                              progress ? "已读 %u%%" : "未开始 · %u%%", progress);
    ui_text_fixed_vc(fb, 244, card.y + 143, progress_px, label, EPD_DRAW_ALIGN_LEFT, false);
    EpdRect bar = {244, card.y + 163, 378, 11};
    ui_fill_round_rect(fb, bar, 5, 0xd0);
    if (progress) {
        EpdRect fill = bar; fill.width = bar.width * progress / 100;
        if (fill.width < 1) fill.width = 1;
        ui_fill_round_rect(fb, fill, 5, 0x40);
    }
    ui_draw_control_frame(fb, bar, 5, 0x50);
    if (row < 3) ui_draw_separator(fb, card.y + 196, 50, 584, 0x60);
}
static void draw_shelf_furniture(uint8_t* fb) {
    uint8_t style = app_settings_shelf_style();
    if (!style || style == 5) return;
    for (int row = 0; row < 3; ++row) {
        int top = (s_view == SHELF ? 192 : 224) + row * 282;
        if (style == 1) {
            ui_fill_rect_fast((EpdRect){36, top + 214, 612, 12}, 0x30, fb);
            ui_hairline(fb, top + 214, 36, 612, 0x58);
            ui_hairline(fb, top + 225, 36, 612, 0x10);
        } else if (style == 2) {
            EpdRect acrylic = {20, top + 136, 644, 90};
            draw_shelf_guard(fb, acrylic, row);
            if(s_view==SHELF&&app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL){
                EpdRect bands[3];for(int i=0;i<3;++i)bands[i]=(EpdRect){0,328+i*282,UI_LOCK_WIDTH,90};
                display_main_transition_gray_bands(bands,3);
            }

        }
    }
}
static EpdRect tool_rect(int i) {
    int x0 = i * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    int x1 = (i + 1) * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    return (EpdRect){x0, 1096, x1 - x0, 120};
}
// 工具条的标签与图标必须是逐项对齐的一份清单。设置页那一侧有 _Static_assert 盯着
// 图标编号表，这里原本没有——少加一个数组元素会让「点生词本触发目录」这类错位
// 静默发生，所以两张表都断言成同一长度。
// Label and icon tables must line up item by item. The settings page has a
// _Static_assert watching its icon slot table; this one had none, and a missing
// entry would silently shift every tap (tapping the vocabulary book opening the
// TOC). Both tables are now asserted against the same count.
static const char* const k_tool_labels[BOOK_TOOL_COUNT] = {
    "目录", "书签", "阅读统计", "阅读设置", "字体设置", "生词本",
};
static const ui_icon_t k_tool_icons[BOOK_TOOL_COUNT] = {
    UI_ICON_LIST,          // 0 目录 / TOC
    UI_ICON_BOOKMARK,      // 1 书签 / bookmark
    UI_ICON_CHART_COLUMN,  // 2 阅读统计 / reading stats
    UI_ICON_BOOK_OPEN,     // 3 阅读设置 / reading settings
    UI_ICON_TYPE,          // 4 字体设置 / font settings
    UI_ICON_A_LARGE_SMALL, // 5 生词本 / vocabulary book (Lucide "a-large-small")
};
_Static_assert(sizeof(k_tool_labels) / sizeof(k_tool_labels[0]) == BOOK_TOOL_COUNT,
               "every reader tool needs a label");
_Static_assert(sizeof(k_tool_icons) / sizeof(k_tool_icons[0]) == BOOK_TOOL_COUNT,
               "every reader tool needs an icon");
static int leaves(void) {
    if (s_view == TOC) return book_toc_pages(book_navigation_count());
    int count = s_visible_count;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    return count ? 1 + (count - 1) / rows : 1;
}
static EpdRect shelf_manage_rect(void) { return (EpdRect){442, 94, 97, 54}; }
static EpdRect shelf_import_rect(void) { return (EpdRect){551, 94, 97, 54}; }
static void draw_shelf_header_button(uint8_t *fb, EpdRect r, bool importing) {
    bool pressed = s_pressed_control == (importing ? 115 : 114);
    ui_fill_round_rect(fb, r, 18, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, r, 18, 0x70);
    if (pressed) ui_draw_pressed_round_rect(fb, r, 18);
    ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2 + (pressed ? 2 : 0), 20,
               importing ? "+ 导入" : "管理", EPD_DRAW_ALIGN_CENTER, false);
}
static EpdRect shelf_page_arrow_rect(int direction) {
    return (EpdRect){direction < 0 ? 168 : 444, 1008, 72, 60};
}
static void draw_shelf_pager(uint8_t *fb, int count, int page, int pages) {
    char label[64];
    snprintf(label, sizeof(label), "%d本书 · %02d/%02d", count, page + 1, pages);
    ui_text_vc(fb, 342, 1038, 21, label, EPD_DRAW_ALIGN_CENTER, false);
    ui_draw_icon(fb, 204, 1038, 28, UI_ICON_CHEVRON_LEFT, page > 0 ? 0 : 0x90);
    ui_draw_icon(fb, 480, 1038, 28, UI_ICON_CHEVRON_RIGHT, page + 1 < pages ? 0 : 0x90);
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction);
static EpdRect import_rect(int index) { return (EpdRect){36, 230 + index * 164, 612, 136}; }
static void clean_filename(char *dst, size_t cap, const char *filename) {
    copy_text(dst, cap, filename);
    char *ext = strrchr(dst, '.');
    if (ext) *ext = 0;
    char *start = dst;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != dst) memmove(dst, start, strlen(start) + 1);
    size_t len = strlen(dst);
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = 0;
}

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void fit_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void fit_fixed_text(char* text, int px, int width) {
    while (*text && ui_text_fixed_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void reader_footer_strip_number(char *dst, size_t cap, const char *source) {
    char title[128];
    copy_text(title, sizeof(title), source ? source : "");
// name 只被读取，但下面会把字符串字面量赋给它。CI 的 sdkconfig.ci 开了
// CONFIG_COMPILER_WARN_WRITE_STRINGS，字面量因此是 const char[]，赋给 char* 会丢 const 并在
// -Werror 下报错。声明成 const 指针即可。
// name is only read from, but it is assigned a string literal below. CI's sdkconfig.ci enables
// CONFIG_COMPILER_WARN_WRITE_STRINGS, which makes literals const char[], so assigning one to a
// char* discards the qualifier and fails under -Werror. A const pointer is enough.
    const char *name = title;
    while (*name == ' ' || *name == '\t') ++name;
    char *chapter_mark = !strncmp(name, "第", strlen("第")) ? strstr(name, "章") : NULL;
    if (chapter_mark && chapter_mark - name < 24) {
        char *after = chapter_mark + strlen("章");
        while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
               !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、")) ||
               !strncmp(after, "·", strlen("·"))) {
            if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
            else after += strlen("：");
        }
        name = after;
    }
    if (!*name || !strcmp(name, "本章") || !strcmp(name, "章节")) name = "未命名章节";
    copy_text(dst, cap, name);
}
static void reader_footer_chapter_name(char *dst, size_t cap) {
    char source[128] = {0};
    bool selected = s_selected_toc < book_navigation_count() &&
        book_navigation_chapter(s_selected_toc) == s_chapter &&
        book_navigation_title(s_selected_toc, source, sizeof(source)) == ESP_OK && source[0];
    if (!selected) {
        if (s_chapter_heading_title[0]) copy_text(source, sizeof(source), s_chapter_heading_title);
        else if (book_chapter_title(s_chapter, source, sizeof(source)) != ESP_OK) source[0] = 0;
    }
    reader_footer_strip_number(dst, cap, source);
}
static uint32_t chapter_end(void) {
    return s_chapter + 1 < book_chapter_count()
        ? book_chapter_byte_offset(s_chapter + 1) : book_total_bytes();
}
static size_t reader_page_offset(size_t page) {
    size_t off = book_layout_page_start_offset(page);
    if (s_jump_offset != SIZE_MAX && page == s_jump_page && s_jump_offset > off)
        off = s_jump_offset;
    return off;
}
// 视觉页寻址 API 前向声明：percent 的末页判定要走视觉页口径。定义留在地图区块内
// （互证测试按区块抽取，percent 留在区块外，抽取内容因此逐字节不变）。
// Forward declarations of the visual-page API: percent's last-page check takes the
// visual-page view. The definitions stay in the map block (which the cross-check
// test extracts); keeping percent outside leaves the extracted text byte-identical.
static size_t reader_visual_count(void);
static size_t reader_visual_index(size_t logical);
static unsigned percent(size_t page) {
    uint32_t total = book_total_bytes();
    if (!total) return 0;
    if (s_chapter + 1 == book_chapter_count() &&
        reader_visual_index(page) + 1 == reader_visual_count()) return 100;
    uint32_t off = book_position_bytes(book_chapter_byte_offset(s_chapter), chapter_end(),
                                       reader_page_offset(page), s_text_len);
    return (unsigned)((uint64_t)off * 100 / total);
}
static pending_progress_t* pending_find(const char* path) {
    for (pending_progress_t* p = s_pending; p; p = p->next) if (!strcmp(p->path, path)) return p;
    return NULL;
}
static int layout_name(uint8_t* fb, const char* name, int y, bool draw);
static EpdRect manage_panel(void) {
    int height = layout_name(NULL, s_managed.name, 0, false) + 495;
    return (EpdRect){UI_MARGIN - 16, 190 + (876 - height) / 2, ui_content_width() + 32, height};
}
static EpdRect manage_rect(int index, int count) {
    EpdRect panel = manage_panel();
    return ui_row_rect(index, count, panel.y + panel.height - 94, 76);
}
// 六个批量按钮排成两行各三格：上排选择与重扫，下排三个需要二次确认的操作。
// Six batch controls in two rows of three: selection and rescan on top, the three actions that
// ask for confirmation below.
static EpdRect batch_rect(int id) {
    return (EpdRect){36 + (id % 3) * 208, id < 3 ? 918 : 988, 196, 58};
}
static EpdRect manage_back_rect(void) { return (EpdRect){36, 92, 44, 44}; }
static EpdRect bulk_filter_rect(int id) { return (EpdRect){36 + id * 208, 216, 196, 70}; }
static EpdRect search_rect(int id) {
    if (id < 40) {
        int width = (ui_content_width() - 54) / 10;
        return (EpdRect){UI_MARGIN + id % 10 * (width + 6), 388 + id / 10 * 100, width, 88};
    }
    if (id < 43) return ui_row_rect(id - 40, 3, 808, 80);
    return ui_bar_rect(id - 43, 2);
}
static size_t selected_count(void) {
    size_t selected = 0;
    for (int i = 0; i < s_count; ++i) if (s_shelf[i].selected) ++selected;
    return selected;
}
static void clear_selection(void) {
    for (int i = 0; i < s_count; ++i) s_shelf[i].selected = false;
}
static void toggle_selection(int index) {
    if (index >= 0 && index < s_visible_count) s_shelf[index].selected = !s_shelf[index].selected;
}
static void select_page(int page) {
    for (int i = page * BOOK_BULK_ROWS; i < s_visible_count && i < (page + 1) * BOOK_BULK_ROWS; ++i) s_shelf[i].selected = true;
}

static void search_begin(void) {
    s_search_parent = s_view;
    memcpy(s_search_draft, s_query, sizeof(s_query));
    ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft));
    ui_keyboard_begin(&s_search_input, false);
    s_view = SEARCH;
}
static void refresh_search_matches(void) {
    for (int i = 0; i < s_count; ++i)
        s_shelf[i].search_match = read_pico_search_match(s_shelf[i].name, s_query);
}
static void search_finish(app_ctx_t* ctx, bool apply) {
    ui_keyboard_end();
    s_view = s_search_parent;
    if (apply) {
        memcpy(s_query, s_search_draft, sizeof(s_query));
        refresh_search_matches();
        clear_selection();
        sort_shelf(ctx);
        s_batch_message[0] = 0;
    }
    memset(s_search_draft, 0, sizeof(s_search_draft));
    ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft));
}

static bool pending_reserve(const char* path) {
    if (pending_find(path)) return true;
    pending_progress_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    copy_text(p->path, sizeof(p->path), path);
    // 阅读页内注册；传书 HTTP 尚未启动，退出后也不注销失败项。
    // Register in reading before transfer HTTP starts; failed records survive page exit.
    p->watch = book_progress_watch_create(path);
    if (!p->watch) { free(p); return false; }
    pending_progress_t** tail = &s_pending;
    while (*tail) tail = &(*tail)->next;
    *tail = p;
    return true;
}
static bool pending_restore(const char* path, uint32_t size, book_progress_t* out) {
    pending_progress_t* pending = pending_find(path);
    if (!pending || !pending->dirty || pending->value.file_size != size) return false;
    *out = pending->value;
    return true;
}
static void pending_discard(const char* path) {
    pending_progress_t** p = &s_pending;
    while (*p) {
        if (!strcmp((*p)->path, path)) {
            pending_progress_t* old = *p;
            *p = old->next;
            book_progress_watch_destroy(old->watch);
            free(old);
            break;
        }
        p = &(*p)->next;
    }
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}
static void pending_mark_latest(const char* path) {
    pending_progress_t** item = &s_pending;
    while (*item && strcmp((*item)->path, path)) item = &(*item)->next;
    if (*item) {
        pending_progress_t* current = *item;
        *item = current->next;
        current->next = NULL;
        pending_progress_t** tail = &s_pending;
        while (*tail) tail = &(*tail)->next;
        *tail = current;
    }
    copy_text(s_latest_path, sizeof(s_latest_path), path);
}
static void pending_drop_invalidated(void) {
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        if (book_progress_watch_invalidated(p->watch)) {
            if (p->dirty) s_pending_invalidated = true;
            pending_discard(p->path);
        }
        p = next;
    }
}
static delete_retry_t* delete_retry_find(const char* path) {
    for (delete_retry_t* p = s_delete_retries; p; p = p->next)
        if (!strcmp(p->entry.path, path)) return p;
    return NULL;
}
static bool pending_flush(pending_progress_t* p) {
    if (!p->dirty) return true;
    if (!p->progress_saved) {
        if (book_progress_save(p->path, &p->value) != ESP_OK) return false;
        p->progress_saved = true;
    }
    if (!strcmp(p->path, s_latest_path) && book_progress_set_last_path(p->path) != ESP_OK) return false;
    p->dirty = false;
    return true;
}
static void retry_progress(void) {
    s_save_failed = false;
    bool had_dirty = false;
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        bool dirty = p->dirty;
        had_dirty |= dirty;
        if (s_text && !strcmp(p->path, s_path)) { p = next; continue; }
        if (!pending_flush(p)) s_save_failed = true;
        else if (strcmp(p->path, s_path)) pending_discard(p->path);
        else if (dirty) s_unsaved = 0;
        p = next;
    }
    if (s_text && (s_unsaved || had_dirty)) save_progress();
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}

/* ---- 管理详情 / Management details ---- */
static int layout_name(uint8_t* fb, const char* name, int y, bool draw) {
    const char* at = name;
    while (*at) {
        char line[256];
        size_t used = 0;
        while (at[used]) {
            unsigned char first = (unsigned char)at[used];
            size_t n = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            size_t remain = strlen(at + used);
            if (n > remain) n = 1;
            if (used + n >= sizeof(line)) break;
            memcpy(line + used, at + used, n);
            line[used + n] = 0;
            if (ttf_text_width_px(UI_PX_CAPTION, line) > ui_content_width() && used) break;
            used += n;
        }
        if (!used) break;
        line[used] = 0;
        if (draw) ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
        y += 36;
        at += used;
    }
    return y;
}
static int draw_wrapped_name(uint8_t* fb, const char* name, int y) {
    return layout_name(fb, name, y, true);
}
static void draw_manage(uint8_t* fb) {
    EpdRect panel = manage_panel();
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text(fb, UI_MARGIN, panel.y + 18, UI_PX_BODY,
            s_clear_confirm ? (s_delete_confirm ? "确认删除文件？" : "确认清除进度？") : "图书详情", EPD_DRAW_ALIGN_LEFT, false);
    int y = draw_wrapped_name(fb, s_managed.name, panel.y + 74) + 12;
    char info[96];
    snprintf(info, sizeof(info), "%s · %s · %.2f MB", s_managed.is_flash ? "内置存储" : "TF 卡",
             strrchr(s_managed.name, '.') ? strrchr(s_managed.name, '.') + 1 : "", s_managed.size / 1048576.0);
    ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(info, sizeof(info), s_managed.has_progress ? "阅读进度 %u%%" : "尚无阅读进度", s_managed.pct);
    ui_text(fb, UI_MARGIN, y + 44, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, y + 88, UI_PX_CAPTION,
            s_managed.is_flash ? "/flash/books" : !strncmp(s_managed.path, "/sdcard/book/", 13) ? "/sdcard/book" : app_settings_books_dir(),
            EPD_DRAW_ALIGN_LEFT, false);
    if (s_manage_message[0]) ui_text(fb, UI_MARGIN, panel.y + panel.height - 270, UI_PX_CAPTION, s_manage_message, EPD_DRAW_ALIGN_LEFT, false);
    if (s_clear_confirm) {
        ui_text(fb, UI_MARGIN, panel.y + panel.height - 142, UI_PX_CAPTION, s_delete_confirm ? "删除后文件无法恢复" : "仅清阅读进度，保留图书文件", EPD_DRAW_ALIGN_LEFT, false);
        draw_control(fb, manage_rect(0, 2), "取消", 300);
        draw_control(fb, manage_rect(1, 2), s_delete_confirm ? "确认删除" : "确认清除", 301);
    } else if (s_file_removed) {
        draw_control(fb, manage_rect(0, 2), "关闭", 400);
        draw_control(fb, manage_rect(1, 2), "重试清理", 403);
    } else {
        draw_control(fb, (EpdRect){UI_MARGIN, panel.y + panel.height - 190, ui_content_width(), 68}, "前往文件管理重命名", 404);
        draw_control(fb, manage_rect(0, 3), "关闭", 400);
        draw_control(fb, manage_rect(1, 3), "清进度", 401);
        draw_control(fb, manage_rect(2, 3), "删除文件", 402);
    }
}

static void editor_start(void) {
    free(s_editor_cover);
    bool pending = false;
    s_editor_cover = load_cover_gray(s_managed.path, s_managed.name, "", false, &pending);
    copy_text(s_editor_title, sizeof(s_editor_title), s_managed.name);
    ui_text_edit_init(&s_editor_input, s_editor_title, sizeof(s_editor_title));
    s_editor_notice[0] = 0;
    ui_keyboard_begin(&s_editor_input, false);
    s_view = EDIT;
}



static void draw_editor(uint8_t* fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, UI_LOCK_WIDTH / 2, 107, 31, "编辑书籍", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 646, 107, 26, "完成", EPD_DRAW_ALIGN_RIGHT, false);
    ui_hairline(fb, 133, 36, 612, UI_GRAY_LIGHT);
    EpdRect cover = {36, 175, 112, 152};
    ui_fill_rect_fast(cover, UI_GRAY_LIGHT, fb);
    if (s_editor_cover) {

        for (int y = 0; y < cover.height; ++y) {
            for (int x = 0; x < cover.width; ++x) {
                const uint8_t tone = ui_contrast_gray(
                    s_editor_cover[(y * BOOK_COVER_H / cover.height) * BOOK_COVER_W +
                                   x * BOOK_COVER_W / cover.width]);
                epd_draw_pixel(cover.x + x, cover.y + y,
                    ui_image_dither_gray(tone, cover.x + x, cover.y + y), fb);
            }
        }
    } else {
        char title[80]; copy_text(title, sizeof(title), s_managed.name);
        fit_text(title, 22, cover.width - 16);
        ui_text_vc(fb, cover.x + cover.width / 2, cover.y + cover.height / 2, 22, title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, cover, 0, UI_GRAY_BLACK);
    char title[128]; copy_text(title, sizeof(title), s_managed.name);
    fit_text(title, 30, 460);
    ui_text(fb, 174, 192, 30, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 174, 244, 22, "只修改书架显示名称", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 356, 36, 612, UI_GRAY_LIGHT);
    ui_text(fb, 36, 378, 24, "书名", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect field = {36, 424, 612, 83};
    ui_text_input_draw(fb, &s_editor_input, field, 31, false, NULL);
    if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 530, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
    ui_keyboard_draw(fb, 560);
}
static void draw_search(uint8_t* fb) {
    ui_clear_page(fb); ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "搜索图书", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 646, 107, 25, "应用", EPD_DRAW_ALIGN_RIGHT, false);
    ui_text_input_draw(fb, &s_search_input, (EpdRect){UI_MARGIN, 200, ui_content_width(), 88}, UI_PX_BODY, false, NULL);
    ui_text_fixed_vc(fb, 36, 337, 23, "可输入中文、完整拼音、首字母或英文", EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_button(fb, (EpdRect){36, 392, 180, 66}, "清空", false);
    ui_keyboard_draw(fb, 560);
}
static void draw_import(uint8_t* fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "导入图书", EPD_DRAW_ALIGN_CENTER, false);
    ui_text(fb, 36, 166, 20, "选择导入方式", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 207, 36, 612, UI_GRAY_LIGHT);
    static const char *titles[] = {"浏览 TF 卡", "WiFi 传书", "热点传书", "USB 读卡"};
    static const char *details[] = {
        "查看目录，打开或管理已有图书", "手机与墨水屏连接同一网络",
        "连接 kiikoread 热点后上传图书", "连接电脑，把图书放入 books 文件夹"
    };
    for (int i = 0; i < 4; ++i) {
        EpdRect r = import_rect(i);
        ui_fill_round_rect(fb, r, 22, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 22, 0xb0);
        ui_text(fb, r.x + 24, r.y + 25, 27, titles[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, r.x + 24, r.y + 78, 19, details[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, r.x + r.width - 24, r.y + r.height / 2, 26, "›", EPD_DRAW_ALIGN_RIGHT, false);
    }
    ui_text(fb, 36, 925, 19, "图书放入 TF 卡的 books 文件夹后会自动出现在书架。",
            EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 1);
}
static void draw_batch_confirmation(uint8_t* fb) {
    if (!s_batch_confirm) return;
    EpdRect panel = {54, 406, 576, 330};
    ui_fill_round_rect(fb, panel, 25, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, panel, 25, 0x48);
    char title[96];
    const char* verb = s_batch_kind == BATCH_DELETE ? "删除" :
                       s_batch_kind == BATCH_CLEAR ? "清除进度：" : "从书架移除";
    const char* note = s_batch_kind == BATCH_DELETE ? "删除文件不可撤销，失败项可重试" :
                       s_batch_kind == BATCH_CLEAR ? "仅清阅读进度，所有文件保留" :
                       "只移出书架，文件与阅读进度都保留";
    snprintf(title, sizeof(title), "%s %u 本图书？", verb, (unsigned)selected_count());
    ui_text_vc(fb, 342, 477, 28, title, EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 342, 541, 19, note, EPD_DRAW_ALIGN_CENTER, false);
    draw_control(fb, ui_row_rect(0, 2, 620, UI_BTN_H), "取消", 600);
    draw_control(fb, ui_row_rect(1, 2, 620, UI_BTN_H), "确认", 601);
}

static EpdRect bulk_nav_rect(int i) { return (EpdRect){36 + i * 208, UI_BAR_TOP, 196, UI_BAR_H}; }
static int bulk_nav_hit(int x, int y) {
    for (int i=0;i<3;++i) if(ui_rect_hit(bulk_nav_rect(i),x,y)) return i;
    return -1;
}
static void draw_bulk(uint8_t* fb, int leaf) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "管理书架", EPD_DRAW_ALIGN_CENTER, false);
    char summary[96];
    snprintf(summary, sizeof(summary), "%d 本图书 · 已选 %u 本", s_visible_count, (unsigned)selected_count());
    ui_text(fb, 36, 166, 20, s_batch_message[0] ? s_batch_message : summary, EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 200, 36, 612, 0x70);
    const char *filters[] = {s_filter == 0 ? "全部来源" : s_filter == 1 ? "TF 卡" : "内置",
                             s_recent_sort ? "按最近" : "按名称", s_query[0] ? "搜索中" : "搜索"};
    for (int i = 0; i < 3; ++i) {
        EpdRect r = bulk_filter_rect(i);
        ui_fill_round_rect(fb, r, 20, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 20, 0x70);
        ui_text_vc(fb, r.x + r.width / 2, r.y + 35, 21, filters[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int row = 0; row < BOOK_BULK_ROWS; ++row) {
        int index = leaf * BOOK_BULK_ROWS + row;
        if (index >= s_visible_count) break;
        EpdRect r = row_rect(row);
        bool selected = s_shelf[index].selected;
        ui_fill_round_rect(fb, r, 18, selected ? 0xd0 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 18, selected ? 0x60 : 0x70);
        char title[128]; copy_text(title, sizeof(title), s_shelf[index].name);
        fit_text(title, 26, 495);
        ui_text_vc(fb, r.x + 22, r.y + 31, 26, title, EPD_DRAW_ALIGN_LEFT, false);
        char detail[48];
        snprintf(detail, sizeof(detail), "%s · %s", s_shelf[index].is_flash ? "内置" : "TF 卡",
                 s_shelf[index].has_progress ? "已有阅读进度" : "未开始阅读");
        ui_text_vc(fb, r.x + 22, r.y + 62, 20, detail, EPD_DRAW_ALIGN_LEFT, false);
        epd_draw_circle(r.x + r.width - 37, r.y + 42, 12, 0x58, fb);
        if (selected) epd_fill_circle(r.x + r.width - 37, r.y + 42, 7, 0x38, fb);
    }
    if (!s_visible_count)
        ui_text_vc(fb, 342, 570, 26, s_message[0] ? s_message : "当前筛选没有图书", EPD_DRAW_ALIGN_CENTER, false);
    // 三列格宽只有 193 像素，标签控制在四字以内，放大系统字号也不会顶出按钮。
    // A three-column cell is 193 px wide, so labels stay within four characters and still fit
    // once the system font scale is turned up.
    static const char *labels[] = {"本页全选", "清除勾选", "重新扫描",
                                   "删除所选", "清进度", "移出书架"};
    for (int i = 0; i < 6; ++i) {
        EpdRect r = batch_rect(i);
        ui_fill_round_rect(fb, r, 18, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 18, 0x70);
        ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2, 20, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    const char *nav[] = {"上一页", "完成", "下一页"};
    char page[48];
    snprintf(page, sizeof(page), "%d / %d 页", leaf + 1, leaves());
    ui_text_vc(fb, 342, 1072, 20, page, EPD_DRAW_ALIGN_CENTER, false);
    for (int i = 0; i < 3; ++i) {
        EpdRect r = bulk_nav_rect(i);
        bool available = i == 1 || (i == 0 ? leaf > 0 : leaf + 1 < leaves());
        ui_fill_round_rect(fb, r, 20, i == 1 ? 0x20 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, r, 20, available ? 0x70 : 0xb0);
        ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2, 25, nav[i], EPD_DRAW_ALIGN_CENTER, i == 1);
    }
    draw_batch_confirmation(fb);
}
static void save_progress(void) {
    if (!s_text || !s_path[0] || !book_layout_page_count()) return;
    bool was_failed = s_save_failed;
    pending_progress_t* pending = pending_find(s_path);
    if (!pending) { s_save_failed = true; return; }
    book_progress_t p = {
        .file_size = s_file_size, .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)reader_page_offset(s_page),
        .px = (uint8_t)s_px, .pct = (uint8_t)percent(s_page),
        .last_open_s = 0,
    };
    if (!pending->dirty || pending->value.file_size != p.file_size || pending->value.chapter != p.chapter ||
        pending->value.byte_off != p.byte_off || pending->value.px != p.px || pending->value.pct != p.pct) {
        pending->value = p;
        pending->progress_saved = false;
    }
    pending->dirty = true;
    if (pending_flush(pending)) {
        s_unsaved = 0;
        s_save_failed = false;
        for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
    }
    else { if (!s_unsaved) s_unsaved = 1; s_save_failed = true; }
    if (was_failed != s_save_failed) invalidate_prep();
}
static void invalidate_prep(void) { s_next_page = s_prep_page = -1; s_next_generation=0; memset(&s_prep_cursor,0,sizeof(s_prep_cursor)); }

// 目录使用系统字体；切换前等待预渲染结束，回正文时恢复阅读字体与字形缓存。
// The directory uses the system face; join painting before switching and restore the reader face on return.
static void set_reader_view(book_view_t view) {
    s_reader_tap.pending = false;
    lock_draw();
    invalidate_prep();
    if (view == READING) app_font_activate_reading();
    else app_font_activate_system();
    s_view = view;
    unlock_draw();
}

/* ---- 阅读工具层 / Reader overlays ---- */
typedef struct {
    uint32_t magic, file_size, byte_off;
    uint16_t chapter;
    char path[BOOK_STORE_PATH_MAX];
} legacy_reader_bookmark_t;

typedef struct {
    uint16_t chapter;
    uint16_t reserved;
    uint32_t byte_off;
    uint32_t saved_s;
} reader_bookmark_entry_t;

typedef struct {
    uint32_t magic, file_size;
    uint16_t count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[BOOKMARK_MAX];
} reader_bookmarks_t;

static void bookmark_key(const char* path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "m_%08lx", (unsigned long)hash);
}
// 收藏按完整路径持久化，与章节书签分别存放。/ Persist whole-book favorites separately from page bookmarks.
static void favorite_key(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "f_%08lx", (unsigned long)hash);
}
static bool favorite_read_handle(nvs_handle_t h, const char *path) {
    uint8_t value = 0;
    char key[11]; favorite_key(path, key);
    return nvs_get_u8(h, key, &value) == ESP_OK && value == 1;
}
static bool favorite_load(const char *path) {
    nvs_handle_t h;
    if (nvs_open("rp_favs", NVS_READONLY, &h) != ESP_OK) return false;
    bool value = favorite_read_handle(h, path);
    nvs_close(h);
    return value;
}
static bool favorite_save(const char *path, bool value) {
    nvs_handle_t h;
    if (nvs_open("rp_favs", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; favorite_key(path, key);
    esp_err_t err = value ? nvs_set_u8(h, key, 1) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !value) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// 从书架移除只隐藏条目，不动文件也不清进度；再次打开这本书会重新上书架。
// Removing from the shelf hides the entry only: no file or progress is touched, and opening the
// book again puts it back. 值为完整路径，命中的同键记录属于别的书时拒绝覆盖。
// The value is the full path, so a colliding key owned by a different book is never overwritten.
#define SHELF_HIDDEN_NS "rp_shelf"
static void shelf_hidden_key(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "h_%08lx", (unsigned long)hash);
}
static bool shelf_hidden_read_handle(nvs_handle_t h, const char *path) {
    char stored[BOOK_STORE_PATH_MAX];
    char key[11];
    shelf_hidden_key(path, key);
    size_t len = sizeof(stored);
    return nvs_get_str(h, key, stored, &len) == ESP_OK && strcmp(stored, path) == 0;
}
static bool shelf_hidden_load(const char *path) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool hidden = shelf_hidden_read_handle(h, path);
    nvs_close(h);
    return hidden;
}
static bool shelf_hidden_save(const char *path, bool hidden) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11];
    shelf_hidden_key(path, key);
    char stored[BOOK_STORE_PATH_MAX];
    size_t len = sizeof(stored);
    esp_err_t existing = nvs_get_str(h, key, stored, &len);
    esp_err_t err = existing == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : existing;
    if (existing == ESP_OK && strcmp(stored, path) != 0) err = ESP_ERR_INVALID_STATE;
    // 恢复上架也校验路径，避免擦掉碰撞键属于另一书的隐藏记录。
    // Check ownership when unhiding too, so a collision cannot erase a different book removal.
    if (err == ESP_OK && hidden) err = nvs_set_str(h, key, path);
    else if (err == ESP_OK) {
        err = nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK)book_lock_collage_invalidate();
    return err == ESP_OK;
}
static bool bookmark_read_handle(nvs_handle_t h, reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    memset(marks, 0, sizeof(*marks));
    char key[11]; bookmark_key(s_path, key);
    size_t size = 0;
    if (nvs_get_blob(h, key, NULL, &size) != ESP_OK) return false;
    if (size == sizeof(legacy_reader_bookmark_t)) {
        legacy_reader_bookmark_t old;
        size = sizeof(old);
        if (nvs_get_blob(h, key, &old, &size) != ESP_OK ||
            old.magic != UINT32_C(0x52504d31) || old.file_size != s_file_size ||
            strcmp(old.path, s_path)) return false;
        marks->magic = UINT32_C(0x52504d32);
        marks->file_size = old.file_size;
        marks->count = 1;
        copy_text(marks->path, sizeof(marks->path), old.path);
        marks->entries[0] = (reader_bookmark_entry_t){
            .chapter = old.chapter, .byte_off = old.byte_off,
        };
        return true;
    }
    if (size != sizeof(*marks) || nvs_get_blob(h, key, marks, &size) != ESP_OK) return false;
    return marks->magic == UINT32_C(0x52504d32) && marks->file_size == s_file_size &&
        marks->count <= BOOKMARK_MAX && !strcmp(marks->path, s_path);
}

static bool bookmark_load(reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = bookmark_read_handle(h, marks);
    nvs_close(h);
    return ok;
}
static bool bookmark_is_current(void) {
    reader_bookmarks_t marks;
    if (!bookmark_load(&marks)) return false;
    size_t offset = book_layout_page_start_offset(s_page);
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) return true;
    return false;
}
static size_t bookmark_count(void) {
    reader_bookmarks_t marks;
    return bookmark_load(&marks) ? marks.count : 0;
}
static int bookmark_row_step(void) {
    int scale=app_settings_system_font_size();
    if(scale<100)scale=100;
    if(scale>200)scale=200;
    return 82+(scale-100)/2;
}
static int bookmark_rows(void) { return (s_bookmark_edit?424:492)/bookmark_row_step(); }
static EpdRect bookmark_row_rect(int row) {
    int step=bookmark_row_step();
    return (EpdRect){36,656+row*step,612,step-10};
}
static int bookmark_pages(size_t count) {
    return count ? ((int)count + bookmark_rows() - 1) / bookmark_rows() : 1;
}
static unsigned bookmark_selected_count(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < BOOKMARK_MAX; ++i) if (s_bookmark_selected & (UINT32_C(1) << i)) ++count;
    return count;
}
static void bookmark_compact(reader_bookmarks_t* marks, uint32_t selected) {
    size_t write = 0;
    for (size_t i = 0; i < marks->count; ++i)
        if (!(selected & (UINT32_C(1) << i))) marks->entries[write++] = marks->entries[i];
    marks->count = (uint16_t)write;
}
static bool bookmark_delete_selected(void) {
    if (!s_bookmark_selected || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    esp_err_t err = valid ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (valid) {
        bookmark_compact(&marks, s_bookmark_selected);
        char key[11]; bookmark_key(s_path, key);
        err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) return false;
    s_bookmark_selected = 0;
    s_bookmark_page = 0;
    s_bookmark_edit = marks.count != 0;
    return true;
}
static bool bookmark_toggle(void) {
    if (!s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; bookmark_key(s_path, key);
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    if (!valid) {
        memset(&marks, 0, sizeof(marks));
        marks.magic = UINT32_C(0x52504d32);
        marks.file_size = s_file_size;
        copy_text(marks.path, sizeof(marks.path), s_path);
    }
    uint32_t offset = (uint32_t)book_layout_page_start_offset(s_page);
    size_t found = marks.count;
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) { found = i; break; }
    bool remove = found < marks.count;
    if (remove) {
        memmove(&marks.entries[found], &marks.entries[found + 1],
                (marks.count - found - 1) * sizeof(marks.entries[0]));
        --marks.count;
    } else {
        if (marks.count == BOOKMARK_MAX) {
            memmove(&marks.entries[0], &marks.entries[1],
                    (BOOKMARK_MAX - 1) * sizeof(marks.entries[0]));
            --marks.count;
        }
        time_t now = time(NULL);
        marks.entries[marks.count++] = (reader_bookmark_entry_t){
            .chapter = (uint16_t)s_chapter, .byte_off = offset,
            .saved_s = now >= 1704067200 ? (uint32_t)now : 0,
        };
    }
    esp_err_t err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !marks.count) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        copy_text(s_reader_notice, sizeof(s_reader_notice), remove ? "书签已移除" : "书签已添加");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1600;
    } else {
        copy_text(s_reader_notice, sizeof(s_reader_notice), "书签保存失败");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
    }
    return err == ESP_OK;
}

static void draw_reader_tool_icon(uint8_t* fb, int cx, int cy, int kind, bool selected) {
    // 已保存的书签用带勾的一档区分未保存；其余图标取自 k_tool_icons。
    // A saved bookmark uses the check variant; every other icon comes from k_tool_icons.
    if (kind < 0 || kind >= BOOK_TOOL_COUNT) return;
    const ui_icon_t icon = (kind == 1 && selected) ? UI_ICON_BOOKMARK_CHECK : k_tool_icons[kind];
    ui_draw_icon(fb, cx, cy, READER_TOOL_ICON_PX, icon, 0x38);
}

// 单行辅助文字保持在当前控件内；本机只使用当前活动字库，按原轮廓重新光栅化。
// Keep one-line UI text inside its slot, rasterizing native outlines in the existing active font.
static void reader_panel_text_vc(uint8_t *fb,int x,int cy,int px,const char *text,
                                  enum EpdFontFlags align,bool inverted) {
    int width=align==EPD_DRAW_ALIGN_LEFT?642-x:align==EPD_DRAW_ALIGN_RIGHT?x-42:
              2*(x<342?x-42:642-x);
    int left=align==EPD_DRAW_ALIGN_LEFT?x:align==EPD_DRAW_ALIGN_RIGHT?x-width:x-width/2;
    ui_text_box(fb,(EpdRect){left,cy-22,width,44},px,text,align,inverted);
}
static void reader_panel_text(uint8_t *fb,int x,int y,int px,const char *text,
                               enum EpdFontFlags align,bool inverted) {
    int height=ui_text_effective_px(px);if(height>44)height=44;
    reader_panel_text_vc(fb,x,y+height/2,px,text,align,inverted);
}

static void draw_sheet(uint8_t* fb, int top, const char* title) {
    EpdRect sheet = {0, top, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - top + 28};
    ui_fill_round_rect(fb, sheet, 28, UI_GRAY_WHITE);
    ui_fill_rect_fast((EpdRect){0, top + 28, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - top - 28}, UI_GRAY_WHITE, fb);
    ui_draw_control_frame(fb, sheet, 28, 0x58);
    ui_fill_round_rect(fb, (EpdRect){292, top + 14, 100, 7}, 3, 0x48);
    ui_text_box(fb,(EpdRect){110,top+26,464,66},30,title,EPD_DRAW_ALIGN_CENTER,false);
}

// 二级面板左上角的返回键：圆底加 Lucide chevron-left，各面板共用。
// Secondary-panel back control: a circle with the Lucide chevron-left, shared by the panels.
static void draw_sheet_back(uint8_t* fb, int top) {
    ui_click_feedback_register(fb, (EpdRect){32, top + 25, 64, 64}, UI_CLICK_BACK, UI_ICON_CHEVRON_LEFT);
    epd_draw_circle(64, top + 57, 24, UI_NAV_BACK_BORDER_GRAY, fb);
    epd_draw_circle(64, top + 57, 23, UI_NAV_BACK_BORDER_GRAY, fb);
    ui_draw_icon(fb, 64, top + 57, READER_SHEET_BACK_PX, UI_ICON_CHEVRON_LEFT, 0x38);
}

static void draw_pill_slider(uint8_t* fb, EpdRect r, const char* left, const char* right,
                             const char* value, int index, int count, int left_px, int right_px) {
    ui_fill_round_rect(fb, r, r.height / 2, 0xb8);
    ui_draw_control_frame(fb, r, r.height / 2, 0x50);
    int cy = r.y + r.height / 2;
    ui_text_box(fb,(EpdRect){r.x+12,cy-26,40,52},left_px,left,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){r.x+r.width-52,cy-26,40,52},right_px,right,EPD_DRAW_ALIGN_CENTER,false);
    int span = r.width - 128;
    int cx = r.x + 64 + (count > 1 ? span * index / (count - 1) : 0);
    EpdRect track = {r.x + 59, cy - 3, r.width - 118, 6};
    ui_fill_round_rect(fb, track, 3, 0x78);
    track.width = cx - track.x;
    if (track.width > 0) ui_fill_round_rect(fb, track, 3, 0x40);
    epd_fill_circle(cx, cy, 37, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, cy, 37, 0x48, fb);
    ui_text_box(fb,(EpdRect){cx-31,cy-28,62,56},22,value,EPD_DRAW_ALIGN_CENTER,false);
}

static EpdRect reader_slider_refresh_rect(int slider) {
    EpdRect r = reader_slider_rect(slider);
    // The knob extends four pixels outside the pill. Include that margin so
    // moving it restores the old white footprint and the rail beneath it.
    r.x -= 8; r.y -= 8; r.width += 16; r.height += 16;
    return r;
}

static const char* font_friendly_name(const ttf_font_item_t* item) {
    return item ? ttf_font_localized_name(item->name) : "";
}

static void draw_font_name_with_current_face(uint8_t* fb, int x, int y, const char* name, int width) {
    int above = 0, below = 0;
    int px=ui_text_effective_px(25);
    while (px>1 && ttf_text_width_px(px,name)>width) --px;
    ttf_measure_line_px(px, name, &above, &below);
    ttf_draw_text_px(fb, x, y + (above - below) / 2, px, name,
                     EPD_DRAW_ALIGN_CENTER, UI_INK_BLACK, UI_INK_WHITE);
}

// 用户确认双列：字体/字重在第一行，书内字体/排版在第二行；绘制与命中共用矩形。
// Approved paired rows: face/weight, then embedded faces/layout; paint and touch share rectangles.
// 放大文字时向上展开面板，保持双列与底部完成键的位置；命中也使用这些矩形。
// Expand sheets upward for larger UI text, retaining paired columns and the Done button; hits share these rectangles.
static int reader_ui_extra(int maximum) {
    int scale = app_settings_system_font_size();
    if (scale < 100) scale = 100;
    if (scale > 200) scale = 200;
    return (scale-100)*maximum/100;
}
static int reader_font_top(void) { return 580-reader_ui_extra(112); }
static EpdRect reader_font_card(int index) {
    int extra=reader_ui_extra(48), footer=reader_ui_extra(16);
    if (index>=4) return (EpdRect){index==4?36:350,1054-footer,298,72+footer};
    int row=index/2;
    return (EpdRect){index%2?350:36,796+row*132-(2-row)*extra-footer,
                     298,(row?110:116)+extra};
}
#define s_font_card reader_font_card(0)
#define s_weight_card reader_font_card(1)
#define s_bookfont_card reader_font_card(2)
#define s_layout_card reader_font_card(3)
#define s_rule_card reader_font_card(4)
#define s_antialias_card reader_font_card(5)
static void draw_reading_switch(uint8_t *fb, EpdRect track, bool on);
static const EpdRect s_rule_offset_up = {36, 980, 190, 88};
static const EpdRect s_rule_offset_reset = {246, 980, 192, 88};
static const EpdRect s_rule_offset_down = {458, 980, 190, 88};

static int background_compare(const void *a, const void *b) {
    return strcasecmp(((const reader_background_item_t *)a)->path, ((const reader_background_item_t *)b)->path);
}
// 字体页入口已按用户要求移除；保留内部旧面板处理，便于安全结束正在运行的预览。
// The requested font-sheet entry is removed; retain the internal legacy panel to end an active preview safely.
static void reader_background_scan(void) __attribute__((unused));
static void reader_background_scan(void) {
    free(s_backgrounds); s_backgrounds = NULL; s_background_count = s_background_selected = s_background_page = 0;
    s_background_notice[0] = 0; s_background_ready = false;
    s_background_preview_notice[0] = 0; s_background_prepare_pending = false;
    DIR *dir = opendir("/sdcard/pictures");
    if (!dir) { copy_text(s_background_notice, sizeof(s_background_notice), "请在 pictures 中放入 JPG 或 PNG 图片"); return; }
    // A bounded catalogue, independent of book count. No image data in the catalogue.
    s_backgrounds = heap_caps_calloc(128, sizeof(*s_backgrounds), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_backgrounds) copy_text(s_background_notice, sizeof(s_background_notice), "内存不足，请关闭图书后重试");
    struct dirent *ent;
    while (s_backgrounds && (ent = readdir(dir))) {
        // AppleDouble ._ files and other hidden metadata are not images.
        if (ent->d_name[0] == '.') continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
        char path[288]; struct stat st;
        int n = snprintf(path, sizeof(path), "/sdcard/pictures/%s", ent->d_name);
        if (n < 0 || n >= (int)sizeof(path) || stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;
        if (s_background_count == 128) { copy_text(s_background_notice, sizeof(s_background_notice), "仅显示前 128 张图片"); break; }
        strcpy(s_backgrounds[s_background_count++].path, path);
    }
    closedir(dir);
    if (s_background_count) qsort(s_backgrounds, s_background_count, sizeof(*s_backgrounds), background_compare);
    for (int i = 0; i < s_background_count; ++i)
        if (!strcmp(s_backgrounds[i].path, app_settings_book_background_path())) s_background_selected = i;
    s_background_page = s_background_selected / READER_BACKGROUND_ROWS;
    if (!s_background_count && !s_background_notice[0])
        copy_text(s_background_notice, sizeof(s_background_notice), "pictures 中暂无 JPG 或 PNG 图片");
}
static int reader_background_pages(void) {
    return s_background_count ? (s_background_count + READER_BACKGROUND_ROWS - 1) / READER_BACKGROUND_ROWS : 1;
}
static EpdRect reader_background_row(int row) { return (EpdRect){36, 344 + row * 96, 612, 96}; }
static void draw_background_directory(uint8_t *fb) {
    draw_sheet(fb, 200, "阅读背景"); draw_sheet_back(fb, 200);
    reader_panel_text(fb, 42, 303, 23, "pictures", EPD_DRAW_ALIGN_LEFT, false);
    char count[32]; snprintf(count, sizeof(count), "%d 张图片", s_background_count);
    reader_panel_text(fb, 642, 303, 22, count, EPD_DRAW_ALIGN_RIGHT, false);
    ui_fill_round_rect(fb, (EpdRect){36,344,612,672}, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, (EpdRect){36,344,612,672}, 20, 0x58);
    for (int row = 0; row < READER_BACKGROUND_ROWS; ++row) {
        int index = s_background_page * READER_BACKGROUND_ROWS + row;
        if (index >= s_background_count) break;
        EpdRect r = reader_background_row(row);
        bool active = !strcmp(s_backgrounds[index].path, app_settings_book_background_path());
        if (active) ui_fill_round_rect(fb, (EpdRect){r.x+6,r.y+6,r.width-12,r.height-12}, 16, 0xe0);
        if (row) ui_hairline(fb, r.y, r.x+20, r.width-40, 0x78);
        ui_draw_icon(fb, 78, r.y+48, 30, UI_ICON_FILE, 0x38);
        const char *base = strrchr(s_backgrounds[index].path, '/');
        char name[128]; copy_text(name, sizeof(name), base ? base+1 : s_backgrounds[index].path);
        fit_text(name, 25, 470);
        reader_panel_text_vc(fb, 112, r.y+48, 25, name, EPD_DRAW_ALIGN_LEFT, false);
        if (active) epd_fill_circle(595, r.y+48, 4, UI_GRAY_BLACK, fb);
        ui_draw_icon(fb, 623, r.y+48, 23, UI_ICON_CHEVRON_RIGHT, 0x38);
    }
    if (s_background_notice[0]) {
        char notice[96]; copy_text(notice, sizeof(notice), s_background_notice);
        fit_text(notice, 21, 572);
        reader_panel_text_vc(fb, 342, s_background_count ? 1098 : 655, 21, notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    char page[32]; snprintf(page, sizeof(page), "%d / %d", s_background_page+1, reader_background_pages());
    ui_draw_button(fb, (EpdRect){36,1040,190,52}, "上一页", false);
    reader_panel_text_vc(fb, 342, 1066, 22, page, EPD_DRAW_ALIGN_CENTER, false);
    ui_draw_button(fb, (EpdRect){458,1040,190,52}, "下一页", false);
    ui_draw_button(fb, (EpdRect){36,1120,294,72}, "无背景", false);
    ui_draw_button(fb, (EpdRect){354,1120,294,72}, "返回", false);
}
static void draw_background_picker(uint8_t *fb) {
    draw_sheet(fb, 200, "背景预览"); draw_sheet_back(fb, 200);
    EpdRect preview = {157, 316, 369, 656}; // 9:16, matching the complete wallpaper canvas.
    ui_fill_rect_fast(preview, UI_GRAY_WHITE, fb);
    if (s_background_ready) ui_reader_background_paint(fb, preview, false);
    ui_draw_control_frame(fb, preview, 0, UI_GRAY_BLACK);
    char name[128] = "暂无图片", position[48];
    if (s_background_count) {
        const char *base = strrchr(s_backgrounds[s_background_selected].path, '/');
        copy_text(name, sizeof(name), base ? base + 1 : "背景");
    }
    fit_text(name, 24, 584);
    reader_panel_text_vc(fb, 342, 1002, 24, name, EPD_DRAW_ALIGN_CENTER, false);
    snprintf(position, sizeof(position), "%d/%d · 上下滑动预览", s_background_count ? s_background_selected + 1 : 0, s_background_count);
    reader_panel_text_vc(fb, 342, 1046, 21, s_background_preview_notice[0] ? s_background_preview_notice : position, EPD_DRAW_ALIGN_CENTER, false);
    ui_draw_button(fb, (EpdRect){36, 1100, 190, 76}, "无背景", false);
    ui_draw_button(fb, (EpdRect){246, 1100, 192, 76}, "返回", false);
    ui_draw_button(fb, (EpdRect){458, 1100, 190, 76}, "使用背景", true);
}
static void reader_background_preview(int index) {
    if (index < 0 || index >= s_background_count) return;
    s_background_selected = index; s_background_page = index / READER_BACKGROUND_ROWS;
    s_reader_panel = READER_PANEL_BACKGROUND_PREVIEW;
    s_background_ready = false; s_background_prepare_pending = true;
    s_background_preview_notice[0] = 0;
}
static app_redraw_t reader_background_step(int dir) {
    if (s_reader_panel == READER_PANEL_BACKGROUND) {
        int next = s_background_page + dir;
        if (next < 0 || next >= reader_background_pages()) return APP_REDRAW_NONE;
        s_background_page = next; return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW)
        reader_background_preview(s_background_selected + dir);
    return APP_REDRAW_NONE; // Preview decoding is deferred to the shallow tick stack.
}
static app_redraw_t reader_background_action(uint16_t x, uint16_t y) {
    const EpdRect back = {32,225,64,64};
    if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) {
        bool close = y < 200 || ui_rect_hit(back, x, y);
        if (y >= 1100 && y < 1176) {
            if (x >= 36 && x < 226) {
                if (!app_settings_set_book_background_path("")) return APP_REDRAW_NONE;
                close = true;
            } else if (x >= 246 && x < 438) close = true;
            else if (x >= 458 && x < 648) {
                if (!s_background_count || !s_background_ready ||
                    !app_settings_set_book_background_path(s_backgrounds[s_background_selected].path)) return APP_REDRAW_NONE;
                close = true;
            }
        }
        if (!close) return APP_REDRAW_NONE;
        s_background_prepare_pending = false; s_background_ready = false;
        s_reader_panel = READER_PANEL_BACKGROUND;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (y < 200 || ui_rect_hit(back, x, y) || ui_rect_hit((EpdRect){354,1120,294,72}, x, y)) {
        s_reader_panel = READER_PANEL_FONT_SETTINGS;
        s_background_prepare_pending = false;
        free(s_backgrounds); s_backgrounds = NULL; s_background_count = s_background_page = 0;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){36,1120,294,72}, x, y)) {
        if (!app_settings_set_book_background_path("")) return APP_REDRAW_NONE;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){36,1040,190,52}, x, y)) return reader_background_step(-1);
    if (ui_rect_hit((EpdRect){458,1040,190,52}, x, y)) return reader_background_step(1);
    for (int row = 0; row < READER_BACKGROUND_ROWS; ++row) {
        int index = s_background_page * READER_BACKGROUND_ROWS + row;
        if (index < s_background_count && ui_rect_hit(reader_background_row(row), x, y)) {
            reader_background_preview(index); invalidate_prep(); return APP_REDRAW_NONE;
        }
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t reader_background_tick(void) {
    if (s_view != READING || s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW || !s_background_prepare_pending)
        return APP_REDRAW_NONE;
    s_background_prepare_pending = false;
    s_background_ready = s_background_count && s_background_selected >= 0 &&
        s_background_selected < s_background_count && ui_reader_background_prepare(s_backgrounds[s_background_selected].path);
    if (!s_background_ready) copy_text(s_background_preview_notice, sizeof(s_background_preview_notice), "图片无法解码或内存不足，请换一张");
    invalidate_prep(); return APP_REDRAW_PAGE;
}
static void draw_font_pair(uint8_t *fb, EpdRect card, const char *title, const char *value) {
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 20, 0x70);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[64]; copy_text(shown, sizeof(shown), value);
    if (!strcmp(title,"阅读字体")) fit_fixed_text(shown,ui_text_effective_px(27),card.width-76);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,card.width-76,card.height/2-8},27,shown,EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,card.x+card.width-23,card.y+3*card.height/4,26,UI_ICON_CHEVRON_RIGHT,0x38);
}
static void reader_weight_value(char *out,size_t size,int steps){
    if(!steps){snprintf(out,size,"标准");return;}
    unsigned amount=steps<0?-steps:steps,whole=amount/16,fraction=(amount%16)*625;
    snprintf(out,size,"%c%u.%04u",steps<0?'-':'+',whole,fraction);
    size_t n=strlen(out);while(n&&out[n-1]=='0')out[--n]=0;if(n&&out[n-1]=='.')out[--n]=0;
}
static void draw_font_settings(uint8_t* fb) {
    draw_sheet(fb, reader_font_top(), "字体设置");
    int shown_px = s_reader_slider >= 0 ? s_reader_preview_px : s_px;
    char value[16]; snprintf(value, sizeof(value), "%d", shown_px);
    draw_pill_slider(fb, reader_slider_rect(0), "A", "A", value,
                     shown_px - BOOK_PX_MIN, BOOK_PX_MAX - BOOK_PX_MIN + 1, 20, 31);
    draw_font_pair(fb, s_font_card, "阅读字体", ttf_font_display_name());
    char weight_value[20];reader_weight_value(weight_value,sizeof(weight_value),app_settings_book_weight_fine());
    draw_font_pair(fb, s_weight_card, "字重", weight_value);
    draw_font_pair(fb, s_layout_card, "排版设置", "边距 · 行距");
    const bool book_fonts = app_settings_book_fonts();
    EpdRect card = s_bookfont_card;
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, card, 20, 0x70);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,"书内自带字体",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,card.width-122,card.height/2-8},27,book_fonts?"开启":"关闭",EPD_DRAW_ALIGN_LEFT,false);
    EpdRect toggle = {card.x+card.width-89,card.y+3*card.height/4-19,69,39};
    ui_fill_round_rect(fb, toggle, 19, book_fonts ? 0x38 : 0xd0);
    int knob = toggle.x + (book_fonts ? 49 : 20);
    epd_fill_circle(knob, toggle.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, toggle.y + 19, 16, 0x78, fb);
    EpdRect rule = s_rule_card;
    ui_fill_round_rect(fb, rule, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, rule, 20, 0x70);
    static const char *const rule_names[] = {"无", "虚线", "点线"};
    int selected_rule = app_settings_book_reading_line();
    ui_text_box(fb,(EpdRect){rule.x+22,rule.y+10,142,rule.height-20},25,"阅读线",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){rule.x+172,rule.y+10,77,rule.height-20},25,rule_names[selected_rule],EPD_DRAW_ALIGN_RIGHT,false);
    ui_draw_icon(fb,rule.x+rule.width-24,rule.y+rule.height/2,27,UI_ICON_CHEVRON_RIGHT,0x38);
    EpdRect aa = s_antialias_card;
    ui_fill_round_rect(fb, aa, 20, UI_GRAY_WHITE); ui_draw_control_frame(fb, aa, 20, 0x70);
    ui_text_box(fb,(EpdRect){aa.x+21,aa.y+10,aa.width-122,aa.height-20},25,"抗锯齿",EPD_DRAW_ALIGN_LEFT,false);
    draw_reading_switch(fb,(EpdRect){aa.x+aa.width-89,aa.y+aa.height/2-19,69,39},app_settings_book_antialias());
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 28, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static EpdRect reader_weight_rect(int index) { return (EpdRect){index?542:56,909,86,86}; }
static int reader_weight_step(int x,int y) {
    if(ui_rect_hit(reader_weight_rect(0),x,y))return -1;
    if(ui_rect_hit(reader_weight_rect(1),x,y))return 1;
    return 0;
}
static void draw_weight_settings(uint8_t *fb) {
    draw_sheet(fb, 580, "字重");
    draw_sheet_back(fb, 580);
    reader_panel_text_vc(fb, 342, 730, 22, "调整阅读文字粗细", EPD_DRAW_ALIGN_CENTER, false);
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    int weight=app_settings_book_weight_fine(),prior_weight=ttf_get_stroke_adjust_fine();
    ttf_set_stroke_adjust_fine(weight);
    ttf_draw_text_px(fb, 342, 833, 48, "阅读文字 Aa 123", EPD_DRAW_ALIGN_CENTER, 0, 15);
    ttf_set_stroke_adjust_fine(prior_weight);
    ui_fill_round_rect(fb,(EpdRect){36,895,612,114},57,0xe0);
    ui_draw_control_frame(fb,(EpdRect){36,895,612,114},57,0x30);
    for(int i=0;i<2;++i){EpdRect control=reader_weight_rect(i);epd_fill_circle(control.x+43,952,43,UI_GRAY_WHITE,fb);ui_draw_control_frame(fb,control,43,0x30);}
    ui_fill_rect_fast((EpdRect){87,951,24,3},weight<=-APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    ui_fill_rect_fast((EpdRect){573,951,24,3},weight>=APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    ui_fill_rect_fast((EpdRect){584,941,3,24},weight>=APP_BOOK_WEIGHT_FINE_MAX?0xa0:0x20,fb);
    char value[20];reader_weight_value(value,sizeof(value),weight);
    reader_panel_text_vc(fb,342,936,44,value,EPD_DRAW_ALIGN_CENTER,false);
    reader_panel_text_vc(fb,342,975,20,"像素 · 每次 1/16",EPD_DRAW_ALIGN_CENTER,false);
    reader_panel_text_vc(fb, 342, 1030, 20, "标准保留字体原始笔画", EPD_DRAW_ALIGN_CENTER, false);
    reader_panel_text_vc(fb, 342, 1070, 20, "同时适用于导入字体和书内字体", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static EpdRect rule_style_rect(int index) {
    return (EpdRect){36 + index * 208, 788, 196, 120};
}

static void draw_rule_settings(uint8_t *fb) {
    const int top = 580;
    draw_sheet(fb, top, "阅读线");
    draw_sheet_back(fb, top);
    reader_panel_text(fb, 42, 741, 21, "选择样式", EPD_DRAW_ALIGN_LEFT, false);
    static const char *const names[] = {"无", "虚线", "点线"};
    int selected = app_settings_book_reading_line();
    for (int i = 0; i < 3; ++i) {
        EpdRect card = rule_style_rect(i);
        ui_fill_round_rect(fb, card, 20, selected == i ? 0xc8 : 0xe8);
        ui_draw_control_frame(fb, card, 20, selected == i ? 0x40 : 0x78);
        reader_panel_text_vc(fb, card.x + card.width / 2, card.y + 37, 24, names[i], EPD_DRAW_ALIGN_CENTER, false);
        if (i) {
            int span = i == 1 ? 14 : 3, step = i == 1 ? 24 : 13;
            int start = card.x + 25, end = card.x + card.width - 25;
            for (int x = start; x < end; x += step)
                ui_fill_rect_fast((EpdRect){x, card.y + 83, x + span <= end ? span : end - x, 2}, 0x48, fb);
        } else reader_panel_text_vc(fb, card.x + card.width / 2, card.y + 83, 17, "不显示", EPD_DRAW_ALIGN_CENTER, false);
    }
    char offset[36];
    snprintf(offset, sizeof(offset), "上下位置  %+d", app_settings_book_reading_line_offset());
    reader_panel_text(fb, 42, 936, 21, offset, EPD_DRAW_ALIGN_LEFT, false);
    const EpdRect controls[] = {s_rule_offset_up, s_rule_offset_reset, s_rule_offset_down};
    const char *const labels[] = {"上移 2px", "恢复居中", "下移 2px"};
    for (int i = 0; i < 3; ++i) {
        ui_fill_round_rect(fb, controls[i], 20, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, controls[i], 20, selected ? 0x60 : 0xa0);
        reader_panel_text_vc(fb, controls[i].x + controls[i].width / 2,
                   controls[i].y + controls[i].height / 2, 22, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
    if (!selected) reader_panel_text(fb, 42, 1080, 18, "选择虚线或点线后可调整位置", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect done = {36, 1113, 612, 72};
    ui_fill_round_rect(fb, done, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1149, 23, "返回字体设置", EPD_DRAW_ALIGN_CENTER, true);
}

static EpdRect indent_adjust_rect(int index) {
    const EpdRect controls[] = {{214,1050,100,64}, {322,1050,208,64}, {538,1050,100,64}};
    return controls[index];
}
static void draw_layout_settings(uint8_t* fb) {
    const int top = 500;
    draw_sheet(fb, top, "排版设置");
    draw_sheet_back(fb, top);
    int shown_margin = s_reader_slider >= 0 ? s_reader_preview_margin : s_margin;
    int shown_line = s_reader_slider >= 0 ? s_reader_preview_line : app_settings_book_line_spacing();
    int shown_para = s_reader_slider >= 0 ? s_reader_preview_para : app_settings_book_paragraph_spacing();
    int shown_tracking = s_reader_slider >= 0 ? s_reader_preview_tracking : app_settings_book_tracking();
    int margin_widths[BOOK_MARGIN_CHOICES], margin_values[BOOK_MARGIN_CHOICES];
    int margin_count = reader_margin_levels(s_px, (shown_tracking - 2) * 2,
                                             margin_widths, margin_values);
    draw_pill_slider(fb, reader_slider_rect(1), "小", "大", "边距",
                     reader_margin_level_for(shown_margin, s_px, (shown_tracking - 2) * 2),
                     margin_count, 21, 21);
    int line_i = shown_line - 110;
    if (line_i < 0) line_i = 0;
    if (line_i > 40) line_i = 40;
    draw_pill_slider(fb, reader_slider_rect(2), "紧", "松", "行距", line_i, 41, 21, 21);
    reader_panel_text(fb, 42, 715, 21, "段距", EPD_DRAW_ALIGN_LEFT, false);
    char value[16]; snprintf(value, sizeof(value), "%d%%", shown_para);
    draw_pill_slider(fb, reader_slider_rect(3), "紧", "松", value, shown_para / 25, 4, 21, 21);
    reader_panel_text(fb, 42, 835, 21, "字间距", EPD_DRAW_ALIGN_LEFT, false);
    static const char* track_names[] = {"-4", "-2", "默认", "+2", "+4"};
    draw_pill_slider(fb, reader_slider_rect(4), "紧", "松", track_names[shown_tracking], shown_tracking, 5, 21, 21);
    ui_text_box(fb,(EpdRect){42,970,154,64},24,"首行缩进",EPD_DRAW_ALIGN_LEFT,false);
    static const char* indent_names[] = {"无", "1字", "2字", "3字"};
    int indent = app_settings_book_indent();
    for (int i = 0; i < 4; ++i) {
        EpdRect choice = {214 + i * 108, 970, 100, 64};
        bool selected = i == indent;
        ui_fill_round_rect(fb, choice, 16, selected ? UI_GRAY_BLACK : 0xe4);
        ui_draw_control_frame(fb, choice, 16, selected ? UI_GRAY_BLACK : 0x98);
        ui_text_box(fb,(EpdRect){choice.x+8,choice.y+5,choice.width-16,choice.height-10},23,indent_names[i],EPD_DRAW_ALIGN_CENTER,selected);
    }
    ui_text_box(fb,(EpdRect){42,1050,154,64},24,"缩进微调",EPD_DRAW_ALIGN_LEFT,false);
    char adjustment[28]; snprintf(adjustment, sizeof(adjustment), "%+d px · 复位", app_settings_book_indent_adjust());
    const char *labels[] = {"−", adjustment, "+"};
    for (int i = 0; i < 3; ++i) {
        EpdRect control = indent_adjust_rect(i);
        ui_fill_round_rect(fb, control, 16, 0xf0);
        ui_draw_control_frame(fb, control, 16, 0x50);
        ui_text_box(fb,(EpdRect){control.x+8,control.y+5,control.width-16,control.height-10},i==1?22:31,labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
    EpdRect back = {36, 1132, 612, 72};
    ui_fill_round_rect(fb, back, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1168, 24, "返回字体设置", EPD_DRAW_ALIGN_CENTER, true);
}

/* 阅读设置面板：上游 r91 的半屏双列卡片壳，行表（我方阶段 0 的不变量）驱动槽位几何、
   绘制、热区与派发，加一项只改这张表。第八槽「横屏阅读」与第九槽「横屏双栏」（第 4 步）
   都是我方功能，故为两列五行（第 4 步起：槽高 76、间距 10，垂直包络与 8 槽时代逐位一致
   ——末行底仍停在 1122，「完成」上约 19px；第 9 槽独占末行左半格）。
   Upstream r91's two-column card shell; a row table (our stage-0 invariant) drives slot
   geometry, drawing, hot zones and dispatch, so adding an entry means editing this table
   only. Slot 8 (横屏阅读) and slot 9 (横屏双栏, step 4) are ours, hence two columns x five
   rows (from step 4: 76px cards, 10px gaps, the vertical envelope unchanged from the
   8-slot era — the last row's bottom still rests at 1122, ~19px above Done; slot 9 sits
   alone in the last row's left half). */
#define READING_ROW_KEYS   4   ///< 按键控制：进按键子面板 / keys row: opens the key sub-panel
#define READING_ROW_FULL   5   ///< 自动全刷值循环卡片 / auto full-refresh cycling card
#define READING_ROW_WAVE   6   ///< 翻页效果卡片 / page-turn effect card
#define READING_ROW_TURN   7   ///< 翻页模式：进子面板 / turn mode: opens the turn sub-panel
#define READING_ROW_COUNT  9   ///< 槽位数（第 4 步新增横屏双栏）/ slot count (step 4 adds the two-column slot)
#define READING_ROW_COLS   2   ///< 每行槽位数 / slots per row
#define READING_ROW_HEIGHT 76  ///< 标准槽位高（与 reading_setting_rect 的 76+extra 同口径；第 4 步 reflow 改为均匀 pitch，末行不再 footer 收缩）/ standard slot height (matches reading_setting_rect's 76+extra; step 4 reflow uses a uniform pitch, the last row no longer shrinks per footer)

typedef enum {
    READING_CELL_TOGGLE,  ///< 开关槽：走 apply_reader_option(kind) / toggle: apply_reader_option(kind)
    READING_CELL_VALUE,   ///< 值循环槽：点击切下一个取值 / cycling value: a tap advances it
    READING_CELL_PAGE,    ///< 子面板槽：点击进子面板 / sub-page: a tap opens it
} reading_cell_t;

typedef struct {
    uint8_t kind;          ///< apply_reader_option() 的 kind；非开关槽见 READING_ROW_* / kind for apply_reader_option(); non-toggle slots use READING_ROW_*
    reading_cell_t cell;   ///< 槽位类型 / cell type
    const char *title;     ///< 标题 / title
    const char *detail;    ///< 副标题；值循环槽用它当取值文案 / subtitle; value cards show it as the value line
} reading_row_t;

static const reading_row_t s_reading_rows[READING_ROW_COUNT] = {
    {READING_ROW_FULL, READING_CELL_VALUE,  "自动全刷",     NULL},
    {READING_ROW_WAVE, READING_CELL_VALUE,  "翻页效果",     NULL},
    {READING_ROW_TURN, READING_CELL_PAGE,   "翻页模式",     NULL},
    {0,                READING_CELL_TOGGLE, "电源键翻页",   "短按下一页，长按锁屏"},
    {READING_ROW_KEYS, READING_CELL_PAGE,   "按键控制",     "自定义三键"},
    {1,                READING_CELL_TOGGLE, "全屏沉浸",     "全屏时隐藏状态栏"},
    {2,                READING_CELL_TOGGLE, "关闭书内图片", "跳过正文插图，保留原书文件"},
    {3,                READING_CELL_TOGGLE, "横屏阅读",     "正文横向铺满，点词查译照常"},
    /* kind=8：开关 kind 0-3 与子面板槽 READING_ROW_*(4-7) 之后顺延，避开命名空间。
       kind 8: toggle kinds 0-3 then past the non-toggle READING_ROW_* markers (4-7). */
    {8,                READING_CELL_TOGGLE, "横屏双栏",     "横屏时正文分两栏"},
};

static int reading_row_count(void) { return READING_ROW_COUNT; }

static bool reading_row_on(int index) {
    switch (s_reading_rows[index].kind) {
        case 0: return app_settings_reader_power_turn();
        case 1: return app_settings_reader_immersive();
        case 2: return app_settings_reader_hide_images();
        case 3: return app_settings_reader_landscape();
        case 8: return app_settings_reader_two_column();
        default: return false; // 非开关槽没有开关状态 / non-toggle cells carry no state
    }
}

// 槽位几何（第 4 步重排为 5 行）：两列，行高 76+extra、间距 10，首行顶 = 面板顶+122
// （标题区 92px + 30px 间隙，任意系统字号下与 4 行时代同式）；5 行总高随 extra 伸缩，
// 末行底恒停在 1122——「完成」(y=1141) 之上约 19px，与 8 槽时代逐位一致。9 槽为奇数，
// 第 9 槽独占末行左半格。fit-check：所有槽位矩形 + 完成矩形在竖屏屏界内（宿主测试
// 逐字号断言）。
// Slot geometry (step 4 reflows to five rows): two columns, 76+extra card height, 10px
// gaps, first row's top at panel top + 122 (92px title zone + 30px gap, the same shape
// as the four-row era at every system font size); the five rows stretch with extra and
// the last row's bottom rests at 1122 — ~19px above Done (y=1141), matching the 8-slot
// era exactly. Nine slots is odd, so slot 9 sits alone in the last row's left half.
// Fit check: every slot rect plus the Done rect stay inside the portrait screen (host
// test asserts per font size).
static EpdRect reading_setting_rect(int index) {
    int extra = reader_ui_extra(41);
    if (index < 0 || index >= READING_ROW_COUNT) return (EpdRect){0, 0, 0, 0};
    int col = index % READING_ROW_COLS, row = index / READING_ROW_COLS;
    int height = 76 + extra, pitch = height + 10;
    return (EpdRect){36 + col * 314, 702 - reader_ui_extra(204) + row * pitch, 298, height};
}
static int reading_settings_top(void) { return 580 - reader_ui_extra(204); }
// 行表按槽位编号寻址：绘制、热区、派发三处共用，间隙天然不穿透。
// The table is addressed by slot index: drawing, hot zones and dispatch all share it, so the
// gaps between cards stay inert by construction.
static EpdRect reading_toggle_rect(int index) { return reading_setting_rect(index); }
static void draw_reading_switch(uint8_t *fb, EpdRect track, bool on) {
    ui_fill_round_rect(fb, track, 19, on ? 0x38 : 0xc4);
    int cx = track.x + (on ? 49 : 20);
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, 0x78, fb);
}
static void draw_reading_card(uint8_t *fb, int index, const char *title,
                              const char *value, bool cycle) {
    EpdRect card = reading_setting_rect(index);
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 20, 0x68);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[48]; copy_text(shown,sizeof(shown),value);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,222,card.height/2-8},26,shown,EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,card.x+272,card.y+3*card.height/4,26,
                 cycle ? UI_ICON_ROTATE_CW : UI_ICON_CHEVRON_RIGHT, 0x38);
}
// 开关槽：标题 + 副标题（无副标题时退化为「开启/关闭」）+ 开关；副标题经 ui_text_box
// 自动缩放，半宽槽位也放得下。/ Toggle slot: title, subtitle (or 开启/关闭 when it has
// none) and the switch; the subtitle auto-fits via ui_text_box, so half-width slots still fit.
static void draw_reading_toggle(uint8_t *fb, int index) {
    EpdRect row = reading_toggle_rect(index);
    const reading_row_t *r = &s_reading_rows[index];
    bool on = reading_row_on(index);
    ui_fill_round_rect(fb, row, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, row, 20, 0x68);
    ui_text_box(fb,(EpdRect){row.x+21,row.y+6,row.width-42,row.height/2-16},23,r->title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[48];
    if (r->detail) copy_text(shown,sizeof(shown),r->detail);
    else copy_text(shown,sizeof(shown),on?"开启":"关闭");
    ui_text_box(fb,(EpdRect){row.x+21,row.y+row.height/2,row.width-108,row.height/2-8},19,shown,EPD_DRAW_ALIGN_LEFT,false);
    draw_reading_switch(fb,(EpdRect){row.x+row.width-88,row.y+row.height/2-19,69,39},on);
}
static void draw_reading_settings(uint8_t* fb) {
    draw_sheet(fb, reading_settings_top(), "阅读设置");
    char interval[24];
    unsigned pages = app_settings_reader_full_pages();
    if (pages) snprintf(interval, sizeof(interval), "每 %u 页", pages);
    else copy_text(interval, sizeof(interval), "不强制");
    // 值循环卡片取设置值，标题与子面板卡片取值都来自行表。
    // Cycling cards read the settings; titles and the sub-page card's value come from the table.
    draw_reading_card(fb, 0, s_reading_rows[0].title, interval, true);
    draw_reading_card(fb, 1, s_reading_rows[1].title, app_settings_reader_turn_effect() ? "水波纹" : "普通", true);
    draw_reading_card(fb, 2, s_reading_rows[2].title, app_settings_reader_vertical_turn() ? "上下翻页" : "左右翻页", false);
    draw_reading_card(fb, 4, s_reading_rows[4].title, s_reading_rows[4].detail, false);
    for (int i = 0; i < reading_row_count(); ++i)
        if (s_reading_rows[i].cell == READING_CELL_TOGGLE) draw_reading_toggle(fb, i);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static void draw_turn_settings(uint8_t *fb) {
    draw_sheet(fb, 270, "翻页模式");
    draw_sheet_back(fb, 270);
    bool vertical = app_settings_reader_vertical_turn();
    for (int i = 0; i < 2; ++i) {
        int y = 370 + i * 322;
        bool selected = vertical == (i != 0);
        EpdRect card = {36, y, 612, 292};
        ui_fill_round_rect(fb, card, 22, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, card, 22, 0x50);
        ui_draw_control_frame(fb, (EpdRect){37, y + 1, 610, 290}, 21, 0x50);
        EpdRect diagram = {64, y + 37, 182, 220};
        ui_fill_round_rect(fb, diagram, 12, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, diagram, 12, 0x58);
        if (i) {
            ui_hairline(fb, diagram.y + 73, 65, 180, 0x58);
            reader_panel_text_vc(fb, 155, diagram.y + 36, 21, "上一页", EPD_DRAW_ALIGN_CENTER, false);
            reader_panel_text_vc(fb, 155, diagram.y + 147, 21, "下一页", EPD_DRAW_ALIGN_CENTER, false);
        } else {
            ui_fill_rect_fast((EpdRect){155, diagram.y + 1, 1, 218}, 0x58, fb);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+94-15,78,30},21,"上一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+94-15,78,30},21,"下一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
        }
        ui_text_box(fb,(EpdRect){274,y+43,350,44},32,i ? "上下翻页" : "左右翻页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+112,350,44},23,i ? "上 1/3：上一页" : "左 1/2：上一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+158,350,44},23,i ? "下 2/3：下一页" : "右 1/2：下一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+213,350,44},21,"左右滑动均可翻页",EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(610, y + 61, 12, 0x38, fb);
        if (selected) epd_fill_circle(610, y + 61, 7, UI_GRAY_BLACK, fb);
    }
    reader_panel_text_vc(fb, 342, 1051, 21, "中间区域双击切换全屏 · 始终可用", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}

static const char *reader_key_label(app_reader_key_action_t action) {
    static const char *const labels[] = {"上一页", "下一页", "阅读工具栏", "切换全屏", "返回主页", "手动全刷", "不操作"};
    return labels[action >= 0 && action < APP_READER_KEY_COUNT ? action : APP_READER_KEY_NONE];
}
static EpdRect reader_key_card(unsigned key) { return (EpdRect){36 + (int)key * 208, 506, 196, 114}; }
static EpdRect reader_key_preset_card(unsigned preset) { return (EpdRect){36, 810 + (int)(preset - 1) * 148, 612, 136}; }
static bool reader_key_preset_selected(unsigned preset) {
    static const app_reader_key_action_t actions[2][3] = {
        {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT},
        {APP_READER_KEY_HOME, APP_READER_KEY_FULLSCREEN, APP_READER_KEY_TOOLS}
    };
    for (unsigned i = 0; i < 3; ++i) if (app_settings_reader_key_action(i) != actions[preset - 1][i]) return false;
    return app_settings_reader_hold_action() == (preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH);
}
static void draw_key_settings(uint8_t *fb) {
    draw_sheet(fb, 270, "按键控制");
    draw_sheet_back(fb, 270);
    static const char *const names[] = {"左键", "中键", "右键"};
    for (unsigned i = 0; i < 3; ++i) {
        int cx = 134 + (int)i * 208;
        if (i == 1) {
            epd_draw_circle(cx, 418, 25, UI_GRAY_BLACK, fb);
            epd_draw_circle(cx, 418, 24, UI_GRAY_BLACK, fb);
        } else ui_fill_round_rect(fb, (EpdRect){cx - 26, 415, 52, 6}, 3, UI_GRAY_BLACK);
        reader_panel_text_vc(fb, cx, 473, 22, names[i], EPD_DRAW_ALIGN_CENTER, false);
        EpdRect card = reader_key_card(i);
        ui_fill_round_rect(fb, card, 20, 0xf0);
        ui_draw_control_frame(fb, card, 20, 0x58);
        ui_text_box(fb,(EpdRect){card.x+17,card.y+7,card.width-57,36},20,"单击",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){card.x+12,card.y+52,card.width-24,52},24,reader_key_label(app_settings_reader_key_action(i)),EPD_DRAW_ALIGN_CENTER,false);
        ui_draw_icon(fb, card.x + card.width - 23, card.y + 24, 20, UI_ICON_CHEVRON_DOWN, 0x38);
    }
    EpdRect hold = {36, 646, 612, 96};
    ui_fill_round_rect(fb, hold, 20, 0xf0); ui_draw_control_frame(fb, hold, 20, 0x58);
    ui_text_box(fb,(EpdRect){58,652,238,48},26,"中键长按",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){322,652,270,48},25,reader_key_label(app_settings_reader_hold_action()),EPD_DRAW_ALIGN_RIGHT,false);
    ui_draw_icon(fb, 625, 679, 20, UI_ICON_CHEVRON_DOWN, 0x38);
    reader_panel_text(fb, 58, 710, 19, "点击选择长按功能，例如返回主页", EPD_DRAW_ALIGN_LEFT, false);
    reader_panel_text_vc(fb, 342, 775, 20, "屏幕中间双击切换全屏，不受按键设置影响", EPD_DRAW_ALIGN_CENTER, false);
    for (unsigned preset = 1; preset <= 2; ++preset) {
        EpdRect card = reader_key_preset_card(preset);
        bool selected = reader_key_preset_selected(preset);
        ui_fill_round_rect(fb, card, 20, selected ? 0xd8 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, card, 20, 0x58);
        char title[32]; snprintf(title, sizeof(title), "推荐设置 %u", preset);
        ui_text_box(fb,(EpdRect){58,card.y+7,344,49},26,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){435,card.y+7,186,49},20,selected?"已应用":"点击应用",EPD_DRAW_ALIGN_RIGHT,false);
        ui_text_box(fb,(EpdRect){58,card.y+59,564,40},21,preset==1?"左上一页 · 中工具栏 · 右下一页":"左回首页 · 中全屏 · 右工具栏",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){58,card.y+103,564,34},19,preset==1?"中键长按：返回主页":"中键长按：手动全刷",EPD_DRAW_ALIGN_LEFT,false);
    }
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "完成", EPD_DRAW_ALIGN_CENTER, true);
}
static void draw_key_picker(uint8_t *fb) {
    static const char *const titles[] = {"左键功能", "中键功能", "右键功能", "中键长按功能"};
    draw_sheet(fb, 270, titles[s_reader_key_pick < 4 ? s_reader_key_pick : 0]);
    draw_sheet_back(fb, 270);
    for (int i = 0; i < APP_READER_KEY_COUNT; ++i) {
        EpdRect card = {36, 390 + i * 94, 612, 78};
        bool selected = (s_reader_key_pick == 3 ? app_settings_reader_hold_action() : app_settings_reader_key_action(s_reader_key_pick)) == i;
        ui_fill_round_rect(fb, card, 20, selected ? 0xd8 : UI_GRAY_WHITE);
        ui_draw_control_frame(fb, card, 20, 0x58);
        reader_panel_text_vc(fb, 58, card.y + 39, 27, reader_key_label(i), EPD_DRAW_ALIGN_LEFT, false);
        epd_draw_circle(608, card.y + 39, 11, 0x38, fb);
        if (selected) epd_fill_circle(608, card.y + 39, 6, UI_GRAY_BLACK, fb);
    }
    reader_panel_text_vc(fb, 342, 1112, 20, s_reader_key_error[0] ? s_reader_key_error : "从底部上滑可随时打开阅读工具栏", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    reader_panel_text_vc(fb, 342, 1170, 26, "返回", EPD_DRAW_ALIGN_CENTER, true);
}

static int reader_font_picker_top(void) { return app_settings_system_font_size()>=150?270:584; }
static EpdRect reader_font_pick_rect(int slot) {
    bool large=app_settings_system_font_size()>=150;
    int columns=large?2:3;
    return (EpdRect){36+(slot%columns)*(large?314:204),
                     reader_font_picker_top()+(large?150:131)+(slot/columns)*146,
                     large?298:180,126};
}
static void draw_font_picker(uint8_t* fb) {
    const int top = reader_font_picker_top();
    draw_sheet(fb, top, "字体");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    int count = ttf_font_count();
    int pages = count > 0 ? (count + BOOK_FONT_PAGE - 1) / BOOK_FONT_PAGE : 1;
    char current[96];
    snprintf(current, sizeof(current), "当前选择 · %s  %d/%d", ttf_font_display_name(), s_font_page + 1, pages);
    fit_text(current, 20, 540);
    ui_text_box(fb,(EpdRect){90,top+94,504,32},20,current,EPD_DRAW_ALIGN_CENTER,false);
    int start = s_font_page * BOOK_FONT_PAGE;
    char active[TTF_FONT_PATH_MAX]; copy_text(active, sizeof(active), ttf_font_path());
    bool active_builtin = ttf_font_is_builtin();
    for (int slot = 0; slot < BOOK_FONT_PAGE && start + slot < count; ++slot) {
        const ttf_font_item_t* item = ttf_font_item(start + slot);
        EpdRect card = reader_font_pick_rect(slot);
        bool selected = !strcmp(active, item->path);
        ui_fill_round_rect(fb, card, 18, selected ? 0xc2 : 0xe4);
        ui_draw_control_frame(fb, card, 18, selected ? 0x48 : 0x98);
        char label[TTF_FONT_NAME_MAX];
        copy_text(label, sizeof(label), font_friendly_name(item));
        if (ttf_font_open(item->path) == ESP_OK) {
            fit_text(label,25,card.width-32);
            draw_font_name_with_current_face(fb, card.x + card.width/2, card.y + 63, label,card.width-32);
        } else {
            fit_fixed_text(label, ui_text_effective_px(25), card.width - 32);
            ui_text_box(fb,ui_inset_rect(card,16),25,label,EPD_DRAW_ALIGN_CENTER,false);
        }
        if (selected) epd_fill_circle(card.x+card.width-22,card.y+22,7,UI_GRAY_BLACK,fb);
    }
    if (active_builtin) (void)ttf_font_open_builtin();
    else if (active[0]) (void)ttf_font_open(active);
    if (!count) reader_panel_text_vc(fb, 342, 780, 24, "所选字体目录中没有可用字体", EPD_DRAW_ALIGN_CENTER, false);
}

// 栏几何：版心按栏数切条，栏间留 gutter。count<=1 时宽/偏移/矩形全部恒等（逐位返回
// 联合区），关形态不改变任何上游可观测行为。右栏绘制 x 与右栏命中减法同出
// reader_column_offset——横屏坑②配方，两边各写一遍必烂；rect 建在 offset 之上，
// 「同出」由构造保证。（签名比 v2 §3 草图多带 area 参数：保持纯函数，几何可单测。）
// Column geometry: the body area splits into strips with a gutter between them. At
// count<=1 the width, offset and rect are all identities (the joint area comes back
// bit-for-bit), so the closed form changes nothing observable upstream. The right
// column's draw x and its hit-test subtraction both come out of reader_column_offset —
// the pitfall-2 recipe — writing either side twice is how this rots; rect is built on
// offset, so "same source" holds by construction. (The signatures carry area where the
// v2 section 3 sketch did not: they stay pure functions, testable in isolation.)
static int reader_column_gutter(void) {
    // 版心边距优先，下限 48：两栏时间隙不能窄过一个字身，否则视觉上连成一片。
    // The body margin wins, floored at 48: the gutter must not fall below one glyph
    // body or the two columns read as one.
    int m = s_margin ? s_margin : 36;
    return m > 48 ? m : 48;
}
static int reader_column_width(EpdRect area, int count) {
    if (count <= 1) return area.width;
    return (area.width - reader_column_gutter()) / count;
}
static int reader_column_offset(EpdRect area, int count, int i) {
    if (count <= 1 || i <= 0) return 0;
    return i * (reader_column_width(area, count) + reader_column_gutter());
}
static EpdRect reader_column_rect(EpdRect area, int count, int i) {
    if (count <= 1) return area;
    return (EpdRect){area.x + reader_column_offset(area, count, i), area.y,
                     reader_column_width(area, count), area.height};
}

/* ---- 视觉页地图 / Visual page map ---- */
// 视觉页 = 用户眼里的一屏。双栏时一个视觉页覆盖两个逻辑页（左右栏），整页插图独占一个
// 视觉页；单栏时视觉页恒等于逻辑页。续读、目录、书签、翻页与页码全部经本模块寻址，
// 散点禁止直连 book_layout 页码——地图与版面失步是这套模型唯一的死法。
// A visual page is one screen. Two columns cover two logical pages per visual page and a
// whole-page image owns one alone; single column maps visual pages identically. Resume,
// TOC, bookmarks, turns and page numbers all address through this module; scattered
// book_layout page access is forbidden — a map out of step with the layout is the only
// way this model dies.
//
// P0-1 落地退化形态：建图与寻址全套就绪，双栏开关与第二次绘制在后续阶段接入；单栏下
// 所有寻址结果与旧逻辑页逐位一致（互证测试①守这件事）。
// P0-1 lands the degenerate form: building and addressing are complete while the
// two-column switch and the second draw arrive in later stages; single-column results
// match the old logical pages exactly (cross-check test 1 guards this).

// 与 book_layout 的 4096 页上限对齐；条目数不超过逻辑页数，静态数组无需分配。
// Aligned with book_layout's 4096-page cap; entries never exceed logical pages, so the
// static array needs no allocation.
#define READER_PAGE_MAX 4096u

// 栏模式：普通文字页与题头页左右两栏；整页插图独占视觉页（draw_page 对它是 no-op，图由调用方画）。
// Column mode: ordinary text and lead pages use two columns; a whole-page image owns its
// visual page alone (draw_page is a no-op for it and the caller paints the image).
#define READER_VISUAL_MODE_COLUMNS2 0
#define READER_VISUAL_MODE_JOINT     1

// 视觉页条目：起始逻辑页（左栏；JOINT 页即其自身）与栏模式。
// Visual page entry: the starting logical page (the left column; a JOINT page is itself)
// and the column mode.
typedef struct { uint16_t start; uint8_t mode; } reader_visual_page_t;

static reader_visual_page_t s_page_map[READER_PAGE_MAX];
static size_t s_page_map_count;
static size_t s_page_map_pages;      // 建图时的逻辑页数，仅作诊断。/ Logical page count at build time; diagnostics only.
static uint32_t s_page_map_generation; // 建图时的布局代次，失步判定用它。/ Layout generation at build time; staleness is judged by it.
static int s_page_map_columns;       // 建图时的栏数，退化公式随它变。/ Column count at build time; the fallback formula follows it.
static bool s_page_map_degenerate;   // true = 公式快路径，数组不参与寻址。/ true = formula fast path; the array is not consulted.

// 整页插图回退路径：页级插图序号有效且没有混排槽。
// Whole-page image fallback: a page-level image index with no inline slot.
static bool reader_page_is_joint(size_t page) {
    return book_layout_page_image(page) >= 0 && book_layout_page_image_count(page) == 0;
}

// 建图：全章走一遍分类，每页两次 O(1) 查询。单栏恒等；双栏把普通页顺序两两配对、
// JOINT 页独占、末页或下一页是 JOINT 时左栏独占。全章无 JOINT 页时置 degenerate，
// 寻址走公式，数组不参与热路径；互证测试断言论两条路径等价。
// Build the map by classifying the chapter once, two O(1) queries per page. Single column
// is the identity; two columns pair ordinary pages in order, give JOINT pages their own
// visual page, and leave the left column alone at the tail or before a JOINT page. A
// chapter without JOINT pages sets the degenerate flag so addressing takes the formula
// and the array stays off the hot path; the cross-check test asserts both paths agree.
static void reader_rebuild_page_map(int columns) {
    s_page_map_columns = columns < 1 ? 1 : columns;
    size_t n = book_layout_page_count();
    if (n > READER_PAGE_MAX) n = READER_PAGE_MAX;
    s_page_map_pages = n;
    s_page_map_generation = book_layout_generation();
    s_page_map_count = 0;
    if (s_page_map_columns <= 1) {
        // 数组仍照实填写，互证测试据此比对公式与建图两条路径。
        // The array is still filled so the cross-check test can compare both paths.
        for (size_t k = 0; k < n; ++k) {
            s_page_map[k].start = (uint16_t)k;
            s_page_map[k].mode = READER_VISUAL_MODE_COLUMNS2;
        }
        s_page_map_count = n;
        s_page_map_degenerate = true;
        return;
    }
    bool joint = false;
    size_t k = 0;
    while (k < n && s_page_map_count < READER_PAGE_MAX) {
        if (reader_page_is_joint(k)) {
            joint = true;
            s_page_map[s_page_map_count].start = (uint16_t)k;
            s_page_map[s_page_map_count].mode = READER_VISUAL_MODE_JOINT;
            ++s_page_map_count;
            ++k;
            continue;
        }
        // 下一页存在且不是 JOINT 才配对，否则这一视觉页只有左栏。
        // Pair only when the next page exists and is not JOINT; otherwise the left column stands alone.
        size_t step = k + 1 < n && !reader_page_is_joint(k + 1) ? 2 : 1;
        s_page_map[s_page_map_count].start = (uint16_t)k;
        s_page_map[s_page_map_count].mode = READER_VISUAL_MODE_COLUMNS2;
        ++s_page_map_count;
        k += step;
    }
    s_page_map_degenerate = !joint;
}

// 地图与版面失步（重排或释放后没有重建）时退化到公式寻址并告警一次，绝不静默错页。
// A map out of step with the layout (a reflow or free without a rebuild) degrades to the
// formula and warns once; a wrong page is never silent.
static void reader_map_check_sync(void) {
    if (book_layout_generation() == s_page_map_generation) return;
    if (!s_page_map_degenerate) {
        s_page_map_degenerate = true;
        ESP_LOGW(TAG, "page map stale: built gen %u, layout gen %u; formula fallback",
                 (unsigned)s_page_map_generation, (unsigned)book_layout_generation());
    }
}

// 失步判定后的视觉页总数（内部复用）。/ Visual page count after the sync check (shared internally).
static size_t reader_visual_count_checked(void) {
    size_t n = book_layout_page_count();
    if (!n) return 0;
    if (s_page_map_degenerate) return s_page_map_columns <= 1 ? n : (n + 1) / 2;
    return s_page_map_count;
}

// 把视觉页下标解析成条目（钳下标 + 失步退化）；空布局返回 false。
// Resolve a visual index into an entry (clamped, with stale degradation); false means an empty layout.
static bool reader_visual_entry(size_t visual, size_t* start, int* mode) {
    size_t count = reader_visual_count_checked();
    if (!count) return false;
    if (visual >= count) visual = count - 1;
    if (s_page_map_degenerate) {
        size_t n = book_layout_page_count();
        *start = s_page_map_columns <= 1 ? visual : visual * 2;
        if (*start >= n) *start = n - 1;
        *mode = READER_VISUAL_MODE_COLUMNS2;
        return true;
    }
    *start = s_page_map[visual].start;
    *mode = s_page_map[visual].mode;
    return true;
}

// 视觉页总数（页脚分母）。/ Total visual pages (the footer denominator).
static size_t reader_visual_count(void) {
    reader_map_check_sync();
    return reader_visual_count_checked();
}

// 逻辑页 → 视觉页下标。/ Logical page to visual page index.
static size_t reader_visual_index(size_t logical) {
    reader_map_check_sync();
    size_t n = book_layout_page_count();
    if (!n) return 0;
    if (logical >= n) logical = n - 1;
    if (s_page_map_degenerate)
        return s_page_map_columns <= 1 ? logical : logical / 2;
    size_t index = 0;
    for (size_t i = 0; i < s_page_map_count; ++i) {
        if (s_page_map[i].start <= logical) index = i;
        else break;
    }
    return index;
}

// 视觉页 → 起始逻辑页（= s_page 落点）。/ Visual page to its starting logical page (the s_page landing).
static size_t reader_visual_start(size_t visual) {
    reader_map_check_sync();
    size_t start;
    int mode;
    if (!reader_visual_entry(visual, &start, &mode)) return 0;
    return start;
}

// 视觉页 → 栏模式。/ Visual page to its column mode.
static int reader_visual_mode(size_t visual) {
    reader_map_check_sync();
    size_t start;
    int mode;
    if (!reader_visual_entry(visual, &start, &mode)) return READER_VISUAL_MODE_COLUMNS2;
    return mode;
}

// 视觉页 → 左右逻辑页；右栏不存在时为 SIZE_MAX（JOINT、单栏或落单左栏）。
// Visual page to its left and right logical pages; the right side is SIZE_MAX when
// absent (JOINT, single column, or a lone left column).
static void reader_logical_pages(size_t visual, size_t* left, size_t* right) {
    size_t start = reader_visual_start(visual);
    if (left) *left = start;
    size_t next = SIZE_MAX;
    size_t n = book_layout_page_count();
    if (n && s_page_map_columns > 1 && reader_visual_mode(visual) == READER_VISUAL_MODE_COLUMNS2) {
        // 右栏属于同一视觉页：数组路径看下一条目是否正好从 start+1 起（JOINT 或落单
        // 左栏则不是）；公式路径只看 start+1 是否越界。
        // The right column belongs to this visual page: on the array path the next entry
        // must not start at start+1 (a JOINT page or a lone left column does); the
        // formula path only checks the bound.
        if (s_page_map_degenerate) {
            if (start + 1 < n) next = start + 1;
        } else {
            size_t count = reader_visual_count();
            size_t index = visual < count ? visual : count - 1;
            if (start + 1 < n &&
                (index + 1 >= s_page_map_count || s_page_map[index + 1].start != start + 1))
                next = start + 1;
        }
    }
    if (right) *right = next;
}

// 字节偏移 → 视觉页（续读、目录与书签落点）；偏移经 book_layout 夹到末页再归视觉页。
// Byte offset to visual page (resume, TOC and bookmark landings); the offset is clamped
// to the last page by book_layout before it is folded into a visual page.
static size_t reader_visual_for_offset(size_t off) {
    return reader_visual_index(book_layout_page_for_offset(off));
}

// 任意逻辑页钳到某视觉页的起始逻辑页（= s_page 落点）。
// Clamp any logical page onto the starting logical page of its visual page (the s_page landing).
static size_t reader_clamp_visual_page(size_t logical) {
    return reader_visual_start(reader_visual_index(logical));
}

static void draw_reader_stats(uint8_t* fb) {
    const int top = 768;
    draw_sheet(fb, top, "阅读统计");
    char values[3][32];
    snprintf(values[0], sizeof(values[0]), "%u%%", percent(s_page));
    snprintf(values[1], sizeof(values[1]), "%lu 分钟", (unsigned long)(s_session_read_ms / 60000));
    snprintf(values[2], sizeof(values[2]), "%lu 次", (unsigned long)s_session_turns);
    const char* labels[] = {"阅读进度", "本次阅读", "翻页"};
    for (int i = 0; i < 3; ++i) {
        int cx = 114 + i * 228;
        ui_text_box(fb,(EpdRect){cx-100,top+100,200,48},33,values[i],EPD_DRAW_ALIGN_CENTER,false);
        ui_text_box(fb,(EpdRect){cx-100,top+154,200,36},20,labels[i],EPD_DRAW_ALIGN_CENTER,false);
        if (i < 2) ui_fill_rect_fast((EpdRect){228 + i * 228, top + 99, 1, 74}, 0x98, fb);
    }
    ui_text_box(fb,(EpdRect){54,top+192,576,42},21,"本书进度",EPD_DRAW_ALIGN_LEFT,false);
    EpdRect track = {54, top + 245, 576, 12};
    ui_fill_round_rect(fb, track, 6, 0xd0);
    EpdRect fill = track; fill.width = fill.width * (int)percent(s_page) / 100;
    if (fill.width) ui_fill_round_rect(fb, fill, 6, 0x40);
    if (book_chapter_count() == 1) {
        char left[32], right[32];
            snprintf(left, sizeof(left), "第 %u 页", (unsigned)reader_visual_index(s_page) + 1);
            snprintf(right, sizeof(right), "共 %u 页", (unsigned)reader_visual_count());
            // 视觉页换算（我方 page_map）+ 上游 r91 的面板文本助手（跟随系统字号）。
            // Visual-page maths (our page_map) plus upstream r91 panel text helper
            // (which follows the system font size).
            reader_panel_text(fb, 54, top + 272, 19, left, EPD_DRAW_ALIGN_LEFT, false);
            reader_panel_text(fb, 630, top + 272, 19, right, EPD_DRAW_ALIGN_RIGHT, false);
    } else {
        reader_panel_text(fb, 54, top + 272, 19, "当前页 —", EPD_DRAW_ALIGN_LEFT, false);
        reader_panel_text(fb, 630, top + 272, 19, "总页数 —", EPD_DRAW_ALIGN_RIGHT, false);
    }
    EpdRect details = {36, top + 319, 294, 78}, recent = {354, top + 319, 294, 78};
    ui_fill_round_rect(fb, details, 20, 0xd8); ui_draw_control_frame(fb, details, 20, 0x68);
    ui_fill_round_rect(fb, recent, 20, 0xd8); ui_draw_control_frame(fb, recent, 20, 0x68);
    char marks[32]; snprintf(marks, sizeof(marks), "查看书签 · %u", (unsigned)bookmark_count());
    ui_text_box(fb,(EpdRect){48,top+331,270,54},24,marks,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){366,top+331,270,54},24,"最近 30 天",EPD_DRAW_ALIGN_CENTER,false);
}

static void draw_reader_bookmarks(uint8_t* fb) {
    const int top = 560;
    draw_sheet(fb, top, "书签");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    reader_bookmarks_t marks = {0};
    bool valid = bookmark_load(&marks);
    int pages = bookmark_pages(valid ? marks.count : 0);
    if (s_bookmark_page >= pages) s_bookmark_page = pages - 1;
    if (valid && marks.count) {
        EpdRect manage = {544, top + 30, 104, 54};
        ui_fill_round_rect(fb, manage, 20, s_bookmark_edit ? 0xd8 : 0xe8);
        ui_draw_control_frame(fb, manage, 20, 0x78);
        reader_panel_text_vc(fb, 596, top + 57, 20, s_bookmark_edit ? "完成" : "管理", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (!valid || !marks.count) {
        reader_panel_text_vc(fb, 342, top + 260, 26, "还没有保存书签", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 309, 18, "阅读时点按“书签”即可保存当前位置", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    int first = s_bookmark_page * bookmark_rows();
    for (int row = 0; row < bookmark_rows() && first + row < marks.count; ++row) {
        int newest = (int)marks.count - 1 - (first + row);
        const reader_bookmark_entry_t* mark = &marks.entries[newest];
        EpdRect item = bookmark_row_rect(row);
        bool current = mark->chapter == s_chapter && mark->byte_off == book_layout_page_start_offset(s_page);
        bool selected = s_bookmark_edit && (s_bookmark_selected & (UINT32_C(1) << newest));
        ui_fill_round_rect(fb, item, 14, selected ? 0xc0 : current ? 0xc8 : 0xe8);
        ui_draw_control_frame(fb, item, 14, current ? 0x50 : 0x98);
        char chapter[96];
        if (book_chapter_title(mark->chapter, chapter, sizeof(chapter)) != ESP_OK)
            snprintf(chapter, sizeof(chapter), "第 %u 节", (unsigned)mark->chapter + 1);
        char where[52];
        if (mark->chapter == s_chapter)
            snprintf(where, sizeof(where), "第 %u 页", (unsigned)reader_visual_for_offset(mark->byte_off) + 1);
        else snprintf(where, sizeof(where), "章节 %u", (unsigned)mark->chapter + 1);
        int end=item.x+item.width-(s_bookmark_edit?70:20);
        int where_width=ui_text_fixed_width_px(ui_text_effective_px(20),where);
        if(where_width>220)where_width=220;
        int chapter_width=end-where_width-item.x-34;
        fit_text(chapter,24,chapter_width);
        ui_text_box(fb,(EpdRect){item.x+18,item.y+5,chapter_width,item.height/2-10},24,chapter,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){end-where_width,item.y+5,where_width,item.height/2-10},20,where,EPD_DRAW_ALIGN_RIGHT,false);
        ui_text_box(fb,(EpdRect){item.x+18,item.y+item.height/2,item.width-90,item.height/2-5},18,
                    s_bookmark_edit?"点按选择":current?"当前阅读位置":"点按跳转到此处",EPD_DRAW_ALIGN_LEFT,false);
        if (s_bookmark_edit) {
            epd_draw_circle(item.x + item.width - 35, item.y + 36, 12, 0x48, fb);
            if (selected) epd_fill_circle(item.x + item.width - 35, item.y + 36, 7, 0x38, fb);
        }
    }
    if (s_bookmark_edit) {
        EpdRect all = {36, top + 520, 258, 64}, remove = {312, top + 520, 336, 64};
        ui_draw_button(fb, all, "全选本页", false);
        char label[40]; snprintf(label, sizeof(label), "删除已选 · %u", bookmark_selected_count());
        ui_draw_button(fb, remove, label, bookmark_selected_count() > 0);
    }
    char page[32]; snprintf(page, sizeof(page), "‹   %d / %d   ›", s_bookmark_page + 1, pages);
    reader_panel_text_vc(fb, 342, top + 606, 22, page, EPD_DRAW_ALIGN_CENTER, false);
    if (s_bookmark_delete_confirm) {
        EpdRect panel = {54, top + 196, 576, 240};
        ui_fill_round_rect(fb, panel, 24, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, panel, 24, 0x48);
        reader_panel_text_vc(fb, 342, top + 252, 27,
                   s_bookmark_delete_error ? "删除失败，请重试" : "删除选中的书签？", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 298, 18, "只删除书签，不影响阅读进度", EPD_DRAW_ALIGN_CENTER, false);
        ui_draw_button(fb, (EpdRect){76, top + 348, 244, 64}, "取消", false);
        ui_draw_button(fb, (EpdRect){364, top + 348, 244, 64}, "确认删除", true);
    }
}

// 栏数（第 4 步起读设置）：横屏 + 双栏开关 = 2，其余 1。建版矩形与地图栏数的同批
// 切换由 reader_relayout 单出口保证——出口内一次栏数快照同时驱动建版矩形（栏数>1
// 即左栏矩形）与 rebuild_page_map 的栏数，两处永不失步。
// Column count (reads the setting from step 4 on): landscape plus the two-column switch
// means 2, otherwise 1. The build rect and the map's column count switch in the same
// batch through the reader_relayout single exit — one snapshot inside the exit drives
// both the columnized build rect and rebuild_page_map, so they can never drift apart.
static int reader_column_count(void) {
    int cols = app_settings_reader_landscape() && app_settings_reader_two_column() ? 2 : 1;
    return cols < 1 ? 1 : cols;
}

// 建版矩形：栏数>1 时为左栏矩形（P3 结论：take_line 按 s_rect.width 断行，建版必须
// 按栏宽建，度量/绘制同源恢复；栏数<=1 时恒等返回 body——关形态逐位不变）。
// Layout build rect: with more than one column this is the left column rect (P3's
// conclusion: take_line wraps at s_rect.width, so the layout must be built at column
// width to restore measure/draw agreement; at one column this is the identity and the
// closed form stays byte-identical).
static EpdRect reader_layout_rect_for(EpdRect body, int cols) {
    return reader_column_rect(body, cols, 0);
}
static EpdRect reader_layout_rect(void) {
    return reader_layout_rect_for(body_rect(), reader_column_count());
}

// 唯一的建版出口（做法方案 §2.4 定案，第 4 步随双栏开关落地）：建版 + 建图不许拆散。
// 栏数快照在本出口内同批驱动建版矩形（栏化统一在此进行，调用方只传版心矩形）与
// rebuild_page_map——地图与版面失步是这套模型唯一的死法。栏数<=1 时建版矩形即版心、
// 地图按 1 建图，与旧路径逐位一致。
// The single layout exit (plan section 2.4, landed with the step-4 switch): build and
// map may never be split. One column snapshot inside the exit drives both the
// columnized build rect (columnization lives here; callers pass the body rect) and
// rebuild_page_map — a map out of step with the layout is the only way this model dies.
// At one column the build rect is the body rect and the map builds at 1, matching the
// old path exactly.
static bool reader_relayout_cols(const char *text, size_t len, blk_t *blocks, size_t count,
                                 EpdRect body, int px, int cols) {
    EpdRect build_rect = reader_layout_rect_for(body, cols);
    // G7-A1 修复：出血宽随栏数快照在本出口内同批发布，先于建版——resolve_image_rect()
    // 在 build_blocks 内跑，读的必须是当次栏数下的发布值。单栏（cols<=1）维持整屏宽
    // 不变（单栏时代「栏=屏」，通栏插图语义无损）；双栏改发栏宽，插图 band 收进栏内，
    // 通栏大图不再越本栏双界、盖住对侧栏文字。发布所有权自 on_enter 收归此处。
    // G7-A1 fix: publish the bleed width at the same column snapshot inside this exit,
    // before the build — resolve_image_rect() runs inside build_blocks and must read the
    // value for this column count. One column keeps the panel width (column == screen in
    // the single-column era, so full-bleed art stays intact); two columns publish the
    // column width so the illustration band stays inside the column and full-bleed art no
    // longer crosses both column bounds and covers the facing column's text. Publish
    // ownership moved here from on_enter.
    book_layout_set_image_bleed_width(cols <= 1 ? UI_LOCK_WIDTH : build_rect.width);
    if (!book_layout_build_blocks(text, len, blocks, count, build_rect, px))
        return false;
    reader_rebuild_page_map(cols);
    return true;
}
static bool reader_relayout(EpdRect body) {
    return reader_relayout_cols(s_text, s_text_len, s_blocks, s_block_count, body, s_px,
                                reader_column_count());
}

// 预绘切片钩子（双栏休眠接线的 prep 侧；P1 侦查答案：draw_slice 一次只画一页一行，
// 双栏必须在 app 层拼两次切片）。reader_prefetch_step 以块作用域弱声明经 volatile
// 指针调它：产品构建同 TU 强定义在场，走双栏拼接；上游 139 行 harness 的链接闭包
// 不含本符号，指针为 0，步进函数自带退化分支直调 draw_slice——harness 逐字节不动。
// 右栏游标函数内自持：被抽取的十个 prep 函数不引用任何新静态，harness 前奏因此无需
// 新增桩。左栏游标代次为 0（新目标或 invalidate 后同目标重踢）即作废右栏状态，防止
// 重踢时右栏续画陈旧行。休眠期（建图栏数 1）右栏恒不存在，整个钩子逐位退化为直调
// draw_slice；双栏下落单左栏（末页或下一个是 JOINT）也只画左栏、同样走左栏矩形——
// 联合区提前返回只允许出现在 cols<=1。栏数>1 而建版矩形仍是联合区时切片宽守卫拒画
// （-1 → invalidate），预绘失败闭环而非画出错栏——「建版矩形与地图栏数同批切换」
// 由这个失败模式机械看守。
// Prefetch slice hook (the prep side of the dormant two-column wiring; P1's answer:
// draw_slice paints one row of one page per call, so two columns must be stitched from
// two slices at the app layer). reader_prefetch_step calls it through a block-scope weak
// declaration and a volatile pointer: product builds have the strong definition in this
// TU and take the two-column stitch; the upstream 139-line harness's link closure lacks
// the symbol, the pointer reads 0 and the step function's own fallback calls draw_slice
// directly — the harness stays untouched. The right cursor lives in function statics: none
// of the ten extracted prep functions reference a new static, so the harness prologue
// needs no new stubs. A left-cursor generation of 0 (a new target, or a same-target
// re-kick after invalidate) voids the right state, stopping a re-kick from resuming
// stale right rows. While dormant (map columns 1) no right column exists and the whole
// hook degenerates to the direct draw_slice call; a lone left column under two columns
// (tail page or a JOINT page next) also paints only the left side, through the left
// column rect just the same — the joint-area early return exists only at cols<=1. With
// columns>1 against a still-joint build rect the slice width guard refuses (-1 ->
// invalidate) so prefetch fails closed instead of mis-columns — "the build rect and the
// map's column count switch in the same batch" is mechanically guarded by that failure
// mode.
int reader_prep_slice(uint8_t* fb, size_t left, EpdRect area, int px,
                      book_layout_draw_cursor_t* cursor) {
    int cols = reader_column_count();
    EpdRect left_rect = reader_column_rect(area, cols, 0);
    size_t right = SIZE_MAX;
    if (cols > 1) reader_logical_pages(reader_visual_index(left), NULL, &right);
    if (right == SIZE_MAX) return book_layout_draw_slice(fb, left, left_rect, px, cursor);
    static book_layout_draw_cursor_t s_right_cursor;
    static size_t s_right_left = SIZE_MAX;
    if (!cursor->generation) s_right_left = SIZE_MAX;   // 左栏新起：右栏状态作废
    int result = book_layout_draw_slice(fb, left, left_rect, px, cursor);
    if (result != 1) return result;                     // 0 = 左栏未尽；-1 = 失败
    if (s_right_left != left) {                         // 右栏首行：重建游标
        memset(&s_right_cursor, 0, sizeof(s_right_cursor));
        s_right_left = left;
    }
    return book_layout_draw_slice(fb, right, reader_column_rect(area, cols, 1), px, &s_right_cursor);
}

static void draw_reader_stats_recent(uint8_t* fb) {
    const int top = 584;
    draw_sheet(fb, top, "最近 30 天");
    epd_draw_line(55, top + 49, 64, top + 58, UI_GRAY_BLACK, fb);
    epd_draw_line(64, top + 58, 73, top + 49, UI_GRAY_BLACK, fb);
    if (!s_recent_days_valid) {
        reader_panel_text_vc(fb, 342, top + 280, 23, "请先完成日期与时间设置", EPD_DRAW_ALIGN_CENTER, false);
        reader_panel_text_vc(fb, 342, top + 326, 18, "完成对时后开始记录每天的阅读时长", EPD_DRAW_ALIGN_CENTER, false);
        return;
    }
    uint64_t sum = 0;
    uint32_t maximum = 0;
    for (int i = 0; i < 30; ++i) {
        sum += s_recent_days[i];
        if (s_recent_days[i] > maximum) maximum = s_recent_days[i];
    }
    char total[64];
    snprintf(total, sizeof(total), "累计 %llu 小时 %llu 分钟",
             (unsigned long long)(sum / 3600), (unsigned long long)((sum / 60) % 60));
    reader_panel_text_vc(fb, 342, top + 112, 28, total, EPD_DRAW_ALIGN_CENTER, false);
    const int base_y = top + 438, max_h = 250;
    ui_hairline(fb, base_y, 42, 600, 0xb0);
    for (int i = 0; i < 30; ++i) {
        int h = maximum ? (int)((uint64_t)s_recent_days[i] * max_h / maximum) : 0;
        if (s_recent_days[i] && h < 5) h = 5;
        if (h) ui_fill_round_rect(fb, (EpdRect){47 + i * 20, base_y - h, 10, h}, 5, 0x38);
    }
    reader_panel_text(fb, 42, base_y + 22, 19, "30 天前", EPD_DRAW_ALIGN_LEFT, false);
    reader_panel_text(fb, 642, base_y + 22, 19, "今天", EPD_DRAW_ALIGN_RIGHT, false);
}

static void draw_reader_panel(uint8_t* fb) {
    if (s_reader_panel==READER_PANEL_NONE) return;
    ui_text_set_system_scale(true);
    if (s_reader_panel == READER_PANEL_TOOLS) {
        ui_fill_rect_fast((EpdRect){0, 1096, UI_LOCK_WIDTH, 120}, UI_GRAY_WHITE, fb);
        ui_hairline(fb, 1096, 0, UI_LOCK_WIDTH, 0x98);
        for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            EpdRect rect = tool_rect(i);
            int cx = rect.x + rect.width / 2;
            bool bookmarked = i == 1 && bookmark_is_current();
            draw_reader_tool_icon(fb, cx, 1138, i, bookmarked);
            // 用上游 r91 的 ui_text_box（面板跟随系统字号后按盒子排布），标签仍取我方
            // 六项表（第六项「生词本」是我方功能）。/ Upstream r91 lays the label out in
            // a box now that panels follow the system font size; labels still come from
            // our six-entry table (the sixth, 生词本, is ours).
            ui_text_box(fb,(EpdRect){rect.x+6,1159,rect.width-12,40},20,bookmarked?"已书签":k_tool_labels[i],EPD_DRAW_ALIGN_CENTER,false);
        }
    } else if (s_reader_panel == READER_PANEL_FONT_SETTINGS) draw_font_settings(fb);
    else if (s_reader_panel == READER_PANEL_WEIGHT_SETTINGS) draw_weight_settings(fb);
    else if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) draw_layout_settings(fb);
    else if (s_reader_panel == READER_PANEL_RULE_SETTINGS) draw_rule_settings(fb);
    else if (s_reader_panel == READER_PANEL_BACKGROUND) draw_background_directory(fb);
    else if (s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) draw_background_picker(fb);
    else if (s_reader_panel == READER_PANEL_FONT_PICKER) draw_font_picker(fb);
    else if (s_reader_panel == READER_PANEL_STATS) draw_reader_stats(fb);
    else if (s_reader_panel == READER_PANEL_BOOKMARKS) draw_reader_bookmarks(fb);
    else if (s_reader_panel == READER_PANEL_STATS_RECENT) draw_reader_stats_recent(fb);
    else if (s_reader_panel == READER_PANEL_REFRESH_SETTINGS) draw_reading_settings(fb);
    else if (s_reader_panel == READER_PANEL_TURN_SETTINGS) draw_turn_settings(fb);
    else if (s_reader_panel == READER_PANEL_KEY_SETTINGS) draw_key_settings(fb);
    else if (s_reader_panel == READER_PANEL_KEY_PICKER) draw_key_picker(fb);
    else if (s_reader_panel == READER_PANEL_DICT) dict_popup_draw(fb);
    // 上游 r91：面板文字跟随系统字号，画完复位；我方词典卡片走同一条 if 链。
    // Upstream r91: panel text follows the system font size and resets afterwards;
    // our dictionary card rides the same if-chain.
    ui_text_set_system_scale(false);
}

/* ---- 绘制与预渲染 / Drawing and preparation ---- */
static uint8_t inline_ink_gray(uint8_t gray) {
    // 解码器已提供灰阶；再次加深并叠加系统对比度会压死抖动前的中间色阶。
    // The decoder already supplies grayscale; extra darkening and UI contrast clip midtones.
    return gray;
}

// 收藏图标右对齐：竖屏 x=605(右端留 49)，横屏按当前逻辑宽度右对齐；绘制与点击共用，避免点空。
// Right-align the favorite icon: portrait x=605 (49px right margin); landscape right-aligns to
// the current logical width. Draw and tap share this so the hit region never drifts from the icon.
static int reader_favorite_x(void) {
    return epd_get_rotation() == EPD_ROT_LANDSCAPE ? ui_w() - 79 : 605;
}
static void draw_reader_header(uint8_t *fb) {
    ui_nav_back(fb, 36, 79);
    char header[128];
    const char *sample = s_book_title[0] ? s_book_title : s_title;
    copy_text(header, sizeof(header), sample);
    int title_px = ui_text_title_fit(header, 32, 450, sample);
    // 返回键、书名与收藏使用同一视觉中心线；字体切换不改变对齐。
    // Back, title and favorite share one visual centerline across font changes.
    ui_text_title_vc(fb, UI_LOCK_WIDTH / 2, 114, title_px, header, sample, EPD_DRAW_ALIGN_CENTER);
    draw_favorite_icon(fb, reader_favorite_x(), 96, 30, 36, s_reader_favorite, 0x38);
    ui_hairline(fb, 151, 36, ui_w() - 72, UI_GRAY_LIGHT);
}
// 竖排章节题头：右列是编号（第三章），左列是章名，读序依旧自右向左；两列整体留出列宽，
// 排版用同一个宽度在首页右边缘预留，绘制与预留因此不会错位。
// A vertical chapter heading: the number column (第三章) on the right and the title on the left,
// still read right to left. Both columns reserve their width, and the layout reserves the same
// width at the right edge of the first page, so drawing and reservation cannot drift apart.
typedef struct {
    int width;       ///< 首页右起需要预留的列宽 / columns to reserve at the first page's right
    int title_px;    ///< 章名字号 / title type size
    int title_x;     ///< 章名列左边缘 / title column's left edge
    int label_px;    ///< 编号字号，无编号时为 0 / number size, 0 without a label
    int label_x;     ///< 编号列左边缘 / label column's left edge
    int text_height; ///< 较长一列的高度，用来垂直居中 / taller column, used to center
} reader_heading_t;

static reader_heading_t reader_heading_metrics(const char *title, const char *label, EpdRect body) {
    reader_heading_t metrics = {0};
    if (title == NULL || title[0] == 0 || body.height <= 0 || body.width <= 0) return metrics;
    // 题头跟横排的题头一样走系统字体：正文换卡上字体不改变章节标题。
    // The heading uses the system face exactly like the horizontal one, so swapping the body face
    // never changes chapter titles.
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    const int avail = body.height - 48;
    int px = 48;
    int need = ttf_vertical_text_height_px(px, title, 0);
    // 一列放不下就缩字号；竖排的题头不折列，缩到 24 为止。
    // Shrink the size when one column cannot hold the title; a vertical heading does not break
    // into more columns and stops at 24.
    if (need > avail && avail > 0) {
        px = px * avail / need;
        px &= ~3;
        if (px < 24) px = 24;
    }
    int label_px = label != NULL && label[0] ? 24 : 0;
    const int gap = 12;
    metrics.title_px = px;
    metrics.label_px = label_px;
    metrics.width = px + (label_px ? label_px + gap : 0);
    // 题头最多占正文带的三分之一，留够列给正文。
    // A heading takes at most a third of the body band, leaving the rest for body columns.
    if (metrics.width > body.width / 3) {
        int scale = body.width / 3 * 100 / metrics.width;
        metrics.title_px = metrics.title_px * scale / 100;
        if (metrics.title_px < 24) metrics.title_px = 24;
        metrics.label_px = label_px ? label_px * scale / 100 : 0;
        if (metrics.label_px && metrics.label_px < 18) metrics.label_px = 18;
        metrics.width = metrics.title_px + (metrics.label_px ? metrics.label_px + gap : 0);
    }
    const int right = body.x + body.width;
    metrics.title_x = right - metrics.width;
    metrics.label_x = right - metrics.label_px;
    int title_h = ttf_vertical_text_height_px(metrics.title_px, title, 0);
    int label_h = metrics.label_px ? ttf_vertical_text_height_px(metrics.label_px, label, 0) : 0;
    metrics.text_height = title_h > label_h ? title_h : label_h;
    return metrics;
}

static void draw_vertical_heading(uint8_t *fb, EpdRect body) {
    reader_heading_t metrics = reader_heading_metrics(s_chapter_heading_title, s_chapter_heading_label, body);
    if (!metrics.width) return;
    int y = body.y + (body.height - metrics.text_height) / 2;
    if (y < body.y + 8) y = body.y + 8;
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    if (metrics.label_px)
        ttf_draw_text_px_vertical(fb, metrics.label_x, y, metrics.label_px, s_chapter_heading_label, 0, 0, 15);
    ttf_draw_text_px_vertical(fb, metrics.title_x, y, metrics.title_px, s_chapter_heading_title, 0, 0, 15);
}

// 重排前把章节题头的几何重新说给排版：竖排留右侧列宽、横排留顶部行高，
// 绘制侧读的是同一组 s_chapter_lead_*。切模式、切全屏、改字号都必须走这里，
// 否则排版按旧模式预留、题头按新模式绘制，题头就会压到正文上。
// Tell the layout the chapter heading geometry before every re-layout: columns to the right in
// vertical setting, rows at the top in horizontal. Drawing reads the same s_chapter_lead_*
// values. Switching mode or full screen or changing the type size must all come through here;
// otherwise the layout reserves for the old mode while the heading draws in the new one and lands
// on top of the body.
// 题头几何按当前模式重算：竖排留右侧列宽、横排留顶部行高。
// height 由调用方按原有规矩算好（首块是插图、或没有可用标题时为 0），这里不再自己推导——
// 无条件按 214 算会把不该出现的题头带出来，正文里那份标题就与它并排成了两个标题。
// Heading geometry for the current mode: columns to the right vertically, rows at the top
// horizontally. `height` arrives already decided by the caller (zero when the first block is an
// illustration or no title is usable); deriving it here unconditionally brought a heading onto
// sheets that should have none, leaving it side by side with the body's own copy.
static void apply_chapter_lead(const char *title, const char *label, size_t skip, unsigned height) {
    s_chapter_lead_skip = skip;
    if (book_kind() != BOOK_KIND_EPUB || title == NULL || title[0] == 0 || !height) {
        s_chapter_lead_height = s_chapter_lead_width = 0;
        book_layout_set_chapter_lead(0, 0);
        book_layout_set_chapter_lead_width(0);
        return;
    }
    if (app_settings_book_vertical()) {
        // 量竖排题头会光栅化字形，调用方须已持有绘制锁。
        // Measuring the vertical heading rasterizes glyphs; the caller must hold the draw lock.
        s_chapter_lead_height = 0;
        s_chapter_lead_width = (unsigned)reader_heading_metrics(title, label, body_rect()).width;
        book_layout_set_chapter_lead(skip, 0);
        book_layout_set_chapter_lead_width(s_chapter_lead_width);
    } else {
        s_chapter_lead_width = 0;
        s_chapter_lead_height = height;
        book_layout_set_chapter_lead(skip, s_chapter_lead_height);
        book_layout_set_chapter_lead_width(0);
    }
}

// 重排前把题头几何交给排版；调用方须持有绘制锁。所有正文重排都要走这一句，
// 否则换了模式或字号之后，排版按上一次的题头尺寸预留，题头就会压住正文。
// Hand the heading geometry to the layout before a re-layout; the caller holds the draw lock.
// Every body re-layout needs this line, otherwise a mode or size change reserves room for the
// previous heading and the heading lands on the body.
static void refresh_chapter_lead(void) {
    apply_chapter_lead(s_chapter_heading_title, s_chapter_heading_label, s_chapter_lead_skip,
                       s_chapter_lead_row_height);
}

// PR9 未知尺寸整页图没有混排槽；在调用侧适配原有位图缓存。/ PR9's unknown-size page has no inline slot; adapt the bitmap cache here.
static int reader_image_slots(size_t page) {
    int count = book_layout_page_image_count(page);
    return count ? count : book_layout_page_image(page) >= 0 ? 1 : 0;
}
static bool reader_image_slot(size_t page, int slot, int *index, int *y, int *width, int *height) {
    if (book_layout_page_image_count(page))
        return book_layout_page_image_at(page, slot, index, y, width, height);
    int image = book_layout_page_image(page);
    if (slot != 0 || image < 0) return false;
    // 未知尺寸的整页图同样通栏，不能拿正文栏宽当上限。
    // An unknown-size whole-page image bleeds as well, so the body column is not its ceiling.
    EpdRect band = book_layout_image_rect();
    if (index) *index = image;
    if (y) *y = 0;
    if (width) *width = band.width;
    if (height) *height = band.height;
    return true;
}
static void draw_reader_images(uint8_t *fb, size_t page, EpdRect body) {
    // 位图和布局代次必须匹配；预渲染另一页不能复用当前页插图。
    // Match page and layout generation; a prefetched page must never borrow current-page bitmaps.
    if (!app_settings_reader_hide_images() && page == s_page &&
        s_page_images_for == page && s_page_images_generation == book_layout_generation()) {
        // 横向按插图区域居中：通栏时图片铺满整屏，不再留正文边距。
        // Center horizontally in the illustration region: with full bleed the image spans the
        // panel and no longer carries the body margins.
        EpdRect band = book_layout_image_rect();
        for (int i = 0; i < reader_image_slots(page); ++i) {
            int y, width, height;
            if (!reader_image_slot(page, i, NULL, &y, &width, &height)) continue;
            const reader_image_t *image = s_page_images && i < s_page_image_count ? &s_page_images[i] : NULL;
            if (image && image->gray) {
                int left = band.x + (band.width - image->width) / 2;
                int top = band.y + y + (height - image->height) / 2;
                // 页首插图贴到阅读区顶端：正文上方那点呼吸空间只属于正文，不该把插图压下来。
                // A page-start illustration sits at the top of the reading area: the inset above
                // body text belongs to text and must not push the illustration down.
                if (y == 0) top = reader_area().y;

                for (int iy = 0; iy < image->height; ++iy)
                    for (int ix = 0; ix < image->width; ++ix)
                        epd_draw_pixel(left + ix, top + iy,
                            ui_image_dither_gray(inline_ink_gray(image->gray[(size_t)iy * image->width + ix]),
                                                 left + ix, top + iy), fb);
            } else if (height >= UI_PX_CAPTION + 4 && width >= 70) {
                char notice[64]; copy_text(notice, sizeof(notice), "此插图暂无法显示");
                fit_text(notice, UI_PX_CAPTION, width - 8);
                ui_text_vc(fb, body.x + body.width / 2, body.y + y + height / 2,
                           UI_PX_CAPTION, notice, EPD_DRAW_ALIGN_CENTER, false);
            }
        }
    }
}

// 正文块必须与当前显示方向同代：横屏阅读里面板是竖屏的，一开面板就要按竖屏重排、
// 关回去再按横屏重排；从书架/目录/书签进入正文时块是按进入前的方向建的，也在这里补齐。
// 只在版面尺寸真正变化时重排——竖屏模式零额外开销；页码按字节偏移重映射，阅读位置不跳。
// Body blocks must match the orientation on screen: panels are portrait, so opening one
// during landscape reading reflows to portrait and closing it reflows back; entries from
// the shelf/TOC/bookmarks build under the previous orientation and are fixed up here too.
// Reflow only when the body size actually changed — portrait mode pays nothing — and remap
// pages by byte offset so the reading position does not jump.
static size_t ensure_body_orientation(size_t page) {
    if (!s_text) return page;
    EpdRect want = reader_layout_rect();
    EpdRect built = book_layout_body_rect();
    if (built.width == want.width && built.height == want.height) return page;
    size_t page_off = reader_page_offset(page < book_layout_page_count() ? page : 0);
    bool jump_page = s_jump_offset != SIZE_MAX && page == s_jump_page;
    size_t current_off = reader_page_offset(s_page < book_layout_page_count() ? s_page : 0);
    if (!reader_relayout(body_rect())) {
        // 失败就回滚旧版面：尺寸守卫至少放过这一帧，不会画出错版文字。/ Roll back to the
        // previous layout on failure: the size guard still lets this frame through.
        // 出血宽按旧建版的栏数同批回滚发布（s_page_map_columns 与 built 同批，规则同
        // reader_relayout_cols 出口）：band 由 build_blocks 内的 resolve_image_rect 现推，
        // 漏发布会让插图 band 挂在错栏数下的值，版面回滚了 band 没回滚。
        // The rollback republishes the bleed width at the old build's column count in the
        // same batch (s_page_map_columns shipped with built; same rule as the
        // reader_relayout_cols exit): the band is derived inside build_blocks by
        // resolve_image_rect, so a missed publish leaves the band at the wrong column
        // count while the layout rolled back.
        (void)book_layout_set_image_bleed_width(s_page_map_columns <= 1 ? UI_LOCK_WIDTH
                                                                       : built.width);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, built, s_px);
        // 回滚后版面与进函数时一致；地图按旧建版的栏数重建（s_page_map_columns 与
        // built 同批），不跟当前设置走——否则地图-版面失步。
        // The rollback restores the entry layout; the map rebuilds at the old build's
        // column count (s_page_map_columns shipped with `built`), never at the current
        // setting — a mismatched count would desync map and layout.
        reader_rebuild_page_map(s_page_map_columns);
        copy_text(s_reader_notice, sizeof(s_reader_notice), "排版失败，请重试");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
        return page;
    }
    // 地图已在 reader_relayout 单出口内按同一栏数快照重建（建版+建图同批）。
    // The map already rebuilt inside the reader_relayout single exit at the same column
    // snapshot (build and map in one batch).
    size_t count = book_layout_page_count();
    size_t mapped = book_layout_page_for_offset(page_off);
    if (count && mapped >= count) mapped = count - 1;
    size_t current = book_layout_page_for_offset(current_off);
    if (count && current >= count) current = count - 1;
    s_page = reader_clamp_visual_page(current);
    size_t landing = reader_clamp_visual_page(mapped);
    if (jump_page) s_jump_page = landing;
    // 预渲染帧属于旧分页：作废，下一次 kick 按新分页重来。/ A prefetched frame belongs to
    // the old pagination: drop it and let the next kick redo it.
    invalidate_prep();
    return landing;
}
static void draw_reader_outer(uint8_t *fb,size_t page) {
    ui_text_set_system_scale(false);
    if (!(s_reader_fullscreen && app_settings_reader_immersive())) ui_nav_status(fb);
    if (!s_reader_fullscreen) draw_reader_header(fb);
    EpdRect body=body_rect();
    if (page == 0 && s_chapter_heading_title[0] &&
        (s_chapter_lead_height || s_chapter_lead_width)) {
        if (app_settings_book_vertical()) {
            // 竖排题头站在首页预留的那条列里，正文从它左边接着排。
            // The vertical heading stands in the columns the first page reserved; the body
            // continues to its left.
            draw_vertical_heading(fb, body);
        } else {
            char heading[128]; copy_text(heading, sizeof(heading), s_chapter_heading_title);
            char label[sizeof(s_chapter_heading_label)]; copy_text(label, sizeof(label), s_chapter_heading_label);
            // 题头一期只占左栏（决策⑥）：双栏时中心、fit 宽度与装饰线全部改走左栏几何；
            // 单栏三项保持既有表达式——balanced 收齐与奇数字号舍入会让「看起来等价」的
            // 改写产生 ±1px 漂移，关形态恒等不许这种漂移（装饰线的 210/264 是肖像硬值，
            // 横屏下本就 stale，本步不借双栏顺手改它）。
            // The heading takes the left column only for now (decision 6): two columns
            // move the center, fit width and rule onto the left column's geometry while
            // single column keeps the existing expressions — balanced rounding and odd
            // font-size rounding make "equivalent" rewrites drift by ±1px, which the
            // closed-form identity does not allow (the rule's 210/264 are portrait
            // constants, already stale in landscape; this step does not smuggle a fix
            // in through the two-column work).
            int center_x = UI_LOCK_WIDTH / 2, fit_w = ui_content_width();
            int rule_x = 210, rule_w = 264;
            int cols = reader_column_count();
            if (cols > 1) {
                EpdRect col0 = reader_column_rect(body, cols, 0);
                center_x = col0.x + col0.width / 2;
                fit_w = col0.width;
                rule_w = col0.width < 264 ? col0.width : 264;
                rule_x = center_x - rule_w / 2;
            }
            int heading_px = ui_text_title_fit(heading, 48, fit_w, s_chapter_heading_title);
            int label_px = ui_text_title_fit(label, 24, fit_w, s_chapter_heading_label);
            ui_text_title_vc(fb, center_x, body.y + 29, label_px, label,
                              s_chapter_heading_label, EPD_DRAW_ALIGN_CENTER);
            ui_text_title_vc(fb, center_x, body.y + 112, heading_px, heading,
                              s_chapter_heading_title, EPD_DRAW_ALIGN_CENTER);
            ui_hairline(fb, body.y + 174, rule_x, rule_w, UI_GRAY_LIGHT);
        }
    }
    if (s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW)
        ui_reader_background_paint(fb, (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT}, true);
    draw_reader_images(fb, page, body);
    if (s_reader_fullscreen) {
        // Full-book progress uses the last few screen rows and no footer text.
        EpdRect track = {36, READER_FULLSCREEN_PROGRESS_TOP + 3, UI_LOCK_WIDTH - 72, 3};
        ui_fill_round_rect(fb, track, 1, 0x90);
        track.width = track.width * (int)percent(page) / 100;
        if (track.width) ui_fill_round_rect(fb, track, 1, UI_GRAY_BLACK);
    } else {
        EpdRect track = progress_rect();
        EpdRect bar = {track.x, track.y + 16, track.width, 12};
        ui_fill_round_rect(fb, bar, 5, UI_GRAY_LIGHT);
        bar.width = bar.width * (int)percent(page) / 100;
        if (bar.width) ui_fill_round_rect(fb, bar, 5, UI_GRAY_BLACK);
        char chapter_name[72] = {0};
        reader_footer_chapter_name(chapter_name, sizeof(chapter_name));
        const int footer_px = 24;
        char progress[48];
        // 页码按视觉页计：双栏时一屏两页，总数不再"书变厚了"。
        // Page numbers count visual pages: two columns halve the total instead of fattening the book.
        snprintf(progress, sizeof(progress), "%u/%u · 全书 %u%%",
                 (unsigned)reader_visual_index(page) + 1, (unsigned)reader_visual_count(), percent(page));
        char chapter_line[192];
        snprintf(chapter_line, sizeof(chapter_line), "%s%s",
                 s_save_failed ? "未保存 · " : "", chapter_name);
        fit_fixed_text(chapter_line, footer_px,
                       track.width - ui_text_fixed_width_px(footer_px, progress) - 16);
        // 页码和全书进度靠右固定，长章节名仅截短左侧；字号不跟随正文设置。
        // Keep page and book progress right-aligned; trim only long chapter titles and ignore body font settings.
        ui_text_fixed(fb, track.x, track.y + 35, footer_px, chapter_line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, track.x + track.width, track.y + 35, footer_px,
                      progress, EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (s_reader_notice[0]) {
        // 气泡跟随屏幕宽度居中：竖屏(684)与原坐标一致，横屏阅读时仍在视觉中心。
        // Center the toast on the screen width: identical to the old coordinates in
        // portrait (684) and visually centered during landscape reading.
        EpdRect notice = {(ui_w() - 248) / 2, 160, 248, 48};
        ui_fill_round_rect(fb, notice, 24, 0xd0);
        ui_text_vc(fb, ui_w() / 2, 184, 19, s_reader_notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    draw_reader_panel(fb);
}
static void draw_reader(uint8_t *fb,size_t page) {
    // 先同步方向再取正文矩形：翻横屏/开合面板后的第一帧就在这里重排。
    // Sync the orientation before taking the body rect: the first frame after an
    // orientation flip or a panel open/close reflows right here.
    page = ensure_body_orientation(page);
    ui_text_set_system_scale(false);ui_clear_page(fb);
    // 视觉页寻址：单栏下左栏即传入页、右栏不存在，与旧路径逐位一致；双栏时同一入口
    // 给出两栏逻辑页，右栏按栏矩形增绘（本步休眠：cols==1 时右栏分支与 JOINT 分支
    // 都退化成与旧路径相同的单次 draw_page）。JOINT 页独占视觉页，draw_page 对它
    // 本是 no-op，整页插图由 draw_reader_images 铺满联合区。
    // Visual addressing: single column yields the passed page as the left column with no
    // right side, matching the old path exactly; two columns resolve both logical pages
    // here and the right column paints into its own column rect (dormant this step: at
    // cols==1 both the right-column and JOINT branches degenerate to the same single
    // draw_page as the old path). A JOINT page owns its visual page alone, draw_page is
    // a no-op for it, and the whole-page image fills the joint area via
    // draw_reader_images.
    size_t left = page, right = SIZE_MAX;
    int mode = READER_VISUAL_MODE_COLUMNS2;
    if (page < book_layout_page_count()) {
        size_t visual = reader_visual_index(page);
        reader_logical_pages(visual, &left, &right);
        mode = reader_visual_mode(visual);
    }
    EpdRect area = body_rect();
    int cols = reader_column_count();
    if (mode == READER_VISUAL_MODE_JOINT) {
        book_layout_draw_page(fb, left, area, s_px);
    } else {
        book_layout_draw_page(fb, left, reader_column_rect(area, cols, 0), s_px);
        if (cols > 1 && right != SIZE_MAX)
            book_layout_draw_page(fb, right, reader_column_rect(area, cols, 1), s_px);
    }
    draw_reader_outer(fb, page);
}
// 预渲染目标解析：给定当前逻辑页，求「下一视觉页」的左右逻辑页。
// Resolve the prefetch target: from the current logical page, the next visual page's
// left and right logical pages.
//
// 弱符号钩子（M1-M4 寻址改形的机械基础）：强定义在本 TU（page_map 在场），prep 决策
// 函数（kick_prep / reader_prefetch_idle）以块作用域弱声明调用它——产品构建走映射
// 路径；上游 139 行 pthread harness 的链接闭包不含 page_map 符号，弱声明地址为 0，
// 退化为上游 s_page+1 语义，harness 逐字节不动保持绿。单栏下 left≡s_page+1、
// right≡SIZE_MAX，谓词集合与上游逐位相同（关形态恒等）。
// 调用点经 volatile 函数指针取址：同 TU 强定义在场时 GCC 判「地址永真」
// （-Waddress，-Werror 即挂，产品构建已实证），volatile 中介防常量折叠；符号缺席
// 时指针为 0，走调用点自带的退化分支。两场景（在场/缺席）均经宿主编译 + 运行实测。
// Weak-symbol hook (the mechanical basis of the M1-M4 addressing change): the strong
// definition lives in this TU (page_map present); the prep decision functions
// (kick_prep / reader_prefetch_idle) call it via a block-scope weak declaration —
// product builds take the mapped path; the upstream 139-line pthread harness's link
// closure has no page_map symbol, the weak address is 0, and it degrades to the
// upstream s_page+1 semantics with the harness untouched. Single column yields
// left≡s_page+1 and right≡SIZE_MAX, so the predicate set is bit-identical to upstream
// (closed-form identity). Callers take the address through a volatile function
// pointer: with the strong definition in the same TU GCC proves the address always
// true (-Waddress, fatal under -Werror — observed in a real product build), and the
// volatile indirection defeats that folding; with the symbol absent the pointer is 0
// and the caller's own fallback branch runs. Both scenarios were verified by host
// compilation and execution.
bool reader_prep_target(size_t page, size_t* left, size_t* right) {
    size_t visual = reader_visual_index(page) + 1;
    if (visual >= reader_visual_count()) return false;
    reader_logical_pages(visual, left, right);
    return true;
}
// 预绘只在同步推屏期间借用字体；结束前逐行停止并交还，输入回调不等待整页预绘。
// Prefetch borrows fonts only during synchronous output; stop between rows before returning, never joining a whole page for input.
#define BOOK_PREP_RESERVE (1024u * 1024u)
#define BOOK_PREP_IDLE_US 4000
static int reader_prefetch_step(void) {
    if(!s_next_fb||s_prep_page<0)return -1;
    if(!s_prep_cursor.generation)memset(s_next_fb,0xff,fb_bytes());
    EpdRect area=body_rect();
    // 双栏切片经弱钩子（本步休眠接线）：产品构建把右栏拼进同一帧（钩子强定义见
    // reader_prep_slice）；上游 harness 链接闭包无钩子符号，volatile 指针取到 0，
    // 直调 draw_slice——参数与逐位行为跟改形前一致，上游断言原样成立。
    // Two-column slicing goes through the weak hook (this step's dormant wiring):
    // product builds stitch the right column into the same frame (the hook's strong
    // definition, reader_prep_slice); the upstream harness's link closure has no hook
    // symbol, the volatile pointer reads 0 and draw_slice is called directly — same
    // arguments, bit-identical behavior, upstream assertions hold as before.
    extern int reader_prep_slice(uint8_t* fb,size_t left,EpdRect area,int px,
                                 book_layout_draw_cursor_t* cursor) __attribute__((weak));
    int (* volatile slice_hook)(uint8_t*,size_t,EpdRect,int,book_layout_draw_cursor_t*)=reader_prep_slice;
    int (*slice)(uint8_t*,size_t,EpdRect,int,book_layout_draw_cursor_t*)=slice_hook;
    int result=slice?slice(s_next_fb,(size_t)s_prep_page,area,s_px,&s_prep_cursor)
                    :book_layout_draw_slice(s_next_fb,(size_t)s_prep_page,area,s_px,&s_prep_cursor);
    if(result<0)invalidate_prep();
    else if(result>0){s_next_page=s_prep_page;s_next_generation=book_layout_generation();}
    return result;
}
static bool reader_prefetch_buffer(void) {
    size_t need=fb_bytes();
    if(!s_next_fb&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>need+BOOK_PREP_RESERVE)
        s_next_fb=heap_caps_aligned_alloc(16,need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return s_next_fb!=NULL;
}
static void prep_task(void *arg) {
    (void)arg;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        lock_draw();
        while(!atomic_load_explicit(&s_prep_stop,memory_order_acquire)){
            if(reader_prefetch_step()!=0)break;
        }
        unlock_draw();
        xSemaphoreGive(s_prep_done);
    }
}
static bool kick_prep(void) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||
       book_layout_page_image_count(s_page)||app_settings_book_background_path()[0])return false;
    // 目标视觉页走地图（M1-M4）：产品构建经弱钩子取下一视觉页的左右逻辑页；harness
    // 链接闭包无 page_map 符号时经 volatile 指针取到 0，退化为上游 s_page+1。末页
    // （M4）与图片守卫（M3）随映射一起收口——JOINT 配对下右栏有图同样拒预绘。
    // The target visual page resolves through the map (M1-M4): product builds take the
    // next visual page's left/right logical pages via the weak hook; the harness reads
    // 0 through the volatile pointer when its link closure lacks page_map and degrades
    // to upstream s_page+1. The last-page (M4) and image (M3) guards close over the
    // mapping together — under a JOINT pair an image in the right column refuses
    // prefetch just the same.
    extern bool reader_prep_target(size_t page,size_t *left,size_t *right) __attribute__((weak));
    bool (* volatile resolve_hook)(size_t,size_t*,size_t*)=reader_prep_target;
    bool (*resolve)(size_t,size_t*,size_t*)=resolve_hook;
    size_t left,right;
    if(resolve){if(!resolve(s_page,&left,&right))return false;}
    else{if(s_page+1>=book_layout_page_count())return false;left=(size_t)s_page+1;right=SIZE_MAX;}
    if(book_layout_page_image_count(left)||(right!=SIZE_MAX&&book_layout_page_image_count(right)))return false;
    int target=(int)left;
    if(s_next_page==target&&s_next_generation==book_layout_generation())return false;
    if(s_prep_page!=target||s_prep_cursor.generation!=book_layout_generation()){
        s_next_page=-1;s_next_generation=0;s_prep_page=target;memset(&s_prep_cursor,0,sizeof(s_prep_cursor));
    }
    if(!reader_prefetch_buffer())return false;
    if(!s_draw_lock)s_draw_lock=xSemaphoreCreateMutex();
    if(!s_prep_done)s_prep_done=xSemaphoreCreateBinary();
    if(!s_prep_task&&s_draw_lock&&s_prep_done&&
       xTaskCreatePinnedToCore(prep_task,"book_prep",12*1024,NULL,3,&s_prep_task,1)!=pdPASS)s_prep_task=NULL;
    if(!s_prep_task)return false;
    xSemaphoreTake(s_prep_done,0);
    atomic_store_explicit(&s_prep_stop,false,memory_order_release);
    xTaskNotifyGive(s_prep_task);
    return true;
}
static void reader_prefetch_end(bool started) {
    if(!started)return;
    atomic_store_explicit(&s_prep_stop,true,memory_order_release);
    // 最多交还当前一行，之后主循环才可改字体、章节或离页，避免竞态。
    // Hand back at the current row boundary before the loop may change fonts, chapters or pages.
    xSemaphoreTake(s_prep_done,portMAX_DELAY);
}
static bool reader_prefetch_resume(size_t page) {
    if(!s_next_fb||s_prep_page!=(int)page||!s_prep_cursor.started||
       s_prep_cursor.generation!=book_layout_generation())return false;
    // 连按到尚未完成的目标时接着画，保留已画文字，不清空重画整页。
    // A rapid turn completes the remaining rows of its target, retaining work already drawn.
    while(reader_prefetch_step()==0){}
    return s_next_page==(int)page&&s_next_generation==book_layout_generation();
}
static void reader_prefetch_idle(app_ctx_t *ctx) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||
       ctx->consumed||ctx->request_app||ctx->request_menu||ctx->request_return||
       (ctx->touch&&ctx->touch->touched))return;
    // 激活条件与图片/末页守卫同步映射（M2）：与 kick_prep 同公式，否则 idle 永不激活
    // 或错激活；harness 经同一 volatile 指针走退化（s_page+1）。
    // The activation condition and image/last-page guards map in step (M2): same formula
    // as kick_prep, or idle never activates or activates wrongly; the harness degrades
    // the same way (s_page+1) through the same volatile pointer.
    extern bool reader_prep_target(size_t page,size_t *left,size_t *right) __attribute__((weak));
    bool (* volatile resolve_hook)(size_t,size_t*,size_t*)=reader_prep_target;
    bool (*resolve)(size_t,size_t*,size_t*)=resolve_hook;
    size_t left,right;
    if(resolve){if(!resolve(s_page,&left,&right))return;}
    else{if(s_page+1>=book_layout_page_count())return;left=(size_t)s_page+1;right=SIZE_MAX;}
    if(s_prep_page!=(int)left)return;
    if(app_settings_book_background_path()[0]||book_layout_page_image_count(left)||
       (right!=SIZE_MAX&&book_layout_page_image_count(right)))return;
    if(s_next_page==s_prep_page&&s_next_generation==book_layout_generation())return;
    if(!reader_prefetch_buffer())return;
    const int64_t deadline=esp_timer_get_time()+BOOK_PREP_IDLE_US;
    for(unsigned row=0;row<8;++row){
        if(reader_prefetch_step()!=0||esp_timer_get_time()>=deadline)break;
    }
}
static size_t current_toc_position(void) {
    size_t count = book_navigation_count();
    if (s_selected_toc < count && book_navigation_chapter(s_selected_toc) == s_chapter)
        return s_selected_toc;
    size_t selected = SIZE_MAX;
    size_t best_chapter = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t chapter = book_navigation_chapter(i);
        if (chapter > s_chapter) break;
        if (selected == SIZE_MAX || chapter > best_chapter) { selected = i; best_chapter = chapter; }
    }
    return selected;
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    // 阅读横屏：仅 READING 视图按设置切到横屏，其它视图(书架/TOC/编辑/导入等)保持竖屏。
    // 方向只在视图切换时改变，而视图切换必走 PAGE/FULL 全刷——全刷会重绘整帧并隐藏方向翻转，
    // 翻页的局部差分刷沿用上次方向，与 epdiy 的 _inverse_rotated_area(横屏为恒等)天然一致。
    // Landscape reading: only the READING view switches per setting; every other view stays
    // portrait. Orientation changes only on view switches, which are always PAGE/FULL full
    // refreshes that repaint the whole frame and hide the flip; page-turn partial diffs keep
    // the last orientation, matching epdiy's _inverse_rotated_area (identity under landscape).
    // 只有"纯阅读正文"与"点词释义卡"走横屏；工具栏/设置/统计/书签等面板保持竖屏(像素级不变、
    // 零触摸映射风险)，打开时借全刷翻回竖屏、关闭翻回横屏。释义卡是阅读最高频操作，单独保持横屏。
    // Only the plain body and the tap-to-lookup card go landscape; toolbar/settings/stats/bookmark
    // panels stay portrait (pixel-identical, zero touch-mapping risk), flipping back on a full
    // refresh when opened. The lookup card is the highest-frequency reading action, so it stays landscape.
    enum EpdRotation want = (s_view == READING && app_settings_reader_landscape() &&
                             (s_reader_panel == READER_PANEL_NONE || s_reader_panel == READER_PANEL_DICT))
                                ? EPD_ROT_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT;
    if (epd_get_rotation() != want) epd_set_rotation(want);
    ui_text_set_system_scale(s_view != READING && s_view != TOC);
    lock_draw();
    ui_text_set_system_scale(s_view != READING);
    if (s_view == EDIT) { draw_editor(fb); unlock_draw(); return; }
    if (s_view == SEARCH) { draw_search(fb); unlock_draw(); return; }
    if (s_view == IMPORT) { draw_import(fb); unlock_draw(); return; }
    if (s_view == BULK) { draw_bulk(fb, ctx->leaf); unlock_draw(); return; }
    if (s_view == MANAGE) {
        ui_clear_page(fb);
        ui_nav_status(fb);
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "图书详情", EPD_DRAW_ALIGN_CENTER, false);
        ui_hairline(fb, 176, 36, 612, 0x70);
        draw_manage(fb);
        unlock_draw();
        return;
    }
    if (s_view == TOC) {
        book_toc_render(fb, s_book_title[0] ? s_book_title : s_title,
                        book_navigation_count(), current_toc_position(), ctx->leaf, s_message);
        if (s_toc_jump_open)
            book_toc_render_jump(fb, book_navigation_count(), s_toc_jump_percent);
        unlock_draw();
        return;
    }
    if (s_view == READING && s_text) {
        draw_reader(fb, s_page);
        unlock_draw();
        return;
    }
    ui_clear_page(fb);
    // 普通和水波纹共享灰阶画面，只有快刷使用黑白底色。
    // Normal and ripple share gray artwork; only fast refresh uses the BW background.
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP},
                  app_settings_main_fast_refresh() ? 0xf0 : 0xe0, fb);
    ui_nav_status(fb);
    char category_title[BOOK_STORE_PATH_MAX];
    copy_text(category_title,sizeof(category_title),s_category_selected ? shelf_category_name(s_category_selected) : "书架");
    fit_text(category_title,52,326);
    ui_text(fb, 36, 90, 52, category_title, EPD_DRAW_ALIGN_LEFT, false);
    int title_width=ui_text_fixed_width_px(ui_text_effective_px(52),category_title);
    ui_draw_icon(fb,36+title_width+27,120,26,
                 s_category_menu ? UI_ICON_CHEVRON_UP : UI_ICON_CHEVRON_DOWN,UI_GRAY_BLACK);
    draw_shelf_header_button(fb, shelf_manage_rect(), false);
    draw_shelf_header_button(fb, shelf_import_rect(), true);
    if (s_message[0] || s_shelf_warning[0])
        ui_text(fb, UI_MARGIN, 991, 17, s_message[0] ? s_message : s_shelf_warning,
                EPD_DRAW_ALIGN_LEFT, false);
    if (app_settings_shelf_style() == 5) {
        ui_fill_round_rect(fb, (EpdRect){36,184,612,788}, 20, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, (EpdRect){36,184,612,788}, 20, 0x50);
    }
    for (int row = 0; row < shelf_rows(); ++row) {
        int i = ctx->leaf * shelf_rows() + row;
        if (i >= s_visible_count) break;
        EpdRect r = row_rect(row);
        char name[128]; copy_text(name, sizeof(name), s_shelf[i].name);
        // 封面抽出只改变书本位置，层板及触摸目标保持原位。
        // Lifting changes the book position only; shelf furniture and touch targets stay put.
        if (s_pressed_control == row)
            r.y -= app_settings_shelf_style() == 5 ? SHELF_LIST_BOUNCE_PX : SHELF_BOOK_LIFT_PX;
        draw_shelf_cover(fb, r, row, name, s_shelf[i].favorite);
        if (app_settings_shelf_style() == 5) draw_list_row(fb, row, &s_shelf[i]);
    }
    draw_shelf_furniture(fb);
    draw_shelf_pager(fb, s_visible_count, ctx->leaf, leaves());
    ui_nav_draw(fb, 1);
    if (s_category_menu) draw_category_menu(fb);
    unlock_draw();
}
static EpdRect reader_status_area(void) { return (EpdRect){0,0,UI_LOCK_WIDTH,80}; }
static bool reader_status_visible(void) {
    return s_view==READING && s_text && !(s_reader_fullscreen && app_settings_reader_immersive());
}
static void reader_status_paint(uint8_t *fb) {
    EpdRect area=reader_status_area();
    ui_fill_rect_fast(area,UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    if (s_reader_panel!=READER_PANEL_BACKGROUND_PREVIEW && app_settings_book_background_path()[0])
        ui_reader_background_paint(fb,area,true);
}
static app_redraw_t reader_clock_tick(app_ctx_t *ctx) {
    if (!reader_status_visible() || (ctx->touch && ctx->touch->touched)) return APP_REDRAW_NONE;
    if (ui_nav_clock_minute()==s_reader_clock_minute) return APP_REDRAW_NONE;
    s_reader_clock_painted_minute=ui_nav_clock_minute();
    lock_draw(); reader_status_paint(ctx->fb); unlock_draw();
    s_reader_clock_pending=true;
    return APP_REDRAW_AREA;
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (s_reader_clock_pending) {
        s_reader_clock_pending=false;
        if (redraw==APP_REDRAW_AREA && reader_status_visible()) {
            // 时钟单独短刷，不准备插图、不预排正文，也不累计正文全刷次数。
            // Update status alone, without image preparation, pagination, or reader cleanup counters.
            enum EpdDrawError err=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,reader_status_area());
            if (err==EPD_DRAW_SUCCESS) s_reader_clock_minute=s_reader_clock_painted_minute;
            guard_draw_result(ctx->hl,err);
            return true;
        }
    }
    // 输入不准备封面或正文；候选停顿只整理词栏，不累计整页清屏次数。
    // Input skips cover/body preparation; idle candidate settling affects only its strip and not whole-page cleanup counting.
    if (redraw == APP_REDRAW_AREA && (s_view == EDIT || s_view == SEARCH)) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_area));
            s_input_settle = false;
            s_du_count = 0;
            return true;
        }
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_mode == MODE_DU ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, s_mode, s_area));
        s_du_count = 0;
        return true;
    }
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE || ctx->request_app) return true;
    if (s_presented_view != (int)s_view && redraw == APP_REDRAW_AREA) redraw = APP_REDRAW_PAGE;
    if ((s_view == SHELF || s_view == MANAGE) &&
        !(redraw == APP_REDRAW_AREA && (s_shelf_feedback_pending || s_cover_batch_present_pending))) {
        prepare_covers(ctx);
        gather_cover_entry(ctx);
    }
    if (ctx->request_app) return true;
    if (s_view == READING && s_text) {
        if (s_reader_panel != READER_PANEL_BACKGROUND_PREVIEW)
            (void)ui_reader_background_prepare(app_settings_book_background_path());
        prepare_inline_image();
    }
    int64_t start = esp_timer_get_time();
    const int64_t frame_minute=ui_nav_clock_minute();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) render(ctx, ctx->fb);
    bool prep = kick_prep();
    int64_t drawn = esp_timer_get_time();
    // 本地细点阵挡板已是黑白目标，不注册灰区，省去暂存和第二轮GL16。
    // Local fine-dot guards are already BW targets; omit gray bands to skip staging and the second GL16 pass.
    if (s_view == SHELF && s_shelf_page_pending) display_main_transition_shelf_page();
    s_shelf_page_pending = false;
    enum EpdDrawError err;
    bool image_gray_refresh = s_reader_image_refresh_pending && s_view == READING &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm;
    if (image_gray_refresh && redraw == APP_REDRAW_AREA && !s_reader_cleanup && s_presented_view == (int)READING) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = app_settings_reader_turn_effect() == 1
            ? update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, reader_area())
            : update_display_area_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16, reader_area());
        if (err == EPD_DRAW_SUCCESS && s_reader_footer_pending)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, progress_rect());
        if (err == EPD_DRAW_SUCCESS && s_reader_fullscreen)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
    }
    else if (image_gray_refresh) err = app_settings_reader_turn_effect() == 1 &&
        redraw != APP_REDRAW_FULL && !s_reader_cleanup
        ? update_display_mode_diff(ctx->hl, MODE_GL16)
        : update_display_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16);
    else if (redraw == APP_REDRAW_FULL || s_reader_cleanup) err = update_display_full(ctx->hl);
    else if (redraw == APP_REDRAW_AREA && s_view == SHELF && s_shelf_feedback_pending) {
        // 抽出与复位只驱动发生变化的像素，避免整张封面被反复压黑。
        // Lift and restore only changed pixels, avoiding a dark pulse over the whole cover.
        err = update_display_area_diff_with(ctx->hl,
            s_mode == MODE_DU ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, s_mode, s_area);
    }
    else if (redraw == APP_REDRAW_AREA) {
        // 快档每次显式设置并在推屏后恢复，不能把阅读节拍带入主页水波纹。
        // Explicitly select fast pacing per turn and restore afterward, without leaking reader timing into main-page ripple.
        const int prior_tick = s_water_turn_pending ? e0470_page_turn_tick_us() : 0;
        if (s_water_turn_pending) e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        // 翻页动画与页脚刷新独立：全屏没有页脚，仍使用用户选择的水波纹。
        // The turn effect is independent of the footer: full-screen turns still use the selected water effect.
        err = s_water_turn_pending
            ? update_display_water_turn(ctx->hl, s_area, s_water_turn_dir)
            // 普通翻页与全屏阅读都只驱动变化像素；设置面板等局部绘制保留原路径。
            // Ordinary turns and full-screen reading drive changed pixels only; other local UI keeps its path.
            : (s_reader_fullscreen || s_reader_turn_pending) && s_mode == MODE_GL16
                ? update_display_area_diff_with(ctx->hl,
                    s_reader_turn_pending ? display_reader_text_waveform() : &E0470_WAVEFORM, s_mode, s_area)
                : update_display_area_with(ctx->hl, &E0470_WAVEFORM, s_mode, s_area);
        if (s_water_turn_pending) e0470_page_turn_set_tick_us(prior_tick);
        if (s_reader_fullscreen && err == EPD_DRAW_SUCCESS) {
            // 百分比不变时差分直接跳过；变化时只驱动最底部的少量像素。
            // Skip unchanged percentages; otherwise update only the small bottom strip.
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
        }
        if (s_reader_footer_pending) {
            // 普通翻页页脚只驱动变化像素；全刷由上方整屏分支一次完成。
            // Ordinary turns drive only changed footer pixels; the full-screen branch handles cleanup at once.
            err = (enum EpdDrawError)(err | update_display_area_with(ctx->hl, &E0470_FOLLOW_WAVEFORM,
                                                                     MODE_DU, progress_rect()));
            ESP_LOGI(TAG, "reader regions body_h=%d footer=diff pct=%u", s_area.height, percent(s_page));
        }
    }
    else err = s_view == READING
        ? update_display_mode_diff(ctx->hl, APP_PAGE_REFRESH_MODE)
        : update_display_fast_page(ctx->hl);
    int64_t displayed = esp_timer_get_time();
    reader_prefetch_end(prep);
    if (redraw == APP_REDRAW_AREA && s_mode == MODE_DU &&
        !s_reader_cleanup && !image_gray_refresh && !s_shelf_feedback_pending) {
        s_du_area = s_du_count ? ui_rect_union(s_du_area, s_area) : s_area;
        ++s_du_count;
        s_du_ms = esp_timer_get_time() / 1000;
    } else s_du_count = 0;
    ESP_LOGI(TAG, "present draw=%lld display=%lld join=%lld ms", (drawn - start) / 1000,
             (displayed - drawn) / 1000, (esp_timer_get_time() - displayed) / 1000);
    guard_draw_result(ctx->hl, err);
    if ((s_view==SHELF || s_view==MANAGE) && err==EPD_DRAW_SUCCESS) {
        if ((redraw==APP_REDRAW_PAGE || redraw==APP_REDRAW_FULL) && shelf_cover_frame_ready()) s_cover_dirty_mask=0;
        else if (s_cover_batch_present_pending) s_cover_dirty_mask &= ~s_cover_present_mask;
    }
    s_cover_present_mask=0;
    if (err==EPD_DRAW_SUCCESS && reader_status_visible() &&
        (redraw==APP_REDRAW_PAGE || redraw==APP_REDRAW_FULL || s_reader_cleanup))
        s_reader_clock_minute=frame_minute;
    s_reader_text_frame = err == EPD_DRAW_SUCCESS && s_view == READING && s_text &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm &&
        book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_reader_cleanup = false;
    s_reader_image_refresh_pending = false;
    s_presented_view = (int)s_view;
    s_reader_footer_pending = false;
    s_water_turn_pending = false;
    s_shelf_feedback_pending = false;
    s_cover_batch_present_pending = false;
    s_reader_turn_pending = false;
    s_mode = MODE_GL16;
    return true;
}
static app_redraw_t paint_reading(app_ctx_t* ctx, enum EpdDrawMode mode) {
    s_water_turn_pending = false;
    s_reader_turn_pending = false;
    int64_t started = esp_timer_get_time();
    (void)ui_reader_background_prepare(app_settings_book_background_path());
    prepare_inline_image();
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() &&
        book_layout_page_image_count(s_page) > 0 && s_page_images;
    lock_draw();
    bool cached = !s_toolbar && !s_clear_confirm && s_next_fb &&
                  ((s_next_page == (int)s_page && s_next_generation == book_layout_generation()) ||
                   reader_prefetch_resume(s_page));
    if (cached) {
        memcpy(ctx->fb, s_next_fb, fb_bytes());
        draw_reader_outer(ctx->fb,s_page);
    }
    else draw_reader(ctx->fb, s_page);
    // 下一页可能预渲染于数分钟前，复制后以当前时间重绘状态栏。
    // A prefetched page can be minutes old; replace its status with the current time.
    if (cached && reader_status_visible()) reader_status_paint(ctx->fb);
    unlock_draw();
    ESP_LOGI(TAG, "paint cached=%d ms=%lld", cached, (esp_timer_get_time() - started) / 1000);
    s_area = reader_area();
    s_reader_footer_pending = !s_reader_fullscreen;
    s_mode = mode;
    return APP_REDRAW_AREA;
}

/* ---- 文件与进度 / Files and progress ---- */
typedef struct {
    size_t chapter;
    char** images;
    size_t count;
} reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;

// 按 PR9 通过完整资源解包测量，兼容 SVG 包装及较晚的 JPEG 尺寸头。/ Use PR9's full resource measurement for SVG wrappers and late JPEG headers.
static bool reader_image_dims(void* ctx, int image, int* width, int* height) {
    const reader_dims_ctx_t* c = (const reader_dims_ctx_t*)ctx;
    if (!c || image < 0 || (size_t)image >= c->count) return false;
    uint8_t* encoded = NULL;
    size_t size = 0;
    bool png = false;
    if (book_chapter_image(c->chapter, c->images[image], &encoded, &size, &png) != ESP_OK) return false;
    unsigned w = 0, h = 0;
    const bool ok = book_image_dimensions(encoded, size, png, &w, &h) && w && h;
    free(encoded);
    if (!ok) return false;
    if (width) *width = (int)w;
    if (height) *height = (int)h;
    return true;
}
static void flush_ticket_stats(void) {
    uint32_t seconds = s_stats_pending_ms / 1000;
    if (!seconds && !s_stats_pending_turns) return;
    if (book_ticket_record(seconds, s_stats_pending_turns) == ESP_OK) {
        s_stats_pending_ms -= seconds * 1000;
        s_stats_pending_turns = 0;
    }
}
static void track_ticket_stats(app_ctx_t* ctx) {
    if (s_view != READING || !s_text) { s_stats_last_ms = ctx->now_ms; return; }
    if (s_stats_last_ms > 0 && ctx->now_ms >= s_stats_last_ms &&
        ctx->now_ms - s_stats_last_ms <= 5000 &&
        ctx->now_ms - s_stats_activity_ms <= 300000 && !s_toolbar) {
        uint32_t elapsed = (uint32_t)(ctx->now_ms - s_stats_last_ms);
        s_stats_pending_ms += elapsed;
        s_session_read_ms += elapsed;
    }
    s_stats_last_ms = ctx->now_ms;
    if (s_stats_pending_ms >= 30000) flush_ticket_stats();
}
static void free_book(void) {
    flush_ticket_stats();
    e0470_page_turn_release();
    s_water_turn_pending = false;
    s_reader_turn_pending = s_reader_text_frame = false;
    pending_progress_t* pending = pending_find(s_path);
    if (pending && !pending->dirty) pending_discard(s_path);
    invalidate_prep();
    book_layout_set_image_dims(NULL, NULL);
    s_reader_dims_ctx = (reader_dims_ctx_t){0};
    book_layout_free();
    book_layout_set_chapter_lead(0, 0);
    book_layout_set_chapter_lead_width(0);
    s_chapter_lead_skip = s_chapter_lead_height = 0;
    s_chapter_lead_width = 0;
    s_chapter_heading_title[0] = s_chapter_heading_label[0] = 0;
    free(s_text);
    free(s_blocks);
    free(s_runs);
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = NULL; s_image_count = 0;
    release_page_images();
    s_reader_image_refresh_pending = false;
    s_blocks = NULL;
    s_block_count = 0;
    s_runs = NULL;
    s_run_count = 0;
    book_layout_set_runs(NULL, 0);
    s_text = NULL;
    s_text_len = 0;
    s_selected_toc = SIZE_MAX;
    s_jump_offset = SIZE_MAX;
    book_close();
    s_path[0] = 0;
    s_reader_favorite = false;
    s_book_title[0] = 0;
    app_font_activate_system();
}
// 通栏之后一页位图更大：装不下就按最大空闲块等比缩一档——图小一圈也胜过整页消失。
// A full-bleed page bitmap is larger: when it does not fit, step the aspect-preserving size down
// into the largest free block. A slightly smaller illustration still beats a missing one.
static uint8_t *alloc_page_bitmap(int *width, int *height, size_t *pixels) {
    uint8_t *gray = heap_caps_malloc(*pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray) return gray;
    size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (room == 0) return NULL;
    for (int guard = 0; guard < 24 && *pixels > room; ++guard) {
        *width = *width > 1 ? *width * 7 / 8 : 1;
        *height = *height > 1 ? *height * 7 / 8 : 1;
        *pixels = (size_t)*width * (size_t)*height;
    }
    if (*pixels > room) return NULL;
    gray = heap_caps_malloc(*pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (gray)
        ESP_LOGW(TAG, "inline illustration shrunk to %dx%d, free block %u KB",
                 *width, *height, (unsigned)(room / 1024));
    return gray;
}
static void release_page_images(void) {
    for (int i = 0; i < s_page_image_count; ++i) free(s_page_images[i].gray);
    free(s_page_images); s_page_images = NULL; s_page_image_count = 0;
    s_page_images_for = SIZE_MAX;
}
static void prepare_inline_image(void) {
    if (app_settings_reader_hide_images()) { release_page_images(); return; }
    uint32_t generation = book_layout_generation();
    if (s_page_images_for == s_page && s_page_images_generation == generation) return;
    // 纯文字跨页不应作废已排好的下一页；布局代次改变或任一侧有插图时才失效。
    // Preserve the prefetched next text page; invalidate only across layout generations or image-bearing pages.
    const bool changed_layout_or_image = s_page_images_generation != generation || s_page_image_count > 0;
    release_page_images();
    s_page_images_for = s_page; s_page_images_generation = generation;
    // 插图不显示没有别的症状，每条失败路径以前都是静默 continue；这里把结局记下来，
    // 免得继续靠猜。
    // A missing illustration has no other symptom and every failure path used to be a silent
    // continue; record the outcome instead of guessing.
    int count = reader_image_slots(s_page);
    if (changed_layout_or_image || count > 0) invalidate_prep();
    size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!count || count > (int)HTML_TEXT_MAX_BLOCKS) {
        ESP_LOGI(TAG, "inline page %u: %d image slots", (unsigned)s_page, count);
        return;
    }
    s_page_images = heap_caps_calloc((size_t)count, sizeof(*s_page_images), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_page_images) {
        // 分配失败时计数必须跟着归零：release_page_images 会按计数遍历这张表。
        // The count has to follow the table to zero: release_page_images walks it by count.
        s_page_image_count = 0;
        ESP_LOGW(TAG, "inline page %u: no PSRAM for %d slots", (unsigned)s_page, count);
        return;
    }
    s_page_image_count = count;
    // 逐张解码并立即释放压缩数据；全页灰阶位图的总面积不超过插图区域。
    // Decode serially and release each encoded buffer; the page's bitmap area stays within its
    // illustration region.
    EpdRect band = book_layout_image_rect();
    size_t pixels_left = (size_t)band.width * band.height;
    int decoded = 0;
    for (int i = 0; i < count; ++i) {
        int index = -1, width = 0, height = 0;
        if (!reader_image_slot(s_page, i, &index, NULL, &width, &height) ||
            index < 0 || (size_t)index >= s_image_count || width <= 0 || height <= 0) {
            ESP_LOGW(TAG, "inline %d: slot i=%d width=%d height=%d", i, index, width, height);
            continue;
        }
        uint8_t *encoded = NULL; size_t size = 0; bool png = false;
        esp_err_t err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "inline %d %s: read %s", i, s_images[index], esp_err_to_name(err));
            free(encoded);
            continue;
        }
        unsigned source_w = 0, source_h = 0;
        if (book_image_dimensions(encoded, size, png, &source_w, &source_h) && source_w && source_h) {
            // 回退槽也使用真实宽高等比缩小，避免尺寸探测失败时拉伸图片。
            // Aspect-fit even fallback slots from decoded dimensions, avoiding stretched unknown-size images.
            uint64_t w = source_w, h = source_h;
            if (w > (unsigned)width) { h = h * width / w; w = width; }
            if (h > (unsigned)height) { w = w * height / h; h = height; }
            width = w ? (int)w : 1; height = h ? (int)h : 1;
            size_t pixels = (size_t)width * height;
            if (pixels > pixels_left) {
                ESP_LOGW(TAG, "inline %d: %ux%u over %u px budget",
                         i, (unsigned)width, (unsigned)height, (unsigned)pixels_left);
                free(encoded);
                continue;
            }
            uint8_t *gray = alloc_page_bitmap(&width, &height, &pixels);
            if (gray == NULL) {
                ESP_LOGW(TAG, "inline %d: no PSRAM for %u px (free %u)",
                         i, (unsigned)pixels,
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                free(encoded);
                continue;
            }
            if (book_image_grayscale(encoded, size, png, width, height, gray)) {
                s_page_images[i] = (reader_image_t){gray, width, height};
                pixels_left -= pixels;
                ++decoded;
            } else {
                ESP_LOGW(TAG, "inline %d: grayscale decode failed", i);
                free(gray);
            }
        } else {
            ESP_LOGW(TAG, "inline %d: unknown dimensions (%u bytes, png=%d)", i, (unsigned)size, (int)png);
        }
        free(encoded);
        vTaskDelay(1);
    }
    // 只在有图没画出来时汇总：插图缺失没有别的症状，而每次都打会把日志淹掉。
    // Summary only when something did not make it: a missing illustration has no other symptom,
    // and logging every page would drown the console.
    if (decoded < count) {
        ESP_LOGW(TAG, "inline page %u: %d/%d decoded, psram %u->%u KB", (unsigned)s_page, decoded, count,
                 (unsigned)(psram_before / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
    }
}
// 标题匹配要宽松：书内导航与正文的写法常常不同（"第一章 出发" 对 "出发"、"1. Prologue" 对
// "序章"，竖排书尤其如此）。逐字比较失败时正文标题块不会被跳过，题头与正文就各显示一次。
// Heading matching has to be forgiving: an authored navigation title rarely matches the body text
// verbatim ("Chapter 1 Departure" against "Departure", a numbered English title against its
// Japanese one), and a strict compare leaves the body heading on the page, so it shows up twice.
static bool heading_text_matches(const char *text, const char *toc, const char *heading,
                                 const char *label) {
    if (text == NULL || text[0] == 0) return false;
    const char *candidates[3] = {toc, heading, label};
    for (int i = 0; i < 3; ++i) {
        const char *cand = candidates[i];
        if (cand == NULL || cand[0] == 0) continue;
        // 互相包含即视为同一标题；短于三个字节的候选不作为依据，免得误吞正文。
        // Either containing the other counts as the same heading; candidates shorter than three
        // bytes are ignored so ordinary body text cannot be swallowed.
        if (strlen(cand) < 3) continue;
        if (strstr(text, cand) != NULL || strstr(cand, text) != NULL) return true;
    }
    return false;
}

static bool load_chapter_at(app_ctx_t* ctx, size_t chapter, size_t offset,
                            bool last_page, const char *anchor, size_t source_offset) {
    bool clear_toc = chapter != s_chapter || (!anchor && source_offset == SIZE_MAX);
    html_text_t loaded = {0};
    size_t anchor_offset = 0;
    esp_err_t err = book_chapter_load_blocks_target(chapter, anchor, source_offset,
                                                    &anchor_offset, &loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter load failed: %s", esp_err_to_name(err));
        copy_text(s_message, sizeof(s_message), "加载失败，请检查存储卡后重试");
        return false;
    }
    char heading[128] = {0}, label[32] = {0}, toc_title[128] = {0};
    size_t lead_skip = 0;
    unsigned lead_height = 0;
    if (book_kind() == BOOK_KIND_EPUB && loaded.len) {
        // 已识别的正文编号题头优先；没有时沿用书内导航或备用标题。
        // Prefer a recognized numbered body heading, then authored navigation or a fallback title.
        if (book_chapter_title(chapter, toc_title, sizeof(toc_title)) != ESP_OK)
            snprintf(toc_title, sizeof(toc_title), "第 %u 节", (unsigned)chapter + 1);
        copy_text(heading, sizeof(heading), toc_title);
        char *chapter_mark = strstr(heading, "章");
        if (chapter_mark && chapter_mark - heading < 24) {
            char *after = chapter_mark + strlen("章");
            while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
                   !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、"))) {
                if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
                else after += strlen("：");
            }
            if (*after && (size_t)(chapter_mark + strlen("章") - heading) < sizeof(label)) {
                size_t label_len = (size_t)(chapter_mark + strlen("章") - heading);
                memcpy(label, heading, label_len); label[label_len] = 0;
                memmove(heading, after, strlen(after) + 1);
            }
        }
        size_t skip_blocks = 0;
        // 最多看四个块：标题可能被拆成"第一章"、"出发"、"——副标题"这样几块。
        // Up to four blocks: a heading can arrive split across several.
        while (skip_blocks < loaded.count && skip_blocks < 4 &&
               loaded.blocks[skip_blocks].heading && loaded.blocks[skip_blocks].image < 0) {
            char body_heading[128] = {0};
            size_t len = loaded.blocks[skip_blocks].len;
            if (len >= sizeof(body_heading)) break;
            memcpy(body_heading, loaded.utf8 + loaded.blocks[skip_blocks].offset, len);
            while (len && (body_heading[len - 1] == ' ' || body_heading[len - 1] == '\t')) body_heading[--len] = 0;
            const char *trimmed = body_heading;
            while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
            if (!heading_text_matches(trimmed, toc_title, heading, label)) break;
            ++skip_blocks;
        }
        if (skip_blocks) lead_skip = skip_blocks < loaded.count ? loaded.blocks[skip_blocks].offset : loaded.len;
        // 首块是插图时不预留题头：那页整页留给插图，标题在它后面才排。
        // 这个条件一直是题头该不该出现的判据，横排竖排都一样。
        // An illustration at the head of the chapter reserves nothing: it owns that page and the
        // heading follows. This condition has always decided whether a sheet gets a heading at all,
        // in either setting.
        if (skip_blocks < loaded.count && loaded.blocks[skip_blocks].image >= 0) {
            lead_skip = 0;
            lead_height = 0;
        } else {
            lead_height = body_rect().height > 214 ? 214 : 0;
        }
    }
    size_t old_lead_skip = s_chapter_lead_skip;
    unsigned old_lead_height = s_chapter_lead_height;
    unsigned old_lead_width = s_chapter_lead_width;
    reader_dims_ctx_t old_dims_ctx = s_reader_dims_ctx;
    lock_draw();
    invalidate_prep();
    // 题头几何按当前模式重算，横排留顶部行高、竖排留右侧列宽，绘制与预留不会错开。
    // Recompute the heading geometry for the current mode -- rows at the top horizontally,
    // columns at the right vertically -- so drawing and reservation cannot drift apart.
    apply_chapter_lead(heading, label, lead_skip, lead_height);
    s_reader_dims_ctx = (reader_dims_ctx_t){chapter, loaded.images, loaded.image_count};
    book_layout_set_image_dims(reader_image_dims, &s_reader_dims_ctx);
    book_layout_set_runs(loaded.runs, loaded.run_count);
    bool ok = reader_relayout_cols(loaded.utf8, loaded.len, loaded.blocks, loaded.count,
                                   body_rect(), s_px, reader_column_count());
    if (!ok) {
        // 回滚前恢复旧章上下文，避免尺寸回调引用已释放的新章图片。/ Restore the previous context before rollback to avoid a freed image list.
        s_reader_dims_ctx = old_dims_ctx;
        html_text_free(&loaded);
        // run 表随 loaded 一起没了，先把排版还回旧章的那份，否则重排会读到已释放的内存。
        // The run table dies with `loaded`, so put the previous chapter's back before the
        // rollback layout reads freed memory.
        book_layout_set_runs(s_runs, s_run_count);
        // 题头几何也跟着回滚，否则这一章的题头参数会留在失败的那次上。
        // Roll the heading geometry back too, or the failed chapter's parameters would stick.
        s_chapter_lead_skip = old_lead_skip;
        s_chapter_lead_height = old_lead_height;
        s_chapter_lead_width = old_lead_width;
        book_layout_set_chapter_lead(old_lead_skip, old_lead_height);
        book_layout_set_chapter_lead_width(old_lead_width);
        bool restored = s_text && reader_relayout(body_rect());
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            ctx->leaf = 0;
        }
        copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
        return false;
    }
    book_excerpt_offer(s_path, &loaded);
    free(s_text);
    free(s_blocks);
    free(s_runs);
    s_text = loaded.utf8;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    s_runs = loaded.runs;
    s_run_count = loaded.run_count;
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = loaded.images; s_image_count = loaded.image_count;
    release_page_images();
    s_chapter = chapter;
    if (clear_toc) s_selected_toc = SIZE_MAX;
    s_chapter_lead_skip = lead_skip;
    s_chapter_lead_row_height = lead_height;
    copy_text(s_chapter_heading_title, sizeof(s_chapter_heading_title), heading);
    copy_text(s_chapter_heading_label, sizeof(s_chapter_heading_label), label);
    // 末页与续读落点都经地图钳到视觉页起始逻辑页；单栏下与旧值逐位一致。
    // Both the last page and resume landings clamp onto a visual page's starting logical
    // page through the map; single column matches the old values exactly.
    s_page = last_page ? reader_visual_start(reader_visual_count() - 1) :
             reader_visual_start(reader_visual_for_offset(anchor || source_offset != SIZE_MAX ? anchor_offset : offset));
    s_jump_offset = anchor || source_offset != SIZE_MAX ? anchor_offset : SIZE_MAX;
    s_jump_page = s_page;
    if (book_chapter_title(chapter, s_title, sizeof(s_title)) != ESP_OK)
        snprintf(s_title, sizeof(s_title), "第 %u 节", (unsigned)chapter + 1);
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    s_message[0] = 0;
    unlock_draw();
    prepare_inline_image();
    // 开书这条链上有解析、解压和字体装载，栈曾经在这里溢出过；留一个可观测的水位。
    // 顺带量一下章节排完后的 PSRAM 余量：书内字体的装载预算要靠这个数来定，不能靠估。
    // Parsing, inflate and face loading all ride this chain, and it overflowed once before;
    // keep the headroom observable. The PSRAM left after a chapter is laid out is also the
    // number the face budget should be derived from rather than guessed.
    ESP_LOGI(TAG, "chapter %u stack free %u psram free %u largest %u", (unsigned)chapter,
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return true;
}
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page) {
    return load_chapter_at(ctx, chapter, offset, last_page, NULL, SIZE_MAX);
}
// 关闭图片时跳过纯插图页，避免出现空白翻页。/ Skip illustration-only pages when images are disabled.
// 隐藏图页在地图里有正式名分（JOINT 类）：跳转走地图标记定位下一个可显示视觉页，
// 不做「逻辑页 ±1 直移再换算」的独立算术——两条路各算各的迟早分叉（连跳多个隐藏
// 图页的场景）。s_page 恒为视觉页起始逻辑页，故 JOINT 判定看落点即看视觉页类。
// Hidden image pages have a formal class in the map (JOINT): the skip walks the map's
// marks to the next displayable visual page instead of moving a logical page by one and
// converting afterwards — two separate computations eventually diverge (runs of hidden
// pages). s_page is always a visual page's starting logical page, so testing the landing
// for JOINT tests the visual page's class.
static void skip_hidden_image_pages(app_ctx_t *ctx, int direction) {
    if (!app_settings_reader_hide_images()) return;
    for (int guard = 0; guard < 128 && reader_page_is_joint(s_page); ++guard) {
        size_t visual = reader_visual_index(s_page);
        if (direction > 0) {
            if (visual + 1 < reader_visual_count()) s_page = reader_visual_start(visual + 1);
            else if (s_chapter + 1 < book_chapter_count()) {
                if (!load_chapter(ctx, s_chapter + 1, 0, false)) break;
            } else break;
        } else {
            if (visual > 0) s_page = reader_visual_start(visual - 1);
            else if (s_chapter > 0) {
                if (!load_chapter(ctx, s_chapter - 1, 0, true)) break;
            } else break;
        }
    }
}
static bool open_book_impl(app_ctx_t* ctx, const char* path) {
    s_reader_return_home = false;
    if (!strncmp(path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0};
        read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) {
            copy_text(s_message, sizeof(s_message), "TF 卡不可用，请重新挂载后打开");
            return false;
        }
    }
    // 自动续读也不能绕过同路径旧文件的清理重试。
    // Automatic resume must also finish cleanup of the old file at this path.
    if (delete_retry_find(path)) {
        copy_text(s_message, sizeof(s_message), "文件已删除，进度清理失败，请重试");
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_message, sizeof(s_message), "文件不可读，请检查 TF 卡和书籍文件");
        return false;
    }
    if (strncmp(path, "/flash/", 7) == 0 && st.st_size > BOOK_STORE_FLASH_FILE_MAX) {
        copy_text(s_message, sizeof(s_message), "内置存储单本限 1 MB");
        return false;
    }
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    // 书架封面和上一本书的预渲染缓冲可以重建；先释放，再解析 EPUB。
    // Cover and previous-page caches are rebuildable; release them before EPUB parsing.
    invalidate_covers();
    app_home_release_clock_cache();
    lock_draw();
    invalidate_prep();
    free(s_next_fb);
    s_next_fb = NULL;
    unlock_draw();
    if (network.network_ready) ttf_font_cache_clear();
    // 开书前把字形缓存全还回去并量一下能腾多少：书内字体要的正是这块 PSRAM，而系统字体
    // 的缓存是里面最容易回收的。字体本身保持装载，翻页时按需重新缓存。
    // Hand every glyph cache back before opening and measure what that was worth: embedded
    // faces need exactly this PSRAM, and the system face's cache is the easiest part to
    // reclaim. The faces stay loaded and re-cache on demand.
    size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t reclaimed = ttf_font_cache_clear_all();
    size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "open start wifi=%d internal=%u/%u psram=%u/%u cache=%uK freed=%uK",
             network.network_ready,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)psram_after,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)(reclaimed / 1024),
             (unsigned)((psram_after - psram_before) / 1024));
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法保留待保存进度");
        return false;
    }
    save_progress();
    free_book();
    s_reader_fullscreen = s_requested_fullscreen;
    app_font_activate_reading();
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法打开图书");
        app_font_activate_system();
        return false;
    }
    esp_err_t err = book_open(path);
    // 书源打开后再把书内字体开关接上：EPUB 才有效，TXT 是空操作。
    // Wire the embedded-face switch only after the source is open: it only applies to EPUB,
    // and is a no-op for TXT.
    if (err == ESP_OK) {
        s_book_fonts_applied = app_settings_book_fonts();
        book_set_embedded_fonts(s_book_fonts_applied);
    }
    if (err != ESP_OK) {
        pending_progress_t* pending = pending_find(path);
        if (pending && !pending->dirty) pending_discard(path);
        copy_text(s_message, sizeof(s_message),
                  err == ESP_ERR_NOT_SUPPORTED ? "文件格式或压缩方式暂不支持" :
                  err == ESP_ERR_NO_MEM ? "内存不足，请重启后重试" :
                  err == ESP_ERR_INVALID_SIZE ? "图书资源过大或章节过多" :
                  "无法打开图书，请检查文件");
        ESP_LOGW(TAG, "open failed: %s internal=%u/%u psram=%u/%u", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        app_font_activate_system();
        return false;
    }
    copy_text(s_path, sizeof(s_path), path);
    // gen21（裁决 A「打开即已读」）：push.txt 实际打开成功即清未读位。此处是
    // 全部开书路径（首页大卡/传书自动开/书架点开）的唯一汇合点——传书页推完
    // 自动开书随即清位（不亮假牌）；用户在别处收推则无人清位，回首页徽章亮。
    // 上行 EVT_READ（notify_read）保持在 book_on_exit 原位，手机侧语义不变。
    // gen21 (ruling A "open means read"): opening push.txt clears the unread
    // flag. open_book_impl is the single funnel for every open path (home card,
    // transfer auto-open, shelf tap) - the auto-open chain clears right away (no
    // false badge), while a push landing elsewhere stays flagged until the home
    // badge lights. The outbound EVT_READ stays in book_on_exit untouched.
    if (strcmp(s_path, PUSH_TEXT_PATH) == 0) push_sink_clear_unread();
    s_reader_favorite = favorite_load(path);
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    if (!book_title_from_path(path, s_book_title, sizeof(s_book_title)))
        clean_filename(s_book_title, sizeof(s_book_title), filename);
    s_file_size = (uint32_t)st.st_size;
    book_progress_t p = {0};
    bool resume = pending_restore(path, s_file_size, &p) || book_progress_load(path, s_file_size, &p);
    // 排版字号以全局阅读设置为准；旧书进度只决定阅读位置。
    // Use the shared reading size for every book; per-book progress restores position only.
    s_px = app_settings_book_px();
    if (s_px < BOOK_PX_MIN || s_px > BOOK_PX_MAX) s_px = 48;
    size_t chapter = resume && p.chapter < book_chapter_count() ? p.chapter : 0;
    if (!load_chapter(ctx, chapter, resume && p.chapter == chapter ? p.byte_off : 0, false)) {
        free_book();
        return false;
    }
    skip_hidden_image_pages(ctx, 1);
    if (!s_text) { free_book(); return false; }
    s_view = READING;
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() &&
        book_layout_page_image_count(s_page) > 0 && s_page_images;
    s_stats_last_ms = s_stats_activity_ms = ctx->now_ms;
    s_reader_panel = READER_PANEL_NONE;
    s_clear_confirm = s_batch_confirm = false;
    s_session_read_ms = s_session_turns = 0;
    s_reader_notice[0] = 0;
    s_turns = s_unsaved = 0;
    pending_mark_latest(s_path);
    save_progress();
    // 打开即重新上书架：移出书架只隐藏条目，重新读这本书就是它该在架上的信号。
    // Opening puts the book back on the shelf: removal only hides the entry, and reading it
    // again is the signal that it belongs there.
    if (shelf_hidden_load(s_path)) (void)shelf_hidden_save(s_path, false);
    ESP_LOGI(TAG, "opened kind=%d chapters=%u pages=%u px=%d", book_kind(), (unsigned)book_chapter_count(), (unsigned)reader_visual_count(), s_px);
    return true;
}
static bool open_book(app_ctx_t *ctx, const char *path) {
    if (!book_cover_jobs_quiesce(2000)) {
        copy_text(s_message,sizeof(s_message),"封面任务正在收尾，请稍后再试");return false;
    }
    if (pico_boot_book_begin(path) != ESP_OK) {
        copy_text(s_message, sizeof(s_message), "启动记录保存失败，请重试");
        return false;
    }
    bool ok = open_book_impl(ctx, path);
    pico_boot_book_end(ok);
    return ok;
}
static bool open_requested_book(app_ctx_t* ctx) {
    if (!s_requested_open[0]) return false;
    char path[BOOK_STORE_PATH_MAX];
    copy_text(path, sizeof(path), s_requested_open);
    bool from_home = s_requested_open_home;
    s_requested_open[0] = 0;
    s_requested_open_home = false;
    bool opened = open_book(ctx, path);
    s_requested_fullscreen = true;
    if (!opened) return false;
    s_reader_return_home = from_home;
    return true;
}

/* ---- 生词本虚拟书 / The vocabulary-book virtual book ----
 * 把 vocab.txt 渲染成一本排版好的 TXT，交给现成阅读器打开：排版、翻页、长按查词、
 * 释义卡和保存 chip 全都是复用，不用新写任何列表 UI。
 * Render vocab.txt into a laid-out TXT and open it with the existing reader:
 * layout, page turns, long-press lookup, the definition card and the save chip are
 * all reused — no new list UI.
 *
 * 走 open_book_impl() 而不是 open_book()：后者会 pico_boot_book_begin() 把生词本
 * 写进启动恢复记录，那样下次开机/唤醒会直接落到生词本，顶掉真正在读的那本书。
 * Use open_book_impl() rather than open_book(): the latter records the boot book,
 * which would resume into the vocabulary view on the next boot and displace the
 * book actually being read.
 */
static app_redraw_t open_vocab_view(app_ctx_t* ctx, bool restore_position) {
    size_t chapter = s_chapter, page = s_page;
    size_t count = 0;
    // 有词条才值得打开；空的就给一句提示，留在原处。提示走阅读页的 notice
    // （s_message 只显示在书架/文件页上，阅读页里看不见）。
    // Only open it when there is something to read; otherwise explain and stay put.
    // The hint uses the reader notice: s_message only surfaces on the shelf/files pages.
    const char* notice = !vocab_log_render(&count) ? "生词本视图生成失败"
                       : count == 0 ? "生词本还是空的：长按英文单词后点「保存」"
                       : NULL;
    if (notice) {
        copy_text(s_reader_notice, sizeof(s_reader_notice), notice);
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    dict_popup_reset();
    s_reader_panel = READER_PANEL_NONE;
    if (!open_book_impl(ctx, vocab_log_view_path())) {
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    // 路径推出来的书名是 "vocab_view"，换成中文。
    // The path-derived title is "vocab_view"; use a readable one.
    copy_text(s_book_title, sizeof(s_book_title), "我的生词本");
    if (restore_position && (chapter || page)) {
        // 删掉一条后文件整体前移，字节偏移已经不可信：先把排版建出来，
        // 再按**页码**跳回去，视觉上还停在原来那一页。
        // After a removal the byte offsets shift; rebuild the layout first and jump
        // by page index so the reader stays on the same page visually.
        if (chapter >= book_chapter_count()) chapter = book_chapter_count() - 1;
        (void)load_chapter(ctx, chapter, 0, false);
        size_t pages = book_layout_page_count();
        size_t target = page < pages ? page : (pages ? pages - 1 : 0);
        (void)load_chapter(ctx, chapter, book_layout_page_start_offset(target), false);
    }
    invalidate_prep();
    return APP_REDRAW_PAGE;
}
/* ---- 书架数据 / Shelf data ---- */
static int shelf_category_add(const char *path) {
    for (int i=2;i<s_category_count;++i) {
        if (!strcmp(s_categories[i].path,path)) { s_categories[i].available=true; return i; }
    }
    if (s_category_count==SHELF_CATEGORY_LIMIT) return -1;
    if (s_category_count==s_category_capacity) {
        int capacity=s_category_capacity ? s_category_capacity*2 : 16;
        if (capacity>SHELF_CATEGORY_LIMIT) capacity=SHELF_CATEGORY_LIMIT;
        shelf_category_t *next=heap_caps_realloc(s_categories,(size_t)capacity*sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!next) return -1;
        s_categories=next; s_category_capacity=capacity;
    }
    int index=s_category_count++;
    memset(&s_categories[index],0,sizeof(s_categories[index]));
    copy_text(s_categories[index].path,sizeof(s_categories[index].path),path);
    s_categories[index].available=true;
    return index;
}
static void shelf_category_init(void) {
    const char *root=app_settings_books_dir();
    if (strcmp(s_category_root,root)) {
        free(s_categories); s_categories=NULL;
        s_category_count=s_category_capacity=s_category_selected=s_category_menu_page=0;
        copy_text(s_category_root,sizeof(s_category_root),root);
        s_shelf_cache_valid=false;
    }
    if (s_categories) return;
    s_categories=heap_caps_calloc(16,sizeof(*s_categories),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_categories) return;
    s_category_capacity=16; s_category_count=2;
    s_categories[0].available=s_categories[1].available=true;
}
static const char *shelf_category_name(int index) {
    if (index==1) return "未分类";
    if (!s_categories || index<2 || index>=s_category_count) return "全部书籍";
    const char *slash=strrchr(s_categories[index].path,'/');
    return slash ? slash+1 : s_categories[index].path;
}
static bool shelf_category_matches(const char *path) {
    if (!s_category_selected || !s_categories) return true;
    if (s_category_selected==1) {
        char child[BOOK_STORE_PATH_MAX];
        return !book_shelf_child(s_category_root,path,child,sizeof(child));
    }
    size_t size=strlen(s_categories[s_category_selected].path);
    return !strncmp(path,s_categories[s_category_selected].path,size) && path[size]=='/';
}
static void shelf_category_remember(app_ctx_t *ctx) {
    if (s_categories && s_view==SHELF && !s_scan_pending)
        s_categories[s_category_selected].pages[app_settings_shelf_style()==5]=ctx->leaf;
}
static void shelf_category_restore(app_ctx_t *ctx) {
    int layout=app_settings_shelf_style()==5;
    int saved=s_categories ? s_categories[s_category_selected].pages[layout] : 0;
    ctx->leaf=book_shelf_saved_page(saved,s_visible_count,shelf_rows());
    if (s_categories) s_categories[s_category_selected].pages[layout]=ctx->leaf;
}
static void shelf_categories_recount(void) {
    if (!s_categories) return;
    for (int i=0;i<s_category_count;++i) s_categories[i].count=0;
    for (int i=0;i<s_count;++i) {
        if (s_shelf[i].removed) continue;
        ++s_categories[0].count;
        char child[BOOK_STORE_PATH_MAX],path[BOOK_STORE_PATH_MAX];
        if (!book_shelf_child(s_category_root,s_shelf[i].path,child,sizeof(child))) {
            ++s_categories[1].count; continue;
        }
        int written=snprintf(path,sizeof(path),"%s/%s",s_category_root,child);
        if (written<0 || (size_t)written>=sizeof(path)) continue;
        int index=shelf_category_add(path);
        if (index>=2) ++s_categories[index].count;
    }
    if (s_category_selected>=s_category_count || !s_categories[s_category_selected].available)
        s_category_selected=0;
}
static int shelf_category_at(int ordinal) {
    for (int i=0;i<s_category_count;++i) {
        if (s_categories[i].available && ordinal--==0) return i;
    }
    return -1;
}
static int shelf_category_available(void) {
    int count=0;
    for (int i=0;i<s_category_count;++i) if (s_categories[i].available) ++count;
    return count;
}
static EpdRect shelf_category_box(void) {
    int count=shelf_category_available()-s_category_menu_page*SHELF_CATEGORY_ROWS;
    if (count>SHELF_CATEGORY_ROWS) count=SHELF_CATEGORY_ROWS;
    if (count<1) count=1;
    return (EpdRect){36,158,400,16+count*83+(shelf_category_available()>SHELF_CATEGORY_ROWS ? 56 : 0)};
}
static void draw_category_menu(uint8_t *fb) {
    EpdRect box=shelf_category_box();
    ui_fill_round_rect(fb,box,23,UI_GRAY_WHITE);
    ui_draw_control_frame(fb,box,23,0x50);
    for (int row=0;row<SHELF_CATEGORY_ROWS;++row) {
        int index=shelf_category_at(s_category_menu_page*SHELF_CATEGORY_ROWS+row);
        if (index<0) break;
        int y=box.y+8+row*83;
        bool selected=index==s_category_selected;
        if (selected) ui_fill_round_rect(fb,(EpdRect){box.x+8,y,box.width-16,79},16,0xd0);
        ui_draw_icon(fb,box.x+35,y+40,27,index<2 ? UI_ICON_LIBRARY_BIG : UI_ICON_FOLDER_OPEN,UI_GRAY_BLACK);
        char name[BOOK_STORE_PATH_MAX],count[24];
        snprintf(count,sizeof(count),"%d",s_categories[index].count);
        int name_width=290-ui_text_fixed_width_px(ui_text_effective_px(22),count);
        if (name_width>228) name_width=228;
        if (name_width<60) name_width=60;
        copy_text(name,sizeof(name),shelf_category_name(index)); fit_text(name,28,name_width);
        ui_text_vc(fb,box.x+62,y+40,28,name,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,box.x+box.width-38,y+40,22,count,EPD_DRAW_ALIGN_RIGHT,false);
        if (selected) epd_fill_circle(box.x+box.width-20,y+40,4,UI_GRAY_BLACK,fb);
    }
    if (shelf_category_available()>SHELF_CATEGORY_ROWS) {
        int y=box.y+box.height-28;
        ui_draw_icon(fb,box.x+48,y,28,UI_ICON_CHEVRON_LEFT,s_category_menu_page ? 0 : 0x90);
        char pages[24]; snprintf(pages,sizeof(pages),"%d / %d",s_category_menu_page+1,
            (shelf_category_available()+SHELF_CATEGORY_ROWS-1)/SHELF_CATEGORY_ROWS);
        ui_text_vc(fb,box.x+box.width/2,y,22,pages,EPD_DRAW_ALIGN_CENTER,false);
        ui_draw_icon(fb,box.x+box.width-48,y,28,UI_ICON_CHEVRON_RIGHT,0);
    }
}
static bool shelf_matches(const shelf_entry_t* item) {
    return shelf_category_matches(item->path) && item->search_match && (s_filter == 0 || (s_filter == 1 && !item->is_flash) || (s_filter == 2 && item->is_flash));
}
static int compare_books(const void* a, const void* b) {
    const shelf_entry_t* x = a;
    const shelf_entry_t* y = b;
    if (shelf_matches(x) != shelf_matches(y)) return shelf_matches(x) ? -1 : 1;
    if (x->favorite != y->favorite)
        return x->favorite ? -1 : 1;
    if (s_recent_sort && x->recent != y->recent) return x->recent > y->recent ? -1 : 1;
    int name = strcasecmp(x->name, y->name);
    return name ? name : strcmp(x->path, y->path);
}
static void sort_shelf(app_ctx_t* ctx) {
    invalidate_covers();
    if (s_count > 1) qsort(s_shelf, s_count, sizeof(*s_shelf), compare_books);
    s_visible_count = 0;
    while (s_visible_count < s_count && shelf_matches(&s_shelf[s_visible_count])) ++s_visible_count;
    ctx->leaf = 0;
}
static app_redraw_t shelf_category_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type!=UI_GESTURE_TAP && ev->type!=UI_GESTURE_SWIPE_U && ev->type!=UI_GESTURE_SWIPE_D)
        return APP_REDRAW_NONE;
    EpdRect previous=shelf_category_box();
    if (ev->type==UI_GESTURE_TAP && ui_nav_hit(ev->x0,ev->y0)>=0) {
        s_category_menu=false; ui_nav_request(ctx,ui_nav_hit(ev->x0,ev->y0)); return APP_REDRAW_NONE;
    }
    if (!s_category_menu) {
        if (ev->type!=UI_GESTURE_TAP || !s_categories) return APP_REDRAW_NONE;
        s_category_menu=true; s_category_menu_page=0;
        int ordinal=0;
        for (int i=0;i<s_category_selected;++i) if (s_categories[i].available) ++ordinal;
        s_category_menu_page=ordinal/SHELF_CATEGORY_ROWS;
    } else {
        int pages=(shelf_category_available()+SHELF_CATEGORY_ROWS-1)/SHELF_CATEGORY_ROWS;
        int direction=ev->type==UI_GESTURE_SWIPE_U ? 1 : ev->type==UI_GESTURE_SWIPE_D ? -1 : 0;
        if (!direction && ui_rect_hit(previous,ev->x0,ev->y0)) {
            int row=((int)ev->y0-previous.y-8)/83;
            bool footer=pages>1 && ev->y0>=previous.y+previous.height-56;
            if (footer) direction=ev->x0<previous.x+previous.width/2 ? -1 : 1;
            else {
                int index=ev->y0>=previous.y+8 && row<SHELF_CATEGORY_ROWS
                    ? shelf_category_at(s_category_menu_page*SHELF_CATEGORY_ROWS+row) : -1;
                if (index>=0) {
                    shelf_category_remember(ctx); s_category_selected=index;
                    s_category_menu=false; clear_selection(); sort_shelf(ctx); shelf_category_restore(ctx);
                    return APP_REDRAW_PAGE;
                }
            }
        } else if (!direction) s_category_menu=false;
        if (direction) {
            int next=s_category_menu_page+direction;
            if (next<0 || next>=pages) return APP_REDRAW_NONE;
            s_category_menu_page=next;
        }
    }
    EpdRect next=shelf_category_box();
    int bottom=previous.y+previous.height>next.y+next.height ? previous.y+previous.height : next.y+next.height;
    s_area=(EpdRect){24,79,424,bottom-79+4}; s_mode=MODE_GL16;
    render(ctx,ctx->fb);
    return APP_REDRAW_AREA;
}
static bool shelf_reserve(void) {
    if ((size_t)s_count < s_shelf_capacity) return true;
    if (s_count == INT_MAX) return false;
    size_t cap = s_shelf_capacity ? s_shelf_capacity * 2 : 32;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(*s_shelf)) return false;
    shelf_entry_t* entries = heap_caps_realloc(s_shelf, cap * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    s_shelf = entries;
    s_shelf_capacity = cap;
    return true;
}

// 在删除文件前预留记录，避免删除成功后才遇到内存不足。
// Reserve before unlink so allocation failure never loses a completed deletion's retry.
static delete_retry_t* delete_retry_reserve(const shelf_entry_t* entry) {
    delete_retry_t* existing = delete_retry_find(entry->path);
    if (existing) return existing;
    delete_retry_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->entry = *entry;
    p->next = s_delete_retries;
    s_delete_retries = p;
    return p;
}
static void delete_retry_discard(const char* path) {
    delete_retry_t** p = &s_delete_retries;
    while (*p) {
        if (!strcmp((*p)->entry.path, path)) {
            delete_retry_t* old = *p;
            *p = old->next;
            free(old);
            return;
        }
        p = &(*p)->next;
    }
}
// 显式目录栈放在 PSRAM；主任务只有 8 KB 栈，递归进多级 books 会在封面出现前溢出。
// Keep directory frames in PSRAM: recursive books scanning can overflow the 8 KB main-task stack before covers appear.
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;
static void scan_shelf_dir(app_ctx_t *ctx, const char *root, bool is_flash, nvs_handle_t favorites,
                           nvs_handle_t hidden, bool *truncated, bool *unreadable,
                           bool *skipped) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    size_t root_len = strnlen(root, sizeof(frames[0].path));
    if (root_len >= sizeof(frames[0].path)) { *skipped = true; free(frames); return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    memcpy(frames[0].path, root, root_len + 1);
    int depth = 0;
    while (depth >= 0 && !*truncated) {
        if (s_view == SHELF && app_poll_main_navigation(ctx)) break;
        errno = 0;
        struct dirent *ent = readdir(frames[depth].dir);
        if (!ent) {
            if (errno) *unreadable = true;
            closedir(frames[depth].dir);
            frames[depth].dir = NULL;
            --depth;
            continue;
        }
        if (ent->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int len = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) { *skipped = true; continue; }
        struct stat st;
        if (stat(path, &st) != 0) { *unreadable = true; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (!is_flash && depth==0 && !strcmp(root,app_settings_books_dir())) shelf_category_add(path);
            if (depth == BOOK_SCAN_DEPTH_LIMIT) { *skipped = true; continue; }
            DIR *child = opendir(path);
            if (!child) { *unreadable = true; continue; }
            ++depth;
            frames[depth].dir = child;
            memcpy(frames[depth].path, path, (size_t)len + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) continue;
        // 显式移出书架的书不再列入；文件仍在存储卡上，随时可以重新读回书架。
        // Books the user removed stay off the shelf; the file stays on the card and reading it
        // again puts it back.
        if (hidden && shelf_hidden_read_handle(hidden, path)) continue;
        if (strlen(ent->d_name) >= sizeof(((shelf_entry_t *)0)->name) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { *skipped = true; continue; }
        if (!shelf_reserve()) { *truncated = true; break; }
        shelf_entry_t candidate = {0};
        shelf_entry_t *item = &candidate;
        copy_text(item->path, sizeof(item->path), path);
        if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
            clean_filename(item->name, sizeof(item->name), ent->d_name);
        if (!strcasecmp(ext, ".epub")) {
            char title[sizeof(item->name)] = {0};
            char author[sizeof(item->author)] = {0};
            // Listing must stay cheap and bounded: never parse every EPUB before
            // the shelf is first painted. Opening a book populates this cache.
            // 首屏列书只读已有元数据缓存，不在封面出现前逐本解析 EPUB。
            if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
                copy_text(item->author, sizeof(item->author), author);
        }
        item->search_match = read_pico_search_match(item->name, s_query);
        item->size = st.st_size;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = book_progress_load(item->path, item->size, &p);
        item->pct = item->has_progress ? p.pct : 0;
        item->chapter = item->has_progress ? p.chapter : 0;
        item->recent = item->has_progress ? p.last_open_s : 0;
        item->favorite = favorites && favorite_read_handle(favorites, item->path);
        s_shelf[s_count++] = candidate;
    }
    for (int i = 0; i <= depth; ++i)
        if (frames[i].dir) closedir(frames[i].dir);
    free(frames);
}

// 阅读过的书即使不在书根目录内也要上书架：文件管理可以直接打开存储卡任意位置的 TXT/EPUB，
// 这些书以前读完就消失了。文件已删除或内容已被替换的记录不上架，与进度不可恢复的口径一致。
// A book that was read belongs on the shelf even outside the book roots, because the file
// manager can open a TXT or EPUB anywhere on the card. Records whose file is gone, or whose
// contents were replaced, stay off the shelf, matching the rule that changed sizes never resume.
typedef struct {
    nvs_handle_t favorites;
    nvs_handle_t hidden;
    bool truncated;
} shelf_backfill_t;

static bool shelf_backfill_visit(const char* path, const book_progress_t* progress, void* ctx) {
    shelf_backfill_t* scan = ctx;
    if (scan->truncated) return false;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, path)) return true;   // 书根目录已经收录
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX || (uint32_t)st.st_size != progress->file_size)
        return true;
    if (scan->hidden && shelf_hidden_read_handle(scan->hidden, path)) return true;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strlen(base) >= sizeof(((shelf_entry_t*)0)->name)) return true;
    if (!shelf_reserve()) { scan->truncated = true; return false; }
    shelf_entry_t candidate = {0};
    shelf_entry_t* item = &candidate;
    copy_text(item->path, sizeof(item->path), path);
    if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
        clean_filename(item->name, sizeof(item->name), base);
    if (!item->name[0]) return true;
    const char* ext = strrchr(base, '.');
    if (ext && !strcasecmp(ext, ".epub") && pico_boot_asset_allowed(item->path)) {
        char title[sizeof(item->name)] = {0};
        char author[sizeof(item->author)] = {0};
        if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
            copy_text(item->author, sizeof(item->author), author);
    }
    item->size = (uint32_t)st.st_size;
    // 内置书根固定在 /flash 下，与 book_store.c 和 open_book 的前缀判断一致。
    // The internal root lives under /flash, matching the prefix test in book_store.c and open_book.
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = read_pico_search_match(item->name, s_query);
    item->favorite = scan->favorites && favorite_read_handle(scan->favorites, item->path);
    s_shelf[s_count++] = candidate;
    return true;
}

// 把书根目录之外读过的书补进书架，返回是否新增了条目。
// 阅读只写进度、不动存储版本，所以缓存复用的路径也必须调用它，否则刚读完的书要等手动重扫。
// Backfill books read outside the roots and report whether anything was added. Reading only
// writes progress and never bumps the store revision, so the cached path has to call this too
// or a freshly read book stays invisible until a manual rescan.
static bool shelf_backfill_read_books(bool* truncated) {
    nvs_handle_t favorites = 0;
    nvs_handle_t hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    const int before = s_count;
    shelf_backfill_t backfill = {.favorites = favorites, .hidden = hidden};
    (void)book_progress_list(shelf_backfill_visit, &backfill);
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    return s_count != before;
}

static void scan_shelf(app_ctx_t* ctx) {
    const uint32_t progress_revision=book_progress_revision();
    shelf_category_init();
    for (int i=2;i<s_category_count;++i) s_categories[i].available=false;
    s_count = 0;
    s_visible_count = 0;
    s_message[0] = 0;
    s_shelf_warning[0] = 0;
    // 内置分区每次启动都必须重新挂载；已有文件不会因此格式化。
    // Flash mounts on each boot; existing files are not formatted by that mount.
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int n = 0;
    esp_err_t root_err = book_store_roots(roots, &n);
    bool truncated = false, unreadable = root_err != ESP_OK || book_store_roots_degraded(), skipped = false;
    nvs_handle_t favorites = 0;
    nvs_handle_t hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    s_storage[0] = 0;
    for (int i = 0; i < n; ++i) {
        if (i == 0 || roots[i].is_flash || roots[i - 1].is_flash) {
            char capacity[64];
            snprintf(capacity, sizeof(capacity), "%s%s %.1f MB", s_storage[0] ? " · " : "",
                     roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
            strncat(s_storage, capacity, sizeof(s_storage) - strlen(s_storage) - 1);
        }
        if (s_view == SHELF && app_poll_main_navigation(ctx)) break;
        scan_shelf_dir(ctx, roots[i].path, roots[i].is_flash, favorites, hidden,
                       &truncated, &unreadable, &skipped);
    }
    if (ctx->request_app) {
        if (favorites) nvs_close(favorites);
        if (hidden) nvs_close(hidden);
        s_shelf_cache_valid = false; s_scan_pending = true;
        return;
    }
    // 同路径重新出现也先完成旧清理，避免新阅读进度被后续重试擦掉。
    // Finish old cleanup even if the path reappears, before new reading can create progress.
    for (delete_retry_t* p = s_delete_retries; p; p = p->next) {
        int i = 0;
        while (i < s_count && strcmp(s_shelf[i].path, p->entry.path)) ++i;
        if (i == s_count) {
            if (!shelf_reserve()) { truncated = true; continue; }
            ++s_count;
        }
        s_shelf[i] = p->entry;
        s_shelf[i].selected = false;
        s_shelf[i].search_match = read_pico_search_match(p->entry.name, s_query);
    }
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    // 补齐书根目录之外读过的书；显式移出书架的除外。
    // Backfill books read outside the roots, minus the ones removed from the shelf.
    (void)shelf_backfill_read_books(&truncated);
    shelf_categories_recount();
    sort_shelf(ctx);
    if (s_view==SHELF) shelf_category_restore(ctx);
    s_latest_path[0] = 0;
    char last_path[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(last_path, sizeof(last_path))) {
        for (int i = 0; i < s_count; ++i) {
            if (!strcmp(s_shelf[i].path, last_path) && !s_shelf[i].removed) {
                copy_text(s_latest_path, sizeof(s_latest_path), last_path);
                break;
            }
        }
    }
    if (!n) copy_text(s_storage, sizeof(s_storage), "存储不可用，请检查 TF 卡");
    if (truncated) snprintf(s_shelf_warning, sizeof(s_shelf_warning), "内存不足，仅列 %d 本；释放后重扫", s_count);
    else if (unreadable) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分目录或文件不可读，请检查后重扫");
    else if (skipped) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分文件过大或名称过长，未列入");
    else if (s_pending_invalidated) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "图书已更新，旧待保存进度已作废");
    if (!s_count && !unreadable && !truncated) copy_text(s_message, sizeof(s_message), "暂无图书，请传入 TXT 或 EPUB");
    read_pico_sd_info_t cache_sd = {0};
    read_pico_sd_get_info(&cache_sd);
    s_cache_sd_present = cache_sd.present;
    s_cache_sd_mounted = cache_sd.mounted;
    s_cache_shelf_style = app_settings_shelf_style();
    s_store_revision = book_store_revision();
    s_progress_revision = progress_revision;
    s_shelf_cache_valid = !unreadable && !truncated;
    ESP_LOGI(TAG, "shelf books=%d roots=%d", s_count, n);
}

static void refresh_cached_progress(app_ctx_t* ctx) {
    const uint32_t revision=book_progress_revision();
    if(s_progress_revision==revision){if(s_view==SHELF)shelf_category_restore(ctx);return;}
    s_progress_revision=revision;
    // 目录没变也要补一次：阅读只写进度，不改存储版本，否则刚读完的书不会出现。
    // Backfill even when the listing is unchanged: reading writes progress and never bumps the
    // store revision, so a freshly read book would otherwise stay invisible until a manual rescan.
    const bool added = shelf_backfill_read_books(NULL);
    bool recent_changed = false;
    char latest[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(latest, sizeof(latest))) {
        for (int i = 0; i < s_count; ++i) {
            shelf_entry_t *item = &s_shelf[i];
            if (strcmp(item->path, latest) || item->removed) continue;
            book_progress_t progress;
            item->has_progress = book_progress_load(item->path, item->size, &progress);
            uint32_t next_recent=item->has_progress?progress.last_open_s:0;
            recent_changed=item->recent!=next_recent;
            item->pct = item->has_progress ? progress.pct : 0;
            item->chapter = item->has_progress ? progress.chapter : 0;
            item->recent = next_recent;
            copy_text(s_latest_path, sizeof(s_latest_path), latest);
            break;
        }
    }
    // 新增条目必须重排才能重算可见列表；只刷新进度时沿用原有顺序。
    // Added entries need a re-sort to recompute the visible list; a progress refresh alone
    // keeps the current order.
    if (added || (s_recent_sort && recent_changed)) { shelf_categories_recount(); sort_shelf(ctx); }
    if (s_view==SHELF) shelf_category_restore(ctx);
}
static void return_to_cached_shelf(app_ctx_t* ctx) {
    if (s_shelf_cache_valid && s_store_revision == book_store_revision()) refresh_cached_progress(ctx);
    else scan_shelf(ctx);
}

static void refresh_capacity(void) {
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int count = 0;
    s_storage[0] = 0;
    book_store_roots(roots, &count);
    for (int i = 0; i < count; ++i) {
        if (i && !roots[i].is_flash && !roots[i - 1].is_flash) continue;
        char line[64];
        snprintf(line, sizeof(line), "%s%s %.1f MB", s_storage[0] ? " · " : "", roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
        strncat(s_storage, line, sizeof(s_storage) - strlen(s_storage) - 1);
    }
}
static void manage_apply(app_ctx_t* ctx) {
    if (!strcmp(s_path, s_managed.path)) free_book();
    esp_err_t err;
    if (s_delete_confirm && !s_file_removed) {
        delete_retry_t* retry = delete_retry_reserve(&s_managed);
        if (!retry) {
            copy_text(s_manage_message, sizeof(s_manage_message), "内存不足，无法保留删除重试记录");
            s_clear_confirm = false;
            return;
        }
        bool removed = false;
        err = book_store_delete(s_managed.path, &removed);
        if (removed) {
            retry->entry.removed = true;
            s_file_removed = true;
            pending_discard(s_managed.path);
            book_store_notify_changed();
            s_store_revision = book_store_revision();
            for (int i = 0; i < s_count; ++i) if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].removed = true;
            refresh_capacity();
        } else delete_retry_discard(s_managed.path);
    } else {
        pending_discard(s_managed.path);
        err = book_progress_forget(s_managed.path);
    }
    s_clear_confirm = false;
    if (err != ESP_OK) {
        copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ?
                  "文件已删除，进度清理失败，请重试" : s_delete_confirm ?
                  "文件删除失败，请检查存储后重试" : "进度清理失败，请重新尝试");
        return;
    }
    delete_retry_discard(s_managed.path);
    s_view = SHELF;
    int write = 0, leaf = ctx->leaf;
    for (int i = 0; i < s_count; ++i) {
        if (!strcmp(s_shelf[i].path, s_managed.path)) {
            if (s_file_removed) continue;
            s_shelf[i].has_progress = false; s_shelf[i].pct = 0; s_shelf[i].chapter = 0; s_shelf[i].recent = 0;
        }
        s_shelf[write++] = s_shelf[i];
    }
    s_count = write;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
}

/* ---- 输入与生命周期 / Input and lifecycle ---- */

static bool reader_turn_chrome(app_ctx_t *ctx) {
    bool changed = !s_reader_fullscreen || s_toolbar;
    if (!s_reader_fullscreen) (void)toggle_reader_fullscreen(ctx);
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    if (changed) { invalidate_prep(); s_reader_text_frame = false; }
    return changed;
}
// 长按正文：定位单词 → 离线查词 → 打开释义卡；失败只提示且不改页面状态。
// Long press on the body: locate the word, look it up offline and open the card;
// on failure show a notice without changing the reader state.
// 词典只有英文词头；中文、标点、空格必然查不到，单独提示比「未收录」准确。
// The dictionary holds English heads only; Chinese, punctuation and spaces can never hit,
// so say so instead of claiming the word is missing.
static bool word_is_english(const char* w) {
    bool alpha = false;
    for (const unsigned char* p = (const unsigned char*)w; *p; ++p) {
        if (*p >= 0x80) return false; // 多字节（中文等）不是词头 / multibyte never matches
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) alpha = true;
    }
    return alpha;
}

static app_redraw_t start_dict_lookup(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    (void)ctx;
    const int64_t t_start = esp_timer_get_time();
    char word[DICT_WORD_MAX] = {0};
    bool hit = false, english = false;
    size_t hit_start = 0, hit_end = 0;
    lock_draw();
    if (s_text && s_view == READING) {
        size_t start = 0, end = 0;
        // 判栏 + 右栏坐标换算（v2 §5.1）：双栏时右栏命中要把 x 减回 s_rect 绝对空间，
        // 减去的量与右栏绘制 x 同出 reader_column_offset（横屏坑②配方）；落在重放空间
        // 之外显式失败并告警，绝不静默错位。单栏恒等：page=s_page、cx=x，调用与改形前
        // 逐位一致。休眠期 cols==1 恒不走右栏分支。
        // Column judge and right-column conversion (v2 section 5.1): a two-column
        // right-column hit subtracts x back into the s_rect absolute space — the same
        // reader_column_offset the right column's draw x comes from (the pitfall-2
        // recipe); landing outside the replay space fails loudly instead of silently
        // mis-hitting. Single column is the identity: page=s_page, cx=x, byte-identical
        // to the pre-change call. The right branch stays dormant while cols==1.
        EpdRect area = body_rect();
        int cols = reader_column_count();
        size_t page = s_page;
        int cx = x;
        bool outside = false;
        if (cols > 1 && x >= area.x + area.width / 2) {
            size_t right;
            reader_logical_pages(reader_visual_index(s_page), NULL, &right);
            if (right != SIZE_MAX) {
                page = right;
                cx = x - reader_column_offset(area, cols, 1);
                EpdRect built = book_layout_body_rect();
                if (cx < built.x || cx >= built.x + built.width) {
                    ESP_LOGW(TAG, "dict tap outside the right column: x=%d cx=%d", (int)x, cx);
                    outside = true;
                }
            }
        }
        if (!outside && book_layout_hit_test(page, cx, y, &start, &end) && end > start &&
            end - start < sizeof(word)) {
            memcpy(word, s_text + start, end - start);
            word[end - start] = 0;
            hit_start = start;
            hit_end = end;
            hit = true;
            english = word_is_english(word);
        }
    }
    unlock_draw();
    // 原句抓取放在解锁之后：扫描是有界的字符串操作，不该拉长持锁时间。
    // 此时还没翻页，s_text 一定仍是刚才命中的那一章。
    // Capture the sentence after unlocking: the scan is bounded string work and must
    // not extend the time the lock is held. No page has turned, so s_text is still the
    // chapter the hit came from.
    if (hit && english) vocab_log_capture(s_path, s_text, hit_start, hit_end);
    const int64_t t_hit = esp_timer_get_time();
    // 弹窗避让锚点：按点词 x 选弹出侧——tap 分界与 gutter 中心重合，单栏横屏下同一谓词
    // 成立（与 v2 §5.1 判栏同一谓词）；分半几何随双栏生效，当前单栏横屏维持铺满
    // （split=false，第 4 步双栏开关落地后随栏状态翻开）。锚点必须先于 open 设置：
    // 开放时按最终卡片宽度折行释义。/ Popup avoidance anchor: pick the side by the tap x —
    // the tap boundary coincides with the gutter center, so the same predicate holds in
    // single-column landscape (the same predicate as the v2 section 5.1 column judge);
    // the split geometry takes effect with two columns while single column stays full
    // width (split=false; flipped with the column state once the two-column switch lands
    // at step 4). The anchor must be set before open: the definition wraps to the final
    // card width at open time.
    EpdRect lookup_body = body_rect();
    dict_popup_set_anchor(x < lookup_body.x + lookup_body.width / 2 ? 0 : 1, false);
    bool found = hit && english && dict_popup_open(word);
    ESP_LOGI(TAG, "dict tap word=\"%s\" hit=%d english=%d hittest=%lldms card=%lldms total=%lldms",
             word, (int)hit, (int)english, (long long)((t_hit - t_start) / 1000),
             (long long)((esp_timer_get_time() - t_hit) / 1000),
             (long long)((esp_timer_get_time() - t_start) / 1000));
    if (found) {
        s_reader_panel = READER_PANEL_DICT;
    } else {
        dict_popup_reset();
        s_reader_panel = READER_PANEL_NONE;
        const char* notice = !dict_ready() ? "TF 卡 dict 目录缺少词典"
                           : (hit && !english) ? "只支持查询英文单词"
                           : "词典未收录该词";
        copy_text(s_reader_notice, sizeof(s_reader_notice), notice);
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
    }
    invalidate_prep();
    return APP_REDRAW_PAGE;
}

static app_redraw_t turn_page(app_ctx_t* ctx, int dir) {
    if (!s_text || s_clear_confirm) return APP_REDRAW_NONE;
    // 页面图片表可能随跨章加载替换，旧页资格以实际成功显示的画面为准。
    // Chapter loads may replace the image table; the old-page eligibility comes from the displayed frame.
    bool from_text_frame = s_reader_text_frame;
    // 翻页后单词位置失效，点译卡片必须关闭。/ Word positions change after a turn; close the card.
    if (s_reader_panel == READER_PANEL_DICT) {
        dict_popup_reset();
        s_reader_panel = READER_PANEL_NONE;
    }
    bool changed = true;
    // 翻页语义视觉页化：一次翻页跨过整个视觉页（双栏时一次两栏）；跨章分支不变。
    // Turns advance by visual pages: one turn crosses a whole visual page (both columns
    // when two-column); the cross-chapter branches are unchanged.
    size_t visual = reader_visual_index(s_page);
    if (dir > 0 && visual + 1 < reader_visual_count()) s_page = reader_visual_start(visual + 1);
    else if (dir < 0 && visual) s_page = reader_visual_start(visual - 1);
    else if (dir > 0 && s_chapter + 1 < book_chapter_count()) { save_progress(); changed = load_chapter(ctx, s_chapter + 1, 0, false); }
    else if (dir < 0 && s_chapter) { save_progress(); changed = load_chapter(ctx, s_chapter - 1, 0, true); }
    else return reader_turn_chrome(ctx) ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    if (!changed) { set_reader_view(s_text ? TOC : SHELF); ctx->leaf = s_text ? s_chapter / BOOK_TOC_ROWS : 0; return APP_REDRAW_PAGE; }
    skip_hidden_image_pages(ctx, dir);
    if (!s_text) { s_view = SHELF; return APP_REDRAW_PAGE; }
    // 先选择旧布局的目标页，再切全屏，保留目标位置而不是跨过未读段落。
    // Choose the destination in the old layout first, then reflow to retain its reading position.
    bool chrome_changed = reader_turn_chrome(ctx);
    if (!s_text) return APP_REDRAW_PAGE;
    s_jump_offset = SIZE_MAX;
    s_last_turn_ms = s_stats_activity_ms = ctx->now_ms;
    ++s_stats_pending_turns;
    ++s_session_turns;
    uint8_t full_pages = app_settings_reader_full_pages();
    if (full_pages) ++s_turns;
    else s_turns = 0;
    s_reader_cleanup = full_pages > 0 && s_turns >= full_pages;
    if (s_reader_cleanup) s_turns = 0;
    if (s_unsaved < 8) ++s_unsaved;
    if (s_unsaved >= 8 && !s_save_failed) save_progress();
    ESP_LOGI(TAG, "turn chapter=%u page=%u/%u pct=%u", (unsigned)s_chapter, (unsigned)reader_visual_index(s_page) + 1, (unsigned)reader_visual_count(), percent(s_page));
    // 预渲染未命中时也使用 GL16；到达设定页数后在本次翻页整屏全刷。
    // A cache miss still uses GL16; the selected turn count triggers a full-screen refresh on this turn.
    app_redraw_t redraw = paint_reading(ctx, MODE_GL16);
    s_reader_turn_pending = !chrome_changed && redraw == APP_REDRAW_AREA && from_text_frame &&
        book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_water_turn_pending = !chrome_changed && redraw == APP_REDRAW_AREA && !s_reader_cleanup &&
                           app_settings_reader_turn_effect() == 1 && from_text_frame &&
                           book_layout_page_image_count(s_page) == 0 && !app_settings_book_background_path()[0];
    s_water_turn_dir = dir > 0 ? E0470_TURN_RTL : E0470_TURN_LTR;
    return chrome_changed ? APP_REDRAW_PAGE : redraw;
}
static app_redraw_t resize_text(app_ctx_t* ctx, int dir) {
    int next = s_px + dir * BOOK_PX_STEP;
    if (next < BOOK_PX_MIN || next > BOOK_PX_MAX) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    int old = s_px;
    if (!reader_relayout_cols(s_text, s_text_len, s_blocks, s_block_count, body_rect(), next,
                              reader_column_count())) {
        bool restored = reader_relayout_cols(s_text, s_text_len, s_blocks, s_block_count,
                                             body_rect(), old, reader_column_count());
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    s_px = next;
    s_page = reader_visual_start(reader_visual_for_offset(off));
    unlock_draw();
    app_settings_set_book_px(s_px);
    save_progress();
    return paint_reading(ctx, MODE_DU);
}

static app_redraw_t apply_reader_layout(app_ctx_t* ctx, int next_px, int next_margin,
                                        uint8_t next_line, uint8_t next_para) {
    (void)ctx;
    if (next_px < BOOK_PX_MIN || next_px > BOOK_PX_MAX ||
        next_margin < BOOK_MARGIN_MIN || next_margin > BOOK_MARGIN_MAX ||
        next_line < 110 || next_line > 150 ||
        next_para > 75 || next_para % 25) return APP_REDRAW_NONE;
    if (next_px == s_px && next_margin == s_margin && next_line == app_settings_book_line_spacing() &&
        next_para == app_settings_book_paragraph_spacing()) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    int old_px = s_px, old_margin = s_margin;
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_para = app_settings_book_paragraph_spacing();
    lock_draw();
    invalidate_prep();
    s_px = next_px;
    s_margin = next_margin;
    s_line_spacing = next_line;
    book_layout_set_spacing(next_line, next_para);
    // 正文带随字号与边距变了，题头的预留跟着重算。
    // The body band moved with the size and margin, so the heading's reservation follows.
    refresh_chapter_lead();
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        s_px = old_px;
        s_margin = old_margin;
        s_line_spacing = old_line;
        book_layout_set_spacing(old_line, old_para);
        refresh_chapter_lead();
        (void)reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_px((uint8_t)s_px);
    app_settings_set_book_margin((uint8_t)s_margin);
    app_settings_set_book_line_spacing(next_line);
    app_settings_set_book_paragraph_spacing(next_para);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_typography(app_ctx_t* ctx, int tracking_index) {
    (void)ctx;
    if (!s_text || tracking_index < 0 || tracking_index > 4)
        return APP_REDRAW_NONE;
    int old_index = app_settings_book_tracking();
    if (tracking_index == old_index) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_typography((tracking_index - 2) * 2);
    bool ok = reader_relayout(body_rect_for_tracking(tracking_index));
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        book_layout_set_typography((old_index - 2) * 2);
        (void)reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_tracking((uint8_t)tracking_index);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_indent(app_ctx_t* ctx, int em, int adjustment) {
    (void)ctx;
    if (!s_text || em < 0 || em > 3 || adjustment < -20 || adjustment > 20 ||
        (em == app_settings_book_indent() && adjustment == app_settings_book_indent_adjust()))
        return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    uint8_t old_em = app_settings_book_indent();
    int old_adjustment = app_settings_book_indent_adjust();
    lock_draw();
    invalidate_prep();
    book_layout_set_first_line_indent((unsigned)em);
    book_layout_set_first_line_indent_adjust(adjustment);
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        book_layout_set_first_line_indent(old_em);
        book_layout_set_first_line_indent_adjust(old_adjustment);
        (void)reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_indent((uint8_t)em);
    app_settings_set_book_indent_adjust((int8_t)adjustment);
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_manual_refresh(app_ctx_t* ctx) {
    (void)ctx;
    if (!s_text) return APP_REDRAW_NONE;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = s_reader_cleanup = false;
    s_du_count = s_turns = 0;
    invalidate_prep();
    return APP_REDRAW_FULL;
}

static app_redraw_t apply_reader_option(app_ctx_t* ctx, int kind) {
    (void)ctx;
    if (kind == 0) {
        app_settings_set_reader_power_turn(!app_settings_reader_power_turn());
        return APP_REDRAW_PAGE;
    }
    if (kind == 3) return toggle_reader_landscape(ctx);
    if (kind == 8) {
        // 横屏双栏开关（第 4 步，kind=8——0-3 是既有开关、4-7 是 READING_ROW_* 子面板
        // 槽标记，命名空间顺延）：只翻设置并保存位置，重排由 draw_reader 的
        // ensure_body_orientation 惰性同步——建版矩形（栏宽）与地图栏数在其单出口
        // reader_relayout 内同批切换。横屏未开时开关只存偏好、无几何效果（副标题口径
        // 「横屏时正文分两栏」）；重排失败时 ensure_body_orientation 的回滚路径兜底。
        // Two-column switch (step 4): flip the setting and save the position; the reflow
        // is ensure_body_orientation's lazy sync, where the build rect (column width) and
        // the map's column count switch in one batch inside the reader_relayout single
        // exit. With landscape off the toggle stores a preference with no geometry effect
        // (the subtitle says so); ensure_body_orientation's rollback covers reflow failure.
        app_settings_set_reader_two_column(!app_settings_reader_two_column());
        if (s_text) save_progress();
        return APP_REDRAW_PAGE;
    }
    if (!s_text || (kind != 1 && kind != 2)) return APP_REDRAW_NONE;
    bool previous = kind == 1 ? app_settings_reader_immersive() : app_settings_reader_hide_images();
    // 重新开启插图时，先补齐尺寸；文件读取必须在绘制锁外。
    // Probe missing dimensions before re-enabling images, outside the drawing lock.
    if (kind == 2 && previous) for (size_t i = 0; i < s_block_count; ++i) {
        blk_t *block = &s_blocks[i];
        if (block->image < 0 || (size_t)block->image >= s_image_count ||
            (block->image_width && block->image_height)) continue;
        unsigned width = 0, height = 0;
        if (book_chapter_image_dimensions(s_chapter, s_images[block->image], &width, &height) == ESP_OK) {
            block->image_width = width; block->image_height = height;
        }
        vTaskDelay(1);
    }
    lock_draw();
    size_t off = book_layout_page_start_offset(s_page);
    invalidate_prep();
    if (kind == 1) app_settings_set_reader_immersive(!previous);
    else {
        app_settings_set_reader_hide_images(!previous);
        book_layout_set_images_visible(previous);
    }
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        if (kind == 1) app_settings_set_reader_immersive(previous);
        else {
            app_settings_set_reader_hide_images(previous);
            book_layout_set_images_visible(!previous);
        }
        (void)reader_relayout(body_rect());
        copy_text(s_reader_notice, sizeof(s_reader_notice), "排版失败，请重试");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
    }
    unlock_draw();
    if (ok) {
        release_page_images();
        prepare_inline_image();
        s_jump_offset = SIZE_MAX;
        save_progress();
    }
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = false;
    return APP_REDRAW_PAGE;
}

// 阅读设置面板热区：固定卡片保持硬编码（一次性面板，无行模型可言），
// 开关行走行表驱动，与绘制锚点同一事实源。/ Refresh-settings hot zones: the fixed
// cards stay hardcoded (one-off panel chrome), the toggle rows are table-driven so
// hit-testing and drawing share one source of truth.
// 阅读设置的点击派发：行表驱动——标题区与「完成」先判，再逐槽命中，按槽位类型分发。
// 旧版那几张固定卡片（手动全刷/自动全刷/翻页效果）在 r91 已并入卡片槽位，派发随之改走行表。
// Tap dispatch for the reading settings: table driven. Header and Done are checked first,
// then the first slot hit decides and the cell type dispatches. r91 folded the old fixed
// cards (manual refresh / auto refresh / turn effect) into slots, so dispatch follows the table.
// 沿用上游 r91 的函数名：上游新增的 reading_settings_host_test 按名抽取它，
// 实现仍是我方行表驱动（槽位类型决定派发）。/ Keep upstream r91's name: its new
// reading_settings_host_test extracts it by name; the body stays our table-driven
// dispatcher (the cell type decides).
static app_redraw_t reading_settings_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (y < reading_settings_top() || ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y))
        s_reader_panel = READER_PANEL_NONE;
    else {
        int hit = -1;
        for (int i = 0; i < reading_row_count(); ++i) {
            if (ui_rect_hit(reading_toggle_rect(i), x, y)) { hit = i; break; }
        }
        if (hit < 0) return APP_REDRAW_NONE;
        switch (s_reading_rows[hit].kind) {
            case READING_ROW_KEYS: s_reader_panel = READER_PANEL_KEY_SETTINGS; break;
            case READING_ROW_TURN: s_reader_panel = READER_PANEL_TURN_SETTINGS; break;
            case READING_ROW_FULL: {
                static const uint8_t options[] = {5, 10, 15, 30, 0};
                unsigned next = 0;
                for (unsigned i = 0; i < 5; ++i)
                    if (app_settings_reader_full_pages() == options[i]) { next = (i + 1) % 5; break; }
                app_settings_set_reader_full_pages(options[next]);
                s_turns = 0;
                break;
            }
            case READING_ROW_WAVE:
                app_settings_set_reader_turn_effect(!app_settings_reader_turn_effect());
                break;
            default: return apply_reader_option(ctx, s_reading_rows[hit].kind);
        }
    }
    invalidate_prep();
    return APP_REDRAW_PAGE;
}

// 返回主页以本次开书来源为准；分类页码已在书架开书前记忆，不被目录/工具栏覆盖。
// Return to this opening's origin; shelf category/page were saved before opening, independently of TOC/tools.
static app_redraw_t reader_return(app_ctx_t *ctx) {
    save_progress();
    if (s_reader_return_home) {
        ui_nav_request(ctx, 0);
        return APP_REDRAW_NONE;
    }
    free_book();
    s_view = SHELF;
    s_reader_panel = READER_PANEL_NONE;
    return_to_cached_shelf(ctx);
    return APP_REDRAW_PAGE;
}

static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx) {
    (void)ctx;
    if (!s_text) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    s_reader_fullscreen = !s_reader_fullscreen;
    // 全屏切换会换掉整个正文带，题头的预留必须跟着换。
    // Toggling full screen swaps the whole body band, so the heading's reservation must follow.
    refresh_chapter_lead();
    bool ok = reader_relayout(body_rect());
    bool restored = true;
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        s_reader_fullscreen = !s_reader_fullscreen;
        refresh_chapter_lead();
        restored = reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) {
        if (!restored) {
            free_book();
            s_view = SHELF;
            copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    release_page_images();
    prepare_inline_image();
    s_jump_offset = SIZE_MAX;
    s_reader_panel = READER_PANEL_NONE;
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t toggle_reader_landscape(app_ctx_t* ctx) {
    (void)ctx;
    // 只翻设置并保存阅读位置(字节偏移与方向无关，重排后按偏移映射回同一处)；
    // 方向切换由 render() 在面板关闭时一次完成，重排由 draw_reader 的
    // ensure_body_orientation() 按版面尺寸惰性同步，这里不做临时旋转。
    // Flip the setting and save the position (the byte offset is orientation
    // independent and maps back to the same spot after the reflow); render() flips the
    // orientation once when the panel closes and draw_reader's ensure_body_orientation()
    // reflows lazily by body size — no temporary rotation dance here.
    app_settings_set_reader_landscape(!app_settings_reader_landscape());
    if (s_text) save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t select_reading_font_item(app_ctx_t* ctx, const ttf_font_item_t* item) {
    if (!item || !strcmp(ttf_font_path(), item->path)) return APP_REDRAW_NONE;
    char previous[TTF_FONT_PATH_MAX];
    copy_text(previous, sizeof(previous), ttf_font_path());
    if (ttf_font_open(item->path) != ESP_OK) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else if (previous[0]) {
        (void)ttf_font_open(previous);
        (void)reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_font_path(item->path);
    copy_text(s_font_path, sizeof(s_font_path), item->path);
    save_progress();
    return APP_REDRAW_PAGE;
}

static int slider_index(EpdRect rect, int x, int count) {
    int span = rect.width - 128;
    int pos = x - rect.x - 64;
    int index = span > 0 && count > 1 ? (pos * (count - 1) + span / 2) / span : 0;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return index;
}
static int reader_margin_input(EpdRect rect, int x, int current, int px, int tracking_px) {
    int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
    int count = reader_margin_levels(px, tracking_px, widths, values);
    int level = reader_margin_level_for(current, px, tracking_px);
    if (x < rect.x + 64) --level;
    else if (x >= rect.x + rect.width - 64) ++level;
    else level = slider_index(rect, x, count);
    return reader_margin_for_level(level, current, px, tracking_px);
}

static EpdRect reader_slider_rect(int slider) {
    // 字号滑杆在双列字体卡片上方，绘制与拖动共用位置。/ The size rail sits above the paired cards; paint and drag share its position.
    if (slider == 0) return (EpdRect){36,700-reader_ui_extra(112),612,76};
    if (slider == 1) return (EpdRect){36, 620, 294, 66};
    if (slider == 2) return (EpdRect){354, 620, 294, 66};
    if (slider == 3) return (EpdRect){36, 740, 612, 66};
    return (EpdRect){36, 860, 612, 66};
}

static int reader_slider_at(uint16_t x, uint16_t y) {
    int first = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 0 : 1;
    int end = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 1 : 5;
    for (int i = first; i < end; ++i) if (ui_rect_hit(reader_slider_rect(i), x, y)) return i;
    return -1;
}

static void reader_slider_map(int slider, int x) {
    if (slider == 0) s_reader_preview_px = BOOK_PX_MIN + slider_index(reader_slider_rect(0), x, 37);
    else if (slider == 1) {
        int widths[BOOK_MARGIN_CHOICES], values[BOOK_MARGIN_CHOICES];
        int count = reader_margin_levels(s_px, (app_settings_book_tracking() - 2) * 2, widths, values);
        s_reader_preview_margin = reader_margin_for_level(
            slider_index(reader_slider_rect(1), x, count), s_reader_preview_margin,
            s_px, (app_settings_book_tracking() - 2) * 2);
    }
    else if (slider == 2) s_reader_preview_line = 110 + slider_index(reader_slider_rect(2), x, 41);
    else if (slider == 3) s_reader_preview_para = slider_index(reader_slider_rect(3), x, 4) * 25;
    else if (slider == 4) s_reader_preview_tracking = slider_index(reader_slider_rect(4), x, 5);
}

static void reader_slider_begin(int slider, int x) {
    s_reader_slider = slider;
    s_reader_preview_px = s_px;
    s_reader_preview_margin = s_margin;
    s_reader_preview_line = app_settings_book_line_spacing();
    s_reader_preview_para = app_settings_book_paragraph_spacing();
    s_reader_preview_tracking = app_settings_book_tracking();
    EpdRect rect = reader_slider_rect(slider);
    int *value = slider == 0 ? &s_reader_preview_px : slider == 1 ? &s_reader_preview_margin :
                 slider == 2 ? &s_reader_preview_line : slider == 3 ? &s_reader_preview_para :
                 &s_reader_preview_tracking;
    s_reader_slider_endpoint = x < rect.x + 64 || x >= rect.x + rect.width - 64;
    if (slider == 1) {
        *value = reader_margin_input(rect, x, *value, s_px, (app_settings_book_tracking() - 2) * 2);
        return;
    }
    int min = slider == 0 ? BOOK_PX_MIN : slider == 2 ? 110 : 0;
    int max = slider == 0 ? BOOK_PX_MAX : slider == 2 ? 150 : slider == 3 ? 75 : 4;
    int step = slider == 3 ? 25 : 1;
    if (x < rect.x + 64) *value -= step;
    else if (x >= rect.x + rect.width - 64) *value += step;
    else reader_slider_map(slider, x);
    if (*value < min) *value = min;
    if (*value > max) *value = max;
}

static void reader_slider_move(int x) {
    int slider = s_reader_slider;
    if (slider < 0) return;
    s_reader_slider_endpoint = false;
    reader_slider_map(slider, x);
}

static app_redraw_t reader_slider_commit(app_ctx_t* ctx) {
    if (s_reader_slider < 0) return APP_REDRAW_NONE;
    int slider = s_reader_slider;
    int px = s_reader_preview_px, margin = s_reader_preview_margin, line = s_reader_preview_line;
    int para = s_reader_preview_para;
    int tracking = s_reader_preview_tracking;
    s_reader_slider = -1;
    if (slider == 4) return apply_reader_typography(ctx, tracking);
    return apply_reader_layout(ctx, px, margin, line, (uint8_t)para);
}


static app_redraw_t reader_panel_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_reader_panel == READER_PANEL_BACKGROUND || s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW)
        return reader_background_action(x, y);
    // 点译卡片（我方）：与上面的阅读背景面板互斥，任何点击都先由它处理。
    // The lookup card (ours): mutually exclusive with the background panel above,
    // and it handles every tap first.
    // 点译卡片：任何点击都关闭，避免误触下面的正文翻页。
    // The lookup card closes on any tap so body page turns cannot fire beneath it.
    if (s_reader_panel == READER_PANEL_DICT) {
        // chip 上切换生词本并保留卡片；点其它地方照旧关闭。
        // The chip toggles the vocabulary book and keeps the card; anywhere else closes.
        dict_popup_tap_t tap = dict_popup_tap(x, y);
        if (tap == DICT_POPUP_TAP_MISSED) {
            dict_popup_reset();
            s_reader_panel = READER_PANEL_NONE;
        } else if (tap == DICT_POPUP_TAP_REMOVED && vocab_log_is_view_path(s_path)) {
            // 在生词本视图里删了一条：重新渲染并回到原来那一页。
            // Removed an entry while reading the vocabulary view: re-render and land
            // back on the same page.
            dict_popup_reset();
            s_reader_panel = READER_PANEL_NONE;
            return open_vocab_view(ctx, true);
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_TOOLS) {
        for (int i = 0; i < BOOK_TOOL_COUNT; ++i) if (ui_rect_hit(tool_rect(i), x, y)) {
            invalidate_prep();
            if (i == 0) {
                save_progress();
                set_reader_view(TOC);
                s_message[0] = 0;
                s_toc_jump_open = s_toc_jump_drag = false;
                s_reader_panel = READER_PANEL_NONE;
                size_t toc = current_toc_position();
                ctx->leaf = toc == SIZE_MAX ? 0 : (int)(toc / BOOK_TOC_ROWS);
            } else if (i == 1) {
                (void)bookmark_toggle();
            } else if (i == 2) {
                s_reader_panel = READER_PANEL_STATS;
            } else if (i == 3) {
                s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
            } else if (i == 4) {
                s_line_spacing = app_settings_book_line_spacing();
                s_reader_slider = -1;
                s_reader_panel = READER_PANEL_FONT_SETTINGS;
            } else {
                // 5 生词本：渲染虚拟书并打开，原书进度由 open_book_impl 先存好。
                // Vocabulary book: render the virtual book and open it; the current
                // book's progress is saved first by open_book_impl().
                return open_vocab_view(ctx, false);
            }
            return APP_REDRAW_PAGE;
        }
        s_reader_panel = READER_PANEL_NONE;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_STATS) {
        const int top = 768;
        EpdRect details = {36, top + 319, 294, 78}, recent = {354, top + 319, 294, 78};
        if (ui_rect_hit(details, x, y)) {
            s_bookmark_page = 0;
            s_bookmark_edit = s_bookmark_delete_confirm = s_bookmark_delete_error = false;
            s_bookmark_selected = 0;
            s_reader_panel = READER_PANEL_BOOKMARKS;
        }
        else if (ui_rect_hit(recent, x, y)) {
            flush_ticket_stats();
            s_recent_days_valid = book_ticket_recent_days(s_recent_days);
            s_reader_panel = READER_PANEL_STATS_RECENT;
        } else s_reader_panel = y < top ? READER_PANEL_NONE : READER_PANEL_TOOLS;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_BOOKMARKS) {
        const int top = 560;
        if (s_bookmark_delete_confirm) {
            if (y >= top + 348 && y < top + 412) {
                if (x < 342) s_bookmark_delete_confirm = s_bookmark_delete_error = false;
                else if (bookmark_delete_selected()) s_bookmark_delete_confirm = s_bookmark_delete_error = false;
                else s_bookmark_delete_error = true;
            }
        } else if (y < top) s_reader_panel = READER_PANEL_NONE;
        else if (y < top + 86) {
            if (x >= 544 && bookmark_count()) {
                s_bookmark_edit = !s_bookmark_edit;
                s_bookmark_selected = 0;
                s_bookmark_page = 0;
            } else s_reader_panel = READER_PANEL_STATS;
        } else {
            reader_bookmarks_t marks = {0};
            bool valid = bookmark_load(&marks);
            int pages = bookmark_pages(valid ? marks.count : 0);
            if (s_bookmark_edit && y >= top + 520 && y < top + 584) {
                if (x < 303) {
                    int first = s_bookmark_page * bookmark_rows();
                    for (int row = 0; row < bookmark_rows() && first + row < marks.count; ++row)
                        s_bookmark_selected |= UINT32_C(1) << (marks.count - 1 - (first + row));
                } else if (s_bookmark_selected) s_bookmark_delete_confirm = true;
            } else if (y >= top + (s_bookmark_edit ? 590 : 570)) {
                if (x < UI_LOCK_WIDTH / 2 && s_bookmark_page > 0) --s_bookmark_page;
                else if (x >= UI_LOCK_WIDTH / 2 && s_bookmark_page + 1 < pages) ++s_bookmark_page;
            } else if (valid && y >= top + 96) {
                int row = ((int)y - (top + 96)) / bookmark_row_step();
                int position = s_bookmark_page * bookmark_rows() + row;
                if (row >= 0 && row < bookmark_rows() && position < marks.count &&
                    ui_rect_hit(bookmark_row_rect(row),x,y)) {
                    int newest = (int)marks.count - 1 - position;
                    reader_bookmark_entry_t mark = marks.entries[newest];
                    if (s_bookmark_edit) s_bookmark_selected ^= UINT32_C(1) << newest;
                    else if (mark.chapter < book_chapter_count() && load_chapter(ctx, mark.chapter, mark.byte_off, false)) {
                        s_reader_panel = READER_PANEL_NONE;
                        save_progress();
                    }
                }
            }
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_STATS_RECENT) {
        int top = 584;
        if (y < top) s_reader_panel = READER_PANEL_NONE;
        else if (y < top + 112) s_reader_panel = READER_PANEL_STATS;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
        if (s_reader_panel == READER_PANEL_REFRESH_SETTINGS) return reading_settings_action(ctx, x, y);
    if (s_reader_panel == READER_PANEL_KEY_SETTINGS || s_reader_panel == READER_PANEL_KEY_PICKER) {
        bool picker = s_reader_panel == READER_PANEL_KEY_PICKER;
        if (y < 270) s_reader_panel = READER_PANEL_NONE;
        else if (ui_rect_hit((EpdRect){36, 299, 56, 56}, x, y) || ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y))
            s_reader_panel = picker ? READER_PANEL_KEY_SETTINGS : READER_PANEL_REFRESH_SETTINGS;
        else if (picker) {
            for (int i = 0; i < APP_READER_KEY_COUNT; ++i) if (ui_rect_hit((EpdRect){36, 390 + i * 94, 612, 78}, x, y)) {
                bool saved = s_reader_key_pick == 3 ? app_settings_set_reader_hold_action(i)
                    : app_settings_set_reader_key_action(s_reader_key_pick, i);
                if (!saved) {
                    copy_text(s_reader_key_error, sizeof(s_reader_key_error), "保存失败，请重试");
                    return APP_REDRAW_PAGE;
                }
                s_reader_panel = READER_PANEL_KEY_SETTINGS;
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        } else {
            for (unsigned i = 0; i < 3; ++i) if (ui_rect_hit(reader_key_card(i), x, y)) {
                s_reader_key_pick = i;
                s_reader_key_error[0] = 0;
                s_reader_panel = READER_PANEL_KEY_PICKER;
                return APP_REDRAW_PAGE;
            }
            if (ui_rect_hit((EpdRect){36, 646, 612, 96}, x, y)) {
                s_reader_key_pick = 3;
                s_reader_key_error[0] = 0;
                s_reader_panel = READER_PANEL_KEY_PICKER;
                return APP_REDRAW_PAGE;
            }
            for (unsigned i = 1; i <= 2; ++i) if (ui_rect_hit(reader_key_preset_card(i), x, y)) {
                app_settings_set_reader_key_preset(i);
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_TURN_SETTINGS) {
        if (y < 270) s_reader_panel = READER_PANEL_NONE;
        else if (ui_rect_hit((EpdRect){36, 299, 56, 56}, x, y)) s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
        else if (ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y)) s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
        else {
            for (int i = 0; i < 2; ++i) {
                if (!ui_rect_hit((EpdRect){36, 370 + i * 322, 612, 292}, x, y)) continue;
                app_settings_set_reader_vertical_turn(i != 0);
                invalidate_prep();
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_WEIGHT_SETTINGS) {
        if (y < 690 || y >= 1141) {
            s_reader_panel = y < 580 ? READER_PANEL_NONE : READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        int step=reader_weight_step(x,y),weight=app_settings_book_weight_fine();
        if(step) {
            int next=weight+step;if(next < -APP_BOOK_WEIGHT_FINE_MAX || next > APP_BOOK_WEIGHT_FINE_MAX)return APP_REDRAW_NONE;
            lock_draw();
            invalidate_prep();
            bool ok = app_settings_set_book_weight_fine(next);
            if (ok) ttf_set_stroke_adjust_fine(next);
            unlock_draw();
            if (!ok) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "保存失败，请重试");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
            }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_reader_panel == READER_PANEL_FONT_SETTINGS) {
        const int top = reader_font_top();
        EpdRect size = reader_slider_rect(0);
        EpdRect font = s_font_card;
        EpdRect layout = s_layout_card;
        EpdRect rule = s_rule_card;
        EpdRect bookfont = s_bookfont_card;
        if (ui_rect_hit((EpdRect){36, 1141, 612, 58}, x, y)) {
            s_reader_panel = READER_PANEL_NONE;
            invalidate_prep(); return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(size, x, y)) {
            int px = BOOK_PX_MIN + slider_index(size, x, BOOK_PX_MAX - BOOK_PX_MIN + 1);
            return apply_reader_layout(ctx, px, s_margin, app_settings_book_line_spacing(),
                                       app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(font, x, y)) {
            ttf_font_scan();
            s_font_page = 0;
            int count = ttf_font_count();
            const char* active = ttf_font_path();
            for (int i = 0; i < count; ++i) {
                const ttf_font_item_t* item = ttf_font_item(i);
                if (item && !strcmp(active, item->path)) { s_font_page = i / BOOK_FONT_PAGE; break; }
            }
            s_reader_panel = READER_PANEL_FONT_PICKER;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(s_weight_card, x, y)) {
            s_reader_panel = READER_PANEL_WEIGHT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(s_antialias_card, x, y)) {
            lock_draw();
            invalidate_prep();
            bool on = !app_settings_book_antialias();
            bool ok = app_settings_set_book_antialias(on);
            if (ok) ttf_set_antialias(on);
            unlock_draw();
            if (!ok) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "保存失败，请重试");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
            }
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(layout, x, y)) {
            s_reader_slider = -1;
            s_reader_panel = READER_PANEL_LAYOUT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(rule, x, y)) {
            s_reader_panel = READER_PANEL_RULE_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(bookfont, x, y)) {
            // 触摸回调的栈比 on_tick 深得多：在这里直接重排一章会把主任务栈顶穿（实测溢出过一次）。
            // 只记下意愿，交给下一轮 on_tick 在浅栈上做。
            // The touch callback runs far deeper than on_tick, and laying a chapter out from here
            // overran the main task stack (measured once). Record the intent and let the next
            // on_tick do it on a shallow stack.
            bool on = !app_settings_book_fonts();
            app_settings_set_book_fonts(on);
            s_book_fonts_applied = on;
            book_set_embedded_fonts(on);
            s_fonts_reload_pending = true;
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        s_reader_panel = y < top ? READER_PANEL_NONE : READER_PANEL_TOOLS;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) {
        const int top = 500;
        EpdRect back_icon = {36, top + 29, 56, 56};
        EpdRect back_button = {36, 1132, 612, 72};
        if (ui_rect_hit(back_icon, x, y) || ui_rect_hit(back_button, x, y)) {
            s_reader_slider = -1;
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        EpdRect margin = reader_slider_rect(1);
        EpdRect line = reader_slider_rect(2);
        EpdRect paragraph = reader_slider_rect(3);
        EpdRect tracking = reader_slider_rect(4);
        if (ui_rect_hit(margin, x, y)) {
            return apply_reader_layout(ctx, s_px,
                                       reader_margin_input(margin, x, s_margin, s_px,
                                                           (app_settings_book_tracking() - 2) * 2),
                                       app_settings_book_line_spacing(), app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(line, x, y)) {
            uint8_t value = (uint8_t)(110 + slider_index(line, x, 41));
            return apply_reader_layout(ctx, s_px, s_margin, value,
                                       app_settings_book_paragraph_spacing());
        }
        if (ui_rect_hit(paragraph, x, y))
            return apply_reader_layout(ctx, s_px, s_margin, app_settings_book_line_spacing(),
                                       (uint8_t)(slider_index(paragraph, x, 4) * 25));
        if (ui_rect_hit(tracking, x, y))
            return apply_reader_typography(ctx, slider_index(tracking, x, 5));
        for (int i = 0; i < 4; ++i) {
            EpdRect choice = {214 + i * 108, 970, 100, 64};
            if (ui_rect_hit(choice, x, y)) return apply_reader_indent(ctx, i, app_settings_book_indent_adjust());
        }
        for (int i = 0; i < 3; ++i) {
            if (!ui_rect_hit(indent_adjust_rect(i), x, y)) continue;
            int adjustment = i == 1 ? 0 : app_settings_book_indent_adjust() + (i == 0 ? -1 : 1);
            if (adjustment < -20) adjustment = -20;
            if (adjustment > 20) adjustment = 20;
            return apply_reader_indent(ctx, app_settings_book_indent(), adjustment);
        }
        s_reader_panel = y < top ? READER_PANEL_NONE : READER_PANEL_FONT_SETTINGS;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_reader_panel == READER_PANEL_RULE_SETTINGS) {
        if (y < 729 || ui_rect_hit((EpdRect){36, 1113, 612, 72}, x, y)) {
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        for (int i = 0; i < 3; ++i) {
            if (!ui_rect_hit(rule_style_rect(i), x, y)) continue;
            app_settings_set_book_reading_line((uint8_t)i);
            book_layout_set_reading_line((unsigned)i);
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        if (app_settings_book_reading_line()) {
            int offset = app_settings_book_reading_line_offset();
            if (ui_rect_hit(s_rule_offset_up, x, y)) offset -= 2;
            else if (ui_rect_hit(s_rule_offset_reset, x, y)) offset = 0;
            else if (ui_rect_hit(s_rule_offset_down, x, y)) offset += 2;
            else return APP_REDRAW_NONE;
            if (offset < -8) offset = -8;
            if (offset > 8) offset = 8;
            app_settings_set_book_reading_line_offset((int8_t)offset);
            book_layout_set_reading_line_offset(offset);
            invalidate_prep();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_reader_panel == READER_PANEL_FONT_PICKER) {
        const int top = reader_font_picker_top();
        if (y < top + 112) {
            s_reader_panel = READER_PANEL_FONT_SETTINGS;
            return APP_REDRAW_PAGE;
        }
        int count = ttf_font_count(), start = s_font_page * BOOK_FONT_PAGE;
        for (int slot = 0; slot < BOOK_FONT_PAGE && start + slot < count; ++slot) {
            EpdRect card = reader_font_pick_rect(slot);
            if (ui_rect_hit(card, x, y)) return select_reading_font_item(ctx, ttf_font_item(start + slot));
        }
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t adjust_spacing(app_ctx_t* ctx, int control) {
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_para = app_settings_book_paragraph_spacing();
    uint8_t line = old_line, para = old_para;
    if (control == 3 && line > 120) line -= 30;
    if (control == 4 && line < 180) line += 30;
    if (control == 5 && para > 0) para -= 25;
    if (control == 6 && para < 75) para += 25;
    if (control == 17) { line = 150; para = 50; }
    if (line == old_line && para == old_para) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_spacing(line, para);
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    else {
        book_layout_set_spacing(old_line, old_para);
        (void)reader_relayout(body_rect());
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_line_spacing(line);
    app_settings_set_book_paragraph_spacing(para);
    save_progress();
    return paint_reading(ctx, MODE_DU);
}
static app_redraw_t select_reading_font(app_ctx_t* ctx, int style) {
    static const char* filenames[] = {
        "Song.ttf", "Hei.ttf", "Kai.ttf", "WenKai.ttf", "FangSong.ttf", "KingHwa.ttf",
        "CangErYunHei05.ttf", "ChillKai.ttf"
    };
    static const char* names[] = {"宋体", "黑体", "楷体", "文楷", "仿宋", "京华老宋", "仓耳云黑05", "寒蝉正楷"};
    char path[160];
    if (snprintf(path, sizeof(path), "%s/%s", app_settings_fonts_dir(), filenames[style]) >= sizeof(path)) return APP_REDRAW_NONE;
    if (!strcmp(ttf_font_path(), path)) return APP_REDRAW_NONE;
    struct stat font_stat;
    if (stat(path, &font_stat) != 0) {
        snprintf(s_message, sizeof(s_message), "请将 %s 字体放入所选字体目录", names[style]);
        return APP_REDRAW_PAGE;
    }
    if (ttf_font_open(path) != ESP_OK) {
        snprintf(s_message, sizeof(s_message), "请将 %s 字体放入所选字体目录", names[style]);
        return APP_REDRAW_PAGE;
    }
    app_settings_set_font_path(path);
    s_message[0] = 0;
    size_t off = book_layout_page_start_offset(s_page);
    save_progress();
    lock_draw();
    invalidate_prep();
    bool ok = reader_relayout(body_rect());
    if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    unlock_draw();
    if (!ok) {
        free_book(); s_view = SHELF;
        copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书");
        return APP_REDRAW_PAGE;
    }
    save_progress();
    return paint_reading(ctx, MODE_GL16);
}
static app_redraw_t manage_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (ui_rect_hit(manage_back_rect(), x, y)) {
        if (s_clear_confirm) s_clear_confirm = false;
        else s_view = SHELF;
        return APP_REDRAW_PAGE;
    }
    if (!s_clear_confirm && !s_file_removed && ui_rect_hit((EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 190, ui_content_width(), 68}, x, y)) {
        extern void app_files_request_folder(int folder);
        app_files_request_folder(0);
        ui_nav_request(ctx, 2);
        return APP_REDRAW_NONE;
    }
    int count = s_clear_confirm || s_file_removed ? 2 : 3;
    for (int i = 0; i < count; ++i) if (ui_rect_hit(manage_rect(i, count), x, y)) {
        if (s_clear_confirm) { if (!i) s_clear_confirm = false; else manage_apply(ctx); }
        else if (!i) s_view = SHELF;
        else if (s_file_removed) manage_apply(ctx);
        else { s_clear_confirm = true; s_delete_confirm = i == 2; s_manage_message[0] = 0; }
        break;
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_save(app_ctx_t* ctx) {
    if (ui_keyboard_pending()) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor_title;
    while (*start == ' ') ++start;
    if (start != s_editor_title) memmove(s_editor_title, start, strlen(start) + 1);
    size_t len = strlen(s_editor_title);
    while (len && s_editor_title[len - 1] == ' ') s_editor_title[--len] = 0;
    if (!s_editor_title[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "书名不能为空");
        return APP_REDRAW_PAGE;
    }
    esp_err_t err = book_title_set(s_managed.path, s_editor_title);
    if (err != ESP_OK) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查书名或存储");
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, s_managed.path)) copy_text(s_shelf[i].name, sizeof(s_shelf[i].name), s_editor_title);
    free(s_editor_cover); s_editor_cover = NULL;
    ui_keyboard_end();
    s_view = SHELF;
    sort_shelf(ctx);
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_paint(app_ctx_t* ctx, bool field) {
    bool search = s_view == SEARCH;
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560,
        search ? (EpdRect){UI_MARGIN, 200, ui_content_width(), 88} : (EpdRect){36, 424, 612, 83},
        search ? UI_PX_BODY : 31, false, NULL, field);
    s_area = update.area; s_mode = update.layout ? MODE_GL16 : MODE_DU; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t editor_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (y < 134) {
        if (x < 160) { ui_keyboard_end(); free(s_editor_cover); s_editor_cover = NULL; s_view = SHELF; return APP_REDRAW_PAGE; }
        if (x > 510) return editor_save(ctx);
        return APP_REDRAW_NONE;
    }
    if (ui_text_input_tap(&s_editor_input, (EpdRect){36, 424, 612, 83}, 31,
                          false, NULL, x, y)) return editor_paint(ctx, true);
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
    if (result == UI_KEYBOARD_DONE) return editor_save(ctx);
    return result == UI_KEYBOARD_CHANGED ? editor_paint(ctx, false) : APP_REDRAW_NONE;
}
// 成功项退出选择，已删文件的失败项仅重试元数据。/ Deselect successes; removed-file failures retry metadata only.
static void batch_apply(app_ctx_t* ctx) {
    unsigned done = 0, failed = 0;
    int write = 0;
    const bool unshelf = s_batch_kind == BATCH_UNSHELF;
    for (int i = 0; i < s_count; ++i) {
        shelf_entry_t item = s_shelf[i];
        bool discard = false;
        if (item.selected) {
            esp_err_t err = ESP_OK;
            if (unshelf) {
                // 只隐藏条目：文件与阅读进度都留在原处，再次打开这本书会重新上书架。
                // Hide the entry only: file and progress stay, and reopening the book puts it back.
                if (shelf_hidden_save(item.path, true)) discard = true;
                else err = ESP_FAIL;
            } else {
                if (!strcmp(s_path, item.path)) free_book();
                if (s_batch_kind == BATCH_DELETE && !item.removed) {
                    delete_retry_t* retry = delete_retry_reserve(&item);
                    if (!retry) { ++failed; s_shelf[write++] = item; continue; }
                    bool removed = false;
                    err = book_store_delete(item.path, &removed);
                    if (removed) {
                        retry->entry.removed = true;
                        item.removed = true; pending_discard(item.path);
                        book_store_notify_changed(); s_store_revision = book_store_revision();
                    } else delete_retry_discard(item.path);
                } else { pending_discard(item.path); err = book_progress_forget(item.path); }
            }
            if (err == ESP_OK) {
                delete_retry_discard(item.path);
                ++done; item.selected = false;
                if (!unshelf) {
                    item.has_progress = false; item.pct = 0;
                    item.chapter = 0; item.recent = 0;
                }
                discard = discard || item.removed;
            } else ++failed;
        }
        if (!discard) s_shelf[write++] = item;
    }
    s_count = write;
    refresh_capacity();
    int leaf = ctx->leaf;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
    s_batch_confirm = false;
    if (unshelf)
        snprintf(s_batch_message, sizeof(s_batch_message), "已移出书架 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
    else
        snprintf(s_batch_message, sizeof(s_batch_message), "成功 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
}
static app_redraw_t batch_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_batch_confirm) {
        if (ui_rect_hit(ui_row_rect(0, 2, 620, UI_BTN_H), x, y)) {
            s_batch_confirm = false;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(ui_row_rect(1, 2, 620, UI_BTN_H), x, y)) {
            batch_apply(ctx);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    for (int i = 0; i < 6; ++i) if (ui_rect_hit(batch_rect(i), x, y)) {
        s_batch_message[0] = 0;
        if (!i) select_page(ctx->leaf);
        else if (i == 1) clear_selection();
        else if (i == 2) { clear_selection(); s_batch_message[0] = 0; scan_shelf(ctx); }
        else if (selected_count()) {
            s_batch_kind = i == 3 ? BATCH_DELETE : i == 4 ? BATCH_CLEAR : BATCH_UNSHELF;
            s_batch_confirm = true;
        }
        else copy_text(s_batch_message, sizeof(s_batch_message), "请先选择图书");
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t bulk_turn_page(app_ctx_t* ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_scan_pending || s_batch_confirm || next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t bulk_finish(app_ctx_t* ctx) {
    int first = ctx->leaf * BOOK_BULK_ROWS;
    clear_selection();
    s_batch_message[0] = 0;
    s_batch_confirm = false;
    s_view = SHELF;
    ctx->leaf = first / shelf_rows();
    if (ctx->leaf >= leaves()) ctx->leaf = leaves() - 1;
    return APP_REDRAW_PAGE;
}
// 批量管理单独路由，不复用带示例菜单的旧底栏。
// Route batch management independently of the legacy bar and demo menu.
static app_redraw_t bulk_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (ui_rect_hit(manage_back_rect(), x, y)) {
        if (s_batch_confirm) { s_batch_confirm = false; return APP_REDRAW_PAGE; }
        return bulk_finish(ctx);
    }
    app_redraw_t result = batch_action(ctx, x, y);
    if (result != APP_REDRAW_NONE || s_batch_confirm || s_scan_pending) return result;
    for (int i = 0; i < 3; ++i) if (ui_rect_hit(bulk_filter_rect(i), x, y)) {
        if (i == 2) { search_begin(); return APP_REDRAW_PAGE; }
        if (i == 0) s_filter = (s_filter + 1) % 3;
        else { s_recent_sort = !s_recent_sort; app_settings_set_shelf_recent_sort(s_recent_sort); }
        clear_selection(); s_batch_message[0] = 0;
        sort_shelf(ctx);
        return APP_REDRAW_PAGE;
    }
    int nav = bulk_nav_hit(x, y);
    if (nav == 1) return bulk_finish(ctx);
    if (nav == 0 || nav == 2) return bulk_turn_page(ctx, nav == 0 ? -1 : 1);
    for (int row = 0; row < BOOK_BULK_ROWS; ++row) {
        int index = ctx->leaf * BOOK_BULK_ROWS + row;
        if (index < s_visible_count && ui_rect_hit(row_rect(row), x, y)) {
            toggle_selection(index); s_batch_message[0] = 0;
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}
static int bulk_control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    *rect = manage_back_rect();
    if (ui_rect_hit(*rect, x, y)) return 711;
    if (s_batch_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 620, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 600 + i;
        }
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        *rect = bulk_filter_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 110 + i;
        *rect = bulk_nav_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    for (int i = 0; i < 6; ++i) {
        *rect = batch_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 610 + i;
        if (ctx->leaf * BOOK_BULK_ROWS + i < s_visible_count) {
            *rect = row_rect(i);
            if (ui_rect_hit(*rect, x, y)) return i;
        }
    }
    return -1;
}
static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_view == BULK) return bulk_action(ctx, x, y);
    if (s_view == IMPORT) {
        if (y < 160 && x < 120) { s_view = SHELF; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(x, y);
        if (tab == 1) { s_view = SHELF; return APP_REDRAW_PAGE; }
        if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        for (int i = 0; i < 4; ++i) if (ui_rect_hit(import_rect(i), x, y)) {
            if (i == 0) {
                extern const app_desc_t app_files;
                ctx->request_app = &app_files;
            } else {
                extern const app_desc_t app_transfer;
                if (i == 1) app_transfer_request_wifi_upload();
                else if (i == 2) app_transfer_request_hotspot_start();
                else app_transfer_request_usb_start();
                ctx->request_app = &app_transfer;
            }
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == TOC) {
        if (s_toc_jump_open) {
            int choice = book_toc_jump_hit(x, y);
            if (choice == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_percent = book_toc_jump_percent(x);
                return APP_REDRAW_PAGE;
            }
            if (choice == BOOK_TOC_JUMP_CONFIRM)
                ctx->leaf = book_toc_page_from_percent(s_toc_jump_percent, leaves());
            s_toc_jump_open = s_toc_jump_drag = false;
            return APP_REDRAW_PAGE;
        }
        int target = book_toc_hit(x, y, ctx->leaf, book_navigation_count());
        if (target == BOOK_TOC_BACK) {
            s_message[0] = 0;
            set_reader_view(READING);
            s_reader_panel = READER_PANEL_NONE;
            return APP_REDRAW_PAGE;
        }
        if (target == BOOK_TOC_PREV || target == BOOK_TOC_NEXT ||
            target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT) {
            int step = target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT ? 10 : 1;
            int next = ctx->leaf + (target == BOOK_TOC_PREV || target == BOOK_TOC_SKIP_PREV ? -step : step);
            if (next < 0) next = 0;
            if (next >= leaves()) next = leaves() - 1;
            if (next != ctx->leaf) { ctx->leaf = next; return APP_REDRAW_PAGE; }
            return APP_REDRAW_NONE;
        }
        if (target == BOOK_TOC_JUMP && book_navigation_count()) {
            s_toc_jump_open = true;
            s_toc_jump_percent = (ctx->leaf + 1) * 100 / leaves();
            return APP_REDRAW_PAGE;
        }
        if (target >= 0 && !s_message[0]) {
            size_t chapter = book_navigation_chapter((size_t)target);
            if (chapter >= book_chapter_count()) return APP_REDRAW_NONE;
            save_progress();
            set_reader_view(READING);
            if (load_chapter_at(ctx, chapter, 0, false, book_navigation_anchor((size_t)target),
                                book_navigation_source_offset((size_t)target))) {
                s_selected_toc = (size_t)target;
                char entry_title[sizeof(s_title)];
                if (book_navigation_title((size_t)target, entry_title, sizeof(entry_title)) == ESP_OK)
                    copy_text(s_title, sizeof(s_title), entry_title);
                s_view = READING;
                s_reader_panel = READER_PANEL_NONE;
                save_progress();
            } else set_reader_view(s_text ? TOC : SHELF);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == MANAGE) return manage_action(ctx, x, y);
    if (s_view == SEARCH) {
        if (y < 160 && x < 160) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (ui_text_input_tap(&s_search_input, (EpdRect){UI_MARGIN, 200, ui_content_width(), 88},
                              UI_PX_BODY, false, NULL, x, y)) return editor_paint(ctx, true);
        if (ui_rect_hit((EpdRect){36, 392, 180, 66}, x, y)) { s_search_draft[0] = 0; ui_text_edit_init(&s_search_input, s_search_draft, sizeof(s_search_draft)); ui_keyboard_begin(&s_search_input, false); return APP_REDRAW_PAGE; }
        ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE || (y < 160 && x > 510 && !ui_keyboard_pending())) { search_finish(ctx, true); return APP_REDRAW_PAGE; }
        if (result == UI_KEYBOARD_CHANGED) return editor_paint(ctx, false);
        return APP_REDRAW_NONE;
    }
    if (s_view == READING) {
        if (s_toolbar && !(s_reader_panel == READER_PANEL_TOOLS && y < 1096 && ui_rect_hit(body_rect(), x, y)))
            return reader_panel_action(ctx, x, y);
        if (!s_reader_fullscreen && ui_rect_hit((EpdRect){32, 75, 64, 64}, x, y))
            return reader_return(ctx);
        if (!s_reader_fullscreen && x >= reader_favorite_x() - 21 && y >= 76 && y < 145) {
            bool next = !s_reader_favorite;
            if (!favorite_save(s_path, next)) {
                copy_text(s_reader_notice, sizeof(s_reader_notice), "收藏保存失败");
                s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
                return APP_REDRAW_PAGE;
            }
            s_reader_favorite = next;
            for (int i = 0; i < s_count; ++i)
                if (!strcmp(s_shelf[i].path, s_path)) s_shelf[i].favorite = next;
            sort_shelf(ctx);
            render(ctx, ctx->fb);
            s_area = (EpdRect){584, 76, 64, 70};
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
        // 通栏插图铺满整屏，点在图两侧的边条上同样要翻页；文字页仍按正文栏判定。
        // A full-bleed illustration spans the panel, so taps on its side strips must turn pages
        // too; text pages keep the body column.
        EpdRect tap_area = body_rect();
        if (reader_image_slots(s_page)) tap_area = ui_rect_union(tap_area, book_layout_image_rect());
        if (!ui_rect_hit(tap_area, x, y)) return APP_REDRAW_NONE;
        int target = book_reader_tap_feed(&s_reader_tap, tap_area, x, y,
                                         app_settings_reader_vertical_turn(),
                                         app_settings_book_vertical(), ctx->now_ms);
        if (target == BOOK_READER_TAP_FULLSCREEN) return toggle_reader_fullscreen(ctx);
        if (target) return turn_page(ctx, target);
        return APP_REDRAW_NONE;
    }
    if (s_view == SHELF) {
        if (s_view == SHELF) {
            int tab = ui_nav_hit(x, y);
            if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
            if (ui_rect_hit(shelf_page_arrow_rect(-1), x, y)) return shelf_turn_page(ctx, -1);
            if (ui_rect_hit(shelf_page_arrow_rect(1), x, y)) return shelf_turn_page(ctx, 1);
        }
        if (s_view == SHELF && ui_rect_hit(shelf_import_rect(), x, y)) {
            s_view = IMPORT;
            return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) {
            clear_selection();
            s_batch_confirm = false;
            s_batch_message[0] = 0;
            s_view = BULK;
            ctx->leaf = 0;
            return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF && s_save_failed && y >= UI_CONTENT_BOTTOM - UI_PX_CAPTION && y < UI_CONTENT_BOTTOM) {
            retry_progress();
            return APP_REDRAW_PAGE;
        }
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return APP_REDRAW_NONE;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    for (int row = 0; row < rows; ++row) if (ui_rect_hit(row_rect(row), x, y)) {
        int i = ctx->leaf * rows + row;
        if (s_view == SHELF && i < s_visible_count) {
            shelf_category_remember(ctx);
            if (s_shelf[i].removed) {
                s_managed = s_shelf[i]; s_view = MANAGE; s_file_removed = s_delete_confirm = true; s_clear_confirm = false;
                copy_text(s_manage_message, sizeof(s_manage_message), "文件已删除，进度清理失败，请重试");
            } else open_book(ctx, s_shelf[i].path);
        }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void on_enter(app_ctx_t* ctx) {
    shelf_category_init();
    s_category_menu=false;
    s_reader_tap.pending = false;
    s_shelf_page_pending = false;
    // 设置页试按的旧事件不得在开书后翻页。/ Discard remote tests from earlier pages before opening a book.
    ble_pt_event_t old_key;
    ble_pt_raw_t old_raw;
    while (ble_pt_pop_key(&old_key)) {}
    while (ble_pt_pop_raw(&old_raw)) {}
    book_layout_set_spacing(app_settings_book_line_spacing(), app_settings_book_paragraph_spacing());
    book_layout_set_images_visible(!app_settings_reader_hide_images());
    // 插图出血宽不在此发布：所有权已收归建版单出口 reader_relayout_cols（G7-A1 修复）——
    // 通栏语义随栏数快照同批驱动（单栏整屏宽、双栏栏宽），进页只设排版参数。
    // The illustration bleed width is not published here: ownership moved to the single
    // layout exit reader_relayout_cols (G7-A1 fix) so full-bleed semantics ride the column
    // snapshot (panel width at one column, column width at two); entry sets typography
    // knobs only.
    // 竖排是阅读设置里的手动开关，进页时同步给排版；关着就是原来的横排。
    // Vertical setting is a manual reader switch, published to the layout on entry; off is the
    // original horizontal setting.
    book_layout_set_vertical(app_settings_book_vertical());
    book_layout_set_typography(((int)app_settings_book_tracking() - 2) * 2);
    book_layout_set_first_line_indent(app_settings_book_indent());
    book_layout_set_first_line_indent_adjust(app_settings_book_indent_adjust());
    book_layout_set_reading_line(app_settings_book_reading_line());
    book_layout_set_reading_line_offset(app_settings_book_reading_line_offset());
    // 字体页可能刚改过书内字体开关；回正文时重排当前章，别让用户对着旧排版猜。
    // The font page may have just changed the embedded-face switch; re-typeset this chapter
    // so the reader is not left guessing at stale type.
    bool book_fonts = app_settings_book_fonts();
    if (book_fonts != s_book_fonts_applied) {
        s_book_fonts_applied = book_fonts;
        book_set_embedded_fonts(book_fonts);
        if (s_text && book_kind() == BOOK_KIND_EPUB)
            (void)load_chapter(ctx, s_chapter, book_layout_page_start_offset(s_page), false);
    }
    s_reader_fullscreen = false;
    s_bookmark_edit = s_bookmark_delete_confirm = s_bookmark_delete_error = false;
    s_bookmark_selected = 0;
    clear_selection();
    s_view = s_requested_manage ? BULK : SHELF;
    s_reader_return_home = false;
    s_requested_manage = false;
    s_presented_view = -1;
    ctx->leaf = s_view==SHELF && s_categories ? s_categories[s_category_selected].pages[app_settings_shelf_style()==5] : 0;
    s_px = app_settings_book_px();
    s_recent_sort = app_settings_shelf_recent_sort();
    s_margin = app_settings_book_margin();
    s_line_spacing = app_settings_book_line_spacing();
    // 传书页已停止并 join HTTP，安全注销且只丢弃对应路径的旧进度。
    // Transfer has stopped and joined HTTP; safely unregister only invalidated paths.
    pending_drop_invalidated();
    s_reader_panel = READER_PANEL_NONE;
    dict_popup_reset();
    // 预热词典：把「首次长按需付的打开开销」挪到进阅读页时；失败不设退避（见 dict_warm）。
    // Warm the dictionary on entry so the first long press does not pay for opening it.
    if (!dict_ready()) {
        const int64_t warm_start = esp_timer_get_time();
        const bool warmed = dict_warm();
        ESP_LOGI(TAG, "dict warm ok=%d ms=%lld", (int)warmed,
                 (long long)((esp_timer_get_time() - warm_start) / 1000));
    }
    // 生词本索引同样预热，把读卡开销挪出查词路径（失败会退化成保存时扫文件）。
    // Warm the vocabulary index too, keeping the card read off the lookup path.
    vocab_log_warm();
    s_reader_slider = -1;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_du_count = 0;
    s_shelf_feedback_pending = false;
    s_cover_batch_present_pending = false;
    s_reader_cleanup = false;
    s_poll_ms = 0;
    read_pico_sd_info_t sd = {0};
    esp_err_t media_err = read_pico_sd_get_info(&sd);
    bool reuse = s_shelf_cache_valid && media_err != ESP_ERR_NOT_FINISHED &&
                 sd.present == s_cache_sd_present && sd.mounted == s_cache_sd_mounted &&
                 s_cache_shelf_style == app_settings_shelf_style() &&
                 s_store_revision == book_store_revision();
    s_scan_pending = !reuse;
    if (reuse) {
        refresh_cached_progress(ctx);
        (void)open_requested_book(ctx);
    } else {
        s_count = s_visible_count = 0;
        s_message[0] = 0;
        s_storage[0] = 0;
        invalidate_covers();
        // 已指定文件可直接打开；书架目录留到真正进入书架时再扫描。
        // Open a requested file directly; scan the shelf only when it is actually shown.
        if (open_requested_book(ctx)) {
            s_scan_pending = false;
            return;
        }
        read_pico_sd_start_probe();
    }

}
static void book_on_exit(app_ctx_t* ctx) {
    book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;
    shelf_category_remember(ctx);
    s_category_menu=false;
    (void)ctx;
    // 惊喜2 反向通道：离开的若是手机推来的虚拟书，就 notify EVT_READ(0x14)——
    // 「推→读→已读」连招的最后一环，手机收到后清剪贴板。放在退出处而非翻开处：
    // 读没读完只有退出这一刻才说得上，且不影响阅读页任何既有路径。
    // Surprise 2's reverse channel: if the book being left is the phone-pushed
    // virtual book, notify EVT_READ(0x14) — the last step of the
    // push->read->read chain, which clears the phone's clipboard. Placed on exit
    // rather than on open: "finished reading" is only knowable at exit, and no
    // existing reader path is touched.
    if (s_path[0] && strcmp(s_path, PUSH_TEXT_PATH) == 0) push_sink_notify_read();
    // gen19（真机 Bug3）：离开 push.txt 不再收摊——包括单词卡/空数据回合。
    // 真机实证：单词卡在 book app 渲染，退出走了旧 cancel（`armed canceled by
    // 退出阅读器，接收结束`），回首页状态卡直接「未开启」，与长文交接路径
    // 行为分裂。保活语义统一：窗口到期/系统级入口收摊，离页不收。
    // gen19 (real-hw Bug3): leaving push.txt no longer tears the session down -
    // including word-card/empty-payload rounds. Real-hw proof: the word card
    // renders inside the book app, so its exit hit the old cancel ("armed
    // canceled by reader exit") and the home card flipped to "off", diverging
    // from the long-text handoff path. Unified semantics: the window expiry
    // and system-level entries close up; leaving a page never does.
    if (s_path[0] && strcmp(s_path, PUSH_TEXT_PATH) == 0) {
        s_push_handoff = false; // 交接标记照旧清位，防旧标记过夜 / clear the stale handoff flag as before
    }
    // 离开阅读即退出全工程唯一的横屏语境：物理旋转若停在 LANDSCAPE，后续任何
    // 页面（Home/菜单/其他 app）都按竖屏绘制——真机实证长按退出后 Home 竖屏
    // 布局画在横屏物理屏上（半屏错位）。这里兜底复位；唯一一次刷新由 app_loop
    // 随后的 present 完成（旋转已归位，竖屏 framebuffer 全量重画后差分收敛），
    // 不在此处自行刷新。与 prepare_lock 的复位互为两面：本处覆盖 app 间切换，
    // 那处覆盖不经切换的系统画面（锁屏/睡眠/关机）。
    // Leaving the reader exits the only landscape context in the tree: if the
    // panel is still LANDSCAPE, every following page (Home/menu/other apps)
    // draws portrait — observed on hardware as a half-screen Home after a
    // long-press exit. Reset here as the fallback; app_loop's subsequent
    // present performs the single refresh (the portrait framebuffer repaints
    // fully and the diff converges), so no extra refresh here. This and
    // prepare_lock's reset are two faces of one boundary: this one covers
    // app-to-app switches, that one covers system frames without a switch
    // (lock/sleep/shutdown).
    if (epd_get_rotation() == EPD_ROT_LANDSCAPE) epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    s_reader_tap.pending = false;
    s_shelf_page_pending = false;
    ui_keyboard_end();
    s_presented_view = -1;
    free(s_editor_cover); s_editor_cover = NULL;
    s_pressed_control = -1;
    s_reader_slider = -1;
    save_progress();
    s_reader_return_home = false;
    ui_reader_background_release();
    free(s_backgrounds); s_backgrounds = NULL; s_background_count = 0;
    free_book();
    free(s_next_fb);
    s_next_fb = NULL;
    if(s_prep_task){vTaskDelete(s_prep_task);s_prep_task=NULL;}
    if(s_prep_done){vSemaphoreDelete(s_prep_done);s_prep_done=NULL;}
    if (s_draw_lock) { vSemaphoreDelete(s_draw_lock); s_draw_lock = NULL; }
    dict_popup_reset();
    dict_close();
    vocab_log_invalidate();
}
// 先停止使用旧卡句柄与字体预渲染，主循环随后切换内置字体。
// Stop old-card handles and font preparation before the loop switches to the builtin font.
static void book_on_media_lost(app_ctx_t* ctx) {
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_reader_tap.pending = false;
    lock_draw();
    save_progress();
    free_book();
    unlock_draw();
    free(s_editor_cover); s_editor_cover = NULL;
    s_view = SHELF;
    s_count = s_visible_count = 0;
    s_shelf_cache_valid = false;
    free(s_shelf); s_shelf = NULL; s_shelf_capacity = 0;
    invalidate_covers();
    ctx->leaf = 0;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_scan_pending = true;
    s_du_count = 0;
    dict_popup_reset();
    dict_close(); // 词典句柄来自失效的卡 / the dictionary file came from the lost card
    vocab_log_invalidate(); // 索引指向的卡已经没了 / the index points at a card that is gone
    copy_text(s_storage, sizeof(s_storage), "TF 卡已移除");
    copy_text(s_message, sizeof(s_message), "TF 卡已移除");
}
static void book_on_media_ready(app_ctx_t* ctx) {
    (void)ctx;
    s_shelf_cache_valid = false;
    if (s_view == SHELF) s_scan_pending = true;
}
// 控件编号仅用于保持按下与抬起命中同一个目标。/ IDs pair a press with release on the same control.
static int control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    if (s_view==SHELF && (s_category_menu || ui_rect_hit((EpdRect){24,79,388,74},x,y))) return -1;
    if (s_view == BULK) return bulk_control_at(ctx, x, y, rect);
    if (s_view == MANAGE) {
        *rect = manage_back_rect();
        if (ui_rect_hit(*rect, x, y)) return 711;
    }
    if (s_view == IMPORT) {
        *rect = (EpdRect){36, 79, 58, 58};
        if (ui_rect_hit(*rect, x, y)) return 700;
        for (int i = 0; i < 4; ++i) {
            *rect = import_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 701 + i;
        }
        int tab = ui_nav_hit(x, y);
        if (tab >= 0) { *rect = (EpdRect){tab * 171, UI_NAV_TOP, 171, 120}; return 705 + tab; }
        return -1;
    }
    if (s_view == SEARCH) {
        for (int i = 0; i < 45; ++i) { *rect = search_rect(i); if (ui_rect_hit(*rect, x, y)) return 500 + i; }
        return -1;
    }
    if (s_view == MANAGE) {
        if (!s_clear_confirm && !s_file_removed) {
            *rect = (EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 274, ui_content_width(), 68};
            if (ui_rect_hit(*rect, x, y)) return 405;
            *rect = (EpdRect){UI_MARGIN, manage_panel().y + manage_panel().height - 190, ui_content_width(), 68};
            if (ui_rect_hit(*rect, x, y)) return 404;
        }
        int count = s_clear_confirm || s_file_removed ? 2 : 3;
        for (int i = 0; i < count; ++i) {
            *rect = manage_rect(i, count);
            if (ui_rect_hit(*rect, x, y)) return s_clear_confirm ? 300 + i : s_file_removed && i == 1 ? 403 : 400 + i;
        }
        return -1;
    }
    if (s_clear_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 610, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 300 + i;
        }
        return -1;
    }
    if (s_view == READING) {
        if (s_reader_panel == READER_PANEL_TOOLS) for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            *rect = tool_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 200 + i;
        }
        return -1;
    }
    if (s_view != SHELF) for (int i = 0; i < 3; ++i) {
        *rect = ui_bar_rect(i, 3);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) { *rect = shelf_manage_rect(); return 114; }
    if (s_view == SHELF && ui_rect_hit(shelf_import_rect(), x, y)) { *rect = shelf_import_rect(); return 115; }
    if (s_view == SHELF) {
        for (int i = 0; i < 2; ++i) {
            *rect = shelf_page_arrow_rect(i ? 1 : -1);
            if (ui_rect_hit(*rect, x, y)) return 116 + i;
        }
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return -1;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    int count = s_visible_count;
    for (int row = 0; row < rows && ctx->leaf * rows + row < count; ++row) {
        *rect = row_rect(row);
        if (ui_rect_hit(*rect, x, y)) return row;
    }
    return -1;
}
static app_redraw_t paint_control(app_ctx_t* ctx, EpdRect rect) {
    if (s_view == SHELF && app_settings_shelf_style() == 5) {
        for (int row = 0; row < shelf_rows(); ++row) {
            EpdRect target = row_rect(row);
            if (memcmp(&rect, &target, sizeof(rect))) continue;
            s_area = paint_list_cover_feedback(ctx, row);
            s_mode = app_settings_main_fast_refresh() ? MODE_DU : MODE_GL16;
            s_shelf_feedback_pending = true;
            return APP_REDRAW_AREA;
        }
    }
    // 小按钮反馈只重画按钮，不重新绘制全部封面；其余装饰沿用原绘制路径。
    // Repaint small header buttons alone rather than every cover; other decorations retain their path.
    if (s_view == SHELF && !memcmp(&rect, &(EpdRect){442,94,97,54}, sizeof(rect)))
        draw_shelf_header_button(ctx->fb, rect, false);
    else if (s_view == SHELF && !memcmp(&rect, &(EpdRect){551,94,97,54}, sizeof(rect)))
        draw_shelf_header_button(ctx->fb, rect, true);
    else render(ctx, ctx->fb);
    s_mode = MODE_DU;
    s_shelf_feedback_pending = false;
    if (s_view == SHELF) {
        for (int row = 0; row < shelf_rows(); ++row) {
            EpdRect target = row_rect(row);
            if (rect.x != target.x || rect.y != target.y ||
                rect.width != target.width || rect.height != target.height) continue;
            // 覆盖抽出前后的并集，释放或取消时也擦净上方的旧封面。
            // Refresh both book positions so release or cancellation clears the lifted edge.
            rect.y -= SHELF_BOOK_LIFT_PX;
            rect.height += SHELF_BOOK_LIFT_PX;
            s_mode = MODE_GL16;
            s_shelf_feedback_pending = true;
            break;
        }
    }
    s_area = rect;
    return APP_REDRAW_AREA;
}
// 左右滑动始终可用；上下点按模式另外保留上下滑动，不改变中间轻点切换全屏。
// Horizontal swipes work in both tap modes; vertical mode also keeps vertical swipes.
// 从屏幕底部120px上滑呼出工具栏；其他面板保留自己的滑动，正文中部仍按翻页模式解释。
// Open tools from the bottom 120px; other panels keep their own swipes and mid-body gestures retain turn semantics.
static bool reader_tools_swipe(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_view != READING || !s_text || s_reader_panel != READER_PANEL_NONE ||
        s_clear_confirm || s_scan_pending || ctx->consumed || ev->type != UI_GESTURE_SWIPE_U ||
        ev->x0 >= UI_LOCK_WIDTH || ev->y0 < UI_LOCK_HEIGHT - 120 || ev->y0 >= UI_LOCK_HEIGHT) return false;
    s_reader_tap.pending = false;
    s_pressed_control = s_reader_slider = -1;
    s_reader_panel = READER_PANEL_TOOLS;
    s_stats_activity_ms = ctx->now_ms;
    ctx->user_activity = true;
    invalidate_prep();
    return true;
}
static int reader_swipe_direction(ui_gesture_type_t type, bool vertical) {
    int direction = 0;
    if (type == UI_GESTURE_SWIPE_L || (vertical && type == UI_GESTURE_SWIPE_U)) direction = 1;
    else if (type == UI_GESTURE_SWIPE_R || (vertical && type == UI_GESTURE_SWIPE_D)) direction = -1;
    if (direction && !vertical && app_settings_book_vertical()) direction = -direction;
    return direction;
}

static app_redraw_t gesture_event(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_view==SHELF && (s_category_menu || ui_rect_hit((EpdRect){24,79,388,74},ev->x0,ev->y0)))
        return shelf_category_gesture(ctx,ev);
    if (s_view == READING && (s_reader_panel == READER_PANEL_BACKGROUND ||
        s_reader_panel == READER_PANEL_BACKGROUND_PREVIEW) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        return reader_background_step(ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
    }
    if (ev->type == UI_GESTURE_CANCEL || ev->type == UI_GESTURE_LONG_PRESS ||
        ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R ||
        ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D ||
        (ev->type == UI_GESTURE_PRESS && !book_reader_center(body_rect(), ev->x0, ev->y0)))
        s_reader_tap.pending = false;
    if (reader_tools_swipe(ctx, ev)) return APP_REDRAW_PAGE;
    bool input_feedback = false;
    if (s_view == SEARCH || s_view == EDIT) {
        if (ev->type == UI_GESTURE_PRESS)
            return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? editor_paint(ctx, false) : APP_REDRAW_NONE;
        input_feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
    }
    if (s_view == SEARCH) {
        if (ev->type == UI_GESTURE_TAP) return action_at(ctx, ev->x0, ev->y0);
        if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
            if (!ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1) && !input_feedback) return APP_REDRAW_NONE;
            return editor_paint(ctx, false);
        }
        return input_feedback ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_view == TOC) {
        if (s_toc_jump_open) {
            if (ev->type == UI_GESTURE_PRESS &&
                book_toc_jump_hit(ev->x0, ev->y0) == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_drag = true;
                s_toc_jump_percent = book_toc_jump_percent(ev->x0);
                return APP_REDRAW_PAGE;
            }
            if (s_toc_jump_drag && ev->type == UI_GESTURE_MOVE) {
                int next = book_toc_jump_percent(ev->x);
                if (next == s_toc_jump_percent) return APP_REDRAW_NONE;
                s_toc_jump_percent = next;
                render(ctx, ctx->fb);
                s_area = (EpdRect){44, 356, 596, 572};
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
            if (s_toc_jump_drag && ev->type != UI_GESTURE_PRESS) {
                s_toc_jump_drag = false;
                if (ev->type == UI_GESTURE_CANCEL) return APP_REDRAW_NONE;
                s_toc_jump_percent = book_toc_jump_percent(ev->x);
                return APP_REDRAW_PAGE;
            }
            if (ev->type == UI_GESTURE_TAP) return action_at(ctx, ev->x0, ev->y0);
            return APP_REDRAW_NONE;
        }
        if (ev->type == UI_GESTURE_TAP) {
            int start = book_toc_hit(ev->x0, ev->y0, ctx->leaf, book_navigation_count());
            int end = book_toc_hit(ev->x, ev->y, ctx->leaf, book_navigation_count());
            if (start != -1 && start == end) return action_at(ctx, ev->x0, ev->y0);
        }
        if (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D) {
            int next = ctx->leaf + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
            if (next >= 0 && next < leaves()) { ctx->leaf = next; return APP_REDRAW_PAGE; }
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == EDIT) {
        if (ev->type == UI_GESTURE_TAP) return editor_action(ctx, ev->x0, ev->y0);
        if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)
            return (ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1) || input_feedback) ? editor_paint(ctx, false) : APP_REDRAW_NONE;
        return input_feedback ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_view == SHELF && ev->type == UI_GESTURE_TAP && ui_nav_hit(ev->x0, ev->y0) >= 0) {
        ui_nav_request(ctx, ui_nav_hit(ev->x0, ev->y0));
        return APP_REDRAW_NONE;
    }
    if (s_view == READING && ev->type != UI_GESTURE_PRESS) s_stats_activity_ms = ctx->now_ms;
    if (s_view == READING &&
        (s_reader_panel == READER_PANEL_FONT_SETTINGS || s_reader_panel == READER_PANEL_LAYOUT_SETTINGS)) {
        if (ev->type == UI_GESTURE_PRESS) {
            int slider = reader_slider_at(ev->x0, ev->y0);
            if (slider >= 0) {
                reader_slider_begin(slider, ev->x0);
                render(ctx, ctx->fb);
                s_area = reader_slider_refresh_rect(slider);
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
        } else if (s_reader_slider >= 0) {
            if (ev->type == UI_GESTURE_MOVE) {
                int slider = s_reader_slider;
                reader_slider_move(ev->x);
                render(ctx, ctx->fb);
                s_area = reader_slider_refresh_rect(slider);
                s_mode = MODE_GL16;
                return APP_REDRAW_AREA;
            }
            if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_SWIPE_L ||
                ev->type == UI_GESTURE_SWIPE_R || ev->type == UI_GESTURE_SWIPE_U ||
                ev->type == UI_GESTURE_SWIPE_D || ev->type == UI_GESTURE_LONG_PRESS) {
                if (!s_reader_slider_endpoint) reader_slider_move(ev->x);
                return reader_slider_commit(ctx);
            }
            if (ev->type == UI_GESTURE_CANCEL) {
                s_reader_slider = -1;
                return APP_REDRAW_PAGE;
            }
        }
    }
    if (s_view == READING && s_reader_panel == READER_PANEL_FONT_PICKER &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int count = ttf_font_count();
        int pages = count > 0 ? (count + BOOK_FONT_PAGE - 1) / BOOK_FONT_PAGE : 1;
        if (ev->type == UI_GESTURE_SWIPE_U && s_font_page + 1 < pages) ++s_font_page;
        else if (ev->type == UI_GESTURE_SWIPE_D && s_font_page > 0) --s_font_page;
        else return APP_REDRAW_NONE;
        return APP_REDRAW_PAGE;
    }
    EpdRect start_rect = {0}, end_rect = {0};
    int start = control_at(ctx, ev->x0, ev->y0, &start_rect);
    int end = control_at(ctx, ev->x, ev->y, &end_rect);
    if (ev->type == UI_GESTURE_PRESS) {
        s_pressed_control = start;
        return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
    }
    bool decorated = s_pressed_control >= 0;
    s_pressed_control = -1;
    if (ev->type == UI_GESTURE_LONG_PRESS && start == end) {
        if (s_view == SHELF && start >= 0 && start < shelf_rows() && !s_clear_confirm) {
            s_managed = s_shelf[ctx->leaf * shelf_rows() + start];
            s_view = MANAGE;
            s_clear_confirm = false;
            s_delete_confirm = s_file_removed = s_managed.removed;
            copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ? "文件已删除，进度清理失败，请重试" : "");
            return APP_REDRAW_PAGE;
        }
        if (s_view == READING && !s_toolbar && !s_clear_confirm && ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            // 长按改为点词查译；目录仍可从工具条进入。
            // A long press now looks up the word; the TOC is still on the toolbar.
            return start_dict_lookup(ctx, ev->x0, ev->y0);
        }
    }
    if (ev->type == UI_GESTURE_TAP && !s_scan_pending) {
        if (start >= 0 && start == end) {
            // 列表先完成回弹，才释放封面缓存并开书；没有人为等待或整页预刷新。
            // Settle a list bounce before opening frees artwork, without a delay or whole-page pre-refresh.
            if (decorated && s_view == SHELF && app_settings_shelf_style() == 5 && start < shelf_rows())
                (void)present(ctx, paint_control(ctx, start_rect));
            app_redraw_t result = action_at(ctx, ev->x0, ev->y0);
            if (ctx->request_app || ctx->request_menu || ctx->request_return) return result;
            return result != APP_REDRAW_NONE ? result : paint_control(ctx, start_rect);
        }
        if (start < 0 && end < 0 && s_view == READING && !s_clear_confirm)
            return action_at(ctx, ev->x0, ev->y0);
    }
    if (!s_clear_confirm && !s_scan_pending) {
        if (s_view == READING && (!s_toolbar || (s_reader_panel == READER_PANEL_TOOLS && ev->y0 < 1096)) &&
            ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            int direction = reader_swipe_direction(ev->type, app_settings_reader_vertical_turn());
            if (direction) { ctx->user_activity = true; return turn_page(ctx, direction); }
        } else if (s_view == SHELF && (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)) {
            app_redraw_t result = shelf_turn_page(ctx, ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
            if (result != APP_REDRAW_NONE) return result;
        } else if ((s_view == BULK && !s_batch_confirm) &&
                   (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
            return bulk_turn_page(ctx, ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
        }
    }
    return decorated ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
}
static app_redraw_t reader_key_action(app_ctx_t *ctx, app_reader_key_action_t action) {
    s_reader_tap.pending = false;
    switch (action) {
        case APP_READER_KEY_PREV: case APP_READER_KEY_NEXT:
            s_stats_activity_ms = ctx->now_ms;
            return turn_page(ctx, action == APP_READER_KEY_PREV ? -1 : 1);
        case APP_READER_KEY_HOME: return reader_return(ctx);
        case APP_READER_KEY_FULLSCREEN: return toggle_reader_fullscreen(ctx);
        case APP_READER_KEY_REFRESH: return reader_manual_refresh(ctx);
        case APP_READER_KEY_TOOLS:
            s_reader_slider = -1;
            s_reader_panel = s_reader_panel == READER_PANEL_TOOLS ? READER_PANEL_NONE : READER_PANEL_TOOLS;
            invalidate_prep();
            return APP_REDRAW_PAGE;
        default: return APP_REDRAW_NONE;
    }
}
static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    s_reader_tap.pending = false;
    s_pressed_control = -1;
    if (s_view == TOC && s_toc_jump_open) {
        if (key == UI_KEY_2) s_toc_jump_open = s_toc_jump_drag = false;
        else if (key == UI_KEY_1 || key == UI_KEY_3) {
            s_toc_jump_percent += key == UI_KEY_1 ? -5 : 5;
            if (s_toc_jump_percent < 0) s_toc_jump_percent = 0;
            if (s_toc_jump_percent > 100) s_toc_jump_percent = 100;
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING && s_view != TOC) {
        if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
        if (s_view == EDIT) { ui_keyboard_end(); free(s_editor_cover); s_editor_cover = NULL; s_view = MANAGE; return APP_REDRAW_PAGE; }
        if (s_view == SEARCH) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (s_batch_confirm) { s_batch_confirm = false; return APP_REDRAW_PAGE; }
        if (s_view == MANAGE || s_view == BULK || s_view == IMPORT) {
            if (s_view == BULK) return bulk_finish(ctx);
            s_view = SHELF; return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    }
    if (s_scan_pending || s_clear_confirm) return APP_REDRAW_NONE;
    if (s_view == READING && (key == UI_KEY_1 || key == UI_KEY_2 || key == UI_KEY_3))
        return reader_key_action(ctx, app_settings_reader_key_action(key == UI_KEY_1 ? 0 : key == UI_KEY_2 ? 1 : 2));
    if (key == UI_KEY_2) {
        if (s_view == BULK) s_view = SHELF;
        else if (s_view == SHELF) { ui_nav_request(ctx, 3); return APP_REDRAW_NONE; }
        else if (s_view == READING) {
            s_reader_slider = -1;
            s_reader_panel = s_reader_panel == READER_PANEL_TOOLS ? READER_PANEL_NONE : READER_PANEL_TOOLS;
            invalidate_prep();
        } else if (s_view == TOC) {
            set_reader_view(s_text ? READING : SHELF);
            s_reader_panel = READER_PANEL_NONE;
        } else {
            ui_nav_request(ctx, 0);
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_PAGE;
    }
    if (key != UI_KEY_1 && key != UI_KEY_3) return APP_REDRAW_NONE;
    int dir = key == UI_KEY_1 ? -1 : 1;
    if (s_view == READING) { s_stats_activity_ms = ctx->now_ms; return turn_page(ctx, dir); }
    int next = ctx->leaf + dir;
    if (next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t on_power_short(app_ctx_t* ctx) {
    s_reader_tap.pending = false;
    if (s_view != READING || !s_text || s_clear_confirm ||
        !app_settings_reader_power_turn()) return APP_REDRAW_NONE;
    s_stats_activity_ms = ctx->now_ms;
    app_redraw_t result = turn_page(ctx, 1);
    return result == APP_REDRAW_NONE ? APP_REDRAW_DONE : result;
}
static app_redraw_t on_key_long(app_ctx_t* ctx, int key) {
    s_reader_tap.pending = false;
    if (key != UI_KEY_2) return APP_REDRAW_NONE;
    if (s_view == TOC && s_toc_jump_open) {
        s_toc_jump_open = s_toc_jump_drag = false;
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING && s_view != TOC) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == READING) {
        return reader_key_action(ctx, app_settings_reader_hold_action());
    }
    s_message[0] = 0;
    set_reader_view(s_text ? READING : SHELF);
    s_reader_panel = READER_PANEL_NONE;
    return APP_REDRAW_PAGE;
}
static bool menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    // 图书所有视图只使用产品导航，旧菜单不能抢占右侧分页按钮。
    // All book views use product navigation; the legacy menu must never steal the right pager.
    return false;
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_view != SHELF || s_scan_pending || s_clear_confirm || next < 0 || next >= leaves())
        return APP_REDRAW_NONE;
    ctx->leaf = next;
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_shelf_page_pending = true;
    s_pressed_control = -1;
    prepare_covers(ctx);
    render(ctx, ctx->fb);
    s_area = (EpdRect){0, 176, UI_LOCK_WIDTH, 912};
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

// 键事件与原始边沿可能属于同一份报告，每轮最多执行一次；其他子页只清理旧输入。
// Keys and raw edges may come from the same report: apply one turn per tick, discard on other subpages.
static int book_remote_direction(void) {
    int direction = 0;
    ble_pt_event_t event;
    while (ble_pt_pop_key(&event)) {
        ble_pt_action_t action = ble_pt_action_for_usage(event.usage, event.mods);
        if (action == BLE_PT_ACTION_PREV) direction = -1;
        else if (action == BLE_PT_ACTION_NEXT) direction = 1;
    }
    ble_pt_raw_t raw;
    while (ble_pt_pop_raw(&raw)) {
        if (!raw.pressed) continue;
        ble_pt_action_t action = ble_pt_action_for_frame(&raw.frame);
        if (action == BLE_PT_ACTION_PREV) direction = -1;
        else if (action == BLE_PT_ACTION_NEXT) direction = 1;
    }
    return direction;
}
static app_redraw_t on_tick(app_ctx_t* ctx) {
    // gen18（18-a 自动切回）：读手机推来的虚拟书时，sink 在后台保活；手机回连
    // （WINDOW→CONNECTED）或宽限内直接再发（COOLDOWN→RECEIVING）时，自动切回
    // 传书页接客——它的泵现成，进度/状态/开书全走既有路径。用户零手动操作。
    // gen18 (18-a auto-switch-back): while reading the phone-pushed virtual
    // book the sink is background-alive; when the phone returns (WINDOW->
    // CONNECTED) or pushes again inside the grace (COOLDOWN->RECEIVING), bounce
    // back to the transfer page to receive - its pump is ready and progress,
    // status and book-opening all follow existing paths. Zero manual steps.
    // COOLDOWN 不触发：刚开书那两秒手机还在，别把用户弹回去。
    // COOLDOWN does not trigger: the phone is still around for a couple of
    // seconds right after the book opens - do not bounce the user back then.
    if (s_view == READING && s_path[0] && strcmp(s_path, PUSH_TEXT_PATH) == 0 && push_sink_armed()) {
        const push_state_t st = push_sink_state();
        if (st == PUSH_STATE_CONNECTED || st == PUSH_STATE_RECEIVING || st == PUSH_STATE_PROCESSING) {
            s_push_handoff = true;
            extern const app_desc_t app_transfer;
            ctx->request_app = &app_transfer;
            return APP_REDRAW_NONE;
        }
    }
    // 字体开关留下的重排在这里做：触摸回调的栈太深，一章排下来会把主任务栈顶穿（实测溢出过）。
    // The font switch's re-typeset happens here: the touch callback runs too deep, and laying out a
    // chapter from there overran the main task stack (measured).
    if (s_fonts_reload_pending) {
        s_fonts_reload_pending = false;
        if (s_text && book_kind() == BOOK_KIND_EPUB)
            (void)load_chapter(ctx, s_chapter, book_layout_page_start_offset(s_page), false);
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING || (s_toolbar && s_reader_panel != READER_PANEL_TOOLS) || s_clear_confirm || ctx->consumed) s_reader_tap.pending = false;
    else {
        int tap = book_reader_tap_tick(&s_reader_tap, ctx->now_ms, ctx->touch && ctx->touch->touched);
        if (tap) return turn_page(ctx, tap);
    }
    track_ticket_stats(ctx);
    if (s_view == SEARCH || s_view == EDIT) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return editor_paint(ctx, false);
        return ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED
            ? editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (ctx->consumed) return APP_REDRAW_NONE;
    // 上/左为上一页，下/右为下一页；书架复用滑动的局部刷新，不闪动题头和底栏。
    // Up/left turn back, down/right turn forward; the shelf reuses swipe refresh without flashing chrome.
    if (app_settings_ble_turner()) {
        int ble_dir = book_remote_direction();
        if (ble_dir && s_view == SHELF) {
            app_redraw_t redraw = shelf_turn_page(ctx, ble_dir);
            if (redraw != APP_REDRAW_NONE) return redraw;
        } else if (ble_dir && s_view == READING) {
            s_stats_activity_ms = ctx->now_ms;
            return turn_page(ctx, ble_dir);
        }
    }
    if (s_reader_notice[0] && ctx->now_ms >= s_reader_notice_until) {
        s_reader_notice[0] = 0;
        if (s_view == READING) {
            // 区域刷新不会自动重绘帧缓冲；先移除提示，再只更新提示所在区域。
            // Area refreshes do not rerender the framebuffer; erase the toast before updating its region.
            render(ctx, ctx->fb);
            s_area = (EpdRect){208, 150, 268, 68};
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    if (s_save_failed && ctx->now_ms - s_save_retry_ms >= 15000) {
        s_save_retry_ms = ctx->now_ms;
        bool failed = s_save_failed;
        retry_progress();
        if (failed != s_save_failed) { invalidate_prep(); return APP_REDRAW_PAGE; }
    }
    app_redraw_t background = reader_background_tick();
    if (background != APP_REDRAW_NONE) return background;
    if (s_scan_pending && ctx->now_ms - s_poll_ms >= 500) {
        s_poll_ms = ctx->now_ms;
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        scan_shelf(ctx);
        if (ctx->request_app) return APP_REDRAW_NONE;
        (void)open_requested_book(ctx);
        return APP_REDRAW_PAGE;
    }
    if ((s_view == SHELF || s_view == MANAGE) && !(ctx->touch && ctx->touch->touched)) {
        (void)prepare_cover_batch(ctx, false);
        // 整页准备好再合并推屏；占位页或失败输出不能消费已加载封面的待刷标记。
        // Reveal only a complete cover group; placeholders and failed output cannot consume pending artwork.
        unsigned changed=shelf_cover_reveal_mask();
        if (ctx->request_app) return APP_REDRAW_NONE;
        EpdRect area = {0};
        for (int row = 0; row < shelf_rows(); ++row) {
            if (!(changed & (1u << row))) continue;
            EpdRect r = row_rect(row);
            if (!area.width) area = r;
            else {
                int right = area.x+area.width > r.x+r.width ? area.x+area.width : r.x+r.width;
                int bottom = area.y+area.height > r.y+r.height ? area.y+area.height : r.y+r.height;
                if (r.x < area.x) area.x = r.x;
                if (r.y < area.y) area.y = r.y;
                area.width = right-area.x; area.height = bottom-area.y;
            }
        }
        if (area.width) {
            render(ctx, ctx->fb);
            s_area = area;
            s_mode = s_view == SHELF && app_settings_main_fast_refresh() ? MODE_DU : MODE_GL16;
            s_shelf_feedback_pending = s_view == SHELF;
            s_cover_batch_present_pending = true;
            s_cover_present_mask=changed;
            return APP_REDRAW_AREA;
        }
    }
    warm_shelf_lock_cover(ctx);
    if (ctx->request_app) return APP_REDRAW_NONE;
    // 目录暂借系统字体，不应把正文按系统字体重新分页。/ The directory borrows the system face without repaginating the reader.
    if (s_view == READING && s_text && strcmp(s_font_path, ttf_font_path())) {
        size_t off = book_layout_page_start_offset(s_page);
        save_progress();
        lock_draw();
        invalidate_prep();
        bool ok = reader_relayout(body_rect());
        if (ok) s_page = reader_visual_start(reader_visual_for_offset(off));
        copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
        unlock_draw();
        if (!ok) { free_book(); s_view = SHELF; copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书"); }
        return APP_REDRAW_PAGE;
    }
    app_redraw_t clock=reader_clock_tick(ctx);
    if(clock==APP_REDRAW_NONE)reader_prefetch_idle(ctx);
    return clock;
}
static void before_lock(app_ctx_t* ctx) {
    shelf_category_remember(ctx);
    s_category_menu=false;
    s_reader_tap.pending = false;
    track_ticket_stats(ctx);
    save_progress();
    // 锁屏前丢弃分行预绘，后续只按当前字体重建。/ Discard idle prefetch before lock; rebuild only with the current face.
    lock_draw();
    invalidate_prep();
    unlock_draw();
    // 锁屏票根始终以当前正在阅读的书为准；不要依赖书架最近项是否已经同步。
    if (s_text && s_path[0]) book_progress_set_last_path(s_path);
    flush_ticket_stats();
    s_stats_last_ms = 0;
}
static EpdRect area_hint(app_ctx_t* ctx) { (void)ctx; return s_area; }
static bool main_page_visible(app_ctx_t* ctx) {
    (void)ctx;
    return s_view == SHELF && !s_clear_confirm && !s_batch_confirm;
}

const app_desc_t app_book = {
    .title = "图书 Books", .detail = "TF 卡 txt / epub 阅读", .enter_full = false, .owns_keys = true,
    .defer_middle_short = true,
    .menu_handle_enabled = menu_handle_enabled,
    .main_page_visible = main_page_visible,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = book_on_exit,
    .on_media_lost = book_on_media_lost, .on_media_ready = book_on_media_ready,
    .on_before_lock = before_lock,
    .on_gesture = gesture_event, .on_key = on_key, .on_key_long = on_key_long,
    .on_power_short = on_power_short,
    .on_tick = on_tick, .area_hint = area_hint,
};
