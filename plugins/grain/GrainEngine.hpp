// pump GRAIN engine: real-time granular effect for clouds, glitches and pitched shimmer. Framework-free.
//
// Capture: the input is written continuously into a stereo ring (10.9-11.9 s at the usual rates, allocated in
// prepare()), with two band-limited copies beside it (6th-order low pass at 0.2 x the rate, kept at 1/2 and 1/4 of
// the rate). A grain pitched up reads the copy that suits its ratio: up to +7 semitones the full-band ring (only
// source content above ~16 kHz can fold back), up to +17 the half-rate copy, above that the quarter-rate copy (what a
// copy loses on top would have landed above ~14 kHz at 48 kHz; nothing audible is lost at 96 kHz). FREEZE fades the writing out over 5 ms and stops the head once a few ms of silence follow
// the fade, so grains that run into the newest audio fade out with it instead of reaching a step; releasing it starts
// the head at once and fades the writing back in.
//
// Grains: up to 64 voices. Each reads the ring with cubic (Hermite) interpolation, both channels, starting POSITION
// behind the write head plus SPRAY (0..1 s, always further back), for SIZE ms, at a pitch ratio from PITCH + a random
// PITCH SPREAD + motion, optionally QUANTIZED to a scale relative to the source (Chromatic, Major, Minor, Pentatonic,
// Octaves, Fifths), backwards with REVERSE probability (a reverse grain starts at its position and reads into the
// past), and panned by STEREO SPREAD (a grain narrows toward a point as it is panned out; equal power for its mono
// part). A grain pitched up starts far enough back that it never overtakes the write head; when FREEZE stops the
// head, grains that would now run into it fade out before they do.
// Envelopes (all click-free: no edge is shorter than 1.5 ms): Smooth (Hann), Soft (flat top, quarter-length cosine
// edges), Percussive (fast attack, quadratic decay), Swell (quadratic rise, short release), Square (flat, 1.5 ms
// edges).
// Scheduling: DENSITY grains per second (spray also jitters the timing, up to +-22 %), or with SYNC one grain per note
// division, locked sample-accurately to the host's grid while the transport plays (following loops and jumps) and
// free-running at the host tempo (120 BPM if there is none) while it doesn't.
// Level: every grain is scaled for the overlap it plays in (grains per second x size): 1 / sqrt(overlap x mean env^2)
// when the grains are uncorrelated (spray, pitch spread, a pitch shift and reverse all decorrelate them), 1 / (overlap
// x mean env) when they read the same audio in step (spray 0, no shift); a coherence estimate blends the two, so
// density and size changes don't jump in level.
// MORPH X (smooth .. glitchy) and MORPH Y (steady .. wild) offset the base controls: X trades size against density
// and adds spray toward glitchy; Y raises spray, pitch spread, reverse, stereo spread and motion toward wild and takes
// them all to 0 at the steady edge (the centre leaves the controls as they are). MOTION: three slow, smooth random
// curves (RATE, DEPTH) move position, size and pitch.
//
// Feedback: the grain sum (DC-blocked, gently low-passed) is added to what is written into the ring, for evolving,
// self-similar clouds; with a pitch shift each pass climbs another step (shimmer). A level-keyed duck holds what is
// written near -6 dBFS (the feedback steps back while the buffer is louder, never the input), and a soft limit to +-1
// catches the rest, so even 100 % sustains at a steady level instead of building up.
// FX after the grains: a resonant TPT state-variable FILTER (off / low / band / high pass; modes crossfade, cutoff
// glides, low/high pass peaks are gain-compensated), then SPACE: input diffusion (three allpasses per side)
// into an 8-line modulated feedback delay network (Hadamard mixing, damped lines); 0..100 % sets the send and the
// decay (0.6..5 s). MIX (equal power) with the untouched input (latency 0: grains only read the past), OUTPUT.
//
// Feeds for the UI: a ring of peaks of what is written (one per ~1.33 ms) and a lock-free snapshot of the live grains
// and of the last 32 that ended (short grains often live and die between two UI frames), a fixed array under a
// sequence counter, written once per block. Engine stats (grains started and not started, a log of the last 1024
// starts) are for the tests.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/Tempo.hpp"
#include "GrainParams.hpp"
#include <atomic>
#include <climits>

