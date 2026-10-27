// pump SPRING engine: spring reverb. Framework-free.
//
// Two engines behind one set of controls: a dispersive model of a real spring tank, and an imitation of the cheap
// "spring" preset of an early digital reverb.
//
// Signal path (the tank is mono in, stereo out, like the hardware):
//   (L + R) / 2 -> pre-delay (0..250 ms, glided) -> the voice playing the current model -> low cut (12 dB/oct) ->
//   tone (a low-pass: 2 kHz dark, 9 kHz in the middle, 20 kHz bright, plus up to +6 dB of high shelf above 2.5 kHz on
//   the bright side) -> width (M/S) -> mix (the dry stays at full level up to 50 %, the wet reaches full level at
//   50 %) -> output
// Two voices: a model change starts the new model in the idle voice while the old one stops taking input (5 ms) and
// rings out under a 150 ms fade (a change that arrives during that fade waits for it).
//
// Spring tanks (Two-spring, Three-spring, Short, Long): 2 or 3 springs of different lengths in parallel, each a
// waveguide loop in the spirit of the published parametric spring-reverberation structures:
//   driver     120 Hz high-pass -> drive (DWELL: -10..+22 dB; DRIP adds up to +9 dB on transients) -> asymmetric soft
//              saturation -> make-up gain. A low-pass at the transition frequency fc feeds the springs; the part above
//              fc (a high-pass at fc) feeds the high-frequency path.
//   a spring   two travels per round trip: forward (input transducer -> output transducer) and back. Each travel is a
//              cascade of 80 stretched first-order allpasses A(z) = (a + D(z)) / (1 + a D(z)), D(z) = z^-K T(z) (T a
//              first-order Thiran allpass, so the transition can sit anywhere and glide; K is fixed per sample rate),
//              then a delay line for the rest of the travel time. Below fc the group delay rises toward fc: every echo
//              is a rising chirp (the drip), and echo n has travelled 2n + 1 lengths, so the chirps lengthen as the
//              tail goes on. The output transducer also hears each wave ~1.1 ms early, quietly (the pre-echo ripple
//              ahead of each chirp); the way back has the loop low-pass at fc (the highest chirps decay faster), a
//              high shelf above fc / 2 that DECAY sets (the chirping band's round trips are longer: it would outlast
//              DECAY) and a 20 Hz high-pass (DC); the loop gain sets the T60 (DECAY). Longer springs get a higher allpass
//              coefficient (longer chirps near fc); the springs' fc are detuned by about 2 %.
//              The 8 cascades (forward and back of up to 4 springs) run side by side in two 4-float vectors, their
//              state contiguous in one ring (rows x sections x lanes).
//   wobble     each travel's delay drifts with slow random motion (WOBBLE: up to +-1.2 ms, a tank being nudged)
//   high path  above fc: a loop of 0.62 x the round trip through 32 plain allpasses with a negative coefficient (the
//              weak, splashy chirps in which high frequencies arrive first), decaying 2.5x faster
//   transducer 70 Hz high-pass and a peak in the chirp band (DRIP: up to +7 dB at fc / 2); springs panned L, R (and C)
//   TENSION    tight..loose: fc 5.3..3.6 kHz, travel times x0.86..x1.16, less..more dispersion
//   KICK       a shaped burst (a noise crack and a low thump, different every time, hitting each spring with its own
//              sign) into the springs, and a jolt (up to ~0.9 ms of extra, faster wobble) that settles over a second
// Digital models (Digital, Lo-fi digital): everything runs at one crude converter clock (GRIT 0..100: Digital 32 ->
// 8 kHz and 16 -> 8 bits, Lo-fi 20 -> 6 kHz and 12 -> 6 bits). The input is sampled at that clock with no anti-
// aliasing filter (aliasing: a sine comes out with non-harmonic partners) and clips hard at full scale (DWELL drives
// it); two short allpasses, a token chirp (6 allpasses on a 2-tick delay: a few short chirps at a quarter of the
// clock), 4 (Lo-fi: 3) short parallel combs whose loops truncate toward zero (8 bits finer than the converters at
// GRIT 0, 3 at 100: the tails grind down and stop), a ringing flutter comb whose tap moves in whole clock periods
// (WOBBLE: zipper noise) and two boxy resonances, then a quantised output held between clock ticks through a crude
// one-pole reconstruction filter. TENSION scales the combs, DRIP the flutter and the chirp; the clock glides (sweeping
// GRIT bends the tail's pitch instead of clicking).
//
// Everything is smoothed: per-sample ramps for mix, output, width and the voice fades; control ticks every 16 samples
// (whatever the block size) with ~50 ms glides for the tank (coefficients, delay times, gains), the converter clock
// and the filters. No latency.
#pragma once

#include "dsp/Fx.hpp"
#include "SpringParams.hpp"

