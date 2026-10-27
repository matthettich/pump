#!/bin/sh
# Applies a patch series (patches/<name>/*.patch, in order) to a checkout of a third-party library: DPF
# (tools/apply-dpf-patches.sh) and the NAM core (tools/apply-nam-patches.sh). The patches are a series, and later ones may
# touch lines earlier ones added. So the check works on the series: find the last patch that is already applied (it
# reverses cleanly), then apply the ones after it. Idempotent; run automatically by `make`.
# Usage: tools/apply-patch-series.sh <patch-folder> <checkout> <label>
set -e
SERIES=$1
DIR=$2
LABEL=$3
# a git checkout (the submodule) uses git apply; any other folder (DPF_PATH=..., an unpacked copy) uses patch(1)
if git -C "$DIR" rev-parse --git-dir >/dev/null 2>&1; then
    applied() { git -C "$DIR" apply --reverse --check "$1" >/dev/null 2>&1; }
    doapply() { git -C "$DIR" apply "$1"; }
else
    applied() { patch -d "$DIR" -p1 -R --dry-run -s -f < "$1" >/dev/null 2>&1; }
    doapply() { patch -d "$DIR" -p1 -s -f < "$1"; }
fi

set -- "$SERIES"/*.patch
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
        echo "$LABEL: applied $(basename "$p")" >&2
    else
        echo "$LABEL: $(basename "$p") does not apply to $DIR (updated upstream? refresh the patch)" >&2
        exit 1
    fi
    i=$((i + 1))
done
