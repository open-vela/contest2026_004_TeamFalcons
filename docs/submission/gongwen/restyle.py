# -*- coding: utf-8 -*-
"""
VelaGuard 技术报告 —— 公文风格版式重构
输入/输出: F:\Documents\velaguard作品提交\VelaGuard-技术报告.docx
"""
import copy, os, sys
import docx
from lxml import etree

W_NS = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
def w(t): return '{%s}%s' % (W_NS, t)
def local(e): return etree.QName(e).localname

# ---------------------------------------------------------------- 版面常量
PAGE_W, PAGE_H = 11905, 16840                 # A4
MAR_T, MAR_B, MAR_L, MAR_R = 2098, 1984, 1587, 1474   # 上3.7 下3.5 左2.8 右2.6 cm
TEXT_W = PAGE_W - MAR_L - MAR_R               # 8844 twips
OLD_TEXT_W = 8305
HDR_D, FTR_D = 851, 992                       # 页眉1.5cm 页脚1.75cm

F_FS, F_HT, F_KT, F_ST, F_ZS, F_EN = '仿宋', '黑体', '楷体', '宋体', '华文中宋', 'Times New Roman'
SZ_2, SZ_3, SZ_4, SZ_X4, SZ_5, SZ_X5 = 44, 32, 28, 24, 21, 18
L24, L28 = 480, 560                           # 固定行距 24pt / 28pt
LINK = '24588D'

# ---------------------------------------------------------------- XML 工具
PPR_ORDER = ['pStyle','keepNext','keepLines','pageBreakBefore','framePr','widowControl',
    'numPr','suppressLineNumbers','pBdr','shd','tabs','suppressAutoHyphens','kinsoku',
    'wordWrap','overflowPunct','topLinePunct','autoSpaceDE','autoSpaceDN','bidi',
    'adjustRightInd','snapToGrid','spacing','ind','contextualSpacing','mirrorIndents',
    'suppressOverlap','jc','textDirection','textAlignment','textboxTightWrap','outlineLvl',
    'divId','cnfStyle','rPr','sectPr','pPrChange']
RPR_ORDER = ['rStyle','rFonts','b','bCs','i','iCs','caps','smallCaps','strike','dstrike',
    'outline','shadow','emboss','imprint','noProof','snapToGrid','vanish','webHidden',
    'color','spacing','w','kern','position','sz','szCs','highlight','u','effect','bdr',
    'shd','fitText','vertAlign','rtl','cs','em','lang','eastAsianLayout','specVanish','oMath']

def mk(tag, attrs=None, kids=None, text=None):
    e = etree.Element(w(tag))
    for k, v in (attrs or {}).items():
        e.set(w(k) if not k.startswith('{') and ':' not in k else (w(k) if ':' not in k else k), str(v))
    for c in (kids or []):
        e.append(c)
    if text is not None:
        e.text = text
    return e

def order(parent, seq):
    """按 OOXML schema 顺序重排子元素"""
    kids = sorted(list(parent), key=lambda c: seq.index(local(c)) if local(c) in seq else len(seq))
    for c in kids:
        parent.append(c)          # append 已有元素 = 移动
    return parent

def rpr(font=F_FS, latin=F_EN, size=SZ_4, bold=None, color=None):
    r = mk('rPr')
    r.append(mk('rFonts', {'ascii': latin, 'hAnsi': latin, 'eastAsia': font, 'cs': latin}))
    if bold is not None:
        r.append(mk('b', {'val': '1' if bold else '0'}))
        r.append(mk('bCs', {'val': '1' if bold else '0'}))
    if color:
        r.append(mk('color', {'val': color}))
    if size:
        r.append(mk('sz', {'val': size}))
        r.append(mk('szCs', {'val': size}))
    return order(r, RPR_ORDER)