namespace pump { namespace spring {

// four floats in one SIMD register (GCC / Clang vector extension: SSE on x86, NEON on ARM)
typedef float f4 __attribute__((vector_size(16)));

struct Settings {
    int model = kTwoSpring;
    double dwell = 50.0, decay = 2.5, tension = 0.0, drip = 50.0, tone = 0.0, lowCut = 100.0, preDelay = 0.0;
    double wobble = 15.0, width = 70.0, grit = 35.0, mix = 30.0, outDb = 0.0;
};

constexpr int kLanes = 4;                 // springs per tank (models use 2 or 3)
constexpr int kSections = 80;             // stretched allpasses per travel
constexpr int kHfSections = 32;           // allpasses in the high-frequency loop
constexpr int kTick = 16;                 // control period (samples)
constexpr double kHfCoef = -0.55;         // the high path's allpass coefficient (high frequencies arrive first)
constexpr double kHfRatio = 0.62;         // high path loop time / spring round trip
constexpr double kMaxPreDelay = 0.25;     // s
constexpr double kMaxStretch = 1.17;      // the longest travel scaling TENSION reaches (x2^0.22), with margin
constexpr double kMaxWobble = 0.0012;     // WOBBLE 100 %: +-1.2 ms on every travel
constexpr double kJoltWobble = 0.0006;    // a kick's extra wobble (per unit of strength)
constexpr double kDecayScale = 1.08;      // the loop low-pass and the chirps take a little energy each trip: made up
constexpr double kMaxLoopGain = 0.9995;

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
constexpr double kLongestTravel = 0.0311;
constexpr double kDetune[kLanes] = { -0.018, 0.014, -0.006, 0.022 };   // per-spring fc offsets
constexpr double kFcMax = 5310.0;                                      // fc at TENSION +100

inline double transitionHz(double tensionPct) { return 4350.0 * std::pow(1.22, clampv(tensionPct, -100.0, 100.0) * 0.01); }
inline double lengthScale(double tensionPct) { return std::pow(2.0, -0.22 * clampv(tensionPct, -100.0, 100.0) * 0.01); }
inline double baseCoef(double tensionPct, double dripPct)
{
    return 0.54 - 0.07 * clampv(tensionPct, -100.0, 100.0) * 0.01 + 0.10 * clampv(dripPct, 0.0, 100.0) * 0.01;
}
// The integer part of the stretch at a sample rate: the largest that still reaches the highest fc of any spring.
inline int stretchInt(double sr)
{
    const double wMax = kTwoPi * kFcMax * (1.0 + kDetune[3]) / sr;
    return std::max(1, (int)std::floor((kPi - 0.06) / wMax));
}
// Thiran coefficient that puts the transition (where D's phase lag reaches pi) at w (rad/sample).
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
    // when the component at f of the first echo arrives (s after the input; echo n arrives at (2n + 1) x this)
    double travelDelay(int l, double f, double sr) const
    {
        const double w = kTwoPi * std::min(f, 0.999 * fcl[l]) / sr;
        return travel[l] + kSections * (sectionDelay(w, kint, eta[l], a[l]) - sectionDelay(1e-4, kint, eta[l], a[l])) / sr;
    }
    double hfLoop(int l) const { return kHfRatio * 2.0 * travel[l]; }
    // the high path: echo n (from 1) of the component at f arrives at n x this
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
        // the peak dispersion (1 + a) / (1 - a) grows with the spring's length (relative to a 20 ms spring)
        const double disp = disp0 * std::pow(tr / 0.020, 0.8);
        d.a[l] = clampv((disp - 1.0) / (disp + 1.0), 0.10, 0.82);
        d.fcl[l] = d.fc * (1.0 + kDetune[l]);
        d.eta[l] = thiranFor(kTwoPi * d.fcl[l] / sr, d.kint);
        d.chainDc[l] = kSections * (d.kint + (1.0 - d.eta[l]) / (1.0 + d.eta[l])) * (1.0 - d.a[l]) / (1.0 + d.a[l]);
    }
    return d;
}

// ---- the digital "spring" ------------------------------------------------------------------------------------------
struct DigitalModel {
    int combs;
    double comb[4];          // comb loop times at TENSION 0 (s)
    double ap[2];            // the two input allpasses (s)
    double flutter;          // flutter comb (s)
    double res[2];           // boxy resonances (Hz)
    double rate0, rate1;     // converter clock at GRIT 0 and 100 (Hz)
    double bits0, bits1;     // word length at GRIT 0 and 100
};
inline const DigitalModel& digitalModel(int m)
{
    static const DigitalModel d[2] = {
        { 4, { 0.0131, 0.0173, 0.0217, 0.0269 }, { 0.0041, 0.00137 }, 0.0063, { 420.0, 1500.0 }, 32000.0, 8000.0, 16.0, 8.0 },
        { 3, { 0.0119, 0.0161, 0.0233, 0.0 }, { 0.0033, 0.0011 }, 0.0049, { 560.0, 1900.0 }, 20000.0, 6000.0, 12.0, 6.0 },
    };
    return d[m == kLofi ? 1 : 0];
}
inline double clockHz(int m, double gritPct)
{
    const DigitalModel& d = digitalModel(m);
    return d.rate0 * std::pow(d.rate1 / d.rate0, clampv(gritPct, 0.0, 100.0) * 0.01);
}
inline double wordBits(int m, double gritPct)
{
    const DigitalModel& d = digitalModel(m);
    return d.bits0 + (d.bits1 - d.bits0) * clampv(gritPct, 0.0, 100.0) * 0.01;
}
inline double digitalScale(double tensionPct) { return std::pow(2.0, -0.18 * clampv(tensionPct, -100.0, 100.0) * 0.01); }
constexpr double kDigitalMaxClock = 32000.0;

// ---- small helpers ---------------------------------------------------------------------------------------------------
inline double quantise(double x, double step) { return std::round(x / step) * step; }
inline double truncate(double x, double step) { return std::trunc(x / step) * step; }
inline f4 splat(float v) { return f4 { v, v, v, v }; }

// A control value: the target set from the settings, the glided value used.
struct Glided {
    double cur = 0.0, target = 0.0;
    inline void step(double k) { cur += (target - cur) * k; }
};

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
    void reset() { t = -1.0; }
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

