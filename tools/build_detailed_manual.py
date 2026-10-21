#!/usr/bin/env python3
"""Create a detailed, illustrated manual preview, without touching the live site.

中文：由同一份经核对的内容生成可搜索网页和带目录的 PDF，只写指定输出目录。
English: Build searchable HTML and a bookmarked PDF into an explicit output directory.
"""
from __future__ import annotations

import argparse
import json
from html import escape
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]

# 中文：图解使用真实固件绘图；编号只作为说明书标注，不是设备新增按钮。
# English: Numbered overlays annotate native firmware drawings, not additional controls.
DIAGRAMS = {
    'home': ('首页：继续阅读与近期记录', 0, 1216, [(1, 611, 197, '点继续阅读，打开最近一本书'), (2, 145, 1180, '底栏切换四个主页面')]),
    'reader': ('正文与阅读工具栏', 0, 1216, [(1, 652, 107, '非全屏题头右侧：收藏整本书'), (2, 484, 1179, '阅读设置入口'), (3, 620, 1179, '字体设置入口')]),
    'shelf-acrylic': ('亚克力书架', 0, 1216, []),
    'shelf-dark': ('深色书架', 0, 1216, []),
    'shelf-list': ('列表书架', 0, 1216, [(1, 611, 107, '导入书籍'), (2, 488, 107, '进入管理书架'), (3, 480, 1065, '底部箭头切换书架页')]),
    'shelf-manage': ('管理书架：排序、搜索与批量操作', 160, 1048, [(1, 418, 247, '按名称 / 按最近：切换排序'), (2, 635, 247, '搜索'), (3, 629, 330, '点圆圈勾选当前书'), (4, 632, 999, '跨页选择后，再执行批量操作')]),
    'wifi': ('无线网络：连接、断开与遗忘', 0, 1216, [(1, 622, 341, '连接 / 断开按钮'), (2, 629, 404, '遗忘网络：删除保存的连接配置'), (3, 626, 886, '重新扫描附近网络')]),
    'ime-nine': ('九宫格：中文拼音与候选', 548, 534, [(1, 305, 582, '顶部切换九宫格 / 全键盘'), (2, 640, 692, '候选词翻页'), (3, 103, 775, '左侧选择读音'), (4, 653, 852, '重输只清待选拼音；清空会清文本')]),
    'ime-pinyin': ('全键盘：连续拼音选词', 548, 534, [(1, 640, 692, '候选词右翻页'), (2, 513, 1026, '中 / En：切换输入语言'), (3, 402, 1026, '有待选拼音时，空格位置变为选择')]),
    'ime-upper': ('英文：大小写切换', 548, 534, [(1, 99, 930, 'a / A：切换大小写'), (2, 513, 1026, 'En / 中：回到中文拼音'), (3, 99, 1026, '123：进入数字页')]),
    'ime-numbers': ('数字与符号', 548, 534, [(1, 99, 1026, 'ABC：回到字母键盘'), (2, 187, 1026, '符号：进入符号页')]),
    'ime-password': ('WiFi 密码：常亮光标与英文键盘', 145, 937, [(1, 613, 285, '输入框右侧箭头移动光标'), (2, 648, 930, '删除；长按连续删除')]),
    'reading-settings': ('阅读设置：刷新、翻页与按键', 168, 1040, [(1, 626, 293, '手动全刷'), (2, 639, 684, '翻页模式'), (3, 648, 1070, '按键控制')]),
    'keys': ('按键控制：三键短按与中键长按', 270, 934, [(1, 647, 679, '中键长按可以单独自定义'), (2, 646, 918, '推荐设置 1'), (3, 646, 1064, '推荐设置 2')]),
    'key-actions': ('功能列表：选择后返回核对', 270, 934, [(1, 645, 628, '至少保留一个阅读工具栏动作')]),
    'tap-zones': ('翻页区域：左右各半，上 1/3 下 2/3', 300, 904, []),
    'font-settings': ('字体设置：阅读字号与排版入口', 640, 568, [(1, 650, 786, '滑动调整阅读字号'), (2, 650, 995, '进入排版设置')]),
    'layout-settings': ('排版：行距、段距、字距与首行微调', 575, 633, [(1, 652, 1090, '首行微调：减 1 px / 复位 / 加 1 px')]),
    'reading-lines': ('阅读线：样式与上下微调', 750, 458, []),
    'auto-lock': ('自动休眠：1、5、10 分钟或关闭', 0, 1216, []),
    'refresh-modes': ('主页刷新：普通、快刷、水波纹', 0, 1216, []),
    'backup': ('保存与恢复：先保存，再确认恢复', 0, 1216, [(1, 645, 581, '保存当前配置到 TF 卡'), (2, 645, 697, '从 TF 卡恢复配置')]),
    'boot': ('软件 BOOT：进入电脑刷写模式', 0, 1216, [(1, 641, 549, '连接 USB 后点进入 BOOT 模式')]),
    'lock-styles': ('锁屏样式：壁纸、票根、书架拼贴', 0, 1216, []),
    'lock-ticket': ('阅读票根锁屏', 0, 1216, []),
    'lock-wallpaper': ('壁纸锁屏', 0, 1216, []),
    'lock-collage': ('书架拼贴锁屏', 0, 1216, []),
    'files-home': ('文件管理：存储卡与三种传输入口', 0, 1216, [(1, 187, 383, 'WiFi：双方连接同一网络'), (2, 397, 383, '热点：手机连接设备提供的网络'), (3, 606, 383, 'USB：电脑直接读取 TF 卡')]),
    'file-actions': ('长按条目后的文件操作菜单', 430, 626, [(1, 317, 698, '重命名：编辑名称主体'), (2, 627, 698, '复制：在原目录创建副本'), (3, 317, 820, '移动：再选择目标目录'), (4, 627, 820, '删除：需要确认')]),
    'bluetooth': ('蓝牙翻页器：连接状态与按键学习', 0, 1216, [(1, 622, 260, '启用蓝牙'), (2, 648, 672, '添加翻页器，进入扫描'), (3, 644, 824, '先点学习，再按翻页器上的目标键')]),
}

