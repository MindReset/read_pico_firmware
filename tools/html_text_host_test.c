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
    t = parse("  <div><div> </div></div><br><hr> ", "");
    assert(t.count == 0);
    html_text_free(&t);
    t = parse("<img src='cover.jpg'/>", "[图片]");
    assert(t.blocks[0].image_src && !strcmp(t.blocks[0].image_src,"cover.jpg"));
    assert(t.count == 1 && !t.blocks[0].heading);
    html_text_free(&t);
    t = parse("<h1>Before<IMG src='x>y'>After</h1><p>end</p>", "Before\n[图片]\nAfter\nend");
    assert(t.count == 4 && t.blocks[0].heading && !t.blocks[1].heading && t.blocks[2].heading);
    html_text_free(&t);
    t = parse("<svg><image href='cover.jpg'/></svg><xhtml:img/><svg:image/></img>", "[图片]\n[图片]\n[图片]");
    html_text_free(&t);
    t = parse("<head><img/></head><script>'<img/>'</script><style><img/></style><!-- <img/> --><p>ok</p>", "ok");
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
        html_test_fail_after = i;
        assert(html_to_blocks("<img/>", 6, &t) == ESP_ERR_NO_MEM);
        assert(!t.utf8 && !t.blocks);
    }
    html_test_fail_after = -1;
    html_text_free(&t);
    puts("html_text_host_test: PASS");
}
