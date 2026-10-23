// pump LIMIT engine: a true-peak lookahead limiter in the spirit of Pro-L 2 / Ozone Maximizer. Framework-free.
//
// Signal flow (base rate unless noted):
//   input -> [DC filter] -> drive -> LEVELING (slow, program dependent; takes a style-defined share of the
//   over-ceiling reduction, peak or RMS detector, linked by "link release")
//   -> upsample (Off/2x/4x/8x) -> [soft clip: Aggressive] -> BRICKWALL (oversampled; lookahead window = attack
//   shape blend of a long and a short triangular LookaheadGain, transient-aware; program-dependent dual release;
//   channel link for transients and release) -> downsample
//   -> SAFETY (base rate: true-peak or sample-peak lookahead stage that catches what the downsampling filters
//   and interpolation leave) -> clip at the ceiling -> meters -> [unity listen] -> [dither] -> clip to the grid.
//
// Latency (base samples) = lookahead L + detector delay E + oversampler latency + safety delay Ds, where
//   L  = round(lookahead * fs), the brickwall window (LookaheadGain: audio delay = window length)
//   E  = (M - 1) / F for the short true-peak interpolator the brickwall uses at 1x (M = 4) and 2x (M = 3)
//        when true peak is on, else 0 (at 4x/8x the oversampled samples are the detector)
//   Ds = 25 + Ls with true peak (the 4x detector's 23/24-sample delay, a 4-sample hold, window Ls), Ls for
//        sample-peak safety after oversampling, 0 at 1x without true peak (the brickwall is already exact).
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Dynamics.hpp"
#include "dsp/Oversampler.hpp"
#include "dsp/Loudness.hpp"
#include "dsp/Saturation.hpp"
#include "LimitParams.hpp"
#include <atomic>

namespace pump { namespace limit {

// What makes the styles different.
struct StyleDef {
    float levelShare;              // share of the over-ceiling reduction the slow leveling stage takes
    float levelAtkMs;              // leveling attack
    float levelRelMul, levelRelAddMs;   // leveling release = release * mul + add
    float levelRms;                // leveling detector: 0 = peak, 1 = RMS (sustained level; transients go to the brickwall)
    float levelKneeDb;             // soft knee of the leveling stage around the ceiling
    float attackBias;              // added to the attack control (share of the long, early window)
    float transient;               // transient handling: detected transients get a late, steep attack and a fast release
    float fastMul, slowMul;        // program-dependent release: spikes above the running reduction / sustained limiting
    float avgMs;                   // how long "sustained" is
    float clip;                    // soft clip ahead of the brickwall (0..1)
};

inline const StyleDef& styleDef(int s)
{
    static const StyleDef d[kNumStyles] = {
        //  share  atk  rMul rAdd  rms  knee  bias   trans fast  slow  avg   clip
        { 0.45f, 15.0f, 4.0f, 200.0f, 0.0f, 6.0f,  0.15f, 0.00f, 1.00f, 3.0f, 400.0f, 0.0f },   // Transparent
        { 0.35f, 25.0f, 3.0f, 120.0f, 1.0f, 6.0f, -0.20f, 0.85f, 0.35f, 2.0f, 250.0f, 0.0f },   // Punchy
        { 0.00f, 10.0f, 1.0f,  50.0f, 0.0f, 6.0f,  0.10f, 0.70f, 0.25f, 6.0f, 800.0f, 0.0f },   // Dynamic
        { 0.25f, 10.0f, 2.0f, 100.0f, 0.0f, 6.0f,  0.00f, 0.30f, 0.70f, 2.5f, 300.0f, 0.0f },   // Allround
        { 0.10f,  5.0f, 1.0f,  50.0f, 0.0f, 4.0f, -0.30f, 0.50f, 0.40f, 1.2f, 150.0f, 1.0f },   // Aggressive
        { 0.40f,  6.0f, 1.5f,  80.0f, 1.0f, 10.0f, -0.10f, 0.65f, 0.45f, 2.0f, 200.0f, 0.35f },  // Modern
        { 0.50f, 30.0f, 5.0f, 300.0f, 0.0f, 8.0f,  0.35f, 0.00f, 1.50f, 4.0f, 800.0f, 0.0f },   // Safe
    };
    return d[clampv(s, 0, kNumStyles - 1)];
}

// Loudness readings published to the UI (written by the audio thread ~10 times a second).
struct LoudnessReadout {
    std::atomic<float> momentary { -INFINITY }, shortTerm { -INFINITY }, integrated { -INFINITY }, range { -INFINITY },
        truePeak { -INFINITY }, maxMomentary { -INFINITY }, maxShortTerm { -INFINITY };
    void publish(LoudnessMeter& m, float samplePeakDb)
    {
        momentary.store((float)m.getMomentary(), std::memory_order_relaxed);
        shortTerm.store((float)m.getShortTerm(), std::memory_order_relaxed);
        integrated.store((float)m.getIntegrated(), std::memory_order_relaxed);
        range.store((float)m.getRange(), std::memory_order_relaxed);
        // the 4x detector reads between samples; a true-peak max is never below the sample peak
        truePeak.store(std::max((float)m.getTruePeakDb(), samplePeakDb), std::memory_order_relaxed);
        maxMomentary.store((float)m.getMaxMomentary(), std::memory_order_relaxed);
        maxShortTerm.store((float)m.getMaxShortTerm(), std::memory_order_relaxed);
    }
};

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr int kHistHz = 200;          // display history: one entry every 5 ms
    static constexpr int kSafetyHold = 4;        // extra samples held around a true peak (interpolation span)

