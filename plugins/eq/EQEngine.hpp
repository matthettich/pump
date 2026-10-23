// pump EQ engine: 24 bands of analog-matched filters (zero latency) or one linear-phase FIR (2x2 matrix for
// mid/side bands), per-band dynamic EQ, solo/audition, auto gain. Framework-free.
#pragma once

#include "dsp/Biquad.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Dynamics.hpp"
#include "EQParams.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

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

    ~Engine() { stopWorker(); }

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        maxBlk = std::max(1, maxBlock);
        for (auto& b : bands) { b.hasCur = false; b.needFull = true; for (auto& c : b.st) for (auto& s : c) s.reset(); }
        outSmooth.setRamp((int)(0.02 * sr)); outSmooth.reset(dbToGain(outGainDb + autoGainDb));
        soloDet[0].reset(); soloDet[1].reset(); soloDet2[0].reset(); soloDet2[1].reset();
        configureLinear();
    }

    // --- settings (call from the audio thread at block start) ---
    BandState& band(int i) { return bands[i]; }
    void setBand(int i, bool used, bool bypass, const BandSpec& s, int place, bool dyn, float range, float thresh)
    {
        BandState& b = bands[i];
        const bool shapeChange = s.shape != b.spec.shape || s.freq != b.spec.freq || s.q != b.spec.q || s.slope != b.spec.slope;
        if (shapeChange) b.needFull = true;
        if (b.used != used || b.place != place) b.needFull = true;
        if (!used || !b.used) { for (auto& c : b.st) for (auto& x : c) x.reset(); b.hasCur = false; }
        b.used = used; b.bypass = bypass; b.spec = s; b.place = place; b.dyn = dyn && shapeHasGain(s.shape);
        b.dynRange = range; b.dynThresh = thresh;
        if (!b.dyn) b.offset = 0.0;
        linDirty = true;
        autoDirty = true;
    }
    void setGainScale(double pct) { if (pct != gainScale) { gainScale = pct; linDirty = true; autoDirty = true; } }
    void setOutput(double db) { outGainDb = db; }
    void setAutoGain(bool on) { if (on != autoGain) { autoGain = on; autoDirty = true; } }
    void setDynTimes(double atkMs, double relMs) { atkMsV = atkMs; relMsV = relMs; }
    void setSolo(int bandIndex) { solo = bandIndex; }   // -1 none
    // phase: 0 zero latency, 1 linear; quality 0..3
    void setPhaseMode(int mode, int quality)
    {
        if (mode == phaseMode && quality == linQuality) return;
        phaseMode = mode; linQuality = quality;
        for (auto& b : bands) { b.needFull = true; b.hasCur = false; b.offset = 0.0; for (auto& c : b.st) for (auto& x : c) x.reset(); }
        configureLinear();
    }
    int latency() const { return phaseMode == 1 ? linLatency : 0; }

    // dB offsets for the UI
    double dynOffset(int i) const { return bands[i].offset; }
    double autoGainValue() const { return autoGainDb; }

    // Effective static gain of a band (gain scale applied)
    double effGain(const BandState& b) const { return b.spec.gainDb * gainScale * 0.01; }

    void process(float* L, float* R, int n)
    {
        if (autoDirty) { updateAutoGain(); autoDirty = false; }
        if (phaseMode == 1)
        {
            if (linDirty) { requestKernel(); linDirty = false; }
            conv.process(L, R, n);
            processDynamicDeltas(L, R, n);
        }
        else
            processZeroLatency(L, R, n);

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
    // ----- zero latency -----
    void designBand(BandState& b, bool full)
    {
        BandSpec s = b.spec;
        s.gainDb = effGain(b) + b.offset;
        const AnalogBand a = analogPrototype(s, sr);
        if (full || b.target.n != a.n) b.target.design(a, sr);
        else b.target.redesignFast(a);
        b.lastGain = s.gainDb;
        if (!b.hasCur || b.cur.n != b.target.n) { b.cur = b.target; b.hasCur = true; b.rampLeft = 0; }
        else b.rampLeft = 4;
        if (full) { designDetector(b.spec, sr, b.det, b.detN); }
        b.needFull = false;
    }

    inline void stepCoeffs(BandState& b)
    {
        if (b.rampLeft <= 0) return;
        const double t = 1.0 / b.rampLeft;
        for (int k = 0; k < b.cur.n; ++k) b.cur.c[k] = BiquadCoeffs::lerp(b.cur.c[k], b.target.c[k], t);
        --b.rampLeft;
    }

    inline double detectLevel(BandState& b, const float* L, const float* R, int s0, int s1, bool fromDelta = false)
    {
        (void)fromDelta;
        // peak level of the band region over the chunk, on the band's own channel
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
        // envelope in dB: attack when rising
        const double ca = std::exp(-samples / (std::max(0.05, atkMsV) * 0.001 * sr));
        const double cr = std::exp(-samples / (std::max(1.0, relMsV) * 0.001 * sr));
        b.env = levelDb + (b.env - levelDb) * (levelDb > b.env ? ca : cr);
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
        for (int bi = 0; bi < kBands; ++bi)
        {
            BandState& b = bands[bi];
            if (!b.active()) continue;
            if (b.needFull || !b.hasCur) designBand(b, true);
            else if (std::fabs(effGain(b) + b.offset - b.lastGain) > 1e-6) designBand(b, false);
            for (int s0 = 0; s0 < n; s0 += kChunk)
            {
                const int s1 = std::min(n, s0 + kChunk);
                if (b.dyn)
                {
                    updateDynamic(b, detectLevel(b, L, R, s0, s1), s1 - s0);
                    if (std::fabs(effGain(b) + b.offset - b.lastGain) > 0.01) designBand(b, false);
                }
                stepCoeffs(b);
                filterSpan(b, L, R, s0, s1);
            }
            for (auto& c : b.st) for (auto& x : c) x.sanitize();
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
    // Dynamic bands in linear-phase mode: the static curve is in the FIR; the dynamic part is a minimum-phase
    // filter of just the offset (unity when the band is idle), applied after it.
    void processDynamicDeltas(float* L, float* R, int n)
    {
        for (int bi = 0; bi < kBands; ++bi)
        {
            BandState& b = bands[bi];
            if (!b.active() || !b.dyn) continue;
            if (b.needFull) { designDetector(b.spec, sr, b.det, b.detN); b.needFull = false; b.hasCur = false; }
            for (int s0 = 0; s0 < n; s0 += kChunk)
            {
                const int s1 = std::min(n, s0 + kChunk);
                updateDynamic(b, detectLevel(b, L, R, s0, s1), s1 - s0);
                BandSpec s = b.spec; s.gainDb = b.offset;
                const AnalogBand a = analogPrototype(s, sr);
                if (!b.hasCur || b.target.n != a.n) { b.target.design(a, sr); b.cur = b.target; b.hasCur = true; b.rampLeft = 0; }
                else { b.target.redesignFast(a); b.rampLeft = 2; }
                stepCoeffs(b);
                filterSpan(b, L, R, s0, s1);
            }
        }
    }

    void configureLinear()
    {
        if (phaseMode != 1) { stopWorker(); return; }
        const int base = linQuality == 0 ? 4096 : linQuality == 1 ? 8192 : linQuality == 2 ? 16384 : 32768;
        int fftN = base;
        if (sr > 50000.0) fftN *= 2;
        if (sr > 100000.0) fftN *= 2;
        linFFT = fftN;
        conv.prepare(512, fftN);
        linLatency = (fftN / 2 - 1) + conv.latency();
        startWorker();
        linDirty = true;
        // build the first kernel right away (synchronously) so there is no pass-through gap
        buildKernel(snapshot(), true);
    }

    struct Snapshot {
        struct Bd { bool on; BandSpec s; int place; } b[kNumBands];
        double scale;
    };
    Snapshot snapshot() const
    {
        Snapshot s;
        for (int i = 0; i < kBands; ++i) { s.b[i].on = bands[i].active(); s.b[i].s = bands[i].spec; s.b[i].place = bands[i].place; }
        s.scale = gainScale;
        return s;
    }

    void buildKernel(const Snapshot& snap, bool now)
    {
        const int N = linFFT, half = N / 2;
        std::vector<double> HL((size_t)half + 1, 1.0), HR((size_t)half + 1, 1.0), HM((size_t)half + 1, 1.0), HS((size_t)half + 1, 1.0);
        bool ms = false;
        for (int i = 0; i < kBands; ++i)
        {
            const auto& bd = snap.b[i];
            if (!bd.on) continue;
            BandSpec s = bd.s; s.gainDb = s.gainDb * snap.scale * 0.01;
            const AnalogBand a = analogPrototype(s, sr);
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
        auto k = conv.makeKernel(p, taps);
        if (now) conv.setKernelNow(k); else conv.setKernel(k);
    }

    void requestKernel()
    {
        std::unique_lock<std::mutex> lk(wMutex, std::try_to_lock);
        if (!lk.owns_lock()) { linDirty = true; return; }
        wSnap = snapshot();
        wPending = true;
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
                wCv.wait(lk, [this] { return wPending || wQuit; });
                if (wQuit) break;
                const Snapshot s = wSnap;
                wPending = false;
                lk.unlock();
                buildKernel(s, false);
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

    // ----- solo -----
    void applySolo(float* L, float* R, int n)
    {
        BandState& b = bands[solo];
        if (soloFor != solo || soloSpec != b.spec)
        {
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
    BandState bands[kNumBands];
    double gainScale = 100.0, outGainDb = 0.0, autoGainDb = 0.0;
    bool autoGain = false, autoDirty = true;
    double atkMsV = 10.0, relMsV = 120.0;
    int solo = -1, soloFor = -2, soloN = 2;
    BandSpec soloSpec;
    BiquadCoeffs soloC[2];
    BiquadState soloDet[2], soloDet2[2];
    LinearSmoother outSmooth;

    int phaseMode = 0, linQuality = 1, linFFT = 8192, linLatency = 0;
    bool linDirty = true;
    StereoConvolver conv;
    std::thread wThread;
    std::mutex wMutex;
    std::condition_variable wCv;
    Snapshot wSnap;
    bool wPending = false, wQuit = false;
};

}} // namespace pump::eq
