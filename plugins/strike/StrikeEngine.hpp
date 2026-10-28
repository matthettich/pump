// pump STRIKE engine. Framework-free.
//
// Every pad has two voices: a new hit starts in the free one and the old one fades out in 15 ms (no click, no cut
// tail when it is re-hit slowly; a fast roll overlaps). A voice runs its pad's engine (see StrikeParams.hpp):
//   Modal     16 modes (two-pole resonators: a state turning by the mode's frequency and shrinking by its decay each
//             sample), in the ratios of a membrane, a plate, a bar or a bell; each dies in DECAY / (1 + D (ratio - 1)),
//             D large for wood and small for metal; struck by a half-sine of area 1 (0.3 ms hard to 6 ms soft)
//   String    a delay line of one period (read with linear interpolation, so BEND can move it), a one-pole low-pass
//             (TONE) and up to three all-passes (stiffness) in the loop, the loop's gain set so it dies in DECAY;
//             struck by a click or a burst of noise one period long (CHAR B)
//   Circuit   one (or, with CHAR B, two) resonators pinged by a pulse, through a gentle asymmetric saturation; the
//             pitch rises with the level (CHAR A, the squeeze)
//   FM        carrier <- modulator (at CHAR A's ratio) <- a self-feedback operator (CHAR B); index TONE x 8, dying at
//             a third of DECAY
//   West      a triangle through the folding cell (CHAR A gain, CHAR B bias) into a light-dependent-resistor gate struck
//             for a millisecond: it rings for DECAY and TONE opens its filter
//   Additive  up to 48 sine partials (complex rotations), from a bell's ratios to a cymbal's dense, inharmonic ones,
//             each with its own decay (CHAR B spreads them), tilted by TONE
//   Chaos     ORBIT's Rossler system stepped so one turn is one period, through a band-pass
// Then NOISE: white noise band-passed near 5 kHz, opened by a short burst and by the body's own level (so it rattles
// with it, like snare wires). The pads are summed with their level and pan, then a drive.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "../orbit/OrbitSystems.hpp"
#include "StrikeParams.hpp"
#include <array>
#include <atomic>