def ppr(style=None, jc=None, before=None, after=None, line=None, rule=None,
        first_line=None, first_chars=None, left=None, outline=None,
        keep_next=False, tabs=None):
    p = mk('pPr')
    if style:       p.append(mk('pStyle', {'val': style}))
    if keep_next:   p.append(mk('keepNext'))
    p.append(mk('widowControl'))
    if tabs:
        p.append(mk('tabs', kids=[mk('tab', t) for t in tabs]))
    if before is not None or after is not None or line is not None:
        a = {}
        if before is not None: a['before'] = before
        if after  is not None: a['after']  = after
        if line   is not None: a['line']   = line
        if rule   is not None: a['lineRule'] = rule
        p.append(mk('spacing', a))
    if first_line is not None or first_chars is not None or left is not None:
        a = {}
        if left        is not None: a['left'] = left
        if first_line  is not None: a['firstLine'] = first_line
        if first_chars is not None: a['firstLineChars'] = first_chars
        p.append(mk('ind', a))
    if jc:      p.append(mk('jc', {'val': jc}))
    if outline is not None: p.append(mk('outlineLvl', {'val': outline}))
    return order(p, PPR_ORDER)

def style(sid, name, p=None, r=None, based='Normal', nxt=None, default=False):
    s = mk('style', {'type': 'paragraph', 'styleId': sid})
    if default: s.set(w('default'), '1')
    s.append(mk('name', {'val': name}))
    if based: s.append(mk('basedOn', {'val': based}))
    if nxt:   s.append(mk('next', {'val': nxt}))
    s.append(mk('uiPriority', {'val': '0'}))
    s.append(mk('qFormat'))
    if p is not None: s.append(order(p, PPR_ORDER))
    if r is not None: s.append(r)
    return s

# ---------------------------------------------------------------- 文档
SRC = sys.argv[1] if len(sys.argv) > 1 else r'F:\Documents\velaguard作品提交\VelaGuard-技术报告.docx'
DST = sys.argv[2] if len(sys.argv) > 2 else SRC
doc = docx.Document(SRC)
body = doc.element.body
log = []

# ================================================================ 1. 样式表
styles_el = doc.styles.element
for c in list(styles_el):
    styles_el.remove(c)

# docDefaults
dd = mk('docDefaults')
dd.append(mk('rPrDefault', kids=[rpr(F_FS, F_EN, SZ_4)]))
dd.append(mk('pPrDefault'))
styles_el.append(dd)

# 右对齐制表位比版心右边界内收 60twips：Word 的目录页码尾部会多占约 3pt
DOT = {'val': 'right', 'leader': 'dot', 'pos': str(TEXT_W - 60)}

