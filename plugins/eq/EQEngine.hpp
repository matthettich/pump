// pump EQ engine: 24 bands of analog-matched filters (zero latency) or one linear-phase FIR (2x2 matrix for
// mid/side bands), per-band dynamic EQ, solo/audition, auto gain. Framework-free.
#pragma once

#include "dsp/Biquad.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Dynamics.hpp"
#include "EQParams.hpp"
#include <memory>
#if !PUMP_SYNC_KERNELS
 #include <chrono>
 #include <condition_variable>
 #include <mutex>
 #include <thread>
#endif

namespace pump { namespace eq {

struct BandState {
    // settings
    bool used = false, bypass = false, dyn = false;
    BandSpec spec;
    int place = 0;          // Place
    float dynRange = -6.0f, dynThresh = -30.0f;
    // running filter (zero latency): current and target coefficients, interpolated in 16-sample steps
    DigitalBand cur, target;
    bool hasCur = false;
    int rampLeft = 0;
    BiquadState st[2][kMaxSections];
    // dynamic detector (band-pass/shelf-region filter on the band's own channel) and gain offset
    BiquadCoeffs det[2];
    int detN = 1;
    BiquadState detSt[2][2];
    double env = -120.0;    // detector level, dB
    double offset = 0.0;    // current dynamic gain offset, dB
    bool needFull = true;   // shape/freq/q/slope changed: full design
    double lastGain = 1e9;  // the gain the target was designed for
    double lastStatic = 1e9;// the static gain (gain scale applied) at the last design
    double anchor = 1e9;    // the gain the matching points were chosen at (full design)
    float mix = 0.0f;       // 0..1: fades a band in/out when it's switched on/off (no clicks)

    bool active() const { return used && !bypass; }
};

// Design the detector filter(s) for a band's dynamic sidechain (and for solo/audition).
inline void designDetector(const BandSpec& s, double sr, BiquadCoeffs* out, int& n)
{
    AnalogBand a;
    const double w0 = kTwoPi * clampv(s.freq, 10.0, 0.45 * sr) / sr;
    AnalogSection sec;
    switch (s.shape)
    {
    case Shape::LowShelf: case Shape::LowCut:   // the region below f
        sec.n2 = 0; sec.n1 = 0; sec.n0 = w0 * w0; sec.d2 = 1; sec.d1 = w0 / 0.7071; sec.d0 = w0 * w0; break;
    case Shape::HighShelf: case Shape::HighCut: case Shape::TiltShelf:   // the region above f
        sec.n2 = 1; sec.n1 = 0; sec.n0 = 0; sec.d2 = 1; sec.d1 = w0 / 0.7071; sec.d0 = w0 * w0; break;
    default: {   // band-pass at the band's own width (at least a little wide so it hears something)
        const double q = clampv(s.q, 0.3, 12.0);
        sec.n2 = 0; sec.n1 = w0 / q; sec.n0 = 0; sec.d2 = 1; sec.d1 = w0 / q; sec.d0 = w0 * w0; break;
    }
    }
    BiquadCoeffs c;
    bestDesign(sec, sr, c);
    out[0] = c; out[1] = c;   // two in series: steeper skirts for the detector
    n = 2;
}

class Engine {
public:
    static constexpr int kBands = kNumBands;
    static constexpr int kChunk = 16;
    static constexpr int kScratch = 4096;   // linear phase: detector input is processed in pieces of this size
    static constexpr int kLinBlock = 512;   // convolver partition size
    static constexpr int kDesignsPerBlock = 4;

    Engine() { wRetire.reserve(8); }
    ~Engine() { stopWorker(); }

    // Not on the audio thread (activate()): allocates, and builds a linear-phase kernel synchronously if needed.
    void prepare(double sampleRate, int maxBlock)
    {
        stopWorker();   // nothing may build against the old rate while it changes
        sr = sampleRate;
        maxBlk = std::max(1, maxBlock);
        fadeStep = (float)(1.0 / std::max(1.0, 0.01 * sr));   // band on/off fades: 10 ms
        for (auto& b : bands) { resetBand(b); b.mix = b.active() ? 1.0f : 0.0f; b.env = -120.0; }
        outSmooth.setRamp((int)(0.02 * sr)); outSmooth.reset(dbToGain(outGainDb + (autoGain ? autoGainDb : 0.0)));
        soloDet[0].reset(); soloDet[1].reset(); soloDet2[0].reset(); soloDet2[1].reset();
        soloFor = -2;   // the solo filter is designed for a sample rate too
        for (auto& d : detDly) { d.setMaxDelay(1 << 17); d.setDelay(0); }
        scratchL.assign((size_t)kScratch, 0.0f); scratchR.assign((size_t)kScratch, 0.0f);
        // whatever the worker had in flight was for the old rate
        wNext.reset(); wNextReady = false; wPending = false; wReconfPending = false; wKernelTarget.reset();
        for (auto& c : wRetire) c.reset();
        wRetire.clear();
        conv.reset(); convN = 0; phaseMode = 0;
        if (wantMode == 1) buildLinearNow();
        startWorker();
    }

