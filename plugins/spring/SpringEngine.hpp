// pump SPRING engine: spring reverb. Framework-free.
//
// Two engines behind one set of controls: a dispersive model of a real spring tank, and an imitation of the cheap
// "spring" preset of an early digital reverb.
//
// Signal path (the tank is mono in, stereo out, like the hardware):
//   (L + R) / 2 -> pre-delay (0..250 ms, glided) -> the voice playing the current model -> low cut -> tone (a low-pass
//   from dark to bright, plus a high shelf on the bright side) -> width (M/S) -> dry/wet balance (dry stays at full up
//   to 50 %, the wet reaches full at 50 %) -> output
// Two voices: a model change starts the new model in the idle voice while the old one stops taking input and fades
// out over 150 ms (a change that arrives during that fade waits for it).
//
// Spring tanks (Two-spring, Three-spring, Short, Long): 2 or 3 springs of different lengths in parallel, each a
// waveguide loop in the spirit of the published parametric spring-reverberation structures:
//   driver     120 Hz high-pass -> drive (DWELL: -10..+22 dB; DRIP adds up to +9 dB on transients) -> asymmetric soft
//              saturation -> make-up gain. A 2nd-order low-pass at the transition frequency fc feeds the springs; the
//              part above fc (a high-pass at fc) feeds the high-frequency path.
//   a spring   two travels per round trip: forward (input transducer -> output transducer) and back. Each travel is a
//              cascade of 48 stretched first-order allpasses A(z) = (a + D(z)) / (1 + a D(z)), with
//              D(z) = z^-Kint T(z) (T a first-order Thiran allpass, so the stretch K = fs / 2fc can be fractional and
//              glide), then a delay line for the rest of the travel time. Below fc the group delay rises toward fc:
//              every echo is a rising chirp (the drip), and echo n has travelled 2n + 1 lengths, so the chirps
//              lengthen as the tail goes on. The forward tap (the output transducer) carries a small pre-echo (the
//              ripple in front of each chirp); the backward travel has the loop low-pass at fc and a 70 Hz high-pass
//              (lows and highs decay faster); the loop gain sets the T60 (DECAY). Longer springs have proportionally
//              more dispersion; the springs' fc are detuned by about 2 % so they never ring as one.
//   wobble     the delay lines move with slow random motion (WOBBLE: up to +-0.5 ms, a tank being nudged)
//   high path  above fc: a loop of 0.62 the round trip through 32 plain allpasses with a negative coefficient (the
//              weak, splashy chirps in which high frequencies arrive first), decaying 2.5x faster
//   transducer a 70 Hz high-pass and a peak in the chirp band (DRIP, up to +7 dB at fc / 2); springs panned L, R (and C)
//   TENSION    tight..loose: fc 5.3..3.6 kHz, travel times x0.86..x1.16, less..more dispersion
//   KICK       a shaped burst (a noise crack and a low thump, different every time) slammed into the springs, and a
//              jolt to the wobble that settles over a second: the tank crashes
// Digital models (Digital, Lo-fi digital): one converter clock at a crude rate (GRIT 0..100: Digital 32 -> 8 kHz and
// 16 -> 8 bits, Lo-fi 20 -> 6 kHz and 12 -> 6 bits). The input is sampled and held at that clock with no anti-
// aliasing filter (aliasing: a sine comes out with non-harmonic partners) and clips hard at full scale (DWELL); two
// short allpasses, 4 (Lo-fi: 3) short parallel combs whose loops truncate to a short word (tails grind down and stop),
// a token chirp (6 allpasses stretched to 8 kHz), a ringing flutter comb whose tap moves in whole clock periods
// (WOBBLE: zipper noise), two boxy resonances, then a held, quantised output through a crude one-pole reconstruction
// filter. TENSION scales the comb lengths, DRIP the flutter and the chirp.
//
// Everything is smoothed: per-sample ramps for mix, output and width; 16-sample control ticks with ~50 ms glides for
// the tank (coefficients, delay times, gains) and the filters. No latency.
#pragma once