// ---- a spring tank (one voice) ---------------------------------------------------------------------------------------
class SpringTank {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        kint = stretchInt(sr);
        rows = 4;
        while (rows < kint + 2) rows <<= 1;
        ring.assign((size_t)(rows * kSections * 2), splat(0.0f));
        vprev.assign((size_t)(kSections * 2), splat(0.0f));
        const double longest = kLongestTravel * kMaxStretch;
        for (auto& d : line) d.setMax((int)(sr * (longest + kMaxWobble + 1.5 * kJoltWobble)) + 64);
        for (auto& d : hfLine) d.setMax((int)(sr * kHfRatio * 2.0 * longest) + 64);
        glideK = 1.0 - std::exp(-kTick / (0.05 * sr));
        joltDecay = std::exp(-kTick / (0.6 * sr));
        for (int k = 0; k < 2 * kLanes; ++k) wob[k].setup(sr / kTick, 0x2F00u + 977u * (uint32_t)k);
        for (int l = 0; l < kLanes; ++l) { loopHp[l].set(20.0, sr); hfDamp[l].set(std::min(16000.0, 0.45 * sr), sr); }
        drvHp.set(120.0, 0.70710678, sr);
        for (int c = 0; c < 2; ++c) outHp[c].set(70.0, 0.70710678, sr);
        envFk = 1.0 - std::exp(-1.0 / (0.001 * sr)); envFr = 1.0 - std::exp(-1.0 / (0.015 * sr));
        envSk = 1.0 - std::exp(-1.0 / (0.030 * sr)); envSr = 1.0 - std::exp(-1.0 / (0.150 * sr));
        reset();
    }
    void reset()
    {
        std::fill(ring.begin(), ring.end(), splat(0.0f));
        std::fill(vprev.begin(), vprev.end(), splat(0.0f));
        for (auto& s : hfState) s = splat(0.0f);
        for (auto& d : line) d.clear();
        for (auto& d : hfLine) d.clear();
        for (int l = 0; l < kLanes; ++l) { loopLp[l].reset(); loopHp[l].reset(); loopShelf[l].reset(); hfDamp[l].reset(); }
        for (int c = 0; c < 2; ++c) { outHp[c].reset(); dripPk[c].reset(); }
        drvHp.reset(); feedLp.reset(); feedHp.reset();
        envF = envS = 0.0;
        wpos = 0;
        jolt = 0.0;
        for (int k = 0; k < 2 * kLanes; ++k) wobFrom[k] = wobTo[k] = 0.0;
        for (auto& v : vis) v = 0.0f;
        for (auto& v : laneOut) v = 0.0f;
        primed = false;
    }
    void setModel(int m) { model = clampv(m, 0, 3); springs = tankModel(model).springs; primed = false; }

    // new targets (when the settings change)
    void setTargets(const Settings& s)
    {
        const Design d = design(model, s.tension, s.drip, sr);
        const double t60 = clampv(s.decay, 0.1, 20.0);
        const double ls = lengthScale(s.tension);
        const double norm = std::sqrt(2.0 / d.springs);
        for (int l = 0; l < kLanes; ++l)
        {
            const bool on = l < d.springs;
            const double trS = d.travel[l] * sr;
            ctl.a[l].target = d.a[l];
            ctl.eta[l].target = d.eta[l];
            ctl.fc[l].target = d.fcl[l];
            // the delay lines make up the rest of each travel (the way back also has the loop low-pass's delay);
            // they're read before the write, so one sample less
            const double lpDelay = 1.41421356 / (kTwoPi * d.fcl[l]) * sr;
            ctl.dA[l].target = std::max(6.0, trS - d.chainDc[l] - 1.0);
            ctl.dB[l].target = std::max(6.0, trS - d.chainDc[l] - lpDelay - 1.0);
            ctl.ripple[l].target = (0.0011 + 0.00023 * l) * sr * ls;
            // the loop gain for the T60 at the frequencies that carry the tail (a round trip there is ~2 x the
            // travel); the shortest settings run a little long because the chirps near fc decay faster than the rest
            const double g = std::min(kMaxLoopGain, std::pow(10.0, -3.0 * 2.0 * d.travel[l] / (t60 * kDecayScale)));
            // the chirping band takes longer per round trip, so at the same loop gain it would ring on past DECAY: a
            // high shelf in the loop (above fc / 2) takes it down by what its extra delay costs at this T60
            const double extra = 2.0 * (d.travelDelay(l, 0.7 * d.fcl[l], sr) - d.travel[l]);
            ctl.shelf[l].target = clampv(std::pow(10.0, -3.0 * extra / t60), 0.12, 1.0);
            ctl.g[l].target = on ? g : 0.0;
            ctl.norm[l].target = on ? std::pow(1.0 - g * g, 0.25) * norm : 0.0;
            const double hl = d.hfLoop(l);
            const double gh = std::pow(10.0, -3.0 * hl / (0.4 * t60));
            ctl.gh[l].target = on ? gh : 0.0;
            ctl.normH[l].target = on ? std::pow(1.0 - gh * gh, 0.25) * norm : 0.0;
            ctl.dH[l].target = std::max(6.0, hl * sr - kHfSections * hfSectionDelay(0.0) - 1.0);
            const double p = clampv(d.pan[l], -1.0, 1.0);
            ctl.panL[l].target = on ? std::cos((p + 1.0) * 0.25 * kPi) : 0.0;
            ctl.panR[l].target = on ? std::sin((p + 1.0) * 0.25 * kPi) : 0.0;
        }
        ctl.fc0.target = d.fc;
        const double drv = dbToGain(-10.0 + 0.32 * clampv(s.dwell, 0.0, 100.0));
        ctl.drive.target = drv;
        ctl.makeup.target = std::pow(drv, -0.7);
        ctl.drip.target = clampv(s.drip, 0.0, 100.0) * 0.01;
        ctl.wobble.target = clampv(s.wobble, 0.0, 100.0) * 0.01 * kMaxWobble * sr;
        if (!primed) { ctl.snap(); primed = true; designFilters(); }
    }

    // every kTick samples: glide, coefficients, wobble
    void tick()
    {
        if (ctl.step(glideK)) designFilters();
        float av[2 * kLanes], ev[2 * kLanes];
        for (int l = 0; l < kLanes; ++l)
        {
            av[l] = av[l + kLanes] = (float)ctl.a[l].cur;
            ev[l] = ev[l + kLanes] = (float)ctl.eta[l].cur;
        }
        for (int h = 0; h < 2; ++h)
        {
            a4[h] = f4 { av[4 * h], av[4 * h + 1], av[4 * h + 2], av[4 * h + 3] };
            e4[h] = f4 { ev[4 * h], ev[4 * h + 1], ev[4 * h + 2], ev[4 * h + 3] };
        }
        // wobble: every travel's delay drifts on its own; a kick adds depth and speed for a moment
        const double j = std::min(jolt, 1.5);
        const double depth = ctl.wobble.cur + kJoltWobble * sr * j;
        for (int k = 0; k < 2 * kLanes; ++k)
        {
            wob[k].setRate((0.35 + 0.17 * (k % kLanes)) * (1.0 + 2.0 * std::min(j, 1.0)));
            wobFrom[k] = wobTo[k];
            wobTo[k] = depth * wob[k].next();
        }
        jolt *= joltDecay;
    }

    // a kick: each spring is hit with its own sign and strength, and the tank shakes
    void kicked(fx::Rng& rng, double strength)
    {
        for (int l = 0; l < kLanes; ++l) kickG[l] = (rng.uni() < 0.5 ? -1.0 : 1.0) * (0.7 + 0.3 * rng.uni());
        jolt += strength;
    }

    // one sample: x = driver input, kick = burst sample, frac = position in the control tick (0..1)
    inline void process(double x, double kick, double frac, double& oL, double& oR)
    {
        // driver: high-pass, transient boost (drip), drive, soft saturation, make-up
        const double h = drvHp.highpass(x);
        const double ah = std::fabs(h);
        envF += (ah - envF) * (ah > envF ? envFk : envFr);
        envS += (ah - envS) * (ah > envS ? envSk : envSr);
        const double tr = clampv(envF / (envS + 1e-5) - 1.0, 0.0, 2.0);
        const double drv = ctl.drive.cur * (1.0 + 0.9 * ctl.drip.cur * tr);
        const double d = ((double)fastTanh((float)(h * drv) + 0.2f) - kDrvBias) * ctl.makeup.cur;
        const double lfIn = feedLp.lowpass(d);
        const double hfIn = feedHp.highpass(d) + 0.5 * kick;
        drvOut = (float)d;

        // the springs: read the travels (before writing), run the cascades, write back
        alignas(16) float xs[2 * kLanes] = {};
        float tap[kLanes] = {};
        for (int l = 0; l < springs; ++l)
        {
            const double dA = ctl.dA[l].cur + wobFrom[l] + (wobTo[l] - wobFrom[l]) * frac;
            const double dB = ctl.dB[l].cur + wobFrom[l + kLanes] + (wobTo[l + kLanes] - wobFrom[l + kLanes]) * frac;
            const float t = line[l].read(dA);
            // the output transducer also hears the wave a little before it arrives (the pre-echo ripple); the loop
            // carries the wave itself
            tap[l] = (t + kRipple * line[l].readLinear(std::max(1.0, dA - ctl.ripple[l].cur))) * kTapNorm;
            xs[l] = (float)((lfIn + kick * kickG[l]) + ctl.g[l].cur * line[l + kLanes].read(dB));
            xs[l + kLanes] = t;
        }
        for (int k = 0; k < 2 * kLanes; ++k) vis[k] = xs[k];
        f4 x0, x1;
        std::memcpy(&x0, xs, sizeof x0);
        std::memcpy(&x1, xs + kLanes, sizeof x1);
        disperse(x0, x1);
        std::memcpy(xs, &x0, sizeof x0);
        std::memcpy(xs + kLanes, &x1, sizeof x1);
        for (int l = 0; l < springs; ++l)
        {
            line[l].push(xs[l]);
            const double y = loopLp[l].lowpass(xs[l + kLanes]);
            const double lo = loopShelf[l].lp(y);
            line[l + kLanes].push((float)loopHp[l].hp(lo + ctl.shelf[l].cur * (y - lo)));
        }

        // the high path
        alignas(16) float hx[kLanes] = {}, htap[kLanes] = {};
        for (int l = 0; l < springs; ++l)
        {
            htap[l] = hfLine[l].readLinear(ctl.dH[l].cur);
            hx[l] = (float)(hfIn + ctl.gh[l].cur * htap[l]);
        }
        f4 hv;
        std::memcpy(&hv, hx, sizeof hv);
        const f4 ha = splat((float)kHfCoef);
        for (int m = 0; m < kHfSections; ++m)
        {
            const f4 w = hv - ha * hfState[m];
            hv = ha * w + hfState[m];
            hfState[m] = w;
        }
        std::memcpy(hx, &hv, sizeof hv);
        for (int l = 0; l < springs; ++l) hfLine[l].push((float)hfDamp[l].lp(hx[l]));

        // output transducer: the springs panned, 70 Hz high-pass, the chirp-band peak
        double L = 0.0, R = 0.0;
        for (int l = 0; l < springs; ++l)
        {
            const double o = ctl.norm[l].cur * tap[l] + kHfLevel * ctl.normH[l].cur * htap[l];
            L += ctl.panL[l].cur * o;
            R += ctl.panR[l].cur * o;
            laneOut[l] = (float)o;
        }
        oL += kTankGain * dripPk[0].peak(outHp[0].highpass(L), dripGain);
        oR += kTankGain * dripPk[1].peak(outHp[1].highpass(R), dripGain);
    }

    double finiteCheck() const
    {
        double s = envF + envS + outHp[0].ic1 + outHp[1].ic1 + dripPk[0].ic2 + dripPk[1].ic2;
        for (int l = 0; l < kLanes; ++l) s += line[l].at(0) + line[l + kLanes].at(0) + hfLine[l].at(0) + loopLp[l].ic1 + loopLp[l].ic2;
        const f4 v = vprev[0] + vprev[1];
        for (int k = 0; k < 4; ++k) s += v[k];
        return s;
    }

    // what the UI draws: the wave entering each spring at both ends (forward lanes, then backward lanes), each
    // spring's output, the driver's output
    float vis[2 * kLanes] = {}, laneOut[kLanes] = {}, drvOut = 0.0f;

