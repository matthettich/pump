"""Test stems for tuning source presets: the parts of tools/make_mix.py rendered separately, plus a synthetic vocal
(formant-filtered pulse with vibrato and sibilant "s" bursts) and a distorted guitar, all 8 bars at 48 kHz, 124 BPM.
Writes <dir>/<stem>.f32 (raw float32 interleaved stereo, the format the tests and the test audio backend read) and
<dir>/<stem>.wav for: drums, kick, snare, bass, keys (the pad), lead, vocal, guitar, and bus (all of them summed).
Each stem is normalised to -6 dBFS peak, like the mix.
Usage: python3 tools/make_stems.py [outdir]   (default: ./stems)"""
import os, sys
import numpy as np
from scipy.signal import lfilter, butter
from scipy.io import wavfile

sr = 48000
bpm = 124
beat = 60 / bpm
bars = 8
n = int(sr * beat * 4 * bars)
rng = np.random.default_rng(7)


def env(length, a, d, curve=4.0):
    k = np.arange(length) / sr
    return np.minimum(k / max(a, 1e-4), 1.0) * np.exp(-k * curve / d)


def blank():
    return np.zeros(n), np.zeros(n)


def put(buf, sig, at, gl=1.0, gr=1.0):
    L, R = buf
    i = int(at * sr)
    m = min(len(sig), n - i)
    if m <= 0:
        return
    L[i:i + m] += sig[:m] * gl
    R[i:i + m] += sig[:m] * gr


def peq(x, f, q, db):
    w0 = 2 * np.pi * f / sr; al = np.sin(w0) / (2 * q); A = 10 ** (db / 40)
    bb = [1 + al * A, -2 * np.cos(w0), 1 - al * A]; aa = [1 + al / A, -2 * np.cos(w0), 1 - al / A]
    return lfilter(bb, aa, x)


stems = {}

# ---- drums (as in make_mix.py)
kl = int(0.45 * sr); kt = np.arange(kl) / sr
kf = 48 + 140 * np.exp(-kt * 38)
kick = np.sin(2 * np.pi * np.cumsum(kf) / sr) * env(kl, 0.001, 0.45, 5) + 0.3 * rng.standard_normal(kl) * env(kl, 0.0005, 0.01, 6)
kick = np.tanh(kick * 1.6) * 0.9
sl = int(0.3 * sr)
b, a = butter(2, [900 / (sr / 2), 9000 / (sr / 2)], 'band')
snare = lfilter(b, a, rng.standard_normal(sl)) * env(sl, 0.0005, 0.22, 5) * 0.9
snare += np.sin(2 * np.pi * 190 * np.arange(sl) / sr) * env(sl, 0.0005, 0.12, 6) * 0.5
hl = int(0.09 * sr)
b, a = butter(4, 7000 / (sr / 2), 'high')
hat = lfilter(b, a, rng.standard_normal(hl)) * env(hl, 0.0003, 0.06, 6) * 0.35
k_buf, s_buf, d_buf = blank(), blank(), blank()
for bar in range(bars):
    for q in range(4):
        tt = (bar * 4 + q) * beat
        put(k_buf, kick, tt); put(d_buf, kick, tt)
        if q in (1, 3):
            put(s_buf, snare, tt, 0.85, 1.0); put(d_buf, snare, tt, 0.85, 1.0)
        for e8 in range(2):
            put(d_buf, hat * (1.0 if e8 else 0.6), tt + e8 * beat / 2, 0.7, 1.0)
stems['kick'] = k_buf; stems['snare'] = s_buf; stems['drums'] = d_buf

# ---- bass
roots = [41.2, 41.2, 49.0, 36.7, 41.2, 41.2, 55.0, 49.0]
bass = np.zeros(n)
for bar, f in enumerate(roots):
    for e8 in range(8):
        st = (bar * 4) * beat + e8 * beat / 2
        ln = int(beat / 2 * 0.9 * sr)
        ph = np.arange(ln) * f / sr
        saw = 2 * (ph % 1) - 1
        i = int(st * sr); m = min(ln, n - i)
        bass[i:i + m] += (saw * env(ln, 0.003, 0.5, 1.5))[:m]
b, a = butter(2, 380 / (sr / 2), 'low')
bass = np.tanh(lfilter(b, a, bass) * 1.4) * 0.45
stems['bass'] = (bass.copy(), bass.copy())

# ---- keys: the chord pad with its 2.9 kHz resonance
chords = [[220, 261.6, 329.6], [220, 261.6, 329.6], [196, 246.9, 293.7], [174.6, 220, 261.6]] * 2
padL = np.zeros(n); padR = np.zeros(n)
for bar, ch in enumerate(chords):
    i = int(bar * 4 * beat * sr); ln = int(4 * beat * sr); m = min(ln, n - i)
    k = np.arange(ln) / sr
    e = np.minimum(k / 0.25, 1) * np.minimum((ln / sr - k) / 0.2, 1)
    for f in ch:
        for det, side in ((0.997, 0), (1.003, 1)):
            saw = 2 * ((k * f * det) % 1) - 1
            (padL if side == 0 else padR)[i:i + m] += (saw * e)[:m] * 0.06
b, a = butter(2, 5500 / (sr / 2), 'low')
padL = peq(lfilter(b, a, padL), 2900, 9, 12); padR = peq(lfilter(b, a, padR), 2900, 9, 12)
stems['keys'] = (padL, padR)

