// pump SNAP engine: a level-independent transient shaper. Framework-free.
//
// Signal path (stereo):
//   detector  the undelayed input -> focus filter (Full: 20 Hz high-pass; Low: + 150 Hz low-pass; Mid: 300 Hz..3 kHz;
//             High: 3 kHz high-pass) -> envelope: the magnitude of the analytic signal from a 90-degree allpass pair
//             (no ripple on steady tones, even in the bass), or the rectified signal where that is higher (onsets
//             register on their first sample) -> three followers, for the left, the right and the linked (the louder)
//             envelope:
//               F   fast: instant attack, 12 ms release (a quarter of RELEASE when that is shorter)
//               S1  slow attack: approaches F with the ATTACK TIME, never above F
//               S2  slow release: instant attack, falls with the RELEASE time, never below F
//             rise = F / S1 in dB: above 0 only while the level climbs faster than the ATTACK TIME follows it
//             fall = S2 / F in dB: above 0 only while the level decays faster than the RELEASE time
//             Both are ratios of envelopes, so they don't depend on the input level and need no threshold; a steady
//             tone gives 0 dB.
//   gain      attack gain = ATTACK x 1.5 x rise, sustain gain = SUSTAIN x fall (both saturating softly toward +18 dB
//             of boost / -24 dB of cut), blended per channel between its own detector and the linked one by LINK.
//             At SUSTAIN +100 % the tail decays no faster than the RELEASE time; at ATTACK -67 % the onset rises no
//             faster than the ATTACK TIME. The attack gain is smoothed by two 0.25 ms one-poles (0.6 ms while a cut
//             deepens, so cuts never click); the sustain gain leaves 0 dB in 0.5 ms and returns in 0.05 ms, so a new
//             hit isn't caught under a tail boost (a boosted tail stays under the slow-release envelope S2, i.e. under
//             the last peak).
//   audio     lookahead delay (0..5 ms = the latency; the detector runs that far ahead of the audio) -> the gain
//             applies to the whole signal, or to the low or the high band of an LR4 split at CROSSOVER (the bands sum
//             to an allpass: zero shaping is flat) -> mix with the aligned dry signal (in the band modes the dry
//             signal is the same allpassed sum, so no mix setting notches the crossover) -> output gain -> soft
//             clipper (linear to 3 dB under the ceiling, then a tanh knee that never passes it).
//   DELTA     plays only what the shaper adds or removes (mixed - dry, after the output gain, unclipped).
//
// Switching: a focus change fades the shaping out (10 ms), switches the envelope to the new band (all four run all the
// time) and restarts it, holds the followers on it while it settles (50 ms) and fades back in; band changes crossfade
// the two outputs (20 ms); lookahead changes fade the output out and back in (6 ms each way) around the new delay. Continuous controls are smoothed (20 ms); the times and the
// crossover glide (30 ms).
#pragma once

#include "dsp/Crossover.hpp"
#include "dsp/Fx.hpp"
#include "SnapParams.hpp"

