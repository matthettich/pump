// pump TONE engine: an intelligent auto-balance EQ (auto-balance-style). Framework-free, header-only.
//
// Per STFT frame (sqrt-Hann, 75% overlap, latency exactly N), linked stereo:
//   1. 48 ERB-spaced band levels (triangular, overlapping bands on the ERB-number scale), expressed as power per
//      octave (pink noise reads flat);
//   2. two time scales: a short-term level per band (Speed) and a slow long-term spectrum;
//   3. the target = the long-term spectrum smoothed across about two octaves with a robust (peak- and
//      dip-ignoring) smoother, pulled a little toward a natural mastering slope;
//   4. deviation = short-term level - target, after removing the frame's own (loudness-weighted) mean deviation, so
//      only the spectral BALANCE counts, never the level; Bias shifts the reference and the Tame/Recover balance;
//   5. over-represented bands are cut by Tame, under-represented ones lifted by Recover, but only where there is
//      something to lift (a gate relative to the frame's loudest region and an absolute floor: noise is never
//      raised); soft limits at about +-12 dB;
//   6. band gains are smoothed in frequency and in time (Speed), Brighten (tilt) and Body (low shelf) are added as
//      smooth curves, and the result is interpolated to the bins on the ERB scale (smooth, so no ringing);
//   7. Auto Level keeps the K-weighted (loudness) average gain at 0 dB, smoothed slowly.
// Delta = delayed dry - output. Mix is latency-aligned sample-exactly.
#pragma once

#include "dsp/FFT.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Util.hpp"
#include "ToneParams.hpp"
#include <atomic>

namespace pump { namespace tone {

struct Settings {
    float recover = 30.0f, tame = 30.0f, bias = 0.0f, brighten = 0.0f, body = 0.0f, speed = 40.0f;   // percent
    bool autoLevel = true;
    float outDb = 0.0f, mix = 100.0f;
    int quality = 0;
    bool delta = false;
};

inline double erbNumber(double f) { return 21.4 * std::log10(1.0 + 0.00437 * f); }
inline double erbToHz(double e) { return (std::pow(10.0, e / 21.4) - 1.0) / 0.00437; }

inline int fftSizeFor(int quality, double sr)
{
    const double r = sr / 48000.0;
    const int e = (int)std::lround(std::log2(std::max(0.2, r)));
    int n = kQualitySizes[clampv(quality, 0, 1)];
    if (e > 0) n <<= std::min(e, 3); else if (e < 0) n >>= std::min(-e, 2);
    return std::max(512, n);
}

// Brighten (tilt around 1 kHz) and Body (low shelf around 150 Hz) as smooth gain curves, dB.
inline double tiltDb(double f, double brighten, double body)
{
    const double oct = std::log2(std::max(f, 10.0) / 1000.0);
    const double t = clampv(brighten, -1.0, 1.0) * 1.5 * clampv(oct, -5.0, 4.0);                 // +-1.5 dB/oct, +-6 dB at 16 kHz
    const double sh = 1.0 / (1.0 + std::pow(std::max(f, 1.0) / 150.0, 2.0));                     // 1 below 150 Hz, 0 above
    return t + clampv(body, -1.0, 1.0) * 5.0 * sh;                                                 // +-5 dB
}

// |H|^2 of the BS.1770 K-weighting (pre-filter shelf and RLB high-pass, the digital filters the loudness meter
// uses) at f Hz for sample rate sr.
inline double kWeightingPower(double f, double sr)
{
    BiquadCoeffs shelf, hp;
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
    const double w = kTwoPi * f / sr;
    return shelf.magSq(w) * hp.magSq(w);
}

class Engine {
public:
    static constexpr int kBands = 48;
    static constexpr int kMaxChunk = 4096;
    static constexpr double kFLo = 25.0, kFHi = 18000.0;

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int q = 0; q < 2; ++q) { sizes[q] = fftSizeFor(q, sr); stft[q].prepare(sizes[q], 2); }
        const int Kmax = sizes[1] / 2 + 1;
        binBand.assign((size_t)Kmax, 0); binFrac.assign((size_t)Kmax, 0.0f);
        binGain.assign((size_t)Kmax, 1.0f); binTiltDb.assign((size_t)Kmax, 0.0f);
        binPow.assign((size_t)Kmax, 0.0f); binDb.assign((size_t)Kmax, 0.0f); binLt.assign((size_t)Kmax, 0.0f); binKw.assign((size_t)Kmax, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            dly[c].setMaxDelay(sizes[1] + 8);
            dryBuf[c].assign(kMaxChunk, 0.0f);
            wetBuf[c].assign(kMaxChunk, 0.0f);
        }
        // band centres on the ERB scale
        const double fHi = std::min(kFHi, 0.45 * sr);
        const double e0 = erbNumber(kFLo), e1 = erbNumber(fHi);
        for (int b = 0; b < kBands; ++b)
        {
            bandE[b] = e0 + (e1 - e0) * b / (kBands - 1);
            bandF[b] = erbToHz(bandE[b]);
            // Recover never lifts the sub-bass rumble region (fades in from 28 to 42 Hz)
            recWeight[b] = clampv(std::log2(bandF[b] / 28.0) / std::log2(42.0 / 28.0), 0.0, 1.0);
        }
        mixS.setRamp((int)(0.02 * sr)); mixS.reset(cur.mix * 0.01);
        outS.setRamp((int)(0.02 * sr)); outS.reset(dbToGain(cur.outDb));
        deltaS.setRamp((int)(0.01 * sr)); deltaS.reset(cur.delta ? 1.0 : 0.0);
        swGain.setRamp((int)(0.005 * sr)); swGain.reset(1.0);
        swState = 0; pending = -1;
        activate(clampv(cur.quality, 0, 1));
    }