private:
    static constexpr float kRipple = 0.16f, kTapNorm = 1.0f / 1.16f;
    static constexpr double kHfLevel = 0.35, kTankGain = 0.55;
    // the asymmetric drive's resting output, taken off again so silence stays exactly 0
    const double kDrvBias = (double)fastTanh(0.2f);

    void designFilters()
    {
        for (int l = 0; l < kLanes; ++l) { loopLp[l].set(ctl.fc[l].cur, 0.70710678, sr); loopShelf[l].set(0.5 * ctl.fc[l].cur, sr); }
        const double fc0 = ctl.fc0.cur;
        feedLp.set(fc0, 0.70710678, sr);
        feedHp.set(fc0, 0.70710678, sr);
        for (int c = 0; c < 2; ++c) dripPk[c].set(0.5 * fc0, 0.8, sr);
        dripGain = 1.0 + 1.25 * ctl.drip.cur;
    }

    // one sample through all 8 cascades: x0 = the forward travels, x1 = the backward ones. The ring holds every
    // section's w for the last `rows` samples ([row][section][2]); D(z) = z^-K T(z) reads rows K and K + 1 back.
    inline void disperse(f4& x0, f4& x1)
    {
        const int stride = kSections * 2;
        f4* __restrict rw = ring.data() + (size_t)(wpos * stride);
        const f4* __restrict ra = ring.data() + (size_t)(((wpos - kint) & (rows - 1)) * stride);
        const f4* __restrict rb = ring.data() + (size_t)(((wpos - kint - 1) & (rows - 1)) * stride);
        f4* __restrict vp = vprev.data();
        const f4 a0 = a4[0], a1 = a4[1], e0 = e4[0], e1 = e4[1];
        for (int m = 0; m < kSections; ++m)
        {
            const f4 v0 = e0 * (ra[0] - vp[0]) + rb[0], v1 = e1 * (ra[1] - vp[1]) + rb[1];
            vp[0] = v0; vp[1] = v1;
            const f4 w0 = x0 - a0 * v0, w1 = x1 - a1 * v1;
            rw[0] = w0; rw[1] = w1;
            x0 = a0 * w0 + v0; x1 = a1 * w1 + v1;
            rw += 2; ra += 2; rb += 2; vp += 2;
        }
        wpos = (wpos + 1) & (rows - 1);
    }

    struct Ctl {
        Glided a[kLanes], eta[kLanes], fc[kLanes], dA[kLanes], dB[kLanes], dH[kLanes], ripple[kLanes];
        Glided g[kLanes], gh[kLanes], norm[kLanes], normH[kLanes], panL[kLanes], panR[kLanes], shelf[kLanes];
        Glided fc0, drive, makeup, drip, wobble;
        template <class F> void each(F f)
        {
            for (int l = 0; l < kLanes; ++l)
            {
                f(a[l]); f(eta[l]); f(fc[l]); f(dA[l]); f(dB[l]); f(dH[l]); f(ripple[l]);
                f(g[l]); f(gh[l]); f(norm[l]); f(normH[l]); f(panL[l]); f(panR[l]); f(shelf[l]);
            }
            f(fc0); f(drive); f(makeup); f(drip); f(wobble);
        }
        void snap() { each([](Glided& v) { v.cur = v.target; }); }
        // glides everything; true while a filter frequency or the drip still moves (the filters need redesigning)
        bool step(double k)
        {
            double m = std::fabs(fc0.target - fc0.cur) / fc0.target + std::fabs(drip.target - drip.cur);
            for (int l = 0; l < kLanes; ++l) m += std::fabs(fc[l].target - fc[l].cur) / std::max(1.0, fc[l].target);
            each([k](Glided& v) { v.step(k); });
            return m > 1e-7;
        }
    } ctl;

    double sr = 48000.0, glideK = 0.01, jolt = 0.0, joltDecay = 0.99, dripGain = 1.0;
    double envF = 0.0, envS = 0.0, envFk = 0.1, envFr = 0.01, envSk = 0.01, envSr = 0.001;
    int model = kTwoSpring, springs = 2, kint = 4, rows = 8, wpos = 0;
    bool primed = false;
    std::vector<f4> ring, vprev;
    f4 hfState[kHfSections] = {};
    f4 a4[2] = {}, e4[2] = {};
    double kickG[kLanes] = { 1, -1, 1, -1 };
    fx::FracDelay line[2 * kLanes], hfLine[kLanes];
    fx::Svf loopLp[kLanes], drvHp, feedLp, feedHp, outHp[2], dripPk[2];
    fx::OnePole loopHp[kLanes], loopShelf[kLanes], hfDamp[kLanes];
    fx::Wander wob[2 * kLanes];
    double wobFrom[2 * kLanes] = {}, wobTo[2 * kLanes] = {};
};

