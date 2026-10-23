// pump COMP engine: a Pro-C 2 style compressor with six detector/envelope designs, lookahead, oversampling,
// mid/side, stereo link, external sidechain with filters, parallel mix and auto makeup. Framework-free (tested
// offline in tests/test_comp.cpp).
//
// Signal flow (per channel pair, "proc" channels are L/R or M/S):
//
//   in -> input gain -+-> dry delay (lookahead + oversampler latency) ---------------------------+-> mix -> out gain
//                     +-> L/R or M/S -> lookahead delay -> upsample -> x gain -> (tube) -> down -+
//   detector source (proc channels, or the external sidechain converted the same way)
//     -> SC gain -> SC high-pass / low-pass -> upsample -> level detector -> static curve -> link -> envelope -> gain
//
// The detector always sees the signal `lookahead` ahead of the audio it controls, and runs at the oversampled
// rate, so peaks between base-rate samples are seen and fast gain changes don't alias.
//
// Level detection: every style starts from the same two measurements of the (upsampled) sidechain signal:
//   peak-ish  = max(|x|, |analytic(x)|). The analytic magnitude comes from a 90-degree IIR allpass pair
//               (phase error < 0.2 degrees from 20 Hz to 20 kHz), so a steady tone reads its true amplitude at any
//               frequency without ripple, and |x| catches every onset with zero delay.
//   RMS-ish   = one-pole mean of |analytic|^2 (a steady sine reads its peak level, so the threshold means the
//               same thing in every style; noise reads its RMS + 3 dB).
//
// The styles (each a different detector, curve and envelope):
//   Clean   feed-forward, peak-ish detector, exact soft-knee curve, log-domain smooth-branching attack/release
//           (one pole in dB). Auto release: dual release, the deeper of a fast follower (0.5 x release) and a slow
//           one (slow attack, 3 x release) wins, so short transients recover quickly and sustained compression
//           recovers slowly.
//   Glue    bus "glue": detector halfway between peak and a 10 ms RMS (in dB), and a program-dependent release that
//           slows as the reduction gets deeper (x3 at 24 dB), so it holds a mix together without pumping.
//           Auto release adds the dual release on top.
//   Opto    optical cell: 5 ms RMS detector; program-dependent attack (big jumps attack faster, like a lamp
//           driving a photocell harder); two-stage release: the gain reduction is the sum of a fast part
//           (0.3 x release) and a slow "memory" part (4 x release) that only charges during sustained compression
//           (0.6 s); and a softening ratio: the reduction is G / (1 + |G| / 30), so heavy overs get gentler.
//           Auto release charges the memory faster and deeper.
//   Punch   feedback topology: the detector listens to its own output (the sidechain times the loop gain). The
//           loop targets the inverse of the static curve so the steady state follows the drawn curve exactly, and
//           its coefficients are compensated by the local loop gain so it is stable at any ratio and the time
//           constants mean what they say. A second, attack-only pole outside the loop rounds the onset, so the
//           first milliseconds of each transient pass through: punch. Auto release: release follows the program
//           load (0.4 x .. 3 x release).
//   Vari-Mu variable-mu tube: 15 ms RMS detector; the ratio rises with level over a very wide knee
//           (G = -s u^2 / (u + W), W = 12 dB + knee); rounded two-pole attack; release slows with depth; and a very
//           gentle asymmetric (2nd-harmonic) tube colour that grows with the reduction, as a biased tube's does
//           (about 0.15% THD idle, 1% at 12 dB of reduction for a -6 dBFS tone; oversampled when oversampling is
//           on; DC-blocked). Auto release: program load.
//   Pump    aggressive EDM pumping: peak-ish detector, attack in dB, release in the LINEAR gain domain (fast
//           recovery out of deep reduction, then a soft landing: the classic sidechain "breathing"). Use with the
//           external sidechain (kick) and a short release. Auto release: program load.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Oversampler.hpp"
#include "dsp/Saturation.hpp"
#include "CompParams.hpp"

namespace pump { namespace comp {

enum class Style : int { Clean = 0, Glue, Opto, Punch, VariMu, Pump, Count };
enum class Chan : int { Stereo = 0, MidSide, Mid, Side };

constexpr double kDbToLn = 0.11512925464970229;   // ln(10) / 20
inline double toDb(double x) { return 8.6858896380650366 * std::log(x > 1e-12 ? x : 1e-12); }
inline double powToDb(double p) { return 4.3429448190325183 * std::log(p > 1e-24 ? p : 1e-24); }
inline double flushTiny(double v) { return std::fabs(v) < 1e-25 ? 0.0 : v; }

// ---------------------------------------------------------------------------------------------------------------
// Static curves (shared with the UI's transfer display).

struct Curve {
    double T = -18.0;     // threshold, dB
    double s = 0.5;       // slope = 1 - 1/ratio (1 = infinite ratio)
    double W = 6.0;       // knee width, dB
    double range = 60.0;  // deepest reduction, dB (positive)
    int style = 0;

