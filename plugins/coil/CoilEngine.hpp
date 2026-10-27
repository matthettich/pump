// pump COIL / pump TWANG engine: SPRING's tanks as tuned sound sources. Framework-free.
//
// A spring in pump SPRING is a waveguide loop: a cascade of stretched allpasses (the dispersion: every echo is a
// rising chirp) and a delay line, a round trip of 18-62 ms. Here each spring of a tank becomes a loop tuned to a note:
// its period is one cycle of the note, so the echoes fuse into a tone and the dispersion becomes the timbre (the
// spring's stretched, clanging partials). That is a dispersive waveguide, Karplus-Strong's cousin.
//   tuning     the loop's total phase lag at the note's frequency is one period: the delay line makes up what the
//              allpass cascade, the loop filters and the read leave (so dispersion never detunes the fundamental). The
//              cascade has up to 80 sections like SPRING's; when the note's period is too short for them all (high
//              notes), it keeps as many as fit.
//   springs    the tank's springs (2 or 3, with SPRING's own transition detune and pans) play together; SPREAD sets
//              their lengths: 0 all on the note (a slow beating chorus), 100 % the tank's own length ratios (the
//              inharmonic cluster a real tank rings with), in between a widening shimmer
//   HARMONIC   0 the springs' own dispersion (and the splashy high path), 100 % none: the partials line up into a
//              clean, string-like pluck
//   the rest   TENSION and DRIP as in SPRING (transition frequency, dispersion, the transducer's peak), DECAY the
//              ring time, TONE the loop's damping, WOBBLE the springs drifting (a slow detune), the KICK burst (a
//              noise crack and a thump, different every time) excites each spring with its own sign; EXCITE leans it
//              to the thump or the crack
//   digital    Digital and Lo-fi digital: their combs tuned to the note (SPREAD from their own ratios), a short-word
//              input, loops truncated to the loop word, a token chirp, the output held at the converter clock
//              (aliasing): the cheap digital "spring", in tune
//   TANK       the real SPRING of the same model, kicked by every note and fed the voices: exactly SPRING's sound,
//              as a body around the tuned springs
// COIL plays notes (poly, 8 voices; velocity is the kick's strength). TWANG is a resonator: its input, through
// SPRING's driver (DWELL), excites springs tuned to the held MIDI notes or to a chord.
#pragma once

#include "../spring/SpringEngine.hpp"
#include "dsp/Synth.hpp"
#include "CoilParams.hpp"
#include <array>
#include <complex>

namespace pump { namespace coil {

using spring::f4;
using spring::splat;

struct Settings {
    int model = spring::kTwoSpring;
    double tension = 0.0, drip = 50.0, decay = 2.0, release = 0.4, harmonic = 0.0, spread = 0.1, wobble = 0.15, tone = 0.0;
    double excite = 0.5, tank = 0.0, width = 0.7, velocity = 0.7, volumeDb = -6.0, grit = 35.0;
    int octave = 0;
    double bend = 2.0;
    // TWANG
    bool resonator = false;
    int tuning = kTuneChord, root = 9, chord = kChordFifth;
    double dwell = 50.0, mix = 0.5;
};

constexpr int kMaxVoices = 8, kMaxSections = spring::kSections;
constexpr double kMinHz = 20.0;

// phase lag (radians) at w of one stretched section, D(z) = z^-K T(z), A = (a + D) / (1 + a D)
inline double sectionLag(double w, int K, double eta, double a)
{
    const double lagT = w - 2.0 * std::atan2(eta * std::sin(w), 1.0 + eta * std::cos(w));
    const double phi = K * w + lagT;
    return phi - 2.0 * std::atan2(a * std::sin(phi), 1.0 + a * std::cos(phi));
}
// phase lag of the TPT state-variable low-pass (Q 0.707) and one-pole high-pass at w
inline double svfLpLag(double w, double fc, double sr)
{
    const double g = std::tan(kPi * clampv(fc, 5.0, 0.49 * sr) / sr), t = std::tan(0.5 * w) / g;
    const std::complex<double> s(0.0, t), h = 1.0 / (s * s + 1.41421356 * s + 1.0);
    return -std::arg(h);
}
inline double onePoleHpLag(double w, double fc, double sr)
{
    const double g = std::tan(kPi * clampv(fc, 1.0, 0.49 * sr) / sr), t = std::tan(0.5 * w) / g;
    const std::complex<double> s(0.0, t), h = s / (1.0 + s);
    return -std::arg(h);
}
inline double onePoleLpLag(double w, double fc, double sr)
{
    const double g = std::tan(kPi * clampv(fc, 1.0, 0.49 * sr) / sr), t = std::tan(0.5 * w) / g;
    const std::complex<double> s(0.0, t), h = 1.0 / (1.0 + s);
    return -std::arg(h);
}
// the loop's damping low-pass: TONE leans it from SPRING's transition down (darker) or up (brighter)
inline double loopCut(double fc, double tone) { return fc * std::pow(2.0, 1.6 * clampv(tone, -1.0, 1.0)); }

// ---- one voice: a tank's springs tuned to a note -------------------------------------------------------------------
class Voice {
public:
    void prepare(double sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        kint = spring::stretchInt(sr);
        rows = 4;
        while (rows < kint + 2) rows <<= 1;   // the history must reach kint + 1 back (17 at 192 kHz), as in SPRING
        ring.assign((size_t)(rows * kMaxSections), splat(0.0f));
        vprev.assign((size_t)kMaxSections, splat(0.0f));
        const int maxP = (int)(sr / kMinHz * 1.35) + 64;   // the lowest note, the longest spread
        for (auto& d : line) d.setMax(maxP);
        for (auto& d : hfLine) d.setMax((int)(maxP * spring::kHfRatio * 2.0) + 64);
        for (auto& d : comb) d.setMax(maxP);
        burst.sr = sr;
        burst.rng.seed(0x51A7u + 7919u * seed);
        rng.seed(0xC011u + 104729u * seed);
        for (int l = 0; l < 4; ++l) wob[l].setup(sr / spring::kTick, 0x2F00u + 977u * (uint32_t)l + 31u * seed);
        glideK = 1.0 - std::exp(-spring::kTick / (0.03 * sr));
        reset();
    }
    void reset()
    {
        std::fill(ring.begin(), ring.end(), splat(0.0f));
        std::fill(vprev.begin(), vprev.end(), splat(0.0f));
        for (auto& s : hfState) s = splat(0.0f);
        for (auto& d : line) d.clear();
        for (auto& d : hfLine) d.clear();
        for (auto& d : comb) d.clear();
        for (int l = 0; l < 4; ++l) { loopLp[l].reset(); loopHp[l].reset(); hfDamp[l].reset(); damp[l].reset(); }
        for (auto& c : chirpW) c[0] = c[1] = 0.0;
        burst.reset();
        active = false; held = false; energy = 0.0; wpos = 0; phase = 1.0; hold[0] = hold[1] = 0.0; chirpPos = 0;
    }