CSS = r'''
:root{--ink:#171a1f;--muted:#65707e;--line:#dce1e8;--blue:#225bde;--light:#f7f9fc;--header:76px}*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:96px}body{margin:0;background:#fff;color:var(--ink);font:16px/1.8 -apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif}a{color:inherit;text-decoration:none}button,input{font:inherit}button,a,input{touch-action:manipulation}button{cursor:pointer}a:hover{color:var(--blue)}:focus-visible{outline:3px solid var(--blue);outline-offset:4px}.wrap{width:min(1320px,calc(100% - 64px));margin:auto}.skip{position:fixed;top:-100px;background:#fff;padding:12px;z-index:99}.skip:focus{top:8px}.header{height:var(--header);background:#ffffffef;border-bottom:1px solid var(--line);position:sticky;top:0;z-index:10;backdrop-filter:blur(12px)}.header-inner{display:flex;align-items:center;justify-content:space-between;height:100%;gap:20px}.brand{font-size:27px;letter-spacing:-1.3px;font-weight:750}.brand span{color:var(--blue)}.header-nav{display:flex;align-items:center;gap:26px;font-size:14px}.pill{display:inline-flex;padding:9px 18px;align-items:center;gap:8px;line-height:1.5;border:1px solid var(--line);border-radius:999px;background:#fff}.pill.primary{background:var(--ink);border-color:var(--ink);color:white}.pill:hover{border-color:var(--blue)}.hero{display:grid;grid-template-columns:1.1fr .9fr;gap:72px;padding:66px 0 45px;align-items:center}.eyebrow{letter-spacing:1.8px;font-size:12px;font-weight:650;color:var(--blue);margin:0 0 18px}.hero h1{font-size:clamp(36px,4.5vw,61px);line-height:1.27;letter-spacing:-1.6px;margin:0 0 21px}.hero-desc{color:var(--muted);font-size:18px;max-width:550px}.hero-actions{display:flex;gap:12px;flex-wrap:wrap;margin:25px 0}.meta{color:var(--muted);font-size:13px}.hero-art{position:relative;min-height:365px;display:flex;justify-content:center;gap:18px;align-items:center}.hero-art img{width:29%;max-width:139px;border:1px solid #b7bec8;border-radius:8px;box-shadow:0 12px 25px #0d183610}.hero-art img:first-child{transform:rotate(-7deg) translateY(24px)}.hero-art img:last-child{transform:rotate(7deg) translateY(24px)}.search-box{padding:23px;border:1px solid var(--line);border-radius:18px;position:relative;margin:0 0 30px}.search-top{display:flex;gap:18px;align-items:center}.search-top label{font-size:16px;font-weight:650;white-space:nowrap}.input-wrap{position:relative;flex:1}.input-wrap input{width:100%;height:52px;padding:0 45px 0 16px;border:1px solid #bfc8d5;border-radius:10px;background:#fff;outline-offset:3px}.input-wrap button{position:absolute;right:10px;top:7px;border:0;background:#fff;padding:3px 8px;color:var(--muted);font-size:22px}.search-note{font-size:13px;color:var(--muted);margin:10px 0 0}.search-results{background:#fff;max-height:480px;overflow:auto;border-top:1px solid var(--line);padding-top:12px;margin-top:15px}.search-result{display:block;padding:10px 12px;border-radius:8px}.search-result:hover{background:var(--light)}.search-result small{display:block;color:var(--muted);font-size:12px}.quick-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:0 0 49px}.quick-link{border:1px solid var(--line);border-radius:14px;padding:16px 18px;font-size:15px}.quick-link b{display:block}.quick-link span{display:block;color:var(--muted);font-size:12px;margin-top:4px}.layout{display:grid;grid-template-columns:265px minmax(0,1fr);gap:65px;align-items:start}.toc{position:sticky;top:101px;max-height:calc(100vh - 125px);overflow:auto;scrollbar-width:thin;padding:0 18px 15px 0}.toc h2{font-size:17px;margin:0 0 14px}.toc a{display:flex;gap:12px;font-size:13px;line-height:1.6;padding:8px 10px;border-radius:7px;margin:2px 0}.toc a span{font:11px/1.9 ui-monospace,monospace;color:var(--muted)}.toc a.active{color:var(--blue);background:#f0f5ff;font-weight:650}.toc .sub{margin-top:21px;border-top:1px solid var(--line);padding-top:17px;font-size:12px;color:var(--muted)}.toc-toggle{display:none}.chapter{padding:0 0 48px;margin-bottom:48px;border-bottom:1px solid var(--line);scroll-margin-top:98px}.chapter-num{font-size:12px;letter-spacing:1.2px;color:var(--blue);font-weight:650;margin-bottom:8px}.chapter h2{font-size:30px;line-height:1.45;letter-spacing:-.7px;margin:0 0 15px}.intro{font-size:17px;color:var(--muted);margin-bottom:25px}.gallery{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));align-items:start;gap:16px;margin:25px 0 36px}.gallery:has(figure:only-child){grid-template-columns:minmax(230px,340px)}.gallery:has(figure:nth-child(2):last-child){grid-template-columns:repeat(2,minmax(0,1fr));max-width:650px}.gallery figure{margin:0;min-width:0}.screen-button{display:block;border:1px solid #cfd5df;border-radius:12px;padding:9px;background:#fafbfd;width:100%;overflow:hidden}.screen-button svg{display:block;width:100%;height:auto;background:white}.screen-button:hover{border-color:var(--blue)}figcaption{font-size:12px;color:var(--muted);line-height:1.7;margin:9px 2px 0}.legend{list-style:none;margin:9px 0 0;padding:0;font-size:12px;color:#354153;line-height:1.7}.legend li{display:flex;align-items:start;gap:7px;margin:6px 0}.legend span{border-radius:50%;min-width:18px;height:18px;display:inline-flex;align-items:center;justify-content:center;color:#fff;background:var(--blue);font:11px/1 ui-monospace,monospace;margin-top:2px}.task:target{border-left:3px solid var(--blue);padding-left:18px}.task{scroll-margin-top:96px;margin:0 0 33px}.task h3{font-size:20px;line-height:1.55;margin:0 0 10px;letter-spacing:-.2px}.task-number{font-family:ui-monospace,monospace;font-size:13px;color:var(--blue);margin-right:11px}.path{border-left:3px solid var(--blue);background:#f7f9ff;color:#344560;padding:9px 14px;border-radius:0 7px 7px 0;font-size:13px;margin-bottom:15px;overflow-wrap:anywhere}.path b{display:inline-block;margin-right:11px;font-weight:600;color:var(--blue)}.steps{list-style:none;counter-reset:step;padding:0;margin:0}.steps li{counter-increment:step;position:relative;padding-left:36px;margin:13px 0}.steps li:before{content:counter(step);position:absolute;left:0;top:2px;width:23px;height:23px;background:#f1f4f8;border:1px solid #d5dde8;border-radius:50%;text-align:center;font:12px/21px ui-monospace,monospace;color:#4c5c70}.result,.tip{font-size:13px;line-height:1.8;padding:10px 14px;margin:12px 0;border:1px solid var(--line);border-radius:8px}.result strong,.tip strong{margin-right:8px;color:#243349}.tip{background:#fafbfd;color:var(--muted)}.task-image-link{font-size:13px;color:var(--blue);display:inline-block;margin-top:7px}.table-wrap{overflow:auto;margin:22px 0 30px}table{width:100%;border-collapse:collapse;font-size:14px}th,td{padding:13px 14px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top;min-width:90px}th{background:#f7f9fc;font-weight:650;border-top:1px solid var(--line)}.table-title{font-size:18px;font-weight:650;margin:20px 0 8px}.scope{font-size:12px;color:var(--muted);max-width:750px;margin:36px 0}.footer{border-top:1px solid var(--line);padding:30px 0;margin-top:35px;font-size:13px;color:var(--muted)}.footer .wrap{display:flex;justify-content:space-between;gap:16px}.back-top{position:fixed;bottom:88px;right:24px;padding:9px 15px;background:#fff;border:1px solid var(--line);border-radius:999px;font-size:12px;box-shadow:0 4px 15px #1426440b}.lightbox{border:1px solid var(--line);border-radius:16px;max-width:min(94vw,1050px);max-height:94vh;padding:24px;background:#fff}.lightbox::backdrop{background:#18233680;backdrop-filter:blur(5px)}.lightbox-head{display:flex;justify-content:space-between;align-items:center;gap:18px;margin-bottom:14px}.lightbox-head h2{font-size:18px;margin:0}.lightbox-close{background:#fff;border:1px solid var(--line);border-radius:999px;padding:6px 13px}.lightbox-body{display:flex;gap:28px;align-items:flex-start}.lightbox svg{display:block;width:min(53vw,470px);height:auto;max-height:74vh}.lightbox .legend{font-size:15px;max-width:315px}.lightbox .legend span{min-width:23px;height:23px;font-size:12px}.lightbox small{display:block;margin-top:18px;color:var(--muted)}[hidden]{display:none!important}@media(max-width:1000px){.layout{grid-template-columns:220px minmax(0,1fr);gap:30px}.hero{gap:30px}.gallery{grid-template-columns:repeat(2,minmax(0,1fr))}.quick-grid{grid-template-columns:repeat(2,1fr)}}@media(max-width:760px){:root{--header:65px}.wrap{width:calc(100% - 36px)}.brand{font-size:24px}.header-nav{gap:15px}.header-nav .official{display:none}.header-nav .pill{font-size:12px;padding:8px 12px}.hero{display:block;padding:38px 0 20px}.hero h1{font-size:37px}.hero-desc{font-size:16px}.hero-art{min-height:240px;margin:23px 0 20px}.hero-art img{width:27%;max-width:105px}.search-top{display:block}.search-top label{display:block;margin-bottom:9px}.search-box{padding:17px}.input-wrap input{font-size:14px}.quick-grid{gap:9px;margin-bottom:25px}.quick-link{padding:13px;font-size:14px}.layout{display:block}.toc-toggle{display:block;width:100%;padding:12px 17px;text-align:left;background:#fff;border:1px solid var(--line);border-radius:10px;margin:0 0 15px;font-size:14px}.toc{position:static;max-height:none;display:none;padding:15px 0 23px;columns:2}.toc.open{display:block}.toc h2,.toc .sub{column-span:all}.toc a{break-inside:avoid;font-size:12px;padding-left:0}.chapter{margin-top:25px;margin-bottom:35px}.chapter h2{font-size:25px}.intro{font-size:16px}.task h3{font-size:18px}.steps li{font-size:15px;padding-left:30px}.gallery{gap:12px;grid-template-columns:repeat(2,minmax(0,1fr))}.gallery:has(figure:only-child){grid-template-columns:minmax(200px,320px)}.screen-button{padding:5px;border-radius:8px}.legend{font-size:11px}.path{font-size:12px}.footer .wrap{display:block}.lightbox-body{display:block}.lightbox svg{width:100%;max-height:60vh}.lightbox{padding:15px}.lightbox .legend{font-size:13px;max-width:none}.back-top{right:12px;bottom:73px}.meta{font-size:12px}th,td{font-size:13px;padding:10px}.scope{font-size:11px}}@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}@media print{.header,.toc,.search-box,.quick-grid,.hero-art,.back-top,.toc-toggle,.footer{display:none}.wrap{width:100%}.layout{display:block}.hero{padding:0;display:block}.chapter{break-before:page}.task{break-inside:avoid}.gallery{grid-template-columns:repeat(3,1fr)}.screen-button{border:0}.hero-actions{display:none}}
'''