    // ---- UI feeds (any thread reads) ----
    ScopeRing histIn { 1 << 12 }, histOut { 1 << 12 }, histGR { 1 << 12 }, histST { 1 << 12 };
    MeterValue inPeak[2], outPeak[2], grMax;     // max-hold since the UI last exchanged them (gains linear, GR in dB)
    LoudnessReadout outLoud, inLoud;
    std::atomic<bool> resetLoudness { false };

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        maxLos = (int)std::ceil(0.010 * sr) * 8 + 16;
        for (int c = 0; c < 2; ++c)
        {
            laLong[c].setup(maxLos);
            laShort[c].setup(maxLos);
            dlShort[c].setMaxDelay(maxLos);
            dlMain[c].setMaxDelay(maxLos + 16);
            dlClip[c].setMaxDelay(maxLos + 16);
            dlSafe[c].setMaxDelay(1024);
            dispLev[c].setMaxDelay(8192);
            dispBw[c].setMaxDelay(8192);
        }
        dispIn.setMaxDelay(8192);
        for (int k = 0; k < 3; ++k) ovs[k].prepare(k + 1, kChunk);
        Ls = std::max(8, (int)std::lround(sr * 0.0006));
        laSafe.setup(Ls + 4);
        laSafe.setLength(Ls);
        holdMin.setup(kSafetyHold + 2);
        holdMin.setLength(kSafetyHold + 1);
        meterOut.prepare(sr);
        meterIn.prepare(sr);
        const int ramp = (int)(0.02 * sr);
        driveSm.setRamp(ramp); ceilSm.setRamp(ramp); unitySm.setRamp(ramp); attackSm.setRamp(ramp);
        shareSm.setRamp((int)(0.05 * sr)); clipSm.setRamp((int)(0.05 * sr)); ltSm.setRamp(ramp); lrSm.setRamp(ramp);
        dcMix.setRamp(ramp);
        dcR = (float)std::exp(-kTwoPi * 5.0 / sr);
        histStep = std::max(1, (int)std::lround(sr / kHistHz));
        dispDecay = (float)tauCoef(0.025, sr);
        designInterpolators();
        applyTargets(true);
        updateCoefs();
        configure(false);
        loudPublishEvery = std::max(1, (int)std::lround(sr * 0.1));
        loudCount = 0;
        tpOutDb = tpInDb = -INFINITY;
        meterOut.reset(); meterIn.reset();
        samplePeakOut = 0.0f;
        started = false;
    }

