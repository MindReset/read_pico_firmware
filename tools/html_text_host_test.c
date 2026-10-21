/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：验证章节 HTML 解析、边界和分配失败。
 * English: Verify chapter HTML parsing, bounds and allocation failures.
 * 冻结：仅供宿主测试。/ Frozen: Host tests only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html_text.h"
int html_test_fail_after = -1;
// 宿主替身字体表：A 映射到槽 1、B 映射到槽 2，其余名字当作书里没有。
// Host stand-in face table: A maps to slot 1, B to slot 2, every other name is absent.
static uint8_t resolve_faces(void* ctx, const char* family, size_t len) {
    (void)ctx;
    if (len == 1 && family[0] == 'A') return 1;
    if (len == 1 && family[0] == 'B') return 2;
    return 0;
}
static html_text_t parse_faces(const char* html, const char* css, const char* expected) {
    html_text_t out = {0};
    html_font_map_t fonts = { .resolve = resolve_faces, .ctx = NULL };
    assert(html_to_blocks_with_css_target(html, strlen(html), css, strlen(css), &fonts,
                                           NULL, SIZE_MAX, NULL, &out) == ESP_OK);
    assert(out.utf8 != NULL && out.len == strlen(expected));
    assert(strcmp(out.utf8, expected) == 0);
    return out;
}
// 每个块的 run 必须从块首起、首尾相接、正好铺满整块；否则排版会漏字或把上一段的
// 字体串到下一段。零 run 表示整块走系统字体，不参与覆盖检查。
// A block's runs must start at its first byte, abut, and exactly cover it, or layout would
// skip text or bleed one span's face into the next. Zero runs means the block is entirely on
// the system face and has nothing to cover.
static void assert_runs_cover(const html_text_t* t) {
    for (size_t i = 0; i < t->count; ++i) {
        const blk_t* b = &t->blocks[i];
        if (!b->run_count) continue;
        size_t at = 0;
        for (size_t r = 0; r < b->run_count; ++r) {
            const html_run_t* run = &t->runs[b->run_first + r];
            assert(run->offset == at && run->len > 0);
            at += run->len;
        }
        assert(at == b->len);
    }
}
static html_text_t parse(const char* html, const char* expected) {
    html_text_t out = {0};
    assert(html_to_blocks(html, strlen(html), &out) == ESP_OK);
    assert(out.utf8 != NULL && out.len == strlen(expected));
    assert(strcmp(out.utf8, expected) == 0);
    for (size_t i = 0; i < out.count; ++i) {
        assert(out.blocks[i].len > 0);
        assert(out.blocks[i].offset + out.blocks[i].len <= out.len);
        if (i) assert(out.blocks[i].offset == out.blocks[i - 1].offset + out.blocks[i - 1].len + 1);
    }
    return out;
}
int main(void) {
    html_text_t t = parse("<HEAD><title>隐</title><style>x</style></HEAD><div><div></div><H1>标题 <i>甲</i></H1><p>A  B\n C</p><div></div><p>乙<br/>丙</p></div>", "标题 甲\nA B C\n乙\n丙");
    assert(t.count == 4 && t.blocks[0].heading && !t.blocks[1].heading);
    html_text_free(&t);
    t = parse("<body><nav><p><a href='#one'>第一章 起点</a></p></nav>"
              "<div role='doc-toc'><h2>第二章 继续</h2></div>"
              "<section epub:type='copyright'><h1>作者资料</h1></section>"
              "<h1 id='one'><a id='anchor'/><span>第一章 起点</span></h1><p>真正正文</p></body>",
              "第一章 起点\n第二章 继续\n作者资料\n第一章 起点\n真正正文");
    assert(t.count == 5 && t.blocks[0].linked && t.blocks[0].auxiliary);
    assert(t.blocks[1].heading_level == 2 && t.blocks[1].auxiliary);
    assert(t.blocks[2].auxiliary && t.blocks[2].heading_level == 1);
    assert(!t.blocks[3].linked && !t.blocks[3].auxiliary && t.blocks[3].heading_level == 1);
    assert(!t.blocks[4].heading_level && !t.blocks[4].auxiliary);
    html_text_free(&t);
    t = parse("<h1>第一章 <a href='#note'>注</a> 起点</h1><p>正文<a href='#note'>注</a>继续</p>",
              "第一章 注 起点\n正文注继续");
    assert(!t.blocks[0].linked && !t.blocks[1].linked);
    html_text_free(&t);
    t = parse("a<script>if(a < b){x='<p>fake</p>'}</script><style>p{content:'<x>'}</style><!-- hidden > -->b", "ab");
    html_text_free(&t);
    t = parse("<head><script>var x='</head>';</script><title>hidden</title></head><p>visible</p>", "visible");
    html_text_free(&t);
    t = parse("a<script><!-- raw JS without a comment close </script>b", "ab");
    html_text_free(&t);
    t = parse("<script>const s = '<!--';</script><p>ok</p>", "ok");
    html_text_free(&t);
    t = parse("<head><style>p:before{content:'<!-- </head>'}</style><title>hidden</title></head><p>ok</p>", "ok");
    html_text_free(&t);
    t = parse("a<3 and 2 > 1", "a<3 and 2 > 1");
    html_text_free(&t);
    t = parse("<p title='x > y'> &amp; &lt; &gt; &quot; &apos; &nbsp; &#20013; &#x1F600; </p>", "& < > \" ' 中 😀");
    html_text_free(&t);
    t = parse("<h2>two</h2><h3>three</h3><h4>four</h4><li>x</li><tr>y</tr><hr><blockquote>z</blockquote>", "two\nthree\nfour\nx\ny\nz");
    assert(t.count == 6 && t.blocks[0].heading && t.blocks[1].heading && !t.blocks[2].heading);
    html_text_free(&t);
    t = parse("<style>body{margin-bottom: .5em} p{ text-indent:2em; margin-top:8px } .center{text-align:center}</style>"
              "<p class='center'>甲</p><p style='text-align:right; margin-bottom:25%'>乙</p>", "甲\n乙");
    assert(t.count == 2);
    assert(t.blocks[0].align == 1 && t.blocks[0].indent_percent == 200);
    assert(t.blocks[0].margin_before_percent == 50 && t.blocks[0].margin_after_percent == 50);
    assert(t.blocks[1].align == 2 && t.blocks[1].indent_percent == 200);
    assert(t.blocks[1].margin_after_percent == 25);
    html_text_free(&t);
    t = parse("<p>前</p><img src='data:image/png;base64,x' data-src='../图&amp;文.png'/>"
              "<svg:image xlink:href='../svg.png'/><object data='../obj.png'/>",
              "前\n￼\n￼\n￼");
    assert(t.image_count == 3 && !strcmp(t.images[0], "../图&文.png") &&
           !strcmp(t.images[1], "../svg.png") && !strcmp(t.images[2], "../obj.png"));
    html_text_free(&t);
    // 后代、子元素、优先级和不支持的选择器不能扩散到普通正文。
    // Descendants, children, specificity and unsupported selectors never broaden to ordinary prose.
    t = parse("<style>.js p{text-align:right}.js > p{text-align:center} p{text-align:left}"
              " .x:before p{text-align:right}.s + p{text-align:right}</style>"
              "<p>normal</p><div class='js'><p>direct</p><section><p>deep</p></section></div><p>tail</p>",
              "normal\ndirect\ndeep\ntail");
    assert(t.count==4&&t.blocks[0].align==0&&t.blocks[1].align==1&&t.blocks[2].align==2&&t.blocks[3].align==0);
    html_text_free(&t);
    t = parse("<style>#one p.x{text-align:right}.x{text-align:center}</style>"
              "<div id='one'><p class='x' style='text-align:left'>inline</p><p class='x'>scoped</p></div><p class='x'>outside</p>",
              "inline\nscoped\noutside");
    assert(t.blocks[0].align==0&&t.blocks[1].align==2&&t.blocks[2].align==1);html_text_free(&t);
    const char *scoped_oom="<style>.js p{text-align:right}</style><p>body</p>";
    html_test_fail_after=0;assert(html_to_blocks(scoped_oom,strlen(scoped_oom),&t)==ESP_ERR_NO_MEM);
    assert(!t.utf8&&!t.count&&!t.blocks);html_test_fail_after=-1;
    const char* external = ".center{text-align:center}";
    assert(html_to_blocks_with_css("<p class='center'>甲</p>", strlen("<p class='center'>甲</p>"),
                                    external, strlen(external), &t) == ESP_OK);
    assert(t.count == 1 && t.blocks[0].align == 1);
    html_text_free(&t);
    // 同一题头用 br 分开章号和章名时，两行保持居中与各自字体，正文仍左对齐。
    // A br splits the chapter label and name without losing centering or faces; prose stays left aligned.
    t = parse_faces("<div><img src='logo.png'/></div><h2 class='chapter-title'>"
                    "<span class='num'>第一章</span><br/> 山边小村</h2><p>正文</p>",
                    "h2.chapter-title{text-align:center;font-family:B;margin-top:0.5em;margin-bottom:1.8em}"
                    "h2.chapter-title .num{font-family:A}",
                    "￼\n第一章\n山边小村\n正文");
    assert(t.count == 4 && t.image_count == 1);
    assert(t.blocks[1].heading && t.blocks[2].heading);
    assert(t.blocks[1].heading_level == 2 && t.blocks[2].heading_level == 2);
    assert(t.blocks[1].align == 1 && t.blocks[2].align == 1);
    assert(t.blocks[1].line_break_after && !t.blocks[2].line_break_after);
    assert(t.blocks[1].margin_before_percent == 50 && t.blocks[1].margin_after_percent == 0);
    assert(t.blocks[2].margin_before_percent == 0 && t.blocks[2].margin_after_percent == 180);
    assert(!t.blocks[0].line_break_after && !t.blocks[3].line_break_after);
    assert(t.runs[t.blocks[1].run_first].slot == 1);
    assert(t.runs[t.blocks[2].run_first].slot == 2);
    assert(!t.blocks[3].heading && t.blocks[3].align == 0);
    assert_runs_cover(&t);
    html_text_free(&t);
    t = parse("<p style='text-align:right;margin-top:1em;margin-bottom:1em'>"
              "甲<br>乙<BR />丙<br/>丁</p><p>正文</p>", "甲\n乙\n丙\n丁\n正文");
    assert(t.count == 5 && t.blocks[4].align == 0);
    for (size_t i = 0; i < 4; ++i) assert(t.blocks[i].align == 2);
    // 换行不加段后距；末行保留一次整段的段后距。/ Defer the paragraph margin until its final line.
    for (size_t i = 0; i < 3; ++i)
        assert(t.blocks[i].line_break_after && t.blocks[i].margin_after_percent == 0);
    for (size_t i = 1; i < 4; ++i) assert(t.blocks[i].margin_before_percent == 0);
    assert(!t.blocks[3].line_break_after && t.blocks[3].margin_after_percent == 100);
    assert(!t.blocks[4].line_break_after);
    html_text_free(&t);
    t = parse("<p style='margin-bottom:1em'>甲<br/><br/></p><p>乙</p>", "甲\n乙");
    assert(t.count == 2 && !t.blocks[0].line_break_after && t.blocks[0].margin_after_percent == 100);
    html_text_free(&t);
    t = parse("<p style='margin-bottom:1em'>甲<br/><img src='a.png'/>乙</p>", "甲\n￼\n乙");
    assert(t.count == 3 && !t.blocks[0].line_break_after && !t.blocks[1].line_break_after);
    assert(t.blocks[0].margin_after_percent == 100 && t.blocks[1].image == 0);
    html_text_free(&t);
    t = parse("  <div><div> </div></div><br><hr> ", "");
    assert(t.count == 0);
    html_text_free(&t);
    t = parse("a &unknown; &amp b < 3", "a &unknown; &amp b < 3");
    html_text_free(&t);
    t = parse("&#0; &#xD800; &#x110000;", "� � �");
    html_text_free(&t);
    assert(html_to_blocks(NULL, 0, &t) == ESP_OK && t.count == 0);
    html_text_free(&t);
    assert(html_to_blocks(NULL, 1, &t) == ESP_ERR_INVALID_ARG);
    assert(html_to_blocks("x", HTML_TEXT_MAX_BYTES + 1, &t) == ESP_ERR_INVALID_SIZE);
    assert(html_to_blocks("\xe7\x94", 2, &t) != ESP_OK && !t.utf8 && !t.blocks);
    assert(html_to_blocks("<p title='unterminated", 22, &t) == ESP_ERR_INVALID_RESPONSE);
    char* large = malloc(HTML_TEXT_MAX_BYTES);
    memset(large, 'x', HTML_TEXT_MAX_BYTES);
    assert(html_to_blocks(large, HTML_TEXT_MAX_BYTES, &t) == ESP_OK);
    assert(t.len == HTML_TEXT_MAX_BYTES && t.count == 1 && t.utf8[t.len] == 0);
    html_text_free(&t);
    free(large);
    size_t n = HTML_TEXT_MAX_BLOCKS + 1;
    char* many = malloc(n * 8 + 1);
    for (size_t i = 0; i < n; ++i) memcpy(many + i * 8, "<p>x</p>", 8);
    assert(html_to_blocks(many, (n - 1) * 8, &t) == ESP_OK && t.count == HTML_TEXT_MAX_BLOCKS);
    html_text_free(&t);
    assert(html_to_blocks(many, n * 8, &t) == ESP_ERR_INVALID_SIZE);
    assert(!t.utf8 && !t.blocks && t.count == 0);
    free(many);
    for (int i = 0; i < 2; ++i) {
        html_test_fail_after = i;
        assert(html_to_blocks("<p>test</p>", 11, &t) == ESP_ERR_NO_MEM);
        assert(!t.utf8 && !t.blocks);
    }
    html_test_fail_after = -1;
    html_text_free(&t);

    // ---- 书内字体的 run 表 / Embedded-face runs ----
    // 没有字体映射时完全不记 run：没有内嵌字体的书不该多花这份内存。
    // No face map means no runs at all: a book without embedded faces pays nothing.
    t = parse("<p>甲<span class='num'>12</span>乙</p>", "甲12乙");
    assert(t.run_count == 0 && t.blocks[0].run_count == 0 && t.runs == NULL);
    html_text_free(&t);

    // 类选择器换字体：三段槽位 0/1/0，字节区间正好铺满整块。
    // A class switches the face: three spans on slots 0/1/0 covering the block exactly.
    t = parse_faces("<p>甲<span class='num'>12</span>乙</p>", ".num{font-family:\"A\"}", "甲12乙");
    assert(t.count == 1 && t.blocks[0].run_count == 3);
    assert(t.runs[0].slot == 0 && t.runs[0].offset == 0 && t.runs[0].len == 3);
    assert(t.runs[1].slot == 1 && t.runs[1].offset == 3 && t.runs[1].len == 2);
    assert(t.runs[2].slot == 0 && t.runs[2].offset == 5 && t.runs[2].len == 3);
    assert_runs_cover(&t);
    html_text_free(&t);

    // 行内 style 属性同样生效，family 列表取第一个认得出来的名字。
    // An inline style attribute counts too, and a family list takes the first known name.
    t = parse_faces("<p>甲<span style='font-family:Missing, \"B\", serif'>1</span>乙</p>", "", "甲1乙");
    assert(t.blocks[0].run_count == 3 && t.runs[1].slot == 2);
    assert_runs_cover(&t);
    html_text_free(&t);

    // 嵌套继承：内层没声明就沿用外层，闭标签回退到外层。
    // Nesting inherits: an inner level without a declaration keeps its parent's, and a close
    // tag restores it.
    t = parse_faces("<p><span class='a'>甲<span class='b'>乙</span>丙</span>丁</p>",
                    ".a{font-family:A}.b{font-family:B}", "甲乙丙丁");
    assert(t.blocks[0].run_count == 4);
    assert(t.runs[0].slot == 1 && t.runs[1].slot == 2 &&
           t.runs[2].slot == 1 && t.runs[3].slot == 0);
    assert_runs_cover(&t);
    html_text_free(&t);

    // 每块的 run 从自己的块首重新开始，上一段的字体不跨段。
    // Each block restarts its runs at its own first byte; a face never spans blocks.
    t = parse_faces("<p><span class='a'>甲</span></p><p>乙<span class='a'>丙</span></p>",
                    ".a{font-family:A}", "甲\n乙丙");
    assert(t.count == 2);
    assert(t.blocks[0].run_count == 1 && t.runs[0].slot == 1 && t.runs[0].len == 3);
    assert(t.blocks[1].run_count == 2 && t.runs[1].slot == 0 && t.runs[2].slot == 1);
    assert_runs_cover(&t);
    html_text_free(&t);

    // @font-face 里的 font-family 在给字体本身命名，不是给正文选字体；认不出的名字同理。
    // 两者都不该白建 run 表，否则每个块都要多记一段。
    // A font-family inside @font-face names the face rather than selecting one for body text,
    // and an unknown name selects nothing. Neither should build a run table, or every block
    // would carry a useless span.
    t = parse_faces("<p>甲</p>", "@font-face{font-family:\"A\";src:url(a.ttf)}", "甲");
    assert(t.run_count == 0 && t.runs == NULL);
    html_text_free(&t);
    t = parse_faces("<p>甲</p>", ".z{font-family:Missing}", "甲");
    assert(t.run_count == 0 && t.runs == NULL);
    html_text_free(&t);

    puts("html_text_host_test: PASS");
}
