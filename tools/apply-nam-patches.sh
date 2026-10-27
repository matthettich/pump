#!/bin/sh
# Applies patches/nam/*.patch (fixes to the NAM core that pump needs, see patches/nam/README.md) to a NAM checkout.
# Idempotent; run automatically by `make` (and by AMP's own Makefile).
# Usage: tools/apply-nam-patches.sh [path-to-nam]   (default: the third_party/NeuralAmpModelerCore submodule)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAM=${1:-$ROOT/third_party/NeuralAmpModelerCore}
[ -d "$NAM/NAM" ] || { echo "apply-nam-patches: no NAM core at $NAM (git submodule update --init --recursive?)" >&2; exit 1; }
exec sh "$ROOT/tools/apply-patch-series.sh" "$ROOT/patches/nam" "$NAM" apply-nam-patches
