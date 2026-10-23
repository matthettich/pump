// pump WIDE engine: 1-4 band stereo imager. Framework-free.
//
// Each band (from the shared multiband splitter, analog or linear phase) is taken to mid/side:
//   width      scales the band's side: 0% = mono, 100% = unchanged, 200% = twice the side
//   stereoize  adds a decorrelated copy of the band's MID to its side: s += a * D(m), with D a cascade of four
//              Schroeder allpasses (short, band-specific delays). L = m + s and R = m - s, so the added part cancels
//              exactly in L + R: mono compatibility is untouched, only the side gains energy
//   mono bass  (on the whole output, applied per band so each band's correlation reads what you hear) removes the
//              side below a frequency with a Linkwitz-Riley high-pass (LR4 24 dB/oct or LR8 48 dB/oct) and gives
//              the mid the matching allpass, so mid and side stay phase-coherent above the split
// Then balance and output gain. Correlation (per band and overall) is measured on the processed signal with a
// 300 ms window. Every setting glides; crossover-type changes fade out and back in (8 ms; switching back before
// the fade-out ends just fades back in). A band that stops being used (fewer bands) keeps running until its
// stereoize / mono-bass tail has rung out (below -100 dBFS, at most 250 ms), so the tail is never cut off.
// A non-finite value (the plugin sanitizes its input) can't get stuck: paths that are off are skipped, not
// multiplied by 0, and a band whose decorrelator, filters or meters went non-finite is cleared at the end of the
// chunk (so is the chunk's output).
#pragma once

#include "dsp/Multiband.hpp"
#include "WideParams.hpp"