#include "dsp/Fx.hpp"
#include "SpringParams.hpp"

namespace pump { namespace spring {

struct Settings {
    int model = kTwoSpring;
    double dwell = 50.0, decay = 2.5, tension = 0.0, drip = 50.0, tone = 0.0, lowCut = 100.0, preDelay = 0.0;
    double wobble = 15.0, width = 70.0, grit = 35.0, mix = 30.0, outDb = 0.0;
};

constexpr int kLanes = 4;                 // springs per tank (models use 2 or 3)
constexpr int kW = 2 * kLanes;            // dispersion lanes: forward and backward travel of every spring
constexpr int kSections = 48;             // stretched allpasses per travel
constexpr int kHfSections = 32;           // allpasses in the high-frequency loop
constexpr int kTick = 16;                 // control period (samples)
constexpr double kHfCoef = -0.55;         // the high path's allpass coefficient (high frequencies arrive first)
constexpr double kHfRatio = 0.62;         // high path loop time / spring round trip
constexpr double kMaxPreDelay = 0.25;     // s

// ---- the tanks -------------------------------------------------------------------------------------------------------
struct TankModel {
    int springs;
    double travel[kLanes];   // one-way travel time of each spring at TENSION 0 (s)
    double pan[kLanes];      // -1 left .. +1 right
};
inline const TankModel& tankModel(int m)
{
    static const TankModel t[4] = {
        { 2, { 0.0165, 0.0205, 0, 0 }, { -1.0, 1.0, 0, 0 } },                 // Two-spring
        { 3, { 0.0140, 0.0176, 0.0213, 0 }, { -1.0, 0.0, 1.0, 0 } },          // Three-spring
        { 2, { 0.0091, 0.0112, 0, 0 }, { -1.0, 1.0, 0, 0 } },                 // Short tank
        { 3, { 0.0222, 0.0268, 0.0311, 0 }, { -1.0, 0.15, 1.0, 0 } },         // Long tank
    };
    return t[clampv(m, 0, 3)];
}
static constexpr double kDetune[kLanes] = { -0.018, 0.014, -0.006, 0.022 };   // per-spring fc offsets
static constexpr double kFcMin = 3570.0, kFcMax = 5310.0;

inline double transitionHz(double tensionPct) { return 4350.0 * std::pow(1.22, clampv(tensionPct, -100.0, 100.0) * 0.01); }
inline double lengthScale(double tensionPct) { return std::pow(2.0, -0.22 * clampv(tensionPct, -100.0, 100.0) * 0.01); }
inline double baseCoef(double tensionPct, double dripPct)
{
    return 0.58 - 0.07 * clampv(tensionPct, -100.0, 100.0) * 0.01 + 0.10 * clampv(dripPct, 0.0, 100.0) * 0.01;
}
// The integer part of the stretch for a sample rate: the largest that still reaches the highest fc of any spring.
inline int stretchInt(double sr)
{
    const double wMax = kTwoPi * kFcMax * (1.0 + kDetune[3]) / sr;
    return std::max(1, (int)std::floor((kPi - 0.06) / wMax));
}
// Thiran coefficient that puts the transition (D's phase lag = pi) at w (rad/sample).
inline double thiranFor(double w, int kint)
{
    const double t = std::tan(0.5 * ((kint + 1) * w - kPi));
    return clampv(t / (std::sin(w) - t * std::cos(w)), -0.95, 0.95);
}
// Group delay (samples) of one stretched section at w.
inline double sectionDelay(double w, int kint, double eta, double a)
{
    const double c = std::cos(w), s = std::sin(w);
    const double tauT = (1.0 - eta * eta) / (1.0 + 2.0 * eta * c + eta * eta);
    const double lagT = w - 2.0 * std::atan2(eta * s, 1.0 + eta * c);
    const double phi = kint * w + lagT;
    return (kint + tauT) * (1.0 - a * a) / (1.0 + 2.0 * a * std::cos(phi) + a * a);
}
inline double hfSectionDelay(double w) { const double a = kHfCoef; return (1.0 - a * a) / (1.0 + 2.0 * a * std::cos(w) + a * a); }

// The tank at given settings (the engine runs it; the UI draws its chirps).
struct Design {
    int springs = 2, kint = 4;
    double fc = 4350.0;                                 // nominal transition (Hz)
    double travel[kLanes] = {}, pan[kLanes] = {};       // s, -1..1
    double a[kLanes] = {}, eta[kLanes] = {}, fcl[kLanes] = {};
    double chainDc[kLanes] = {};                        // the cascade's delay at DC (samples)
    // arrival of an echo's component at f (s after the input): echo n has travelled 2n + 1 lengths
    double travelDelay(int l, double f, double sr) const
    {
        const double w = kTwoPi * std::min(f, 0.999 * fcl[l]) / sr;
        return travel[l] + kSections * (sectionDelay(w, kint, eta[l], a[l]) - sectionDelay(1e-4, kint, eta[l], a[l])) / sr;
    }
    double hfLoop(int l) const { return kHfRatio * 2.0 * travel[l]; }
    double hfDelay(int l, double f, double sr) const
    {
        return hfLoop(l) + kHfSections * (hfSectionDelay(kTwoPi * std::min(f, 0.45 * sr) / sr) - hfSectionDelay(0.0)) / sr;
    }
};
inline Design design(int model, double tensionPct, double dripPct, double sr)
{
    Design d;
    const TankModel& tm = tankModel(model);
    d.springs = tm.springs;
    d.kint = stretchInt(sr);
    d.fc = transitionHz(tensionPct);
    const double ls = lengthScale(tensionPct);
    const double a0 = baseCoef(tensionPct, dripPct);
    const double disp0 = (1.0 + a0) / (1.0 - a0);
    for (int l = 0; l < kLanes; ++l)
    {
        const double tr = (l < tm.springs ? tm.travel[l] : 0.015) * ls;
        d.travel[l] = tr;
        d.pan[l] = tm.pan[l];
        // dispersion grows with the spring's length (relative to a 20 ms spring)
        const double disp = disp0 * std::pow(tr / 0.020, 0.8);
        d.a[l] = clampv((disp - 1.0) / (disp + 1.0), 0.10, 0.82);
        d.fcl[l] = d.fc * (1.0 + kDetune[l]);
        d.eta[l] = thiranFor(kTwoPi * d.fcl[l] / sr, d.kint);
        d.chainDc[l] = kSections * (d.kint + (1.0 - d.eta[l]) / (1.0 + d.eta[l])) * (1.0 - d.a[l]) / (1.0 + d.a[l]);
    }
    return d;
}

// ---- small helpers ---------------------------------------------------------------------------------------------------
inline float softDrive(float x) { return fastTanh(x + 0.2f) - 0.19737532f; }   // tanh(0.2): silence stays 0
inline double quantise(double x, double step) { return std::round(x / step) * step; }
inline double truncate(double x, double step) { return std::trunc(x / step) * step; }

// The kick: a noise crack and a low thump, a little different every time.
struct KickBurst {
    fx::Rng rng { 0x51A7u };
    double sr = 48000.0, t = -1.0, amp = 0.0, f0 = 160.0, crackT = 0.012;
    void trigger(double strength)
    {
        t = 0.0;
        amp = clampv(strength, 0.0, 1.5);
        f0 = 120.0 + 90.0 * rng.uni();
        crackT = 0.008 + 0.010 * rng.uni();
    }
    bool active() const { return t >= 0.0; }
    inline double next()
    {
        if (t < 0.0) return 0.0;
        const double att = 1.0 - std::exp(-t / 0.0004);
        const double crack = rng.bi() * att * std::exp(-t / crackT);
        const double thump = std::sin(kTwoPi * f0 * t) * std::min(1.0, t / 0.001) * std::exp(-t / 0.035);
        const double v = amp * (0.9 * crack + 0.8 * thump);
        t += 1.0 / sr;
        if (t > 0.25) t = -1.0;
        return v;
    }
};

// Control values that move with the settings (targets) and their glided values.
struct Glided {
    double cur = 0.0, target = 0.0;
    void reset(double v) { cur = target = v; }
    inline void step(double k) { cur += (target - cur) * k; }
};

// ---- the spring tank (one voice) ---------------------------------------------------------------------------------
class SpringTank {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        kint = stretchInt(sr);
        rows = 4;
        while (rows < kint + 2) rows <<= 1;
        ring.assign((size_t)(rows * kSections * kW), 0.0f);
        vprev.assign((size_t)(kSections * kW), 0.0f);
        hfState.assign((size_t)(kHfSections * kLanes), 0.0f);
        const int maxTravel = (int)(sr * 0.0311 * 1.17) + 64;
        for (auto& d : line) d.setMax(maxTravel);
        for (auto& d : hfLine) d.setMax((int)(sr * kHfRatio * 2.0 * 0.0311 * 1.17) + 64);
        glideK = 1.0 - std::exp(-kTick / (0.05 * sr));
        for (int l = 0; l < kLanes; ++l) wob[l].setup(sr / kTick, 0x2F00u + 977u * (uint32_t)l), wob[l + kLanes].setup(sr / kTick, 0x7A11u + 131u * (uint32_t)l);
        decim = std::max(1, (int)std::lround(sr / 12000.0));
        reset();
    }
    void reset()
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
        std::fill(vprev.begin(), vprev.end(), 0.0f);
        std::fill(hfState.begin(), hfState.end(), 0.0f);
        for (auto& d : line) d.clear();
        for (auto& d : hfLine) d.clear();
        for (int l = 0; l < kLanes; ++l) { loopLp[l].reset(); loopHp[l].reset(); hfDamp[l].reset(); }
        for (int c = 0; c < 2; ++c) { outHp[c].reset(); dripPk[c].reset(); }
        drvHp.reset(); feedLp.reset(); feedHp.reset();
        envF = envS = 0.0;
        wpos = 0;
        jolt = 0.0;
        primed = false;
        decimPos = 0;
    }
    // the model this voice plays (set while it is silent)
    void setModel(int m) { model = clampv(m, 0, 3); primed = false; }

