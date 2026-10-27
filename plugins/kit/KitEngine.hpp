// pump KIT engine: a synthesized drum kit. Framework-free.
//
// Eight pads; each is one of eight voice models (see KitParams.hpp) with the same twelve controls. A hit takes a
// snapshot of its pad's settings and plays to its end; a new hit on the same pad takes the pad's other voice and the
// old one fades out in 4 ms (no clicks, tails don't stack). The closed hat chokes the open hat. Pads answer the
// General MIDI drum notes (toms on 41 43 45 47 48 50 at their own pitches, cymbals on 49 51 52 55 57). VELOCITY sets
// how much a hit's velocity changes its level (and brightens the click); HUMANIZE varies every hit's pitch, length
// and level a little. Per pad: drive, level and pan; then the kit's drive and volume.
#pragma once

#include "dsp/Synth.hpp"
#include "KitParams.hpp"
#include <array>

namespace pump { namespace kit {

struct PadSettings { int model = kKick; double tune = 0, decay = 0.4, pitch = 0, pdecay = 0.04, tone = 0.5, noise = 0, click = 0, chr = 0.5, drive = 0, levelDb = 0, pan = 0; };
struct Settings {
    PadSettings pad[kPads];
    double kitDrive = 0.0, velocity = 0.6, humanize = 0.0, volumeDb = -6.0;
};

// which pad a General MIDI note plays, and at what pitch offset (semitones); -1 = none
inline int padForNote(int note, double& offset)
{
    offset = 0.0;
    switch (note)
    {
    case 35: case 36: return 0;
    case 38: case 40: return 1;
    case 39: return 2;
    case 42: case 44: return 3;
    case 46: return 4;
    case 41: offset = -5; return 5;
    case 43: offset = -3; return 5;
    case 45: return 5;
    case 47: offset = 2; return 5;
    case 48: offset = 5; return 5;
    case 50: offset = 7; return 5;
    case 37: return 6;
    case 56: offset = 5; return 6;
    case 49: case 57: return 7;
    case 51: case 59: offset = -2; return 7;
    case 52: case 55: offset = 1; return 7;
    default: return -1;
    }
}
inline int noteForPad(int pad) { return kPadInfo[clampv(pad, 0, kPads - 1)].note; }

class Engine {
public:
    uint32_t lastHit() const { return lastHitFeed.load(std::memory_order_relaxed); }   // pad | count << 8
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        fadeStep = 1.0 / std::max(1.0, 0.004 * sr);
    }
    void setSettings(const Settings& s) { set = s; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    float padLevel(int p) const { return padFeed[(size_t)clampv(p, 0, kPads - 1)].load(std::memory_order_relaxed); }

    // play a pad now (the UI's pads), velocity 0..1
    void hit(int pad, double vel, double offset = 0.0)
    {
        if (pad < 0 || pad >= kPads) return;
        noteLastHit(pad);
        if (pad == 3) for (auto& v : voices) if (v.active && v.pad == 4 && v.fade >= 1.0) v.fadeOut = true;   // the closed hat chokes the open one
        // the pad's previous voice fades; the other slot plays
        Voice* slot = nullptr;
        for (int k = 0; k < 2; ++k) { Voice& v = voices[(size_t)(pad * 2 + k)]; if (v.active) v.fadeOut = true; else if (!slot) slot = &v; }
        if (!slot) { Voice& a = voices[(size_t)pad * 2], &b = voices[(size_t)pad * 2 + 1]; slot = a.fade < b.fade ? &a : &b; }
        start(*slot, pad, clampv(vel, 0.0, 1.0), offset);
    }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const double kd = set.kitDrive > 0.001 ? 1.0 + set.kitDrive * set.kitDrive * 9.0 : 0.0, kdn = kd > 0.0 ? 1.0 / std::tanh(kd) : 1.0;
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            double l = 0.0, r = 0.0;
            for (auto& v : voices)
            {
                if (!v.active) continue;
                double y = voiceSample(v);
                if (v.fadeOut) { v.fade -= fadeStep; if (v.fade <= 0.0) { v.active = false; continue; } y *= v.fade; }
                l += y * v.gl; r += y * v.gr;
            }
            l *= 0.5; r *= 0.5;   // headroom: a full kit's hits add up
            if (kd > 0.0) { l = std::tanh(l * kd) * kdn; r = std::tanh(r * kd) * kdn; }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) v.active = false; sanitizeBlock(L, n); sanitizeBlock(R, n); }
        float lv[kPads] = {};
        for (const auto& v : voices) if (v.active) lv[v.pad] = std::max(lv[v.pad], (float)(v.amp * v.fade));
        for (int p = 0; p < kPads; ++p) padFeed[(size_t)p].store(lv[p], std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    int voicesNow() const { int c = 0; for (const auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    struct Voice {
        bool active = false, fadeOut = false;
        int pad = 0;
        PadSettings p;
        double fade = 1.0, amp = 0.0, ampK = 0.999, atk = 1.0, atkInc = 1.0, body = 0.0, bodyK = 0.999, nz = 0.0, nzK = 0.999;
        double pe = 1.0, peK = 0.999, click = 0.0, clickK = 0.99, vel = 1.0, gain = 1.0, gl = 1.0, gr = 1.0, f0 = 100.0;
        double ph[6] = {}, mph = 0.0, t = 0.0, burstGap = 0.01, driveK = 0.0, driveN = 1.0, lastTone = -1.0;
        fx::Svf svf, svf2;
        fx::OnePole lp, hp, clickHp;
        fx::Rng rng { 1u };
    };

    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st == 0x90 && m.d2 > 0)
        {
            double off = 0.0;
            const int pad = padForNote(m.d1, off);
            if (pad >= 0) hit(pad, m.d2 / 127.0, off);
        }
        else if (st == 0xB0 && (m.d1 == 120 || m.d1 == 123)) for (auto& v : voices) if (v.active) v.fadeOut = true;
    }
    double coef(double seconds) const { return std::exp(std::log(1e-3) / std::max(1.0, seconds * sr)); }   // -60 dB in `seconds`
    double tauK(double seconds) const { return std::exp(-1.0 / std::max(1.0, seconds * sr)); }

    void start(Voice& v, int pad, double vel, double offset)
    {
        const PadSettings& p = set.pad[pad];
        v = Voice();
        v.active = true; v.pad = pad; v.p = p; v.vel = vel;
        v.rng = fx::Rng((uint32_t)(0x1234u + 7919u * (uint32_t)(++hits)));
        // humanize: a little pitch, length and level per hit
        const double h = set.humanize;
        const double dTune = h * 0.35 * rng.bi(), dDecay = 1.0 + h * 0.18 * rng.bi(), dLevel = h * 2.0 * rng.bi();
        v.p.tune += offset + dTune; v.p.decay *= dDecay;
        const double velGain = 1.0 - set.velocity * (1.0 - vel * vel);
        v.gain = dbToGain(p.levelDb + dLevel) * velGain;
        const double pan = clampv(p.pan, -1.0, 1.0);
        v.gl = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356; v.gr = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
        v.amp = 1.0; v.ampK = coef(v.p.decay);
        v.atk = 0.0; v.atkInc = 1.0 / std::max(1.0, 0.0005 * sr);
        v.pe = 1.0; v.peK = tauK(v.p.pdecay);
        v.click = 0.5 + 0.5 * vel; v.clickK = tauK(0.0025);
        v.clickHp.set(1500.0, sr);
        v.driveK = p.drive > 0.001 ? 1.0 + p.drive * p.drive * 10.0 : 0.0; v.driveN = v.driveK > 0.0 ? 1.0 / std::tanh(v.driveK) : 1.0;
        const double tr = std::exp2(v.p.tune / 12.0);
        switch (p.model)
        {
        case kKick: v.f0 = 50.0 * tr; v.lp.set(toneLp(p.tone), sr); v.hp.set(3000.0, sr); break;
        case kSnare:
            v.f0 = 180.0 * tr; v.body = 1.0; v.bodyK = coef(v.p.decay * 0.55); v.nz = 1.0; v.nzK = coef(v.p.decay * (0.35 + p.chr * 1.3));
            v.hp.set(200.0 * std::exp2(p.tone * 5.3), sr); break;
        case kClap: v.f0 = 300.0 * std::exp2(p.tone * 4.0) * tr; v.burstGap = (0.006 + p.chr * 0.014); v.hp.set(2000.0, sr); break;
        case kHat: v.f0 = 205.3 * tr; v.hp.set(5500.0 * std::exp2((p.tone - 0.5) * 1.5), sr); break;
        case kMetal: v.f0 = 150.0 * tr; v.hp.set(400.0 * std::exp2(p.tone * 3.0), sr); break;
        case kTom: v.f0 = 100.0 * tr; v.lp.set(toneLp(p.tone), sr); v.hp.set(3000.0, sr); break;
        case kFm: v.f0 = 200.0 * tr; v.lp.set(toneLp(p.tone), sr); break;
        default: v.f0 = 100.0 * std::exp2(p.tone * 8.0) * tr; break;
        }
        if (p.model == kKick || p.model == kTom || p.model == kSnare)
            for (double& x : v.ph) x = 0.0;
        else for (int k = 0; k < 6; ++k) v.ph[k] = v.rng.uni();
    }
    static double toneLp(double t) { return 80.0 * std::exp2(clampv(t, 0.0, 1.0) * 8.0); }
    static double sine(double ph) { return std::sin(kTwoPi * ph); }

    // the six square partials of the hat and metal models
    double squares(Voice& v, double base, double spread)
    {
        static const double ratio[6] = { 1.0, 1.4823, 1.8023, 2.5461, 2.6302, 3.8971 };
        double s = 0.0;
        for (int k = 0; k < 6; ++k)
        {
            const double f = base * (1.0 + (ratio[k] - 1.0) * spread), inc = std::min(0.45, f / sr);
            s += synth::blepPulse(v.ph[k], inc, 0.5);
            v.ph[k] += inc; if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
        }
        return s * (1.0 / 6.0);
    }

    double voiceSample(Voice& v)
    {
        const PadSettings& p = v.p;
        const double wn = v.rng.bi();
        double y = 0.0;
        bool own = false;   // the model applied its own envelope (snare, clap)
        const double pitchMul = std::exp2(p.pitch * v.pe / 12.0);
        switch (p.model)
        {
        case kKick:
        {
            const double f = v.f0 * pitchMul;
            double s = sine(v.ph[0]);
            v.ph[0] += f / sr; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            if (p.chr > 0.001) { const double g = 1.0 + p.chr * 6.0; s = std::tanh(s * g) / std::tanh(g); }
            y = s + p.noise * 0.3 * v.hp.lp(wn);
            y = v.lp.lp(y);
            break;
        }
        case kSnare:
        {
            const double f = v.f0 * pitchMul;
            const double b = 0.6 * sine(v.ph[0]) + 0.4 * sine(v.ph[1]);
            v.ph[0] += f / sr; v.ph[1] += f * 1.56 / sr;
            for (int k = 0; k < 2; ++k) if (v.ph[k] >= 1.0) v.ph[k] -= 1.0;
            v.body *= v.bodyK; v.nz *= v.nzK;
            const double nz = v.hp.hp(wn);
            y = b * v.body * (1.0 - 0.6 * p.noise) + nz * v.nz * p.noise * 1.2;
            own = true;
            break;
        }
        case kClap:
        {
            // three quick bursts, then the tail
            const double t = v.t, g = v.burstGap;
            double env = 0.0;
            for (int k = 0; k < 3; ++k) { const double dt = t - k * g; if (dt >= 0.0) env = std::max(env, std::exp(-dt / 0.0025)); }
            const double tail = t >= 2.0 * g ? 1.0 : 0.0;
            const double fc = v.f0 * pitchMul;
            if (std::fabs(fc - v.lastTone) > 1.0) { v.svf.set(fc, 1.8, sr); v.lastTone = fc; }
            const double band = v.svf.bandpass(wn), air = v.hp.hp(wn);
            const double x = band * (1.0 - 0.4 * p.noise) + air * p.noise * 0.6;
            y = x * (tail * v.amp * 0.7 + env * 1.2) * 1.6;
            own = true;
            break;
        }
        case kHat:
        case kMetal:
        {
            const bool hat = p.model == kHat;
            const double spread = 0.4 + p.chr * 1.2;
            const double m = squares(v, v.f0 * pitchMul, spread);
            const double x = m * (1.0 - 0.7 * p.noise) + wn * p.noise * 0.7;
            const double fc = (hat ? 2500.0 * std::exp2(p.tone * 2.6) : 800.0 * std::exp2(p.tone * 3.4)) * pitchMul;
            if (std::fabs(fc - v.lastTone) > 1.0) { v.svf.set(fc, hat ? 1.2 : 1.6, sr); v.lastTone = fc; }
            y = v.hp.hp(v.svf.bandpass(x)) * (hat ? 1.8 : 1.5);
            break;
        }
        case kTom:
        {
            const double f = v.f0 * pitchMul;
            const double ph = v.ph[0];
            const double tri = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
            const double s = sine(ph) * (1.0 - p.chr) + tri * p.chr;
            v.ph[0] += f / sr; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            y = v.lp.lp(s + p.noise * 0.25 * v.hp.lp(wn));
            break;
        }
        case kFm:
        {
            // PITCH is the index here, falling with PITCH DECAY; CHAR the modulator's ratio (0.5 .. 8)
            const double ratio = 0.5 * std::exp2(p.chr * 4.0), index = (p.pitch / 48.0) * 12.0 * v.pe + 0.2;
            const double mod = sine(v.mph);
            v.mph += v.f0 * ratio / sr; if (v.mph >= 1.0) v.mph -= std::floor(v.mph);
            const double s = sine(v.ph[0] + index * mod / kTwoPi);
            v.ph[0] += v.f0 / sr; if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            y = v.lp.lp(s + p.noise * 0.3 * wn);
            break;
        }
        default:
        {
            const double fc = clampv(v.f0 * pitchMul, 20.0, 0.45 * sr);
            if (std::fabs(fc - v.lastTone) > 0.5) { v.svf.set(fc, 0.7 + p.chr * p.chr * 20.0, sr); v.lastTone = fc; }
            y = v.svf.bandpass(wn) * (1.0 + p.chr) * (1.0 - p.noise) + wn * p.noise * 0.5;
            break;
        }
        }
        // the click: a short high-passed noise and pulse transient at the start
        if (p.click > 0.001 && v.click > 1e-4)
        {
            // a rounded half-millisecond pulse plus high-passed noise
            const double tp = v.t * 2000.0, pulse = tp < 1.0 ? std::sin(kPi * tp) * 0.35 : 0.0;
            y += p.click * v.click * (v.clickHp.hp(wn) * 0.7 + pulse);
            v.click *= v.clickK;
        }
        if (v.atk < 1.0) v.atk = std::min(1.0, v.atk + v.atkInc);
        y *= (own ? 1.0 : v.amp) * v.atk;
        v.amp *= v.ampK;
        v.pe *= v.peK;
        v.t += 1.0 / sr;
        if (v.driveK > 0.0) y = std::tanh(y * v.driveK) * v.driveN;
        y *= v.gain;
        const bool done = p.model == kSnare ? (v.body < 1e-4 && v.nz < 1e-4) : p.model == kClap ? (v.amp < 1e-4 && v.t > 3.0 * v.burstGap + 0.02) : v.amp < 1e-4;
        if (done && v.click < 1e-4) v.active = false;
        return y;
    }

    Settings set;
    std::array<Voice, kPads * 2> voices {};
    fx::Rng rng { 0xD0D0u };
    double sr = 48000.0, fadeStep = 0.005;
    uint32_t hits = 0;
    LinearSmoother volS;
    std::array<std::atomic<float>, kPads> padFeed {};
    // the last pad hit (by MIDI or the window): pad | count << 8, for the window to follow
    std::atomic<uint32_t> lastHitFeed { 0u };
    void noteLastHit(int p) { lastHitFeed.store((uint32_t)p | (((lastHitFeed.load(std::memory_order_relaxed) >> 8) + 1u) << 8), std::memory_order_relaxed); }
};

}} // namespace pump::kit
