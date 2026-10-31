// pump MASTER engine: one-button mastering that stays subtle. Framework-free, header-only.
//
// LEARN: while the song plays, the input is measured (nothing is changed by learning itself):
//   - integrated loudness, loudness range and true peak (BS.1770, the suite's LoudnessMeter);
//   - the long-term spectrum of mid and side in 31 third-octave bands (power average of 8192-point frames, so the
//     loud parts of the song count most; frames under -60 dBFS are skipped);
//   - the level distribution of 100 ms blocks of the compressor's own detector signal (quantiles: how loud the loud
//     parts are, how quiet the quiet ones);
//   - the crest factor of 400 ms windows (how dense the mix already is).
// The result is an Analysis (a few hundred bytes, saved with the session as text).
//
// The PLAN is a pure function of the Analysis, the style and the amounts (computePlan), so the same song and
// settings always give the same master:
//   TONE      the smoothed (one octave) spectrum compared with the style's target curve; a share of the difference,
//             soft-limited (about +-3 dB at medium, boosts smaller than cuts, nothing lifted where there is no
//             music), fitted by least squares onto nine broad filters (two shelves, seven octave-wide bells).
//   DYNAMICS  a slow RMS compressor (low ratio, wide knee, 20 ms attack, 250 ms release, bass-filtered detector)
//             whose threshold sits a few dB under the song's loud parts; already-dense mixes get less.
//   STEREO    side below 120 Hz partly folded to mono when the low end is wide; side above it nudged by at most
//             1 dB toward the style's width (never widening a near-mono mix).
//   LOUDNESS  makeup to the target loudness, predicted from the analysis through the tone and dynamics stages,
//             then LIMIT's true-peak limiter at the ceiling in its dynamic style (brief, peak-only limiting). GENTLE
//             mode adds level only as far as the peaks allow (a couple of dB of peak limiting at most).
// INTENSITY scales every stage (50 % = medium, the reference; 100 % = twice that), each stage's AMOUNT scales it
// again, and each stage can be switched off (smoothly). COMPARE plays the input, delayed and gain-matched to the
// predicted output loudness, so the comparison is about the sound, not the level.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Loudness.hpp"
#include "dsp/Dynamics.hpp"
#include "dsp/Crossover.hpp"
#include "../limit/LimitEngine.hpp"
#include "MasterParams.hpp"
#include <atomic>
#include <cstdio>
#include <string>