    void set(const Settings& s)
    {
        if (s.brighten != cur.brighten || s.body != cur.body) tiltDirty = true;
        const int q = clampv(s.quality, 0, 1);
        cur = s; cur.quality = q;
        if (q != active && q != pending) { pending = q; if (swState == 0 || swState == 3) { swState = 1; swGain.setTarget(0.0); } }
        else if (q == active && pending >= 0 && swState == 1) { pending = -1; swState = 0; swGain.setTarget(1.0); }
        mixS.setTarget(clampv(cur.mix, 0.0f, 100.0f) * 0.01);
        outS.setTarget(dbToGain(cur.outDb));
        deltaS.setTarget(cur.delta ? 1.0 : 0.0);
    }
    const Settings& settings() const { return cur; }
    int latency() const { return sizes[active]; }
    int fftSize() const { return sizes[active]; }
    int sizeFor(int q) const { return sizes[clampv(q, 0, 1)]; }

    void process(float* L, float* R, int n)
    {
        int done = 0;
        while (done < n) { const int m = std::min(kMaxChunk, n - done); processChunk(L + done, R + done, m); done += m; }
    }

    // ---- UI feeds -----------------------------------------------------------------------------------------------
    double bandFreq(int b) const { return bandF[b]; }
    double bandErb(int b) const { return bandE[b]; }
    float uiGain(int b) const { return uiG[b].load(std::memory_order_relaxed); }       // total gain incl. tilt and auto level, dB
    float uiTarget(int b) const { return uiT[b].load(std::memory_order_relaxed); }     // target level in analyzer dB (pink flat)
    float uiLevel(int b) const { return uiL[b].load(std::memory_order_relaxed); }      // short-term level, same scale
    float uiMakeup() const { return uiMk.load(std::memory_order_relaxed); }
    // gain (dB) of the curve at frequency f, interpolated the way the audio is
    static float interp(const float* g, const double* e, double f)
    {
        const double x = erbNumber(f);
        if (x <= e[0]) return g[0];
        if (x >= e[kBands - 1]) return g[kBands - 1];
        const double t = (x - e[0]) / (e[kBands - 1] - e[0]) * (kBands - 1);
        const int b = std::min(kBands - 2, (int)t);
        const float fr = (float)(t - b);
        return g[b] + (g[b + 1] - g[b]) * fr;
    }

private:
    void activate(int q)
    {
        active = q;
        N = sizes[q]; K = N / 2 + 1; H = N / 4;
        stft[q].reset();
        for (int c = 0; c < 2; ++c) { dly[c].setDelay(N); dly[c].clear(); }
        const double binHz = sr / N;
        // per bin: band weights (triangles on the ERB scale = linear interpolation between band centres)
        for (int k = 0; k < K; ++k)
        {
            const double f = std::max(1, k) * binHz;
            const double t = (erbNumber(f) - bandE[0]) / (bandE[kBands - 1] - bandE[0]) * (kBands - 1);
            binKw[(size_t)k] = (float)kWeightingPower(f, sr);
            binLt[(size_t)k] = 0.0f;
            if (t <= 0.0) { binBand[(size_t)k] = 0; binFrac[(size_t)k] = 0.0f; }
            else if (t >= kBands - 1) { binBand[(size_t)k] = kBands - 2; binFrac[(size_t)k] = 1.0f; }
            else { binBand[(size_t)k] = (int)t; binFrac[(size_t)k] = (float)(t - (int)t); }
        }
        frames = 0;
        for (int b = 0; b < kBands; ++b) { st[b] = lt[b] = -200.0; gS[b] = 0.0; }
        makeupDb = 0.0;
        tiltDirty = true;
    }