    // a note: tune and kick (strength 0..1.5; 0 = no kick, the resonator's tuning)
    void start(int noteNum, double hz, double strength, const Settings& s)
    {
        const bool was = active;
        note = noteNum; freq = clampv(hz, kMinHz, 0.45 * sr);
        held = true;
        if (!was) { reset(); held = true; }
        design(s, true);
        active = true;
        if (strength > 0.0)
        {
            burst.trigger(strength);
            hitSprings();
        }
        silentFor = 0;
    }
    void release(const Settings& s) { held = false; design(s, false); }
    void kick(double strength) { if (!active) return; burst.trigger(strength); hitSprings(); }
    // each spring takes the kick a little harder or softer; all with the same sign (tuned alike, springs hit with
    // opposite signs would cancel each other in mono)
    void hitSprings() { for (int l = 0; l < 4; ++l) kickG[l] = 0.7 + 0.3 * rng.uni(); }
    // the settings changed while it rings: retune the same sections (no clicks: the values glide)
    void update(const Settings& s) { if (active) design(s, false); }
    void retune(double hz, const Settings& s) { freq = clampv(hz, kMinHz, 0.45 * sr); if (active) design(s, false); }

    bool isActive() const { return active; }
    bool isHeld() const { return held; }
    int noteNumber() const { return note; }
    double level() const { return energy; }

    void tick()
    {
        for (int l = 0; l < 4; ++l)
        {
            dl[l].step(glideK); g[l].step(glideK); dh[l].step(glideK); gh[l].step(glideK);
            ap[l].step(glideK); et[l].step(glideK);
            wobFrom[l] = wobTo[l];
            wobTo[l] = wobDepth * dl[l].cur * wob[l].next();
        }
        float av[4], ev[4];
        for (int l = 0; l < 4; ++l) { av[l] = (float)ap[l].cur; ev[l] = (float)et[l].cur; }
        a4 = f4 { av[0], av[1], av[2], av[3] };
        e4 = f4 { ev[0], ev[1], ev[2], ev[3] };
        for (int l = 0; l < lanes; ++l) loopLp[l].set(cut[l], 0.70710678, sr);
    }

    // one sample: x = excitation from the input (TWANG), frac = position in the control tick
    inline void process(double x, double frac, double& oL, double& oR)
    {
        if (!active) return;
        const double k = burst.next();
        double L = 0.0, R = 0.0;
        if (digital) digitalStep(x, k, frac, L, R); else tankStep(x, k, frac, L, R);
        oL += L; oR += R;
        const double e = std::fabs(L) + std::fabs(R);
        energy += (e - energy) * 0.0005;
        if (!held && energy < 2e-6 && burst.t < 0.0) { if (++silentFor > (int)(0.05 * sr)) active = false; }
        else silentFor = 0;
    }