// ---- the digital "spring" (one voice) --------------------------------------------------------------------------------
class DigitalSpring {
public:
    static constexpr int kChirp = 6;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        const double c = kDigitalMaxClock;
        for (auto& d : comb) d.setMax((int)(c * 0.0269 * 1.15) + 16);
        ap[0].setMax((int)(c * 0.0041) + 16);
        ap[1].setMax((int)(c * 0.0014) + 16);
        for (auto& d : flut) d.setMax((int)(c * 0.0063 * 1.6) + 16);
        glideK = 1.0 - std::exp(-kTick / (0.05 * sr));
        flWander.setup(sr / kTick, 0x0D1Fu);
        reset();
    }
    void reset()
    {
        for (auto& d : comb) d.clear();
        for (auto& d : flut) d.clear();
        ap[0].clear(); ap[1].clear();
        for (auto& p : damp) p.reset();
        for (int c = 0; c < 2; ++c) { res[c][0].reset(); res[c][1].reset(); recon[c].reset(); held[c] = 0.0; }
        for (auto& w : chirpW) w[0] = w[1] = 0.0;
        for (auto& v : vis) v = 0.0f;
        chirpPos = 0;
        phase = 1.0;
        primed = false;
    }
    void setModel(int m) { model = m == kLofi ? kLofi : kDigital; primed = false; }

    void setTargets(const Settings& s)
    {
        const DigitalModel& dm = digitalModel(model);
        const double t60 = clampv(s.decay, 0.1, 20.0), ts = digitalScale(s.tension);
        ctl.clock.target = clockHz(model, s.grit);
        ctl.bits.target = wordBits(model, s.grit);
        ctl.loopBits.target = 8.0 - 5.0 * clampv(s.grit, 0.0, 100.0) * 0.01;
        const double drv = dbToGain(-12.0 + 0.24 * clampv(s.dwell, 0.0, 100.0));
        ctl.drive.target = drv;
        ctl.makeup.target = std::pow(drv, -0.8);
        for (int i = 0; i < 4; ++i)
        {
            const bool on = i < dm.combs;
            const double len = on ? dm.comb[i] * ts : 0.01;
            ctl.len[i].target = len;
            const double g = std::pow(10.0, -3.0 * len / t60);
            ctl.g[i].target = on ? g : 0.0;
            ctl.norm[i].target = on ? std::pow(1.0 - g * g, 0.25) * std::sqrt(2.0 / dm.combs) : 0.0;
        }
        const double drip = clampv(s.drip, 0.0, 100.0) * 0.01;
        ctl.flutter.target = dm.flutter * ts;
        ctl.flutterG.target = 0.40 + 0.35 * drip;
        ctl.chirpA.target = 0.35 + 0.30 * drip;
        ctl.wobble.target = clampv(s.wobble, 0.0, 100.0) * 0.01;
        if (!primed) { ctl.snap(); primed = true; }
    }

    void tick()
    {
        ctl.step(glideK);
        const DigitalModel& dm = digitalModel(model);
        const double fsC = ctl.clock.cur;
        ratio = std::min(1.0, fsC / sr);
        stepQ = std::pow(2.0, 1.0 - ctl.bits.cur);           // one step of a signed word (the converters)
        loopQ = stepQ * std::pow(2.0, -ctl.loopBits.cur);   // the loops keep a few bits more
        for (int i = 0; i < 4; ++i) lenS[i] = clampv(ctl.len[i].cur * fsC, 2.0, (double)comb[i].maxDelay());
        apS[0] = clampv(dm.ap[0] * fsC, 2.0, (double)ap[0].line().maxDelay() - 2.0);
        apS[1] = clampv(dm.ap[1] * fsC, 2.0, (double)ap[1].line().maxDelay() - 2.0);
        // the flutter tap moves in whole clock periods (zipper)
        const double w = flWander.next() * ctl.wobble.cur;
        flS = clampv((int)std::lround(ctl.flutter.cur * fsC * (1.0 + 0.35 * w)), 2, flut[0].maxDelay() - 2);
        for (int i = 0; i < 4; ++i) damp[i].set(0.30 * fsC, fsC);
        for (int c = 0; c < 2; ++c)
        {
            res[c][0].set(std::min(dm.res[0], 0.45 * fsC), 3.0, fsC);
            res[c][1].set(std::min(dm.res[1], 0.45 * fsC), 4.0, fsC);
            recon[c].set(0.42 * fsC, sr);
        }
    }

    inline void process(double x, double kick, double& oL, double& oR)
    {
        phase += ratio;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            clockStep(x, kick);
        }
        const double g = kDigitalGain * ctl.makeup.cur;
        oL += g * recon[0].lp(held[0]);
        oR += g * recon[1].lp(held[1]);
    }

    double finiteCheck() const { return held[0] + held[1] + recon[0].s + recon[1].s + comb[0].at(0) + flut[0].at(0) + flut[1].at(0); }

    // what the UI draws: each comb's output (at the clock rate, held)
    float vis[kLanes] = {};