namespace pump { namespace wide {

struct BandSettings {
    float width = 100.0f, stereoize = 0.0f;
    bool solo = false, mute = false, bypass = false;
};

// Four Schroeder allpasses in series (mono). |H| = 1 at every frequency; the phase scrambles quickly with
// frequency, so D(m) is uncorrelated with m over any band wider than a few hundred Hz.
class Decorrelator {
public:
    void prepare(double sr, int variant)
    {
        static const double base[4] = { 1.31, 2.07, 3.19, 4.91 };   // ms
        const double scale = 1.0 + 0.137 * variant;
        for (int s = 0; s < 4; ++s)
        {
            const int d = std::max(1, (int)std::lround(base[s] * scale * 0.001 * sr));
            int size = 1; while (size < d + 2) size <<= 1;
            st[s].buf.assign((size_t)size, 0.0f);
            st[s].mask = size - 1; st[s].d = d; st[s].w = 0;
            st[s].g = (s & 1) ? -0.5f : 0.5f;
        }
    }
    void reset() { for (auto& s : st) { std::fill(s.buf.begin(), s.buf.end(), 0.0f); s.w = 0; } }
    inline float process(float x)
    {
        for (auto& s : st)
        {
            // w[n] = x[n] + g w[n-D];  y[n] = -g w[n] + w[n-D]
            const float old = s.buf[(size_t)((s.w - s.d) & s.mask)];
            const float wv = x + s.g * old;
            s.buf[(size_t)s.w] = wv;
            s.w = (s.w + 1) & s.mask;
            x = -s.g * wv + old;
        }
        return x;
    }
private:
    struct Stage { std::vector<float> buf { 0.0f, 0.0f }; int mask = 1, d = 1, w = 0; float g = 0.5f; };
    Stage st[4];
};

// Linkwitz-Riley mono-bass pair for one band: high-pass for the side, matching allpass for the mid.
// LR4 = (2nd-order Butterworth)^2, LR8 = (4th-order Butterworth)^2; LP + HP = the allpass D(-s)/D(s).
struct MonoBassFilters {
    SVF hp4a, hp4b, ap4;                 // LR4: two Butterworth HP2 (Q 0.7071), one AP2
    SVF hp8a, hp8b, hp8c, hp8d, ap8a, ap8b;   // LR8: Q 0.5412 / 1.3066 pairs
    void set(double f, double sr)
    {
        const double q4 = 0.70710678118654752, q8a = 0.54119610014619698, q8b = 1.30656296487637652;
        hp4a.set(f, q4, sr); hp4b.set(f, q4, sr); ap4.set(f, q4, sr);
        hp8a.set(f, q8a, sr); hp8b.set(f, q8b, sr); hp8c.set(f, q8a, sr); hp8d.set(f, q8b, sr);
        ap8a.set(f, q8a, sr); ap8b.set(f, q8b, sr);
    }
    void reset4() { hp4a.reset(); hp4b.reset(); ap4.reset(); }
    void reset8() { hp8a.reset(); hp8b.reset(); hp8c.reset(); hp8d.reset(); ap8a.reset(); ap8b.reset(); }
    bool finite() const
    {
        double s = 0.0;
        for (const SVF* f : { &hp4a, &hp4b, &ap4, &hp8a, &hp8b, &hp8c, &hp8d, &ap8a, &ap8b }) s += f->ic1 + f->ic2;
        return std::isfinite(s);
    }
    static inline double hp(SVF& f, double x) { double l, b, h; f.process(x, l, b, h); return h; }
    static inline double ap(SVF& f, double x) { double l, b, h; f.process(x, l, b, h); return x - 2.0 * f.k * b; }
    inline void lr4(double m, double s, double& mo, double& so) { so = hp(hp4b, hp(hp4a, s)); mo = ap(ap4, m); }
    inline void lr8(double m, double s, double& mo, double& so) { so = hp(hp8d, hp(hp8c, hp(hp8b, hp(hp8a, s)))); mo = ap(ap8b, ap(ap8a, m)); }
};

class Engine {
public:
    static constexpr int kChunk = 256;

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        split.prepare(sr, kChunk);
        const int ramp = std::max(1, (int)(0.020 * sr));
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            s.deco.prepare(sr, b);
            s.width.setRamp(ramp); s.stereo.setRamp(ramp); s.weight.setRamp(std::max(1, (int)(0.010 * sr))); s.bypassFade.setRamp(ramp);
            s.width.reset(set[b].width * 0.01); s.stereo.reset(set[b].stereoize * 0.01 * kStereoGain);
            s.weight.reset(targetWeight(b)); s.bypassFade.reset(set[b].bypass ? 1.0 : 0.0);
            s.xy = s.xx = s.yy = 0.0; s.tailPeak = 0.0;
            s.mb.reset4(); s.mb.reset8();
        }
        monoAmt.setRamp(ramp); monoAmt.reset(monoOn ? 1.0 : 0.0);
        slopeMix.setRamp(ramp); slopeMix.reset(monoSlope ? 1.0 : 0.0);
        live4 = live8 = false;
        monoCur = monoFreq; applyMonoFreq();
        balL.setRamp(ramp); balR.setRamp(ramp); outS.setRamp(ramp);
        double gl, gr; balanceGains(gl, gr); balL.reset(gl); balR.reset(gr); outS.reset(dbToGain(outDb));
        trans.setRamp(std::max(1, (int)(0.008 * sr))); trans.reset(1.0);
        split.setMode(wantMode); modePending = false;
        corrK = 1.0 - std::exp(-1.0 / (0.3 * sr));
        axy = axx = ayy = 0.0;
    }

    // ---- settings (audio thread, block start) ----
    void setBands(int n) { nb = clampv(n, 1, kNumBands); split.setBands(nb); }
    void setCrossovers(float a, float b, float c) { split.setCrossovers(a, b, c); }
    // fades out, switches, fades back in; asking for the type in use again cancels a pending switch
    void setXoverMode(int m) { wantMode = m ? 1 : 0; modePending = wantMode != split.getMode(); }
    void setMonoBass(bool on, float freq, int slope) { monoOn = on; monoFreq = clampv((double)freq, 20.0, 500.0); monoSlope = slope ? 1 : 0; }
    void setBalance(float pct) { balance = clampv((double)pct, -100.0, 100.0) * 0.01; }
    void setOutput(float db) { outDb = db; }
    void setListen(int band) { listen = band; }
    void setBand(int b, const BandSettings& s) { set[b] = s; }

    int latency() const { return split.latency(); }
    void effectiveCrossovers(double out[3]) const { split.effective(out); }
    bool kernelsCurrent() const { return split.kernelsCurrent(); }