namespace pump { namespace snap {

struct Settings {
    double attack = 0.0, sustain = 0.0;            // -100..100 %
    double attackMs = 10.0, releaseMs = 200.0;
    double link = 100.0;                           // %
    int focus = kFocusFull, band = kBandFull;
    double crossover = 250.0, lookaheadMs = 0.0;
    bool clip = false;
    double ceilingDb = -1.0;
    bool delta = false;
    double mix = 100.0, outDb = 0.0;
};

constexpr double kAttackSens = 1.5, kSustainSens = 1.0;
constexpr double kBoostMax = 18.0, kCutMax = 24.0;     // where the gain law saturates (dB)
constexpr double kFastReleaseMs = 12.0;
constexpr double kEnvFloor = 1e-7;                      // -140 dBFS: keeps the ratios finite in digital silence
constexpr double kClipKnee = 0.70794578;                // the clipper is linear up to 3 dB below the ceiling
constexpr double kDbToLn = 0.11512925464970229;         // ln(10) / 20
constexpr double kLnToDb = 8.6858896380650366;

inline double poleK(double ms, double sr) { return 1.0 - std::exp(-1000.0 / (std::max(ms, 1e-3) * sr)); }

// ---- detector --------------------------------------------------------------------------------------------------------
struct DetectorCoefs {
    double fRel = 0.002, sAtt = 0.002, sRel = 1e-4;
    void set(double attackMs, double releaseMs, double sr)
    {
        fRel = poleK(std::min(kFastReleaseMs, 0.25 * releaseMs), sr);
        sAtt = poleK(attackMs, sr);
        sRel = poleK(releaseMs, sr);
    }
};

struct Detector {
    double f = 0.0, s1 = 0.0, s2 = 0.0;
    void reset(double v = 0.0) { f = s1 = s2 = v; }
    // e = the envelope (linear); rise and fall come out in dB (both >= 0)
    inline void process(double e, const DetectorCoefs& c, double& rise, double& fall)
    {
        f = e > f ? e : f + (e - f) * c.fRel;
        s1 = f > s1 ? s1 + (f - s1) * c.sAtt : f;
        s2 = f > s2 ? f : s2 + (f - s2) * c.sRel;
        rise = kLnToDb * std::log((f + kEnvFloor) / (s1 + kEnvFloor));
        fall = kLnToDb * std::log((s2 + kEnvFloor) / (f + kEnvFloor));
    }
};

// The gain law: amount -1..1 times the detector's dB (scaled by sens), saturating softly at the boost / cut limits.
inline double shapeDb(double amount, double d, double sens)
{
    const double lim = amount >= 0.0 ? kBoostMax : kCutMax;
    return amount * lim * (double)fastTanh((float)(sens * d / lim));
}

// The gain smoothing (see the header): attack part two one-poles (gentler while a cut deepens), sustain part
// asymmetric.
struct GainSmoother {
    double a1 = 0.0, a2 = 0.0, s = 0.0;
    double kSm = 0.1, kCut = 0.05, kAway = 0.05, kBack = 0.3;
    void setup(double sr) { kSm = poleK(0.25, sr); kCut = poleK(0.6, sr); kAway = poleK(0.5, sr); kBack = poleK(0.05, sr); }
    void reset() { a1 = a2 = s = 0.0; }
    inline void process(double ta, double ts)
    {
        const double k = (ta < 0.0 && ta < a1) ? kCut : kSm;
        a1 += (ta - a1) * k;
        a2 += (a1 - a2) * k;
        s += (ts - s) * (std::fabs(ts) < std::fabs(s) ? kBack : kAway);
    }
    double total() const { return a2 + s; }
};

// Envelope of the analytic signal: a 90-degree phase-difference allpass pair (two cascades of four 2nd-order allpass
// sections in z^-2, one path a sample later), within +-0.7 degrees from 0.0005 to 0.4995 x the sample rate.
class Envelope {
public:
    void reset() { for (auto& s : sa) s = Sec(); for (auto& s : sb) s = Sec(); aPrev = 0.0; }
    inline double process(double x)
    {
        double a = x, b = x;
        for (int i = 0; i < 4; ++i) { a = sa[i].run(a, kA[i]); b = sb[i].run(b, kB[i]); }
        const double re = aPrev;
        aPrev = a;
        return std::sqrt(re * re + b * b);
    }
    double state() const { return aPrev + sa[3].y1 + sb[3].y1; }
private:
    struct Sec {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        inline double run(double x, double c) { const double y = c * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };
    // the published pole pairs, squared (the sections run in z^-2)
    static constexpr double kA[4] = { 0.6923878 * 0.6923878, 0.9360654322959 * 0.9360654322959, 0.9882295226860 * 0.9882295226860, 0.9987488452737 * 0.9987488452737 };
    static constexpr double kB[4] = { 0.4021921162426 * 0.4021921162426, 0.8561710882420 * 0.8561710882420, 0.9722909545651 * 0.9722909545651, 0.9952884791278 * 0.9952884791278 };
    Sec sa[4], sb[4];
    double aPrev = 0.0;
};

// The detector's focus: a band of the input that drives the shaping (the audio is never filtered by it). All four
// bands run all the time, so a focus switch hands the envelope a signal that is already settled.
struct FocusBank {
    fx::Svf hp20, lp150, hp300, lp3k, hp3k;
    void setup(double sr)
    {
        const double q = 0.70710678;
        hp20.set(20.0, q, sr); lp150.set(150.0, q, sr); hp300.set(300.0, q, sr); lp3k.set(3000.0, q, sr); hp3k.set(3000.0, q, sr);
    }
    void reset() { hp20.reset(); lp150.reset(); hp300.reset(); lp3k.reset(); hp3k.reset(); }
    inline double process(double x, int focus)
    {
        const double full = hp20.highpass(x);
        const double low = lp150.lowpass(full);
        const double mid = lp3k.lowpass(hp300.highpass(x));
        const double high = hp3k.highpass(x);
        switch (focus)
        {
        case kFocusLow: return low;
        case kFocusMid: return mid;
        case kFocusHigh: return high;
        default: return full;
        }
    }
    double state() const { return hp20.ic2 + lp150.ic2 + hp300.ic2 + lp3k.ic2 + hp3k.ic2; }
};

// Soft clipper: linear up to kClipKnee x the ceiling, then a tanh knee that approaches the ceiling and never passes it.
inline double softClip(double x, double ceiling)
{
    const double ax = std::fabs(x), k = kClipKnee * ceiling;
    if (ax <= k) return x;
    const double y = std::min(ceiling, k + (ceiling - k) * std::tanh((ax - k) / (ceiling - k)));
    return x < 0.0 ? -y : y;
}

// The shaped and the dry signal of one channel for a band mode (gain G on the band, mixed in by mx).
inline void bandMix(int band, double xd, double lo, double hi, double G, double mx, double& out, double& dry)
{
    const double m = mx * (G - 1.0);
    switch (band)
    {
    case kBandLow: dry = lo + hi; out = dry + m * lo; break;
    case kBandHigh: dry = lo + hi; out = dry + m * hi; break;
    default: dry = xd; out = xd + m * xd; break;
    }
}

// ---- the shape display -------------------------------------------------------------------------------------------------
// A stylised drum hit (a 0.6 ms rise to a click that drops ~8 dB in 15 ms, then a body decaying ~40 dB in 400 ms, over a
// -42 dB room) through the same detector, gain law, smoothing, lookahead and mix, at `rate` Hz. before / after: the
// amplitude envelopes (the hit's peak = 1) at t = t0 + k / rate (seconds from the onset), k = 0..n-1.
inline double demoHit(double t)
{
    const double room = 0.0079;
    if (t < 0.0) return room;
    const double rise = std::min(1.0, t / 0.0006);
    return room + rise * (0.62 * std::exp(-t / 0.006) + 0.38 * std::exp(-t / 0.075)) / (0.62 * std::exp(-0.1) + 0.38 * std::exp(-0.008));
}
inline void shapeDemo(const Settings& s, double rate, double t0, int n, float* before, float* after)
{
    DetectorCoefs c;
    c.set(clampv(s.attackMs, 1.0, 100.0), clampv(s.releaseMs, 20.0, 2000.0), rate);
    GainSmoother g;
    g.setup(rate);
    Detector d;
    d.reset(demoHit(-1.0));
    const int la = (int)std::lround(clampv(s.lookaheadMs, 0.0, (double)kMaxLookaheadMs) * 0.001 * rate);
    const int k0 = (int)std::lround(t0 * rate);   // the first output sample's time index
    const double atk = clampv(s.attack, -100.0, 100.0) * 0.01, sus = clampv(s.sustain, -100.0, 100.0) * 0.01;
    const double mx = clampv(s.mix, 0.0, 100.0) * 0.01;
    for (int j = k0 - (int)(0.4 * rate); j < k0 + n + la; ++j)   // the detector runs `la` samples ahead of the audio
    {
        double rise = 0.0, fall = 0.0;
        d.process(demoHit(j / rate), c, rise, fall);
        g.process(shapeDb(atk, rise, kAttackSens), shapeDb(sus, fall, kSustainSens));
        const int k = j - la - k0;
        if (k >= 0 && k < n)
        {
            const double e = demoHit((j - la) / rate);
            before[k] = (float)e;
            after[k] = (float)(e * (1.0 + mx * (std::exp(g.total() * kDbToLn) - 1.0)));
        }
    }
}

// ---- the engine ----------------------------------------------------------------------------------------------------------
class Engine {
public:
    static constexpr int kChunk = 64;
    static constexpr double kHistRate = 360.0;      // history points per second
    static constexpr int kHistRing = 1 << 12;

