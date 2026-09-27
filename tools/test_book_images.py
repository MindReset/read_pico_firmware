#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""图片解码与书源集成回归。/ Image decoding and book source integration regressions."""
import base64
from pathlib import Path
import os
import struct
import subprocess
import zlib
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def png(width=16, height=8):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    row = b''.join(bytes((0, 0, 0, (0, 128, 255)[x % 3])) for x in range(width))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress((b'\0' + row) * height)) + chunk(b'IEND', b'')

# 自制16×8灰色JPEG，固定夹具避免新增图像工具依赖。
# Locally generated 16x8 gray JPEG; a fixed fixture avoids extra image-tool dependencies.
JPEG = base64.b64decode('/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAAIABADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDKooooA//Z')

def gray_jpeg(width, height, overshoot=False):
    # 单色基线 JPEG：DC 差值零与 AC EOB 各一位，无外部编码器依赖。
    # Constant baseline JPEG: one bit each for zero DC delta and AC EOB, without an external encoder.
    def marker(tag, data):
        return b'\xff' + bytes([tag]) + struct.pack('>H', len(data) + 2) + data
    dqt = marker(0xdb, b'\0' + bytes([1]) * 64)
    sof = marker(0xc0, struct.pack('>BHHBBBB', 8, height, width, 1, 1, 0x11, 0))
    table = bytes([1]) + bytes(15) + b'\0'
    dc_table=bytes([1,1])+bytes(14)+bytes([0,11]) if overshoot else table
    dht = marker(0xc4, b'\0' + dc_table + b'\x10' + table)
    sos = marker(0xda, bytes([1, 1, 0, 0, 63, 0]))
    bits = ((width + 7) // 8) * ((height + 7) // 8) * 2
    bitstream=('10'+format(1080,'011b')+'0'+'0'*(bits-2)) if overshoot else '0'*bits
    bitstream+='1'*((-len(bitstream))%8)
    entropy=bytes(int(bitstream[i:i+8],2) for i in range(0,len(bitstream),8)).replace(b'\xff',b'\xff\0')
    return b'\xff\xd8' + dqt + sof + dht + sos + entropy + b'\xff\xd9'

def filtered_png(color, filter_type):
    def chunk(tag,data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    def paeth(a,b,c):
        p=a+b-c
        return min((a,b,c),key=lambda x: abs(p-x))
    channels={0:1,2:3,3:1,4:2,6:4}[color]
    rows=[];expected=bytearray();previous=bytes(16*channels)
    for y in range(8):
        row=bytearray()
        for x in range(16):
            v=(x*13+y*23)%256;alpha=(x*17+y*31)%256
            rgb=(v,(v+37)%256,(v+91)%256)
            if color==3:
                index=(x+y)%3;row.append(index);gray=(0,128,255)[index];alpha=(0,128,255)[index]
            elif color in (0,4):
                row.append(v);gray=v
                if color==4:row.append(alpha)
                else:alpha=0 if v==0 else 255
            else:
                row.extend(rgb);gray=(77*rgb[0]+150*rgb[1]+29*rgb[2])>>8
                if color==6:row.append(alpha)
                else:alpha=0 if rgb==(0,37,91) else 255
            expected.append((gray*alpha+255*(255-alpha)+127)//255)
        filtered=bytearray([filter_type])
        for x,value in enumerate(row):
            a=row[x-channels] if x>=channels else 0;b=previous[x];c=previous[x-channels] if x>=channels else 0
            add=(0,a,b,(a+b)//2,paeth(a,b,c))[filter_type]
            filtered.append((value-add)%256)
        rows.append(filtered);previous=row
    extra=b''
    if color==3:extra=chunk(b'PLTE',bytes((0,0,0,128,128,128,255,255,255)))+chunk(b'tRNS',bytes((0,128,255)))
    elif color==0:extra=chunk(b'tRNS',b'\0\0')
    elif color==2:extra=chunk(b'tRNS',struct.pack('>HHH',0,37,91))
    packed=zlib.compress(b''.join(rows));mid=len(packed)//2
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',16,8,8,color,0,0,0))+extra+chunk(b'IDAT',packed[:mid])+chunk(b'IDAT',packed[mid:])+chunk(b'IEND',b'')
    return data,bytes(expected)

def main():
    work = ROOT / 'build/book-image-tests'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'alpha.png').write_bytes(png())
    (work / 'gray.jpg').write_bytes(JPEG)
    for name, width, height in [('cover',1000,1333),('tall',1080,2400),('edge',9,17)]:
        (work / (name + '.jpg')).write_bytes(gray_jpeg(width,height))
    book = work / 'images.epub'
    with zipfile.ZipFile(book, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
        z.writestr('OPS/book.opf', '<package><manifest><item id="a" href="text/chapter.xhtml" media-type="application/xhtml+xml"/><item id="b" href="other/chapter.xhtml" media-type="application/xhtml+xml"/><item id="c" href="text/titled.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/><itemref idref="c"/></spine></package>')
        z.writestr('OPS/text/chapter.xhtml', '<p>before</p><img src="../images/a%20b&amp;c.png"/><svg><image xlink:href="../images/gray.jpg"/></svg><img src="https://invalid/a.png"/><img src="../../../escape.png"/><img src="../images/broken.png"/><img src="../images/huge.png"/><img src="../images/absent.png"/><p>after</p>')
        z.writestr('OPS/other/chapter.xhtml', '<img src="../images/./a%20b%26c.png"/><img src="../images/gray.jpg#same"/>')
        z.writestr('OPS/text/titled.xhtml', '<img src="../images/a%20b%26c.png"/><h2>Chapter title</h2><img src="../images/gray.jpg"/><p>body</p>')
        z.writestr('OPS/images/a b&c.png', png())
        z.writestr('OPS/images/gray.jpg', JPEG)
        z.writestr('OPS/images/broken.png', b'broken')
        z.writestr('OPS/images/huge.png', png(1025, 1024))
    exe = work / 'test'
    sources = ['tools/book_image_host_test.c', 'main/book/book_image.c', 'main/book/vendor/tjpgd.c', 'main/book/book_epub.c', 'main/book/html_text.c', 'main/book/zip_reader.c']
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-Itools/book_image_stubs', '-Itools/book_epub_stubs', '-Itools/zip_host_stubs', '-Imain/book', *sources, '-lz', '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe), str(work / 'alpha.png'), str(work / 'gray.jpg'), str(book), *[str(work / (n + '.jpg')) for n in ['cover','tall','edge']]], check=True, env={**os.environ, 'UBSAN_OPTIONS':'halt_on_error=1'})
    for color in [0,2,3,4,6]:
        for filter_type in range(5):
            data,expected=filtered_png(color,filter_type)
            name=work / f'filter-{color}-{filter_type}'
            name.with_suffix('.png').write_bytes(data);name.with_suffix('.gray').write_bytes(expected)
            subprocess.run([str(exe),'--expect',str(name.with_suffix('.png')),str(name.with_suffix('.gray'))],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
    (work/'overshoot.jpg').write_bytes(gray_jpeg(16,8,True))
    (work/'overshoot.gray').write_bytes(bytes([255])*128)
    subprocess.run([str(exe),'--expect',str(work/'overshoot.jpg'),str(work/'overshoot.gray')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
    print('PNG: all 5 row filters, 5 color types, split IDAT, palette and color-key alpha passed')

if __name__ == '__main__':
    main()