    // control tick: targets from the settings
    void control(const Settings& s, double joltAdd)
    {
        const Design d = design(model, s.tension, s.drip, sr);
        const double t60 = clampv(s.decay, 0.1, 20.0);
        const double ls = lengthScale(s.tension);
        const double nSprings = (double)d.springs;
        for (int l = 0; l < kLanes; ++l)
        {
            const bool on = l < d.springs;
            const double trS = d.travel[l] * sr;
            ctl.a[l].target = d.a[l];
            ctl.eta[l].target = d.eta[l];
            ctl.fc[l].target = d.fcl[l];
            // delay lines: the travel minus the cascade (minus the loop low-pass on the way back); reads happen before
            // the write, so one sample less
            const double lpDelay = 1.41421356 / (kTwoPi * d.fcl[l]) * sr;
            ctl.dA[l].target = std::max(6.0, trS - d.chainDc[l] - 1.0);
            ctl.dB[l].target = std::max(6.0, trS - d.chainDc[l] - lpDelay - 1.0);
            ctl.ripple[l].target = (0.0011 + 0.00023 * l) * sr * ls;
            const double g = std::pow(10.0, -3.0 * 2.0 * d.travel[l] / t60);
            ctl.g[l].target = on ? g : 0.0;
            ctl.norm[l].target = on ? std::pow(1.0 - g * g, 0.25) * std::sqrt(2.0 / nSprings) : 0.0;
            const double hl = d.hfLoop(l);
            const double gh = std::pow(10.0, -3.0 * hl / (0.4 * t60));
            ctl.gh[l].target = on ? gh : 0.0;
            ctl.normH[l].target = on ? std::pow(1.0 - gh * gh, 0.25) * std::sqrt(2.0 / nSprings) : 0.0;
            ctl.dH[l].target = std::max(6.0, hl * sr - kHfSections * hfSectionDelay(0.0) - 1.0);
            ctl.in[l].target = on ? 1.0 : 0.0;
            const double p = clampv(d.pan[l], -1.0, 1.0);
            ctl.panL[l].target = on ? std::cos((p + 1.0) * 0.25 * kPi) : 0.0;
            ctl.panR[l].target = on ? std::sin((p + 1.0) * 0.25 * kPi) : 0.0;
        }
        ctl.fc0.target = d.fc;
        ctl.drive.target = dbToGain(-10.0 + 0.32 * s.dwell);
        ctl.drip.target = clampv(s.drip, 0.0, 100.0) * 0.01;
        ctl.wobble.target = clampv(s.wobble, 0.0, 100.0) * 0.01 * 0.0005 * sr;
        jolt += joltAdd;
        if (!primed) { ctl.snap(); primed = true; }
        else ctl.step(glideK);
        jolt *= std::exp(-kTick / (0.6 * sr));

        // coefficients for this tick
        for (int l = 0; l < kLanes; ++l)
        {
            a[l] = a[l + kLanes] = (float)ctl.a[l].cur;
            eta[l] = eta[l + kLanes] = (float)ctl.eta[l].cur;
            loopLp[l].set(ctl.fc[l].cur, 0.70710678, sr);
            loopHp[l].set(70.0, sr);
            hfDamp[l].set(std::min(9000.0, 0.45 * sr), sr);
        }
        const double fc0 = ctl.fc0.cur;
        feedLp.set(fc0, 0.70710678, sr);
        feedHp.set(fc0, 0.70710678, sr);
        drvHp.set(120.0, 0.70710678, sr);
        for (int c = 0; c < 2; ++c) { outHp[c].set(70.0, 0.70710678, sr); dripPk[c].set(0.5 * fc0, 0.8, sr); }
        dripGain = 1.0 + 1.25 * ctl.drip.cur;
        envFk = 1.0 - std::exp(-1.0 / (0.001 * sr)); envFr = 1.0 - std::exp(-1.0 / (0.015 * sr));
        envSk = 1.0 - std::exp(-1.0 / (0.030 * sr)); envSr = 1.0 - std::exp(-1.0 / (0.150 * sr));
        // wobble: each travel's delay drifts on its own; a kick multiplies the depth for a moment
        const double depth = ctl.wobble.cur * (1.0 + 7.0 * std::min(jolt, 1.5)) + 0.0004 * sr * std::min(jolt, 1.5);
        for (int k = 0; k < kW; ++k)
        {
            wob[k].setRate((0.35 + 0.17 * (k % kLanes)) * (1.0 + 2.0 * std::min(jolt, 1.0)));
            wobFrom[k] = wobTo[k];
            wobTo[k] = depth * wob[k].next();
        }
    }