    // feeds for the UI: the history (input and output peaks in dBFS, attack and sustain gain in dB, each the extreme
    // of 1/360 s, all aligned with the output) and the gain extremes since the UI last read them
    ScopeRing histIn { kHistRing }, histOut { kHistRing }, histAtk { kHistRing }, histSus { kHistRing };
    MeterValue gainHi, gainLo, atkHi, atkLo, susHi, susLo;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        maxLa = (int)std::ceil(kMaxLookaheadMs * 0.001 * sr) + 1;
        for (auto& d : dly) d.setMaxDelay(maxLa + 2);
        const int ramp = std::max(1, (int)(0.020 * sr));
        atkS.setRamp(ramp); atkS.reset(set.attack * 0.01);
        susS.setRamp(ramp); susS.reset(set.sustain * 0.01);
        linkS.setRamp(ramp); linkS.reset(set.link * 0.01);
        mixS.setRamp(ramp); mixS.reset(set.mix * 0.01);
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        ceilS.setRamp(ramp); ceilS.reset(dbToGain(set.ceilingDb));
        clipS.setRamp(std::max(1, (int)(0.010 * sr))); clipS.reset(set.clip ? 1.0 : 0.0);
        deltaS.setRamp(std::max(1, (int)(0.010 * sr))); deltaS.reset(set.delta ? 1.0 : 0.0);
        depthS.setRamp(std::max(1, (int)(0.010 * sr))); depthS.reset(1.0);
        trans.setRamp(std::max(1, (int)(0.006 * sr))); trans.reset(1.0);
        settleLen = (int)(0.050 * sr);
        bandStep = 1.0 / (0.020 * sr);
        glideK = 1.0 - std::exp(-(double)kChunk / (0.030 * sr));
        histHop = std::max(1, (int)std::lround(sr / kHistRate));
        for (auto& g : gain) g.setup(sr);
        wantLa = curLa = lookaheadSamples(set.lookaheadMs);
        wantFocus = curFocus = clampv(set.focus, 0, kNumFocus - 1);
        wantBand = curBand = fromBand = clampv(set.band, 0, kNumBands - 1);
        bandT = 1.0; focusPhase = 0; settle = 0;
        atkMs = clampv(set.attackMs, 1.0, 100.0); relMs = clampv(set.releaseMs, 20.0, 2000.0);
        coefs.set(atkMs, relMs, sr);
        xover = clampv(set.crossover, 40.0, 8000.0); xoverSet = xover;
        for (int ch = 0; ch < 2; ++ch) { focusF[ch].setup(sr); split[ch].set(xover, sr); dly[ch].setDelay(curLa); }
        clearState();
    }