    // ---- settings (audio thread, block start) ----
    void setGain(float db) { gainDb = db; }
    void setCeiling(float db) { ceilDb = db; }
    void setStyle(int s) { if (s != style) { style = clampv(s, 0, kNumStyles - 1); coefDirty = true; } }
    void setAttack(float pct) { attackPct = pct; }
    void setRelease(float ms) { if (ms != releaseMs) { releaseMs = ms; coefDirty = true; } }
    void setLinks(float transPct, float relPct) { linkTrans = transPct * 0.01f; linkRel = relPct * 0.01f; }
    void setDC(bool on) { dcOn = on; }
    void setUnity(bool on) { unity = on; }
    void setDither(int mode, int shaping)
    {
        const int m = mode <= 0 ? 0 : (shaping ? 2 : 1), bits = mode == 1 ? 16 : 24;
        if (m != ditherMode || bits != ditherBits) { ditherMode = m; ditherBits = bits; dither.setup(bits, (Ditherer::Mode)m); }
    }
    // These change the latency: the engine reconfigures (short mute + fade) when they change.
    // (before any audio has run since prepare() there is nothing to fade)
    // While audio runs a change is applied after a 5 ms fade-out (then a mute for the new latency and a fade-in),
    // and not at all when the rounded lookahead, the factor and the detector stay the same.
    void setLookahead(float ms) { if (ms != lookMs) { lookMs = ms; requestConfigure(); } }
    void setTruePeak(bool on) { if (on != truePeak) { truePeak = on; requestConfigure(); } }
    void setOversampling(int idx) { idx = clampv(idx, 0, 3); if (idx != osIdx) { osIdx = idx; requestConfigure(); } }

    int latency() const { return latencyTotal; }
    int oversamplingFactor() const { return F; }
    float currentGainDb() const { return gainDb; }

    void process(float* L, float* R, int n)
    {
        if (resetLoudness.exchange(false, std::memory_order_acq_rel))
        {
            meterOut.reset(); meterIn.reset(); samplePeakOut = 0.0f; samplePeakIn = 0.0f;
            outLoud.publish(meterOut, -INFINITY); inLoud.publish(meterIn, -INFINITY);
        }
        applyTargets(false);
        if (coefDirty) updateCoefs();
        if (n > 0) started = true;
        for (int off = 0; off < n; off += kChunk)
        {
            handlePendingConfigure();
            chunk(L + off, R + off, std::min(kChunk, n - off));
        }
    }

    // Static helpers for tests
    static float softClip(float x, float c, float knee)
    {
        const float a = std::fabs(x), k = knee * c;
        if (a <= k) return x;
        const float y = k + (c - k) * fastTanh((a - k) / (c - k));
        return x < 0.0f ? -y : y;
    }

private:
    // ------------------------------------------------------------------------------------------------- config
    void requestConfigure()
    {
        if (!started) { pendingCfg = false; configure(false); return; }
        const int newL = std::max(2, (int)std::lround(lookMs * 0.001 * sr));
        const int newF = osIdx == 0 ? 1 : (1 << osIdx);
        pendingCfg = !(newL == Lbase && newF == F && truePeak == cfgTruePeak);
    }
    void handlePendingConfigure()
    {
        const int ramp = std::max(1, (int)(0.005 * sr));
        if (pendingCfg)
        {
            // already silent (muted or faded out): switch now
            if (muteLeft > 0 || (!fade.isSmoothing() && fade.current() <= 0.0)) { pendingCfg = false; configure(true); }
            else if (fade.getTarget() != 0.0) { fade.setRamp(ramp); fade.setTarget(0.0); }
        }
        else if (fade.getTarget() == 0.0 && muteLeft == 0) { fade.setRamp(ramp); fade.setTarget(1.0); }   // change undone mid fade
    }
    static inline float sanitize(float x)
    {
        if (!(std::fabs(x) < 1e6f)) return 0.0f;   // NaN, Inf, absurd values
        return std::fabs(x) < 1e-30f ? 0.0f : x;
    }

