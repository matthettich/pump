// pump BAND engine: 1-4 band dynamics (downward compression, downward expansion, upward compression) on an analog
// (Linkwitz-Riley, zero latency) or linear-phase (exact reconstruction) crossover. Framework-free.
//
// Per band and channel:
//   detector   Peak: |x|; RMS: sqrt of a 20 ms moving average of x^2 (a sine reads 3 dB under its peak)
//   hold       a sliding maximum over 0.6 / (the band's lowest frequency), 0.5..30 ms: steady tones read their
//              exact level with no ripple, and release starts after the hold
//   link       detector levels (dB) mix toward the louder channel's by the stereo-link amount
//   envelope   in dB: attack while the level rises, release while it falls - so for every mode "attack" is how
//              fast the band reacts to getting louder (compression deepens, expansion opens, upward boost backs off)
//   curve      static gain from the envelope: threshold, ratio (max = infinity), soft knee, range (the most the band
//              may cut or, for upward, boost), then makeup
//   lookahead  the band's audio is delayed by the lookahead; the detector is not
// Mix blends each band between its unprocessed and processed signal (the dry path is the split signal itself, so
// parallel mixing never comb-filters against the analog crossover's allpass). Solo/mute/listen weight the bands
// after that. All settings glide; mode switches and lookahead/crossover-type changes fade out and back in (8 ms).
//
// Auto makeup (Compress bands only): adds back half the gain reduction the band's static curve applies to a
// -6 dBFS peak (-9 dB RMS) level, i.e. a fixed, material-independent estimate that never pumps; Expand and
// Upward bands don't need makeup (they leave loud material alone). Upward bands' boost is also capped at once by
// the curve's boost for the instantaneous level (see processChunk), and silence (below -80 dBFS) is never lifted.
#pragma once

#include "dsp/Dynamics.hpp"
#include "dsp/Multiband.hpp"
#include "BandParams.hpp"

namespace pump { namespace band {

struct BandSettings {
    int mode = kCompress;
    float thresh = 0.0f, ratio = 2.0f, attack = 15.0f, release = 150.0f, knee = 6.0f, range = 24.0f, makeup = 0.0f;
    bool solo = false, mute = false, bypass = false;
};

// The static curve (dB in -> gain change in dB), shared with the UI.
inline double staticGain(int mode, double x, double thr, double ratio, double knee, double range)
{
    const bool inf = ratio >= 19.999;
    double slope, d;
    switch (mode)
    {
    case kExpand: slope = inf ? 100.0 : std::max(ratio, 1.0) - 1.0; d = thr - x; break;
    case kUpward: slope = inf ? 1.0 : 1.0 - 1.0 / std::max(ratio, 1.0); d = thr - x; break;
    default: slope = inf ? 1.0 : 1.0 - 1.0 / std::max(ratio, 1.0); d = x - thr; break;
    }
    double g;
    if (knee > 0.0 && std::fabs(d) <= 0.5 * knee) g = slope * (d + 0.5 * knee) * (d + 0.5 * knee) / (2.0 * knee);
    else g = d > 0.0 ? slope * d : 0.0;
    g = std::min(g, std::max(range, 0.0));
    return mode == kUpward ? g : -g;
}

inline double autoMakeupDb(const BandSettings& s, bool rms)
{
    if (s.mode != kCompress) return 0.0;
    return -0.5 * staticGain(kCompress, rms ? -9.0 : -6.0, s.thresh, s.ratio, s.knee, s.range);
}

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr double kMaxLookaheadMs = 10.0, kMaxHoldMs = 30.0;

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        split.prepare(sr, kChunk);
        maxLa = (int)std::ceil(kMaxLookaheadMs * 0.001 * sr) + 2;
        const int maxHold = (int)std::ceil(kMaxHoldMs * 0.001 * sr) + 2;
        rmsLen = std::max(1, (int)std::lround(0.020 * sr));
        for (auto& b : st)
            for (int ch = 0; ch < 2; ++ch)
            {
                b.dly[ch].setMaxDelay(maxLa + 2);
                b.hold[ch].setup(maxHold);
                b.rms[ch].setup(rmsLen);
                b.rms[ch].reset(0.0);
            }
        const int ramp = std::max(1, (int)(0.010 * sr));
        for (auto& b : st) { b.weight.setRamp(ramp); b.bypassFade.setRamp(ramp); }
        mixS.setRamp((int)(0.02 * sr)); outS.setRamp((int)(0.02 * sr));
        trans.setRamp(std::max(1, (int)(0.008 * sr)));
        smoothK = 1.0 - std::exp(-1.0 / (0.015 * sr));
        glideK = 1.0 - std::exp(-1.0 / (0.002 * sr));
        // apply everything immediately
        applyStructural();
        trans.reset(1.0);
        mixS.reset(mixPct * 0.01); outS.reset(dbToGain(outDb));
        for (int b = 0; b < kNumBands; ++b) { snapBand(b); st[b].weight.reset(targetWeight(b)); st[b].bypassFade.reset(set[b].bypass ? 1.0 : 0.0); }
        resetDetectors();
    }