    // ---- feeds ----
    float correlation(int b) const { return corrFeed[b].load(std::memory_order_relaxed); }    // -1..1
    float bandLevel(int b) const { return levelFeed[b].load(std::memory_order_relaxed); }     // dB (L+R power)
    float correlationAll() const { return corrAll.load(std::memory_order_relaxed); }
    float levelAll() const { return levelAllF.load(std::memory_order_relaxed); }
    static float corrOf(double xy, double xx, double yy) { const double d = std::sqrt(xx * yy); return d > 1e-14 ? (float)clampv(xy / d, -1.0, 1.0) : 0.0f; }

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    static constexpr double kStereoGain = 0.8;   // 100% stereoize adds the decorrelated mid at 0.8 x
    // an unused band runs on until its tail is below -100 dBFS, for at most 250 ms
    static constexpr double kTailFloor = 1e-5, kTailMaxS = 0.25;

    struct BandState {
        Decorrelator deco;
        MonoBassFilters mb;
        LinearSmoother width, stereo, weight, bypassFade;
        double xy = 0, xx = 0, yy = 0;
        double tailPeak = 0;   // the band's largest |mid| + |side| over its last chunk
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
    void balanceGains(double& gl, double& gr) const
    {
        // balance: turn the opposite side down (constant at the centre, -inf at the extremes)
        gl = balance > 0.0 ? 1.0 - balance : 1.0;
        gr = balance < 0.0 ? 1.0 + balance : 1.0;
    }
    void applyMonoFreq() { for (auto& s : st) s.mb.set(monoCur, sr); }