    void processChunk(float* L, float* R, int n)
    {
        if (swState == 2) { activate(pending); pending = -1; swState = 3; muteLeft = N; }
        float* io[2] = { wetBuf[0].data(), wetBuf[1].data() };
        for (int i = 0; i < n; ++i)
        {
            wetBuf[0][(size_t)i] = L[i]; wetBuf[1][(size_t)i] = R[i];
            dryBuf[0][(size_t)i] = dly[0].process(L[i]);
            dryBuf[1][(size_t)i] = dly[1].process(R[i]);
        }
        stft[active].process(io, n, &Engine::frameCb, this);
        for (int i = 0; i < n; ++i)
        {
            const float m = (float)mixS.next(), d = (float)deltaS.next();
            float g = (float)outS.next();
            if (swState == 3) { g = 0.0f; if (--muteLeft <= 0) { swState = 0; swGain.setTarget(1.0); } }
            else
            {
                g *= (float)swGain.next();
                if (swState == 1 && !swGain.isSmoothing() && swGain.current() <= 0.0) swState = 2;
            }
            const float dl = dryBuf[0][(size_t)i], dr = dryBuf[1][(size_t)i];
            const float wl = wetBuf[0][(size_t)i], wr = wetBuf[1][(size_t)i];
            const float nl = dl + (wl - dl) * m, nr = dr + (wr - dr) * m;
            const float xl = (dl - wl) * m, xr = (dr - wr) * m;
            L[i] = (nl + (xl - nl) * d) * g;
            R[i] = (nr + (xr - nr) * d) * g;
        }
    }

    static void frameCb(void* user, float* const* spectra, int channels, int fftSize)
    {
        static_cast<Engine*>(user)->frame(spectra, channels, fftSize);
    }

    static inline float binPower(const float* s, int k, int K)
    {
        if (k == 0) return s[0] * s[0];
        if (k == K - 1) return s[1] * s[1];
        return s[2 * k] * s[2 * k] + s[2 * k + 1] * s[2 * k + 1];
    }

