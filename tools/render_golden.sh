#!/bin/sh
# Renders every plugin's defaults and factory presets (tools/render_golden.cpp) into <outdir>/<plugin>/, so a
# refactor can be checked for changes in sound:
#   sh tools/render_golden.sh /tmp/before        (on the old code)
#   sh tools/render_golden.sh /tmp/after         (on the new code)
#   python3 tools/compare_renders.py /tmp/before /tmp/after
# Optional: PLUGINS="eq comp" to render only some. Run from anywhere; needs the submodules (AMP's NAM core) and
# the DPF patches applied (make dpf-patches).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: render_golden.sh <outdir>}
CXX=${CXX:-c++}
CC=${CC:-cc}
mkdir -p "$OUT"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT
cd "$ROOT"
$CC -O2 -DPFFFT_STATIC_DEFINE -c common/pump_pffft.c -o "$BUILD/pffft.o"
FLAGS="-O2 -std=c++17 -fno-finite-math-only -DPFFFT_STATIC_DEFINE -DDGL_USE_FILE_BROWSER -w -Idpf/distrho -Idpf/distrho/src -Icommon"
for d in ${PLUGINS:-$(cd plugins && ls -d */ | tr -d /)}; do
  [ -f "plugins/$d/Makefile" ] || continue
  params=$(cd "plugins/$d" && ls *Params.hpp | head -1)
  files=$(sed -n 's/^FILES_DSP *= *//p' "plugins/$d/Makefile")
  srcs=""; objs="$BUILD/pffft.o"; extra=""
  for f in $files; do
    case "$f" in
      pffft.c) ;;
      *.c) # MACHINE's firmware, built as plugins/machine/Makefile builds it
           o="$BUILD/$d-$(basename "$f" .c).o"
           fp=""; $CC --version 2>/dev/null | grep -qi clang || fp="-D__fp16=_Float16"
           $CC -O2 -std=gnu11 -funsigned-char -w $fp -Iplugins/$d/fw/stub -Iplugins/$d/fw/firmware/Core/Inc -Iplugins/$d/fw -c "plugins/$d/$f" -o "$o"
           objs="$objs $o" ;;
      *) srcs="$srcs plugins/$d/$f" ;;
    esac
  done
  if [ "$d" = amp ]; then
    NAM=third_party/NeuralAmpModelerCore
    extra="-DNAM_SAMPLE_FLOAT -I$NAM -isystem $NAM/Dependencies/eigen -I$NAM/Dependencies/nlohmann"
  fi
  $CXX $FLAGS $extra -Iplugins/$d -DPARAMS_HDR="\"$params\"" -DNS=$d tools/render_golden.cpp $srcs $objs -lpthread -o "$BUILD/render_$d"
  mkdir -p "$OUT/$d"
  printf '%-8s ' "$d"
  "$BUILD/render_$d" "$ROOT/tests/data/mix.f32" "$OUT/$d"
done
