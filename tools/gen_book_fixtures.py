#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 生成原创测试书并校验编码和 EPUB 结构，不访问设备。/ Generate original fixtures and verify encoding and EPUB structure without device access.

import argparse
import hashlib
import json
import posixpath
from pathlib import Path
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import zipfile


TITLES = ["第一章 河边清晨", "第二章 雨后小路", "第三章 山间书屋", "第四章 灯下归途"]
PARAGRAPH = (
    "清晨的河水映着天光，小舟从石桥下面慢慢经过。"
    "我们带上纸笔，沿着岸边的小路记录今天看到的景色。"
    "树叶上的水珠落在石阶上，远处的屋顶升起一缕白烟。"
    "走到书屋门前，大家停下来读了一页书，又把心里的问题写在纸上。"
    "每一次出发都有新的发现，每一次归来都有值得保存的记忆。"
)
NS = {
    "opf": "http://www.idpf.org/2007/opf",
    "ocf": "urn:oasis:names:tc:opendocument:xmlns:container",
    "ncx": "http://www.daisy.org/z3986/2005/ncx/",
    "x": "http://www.w3.org/1999/xhtml",
}


def paragraphs(chapter, count):
    return [f"记录 {chapter + 1}-{i + 1:03d}：{PARAGRAPH}" for i in range(count)]


def text_book(count):
    return "\n\n".join(
        title + "\n\n" + "\n\n".join(paragraphs(chapter, count))
        for chapter, title in enumerate(TITLES)
    ) + "\n"


def zip_entry(archive, name, content, compression=zipfile.ZIP_DEFLATED):
    # 固定时间戳和权限，重复生成保持同样字节。/ Fix timestamps and permissions for byte-identical regeneration.
    info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.compress_type = compression
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    archive.writestr(info, content.encode("utf-8"))


def make_epub(path, navigation, div_heavy=False):
    version = "2.0" if navigation == "ncx" else "3.0"
    toc_item = (
        '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
        if navigation == "ncx" else
        '<item id="toc" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
    )
    items = "".join(
        f'<item id="c{i}" href="text/ch{i}.xhtml" media-type="application/xhtml+xml"/>'
        for i in range(1, 5)
    )
    spine = "".join(f'<itemref idref="c{i}"/>' for i in range(1, 5))
    toc_attr = ' toc="toc"' if navigation == "ncx" else ""
    modified = '<meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>' if version == "3.0" else ""
    package = f'''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="{NS['opf']}" version="{version}" unique-identifier="book-id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="book-id">urn:read-pico:fixture:{navigation}:{int(div_heavy)}</dc:identifier>
<dc:title>河边记事</dc:title><dc:language>zh-CN</dc:language><dc:creator>Read Pico fixtures</dc:creator>{modified}
</metadata><manifest>{toc_item}{items}</manifest><spine{toc_attr}>{spine}</spine></package>'''
    container = f'''<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="{NS['ocf']}" version="1.0"><rootfiles>
<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
</rootfiles></container>'''
    with zipfile.ZipFile(path, "w") as archive:
        zip_entry(archive, "mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        zip_entry(archive, "META-INF/container.xml", container)
        zip_entry(archive, "OEBPS/content.opf", package)
        for index, title in enumerate(TITLES, 1):
            body = "".join(
                f'<div class="paragraph"><div><span>{escape(p)}</span></div></div>'
                if div_heavy else f"<p>{escape(p)}</p>"
                for p in paragraphs(index - 1, 60)
            )
            chapter = f'''<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="{NS['x']}" xml:lang="zh-CN"><head><title>{title}</title></head>
<body><h1 id="start">{title}</h1>{body}<p>实体检查：&amp; &lt; &gt; &#160;，段落终点。</p></body></html>'''
            zip_entry(archive, f"OEBPS/text/ch{index}.xhtml", chapter)
        if navigation == "ncx":
            points = "".join(
                f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>{title}</text></navLabel>'
                f'<content src="text/ch{i}.xhtml#start"/></navPoint>'
                for i, title in enumerate(TITLES, 1)
            )
            toc = f'''<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="{NS['ncx']}" version="2005-1"><head>
<meta name="dtb:uid" content="urn:read-pico:fixture:ncx:0"/>
<meta name="dtb:depth" content="1"/><meta name="dtb:totalPageCount" content="0"/>
<meta name="dtb:maxPageNumber" content="0"/></head>
<docTitle><text>河边记事</text></docTitle><navMap>{points}</navMap></ncx>'''
            zip_entry(archive, "OEBPS/toc.ncx", toc)
        else:
            links = "".join(
                f'<li><a href="text/ch{i}.xhtml#start">{title}</a></li>'
                for i, title in enumerate(TITLES, 1)
            )
            toc = f'''<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="{NS['x']}" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="zh-CN">
<head><title>目录</title></head><body><nav epub:type="toc" id="toc"><h1>目录</h1>
<ol>{links}</ol></nav></body></html>'''
            zip_entry(archive, "OEBPS/nav.xhtml", toc)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_epub(path, navigation, div_heavy):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, "ZIP CRC failure")
        first = archive.infolist()[0]
        require(first.filename == "mimetype" and first.compress_type == zipfile.ZIP_STORED, "mimetype order/compression")
        require(archive.read("mimetype") == b"application/epub+zip", "mimetype content")
        require(all(i.compress_type == zipfile.ZIP_DEFLATED for i in archive.infolist()[1:]), "deflated fixture entries")
        for name in archive.namelist()[1:]:
            ET.fromstring(archive.read(name))
        container = ET.fromstring(archive.read("META-INF/container.xml"))
        opf_path = container.find("ocf:rootfiles/ocf:rootfile", NS).attrib["full-path"]
        package = ET.fromstring(archive.read(opf_path))
        items = {item.attrib["id"]: item for item in package.findall("opf:manifest/opf:item", NS)}
        for item in items.values():
            require(posixpath.join(posixpath.dirname(opf_path), item.attrib["href"]) in archive.namelist(), "missing manifest resource")
        spine = package.findall("opf:spine/opf:itemref", NS)
        require([item.attrib["idref"] for item in spine] == ["c1", "c2", "c3", "c4"], "spine order")
        toc_path = "OEBPS/" + ("toc.ncx" if navigation == "ncx" else "nav.xhtml")
        toc = ET.fromstring(archive.read(toc_path))
        if navigation == "ncx":
            targets = [x.attrib["src"] for x in toc.findall(".//ncx:content", NS)]
            labels = [x.text for x in toc.findall(".//ncx:navLabel/ncx:text", NS)]
        else:
            require(not any(name.endswith(".ncx") for name in archive.namelist()), "nav-only fixture has NCX")
            require(items["toc"].attrib.get("properties") == "nav", "missing nav property")
            links = toc.findall(".//x:nav/x:ol/x:li/x:a", NS)
            targets = [x.attrib["href"] for x in links]
            labels = [x.text for x in links]
        require(labels == TITLES, "TOC labels/order")
        for target in targets:
            href, anchor = target.split("#")
            chapter = ET.fromstring(archive.read(posixpath.join("OEBPS", href)))
            require(any(node.attrib.get("id") == anchor for node in chapter.iter()), "TOC anchor missing")
            if div_heavy:
                require(len(chapter.findall(".//x:div", NS)) == 120, "nested div coverage")


