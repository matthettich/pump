// pump MODAL engine: modal synthesis. Framework-free.
//
// A voice is a bank of resonant modes: each a two-pole resonator (a complex one-pole: the state turns by the mode's
// frequency each sample and shrinks by its decay), so it rings as a decaying sine when struck and swells to a
// steady tone when driven. Ratios come from the object (tables below, the classic analyses of bars, membranes,
// plates, bells and glass); STRETCH bends them (ratio^(1 +- 0.3)); every mode's level is the object's, times the
// strike POSITION's effect (sin(pi m x) for a string; the other objects use their mode number the same way, a fair
// stand-in), tilted by BRIGHT. Each mode decays in DECAY / (1 + D (ratio - 1)), D large for wood, small for metal
// (MATERIAL). Key up: DAMP (the time the modes then die in). Modes above Nyquist are left out.
// Exciters: MALLET a half-sine pulse 0.4 ms (hard) to 6 ms (soft) long, with a little noise; PLUCK a noise burst
// whose length and colour follow HARDNESS; BOW stick-slip pulses at the note (with jitter) and friction noise, held
// at PRESSURE while the key is down; BREATH noise around the note's pitch, with flutter. A driven exciter's input is
// scaled by each mode's (1 - r), so a long-decay object doesn't build up without bound.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "ModalParams.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace pump { namespace modal {

constexpr int kMaxModes = 48;

struct Settings {
    int model = kMarimba, modes = 24, exciter = kMallet, voices = 8, voiceMode = 0, octave = 0;
    double material = 0.3, decay = 1.5, damp = 1.0, bright = 0.0, stretch = 0.0, position = 0.22;
    double hardness = 0.55, noise = 0.1, pressure = 0.6, vibrato = 0.0, vibRate = 5.0, velocity = 0.7, spread = 0.5, glide = 0.0, volumeDb = 0.0;
};

// the objects' mode ratios (to the first) and levels
struct ModelTable { double ratio[kMaxModes]; double level[kMaxModes]; };
inline const ModelTable& modelTable(int m)
{
    static const std::array<ModelTable, kNumModels> T = [] {
        std::array<ModelTable, kNumModels> t {};
        for (int k = 0; k < kMaxModes; ++k)
        {
            const double n = k + 1;
            // free-free bar: (2n + 1)^2 / 9 beyond the first few measured ones
            static const double bar[6] = { 1.0, 2.756, 5.404, 8.933, 13.344, 18.638 };
            t[kBar].ratio[k] = k < 6 ? bar[k] : std::pow(2.0 * n + 1.0, 2.0) / 9.0;
            t[kBar].level[k] = 1.0 / std::pow(n, 0.9);
            // a marimba bar, undercut to 1 : 4 : 10, then bar-like
            static const double mar[5] = { 1.0, 3.99, 10.07, 17.5, 26.6 };
            t[kMarimba].ratio[k] = k < 5 ? mar[k] : mar[4] * std::pow((n) / 5.0, 2.0);
            t[kMarimba].level[k] = k == 0 ? 1.0 : 0.5 / std::pow(n, 1.1);
            t[kString].ratio[k] = n; t[kString].level[k] = 1.0 / n;
            // circular membrane (ideal): the Bessel zeros over the first
            static const double mem[16] = { 1.0, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154, 4.601, 4.832, 4.903, 5.131 };
            t[kMembrane].ratio[k] = k < 16 ? mem[k] : mem[15] + (k - 15) * 0.32;
            t[kMembrane].level[k] = 1.0 / std::pow(n, 0.6);
            // square plate: sqrt of (m^2 + n^2) combinations, normalised to the lowest
            {
                static double pl[kMaxModes]; static bool built = false;
                if (!built)
                {
                    std::vector<double> v;
                    for (int a = 1; a <= 10; ++a) for (int b = 1; b <= 10; ++b) v.push_back((a * a + b * b) / 2.0);
                    std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end());
                    for (int i = 0; i < kMaxModes; ++i) pl[i] = v[(size_t)i];
                    built = true;
                }
                t[kPlate].ratio[k] = pl[k]; t[kPlate].level[k] = 1.0 / std::pow(n, 0.7);
            }
            // a church bell: hum, prime, tierce (minor third), quint, nominal, and up
            static const double bell[12] = { 0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215 };
            t[kBell].ratio[k] = k < 12 ? bell[k] : bell[11] + (k - 11) * 1.32;
            t[kBell].level[k] = k < 5 ? 0.7 + 0.3 * (k == 4) : 0.6 / std::pow(n - 4.0, 0.8);
            t[kTube].ratio[k] = 2.0 * n - 1.0; t[kTube].level[k] = 1.0 / (2.0 * n - 1.0);
            static const double gl[8] = { 1.0, 2.32, 4.25, 6.63, 9.38, 12.6, 16.2, 20.1 };
            t[kGlass].ratio[k] = k < 8 ? gl[k] : gl[7] + (k - 7) * 4.3;
            t[kGlass].level[k] = 1.0 / std::pow(n, 1.2);
        }
        return t;
    }();
    return T[(size_t)clampv(m, 0, kNumModels - 1)];
}

