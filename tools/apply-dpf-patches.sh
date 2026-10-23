#!/bin/sh
# Applies patches/dpf/*.patch (fixes to DPF that pump needs, see patches/dpf/README.md) to a DPF checkout.
# The patches are a series, applied in order, and later ones may touch lines earlier ones added. So the check
# works on the series: find the last patch that is already applied (it reverses cleanly), then apply the ones
# after it. Idempotent; run automatically by `make`.
# Usage: tools/apply-dpf-patches.sh [path-to-dpf]   (default: the dpf submodule)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPF=${1:-$ROOT/dpf}
[ -d "$DPF/distrho" ] || { echo "apply-dpf-patches: no DPF at $DPF (git submodule update --init --recursive?)" >&2; exit 1; }
# a git checkout (the submodule) uses git apply; any other DPF folder (DPF_PATH=...) uses patch(1)
if git -C "$DPF" rev-parse --git-dir >/dev/null 2>&1; then
    applied() { git -C "$DPF" apply --reverse --check "$1" >/dev/null 2>&1; }
    doapply() { git -C "$DPF" apply "$1"; }
else
    applied() { patch -d "$DPF" -p1 -R --dry-run -s -f < "$1" >/dev/null 2>&1; }
    doapply() { patch -d "$DPF" -p1 -s -f < "$1"; }
fi

set -- "$ROOT"/patches/dpf/*.patch
[ -e "$1" ] || exit 0
n=$#

# how many of the series are applied already (0..n)
done_count=0
i=$n
while [ $i -gt 0 ]; do
    eval "p=\${$i}"
    if applied "$p"; then done_count=$i; break; fi
    i=$((i - 1))
done

i=$((done_count + 1))
while [ $i -le $n ]; do
    eval "p=\${$i}"
    if doapply "$p"; then
        echo "apply-dpf-patches: applied $(basename "$p")" >&2
    else
        echo "apply-dpf-patches: $(basename "$p") does not apply to $DPF (DPF updated? refresh the patch)" >&2
        exit 1
    fi
    i=$((i + 1))
done