namespace pump { namespace master {

constexpr int kBands = 31;          // third octaves, 20 Hz .. 20 kHz
constexpr int kQuantiles = 19;      // 5 %, 10 % .. 95 %
constexpr int kEq = 9;              // the tone stage's filters
constexpr int kCurvePts = 96;       // the tone curve as published for the window
constexpr int kLoss = 49;           // the limiter's loudness loss for peaks driven -10 .. +38 dB over the ceiling
constexpr double kLossFrom = -10.0;

inline double bandFreq(int b) { return 1000.0 * std::pow(2.0, (b - 17) / 3.0); }

// ---------------------------------------------------------------------------------------------------------------
// Analysis

struct Analysis {
    bool valid = false;
    float seconds = 0.0f;
    float lufs = -70.0f, lra = 0.0f, truePeak = -120.0f, crest = 12.0f;
    float mid[kBands] = {}, side[kBands] = {};   // dB per third octave (pink noise reads flat)
    float q[kQuantiles] = {};                    // detector level quantiles of 100 ms blocks, dB
    float st[kQuantiles] = {};                   // short-term loudness quantiles (LUFS, every 100 ms, above -70)
    float loss[kLoss] = {};                      // loudness a peak limiter takes (dB, <= 0) when the song's peaks are
                                                 // driven h = kLossFrom + i dB over its ceiling
};

// The loss curve at h dB over the ceiling (linear between the points; beyond the ends the end points)
inline double lossAt(const Analysis& a, double h)
{
    const double x = clampv(h - kLossFrom, 0.0, (double)(kLoss - 1));
    const int i = std::min((int)x, kLoss - 2);
    const double t = x - i;
    return a.loss[i] + (a.loss[i + 1] - a.loss[i]) * t;
}

inline std::string serializeAnalysis(const Analysis& a)
{
    if (!a.valid) return "";
    std::string s = "pumpmaster1";
    char b[48];
    auto num = [&](const char* k, float v) { std::snprintf(b, sizeof b, ";%s=%.3f", k, v); s += b; };
    num("sec", a.seconds); num("lufs", a.lufs); num("lra", a.lra); num("tp", a.truePeak); num("crest", a.crest);
    auto arr = [&](const char* k, const float* v, int n) {
        s += ";"; s += k; s += "=";
        for (int i = 0; i < n; ++i) { std::snprintf(b, sizeof b, i ? ",%.2f" : "%.2f", v[i]); s += b; }
    };
    arr("mid", a.mid, kBands); arr("side", a.side, kBands); arr("q", a.q, kQuantiles); arr("st", a.st, kQuantiles); arr("loss", a.loss, kLoss);
    return s;
}

// false (and `a` untouched) unless the text is a complete analysis
inline bool parseAnalysis(const char* text, Analysis& a)
{
    if (!text || std::strncmp(text, "pumpmaster1", 11) != 0) return false;
    Analysis r;
    int got = 0;
    const char* p = text + 11;
    while (*p == ';')
    {
        ++p;
        const char* eq = std::strchr(p, '=');
        if (!eq) return false;
        const std::string key(p, (size_t)(eq - p));
        p = eq + 1;
        auto readArr = [&](float* v, int n) -> bool {
            for (int i = 0; i < n; ++i)
            {
                const char* e = nullptr;
                const double x = parseNumber(p, &e);
                if (e == p) return false;
                v[i] = (float)clampv(x, -200.0, 200.0);
                p = e;
                if (i + 1 < n) { if (*p != ',') return false; ++p; }
            }
            return true;
        };
        if (key == "mid") { if (!readArr(r.mid, kBands)) return false; got |= 32; }
        else if (key == "side") { if (!readArr(r.side, kBands)) return false; got |= 64; }
        else if (key == "q") { if (!readArr(r.q, kQuantiles)) return false; got |= 128; }
        else if (key == "loss") { if (!readArr(r.loss, kLoss)) return false; got |= 256; }
        else if (key == "st") { if (!readArr(r.st, kQuantiles)) return false; got |= 512; }
        else
        {
            const char* e = nullptr;
            const double x = parseNumber(p, &e);
            if (e == p) return false;
            p = e;
            if (key == "sec") { r.seconds = (float)clampv(x, 0.0, 1e6); got |= 1; }
            else if (key == "lufs") { r.lufs = (float)clampv(x, -100.0, 10.0); got |= 2; }
            else if (key == "lra") { r.lra = (float)clampv(x, 0.0, 60.0); got |= 4; }
            else if (key == "tp") { r.truePeak = (float)clampv(x, -200.0, 30.0); got |= 8; }
            else if (key == "crest") { r.crest = (float)clampv(x, 0.0, 60.0); got |= 16; }
        }
        while (*p && *p != ';') ++p;   // ignore anything a later version adds after a value
    }
    if ((got & 1023) != 1023) return false;
    r.valid = true;
    a = r;
    return true;
}

// An Analysis handed between threads: a sequence lock over atomics (one writer per instance; readers retry on a
// torn read). Everything inside is an atomic, so there is no data race even while a write is in progress.
class AnalysisSlot {
public:
    void store(const Analysis& a)
    {
        const unsigned s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        f[0].store(a.valid ? 1.0f : 0.0f, std::memory_order_relaxed);
        f[1].store(a.seconds, std::memory_order_relaxed); f[2].store(a.lufs, std::memory_order_relaxed);
        f[3].store(a.lra, std::memory_order_relaxed); f[4].store(a.truePeak, std::memory_order_relaxed);
        f[5].store(a.crest, std::memory_order_relaxed);
        int k = 6;
        for (int i = 0; i < kBands; ++i) f[k++].store(a.mid[i], std::memory_order_relaxed);
        for (int i = 0; i < kBands; ++i) f[k++].store(a.side[i], std::memory_order_relaxed);
        for (int i = 0; i < kQuantiles; ++i) f[k++].store(a.q[i], std::memory_order_relaxed);
        for (int i = 0; i < kLoss; ++i) f[k++].store(a.loss[i], std::memory_order_relaxed);
        for (int i = 0; i < kQuantiles; ++i) f[k++].store(a.st[i], std::memory_order_relaxed);
        seq.store(s + 2, std::memory_order_release);
    }
    // false when a write was in progress (try again later)
    bool load(Analysis& a, unsigned* serial = nullptr) const
    {
        const unsigned s0 = seq.load(std::memory_order_acquire);
        if (s0 & 1u) return false;
        Analysis r;
        r.valid = f[0].load(std::memory_order_relaxed) > 0.5f;
        r.seconds = f[1].load(std::memory_order_relaxed); r.lufs = f[2].load(std::memory_order_relaxed);
        r.lra = f[3].load(std::memory_order_relaxed); r.truePeak = f[4].load(std::memory_order_relaxed);
        r.crest = f[5].load(std::memory_order_relaxed);
        int k = 6;
        for (int i = 0; i < kBands; ++i) r.mid[i] = f[k++].load(std::memory_order_relaxed);
        for (int i = 0; i < kBands; ++i) r.side[i] = f[k++].load(std::memory_order_relaxed);
        for (int i = 0; i < kQuantiles; ++i) r.q[i] = f[k++].load(std::memory_order_relaxed);
        for (int i = 0; i < kLoss; ++i) r.loss[i] = f[k++].load(std::memory_order_relaxed);
        for (int i = 0; i < kQuantiles; ++i) r.st[i] = f[k++].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq.load(std::memory_order_relaxed) != s0) return false;
        a = r;
        if (serial) *serial = s0;
        return true;
    }
    unsigned serial() const { return seq.load(std::memory_order_acquire); }
private:
    static constexpr int kN = 6 + 2 * kBands + 2 * kQuantiles + kLoss;
    std::atomic<float> f[kN] = {};
    std::atomic<unsigned> seq { 0 };
};

// ---------------------------------------------------------------------------------------------------------------
// Styles and the plan

struct Settings {
    int style = kBalanced;
    float intensity = 50.0f;                              // percent
    bool toneOn = true, dynOn = true, stereoOn = true, loudOn = true;
    float toneAmt = 100.0f, dynAmt = 100.0f, stereoAmt = 100.0f;   // percent
    float target = -12.0f, ceiling = -1.0f;              // LUFS, dBTP
    int loudMode = kGentle;                               // Gentle: only as loud as the peaks allow; Target: to TARGET
    bool compare = false;
};

struct StyleDef {
    float tilt;        // dB/oct added to the target around 1 kHz (+ brighter)
    float body;        // dB, low shelf on the target below about 150 Hz
    float air;         // dB, high shelf on the target above about 10 kHz
    float sub;         // dB, steep shelf on the target below about 60 Hz
    float strength;    // share of the smoothed difference the tone stage corrects at medium
    float toneMax;     // largest cut at medium, dB (boosts get 70 % of it)
    float ratio;       // glue compressor ratio at medium (1 = none)
    float depth;       // glue threshold below the loud parts (95th percentile block level), dB
    float lift;        // dB the leveler lifts quiet sections by at medium
    float widthPull;   // how far the width moves toward the style's (0 = left alone)
    float width;       // dB of side/mid the style aims for, relative to a typical master
};

// Balanced is calibrated on a reference pair (a song and a commercial service's subtle master of it at 50 %):
// a gentle, mostly broad EQ (more sub, less extreme top, about a third of the way to the target), quiet sections
// lifted about 1 dB more than loud ones, no downward compression, no width change, about +1 dB of level.
inline const StyleDef& styleDef(int s)
{
    static const StyleDef d[kNumStyles] = {
        //  tilt   body   air   sub   str    max   ratio depth lift  pull  width
        {  0.20f,  0.0f, -4.0f, 6.0f, 0.40f, 2.5f, 1.0f, 5.0f, 0.8f, 0.0f,  0.0f },   // Balanced
        { -0.15f,  1.0f, -5.0f, 6.0f, 0.40f, 2.5f, 1.2f, 5.0f, 0.8f, 0.0f,  0.0f },   // Warm
        {  0.45f, -1.0f, -2.0f, 5.0f, 0.40f, 3.0f, 1.0f, 5.0f, 0.8f, 0.4f,  1.0f },   // Open
        {  0.20f,  1.0f, -3.0f, 7.0f, 0.45f, 3.0f, 1.6f, 6.0f, 0.5f, 0.2f,  0.0f },   // Punch
    };
    return d[clampv(s, 0, kNumStyles - 1)];
}

// The tone stage's filters: two shelves and seven octave-wide bells (fixed frequencies; only the gains move).
inline BandSpec eqSpec(int i, double gainDb)
{
    static const double bells[7] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
    BandSpec b;
    b.gainDb = gainDb;
    if (i == 0) { b.shape = Shape::LowShelf; b.freq = 60.0; b.q = 0.7071; b.slope = 12; }
    else if (i == kEq - 1) { b.shape = Shape::HighShelf; b.freq = 12000.0; b.q = 0.7071; b.slope = 12; }
    else { b.shape = Shape::Bell; b.freq = bells[i - 1]; b.q = 1.0; }
    return b;
}

// The style's target on the pink-flat scale (dB per third octave, relative): flat to 200 Hz, then falling
// 1.6 dB/oct, and 2 dB/oct more above 8 kHz; the style's tilt, body and air on top.
inline double targetDb(double f, const StyleDef& st)
{
    const double o = std::log2(std::max(f, 10.0) / 1000.0);
    double t = -1.6 * std::max(o, std::log2(200.0 / 1000.0));
    if (f > 8000.0) t -= 2.0 * std::log2(f / 8000.0);
    t += st.tilt * clampv(o, -4.0, 4.0);
    t += st.body / (1.0 + std::pow(f / 150.0, 2.0));
    t += st.air / (1.0 + std::pow(10000.0 / std::max(f, 1.0), 2.0));
    t += st.sub / (1.0 + std::pow(f / 60.0, 4.0));
    return t;
}

struct Plan {
    bool valid = false;
    double eqGain[kEq] = {};                      // dB, final
    double target[kBands] = {}, measured[kBands] = {};   // aligned for display (dB)
    double thr = 0.0, ratio = 1.0;                // glue compressor (ratio 1 = nothing)
    double lift = 0.0, liftRef = -14.0;           // leveler: dB for quiet sections, the song's loudness after TONE
    double sideLo = 1.0, sideHi = 1.0;            // stereo stage gains (linear)
    double makeupDb = 0.0, limitLossDb = 0.0, eqDeltaDb = 0.0, compDeltaDb = 0.0, outLufs = -70.0, compareDb = 0.0, peakOverDb = 0.0;
    double toneK = 0.0, dynK = 0.0, stereoK = 0.0;
};

// The leveler's lift (dB) for a short-term loudness lvl, given the song's loudness ref: nothing within 5 LU of it,
// rising to the full lift 9 LU under it, and fading back out toward near-silence (never lifting a fade or a
// noise floor).
inline double liftDb(double lvl, double ref, double lift)
{
    if (lift <= 0.0) return 0.0;
    const double in = clampv((ref - 5.0 - lvl) / 4.0, 0.0, 1.0);
    const double floor = clampv((lvl - (ref - 30.0)) / 6.0, 0.0, 1.0);
    return lift * in * floor;
}

// Solve (A^T W A + lambda I) g = A^T W c for n unknowns (n <= kEq). Gaussian elimination with partial pivoting.
inline void leastSquares(const double A[kBands][kEq], const double* w, const double* c, double lambda, double* g)
{
    double M[kEq][kEq + 1] = {};
    for (int i = 0; i < kEq; ++i)
    {
        for (int j = 0; j < kEq; ++j)
        {
            double s = 0.0;
            for (int b = 0; b < kBands; ++b) s += A[b][i] * w[b] * A[b][j];
            M[i][j] = s + (i == j ? lambda : 0.0);
        }
        double s = 0.0;
        for (int b = 0; b < kBands; ++b) s += A[b][i] * w[b] * c[b];
        M[i][kEq] = s;
    }
    for (int col = 0; col < kEq; ++col)
    {
        int piv = col;
        for (int r = col + 1; r < kEq; ++r) if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
        if (piv != col) for (int k = 0; k <= kEq; ++k) std::swap(M[piv][k], M[col][k]);
        const double d = M[col][col];
        if (std::fabs(d) < 1e-12) { for (int k = 0; k < kEq; ++k) g[k] = 0.0; return; }
        for (int r = 0; r < kEq; ++r)
        {
            if (r == col) continue;
            const double f = M[r][col] / d;
            if (f == 0.0) continue;
            for (int k = col; k <= kEq; ++k) M[r][k] -= f * M[col][k];
        }
    }
    for (int i = 0; i < kEq; ++i) g[i] = M[i][kEq] / M[i][i];
}

// ---------------------------------------------------------------------------------------------------------------

class Engine {
public:
    static constexpr int kMaxChunk = 4096;
    static constexpr double kStereoXover = 120.0;