    // what the window draws: the voice's springs (lane periods in samples, 0 = unused) and how many sections fit
    double lanePeriod(int l) const { return l < lanes ? period[l] : 0.0; }
    int sectionsNow() const { return sections; }

private:
    void design(const Settings& s, bool fresh)
    {
        model = clampv(s.model, 0, spring::kNumModels - 1);
        digital = spring::isDigital(model);
        const double h = clampv(s.harmonic, 0.0, 1.0), sp = clampv(s.spread, 0.0, 1.0);
        const double P0 = sr / freq;
        const double t60 = held ? clampv(s.decay, 0.05, 20.0) : clampv(std::min(s.decay, s.release), 0.02, 20.0);
        wobDepth = clampv(s.wobble, 0.0, 1.0) * 0.004;
        burst.mixCrack = 0.4 + 1.2 * clampv(s.excite, 0.0, 1.0);
        burst.mixThump = 1.6 - 1.2 * clampv(s.excite, 0.0, 1.0);
        if (digital)
        {
            const spring::DigitalModel& dm = spring::digitalModel(model);
            lanes = dm.combs;
            clock = std::min(sr, spring::clockHz(model, s.grit));
            stepQ = std::pow(2.0, 1.0 - spring::wordBits(model, s.grit));
            loopQ = stepQ * std::pow(2.0, -(8.0 - 5.0 * clampv(s.grit, 0.0, 100.0) * 0.01));
            chirpA = (0.35 + 0.30 * clampv(s.drip, 0.0, 100.0) * 0.01) * (1.0 - h);
            // the damping stays well above the note (it would eat a high note in a few hundred passes)
            const double dcut = std::min(std::max(0.30 * clock, 12.0 * freq), 0.45 * sr);
            double gm = 0.0; for (int l = 0; l < lanes; ++l) gm += std::log(dm.comb[l] / dm.comb[0]) / lanes;
            for (int l = 0; l < 4; ++l)
            {
                const bool on = l < lanes;
                const double ratio = on ? dm.comb[l] / dm.comb[0] : 1.0;
                period[l] = on ? P0 * std::exp(sp * (std::log(ratio) - gm)) : 0.0;   // the spread centred on the note
                damp[l].set(dcut, sr);
                const double w = on ? kTwoPi / period[l] : kTwoPi * freq / sr;
                const double lag = onePoleLpLag(w, dcut, sr) / w;
                dl[l].target = on ? std::max(1.0, period[l] - lag - 1.0) : 2.0;
                // the damping takes |H| at the note on every pass: made up, so DECAY holds at any pitch
                const double t = std::tan(0.5 * w) / std::tan(kPi * dcut / sr), mag = 1.0 / std::sqrt(1.0 + t * t);
                g[l].target = on ? std::min(0.99995, std::pow(10.0, -3.0 * period[l] / sr / t60) / mag) : 0.0;
                inNorm[l] = on ? std::sqrt(1.0 - g[l].target * g[l].target) * 3.0 : 0.0;
                norm[l] = on ? std::sqrt(2.0 / lanes) : 0.0;
                const double p = lanes == 4 ? (l % 2 ? 0.6 : -0.6) * (l < 2 ? 1.0 : 0.5) : (l == 0 ? -1.0 : l == 1 ? 1.0 : 0.0);
                panL[l] = std::cos((p + 1.0) * 0.25 * kPi); panR[l] = std::sin((p + 1.0) * 0.25 * kPi);
            }
            if (fresh) snap();
            return;
        }
        const spring::Design d = spring::design(model, s.tension, s.drip, sr);
        lanes = d.springs;
        const double travel0 = d.travel[0];
        // how many sections fit: every lane's cascade must leave room for its delay line
        int M = kMaxSections;
        double a[4] = {}, eta[4] = {};
        double gm = 0.0; for (int l = 0; l < lanes; ++l) gm += std::log(d.travel[l] / travel0) / lanes;
        // near the transition the dispersion crowds the modes (a kick would ring a neighbour): it thins out from about
        // a quarter of fc to half of it
        const double thin = clampv((0.46 * d.fc - freq) / (0.23 * d.fc), 0.0, 1.0);
        for (int l = 0; l < 4; ++l)
        {
            const bool on = l < lanes;
            const double ratio = on ? d.travel[l] / travel0 : 1.0;
            period[l] = on ? P0 * std::exp(sp * (std::log(ratio) - gm)) : P0;   // the spread centred on the note
            a[l] = d.a[l] * (1.0 - h) * thin;
            eta[l] = d.eta[l];
            // the loop's low-pass is the spring's transition, but never under 8 x the note (a high note passes it
            // thousands of times a second: it would die at once)
            cut[l] = std::min(0.45 * sr, std::max(loopCut(d.fcl[l], s.tone) * (1.0 + 3.0 * h), 8.0 * freq / std::exp(sp * (std::log(ratio) - gm))));
        }
        if (fresh || sections < 0)
        {
            for (int l = 0; l < lanes; ++l)
            {
                const double perDc = (kint + (1.0 - eta[l]) / (1.0 + eta[l])) * (1.0 - a[l]) / (1.0 + a[l]);
                M = std::min(M, (int)std::floor(0.7 * period[l] / std::max(1e-3, perDc)));
            }
            // HARMONIC also takes sections out: even with no dispersion of their own, their fractional delays stretch
            sections = clampv((int)std::lround(M * (1.0 - h) * thin), 0, kMaxSections);
        }
        for (int l = 0; l < 4; ++l)
        {
            const bool on = l < lanes;
            const double w = kTwoPi * (P0 > 0 ? sr / period[l] : freq) / sr;   // this lane's own fundamental
            // (the loop's DC blocker is left out: compensating its lead at the fundamental would put every overtone flat)
            const double lag = (sections * sectionLag(w, kint, eta[l], a[l]) + svfLpLag(w, cut[l], sr)) / w;
            dl[l].target = on ? std::max(1.0, period[l] - lag - 1.0) : 2.0;
            ap[l].target = a[l];
            et[l].target = eta[l];
            // the loop filters take |H| at the note on every pass: made up, so DECAY holds at any pitch
            const double tl = std::tan(0.5 * w) / std::tan(kPi * clampv(cut[l], 5.0, 0.49 * sr) / sr);
            const double magLp = 1.0 / std::sqrt((1.0 - tl * tl) * (1.0 - tl * tl) + 2.0 * tl * tl);
            const double magHp = 1.0;
            g[l].target = on ? std::min(0.99995, std::pow(10.0, -3.0 * period[l] / sr / t60) / (magLp * magHp)) : 0.0;
            inNorm[l] = on ? std::sqrt(1.0 - g[l].target * g[l].target) * 3.0 : 0.0;
            norm[l] = on ? std::sqrt(2.0 / lanes) : 0.0;
            // the high path: a loop 1.24 x the period through the plain allpasses, ringing shorter, fading with HARMONIC
            const double hl = spring::kHfRatio * 2.0 * period[l];
            dh[l].target = std::max(2.0, hl - spring::kHfSections * spring::hfSectionDelay(0.0) - 1.0);
            gh[l].target = on ? std::pow(10.0, -3.0 * hl / sr / (0.4 * t60)) : 0.0;
            // (in a tank it carries what lies above the transition: it fades with the dispersion at the top notes)
            normH[l] = on ? std::pow(1.0 - gh[l].target * gh[l].target, 0.25) * std::sqrt(2.0 / lanes) * (1.0 - h) * thin : 0.0;
            const double p = clampv(d.pan[l], -1.0, 1.0);
            panL[l] = on ? std::cos((p + 1.0) * 0.25 * kPi) : 0.0;
            panR[l] = on ? std::sin((p + 1.0) * 0.25 * kPi) : 0.0;
            hfDamp[l].set(std::min(16000.0, 0.45 * sr), sr);
            loopHp[l].set(0.5, sr);   // only keeps DC out: anything higher would lead the fundamental (sharp low notes)
        }
        hfIn = thin * (1.0 - h);
        if (fresh) snap();
    }
    void snap()
    {
        for (int l = 0; l < 4; ++l) { dl[l].cur = dl[l].target; g[l].cur = g[l].target; dh[l].cur = dh[l].target; gh[l].cur = gh[l].target; ap[l].cur = ap[l].target; et[l].cur = et[l].target; wobFrom[l] = wobTo[l] = 0.0; }
        tick();
    }

