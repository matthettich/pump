#!/usr/bin/env python3
# Compares two sets of renders from tools/render_golden.sh: per plugin, how many settings differ and by how much
# (the largest sample difference, and relative to the render's peak, in dB). Exit status 1 when anything differs by
# more than the tolerance (--tol, a difference relative to the peak in dB; default -120 dB, i.e. float rounding).
# Needs numpy.
import argparse, os, sys
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('before'); ap.add_argument('after')
ap.add_argument('--tol', type=float, default=-120.0)
a = ap.parse_args()

bad = 0
for plug in sorted(os.listdir(a.before)):
    pb, pa = os.path.join(a.before, plug), os.path.join(a.after, plug)
    if not os.path.isdir(pa): print(f'{plug:8} missing in {a.after}'); bad += 1; continue
    worst, worst_rel, differ, n = 0.0, -300.0, 0, 0
    for fn in sorted(os.listdir(pb), key=lambda s: int(s.split('.')[0])):
        n += 1
        fa = os.path.join(pa, fn)
        x = np.fromfile(os.path.join(pb, fn), dtype=np.float32).astype(np.float64)
        if not os.path.exists(fa): differ += 1; worst_rel = 0.0; continue
        y = np.fromfile(fa, dtype=np.float32).astype(np.float64)
        if len(x) != len(y): differ += 1; worst_rel = 0.0; continue
        nan_x, nan_y = np.isnan(x), np.isnan(y)
        if (nan_x != nan_y).any(): differ += 1; worst_rel = 0.0; continue
        d = float(np.max(np.abs(np.where(nan_x, 0.0, x - y)))) if len(x) else 0.0
        pk = float(np.max(np.abs(np.where(nan_x, 0.0, x)))) if len(x) else 0.0
        rel = 20 * np.log10(d / pk) if d > 0 and pk > 0 else (-300.0 if d == 0 else 0.0)
        if d > 0: differ += 1
        worst, worst_rel = max(worst, d), max(worst_rel, rel)
    changed = worst_rel > a.tol
    if changed: bad += 1
    flag = 'CHANGED' if changed else ('ok (rounding)' if differ else 'identical')
    print(f'{plug:8} {n:3} settings  {differ:3} differ  max diff {worst:.2e}  ({worst_rel:7.1f} dB of peak)  {flag}')
sys.exit(1 if bad else 0)