    static double slopeOf(double ratio) { return ratio >= 99.95 ? 1.0 : 1.0 - 1.0 / std::max(1.0, ratio); }
    void set(int st, double threshold, double ratio, double knee, double rng)
    {
        style = st; T = threshold; s = slopeOf(ratio); W = std::max(0.0, knee); range = std::max(0.0, rng);
    }

    // Standard soft knee (quadratic, centred on the threshold).
    double standard(double L) const
    {
        const double o = L - T;
        if (W > 0.0 && std::fabs(o) < 0.5 * W) return -s * (o + 0.5 * W) * (o + 0.5 * W) / (2.0 * W);
        return o > 0.0 ? -s * o : 0.0;
    }
    static constexpr double kOptoSoft = 30.0;
    // Gain change (dB, <= 0) for a detector level, for the style's curve.
    double gain(double L) const
    {
        double g;
        switch ((Style)style)
        {
        case Style::Opto: g = standard(L); g = g / (1.0 + std::fabs(g) / kOptoSoft); break;
        case Style::VariMu: {
            const double Wk = 12.0 + W;
            const double u = L - T + 0.5 * Wk;
            g = u > 0.0 ? -s * u * u / (u + Wk) : 0.0;
            break;
        }
        default: g = standard(L); break;
        }
        return std::max(g, -range);
    }
    // Auto makeup: half the reduction a 0 dBFS level would get (as Thunder Plus does).
    double makeup() const { return -0.5 * gain(0.0); }