JS = r'''
// 中文：搜索正文但不隐藏章节；点击结果定位到真实操作。
// English: Search all instructions and navigate to tasks without removing chapters.
document.querySelector('#toc-toggle').addEventListener('click',e=>{const open=document.querySelector('.toc').classList.toggle('open');e.currentTarget.setAttribute('aria-expanded',String(open));});
const dialog=document.querySelector('#image-dialog'),body=document.querySelector('#image-dialog-body');let openedFrom;
function openFigure(figure){if(!figure)return;openedFrom=document.activeElement;body.replaceChildren(figure.querySelector('svg').cloneNode(true));const legend=figure.querySelector('.legend');if(legend)body.append(legend.cloneNode(true));document.querySelector('#image-dialog-title').textContent=figure.querySelector('figcaption').textContent;dialog.showModal();}
document.querySelectorAll('.screen-button').forEach(b=>b.addEventListener('click',()=>openFigure(b.closest('figure'))));
document.querySelectorAll('[data-figure]').forEach(b=>b.addEventListener('click',()=>openFigure(document.querySelector(`#figure-${b.dataset.figure}`))));
document.querySelector('#close-dialog').addEventListener('click',()=>dialog.close());dialog.addEventListener('close',()=>openedFrom?.focus());dialog.addEventListener('click',e=>{if(e.target===dialog)dialog.close();});
const tocLinks=[...document.querySelectorAll('.toc a[href^="#"]')];
const observer=new IntersectionObserver(entries=>{for(const e of entries){if(e.isIntersecting){tocLinks.forEach(a=>{const active=a.hash==='#'+e.target.id;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','location');else a.removeAttribute('aria-current');});}}},{rootMargin:'-95px 0px -65% 0px'});document.querySelectorAll('.chapter').forEach(c=>observer.observe(c));
'''