namespace pump { namespace strike {

struct PadSettings { int engine = kModal; double pitch = 48, decay = 0.3, tone = 0.5, a = 0.5, b = 0.5, bend = 0, bendTime = 0.03, noise = 0, level = 0, pan = 0; };
struct Settings { std::array<PadSettings, kPads> pad {}; bool choke = true; double velocity = 0.7, drive = 0.1, volumeDb = 0.0; };

constexpr int kModes = 16, kPartials = 48, kMaxDelay = 8192;
// per-engine output gains, so a pad at LEVEL 0 dB is about as loud whatever its engine
static const double kEngineGain[kNumEngines] = { 1.6, 3.0, 0.9, 0.5, 0.9, 0.9, 2.6 };

// the modal objects' ratios
inline const double* objectRatios(int o)
{
    static const double mem[kModes] = { 1.0, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154, 4.601, 4.832, 4.903, 5.131 };
    static const double plate[kModes] = { 1.0, 2.5, 4.0, 5.0, 6.5, 8.5, 9.0, 10.0, 12.5, 13.0, 16.5, 17.0, 18.0, 20.0, 22.5, 25.0 };
    static const double bar[kModes] = { 1.0, 2.756, 5.404, 8.933, 13.344, 18.638, 24.81, 31.87, 39.81, 48.62, 58.32, 68.90, 80.36, 92.69, 105.9, 120.0 };
    static const double bell[kModes] = { 0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215, 9.53, 10.86, 12.18, 13.5 };
    switch (clampv(o, 0, 3)) { case 0: return mem; case 1: return plate; case 2: return bar; default: return bell; }
}
inline double foldTri(double u, double edge)
{
    u = clampv(u, -9.0, 9.0);
    double t = u + 1.0; t -= 4.0 * std::floor(t * 0.25); t = t < 2.0 ? t - 1.0 : 3.0 - t;
    const double w = 0.55 * (1.0 - edge), a = 1.0 - std::fabs(t);
    if (a < w) { const double y = 1.0 - 0.5 * (a * a / w + w); return t < 0.0 ? -y : y; }
    return t;
}

class Engine {
public:
    uint32_t lastHit() const { return lastHitFeed.load(std::memory_order_relaxed); }   // pad | count << 8
    static constexpr int kVoicesPerPad = 2, kCtl = 8;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        (void)orbit::calTable();
        for (int p = 0; p < kPads; ++p) for (int k = 0; k < kVoicesPerPad; ++k)
        {
            Voice& v = voice[(size_t)p][(size_t)k];
            v = Voice();
            v.rng.seed(0x5711u + 131u * (uint32_t)(p * 2 + k));
            v.delay.assign(kMaxDelay, 0.0f);
        }
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        nz.seed(77u);
    }
    void setSettings(const Settings& s) { set = s; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float padLevel(int p) const { return padFeed[(size_t)clampv(p, 0, kPads - 1)].load(std::memory_order_relaxed); }
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    uint32_t takeHits() { return hitFeed.exchange(0u, std::memory_order_relaxed); }

    // a hit on a pad (0..7) at a velocity (0..1)
    void hit(int p, double vel)
    {
        if (p < 0 || p >= kPads) return;
        hitFeed.fetch_or(1u << p, std::memory_order_relaxed);
        noteLastHit(p);
        if (set.choke && p == 5) for (auto& v : voice[6]) if (v.active) v.fade = std::min(v.fade, 1.0), v.fading = true;
        auto& vs = voice[(size_t)p];
        int k = vs[0].active ? (vs[1].active ? (vs[0].age < vs[1].age ? 0 : 1) : 1) : 0;
        for (auto& o : vs) if (o.active && &o != &vs[(size_t)k]) o.fading = true;
        start(vs[(size_t)k], p, vel);
    }
    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        const double dg = 1.0 + 5.0 * set.drive * set.drive;
        float pk[kPads] = {};
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) midi(ev[e++]);
            double l = 0.0, r = 0.0;
            for (int p = 0; p < kPads; ++p)
            {
                const PadSettings& ps = set.pad[(size_t)p];
                double padOut = 0.0;
                for (auto& v : voice[(size_t)p])
                {
                    if (!v.active) continue;
                    double y = sample(v, ps) * v.vel;
                    if (v.fading) { v.fade -= 1.0 / (0.015 * sr); if (v.fade <= 0.0) { v.active = false; continue; } y *= v.fade; }
                    padOut += y;
                }
                if (padOut == 0.0) continue;
                padOut *= dbToGain(ps.level) * kEngineGain[clampv(ps.engine, 0, kNumEngines - 1)];
                pk[p] = std::max(pk[p], (float)std::fabs(padOut));
                const double pan = clampv(ps.pan, -1.0, 1.0);
                l += padOut * std::sqrt(0.5 * (1.0 - pan)); r += padOut * std::sqrt(0.5 * (1.0 + pan));
            }
            if (set.drive > 0.001) { const double c = 1.0 / std::sqrt(dg); l = std::tanh(l * dg) * c; r = std::tanh(r * dg) * c; }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) midi(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& pv : voice) for (auto& v : pv) v.active = false; sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0;
        for (int p = 0; p < kPads; ++p) { padFeed[(size_t)p].store(pk[p]); for (auto& v : voice[(size_t)p]) act += v.active; }
        activeFeed.store(act);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiHit(int pad, uint8_t vel) { hit(pad, vel / 127.0); }
    void uiNote(uint8_t note, uint8_t vel) { if (vel) midi(synth::Midi { 0, 0x90, note, vel }); }

private:
    struct Voice {
        bool active = false, fading = false;
        uint64_t age = 0;
        double vel = 1.0, t = 0.0, fade = 1.0, f0 = 100.0, f = 100.0;
        int ctl = 0;
        // modal / circuit / additive: resonators (complex one-poles)
        double zr[kPartials] = {}, zi[kPartials] = {}, pr[kPartials] = {}, pim[kPartials] = {}, g[kPartials] = {}, ratio[kPartials] = {}, t60[kPartials] = {};
        int nm = 0;
        // string
        std::vector<float> delay;
        int dw = 0;
        double lp = 0.0, ap[3] = {}, apx[3] = {}, loopG = 0.99, burst = 0.0;
        // fm / west / chaos
        double p1 = 0, p2 = 0, p3 = 0, fb = 0, lpr = 0, svf1 = 0, svf2 = 0;
        orbit::State cs;
        orbit::Cal cal {};
        // circuit
        double sq = 0.0;
        // noise
        double follow = 0.0, nb1 = 0.0, nb2 = 0.0;
        fx::Rng rng { 1u };
    };