    // --- settings (call from the audio thread at block start) ---
    BandState& band(int i) { return bands[i]; }
    void setBand(int i, bool used, bool bypass, const BandSpec& s, int place, bool dyn, float range, float thresh)
    {
        BandState& b = bands[i];
        const bool wasActive = b.active();
        const bool shapeChange = s.shape != b.spec.shape || s.freq != b.spec.freq || s.q != b.spec.q || s.slope != b.spec.slope;
        if (shapeChange) b.needFull = true;
        if (b.used != used || b.place != place) b.needFull = true;
        b.used = used; b.bypass = bypass; b.spec = s; b.place = place; b.dyn = dyn && shapeHasGain(s.shape);
        b.dynRange = range; b.dynThresh = thresh;
        if (!b.dyn) b.offset = 0.0;
        // a band switched back on starts from rest (stale filter state would replay as a burst) and fades in
        if (!wasActive && b.active() && b.mix <= 0.0f) resetBand(b);
        linDirty = true;
        autoDirty = true;
    }
    void setGainScale(double pct) { if (pct != gainScale) { gainScale = pct; linDirty = true; autoDirty = true; } }
    void setOutput(double db) { outGainDb = db; }
    void setAutoGain(bool on) { if (on != autoGain) { autoGain = on; autoDirty = true; } }
    void setDynTimes(double atkMs, double relMs) { atkMsV = atkMs; relMsV = relMs; }
    void setSolo(int bandIndex) { solo = bandIndex; }   // -1 none
    // phase: 0 zero latency, 1 linear; quality 0..3. Switching to linear phase (or to another quality) never
    // blocks the audio thread: a convolver of the new size is built on the worker and swapped in by process()
    // a few blocks later, together with the new latency. Until then the current mode keeps running.
    void setPhaseMode(int mode, int quality)
    {
        if (mode == wantMode && quality == wantQuality) return;
        wantMode = mode; wantQuality = quality;
        if (mode != 1)
        {
            if (phaseMode == 1) { phaseMode = 0; resetBandFilters(); }
            return;
        }
        if (phaseMode == 1 && convN == fftFor(quality)) { wantGen = curGen; return; }   // already running at that size
        ++wantGen;
    }
    // Blocking: finish a pending switch to linear phase now. For activate() and offline use, never the audio thread.
    void finishPending()
    {
        if (wantMode != 1 || curGen == wantGen) return;
        stopWorker();
        wNext.reset(); wNextReady = false; wReconfPending = false; wPending = false; wKernelTarget.reset();
        for (auto& c : wRetire) c.reset();
        wRetire.clear();
        buildLinearNow();
        startWorker();
    }
    int latency() const { return phaseMode == 1 ? linLatency : 0; }

    // dB offsets for the UI
    double dynOffset(int i) const { return bands[i].offset; }
    double autoGainValue() const { return autoGainDb; }

    // Effective static gain of a band (gain scale applied)
    double effGain(const BandState& b) const { return b.spec.gainDb * gainScale * 0.01; }

    void process(float* L, float* R, int n)
    {
        // a non-finite input sample must never get into filter states, detectors or the FIR history
        for (int i = 0; i < n; ++i) { if (!std::isfinite(L[i])) L[i] = 0.0f; if (!std::isfinite(R[i])) R[i] = 0.0f; }
        if (autoDirty) { updateAutoGain(); autoDirty = false; }
        if (wantMode == 1 && curGen != wantGen) pollLinear();
        if (phaseMode == 1) processLinear(L, R, n);
        else processZeroLatency(L, R, n);

        // solo (audition): replace the output with the selected band's region of the signal
        if (solo >= 0 && solo < kBands && bands[solo].used)
            applySolo(L, R, n);

        // output gain (+ auto gain), smoothed
        outSmooth.setTarget(dbToGain(outGainDb + (autoGain ? autoGainDb : 0.0)));
        for (int i = 0; i < n; ++i)
        {
            const float g = (float)outSmooth.next();
            L[i] *= g; R[i] *= g;
        }
    }

