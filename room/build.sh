#!/bin/sh
# Builds pump ROOM, the room measurement app: one self-contained page (bin/pump-room.html) with the measurement DSP
# (common/dsp/Room.hpp via wasm/room_wasm.cpp) compiled to WebAssembly and the fonts inside it. Open the file in
# Chrome or Edge: nothing to install, nothing to serve.
#
# Toolchain: wasi-sdk 25, as for wasm/build.sh (WASI_SDK = its folder).
# Usage: WASI_SDK=/path/to/wasi-sdk-25.0-x86_64-linux sh room/build.sh [OUT.html]
set -e
H=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$H/bin/pump-room.html}
[ -x "$WASI_SDK/bin/clang++" ] || { echo "set WASI_SDK to the wasi-sdk 25 folder"; exit 1; }
O=$(mktemp -d)
FLAGS="--target=wasm32-wasip1 --sysroot=$WASI_SDK/share/wasi-sysroot -O2 -fno-finite-math-only -msimd128 -DPFFFT_STATIC_DEFINE -DNDEBUG"
"$WASI_SDK/bin/clang" $FLAGS -Wno-everything -c "$H/common/pump_pffft.c" -I"$H/common" -o "$O/pffft.o"
"$WASI_SDK/bin/clang++" $FLAGS -std=c++17 -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter -Wno-unused-private-field -I"$H/common" -c "$H/wasm/room_wasm.cpp" -o "$O/room.o"
EXPORTS=""
for f in room_alloc room_release_buf room_sweep room_sweep_ptr room_sweep_band room_analyze room_load room_free room_info \
         room_ir_ptr room_response room_grid_size room_grid_ptr room_cal room_cal_clear room_cal_info room_cal_at \
         room_rms_db room_peak_db room_corr_begin room_corr_add room_corr_add_curve room_corr_target room_corr_design room_corr_curve \
         room_corr_grid_size room_corr_info room_corr_profile room_text_ptr room_text_len room_corr_fir room_fir_ptr; do EXPORTS="$EXPORTS -Wl,--export=$f"; done
"$WASI_SDK/bin/clang++" $FLAGS -mexec-model=reactor -Wl,--export=_initialize $EXPORTS -Wl,--strip-all -Wl,-z,stack-size=1048576 \
  -Wl,--initial-memory=33554432 -Wl,--max-memory=1073741824 "$O/room.o" "$O/pffft.o" -o "$O/pump-room.wasm"
# the build's stamp for the update check: the commit and its time (version.json beside the page, which CI publishes)
BUILD_ID=$(git -C "$H" rev-parse --short=12 HEAD 2>/dev/null || echo dev)
BUILD_TIME=$(git -C "$H" log -1 --format=%ct 2>/dev/null || echo 0)
mkdir -p "$(dirname "$OUT")"
python3 - "$H" "$O/pump-room.wasm" "$OUT" "$BUILD_ID" "$BUILD_TIME" <<'PY'
import base64, json, sys, os
h, wasm, out, build_id, build_time = sys.argv[1:6]
build = json.dumps({'id': build_id, 'time': int(build_time)})

# the Pi buying guide (docs/PI-BOX.md) as HTML: headings, paragraphs, lists, tables, bold, italics, code, links
import html, re
def md_inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r'`([^`]+)`', r'<code>\1</code>', t)
    t = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', t)
    t = re.sub(r'(?<![*\w])\*([^*]+)\*(?!\*)', r'<i>\1</i>', t)
    t = re.sub(r'\[([^\]]+)\]\((https?://[^)\s]+)\)', r'<a href="\2" target="_blank" rel="noopener">\1</a>', t)
    return t
def md_html(md):
    out, lines, i = [], md.split('\n'), 0
    while i < len(lines):
        l = lines[i]
        if not l.strip(): i += 1; continue
        m = re.match(r'(#{1,3}) (.*)', l)
        if m:
            n = len(m.group(1)); out.append('<h%d>%s</h%d>' % (n, md_inline(m.group(2)), n)); i += 1; continue
        if l.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                rows.append([c.strip() for c in lines[i].strip().strip('|').split('|')]); i += 1
            body = [r for r in rows[1:] if not all(re.fullmatch(r'-+', c) for c in r)]
            t = '<div class="guide-table"><table><tr>' + ''.join('<th>%s</th>' % md_inline(c) for c in rows[0]) + '</tr>'
            t += ''.join('<tr>' + ''.join('<td>%s</td>' % md_inline(c) for c in r) + '</tr>' for r in body) + '</table></div>'
            out.append(t); continue
        m = re.match(r'(- |\d+\. )', l)
        if m:
            tag = 'ul' if l.startswith('- ') else 'ol'; items = []
            while i < len(lines) and re.match(r'(- |\d+\. )', lines[i]):
                items.append(re.sub(r'^(- |\d+\. )', '', lines[i])); i += 1
            out.append('<%s>%s</%s>' % (tag, ''.join('<li>%s</li>' % md_inline(x) for x in items), tag)); continue
        para = []
        while i < len(lines) and lines[i].strip() and not re.match(r'(#|\||- |\d+\. )', lines[i]):
            para.append(lines[i]); i += 1
        out.append('<p>%s</p>' % md_inline(' '.join(para)))
    return '\n'.join(out)
r = lambda p: open(os.path.join(h, p), encoding='utf-8').read()
b64 = lambda p: base64.b64encode(open(p, 'rb').read()).decode()
page = r('room/app.html')
for key, value in [('@STYLE@', r('room/app.css')),
                   ('@FONT_STM@', b64(os.path.join(h, 'common/fonts/ShareTechMono-Regular.ttf'))),
                   ('@FONT_JBM@', b64(os.path.join(h, 'common/fonts/JetBrainsMono-Regular.ttf'))),
                   ('@WASM@', b64(wasm)),
                   ('@WORKLET@', json.dumps(r('room/worklet.js'))),
                   ('@BUILD@', build),
                   ('@PIGUIDE@', md_html(r('docs/PI-BOX.md'))),
                   ('@PIGUIDE_MD@', json.dumps(r('docs/PI-BOX.md')).replace('</', '<\\/')),
                   ('@SCRIPT@', r('room/app.js'))]:
    assert key in page, key
    page = page.replace(key, value)
open(out, 'w', encoding='utf-8').write(page)
open(os.path.join(os.path.dirname(out), 'pump-room-version.json'), 'w').write(build + '\n')
print('built %s (%d KB)' % (out, len(page) // 1024))
PY
rm -rf "$O"