    // one sample: driver input x (after pre-delay), kick burst sample; adds the panned output to oL / oR
    inline void process(double x, double kick, double inGain, double& oL, double& oR, double frac, bool feed)
    {
        // driver: high-pass, transient boost (drip), drive, soft saturation, make-up
        const double h = drvHp.highpass(x);
        const double ah = std::fabs(h);
        envF += (ah - envF) * (ah > envF ? envFk : envFr);
        envS += (ah - envS) * (ah > envS ? envSk : envSr);
        const double tr = clampv(envF / (envS + 1e-5) - 1.0, 0.0, 2.0);
        const double drv = ctl.drive.cur;
        const double boost = 1.0 + 0.9 * ctl.drip.cur * tr;
        const double d = (double)softDrive((float)(h * drv * boost)) * std::pow(drv, -0.55) * inGain;
        double lfIn, bp, hfIn;
        feedLp.process(d, lfIn, bp, hfIn);
        hfIn = feedHp.highpass(d);
        lfIn += kick;

        // the springs: read the travels (before writing), run the cascades, write back
        alignas(16) float xs[kW];
        float tap[kLanes];
        for (int l = 0; l < kLanes; ++l)
        {
            const double dA = ctl.dA[l].cur + wobFrom[l] + (wobTo[l] - wobFrom[l]) * frac;
            const double dB = ctl.dB[l].cur + wobFrom[l + kLanes] + (wobTo[l + kLanes] - wobFrom[l + kLanes]) * frac;
            const double rp = std::max(1.0, dA - ctl.ripple[l].cur);
            const float t = (line[l].read(dA) + kRipple * line[l].read(rp)) * (float)(1.0 / (1.0 + kRipple));
            tap[l] = t;
            const float back = line[l + kLanes].read(dB);
            xs[l] = (float)(lfIn * ctl.in[l].cur * kickSign(l, kick) + ctl.g[l].cur * back);
            xs[l + kLanes] = t;
        }
        if (feed && ++decimPos >= decim)
        {
            decimPos = 0;
            for (int l = 0; l < kLanes; ++l) { fwdRing[l].push(xs[l]); bwdRing[l].push(xs[l + kLanes]); }
        }
        disperse(xs);
        for (int l = 0; l < kLanes; ++l)
        {
            line[l].push(xs[l]);
            const double y = loopHp[l].hp(loopLp[l].lowpass(xs[l + kLanes]));
            line[l + kLanes].push((float)y);
        }

        // the high path
        alignas(16) float hx[kLanes];
        float htap[kLanes];
        for (int l = 0; l < kLanes; ++l)
        {
            htap[l] = hfLine[l].read(ctl.dH[l].cur);
            hx[l] = (float)((hfIn + 0.5 * kick) * ctl.in[l].cur + ctl.gh[l].cur * htap[l]);
        }
        float* st = hfState.data();
        const float ha = (float)kHfCoef;
        for (int m = 0; m < kHfSections; ++m, st += kLanes)
            for (int l = 0; l < kLanes; ++l)
            {
                const float w = hx[l] - ha * st[l];
                hx[l] = ha * w + st[l];
                st[l] = w;
            }
        for (int l = 0; l < kLanes; ++l) hfLine[l].push((float)hfDamp[l].lp(hx[l]));

        // output transducer: the springs panned, 70 Hz high-pass, the chirp-band peak
        double L = 0.0, R = 0.0;
        for (int l = 0; l < kLanes; ++l)
        {
            const double o = ctl.norm[l].cur * tap[l] + kHfLevel * ctl.normH[l].cur * htap[l];
            L += ctl.panL[l].cur * o;
            R += ctl.panR[l].cur * o;
            if (feed) level[l] = std::max(level[l], (float)std::fabs(o));
        }
        oL += kTankGain * dripPk[0].peak(outHp[0].highpass(L), dripGain);
        oR += kTankGain * dripPk[1].peak(outHp[1].highpass(R), dripGain);
        if (feed) drvLevel = std::max(drvLevel, (float)std::fabs(d));
    }