    inline void tankStep(double x, double k, double frac, double& L, double& R)
    {
        alignas(16) float xs[4] = {};
        float tap[4] = {};
        for (int l = 0; l < lanes; ++l)
        {
            const double d = dl[l].cur + wobFrom[l] + (wobTo[l] - wobFrom[l]) * frac;
            const float t = line[l].read(d);
            tap[l] = t;
            xs[l] = (float)(x * inNorm[l] + k * kickG[l] + g[l].cur * t);
        }
        f4 v;
        std::memcpy(&v, xs, sizeof v);
        disperse(v);
        std::memcpy(xs, &v, sizeof v);
        for (int l = 0; l < lanes; ++l) line[l].push((float)loopHp[l].hp(loopLp[l].lowpass(xs[l])));
        // the high path
        alignas(16) float hx[4] = {}, htap[4] = {};
        for (int l = 0; l < lanes; ++l)
        {
            htap[l] = hfLine[l].readLinear(dh[l].cur);
            hx[l] = (float)((0.5 * k * kickG[l] + 0.3 * x) * hfIn + gh[l].cur * htap[l]);
        }
        f4 hv;
        std::memcpy(&hv, hx, sizeof hv);
        const f4 ha = splat((float)spring::kHfCoef);
        for (int m = 0; m < spring::kHfSections; ++m)
        {
            const f4 w = hv - ha * hfState[m];
            hv = ha * w + hfState[m];
            hfState[m] = w;
        }
        std::memcpy(hx, &hv, sizeof hv);
        for (int l = 0; l < lanes; ++l) hfLine[l].push((float)hfDamp[l].lp(hx[l]));
        for (int l = 0; l < lanes; ++l)
        {
            const double o = norm[l] * tap[l] * kLoopOut + kHfLevel * normH[l] * htap[l];
            L += panL[l] * o; R += panR[l] * o;
        }
    }
    // the stretched allpass cascade, the lanes side by side
    inline void disperse(f4& x)
    {
        if (sections <= 0) return;
        f4* __restrict rw = ring.data() + (size_t)(wpos * kMaxSections);
        const f4* __restrict ra = ring.data() + (size_t)(((wpos - kint) & (rows - 1)) * kMaxSections);
        const f4* __restrict rb = ring.data() + (size_t)(((wpos - kint - 1) & (rows - 1)) * kMaxSections);
        f4* __restrict vp = vprev.data();
        const f4 a = a4, e = e4;
        for (int m = 0; m < sections; ++m)
        {
            const f4 v = e * (ra[m] - vp[m]) + rb[m];
            vp[m] = v;
            const f4 w = x - a * v;
            rw[m] = w;
            x = a * w + v;
        }
        wpos = (wpos + 1) & (rows - 1);
    }