    // ---- UI feeds (any thread reads) ----
    AnalysisSlot published;                       // the analysis in use (written by the audio thread only)
    std::atomic<float> uiCurve[kCurvePts] = {}, uiTarget[kBands] = {}, uiMeasured[kBands] = {};
    std::atomic<float> uiEqGain[kEq] = {};
    std::atomic<float> uiLift { 0.0f }, uiLiftMax { 0.0f }, uiThr { 0.0f }, uiRatio { 1.0f }, uiSideLoDb { 0.0f }, uiSideHiDb { 0.0f }, uiMakeup { 0.0f },
        uiOutLufs { -70.0f }, uiCompare { 0.0f }, uiPeakOver { 0.0f }, uiEqDelta { 0.0f }, uiCompDelta { 0.0f }, uiLimitLoss { 0.0f };
    std::atomic<bool> uiPlanValid { false };
    std::atomic<unsigned> uiPlanSerial { 0 };
    std::atomic<bool> uiLearning { false };
    std::atomic<float> uiLearnSeconds { 0.0f };
    std::atomic<float> uiInST { -INFINITY }, uiOutST { -INFINITY }, uiOutI { -INFINITY };
    MeterValue compGR, limGR;                     // dB of reduction (max since the UI last exchanged them)

    // ---- other threads -> audio thread ----
    AnalysisSlot incoming;                        // an analysis loaded from a session (setState writes it)
    std::atomic<unsigned> incomingSerial { 0 };   // bumped after each incoming.store
    std::atomic<unsigned> consumedSerial { 0 };   // the last incomingSerial the audio thread took
    std::atomic<int> learnCmd { 0 };              // 1 = start learning, 2 = finish, 3 = cancel
    std::atomic<bool> resetOutMeter { false };