    // Combined static response (dB) at f, as heard (zero latency: digital; linear: analog). For displays.
    double responseDb(double f, int channelView /*0 = L/stereo, 1 = R, 2 = M, 3 = S*/) const
    {
        (void)channelView;
        double m = 1.0;
        for (const auto& b : bands)
        {
            if (!b.active()) continue;
            BandSpec s = b.spec; s.gainDb = effGain(b);
            const AnalogBand a = analogPrototype(s, sr);
            m *= a.magSq(kTwoPi * f / sr);
        }
        return 10.0 * std::log10(std::max(m, 1e-30));
    }

private:
    void resetBand(BandState& b)
    {
        b.needFull = true; b.hasCur = false; b.offset = 0.0; b.anchor = 1e9; b.lastStatic = 1e9;
        for (auto& c : b.st) for (auto& x : c) x.reset();
        for (auto& c : b.detSt) for (auto& x : c) x.reset();
    }
    void resetBandFilters() { for (auto& b : bands) { resetBand(b); b.mix = b.active() ? 1.0f : 0.0f; } }

    // ----- zero latency -----
    enum Design { kFast = 0, kAnchor, kFull };
    // Matching points (bestDesign) are chosen by a full design at the band's static gain (plus the dynamic range
    // for dynamic bands, where the gain spends its time). A fast redesign reuses them, which is only accurate
    // near that gain: at 0 dB every choice fits, so a band created flat and then boosted must be re-anchored.
    double anchorGain(const BandState& b) const { return effGain(b) + (b.dyn ? (double)b.dynRange : 0.0); }
    void designBand(BandState& b, Design how)
    {
        BandSpec s = b.spec;
        s.gainDb = effGain(b) + b.offset;
        const AnalogBand a = analogPrototype(s, sr);
        const int prevN = b.hasCur ? b.cur.n : 0;
        if (how != kFast || b.target.n != a.n || b.anchor > 1e8)
        {
            BandSpec sa = b.spec; sa.gainDb = anchorGain(b);
            const AnalogBand aa = analogPrototype(sa, sr);
            if (aa.n == a.n) { b.target.design(aa, sr); if (std::fabs(sa.gainDb - s.gainDb) > 1e-9) b.target.redesignFast(a); }
            else b.target.design(a, sr);
            b.anchor = sa.gainDb;
        }
        else b.target.redesignFast(a);
        b.lastGain = s.gainDb;
        b.lastStatic = effGain(b);
        if (!b.hasCur || b.cur.n != b.target.n)
        {
            // sections that weren't running hold stale state: start them from rest
            for (int k = prevN; k < b.target.n; ++k) { b.st[0][k].reset(); b.st[1][k].reset(); }
            b.cur = b.target; b.hasCur = true; b.rampLeft = 0;
        }
        else b.rampLeft = 4;
        if (how == kFull) { designDetector(b.spec, sr, b.det, b.detN); }
        b.needFull = false;
    }

    inline void stepCoeffs(BandState& b)
    {
        if (b.rampLeft <= 0) return;
        const double t = 1.0 / b.rampLeft;
        for (int k = 0; k < b.cur.n; ++k) b.cur.c[k] = BiquadCoeffs::lerp(b.cur.c[k], b.target.c[k], t);
        --b.rampLeft;
    }

    // peak level of the band region over [s0, s1) of the detector input, on the band's own channel
    inline double detectLevel(BandState& b, const float* L, const float* R, int s0, int s1)
    {
        double pk = 0.0;
        for (int i = s0; i < s1; ++i)
        {
            double x0, x1 = 0.0;
            switch (b.place)
            {
            case kLeft: x0 = L[i]; break;
            case kRight: x0 = R[i]; break;
            case kMid: x0 = 0.5 * (L[i] + R[i]); break;
            case kSide: x0 = 0.5 * (L[i] - R[i]); break;
            default: x0 = L[i]; x1 = R[i]; break;
            }
            double y0 = x0, y1 = x1;
            for (int k = 0; k < b.detN; ++k) y0 = b.detSt[0][k].process(b.det[k], y0);
            if (b.place == kStereo) for (int k = 0; k < b.detN; ++k) y1 = b.detSt[1][k].process(b.det[k], y1);
            pk = std::max(pk, std::max(std::fabs(y0), std::fabs(y1)));
        }
        return gainToDb(pk);
    }