    // a kick hits each spring with its own sign and strength
    void kickShape(fx::Rng& rng) { for (int l = 0; l < kLanes; ++l) kickG[l] = (rng.uni() < 0.5 ? -1.0f : 1.0f) * (float)(0.7 + 0.3 * rng.uni()); }
    double finiteCheck() const
    {
        double s = envF + envS;
        for (int l = 0; l < kW; ++l) s += line[l].at(1);
        for (int l = 0; l < kLanes; ++l) s += hfLine[l].at(1) + loopLp[l].ic1 + loopLp[l].ic2 + hfState[(size_t)l];
        return s;
    }
    int springs() const { return tankModel(model).springs; }

    // UI feeds (decimated to ~12 kHz): the wave entering each spring's forward and backward travel, and levels
    pump::ScopeRing fwdRing[kLanes] = { pump::ScopeRing(4096), pump::ScopeRing(4096), pump::ScopeRing(4096), pump::ScopeRing(4096) };
    pump::ScopeRing bwdRing[kLanes] = { pump::ScopeRing(4096), pump::ScopeRing(4096), pump::ScopeRing(4096), pump::ScopeRing(4096) };
    float level[kLanes] = {}, drvLevel = 0.0f;
    int decim = 4;

private:
    static constexpr float kRipple = 0.16f;
    static constexpr double kHfLevel = 0.35, kTankGain = 0.55;