    // Punch (feedback): input level that the standard curve maps to output level y (ratio capped at 100:1).
    // Also returns the local loop gain k = d(input)/d(output) - 1 (0 below the knee, ratio - 1 above).
    double inverse(double y, double& k) const
    {
        const double sc = std::min(s, 0.99);
        k = 0.0;
        if (sc < 1e-9) return y;
        const double R = 1.0 / (1.0 - sc);
        const double u = y - T;
        if (W <= 0.0)
        {
            if (u <= 0.0) return y;
            k = R - 1.0;
            return T + u * R;
        }
        if (u <= -0.5 * W) return y;
        if (u >= 0.5 * W / R) { k = R - 1.0; return T + u * R; }
        const double disc = std::max(0.0, 1.0 - 2.0 * sc * (u + 0.5 * W) / W);
        const double v = W / sc * (1.0 - std::sqrt(disc));   // distance into the knee (input side)
        const double fp = std::max(1e-6, 1.0 - sc * v / W);   // output slope there
        k = 1.0 / fp - 1.0;
        return T + v - 0.5 * W;
    }
    // Feedback target for an output-side level: the reduction that puts the output on the curve.
    double feedbackTarget(double y, double& k) const
    {
        const double x = inverse(y, k);
        const double g = y - x;
        if (g < -range) { k = 0.0; return -range; }
        return g;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// 90-degree phase-difference allpass pair (polyphase IIR, elliptic-halfband design as in Laurent de Soras' HIIR),
// giving the analytic signal's magnitude: a ripple-free envelope of tones at any frequency.

class HilbertEnvelope {
public:
    static constexpr int kMax = 16;
    void setup(double sr)
    {
        nc = sr > 100000.0 ? 16 : 12;
        design(nc, 24.0 / sr, c);
        reset();
    }
    void reset() { for (auto& s : st) s = St(); bPrev = 0.0; }
    inline double process(double x)
    {
        double a = x, b = x;
        for (int k = 0; k < nc; k += 2) a = stage(k, a);
        for (int k = 1; k < nc; k += 2) b = stage(k, b);
        const double bd = bPrev;
        bPrev = b;
        return std::sqrt(a * a + bd * bd);
    }
    static void design(int n, double tw, double* out)
    {
        double k = std::tan((1.0 - tw * 2.0) * kPi / 4.0);
        k *= k;
        const double kk = std::pow(1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
        const double e2 = e * e, e4 = e2 * e2;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
        const int order = 2 * n + 1;
        for (int idx = 0; idx < n; ++idx)
        {
            const int cc = idx + 1;
            double num = 0.0, sgn = 1.0;
            for (int i = 0; i < 64; ++i)
            {
                const double t = std::pow(q, (double)(i * (i + 1))) * std::sin((i * 2 + 1) * cc * kPi / order) * sgn;
                num += t; sgn = -sgn;
                if (std::fabs(t) < 1e-100) break;
            }
            num *= std::pow(q, 0.25);
            double den = 0.0; sgn = -1.0;
            for (int i = 1; i < 64; ++i)
            {
                const double t = std::pow(q, (double)(i * i)) * std::cos(i * 2 * cc * kPi / order) * sgn;
                den += t; sgn = -sgn;
                if (std::fabs(t) < 1e-100) break;
            }
            den += 0.5;
            const double ww = num / den, w2 = ww * ww;
            const double x = std::sqrt((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
            out[idx] = (1.0 - x) / (1.0 + x);
        }
    }
private:
    struct St { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    // (c - z^-2) / (1 - c z^-2)
    inline double stage(int k, double x)
    {
        St& s = st[k];
        const double y = flushTiny(c[k] * (x + s.y2) - s.x2);
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    double c[kMax] = {};
    St st[kMax];
    double bPrev = 0.0;
    int nc = 12;
};

// TPT state-variable filter (2-pole Butterworth high- or low-pass), modulation friendly.
struct SvfFilter {
    double g = 0.0, k = 1.41421356237, a1 = 0.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;
    void set(double f, double sr)
    {
        g = std::tan(kPi * clampv(f, 2.0, 0.49 * sr) / sr);
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0; }
    inline void run(double v0, double& lp, double& hp)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushTiny(2.0 * v1 - ic1);
        ic2 = flushTiny(2.0 * v2 - ic2);
        lp = v2; hp = v0 - k * v1 - v2;
    }
    inline double highpass(double x) { double l, h; run(x, l, h); return h; }
    inline double lowpass(double x) { double l, h; run(x, l, h); return l; }
};

// One-pole smoother toward a target (per sample), for gains.
struct OnePole {
    double y = 0.0, c = 0.0;
    void setTime(double ms, double sr) { c = tauCoef(ms * 0.001, sr); }
    void reset(double v) { y = v; }
    inline double next(double target) { y = target + (y - target) * c; if (std::fabs(y - target) < 1e-9) y = target; return y; }
};

// ---------------------------------------------------------------------------------------------------------------

struct Settings {
    int style = 0;
    double threshold = -18.0, ratio = 2.0, knee = 6.0, range = 60.0;
    double attack = 10.0, release = 100.0, hold = 0.0, lookahead = 0.0;   // ms
    bool autoRelease = false, autoMakeup = false;
    double mix = 100.0, inGain = 0.0, outGain = 0.0, link = 100.0;      // %, dB, dB, %
    int channelMode = 0;
    bool scExternal = false;
    double scGain = 0.0, scHpf = kHpfOff, scLpf = kLpfOff;
    bool scListen = false;
    int oversampling = 0;   // 0 = 1x, 1 = 2x, 2 = 4x
};

class Engine {
public:
    static constexpr int kChunk = 64;
    static constexpr int kHistRing = 1 << 13;
    static constexpr double kHistRate = 400.0;   // history points per second

    // ---- UI feeds (read from the UI thread) ----
    ScopeRing histIn { kHistRing }, histOut { kHistRing }, histGr { kHistRing }, histDet { kHistRing };   // dB
    MeterValue inPeak[2], outPeak[2];   // linear max since the UI last read
    MeterValue grMin[2];                // dB min (deepest) per proc channel
    MeterValue detLevel;                // dB max: input-referred detector level (transfer display dot)
    MeterValue scPeak;                  // linear max of the raw external sidechain input
    std::atomic<float> makeupNow { 0.0f };

    void prepare(double sampleRate, int maxBlock)
    {
        (void)maxBlock;
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int i = 0; i < 3; ++i) { osMain[i].prepare(i, kChunk); osDet[i].prepare(i, kChunk); }
        const int maxLa = (int)std::ceil(0.020 * sr) + 2;
        maxLatency = maxLa + osMain[2].latency() + 2;
        for (int c = 0; c < 2; ++c)
        {
            laDly[c].setMaxDelay(maxLa);
            dryDly[c].setMaxDelay(maxLatency);
            lisDly[c].setMaxDelay(maxLatency);
        }
        fadeLen = std::max(16, (int)std::lround(0.003 * sr));
        histHop = std::max(1, (int)std::lround(sr / kHistRate));
        for (auto& f : hpf) f.reset();
        for (auto& f : lpf) f.reset();
        const double gms = 20.0;
        inG.setTime(gms, sr); outG.setTime(gms, sr); mkG.setTime(60.0, sr); mixS.setTime(gms, sr);
        scG.setTime(gms, sr); lisS.setTime(10.0, sr);
        prepared = true;
        applySettings(true);
        applyTopology();
        resetAll();
        // jump smoothers to their targets
        inG.reset(dbToGain(set.inGain)); outG.reset(dbToGain(set.outGain)); mixS.reset(set.mix * 0.01);
        scG.reset(dbToGain(set.scGain)); lisS.reset(set.scListen ? 1.0 : 0.0);
        mkG.reset(dbToGain(set.autoMakeup ? curveTarget.makeup() : 0.0));
        cur = curveTarget;
        hpfHz = targetHpf(); lpfHz = targetLpf();
        updateFilters();
    }

    void setSettings(const Settings& s)
    {
        set = s;
        if (prepared) applySettings(false);
    }
    const Settings& settings() const { return set; }

    // Apply a pending latency/topology change at once (no fade) — for tests and activation.
    void commitTopology() { applySettings(false); if (!(topo == pending)) applyTopology(); fadeState = 0; }

    int latency() const { return topo.la + osMain[topo.os].latency(); }
    int maxLatencySamples() const { return maxLatency; }
    double sampleRate() const { return sr; }
    double grDb(int c) const { return ch[c].g; }   // current applied gain change (tests)

    // Process a block. scL/scR may be null (no sidechain bus: treated as silence). In-place safe.
    void process(const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min(kChunk, n - done);
            processChunk(inL + done, inR + done, scL ? scL + done : nullptr, scR ? scR + done : nullptr, outL + done, outR + done, m);
            done += m;
        }
    }

private:
    struct Topo {
        int os = 0, la = 0, chan = 0;
        bool operator==(const Topo& o) const { return os == o.os && la == o.la && chan == o.chan; }
    };
    struct ChanState {
        HilbertEnvelope hil;
        double pw = 0.0;          // RMS-ish power follower
        double g = 0.0;           // applied gain change, dB
        double f = 0.0, s = 0.0;  // dual release followers (Clean/Glue auto)
        double a = 0.0, b = 0.0;  // Opto fast + slow parts
        double g1 = 0.0;          // Punch loop state / Vari-Mu first attack pole
        double lin = 1.0;         // Pump: linear-domain gain
        double load = 0.0;        // program load (auto release)
        double fb = 1.0;          // Punch: loop gain applied to the detector
        int hold = 0;
        double dcx = 0.0, dcy = 0.0;
        void resetDyn() { pw = 0.0; g = f = s = a = b = g1 = 0.0; lin = 1.0; load = 0.0; fb = 1.0; hold = 0; dcx = dcy = 0.0; hil.reset(); }
    };

    double targetHpf() const { return hpfActive(set.scHpf) ? set.scHpf : 5.0; }
    double targetLpf() const { return lpfActive(set.scLpf) ? set.scLpf : std::min(0.45 * sr, 40000.0); }
    void updateFilters()
    {
        for (int c = 0; c < 2; ++c) { hpf[c].set(hpfHz, sr); lpf[c].set(lpfHz, sr); }
    }

    void applySettings(bool first)
    {
        curveTarget.set(clampv(set.style, 0, 5), set.threshold, set.ratio, set.knee, set.range);
        pending.os = clampv(set.oversampling, 0, 2);
        pending.la = clampv((int)std::lround(set.lookahead * 0.001 * sr), 0, (int)std::ceil(0.020 * sr));
        pending.chan = clampv(set.channelMode, 0, 3);
        if (first) topo = pending;
        const int newStyle = clampv(set.style, 0, 5);
        if (!first && newStyle != styleNow) switchStyle(newStyle);
        styleNow = newStyle;
        if (!first && set.autoRelease != autoNow) for (auto& c : ch) { c.f = c.s = c.g; }
        autoNow = set.autoRelease;
        updateTimes();
    }

    // Keep the gain continuous across a style change: seed the new style's states from the current reduction.
    void switchStyle(int st)
    {
        for (auto& c : ch)
        {
            c.f = c.s = c.g1 = c.g;
            c.a = c.g; c.b = 0.0;
            c.lin = dbToGain(c.g);
            c.fb = dbToGain(c.g);
            if (st == (int)Style::Punch) c.g1 = c.g;
        }
    }

    void updateTimes()
    {
        const double fs = sr * (1 << topo.os);
        fsOs = fs;
        auto lnc = [&](double ms) { return -1.0 / (std::max(ms, 1e-4) * 0.001 * fs); };
        lnA = lnc(set.attack);
        lnR = lnc(set.release);
        cA = std::exp(lnA); cR = std::exp(lnR);
        cAhalf = std::exp(lnc(0.5 * set.attack));
        cFastRel = std::exp(lnc(0.5 * set.release));          // dual: fast
        cSlowAtk = std::exp(lnc(2.0 * set.attack + 400.0));    // dual: slow follower charges only under sustained reduction
        cSlowRel = std::exp(lnc(3.0 * set.release));           // dual: slow follower release
        cOptoFast = std::exp(lnc(0.3 * set.release));
        cOptoSlow = std::exp(lnc((set.autoRelease ? 2.5 : 4.0) * set.release));
        cOptoCharge = std::exp(lnc(set.autoRelease ? 250.0 : 600.0));
        optoMem = set.autoRelease ? 0.65 : 0.5;
        cRms5 = std::exp(lnc(5.0)); cRms10 = std::exp(lnc(10.0)); cRms15 = std::exp(lnc(15.0));
        cLoad = std::exp(lnc(500.0));
        holdLen = (int)std::lround(set.hold * 0.001 * fs);
        dcR = std::exp(-kTwoPi * 10.0 / fs);
    }

    void applyTopology()
    {
        const bool osChanged = topo.os != pending.os;
        topo = pending;
        osMain[topo.os].reset();
        osDet[topo.os].reset();
        // The delay lines keep their history (they run all the time), so the new delays read real audio at once;
        // only the newly selected oversampler starts empty (the caller mutes until it is primed). The gain
        // reduction state carries over, so the level doesn't jump when the sound comes back.
        for (int c = 0; c < 2; ++c)
        {
            laDly[c].setDelay(topo.la);
            dryDly[c].setDelay(latency());
            lisDly[c].setDelay(latency());
        }
        updateTimes();
        if (osChanged) for (auto& c : ch) c.hil.setup(fsOs);
    }
    void resetAll()
    {
        for (int c = 0; c < 2; ++c) { laDly[c].clear(); dryDly[c].clear(); lisDly[c].clear(); hpf[c].reset(); lpf[c].reset(); }
        for (int i = 0; i < 3; ++i) { osMain[i].reset(); osDet[i].reset(); }
        for (auto& c : ch) { c.hil.setup(fsOs); c.resetDyn(); }
        hIn = hOut = hDet = hGr = 0.0f; hCount = 0;
    }

    // ---- the per-sample detector + envelope, one proc channel ----
    inline double detect(ChanState& c, double x, int st)
    {
        const double e = c.hil.process(x);
        const double pk = std::max(std::fabs(x), e);
        switch ((Style)st)
        {
        case Style::Glue:
            c.pw = flushTiny(e * e + (c.pw - e * e) * cRms10);
            return 0.5 * (toDb(pk) + powToDb(c.pw));
        case Style::Opto:
            c.pw = flushTiny(e * e + (c.pw - e * e) * cRms5);
            return powToDb(c.pw);
        case Style::VariMu:
            c.pw = flushTiny(e * e + (c.pw - e * e) * cRms15);
            return powToDb(c.pw);
        default:
            return toDb(pk);
        }
    }

    // One-pole in dB with hold; attack when the target is deeper.
    inline double follow(double y, double t, double ca, double cr, int& hold)
    {
        if (t < y) { hold = holdLen; return t + (y - t) * ca; }
        if (hold > 0) { --hold; return y; }
        return t + (y - t) * cr;
    }

    inline void envelope(ChanState& c, double t, double k, int st)
    {
        const bool autoRel = set.autoRelease;
        switch ((Style)st)
        {
        case Style::Clean:
        case Style::Glue: {
            double cr = cR, crFast = cFastRel, crSlow = cSlowRel;
            if (st == (int)Style::Glue)
            {
                // program-dependent release: slower when the reduction is deep
                const double m = 1.0 / (1.0 + std::fabs(c.g) / 12.0);
                if (!autoRel) cr = std::exp(lnR * m);
                else { crFast = std::pow(cFastRel, m); crSlow = std::pow(cSlowRel, m); }
            }
            if (!autoRel) { c.g = follow(c.g, t, cA, cr, c.hold); }
            else
            {
                int h2 = c.hold;
                c.f = follow(c.f, t, cA, crFast, c.hold);
                c.s = (t < c.s) ? t + (c.s - t) * cSlowAtk : (h2 > 0 ? c.s : t + (c.s - t) * crSlow);
                c.g = std::min(c.f, c.s);
            }
            break;
        }
        case Style::Opto: {
            // slow "memory" part: charges only under sustained reduction, releases slowly
            const double bt = optoMem * t;
            const bool holding = c.hold > 0;
            if (bt < c.b) c.b = bt + (c.b - bt) * cOptoCharge;
            else if (!holding) c.b = bt + (c.b - bt) * cOptoSlow;
            // fast part: program-dependent attack (bigger jumps = faster), fast release
            const double at = std::min(0.0, t - c.b);
            if (at < c.a)
            {
                const double over = c.a - at;
                c.a = at + (c.a - at) * std::exp(lnA * (1.0 + over / 6.0));
                c.hold = holdLen;
            }
            else if (c.hold > 0) --c.hold;
            else c.a = at + (c.a - at) * cOptoFast;
            c.g = c.a + c.b;
            break;
        }
        case Style::Punch: {
            // loop state with coefficients compensated by the local loop gain k (stable, true time constants)
            double cr = cR;
            if (autoRel) cr = std::exp(lnR / (0.4 + 2.6 * c.load));
            const double kk = 1.0 + k;
            if (t < c.g1) { c.g1 = t + (c.g1 - t) * (1.0 - (1.0 - cA) / kk); c.hold = holdLen; }
            else if (c.hold > 0) --c.hold;
            else c.g1 = t + (c.g1 - t) * (1.0 - (1.0 - cr) / kk);
            c.fb = std::exp(c.g1 * kDbToLn);
            // rounded onset outside the loop: transients get through
            c.g = (c.g1 < c.g) ? c.g1 + (c.g - c.g1) * cAhalf : c.g1;
            break;
        }
        case Style::VariMu: {
            double m = 1.0 / (1.0 + std::fabs(c.g) / 18.0);
            if (autoRel) m /= (0.4 + 2.6 * c.load);
            const double cr = std::exp(lnR * m);
            if (t < c.g1) { c.g1 = t + (c.g1 - t) * cAhalf; c.hold = holdLen; }
            else if (c.hold > 0) --c.hold;
            else c.g1 = t + (c.g1 - t) * cr;
            c.g = (c.g1 < c.g) ? c.g1 + (c.g - c.g1) * cAhalf : c.g1;
            break;
        }
        case Style::Pump: {
            double cr = cR;
            if (autoRel) cr = std::exp(lnR / (0.4 + 2.6 * c.load));
            if (t < c.g) { c.g = t + (c.g - t) * cA; c.lin = std::exp(c.g * kDbToLn); c.hold = holdLen; }
            else if (c.hold > 0) --c.hold;
            else
            {
                const double tl = std::exp(t * kDbToLn);
                c.lin = tl + (c.lin - tl) * cr;   // linear-domain release
                c.g = std::min(0.0, toDb(c.lin));
            }
            break;
        }
        default: break;
        }
        if (autoRel) c.load = c.load * cLoad + (t < -1.0 ? 1.0 - cLoad : 0.0);
        c.g = flushTiny(std::min(0.0, c.g));
        if (c.g > -1e-9 && t >= 0.0 && c.hold == 0) { c.g = 0.0; c.f = flushTiny(c.f); c.s = flushTiny(c.s); c.a = flushTiny(c.a); c.b = flushTiny(c.b); c.g1 = flushTiny(c.g1); }
    }

    // Vari-Mu colour: the tube is the gain element, so its distortion grows with the bias (the reduction). The
    // colour is shaped from the signal entering the stage and scaled with the gain, at 10% when idle rising to 100%
    // at 12 dB of reduction (about 0.15% THD idle and 1% at full depth for a -6 dBFS tone). DC-blocked.
    inline double tube(ChanState& c, double x, double gain, double grDb)
    {
        constexpr double d = 0.45;
        const double depth = 0.1 + 0.9 * std::min(1.0, std::fabs(grDb) / 12.0);
        const double y = ((double)satCurve(SatMode::Tube, (float)(d * x)) - (double)satCurve(SatMode::Tube, 0.0f)) / d;
        const double diff = (y - x) * depth * gain;
        const double out = diff - c.dcx + dcR * c.dcy;   // DC blocker on the colour only (the curve is asymmetric)
        c.dcx = diff; c.dcy = flushTiny(out);
        return x * gain + out;
    }

    void processChunk(const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR, int m)
    {
        if (!(topo == pending) && fadeState == 0) { fadeState = -1; fadePos = 0; }
        const Chan mode = (Chan)topo.chan;
        const bool ms = mode != Chan::Stereo;
        const bool ext = set.scExternal;
        const int factor = 1 << topo.os;

        // --- inputs (copied first: the host may process in place) ---
        float xl[kChunk], xr[kChunk], sl[kChunk], srr[kChunk];
        float scMax = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            xl[i] = sanitize(inL[i]); xr[i] = sanitize(inR[i]);
            sl[i] = scL ? sanitize(scL[i]) : 0.0f; srr[i] = scR ? sanitize(scR[i]) : 0.0f;
            scMax = std::max(scMax, std::max(std::fabs(sl[i]), std::fabs(srr[i])));
        }
        scPeak.setMax(scMax);

        // --- sidechain filter frequencies glide per chunk ---
        {
            const double kf = 1.0 - std::exp(-(double)m / (0.03 * sr));
            const double th = targetHpf(), tl = targetLpf();
            const double nh = std::exp(std::log(hpfHz) + (std::log(th) - std::log(hpfHz)) * kf);
            const double nl = std::exp(std::log(lpfHz) + (std::log(tl) - std::log(lpfHz)) * kf);
            hpfHz = std::fabs(nh - th) < 1e-3 ? th : nh;
            lpfHz = std::fabs(nl - tl) < 1e-2 ? tl : nl;
            updateFilters();
        }

        // --- base rate: input gain, dry, proc channels, detector source ---
        float inPk[2] = { 0.0f, 0.0f };
        const double inT = dbToGain(set.inGain), scT = dbToGain(set.scGain);
        for (int i = 0; i < m; ++i)
        {
            const double gi = inG.next(inT);
            const double l = xl[i] * gi, r = xr[i] * gi;
            inPk[0] = std::max(inPk[0], (float)std::fabs(l)); inPk[1] = std::max(inPk[1], (float)std::fabs(r));
            dry[0][i] = dryDly[0].process((float)l);
            dry[1][i] = dryDly[1].process((float)r);
            const double p0 = ms ? 0.5 * (l + r) : l, p1 = ms ? 0.5 * (l - r) : r;
            double d0 = p0, d1 = p1;
            if (ext)
            {
                const double el = sl[i], er = srr[i];
                d0 = ms ? 0.5 * (el + er) : el; d1 = ms ? 0.5 * (el - er) : er;
            }
            const double gs = scG.next(scT);
            d0 = lpf[0].lowpass(hpf[0].highpass(d0 * gs));
            d1 = lpf[1].lowpass(hpf[1].highpass(d1 * gs));
            det[0][i] = (float)d0; det[1][i] = (float)d1;
            const double ll = ms ? d0 + d1 : d0, lr = ms ? d0 - d1 : d1;
            lis[0][i] = lisDly[0].process((float)ll);
            lis[1][i] = lisDly[1].process((float)lr);
            mainB[0][i] = laDly[0].process((float)p0);
            mainB[1][i] = laDly[1].process((float)p1);
        }
        inPeak[0].setMax(inPk[0]); inPeak[1].setMax(inPk[1]);

        // --- curve parameters glide (one-pole per chunk, interpolated per sample) ---
        Curve c0 = cur, c1 = cur;
        {
            const double kc = 1.0 - std::exp(-(double)m / (0.02 * sr));
            c1.T += (curveTarget.T - cur.T) * kc;
            c1.s += (curveTarget.s - cur.s) * kc;
            c1.W += (curveTarget.W - cur.W) * kc;
            c1.range += (curveTarget.range - cur.range) * kc;
            if (std::fabs(c1.T - curveTarget.T) < 1e-4) c1.T = curveTarget.T;
            if (std::fabs(c1.s - curveTarget.s) < 1e-6) c1.s = curveTarget.s;
            if (std::fabs(c1.W - curveTarget.W) < 1e-4) c1.W = curveTarget.W;
            if (std::fabs(c1.range - curveTarget.range) < 1e-4) c1.range = curveTarget.range;
            c1.style = c0.style = curveTarget.style;
            cur = c1;
        }

        // --- oversampled detector + gain ---
        float* const* up = osMain[topo.os].upsample(mainB[0], mainB[1], m);
        float* const* ud = osDet[topo.os].upsample(det[0], det[1], m);
        const int N = m * factor;
        const int st = styleNow;
        const bool c0on = mode != Chan::Side, c1on = mode != Chan::Mid;
        const double link = (mode == Chan::Mid || mode == Chan::Side) ? 0.0 : clampv(set.link * 0.01, 0.0, 1.0);
        double grLo[2] = { 0.0, 0.0 };
        double lvlHi = -200.0;
        const bool tubeOn = st == (int)Style::VariMu;
        for (int i = 0; i < N; ++i)
        {
            const double fr = (double)(i + 1) / N;
            Curve cv = c0;
            cv.T += (c1.T - c0.T) * fr; cv.s += (c1.s - c0.s) * fr; cv.W += (c1.W - c0.W) * fr; cv.range += (c1.range - c0.range) * fr;
            double t[2] = { 0.0, 0.0 }, k[2] = { 0.0, 0.0 }, L[2] = { -200.0, -200.0 };
            for (int c = 0; c < 2; ++c)
            {
                if (!(c == 0 ? c0on : c1on)) continue;
                ChanState& s = ch[c];
                double x = ud[c][i];
                if (st == (int)Style::Punch) x *= s.fb;
                L[c] = detect(s, x, st);
                if (st == (int)Style::Punch) { t[c] = cv.feedbackTarget(L[c], k[c]); L[c] -= s.g1; }
                else t[c] = cv.gain(L[c]);
            }
            if (link > 0.0)
            {
                const double tm = std::min(t[0], t[1]), km = std::max(k[0], k[1]);
                for (int c = 0; c < 2; ++c) { t[c] += (tm - t[c]) * link; k[c] = std::max(k[c], km * link); }
            }
            for (int c = 0; c < 2; ++c)
            {
                if (!(c == 0 ? c0on : c1on)) continue;
                ChanState& s = ch[c];
                envelope(s, t[c], k[c], st);
                const double gl = std::exp(s.g * kDbToLn);
                up[c][i] = (float)(tubeOn ? tube(s, up[c][i], gl, s.g) : up[c][i] * gl);
                grLo[c] = std::min(grLo[c], s.g);
                lvlHi = std::max(lvlHi, L[c]);
            }
        }
        osMain[topo.os].downsample(mainB[0], mainB[1], m);
        grMin[0].setMin((float)grLo[0]); grMin[1].setMin((float)grLo[1]);
        detLevel.setMax((float)lvlHi);
        const float grChunk = (float)std::min(grLo[0], grLo[1]);

        // --- base rate: back to L/R, makeup, mix, listen, output gain, fades, meters ---
        const double mkT = dbToGain(set.autoMakeup ? curveTarget.makeup() : 0.0);
        makeupNow.store((float)(set.autoMakeup ? curveTarget.makeup() : 0.0), std::memory_order_relaxed);
        const double mixT = clampv(set.mix * 0.01, 0.0, 1.0), outT = dbToGain(set.outGain), lisT = set.scListen ? 1.0 : 0.0;
        float outPk[2] = { 0.0f, 0.0f };
        bool switchNow = false;
        for (int i = 0; i < m; ++i)
        {
            const double w0 = mainB[0][i], w1 = mainB[1][i];
            double wl = ms ? w0 + w1 : w0, wr = ms ? w0 - w1 : w1;
            const double mk = mkG.next(mkT);
            wl *= mk; wr *= mk;
            const double mx = mixS.next(mixT);
            double ol = dry[0][i] + (wl - dry[0][i]) * mx;
            double orr = dry[1][i] + (wr - dry[1][i]) * mx;
            const double ls = lisS.next(lisT);
            if (ls > 0.0) { ol += (lis[0][i] - ol) * ls; orr += (lis[1][i] - orr) * ls; }
            const double og = outG.next(outT);
            ol *= og; orr *= og;
            if (fadeState != 0)
            {
                double fg = 0.0;
                if (fadeState < 0) { fg = fadePos >= fadeLen ? 0.0 : 1.0 - (double)(fadePos + 1) / fadeLen; if (fadePos < fadeLen) ++fadePos; if (fadePos >= fadeLen) switchNow = true; }
                else if (fadeState == 2) { fg = 0.0; if (++fadePos >= muteLen) { fadeState = 1; fadePos = 0; } }
                else { fg = (double)(fadePos + 1) / fadeLen; if (++fadePos >= fadeLen) { fadeState = 0; } }
                fg = fg * fg * (3.0 - 2.0 * fg);   // smoothstep
                ol *= fg; orr *= fg;
            }
            outL[i] = (float)ol; outR[i] = (float)orr;
            outPk[0] = std::max(outPk[0], (float)std::fabs(ol)); outPk[1] = std::max(outPk[1], (float)std::fabs(orr));
            // history feed
            hIn = std::max(hIn, std::max(std::fabs(xl[i] * (float)inG.y), std::fabs(xr[i] * (float)inG.y)));
            hOut = std::max(hOut, (float)std::max(std::fabs(ol), std::fabs(orr)));
            hDet = std::max(hDet, std::max(std::fabs(det[0][i]), std::fabs(det[1][i])) * (ms ? 1.4142f : 1.0f));
            hGr = std::min(hGr, grChunk);
            if (++hCount >= histHop)
            {
                histIn.push((float)toDb(hIn)); histOut.push((float)toDb(hOut)); histGr.push(hGr); histDet.push((float)toDb(hDet));
                hIn = hOut = hDet = 0.0f; hGr = 0.0f; hCount = 0;
            }
        }
        outPeak[0].setMax(outPk[0]); outPeak[1].setMax(outPk[1]);
        if (switchNow && fadeState < 0)
        {
            const bool osChange = topo.os != pending.os;
            applyTopology();
            // mute until the newly selected oversampler has filled (its own latency), then fade in
            fadeState = 2; fadePos = 0; muteLen = osChange ? osMain[topo.os].latency() * 2 + 8 : 8;
        }
    }

    static inline float sanitize(float x)
    {
        if (!(std::fabs(x) < 1e6f)) return 0.0f;   // NaN, inf, absurd values
        return std::fabs(x) < 1e-30f ? 0.0f : x;
    }

    double sr = 48000.0, fsOs = 48000.0;
    bool prepared = false;
    Settings set;
    Curve curveTarget, cur;
    int styleNow = 0;
    bool autoNow = false;
    Topo topo, pending;
    Oversampler osMain[3], osDet[3];
    DelayLine laDly[2], dryDly[2], lisDly[2];
    SvfFilter hpf[2], lpf[2];
    double hpfHz = 5.0, lpfHz = 20000.0;
    OnePole inG, outG, mkG, mixS, scG, lisS;
    ChanState ch[2];
    double lnA = 0, lnR = 0, cA = 0, cR = 0, cAhalf = 0, cFastRel = 0, cSlowAtk = 0, cSlowRel = 0;
    double cOptoFast = 0, cOptoSlow = 0, cOptoCharge = 0, optoMem = 0.5, cRms5 = 0, cRms10 = 0, cRms15 = 0, cLoad = 0, dcR = 0;
    int holdLen = 0, maxLatency = 0, fadeLen = 144, fadeState = 0, fadePos = 0, muteLen = 8, histHop = 120, hCount = 0;
    float hIn = 0.0f, hOut = 0.0f, hDet = 0.0f, hGr = 0.0f;
    float dry[2][kChunk] = {}, lis[2][kChunk] = {}, det[2][kChunk] = {}, mainB[2][kChunk] = {};
};

// Settings from parameter values (plugin and tests share this mapping).
inline Settings settingsFromParams(const float* v)
{
    Settings s;
    s.style = (int)std::lround(v[kStyle]);
    s.threshold = v[kThreshold]; s.ratio = v[kRatio]; s.knee = v[kKnee]; s.range = v[kRange];
    s.attack = v[kAttack]; s.release = v[kRelease]; s.autoRelease = v[kAutoRelease] >= 0.5f; s.hold = v[kHold];
    s.lookahead = v[kLookahead]; s.mix = v[kMix]; s.inGain = v[kInGain]; s.outGain = v[kOutGain];
    s.autoMakeup = v[kAutoMakeup] >= 0.5f; s.link = v[kLink]; s.channelMode = (int)std::lround(v[kChannelMode]);
    s.scExternal = (int)std::lround(v[kScSource]) == 1; s.scGain = v[kScGain]; s.scHpf = v[kScHpf]; s.scLpf = v[kScLpf];
    s.scListen = v[kScListen] >= 0.5f; s.oversampling = (int)std::lround(v[kOversampling]);
    return s;
}

}} // namespace pump::comp