private:
    static constexpr double kDigitalGain = 0.42;

    // one tick of the converter clock: sample (no anti-aliasing), clip, quantise, the network, quantise, hold
    inline void clockStep(double x, double kick)
    {
        const double q = stepQ;
        double v = quantise(clampv(x * ctl.drive.cur + 0.8 * kick, -1.0, 1.0), q);
        v = ap[0].process((float)v, apS[0], 0.62f);
        v = ap[1].process((float)v, apS[1], 0.60f);
        // token chirp: 6 allpasses (a + z^-2) / (1 + a z^-2)
        const double ca = ctl.chirpA.cur;
        for (int k = 0; k < kChirp; ++k)
        {
            const double w2 = chirpW[k][chirpPos];   // w two ticks ago (two slots per section, alternating)
            const double w = v - ca * w2;
            v = ca * w + w2;
            chirpW[k][chirpPos] = w;
        }
        chirpPos ^= 1;
        // the combs: damped loops truncated to the loop word
        double y[4];
        for (int i = 0; i < 4; ++i)
        {
            y[i] = comb[i].readLinear(lenS[i] - 1.0);
            comb[i].push((float)truncate(v + ctl.g[i].cur * damp[i].lp(y[i]), loopQ));
            y[i] *= ctl.norm[i].cur;
            vis[i] = (float)y[i];
        }
        const double o[2] = { y[0] + y[2] + 0.35 * y[1], y[1] + y[3] + 0.35 * y[2] };
        for (int c = 0; c < 2; ++c)
        {
            const double fv = o[c] + ctl.flutterG.cur * flut[c].at(flS - 1 + c);
            flut[c].push((float)truncate(fv, loopQ));
            double r = 0.6 * o[c] + 0.4 * fv;
            r = res[c][0].peak(r, 2.8);
            r = res[c][1].peak(r, 2.2);
            held[c] = quantise(clampv(r, -1.0, 1.0), q);
        }
    }

    struct Ctl {
        Glided clock, bits, loopBits, drive, makeup, len[4], g[4], norm[4], flutter, flutterG, chirpA, wobble;
        template <class F> void each(F f)
        {
            f(clock); f(bits); f(loopBits); f(drive); f(makeup); f(flutter); f(flutterG); f(chirpA); f(wobble);
            for (int i = 0; i < 4; ++i) { f(len[i]); f(g[i]); f(norm[i]); }
        }
        void snap() { each([](Glided& v) { v.cur = v.target; }); }
        void step(double k) { each([k](Glided& v) { v.step(k); }); }
    } ctl;

    double sr = 48000.0, glideK = 0.01, ratio = 0.5, phase = 1.0, stepQ = 1.0 / 32768.0, loopQ = 1.0 / 8388608.0;
    double lenS[4] = { 400, 500, 600, 700 }, apS[2] = { 100, 40 }, held[2] = { 0, 0 };
    double chirpW[kChirp][2] = {};
    int model = kDigital, flS = 200, chirpPos = 0;
    bool primed = false;
    fx::FracDelay comb[4], flut[2];
    fx::Allpass ap[2];
    fx::OnePole damp[4], recon[2];
    fx::Svf res[2][2];
    fx::Wander flWander;
};

