#!/bin/sh
# Applies patches/dpf/*.patch (fixes to DPF that pump needs, see patches/dpf/README.md) to a DPF checkout.
# Idempotent: patches that are already applied are skipped. Run automatically by `make`.
# Usage: tools/apply-dpf-patches.sh [path-to-dpf]   (default: the dpf submodule)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPF=${1:-$ROOT/dpf}
[ -d "$DPF/distrho" ] || { echo "apply-dpf-patches: no DPF at $DPF (git submodule update --init --recursive?)" >&2; exit 1; }
# a git checkout (the submodule) uses git apply; any other DPF folder (DPF_PATH=...) uses patch(1)
if git -C "$DPF" rev-parse --git-dir >/dev/null 2>&1; then
    applied() { git -C "$DPF" apply --reverse --check "$1" >/dev/null 2>&1; }
    applies() { git -C "$DPF" apply --check "$1" >/dev/null 2>&1; }
    doapply() { git -C "$DPF" apply "$1"; }
else
    applied() { patch -d "$DPF" -p1 -R --dry-run -s -f < "$1" >/dev/null 2>&1; }
    applies() { patch -d "$DPF" -p1 --dry-run -s -f < "$1" >/dev/null 2>&1; }
    doapply() { patch -d "$DPF" -p1 -s -f < "$1"; }
fi
for p in "$ROOT"/patches/dpf/*.patch; do
    [ -e "$p" ] || continue
    if applied "$p"; then continue; fi
    if applies "$p"; then
        doapply "$p"
        echo "apply-dpf-patches: applied $(basename "$p")" >&2
    else
        echo "apply-dpf-patches: $(basename "$p") does not apply to $DPF (DPF updated? refresh the patch)" >&2
        exit 1
    fi
done