    static double curveFreq(int p) { return 20.0 * std::pow(1000.0, (double)p / (kCurvePts - 1)); }

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        // analysis FFT: 8192 at 44.1/48 kHz, scaled with the rate
        int n = 8192;
        const double r = sr / 48000.0;
        const int e = (int)std::lround(std::log2(std::max(0.25, r)));
        if (e > 0) n <<= std::min(e, 2); else if (e < 0) n >>= std::min(-e, 1);
        fftN = n;
        fft.setup(fftN);
        win.resize((size_t)fftN); fIn.resize((size_t)fftN); fOut.resize((size_t)fftN);
        for (int i = 0; i < fftN; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * i / fftN));
        for (int c = 0; c < 2; ++c) ring[c].assign((size_t)fftN, 0.0f);
        const double df = sr / fftN;
        for (int b = 0; b < kBands; ++b)
        {
            const double fc = bandFreq(b), lo = fc * std::pow(2.0, -1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
            bk0[b] = (int)std::ceil(lo / df); bk1[b] = (int)std::ceil(hi / df);
            bk0[b] = clampv(bk0[b], 1, fftN / 2); bk1[b] = clampv(bk1[b], 1, fftN / 2);
            bandOk[b] = hi < 0.47 * sr;
            bandScale[b] = 1.0;
            if (bk1[b] <= bk0[b])   // no bin centre inside: nearest bin, scaled to the band's width
            {
                bk0[b] = clampv((int)std::lround(fc / df), 1, fftN / 2 - 1); bk1[b] = bk0[b] + 1;
                bandScale[b] = (hi - lo) / df;
            }
        }
        // basis: each tone filter at +1 dB, in dB at the band centres (designed once: only gains move afterwards)
        for (int i = 0; i < kEq; ++i)
        {
            const AnalogBand a = analogPrototype(eqSpec(i, 1.0), sr);
            unitBand[i].design(a, sr);
            for (int b = 0; b < kBands; ++b)
            {
                const double f = std::min(bandFreq(b), 0.49 * sr);
                basis[b][i] = 10.0 * std::log10(std::max(1e-30, unitBand[i].magSq(kTwoPi * f / sr)));
            }
            eq[i].d = unitBand[i];
            eq[i].reset();
            curGain[i] = 0.0;
            eqFlat[i] = true;
            redesign(i, 0.0);
        }
        for (int b = 0; b < kBands; ++b) kw[b] = tone_kWeight(bandFreq(b));
        // detector
        {
            const AnalogBand a = analogPrototype(BandSpec{ Shape::LowCut, 70.0, 0.0, 0.7071, 12 }, sr);
            DigitalBand d; d.design(a, sr);
            for (int c = 0; c < 2; ++c) { det[c].d = d; det[c].reset(); }
        }
        cRms = tauCoef(0.010, sr);
        levCoef = 1.0 - std::exp(-32.0 / (1.5 * sr));    // ~short-term loudness (1.5 s)
        liftCoef = 1.0 - std::exp(-32.0 / (1.0 * sr));   // the lift glides (1 s)
        for (auto& c : kwRun) for (auto& st : c) st.reset();
        levAcc = levP = levDb = 0.0; levN = 0;
        rmsP = 0.0;
        gcar.setup(20.0, 250.0, sr);
        gcar.reset(0.0);
        // stereo
        for (auto& s : sideSplit) { s.set(kStereoXover, sr); s.reset(); }
        midAp.set(kStereoXover, sr); midAp.reset();
        // limiter: LIMIT's transparent style, true peak, 2x
        lim.setStyle(limit::kDynamic);   // brief, peak-only limiting: no slow leveling stage pulling the loud parts down
        lim.setLookahead(5.0f); lim.setAttack(50.0f); lim.setRelease(100.0f); lim.setLinks(75.0f, 100.0f);
        lim.setTruePeak(true); lim.setOversampling(1); lim.setDC(false); lim.setDither(0, 0); lim.setUnity(false);
        lim.setGain(0.0f); lim.setCeiling(cur.ceiling);
        lim.prepare(sr, kMaxChunk);
        lat = lim.latency();
        for (int c = 0; c < 2; ++c)
        {
            limDly[c].setMaxDelay(lat + 8); dryDly[c].setMaxDelay(lat + 8);
            limDly[c].setDelay(lat); dryDly[c].setDelay(lat);
            limDly[c].clear(); dryDly[c].clear();
        }
        const int ramp = (int)(0.03 * sr);
        for (auto* s : { &dynMix, &stMix, &limMix, &cmpMix, &makeupS, &cmpGainS, &sideLoS, &sideHiS }) s->setRamp(ramp);
        makeupS.setRamp((int)(0.08 * sr)); cmpGainS.setRamp((int)(0.08 * sr));
        inMeter.prepare(sr); outMeter.prepare(sr); learnMeter.prepare(sr);
        publishEvery = std::max(1, (int)std::lround(sr * 0.1));
        publishCount = 0;
        block100 = std::max(1, (int)std::lround(sr * 0.1));
        block400 = std::max(1, (int)std::lround(sr * 0.4));
        block5 = std::max(1, (int)std::lround(sr * 0.005));
        lossRel = std::exp(-0.005 / kLossReleaseS);
        kWeightCoeffs(kwShelf, kwHp);
        resetLearn();
        planDirty = true;
        applyPlan(true);
    }

    void set(const Settings& s)
    {
        const bool planChange = s.style != cur.style || s.intensity != cur.intensity || s.toneOn != cur.toneOn || s.dynOn != cur.dynOn
            || s.stereoOn != cur.stereoOn || s.loudOn != cur.loudOn || s.toneAmt != cur.toneAmt || s.dynAmt != cur.dynAmt
            || s.stereoAmt != cur.stereoAmt || s.target != cur.target || s.ceiling != cur.ceiling || s.loudMode != cur.loudMode;
        cur = s;
        lim.setCeiling(clampv(cur.ceiling, -3.0f, 0.0f));
        if (planChange) planDirty = true;
    }

    int latency() const { return lat; }
    // Calibration tools only: plan with this style instead of the table's (nullptr = the table)
    const StyleDef* styleOverride = nullptr;
    void replan() { planDirty = true; applyPlan(false); }
    bool hasAnalysis() const { return ana.valid; }
    const Analysis& analysis() const { return ana; }   // audio thread / tests only
    const Plan& plan() const { return pl; }            // audio thread / tests only

    // Tests and offline tools: use this analysis now (the plugin goes through `incoming`).
    void setAnalysis(const Analysis& a) { ana = a; published.store(ana); planDirty = true; applyPlan(false); }
    // Tests and offline tools: learn from a whole buffer at once (doesn't change it).
    void learnBuffer(const float* L, const float* R, int n)
    {
        beginLearn();
        for (int off = 0; off < n; off += kMaxChunk) learnBlock(L + off, R + off, std::min(kMaxChunk, n - off));
        finishLearn();
        applyPlan(false);
    }