    void configure(bool runtime)
    {
        cfgTruePeak = truePeak;
        F = osIdx == 0 ? 1 : (1 << osIdx);
        os = osIdx == 0 ? nullptr : &ovs[osIdx - 1];
        if (os) os->reset();
        Lbase = std::max(2, (int)std::lround(lookMs * 0.001 * sr));
        Los = Lbase * F;
        Sos = std::max(2, Los / 8);
        // short true-peak interpolator in the brickwall's detector at 1x and 2x
        M = !truePeak ? 0 : (F == 1 ? 4 : (F == 2 ? 3 : 0));
        Dmain = Los + (M > 0 ? M - 1 : 0);
        const int Ebase = (Dmain - Los) / F;
        safetyOn = truePeak || F > 1;
        Ds = !safetyOn ? 0 : (truePeak ? 23 + kSafetyHold / 2 + Ls : Ls);
        osLat = os ? os->latency() : 0;
        latencyTotal = Lbase + Ebase + osLat + Ds;
        for (int c = 0; c < 2; ++c)
        {
            laLong[c].setLength(Los);
            laShort[c].setLength(Sos);
            dlShort[c].clear(); dlShort[c].setDelay(Los - Sos);
            dlMain[c].clear(); dlMain[c].setDelay(Dmain);
            dlClip[c].clear(); dlClip[c].setDelay(Dmain);
            dlSafe[c].clear(); dlSafe[c].setDelay(Ds);
            tpd[c].reset();
            dispLev[c].clear(); dispLev[c].setDelay(latencyTotal);
            dispBw[c].clear(); dispBw[c].setDelay(osLat / 2 + Ds);
            env[c] = 0.0f; avg[c] = 0.0f;
            levEnv[c] = 0.0f; levPow[c] = 0.0f;
            std::fill(std::begin(ihist[c]), std::end(ihist[c]), 0.0f);
            std::fill(std::begin(shist[c]), std::end(shist[c]), 0.0f);
        }
        ipos = 0; spos = 0;
        laSafe.reset(); holdMin.reset(); senv = 1.0f;
        dispIn.clear(); dispIn.setDelay(latencyTotal);
        trFast = trPow = trEnv = 0.0f;
        dcX1[0] = dcX1[1] = dcY1[0] = dcY1[1] = 0.0;
        if (runtime) { muteLeft = latencyTotal; fade.setRamp((int)(0.01 * sr)); fade.reset(0.0); fade.setTarget(1.0); }
        else { muteLeft = 0; fade.reset(1.0); }
        updateCoefs();
    }

    void updateCoefs()
    {
        coefDirty = false;
        const StyleDef& sd = styleDef(style);
        const double srOs = sr * F;
        const double R = std::max(1.0, (double)releaseMs) * 0.001;
        cFast = (float)tauCoef(R * sd.fastMul, srOs);
        cFastTr = (float)tauCoef(R * sd.fastMul * 0.35, srOs);
        cSlow = (float)tauCoef(R * sd.slowMul, srOs);
        cAvgA = (float)tauCoef(0.030, srOs);
        cAvgR = (float)tauCoef(sd.avgMs * 0.001, srOs);
        levA = (float)tauCoef(sd.levelAtkMs * 0.001, sr);
        levR = (float)tauCoef(R * sd.levelRelMul + sd.levelRelAddMs * 0.001, sr);
        levPowC = (float)tauCoef(0.010, sr);
        levRms = sd.levelRms;
        levKnee = sd.levelKneeDb;
        trPkC = (float)tauCoef(0.010, sr);
        trPowC = (float)tauCoef(0.060, sr);
        trAtkC = (float)tauCoef(0.001, sr);
        trRelC = (float)tauCoef(0.080, sr);
        trAmount = sd.transient;
        cSafeRel = (float)tauCoef(0.020, sr);
    }

    void applyTargets(bool snap)
    {
        const StyleDef& sd = styleDef(style);
        const double drive = dbToGain(gainDb), ceil = dbToGain(ceilDb);
        const double un = unity ? dbToGain(-gainDb) : 1.0;
        const double att = clampv(attackPct * 0.01f + sd.attackBias, 0.0f, 1.0f);
        if (snap)
        {
            driveSm.reset(drive); ceilSm.reset(ceil); unitySm.reset(un); attackSm.reset(att);
            shareSm.reset(sd.levelShare); clipSm.reset(sd.clip); ltSm.reset(linkTrans); lrSm.reset(linkRel);
            dcMix.reset(dcOn ? 1.0 : 0.0);
            return;
        }
        driveSm.setTarget(drive); ceilSm.setTarget(ceil); unitySm.setTarget(un); attackSm.setTarget(att);
        shareSm.setTarget(sd.levelShare); clipSm.setTarget(sd.clip); ltSm.setTarget(linkTrans); lrSm.setTarget(linkRel);
        dcMix.setTarget(dcOn ? 1.0 : 0.0);
    }