def main():
    parser = argparse.ArgumentParser(description="Generate and self-check original TXT/EPUB test books.")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "build/book-fixtures")
    args = parser.parse_args()
    books = args.output / "books"
    books.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, encoding, count, purpose in [
        ("河边记事_UTF8.txt", "utf-8", 100, "UTF-8 without BOM; 20+ page turns and progress persistence"),
        ("河边记事_UTF8_BOM.txt", "utf-8-sig", 100, "UTF-8 BOM removal and chapter detection"),
        ("河边记事_GBK.txt", "gbk", 100, "GBK decoding; Chinese filenames and chapter detection"),
        ("flash_small.txt", "utf-8", 4, "Under 1 MiB; flash fallback without TF card"),
    ]:
        path = books / name
        expected = text_book(count)
        path.write_bytes(expected.encode(encoding))
        data = path.read_bytes()
        require(data.decode(encoding) == expected, "TXT encoding round trip")
        require(data.startswith(b"\xef\xbb\xbf") == (encoding == "utf-8-sig"), "TXT BOM")
        require(all(title in data.decode(encoding) for title in TITLES), "TXT chapter labels")
        if encoding == "gbk":
            try:
                data.decode("utf-8")
            except UnicodeDecodeError:
                pass
            else:
                raise ValueError("GBK fixture incorrectly accepts UTF-8")
        require(len(data) < 1024 * 1024, "TXT exceeds flash test limit")
        manifest.append(dict(file=f"books/{name}", encoding=encoding, chapters=TITLES, purpose=purpose))
    for name, navigation, div_heavy in [
        ("河边记事_ncx.epub", "ncx", False),
        ("河边记事_nav.epub", "nav", False),
        ("河边记事_div.epub", "nav", True),
    ]:
        path = books / name
        make_epub(path, navigation, div_heavy)
        check_epub(path, navigation, div_heavy)
        manifest.append(dict(file=f"books/{name}", encoding="utf-8", chapters=TITLES, navigation=navigation,
                             purpose="Nested div paragraph boundaries" if div_heavy else f"{navigation} TOC and spine order"))
    for item in manifest:
        data = (args.output / item["file"]).read_bytes()
        item.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    output = dict(schema=1, generator="tools/gen_book_fixtures.py", original_content=True,
                  verification="Host encoding/XML/ZIP/reference checks passed; on-device pagination and rendering not verified.",
                  fixtures=manifest)
    (args.output / "manifest.json").write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(manifest)} books; encoding/XML/ZIP/TOC self-checks passed.")
    print(f"Output: {args.output.resolve()}")


if __name__ == "__main__":
    main()
