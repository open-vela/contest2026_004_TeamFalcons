# -*- coding: utf-8 -*-
"""把主题字体引用改成显式中文字体。

Word 生成目录时会写两类主题字体引用：
  1. word/theme/theme1.xml 里 <a:ea typeface=""/> 为空 -> 东亚字回落到 minor 拉丁字体“等线”
  2. document.xml 中各目录段落的“段落标记 rPr”w:asciiTheme="minorHAnsi" 等
第 2 类决定目录前导点线的字体，所以必须一起改掉，否则点线仍是等线。
"""
import sys, zipfile, os, re

SRC = sys.argv[1]
LATIN, EA = 'Times New Roman', '仿宋'

THEME_ATTR = {
    'asciiTheme':    ('ascii',    LATIN),
    'hAnsiTheme':    ('hAnsi',    LATIN),
    'eastAsiaTheme': ('eastAsia', EA),
    'cstheme':       ('cs',       LATIN),
}

def fix_rfonts(xml):
    """把带 *Theme 的 w:rFonts 改写成显式字体"""
    n = 0
    def repl(m):
        nonlocal n
        tag = m.group(0)
        pairs = re.findall(r'([\w:]+)="([^"]*)"', tag)
        local = lambda k: k.split(':')[-1]
        if not any(local(k) in THEME_ATTR for k, _ in pairs):
            return tag
        n += 1
        kept = [(k, v) for k, v in pairs if local(k) not in THEME_ATTR]
        have = {local(k) for k, _ in kept}
        for name, face in THEME_ATTR.values():
            if name not in have:
                kept.append(('w:' + name, face))
        body = ''.join(' %s="%s"' % (k, v) for k, v in kept)
        return '<w:rFonts%s/>' % body
    out = re.sub(r'<w:rFonts[^>]*>', repl, xml)
    return out, n

tmp = SRC + '.tmp'
zin = zipfile.ZipFile(SRC, 'r')
zout = zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED)
ea_hit = fn_hit = 0
for item in zin.infolist():
    data = zin.read(item.filename)
    if item.filename == 'word/theme/theme1.xml':
        s = data.decode('utf-8')
        for face in ('黑体', EA):        # majorFont 在前，minorFont 在后
            new = s.replace('<a:ea typeface=""/>', '<a:ea typeface="%s"/>' % face, 1)
            if new != s:
                ea_hit += 1; s = new
        data = s.encode('utf-8')
    elif item.filename == 'word/document.xml':
        s = data.decode('utf-8')
        s, fn_hit = fix_rfonts(s)
        data = s.encode('utf-8')
    zout.writestr(item, data)
zout.close(); zin.close()
os.replace(tmp, SRC)
print('theme <a:ea> patched: %d ; rFonts(theme) rewritten: %d' % (ea_hit, fn_hit))