    inline void updateDynamic(BandState& b, double levelDb, int samples)
    {
        if (!std::isfinite(levelDb)) levelDb = -120.0;
        // envelope in dB: attack when rising
        const double ca = std::exp(-samples / (std::max(0.05, atkMsV) * 0.001 * sr));
        const double cr = std::exp(-samples / (std::max(1.0, relMsV) * 0.001 * sr));
        b.env = levelDb + (b.env - levelDb) * (levelDb > b.env ? ca : cr);
        if (!std::isfinite(b.env)) b.env = -120.0;
        // 2:1 soft-knee law toward the dynamic range
        const double over = b.env - b.dynThresh, knee = 6.0;
        double amt;
        if (over <= -knee * 0.5) amt = 0.0;
        else if (over < knee * 0.5) amt = 0.5 * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
        else amt = 0.5 * over;
        const double range = b.dynRange;
        b.offset = range < 0 ? -std::min(-range, amt) : std::min(range, amt);
    }

    void processZeroLatency(float* L, float* R, int n)
    {
        // full designs cost ~0.1 ms each: a preset load, A/B or mode switch changes all 24 bands at once, so they
        // are spread over a few blocks (a band waiting for its turn keeps its old curve, or fades in when new)
        int budget = kDesignsPerBlock;
        for (int bi = 0; bi < kBands; ++bi)
        {
            BandState& b = bands[bi];
            const bool on = b.active();
            if (!on && b.mix <= 0.0f) continue;
            if (on)
            {
                if (b.needFull || !b.hasCur)
                {
                    if (budget > 0) { designBand(b, kFull); --budget; }
                    else if (!b.hasCur) { b.mix = 0.0f; continue; }   // not designed yet: silent this block
                }
                else
                {
                    const double ag = anchorGain(b), d = std::fabs(ag - b.anchor);
                    const bool staticMoved = std::fabs(effGain(b) - b.lastStatic) > 1e-6;
                    // re-anchor when far from the anchor, when leaving a ~0 dB anchor, or once the gain settles
                    const bool reanchor = d > 1.0 || (std::fabs(b.anchor) < 0.5 && std::fabs(ag) >= 0.5) || (d > 1e-6 && !staticMoved);
                    if (reanchor && budget > 0) { designBand(b, kAnchor); --budget; }
                    else if (std::fabs(effGain(b) + b.offset - b.lastGain) > 1e-6) designBand(b, kFast);
                }
            }
            else if (!b.hasCur) { b.mix = 0.0f; continue; }   // nothing running to fade out
            for (int s0 = 0; s0 < n; s0 += kChunk)
            {
                const int s1 = std::min(n, s0 + kChunk);
                if (on && b.dyn)
                {
                    updateDynamic(b, detectLevel(b, L, R, s0, s1), s1 - s0);
                    if (std::fabs(effGain(b) + b.offset - b.lastGain) > 0.01) designBand(b, kFast);
                }
                stepCoeffs(b);
                const float target = on ? 1.0f : 0.0f;
                if (b.mix == target) { if (on) filterSpan(b, L, R, s0, s1); continue; }   // faded out: passes dry
                float inL[kChunk], inR[kChunk];
                for (int i = s0; i < s1; ++i) { inL[i - s0] = L[i]; inR[i - s0] = R[i]; }
                filterSpan(b, L, R, s0, s1);
                float m = b.mix;
                for (int i = s0; i < s1; ++i)
                {
                    m = on ? std::min(1.0f, m + fadeStep) : std::max(0.0f, m - fadeStep);
                    L[i] = inL[i - s0] + (L[i] - inL[i - s0]) * m;
                    R[i] = inR[i - s0] + (R[i] - inR[i - s0]) * m;
                }
                b.mix = m;
            }
            for (auto& c : b.st) for (auto& x : c) x.sanitize();
            for (auto& c : b.detSt) for (auto& x : c) x.sanitize();
            if (!on && b.mix <= 0.0f) resetBand(b);   // fully faded out: next time it starts from rest
        }
    }