class Engine {
public:
    static constexpr int kMaxVoices = 8, kCtl = 32;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        (void)modelTable(0);   // built here, not by the first note on the audio thread (it allocates)
        for (int i = 0; i < kMaxVoices; ++i) { voices[(size_t)i] = Voice(); voices[(size_t)i].rng.seed(0xA11u + 131u * (uint32_t)i); }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        vibPh = 0.0; ctl = 0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s; set.modes = clampv(set.modes, 1, kMaxModes);
        if (re) configure();
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    // the newest voice's modes: ratio to the first and level now
    float modeRatio(int m) const { return ratioFeed[(size_t)clampv(m, 0, kMaxModes - 1)].load(std::memory_order_relaxed); }
    float modeLevel(int m) const { return levelFeed[(size_t)clampv(m, 0, kMaxModes - 1)].load(std::memory_order_relaxed); }
    float exciteNow() const { return exFeed.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        double exMax = 0.0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                double xn = 0.0;
                const double x = excite(v, xn);
                exMax = std::max(exMax, std::fabs(x) + std::fabs(xn));
                double vl = 0.0, vr = 0.0;
                for (int m = 0; m < v.nm; ++m)
                {
                    // the mode: z <- z * p + g x (p = r e^{iw}); its output is the imaginary part
                    const double zr = v.zr[m] * v.pr[m] - v.zi[m] * v.pi[m] + v.gin[m] * x + v.gnz[m] * xn;
                    const double zi = v.zr[m] * v.pi[m] + v.zi[m] * v.pr[m];
                    v.zr[m] = zr; v.zi[m] = zi;
                    vl += zi * v.gl[m]; vr += zi * v.gr[m];
                }
                l += vl; r += vr;
                // silent and done?
                if (!v.gate && !v.driven && ((ctl & 255) == 0))
                {
                    double en = 0.0; for (int m = 0; m < v.nm; ++m) en += v.zr[m] * v.zr[m] + v.zi[m] * v.zi[m];
                    if (en < 1e-12 && v.exT > 0.05) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
                }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; for (int m = 0; m < kMaxModes; ++m) v.zr[m] = v.zi[m] = 0.0; } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 }; uint64_t newest = 0; const Voice* nv = nullptr;
        for (const Voice& v : voices)
        {
            if (!v.active) continue;
            ++act;
            if (v.gate) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
            if (v.age >= newest) { newest = v.age; nv = &v; }
        }
        for (int m = 0; m < kMaxModes; ++m)
        {
            float lv = 0.0f, rt = 0.0f;
            if (nv && m < nv->nm) { lv = (float)std::sqrt(nv->zr[m] * nv->zr[m] + nv->zi[m] * nv->zi[m]); rt = (float)nv->ratio[m]; }
            levelFeed[(size_t)m].store(lv); ratioFeed[(size_t)m].store(rt);
        }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]); exFeed.store((float)exMax);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Voice {
        bool active = false, gate = false, driven = false;
        int note = 60, nm = 0;
        uint64_t age = 0;
        double f1 = 261.6, vel = 1.0, pitch = 60.0, target = 60.0, exT = 0.0, drive = 0.0, bowPh = 0.0, nz = 0.0, nz2 = 0.0, flut = 0.0;
        double zr[kMaxModes] = {}, zi[kMaxModes] = {}, pr[kMaxModes] = {}, pi[kMaxModes] = {}, gin[kMaxModes] = {}, gnz[kMaxModes] = {}, gl[kMaxModes] = {}, gr[kMaxModes] = {}, ratio[kMaxModes] = {};
        double bpX = 0.0, bpS1 = 0.0, bpS2 = 0.0;
        fx::Rng rng { 1u };
    };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        for (auto& v : voices) v.active = false;
    }
    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            Voice& v = voices[(size_t)clampv(c.voice, 0, kMaxVoices - 1)];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            {
                const bool was = v.active;
                v.active = true; v.gate = true; v.note = c.note; v.target = c.note; v.age = ++counter;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                if (c.velocity >= 0.0f) { v.exT = 0.0; v.drive = 0.0; }
                if (!was) for (int k = 0; k < kMaxModes; ++k) v.zr[k] = v.zi[k] = 0.0;
                tune(v, true);
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.gate = false; tune(v, false); break;
            case synth::VoiceCmd::Kill: v.active = false; v.gate = false; break;
            }
        });
    }

    // the modes for the voice's pitch, the object and the settings (each control tick, and at note-on)
    void tune(Voice& v, bool fresh)
    {
        const Settings& s = set;
        const ModelTable& T = modelTable(s.model);
        const double vib = s.vibrato * 0.5 * std::sin(kTwoPi * vibPh);   // semitones
        const double f1 = synth::midiToHz(v.pitch + 12.0 * s.octave + vm.pitchBend() * 2.0 + vib);
        const double st = clampv(s.stretch, -1.0, 1.0) * 0.3;
        const double D = 3.0 * std::pow(1.0 - clampv(s.material, 0.0, 1.0), 2.0) + 0.004;
        v.driven = (s.exciter == kBow || s.exciter == kBlow) && v.gate;
        const double x = clampv(s.position, 0.0, 1.0) * 0.5 + 0.0025;
        const double vg = (1.0 - s.velocity) + s.velocity * v.vel;
        int nm = 0;
        double norm = 0.0;
        for (int m = 0; m < s.modes; ++m)
        {
            const double rr = std::pow(T.ratio[m], 1.0 + st);
            const double f = f1 * rr;
            if (f > sr * 0.45) break;
            double t60 = clampv(s.decay, 0.02, 30.0) / (1.0 + D * std::max(0.0, rr - 1.0));
            if (!v.gate) t60 = std::min(t60, clampv(s.damp, 0.01, 30.0));
            const double rad = std::exp(-6.9077552789821 / (t60 * sr)), w = kTwoPi * f / sr;
            v.pr[m] = rad * std::cos(w); v.pi[m] = rad * std::sin(w);
            const double a = T.level[m] * (std::fabs(std::sin(kPi * (m + 1) * x)) + 0.02) * std::pow(rr, s.bright * 1.2);
            // an impulse of area 1 rings a mode at its gain; noise of unit power settles at sqrt(1 - r^2) times it
            // a driven mode fed one pulse a period settles at gain / (1 - r^period): hold that at the level
            v.gin[m] = v.driven || s.exciter == kBow ? a * vg * (1.0 - std::pow(rad, sr / f1)) * 0.9 : a * vg;
            v.gnz[m] = a * vg * std::sqrt(std::max(0.0, 1.0 - rad * rad)) * 3.0;
            const double pan = ((m & 1) ? 1.0 : -1.0) * s.spread * std::min(1.0, m / 3.0);
            v.gl[m] = std::sqrt(0.5 * (1.0 - pan)); v.gr[m] = std::sqrt(0.5 * (1.0 + pan));
            v.ratio[m] = rr;
            norm += a * a;
            ++nm;
        }
        // keep the struck loudness roughly the same whatever the object and mode count
        const double k = 0.35 / std::max(0.2, std::sqrt(norm));
        for (int m = 0; m < nm; ++m) { v.gin[m] *= k; v.gnz[m] *= k; }
        for (int m = nm; m < kMaxModes; ++m) { v.zr[m] = 0.0; v.zi[m] = 0.0; }
        v.nm = nm;
        v.f1 = f1;
        (void)fresh;
    }
    void control()
    {
        const double dt = kCtl / sr;
        vibPh += set.vibRate * dt; vibPh -= std::floor(vibPh);
        const double gk = set.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / set.glide) : 1.0;
        for (auto& v : voices) if (v.active) { v.pitch += (v.target - v.pitch) * gk; tune(v, false); }
    }

    // the exciter's next sample: x is impulsive (an area of 1 rings the modes at their level), xn noise-like (unit
    // power holds them at their level)
    inline double excite(Voice& v, double& xn)
    {
        const Settings& s = set;
        const double t = v.exT;
        v.exT += 1.0 / sr;
        const double h = clampv(s.hardness, 0.0, 1.0);
        double x = 0.0;
        xn = 0.0;
        switch (s.exciter)
        {
        case kPluck:
        {
            const double len = 0.0015 + (1.0 - h) * 0.012;
            if (t < len)
            {
                v.nz += (v.rng.bi() - v.nz) * (0.08 + 0.9 * h);
                x = v.nz * (1.0 - t / len) * 2.5 / std::sqrt(len * sr);
                if (t * sr < 1.0) x += 0.35;   // the string let go
            }
            break;
        }
        case kBow: case kBlow:
        {
            const double target = v.gate ? 0.15 + 0.85 * clampv(s.pressure, 0.0, 1.0) : 0.0;
            v.drive += (target - v.drive) * (target > v.drive ? 0.0012 : 0.003);
            if (v.drive < 1e-5) break;
            const double nz = v.rng.bi();
            if (s.exciter == kBow)
            {
                // stick-slip: a slip each period of the note (with a little jitter), and the friction noise
                v.bowPh += v.f1 / sr * (1.0 + 0.006 * nz);
                if (v.bowPh >= 1.0) { v.bowPh -= 1.0; x = v.drive * (0.3 + 0.5 * h) * 1.0; }
                v.nz += (nz - v.nz) * (0.05 + 0.6 * h);
                xn = v.nz * (0.25 + 0.75 * s.noise) * v.drive * 0.22;
            }
            else
            {
                // breath: noise band-passed around the note, with a slow flutter
                v.flut += (v.rng.bi() - v.flut) * 0.0005;
                const double fc = v.f1 * (1.0 + 0.6 * h), q = 1.0 + 5.0 * (1.0 - s.noise);
                const double g = std::tan(kPi * std::min(fc, sr * 0.45) / sr), k = 1.0 / q;
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const double v3 = nz - v.bpS2, v1 = a1 * v.bpS1 + a2 * v3, v2 = v.bpS2 + a2 * v.bpS1 + a3 * v3;
                v.bpS1 = 2.0 * v1 - v.bpS1; v.bpS2 = 2.0 * v2 - v.bpS2;
                xn = (v1 * 2.0 + nz * s.noise * 0.3) * v.drive * (1.0 + 0.6 * v.flut) * 0.11;
            }
            break;
        }
        default:
        {
            // a half-sine of area 1: short (hard) excites the high modes, long (soft) only the low ones
            const double len = 0.0003 + std::pow(1.0 - h, 2.0) * 0.006;
            if (t < len) x = std::sin(kPi * t / len) * kPi * 0.5 / (len * sr);
            if (s.noise > 0.0 && t < 0.01) x += v.rng.bi() * s.noise * 1.5 * (1.0 - t / 0.01) / std::sqrt(0.01 * sr);
            break;
        }
        }
        return x;
    }

    Settings set;
    double sr = 48000.0, vibPh = 0.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    uint64_t ctl = 0, counter = 0;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> exFeed { 0.0f };
    std::array<std::atomic<float>, kMaxModes> levelFeed {}, ratioFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::modal