STYLES = [
    # 正文：首行缩进用绝对值 560twips(2×14pt)，不用 firstLineChars——
    # 否则 16pt 标题会按自身字号算成 640twips，与正文首行对不齐
    style('Normal', 'Normal',
          ppr(jc='both', before=0, after=0, line=L24, rule='exact',
              first_line=560),
          rpr(F_FS, F_EN, SZ_4), based=None, nxt='Normal', default=True),

    # 文档大标题：华文中宋 二号 居中
    style('Title', 'Title',
          ppr(jc='center', before=0, after=80, line=320, rule='auto',
              first_line=0, first_chars=0, keep_next=True),
          rpr(F_ZS, F_EN, SZ_2)),

    # 副标题（大赛名称）：楷体 三号 居中
    style('ReportSubtitle', 'Report Subtitle',
          ppr(jc='center', before=0, after=140, line=280, rule='auto',
              first_line=0, first_chars=0),
          rpr(F_KT, F_EN, SZ_3)),

    # 标题层级统一左空二字（560twips，与正文首行对齐）——公文体例
    # 一级标题：黑体 三号
    style('Heading1', 'heading 1',
          ppr(jc='left', before=300, after=140, line=L28, rule='exact',
              first_line=560, left=0, keep_next=True, outline=0),
          rpr(F_HT, F_EN, SZ_3), nxt='Normal'),

    # 二级标题：楷体 三号
    style('Heading2', 'heading 2',
          ppr(jc='left', before=240, after=120, line=L28, rule='exact',
              first_line=560, left=0, keep_next=True, outline=1),
          rpr(F_KT, F_EN, SZ_3), nxt='Normal'),

    # 三级标题：仿宋 三号 加粗
    style('Heading3', 'heading 3',
          ppr(jc='left', before=180, after=100, line=L28, rule='exact',
              first_line=560, left=0, keep_next=True, outline=2),
          rpr(F_FS, F_EN, SZ_3, bold=True), nxt='Normal'),

    # 图表题注：楷体 小四 居中
    style('Caption', 'Caption',
          ppr(jc='center', before=80, after=80, line=300, rule='auto',
              first_line=0, first_chars=0, left=0),
          rpr(F_KT, F_EN, SZ_X4)),

    # 小字注记：仿宋 五号
    style('ReportSmall', 'Report Small',
          ppr(jc='left', before=80, after=160, line=300, rule='auto',
              first_line=560),
          rpr(F_FS, F_EN, SZ_5)),

    # 正文
    style('ReportSubtitleX', 'unused', ppr(), rpr()),

    # 参考文献索引：仿宋 小五，三栏制表位（列宽按实测最长条目定，避免相邻栏撞字）
    style('ReportReference', 'Report Reference',
          ppr(jc='left', before=0, after=60, line=300, rule='auto',
              first_line=0, first_chars=0, left=0,
              tabs=[{'val': 'left', 'pos': '2700'}, {'val': 'left', 'pos': '5900'}]),
          rpr(F_FS, F_EN, SZ_X5)),

    # 表格正文：仿宋 五号
    style('TableText', 'Table Text',
          ppr(jc='left', before=30, after=30, line=280, rule='auto',
              first_line=0, first_chars=0, left=0),
          rpr(F_FS, F_EN, SZ_5)),

    # 表头：黑体 五号 居中
    style('TableHead', 'Table Head',
          ppr(jc='center', before=30, after=30, line=280, rule='auto',
              first_line=0, first_chars=0, left=0),
          rpr(F_HT, F_EN, SZ_5)),

    # 目录
    style('TOCHeading', 'TOC Heading',
          ppr(jc='center', before=0, after=120, line=300, rule='auto',
              first_line=0, first_chars=0, left=0),
          rpr(F_ZS, F_EN, SZ_2)),

    # 目录条目行距用固定值压紧，保证 3 级目录单页排得下。
    # 行距留出的余量要够放 Word 在目录域之后补的那个空段落，
    # 否则空段落被挤到下一页，正文再 pageBreakBefore 就会多出一张空白页。
    style('TOC1', 'toc 1',
          ppr(jc='left', before=0, after=0, line=330, rule='exact',
              first_line=0, first_chars=0, left=0, tabs=[DOT]),
          rpr(F_HT, F_EN, SZ_X4)),
    style('TOC2', 'toc 2',
          ppr(jc='left', before=0, after=0, line=285, rule='exact',
              first_line=0, first_chars=0, left=360, tabs=[DOT]),
          rpr(F_KT, F_EN, SZ_X4)),
    style('TOC3', 'toc 3',
          ppr(jc='left', before=0, after=0, line=260, rule='exact',
              first_line=0, first_chars=0, left=720, tabs=[DOT]),
          rpr(F_FS, F_EN, SZ_5)),
]
# 去掉占位
STYLES = [s for s in STYLES if s.get(w('styleId')) != 'ReportSubtitleX']
for s in STYLES:
    styles_el.append(s)
log.append('样式表重建: %d 个样式' % len(STYLES))

# ================================================================ 2. 节属性
sect2 = body.find(w('sectPr'))
sect1 = copy.deepcopy(sect2)
# 清掉 sect1 的页眉页脚引用（封面/目录页不显示页码）
for tag in ('headerReference', 'footerReference'):
    for e in sect1.findall(w(tag)):
        sect1.remove(e)

def set_pgmar(sect):
    old = sect.find(w('pgMar'))
    if old is not None: sect.remove(old)
    pg = mk('pgMar', {'top': MAR_T, 'bottom': MAR_B, 'left': MAR_L, 'right': MAR_R,
                      'header': HDR_D, 'footer': FTR_D, 'gutter': 0})
    # pgSz 之后紧跟 pgMar
    idx = list(sect).index(sect.find(w('pgSz'))) + 1
    sect.insert(idx, pg)

set_pgmar(sect1); set_pgmar(sect2)

# 正文节页码从 1 开始
old = sect2.find(w('pgNumType'))
if old is not None: sect2.remove(old)
sect2.append(mk('pgNumType', {'start': '1'}))
log.append('页面: A4, 页边距 上3.7/下3.5/左2.8/右2.6cm, 版心 %d twips' % TEXT_W)