    inline void filterSpan(BandState& b, float* L, float* R, int s0, int s1)
    {
        const int ns = b.cur.n;
        const BiquadCoeffs* c = b.cur.c;
        switch (b.place)
        {
        case kLeft:
            for (int i = s0; i < s1; ++i) { double x = L[i]; for (int k = 0; k < ns; ++k) x = b.st[0][k].process(c[k], x); L[i] = (float)x; }
            break;
        case kRight:
            for (int i = s0; i < s1; ++i) { double x = R[i]; for (int k = 0; k < ns; ++k) x = b.st[1][k].process(c[k], x); R[i] = (float)x; }
            break;
        case kMid: case kSide:
            for (int i = s0; i < s1; ++i)
            {
                double m = 0.5 * ((double)L[i] + R[i]), s = 0.5 * ((double)L[i] - R[i]);
                double& x = b.place == kMid ? m : s;
                for (int k = 0; k < ns; ++k) x = b.st[0][k].process(c[k], x);
                L[i] = (float)(m + s); R[i] = (float)(m - s);
            }
            break;
        default:
            for (int i = s0; i < s1; ++i)
            {
                double x = L[i], y = R[i];
                for (int k = 0; k < ns; ++k) { x = b.st[0][k].process(c[k], x); y = b.st[1][k].process(c[k], y); }
                L[i] = (float)x; R[i] = (float)y;
            }
            break;
        }
    }

    // ----- linear phase -----
    int fftFor(int quality) const
    {
        int fftN = quality <= 0 ? 4096 : quality == 1 ? 8192 : quality == 2 ? 16384 : 32768;
        if (sr > 50000.0) fftN *= 2;
        if (sr > 100000.0) fftN *= 2;
        return fftN;
    }
    void processLinear(float* L, float* R, int n)
    {
       #if PUMP_SYNC_KERNELS
        sinceBuild = sinceBuild > (1 << 28) ? sinceBuild : sinceBuild + n;
       #endif
        if (linDirty) { linDirty = false; requestKernel(); }   // requestKernel re-arms linDirty if it can't hand off now
        for (int off = 0; off < n; off += kScratch)
        {
            const int m = std::min(kScratch, n - off);
            float* l = L + off; float* r = R + off;
            // dynamic detectors listen to the input (delayed to line up with the FIR output), as in zero latency
            // mode: not to the FIR output, which already contains the band's own static boost/cut
            for (int i = 0; i < m; ++i) { scratchL[(size_t)i] = detDly[0].process(l[i]); scratchR[(size_t)i] = detDly[1].process(r[i]); }
            conv->process(l, r, m);
            processDynamicDeltas(l, r, scratchL.data(), scratchR.data(), m);
        }
    }
    // Dynamic bands in linear-phase mode: the static curve is in the FIR; the dynamic part is a minimum-phase
    // filter of just the offset (unity when the band is idle), applied after it.
    void processDynamicDeltas(float* L, float* R, const float* dL, const float* dR, int n)
    {
        for (int bi = 0; bi < kBands; ++bi)
        {
            BandState& b = bands[bi];
            if (!b.active() || !b.dyn) continue;
            if (b.needFull) { designDetector(b.spec, sr, b.det, b.detN); b.needFull = false; b.hasCur = false; }
            for (int s0 = 0; s0 < n; s0 += kChunk)
            {
                const int s1 = std::min(n, s0 + kChunk);
                updateDynamic(b, detectLevel(b, dL, dR, s0, s1), s1 - s0);
                BandSpec s = b.spec; s.gainDb = b.offset;
                const AnalogBand a = analogPrototype(s, sr);
                if (!b.hasCur || b.target.n != a.n || b.anchor != (double)b.dynRange)
                {
                    // matching points chosen at the full dynamic range (at 0 dB every choice looks equally good)
                    BandSpec sa = b.spec; sa.gainDb = b.dynRange;
                    const AnalogBand aa = analogPrototype(sa, sr);
                    if (aa.n == a.n) { b.target.design(aa, sr); b.target.redesignFast(a); }
                    else b.target.design(a, sr);
                    b.anchor = b.dynRange;
                    if (!b.hasCur || b.cur.n != b.target.n)
                    {
                        const int prevN = b.hasCur ? b.cur.n : 0;
                        for (int k = prevN; k < b.target.n; ++k) { b.st[0][k].reset(); b.st[1][k].reset(); }
                        b.cur = b.target; b.hasCur = true; b.rampLeft = 0;
                    }
                    else b.rampLeft = 2;
                }
                else { b.target.redesignFast(a); b.rampLeft = 2; }
                stepCoeffs(b);
                filterSpan(b, L, R, s0, s1);
            }
            for (auto& c : b.st) for (auto& x : c) x.sanitize();
            for (auto& c : b.detSt) for (auto& x : c) x.sanitize();
        }
    }

