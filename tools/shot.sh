#!/bin/sh
# Headless screenshot of a standalone build: tools/shot.sh <binary> <out.png> [preset] [dark] [seconds]
# Needs Xvfb, ImageMagick (import) and a DPF build with the local test audio backend (PUMP_TEST_AUDIO).
BIN="$1"; OUT="$2"; PRESET="${3:-}"; DARK="${4:-0}"; SECS="${5:-4}"
DISPLAY_NUM="${DISPLAY_NUM:-:99}"
if ! xdpyinfo -display $DISPLAY_NUM >/dev/null 2>&1 && ! pgrep -f "Xvfb $DISPLAY_NUM" >/dev/null; then
  Xvfb $DISPLAY_NUM -screen 0 1600x1000x24 >/dev/null 2>&1 &
  sleep 1
fi
export DISPLAY=$DISPLAY_NUM LIBGL_ALWAYS_SOFTWARE=1
# no JACK server here: the stand-in in tools/headless_jack.c runs the plugin and plays PUMP_TEST_NOTES into it
JACKSHIM="$(cd "$(dirname "$0")/.." && pwd)/build/headless-jack"
[ -f "$JACKSHIM/libjack.so.0" ] && export LD_LIBRARY_PATH="$JACKSHIM${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PUMP_TEST_AUDIO="${PUMP_TEST_AUDIO:-$(cd "$(dirname "$0")/.." && pwd)/tests/data/mix.f32}"
[ -n "$PRESET" ] && export PUMP_TEST_PRESET="$PRESET"
export PUMP_TEST_DARK="$DARK"
LOG="${OUT%.png}.log"
"$BIN" >"$LOG" 2>&1 &
PID=$!
sleep "$SECS"
NAME=$(basename "$BIN")
import -window root "$OUT.full.png" 2>/dev/null
kill $PID 2>/dev/null; wait $PID 2>/dev/null
# crop to the non-black area (the window)
convert "$OUT.full.png" -trim +repage "$OUT" 2>/dev/null
rm -f "$OUT.full.png"
echo "$OUT"