    // ---- settings (audio thread, block start) ----
    void setBands(int n) { n = clampv(n, 1, kNumBands); if (n != nb) { nb = n; split.setBands(nb); updateHolds(); } }
    void setCrossovers(float a, float b, float c) { split.setCrossovers(a, b, c); updateHolds(); }
    void setXoverMode(int m) { m = m ? 1 : 0; if (m != wantMode) { wantMode = m; structuralPending = true; } }
    void setLookahead(float ms)
    {
        const int la = (int)std::lround(clampv((double)ms, 0.0, kMaxLookaheadMs) * 0.001 * sr);
        if (la != wantLa) { wantLa = la; structuralPending = true; }
    }
    void setDetection(int d) { if ((d ? 1 : 0) != detRms) { detRms = d ? 1 : 0; resetDetectors(); } }
    void setLink(float pct) { link = clampv((double)pct, 0.0, 100.0) * 0.01; }
    void setMix(float pct) { mixPct = clampv((double)pct, 0.0, 100.0); }
    void setOutput(float db) { outDb = db; }
    void setAutoMakeup(bool on) { autoMk = on; }
    void setListen(int band) { listen = band; }   // -1 = off
    void setBand(int b, const BandSettings& s)
    {
        if (s.mode != set[b].mode) st[b].glide = (int)(0.012 * sr);
        set[b] = s;
    }
    const BandSettings& bandSettings(int b) const { return set[b]; }

    int latency() const { return split.latency() + curLa; }
    int bands() const { return nb; }
    void effectiveCrossovers(double out[3]) const { split.effective(out); }
    bool kernelsCurrent() const { return split.kernelsCurrent(); }
    bool settled() const { return split.analogSettled() && !structuralPending && !trans.isSmoothing(); }