    // Windowed-sinc fractional interpolators for the brickwall detector (true peak at 1x / 2x).
    void designInterpolators()
    {
        auto bessel0 = [](double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double beta = 4.0;
        for (int which = 0; which < 2; ++which)
        {
            const int m = which == 0 ? 4 : 3;
            const int nph = which == 0 ? 3 : 1;
            for (int p = 0; p < nph; ++p)
            {
                const double frac = which == 0 ? 0.25 * (p + 1) : 0.5;
                double sum = 0.0, c[8] = {};
                for (int k = 0; k < 2 * m; ++k)
                {
                    const double d = m - k - frac;   // distance of x[t-k] from the interpolated point
                    const double s = std::fabs(d) < 1e-12 ? 1.0 : std::sin(kPi * d) / (kPi * d);
                    const double u = d / m;
                    const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / bessel0(beta);
                    c[k] = s * w; sum += c[k];
                }
                for (int k = 0; k < 2 * m; ++k) icoef[which][p][k] = (float)(c[k] / sum);
            }
        }
    }

    // ------------------------------------------------------------------------------------------------- audio
    void chunk(float* L, float* R, int n)
    {
        // a NaN/Inf from upstream would otherwise stick in every filter and envelope until re-activation
        for (int i = 0; i < n; ++i) { L[i] = sanitize(L[i]); R[i] = sanitize(R[i]); }
        // input metering (raw input)
        meterIn.process(L, R, n);
        {
            float a = 0.0f, b = 0.0f;
            for (int i = 0; i < n; ++i) { a = std::max(a, std::fabs(L[i])); b = std::max(b, std::fabs(R[i])); }
            inPeak[0].setMax(a); inPeak[1].setMax(b);
            samplePeakIn = std::max(samplePeakIn, std::max(a, b));
        }

        // ---- front end: DC filter, drive, transient detector, leveling
        for (int i = 0; i < n; ++i)
        {
            float x = L[i], y = R[i];
            {
                const float w = (float)dcMix.next();
                const double hx = (double)x - dcX1[0] + dcR * dcY1[0]; dcX1[0] = x; dcY1[0] = hx;
                const double hy = (double)y - dcX1[1] + dcR * dcY1[1]; dcX1[1] = y; dcY1[1] = hy;
                if (w > 0.0f) { x += w * ((float)hx - x); y += w * ((float)hy - y); }
            }
            const float g = (float)driveSm.next();
            x *= g; y *= g;
            const float ceil = (float)ceilSm.next();
            ceilBuf[i] = ceil;
            const float a = std::max(std::fabs(x), std::fabs(y));
            dispInEnv = std::max(a, dispInEnv * dispDecay);
            inDispBuf[i] = dispIn.process(dispInEnv);
            // transient detector: crest of the fast peak over the slow RMS
            trFast = std::max(a, trFast * trPkC);
            trPow = a * a + (trPow - a * a) * trPowC;
            float trT = 0.0f;
            if (trAmount > 0.0f && trFast > 1e-5f)
            {
                const float crest = gainToDbF(trFast / (std::sqrt(2.0f * trPow) + 1e-9f));
                trT = clampv((crest - 4.0f) * 0.125f, 0.0f, 1.0f) * trAmount;
            }
            trEnv = trT + (trEnv - trT) * (trT > trEnv ? trAtkC : trRelC);
            trBuf[i] = trEnv;
            wBuf[i] = (float)attackSm.next() * (1.0f - trEnv);
            ltBuf[i] = (float)ltSm.next();
            lrBuf[i] = (float)lrSm.next();
            clipBuf[i] = (float)clipSm.next();
            // leveling (slow stage): a share of the reduction the brickwall would need
            const float share = (float)shareSm.next();
            float lg[2] = { 1.0f, 1.0f };
            if (share > 1e-4f)
            {
                const float cdb = gainToDbF(ceil);
                float over[2];
                const float xs[2] = { x, y };
                for (int c = 0; c < 2; ++c)
                {
                    levPow[c] = xs[c] * xs[c] + (levPow[c] - xs[c] * xs[c]) * levPowC;
                    const float det = std::fabs(xs[c]) + levRms * (std::sqrt(2.0f * levPow[c]) - std::fabs(xs[c]));
                    float o = gainToDbF(det) - cdb;
                    const float k = levKnee;
                    o = o <= -0.5f * k ? 0.0f : (o < 0.5f * k ? (o + 0.5f * k) * (o + 0.5f * k) / (2.0f * k) : o);
                    levEnv[c] = o + (levEnv[c] - o) * (o > levEnv[c] ? levA : levR);
                    over[c] = levEnv[c];
                }
                const float mx = std::max(over[0], over[1]);
                for (int c = 0; c < 2; ++c)
                {
                    const float e = over[c] + lrBuf[i] * (mx - over[c]);
                    if (e > 1e-6f) lg[c] = dbToGainF(-share * e);
                }
            }
            else { levEnv[0] *= levR; levEnv[1] *= levR; }
            x *= lg[0]; y *= lg[1];
            levDispBuf[0][i] = 1.0f - dispLev[0].process(1.0f - lg[0]);
            levDispBuf[1][i] = 1.0f - dispLev[1].process(1.0f - lg[1]);
            L[i] = x; R[i] = y;
        }

        // ---- brickwall (oversampled)
        float* b0 = L;
        float* b1 = R;
        if (os) { float* const* p = os->upsample(L, R, n); b0 = p[0]; b1 = p[1]; }
        const int N = n * F;
        float bwMin[2] = { 1.0f, 1.0f };
        for (int j = 0; j < N; ++j)
        {
            const int i = j / F;
            const float ceil = ceilBuf[i];
            float xs[2] = { b0[j], b1[j] };
            const float clipAmt = clipBuf[i];
            float clipG[2] = { 1.0f, 1.0f };   // what the clipper took (for the GR display)
            if (clipAmt > 1e-4f)
            {
                const float before[2] = { std::fabs(xs[0]), std::fabs(xs[1]) };
                // saturates toward 2 dB above the ceiling (rounding off the tips of transients, the way a
                // clipper in front of a limiter does); the brickwall takes the rest
                const float cc = ceil * 1.2589f;
                for (int c = 0; c < 2; ++c)
                {
                    xs[c] += clipAmt * (softClip(xs[c], cc, 0.5f) - xs[c]);
                    if (before[c] > 1e-6f) clipG[c] = std::min(1.0f, std::fabs(xs[c]) / before[c]);
                }
            }
            // detection (peak, or true peak through the short interpolator)
            float pk[2];
            if (M > 0)
            {
                const int m2 = 2 * M;
                ipos = (ipos - 1 + m2) % m2;
                for (int c = 0; c < 2; ++c)
                {
                    ihist[c][ipos] = ihist[c][ipos + m2] = xs[c];
                    const float* h = ihist[c] + ipos;   // h[k] = x[t-k]
                    float m = std::max(std::fabs(h[M]), std::fabs(h[M - 1]));
                    const int which = F == 1 ? 0 : 1, nph = F == 1 ? 3 : 1;
                    for (int p = 0; p < nph; ++p)
                    {
                        const float* cf = icoef[which][p];
                        float acc = 0.0f;
                        for (int k = 0; k < m2; ++k) acc += cf[k] * h[k];
                        m = std::max(m, std::fabs(acc));
                    }
                    pk[c] = m;
                }
            }
            else { pk[0] = std::fabs(xs[0]); pk[1] = std::fabs(xs[1]); }
            float need[2] = { pk[0] > ceil ? ceil / pk[0] : 1.0f, pk[1] > ceil ? ceil / pk[1] : 1.0f };
            const float lt = ltBuf[i];
            if ((need[0] < 1.0f || need[1] < 1.0f) && lt > 0.0f)
            {
                const float mn = std::min(need[0], need[1]);
                for (int c = 0; c < 2; ++c) if (need[c] > mn) need[c] *= std::pow(mn / need[c], lt);
            }
            const float w = wBuf[i];
            float e[2];
            for (int c = 0; c < 2; ++c)
            {
                const float gl = laLong[c].process(need[c]);
                const float gs = laShort[c].process(1.0f - dlShort[c].process(1.0f - need[c]));
                const float ga = gs + w * (gl - gs);
                const float gdb = ga < 1.0f ? gainToDbF(ga) : 0.0f;
                avg[c] = gdb + (avg[c] - gdb) * (gdb < avg[c] ? cAvgA : cAvgR);
                if (gdb <= env[c]) env[c] = gdb;
                else
                {
                    const float wt = clampv((avg[c] - env[c]) * 0.5f, 0.0f, 1.0f);
                    const float cf = cFast + trBuf[i] * (cFastTr - cFast);
                    const float co = cSlow + wt * (cf - cSlow);
                    env[c] = gdb + (env[c] - gdb) * co;
                    if (env[c] > -1e-7f) env[c] = 0.0f;
                }
                e[c] = env[c];
            }
            const float lr = lrBuf[i];
            const float mn = std::min(e[0], e[1]);
            for (int c = 0; c < 2; ++c)
            {
                const float ed = e[c] + lr * (mn - e[c]);
                const float gg = ed < 0.0f ? dbToGainF(ed) : 1.0f;
                xs[c] = dlMain[c].process(xs[c]) * gg;
                // the clip's reduction, aligned with the delayed audio (display only)
                bwMin[c] = std::min(bwMin[c], gg * (1.0f - dlClip[c].process(1.0f - clipG[c])));   // stored as 1 - g: cleared = no reduction
            }
            b0[j] = xs[0]; b1[j] = xs[1];
            if ((j + 1) % F == 0)
            {
                bwDispBuf[0][i] = 1.0f - dispBw[0].process(1.0f - bwMin[0]);
                bwDispBuf[1][i] = 1.0f - dispBw[1].process(1.0f - bwMin[1]);
                bwMin[0] = bwMin[1] = 1.0f;
            }
        }
        if (os) os->downsample(L, R, n);

        // ---- safety stage, ceiling clip, meters, unity, dither
        float opl = 0.0f, opr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float y0 = L[i], y1 = R[i];
            const float ceil = ceilBuf[i];
            if (safetyOn)
            {
                float d0, d1;
                if (truePeak)
                {
                    spos = (spos - 1 + kSH) % kSH;
                    shist[0][spos] = shist[0][spos + kSH] = y0;
                    shist[1][spos] = shist[1][spos + kSH] = y1;
                    d0 = std::max(tpd[0].process(y0), std::max(std::fabs(shist[0][spos + 23]), std::fabs(shist[0][spos + 24])));
                    d1 = std::max(tpd[1].process(y1), std::max(std::fabs(shist[1][spos + 23]), std::fabs(shist[1][spos + 24])));
                }
                else { d0 = std::fabs(y0); d1 = std::fabs(y1); }
                const float pk = std::max(d0, d1);
                float need = pk > ceil ? ceil / pk : 1.0f;
                if (truePeak) need = holdMin.process(need);
                const float gs = laSafe.process(need);
                senv = gs < senv ? gs : gs + (senv - gs) * cSafeRel;
                y0 = dlSafe[0].process(y0) * senv;
                y1 = dlSafe[1].process(y1) * senv;
            }
            y0 = clampv(y0, -ceil, ceil);
            y1 = clampv(y1, -ceil, ceil);
            if (muteLeft > 0) { --muteLeft; y0 = y1 = 0.0f; }
            else if (fade.isSmoothing() || fade.current() < 1.0) { const float f = (float)fade.next(); y0 *= f; y1 *= f; }
            L[i] = y0; R[i] = y1;
            opl = std::max(opl, std::fabs(y0)); opr = std::max(opr, std::fabs(y1));
            // display history
            const float s = safetyOn ? senv : 1.0f;
            const float gr = -gainToDbF(std::min(levDispBuf[0][i] * bwDispBuf[0][i], levDispBuf[1][i] * bwDispBuf[1][i]) * s);
            stepIn = std::max(stepIn, inDispBuf[i]);
            dispOutEnv = std::max(std::max(std::fabs(y0), std::fabs(y1)), dispOutEnv * dispDecay);
            stepOut = std::max(stepOut, dispOutEnv);
            stepGR = std::max(stepGR, gr);
            if (++stepCount >= histStep)
            {
                histIn.push(gainToDbF(stepIn)); histOut.push(gainToDbF(stepOut)); histGR.push(stepGR);
                histST.push((float)meterOut.getShortTerm());
                grMax.setMax(stepGR);
                stepIn = stepOut = stepGR = 0.0f; stepCount = 0;
            }
        }
        outPeak[0].setMax(opl); outPeak[1].setMax(opr);
        samplePeakOut = std::max(samplePeakOut, std::max(opl, opr));
        // backstop: no envelope may stay non-finite (inputs are sanitized, so this should never trigger)
        for (int c = 0; c < 2; ++c)
            if (!std::isfinite(env[c]) || !std::isfinite(avg[c]) || !std::isfinite(levEnv[c]) || !std::isfinite(levPow[c]))
                env[c] = avg[c] = levEnv[c] = levPow[c] = 0.0f;
        if (!std::isfinite(trFast) || !std::isfinite(trPow) || !std::isfinite(trEnv)) trFast = trPow = trEnv = 0.0f;
        if (!std::isfinite(dcY1[0]) || !std::isfinite(dcY1[1])) dcX1[0] = dcX1[1] = dcY1[0] = dcY1[1] = 0.0;
        if (!std::isfinite(senv)) senv = 1.0f;
        meterOut.process(L, R, n);
        loudCount += n;
        if (loudCount >= loudPublishEvery)
        {
            loudCount = 0;
            outLoud.publish(meterOut, gainToDbF(samplePeakOut));
            inLoud.publish(meterIn, gainToDbF(samplePeakIn));
        }
        // unity listen and dither (after metering: the readouts show the limited master)
        for (int i = 0; i < n; ++i)
        {
            const float u = (float)unitySm.next();
            if (u != 1.0f) { L[i] *= u; R[i] *= u; }
        }
        if (ditherMode != 0)
        {
            const float q = (float)std::ldexp(1.0, -(ditherBits - 1));
            for (int i = 0; i < n; ++i)
            {
                const float cq = std::floor(ceilBuf[i] / q) * q;   // a ceiling on the quantiser's grid
                L[i] = clampv(dither.process(0, L[i]), -cq, cq);
                R[i] = clampv(dither.process(1, R[i]), -cq, cq);
            }
        }
    }

