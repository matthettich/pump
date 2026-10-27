#!/bin/sh
# Applies patches/dpf/*.patch (fixes to DPF that pump needs, see patches/dpf/README.md) to a DPF checkout.
# Idempotent; run automatically by `make`. Usage: tools/apply-dpf-patches.sh [path-to-dpf]   (default: the dpf submodule)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPF=${1:-$ROOT/dpf}
[ -d "$DPF/distrho" ] || { echo "apply-dpf-patches: no DPF at $DPF (git submodule update --init --recursive?)" >&2; exit 1; }
exec sh "$ROOT/tools/apply-patch-series.sh" "$ROOT/patches/dpf" "$DPF" apply-dpf-patches