namespace pump { namespace grain {

constexpr int kMaxGrains = 64;
constexpr int kMaxRecent = 32;

struct Settings {
    double positionMs = 250.0, sizeMs = 120.0, density = 12.0, spray = 10.0;
    bool sync = false;
    int division = kNote16, envelope = kEnvSmooth;
    double reverse = 0.0, stereo = 40.0, pitch = 0.0, pitchSpread = 0.0;
    int quantize = kScaleOff;
    double feedback = 0.0;
    int filter = kFilterOff;
    double cutoff = 2000.0, resonance = 20.0, space = 25.0, motionRate = 0.25, motionDepth = 20.0;
    double morphX = 0.0, morphY = 0.0;
    bool freeze = false;
    double mix = 50.0, outDb = 0.0;
};

// ---- the macros ------------------------------------------------------------------------------------------------------
// The grain controls after MORPH X / Y (shared by the engine and the UI's pad readout). Fractions are 0..1.
struct Shaped { double sizeMs, density, spray, reverse, stereo, pitchSpread, motionDepth; };
inline Shaped applyMacros(const Settings& s)
{
    const double x = clampv(s.morphX * 0.01, -1.0, 1.0), y = clampv(s.morphY * 0.01, -1.0, 1.0);
    Shaped o;
    // X: smooth (long, few, steady grains) .. glitchy (short, dense, scattered)
    o.sizeMs = clampv(s.sizeMs * std::exp2(x > 0.0 ? -3.0 * x : -1.5 * x), 5.0, 1000.0);
    o.density = clampv(s.density * std::exp2(x > 0.0 ? 2.5 * x : 1.5 * x), 1.0, 100.0);
    double spray = clampv(s.spray * 0.01, 0.0, 1.0);
    spray = x > 0.0 ? spray + 0.35 * x * (1.0 - spray) : spray * (1.0 + 0.8 * x);
    double rev = clampv(s.reverse * 0.01, 0.0, 1.0), st = clampv(s.stereo * 0.01, 0.0, 1.0);
    double spread = clampv(s.pitchSpread, 0.0, 24.0), md = clampv(s.motionDepth * 0.01, 0.0, 1.0);
    // Y: steady (nothing random) .. wild (more spray, pitch spread, reverse, width, motion)
    if (y > 0.0)
    {
        spray += 0.5 * y * (1.0 - spray);
        rev += 0.5 * y * (1.0 - rev);
        st += 0.6 * y * (1.0 - st);
        spread += 12.0 * y * y;
        md += 0.5 * y * (1.0 - md);
    }
    else
    {
        const double k = 1.0 + y;
        spray *= k; rev *= k; spread *= k; md *= k;
        st *= 1.0 + 0.5 * y;
    }
    o.spray = clampv(spray, 0.0, 1.0);
    o.reverse = clampv(rev, 0.0, 1.0);
    o.stereo = clampv(st, 0.0, 1.0);
    o.pitchSpread = clampv(spread, 0.0, 24.0);
    o.motionDepth = clampv(md, 0.0, 1.0);
    return o;
}

// ---- pitch quantising ------------------------------------------------------------------------------------------------
// Allowed pitch classes (bit n = n semitones above the source) of each scale.
static const uint16_t kScaleMask[kNumScales] = { 0xFFF, 0xFFF, 0xAB5, 0x5AD, 0x295, 0x001, 0x081 };
inline double quantizeSemis(double st, int scale)
{
    if (scale <= kScaleOff || scale >= kNumScales) return st;
    if (scale == kScaleChromatic) return std::round(st);
    const int c0 = (int)std::lround(st);
    double best = 0.0, bestD = 1e9;
    for (int c = c0 - 12; c <= c0 + 12; ++c)
    {
        const int pc = ((c % 12) + 12) % 12;
        if (!(kScaleMask[scale] & (1u << pc))) continue;
        const double d = std::fabs(c - st);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
}

// ---- grain envelopes -------------------------------------------------------------------------------------------------
// sin^2 edge (0 -> 1 over x = 0..1), tabulated
constexpr int kEdgeN = 512;
inline const float* edgeTable()
{
    static const struct T { float v[kEdgeN + 2]; T() { for (int i = 0; i <= kEdgeN + 1; ++i) { const double s = std::sin(0.5 * kPi * std::min(1.0, (double)i / kEdgeN)); v[i] = (float)(s * s); } } } t;
    return t.v;
}
inline float edge(const float* tab, float x)
{
    const float f = clampv(x, 0.0f, 1.0f) * (float)kEdgeN;
    const int i = std::min((int)f, kEdgeN - 1);
    return tab[i] + (tab[i + 1] - tab[i]) * (f - (float)i);
}
enum Body : int { kBodyFlat = 0, kBodyDecay, kBodyRise };
struct EnvSpec { int atk, rel, body; double mean, meanSq; };   // edges in samples; mean and mean square over the grain
inline EnvSpec envSpec(int shape, int len, double sr)
{
    const int e = std::max(8, (int)(0.0015 * sr));   // the shortest edge: 1.5 ms
    const int half = len / 2;
    auto edgeLen = [&](int want) { return clampv(want, std::min(e, half), half); };
    EnvSpec s { 1, 1, kBodyFlat, 1.0, 1.0 };
    switch (shape)
    {
    case kEnvSoft: s.atk = s.rel = edgeLen(len / 4); break;
    case kEnvPerc: s.atk = edgeLen(len / 50); s.rel = edgeLen(e); s.body = kBodyDecay; break;
    case kEnvSwell: s.atk = edgeLen(e); s.rel = edgeLen(len / 12); s.body = kBodyRise; break;
    case kEnvSquare: s.atk = s.rel = edgeLen(len / 50); break;
    case kEnvSmooth:
    default: s.atk = half; s.rel = len - half; break;   // Hann
    }
    s.atk = std::max(1, s.atk); s.rel = std::max(1, s.rel);
    const double L = std::max(1, len);
    if (s.body == kBodyFlat)
    {
        // a sin^2 edge has mean 1/2 and mean square 3/8
        s.mean = 1.0 - 0.5 * (s.atk + s.rel) / L;
        s.meanSq = 1.0 - 0.625 * (s.atk + s.rel) / L;
    }
    else { s.mean = 1.0 / 3.0; s.meanSq = 0.2; }   // (1 - t)^2 or t^2
    return s;
}

// ---- the UI snapshot -------------------------------------------------------------------------------------------------
// Written by the audio thread once per block, read by the UI: a sequence counter around a fixed set of atomics (odd =
// being written). The reader retries if the counter moved; nothing ever waits.
struct GrainView {
    struct G { float from, to, head, env, pitch, reverse; };   // delays behind the write head (seconds)
    // a grain that ended: where it read (seconds behind the head when it ended), and the head then (samples written)
    struct E { float from, to, pitch, reverse; uint32_t head; };
    int count = 0;
    uint32_t ended = 0;         // grains ended so far; the last kMaxRecent are in e[], index = number % kMaxRecent
    uint32_t head = 0;          // samples written so far (stands still while frozen)
    G g[kMaxGrains] = {};
    E e[kMaxRecent] = {};
    float hopPhase = 0.0f;      // the newest buffer peak ends this many seconds before the write head
    float position = 0.0f, spray = 0.0f, rate = 0.0f;   // where grains start (s), the spray (s), grains per second
    float motion[3] = {};       // the motion curves (position, size, pitch), -1..1 x depth
    float frozen = 0.0f;
};
class GrainSnapshot {
public:
    static constexpr int kHdr = 8, kGF = 6, kEF = 4, kEOff = kHdr + kMaxGrains * kGF;
    GrainSnapshot() { for (auto& d : data) d.store(0.0f, std::memory_order_relaxed); }
    void publish(const GrainView& v)
    {
        const uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        ints[0].store((uint32_t)v.count, std::memory_order_relaxed);
        ints[1].store(v.ended, std::memory_order_relaxed);
        ints[2].store(v.head, std::memory_order_relaxed);
        const float hdr[kHdr] = { v.hopPhase, v.position, v.spray, v.rate, v.motion[0], v.motion[1], v.motion[2], v.frozen };
        for (int i = 0; i < kHdr; ++i) data[i].store(hdr[i], std::memory_order_relaxed);
        for (int k = 0; k < v.count; ++k)
        {
            const float f[kGF] = { v.g[k].from, v.g[k].to, v.g[k].head, v.g[k].env, v.g[k].pitch, v.g[k].reverse };
            for (int i = 0; i < kGF; ++i) data[kHdr + k * kGF + i].store(f[i], std::memory_order_relaxed);
        }
        for (int k = 0; k < kMaxRecent; ++k)
        {
            const float f[kEF] = { v.e[k].from, v.e[k].to, v.e[k].pitch, v.e[k].reverse };
            for (int i = 0; i < kEF; ++i) data[kEOff + k * kEF + i].store(f[i], std::memory_order_relaxed);
            endHead[k].store(v.e[k].head, std::memory_order_relaxed);
        }
        seq.store(s + 2, std::memory_order_release);
    }
    bool read(GrainView& v) const
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t s0 = seq.load(std::memory_order_acquire);
            if (s0 & 1u) continue;
            v.count = clampv((int)ints[0].load(std::memory_order_relaxed), 0, kMaxGrains);
            v.ended = ints[1].load(std::memory_order_relaxed);
            v.head = ints[2].load(std::memory_order_relaxed);
            float hdr[kHdr];
            for (int i = 0; i < kHdr; ++i) hdr[i] = data[i].load(std::memory_order_relaxed);
            v.hopPhase = hdr[0]; v.position = hdr[1]; v.spray = hdr[2]; v.rate = hdr[3];
            v.motion[0] = hdr[4]; v.motion[1] = hdr[5]; v.motion[2] = hdr[6]; v.frozen = hdr[7];
            for (int k = 0; k < v.count; ++k)
            {
                float f[kGF];
                for (int i = 0; i < kGF; ++i) f[i] = data[kHdr + k * kGF + i].load(std::memory_order_relaxed);
                v.g[k] = { f[0], f[1], f[2], f[3], f[4], f[5] };
            }
            for (int k = 0; k < kMaxRecent; ++k)
            {
                float f[kEF];
                for (int i = 0; i < kEF; ++i) f[i] = data[kEOff + k * kEF + i].load(std::memory_order_relaxed);
                v.e[k] = { f[0], f[1], f[2], f[3], endHead[k].load(std::memory_order_relaxed) };
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq.load(std::memory_order_relaxed) == s0) return true;
        }
        return false;
    }
private:
    std::atomic<uint32_t> seq { 0 };
    std::atomic<uint32_t> ints[3] = {};
    std::atomic<uint32_t> endHead[kMaxRecent] = {};
    std::atomic<float> data[kEOff + kMaxRecent * kEF];
};

// ---- SPACE: a compact modulated FDN ----------------------------------------------------------------------------------
class Space {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        static const double ms[8] = { 31.7, 37.3, 41.9, 47.3, 53.1, 59.7, 67.3, 73.9 };
        static const double rates[8] = { 0.31, 0.43, 0.53, 0.61, 0.71, 0.79, 0.89, 0.97 };
        modDepth = 0.0003 * sr;
        for (int i = 0; i < 8; ++i)
        {
            len[i] = ms[i] * 0.001 * sr;
            line[i].setMax((int)(len[i] + 2.0 * modDepth) + 8);
            const double w = kTwoPi * rates[i] / sr;
            rc[i] = std::cos(w); rs[i] = std::sin(w);
            const double ph = kTwoPi * i / 8.0;
            pc[i] = std::cos(ph); ps[i] = std::sin(ph);
        }
        static const double dms[2][3] = { { 4.3, 7.1, 11.9 }, { 5.1, 8.3, 12.7 } };
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 3; ++j) { apLen[c][j] = std::max(2, (int)(dms[c][j] * 0.001 * sr)); ap[c][j].setMax(apLen[c][j] + 2); }
        clear();
        amount = -1.0;
        setAmount(0.0);
    }
    void clear()
    {
        for (auto& l : line) l.clear();
        for (auto& c : ap) for (auto& a : c) a.clear();
        for (auto& d : damp) d.reset();
    }
    // 0..1: the decay (0.6..5 s) and the lines' damping
    void setAmount(double a)
    {
        if (std::fabs(a - amount) < 1e-4) return;
        amount = a;
        const double t60 = 0.6 + 4.4 * a * std::sqrt(a);
        double g2 = 0.0;
        for (int i = 0; i < 8; ++i) { g[i] = std::pow(10.0, -3.0 * len[i] / (t60 * sr)); g2 += g[i] * g[i]; }
        for (auto& d : damp) d.set(9000.0 - 3500.0 * a, sr);
        // steady-state level of uncorrelated lines = input level x kIn / sqrt(1 - g^2): normalise to unity
        norm = std::sqrt(std::max(1e-4, 1.0 - g2 / 8.0)) / kIn;
    }
    // per sub-block: keep the LFO phasors on the unit circle
    void renormalise()
    {
        for (int i = 0; i < 8; ++i) { const double k = 1.5 - 0.5 * (pc[i] * pc[i] + ps[i] * ps[i]); pc[i] *= k; ps[i] *= k; }
    }
    inline void process(double inL, double inR, double& outL, double& outR)
    {
        double dl = inL, dr = inR;
        for (int j = 0; j < 3; ++j) { dl = ap[0][j].processInt((float)dl, apLen[0][j], 0.6f); dr = ap[1][j].processInt((float)dr, apLen[1][j], 0.6f); }
        double y[8];
        for (int i = 0; i < 8; ++i)
        {
            const double c = pc[i] * rc[i] - ps[i] * rs[i];
            ps[i] = ps[i] * rc[i] + pc[i] * rs[i];
            pc[i] = c;
            y[i] = damp[i].lp(line[i].readLinear(len[i] - 1.0 + modDepth * (1.0 + ps[i]))) * g[i];
        }
        double oL = 0.0, oR = 0.0;
        for (int i = 0; i < 8; ++i) { oL += (i & 1) ? -y[i] : y[i]; oR += (i & 2) ? -y[i] : y[i]; }
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += 2 * h)
                for (int j = i; j < i + h; ++j) { const double a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        for (int i = 0; i < 8; ++i) line[i].push((float)(y[i] * 0.35355339059 + ((i & 1) ? dr : dl) * kIn));
        outL = oL * 0.35355339059 * norm;
        outR = oR * 0.35355339059 * norm;
    }
    double state() const { return line[0].at(0) + line[7].at(0) + damp[0].s + damp[7].s; }