    inline double kickSign(int l, double kick) const { return kick != 0.0 ? 1.0 : 1.0; }

    // one sample through all 8 cascades (contiguous float state: ring rows [row][section][lane])
    inline void disperse(float* x)
    {
        const int stride = kSections * kW;
        float* __restrict rw = ring.data() + (size_t)(wpos * stride);
        const float* __restrict ra = ring.data() + (size_t)(((wpos - kint) & (rows - 1)) * stride);
        const float* __restrict rb = ring.data() + (size_t)(((wpos - kint - 1) & (rows - 1)) * stride);
        float* __restrict vp = vprev.data();
        float xv[kW], av[kW], ev[kW];
        for (int j = 0; j < kW; ++j) { xv[j] = x[j]; av[j] = a[j]; ev[j] = eta[j]; }
        for (int m = 0; m < kSections; ++m)
        {
            for (int j = 0; j < kW; ++j)
            {
                const float v = ev[j] * (ra[j] - vp[j]) + rb[j];
                vp[j] = v;
                const float w = xv[j] - av[j] * v;
                rw[j] = w;
                xv[j] = av[j] * w + v;
            }
            rw += kW; ra += kW; rb += kW; vp += kW;
        }
        for (int j = 0; j < kW; ++j) x[j] = xv[j];
        wpos = (wpos + 1) & (rows - 1);
    }