    // settings
    double sr = 48000.0;
    float gainDb = 0.0f, ceilDb = -1.0f, attackPct = 50.0f, releaseMs = 100.0f, lookMs = 5.0f, linkTrans = 0.75f, linkRel = 1.0f;
    int style = kAllround, osIdx = 1, ditherMode = 0, ditherBits = 24;
    bool truePeak = true, dcOn = false, unity = false, coefDirty = true, started = false;
    bool pendingCfg = false, cfgTruePeak = true;

    // structure
    int F = 2, Lbase = 240, Los = 480, Sos = 60, M = 0, Dmain = 480, Ds = 0, Ls = 29, osLat = 0, latencyTotal = 0, maxLos = 0;
    bool safetyOn = true;
    Oversampler ovs[3];
    Oversampler* os = nullptr;

    // brickwall
    LookaheadGain laLong[2], laShort[2];
    DelayLine dlShort[2], dlMain[2];
    float env[2] = {}, avg[2] = {};
    float cFast = 0, cFastTr = 0, cSlow = 0, cAvgA = 0, cAvgR = 0;
    float ihist[2][16] = {};
    int ipos = 0;
    float icoef[2][3][8] = {};
    DelayLine dlClip[2];   // display only: the clip's gain, delayed like the audio

    // leveling + transient detector
    float levEnv[2] = {}, levPow[2] = {};
    float levA = 0, levR = 0, levPowC = 0, levRms = 0, levKnee = 6;
    float trFast = 0, trPow = 0, trEnv = 0, trPkC = 0, trPowC = 0, trAtkC = 0, trRelC = 0, trAmount = 0;