    // ---- feeds (read from any thread) ----
    float gainDb(int b) const { return grFeed[b].load(std::memory_order_relaxed); }    // dynamic gain change (no makeup)
    float levelDb(int b) const { return lvFeed[b].load(std::memory_order_relaxed); }   // detector envelope
    float makeupDb(int b) const { return mkFeed[b].load(std::memory_order_relaxed); }  // makeup in use (incl. auto)

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk)
            processChunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    struct BandState {
        DelayLine dly[2];
        SlidingMax hold[2];
        MovingAverage rms[2];
        double env[2] = { -120.0, -120.0 };
        // smoothed parameters
        double thr = 0, ratio = 2, knee = 6, range = 24, makeup = 0;
        double gOut[2] = { 0.0, 0.0 };   // last gain dB (for mode-change glides)
        double ceil[2] = { 0.0, 0.0 };   // upward mode: the most boost that keeps the band below its threshold
        int glide = 0, unusedFor = 0;
        bool idle = false;
        LinearSmoother weight, bypassFade;
    };

    // Solo / mute / listen weighting. Bands above the band count are drained by the splitter's routing ramp, so
    // they keep weight 1 here (a second fade would dip the sum while the count changes).
    double targetWeight(int b) const
    {
        if (listen >= 0) return b == listen ? 1.0 : 0.0;
        bool anySolo = false;
        for (int k = 0; k < nb; ++k) anySolo = anySolo || set[k].solo;
        if (b >= nb) return anySolo ? 0.0 : 1.0;
        return anySolo ? (set[b].solo ? 1.0 : 0.0) : (set[b].mute ? 0.0 : 1.0);
    }
    double makeupTarget(int b) const { return set[b].makeup + (autoMk ? autoMakeupDb(set[b], detRms != 0) : 0.0); }
    void snapBand(int b)
    {
        BandState& s = st[b];
        s.thr = set[b].thresh; s.ratio = set[b].ratio; s.knee = set[b].knee; s.range = set[b].range; s.makeup = makeupTarget(b);
    }
    void resetDetectors()
    {
        for (auto& b : st)
            for (int ch = 0; ch < 2; ++ch) { b.hold[ch].reset(); b.rms[ch].reset(0.0); b.env[ch] = -120.0; b.ceil[ch] = 0.0; }
    }
    void updateHolds()
    {
        double e[3];
        split.effective(e);
        for (int b = 0; b < kNumBands; ++b)
        {
            const double flo = b == 0 ? 20.0 : e[b - 1];
            const int len = (int)std::lround(clampv(0.6 / flo, 0.0005, kMaxHoldMs * 0.001) * sr);
            for (int ch = 0; ch < 2; ++ch) st[b].hold[ch].setLength(std::max(1, len));
        }
    }
    void applyStructural()
    {
        split.setMode(wantMode);
        curLa = clampv(wantLa, 0, maxLa);
        for (auto& b : st) for (int ch = 0; ch < 2; ++ch) { b.dly[ch].clear(); b.dly[ch].setDelay(curLa); }
        split.setBands(nb);
        updateHolds();
        structuralPending = false;
    }

    void processChunk(float* L, float* R, int n)
    {
        // structural changes (crossover type, lookahead): fade out, switch, fade back in
        if (structuralPending)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) { applyStructural(); resetDetectors(); trans.setTarget(1.0); }
        }
        split.process(L, R, n);
        const int used = std::min(kNumBands, std::max(nb, split.bandsInUse()));
        mixS.setTarget(mixPct * 0.01);
        outS.setTarget(dbToGain(outDb));
        float mixv[kChunk], outv[kChunk];
        for (int i = 0; i < n; ++i) { mixv[i] = (float)mixS.next(); outv[i] = (float)(outS.next() * trans.next()); L[i] = 0.0f; R[i] = 0.0f; }
        for (int b = 0; b < kNumBands; ++b)
        {
            BandState& s = st[b];
            const BandSettings& p = set[b];
            s.weight.setTarget(targetWeight(b));
            s.bypassFade.setTarget(p.bypass ? 1.0 : 0.0);
            // a band that carries nothing any more keeps running until its delayed tail has played out
            if (b >= used) s.unusedFor += n; else s.unusedFor = 0;
            if (s.unusedFor > curLa + kChunk)
            {
                if (!s.idle)
                {
                    for (int ch = 0; ch < 2; ++ch) { s.dly[ch].clear(); s.hold[ch].reset(); s.rms[ch].reset(0.0); s.env[ch] = -120.0; s.gOut[ch] = 0.0; }
                    s.idle = true;
                }
                for (int i = 0; i < n; ++i) { s.weight.next(); s.bypassFade.next(); }
                grFeed[b].store(0.0f, std::memory_order_relaxed);
                lvFeed[b].store(-120.0f, std::memory_order_relaxed);
                continue;
            }
            s.idle = false;
            const float* x[2] = { split.band(b, 0), split.band(b, 1) };
            const double ca = std::exp(-1.0 / (std::max(0.01, (double)p.attack) * 0.001 * sr));
            const double cr = std::exp(-1.0 / (std::max(1.0, (double)p.release) * 0.001 * sr));
            const double mkT = makeupTarget(b);
            double extreme = 0.0;
            for (int i = 0; i < n; ++i)
            {
                // glide the curve parameters
                s.thr += (p.thresh - s.thr) * smoothK;
                s.ratio += (p.ratio - s.ratio) * smoothK;
                s.knee += (p.knee - s.knee) * smoothK;
                s.range += (p.range - s.range) * smoothK;
                s.makeup += (mkT - s.makeup) * smoothK;
                // detector levels (dB): peak or RMS, held over about a period of the band's lowest frequency
                double lv[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float v = x[ch][i];
                    const float d = detRms ? (float)std::sqrt(std::max(0.0, s.rms[ch].process((double)v * v))) : std::fabs(v);
                    const float h = s.hold[ch].process(d);
                    lv[ch] = h > 1e-6f ? (double)gainToDbF(h) : -120.0;
                }
                const double mx = std::max(lv[0], lv[1]);
                const float w = (float)s.weight.next(), byp = (float)s.bypassFade.next();
                const float mixEff = mixv[i] * (1.0f - byp);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double l = lv[ch] + (mx - lv[ch]) * link;
                    double& e = s.env[ch];
                    e = l + (e - l) * (l > e ? ca : cr);
                    double g = staticGain(p.mode, e, s.thr, s.ratio, s.knee, s.range);
                    if (p.mode == kUpward)
                    {
                        // never lift digital silence or dither (the boost fades out from -80 to -90 dBFS)
                        g *= clampv((e + 90.0) * 0.1, 0.0, 1.0);
                        // and when a loud passage arrives the boost drops at once: it is capped by the curve's boost
                        // for the instantaneous (held) level, recovering at the release time, so a slow attack can't
                        // carry a big lift of a quiet part into a loud one (steady-state levels follow the curve)
                        const double ceil = staticGain(kUpward, l, s.thr, s.ratio, s.knee, s.range) * clampv((l + 90.0) * 0.1, 0.0, 1.0);
                        double& c = s.ceil[ch];
                        c = ceil < c ? ceil : ceil + (c - ceil) * cr;
                        g = std::min(g, c);
                    }
                    if (s.glide > 0) g = s.gOut[ch] + (g - s.gOut[ch]) * glideK;
                    s.gOut[ch] = g;
                    if (std::fabs(g) > std::fabs(extreme)) extreme = g;
                    const float gl = dbToGainF((float)(g + s.makeup));
                    const float y = s.dly[ch].process(x[ch][i]);
                    (ch == 0 ? L : R)[i] += y * (1.0f + mixEff * (gl - 1.0f)) * w;
                }
                if (s.glide > 0) --s.glide;
            }
            grFeed[b].store((float)extreme, std::memory_order_relaxed);
            lvFeed[b].store((float)std::max(s.env[0], s.env[1]), std::memory_order_relaxed);
            mkFeed[b].store((float)s.makeup, std::memory_order_relaxed);
        }
        for (int i = 0; i < n; ++i) { L[i] *= outv[i]; R[i] *= outv[i]; }
    }

    double sr = 48000.0;
    mb::Splitter split;
    BandSettings set[kNumBands];
    BandState st[kNumBands];
    int nb = 3, wantMode = 0, wantLa = 0, curLa = 0, maxLa = 482, rmsLen = 960, detRms = 0, listen = -1;
    bool structuralPending = false, autoMk = false;
    double link = 1.0, mixPct = 100.0, outDb = 0.0, smoothK = 0.001, glideK = 0.01;
    LinearSmoother mixS, outS, trans;
    std::atomic<float> grFeed[kNumBands] {}, lvFeed[kNumBands] {}, mkFeed[kNumBands] {};
};

}} // namespace pump::band