# ================================================================ 3. 正文段落重新套用
def para_text(p):
    return ''.join(t.text or '' for t in p.iter(w('t')))

def has_drawing(p):
    return p.find('.//' + w('drawing')) is not None or p.find('.//' + w('pict')) is not None

def set_pstyle(p, sid):
    pPr = p.find(w('pPr'))
    if pPr is None:
        pPr = mk('pPr'); p.insert(0, pPr)
    old = pPr.find(w('pStyle'))
    if old is not None: pPr.remove(old)
    pPr.insert(0, mk('pStyle', {'val': sid}))
    return pPr

def clear_run_fmt(run, keep_color=False, size=None):
    """清除 run 上的字体/字号直接格式，改由样式控制"""
    rPr = run.find(w('rPr'))
    if rPr is None: return
    for tag in ('rFonts', 'sz', 'szCs', 'b', 'bCs', 'i', 'iCs'):
        for e in rPr.findall(w(tag)):
            rPr.remove(e)
    if not keep_color:
        for e in rPr.findall(w('color')):
            rPr.remove(e)
    if size is not None:
        rPr.append(mk('sz', {'val': size}))
        rPr.append(mk('szCs', {'val': size}))
    if len(rPr) == 0:
        run.remove(rPr)
    else:
        order(rPr, RPR_ORDER)

body_kids = list(body.iterchildren())
n_body = n_img = n_empty = 0
for el in body_kids:
    if el.tag != w('p'):
        continue
    p  = el
    pPr = p.find(w('pPr'))
    sid = None
    if pPr is not None:
        ps = pPr.find(w('pStyle'))
        sid = ps.get(w('val')) if ps is not None else None

    if sid in ('Title', 'Heading1', 'Heading2', 'Heading3', 'Caption',
               'ReportSmall', 'ReportReference'):
        # 段落级格式交给样式；清除 direct 的 jc/spacing/ind
        if pPr is not None:
            for tag in ('jc', 'spacing', 'ind', 'keepNext'):
                for e in pPr.findall(w(tag)):
                    pPr.remove(e)
        for run in p.iter(w('r')):
            if sid == 'ReportReference':
                clear_run_fmt(run, keep_color=True, size=SZ_X5)
            else:
                clear_run_fmt(run, keep_color=True)
        continue

    # ---- 普通段落
    if has_drawing(p):
        # 图片段：居中、单倍行距（避免固定行距裁切图片）
        if pPr is None:
            pPr = mk('pPr'); p.insert(0, pPr)
        for tag in ('jc', 'spacing', 'ind'):
            for e in pPr.findall(w(tag)):
                pPr.remove(e)
        pPr.append(mk('spacing', {'before': '120', 'after': '60', 'line': '240', 'lineRule': 'auto'}))
        pPr.append(mk('ind', {'left': '0', 'firstLine': '0', 'firstLineChars': '0'}))
        pPr.append(mk('jc', {'val': 'center'}))
        order(pPr, PPR_ORDER)
        n_img += 1
        continue

    if not para_text(p).strip():
        # 空段：去掉首行缩进，压缩高度
        if pPr is None:
            pPr = mk('pPr'); p.insert(0, pPr)
        for tag in ('jc', 'spacing', 'ind'):
            for e in pPr.findall(w(tag)):
                pPr.remove(e)
        pPr.append(mk('spacing', {'before': '0', 'after': '0', 'line': '280', 'lineRule': 'auto'}))
        pPr.append(mk('ind', {'left': '0', 'firstLine': '0', 'firstLineChars': '0'}))
        order(pPr, PPR_ORDER)
        n_empty += 1
        continue

    # 普通正文段
    if pPr is None:
        pPr = mk('pPr'); p.insert(0, pPr)
    for tag in ('jc', 'spacing', 'ind'):
        for e in pPr.findall(w(tag)):
            pPr.remove(e)
    order(pPr, PPR_ORDER)
    for run in p.iter(w('r')):
        clear_run_fmt(run, keep_color=True, size=SZ_X4 if run.find(w('rPr')) is not None
                      and run.find(w('rPr')).find(w('color')) is not None else None)
    n_body += 1