    // safety
    static constexpr int kSH = 32;
    TruePeakDetector tpd[2];
    float shist[2][2 * kSH] = {};
    int spos = 0;
    SlidingMin holdMin;
    LookaheadGain laSafe;
    DelayLine dlSafe[2];
    float senv = 1.0f, cSafeRel = 0.0f;

    // DC
    double dcX1[2] = {}, dcY1[2] = {};
    float dcR = 0.9993f;

    // smoothing, fades
    LinearSmoother driveSm, ceilSm, unitySm, attackSm, shareSm, clipSm, ltSm, lrSm, dcMix, fade;
    int muteLeft = 0;

    // dither
    Ditherer dither;

    // meters and display
    LoudnessMeter meterOut, meterIn;
    float samplePeakOut = 0.0f, samplePeakIn = 0.0f, tpOutDb = 0.0f, tpInDb = 0.0f;
    int loudPublishEvery = 4800, loudCount = 0;
    DelayLine dispIn, dispLev[2], dispBw[2];
    int histStep = 240, stepCount = 0;
    float dispInEnv = 0.0f, dispOutEnv = 0.0f, dispDecay = 0.999f;
    float stepIn = 0, stepOut = 0, stepGR = 0;

    // per-chunk scratch
    float ceilBuf[kChunk] = {}, trBuf[kChunk] = {}, wBuf[kChunk] = {}, ltBuf[kChunk] = {}, lrBuf[kChunk] = {}, clipBuf[kChunk] = {};
    float inDispBuf[kChunk] = {}, levDispBuf[2][kChunk] = {}, bwDispBuf[2][kChunk] = {};
};

}} // namespace pump::limit
