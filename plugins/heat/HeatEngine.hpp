// pump HEAT engine: 1-4 band saturation / exciter. Framework-free.
//
// Each band from the shared multiband splitter (analog or linear phase) is oversampled on its own (the common
// linear-phase halfband Oversampler, 1x / 2x / 4x / 8x; all factors are prepared up front so switching never
// allocates), driven into its curve at the high rate, and brought back down. The band's dry signal is delayed by
// exactly the oversampler's latency, so band mix and global mix line up sample-exactly (the dry path is the split
// signal itself, so mixing never comb-filters against the analog crossover's allpass).
//
// Curves (unity small-signal gain), refined from common/dsp/Saturation.hpp:
//   Warm    tanh: smooth, odd harmonics falling fast
//   Tape    algebraic soft knee (softer than tanh) with a little asymmetry, inside a pre-/de-emphasis pair (+6 dB
//           shelf above ~3 kHz before the curve, the exact inverse after it: small signals pass flat, highs
//           saturate first, as on tape), then a drive-dependent high-frequency loss (16 kHz -> 7 kHz)
//   Tube    asymmetric tanh (the positive side compresses earlier) plus a square term: even harmonics
//   Triode  strongly asymmetric: a firm knee on positive swings (grid conduction), soft on negative ones
//   Retro   cubic soft clip that flattens at 1.0 (transformer-like)
//   Clip    a clipper with a rounded quadratic knee (linear to 0.6, flat at 1.0 from 1.4): sounds like clipping
//           but has a continuous slope, so its harmonics fall off fast enough to oversample cleanly
// Retro and Clip (the curves with a flat ceiling) use first-order antiderivative anti-aliasing (ADAA) whenever
// oversampling is on. The asymmetric curves' DC is removed (4 Hz blocker on the wet signal only).
//
// Auto gain keeps the perceived level steady as drive changes: the wet signal is first divided by the drive (exact
// for small signals), then a slow per-band level match (300 ms RMS of the band's dry vs wet signal, limited to
// -6..+18 dB, frozen in silence) takes out the level the curve's compression removed. Long-term band levels follow
// the input; only the saturation's short-term density changes.
//
// Oversampling and crossover-type changes fade out, switch and fade back in (8 ms; asking for the setting in use
// again before the fade-out ends just fades back in). A non-finite value (the plugin sanitizes its input) can't get
// stuck: paths that are off (DC blocker, wet signal at mix 0) are left out rather than multiplied by 0, and a band
// whose filters, oversampler or level match went non-finite is cleared at the end of the chunk (so is the output).
#pragma once

#include "dsp/Oversampler.hpp"
#include "dsp/Multiband.hpp"
#include "HeatParams.hpp"