    struct Snapshot {
        struct Bd { bool on; BandSpec s; int place; } b[kNumBands];
        double scale;
        double sr;
    };
    Snapshot snapshot() const
    {
        Snapshot s;
        for (int i = 0; i < kBands; ++i) { s.b[i].on = bands[i].active(); s.b[i].s = bands[i].spec; s.b[i].place = bands[i].place; }
        s.scale = gainScale;
        s.sr = sr;
        return s;
    }

    void adopt(std::shared_ptr<StereoConvolver> c, int N)
    {
        const bool wasLinear = phaseMode == 1;
        conv = std::move(c);   // conv was empty (handed to the worker) or this isn't the audio thread
        convN = N; phaseMode = 1;
        linLatency = (N / 2 - 1) + conv->latency();
        for (auto& d : detDly) { d.clear(); d.setDelay(linLatency); }
        if (!wasLinear) resetBandFilters();
        linDirty = true;   // the kernel was built from the settings at request time
    }
    // not on the audio thread
    void buildLinearNow()
    {
        const int N = fftFor(wantQuality);
        auto c = std::make_shared<StereoConvolver>();
        c->prepare(kLinBlock, N);
        buildKernel(*c, N, snapshot(), true);
        adopt(std::move(c), N);
        curGen = wReqGen = wantGen;
    }
#if PUMP_SYNC_KERNELS
    // No worker (PUMP_SYNC_KERNELS): the convolver of the new size is built here, on the calling thread, and taken at once.
    void pollLinear()
    {
        buildLinearNow();
        sinceBuild = 0;
    }
#else
    // audio thread: swap in a convolver the worker finished, or ask for one
    void pollLinear()
    {
        std::unique_lock<std::mutex> lk(wMutex, std::try_to_lock);
        if (!lk.owns_lock()) return;
        if (wNextReady && wNextGen == wantGen)
        {
            if (conv && wRetire.size() >= wRetire.capacity()) return;   // the worker hasn't freed earlier ones yet
            if (conv) wRetire.push_back(std::move(conv));                // freed on the worker, never here
            std::shared_ptr<StereoConvolver> c = std::move(wNext);
            const int N = wNextN;
            wNextReady = false;
            lk.unlock();
            wCv.notify_one();
            adopt(std::move(c), N);
            curGen = wantGen;
            return;
        }
        if (wReqGen != wantGen)
        {
            wReqGen = wantGen; wReqN = fftFor(wantQuality); wReqSnap = snapshot(); wReconfPending = true;
            lk.unlock();
            wCv.notify_one();
        }
    }

#endif

