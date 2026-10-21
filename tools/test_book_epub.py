#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：生成有界 EPUB 元数据测试。/ English: Generate bounded EPUB metadata tests.
冻结：仅测试，不写产品构建。/ Frozen: Tests only, no product build writes.
"""
from pathlib import Path
import struct
import os
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def make_large_book(path, chapters=5000):
    """Original small chapters exercise a larger spine without shipping copyrighted books."""
    items = ''.join(f'<item id="c{i}" href="text/ch{i}.xhtml" media-type="application/xhtml+xml"/>'
                    for i in range(1, chapters + 1))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
    opf = ('<package><manifest>' + items +
           '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>' +
           '</manifest><spine toc="toc">' + spine + '</spine></package>')
    points = ''.join(
        f'<navPoint><navLabel><text>第{i}章 {"终点" if i == chapters else "记录"}</text></navLabel>'
        f'<content src="text/ch{i}.xhtml"/></navPoint>' for i in range(1, chapters + 1))
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip')
        archive.writestr('META-INF/container.xml',
                         '<container><rootfiles><rootfile full-path="OPS/book.opf" '
                         'media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('OPS/book.opf', opf)
        archive.writestr('OPS/toc.ncx', '<ncx><navMap>' + points + '</navMap></ncx>')
        for i in range(1, chapters + 1):
            title = '终点' if i == chapters else '记录'
            archive.writestr(f'OPS/text/ch{i}.xhtml',
                             f'<html><body><h1>第{i}章 {title}</h1><p>正文 {i}</p></body></html>')


def make_book(path, change=None):
    container = '<container><rootfiles><rootfile full-path="OPS/pkg/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
    items = ''.join(f'<item id="c{i}" href="../text/part%20{i}%26x.xhtml#start" media-type="application/xhtml+xml"/>' for i in range(1, 5))
    items += '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/><item id="nav" href="../toc/nav.xhtml" media-type="application/xhtml+xml" properties="cover nav"/>'
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, 5))
    opf = f'<package><manifest>{items}</manifest><spine toc="toc">{spine}</spine></package>'
    points = '<navPoint><navLabel><text>Parent &amp; One</text></navLabel><content src="../text/part%201&amp;x.xhtml#title"/><navPoint><navLabel><text>Child &#84;wo</text></navLabel><content src="../text/part%202%26x.xhtml#child"/></navPoint></navPoint>'
    points += ''.join(f'<navPoint><navLabel><text>Chapter {i}</text></navLabel><content src="../text/part%20{i}%26x.xhtml"/></navPoint>' for i in (3, 4))
    ncx = f'<?xml version="1.0"?><!DOCTYPE ncx SYSTEM "http://invalid.example/never-read.dtd"><ncx><navMap>{points}</navMap></ncx>'
    links = ''.join(f'<li><a href="../text/part%20{i}%26x.xhtml"><span>NAV</span> {i}</a></li>' for i in range(1, 5))
    nav = f'<html><body><nav epub:type="landmarks"><a href="../text/part%201%26x.xhtml">WRONG</a></nav><nav epub:type="toc"><ol>{links}</ol></nav></body></html>'
    files = {'META-INF/container.xml': container, 'OPS/pkg/book.opf': opf, 'OPS/toc/book.ncx': ncx, 'OPS/toc/nav.xhtml': nav}
    for i in range(1, 5):
        files[f'OPS/text/part {i}&x.xhtml'] = f'<html><body><h1>Chapter {i}</h1><div>正文 {i} &amp; safe</div></body></html>'
    if change:
        change(files)
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip')
        for name, content in files.items():
            archive.writestr(name, content)


def main():
    with tempfile.TemporaryDirectory(prefix='book-epub-') as temp:
        work = Path(temp)
        exe = work / 'test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'tools/book_epub_stubs'), '-I' + str(ROOT / 'tools/zip_host_stubs'),
                        '-I' + str(ROOT / 'main/book'), str(ROOT / 'tools/book_epub_host_test.c'),
                        str(ROOT / 'main/book/book_epub.c'), str(ROOT / 'main/book/zip_reader.c'),
                        str(ROOT / 'main/book/html_text.c'), str(ROOT / 'main/book/book_index_cache.c'),
                        str(ROOT / 'main/book/book_image_header.c'),
                        '-lz', '-o', str(exe)], check=True)
        cases = []

        def case(name, edit=None):
            file = work / (name + '.epub'); make_book(file, edit); cases.append(file)

        def replace_opf(old, new):
            return lambda files: files.__setitem__('OPS/pkg/book.opf', files['OPS/pkg/book.opf'].replace(old, new))

        case('good_paths')
        def bounded_cover(files):
            files['OPS/pkg/book.opf'] = files['OPS/pkg/book.opf'].replace(
                '</manifest>', '<item id="image-cover" href="../covers/test.png" '
                'media-type="image/png" properties="cover-image"/></manifest>')
            files['OPS/covers/test.png'] = bytes(range(128))
        case('good_bounded_cover', bounded_cover)
        case('good_no_cover', lambda f: f.__setitem__(
            'OPS/pkg/book.opf', f['OPS/pkg/book.opf'].replace('properties="cover nav"', 'properties="nav"')))
        case('good_navfallback', lambda f: f.__setitem__('OPS/toc/book.ncx', '<ncx><broken></ncx>'))
        case('good_navfallback_partial', lambda f: f.__setitem__('OPS/toc/book.ncx', f['OPS/toc/book.ncx'] + '<broken>'))
        def partial_ncx(files, nav_entries=(1, 2, 3, 4)):
            files['OPS/toc/book.ncx'] = ('<ncx><navMap>' + ''.join(
                f'<navPoint><navLabel><text>NCX {i}</text></navLabel>'
                f'<content src="../text/part%20{i}%26x.xhtml"/></navPoint>'
                for i in (1, 3)) + '</navMap></ncx>')
            links = ''.join(
                f'<li><a href="../text/part%20{i}%26x.xhtml">NAV {i}</a></li>'
                for i in nav_entries)
            files['OPS/toc/nav.xhtml'] = (
                '<html><body><nav epub:type="toc"><ol>' + links +
                '</ol></nav></body></html>')
        case('good_ncx_partial_nav_full', partial_ncx)
        case('good_ncx_nav_complement', lambda f: partial_ncx(f, (2, 4)))
        case('good_ncx_nav_corrupt', lambda f: (
            partial_ncx(f), f.__setitem__('OPS/toc/nav.xhtml', '<html><nav>broken')))
        case('good_navrole', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/toc/nav.xhtml', f['OPS/toc/nav.xhtml'].replace('epub:type="toc"', 'role="doc-toc"'))))
        case('good_navtype', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/toc/nav.xhtml', f['OPS/toc/nav.xhtml'].replace('epub:type="toc"', 'type="toc"'))))
        case('good_navunmarked', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/toc/nav.xhtml', f['OPS/toc/nav.xhtml'].replace('<nav epub:type="landmarks"><a href="../text/part%201%26x.xhtml">WRONG</a></nav>', '').replace('epub:type="toc"', ''))))
        case('good_defaults', lambda f: (f.pop('OPS/toc/book.ncx'), f.pop('OPS/toc/nav.xhtml')))
        case('good_defaults_emptytoc', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/toc/nav.xhtml', '<html><nav epub:type="toc"/><a href="../text/part%201%26x.xhtml">WRONG</a></html>')))
        case('good_samefile', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/pkg/book.opf', f['OPS/pkg/book.opf'].replace('../text/part%201%26x.xhtml#start', '../toc/nav.xhtml')), f.__setitem__('OPS/toc/nav.xhtml', f['OPS/toc/nav.xhtml'].replace('../text/part%201%26x.xhtml', '#start'))))
        def frontmatter(files, fallback=False):
            files['OPS/pkg/book.opf'] = files['OPS/pkg/book.opf'].replace(
                '<item id="c1"', '<item id="front" href="../text/front.xhtml" media-type="application/xhtml+xml"/><item id="c1"').replace(
                '<itemref idref="c1"/>', '<itemref idref="front"/><itemref idref="c1"/>')
            files['OPS/text/front.xhtml'] = '<html><body><h1>作者信息</h1><p>出版社与版权资料</p></body></html>'
            if fallback:
                files.pop('OPS/toc/book.ncx')
                files.pop('OPS/toc/nav.xhtml')
            else:
                files['OPS/toc/book.ncx'] = files['OPS/toc/book.ncx'].replace(
                    '<navMap>', '<navMap><navPoint><navLabel><text>作者信息</text></navLabel>'
                    '<content src="../text/front.xhtml"/></navPoint>')
                files['OPS/toc/nav.xhtml'] = files['OPS/toc/nav.xhtml'].replace(
                    '<nav epub:type="toc"><ol>',
                    '<nav epub:type="toc"><ol><li><a href="../text/front.xhtml">作者信息</a></li>')
        case('good_frontmatter_nav', frontmatter)
        case('good_frontmatter_fallback', lambda f: frontmatter(f, True))
        def multianchor(files):
            files['OPS/pkg/book.opf'] = ('<package><manifest>'
                '<item id="body" href="../text/combined.xhtml" media-type="application/xhtml+xml"/>'
                '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/>'
                '</manifest><spine toc="toc"><itemref idref="body"/></spine></package>')
            files['OPS/text/combined.xhtml'] = ('<html><body><h1 id="info">作者信息</h1><p>出版社</p>'
                '<h1 id="one">第一章 起点</h1><p>第一章正文</p>'
                '<h1 id="two">第二章 继续</h1><p>第二章正文</p></body></html>')
            files['OPS/toc/book.ncx'] = ('<ncx><navMap>'
                '<navPoint><navLabel><text>作者信息</text></navLabel><content src="../text/combined.xhtml#info"/></navPoint>'
                '<navPoint><navLabel><text>第一章 起点</text></navLabel><content src="../text/combined.xhtml#one"/></navPoint>'
                '<navPoint><navLabel><text>第二章 继续</text></navLabel><content src="../text/combined.xhtml#two"/></navPoint>'
                '</navMap></ncx>')
        case('good_multianchor', multianchor)
        def chapter_breaks(files, named=False, fallback=False, auxiliary=False, long_body=False):
            files['OPS/pkg/book.opf'] = ('<package><manifest>'
                '<item id="body" href="../text/combined.xhtml" media-type="application/xhtml+xml"/>'
                '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/>'
                '</manifest><spine toc="toc"><itemref idref="body"/></spine></package>')
            first, second = ('出发', '归来') if named else ('第一章 起点', '第2章 第二站')
            files['OPS/text/combined.xhtml'] = (
                '<html><body' + (' epub:type="copyright"' if auxiliary else '') + '>'
                '<h1>作者信息</h1><p>出版社与版权资料</p>'
                '<h1>内容简介</h1><p>一本用于分页测试的虚构作品。</p>'
                '<h1>目录</h1><p>第一章 假目录条目</p><p>第2章 假目录条目</p><p>目录说明</p>'
                '<section role="doc-toc"><h2>第3章 容器中的目录</h2></section>'
                '<p><a href="#one">第4章 目录链接</a></p>'
                f'<h1 id="one">{first}</h1><p>第一章正文内容。</p>'
                '<h2>普通小节</h2><p>同一章节里的另一段正文。</p>'
                '<p>第1章说到的人物并不是本章的标题</p>'
                f'<h1 id="two">{second}</h1><p>第二章正文内容。</p></body></html>')
            files['OPS/toc/book.ncx'] = ('<ncx><navMap>'
                f'<navPoint><navLabel><text>{first}</text></navLabel><content src="../text/combined.xhtml#one"/></navPoint>'
                f'<navPoint><navLabel><text>{second}</text></navLabel><content src="../text/combined.xhtml#two"/></navPoint>'
                '</navMap></ncx>')
            files.pop('OPS/toc/nav.xhtml')
            if long_body:
                files['OPS/text/combined.xhtml'] = files['OPS/text/combined.xhtml'].replace(
                    '第一章正文内容。', '真正的长段落。' * 100 + '<a href="#note">注</a>继续正文。')
            if fallback:
                files.pop('OPS/toc/book.ncx')
        case('good_chapter_breaks_numbered', chapter_breaks)
        case('good_chapter_breaks_named', lambda f: chapter_breaks(f, named=True))
        case('good_chapter_breaks_fallback', lambda f: chapter_breaks(f, fallback=True))
        case('good_chapter_breaks_auxiliary', lambda f: chapter_breaks(f, auxiliary=True))
        case('good_chapter_breaks_long_body', lambda f: chapter_breaks(f, fallback=True, long_body=True))
        def multianchor_nav(files):
            multianchor(files)
            files['OPS/pkg/book.opf'] = files['OPS/pkg/book.opf'].replace(
                '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/>',
                '<item id="nav" href="../toc/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')
            files.pop('OPS/toc/book.ncx')
            files['OPS/toc/nav.xhtml'] = ('<html><body><nav epub:type="toc"><ol>'
                '<li><a href="../text/combined.xhtml#info">作者信息</a></li>'
                '<li><a href="../text/combined.xhtml#one">第一章 起点</a></li>'
                '<li><a href="../text/combined.xhtml#two">第二章 继续</a></li>'
                '</ol></nav></body></html>')
        case('good_multianchor_nav', multianchor_nav)
        def dual_samefile_partial_ncx(files):
            multianchor(files)
            files['OPS/pkg/book.opf'] = files['OPS/pkg/book.opf'].replace(
                '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/>',
                '<item id="toc" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/>'
                '<item id="nav" href="../toc/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')
            files['OPS/toc/book.ncx'] = ('<ncx><navMap><navPoint><navLabel>'
                '<text>第一章 起点</text></navLabel><content src="../text/combined.xhtml#one"/>'
                '</navPoint></navMap></ncx>')
            files['OPS/toc/nav.xhtml'] = ('<html><body><nav epub:type="toc"><ol>'
                '<li><a href="../text/combined.xhtml#one">第一章 起点</a></li>'
                '<li><a href="../text/combined.xhtml#two">第二章 继续</a></li>'
                '</ol></nav></body></html>')
        case('good_dual_samefile_partial_ncx', dual_samefile_partial_ncx)
        def body_priority(files, authored=True):
            frontmatter(files, not authored)
            files['OPS/text/part 1&x.xhtml'] = '<html><body><h1>第一章 起点</h1><p>正文一</p></body></html>'
            files['OPS/text/part 2&x.xhtml'] = '<html><body><p>第2章 第二站</p><p>正文二</p></body></html>'
            files['OPS/text/part 3&x.xhtml'] = '<html><body><h2>第三章 继续</h2><p>正文三</p></body></html>'
            files['OPS/text/part 4&x.xhtml'] = '<html><body><h1>第四章 终点</h1><p>正文四</p></body></html>'
        case('good_body_priority', body_priority)
        case('good_body_without_nav', lambda f: body_priority(f, False))
        case('good_body_ideographic', lambda f: (body_priority(f), f.__setitem__(
            'OPS/text/part 2&x.xhtml', f['OPS/text/part 2&x.xhtml'].replace('第2章 第二站', '第2章　第二站'))))
        def body_samefile(files):
            multianchor(files)
            files['OPS/text/combined.xhtml'] = ('<html><body><h1>作者信息</h1><p>出版社</p>'
                '<h1>第一章 起点</h1><p>正文一</p>'
                '<p>第2章 第二站</p><p>正文二</p></body></html>')
            files['OPS/toc/book.ncx'] = ('<ncx><navMap><navPoint><navLabel><text>全书</text></navLabel>'
                '<content src="../text/combined.xhtml"/></navPoint></navMap></ncx>')
        case('good_body_samefile', body_samefile)
        def body_partial(files):
            files['OPS/text/part 1&x.xhtml'] = '<html><body><h1>第一章 起点</h1><p>正文一</p></body></html>'
        case('good_body_partial', body_partial)
        def body_false_positive(files):
            files['OPS/text/part 1&x.xhtml'] = ('<html><body><p>第1章说到的人物并不是本章的标题</p>'
                '<h1>无编号标题</h1><p>正文</p></body></html>')
        case('good_body_false_positive', body_false_positive)
        def resources(files):
            files['OPS/text/part 1&x.xhtml'] = ('<html><head><link rel="stylesheet" href="../styles/book.css"/></head>'
                '<body><p class="center">正文</p><img data-src="../images/pic&amp;one.png"/></body></html>')
            files['OPS/styles/book.css'] = '.center{text-align:center}'
            files['OPS/images/pic&one.png'] = b'\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR' + struct.pack('>II', 80, 20)
            files['OPS/images/wrapper.svg'] = '<svg><image href="pic&amp;one.png"/></svg>'
            # SOF follows more than 64 KiB of metadata, past the bounded prefix probe.
            files['OPS/images/late.jpg'] = (b'\xff\xd8' +
                (b'\xff\xe1\xff\xff' + b'x' * 65533) * 2 +
                b'\xff\xc0\x00\x0b\x08\x00\x14\x00\x50\x01\x01\x11\x00\xff\xd9')
            files['OPS/images/broken.jpg'] = b'\xff\xd8\xff\xe1\x00\x20broken'
            # A genuinely large, incompressible unused asset must not reject the entire book.
            files['OPS/images/unused.bin'] = os.urandom(24 * 1024 * 1024)
        case('good_resources', resources)
        case('bad_escape', replace_opf('../text/part%201%26x.xhtml#start', '../../../escape.xhtml'))
        case('bad_url', replace_opf('../text/part%201%26x.xhtml#start', 'https://example.org/a.xhtml'))
        case('bad_percent', replace_opf('../text/part%201%26x.xhtml#start', '../text/%XX.xhtml'))
        case('bad_duplicate', replace_opf('id="c2"', 'id="c1"'))
        case('bad_missingref', replace_opf('idref="c2"', 'idref="missing"'))
        case('bad_nul', replace_opf('</package>', '\0</package>'))
        case('bad_xml', replace_opf('</manifest>', '</wrong>'))
        case('bad_multiroot', replace_opf('</package>', '</package><extra/>'))
        case('bad_spine_limit', replace_opf('<itemref idref="c1"/>', '<itemref idref="c1"/>' * 8193))
        case('bad_container', lambda f: f.__setitem__('META-INF/container.xml', '<container><rootfile full-path="../../escape.opf"/></container>'))
        subprocess.run([str(exe)] + [str(p) for p in sorted((ROOT / 'build/book-fixtures/books').glob('*.epub'))] + [str(p) for p in cases], check=True)

        large = work / 'long_5000.epub'
        make_large_book(large)
        large_exe = work / 'large-test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'tools/book_epub_stubs'), '-I' + str(ROOT / 'tools/zip_host_stubs'),
                        '-I' + str(ROOT / 'main/book'), str(ROOT / 'tools/book_epub_large_host_test.c'),
                        str(ROOT / 'main/book/book_epub.c'), str(ROOT / 'main/book/zip_reader.c'),
                        str(ROOT / 'main/book/html_text.c'), str(ROOT / 'main/book/book_index_cache.c'),
                        str(ROOT / 'main/book/book_image_header.c'),
                        '-lz', '-o', str(large_exe)], check=True)
        subprocess.run([str(large_exe), str(large)], check=True)


if __name__ == '__main__':
    main()