namespace pump { namespace heat {

struct BandSettings {
    int mode = kWarm;
    float drive = 6.0f, mix = 100.0f, level = 0.0f;
    bool solo = false, mute = false, bypass = false;
};

constexpr double kClipKnee = 0.4;

// ---- the curves (also used by the UI's transfer plot) ----
inline double curve(int type, double x)
{
    switch (type)
    {
    case kTape: { const double u = x / std::sqrt(1.0 + x * x); return u + 0.05 * u * u; }
    case kTube: { const double y = x >= 0.0 ? std::tanh(1.15 * x) / 1.15 : std::tanh(0.85 * x) / 0.85; return y - 0.08 * y * y; }
    case kTriode: return x >= 0.0 ? x / std::cbrt(1.0 + (x / 0.65) * (x / 0.65) * (x / 0.65)) : 1.35 * std::tanh(x / 1.35);
    case kRetro: { const double c = clampv(x, -1.5, 1.5); return c - (4.0 / 27.0) * c * c * c; }
    case kClip: {
        const double a = std::fabs(x), s = x < 0.0 ? -1.0 : 1.0, k = kClipKnee;
        if (a < 1.0 - k) return x;
        if (a > 1.0 + k) return s;
        const double d = a - (1.0 - k);
        return s * (a - d * d / (4.0 * k));
    }
    default: return std::tanh(x);
    }
}
inline bool usesAdaa(int type) { return type == kRetro || type == kClip; }
inline bool asymmetric(int type) { return type == kTape || type == kTube || type == kTriode; }
// antiderivatives (even functions) for ADAA
inline double curveAD(int type, double x)
{
    const double a = std::fabs(x);
    if (type == kRetro)
    {
        if (a <= 1.5) return 0.5 * a * a - a * a * a * a / 27.0;
        return 0.9375 + (a - 1.5);
    }
    const double k = kClipKnee, a0 = 1.0 - k;
    if (a < a0) return 0.5 * a * a;
    auto knee = [&](double t) { const double d = t - a0; return 0.5 * t * t - d * d * d / (12.0 * k); };
    if (a <= 1.0 + k) return knee(a);
    return knee(1.0 + k) + (a - (1.0 + k));
}

// First-order shelf pair for the tape curve (bilinear with the same prewarp, so de(pre(x)) == x exactly).
struct Shelf1 {
    double b0 = 1, b1 = 0, a1 = 0, s = 0;
    void design(double B1, double B0, double A0, double K)
    {
        const double n = K + A0;
        b0 = (B1 * K + B0) / n; b1 = (B0 - B1 * K) / n; a1 = (A0 - K) / n;
    }
    inline double process(double x) { const double y = b0 * x + s; s = b1 * x - a1 * y; return y; }
    void reset() { s = 0.0; }
};

class Engine {
public:
    static constexpr int kChunk = 256;

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        split.prepare(sr, kChunk);
        int maxLat = 0;
        for (int b = 0; b < kNumBands; ++b)
            for (int lg = 1; lg <= 3; ++lg) { os[b][lg - 1].prepare(lg, kChunk); maxLat = std::max(maxLat, os[b][lg - 1].latency()); }
        for (auto& s : st)
            for (int ch = 0; ch < 2; ++ch) { s.dry[ch].setMaxDelay(maxLat + 2); s.dry[ch].clear(); }
        for (auto& w : wetBuf) for (auto& c : w) c.assign((size_t)kChunk, 0.0f);
        const int ramp = std::max(1, (int)(0.020 * sr));
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            s.drive.setRamp(ramp); s.drive.reset(dbToGain(set[b].drive));
            s.bmix.setRamp(ramp); s.bmix.reset(set[b].mix * 0.01);
            s.level.setRamp(ramp); s.level.reset(dbToGain(set[b].level));
            s.weight.setRamp(std::max(1, (int)(0.010 * sr))); s.weight.reset(targetWeight(b));
            s.bypassFade.setRamp(ramp); s.bypassFade.reset(set[b].bypass ? 1.0 : 0.0);
            s.autoBlend.setRamp(ramp); s.autoBlend.reset(autoGain ? 1.0 : 0.0);
            s.mode = s.fromMode = set[b].mode; s.modeT = 1.0;
            s.asym.setRamp(std::max(1, (int)(0.010 * sr))); s.asym.reset(asymmetric(set[b].mode) ? 1.0 : 0.0);
            s.msDry = s.msWet = 0.0; s.agc = 1.0;
        }
        mixS.setRamp(ramp); mixS.reset(mixPct * 0.01);
        outS.setRamp(ramp); outS.reset(dbToGain(outDb));
        trans.setRamp(std::max(1, (int)(0.008 * sr))); trans.reset(1.0);
        agcK = 1.0 - tauCoef(0.3, sr);
        agcSmooth = 1.0 - tauCoef(0.15, sr);
        dcR = std::exp(-kTwoPi * 4.0 / sr);
        applyStructural();
    }

    // ---- settings (audio thread, block start) ----
    void setBands(int n) { nb = clampv(n, 1, kNumBands); split.setBands(nb); }
    void setCrossovers(float a, float b, float c) { split.setCrossovers(a, b, c); }
    void setXoverMode(int m) { wantMode = m ? 1 : 0; updatePending(); }
    void setOversampling(int idx) { wantLg = clampv(idx, 0, 3); updatePending(); }
    void setAutoGain(bool on) { autoGain = on; }
    void setMix(float pct) { mixPct = clampv((double)pct, 0.0, 100.0); }
    void setOutput(float db) { outDb = db; }
    void setListen(int band) { listen = band; }
    void setBand(int b, const BandSettings& s) { set[b] = s; }

    int latency() const { return split.latency() + osLatency(); }
    int oversampling() const { return 1 << curLg; }
    void effectiveCrossovers(double out[3]) const { split.effective(out); }
    bool kernelsCurrent() const { return split.kernelsCurrent(); }
    bool settled() const { return !pending && !trans.isSmoothing(); }

