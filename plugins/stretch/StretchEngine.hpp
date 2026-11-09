// pump STRETCH: the engine. Phase-randomised time stretching: the last 12 s are recorded and a read head runs through
// them 1.5 .. 50 times slower. Every hop (a quarter of a window) a Hann-windowed slice of the recording at the read
// head is taken to the frequency domain, every bin keeps its loudness and gets a random phase, and the inverse
// transform, windowed again, is overlap-added with its neighbours. Windows are 2048 .. 32768 samples (43 ms .. 0.68 s
// at 48 kHz; doubled from 88.2 kHz and quadrupled from 176.4 kHz so the times stay the same).
//
// This is a port of Thunder Plus's Extreme stretch (chains/chains-worklet.js, XStretch), with the same decisions:
//   - Freeze catches the moment at the read head and reads from the held copy, whose read point wanders +-N/16
//     (a drone that moves); 0.4 s crossfades into and out of it, 1 s for a jump.
//   - When the read head falls about 10 s behind (or the song restarts, or the JUMP key) it jumps to the newest sound
//     with a 1 s crossfade of the two read points' powers.
//   - Reading silence (the last window held less than -90 dB) it waits at the newest sound; when sound comes it is read
//     at normal speed until the head reaches the end of the window it came in, then slows down.
//   - The random numbers start from a fixed seed, so the same input renders the same output.
// What differs from the web build: the transforms are pffft's (one whole transform per unit of work, so each unit is a
// grab, a transform, a phase randomisation, an overlap-add or a slice of the Tone table, scheduled evenly over the hop),
// and the sizes scale with the sample rate.
//
// Real time: nothing is allocated outside prepare().
#pragma once

#include "dsp/Util.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Tempo.hpp"
#include "StretchParams.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace pump { namespace stretch {

struct Settings {
    float stretch = 0.5f;   // 0..1 (x1.5 .. x50)
    int window = 2;         // 0..4
    bool freeze = false;
    float tone = 0.0f;      // -1..1
    float mix = 0.5f;       // 0..1
    float outDb = 0.0f;
};

class Engine {
public:
    static constexpr int kHistCols = 600;      // the window's picture of the recording: one peak per column
    static constexpr int kToneChunk = 8192;    // Tone's table is made this many bins at a time

    Engine() : histPeak(new std::atomic<float>[kHistCols]) { for (int i = 0; i < kHistCols; ++i) histPeak[i].store(0.0f, std::memory_order_relaxed); }