    // Robust smoothing across bands: a running median over +-halfOct (a bump or dip narrower than the window
    // can't move it), then a raised-cosine average over half that width (so the result is smooth).
    void robustSmooth(const double* x, double* out, double halfOct)
    {
        double med[kBands], win[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            int n = 0;
            for (int j = 0; j < kBands; ++j)
                if (std::fabs(std::log2(bandF[j] / bandF[b])) <= halfOct) win[n++] = x[j];
            // near the ends the window is one-sided: mirror around the band (keeps the slope instead of flattening it)
            const int lo = b, hi = kBands - 1 - b, want = n;
            (void)want;
            std::nth_element(win, win + n / 2, win + n);
            double m = win[n / 2];
            if (n % 2 == 0) { const double a = *std::max_element(win, win + n / 2); m = 0.5 * (m + a); }
            med[b] = m;
            (void)lo; (void)hi;
        }
        const double h2 = 0.5 * halfOct;
        for (int b = 0; b < kBands; ++b)
        {
            double s = 0.0, w = 0.0;
            for (int j = 0; j < kBands; ++j)
            {
                const double d = std::fabs(std::log2(bandF[j] / bandF[b]));
                if (d > h2) continue;
                const double wt = 0.5 + 0.5 * std::cos(kPi * d / h2);
                s += med[j] * wt; w += wt;
            }
            out[b] = s / std::max(w, 1e-9);
        }
    }

    void buildTilt()
    {
        const double binHz = sr / N;
        for (int k = 0; k < K; ++k) binTiltDb[(size_t)k] = (float)tiltDb(std::max(1, k) * binHz, cur.brighten * 0.01, cur.body * 0.01);
        for (int b = 0; b < kBands; ++b) bandTilt[b] = tiltDb(bandF[b], cur.brighten * 0.01, cur.body * 0.01);
        tiltDirty = false;
    }