def legend_html(marks):
    return '<ol class="legend">' + ''.join(f'<li><span>{n}</span>{escape(t)}</li>' for n, _, _, t in marks) + '</ol>' if marks else ''


def diagram_html(key, assets):
    title, top, height, marks = DIAGRAMS[key]
    if not (assets / (key + '.png')).is_file():
        raise FileNotFoundError(key)
    svg = f'<svg viewBox="0 {top} 684 {height}" role="img" aria-label="{escape(title)}" xmlns="http://www.w3.org/2000/svg"><image href="assets/{key}.png" x="0" y="0" width="684" height="1216"/>'
    for n, x, y, _ in marks:
        x = min(656, max(27, x))
        svg += f'<circle cx="{x}" cy="{y}" r="24" fill="#225bde" stroke="#fff" stroke-width="4"/><text x="{x}" y="{y + 8}" text-anchor="middle" fill="#fff" font-size="25" font-family="Arial,sans-serif" font-weight="bold">{n}</text>'
    svg += '</svg>'
    return f'<figure id="figure-{key}"><button class="screen-button" aria-label="放大图解：{escape(title)}">{svg}</button><figcaption>{escape(title)} · 点图放大</figcaption>{legend_html(marks)}</figure>'


def validate(content, assets):
    ids = []
    for c in content['chapters']:
        ids.append(c['id'])
        for t in c['tasks']:
            ids.append(t['id'])
            if not t['steps']: raise ValueError('No steps: ' + t['id'])
        for k in c.get('images', []):
            if k not in DIAGRAMS or not (assets / (k + '.png')).is_file(): raise ValueError('Missing diagram: ' + k)
    if len(ids) != len(set(ids)): raise ValueError('Duplicate chapter / task anchor')