    void buildKernel(StereoConvolver& target, int N, const Snapshot& snap, bool now)
    {
        const int half = N / 2;
        std::vector<double> HL((size_t)half + 1, 1.0), HR((size_t)half + 1, 1.0), HM((size_t)half + 1, 1.0), HS((size_t)half + 1, 1.0);
        bool ms = false;
        for (int i = 0; i < kBands; ++i)
        {
            const auto& bd = snap.b[i];
            if (!bd.on) continue;
            BandSpec s = bd.s; s.gainDb = s.gainDb * snap.scale * 0.01;
            const AnalogBand a = analogPrototype(s, snap.sr);
            std::vector<double>* tgt[2] = { nullptr, nullptr };
            switch (bd.place)
            {
            case kLeft: tgt[0] = &HL; break;
            case kRight: tgt[0] = &HR; break;
            case kMid: tgt[0] = &HM; ms = true; break;
            case kSide: tgt[0] = &HS; ms = true; break;
            default: tgt[0] = &HL; tgt[1] = &HR; break;
            }
            for (int k = 0; k <= half; ++k)
            {
                const double m = std::sqrt(a.magSq(kTwoPi * k / N));
                for (auto* t : tgt) if (t) (*t)[(size_t)k] *= m;
            }
        }
        // 2x2 matrix: out = Q P in, P = diag(HL, HR), Q = M/S stage
        std::vector<float> irs[4];
        const bool cross = ms;
        RealFFT fft(N);
        AlignedBuffer spec((size_t)N), tmp((size_t)N);
        const int taps = N - 1, D = half - 1;
        auto make = [&](int which, std::vector<float>& out) {
            for (int k = 0; k <= half; ++k)
            {
                double v;
                const double l = HL[(size_t)k], r = HR[(size_t)k], m = HM[(size_t)k], s = HS[(size_t)k];
                switch (which)
                {
                case 0: v = 0.5 * (m + s) * l; break;
                case 1: v = 0.5 * (m - s) * r; break;
                case 2: v = 0.5 * (m - s) * l; break;
                default: v = 0.5 * (m + s) * r; break;
                }
                if (k == 0) spec[0] = (float)v;
                else if (k == half) spec[1] = (float)v;
                else { spec[(size_t)(2 * k)] = (float)v; spec[(size_t)(2 * k + 1)] = 0.0f; }
            }
            fft.inverse(spec.data(), tmp.data());
            out.assign((size_t)taps, 0.0f);
            for (int t = 0; t < taps; ++t)
            {
                const int idx = (t - D + N) % N;
                // Tukey window: flat over the middle 70%, cosine tapers on the outer 15% each side. Keeps the
                // full length for long low-frequency impulse tails (steep low cuts) while still fading to zero.
                const double u = (t + 1.0) / (taps + 1.0), a = 0.30;
                double w = 1.0;
                if (u < 0.5 * a) w = 0.5 * (1.0 - std::cos(kTwoPi * u / a));
                else if (u > 1.0 - 0.5 * a) w = 0.5 * (1.0 - std::cos(kTwoPi * (1.0 - u) / a));
                out[(size_t)t] = (float)(tmp[(size_t)idx] / N * w);
            }
        };
        make(0, irs[0]); make(3, irs[3]);
        if (cross) { make(1, irs[1]); make(2, irs[2]); }
        const float* p[4] = { irs[0].data(), cross ? irs[1].data() : nullptr, cross ? irs[2].data() : nullptr, irs[3].data() };
        auto k = target.makeKernel(p, taps);
        if (now) target.setKernelNow(k); else target.setKernel(k);
    }

#if PUMP_SYNC_KERNELS
    // No worker: the current convolver gets its new kernel here (crossfaded in, as the worker's would be). A burst of
    // changes (a drag) is built at most every kSyncEverySec of audio; linDirty stays set until then, and the last setting
    // is always built.
    static constexpr double kSyncEverySec = 0.08;
    void requestKernel()
    {
        if (!conv) return;
        if (sinceBuild < (int)(kSyncEverySec * sr)) { linDirty = true; return; }
        buildKernel(*conv, convN, snapshot(), false);
        sinceBuild = 0;
    }
    void startWorker() {}
    void stopWorker() {}
#else
    void requestKernel()
    {
        std::unique_lock<std::mutex> lk(wMutex, std::try_to_lock);
        if (!lk.owns_lock()) { linDirty = true; return; }
        // wKernelTarget is always empty or shares an object that wRetire/conv also hold: never freed here
        wSnap = snapshot(); wKernelTarget = conv; wKernelN = convN; wPending = true;
        lk.unlock();
        wCv.notify_one();
    }
    void startWorker()
    {
        if (wThread.joinable()) return;
        wQuit = false;
        wThread = std::thread([this] {
            std::unique_lock<std::mutex> lk(wMutex);
            while (!wQuit)
            {
                wCv.wait_for(lk, std::chrono::milliseconds(50), [this] { return wQuit || wPending || wReconfPending || !wRetire.empty(); });
                if (wQuit) break;
                // take every job under the lock (the audio thread only ever try-locks it), work without it
                std::vector<std::shared_ptr<StereoConvolver>> dead;
                for (auto& c : wRetire) dead.push_back(std::move(c));
                wRetire.clear();   // keeps its capacity: the audio thread pushes without allocating
                const bool reconf = wReconfPending; const int rN = wReqN, rGen = wReqGen;
                Snapshot rs; if (reconf) rs = wReqSnap;
                wReconfPending = false;
                const bool kern = wPending; Snapshot ks; if (kern) ks = wSnap;
                std::shared_ptr<StereoConvolver> target = std::move(wKernelTarget);
                const int kN = wKernelN;
                wPending = false;
                lk.unlock();
                dead.clear();   // retired convolvers are freed here
                if (reconf)
                {
                    auto c = std::make_shared<StereoConvolver>();
                    c->prepare(kLinBlock, rN);
                    buildKernel(*c, rN, rs, true);
                    std::shared_ptr<StereoConvolver> stale;
                    lk.lock();
                    if (rGen == wReqGen) { stale = std::move(wNext); wNext = std::move(c); wNextN = rN; wNextGen = rGen; wNextReady = true; }
                    lk.unlock();
                    // `stale`, or `c` if it's no longer wanted, is freed here
                }
                if (kern && target) buildKernel(*target, kN, ks, false);
                target.reset();
                lk.lock();
            }
        });
    }
    void stopWorker()
    {
        if (!wThread.joinable()) return;
        { std::lock_guard<std::mutex> lk(wMutex); wQuit = true; }
        wCv.notify_one();
        wThread.join();
    }

#endif