log.append('正文段 %d / 图片段 %d / 空段 %d' % (n_body, n_img, n_empty))

# ================================================================ 4. 副标题（第2段）
paras = [e for e in body.iterchildren() if e.tag == w('p')]
if len(paras) > 1:
    set_pstyle(paras[1], 'ReportSubtitle')

# ================================================================ 5. 图片缩放至新版心宽
ratio = TEXT_W / float(OLD_TEXT_W)
n_scaled = 0
for ext in body.iter():
    if local(ext) in ('extent', 'ext') and ext.get('cx') and ext.get('cy'):
        ext.set('cx', str(int(round(int(ext.get('cx')) * ratio))))
        ext.set('cy', str(int(round(int(ext.get('cy')) * ratio))))
        n_scaled += 1
log.append('图片缩放 %d 处 (x%.3f)' % (n_scaled, ratio))

# ================================================================ 6. 表格
def scale_widths(ws, total):
    s = sum(ws)
    out = [max(1, int(round(x * total / float(s)))) for x in ws]
    out[-1] += total - sum(out)
    return out

BLACK = '000000'
for ti, tbl in enumerate(body.findall(w('tbl'))):
    tblPr = tbl.find(w('tblPr'))
    if tblPr is None:
        tblPr = mk('tblPr'); tbl.insert(0, tblPr)

    # 表宽
    for tag in ('tblW', 'tblBorders', 'tblLayout', 'tblCellMar', 'jc', 'tblInd'):
        for e in tblPr.findall(w(tag)):
            tblPr.remove(e)
    tblPr.append(mk('tblW', {'w': str(TEXT_W), 'type': 'dxa'}))
    tblPr.append(mk('jc', {'val': 'center'}))
    tblPr.append(mk('tblInd', {'w': '0', 'type': 'dxa'}))
    tblPr.append(mk('tblBorders', kids=[
        mk('top',     {'val': 'single', 'sz': '12', 'space': '0', 'color': BLACK}),
        mk('left',    {'val': 'single', 'sz': '4',  'space': '0', 'color': BLACK}),
        mk('bottom',  {'val': 'single', 'sz': '12', 'space': '0', 'color': BLACK}),
        mk('right',   {'val': 'single', 'sz': '4',  'space': '0', 'color': BLACK}),
        mk('insideH', {'val': 'single', 'sz': '4',  'space': '0', 'color': BLACK}),
        mk('insideV', {'val': 'single', 'sz': '4',  'space': '0', 'color': BLACK}),
    ]))
    tblPr.append(mk('tblLayout', {'type': 'fixed'}))
    tblPr.append(mk('tblCellMar', kids=[
        mk('top',    {'w': '40',  'type': 'dxa'}),
        mk('left',   {'w': '113', 'type': 'dxa'}),
        mk('bottom', {'w': '40',  'type': 'dxa'}),
        mk('right',  {'w': '113', 'type': 'dxa'}),
    ]))

    rows = tbl.findall(w('tr'))

    # 列宽按新版心等比缩放
    grid = tbl.find(w('tblGrid'))
    if grid is not None:
        cols = grid.findall(w('gridCol'))
        ws = [int(c.get(w('w'))) for c in cols]
        nw = scale_widths(ws, TEXT_W)
        for c, v in zip(cols, nw):
            c.set(w('w'), str(v))
    else:
        nw = None

    if nw is None:
        first = rows[0].findall(w('tc'))
        ws = [int(tc.find(w('tcPr')).find(w('tcW')).get(w('w'))) for tc in first]
        nw = scale_widths(ws, TEXT_W)

    # 每行单元格宽度
    for tr in rows:
        tcs = tr.findall(w('tc'))
        if len(tcs) != len(nw):
            continue
        for tc, v in zip(tcs, nw):
            tcPr = tc.find(w('tcPr'))
            if tcPr is None:
                tcPr = mk('tcPr'); tc.insert(0, tcPr)
            for e in tcPr.findall(w('tcW')):
                tcPr.remove(e)
            tcPr.insert(0, mk('tcW', {'w': str(v), 'type': 'dxa'}))

    # 列对齐：整列短文本 -> 居中
    ncols = len(nw)
    col_center = []
    for ci in range(ncols):
        mx = 0
        for tr in rows[1:]:
            tcs = tr.findall(w('tc'))
            if ci < len(tcs):
                mx = max(mx, len(''.join(t.text or '' for t in tcs[ci].iter(w('t'))).strip()))
        col_center.append(mx > 0 and mx <= 10)

    for ri, tr in enumerate(rows):
        tcs = tr.findall(w('tc'))
        for ci, tc in enumerate(tcs):
            head = (ri == 0)
            sid = 'TableHead' if head else 'TableText'
            for p in tc.findall(w('p')):
                set_pstyle(p, sid)
                pPr = p.find(w('pPr'))
                for tag in ('jc', 'spacing', 'ind'):
                    for e in pPr.findall(w(tag)):
                        pPr.remove(e)
                if not head and col_center[ci] and len(tcs) <= 4:
                    pPr.append(mk('jc', {'val': 'center'}))
                order(pPr, PPR_ORDER)
                for run in p.iter(w('r')):
                    clear_run_fmt(run, keep_color=False)
        # 表头行加粗底线
        if ri == 0:
            for tc in tcs:
                tcPr = tc.find(w('tcPr'))
                for e in tcPr.findall(w('tcBorders')):
                    tcPr.remove(e)
                tcPr.append(mk('tcBorders', kids=[
                    mk('bottom', {'val': 'single', 'sz': '8', 'space': '0', 'color': BLACK})]))