    struct Ctl {
        Glided a[kLanes], eta[kLanes], fc[kLanes], dA[kLanes], dB[kLanes], dH[kLanes], ripple[kLanes];
        Glided g[kLanes], gh[kLanes], norm[kLanes], normH[kLanes], in[kLanes], panL[kLanes], panR[kLanes];
        Glided fc0, drive, drip, wobble;
        template <class F> void each(F f)
        {
            for (int l = 0; l < kLanes; ++l)
                for (Glided* p : { &a[l], &eta[l], &fc[l], &dA[l], &dB[l], &dH[l], &ripple[l], &g[l], &gh[l], &norm[l], &normH[l], &in[l], &panL[l], &panR[l] }) f(*p);
            f(fc0); f(drive); f(drip); f(wobble);
        }
        void snap() { each([](Glided& v) { v.cur = v.target; }); }
        void step(double k) { each([k](Glided& v) { v.step(k); }); }
    } ctl;

    double sr = 48000.0, glideK = 0.01, jolt = 0.0, dripGain = 1.0;
    double envF = 0.0, envS = 0.0, envFk = 0.1, envFr = 0.01, envSk = 0.01, envSr = 0.001;
    int model = kTwoSpring, kint = 4, rows = 8, wpos = 0, decimPos = 0;
    bool primed = false;
    std::vector<float> ring, vprev, hfState;
    float a[kW] = {}, eta[kW] = {}, kickG[kLanes] = { 1, 1, 1, 1 };
    fx::FracDelay line[kW], hfLine[kLanes];
    fx::Svf loopLp[kLanes], drvHp, feedLp, feedHp, outHp[2], dripPk[2];
    fx::OnePole loopHp[kLanes], hfDamp[kLanes];
    fx::Wander wob[kW];
    double wobFrom[kW] = {}, wobTo[kW] = {};
};

}} // namespace pump::spring