    // ---- settings (audio thread, block start) ----
    void setSettings(const Settings& s)
    {
        set = s;
        wantLa = lookaheadSamples(s.lookaheadMs);
        wantFocus = clampv(s.focus, 0, kNumFocus - 1);
        wantBand = clampv(s.band, 0, kNumBands - 1);
    }

    int latency() const { return curLa; }
    int maxLatency() const { return maxLa; }
    int lookaheadSamples(double ms) const { return clampv((int)std::lround(clampv(ms, 0.0, (double)kMaxLookaheadMs) * 0.001 * sr), 0, maxLa); }

    // gainDb (optional, for analysis): the gain applied to each sample (dB, mean of the channels)
    void process(float* L, float* R, int n, float* gainDb = nullptr)
    {
        for (int off = 0; off < n; off += kChunk)
            processChunk(L + off, R + off, std::min(kChunk, n - off), gainDb ? gainDb + off : nullptr);
    }

private:
    void clearState()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            focusF[ch].reset(); env[ch].reset(); split[ch].reset(); dly[ch].clear(); gain[ch].reset();
        }
        for (auto& d : det) d.reset();
        hIn = hOut = 0.0f; hAtk = hSus = 0.0f; hCount = 0;
    }
    void applyLookahead()
    {
        curLa = wantLa;
        for (auto& d : dly) d.setDelay(curLa);
    }