    inline void digitalStep(double x, double k, double frac, double& L, double& R)
    {
        (void)frac;
        // the input at the converter's word and clock
        phase += clock / sr;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            const double xk = 0.8 * k;
            kickPart = std::fabs(xk) > std::fabs(x) ? 1.0 : 0.0;
            double v = spring::quantise(clampv(x + xk, -1.0, 1.0), stepQ);
            for (int c = 0; c < kChirp; ++c)
            {
                const double w2 = chirpW[c][chirpPos], w = v - chirpA * w2;
                v = chirpA * w + w2; chirpW[c][chirpPos] = w;
            }
            chirpPos ^= 1;
            inHeld = v;
        }
        double o[2] = { 0.0, 0.0 };
        for (int l = 0; l < lanes; ++l)
        {
            const double y = comb[l].read(dl[l].cur);
            comb[l].push((float)spring::truncate(inHeld * (kickG[l] * kickPart + inNorm[l] * (1.0 - kickPart)) + g[l].cur * damp[l].lp(y), loopQ));
            o[0] += panL[l] * norm[l] * y; o[1] += panR[l] * norm[l] * y;
        }
        // the output held between clock ticks (no reconstruction filter worth the name)
        if (phase < clock / sr) { hold[0] = spring::quantise(clampv(o[0], -1.0, 1.0), stepQ); hold[1] = spring::quantise(clampv(o[1], -1.0, 1.0), stepQ); }
        L += hold[0] * kDigitalOut; R += hold[1] * kDigitalOut;
    }

    // SPRING's kick, with EXCITE leaning it toward the crack or the thump
    struct KickB {
        fx::Rng rng { 0x51A7u };
        double sr = 48000.0, t = -1.0, amp = 0.0, f0 = 160.0, crackT = 0.012, mixCrack = 1.0, mixThump = 1.0;
        void trigger(double strength) { t = 0.0; amp = clampv(strength, 0.0, 1.5); f0 = 120.0 + 90.0 * rng.uni(); crackT = 0.008 + 0.010 * rng.uni(); }
        void reset() { t = -1.0; }
        inline double next()
        {
            if (t < 0.0) return 0.0;
            const double att = 1.0 - std::exp(-t / 0.0004);
            const double crack = rng.bi() * att * std::exp(-t / crackT);
            const double thump = std::sin(kTwoPi * f0 * t) * std::min(1.0, t / 0.001) * std::exp(-t / 0.035);
            const double v = amp * (0.9 * mixCrack * crack + 0.8 * mixThump * thump);
            t += 1.0 / sr;
            if (t > 0.25) t = -1.0;
            return v;
        }
    };

