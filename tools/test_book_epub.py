#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：生成有界 EPUB 元数据测试。/ English: Generate bounded EPUB metadata tests.
冻结：仅测试，不写产品构建。/ Frozen: Tests only, no product build writes.
"""
from pathlib import Path
import subprocess
import tempfile
import zipfile
import sys

ROOT = Path(__file__).resolve().parents[1]


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
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-DBOOK_HEAP_TRACK', '-Wl,--wrap=free',
                        '-I' + str(ROOT / 'tools/book_epub_stubs'), '-I' + str(ROOT / 'tools/zip_host_stubs'),
                        '-I' + str(ROOT / 'main/book'), str(ROOT / 'tools/book_epub_host_test.c'),
                        str(ROOT / 'main/book/book_epub.c'), str(ROOT / 'main/book/zip_reader.c'),
                        str(ROOT / 'main/book/html_text.c'), str(ROOT / 'main/book/book_image.c'), str(ROOT / 'main/book/vendor/tjpgd.c'), str(ROOT / 'tools/book_heap_track.c'), '-lz', '-o', str(exe)], check=True)
        cases = []

        def case(name, edit=None):
            file = work / (name + '.epub'); make_book(file, edit); cases.append(file)

        def replace_opf(old, new):
            return lambda files: files.__setitem__('OPS/pkg/book.opf', files['OPS/pkg/book.opf'].replace(old, new))

        case('good_paths')
        case('good_navfallback', lambda f: f.__setitem__('OPS/toc/book.ncx', '<ncx><broken></ncx>'))
        case('good_navfallback_partial', lambda f: f.__setitem__('OPS/toc/book.ncx', f['OPS/toc/book.ncx'] + '<broken>'))
        case('good_defaults', lambda f: (f.pop('OPS/toc/book.ncx'), f.pop('OPS/toc/nav.xhtml')))
        case('good_defaults_emptytoc', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/toc/nav.xhtml', '<html><nav epub:type="toc"/><a href="../text/part%201%26x.xhtml">WRONG</a></html>')))
        case('good_samefile', lambda f: (f.pop('OPS/toc/book.ncx'), f.__setitem__('OPS/pkg/book.opf', f['OPS/pkg/book.opf'].replace('../text/part%201%26x.xhtml#start', '../toc/nav.xhtml')), f.__setitem__('OPS/toc/nav.xhtml', f['OPS/toc/nav.xhtml'].replace('../text/part%201%26x.xhtml', '#start'))))
        case('bad_escape', replace_opf('../text/part%201%26x.xhtml#start', '../../../escape.xhtml'))
        case('bad_url', replace_opf('../text/part%201%26x.xhtml#start', 'https://example.org/a.xhtml'))
        case('bad_percent', replace_opf('../text/part%201%26x.xhtml#start', '../text/%XX.xhtml'))
        case('bad_duplicate', replace_opf('id="c2"', 'id="c1"'))
        case('bad_missingref', replace_opf('idref="c2"', 'idref="missing"'))
        case('bad_nul', replace_opf('</package>', '\0</package>'))
        case('bad_xml', replace_opf('</manifest>', '</wrong>'))
        case('bad_multiroot', replace_opf('</package>', '</package><extra/>'))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, 5))
        case('good_spine_2048', replace_opf(spine, '<itemref idref="c1"/>' * 2048))
        case('good_spine_limit', replace_opf(spine, '<itemref idref="c1"/>' * 32768))
        case('bad_spine_limit', replace_opf(spine, '<itemref idref="c1"/>' * 32769))
        def large_book(files, count=1486):
            items = ''.join(f'<item id="large{i}" href="../text/large{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(count))
            refs = ''.join(f'<itemref idref="large{i}"/>' for i in range(count))
            files['OPS/pkg/book.opf'] = f'<package><manifest>{items}</manifest><spine>{refs}</spine></package>'
            for i in range(count):
                files[f'OPS/text/large{i}.xhtml'] = f'<html><body><p>Chapter {i}</p></body></html>'
        case('good_large', large_book)
        case('good_many', lambda files: large_book(files, 30000))
        def titled_many(files):
            large_book(files, 30000)
            files['OPS/pkg/book.opf'] = files['OPS/pkg/book.opf'].replace('</manifest>', '<item id="nav" href="../toc/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>')
            links = ''.join(f'<a href="../text/large{i}.xhtml">Chapter {i}</a>' for i in range(30000))
            files['OPS/toc/nav.xhtml'] = f'<html><nav epub:type="toc">{links}</nav></html>'
        case('good_many_titled', titled_many)
        case('good_manifest_limit', replace_opf('</manifest>', ''.join(f'<item id="extra{i}" href="extra{i}" media-type="image/png"/>' for i in range(32762)) + '</manifest>'))
        case('bad_manifest_limit', replace_opf('</manifest>', ''.join(f'<item id="extra{i}" href="extra{i}" media-type="image/png"/>' for i in range(32763)) + '</manifest>'))
        case('bad_container', lambda f: f.__setitem__('META-INF/container.xml', '<container><rootfile full-path="../../escape.opf"/></container>'))
        subprocess.run([str(exe)] + [str(p) for p in sorted((ROOT / 'build/book-fixtures/books').glob('*.epub'))] + [str(p) for p in cases], check=True)
        if len(sys.argv) > 1:
            subprocess.run([str(exe), '--inspect'] + sys.argv[1:], check=True)


if __name__ == '__main__':
    main()