# ---- lead synth with its ringing 1.2 kHz resonance
mel = [659.3, 587.3, 523.3, 587.3, 659.3, 784.0, 659.3, 523.3]
lead = blank()
for bar in range(bars):
    for k2 in range(2):
        f = mel[(bar * 2 + k2) % len(mel)]
        st = bar * 4 * beat + k2 * 2 * beat + beat * 0.5
        ln = int(beat * 1.3 * sr); kk = np.arange(ln) / sr
        sq = np.sign(np.sin(2 * np.pi * f * kk + 0.3 * np.sin(2 * np.pi * 5.5 * kk))) * env(ln, 0.01, 1.2, 1.2) * 0.07
        put(lead, sq, st, 0.8, 0.6)
stems['lead'] = (peq(lead[0], 1200, 14, 10), peq(lead[1], 1200, 14, 10))

# ---- vocal: a glottal-ish pulse train through vowel formants, vibrato, phrase dynamics, sibilant bursts
vox = np.zeros(n)
notes = [(0, 2, 220.0), (2, 1.5, 246.9), (4, 2, 261.6), (6.5, 1.5, 246.9), (8, 3, 220.0), (12, 1, 196.0), (13, 2.5, 220.0),
         (16, 2, 293.7), (18.5, 1.5, 261.6), (20, 3, 246.9), (24, 2, 220.0), (26.5, 1.5, 196.0), (28, 3.5, 220.0)]
vowels = [(730, 1090, 2440), (530, 1840, 2480), (300, 2290, 3010), (570, 840, 2410)]   # a, e, i, o
for idx, (b0, dur, f0) in enumerate(notes):
    i = int(b0 * beat * sr); ln = int(dur * beat * sr); m = min(ln, n - i)
    k = np.arange(ln) / sr
    f = f0 * (1 + 0.012 * np.sin(2 * np.pi * 5.2 * k) * np.minimum(k / 0.4, 1))
    ph = np.cumsum(f) / sr
    src = (ph % 1 < 0.35).astype(float) - 0.35 + 0.02 * rng.standard_normal(ln)
    fm = vowels[idx % len(vowels)]
    y = np.zeros(ln)
    for fr, g in zip(fm, (1.0, 0.5, 0.25)):
        y += g * peq(src, fr, 6, 18)
    y -= src * 0.9
    swell = np.minimum(k / 0.06, 1) * np.minimum((ln / sr - k) / 0.12, 1) * (0.6 + 0.4 * np.sin(np.pi * k / (ln / sr)))
    vox[i:i + m] += (y * swell)[:m]
    # an "s" at the end of some phrases: sharp 5-9 kHz noise (what a de-esser should catch)
    if idx % 3 == 2:
        sl2 = int(0.12 * sr); bb2, aa2 = butter(4, [5000 / (sr / 2), 9500 / (sr / 2)], 'band')
        s_noise = lfilter(bb2, aa2, rng.standard_normal(sl2)) * env(sl2, 0.01, 0.12, 2) * 3.0
        j = min(n - sl2, i + ln - sl2 // 2)
        vox[j:j + sl2] += s_noise
b, a = butter(2, 90 / (sr / 2), 'high')
vox = lfilter(b, a, vox)
stems['vocal'] = (vox.copy(), vox.copy())

# ---- guitar: distorted power chords, double-tracked (wide), cabinet low-pass
gch = [(82.4, 123.5), (82.4, 123.5), (98.0, 146.8), (73.4, 110.0)] * 2
gL = np.zeros(n); gR = np.zeros(n)
for bar, (r1, r5) in enumerate(gch):
    for e8 in range(8):
        st = bar * 4 * beat + e8 * beat / 2
        ln = int(beat / 2 * 0.95 * sr); k = np.arange(ln) / sr
        for buf, det, ofs in ((gL, 1.0, 0.0), (gR, 1.004, 0.006)):
            i = int((st + ofs) * sr); m = min(ln, n - i)
            if m <= 0:
                continue
            x = sum(np.sin(2 * np.pi * fr * det * k + 0.2 * np.sin(2 * np.pi * fr * 2 * k)) for fr in (r1, r5, r1 * 2))
            x = np.tanh(x * 6.0) * env(ln, 0.002, 0.6, 1.0) * (1.0 if e8 % 2 == 0 else 0.75)
            buf[i:i + m] += x[:m]
b, a = butter(4, 4800 / (sr / 2), 'low')
gL = peq(lfilter(b, a, gL), 2500, 1.2, 4); gR = peq(lfilter(b, a, gR), 2500, 1.2, 4)
stems['guitar'] = (gL, gR)


def norm(L, R):
    pk = max(np.abs(L).max(), np.abs(R).max(), 1e-9)
    return L * 0.5 / pk, R * 0.5 / pk


out = sys.argv[1] if len(sys.argv) > 1 else 'stems'
os.makedirs(out, exist_ok=True)
busL = np.zeros(n); busR = np.zeros(n)
levels = {'drums': 1.0, 'bass': 0.8, 'keys': 0.5, 'lead': 0.45, 'vocal': 0.7, 'guitar': 0.5}
for name, (L, R) in stems.items():
    L, R = norm(L, R)
    if name in levels:
        busL += L * levels[name]; busR += R * levels[name]
    data = np.stack([L, R], 1).astype(np.float32)
    data.tofile(os.path.join(out, name + '.f32'))
    wavfile.write(os.path.join(out, name + '.wav'), sr, data)
L, R = norm(busL, busR)
data = np.stack([L, R], 1).astype(np.float32)
data.tofile(os.path.join(out, 'bus.f32'))
wavfile.write(os.path.join(out, 'bus.wav'), sr, data)
print('wrote', len(stems) + 1, 'stems to', out, '(%d samples each)' % n)