log.append('表格 %d 个已按版心宽等比缩放' % len(body.findall(w('tbl'))))

# ================================================================ 7. 目录 + 分节
title_p  = paras[0]
sub_p    = paras[1]
first_body_p = paras[2]        # “1、信息表”

# 目录标题
toc_h = mk('p')
toc_h.append(ppr(style='TOCHeading'))
r = mk('r'); r.append(mk('t', text='目　录')); toc_h.append(r)

# 目录域
toc_p = mk('p')
tp = mk('pPr')
tp.append(mk('spacing', {'before': '0', 'after': '0', 'line': '240', 'lineRule': 'auto'}))
tp.append(mk('ind', {'left': '0', 'firstLine': '0', 'firstLineChars': '0'}))
toc_p.append(order(tp, PPR_ORDER))
fb = mk('r'); fb.append(mk('fldChar', {'fldCharType': 'begin'})); toc_p.append(fb)
it = mk('r'); itx = mk('instrText', {'{http://www.w3.org/XML/1998/namespace}space': 'preserve'})
itx.text = ' TOC \\o "1-3" \\h \\z \\u '
it.append(itx); toc_p.append(it)
fs = mk('r'); fs.append(mk('fldChar', {'fldCharType': 'separate'})); toc_p.append(fs)
fr = mk('r'); fr.append(mk('t', text='【请在 Word 中按 Ctrl+A 后按 F9 更新目录】')); toc_p.append(fr)
fe = mk('r'); fe.append(mk('fldChar', {'fldCharType': 'end'})); toc_p.append(fe)

# 插入：Title, Subtitle, [TOCHeading, TOCField], rest...
sub_p.addnext(toc_p)
sub_p.addnext(toc_h)

# 单节方案：正文靠 pageBreakBefore 另起一页（分节符会多出一张空白页）
fp_pPr = first_body_p.find(w('pPr'))
if fp_pPr is None:
    fp_pPr = mk('pPr'); first_body_p.insert(0, fp_pPr)
if fp_pPr.find(w('pageBreakBefore')) is None:
    fp_pPr.append(mk('pageBreakBefore'))
    order(fp_pPr, PPR_ORDER)
log.append('已插入目录域(TOC 1-3级)；正文 pageBreakBefore 另起一页')

# ================================================================ 8. settings
st = doc.settings.element
for c in list(st):
    st.remove(c)
st.append(mk('defaultTabStop', {'val': '420'}))
st.append(mk('evenAndOddHeaders'))
st.append(mk('compat', kids=[
    mk('compatSetting', {'name': 'compatibilityMode', 'uri': 'http://schemas.microsoft.com/office/word',
                         'val': '15'})]))
log.append('settings: evenAndOddHeaders 已启用')

doc.save(DST)
print('\n'.join(log))
print('saved:', DST)