    static constexpr int kChirp = 6;
    static constexpr double kHfLevel = 0.35, kLoopOut = 0.6, kDigitalOut = 0.5;
    double sr = 48000.0, freq = 220.0, glideK = 0.05, wobDepth = 0.0, energy = 0.0;
    int kint = 4, rows = 16, lanes = 2, sections = -1, wpos = 0, model = 0, note = -1, silentFor = 0;
    bool active = false, held = false, digital = false;
    std::vector<f4> ring, vprev;
    f4 hfState[spring::kHfSections] = {};
    f4 a4 = {}, e4 = {};
    spring::Glided dl[4], g[4], dh[4], gh[4], ap[4], et[4];
    double inNorm[4] = {}, kickPart = 1.0, hfIn = 1.0;
    double period[4] = {}, cut[4] = { 4000, 4000, 4000, 4000 }, norm[4] = {}, normH[4] = {}, panL[4] = {}, panR[4] = {}, kickG[4] = { 1, -1, 1, -1 };
    double wobFrom[4] = {}, wobTo[4] = {};
    fx::FracDelay line[4], hfLine[4], comb[4];
    fx::Svf loopLp[4];
    fx::OnePole loopHp[4], hfDamp[4], damp[4];
    fx::Wander wob[4];
    fx::Rng rng { 1u };
    KickB burst;
    // digital
    double clock = 32000.0, stepQ = 1.0 / 32768.0, loopQ = 1.0 / 8388608.0, chirpA = 0.5, phase = 1.0, inHeld = 0.0, hold[2] = {};
    double chirpW[kChirp][2] = {};
    int chirpPos = 0;
};

// ---- the engine ------------------------------------------------------------------------------------------------------
class Engine {
public:
    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        decim = std::max(1, (int)std::lround(sr / 12000.0)); decimPos = 0;
        for (int i = 0; i < kMaxVoices; ++i) voices[(size_t)i].prepare(sr, (uint32_t)i + 1);
        body.prepare(sr, maxBlock);
        bodyIn.assign((size_t)std::max(1024, maxBlock), 0.0f);
        bodyInR.assign(bodyIn.size(), 0.0f);
        drvHp.set(120.0, 0.70710678, sr);
        for (int c = 0; c < 2; ++c) { outHp[c].set(70.0, 0.70710678, sr); dripPk[c].reset(); outHp[c].reset(); }
        envF = envS = 0.0;
        envFk = 1.0 - std::exp(-1.0 / (0.001 * sr)); envFr = 1.0 - std::exp(-1.0 / (0.015 * sr));
        envSk = 1.0 - std::exp(-1.0 / (0.030 * sr)); envSr = 1.0 - std::exp(-1.0 / (0.150 * sr));
        const int ramp = std::max(1, (int)(0.02 * sr));
        volS.setRamp(ramp); volS.reset(dbToGain(set.volumeDb));
        tankS.setRamp(ramp); tankS.reset(set.tank);
        mixS.setRamp(ramp); mixS.reset(set.mix);
        vm.setup(kMaxVoices, synth::kPoly); vm.reset();
        tickPos = 0;
        prepared = true;
        configure(true);
    }
    void setSettings(const Settings& s)
    {
        const bool chordChange = s.tuning != set.tuning || s.root != set.root || s.chord != set.chord || s.octave != set.octave;
        set = s;
        configure(false);
        if (set.resonator && chordChange) chordVoices();
    }
    // the KICK key: every ringing spring (and the tank) gets a kick
    void kick(double strength) { kickReq = clampv(strength, 0.2, 1.5); }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    int sectionsNow() const { return sectFeed.load(std::memory_order_relaxed); }
    float lanePeriodNow(int l) const { return laneFeed[(size_t)clampv(l, 0, 3)].load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    // each voice's sound, at visRate(): the wave the SPRINGS screen sends along its spring
    std::array<ScopeRing, kMaxVoices> visV { ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024) };
    double visRate() const { return sr / decim; }
    float voiceLevel(int i) const { return levelFeed[(size_t)clampv(i, 0, kMaxVoices - 1)].load(std::memory_order_relaxed); }
    int voiceNote(int i) const { return noteFeed[(size_t)clampv(i, 0, kMaxVoices - 1)].load(std::memory_order_relaxed); }