    // ----- solo -----
    void applySolo(float* L, float* R, int n)
    {
        BandState& b = bands[solo];
        if (soloFor != solo || soloSpec != b.spec)
        {
            if (soloFor != solo) { soloDet[0].reset(); soloDet[1].reset(); soloDet2[0].reset(); soloDet2[1].reset(); }
            designDetector(b.spec, sr, soloC, soloN);
            soloFor = solo; soloSpec = b.spec;
        }
        for (int i = 0; i < n; ++i)
        {
            double x = L[i], y = R[i];
            x = soloDet[0].process(soloC[0], x); x = soloDet2[0].process(soloC[1], x);
            y = soloDet[1].process(soloC[0], y); y = soloDet2[1].process(soloC[1], y);
            L[i] = (float)x; R[i] = (float)y;
        }
        soloDet[0].sanitize(); soloDet[1].sanitize(); soloDet2[0].sanitize(); soloDet2[1].sanitize();
    }

    // ----- auto gain: compensate the curve's average level over the musically dense range -----
    void updateAutoGain()
    {
        double sum = 0.0, wsum = 0.0;
        for (int i = 0; i < 48; ++i)
        {
            const double f = 40.0 * std::pow(12000.0 / 40.0, i / 47.0);
            const double w = 1.0 / (1.0 + std::pow(f / 6000.0, 2.0)) * (1.0 - 1.0 / (1.0 + std::pow(f / 60.0, 2.0)));
            sum += responseDb(f, 0) * w; wsum += w;
        }
        autoGainDb = clampv(-sum / std::max(wsum, 1e-9), -24.0, 24.0);
    }

    double sr = 48000.0;
    int maxBlk = 512;
    float fadeStep = 1.0f / 480.0f;
    BandState bands[kNumBands];
    double gainScale = 100.0, outGainDb = 0.0, autoGainDb = 0.0;
    bool autoGain = false, autoDirty = true;
    double atkMsV = 10.0, relMsV = 120.0;
    int solo = -1, soloFor = -2, soloN = 2;
    BandSpec soloSpec;
    BiquadCoeffs soloC[2];
    BiquadState soloDet[2], soloDet2[2];
    LinearSmoother outSmooth;

    // phase mode: what's running (audio thread) vs. what was asked for
    int phaseMode = 0, linLatency = 0, convN = 0;
    int wantMode = 0, wantQuality = 1, wantGen = 0, curGen = 0;
    bool linDirty = true;
    std::shared_ptr<StereoConvolver> conv;
    DelayLine detDly[2];
    std::vector<float> scratchL, scratchR;

#if PUMP_SYNC_KERNELS
    int sinceBuild = 1 << 28;   // samples since the last kernel build (no worker: see requestKernel)
    // the worker's fields that prepare() / finishPending() reset (unused without a worker)
    bool wPending = false, wReconfPending = false, wNextReady = false;
    int wReqGen = 0;
    std::shared_ptr<StereoConvolver> wKernelTarget, wNext;
    std::vector<std::shared_ptr<StereoConvolver>> wRetire;
#else
    // worker (kernel builds, convolver builds, frees); everything below is guarded by wMutex
    std::thread wThread;
    std::mutex wMutex;
    std::condition_variable wCv;
    bool wQuit = false;
    bool wPending = false; Snapshot wSnap {}; std::shared_ptr<StereoConvolver> wKernelTarget; int wKernelN = 0;
    bool wReconfPending = false; int wReqN = 0, wReqGen = 0; Snapshot wReqSnap {};
    std::shared_ptr<StereoConvolver> wNext; bool wNextReady = false; int wNextN = 0, wNextGen = -1;
    std::vector<std::shared_ptr<StereoConvolver>> wRetire;
#endif
};

}} // namespace pump::eq
