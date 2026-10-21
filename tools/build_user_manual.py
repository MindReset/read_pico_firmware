#!/usr/bin/env python3
"""Build the HTML and PDF user manual from shared Chinese content.
中文：同一份内容生成网页与 PDF；只读取清单，不改变固件。
English: Generate HTML/PDF from one manuscript without modifying firmware.
"""
from pathlib import Path
from html import escape
import json
import argparse
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, Image, KeepTogether
from reportlab.lib.pagesizes import A4

ROOT = Path(__file__).resolve().parents[1]
FLASH = ROOT / 'flash'

def build(font_path: Path, pdf_output: Path):
    content = json.loads((ROOT / 'docs/user-manual.zh-CN.json').read_text())
    manifest = json.loads((FLASH / 'manifest.json').read_text())
    version = manifest['version']
    date = manifest['updated_at'].split('T', 1)[0]
    nav = ''.join(f'<a href="#{c["id"]}">{i:02d} / {escape(c["title"])}</a>' for i,c in enumerate(content['chapters'],1))
    sections = []
    for i, chapter in enumerate(content['chapters'], 1):
        blocks = []
        for b in chapter['blocks']:
            kind = b['kind']
            if kind in ('para', 'note'):
                cls = ' class="note"' if kind == 'note' else ''
                blocks.append(f'<p{cls}>{escape(b["text"])}</p>')
            elif kind == 'heading': blocks.append(f'<h3>{escape(b["text"])}</h3>')
            elif kind == 'steps': blocks.append('<ol>'+''.join(f'<li>{escape(item)}</li>' for item in b['items'])+'</ol>')
            elif kind == 'table':
                header=''.join(f'<th scope="col">{escape(t)}</th>' for t in b['headers'])
                rows=''.join('<tr>'+''.join(f'<td>{escape(t)}</td>' for t in row)+'</tr>' for row in b['rows'])
                blocks.append(f'<table><thead><tr>{header}</tr></thead><tbody>{rows}</tbody></table>')
            elif kind == 'images':
                figures=''.join(f'<figure><img src="./assets/screens/{key}.png" alt="{escape(caption)}" loading="lazy"><figcaption>{escape(caption)}</figcaption></figure>' for key,caption in zip(b['keys'],b['captions']))
                blocks.append(f'<div class="manual-cover-preview">{figures}</div>')
        sections.append(f'<section id="{chapter["id"]}"><p class="eyebrow">{i:02d} / GUIDE</p><h2>{escape(chapter["title"])}</h2><p>{escape(chapter["intro"])}</p>'+''.join(blocks)+'</section>')
    html = f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#ffffff"><meta name="description" content="kiikoread 墨水屏固件中文使用说明：安装、传书、阅读、升级和恢复。"><title>使用说明 · kiikoread</title><link rel="stylesheet" href="./site.css"></head><body><a class="skip" href="#main">跳到正文</a><header class="site-header"><div class="header-inner"><a class="wordmark" href="./index.html">kiikoread<span>.</span></a><nav aria-label="主导航"><a href="./index.html#preview">界面预览</a><a href="./index.html#features">特色功能</a><a href="./manual.html" aria-current="page">使用说明</a></nav><a class="button small primary" href="./index.html#flash-action">刷机与下载 ↗</a></div></header><main id="main"><section class="wrap manual-hero"><p class="eyebrow">THE USER GUIDE</p><h1>用起来，就顺手。</h1><p>从第一次刷机，到传书、阅读与升级。<br>这份说明帮你找到每一步。</p><p class="manual-version">当前正式版 {escape(version)} · {date} · 适用墨水屏 RDP-G01-W</p><a class="text-link" href="./manual/kiikoread-manual.pdf" download>下载中文 PDF <img src="./assets/icons/download.svg" width="18" height="18" alt=""></a></section><div class="wrap manual-layout"><nav class="manual-nav" aria-label="说明书目录">{nav}</nav><div class="manual-body">{''.join(sections)}<a class="button primary" href="./index.html#flash-action">准备好了，去刷机 →</a></div></div></main><footer class="site-footer"><div class="wrap footer-bottom"><span>kiikoread · 基于 MindReset 开源固件，非官方发布</span><span><a href="./notices.html">开源许可</a> / <a href="https://github.com/wegooo-cell/read-pico-reader">GitHub</a></span></div></footer></body></html>'''
    (FLASH / 'manual.html').write_text(html, encoding='utf-8')

    manual_font = TTFont('Manual', str(font_path))
    required_text = json.dumps(content, ensure_ascii=False) + '使用说明当前正式版墨水屏基于开源固件' + version + date
    missing = sorted({c for c in required_text if ord(c) > 127 and ord(c) not in manual_font.face.charToGlyph})
    if missing:
        raise ValueError('Manual font is missing glyphs: ' + ''.join(missing))
    pdfmetrics.registerFont(manual_font)
    ink=colors.HexColor('#141616'); muted=colors.HexColor('#596060'); line=colors.HexColor('#d7dddd')
    styles={
        'body': ParagraphStyle('body',fontName='Manual',fontSize=10.4,leading=18,textColor=ink,wordWrap='CJK',spaceAfter=10),
        'intro': ParagraphStyle('intro',fontName='Manual',fontSize=11,leading=19,textColor=muted,wordWrap='CJK',spaceAfter=21),
        'title': ParagraphStyle('title',fontName='Manual',fontSize=27,leading=38,textColor=ink,wordWrap='CJK',spaceAfter=14),
        'head': ParagraphStyle('head',fontName='Manual',fontSize=14,leading=22,textColor=ink,wordWrap='CJK',spaceBefore=12,spaceAfter=7,keepWithNext=True),
        'note': ParagraphStyle('note',fontName='Manual',fontSize=9,leading=16,textColor=muted,wordWrap='CJK',spaceBefore=9,spaceAfter=12,borderColor=line,borderWidth=.6,borderPadding=12),
        'table': ParagraphStyle('table',fontName='Manual',fontSize=9.4,leading=16,textColor=ink,wordWrap='CJK'),
        'caption': ParagraphStyle('caption',fontName='Manual',fontSize=8,leading=14,textColor=muted,wordWrap='CJK',spaceBefore=7),
        'eyebrow': ParagraphStyle('eyebrow',fontName='Helvetica',fontSize=9,leading=14,textColor=muted,spaceAfter=13),
    }
    def para(text, kind='body'): return Paragraph(escape(text),styles[kind])
    flow=[]
    for i,c in enumerate(content['chapters'],1):
        if i>1: flow.append(PageBreak())
        flow.extend([para(f'{i:02d} / USER GUIDE','eyebrow'),para(c['title'],'title'),para(c['intro'],'intro')])
        if i==1: flow.append(para(f'当前正式版 {version} · 墨水屏 RDP-G01-W · {date}','caption'))
        for b in c['blocks']:
            if b['kind']=='heading': flow.append(para(b['text'],'head'))
            elif b['kind'] in ('para','note'): flow.append(para(b['text'], 'note' if b['kind']=='note' else 'body'))
            elif b['kind']=='steps':
                for n,item in enumerate(b['items'],1): flow.append(para(f'{n}. {item}'))
            elif b['kind']=='table':
                rows=[[para(t,'table') for t in b['headers']]]+[[para(t,'table') for t in row] for row in b['rows']]
                t=Table(rows,colWidths=[117,A4[0]-108-117],hAlign='LEFT',repeatRows=1)
                t.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),8),('RIGHTPADDING',(0,0),(-1,-1),8),('TOPPADDING',(0,0),(-1,-1),9),('BOTTOMPADDING',(0,0),(-1,-1),9),('LINEABOVE',(0,0),(-1,0),.7,ink),('LINEBELOW',(0,0),(-1,-1),.5,line)]))
                flow.extend([t,Spacer(1,13)])
            elif b['kind']=='images':
                cells=[]
                for key,caption in zip(b['keys'],b['captions']):
                    from PIL import Image as PILImage
                    path=FLASH / f'assets/screens/{key}.png'
                    with PILImage.open(path) as im: w,h=im.size
                    cells.append([Image(str(path),width=110,height=110*h/w),para(caption,'caption')])
                t=Table([cells],colWidths=[170,170],hAlign='LEFT')
                t.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),0)]))
                flow.extend([t,Spacer(1,16)])
    def page(canvas,doc):
        width,height=A4
        canvas.saveState()
        canvas.setFillColor(ink); canvas.setFont('Helvetica-Bold',14); canvas.drawString(54,height-35,'kiikoread.')
        canvas.setFont('Manual',8); canvas.setFillColor(muted); canvas.drawRightString(width-54,height-34,f'使用说明 / {version}')
        canvas.setStrokeColor(line); canvas.setLineWidth(.5); canvas.line(54,height-45,width-54,height-45)
        canvas.line(54,40,width-54,40)
        canvas.setFont('Manual',8); canvas.drawString(54,25,'墨水屏 RDP-G01-W · 基于 MindReset 开源固件')
        canvas.setFont('Helvetica',8); canvas.drawRightString(width-54,25,f'{doc.page:02d}')
        canvas.restoreState()
    pdf_output.parent.mkdir(parents=True,exist_ok=True)
    doc=SimpleDocTemplate(str(pdf_output),pagesize=A4,rightMargin=54,leftMargin=54,topMargin=72,bottomMargin=57,title=content['title'],author='kiikoread / Kiiko')
    doc.build(flow,onFirstPage=page,onLaterPages=page)
    public=FLASH / 'manual/kiikoread-manual.pdf'
    public.parent.mkdir(exist_ok=True)
    public.write_bytes(pdf_output.read_bytes())
    print(f'Manual HTML and PDF built for {version}: {public.stat().st_size} bytes')

if __name__=='__main__':
    args=argparse.ArgumentParser()
    args.add_argument('--font',type=Path,required=True)
    args.add_argument('--pdf-output',type=Path,default=ROOT/'output/pdf/kiikoread-manual.pdf')
    options=args.parse_args()
    build(options.font,options.pdf_output)