    void processChunk(float* L, float* R, int n, float* gainDb)
    {
        // structural changes: lookahead (output fade), focus (shaping fade + settle), band (crossfade)
        if (wantLa != curLa)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0) applyLookahead();
        }
        if (wantLa == curLa) trans.setTarget(1.0);
        if (focusPhase == 0 && wantFocus != curFocus) { focusPhase = 1; depthS.setTarget(0.0); }
        if (focusPhase == 1 && !depthS.isSmoothing())
        {
            curFocus = wantFocus;
            for (auto& en : env) en.reset();
            settle = settleLen;
            focusPhase = 2;
        }
        if (focusPhase == 2 && settle <= 0) { focusPhase = 0; depthS.setTarget(1.0); }
        if (bandT >= 1.0 && wantBand != curBand) { fromBand = curBand; curBand = wantBand; bandT = 0.0; }

        // control-rate glides (log domain): times and crossover
        atkMs *= std::exp((std::log(clampv(set.attackMs, 1.0, 100.0)) - std::log(atkMs)) * glideK);
        relMs *= std::exp((std::log(clampv(set.releaseMs, 20.0, 2000.0)) - std::log(relMs)) * glideK);
        coefs.set(atkMs, relMs, sr);
        xover *= std::exp((std::log(clampv(set.crossover, 40.0, 8000.0)) - std::log(xover)) * glideK);
        if (std::fabs(xover - xoverSet) > 1e-4 * xoverSet) { xoverSet = xover; for (auto& s : split) s.set(xover, sr); }

        atkS.setTarget(clampv(set.attack, -100.0, 100.0) * 0.01);
        susS.setTarget(clampv(set.sustain, -100.0, 100.0) * 0.01);
        linkS.setTarget(clampv(set.link, 0.0, 100.0) * 0.01);
        mixS.setTarget(clampv(set.mix, 0.0, 100.0) * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        ceilS.setTarget(dbToGain(clampv(set.ceilingDb, -12.0, 0.0)));
        clipS.setTarget(set.clip ? 1.0 : 0.0);
        deltaS.setTarget(set.delta ? 1.0 : 0.0);

        float gHi = 0.0f, gLo = 0.0f, aHi = 0.0f, aLo = 0.0f, sHi = 0.0f, sLo = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double atk = atkS.next(), sus = susS.next(), lnk = linkS.next(), mx = mixS.next(), og = outS.next();
            const double cg = ceilS.next(), cl = clipS.next(), dl = deltaS.next(), tr = trans.next(), dep = depthS.next();
            const double x[2] = { L[i], R[i] };
            // the detector (ahead of the audio by the lookahead)
            double e[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const double xf = focusF[ch].process(x[ch], curFocus);
                e[ch] = std::max(env[ch].process(xf), std::fabs(xf));
            }
            const double eLink = std::max(e[0], e[1]);
            double rise[3] = { 0.0, 0.0, 0.0 }, fall[3] = { 0.0, 0.0, 0.0 };
            if (settle > 0)
            {
                --settle;
                det[0].reset(e[0]); det[1].reset(e[1]); det[2].reset(eLink);
            }
            else
            {
                det[0].process(e[0], coefs, rise[0], fall[0]);
                det[1].process(e[1], coefs, rise[1], fall[1]);
                det[2].process(eLink, coefs, rise[2], fall[2]);
            }
            double w = 1.0;   // the current band's share of a band crossfade
            if (bandT < 1.0) { bandT = std::min(1.0, bandT + bandStep); w = 0.5 - 0.5 * std::cos(kPi * bandT); }
            double gMean = 0.0, aMean = 0.0, sMean = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const double r = rise[ch] + (rise[2] - rise[ch]) * lnk;
                const double f = fall[ch] + (fall[2] - fall[ch]) * lnk;
                GainSmoother& g = gain[ch];
                g.process(dep * shapeDb(atk, r, kAttackSens), dep * shapeDb(sus, f, kSustainSens));
                const double gdb = g.total();
                const double G = std::exp(gdb * kDbToLn);
                // the audio
                const double xd = dly[ch].process((float)x[ch]);
                double lo, hi;
                split[ch].process(xd, lo, hi);
                double out, dry;
                bandMix(curBand, xd, lo, hi, G, mx, out, dry);
                if (w < 1.0)
                {
                    double o0, d0;
                    bandMix(fromBand, xd, lo, hi, G, mx, o0, d0);
                    out = o0 + (out - o0) * w;
                    dry = d0 + (dry - d0) * w;
                }
                double y = out * og;
                if (cl > 0.0) y += (softClip(y, cg) - y) * cl;
                if (dl > 0.0) y += ((out - dry) * og - y) * dl;
                y *= tr;
                (ch ? R : L)[i] = (float)y;
                gMean += 0.5 * gdb; aMean += 0.5 * g.a2; sMean += 0.5 * g.s;
                hIn = std::max(hIn, (float)std::fabs(xd));
                hOut = std::max(hOut, (float)std::fabs(y));
            }
            if (gainDb) gainDb[i] = (float)gMean;
            gHi = std::max(gHi, (float)gMean); gLo = std::min(gLo, (float)gMean);
            aHi = std::max(aHi, (float)aMean); aLo = std::min(aLo, (float)aMean);
            sHi = std::max(sHi, (float)sMean); sLo = std::min(sLo, (float)sMean);
            if (std::fabs(aMean) > std::fabs(hAtk)) hAtk = (float)aMean;
            if (std::fabs(sMean) > std::fabs(hSus)) hSus = (float)sMean;
            if (++hCount >= histHop)
            {
                histIn.push(gainToDbF(hIn)); histOut.push(gainToDbF(hOut)); histAtk.push(hAtk); histSus.push(hSus);
                hIn = hOut = hAtk = hSus = 0.0f; hCount = 0;
            }
        }
        // recover from a non-finite state (clear and keep going)
        double chk = 0.0;
        for (int ch = 0; ch < 2; ++ch) chk += gain[ch].total() + gain[ch].a1 + env[ch].state() + split[ch].lp2.ic2 + split[ch].hp2.ic2 + focusF[ch].state();
        for (const auto& d : det) chk += d.f + d.s1 + d.s2;
        if (!std::isfinite(chk))
        {
            clearState();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
            gHi = gLo = aHi = aLo = sHi = sLo = 0.0f;
        }
        gainHi.setMax(gHi); gainLo.setMin(gLo);
        atkHi.setMax(aHi); atkLo.setMin(aLo);
        susHi.setMax(sHi); susLo.setMin(sLo);
    }

    double sr = 48000.0;
    Settings set;
    FocusBank focusF[2];
    Envelope env[2];
    Detector det[3];            // left, right, linked
    DetectorCoefs coefs;
    GainSmoother gain[2];
    LR4Split split[2];
    DelayLine dly[2];
    LinearSmoother atkS, susS, linkS, mixS, outS, ceilS, clipS, deltaS, depthS, trans;
    double atkMs = 10.0, relMs = 200.0, xover = 250.0, xoverSet = 250.0, glideK = 0.1, bandT = 1.0, bandStep = 0.001;
    int maxLa = 0, wantLa = 0, curLa = 0, wantFocus = 0, curFocus = 0, wantBand = 0, curBand = 0, fromBand = 0;
    int focusPhase = 0, settle = 0, settleLen = 2400, histHop = 120, hCount = 0;
    float hIn = 0.0f, hOut = 0.0f, hAtk = 0.0f, hSus = 0.0f;
};

}} // namespace pump::snap