    void processChunk(float* L, float* R, int n)
    {
        if (modePending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { split.setMode(wantMode); modePending = false; trans.setTarget(1.0); }
        }
        else trans.setTarget(1.0);   // (a switch that was undone during the fade-out: straight back in)
        // mono-bass frequency glide (per chunk), on/off and slope crossfades
        if (monoCur != monoFreq)
        {
            const double a = 1.0 - std::exp(-n / (0.010 * sr));
            monoCur = std::exp(std::log(monoCur) + (std::log(monoFreq) - std::log(monoCur)) * a);
            if (std::fabs(std::log(monoCur / monoFreq)) < 1e-4) monoCur = monoFreq;
            applyMonoFreq();
        }
        monoAmt.setTarget(monoOn ? 1.0 : 0.0);
        slopeMix.setTarget(monoSlope ? 1.0 : 0.0);
        const bool monoActive = monoAmt.isSmoothing() || monoAmt.current() > 0.0;
        const bool need4 = monoActive && (slopeMix.isSmoothing() || slopeMix.current() < 1.0);
        const bool need8 = monoActive && (slopeMix.isSmoothing() || slopeMix.current() > 0.0);
        if (need4 && !live4) for (auto& s : st) s.mb.reset4();
        if (need8 && !live8) for (auto& s : st) s.mb.reset8();
        live4 = need4; live8 = need8;
        double gl, gr; balanceGains(gl, gr);
        balL.setTarget(gl); balR.setTarget(gr); outS.setTarget(dbToGain(outDb));

        split.process(L, R, n);
        const int used = std::min(kNumBands, std::max(nb, split.bandsInUse()));
        float mono[kChunk], slope[kChunk];
        for (int i = 0; i < n; ++i) { mono[i] = (float)monoAmt.next(); slope[i] = (float)slopeMix.next(); L[i] = 0.0f; R[i] = 0.0f; }
        bool bad = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            const BandSettings& p = set[b];
            s.width.setTarget(p.width * 0.01);
            s.stereo.setTarget(p.stereoize * 0.01 * kStereoGain);
            s.weight.setTarget(targetWeight(b));
            s.bypassFade.setTarget(p.bypass ? 1.0 : 0.0);
            // A band that carries nothing any more (its routing ramp has ended, so its input is silence) keeps
            // running until its tail - the decorrelator and mono-bass filters ring for tens of ms - is below
            // -100 dBFS (or for at most 250 ms), so a band-count change never cuts it off.
            if (b >= used) s.unusedFor = std::min(s.unusedFor + n, 1 << 30); else s.unusedFor = 0;   // (capped: no overflow)
            if (s.unusedFor > kChunk && (s.tailPeak < kTailFloor || s.unusedFor > (int)(kTailMaxS * sr)))
            {
                if (!s.idle) { s.deco.reset(); s.mb.reset4(); s.mb.reset8(); s.xy = s.xx = s.yy = 0.0; s.idle = true; }
                for (int i = 0; i < n; ++i) { s.width.next(); s.stereo.next(); s.weight.next(); s.bypassFade.next(); }
                corrFeed[b].store(0.0f, std::memory_order_relaxed);
                levelFeed[b].store(-120.0f, std::memory_order_relaxed);
                continue;
            }
            s.idle = false;
            const float* xl = split.band(b, 0);
            const float* xr = split.band(b, 1);
            double chk = 0.0, peak = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double byp = s.bypassFade.next();
                const double w = 1.0 + (s.width.next() - 1.0) * (1.0 - byp);
                const double a = s.stereo.next() * (1.0 - byp);
                const double wt = s.weight.next();
                const double m = 0.5 * ((double)xl[i] + xr[i]), sd = 0.5 * ((double)xl[i] - xr[i]);
                const double d = s.deco.process((float)m);
                // (paths that are off are left out, never multiplied by 0: that would pass a NaN on)
                double mo = m, so = w * sd;
                if (a != 0.0) so += a * d;
                if (monoActive)
                {
                    double m4 = 0, s4 = 0, m8 = 0, s8 = 0;
                    if (need4) s.mb.lr4(mo, so, m4, s4);
                    if (need8) s.mb.lr8(mo, so, m8, s8);
                    const double t = slope[i], k = mono[i];
                    const bool use8 = need8 && (!need4 || t >= 1.0), use4 = need4 && (!need8 || t <= 0.0);
                    const double mm = use8 ? m8 : (use4 ? m4 : m4 + (m8 - m4) * t);
                    const double ss = use8 ? s8 : (use4 ? s4 : s4 + (s8 - s4) * t);
                    if (k >= 1.0) { mo = mm; so = ss; }
                    else if (k > 0.0) { mo += (mm - mo) * k; so += (ss - so) * k; }
                }
                chk += d + mo + so;
                peak = std::max(peak, std::fabs(mo) + std::fabs(so));
                const double l = (mo + so) * wt, r = (mo - so) * wt;
                s.xy += corrK * (l * r - s.xy); s.xx += corrK * (l * l - s.xx); s.yy += corrK * (r * r - s.yy);
                L[i] += (float)l; R[i] += (float)r;
            }
            s.tailPeak = peak;
            // backstop: a NaN / Inf in this band (its input, decorrelator, filters or meters) is cleared here
            if (!std::isfinite(chk + s.xy + s.xx + s.yy) || !s.mb.finite())
            {
                s.deco.reset(); s.mb.reset4(); s.mb.reset8(); s.xy = s.xx = s.yy = 0.0; s.tailPeak = 0.0;
                bad = true;
            }
            corrFeed[b].store(corrOf(s.xy, s.xx, s.yy), std::memory_order_relaxed);
            levelFeed[b].store((float)(10.0 * std::log10(std::max(1e-12, 0.5 * (s.xx + s.yy)))), std::memory_order_relaxed);
        }
        if (bad) { sanitizeBlock(L, n); sanitizeBlock(R, n); }   // never pass a non-finite sample on
        for (int i = 0; i < n; ++i)
        {
            const double g = outS.next() * trans.next();
            const double l = L[i] * g * balL.next(), r = R[i] * g * balR.next();
            L[i] = (float)l; R[i] = (float)r;
            axy += corrK * (l * r - axy); axx += corrK * (l * l - axx); ayy += corrK * (r * r - ayy);
        }
        if (!std::isfinite(axy + axx + ayy)) axy = axx = ayy = 0.0;
        corrAll.store(corrOf(axy, axx, ayy), std::memory_order_relaxed);
        levelAllF.store((float)(10.0 * std::log10(std::max(1e-12, 0.5 * (axx + ayy)))), std::memory_order_relaxed);
    }

    double sr = 48000.0;
    mb::Splitter split;
    BandSettings set[kNumBands];
    BandState st[kNumBands];
    int nb = 4, wantMode = 0, listen = -1, monoSlope = 0;
    bool modePending = false, monoOn = false, live4 = false, live8 = false;
    double monoFreq = 120.0, monoCur = 120.0, balance = 0.0, outDb = 0.0, corrK = 1e-4;
    double axy = 0, axx = 0, ayy = 0;
    LinearSmoother monoAmt, slopeMix, balL, balR, outS, trans;
    std::atomic<float> corrFeed[kNumBands] {}, levelFeed[kNumBands] {}, corrAll { 0.0f }, levelAllF { -120.0f };
};

}} // namespace pump::wide
