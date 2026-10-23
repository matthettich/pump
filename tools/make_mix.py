"""A 8-bar test mix at 48 kHz for driving the pump plugins headless: kick, snare, hats, bass, a chord pad
and a lead with a couple of deliberate resonances (so the suppressor has something to find). Writes
mix.f32 (raw float32 interleaved stereo, for the test audio backend) and mix.wav."""
import numpy as np, sys
from scipy.signal import lfilter, butter
from scipy.io import wavfile

sr = 48000
bpm = 124
beat = 60 / bpm
bars = 8
n = int(sr * beat * 4 * bars)
t = np.arange(n) / sr
L = np.zeros(n); R = np.zeros(n)
rng = np.random.default_rng(7)

def env(length, a, d, curve=4.0):
    k = np.arange(length) / sr
    e = np.minimum(k / max(a, 1e-4), 1.0) * np.exp(-k * curve / d)
    return e

def put(sig, at, gl=1.0, gr=1.0):
    i = int(at * sr)
    m = min(len(sig), n - i)
    if m <= 0: return
    L[i:i+m] += sig[:m] * gl; R[i:i+m] += sig[:m] * gr

# kick
kl = int(0.45 * sr); kt = np.arange(kl) / sr
kf = 48 + 140 * np.exp(-kt * 38)
kick = np.sin(2 * np.pi * np.cumsum(kf) / sr) * env(kl, 0.001, 0.45, 5) + 0.3 * rng.standard_normal(kl) * env(kl, 0.0005, 0.01, 6)
kick = np.tanh(kick * 1.6) * 0.9
# snare
sl = int(0.3 * sr)
b, a = butter(2, [900 / (sr / 2), 9000 / (sr / 2)], 'band')
snare = lfilter(b, a, rng.standard_normal(sl)) * env(sl, 0.0005, 0.22, 5) * 0.9
snare += np.sin(2 * np.pi * 190 * np.arange(sl) / sr) * env(sl, 0.0005, 0.12, 6) * 0.5
# hat
hl = int(0.09 * sr)
b, a = butter(4, 7000 / (sr / 2), 'high')
hat = lfilter(b, a, rng.standard_normal(hl)) * env(hl, 0.0003, 0.06, 6) * 0.35

for bar in range(bars):
    for q in range(4):
        tt = (bar * 4 + q) * beat
        put(kick, tt)
        if q in (1, 3): put(snare, tt, 0.85, 1.0)
        for e8 in range(2):
            put(hat * (1.0 if e8 else 0.6), tt + e8 * beat / 2, 0.7, 1.0)

# bass (saw through a low-pass), root notes per bar
roots = [41.2, 41.2, 49.0, 36.7, 41.2, 41.2, 55.0, 49.0]
bass = np.zeros(n)
for bar, f in enumerate(roots):
    for e8 in range(8):
        st = (bar * 4) * beat + e8 * beat / 2
        ln = int(beat / 2 * 0.9 * sr)
        ph = np.arange(ln) * f / sr
        saw = 2 * (ph % 1) - 1
        i = int(st * sr); m = min(ln, n - i)
        bass[i:i+m] += (saw * env(ln, 0.003, 0.5, 1.5))[:m]
b, a = butter(2, 380 / (sr / 2), 'low')
bass = np.tanh(lfilter(b, a, bass) * 1.4) * 0.45
L += bass; R += bass

# chord pad (detuned saws, wide), with a resonant peak around 2.9 kHz (a harsh resonance)
chords = [[220, 261.6, 329.6], [220, 261.6, 329.6], [196, 246.9, 293.7], [174.6, 220, 261.6]] * 2
padL = np.zeros(n); padR = np.zeros(n)
for bar, ch in enumerate(chords):
    i = int(bar * 4 * beat * sr); ln = int(4 * beat * sr); m = min(ln, n - i)
    k = np.arange(ln) / sr
    e = np.minimum(k / 0.25, 1) * np.minimum((ln / sr - k) / 0.2, 1)
    for f in ch:
        for det, side in ((0.997, 0), (1.003, 1)):
            saw = 2 * ((k * f * det) % 1) - 1
            (padL if side == 0 else padR)[i:i+m] += (saw * e)[:m] * 0.06
w0 = 2 * np.pi * 2900 / sr; q = 9; al = np.sin(w0) / (2 * q); A = 10 ** (12 / 40)
bb = [1 + al * A, -2 * np.cos(w0), 1 - al * A]; aa = [1 + al / A, -2 * np.cos(w0), 1 - al / A]
b, a = butter(2, 5500 / (sr / 2), 'low')
padL = lfilter(bb, aa, lfilter(b, a, padL)); padR = lfilter(bb, aa, lfilter(b, a, padR))
L += padL; R += padR

# lead with a ringing 1.2 kHz resonance on some notes
mel = [659.3, 587.3, 523.3, 587.3, 659.3, 784.0, 659.3, 523.3]
for bar in range(bars):
    for k2 in range(2):
        f = mel[(bar * 2 + k2) % len(mel)]
        st = bar * 4 * beat + k2 * 2 * beat + beat * 0.5
        ln = int(beat * 1.3 * sr); kk = np.arange(ln) / sr
        sq = np.sign(np.sin(2 * np.pi * f * kk + 0.3 * np.sin(2 * np.pi * 5.5 * kk))) * env(ln, 0.01, 1.2, 1.2) * 0.07
        put(sq, st, 0.8, 0.6)
w0 = 2 * np.pi * 1200 / sr; q = 14; al = np.sin(w0) / (2 * q); A = 10 ** (10 / 40)
bb = [1 + al * A, -2 * np.cos(w0), 1 - al * A]; aa = [1 + al / A, -2 * np.cos(w0), 1 - al / A]
L = lfilter(bb, aa, L); R = lfilter(bb, aa, R)

pk = max(np.abs(L).max(), np.abs(R).max())
L *= 0.5 / pk; R *= 0.5 / pk   # about -6 dBFS peak: room for the limiter to work
out = np.stack([L, R], 1).astype(np.float32)
base = sys.argv[1] if len(sys.argv) > 1 else 'mix'
out.tofile(base + '.f32')
wavfile.write(base + '.wav', sr, out)
print(base, out.shape, 'peak', pk)