def write_html(content, assets, output, website=False):
    output.mkdir(parents=True, exist_ok=True)
    if assets.resolve() != (output / 'assets').resolve():
        shutil.copytree(assets, output / 'assets', dirs_exist_ok=True)
    chapters = content['chapters']
    total = sum(len(c['tasks']) for c in chapters)
    nav = ''.join(f'<a href="#{c["id"]}"><span>{i:02d}</span>{escape(c["title"])}</a>' for i, c in enumerate(chapters, 1))
    sections = []
    index = []
    seen_figures = set()
    all_chapter_figures = {k for c in chapters for k in c.get('images', [])}
    for i, c in enumerate(chapters, 1):
        blocks = []
        for k in c.get('images', []):
            if k not in seen_figures:
                blocks.append(diagram_html(k, assets)); seen_figures.add(k)
        gallery = f'<div class="gallery">{"".join(blocks)}</div>' if blocks else ''
        task_blocks = []
        for j, t in enumerate(c['tasks'], 1):
            key = t.get('image')
            if key and key not in all_chapter_figures and key not in seen_figures:
                task_blocks.append(f'<div class="gallery">{diagram_html(key, assets)}</div>');seen_figures.add(key)
            steps = ''.join(f'<li>{escape(step)}</li>' for step in t['steps'])
            after = ''
            if t.get('result'): after += f'<p class="result"><strong>完成之后</strong>{escape(t["result"])}</p>'
            if t.get('tip'): after += f'<p class="tip"><strong>操作提醒</strong>{escape(t["tip"])}</p>'
            if key: after += f'<button class="pill task-image-link" data-figure="{key}">查看这一步的按钮图解 ↗</button>'
            task_blocks.append(f'<article class="task" id="{t["id"]}"><h3 tabindex="-1"><span class="task-number">{i:02d}.{j:02d}</span>{escape(t["title"])}</h3><div class="path"><b>操作路径</b>{escape(t["path"])}</div><ol class="steps">{steps}</ol>{after}</article>')
            index.append({'id': t['id'], 'chapter': c['title'], 'title': t['title'], 'path': t['path'],
                          'steps': t['steps'], 'result': t.get('result', ''), 'tip': t.get('tip', '')})
        tables = []
        for tab in c.get('tables', []):
            head = ''.join(f'<th scope="col">{escape(x)}</th>' for x in tab['headers'])
            rows = ''.join('<tr>' + ''.join(f'<td>{escape(x)}</td>' for x in row) + '</tr>' for row in tab['rows'])
            tables.append(f'<p class="table-title">{escape(tab["title"])}</p><div class="table-wrap"><table><thead><tr>{head}</tr></thead><tbody>{rows}</tbody></table></div>')
        sections.append(f'<section class="chapter" id="{c["id"]}"><div class="chapter-num">第 {i:02d} 章 / {len(c["tasks"])} 项操作</div><h2>{escape(c["title"])}</h2><p class="intro">{escape(c["intro"])}</p>{gallery}{"".join(task_blocks + tables)}</section>')
    quick = [('recent-sort','按最近排列书架','管理 → 按最近'),('ime-case','切换英文大小写','全键盘 → En → a / A'),('custom-hold','自定义中键长按','阅读设置 → 按键控制'),('restore-config','恢复配置与阅读进度','设置 → 保存与恢复')]
    # Resolve labels to stable manuscript IDs so editorial ID changes fail safely.
    task_by_title = {t['title']: t['id'] for c in chapters for t in c['tasks']}
    fallback = {'recent-sort':'按照最近阅读排列书架','ime-case':'切换英文大小写','restore-config':'刷机后恢复设置与阅读进度'}
    existing = {t['id'] for c in chapters for t in c['tasks']}
    quick_html = ''
    for key, title, sub in quick:
        if key not in existing:
            key = task_by_title.get(fallback.get(key, title), next(c['id'] for c in chapters if c['id'] == ('shelf' if '最近' in title else 'keyboard' if '大小写' in title else 'backup')))
        quick_html += f'<a class="quick-link" href="#{key}"><b>{escape(title)} ↗</b><span>{escape(sub)}</span></a>'
    html = f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#ffffff"><meta name="description" content="kiikoread 超详细系统图文说明书，覆盖书架排序、输入法、传书、阅读、锁屏、升级和恢复的逐步操作。"><title>系统图文说明书 · kiikoread</title><link rel="stylesheet" href="manual.css"><script src="manual.js" defer></script></head><body id="top"><a class="skip" href="#manual-body">跳到说明书正文</a><header class="header"><div class="wrap header-inner"><a class="brand" href="#top">kiikoread<span>.</span></a><nav class="header-nav" aria-label="主导航"><a href="#basics">阅读说明书</a><a class="official" href="https://kiikoread.com/">官网 ↗</a><a class="pill primary" href="output/pdf/kiikoread-system-manual.pdf" download>下载 PDF ↓</a></nav></div></header><main class="wrap"><section class="hero"><div><p class="eyebrow">KIIKOREAD / USER MANUAL</p><h1>每个操作，<br>都能找到答案。</h1><p class="hero-desc">从第一次开机，到书架排序、大小写切换与配置恢复。按界面里的真实按钮，一步一步说明。</p><div class="hero-actions"><a class="pill primary" href="#basics">从第一章开始 →</a><a class="pill" href="#manual-search">直接找一个操作</a></div><p class="meta">{len(chapters)} 章 · {total} 项操作 · {len(seen_figures)} 幅界面图解<br>对应固件 {escape(content["version"])} · 编写于 {content["date"]}</p></div><div class="hero-art" aria-label="书架、阅读与输入法界面"><img src="assets/shelf-list.png" alt="列表书架"><img src="assets/reader.png" alt="阅读界面"><img src="assets/ime-upper.png" alt="全键盘输入法"></div></section><section class="search-box" aria-label="查找操作"><div class="search-top"><label for="manual-search">你想怎么操作？</label><div class="input-wrap"><input id="manual-search" type="search" placeholder="搜索：按最近、大小写、WiFi 密码、恢复……" autocomplete="off" aria-controls="search-results"><button id="clear-search" aria-label="清空搜索">×</button></div></div><p class="search-note" id="search-status" role="status" aria-live="polite">支持查找按钮、功能和操作步骤。</p><div class="search-results" id="search-results" hidden></div></section><div class="quick-grid">{quick_html}</div><button class="toc-toggle" id="toc-toggle" aria-controls="manual-toc" aria-expanded="false">展开 / 收起全部 21 章目录 ↓</button><div class="layout"><nav class="toc" id="manual-toc" aria-label="说明书目录"><h2>全部目录</h2>{nav}<p class="sub">按按钮名称查找，或在正文点图放大。<br>PDF 提供可点击目录与书签。</p></nav><div id="manual-body">{''.join(sections)}<p class="scope">{escape(content['scope'])}</p></div></div></main><footer class="footer"><div class="wrap"><span>kiikoread · 系统图文说明书 · {escape(content['version'])}</span><a href="https://kiikoread.com/">返回官网 ↗</a></div></footer><a class="back-top" href="#top">返回顶部 ↑</a><dialog class="lightbox" id="image-dialog"><div class="lightbox-head"><h2 id="image-dialog-title">界面图解</h2><button class="lightbox-close" id="close-dialog">关闭 ×</button></div><div class="lightbox-body" id="image-dialog-body"></div><small>蓝色编号是说明书标注；设备界面没有这些蓝色标记。按 Esc 也可关闭。</small></dialog></body></html>'''
    # 中文：预览与官网保持同一正文；官网沿用既有 PDF 下载地址。
    # English: Share content across preview and production; preserve the existing PDF URL.
    prefix = '../assets/' if website else './'
    helper_tags = (f'<link rel="stylesheet" href="{prefix}manual-helper.css">'
                   f'<script src="{prefix}manual-search.js" defer></script>'
                   f'<script src="{prefix}manual-helper.js" data-index="manual-index.json" data-manual="./" defer></script>')
    html = html.replace('</head>', helper_tags + '</head>')
    html = html.replace('支持查找按钮、功能和操作步骤。', '支持关键词、模糊搜索，或直接输入一个问题。')
    if website:
        html = html.replace('output/pdf/kiikoread-system-manual.pdf', 'kiikoread-manual.pdf')
        html = html.replace('<a class="brand" href="#top">', '<a class="brand" href="../index.html">')
    else:
        for name in ('manual-search.js', 'manual-helper.js', 'manual-helper.css'):
            shutil.copy2(ROOT / 'flash/assets' / name, output / name)
    (output / 'index.html').write_text(html, encoding='utf-8')
    (output / 'manual.css').write_text(CSS, encoding='utf-8')
    (output / 'manual.js').write_text(JS, encoding='utf-8')
    (output / 'manual-index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2) + '\n')
    return len(seen_figures)


def write_pdf(content, assets, output, font_path):
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    from reportlab.platypus import BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle, PageBreak, Flowable, KeepTogether
    from reportlab.platypus.tableofcontents import TableOfContents
    from reportlab.lib.utils import ImageReader
    font = TTFont('DetailedManual', str(font_path))
    needed = json.dumps(content, ensure_ascii=False) + ''.join(x[0] + ''.join(m[3] for m in x[3]) for x in DIAGRAMS.values()) + '操作路径完成之后操作提醒界面图解使用目录第章系统图文说明书'
    missing = sorted({c for c in needed if ord(c) > 127 and ord(c) not in font.face.charToGlyph})
    if missing: raise ValueError('Font lacks: ' + ''.join(missing))
    pdfmetrics.registerFont(font)
    ink = colors.HexColor('#171a1f'); muted = colors.HexColor('#65707e'); blue = colors.HexColor('#225bde'); line = colors.HexColor('#dce1e8')
    width, height = A4; margin = 47; available = width - 2 * margin
    styles = {
        'body': ParagraphStyle('Body', fontName='DetailedManual', fontSize=10.4, leading=18.2, wordWrap='CJK', textColor=ink, spaceAfter=6, allowWidows=0, allowOrphans=0),
        'title': ParagraphStyle('Chapter', fontName='DetailedManual', fontSize=24, leading=33, wordWrap='CJK', textColor=ink, spaceAfter=13, keepWithNext=True),
        'intro': ParagraphStyle('Intro', fontName='DetailedManual', fontSize=10.6, leading=18.5, wordWrap='CJK', textColor=muted, spaceAfter=17, keepWithNext=True),
        'head': ParagraphStyle('Task', fontName='DetailedManual', fontSize=13.4, leading=22, wordWrap='CJK', textColor=ink, spaceBefore=14, spaceAfter=7, keepWithNext=True),
        'path': ParagraphStyle('Path', fontName='DetailedManual', fontSize=8.8, leading=15, wordWrap='CJK', textColor=blue, spaceAfter=10, keepWithNext=True, borderPadding=6, backColor=colors.HexColor('#f7f9ff')),
        'tip': ParagraphStyle('Tip', fontName='DetailedManual', fontSize=8.6, leading=15, wordWrap='CJK', textColor=muted, spaceBefore=3, spaceAfter=8, borderColor=line, borderWidth=.45, borderPadding=8),
        'result': ParagraphStyle('Result', fontName='DetailedManual', fontSize=8.8, leading=15, wordWrap='CJK', textColor=ink, spaceBefore=3, spaceAfter=6),
        'caption': ParagraphStyle('Caption', fontName='DetailedManual', fontSize=8.2, leading=14, wordWrap='CJK', textColor=muted, spaceBefore=6, spaceAfter=4),
        'table': ParagraphStyle('Table', fontName='DetailedManual', fontSize=8.8, leading=15, wordWrap='CJK', textColor=ink),
        'label': ParagraphStyle('Label', fontName='DetailedManual', fontSize=9, leading=16, textColor=blue, spaceAfter=11),
    }
    def p(text, style='body'):
        return Paragraph(escape(text), styles[style])
    def footer(canvas, doc):
        canvas.saveState();canvas.setStrokeColor(line);canvas.setLineWidth(.5)
        if doc.page > 1:
            canvas.setFont('Helvetica-Bold', 13);canvas.setFillColor(ink);canvas.drawString(margin, height - 34, 'kiikoread.')
            canvas.setFillColor(muted);canvas.setFont('DetailedManual', 8);canvas.drawRightString(width - margin, height - 32, '系统图文说明书 / ' + content['version'])
            canvas.line(margin, height - 44, width - margin, height - 44)
        canvas.line(margin, 39, width - margin, 39);canvas.setFillColor(muted);canvas.setFont('DetailedManual', 8)
        canvas.drawString(margin, 24, 'kiikoread · ' + content['date']);canvas.drawRightString(width - margin, 24, str(doc.page));canvas.restoreState()
    class ManualDoc(BaseDocTemplate):
        def afterFlowable(self, flowable):
            if isinstance(flowable, Paragraph) and getattr(flowable, 'bookmark', None):
                key = flowable.bookmark;label = flowable.getPlainText()
                self.canv.bookmarkPage(key)
                self.canv.addOutlineEntry(label, key, level=flowable.outline_level, closed=True)
                if flowable.outline_level == 0:
                    self.notify('TOCEntry', (0, label, self.page, key))
    class Diagram(Flowable):
        def __init__(self, key, target_width):
            Flowable.__init__(self);self.key=key;self.width=target_width
            self.title,self.top,self.crop_height,self.marks=DIAGRAMS[key]
            self.height=target_width*self.crop_height/684;self.hAlign='CENTER'
        def draw(self):
            c=self.canv;c.saveState();scale=self.width/684
            clip=c.beginPath();clip.rect(0,0,self.width,self.height);c.clipPath(clip,stroke=0)
            c.drawImage(ImageReader(str(assets/(self.key+'.png'))),0,-(1216-self.top-self.crop_height)*scale,width=self.width,height=1216*scale)
            for n,x,y,_ in self.marks:
                x=min(656,max(27,x))*scale;y=(self.top+self.crop_height-y)*scale
                c.setFillColor(blue);c.setStrokeColor(colors.white);c.setLineWidth(1.5);c.circle(x,y,24*scale,stroke=1,fill=1)
                c.setFillColor(colors.white);c.setFont('Helvetica-Bold',25*scale);c.drawCentredString(x,y-8*scale,str(n))
            c.restoreState();c.setStrokeColor(line);c.setLineWidth(.5);c.roundRect(0,0,self.width,self.height,3,stroke=1,fill=0)
    def gallery(keys):
        result=[]
        for offset in range(0,len(keys),2):
            cells=[]
            for key in keys[offset:offset+2]:
                title,_,_,marks=DIAGRAMS[key]
                # The cropped panel is larger than a complete display when details matter.
                image_width=215 if DIAGRAMS[key][2]<1100 else 166
                cell=[Diagram(key,image_width),p(title,'caption')]
                cell += [p(f'{n}  {label}','caption') for n,_,_,label in marks]
                cells.append(cell)
            table=Table([cells],colWidths=[available/2]*len(cells),hAlign='LEFT')
            table.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),0),('RIGHTPADDING',(0,0),(-1,-1),12),('TOPPADDING',(0,0),(-1,-1),0),('BOTTOMPADDING',(0,0),(-1,-1),14)]))
            result.append(table)
        return result
    flow=[]
    flow += [Spacer(1,30),p('KIIKOREAD / USER MANUAL','label')]
    cover_style=ParagraphStyle('Cover',parent=styles['title'],fontSize=36,leading=50,spaceAfter=22)
    flow += [Paragraph('系统图文<br/>说明书',cover_style),p(content['subtitle'],'intro'),p(f'{len(content["chapters"])} 章 · {sum(len(c["tasks"]) for c in content["chapters"])} 项操作','head')]
    cover = Table([[Diagram('shelf-list',126),Diagram('reader',126),Diagram('ime-upper',180)]],colWidths=[154,154,192])
    cover.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),0)]))
    flow += [Spacer(1,13),cover,Spacer(1,18),p('对应固件 '+content['version']+' · 适用墨水屏 '+content['model'],'caption'),p(content['scope'],'caption'),PageBreak(),p('目录','title'),p('点击目录标题跳到相应章节。PDF 阅读器的书签面板也可直接定位到单项操作。','intro')]
    toc=TableOfContents();toc.levelStyles=[ParagraphStyle('TOC',fontName='DetailedManual',fontSize=9.8,leading=18,wordWrap='CJK',textColor=ink,leftIndent=0,firstLineIndent=0,spaceBefore=0)]
    flow += [toc,PageBreak()]
    illustrated=set()
    for i,c in enumerate(content['chapters'],1):
        if i>1:flow.append(PageBreak())
        flow.append(p(f'第 {i:02d} 章 / {len(c["tasks"])} 项操作','label'))
        heading=p(c['title'],'title');heading.bookmark=c['id'];heading.outline_level=0;flow.append(heading);flow.append(p(c['intro'],'intro'))
        keys=[key for key in c.get('images',[]) if key not in illustrated];illustrated.update(keys);flow+=gallery(keys)
        for j,t in enumerate(c['tasks'],1):
            heading=p(f'{i:02d}.{j:02d}  {t["title"]}','head');heading.bookmark=t['id'];heading.outline_level=1
            flow += [heading,p('操作路径  '+t['path'],'path')]
            for n,step in enumerate(t['steps'],1):
                ps=ParagraphStyle('Step',parent=styles['body'],leftIndent=23,firstLineIndent=-20)
                if n==len(t['steps']) and (t.get('result') or t.get('tip')):ps.keepWithNext=True
                flow.append(Paragraph(escape(f'{n}.  {step}'),ps))
            if t.get('result'):
                result=p('完成之后  '+t['result'],'result')
                if t.get('tip'):result.keepWithNext=True
                flow.append(result)
            if t.get('tip'):flow.append(p('操作提醒  '+t['tip'],'tip'))
            key=t.get('image')
            if key and key not in illustrated:flow+=gallery([key]);illustrated.add(key)
        for tab in c.get('tables',[]):
            flow.append(p(tab['title'],'head'))
            rows=[[p(s,'table') for s in row] for row in [tab['headers']]+tab['rows']]
            cols=len(tab['headers']);col_widths=([115,available-115] if cols==2 else [95,130,available-225] if cols==3 else [available/cols]*cols)
            table=Table(rows,colWidths=col_widths,repeatRows=1,hAlign='LEFT')
            table.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('BACKGROUND',(0,0),(-1,0),colors.HexColor('#f7f9fc')),('LINEABOVE',(0,0),(-1,0),.5,line),('LINEBELOW',(0,0),(-1,-1),.45,line),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),9),('BOTTOMPADDING',(0,0),(-1,-1),9)]))
            flow += [table,Spacer(1,14)]
    pdf=output/'output/pdf/kiikoread-system-manual.pdf';pdf.parent.mkdir(parents=True,exist_ok=True)
    doc=ManualDoc(str(pdf),pagesize=A4,leftMargin=margin,rightMargin=margin,topMargin=67,bottomMargin=58,title=content['title'],author='kiikoread / Kiiko',pageCompression=1)
    frame=Frame(margin,58,available,height-125,leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0)
    doc.addPageTemplates(PageTemplate(id='Manual',frames=[frame],onPage=footer))
    doc.multiBuild(flow)
    return pdf


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content',type=Path,default=ROOT/'docs/user-manual-detailed.zh-CN.json')
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--font',type=Path)
    parser.add_argument('--pdf',action='store_true')
    parser.add_argument('--website',action='store_true',help='Use website-relative helper and PDF URLs')
    args=parser.parse_args();content=json.loads(args.content.read_text(encoding='utf-8'))
    validate(content,args.assets)
    figures=write_html(content,args.assets,args.output,args.website)
    if args.pdf:
        if not args.font:parser.error('--pdf requires --font')
        print(write_pdf(content,args.assets,args.output,args.font))
    print(f'{len(content["chapters"])} chapters / {sum(len(c["tasks"]) for c in content["chapters"])} tasks / {figures} diagrams')


if __name__=='__main__':main()