private:
    static constexpr double kIn = 0.5;
    double sr = 48000.0, modDepth = 14.0, amount = -1.0, norm = 1.0;
    double len[8] = {}, g[8] = {}, rc[8] = {}, rs[8] = {}, pc[8] = {}, ps[8] = {};
    fx::FracDelay line[8];
    fx::OnePole damp[8];
    fx::Allpass ap[2][3];
    int apLen[2][3] = {};
};

// ---- the engine ------------------------------------------------------------------------------------------------------
class Engine {
public:
    static constexpr int kSub = 32;            // grains are rendered in sub-blocks of this many samples
    static constexpr int kMargin = 128;        // closest a grain starts behind the write head (base samples)
    static constexpr int kSafe = 64;           // closest it may ever read: a sub-block, the interpolation taps and
                                               // the band-limited copies' filter delay (a grain fades out before)
    static constexpr int kLogN = 1024;

    struct SpawnInfo { int64_t time; float ratio, delay, size; bool reverse; };   // delay, size in samples

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        tab = edgeTable();
        int n0 = 1 << 12;
        while (n0 < (int)std::ceil(sr * 10.5)) n0 <<= 1;
        for (int k = 0; k < 3; ++k) { ring[k].assign((size_t)(2 * (n0 >> k)), 0.0f); mask[k] = (int64_t)(n0 >> k) - 1; }
        maxD = (double)n0 - 0.02 * sr - 64.0;   // headroom past the far end for a grain's last fade
        // band-limiting filters of the copies (6th-order Butterworth at 0.2 x the rate each copy is filtered at)
        static const double q[3] = { 0.51763809, 0.70710678, 1.93185165 };
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 3; ++s) { lpHalf[ch][s].set(0.2 * sr, q[s], sr); lpQuarter[ch][s].set(0.1 * sr, q[s], 0.5 * sr); }
        // the copies' delay at low frequencies, in base samples (sum of 1 / (Q w) over the sections)
        const double gdSec = (1.0 / q[0] + 1.0 / q[1] + 1.0 / q[2]) / (kTwoPi * 0.2);
        groupDelay[0] = 0.0; groupDelay[1] = gdSec; groupDelay[2] = gdSec + 2.0 * gdSec;
        hop = std::max(16, (int)std::lround(sr / 750.0));
        for (int ch = 0; ch < 2; ++ch) { fbDc[ch].set(15.0, sr); fbLp[ch].set(std::min(16000.0, 0.4 * sr), sr); }
        duckAtt = 1.0 - tauCoef(0.002, sr);
        duckRel = 1.0 - tauCoef(0.400, sr);
        duckGlide = 1.0 - tauCoef(0.010, sr);
        space.prepare(sr);
        const int ramp = std::max(1, (int)(0.020 * sr));
        mixS.setRamp(ramp); outS.setRamp(ramp); fbS.setRamp(ramp); spaceS.setRamp(ramp); cutS.setRamp(std::max(1, (int)(0.030 * sr)));
        resS.setRamp(ramp);
        for (auto& w : fw) w.setRamp(ramp);
        writeG.setRamp(std::max(1, (int)(0.005 * sr)));
        motion[0].setup(sr, 0x1234567u); motion[1].setup(sr, 0x2345671u); motion[2].setup(sr, 0x3456712u);
        rng.seed(0x9E3779B9u); timeRng.seed(0x7F4A7C15u);
        reset();
    }

    // ---- settings and transport (audio thread, block start) ----
    void setSettings(const Settings& s) { set = s; }
    void setTransport(const Transport& t) { tr = t; }
    int latency() const { return 0; }

    // ---- feeds and stats ----
    pump::ScopeRing wave { 1 << 13 };      // peaks of what is written (one per hop); the UI's buffer display
    GrainSnapshot snapshot;
    double hopSeconds() const { return hop / sr; }
    uint64_t spawnCount() const { return spawned; }
    uint64_t droppedCount() const { return dropped; }
    int activeGrains() const { return numActive; }
    const SpawnInfo& spawnLog(uint64_t i) const { return history[i % kLogN]; }

    void process(float* L, float* R, int n)
    {
        beginBlock();
        for (int off = 0; off < n; off += kSub) processSub(L + off, R + off, std::min(kSub, n - off));
        endBlock(L, R, n);
    }