    // ---- feeds ----
    float autoGainDb(int b) const { return agFeed[b].load(std::memory_order_relaxed); }   // the level match in use
    float inLevelDb(int b) const { return inFeed[b].load(std::memory_order_relaxed); }    // band RMS (300 ms), dB
    float outLevelDb(int b) const { return outFeed[b].load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    struct BandState {
        DelayLine dry[2];
        LinearSmoother drive, bmix, level, weight, bypassFade, autoBlend, asym;
        int mode = kWarm, fromMode = kWarm;
        double modeT = 1.0;
        // per channel curve state at the oversampled rate
        double xPrev[2] = { 0, 0 };
        Shelf1 pre[2], de[2];
        double hf[2] = { 0, 0 };
        double dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 };
        double msDry = 0, msWet = 0, agc = 1.0, msOut = 0;
        int unusedFor = 0;
        bool idle = false;
    };

    double targetWeight(int b) const
    {
        if (listen >= 0 && listen < nb) return b == listen ? 1.0 : 0.0;
        bool anySolo = false;
        for (int k = 0; k < nb; ++k) anySolo = anySolo || set[k].solo;
        if (b >= nb) return anySolo ? 0.0 : 1.0;
        return anySolo ? (set[b].solo ? 1.0 : 0.0) : (set[b].mute ? 0.0 : 1.0);
    }
    int osLatency() const { return curLg == 0 ? 0 : os[0][curLg - 1].latency(); }
    // a change fades out, switches, fades back in; setting what is in use again cancels it
    void updatePending() { pending = wantMode != split.getMode() || wantLg != curLg; }
    // a band whose state went non-finite: start its curve, filters, oversampler and level match afresh
    void clearBand(int b)
    {
        BandState& s = st[b];
        if (curLg > 0) os[b][curLg - 1].reset();
        for (int ch = 0; ch < 2; ++ch) { s.dry[ch].clear(); s.xPrev[ch] = 0; s.hf[ch] = 0; s.dcX[ch] = s.dcY[ch] = 0; s.pre[ch].reset(); s.de[ch].reset(); }
        s.msDry = s.msWet = s.msOut = 0.0; s.agc = 1.0;
    }