    void process(float* L, float* R, int n)
    {
        // commands and a loaded analysis (block start)
        const int cmd = learnCmd.exchange(0, std::memory_order_acq_rel);
        if (cmd == 1) beginLearn();
        else if (cmd == 2 && learning) finishLearn();
        else if (cmd == 3) { learning = false; uiLearning.store(false, std::memory_order_relaxed); }
        const unsigned inSer = incomingSerial.load(std::memory_order_acquire);
        if (inSer != seenIncoming)
        {
            Analysis a;
            if (incoming.load(a))
            {
                seenIncoming = inSer; ana = a; published.store(ana); planDirty = true;
                consumedSerial.store(inSer, std::memory_order_release);
            }
        }
        if (resetOutMeter.exchange(false, std::memory_order_acq_rel)) outMeter.reset();
        applyPlan(false);

        for (int off = 0; off < n; off += kMaxChunk)
        {
            const int m = std::min(kMaxChunk, n - off);
            float* l = L + off;
            float* r = R + off;
            if (learning) learnBlock(l, r, m);
            inMeter.process(l, r, m);
            processChunk(l, r, m);
            outMeter.process(l, r, m);
            publishCount += m;
            if (publishCount >= publishEvery)
            {
                publishCount = 0;
                uiInST.store((float)inMeter.getShortTerm(), std::memory_order_relaxed);
                uiOutST.store((float)outMeter.getShortTerm(), std::memory_order_relaxed);
                uiOutI.store((float)outMeter.getIntegrated(), std::memory_order_relaxed);
                uiLearnSeconds.store((float)(learnSamples / sr), std::memory_order_relaxed);
            }
        }
    }

private:
    // The BS.1770 K-weighting at this rate (the loudness meter's own filters)
    void kWeightCoeffs(BiquadCoeffs& shelf, BiquadCoeffs& hp) const
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(kPi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    // K-weighting power at f, for predicting loudness changes
    double tone_kWeight(double f) const
    {
        BiquadCoeffs shelf, hp;
        kWeightCoeffs(shelf, hp);
        const double w = kTwoPi * std::min(f, 0.49 * sr) / sr;
        return shelf.magSq(w) * hp.magSq(w);
    }

    void redesign(int i, double g)
    {
        eqDesigned[i] = g;
        if (std::fabs(g) < 1e-6) { eqFlat[i] = true; return; }
        if (eqFlat[i]) eq[i].reset();   // it sat out while flat: start from rest, not from old state
        eqFlat[i] = false;
        const AnalogBand a = analogPrototype(eqSpec(i, g), sr);
        eq[i].d = unitBand[i];
        eq[i].d.redesignFast(a);
    }

    // ---------------------------------------------------------------- learning
    void resetLearn()
    {
        learning = false;
        learnSamples = 0;
        for (int b = 0; b < kBands; ++b) accMid[b] = accSide[b] = 0.0;
        frames = 0;
        ringPos = 0; hopCount = 0; ringFill = 0;
        for (auto& c : ring) std::fill(c.begin(), c.end(), 0.0f);
        std::fill(std::begin(lvlHist), std::end(lvlHist), 0u);
        std::fill(std::begin(crestHist), std::end(crestHist), 0u);
        std::fill(std::begin(stHist), std::end(stHist), 0u);
        stTimer = 0;
        b100Acc = 0.0; b100N = 0; b400Acc = 0.0; b400Pk = 0.0f; b400N = 0;
        for (auto& d : ldet) d.reset();
        for (auto& c : kwSt) for (auto& st : c) st.reset();
        b5Acc = 0.0; b5Pk = 0.0f; b5N = 0; lossPow = 0.0;
        for (int j = 0; j < kLoss; ++j) { lossGr[j] = 0.0; lossAcc[j] = 0.0; }
        learnMeter.reset();
    }
    void beginLearn()
    {
        resetLearn();
        for (int c = 0; c < 2; ++c) ldet[c].d = det[c].d;
        learning = true;
        uiLearning.store(true, std::memory_order_relaxed);
        uiLearnSeconds.store(0.0f, std::memory_order_relaxed);
    }
    void learnBlock(const float* L, const float* R, int n)
    {
        learnMeter.process(L, R, n);
        learnSamples += n;
        for (stTimer += n; stTimer >= block100; stTimer -= block100)
        {
            const double v = learnMeter.getShortTerm();
            if (std::isfinite(v) && v > -70.0) ++stHist[clampv((int)std::floor((v + 80.0) * 2.0), 0, kLvlBins - 1)];
        }
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            ring[0][(size_t)ringPos] = l; ring[1][(size_t)ringPos] = r;
            if (++ringPos == fftN) ringPos = 0;
            if (ringFill < fftN) ++ringFill;
            if (++hopCount >= fftN / 2 && ringFill >= fftN) { hopCount = 0; analyseFrame(); }
            // the compressor's detector signal, 100 ms blocks
            const double hl = ldet[0].process(0, l), hr = ldet[1].process(0, r);
            b100Acc += 0.5 * (hl * hl + hr * hr);
            if (++b100N >= block100)
            {
                const double db = 10.0 * std::log10(b100Acc / b100N + 1e-30);
                if (db > -70.0) ++lvlHist[clampv((int)std::floor((db + 80.0) * 2.0), 0, kLvlBins - 1)];
                b100Acc = 0.0; b100N = 0;
            }
            // 5 ms blocks: K-weighted power and peak, through a model limiter at every drive (the loss curve)
            {
                const double kl = kwSt[0][1].process(kwHp, kwSt[0][0].process(kwShelf, l));
                const double kr = kwSt[1][1].process(kwHp, kwSt[1][0].process(kwShelf, r));
                b5Acc += kl * kl + kr * kr;
                b5Pk = std::max(b5Pk, std::max(std::fabs(l), std::fabs(r)));
                if (++b5N >= block5)
                {
                    const double pw = b5Acc / b5N;
                    if (pw > 1e-9)
                    {
                        const double pk = 20.0 * std::log10(b5Pk + 1e-30);
                        lossPow += pw;
                        for (int j = 0; j < kLoss; ++j)
                        {
                            const double over = pk + kLossFrom + j;   // dB over the ceiling at this drive
                            lossGr[j] = over > lossGr[j] ? over : std::max(over, lossGr[j] * lossRel);
                            const double g = std::max(0.0, lossGr[j]);
                            lossAcc[j] += pw * std::pow(10.0, -0.1 * g);
                        }
                    }
                    b5Acc = 0.0; b5Pk = 0.0f; b5N = 0;
                }
            }
            // crest of 400 ms windows
            b400Acc += 0.5 * ((double)l * l + (double)r * r);
            b400Pk = std::max(b400Pk, std::max(std::fabs(l), std::fabs(r)));
            if (++b400N >= block400)
            {
                const double rms = std::sqrt(b400Acc / b400N);
                if (rms > 1e-3)
                {
                    const double c = 20.0 * std::log10(b400Pk / rms);
                    ++crestHist[clampv((int)std::floor(c * 4.0), 0, kCrestBins - 1)];
                }
                b400Acc = 0.0; b400Pk = 0.0f; b400N = 0;
            }
        }
    }
    void analyseFrame()
    {
        double ms = 0.0;
        for (int i = 0; i < fftN; ++i)
        {
            const int k = (ringPos + i) % fftN;
            const float l = ring[0][(size_t)k], r = ring[1][(size_t)k];
            ms += 0.5 * ((double)l * l + (double)r * r);
        }
        ms /= fftN;
        if (ms < 1e-6) return;   // under -60 dBFS: silence or a fade, not the music's balance
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < fftN; ++i)
            {
                const int k = (ringPos + i) % fftN;
                const float l = ring[0][(size_t)k], r = ring[1][(size_t)k];
                fIn[(size_t)i] = (pass == 0 ? 0.5f * (l + r) : 0.5f * (l - r)) * win[(size_t)i];
            }
            fft.forward(fIn.data(), fOut.data());
            double* acc = pass == 0 ? accMid : accSide;
            for (int b = 0; b < kBands; ++b)
            {
                double s = 0.0;
                for (int k = bk0[b]; k < bk1[b]; ++k)
                {
                    const double re = fOut[(size_t)(2 * k)], im = fOut[(size_t)(2 * k + 1)];
                    s += re * re + im * im;
                }
                acc[b] += s * bandScale[b];
            }
        }
        ++frames;
    }
    void finishLearn()
    {
        learning = false;
        uiLearning.store(false, std::memory_order_relaxed);
        Analysis a;
        a.seconds = (float)(learnSamples / sr);
        a.lufs = (float)learnMeter.getIntegrated();
        a.lra = (float)std::max(0.0, learnMeter.getRange());
        a.truePeak = (float)learnMeter.getTruePeakDb();
        uint64_t tot = 0;
        for (unsigned v : lvlHist) tot += v;
        if (frames < 4 || tot < 20 || !std::isfinite(a.lufs) || a.lufs < -60.0f) return;   // not enough music: keep the old analysis
        // spectrum: per third octave (pink noise reads flat), same normalisation for every song (only the shape and
        // the mid/side relation are used)
        const double norm = 1.0 / ((double)frames * fftN * fftN * 0.375);
        for (int b = 0; b < kBands; ++b)
        {
            a.mid[b] = (float)(10.0 * std::log10(accMid[b] * norm + 1e-20));
            a.side[b] = (float)(10.0 * std::log10(accSide[b] * norm + 1e-20));
        }
        // level quantiles
        auto quantiles = [&](const unsigned* h, float* out) {
            uint64_t t = 0;
            for (int k = 0; k < kLvlBins; ++k) t += h[k];
            for (int qi = 0; qi < kQuantiles; ++qi)
            {
                const double want = (qi + 1) * 0.05 * (double)t;
                uint64_t run = 0;
                int bin = kLvlBins - 1;
                for (int k = 0; k < kLvlBins; ++k) { run += h[k]; if ((double)run >= want) { bin = k; break; } }
                out[qi] = t ? (float)(bin * 0.5 - 80.0 + 0.25) : a.lufs;
            }
        };
        quantiles(lvlHist, a.q);
        quantiles(stHist, a.st);
        uint64_t ct = 0;
        for (unsigned v : crestHist) ct += v;
        if (ct > 0)
        {
            uint64_t run = 0;
            for (int k = 0; k < kCrestBins; ++k) { run += crestHist[k]; if (run * 2 >= ct) { a.crest = (float)((k + 0.5) * 0.25); break; } }
        }
        for (int j = 0; j < kLoss; ++j)
            a.loss[j] = lossPow > 0.0 ? (float)std::min(0.0, 10.0 * std::log10(lossAcc[j] / lossPow + 1e-30)) : 0.0f;
        a.valid = true;
        ana = a;
        published.store(ana);
        planDirty = true;
    }

    // ---------------------------------------------------------------- the plan
    void computePlan(Plan& p) const
    {
        p = Plan();
        const StyleDef& st = styleOverride ? *styleOverride : styleDef(cur.style);
        const double inten = clampv(cur.intensity, 0.0f, 100.0f) / 50.0;   // 1 = medium
        p.toneK = cur.toneOn ? inten * clampv(cur.toneAmt, 0.0f, 200.0f) * 0.01 : 0.0;
        p.dynK = cur.dynOn ? inten * clampv(cur.dynAmt, 0.0f, 200.0f) * 0.01 : 0.0;
        p.stereoK = cur.stereoOn ? inten * clampv(cur.stereoAmt, 0.0f, 200.0f) * 0.01 : 0.0;
        if (!ana.valid) return;
        p.valid = true;

        // ---- tone
        double tot[kBands], sm[kBands], w[kBands], d[kBands];
        double loudest = -300.0;
        for (int b = 0; b < kBands; ++b)
        {
            tot[b] = 10.0 * std::log10(std::pow(10.0, ana.mid[b] * 0.1) + std::pow(10.0, ana.side[b] * 0.1));
            if (bandOk[b]) loudest = std::max(loudest, tot[b]);
        }
        for (int b = 0; b < kBands; ++b)   // one-octave triangular smoothing in dB (one-sided near the ends)
        {
            double s = 0.0, ws = 0.0;
            for (int k = -3; k <= 3; ++k)
            {
                const int j = b + k;
                if (j < 0 || j >= kBands || !bandOk[j]) continue;
                const double wk = 4.0 - std::abs(k);
                s += wk * tot[j]; ws += wk;
            }
            sm[b] = ws > 0.0 ? s / ws : tot[b];
            const double f = bandFreq(b);
            w[b] = !bandOk[b] || f < 25.0 || f > 18000.0 ? 0.0 : (f < 40.0 || f > 14000.0 ? 0.3 : 1.0);
        }
        double off = 0.0, offW = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const double f = bandFreq(b);
            d[b] = targetDb(f, st) - sm[b];
            if (f >= 100.0 && f <= 5000.0) { off += d[b]; offW += 1.0; }
        }
        off = offW > 0.0 ? off / offW : 0.0;
        double c[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const double f = bandFreq(b);
            const double dev = d[b] - off;   // + = the song is short of the target here
            double maxCut = st.toneMax, maxBoost = 0.7 * st.toneMax;
            if (f < 50.0 || f > 12000.0) { maxCut *= 0.5; maxBoost *= 0.5; }
            double v = st.strength * dev;
            v = v > 0.0 ? maxBoost * std::tanh(v / maxBoost) : maxCut * std::tanh(v / maxCut);
            if (sm[b] < loudest - 35.0) v = std::min(v, 0.0);   // nothing there: never lift noise
            c[b] = v;
            p.target[b] = targetDb(f, st) - off;
            p.measured[b] = sm[b];
        }
        double g[kEq];
        leastSquares(basis, w, c, 0.05, g);
        for (int i = 0; i < kEq; ++i) p.eqGain[i] = clampv(g[i] * p.toneK, -12.0, 12.0);

        // predicted loudness change of the tone stage (K-weighted, on the song's own spectrum)
        {
            double a0 = 0.0, a1 = 0.0;
            for (int b = 0; b < kBands; ++b)
            {
                if (!bandOk[b]) continue;
                const double pw = (std::pow(10.0, ana.mid[b] * 0.1) + std::pow(10.0, ana.side[b] * 0.1)) * kw[b];
                double db = 0.0;
                for (int i = 0; i < kEq; ++i) db += basis[b][i] * p.eqGain[i];
                a0 += pw; a1 += pw * std::pow(10.0, db * 0.1);
            }
            p.eqDeltaDb = a0 > 0.0 ? 10.0 * std::log10(a1 / a0) : 0.0;
        }

        // ---- dynamics: a slow leveler that lifts quiet sections (by short-term loudness), and a glue compressor
        const double loud = ana.q[kQuantiles - 1];
        double dense = clampv((ana.crest - 6.0) / 4.0, 0.25, 1.0);   // crest under ~10 dB: already dense
        p.thr = loud - st.depth;
        p.ratio = 1.0 + (st.ratio - 1.0) * p.dynK * dense;
        p.lift = st.lift * p.dynK;
        p.liftRef = ana.lufs + p.eqDeltaDb;
        {
            GainComputer gc; gc.threshold = p.thr; gc.ratio = p.ratio; gc.knee = 8.0;
            double a0 = 0.0, a1 = 0.0;
            for (int qi = 0; qi < kQuantiles; ++qi)
            {
                const double pw = std::pow(10.0, ana.q[qi] * 0.1);
                a0 += pw; a1 += pw * std::pow(10.0, gc.compress(ana.q[qi] + p.eqDeltaDb) * 0.1);
            }
            double glue = a0 > 0.0 ? 10.0 * std::log10(a1 / a0) : 0.0;
            // the leveler, on the short-term loudness distribution (blocks within 20 LU of the song, as the
            // integrated loudness's own gate roughly keeps them)
            a0 = a1 = 0.0;
            for (int qi = 0; qi < kQuantiles; ++qi)
            {
                const double lv = ana.st[qi] + p.eqDeltaDb;
                if (lv < p.liftRef - 20.0) continue;
                const double pw = std::pow(10.0, lv * 0.1);
                a0 += pw; a1 += pw * std::pow(10.0, liftDb(lv, p.liftRef, p.lift) * 0.1);
            }
            p.compDeltaDb = glue + (a0 > 0.0 ? 10.0 * std::log10(a1 / a0) : 0.0);
        }

        // ---- stereo
        {
            double lm = 0.0, ls = 0.0, hm = 0.0, hs = 0.0;
            for (int b = 0; b < kBands; ++b)
            {
                const double f = bandFreq(b);
                const double pm = std::pow(10.0, ana.mid[b] * 0.1), ps = std::pow(10.0, ana.side[b] * 0.1);
                if (f >= 30.0 && f <= 120.0) { lm += pm; ls += ps; }
                if (f >= 1000.0 && f <= 8000.0 && bandOk[b]) { hm += pm; hs += ps; }
            }
            const double lowSide = lm > 0.0 ? 10.0 * std::log10(ls / lm + 1e-12) : -60.0;
            const double hiSide = hm > 0.0 ? 10.0 * std::log10(hs / hm + 1e-12) : -60.0;
            // fold the lows toward mono when they are wide (side within 10 dB of mid)
            const double wide = clampv((lowSide + 10.0) / 8.0, 0.0, 1.0);
            p.sideLo = clampv(1.0 - 0.5 * wide * p.stereoK, 0.0, 1.0);
            // nudge the width above 120 Hz toward a typical master (side about 8 dB under mid) and the style
            double wdb = clampv(st.widthPull * ((-8.0 + st.width) - hiSide), -1.0, 1.0);
            if (hiSide < -25.0) wdb = std::min(wdb, 0.0);   // near mono: widening would only lift artefacts
            p.sideHi = dbToGain(wdb * p.stereoK);
        }

        // ---- loudness
        // the drive into the limiter h (dB of the song's peaks over the ceiling) and what limiting there takes from
        // the loudness come from the learned loss curve; the makeup is solved so the result lands on the target
        const double pre = ana.lufs + p.eqDeltaDb + p.compDeltaDb;
        if (cur.loudOn)
        {
            double mk = cur.target - pre;
            for (int it = 0; it < 12; ++it)
            {
                const double h = (pre - ana.lufs) + mk - cur.ceiling;
                const double want = cur.target - pre - lossAt(ana, h);
                if (std::fabs(want - mk) < 0.005) { mk = want; break; }
                mk += 0.8 * (want - mk);
            }
            if (cur.loudMode == kGentle)
            {
                // only as loud as the peaks allow: the limiter may take at most a couple of dB off the highest
                // peaks (2 dB at medium intensity), never more than the target asks for
                const double inten = clampv(cur.intensity, 0.0f, 100.0f) / 50.0;
                const double over = 2.0 * clampv(inten, 0.25, 2.0);
                const double peakAfter = ana.truePeak + std::max(0.0, p.eqDeltaDb);
                mk = std::min(mk, cur.ceiling + over - peakAfter);
            }
            p.makeupDb = clampv(mk, -24.0, 30.0);
            p.limitLossDb = lossAt(ana, (pre - ana.lufs) + p.makeupDb - cur.ceiling);
        }
        p.outLufs = pre + p.makeupDb + p.limitLossDb;
        p.compareDb = p.outLufs - ana.lufs;
        p.peakOverDb = cur.loudOn ? std::max(0.0, ana.truePeak + (pre - ana.lufs) + p.makeupDb - cur.ceiling) : 0.0;
    }

    void applyPlan(bool immediate)
    {
        if (!planDirty) return;
        planDirty = false;
        computePlan(pl);
        for (int i = 0; i < kEq; ++i) tgtGain[i] = pl.valid ? pl.eqGain[i] : 0.0;
        gc.threshold = pl.thr; gc.ratio = std::max(1.0, pl.ratio); gc.knee = 8.0;
        const bool dynActive = pl.valid && (pl.ratio > 1.0001 || pl.lift > 1e-4);
        const bool stActive = pl.valid && (std::fabs(pl.sideLo - 1.0) > 1e-4 || std::fabs(pl.sideHi - 1.0) > 1e-4);
        const bool limActive = pl.valid && cur.loudOn;
        auto setS = [&](LinearSmoother& s, double v) { if (immediate) s.reset(v); else s.setTarget(v); };
        setS(dynMix, dynActive ? 1.0 : 0.0);
        if (stActive) { setS(sideLoS, pl.sideLo); setS(sideHiS, pl.sideHi); }
        setS(stMix, stActive ? 1.0 : 0.0);
        limWanted = limActive;
        if (immediate || limActive) setS(limMix, limActive ? 1.0 : 0.0);
        setS(makeupS, dbToGain(pl.valid ? pl.makeupDb : 0.0));
        setS(cmpGainS, dbToGain(pl.valid ? pl.compareDb : 0.0));
        if (immediate) for (int i = 0; i < kEq; ++i) { curGain[i] = tgtGain[i]; redesign(i, curGain[i]); }
        publishPlan();
    }

    void publishPlan()
    {
        uiPlanValid.store(pl.valid, std::memory_order_relaxed);
        for (int i = 0; i < kEq; ++i) uiEqGain[i].store((float)(pl.valid ? pl.eqGain[i] : 0.0), std::memory_order_relaxed);
        for (int b = 0; b < kBands; ++b)
        {
            uiTarget[b].store((float)pl.target[b], std::memory_order_relaxed);
            uiMeasured[b].store((float)pl.measured[b], std::memory_order_relaxed);
        }
        // the tone curve as designed (exact, not the linear estimate)
        DigitalBand d[kEq];
        for (int i = 0; i < kEq; ++i)
        {
            d[i] = unitBand[i];
            const double g = pl.valid ? pl.eqGain[i] : 0.0;
            if (std::fabs(g) > 1e-6) d[i].redesignFast(analogPrototype(eqSpec(i, g), sr));
        }
        for (int k = 0; k < kCurvePts; ++k)
        {
            const double f = std::min(curveFreq(k), 0.49 * sr), wv = kTwoPi * f / sr;
            double db = 0.0;
            for (int i = 0; i < kEq; ++i)
                if (pl.valid && std::fabs(pl.eqGain[i]) > 1e-6) db += 10.0 * std::log10(std::max(1e-30, d[i].magSq(wv)));
            uiCurve[k].store((float)db, std::memory_order_relaxed);
        }
        uiThr.store((float)pl.thr, std::memory_order_relaxed);
        uiRatio.store((float)pl.ratio, std::memory_order_relaxed);
        uiLiftMax.store((float)pl.lift, std::memory_order_relaxed);
        uiSideLoDb.store((float)gainToDb(std::max(pl.sideLo, 1e-6)), std::memory_order_relaxed);
        uiSideHiDb.store((float)gainToDb(pl.sideHi), std::memory_order_relaxed);
        uiMakeup.store((float)pl.makeupDb, std::memory_order_relaxed);
        uiOutLufs.store((float)pl.outLufs, std::memory_order_relaxed);
        uiCompare.store((float)pl.compareDb, std::memory_order_relaxed);
        uiPeakOver.store((float)pl.peakOverDb, std::memory_order_relaxed);
        uiEqDelta.store((float)pl.eqDeltaDb, std::memory_order_relaxed);
        uiCompDelta.store((float)pl.compDeltaDb, std::memory_order_relaxed);
        uiLimitLoss.store((float)pl.limitLossDb, std::memory_order_relaxed);
        uiPlanSerial.fetch_add(1, std::memory_order_release);
    }

    // ---------------------------------------------------------------- processing
    void processChunk(float* L, float* R, int n)
    {
        // the dry path for COMPARE: the input, delayed by the chain's latency
        for (int i = 0; i < n; ++i) { dryBuf[0][i] = L[i]; dryBuf[1][i] = R[i]; }

        float grMax = 0.0f;
        for (int off = 0; off < n; off += kSub)
        {
            const int m = std::min(kSub, n - off);
            // tone gains glide toward the plan (about 25 dB/s), redesigned per sub-block
            for (int i = 0; i < kEq; ++i)
            {
                if (curGain[i] == tgtGain[i]) continue;
                const double step = 25.0 * m / sr;
                const double dlt = tgtGain[i] - curGain[i];
                curGain[i] = std::fabs(dlt) <= step ? tgtGain[i] : curGain[i] + (dlt > 0 ? step : -step);
                redesign(i, curGain[i]);
            }
            for (int s = off; s < off + m; ++s)
            {
                double l = L[s], r = R[s];
                // TONE
                for (int i = 0; i < kEq; ++i)
                {
                    if (eqFlat[i]) continue;
                    l = eq[i].process(0, l); r = eq[i].process(1, r);
                }
                // DYNAMICS: the leveler (short-term loudness of the K-weighted signal, updated every 32 samples, its
                // lift gliding with a 1 s time constant) and the glue compressor (feed-forward, stereo-linked RMS
                // of the bass-filtered signal)
                {
                    const double kl = kwRun[0][1].process(kwHp, kwRun[0][0].process(kwShelf, l));
                    const double kr = kwRun[1][1].process(kwHp, kwRun[1][0].process(kwShelf, r));
                    levAcc += kl * kl + kr * kr;
                    if (++levN >= 32)
                    {
                        levP += (levAcc / levN - levP) * levCoef;
                        const double lv = -0.691 + 10.0 * std::log10(levP + 1e-30);
                        levDb += (liftDb(lv, pl.liftRef, pl.valid ? pl.lift : 0.0) - levDb) * liftCoef;
                        levAcc = 0.0; levN = 0;
                    }
                    const double hl = det[0].process(0, l), hr = det[1].process(0, r);
                    rmsP = 0.5 * (hl * hl + hr * hr) + (rmsP - 0.5 * (hl * hl + hr * hr)) * cRms;
                    const double lvl = 10.0 * std::log10(rmsP + 1e-30);
                    const double grDb = gcar.processGR(gc.compress(lvl));
                    const double dm = dynMix.next();
                    const double gg = dbToGain((grDb + levDb) * dm);
                    l *= gg; r *= gg;
                    grMax = std::max(grMax, (float)(-grDb * dm));
                    liftNow = (float)(levDb * dm);
                }
                // STEREO
                {
                    const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
                    double sLo, sHi;
                    sideSplit[0].process(side, sLo, sHi);
                    const double mAp = midAp.process(mid);
                    const double s2 = sLo * sideLoS.next() + sHi * sideHiS.next();
                    const double mx = stMix.next();
                    l = l + ((mAp + s2) - l) * mx;
                    r = r + ((mAp - s2) - r) * mx;
                }
                const double mk = makeupS.next();
                L[s] = (float)(l * mk); R[s] = (float)(r * mk);
            }
        }
        compGR.setMax(grMax);
        uiLift.store(liftNow, std::memory_order_relaxed);

        // LOUDNESS: the limiter, or its latency when the stage is off (crossfaded)
        for (int i = 0; i < n; ++i) { limBuf[0][i] = limDly[0].process(L[i]); limBuf[1][i] = limDly[1].process(R[i]); }
        lim.process(L, R, n);
        limGR.setMax(lim.grMax.exchange(0.0f));
        for (int i = 0; i < n; ++i)
        {
            // the limiter stays in until the makeup has come back down to unity (switching LOUDNESS off never lets
            // the raised level through unlimited)
            if ((i & 63) == 0) limMix.setTarget(limWanted || makeupS.current() > 1.0001 ? 1.0 : 0.0);
            const double m = limMix.next();
            L[i] = (float)(limBuf[0][i] + (L[i] - limBuf[0][i]) * m);
            R[i] = (float)(limBuf[1][i] + (R[i] - limBuf[1][i]) * m);
        }
        // COMPARE
        for (int i = 0; i < n; ++i)
        {
            const double g = cmpGainS.next();
            const double dl = dryDly[0].process(dryBuf[0][i]) * g, dr = dryDly[1].process(dryBuf[1][i]) * g;
            cmpMix.setTarget(cur.compare ? 1.0 : 0.0);
            const double m = cmpMix.next();
            L[i] = (float)(L[i] + (dl - L[i]) * m);
            R[i] = (float)(R[i] + (dr - R[i]) * m);
        }
    }

    // ---- state
    static constexpr int kSub = 64;
    static constexpr int kLvlBins = 172;     // 0.5 dB, -80 .. +6
    static constexpr int kCrestBins = 160;   // 0.25 dB, 0 .. 40
    double sr = 48000.0;
    Settings cur;
    Analysis ana;
    Plan pl;
    bool planDirty = true;
    unsigned seenIncoming = 0;

    // learning
    bool learning = false;
    int64_t learnSamples = 0;
    RealFFT fft;
    int fftN = 8192;
    std::vector<float> win;
    AlignedBuffer fIn, fOut;
    std::vector<float> ring[2];
    int ringPos = 0, hopCount = 0, ringFill = 0;
    int bk0[kBands] = {}, bk1[kBands] = {};
    double bandScale[kBands] = {};
    bool bandOk[kBands] = {};
    double accMid[kBands] = {}, accSide[kBands] = {};
    int64_t frames = 0;
    unsigned lvlHist[kLvlBins] = {}, crestHist[kCrestBins] = {}, stHist[kLvlBins] = {};
    int stTimer = 0;
    double b100Acc = 0.0, b400Acc = 0.0;
    float b400Pk = 0.0f;
    int b100N = 0, b400N = 0, block100 = 4800, block400 = 19200;
    BandFilter<1> ldet[2];
    LoudnessMeter learnMeter;
    static constexpr double kLossReleaseS = 0.10;   // the model limiter's release (LIMIT's dynamic style)
    BiquadCoeffs kwShelf, kwHp;
    BiquadState kwSt[2][2];
    double b5Acc = 0.0, lossPow = 0.0, lossRel = 0.98, lossGr[kLoss] = {}, lossAcc[kLoss] = {};
    float b5Pk = 0.0f;
    int b5N = 0, block5 = 240;

    // tone
    DigitalBand unitBand[kEq];
    BandFilter<2> eq[kEq];
    double basis[kBands][kEq] = {};
    double kw[kBands] = {};
    double curGain[kEq] = {}, tgtGain[kEq] = {}, eqDesigned[kEq] = {};
    bool eqFlat[kEq] = {};

    // dynamics
    BandFilter<1> det[2];
    double cRms = 0.0, rmsP = 0.0;
    BiquadState kwRun[2][2];
    double levAcc = 0.0, levP = 0.0, levDb = 0.0, levCoef = 0.0, liftCoef = 0.0;
    int levN = 0;
    float liftNow = 0.0f;
    GainComputer gc;
    AttackRelease gcar;

    // stereo
    LR4Split sideSplit[1];
    LR4Allpass midAp;

    // loudness
    limit::Engine lim;
    int lat = 0;
    DelayLine limDly[2], dryDly[2];
    float limBuf[2][kMaxChunk] = {}, dryBuf[2][kMaxChunk] = {};

    bool limWanted = false;
    LinearSmoother dynMix, stMix, limMix, cmpMix, makeupS, cmpGainS, sideLoS, sideHiS;
    LoudnessMeter inMeter, outMeter;
    int publishEvery = 4800, publishCount = 0;
};

}} // namespace pump::master