    // in: TWANG's input (nullptr for COIL); L / R out (may alias in)
    // Any block size: a block longer than the buffers prepared for (some hosts send more than they announced, e.g.
    // when bouncing) is run in pieces, so nothing is resized on the audio thread.
    void process(const float* inL, const float* inR, float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        const int cap = (int)bodyIn.size();
        int done = 0, e = 0;
        while (done < n)
        {
            const int m = std::min(cap, n - done);
            int e2 = e; while (e2 < nev && (int)ev[e2].offset < done + m) ++e2;
            processChunk(inL ? inL + done : nullptr, inR ? inR + done : nullptr, L + done, R + done, m, ev + e, e2 - e, done);
            e = e2; done += m;
        }
    }

private:
    void processChunk(const float* inL, const float* inR, float* L, float* R, int n, const synth::Midi* ev, int nev, int evBase)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        tankS.setTarget(set.tank);
        mixS.setTarget(set.mix);
        if (kickReq > 0.0)
        {
            bool ringing = false;
            for (auto& v : voices) if (v.isActive()) { v.kick(kickReq); ringing = true; }
            body.kick(kickReq);
            // nothing ringing (no note played yet, or all rung out): KICK strikes the last note played, so it always
            // sounds (struck and let go at once: it rings out by DECAY / RELEASE)
            if (!ringing && !set.resonator)
            {
                const uint8_t vel = (uint8_t)clampv((int)std::lround(kickReq * 100.0), 1, 127);
                handle(synth::Midi { 0, 0x90, (uint8_t)lastNote, vel });
                handle(synth::Midi { 0, 0x80, (uint8_t)lastNote, 0 });
            }
            kickReq = 0.0;
        }
        int e = 0;
        const double drv = dbToGain(-10.0 + 0.32 * clampv(set.dwell, 0.0, 100.0)), makeup = std::pow(drv, -0.7);
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset - evBase <= i) handle(ev[e++]);
            if (tickPos == 0) for (auto& v : voices) v.tick();
            const double frac = tickPos / (double)spring::kTick;
            tickPos = (tickPos + 1) % spring::kTick;
            // TWANG: the input through SPRING's driver (high-pass, transient boost, drive, soft saturation)
            double x = 0.0, dryL = 0.0, dryR = 0.0;
            if (inL)
            {
                dryL = inL[i]; dryR = inR ? inR[i] : inL[i];
                const double h = drvHp.highpass(0.5 * (dryL + dryR));
                const double ah = std::fabs(h);
                envF += (ah - envF) * (ah > envF ? envFk : envFr);
                envS += (ah - envS) * (ah > envS ? envSk : envSr);
                const double tr = clampv(envF / (envS + 1e-5) - 1.0, 0.0, 2.0);
                const double dd = drv * (1.0 + 0.9 * set.drip * 0.01 * tr);
                x = ((double)fastTanh((float)(h * dd) + 0.2f) - kDrvBias) * makeup * 0.25;
            }
            double l = 0.0, r = 0.0;
            const bool visNow = ++decimPos >= decim;
            if (visNow) decimPos = 0;
            for (int k = 0; k < kMaxVoices; ++k)
            {
                const double before = l + r;
                voices[(size_t)k].process(x, frac, l, r);
                if (visNow) visV[(size_t)k].push((float)(0.5 * (l + r - before)));
            }
            // the output transducer: 70 Hz high-pass and the chirp-band peak (DRIP)
            l = dripPk[0].peak(outHp[0].highpass(l), dripGain);
            r = dripPk[1].peak(outHp[1].highpass(r), dripGain);
            // width
            const double m = 0.5 * (l + r), sd = 0.5 * (l - r) * set.width;
            l = m + sd; r = m - sd;
            bodyIn[(size_t)i] = (float)(l * 0.7 + (inL ? x * 2.0 : 0.0)); bodyInR[(size_t)i] = (float)(r * 0.7 + (inL ? x * 2.0 : 0.0));
            if (inL) { const double mx = mixS.next(); L[i] = (float)(dryL * (1.0 - mx) + l * mx); R[i] = (float)(dryR * (1.0 - mx) + r * mx); }
            else { L[i] = (float)l; R[i] = (float)r; }
        }
        while (e < nev) handle(ev[e++]);
        // the real tank of the same model around them
        if (set.tank > 0.001 || bodyOn)
        {
            body.process(bodyIn.data(), bodyInR.data(), n);
            bodyOn = set.tank > 0.001 || ++bodyTail < (int)(8.0 * sr / std::max(1, n));
            if (set.tank > 0.001) bodyTail = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t = tankS.next() * (inL ? set.mix : 1.0);
                L[i] += (float)(bodyIn[(size_t)i] * t); R[i] += (float)(bodyInR[(size_t)i] * t);
            }
        }
        for (int i = 0; i < n; ++i) { const double vol = volS.next(); L[i] = (float)(L[i] * vol); R[i] = (float)(R[i] * vol); }
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.reset(); body.recover(); sanitizeBlock(L, n); sanitizeBlock(R, n); }
        // feeds
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const Voice& v = voices[(size_t)i];
            levelFeed[(size_t)i].store(v.isActive() ? (float)v.level() : 0.0f, std::memory_order_relaxed);
            noteFeed[(size_t)i].store(v.isActive() ? v.noteNumber() : -1, std::memory_order_relaxed);
            if (!v.isActive()) continue;
            ++act;
            const int nn = v.noteNumber() >= 1000 ? v.noteNumber() - 1000 : v.noteNumber();
            if (v.isHeld() && nn >= 0 && nn < 128) lit[(nn >> 6) & 1] |= 1ull << (nn & 63);
        }
        const Voice* latest = lastStarted >= 0 ? &voices[(size_t)lastStarted] : nullptr;
        if (latest) { sectFeed.store(latest->sectionsNow()); for (int l = 0; l < 4; ++l) laneFeed[(size_t)l].store((float)latest->lanePeriod(l)); }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }

public:
    // the chord's notes (TWANG): root + octave + intervals
    static int chordNotes(int root, int octave, int chord, int* out)
    {
        static const int iv[kNumChords][5] = {
            { 0, -1 }, { 0, 12, -1 }, { 0, 7, -1 }, { 0, 7, 12, -1 }, { 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 5, 7, -1 },
            { 0, 4, 7, 11, -1 }, { 0, 3, 7, 10, -1 }, { 0, 4, 7, 10, -1 }, { 0, 2, 7, -1 }, { 0, 7, 14, 21, -1 } };
        const int base = 48 + clampv(root, 0, 11) + 12 * octave;
        int n = 0;
        for (int k = 0; k < 5 && iv[clampv(chord, 0, kNumChords - 1)][k] >= 0; ++k) out[n++] = base + iv[clampv(chord, 0, kNumChords - 1)][k];
        return n;
    }