    void frame(float* const* spec, int channels, int fftSize)
    {
        if (fftSize != N) return;
        if (tiltDirty) buildTilt();
        float* L = spec[0];
        float* R = channels > 1 ? spec[1] : spec[0];
        // analyzer scale: a full-scale sine ~ 0 dB in its bin; per-bin mean power -> "analyzer dB" with the pink tilt
        // level scale = the UI analyzer's (pump::SpectrumView: Na-point Blackman-Harris, Na = analyzerSize(sr), band
        // averages +3 dB), so the target curve lines up with the drawn spectrum: noise of variance s^2 reads
        // 16 s^2 / Na per bin there, and has s^2 N / 2 per bin here (sqrt-Hann)
        const double norm = 32.0 / ((double)pump::analyzerSize(sr) * (double)N);
        for (int k = 0; k < K; ++k) binPow[(size_t)k] = 0.5f * (binPower(L, k, K) + binPower(R, k, K));
        double bp[kBands] = {}, bw[kBands] = {};
        for (int k = 1; k < K; ++k)
        {
            const int b = binBand[(size_t)k];
            const float fr = binFrac[(size_t)k], p = binPow[(size_t)k];
            bp[b] += p * (1.0f - fr); bw[b] += 1.0f - fr;
            bp[b + 1] += p * fr; bw[b + 1] += fr;
        }
        // band level per octave (mean bin power + 3 dB/oct: pink noise reads flat); empty bands (the low end of a
        // small FFT) borrow from their neighbours
        double lvl[kBands];
        for (int b = 0; b < kBands; ++b)
            lvl[b] = bw[b] > 0.3 ? 10.0 * std::log10(bp[b] / bw[b] * norm + 1e-30) + 3.0103 * std::log2(bandF[b] / 1000.0) : -1000.0;
        for (int b = 1; b < kBands; ++b) if (lvl[b] < -900.0) lvl[b] = lvl[b - 1];
        for (int b = kBands - 2; b >= 0; --b) if (lvl[b] < -900.0) lvl[b] = lvl[b + 1];
        for (int b = 0; b < kBands; ++b) lvl[b] = std::max(lvl[b], -160.0);

        // time scales
        const double hopSec = (double)H / sr;
        const double sp = clampv(cur.speed, 0.0f, 100.0f) * 0.01;
        const double tauG = 1.6 * std::pow(0.04, sp);            // gain smoothing: 1.6 s (relaxed) .. 64 ms (fast)
        const double tauS = std::max(0.03, 0.4 * tauG);           // short-term band level
        const double tauL = clampv(5.0 * tauG, 1.5, 8.0);         // long-term spectrum (the target follows it)
        ++frames;
        const double warm = 1.0 / (double)frames;                 // fast start: running mean for the first frames
        const double aS = std::max(1.0 - std::exp(-hopSec / tauS), warm), aL = std::max(1.0 - std::exp(-hopSec / tauL), warm);
        // smooth in the power domain (so loud moments count as loud), then dB
        for (int b = 0; b < kBands; ++b)
        {
            const double pw = std::pow(10.0, lvl[b] * 0.1);
            stP[b] += (pw - stP[b]) * aS;
            ltP[b] += (pw - ltP[b]) * aL;
            if (frames == 1) { stP[b] = ltP[b] = pw; }
            st[b] = 10.0 * std::log10(stP[b] + 1e-30);
            lt[b] = 10.0 * std::log10(ltP[b] + 1e-30);
        }
        // the target: robust ~2-octave smoothing of the long-term spectrum, pulled 30% toward a natural slope
        double tgt[kBands];
        robustSmooth(lt, tgt, 1.6);
        {
            // least-squares line through the target, 100 Hz .. 10 kHz (dB per octave)
            double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
            for (int b = 0; b < kBands; ++b)
            {
                if (bandF[b] < 100.0 || bandF[b] > 10000.0) continue;
                const double x = std::log2(bandF[b] / 1000.0);
                sx += x; sy += tgt[b]; sxx += x * x; sxy += x * tgt[b]; n += 1;
            }
            const double slope = n > 2 ? (n * sxy - sx * sy) / std::max(1e-9, n * sxx - sx * sx) : 0.0;
            const double natural = -1.0;   // dB/oct on the pink-flat scale: mastered music is a little darker than pink
            const double pull = 0.25 * clampv(natural - slope, -3.0, 3.0);
            for (int b = 0; b < kBands; ++b) tgt[b] += pull * clampv(std::log2(bandF[b] / 1000.0), -4.0, 4.0);
        }
        // Brighten / Body shape the target: the adaptive part then moves the music toward it
        for (int b = 0; b < kBands; ++b) tgt[b] += bandTilt[b];
        // gate: only bands with real content are worked on (loudest region of the frame, absolute floor)
        double stMax = -300.0;
        for (int b = 0; b < kBands; ++b) stMax = std::max(stMax, st[b]);
        double gate[kBands];
        for (int b = 0; b < kBands; ++b)
            gate[b] = clampv((st[b] - (stMax - 40.0)) / 10.0, 0.0, 1.0) * clampv((st[b] + 100.0) / 12.0, 0.0, 1.0);
        // deviation, without the frame's own mean (only the balance matters)
        double dev[kBands], sw = 0.0, sd = 0.0;
        for (int b = 0; b < kBands; ++b) { dev[b] = st[b] - tgt[b]; const double w = gate[b] * (bandF[b] > 60.0 && bandF[b] < 12000.0 ? 1.0 : 0.3); sd += dev[b] * w; sw += w; }
        const double bias = clampv(cur.bias, -100.0f, 100.0f) * 0.01;
        const double meanDev = sw > 0 ? sd / sw : 0.0;
        // Bias: + = more taming (reference lower and Tame stronger), - = more recovering
        const double ref = meanDev - 2.0 * bias;
        // amount of the deviation taken away. The target sits a little into a broad bump or dip (it is the music's own
        // smoothed spectrum), so 100% takes away a bit more than the measured deviation.
        auto amount = [](double x) { return std::min(1.25, 1.25 * std::pow(clampv(x, 0.0, 1.0), 0.8)); };
        const double tame = amount(clampv(cur.tame, 0.0f, 100.0f) * 0.01 * (1.0 + 0.6 * bias));
        const double rec = amount(clampv(cur.recover, 0.0f, 100.0f) * 0.01 * (1.0 - 0.6 * bias));
        double g[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const double d = dev[b] - ref;
            const double ad = std::max(0.0, std::fabs(d) - 0.5);   // a 0.5 dB dead zone: nothing to do when close
            double v = d > 0.0 ? -tame * ad : rec * ad;
            // never lift what isn't there: noise far below the music, or a band far below the target (a filter's
            // skirt, an empty octave) is absent, not masked
            if (d < 0.0) v *= gate[b] * clampv((d + 20.0) / 8.0, 0.0, 1.0) * recWeight[b];
            else v *= std::min(1.0, 0.25 + gate[b]);
            g[b] = 12.0 * std::tanh(v / 12.0);                     // soft limit, about +-12 dB
        }
        // smooth across frequency ([1 2 1] twice: about 2 ERB)
        for (int pass = 0; pass < 2; ++pass)
        {
            double t[kBands];
            for (int b = 0; b < kBands; ++b) t[b] = 0.25 * g[b > 0 ? b - 1 : b] + 0.5 * g[b] + 0.25 * g[b < kBands - 1 ? b + 1 : b];
            for (int b = 0; b < kBands; ++b) g[b] = t[b];
        }
        // the smoothing must not carry a lift into a gated region (the noise below a low cut, say)
        for (int b = 0; b < kBands; ++b) if (g[b] > 0.0) g[b] *= gate[b];
        // smooth in time (dB, one pole: Speed)
        const double aG = std::max(1.0 - std::exp(-hopSec / tauG), frames < 8 ? 0.5 : 0.0);
        for (int b = 0; b < kBands; ++b) gS[b] += (g[b] - gS[b]) * aG;
        // ... and the share of the tilt the adaptive part can't deliver (little Recover / Tame) is applied directly,
        // so Brighten and Body always do what they say
        // (the adaptive part delivers about 70% of a target change at high settings: keep a third directly)
        const float direct = (float)clampv(1.0 - 0.33 * (std::min(1.0, tame) + std::min(1.0, rec)), 0.34, 1.0);
        double totalDb[kBands];
        for (int b = 0; b < kBands; ++b) totalDb[b] = gS[b] + direct * bandTilt[b];
        // the gain curve per bin (dB, without the auto level makeup)
        bool shaping = cur.brighten != 0.0f || cur.body != 0.0f;
        for (int b = 0; b < kBands && !shaping; ++b) if (gS[b] != 0.0) shaping = true;
        if (shaping)
        {
            float gf[kBands];
            for (int b = 0; b < kBands; ++b) gf[b] = (float)gS[b];
            for (int k = 0; k < K; ++k)
            {
                const int b = binBand[(size_t)k];
                const float fr = binFrac[(size_t)k];
                binDb[(size_t)k] = gf[b] + (gf[b + 1] - gf[b]) * fr;
            }
            // At the low end ERB bands are narrower than a bin: smooth the curve across bins too ([1 2 1] eight
            // times, a Gaussian of about 2 bins), so the gain never changes abruptly from bin to bin: its impulse
            // response stays short against the frame and nothing smears into empty regions (above ~1 kHz the ERB
            // curve is far smoother than this anyway)
            for (int pass = 0; pass < 8; ++pass)
            {
                float prev = binDb[0];
                for (int k = 1; k < K - 1; ++k) { const float c = binDb[(size_t)k]; binDb[(size_t)k] = 0.25f * prev + 0.5f * c + 0.25f * binDb[(size_t)k + 1]; prev = c; }
            }
            for (int k = 0; k < K; ++k) binDb[(size_t)k] += direct * binTiltDb[(size_t)k];
        }
        // auto level: the K-weighted (BS.1770) energy of the long-term spectrum before and after the curve, per bin
        {
            const double aR = std::max(1.0 - std::exp(-hopSec / 3.0), warm);   // ~ the span of integrated loudness
            for (int k = 0; k < K; ++k) binLt[(size_t)k] += (binPow[(size_t)k] - binLt[(size_t)k]) * (float)aR;
        }
        if (cur.autoLevel)
        {
            double target = 0.0;
            if (shaping)
            {
                double eIn = 0.0, eOut = 0.0;
                for (int k = 1; k < K; ++k)
                {
                    const double pw = (double)binLt[(size_t)k] * binKw[(size_t)k];
                    const float gl = dbToGainF(binDb[(size_t)k]);
                    eIn += pw; eOut += pw * gl * gl;
                }
                target = eIn > 1e-30 && eOut > 1e-30 ? clampv(-10.0 * std::log10(eOut / eIn), -12.0, 12.0) : 0.0;
            }
            const double aM = std::max(1.0 - std::exp(-hopSec / 0.3), frames < 8 ? 0.5 : 0.0);
            makeupDb += (target - makeupDb) * aM;
            if (!shaping && std::fabs(makeupDb) < 1e-4) makeupDb = 0.0;
        }
        else makeupDb = 0.0;
        // apply
        if (shaping || makeupDb != 0.0)
        {
            const float mk = (float)makeupDb;
            for (int k = 0; k < K; ++k) binGain[(size_t)k] = dbToGainF((shaping ? binDb[(size_t)k] : 0.0f) + mk);
            L[0] *= binGain[0]; R[0] *= binGain[0];
            L[1] *= binGain[(size_t)K - 1]; R[1] *= binGain[(size_t)K - 1];
            for (int k = 1; k < K - 1; ++k)
            {
                const float gk = binGain[(size_t)k];
                L[2 * k] *= gk; L[2 * k + 1] *= gk; R[2 * k] *= gk; R[2 * k + 1] *= gk;
            }
        }
        // UI
        for (int b = 0; b < kBands; ++b)
        {
            uiG[b].store((float)(totalDb[b] + makeupDb), std::memory_order_relaxed);
            const double toAna = 1.4897 * std::log2(bandF[b] / 1000.0);   // pink-flat -> the analyzer's 4.5 dB/oct
            uiT[b].store((float)(tgt[b] + ref + toAna), std::memory_order_relaxed);
            uiL[b].store((float)(st[b] + toAna), std::memory_order_relaxed);
        }
        uiMk.store((float)makeupDb, std::memory_order_relaxed);
    }

    // ---- data ----------------------------------------------------------------------------------------------------
    double sr = 48000.0;
    Settings cur;
    STFT stft[2];
    int sizes[2] = { 2048, 4096 };
    int active = 0, pending = -1;
    int N = 2048, K = 1025, H = 512;
    long frames = 0;
    double bandE[kBands] = {}, bandF[kBands] = {}, bandTilt[kBands] = {}, recWeight[kBands] = {};
    double st[kBands] = {}, lt[kBands] = {}, stP[kBands] = {}, ltP[kBands] = {}, gS[kBands] = {};
    double makeupDb = 0.0;
    bool tiltDirty = true;
    std::vector<int> binBand;
    std::vector<float> binFrac, binGain, binTiltDb, binPow, binDb, binLt, binKw;
    std::atomic<float> uiG[kBands], uiT[kBands], uiL[kBands], uiMk { 0.0f };
    DelayLine dly[2];
    std::vector<float> dryBuf[2], wetBuf[2];
    LinearSmoother mixS, outS, deltaS, swGain;
    int swState = 0, muteLeft = 0;
};

}} // namespace pump::tone