// ---- the engine ------------------------------------------------------------------------------------------------------
class Engine {
public:
    // after a non-finite output: silence the tank without reallocating (safe on the audio thread)
    void recover() { clearState(); }

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voice)
        {
            v.tank.prepare(sr);
            v.dig.prepare(sr);
            v.inG.setRamp(std::max(1, (int)(0.005 * sr)));
            v.outG.setRamp(std::max(1, (int)(0.150 * sr)));
        }
        pre.setMax((int)(kMaxPreDelay * sr) + 8);
        preGlide.setTime(0.06, sr);
        burst.sr = sr;
        const int ramp = std::max(1, (int)(0.020 * sr));
        mixS.setRamp(ramp); outS.setRamp(ramp); widthS.setRamp(ramp); lcOn.setRamp(ramp);
        filterK = 1.0 - std::exp(-kTick / (0.03 * sr));
        decim = std::max(1, (int)std::lround(sr / 12000.0));
        for (int c = 0; c < 2; ++c) shelf[c].set(2500.0, 0.6, sr);
        clearState();
    }

    // ---- settings (audio thread, block start) ----
    void setSettings(const Settings& s) { set = s; set.model = clampv(s.model, 0, kNumModels - 1); dirty = true; }
    // the KICK key (audio thread): crash the tank (strength 1 = a full kick)
    void kick(double strength = 1.0)
    {
        burst.trigger(strength);
        voice[activeV].tank.kicked(rng, strength);
        kickAgeS = 0;
    }

    int latency() const { return 0; }
    double visRate() const { return sr / decim; }

    // ---- feeds (UI) ----
    // the wave entering each spring at the input end (F) and at the far end (B), at visRate(); digital models: the
    // combs' outputs (in both)
    pump::ScopeRing visF[kLanes] = { pump::ScopeRing(2048), pump::ScopeRing(2048), pump::ScopeRing(2048), pump::ScopeRing(2048) };
    pump::ScopeRing visB[kLanes] = { pump::ScopeRing(2048), pump::ScopeRing(2048), pump::ScopeRing(2048), pump::ScopeRing(2048) };
    pump::MeterValue laneLevel[kLanes], driverLevel;   // peaks since the UI last read them (driver: digital = input)
    std::atomic<int> kickAge { 1 << 30 };              // samples since the last kick

    void process(float* L, float* R, int n)
    {
        int done = 0;
        while (done < n)
        {
            if (tickPos == 0) controlTick();
            const int m = std::min(n - done, kTick - tickPos);
            processRun(L + done, R + done, m);
            tickPos = (tickPos + m) % kTick;
            done += m;
        }
        if (!std::isfinite(chk))
        {
            clearState();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        chk = 0.0;
        kickAgeS = std::min(kickAgeS + n, 1 << 30);
        kickAge.store(kickAgeS, std::memory_order_relaxed);
    }

private:
    struct Voice {
        SpringTank tank;
        DigitalSpring dig;
        LinearSmoother inG, outG;
        int model = kTwoSpring;
        bool busy = false;
        void start(int m, const Settings& s)
        {
            model = m;
            if (isDigital(m)) { dig.setModel(m); dig.reset(); dig.setTargets(s); dig.tick(); }
            else { tank.setModel(m); tank.reset(); tank.setTargets(s); tank.tick(); }
            inG.reset(1.0); outG.reset(1.0);
            busy = true;
        }
        void setTargets(const Settings& s) { if (isDigital(model)) dig.setTargets(s); else tank.setTargets(s); }
        void tick() { if (isDigital(model)) dig.tick(); else tank.tick(); }
        double finiteCheck() const { return isDigital(model) ? dig.finiteCheck() : tank.finiteCheck(); }
    };

    void clearState()
    {
        pre.clear();
        preGlide.reset(clampv(set.preDelay, 0.0, 250.0) * 0.001 * sr);
        mixS.reset(set.mix * 0.01); outS.reset(dbToGain(set.outDb)); widthS.reset(set.width * 0.01);
        lcOn.reset(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        for (int c = 0; c < 2; ++c) { lc[c].reset(); toneLp[c].reset(); shelf[c].reset(); }
        lcHz.cur = lcHz.target = std::max((double)kLowCutOff, set.lowCut);
        lpHz.cur = lpHz.target = toneLpHz(set.tone);
        shelfG.cur = shelfG.target = toneShelf(set.tone);
        designWet();
        voice[1].busy = false;
        activeV = 0;
        voice[0].start(set.model, set);
        burst.reset();
        tickPos = 0;
        decimPos = 0;
        chk = 0.0;
        dirty = false;
    }
    // TONE: a low-pass at 9 kHz in the middle, 2 kHz fully dark, 20 kHz fully bright (plus the shelf)
    static double toneLpHz(double tone)
    {
        const double t = clampv(tone, -100.0, 100.0) * 0.01;
        return t < 0.0 ? 9000.0 * std::pow(2000.0 / 9000.0, -t) : 9000.0 * std::pow(20000.0 / 9000.0, t);
    }
    static double toneShelf(double tone) { return dbToGain(0.06 * std::max(0.0, tone)); }
    void designWet()
    {
        for (int c = 0; c < 2; ++c)
        {
            lc[c].set(lcHz.cur, 0.70710678, sr);
            toneLp[c].set(std::min(lpHz.cur, 0.45 * sr), 0.70710678, sr);
        }
    }

    void controlTick()
    {
        // a model change: the idle voice takes over, the old one rings out (a change during that fade waits for it)
        Voice& act = voice[activeV];
        Voice& other = voice[1 - activeV];
        if (set.model != act.model && !other.busy)
        {
            act.inG.setTarget(0.0);
            act.outG.setTarget(0.0);
            other.start(set.model, set);
            activeV = 1 - activeV;
        }
        for (auto& v : voice)
        {
            if (!v.busy) continue;
            if (dirty) v.setTargets(set);
            v.tick();
        }
        dirty = false;
        mixS.setTarget(set.mix * 0.01);
        outS.setTarget(dbToGain(set.outDb));
        widthS.setTarget(set.width * 0.01);
        lcOn.setTarget(lowCutActive(set.lowCut) ? 1.0 : 0.0);
        lcHz.target = std::max((double)kLowCutOff, set.lowCut);
        lpHz.target = toneLpHz(set.tone);
        shelfG.target = toneShelf(set.tone);
        const double moved = std::fabs(lcHz.target - lcHz.cur) / lcHz.target + std::fabs(lpHz.target - lpHz.cur) / lpHz.target;
        lcHz.step(filterK); lpHz.step(filterK); shelfG.step(filterK);
        if (moved > 1e-6) designWet();
    }

    void processRun(float* L, float* R, int n)
    {
        const double preTarget = clampv(set.preDelay, 0.0, 250.0) * 0.001 * sr;
        const Voice& a = voice[activeV];
        const bool digital = isDigital(a.model);
        float lv[kLanes] = {}, dv = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double frac = (tickPos + i + 0.5) / kTick;
            pre.push(0.5f * (L[i] + R[i]));
            const double x = pre.readLinear(preGlide.next(preTarget));
            const double k = burst.next();
            double wl = 0.0, wr = 0.0;
            for (int vi = 0; vi < 2; ++vi)
            {
                Voice& v = voice[vi];
                if (!v.busy) continue;
                const double gi = v.inG.next(), go = v.outG.next();
                const double kk = vi == activeV ? k : 0.0;
                double ol = 0.0, orr = 0.0;
                if (isDigital(v.model)) v.dig.process(x * gi, kk, ol, orr);
                else v.tank.process(x * gi, kk, frac, ol, orr);
                wl += go * ol; wr += go * orr;
                if (go <= 0.0 && !v.outG.isSmoothing()) v.busy = false;
            }
            // the wet path: low cut, tone, width
            double w[2] = { wl, wr };
            const double lcm = lcOn.next(), sh = shelfG.cur;
            for (int c = 0; c < 2; ++c)
            {
                const double hcut = lc[c].highpass(w[c]);
                double y = w[c] + (hcut - w[c]) * lcm;
                y = toneLp[c].lowpass(y);
                if (sh > 1.0001) y = shelf[c].highShelf(y, sh);
                w[c] = y;
            }
            const double wd = widthS.next();
            const double mid = 0.5 * (w[0] + w[1]), side = 0.5 * (w[0] - w[1]) * wd;
            const double mx = mixS.next(), og = outS.next();
            const double dryG = std::min(1.0, 2.0 * (1.0 - mx)), wetG = std::min(1.0, 2.0 * mx);
            L[i] = (float)((L[i] * dryG + (mid + side) * wetG) * og);
            R[i] = (float)((R[i] * dryG + (mid - side) * wetG) * og);
            chk += wl + wr;
            // UI feeds
            if (++decimPos >= decim)
            {
                decimPos = 0;
                for (int l = 0; l < kLanes; ++l)
                {
                    visF[l].push(digital ? a.dig.vis[l] : a.tank.vis[l]);
                    visB[l].push(digital ? a.dig.vis[l] : a.tank.vis[l + kLanes]);
                }
            }
            for (int l = 0; l < kLanes; ++l) lv[l] = std::max(lv[l], std::fabs(digital ? a.dig.vis[l] : a.tank.laneOut[l]));
            dv = std::max(dv, digital ? (float)std::fabs(x) : std::fabs(a.tank.drvOut));
        }
        for (const auto& v : voice) if (v.busy) chk += v.finiteCheck();
        for (int l = 0; l < kLanes; ++l) laneLevel[l].setMax(lv[l]);
        driverLevel.setMax(dv);
    }

    double sr = 48000.0, filterK = 0.01, chk = 0.0;
    Settings set;
    Voice voice[2];
    int activeV = 0, decim = 4, decimPos = 0, tickPos = 0, kickAgeS = 1 << 30;
    bool dirty = true;
    fx::FracDelay pre;
    fx::Glide preGlide;
    KickBurst burst;
    fx::Rng rng { 0xC0FFEEu };
    fx::Svf lc[2], toneLp[2], shelf[2];
    Glided lcHz, lpHz, shelfG;
    LinearSmoother mixS, outS, widthS, lcOn;
};

}} // namespace pump::spring