private:
    void configure(bool fresh)
    {
        spring::Settings b;
        b.model = set.model; b.dwell = 50.0; b.decay = clampv(std::max(2.0, set.decay * 1.2), 0.3, 8.0); b.tension = set.tension; b.drip = set.drip;
        b.tone = 0.0; b.lowCut = 100.0; b.preDelay = 0.0; b.wobble = set.wobble * 100.0; b.width = set.width * 100.0; b.grit = set.grit;
        b.mix = 100.0; b.outDb = 0.0;
        body.setSettings(b);
        dripGain = 1.0 + 1.25 * clampv(set.drip, 0.0, 100.0) * 0.01;
        const double fc = spring::transitionHz(set.tension);
        for (int c = 0; c < 2; ++c) dripPk[c].set(0.5 * fc, 0.8, sr);
        if (!fresh) for (auto& v : voices) v.update(set);
        if (fresh && set.resonator) chordVoices();
    }
    double hzOf(int note) const { return synth::midiToHz(note + 12.0 * set.octave + vm.pitchBend() * set.bend); }
    // TWANG's chord (or nothing in MIDI mode): voice i rings note i of the chord, without a kick
    void chordVoices()
    {
        if (!prepared) return;
        if (set.tuning != kTuneChord) { for (auto& v : voices) if (v.isActive() && v.noteNumber() >= 1000) v.release(set); return; }
        int notes[5]; const int n = chordNotes(set.root, set.octave, set.chord, notes);
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t)i];
            if (i < n) { v.start(1000 + notes[i], synth::midiToHz(notes[i]), 0.0, set); lastStarted = i; }
            else if (v.isActive()) v.release(set);
        }
    }
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (set.resonator && set.tuning == kTuneChord && (st == 0x90 || st == 0x80)) return;   // the chord tunes; notes don't
        const bool on = st == 0x90 && m.d2 > 0, off = st == 0x80 || (st == 0x90 && m.d2 == 0);
        // LATCH: a new chord (a key after all were up) replaces the ringing one
        if (on && set.resonator && set.tuning == kTuneLatch && keysDown == 0) for (auto& v : voices) if (v.isActive()) v.release(set);
        if (on) { ++keysDown; lastNote = m.d1 & 0x7F; } else if (off && keysDown > 0) --keysDown;
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            Voice& v = voices[(size_t)clampv(c.voice, 0, kMaxVoices - 1)];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            case synth::VoiceCmd::Glide:
            {
                const double vel = c.velocity >= 0.0f ? c.velocity : 0.8;
                const double strength = set.resonator ? 0.0 : (1.0 - set.velocity * (1.0 - vel)) * 1.2;
                v.start(c.note, hzOf(c.note), strength, set);
                if (!set.resonator && set.tank > 0.001) body.kick(0.4 * strength);
                lastStarted = c.voice;
                break;
            }
            case synth::VoiceCmd::Release:
                if (!(set.resonator && set.tuning == kTuneLatch)) v.release(set);
                break;
            case synth::VoiceCmd::Kill: v.reset(); vm.voiceFinished(c.voice); break;
            }
        });
        if (st == 0xE0) for (auto& v : voices) if (v.isActive() && v.noteNumber() < 1000) v.retune(hzOf(v.noteNumber()), set);
        // voices that have gone silent free their slot
        for (int i = 0; i < kMaxVoices; ++i) if (!voices[(size_t)i].isActive()) vm.voiceFinished(i);
    }

    const double kDrvBias = (double)fastTanh(0.2f);
    Settings set;
    std::array<Voice, kMaxVoices> voices;
    spring::Engine body;
    std::vector<float> bodyIn, bodyInR;
    synth::VoiceManager vm;
    fx::Svf drvHp, outHp[2], dripPk[2];
    double sr = 48000.0, kickReq = 0.0, dripGain = 1.6;
    int lastNote = 48;   // what KICK strikes when nothing rings (C3 until a note is played)
    double envF = 0.0, envS = 0.0, envFk = 0.1, envFr = 0.01, envSk = 0.01, envSr = 0.001;
    int tickPos = 0, lastStarted = -1, keysDown = 0;
    bool prepared = false, bodyOn = false;
    int bodyTail = 0;
    LinearSmoother volS, tankS, mixS;
    std::atomic<int> activeFeed { 0 }, sectFeed { 0 };
    std::array<std::atomic<float>, 4> laneFeed {};
    std::array<std::atomic<float>, kMaxVoices> levelFeed {};
    int decim = 4, decimPos = 0;
    std::array<std::atomic<int>, kMaxVoices> noteFeed {};
    std::atomic<uint64_t> litFeed[2] {};
};

}} // namespace pump::coil