    // Allocates everything. Not for the audio thread.
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 8000.0 ? sampleRate : 48000.0;
        sf = sr >= 176400.0 ? 4 : (sr >= 88200.0 ? 2 : 1);
        for (int i = 0; i < kNumWindows; ++i) size[i] = kWindowSamples[i] * sf;
        NM = size[kNumWindows - 1];
        K = NM / 2 + 1;
        cap = (int)std::lround(sr * 12.0);
        J = NM / 16;
        for (int c = 0; c < 2; ++c)
        {
            ring[c].assign((size_t)cap, 0.0f);
            hold[c].assign((size_t)(NM + 2 * J), 0.0f);
            acc[c].assign((size_t)(NM + NM / 4 + 8), 0.0f);
        }
        for (int i = 0; i < kNumWindows; ++i)
        {
            const int n = size[i];
            hann[i].resize((size_t)n);
            for (int k = 0; k < n; ++k) hann[i][(size_t)k] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * k / n));
            fft[i].setup(n);
        }
        x.resize((size_t)NM); spec.resize((size_t)NM);
        pw.assign((size_t)K, 0.0f); tilt.assign((size_t)K, 1.0f); ln.assign((size_t)K, 0.0f);
        for (int k = 0; k < K; ++k) ln[(size_t)k] = (float)std::log((double)std::max(k, 1));
        sinTab.resize((size_t)kSinN + 1);
        for (int i = 0; i <= kSinN; ++i) sinTab[(size_t)i] = (float)std::sin((double)i / kSinN * 2.0 * kPi);
        mixCoef = 0.002 / sf;
        colLen = std::max(1, cap / kHistCols);
        reset();
    }

    // Back to the start: an empty recording, the same random numbers.
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill(ring[c].begin(), ring[c].end(), 0.0f);
            std::fill(hold[c].begin(), hold[c].end(), 0.0f);
            std::fill(acc[c].begin(), acc[c].end(), 0.0f);
        }
        wpos = 0; H = size[2] / 4; cnt = 0; cen = 0.0; oldCen = 0.0; hc = 0.0;
        srcA = 0; srcB = 0; xf = 0; xfn = 0; frozen = false; jumpReq = false;
        u = 0; nu = 0; fe[0] = fe[1] = 0.0; quiet = true; catchTo = -INFINITY;
        fr = Frame(); fr.N = size[2];
        rng = 0x6d2b79f5u; tiltV = NAN; tiltN = 0;
        mixNow = -1.0; gainNow = 1.0;
        prevPlaying = false; prevPpq = 0.0; havePrev = false;
        jumpRequest.store(false, std::memory_order_relaxed);
        histCount = 0; histPk = 0.0f; histIdx = 0;
        for (int i = 0; i < kHistCols; ++i) histPeak[i].store(0.0f, std::memory_order_relaxed);
        visWritten.store(0, std::memory_order_relaxed); histCols.store(0, std::memory_order_relaxed);
        publish();
    }

    void setSettings(const Settings& s) { set = s; }
    int latency() const { return 0; }

    // The host transport of this block (may be absent: valid false, never playing). A start, or a jump back in the
    // song, asks for a jump to the newest sound.
    void setTransport(const Transport& t)
    {
        bool restart = false;
        if (t.playing && !prevPlaying) restart = true;
        else if (t.playing && t.valid && havePrev && t.ppq < prevPpq - 0.05) restart = true;
        prevPlaying = t.playing;
        if (t.valid) { prevPpq = t.ppq; havePrev = true; } else havePrev = false;
        if (restart) jumpRequest.store(true, std::memory_order_relaxed);
    }
    // From any thread (the window's JUMP key): read the newest sound again.
    void requestJump() { jumpRequest.store(true, std::memory_order_relaxed); }

    // ---- the window's view (any thread) ----
    double sampleRate() const { return sr; }
    int ringSamples() const { return cap; }
    int windowSamples(int w) const { return size[clampv(w, 0, kNumWindows - 1)]; }
    int64_t written() const { return visWritten.load(std::memory_order_relaxed); }
    double readCentre() const { return visCen.load(std::memory_order_relaxed); }
    double holdCentre() const { return visHold.load(std::memory_order_relaxed); }
    bool isFrozen() const { return visFrozen.load(std::memory_order_relaxed) != 0; }
    bool isWaiting() const { return visQuiet.load(std::memory_order_relaxed) != 0; }
    int windowNow() const { return visN.load(std::memory_order_relaxed); }
    uint32_t jumps() const { return visJumps.load(std::memory_order_relaxed); }
    // seconds the read head may fall behind before it jumps (for the window with N samples)
    double jumpBehindSeconds(int N) const { return (double)(cap - (int)std::lround(1.2 * sr) - NM - 2 * J + N / 2) / sr; }
    // the recording's picture: column c (absolute, the newest complete one is columns() - 1), `colLen` samples each
    int64_t columns() const { return histCols.load(std::memory_order_acquire); }
    float columnPeak(int64_t c) const { return histPeak[c % kHistCols].load(std::memory_order_relaxed); }
    float columnNow() const { return histNow.load(std::memory_order_relaxed); }
    int columnSamples() const { return colLen; }

    // ---- statistics of the work done (audio thread's own; tests read them after) ----
    int statMaxUnits = 0;       // most units run in one process() call
    double statMaxWork = 0.0;   // the same in "points" (a transform counts N log2 N, other units N)
    int statForced = 0;         // frames not finished when their hop ended (finished then, all at once)
    int statUnitsTotal = 0;

    // In place on L and R (n samples). `n` is the block the host gave: each frame's units are spread so that none of
    // the blocks of a hop does much more than another.
    void process(float* L, float* R, int B)
    {
        if (jumpRequest.exchange(false, std::memory_order_relaxed) && wpos > 0) jumpReq = true;
        const double mx = clampv((double)set.mix, 0.0, 1.0);
        if (mixNow < 0.0) mixNow = mx;
        const double gt = set.outDb == 0.0f ? 1.0 : std::pow(10.0, (double)set.outDb * 0.05);
        const int cap_ = cap;
        float* const r0 = ring[0].data();
        float* const r1 = ring[1].data();
        float* const a0 = acc[0].data();
        float* const a1 = acc[1].data();
        blockUnits = 0; blockWork = 0.0;
        int done = 0;
        while (done < B)
        {
            // this hop's share of the frame's work: it must be finished when the hop ends
            const int left = H - cnt, n = std::min(B - done, left);
            const int blocksLeft = std::max(1, (left + B - 1) / B);
            const int want = (nu - u + blocksLeft - 1) / blocksLeft;
            for (int k = 0; k < want && u < nu; ++k) unit();
            int w = (int)(wpos % cap_);
            double m = mixNow, g = gainNow;
            for (int i = done, j = cnt; i < done + n; ++i, ++j)
            {
                // a NaN, an infinity or an absurd sample (|x| >= 8) in is a 0, on the dry path too
                const float l0 = L[i], q0 = R[i];
                const float l = (l0 > -8.0f && l0 < 8.0f) ? l0 : 0.0f, r = (q0 > -8.0f && q0 < 8.0f) ? q0 : 0.0f;
                r0[w] = l; r1[w] = r;
                if (++w == cap_) w = 0;
                const float pk = std::max(std::fabs(l), std::fabs(r));
                if (pk > histPk) histPk = pk;
                if (++histCount == colLen)
                {
                    histPeak[histIdx % kHistCols].store(histPk, std::memory_order_relaxed);
                    histCols.store(++histIdx, std::memory_order_release);
                    histPk = 0.0f; histCount = 0;
                }
                m += (mx - m) * mixCoef;   // Mix glides, so it never clicks
                if (std::fabs(mx - m) < 1e-7) m = mx;
                const float wl = wetSafe(a0[j]), wr = wetSafe(a1[j]);
                float ol = l + (wl - l) * (float)m, orr = r + (wr - r) * (float)m;
                if (gt != 1.0 || g != 1.0)
                {
                    g += (gt - g) * mixCoef;
                    if (std::fabs(gt - g) < 1e-7) g = gt;
                    ol *= (float)g; orr *= (float)g;
                }
                L[i] = ol; R[i] = orr;
            }
            mixNow = m; gainNow = g;
            wpos += n; cnt += n; done += n;
            if (cnt >= H)
            {
                if (u < nu) ++statForced;
                while (u < nu) unit();
                hop();
            }
        }
        histNow.store(histPk, std::memory_order_relaxed);
        if (blockUnits > statMaxUnits) statMaxUnits = blockUnits;
        if (blockWork > statMaxWork) statMaxWork = blockWork;
        publish();
    }