private:
    struct Grain {
        double pos = 0.0, step = 1.0;          // absolute source position (base samples) and its step per sample
        int len = 0, k = 0, atk = 1, rel = 1, body = kBodyFlat, level = 0, skip = 0;
        float invAtk = 1, invRel = 1, invLen = 1;
        float kg = 1.0f, kd = 0.0f;            // an early fade-out (gain, decrement per sample) when one is needed
        float gmL = 1, gmR = 1, gs = 1;        // pan gains (mono part per side, side part) incl. the level scaling
        float pitch = 0;
    };

    void reset()
    {
        for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
        W = 0; clock = 0;
        numActive = 0;
        for (int i = 0; i < kMaxGrains; ++i) freeList[i] = kMaxGrains - 1 - i;
        numFree = kMaxGrains;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& f : lpHalf[ch]) f.reset();
            for (auto& f : lpQuarter[ch]) f.reset();
            fbDc[ch].reset(); fbLp[ch].reset(); svf[ch].reset();
        }
        space.clear();
        spaceIdle = true;
        countdown = 0.0; gridLocked = false; lastGrid = INT64_MIN; nextGrid = 0;
        moving = !set.freeze;
        zeroRun = set.freeze ? kMargin : 0;
        spDry = 1.0; spWet = 0.0;
        writeG.reset(set.freeze ? 0.0 : 1.0);
        mixS.reset(set.mix * 0.01); outS.reset(dbToGain(set.outDb)); fbS.reset(fbGain(set.feedback));
        spaceS.reset(set.space * 0.01); cutS.reset(std::log2(clampv(set.cutoff, 20.0, 20000.0))); resS.reset(resQ(set.resonance));
        for (int m = 0; m < kNumFilterModes; ++m) fw[m].reset(m == clampv(set.filter, 0, kNumFilterModes - 1) ? 1.0 : 0.0);
        mixDry = std::cos(0.5 * kPi * set.mix * 0.01); mixWet = std::sin(0.5 * kPi * set.mix * 0.01);
        peak = 0.0f; hopN = 0;
        envW = 0.0; duck = 1.0;
        spawned = dropped = 0;
    }

    static double fbGain(double pct) { return std::pow(clampv(pct * 0.01, 0.0, 1.0), 0.7); }
    static double resQ(double pct) { return 0.6 * std::pow(25.0, clampv(pct * 0.01, 0.0, 1.0)); }
    static float softLimit(float x)
    {
        const float a = std::fabs(x);
        if (a <= 0.5f) return x;
        const float y = 0.5f + 0.5f * fastTanh((a - 0.5f) * 2.0f);
        return x < 0.0f ? -y : y;
    }

    // grains per second right now (free or synced) and the grid length in samples
    double grainRate() const { return set.sync ? sr / gridSamples : sh.density; }

    void beginBlock()
    {
        sh = applyMacros(set);
        const double qn = noteQuarters(set.division);
        gridSamples = std::max(16.0, qn * 60.0 / clampv(tr.bpm, 20.0, 999.0) * sr);
        if (set.sync && tr.playing && tr.valid)
        {
            // the next grid line at or after the block start (within half a sample: hosts round the position);
            // never one already started, unless the transport jumped back (a loop)
            const double g = tr.ppq / qn;
            int64_t next = (int64_t)std::ceil(g - 0.5 / gridSamples);
            if (gridLocked && next <= lastGrid)
            {
                if ((double)lastGrid - g > 1.5) lastGrid = next - 1;
                else next = lastGrid + 1;
            }
            nextGrid = next;
            countdown = std::max(0.0, ((double)next - g) * gridSamples);
            gridLocked = true;
        }
        else
        {
            gridLocked = false;
            // a faster rate takes effect at once (the longest interval it can draw, so spray doesn't bias the rate)
            countdown = std::min(countdown, set.sync ? gridSamples : sr / sh.density * (1.0 + 0.45 * sh.spray));
        }
        mixS.setTarget(clampv(set.mix * 0.01, 0.0, 1.0));
        outS.setTarget(dbToGain(set.outDb));
        fbS.setTarget(fbGain(set.feedback));
        spaceS.setTarget(clampv(set.space * 0.01, 0.0, 1.0));
        cutS.setTarget(std::log2(clampv(set.cutoff, 20.0, 20000.0)));
        resS.setTarget(resQ(set.resonance));
        const int fm = clampv(set.filter, 0, kNumFilterModes - 1);
        for (int m = 0; m < kNumFilterModes; ++m) fw[m].setTarget(m == fm ? 1.0 : 0.0);
        writeG.setTarget(set.freeze ? 0.0 : 1.0);
        for (auto& w : motion) w.setRate(clampv(set.motionRate, 0.01, 20.0));
    }

    double nextInterval()
    {
        if (set.sync) return gridSamples;
        const double base = sr / sh.density;
        return base * (1.0 + 0.45 * sh.spray * timeRng.bi());
    }

    // When the head starts or stops, grains that would now run into it (or off the far end) fade out in time.
    void recheck(double h)
    {
        for (int j = 0; j < numActive; ++j)
        {
            Grain& g = voices[active[j]];
            const double D = (double)W - g.pos;
            const double rate = h - g.step;
            const int remain = g.len - g.k;
            double v = remain;
            // toward the head: the audio there is the freeze fade (quiet), so a short fade is enough; toward the far
            // end: at least the shortest envelope edge (the ring has room for it)
            if (rate < 0.0) v = std::max(1.0, (D - kSafe) / -rate);
            else if (rate > 0.0) v = std::max((maxD - D) / rate, 0.0015 * sr);
            if (v < remain) g.kd = std::max(g.kd, g.kg / (float)std::max(1.0, v));
        }
    }

    void spawn(int at, double h)
    {
        if (numActive >= kMaxGrains || numFree <= 0) { ++dropped; return; }
        const double md = sh.motionDepth;
        double sizeMs = clampv(sh.sizeMs * std::exp2(md * mot[1]), 5.0, 1000.0);
        const int len = std::max(16, (int)(sizeMs * 0.001 * sr));
        double st = set.pitch + sh.pitchSpread * rng.bi() + 0.5 * md * mot[2];
        st = clampv(quantizeSemis(clampv(st, -24.0, 24.0), set.quantize), -24.0, 24.0);
        const double r = std::exp2(st / 12.0);
        const bool rev = rng.uni() < sh.reverse;
        const double step = rev ? -r : r;
        double d0 = (set.positionMs + md * mot[0] * (0.5 * set.positionMs + 150.0)) * 0.001 * sr + sh.spray * sr * rng.uni();
        // keep the whole grain inside the valid part of the ring: never ahead of the head, never off the far end
        const double rate = h - step;                 // how fast its delay changes
        if (rate < 0.0) d0 = std::max(d0, -rate * len + kMargin);
        d0 = clampv(d0, (double)kMargin, maxD - std::max(0.0, rate) * len);
        const double pan = sh.stereo * rng.bi();

        Grain& g = voices[freeList[--numFree]];
        active[numActive++] = (int)(&g - voices);
        g.pos = (double)W + at * h - d0;   // (the input of sample `at` is written at W + at)
        g.step = step;
        g.len = len; g.k = 0; g.skip = at;
        g.level = r <= 1.4984 ? 0 : (r <= 2.6697 ? 1 : 2);   // +7 / +17 semitones
        const EnvSpec es = envSpec(set.envelope, len, sr);
        g.atk = es.atk; g.rel = es.rel; g.body = es.body;
        g.invAtk = 1.0f / (float)es.atk; g.invRel = 1.0f / (float)es.rel; g.invLen = 1.0f / (float)len;
        g.kg = 1.0f; g.kd = 0.0f;
        // level: scaled for the overlap it plays in, between the uncorrelated (power) and in-step (amplitude) sums
        const double rateHz = grainRate();
        const double N = std::min((double)kMaxGrains, rateHz * len / sr);
        const double rBase = std::exp2(clampv(set.pitch, -24.0, 24.0) / 12.0);
        const double c = std::exp(-sh.spray * 1000.0 / 4.0) * std::exp(-sh.pitchSpread / 0.15)
                       * std::exp(-std::fabs(1.0 - rBase) * (1000.0 / rateHz) / 4.0) * std::exp(-8.0 * md)
                       * (1.0 - sh.reverse) * (1.0 - sh.reverse);
        const double gInc = 1.0 / std::sqrt(std::max(1.0, N * es.meanSq));
        const double gCoh = 1.0 / std::max(1.0, N * es.mean);
        const double gain = std::pow(gInc, 1.0 - c) * std::pow(gCoh, c);
        const double th = (pan + 1.0) * 0.25 * kPi;
        g.gmL = (float)(kSqrt2 * std::cos(th) * gain);
        g.gmR = (float)(kSqrt2 * std::sin(th) * gain);
        g.gs = (float)((1.0 - std::fabs(pan)) * gain);
        g.pitch = (float)st;
        SpawnInfo& si = history[spawned % kLogN];
        si.time = clock + at; si.ratio = (float)r; si.delay = (float)d0; si.size = (float)len; si.reverse = rev;
        ++spawned;
    }

    inline float envelope(const Grain& g, int k) const
    {
        float e = 1.0f;
        if (k < g.atk) e = edge(tab, (float)k * g.invAtk);
        const int fromEnd = g.len - k;
        if (fromEnd < g.rel) e *= edge(tab, (float)fromEnd * g.invRel);
        if (g.body == kBodyDecay) { const float u = 1.0f - (float)k * g.invLen; e *= u * u; }
        else if (g.body == kBodyRise) { const float u = (float)k * g.invLen; e *= u * u; }
        return e;
    }
    static inline float cubic(float ym1, float y0, float y1, float y2, float t)
    {
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    void write(float wl, float wr)
    {
        const int64_t a = W;
        float* p0 = ring[0].data() + 2 * (a & mask[0]);
        p0[0] = wl; p0[1] = wr;
        double y[2] = { wl, wr };
        for (int ch = 0; ch < 2; ++ch) for (auto& f : lpHalf[ch]) y[ch] = f.lowpass(y[ch]);
        if ((a & 1) == 0)
        {
            float* p1 = ring[1].data() + 2 * ((a >> 1) & mask[1]);
            p1[0] = (float)y[0]; p1[1] = (float)y[1];
            for (int ch = 0; ch < 2; ++ch) for (auto& f : lpQuarter[ch]) y[ch] = f.lowpass(y[ch]);
            if ((a & 3) == 0)
            {
                float* p2 = ring[2].data() + 2 * ((a >> 2) & mask[2]);
                p2[0] = (float)y[0]; p2[1] = (float)y[1];
            }
        }
        peak = std::max(peak, std::max(std::fabs(wl), std::fabs(wr)));
        if (++hopN >= hop) { wave.push(peak); peak = 0.0f; hopN = 0; }
        ++W;
    }

    void processSub(float* L, float* R, int n)
    {
        // the head stops once the freeze fade has finished and kMargin samples of silence follow it; it moves again
        // as soon as freeze is released
        const bool nowMoving = !(set.freeze && !writeG.isSmoothing() && writeG.current() <= 0.0 && zeroRun >= kMargin);
        if (nowMoving != moving) { moving = nowMoving; recheck(moving ? 1.0 : 0.0); }
        const double h = moving ? 1.0 : 0.0;
        for (int m = 0; m < 3; ++m) mot[m] = motion[m].advance(n);

        // ---- spawn the grains that start in this sub-block
        while (countdown < n)
        {
            const int at = countdown <= 0.0 ? 0 : (int)std::floor(countdown + 0.5);
            if (at >= n) break;
            spawn(at, h);
            if (gridLocked) { lastGrid = nextGrid++; countdown += gridSamples; }
            else countdown += nextInterval();
        }
        countdown -= n;

        // ---- render the grains
        float accL[kSub], accR[kSub];
        std::fill(accL, accL + n, 0.0f);
        std::fill(accR, accR + n, 0.0f);
        for (int j = numActive - 1; j >= 0; --j)
        {
            Grain& g = voices[active[j]];
            const int lv = g.level;
            const float* b = ring[lv].data();
            const int64_t m = mask[lv];
            const double sc = lv == 0 ? 1.0 : (lv == 1 ? 0.5 : 0.25), off = groupDelay[lv];
            double pos = g.pos;
            int k = g.k;
            float kg = g.kg;
            bool done = false;
            for (int i = g.skip; i < n; ++i)
            {
                float e = envelope(g, k);
                if (g.kd > 0.0f)
                {
                    if (kg <= 0.0f) { done = true; break; }
                    e *= kg;
                    kg -= g.kd;
                }
                const double p = (pos + off) * sc;
                int64_t ii = (int64_t)p;                 // floor (positions are negative before the ring has filled)
                if (p < (double)ii) --ii;
                const float t = (float)(p - (double)ii);
                const float* a0 = b + 2 * ((ii - 1) & m);
                const float* a1 = b + 2 * (ii & m);
                const float* a2 = b + 2 * ((ii + 1) & m);
                const float* a3 = b + 2 * ((ii + 2) & m);
                const float l = cubic(a0[0], a1[0], a2[0], a3[0], t);
                const float r = cubic(a0[1], a1[1], a2[1], a3[1], t);
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
                accL[i] += e * (g.gmL * mid + g.gs * side);
                accR[i] += e * (g.gmR * mid - g.gs * side);
                pos += g.step;
                if (++k >= g.len) { done = true; break; }
            }
            g.pos = pos; g.k = k; g.kg = kg; g.skip = 0;
            if (done)
            {
                // where it read, for the UI's trail (the head moves on within this sub-block: close enough)
                const double D = (double)W - g.pos;
                GrainView::E& en = view.e[view.ended++ % kMaxRecent];
                en.from = (float)((D + g.step * k) / sr); en.to = (float)(D / sr); en.pitch = g.pitch;
                en.reverse = g.step < 0.0 ? 1.0f : 0.0f; en.head = (uint32_t)W;
                freeList[numFree++] = active[j]; active[j] = active[--numActive];
            }
        }

        // ---- per-sample: write (input + feedback), filter, space, mix
        const double q = resS.current();
        svf[0].set(std::exp2(cutS.current()), q, sr);
        svf[1].set(std::exp2(cutS.current()), q, sr);
        const double comp = 1.0 / std::sqrt(std::max(1.0, q));
        const bool filterOn = !(fw[kFilterOff].current() >= 1.0 && !fw[kFilterOff].isSmoothing());
        if (filterOn && filterIdle) { svf[0].reset(); svf[1].reset(); }
        filterIdle = !filterOn;
        const bool spaceOn = spaceS.current() > 0.0 || spaceS.isSmoothing();
        if (spaceOn)
        {
            space.setAmount(spaceS.current());
            space.renormalise();
        }
        else if (!spaceIdle) { space.clear(); spDry = 1.0; spWet = 0.0; }
        spaceIdle = !spaceOn;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            const float gL = accL[i], gR = accR[i];
            const double fb = fbS.next();
            const double wgn = writeG.next();
            if (moving)
            {
                const float xl = clampv(inL, -8.0f, 8.0f), xr = clampv(inR, -8.0f, 8.0f);
                float wl = xl, wr = xr;
                if (fb > 0.0)
                {
                    const float k = (float)(fb * duck);
                    wl += softLimit((float)fbLp[0].lp(fbDc[0].process(gL * k)));
                    wr += softLimit((float)fbLp[1].lp(fbDc[1].process(gR * k)));
                    // the duck follows the level written (fast down, slow back up)
                    const double a = std::max(std::fabs(wl), std::fabs(wr));
                    envW += (a - envW) * (a > envW ? duckAtt : duckRel);
                    duck += ((envW > kDuckAt ? kDuckAt / envW : 1.0) - duck) * duckGlide;
                }
                else { envW = 0.0; duck = 1.0; }
                write((float)(wl * wgn), (float)(wr * wgn));
                zeroRun = wgn > 0.0 ? 0 : zeroRun + 1;
            }
            double yl = gL, yr = gR;
            cutS.next(); resS.next();
            if (filterOn)
            {
                double w[kNumFilterModes];
                for (int m = 0; m < kNumFilterModes; ++m) w[m] = fw[m].next();
                double lp, bp, hp;
                svf[0].process(yl, lp, bp, hp);
                yl = w[kFilterOff] * yl + comp * (w[kFilterLP] * lp + w[kFilterHP] * hp) + w[kFilterBP] * bp * svf[0].k;
                svf[1].process(yr, lp, bp, hp);
                yr = w[kFilterOff] * yr + comp * (w[kFilterLP] * lp + w[kFilterHP] * hp) + w[kFilterBP] * bp * svf[1].k;
            }
            else for (auto& f : fw) f.next();
            if (spaceOn)
            {
                if (spaceS.isSmoothing() || spWet == 0.0)
                {
                    const double a = spaceS.next();
                    spDry = std::cos(0.3 * kPi * a); spWet = std::sin(0.45 * kPi * a);
                }
                double sl, sr2;
                space.process(yl, yr, sl, sr2);
                yl = yl * spDry + sl * spWet;
                yr = yr * spDry + sr2 * spWet;
            }
            if (mixS.isSmoothing())
            {
                const double mx = mixS.next();
                mixDry = std::cos(0.5 * kPi * mx); mixWet = std::sin(0.5 * kPi * mx);
            }
            const double og = outS.next();
            L[i] = (float)((inL * mixDry + yl * mixWet) * og);
            R[i] = (float)((inR * mixDry + yr * mixWet) * og);
        }
        clock += n;
    }

    void endBlock(float* L, float* R, int n)
    {
        double chk = svf[0].ic1 + svf[0].ic2 + svf[1].ic1 + svf[1].ic2 + fbDc[0].y1 + fbDc[1].y1 + fbLp[0].s + fbLp[1].s + space.state() + envW + duck;
        for (int i = 0; i < n; ++i) chk += (double)L[i] + R[i];
        if (!std::isfinite(chk))
        {
            reset();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        publish();
    }

    void publish()
    {
        GrainView& v = view;
        const double toS = 1.0 / sr;
        v.count = numActive;
        for (int j = 0; j < numActive; ++j)
        {
            const Grain& g = voices[active[j]];
            const double D = (double)W - g.pos;
            auto& o = v.g[j];
            o.head = (float)(D * toS);
            o.from = (float)((D + g.step * g.k) * toS);
            o.to = (float)((D - g.step * (g.len - g.k)) * toS);
            o.env = envelope(g, std::min(g.k, g.len - 1)) * (g.kd > 0.0f ? std::max(0.0f, g.kg) : 1.0f);
            o.pitch = g.pitch; o.reverse = g.step < 0.0 ? 1.0f : 0.0f;
        }
        v.head = (uint32_t)W;
        v.hopPhase = (float)(hopN * toS);
        const double md = sh.motionDepth;
        v.position = (float)(std::max(0.0, set.positionMs + md * mot[0] * (0.5 * set.positionMs + 150.0)) * 0.001);
        v.spray = (float)sh.spray;
        v.rate = (float)grainRate();
        for (int m = 0; m < 3; ++m) v.motion[m] = (float)(md * mot[m]);
        v.frozen = moving ? 0.0f : 1.0f;
        snapshot.publish(v);
    }

    static constexpr double kSqrt2 = 1.41421356237;
    double sr = 48000.0;
    Settings set;
    Shaped sh = applyMacros(Settings());
    Transport tr;
    const float* tab = nullptr;
    std::vector<float> ring[3];                   // interleaved stereo: full rate, 1/2, 1/4
    int64_t mask[3] = { 0, 0, 0 };
    double groupDelay[3] = { 0, 0, 0 };
    double maxD = 1.0;
    int64_t W = 0, clock = 0;                     // samples written (the head) / processed
    fx::Svf lpHalf[2][3], lpQuarter[2][3];
    Grain voices[kMaxGrains];
    int active[kMaxGrains] = {}, freeList[kMaxGrains] = {};
    int numActive = 0, numFree = 0;
    bool moving = true, gridLocked = false, spaceIdle = true, filterIdle = true;
    double countdown = 0.0, gridSamples = 6000.0;
    int64_t lastGrid = INT64_MIN, nextGrid = 0;
    fx::Rng rng, timeRng;
    fx::Wander motion[3];
    double mot[3] = { 0, 0, 0 };
    fx::DcBlocker fbDc[2];
    fx::OnePole fbLp[2];
    fx::Svf svf[2];
    Space space;
    double spDry = 1.0, spWet = 0.0, mixDry = 1.0, mixWet = 0.0;
    LinearSmoother mixS, outS, fbS, spaceS, cutS, resS, writeG, fw[kNumFilterModes];
    int hop = 64, hopN = 0, zeroRun = 0;
    static constexpr double kDuckAt = 0.5;        // the feedback ducks when what is written goes above -6 dBFS
    double envW = 0.0, duck = 1.0, duckAtt = 0.01, duckRel = 1e-4, duckGlide = 0.002;
    float peak = 0.0f;
    uint64_t spawned = 0, dropped = 0;
    SpawnInfo history[kLogN] = {};
    GrainView view;
};

}} // namespace pump::grain
