#!/usr/bin/env bash
# Regenerate gui/main/ui/fonts/vg_font_ui_14.c
# Sources: UI C strings, point-table JSON, MThings names, GB2312 level-1 (~3755).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$ROOT"
python3 <<'PY'
import json
import pathlib
import re

root = pathlib.Path('.')
ui = root / 'gui' / 'main' / 'ui'
sym_path = ui / 'fonts' / 'cjk_symbols.txt'
str_re = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"')
chars = set()

def add_text(s):
    if not s:
        return
    for c in s:
        o = ord(c)
        if o >= 0x80:
            chars.add(c)

def walk_json(obj):
    if isinstance(obj, str):
        add_text(obj)
    elif isinstance(obj, dict):
        for v in obj.values():
            walk_json(v)
    elif isinstance(obj, list):
        for v in obj:
            walk_json(v)

for p in ui.rglob('*.c'):
    if 'fonts' in p.parts:
        continue
    add_text(p.read_text(encoding='utf-8'))

for p in (root / 'gui' / 'main' / 'ui' / 'model').glob('*.c'):
    add_text(p.read_text(encoding='utf-8'))

json_dirs = [
    root / 'scripts',
    pathlib.Path('/mnt/f/Project/uppercomputer/examples'),
    pathlib.Path('/mnt/f/Project/uppercomputer'),
]
for d in json_dirs:
    if not d.is_dir():
        continue
    for p in d.rglob('*.json'):
        try:
            walk_json(json.loads(p.read_text(encoding='utf-8')))
        except Exception:
            continue

# GB2312 level-1 hanzi (covers 相/旁/磁 and typical SCADA names)
for b1 in range(0xB0, 0xD8):
    for b2 in range(0xA1, 0xFF):
        try:
            add_text(bytes((b1, b2)).decode('gb2312'))
        except UnicodeDecodeError:
            pass

# Punctuation used on HMI / point names (not all in GB2312 L1)
add_text('·–…→、「」。【】（）：？，！；℃°')

# Keep previous extras that the converter already shipped
old = []
if sym_path.exists():
    old = sym_path.read_text(encoding='utf-8').split()
    for t in old:
        add_text(t)

punct = []
han = []
other = []
for c in chars:
    o = ord(c)
    if 0x4E00 <= o <= 0x9FFF:
        han.append(c)
    elif o >= 0x80:
        if c in '·–…→、「」。【】（）：？，！；℃°':
            punct.append(c)
        else:
            other.append(c)
punct.sort(key=ord)
han.sort(key=ord)
other.sort(key=ord)
out = punct + other + han
sym_path.write_text(' '.join(out) + '\n', encoding='utf-8')
print(f'cjk_symbols.txt tokens={len(out)} hanzi={len(han)}')
PY
FONT=${FONT:-/mnt/c/Windows/Fonts/simhei.ttf}
if [[ ! -f "$FONT" ]]; then
  echo "missing TTF: $FONT" >&2
  exit 1
fi
# Read symbols as one argument (GB2312 L1 is ~11KB; well under ARG_MAX).
SYMS=$(tr -d '\n' < gui/main/ui/fonts/cjk_symbols.txt)
npx --yes lv_font_conv@1.5.2 \
  --font "$FONT" --size 14 --bpp 4 --format lvgl \
  -r 0x20-0x7E --symbols "$SYMS" --no-compress \
  -o gui/main/ui/fonts/vg_font_ui_14.c
echo "regenerated gui/main/ui/fonts/vg_font_ui_14.c ($(wc -c < gui/main/ui/fonts/vg_font_ui_14.c) bytes)"