    void applyStructural()
    {
        split.setMode(wantMode);
        curLg = wantLg;
        const int lat = osLatency();
        const double osr = sr * (1 << curLg);
        // tape shelf pair (+6 dB above ~3 kHz) and the prewarp for this rate
        const double w = kTwoPi * 3000.0, K = w / std::tan(w / (2.0 * osr)), G = 2.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            if (curLg > 0) os[b][curLg - 1].reset();
            for (int ch = 0; ch < 2; ++ch)
            {
                s.dry[ch].clear(); s.dry[ch].setDelay(lat);
                s.pre[ch].design(G, w, w, K); s.de[ch].design(1.0 / G, w / G, w / G, K);
                s.pre[ch].reset(); s.de[ch].reset();
                s.xPrev[ch] = 0.0; s.hf[ch] = 0.0; s.dcX[ch] = s.dcY[ch] = 0.0;
            }
        }
        pending = false;
    }

    // one sample of a band's curve at the oversampled rate (channel ch, type t, already driven input u)
    inline double shape(BandState& s, int ch, int t, double u, double xp, bool adaa, double hfCoef)
    {
        if (t == kTape) u = s.pre[ch].process(u);
        double y;
        if (adaa && usesAdaa(t))
        {
            const double d = u - xp;
            y = std::fabs(d) < 1e-6 ? curve(t, 0.5 * (u + xp)) : (curveAD(t, u) - curveAD(t, xp)) / d;
        }
        else y = curve(t, u);
        if (t == kTape)
        {
            y = s.de[ch].process(y);
            s.hf[ch] = y + (s.hf[ch] - y) * hfCoef;
            y = s.hf[ch];
        }
        return y;
    }

    void processChunk(float* L, float* R, int n)
    {
        if (pending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(); trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);   // (a change that was undone during the fade-out: straight back in)
        split.process(L, R, n);
        bool bad = false;
        const int used = std::min(kNumBands, std::max(nb, split.bandsInUse()));
        const int F = 1 << curLg;
        const double osr = sr * F;
        mixS.setTarget(mixPct * 0.01);
        outS.setTarget(dbToGain(outDb));
        float gm[kChunk], og[kChunk];
        for (int i = 0; i < n; ++i) { gm[i] = (float)mixS.next(); og[i] = (float)(outS.next() * trans.next()); L[i] = 0.0f; R[i] = 0.0f; }
        const double modeStep = 1.0 / (0.012 * osr);
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            const BandSettings& p = set[b];
            s.drive.setTarget(dbToGain(p.drive));
            s.bmix.setTarget(p.mix * 0.01);
            s.level.setTarget(dbToGain(p.level));
            s.weight.setTarget(targetWeight(b));
            s.bypassFade.setTarget(p.bypass ? 1.0 : 0.0);
            s.autoBlend.setTarget(autoGain ? 1.0 : 0.0);
            if (p.mode != s.mode)
            {
                // crossfade to the new curve (12 ms); a tape curve coming in starts with clean filters
                s.fromMode = s.mode; s.mode = p.mode; s.modeT = 0.0;
                if (p.mode == kTape) for (int ch = 0; ch < 2; ++ch) { s.pre[ch].reset(); s.de[ch].reset(); s.hf[ch] = 0.0; }
            }
            s.asym.setTarget(asymmetric(s.mode) ? 1.0 : 0.0);
            if (b >= used) s.unusedFor = std::min(s.unusedFor + n, 1 << 30); else s.unusedFor = 0;   // (capped: no overflow)
            if (s.unusedFor > osLatency() + kChunk)
            {
                if (!s.idle) { clearBand(b); s.idle = true; }
                for (int i = 0; i < n; ++i) { s.drive.next(); s.bmix.next(); s.level.next(); s.weight.next(); s.bypassFade.next(); s.autoBlend.next(); s.asym.next(); }
                agFeed[b].store(0.0f, std::memory_order_relaxed);
                inFeed[b].store(-120.0f, std::memory_order_relaxed);
                outFeed[b].store(-120.0f, std::memory_order_relaxed);
                continue;
            }
            s.idle = false;
            const float* xin[2] = { split.band(b, 0), split.band(b, 1) };
            // drive per base sample (shared by its oversampled samples)
            float drv[kChunk];
            for (int i = 0; i < n; ++i) drv[i] = (float)s.drive.next();
            const double hfCoef = std::exp(-kTwoPi * (16000.0 - 9000.0 * clampv(p.drive / 24.0f, 0.0f, 1.0f)) / osr);
            const bool adaa = curLg > 0;
            // ---- wet: up, curve, down
            float* wl = wetBuf[b][0].data();
            float* wr = wetBuf[b][1].data();
            double modeT0 = s.modeT;
            if (curLg == 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    double t = modeT0;
                    float* w = ch ? wr : wl;
                    for (int i = 0; i < n; ++i)
                    {
                        const double u = (double)xin[ch][i] * drv[i];
                        double y = shape(s, ch, s.mode, u, s.xPrev[ch], false, hfCoef);
                        if (t < 1.0) { y = shape(s, ch, s.fromMode, u, s.xPrev[ch], false, hfCoef) * (1.0 - t) + y * t; t = std::min(1.0, t + modeStep); }
                        s.xPrev[ch] = u;
                        w[i] = (float)y;
                    }
                    if (ch == 1) s.modeT = t;
                }
            }
            else
            {
                Oversampler& o = os[b][curLg - 1];
                float* const* up = o.upsample(xin[0], xin[1], n);
                for (int ch = 0; ch < 2; ++ch)
                {
                    double t = modeT0;
                    float* u0 = up[ch];
                    for (int i = 0; i < n; ++i)
                    {
                        const double g = drv[i];
                        for (int k = 0; k < F; ++k)
                        {
                            const int j = i * F + k;
                            const double u = (double)u0[j] * g;
                            double y = shape(s, ch, s.mode, u, s.xPrev[ch], adaa, hfCoef);
                            if (t < 1.0) { y = shape(s, ch, s.fromMode, u, s.xPrev[ch], adaa, hfCoef) * (1.0 - t) + y * t; t = std::min(1.0, t + modeStep); }
                            s.xPrev[ch] = u;
                            u0[j] = (float)y;
                        }
                    }
                    if (ch == 1) s.modeT = t;
                }
                o.downsample(wl, wr, n);
            }
            // ---- base rate: DC, auto gain, band mix / level, global mix, solo weighting
            double eOut = 0, chk = 0;
            for (int i = 0; i < n; ++i)
            {
                const double asym = s.asym.next();
                const double ab = s.autoBlend.next();
                const double byp = s.bypassFade.next();
                const double bm = s.bmix.next() * (1.0 - byp);
                const double lv = 1.0 + (s.level.next() - 1.0) * (1.0 - byp);
                const double wt = s.weight.next();
                const double g = drv[i];
                double d[2], w[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    d[ch] = s.dry[ch].process(xin[ch][i]);
                    double v = (ch ? wr : wl)[i];
                    // DC blocker for the asymmetric curves (blended in and out with the curve; when it's out it is
                    // left out, not multiplied by 0, which would pass on a NaN it holds)
                    const double y = v - s.dcX[ch] + dcR * s.dcY[ch];
                    s.dcX[ch] = v; s.dcY[ch] = y;
                    chk += v + y;
                    if (asym >= 1.0) v = y;
                    else if (asym > 0.0) v += (y - v) * asym;
                    w[ch] = v / g;   // static part of auto gain (exact for small signals)
                }
                // level match (slow, stereo-linked, frozen in silence)
                const double pd = 0.5 * (d[0] * d[0] + d[1] * d[1]), pw = 0.5 * (w[0] * w[0] + w[1] * w[1]);
                s.msDry += agcK * (pd - s.msDry);
                s.msWet += agcK * (pw - s.msWet);
                if (s.msDry > 1e-10 && s.msWet > 1e-14)
                {
                    const double target = clampv(std::sqrt(s.msDry / s.msWet), 0.5, 8.0);
                    s.agc += (target - s.agc) * agcSmooth;
                }
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double raw = w[ch] * g;                         // the curve's own level (auto gain off)
                    const double wet = raw + (w[ch] * s.agc - raw) * ab;  // auto gain on: static + level match
                    // (at mix 0 the wet signal is left out, not multiplied by 0)
                    const double band = lv * (bm > 0.0 ? d[ch] + bm * (wet - d[ch]) : d[ch]);
                    const double o = wt * (gm[i] > 0.0f ? d[ch] + gm[i] * (band - d[ch]) : d[ch]);
                    (ch ? R : L)[i] += (float)o;
                    eOut += o * o;
                    chk += d[ch];
                }
            }
            s.msOut += (eOut / (2.0 * n) - s.msOut) * std::min(1.0, n / (0.3 * sr));
            // backstop: a NaN / Inf in this band (input, curve and filter states, oversampler, level match) is
            // cleared here, once per chunk
            chk += s.msDry + s.msWet + s.msOut + s.agc;
            for (int ch = 0; ch < 2; ++ch) chk += s.xPrev[ch] + s.hf[ch] + s.pre[ch].s + s.de[ch].s;
            if (!std::isfinite(chk)) { clearBand(b); bad = true; }
            agFeed[b].store((float)(autoGain ? gainToDb(s.agc) : 0.0), std::memory_order_relaxed);
            inFeed[b].store((float)(10.0 * std::log10(std::max(1e-12, s.msDry))), std::memory_order_relaxed);
            outFeed[b].store((float)(10.0 * std::log10(std::max(1e-12, s.msOut))), std::memory_order_relaxed);
        }
        for (int i = 0; i < n; ++i) { L[i] *= og[i]; R[i] *= og[i]; }
        if (bad) { sanitizeBlock(L, n); sanitizeBlock(R, n); }   // never pass a non-finite sample on
    }

    double sr = 48000.0;
    mb::Splitter split;
    Oversampler os[kNumBands][3];
    BandSettings set[kNumBands];
    BandState st[kNumBands];
    std::vector<float> wetBuf[kNumBands][2];
    int nb = 3, wantMode = 0, wantLg = 2, curLg = 2, listen = -1;
    bool pending = false, autoGain = true;
    double mixPct = 100.0, outDb = 0.0, agcK = 1e-4, agcSmooth = 1e-4, dcR = 0.9995;
    LinearSmoother mixS, outS, trans;
    std::atomic<float> agFeed[kNumBands] {}, inFeed[kNumBands] {}, outFeed[kNumBands] {};
};

}} // namespace pump::heat