    void midi(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st != 0x90 || m.d2 == 0) return;
        for (int p = 0; p < kPads; ++p) if (kPadInfo[p].note == m.d1) { hit(p, m.d2 / 127.0); return; }
        // the General MIDI drum notes (the pads' notes before 2026-10-27, so older patterns still play)
        static const int gm[kPads] = { 36, 38, 37, 43, 48, 42, 46, 49 };
        for (int p = 0; p < kPads; ++p) if (gm[p] == m.d1) { hit(p, m.d2 / 127.0); return; }
        // and what is left of the old run of pads up from 60 in semitones: 60, 62, 64 and 66 are the pads' own notes now,
        // so only 61 (snare), 63 (low tom), 65 (closed hat) and 67 (cymbal) still play their old pads
        if (m.d1 >= 61 && m.d1 < 60 + kPads && (m.d1 & 1)) hit(m.d1 - 60, m.d2 / 127.0);
    }

    void start(Voice& v, int p, double vel)
    {
        const PadSettings& s = set.pad[(size_t)p];
        v.active = true; v.fading = false; v.fade = 1.0; v.age = ++counter; v.t = 0.0; v.ctl = 0;
        v.vel = (1.0 - set.velocity) + set.velocity * vel;
        v.f0 = synth::midiToHz(s.pitch);
        v.follow = 0.0; v.nb1 = v.nb2 = 0.0;
        for (int k = 0; k < kPartials; ++k) v.zr[k] = v.zi[k] = 0.0;
        v.p1 = v.p2 = v.p3 = v.fb = 0.0; v.lpr = 0.0; v.svf1 = v.svf2 = 0.0; v.sq = 0.0;
        const int eng = clampv(s.engine, 0, kNumEngines - 1);
        if (eng == kString)
        {
            std::fill(v.delay.begin(), v.delay.end(), 0.0f);
            v.dw = 0; v.lp = 0.0; for (int k = 0; k < 3; ++k) v.ap[k] = v.apx[k] = 0.0;
            // the excitation: a period of noise (coloured by TONE) mixed with a click, written into the line
            const int N = clampv((int)(sr / v.f0), 2, kMaxDelay - 8);
            double c = 0.0;
            for (int i = 0; i < N; ++i)
            {
                c += (v.rng.bi() - c) * (0.2 + 0.8 * s.tone);
                const double click = std::exp(-i * 12.0 / N) * (i < N / 2 ? 1.0 : -0.6);
                v.delay[(size_t)i] = (float)(s.b * c * 0.9 + (1.0 - s.b) * click);
            }
            v.dw = N;
        }
        if (eng == kAdditive) setupAdditive(v, s);
        if (eng == kChaos) { v.cal = orbit::calAt(orbit::kRossler, s.a); v.cs = v.cal.on; }
        control(v, s);
    }
    void setupAdditive(Voice& v, const PadSettings& s)
    {
        static const double bellR[kPartials] = { 1.0, 2.0, 2.4, 3.0, 4.2, 5.4, 6.8, 8.2, 9.5, 10.9, 12.2, 13.6 };
        const double a = clampv(s.a, 0.0, 1.0);
        const int n = 6 + (int)(42 * a);
        v.nm = n;
        for (int k = 0; k < n; ++k)
        {
            // a dense, inharmonic set (fixed per partial) spread over about 1x .. 9x
            const double h = std::fmod(std::sin(k * 12.9898 + 4.1) * 43758.5453, 1.0);
            const double u = std::fabs(h);
            const double cym = std::exp(std::log(9.0) * (0.03 + 0.97 * std::pow((k + u) / n, 0.8)));
            const double bell = k < 12 ? bellR[k] : bellR[11] + (k - 11) * 1.33;
            v.ratio[k] = bell + (cym - bell) * a;
            const double sh = 1.0 + clampv(s.b, 0.0, 1.0) * 1.6 * (u - 0.5);
            v.t60[k] = std::max(0.01, s.decay * sh / (1.0 + 0.15 * (v.ratio[k] - 1.0)));
            v.g[k] = std::pow(v.ratio[k], 1.8 * s.tone - 1.4) * (0.6 + 0.4 * u);
            const double ph = kTwoPi * u * 7.0;
            v.zr[k] = v.g[k] * std::cos(ph); v.zi[k] = v.g[k] * std::sin(ph);
        }
        double nrm = 0.0; for (int k = 0; k < n; ++k) nrm += v.g[k] * v.g[k];
        const double sc = 0.5 / std::sqrt(std::max(1e-9, nrm));
        for (int k = 0; k < n; ++k) { v.zr[k] *= sc; v.zi[k] *= sc; }
    }

    // pitch (with the bend) and the resonators, every kCtl samples
    void control(Voice& v, const PadSettings& s)
    {
        const int eng = clampv(s.engine, 0, kNumEngines - 1);
        const double bendEnv = s.bendTime > 0.0 ? std::exp(-v.t / std::max(0.001, s.bendTime)) : 0.0;
        v.f = v.f0 * std::exp2(s.bend * bendEnv / 12.0);
        if (eng == kModal)
        {
            const int obj = std::min(3, (int)(clampv(s.a, 0.0, 1.0) * 4.0));
            const double* R = objectRatios(obj);
            const double D = 3.0 * std::pow(1.0 - clampv(s.b, 0.0, 1.0), 2.0) + 0.004;
            int n = 0; double nrm = 0.0;
            for (int m = 0; m < kModes; ++m)
            {
                const double f = v.f * R[m];
                if (f > 0.45 * sr) break;
                const double t60 = std::max(0.005, s.decay) / (1.0 + D * std::max(0.0, R[m] - 1.0));
                const double rad = std::exp(-6.9078 / (t60 * sr)), w = kTwoPi * f / sr;
                v.pr[m] = rad * std::cos(w); v.pim[m] = rad * std::sin(w);
                const double a = std::fabs(std::sin(kPi * (m + 1) * 0.31)) / std::pow(m + 1.0, 0.6) + 0.05;
                v.g[m] = a; nrm += a * a; ++n;
            }
            for (int m = 0; m < n; ++m) v.g[m] *= 0.5 / std::sqrt(std::max(1e-9, nrm));
            for (int m = n; m < kModes; ++m) v.zr[m] = v.zi[m] = 0.0;
            v.nm = n;
        }
        else if (eng == kCircuit)
        {
            // the squeeze: the pitch rises with the level (taken from the first resonator's amplitude)
            const double lv = std::sqrt(v.zr[0] * v.zr[0] + v.zi[0] * v.zi[0]);
            const double f1 = v.f * (1.0 + 0.8 * s.a * std::min(1.5, lv * 2.0));
            const double t60 = std::max(0.005, s.decay);
            const double fs[2] = { f1, f1 * 1.48 };
            for (int m = 0; m < 2; ++m)
            {
                const double rad = std::exp(-6.9078 / (t60 * (m ? 0.8 : 1.0) * sr)), w = kTwoPi * std::min(fs[m], 0.45 * sr) / sr;
                v.pr[m] = rad * std::cos(w); v.pim[m] = rad * std::sin(w);
            }
            v.nm = 2;
        }
        else if (eng == kAdditive)
        {
            for (int k = 0; k < v.nm; ++k)
            {
                const double f = v.f * v.ratio[k];
                const double rad = f < 0.45 * sr ? std::exp(-6.9078 / (v.t60[k] * sr)) : 0.0, w = kTwoPi * std::min(f, 0.45 * sr) / sr;
                v.pr[k] = rad * std::cos(w); v.pim[k] = rad * std::sin(w);
            }
        }
        else if (eng == kString)
        {
            // the loop's gain: down 60 dB in DECAY
            v.loopG = std::pow(10.0, -3.0 / (std::max(0.01, s.decay) * v.f));
        }
    }

    inline double sample(Voice& v, const PadSettings& s)
    {
        const int eng = clampv(s.engine, 0, kNumEngines - 1);
        if (++v.ctl >= kCtl) { v.ctl = 0; control(v, s); }
        const double t = v.t;
        v.t += 1.0 / sr;
        double y = 0.0;
        switch (eng)
        {
        case kModal:
        {
            const double len = 0.0003 + std::pow(1.0 - clampv(s.tone, 0.0, 1.0), 2.0) * 0.0057;
            const double x = t < len ? std::sin(kPi * t / len) * kPi * 0.5 / (len * sr) : 0.0;
            for (int m = 0; m < v.nm; ++m)
            {
                const double zr = v.zr[m] * v.pr[m] - v.zi[m] * v.pim[m] + v.g[m] * x;
                const double zi = v.zr[m] * v.pim[m] + v.zi[m] * v.pr[m];
                v.zr[m] = zr; v.zi[m] = zi; y += zi;
            }
            break;
        }
        case kString:
        {
            // read one period back (fractional, so the bend can move it), the loop filter, the all-passes
            const double stiff = clampv(s.a, 0.0, 1.0);
            const int nap = stiff > 0.01 ? 3 : 0;
            const double apc = -0.65 * stiff;
            const double apDelay = nap * (1.0 - apc) / (1.0 + apc);   // their delay at low frequencies, in samples
            const double lpc = 0.15 + 0.84 * clampv(s.tone, 0.0, 1.0);
            const double lpDelay = (1.0 - lpc) / lpc;
            const double D = clampv(sr / v.f - apDelay - lpDelay, 2.0, (double)kMaxDelay - 4.0);
            double rp = v.dw - D; while (rp < 0.0) rp += kMaxDelay;
            const int i0 = (int)rp; const double fr = rp - i0;
            const double a = v.delay[(size_t)(i0 & (kMaxDelay - 1))], b = v.delay[(size_t)((i0 + 1) & (kMaxDelay - 1))];
            double x = a + (b - a) * fr;
            v.lp += (x - v.lp) * lpc;
            x = v.lp;
            for (int k = 0; k < nap; ++k) { const double o = apc * x + v.apx[k] - apc * v.ap[k]; v.apx[k] = x; v.ap[k] = o; x = o; }
            x *= v.loopG;
            v.delay[(size_t)(v.dw & (kMaxDelay - 1))] = (float)x;
            ++v.dw;
            y = x;
            break;
        }
        case kCircuit:
        {
            // a pulse (half-sine, 0.2 ms bright to 1.5 ms dull) into the resonators
            const double len = 0.0002 + (1.0 - clampv(s.tone, 0.0, 1.0)) * 0.0013;
            const double x = t < len ? std::sin(kPi * t / len) * kPi * 0.5 / (len * sr) : 0.0;
            double out = 0.0;
            for (int m = 0; m < 2; ++m)
            {
                const double gin = m ? clampv(s.b, 0.0, 1.0) : 1.0;
                const double zr = v.zr[m] * v.pr[m] - v.zi[m] * v.pim[m] + gin * x * 0.6;
                const double zi = v.zr[m] * v.pim[m] + v.zi[m] * v.pr[m];
                v.zr[m] = zr; v.zi[m] = zi;
                out += zi * (m ? 1.0 : 1.0 - 0.4 * s.b);
            }
            // the circuit's asymmetric saturation (and, with the second resonator, the hard edge of a cowbell)
            const double dr = 1.0 + 3.0 * s.b;
            y = (std::tanh(dr * out + 0.15) - std::tanh(0.15)) / dr;
            break;
        }
        case kFm:
        {
            const double ie = std::exp(-6.9078 * t / std::max(0.005, s.decay * 0.33)), ae = std::exp(-6.9078 * t / std::max(0.005, s.decay));
            const double ratio = 0.5 + 7.5 * s.a * s.a;
            v.p3 += v.f * ratio * 3.17 / sr; v.p3 -= std::floor(v.p3);
            const double o3 = std::sin(kTwoPi * v.p3 + 1.2 * s.b * v.fb); v.fb = o3;
            v.p2 += v.f * ratio / sr; v.p2 -= std::floor(v.p2);
            const double o2 = std::sin(kTwoPi * v.p2 + 3.0 * s.b * ie * o3);
            v.p1 += v.f / sr; v.p1 -= std::floor(v.p1);
            y = std::sin(kTwoPi * v.p1 + 8.0 * s.tone * ie * o2) * ae;
            break;
        }
        case kWest:
        {
            v.p1 += v.f / sr; v.p1 -= std::floor(v.p1);
            const double tri = v.p1 < 0.25 ? 4.0 * v.p1 : (v.p1 < 0.75 ? 2.0 - 4.0 * v.p1 : 4.0 * v.p1 - 4.0);
            const double g = 0.4 + 8.0 * s.a;
            const double fy = foldTri(tri * g + s.b, 0.3) / std::min(1.0, g);
            // the gate: struck for a millisecond, letting go over DECAY (slower as it darkens)
            const double ctlv = t < 0.001 ? 1.0 : 0.0;
            if (ctlv > v.lpr) v.lpr += (ctlv - v.lpr) * 0.08;
            else v.lpr -= v.lpr * (6.9078 / (std::max(0.01, s.decay) * sr)) * (0.4 + 1.5 * v.lpr);
            const double fc = 40.0 * std::exp2((6.0 + 3.5 * s.tone) * std::pow(clampv(v.lpr, 0.0, 1.0), 0.8));
            const double gg = std::tan(kPi * std::min(fc, 0.45 * sr) / sr), k = 1.6;
            const double a1 = 1.0 / (1.0 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            const double v3 = fy - v.svf2, v1 = a1 * v.svf1 + a2 * v3, v2 = v.svf2 + a2 * v.svf1 + a3 * v3;
            v.svf1 = 2.0 * v1 - v.svf1; v.svf2 = 2.0 * v2 - v.svf2;
            y = v2 * std::pow(clampv(v.lpr, 0.0, 1.0), 1.4);
            break;
        }
        case kAdditive:
        {
            for (int k = 0; k < v.nm; ++k)
            {
                const double zr = v.zr[k] * v.pr[k] - v.zi[k] * v.pim[k];
                const double zi = v.zr[k] * v.pim[k] + v.zi[k] * v.pr[k];
                v.zr[k] = zr; v.zi[k] = zi; y += zi;
            }
            break;
        }
        default:
        {
            // Rossler at the pitch, through a band-pass
            const double dts = v.cal.turn * v.f / sr;
            const int sub = clampv((int)std::ceil(dts / (v.cal.turn / 40.0)), 1, 6);
            const double prm = orbit::sysParam(orbit::kRossler, s.a);
            for (int k = 0; k < sub; ++k) orbit::rk4(orbit::kRossler, v.cs, prm, dts / sub);
            if (!(std::fabs(v.cs.x) < 1e4) || !std::isfinite(v.cs.x)) v.cs = v.cal.on;
            const double raw = (v.cs.x - v.cal.mean[0]) / v.cal.sd[0] * 0.4;
            const double fc = 200.0 * std::exp2(6.5 * s.tone), q = 0.7 + 10.0 * s.b;
            const double gg = std::tan(kPi * std::min(fc, 0.45 * sr) / sr), k = 1.0 / q;
            const double a1 = 1.0 / (1.0 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            const double v3 = raw - v.svf2, v1 = a1 * v.svf1 + a2 * v3, v2 = v.svf2 + a2 * v.svf1 + a3 * v3;
            v.svf1 = 2.0 * v1 - v.svf1; v.svf2 = 2.0 * v2 - v.svf2;
            y = (v1 * k * (0.5 + 0.5 * s.b) + raw * 0.5 * (1.0 - s.b)) * std::exp(-6.9078 * t / std::max(0.005, s.decay));
            break;
        }
        }
        // the noise: a short burst, then rattling with the body
        if (s.noise > 0.0)
        {
            const double a = std::fabs(y);
            v.follow = a > v.follow ? v.follow + (a - v.follow) * 0.05 : v.follow * (1.0 - 1.0 / (0.03 * sr));
            const double w = v.rng.bi();
            // a band-pass near 5 kHz (two one-poles: a high-pass then a low-pass)
            v.nb1 += (w - v.nb1) * 0.35; const double hp = w - v.nb1;
            v.nb2 += (hp - v.nb2) * 0.6;
            const double burst = std::exp(-t / 0.012);
            y += v.nb2 * s.noise * (0.35 * burst + 2.0 * v.follow);
        }
        // done?
        if (v.t > 0.02 && ((v.ctl == 0 && quiet(v, eng, y)) || v.t > std::max(0.2, s.decay * 4.0 + s.bendTime))) v.active = false;
        return y;
    }
    bool quiet(const Voice& v, int eng, double y) const
    {
        if (std::fabs(y) > 1e-4) return false;
        switch (eng)
        {
        case kModal: case kCircuit: case kAdditive: { double e = 0.0; for (int m = 0; m < v.nm; ++m) e += v.zr[m] * v.zr[m] + v.zi[m] * v.zi[m]; return e < 1e-9; }
        case kString: { double e = 0.0; const int N = std::min(kMaxDelay, (int)(sr / std::max(20.0, v.f)) + 2); for (int i = 0; i < N; ++i) e = std::max(e, (double)std::fabs(v.delay[(size_t)((v.dw - 1 - i) & (kMaxDelay - 1))])); return e < 1e-4; }
        case kWest: return v.lpr < 2e-3;
        default: return false;
        }
    }

    Settings set;
    double sr = 48000.0;
    std::array<std::array<Voice, kVoicesPerPad>, kPads> voice {};
    uint64_t counter = 0;
    LinearSmoother volS;
    fx::Rng nz { 1u };
    std::array<std::atomic<float>, kPads> padFeed {};
    std::atomic<int> activeFeed { 0 };
    std::atomic<uint32_t> hitFeed { 0u };
    // the last pad hit (by MIDI or the window): pad | count << 8, for the window to follow
    std::atomic<uint32_t> lastHitFeed { 0u };
    void noteLastHit(int p) { lastHitFeed.store((uint32_t)p | (((lastHitFeed.load(std::memory_order_relaxed) >> 8) + 1u) << 8), std::memory_order_relaxed); }
};

}} // namespace pump::strike