private:
    static constexpr int kSinN = 4096;
    struct Frame { int N = 2048; bool two = false; int ns = 1; bool tu = false; int nTone = 0; double wa = 0.0; int sa = 0; int64_t a = 0; int sb = 0; int64_t b = 0; double tone = 0.0; };

    // The last word on a wet signal: untouched below 2, rounds off towards 4 above (never more); never NaN.
    static float wetSafe(float y)
    {
        if (!(y == y)) return 0.0f;
        const float a = y < 0.0f ? -y : y;
        if (a < 2.0f) return y;
        if (!(a < 1e6f)) return y < 0.0f ? -4.0f : 4.0f;
        const float e = a - 2.0f;
        return (y < 0.0f ? -1.0f : 1.0f) * (2.0f + e / (1.0f + e * 0.5f));
    }
    static int64_t jsRound(double v) { return (int64_t)std::floor(v + 0.5); }
    float sinT(float p) const
    {
        p -= std::floor(p);
        const float xx = p * kSinN;
        int i = (int)xx;
        if (i > kSinN - 1) i = kSinN - 1;
        return sinTab[(size_t)i] + (sinTab[(size_t)i + 1] - sinTab[(size_t)i]) * (xx - (float)i);
    }
    double nextRand()   // xorshift32 -> [0, 1)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double)rng * 2.3283064365386963e-10;
    }

    void publish()
    {
        visWritten.store(wpos, std::memory_order_relaxed);
        visCen.store(cen, std::memory_order_relaxed);
        visHold.store(hc, std::memory_order_relaxed);
        visFrozen.store(frozen ? 1 : 0, std::memory_order_relaxed);
        visQuiet.store(quiet && !frozen ? 1 : 0, std::memory_order_relaxed);
        visN.store(fr.N, std::memory_order_relaxed);
        visJumps.store(jumpCount, std::memory_order_relaxed);
    }

    // End of a hop: the frame made during it is in the accumulators; move them on, move the read head, plan the next
    // frame. Read sources: 0 the recording at the read head, 1 the frozen copy, 2 the recording at the old head (while a
    // jump crossfades away from it). A crossfade blends two sources' powers: 0.4 s into and out of Freeze, 1 s for a jump.
    void hop()
    {
        const int oldH = H;
        const int nextN = size[clampv(set.window, 0, kNumWindows - 1)], nextH = nextN / 4;
        for (int c = 0; c < 2; ++c)
        {
            float* a = acc[c].data();
            const size_t len = acc[c].size();
            std::memmove(a, a + oldH, (len - (size_t)oldH) * sizeof(float));
            std::memset(a + (len - (size_t)oldH), 0, (size_t)oldH * sizeof(float));
            // something went wrong (a non-finite or absurd value about to be played): start this channel afresh
            bool bad = false;
            for (int i = 0; i < nextH; ++i) if (!(a[i] > -64.0f && a[i] < 64.0f)) { bad = true; break; }
            if (bad) std::fill(acc[c].begin(), acc[c].end(), 0.0f);
        }
        cnt = 0;
        const int N = nextN, Hn = N / 4;
        const double stretch = stretchFactor((double)set.stretch), adv = (double)oldH / stretch;
        const bool frz = set.freeze;
        const int xfF = clampv((int)jsRound(0.4 * sr / Hn), 2, 40), xfJ = clampv((int)jsRound(sr / Hn), 2, 100);
        // how old the head may get: the room a 1 s crossfade and the largest window need, inside the 12 s
        const double oldest = (double)(wpos - cap + jsRound(1.2 * sr) + NM + 2 * J), newest = (double)wpos - N / 2.0;
        // Reading silence (the last frame's window held less than -90 dB): wait at the newest sound. When sound comes,
        // read at normal speed until the head reaches where it came in (the end of that window), then slow down.
        const bool freeNow = !frozen && srcB == 0 && xf >= xfn;
        if (freeNow)
        {
            if (fe[0] + fe[1] < 7.5e-10 * fr.N) quiet = true;
            else if (quiet) { quiet = false; catchTo = (double)(fr.b + fr.N); }
        }
        if (!frozen) cen = (freeNow && quiet) ? newest : std::min(cen + (cen < catchTo ? (double)oldH : adv), newest);   // frozen, it stays put
        if (srcA == 2 && xf < xfn) oldCen += adv;
        const bool tooOld = cen - N / 2.0 < oldest;
        if (frz && !frozen)
        {
            // catch the moment: copy the largest window (and room to jitter) around the read head
            frozen = true; hc = cen;
            const int64_t h0 = jsRound(hc - NM / 2.0 - J);
            for (int c = 0; c < 2; ++c) copyHold(c, h0);
            srcA = srcB == 2 ? 0 : srcB; srcB = 1; xf = 0; xfn = xfF;
        }
        else if (!frz && frozen)
        {
            frozen = false;
            if (jumpReq || tooOld) { cen = newest; jumpReq = false; catchTo = -INFINITY; ++jumpCount; }
            srcA = 1; srcB = 0; xf = 0; xfn = xfF;
        }
        else if (!frz && xf >= xfn && (jumpReq || tooOld))
        {
            // a jump to the newest sound: the old head keeps reading (fading out) while the new one fades in
            oldCen = cen; cen = newest; jumpReq = false; catchTo = -INFINITY; srcA = 2; srcB = 0; xf = 0; xfn = xfJ; ++jumpCount;
        }
        Frame& f = fr;
        const bool two = xf < xfn && srcA != srcB;
        f.N = N; f.two = two; f.ns = two ? 2 : 1;
        f.wa = two ? 1.0 - (double)(xf + 1) / (double)(xfn + 1) : 0.0;
        if (xf < xfn) ++xf;
        f.tone = clampv((double)set.tone, -1.0, 1.0);
        f.tu = !(std::fabs(f.tone - tiltV) <= 0.004) || N != tiltN;
        f.nTone = f.tu ? (K_of(N) + kToneChunk - 1) / kToneChunk : 0;
        f.sa = srcA; f.a = two ? jsRound(readPos(srcA, N) - N / 2.0) : 0;
        f.sb = srcB; f.b = jsRound(readPos(srcB, N) - N / 2.0);
        H = Hn; u = 0; nu = f.nTone + 2 * (2 * f.ns + 3);
    }
    static int K_of(int N) { return N / 2 + 1; }

    // the largest window (and room to jitter) around the held point, from the recording, into hold[c]; zeros where
    // nothing was recorded
    void copyHold(int c, int64_t h0)
    {
        std::vector<float>& hd = hold[c];
        const std::vector<float>& rg = ring[c];
        const int64_t lo = std::max<int64_t>(0, wpos - cap), hi = wpos, len = (int64_t)hd.size();
        for (int64_t i = 0; i < len;)
        {
            const int64_t q = h0 + i;
            if (q < lo || q >= hi) { hd[(size_t)i] = 0.0f; ++i; continue; }
            // a run inside the recording that doesn't wrap
            const int64_t k = q % cap;
            const int64_t run = std::min<int64_t>(std::min<int64_t>(len - i, hi - q), cap - k);
            std::memcpy(hd.data() + i, rg.data() + k, (size_t)run * sizeof(float));
            i += run;
        }
    }

    // Where a source reads; the frozen read point wanders +-N/16, so the drone moves.
    double readPos(int s, int N)
    {
        if (s != 1) return s == 2 ? oldCen : cen;
        return hc + (nextRand() * 2.0 - 1.0) * N / 16.0;
    }

    // One window of a source into x, Hann-windowed (zeros where nothing was recorded); returns its energy.
    double grab(int c, int s, int64_t at, int N, int wi)
    {
        float* xx = x.data();
        const float* w = hann[wi].data();
        double e = 0.0;
        if (s == 1)
        {
            const std::vector<float>& h = hold[c];
            const int64_t o = at - jsRound(hc - NM / 2.0 - J), hl = (int64_t)h.size();
            for (int i = 0; i < N; ++i) { const int64_t q = o + i; xx[i] = (q >= 0 && q < hl) ? h[(size_t)q] * w[i] : 0.0f; }
        }
        else
        {
            const float* rg = ring[c].data();
            const int64_t lo = std::max<int64_t>(0, wpos - cap), hi = wpos;
            int64_t q = at;
            int64_t k = ((at % cap) + cap) % cap;
            for (int i = 0; i < N; ++i, ++q)
            {
                const float v = (q >= lo && q < hi) ? rg[(size_t)k] * w[i] : 0.0f;
                xx[i] = v; e += (double)v * v;
                if (++k == cap) k = 0;
            }
        }
        return e;
    }

    static double fftWork(int N) { return (double)N * std::log2((double)N); }

    // One unit of the frame's work. In order: [the Tone table, in slices], then for each channel: each source (grab;
    // transform and power), then the resynthesis (random phases; inverse transform; window and add).
    void unit()
    {
        const Frame& f = fr;
        const int N = f.N, Kn = N / 2 + 1;
        int wi = 0; while (size[wi] != N) ++wi;
        int v = u++;
        ++statUnitsTotal; ++blockUnits;
        if (f.tu)
        {
            if (v < f.nTone)
            {
                // Tone: +-4.5 dB an octave about 1 kHz (+12 / -24 dB at most), from a table of ln k
                const double a = f.tone * 0.75, cb = std::log(sr / N / 1000.0), l30 = std::log(0.03);
                const int k0 = v * kToneChunk, k1 = std::min(Kn, k0 + kToneChunk);
                for (int k = k0; k < k1; ++k)
                {
                    double e = (double)ln[(size_t)k] + cb;
                    if (e < l30) e = l30;
                    e *= a;
                    tilt[(size_t)k] = e > 1.386 ? 4.0f : (e < -2.765 ? 0.063f : (float)std::exp(e));
                }
                blockWork += (double)(k1 - k0) * 4.0;
                if (v == f.nTone - 1) { tiltV = f.tone; tiltN = N; }
                return;
            }
            v -= f.nTone;
        }
        const int per = 2 * f.ns + 3, ci = v >= per ? 1 : 0;
        v -= ci * per;
        float* xx = x.data();
        float* sp = spec.data();
        if (v < 2 * f.ns)
        {
            const int si = v / 2;
            const bool first = f.two && si == 0;
            if ((v & 1) == 0)
            {
                const double e = grab(ci, first ? f.sa : f.sb, first ? f.a : f.b, N, wi);
                fe[ci] = first ? 1.0 : e;
                blockWork += N;
            }
            else
            {
                fft[wi].forward(xx, sp);
                // pffft's ordered layout: [re0, reN/2, re1, im1, re2, im2, ...]
                const float g = first ? (float)f.wa : (float)(1.0 - f.wa);
                float* p = pw.data();
                if (!f.two)
                {
                    p[0] = sp[0] * sp[0]; p[Kn - 1] = sp[1] * sp[1];
                    for (int k = 1; k < Kn - 1; ++k) p[k] = sp[2 * k] * sp[2 * k] + sp[2 * k + 1] * sp[2 * k + 1];
                }
                else if (first)
                {
                    p[0] = sp[0] * sp[0] * g; p[Kn - 1] = sp[1] * sp[1] * g;
                    for (int k = 1; k < Kn - 1; ++k) p[k] = (sp[2 * k] * sp[2 * k] + sp[2 * k + 1] * sp[2 * k + 1]) * g;
                }
                else
                {
                    p[0] += sp[0] * sp[0] * g; p[Kn - 1] += sp[1] * sp[1] * g;
                    for (int k = 1; k < Kn - 1; ++k) p[k] += (sp[2 * k] * sp[2 * k] + sp[2 * k + 1] * sp[2 * k + 1]) * g;
                }
                blockWork += fftWork(N) + 2.0 * N;
            }
            return;
        }
        const int w = v - 2 * f.ns;
        if (w == 0)
        {
            // keep each bin's loudness, give it a random phase
            const float* T = tilt.data();
            const float* p = pw.data();
            for (int k = 1; k < Kn - 1; ++k)
            {
                float m = std::sqrt(p[k]) * T[k];
                if (m < 1e-20f) m = 0.0f;
                const float ph = (float)nextRand();
                sp[2 * k] = m * sinT(ph + 0.25f); sp[2 * k + 1] = m * sinT(ph);
            }
            nextRand();
            sp[0] = 0.0f; sp[1] = 0.0f;   // no DC, no Nyquist
            blockWork += 6.0 * N;
        }
        else if (w == 1)
        {
            fft[wi].inverse(sp, xx);   // unscaled (x N)
            blockWork += fftWork(N);
        }
        else
        {
            // random phases scatter each frame's energy, so overlapped frames add as power: x4/3 keeps the level
            // (Hann in and out, hop N/4: 1/sqrt(sum w^2 / N * sum of overlapped w^2) = 1/sqrt(3/8 * 3/2));
            // the inverse transform is unscaled, so it also divides by N
            const float g = (float)(4.0 / 3.0 / (double)N);
            const float* hw = hann[wi].data();
            float* a = acc[ci].data() + H;
            for (int i = 0; i < N; ++i) a[i] += xx[i] * g * hw[i];
            blockWork += N;
        }
    }

    // ---- state ----
    Settings set;
    double sr = 48000.0;
    int sf = 1, size[kNumWindows] = {}, NM = 0, K = 0, cap = 0, J = 0;
    std::vector<float> ring[2], hold[2], acc[2], hann[kNumWindows], pw, tilt, ln, sinTab;
    RealFFT fft[kNumWindows];
    AlignedBuffer x, spec;
    int64_t wpos = 0;
    int H = 512, cnt = 0;
    double cen = 0.0, oldCen = 0.0, hc = 0.0;
    int srcA = 0, srcB = 0, xf = 0, xfn = 0;
    bool frozen = false, jumpReq = false, quiet = true;
    double catchTo = -INFINITY, fe[2] = { 0.0, 0.0 };
    int u = 0, nu = 0;
    Frame fr;
    uint32_t rng = 0x6d2b79f5u;
    double tiltV = NAN;
    int tiltN = 0;
    double mixNow = -1.0, gainNow = 1.0, mixCoef = 0.002;
    bool prevPlaying = false, havePrev = false;
    double prevPpq = 0.0;
    std::atomic<bool> jumpRequest { false };
    int blockUnits = 0;
    double blockWork = 0.0;
    uint32_t jumpCount = 0;

    // the recording's picture
    int colLen = 1, histCount = 0;
    float histPk = 0.0f;
    int64_t histIdx = 0;
    std::unique_ptr<std::atomic<float>[]> histPeak;
    std::atomic<int64_t> histCols { 0 };
    std::atomic<float> histNow { 0.0f };

    // telemetry
    std::atomic<int64_t> visWritten { 0 };
    std::atomic<double> visCen { 0.0 }, visHold { 0.0 };
    std::atomic<int> visFrozen { 0 }, visQuiet { 0 }, visN { 2048 };
    std::atomic<uint32_t> visJumps { 0 };
};

}} // namespace pump::stretch
