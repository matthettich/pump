// pump TRACT engine. Framework-free.
//
// A singer: the glottis makes the vocal folds' airflow, a Rosenberg pulse each period (opening over a part of the
// period, closing faster, then shut; TENSE shortens the open part and sharpens the closure, a breathy voice stays open
// longer), with vibrato and jitter (each period a little longer or shorter and louder or softer); its derivative, plus
// aspiration noise shaped by the flow (BREATH), goes into the tract. The tract is 44 sections from the glottis to the
// lips at about 3.6 mm each (17 cm at SIZE 100 %) in a Kelly-Lochbaum waveguide: in every section a wave travels each
// way; where two sections meet, k = (A1 - A2) / (A1 + A2) of it reflects; the glottis end reflects 0.75 back, the lips
// -0.85, and what leaves the lips is the sound. It steps at 96 kHz / SIZE (several steps a sample; their outputs are
// averaged). The shape: a resting tract (narrow above the glottis), the tongue's constriction (a cos^2 bump narrowing
// it to a diameter at a place) and the lips; the five vowels' shapes were fitted so the first two formants land on
// textbook values (A 730 / 1090 Hz, E 530 / 1840, I 270 / 2290, O 570 / 840, U 300 / 870). VOWEL and MOTION move
// between them. A note is 1 to 4 such singers, each detuned, sized, voweled and panned a little differently.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "TractParams.hpp"
#include <array>
#include <atomic>

namespace pump { namespace tract {

constexpr int kSections = 44, kMaxSingers = 4;
// the five vowels' tongue place (section), tongue diameter at it, lip diameter (and a level that evens them out)
struct VowelShape { double idx, minD, lips, gain; };
static const VowelShape kVowels[5] = { { 12, 0.30, 1.50, 1.0 }, { 24, 0.70, 1.50, 1.09 }, { 27, 0.20, 1.50, 0.94 }, { 13, 0.20, 0.90, 1.0 }, { 25, 0.40, 0.40, 1.45 } };
static const char* const kVowelNames[5] = { "A", "E", "I", "O", "U" };

inline VowelShape vowelAt(double v)
{
    const double u = clampv(v, 0.0, 1.0) * 4.0;
    const int a = std::min(3, (int)u);
    const double f = u - a;
    const VowelShape& p = kVowels[a]; const VowelShape& q = kVowels[a + 1];
    return { p.idx + (q.idx - p.idx) * f, p.minD + (q.minD - p.minD) * f, p.lips + (q.lips - p.lips) * f, p.gain + (q.gain - p.gain) * f };
}
inline void tractShape(const VowelShape& s, double* d)
{
    for (int i = 0; i < kSections; ++i)
    {
        const double rest = i < 7 ? 0.6 + 0.5 * i / 7.0 : (i < 12 ? 1.1 + 0.4 * (i - 7) / 5.0 : 1.5);
        const double w = std::fabs(i - s.idx) < 10.0 ? std::pow(std::cos(kPi * 0.5 * (i - s.idx) / 10.0), 2.0) : 0.0;
        double v = rest - (rest - s.minD) * w;
        if (i >= kSections - 4) v = std::min(v, s.lips);
        d[i] = std::max(0.05, v);
    }
}

struct Settings {
    double vowel = 0.0, motion = 0.1, motionRate = 0.2, size = 1.0, tense = 0.55, breath = 0.25, vibrato = 0.25, vibRate = 5.2, jitter = 0.2;
    int singers = 2, voices = 6, voiceMode = 0, octave = 0;
    double detune = 12.0, sizeSpread = 0.3, vowelSpread = 0.1, spread = 0.7, bright = 0.6;
    double attack = 0.4, decay = 0.8, sustain = 1.0, release = 1.2, velocity = 0.4, glide = 0.0, volumeDb = 0.0;
};

class Engine {
public:
    static constexpr int kMaxVoices = 8, kCtl = 32;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t)i];
            v = Voice();
            v.amp.setSampleRate(sr);
            for (int k = 0; k < kMaxSingers; ++k) v.sg[(size_t)k].rng.seed(0x7AC7u + 613u * (uint32_t)(i * 4 + k));
        }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        ctl = 0; motionT = 0.0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        for (auto& v : voices) v.amp.set(s.attack, s.decay, s.sustain, s.release);
        if (re) configure();
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    float diameter(int i) const { return shapeFeed[(size_t)clampv(i, 0, kSections - 1)].load(std::memory_order_relaxed); }   // the newest voice's first singer
    float vowelNow() const { return vowelFeed.load(std::memory_order_relaxed); }
    float glottisNow() const { return glotFeed.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        const double steps = 96000.0 / sr;
        const double bc = 1.0 - std::exp(-kTwoPi * (1500.0 * std::pow(8.0, set.bright)) / sr);
        float gpk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                const double a = v.amp.next() * v.gain;
                for (int k = 0; k < v.ns; ++k)
                {
                    Singer& s = v.sg[(size_t)k];
                    s.acc += steps / s.size;
                    double sum = 0.0; int cnt = 0;
                    while (s.acc >= 1.0) { s.acc -= 1.0; sum += tractStep(s, glottis(s)); ++cnt; }
                    const double y = cnt ? sum / cnt : s.last;
                    s.last = y;
                    s.lp += (y - s.lp) * bc;
                    const double o = s.lp * a * s.gain * s.vgain;
                    l += o * s.gl; r += o * s.gr;
                    if (vi == newest && k == 0) gpk = std::max(gpk, (float)s.flow);
                }
                if (!v.amp.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; v.amp.kill(); for (auto& s : v.sg) s.clear(); } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (const Voice& v : voices) { if (!v.active) continue; ++act; if (v.amp.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63); }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        const Voice& nv = voices[(size_t)newest];
        double d[kSections];
        tractShape(vowelAt(nv.active ? nv.sg[0].vowel : set.vowel), d);
        for (int k = 0; k < kSections; ++k) shapeFeed[(size_t)k].store((float)d[k]);
        vowelFeed.store((float)(nv.active ? nv.sg[0].vowel : set.vowel));
        glotFeed.store(nv.active ? gpk : 0.0f);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Singer {
        double Rb[2][kSections] = {}, Lb[2][kSections] = {}, k[kSections] = {};
        int cur = 0;
        double vgain = 1.0;
        double acc = 0.0, last = 0.0, lp = 0.0, size = 1.0, vowel = 0.0, gain = 1.0, gl = 0.7071, gr = 0.7071;
        double ph = 0.0, f = 220.0, fNow = 220.0, amp = 1.0, flow = 0.0, prevFlow = 0.0, cents = 0.0, vibPh = 0.0, vibRate = 5.2, motionPh = 0.0;
        double nz1 = 0.0, nz2 = 0.0;
        fx::Rng rng { 1u };
        void clear() { for (int b = 0; b < 2; ++b) for (int i = 0; i < kSections; ++i) Rb[b][i] = Lb[b][i] = 0.0; acc = last = lp = 0.0; flow = prevFlow = 0.0; }
    };
    struct Voice {
        bool active = false;
        int note = 60, ns = 2;
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, gain = 1.0;
        std::array<Singer, kMaxSingers> sg {};
        synth::Adsr amp;
    };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        for (auto& v : voices) { v.active = false; v.amp.kill(); }
    }
    void handle(const synth::Midi& m)
    {
        vm.midi(m.status, m.d1, m.d2, [&](const synth::VoiceCmd& c) {
            const int vi = clampv(c.voice, 0, kMaxVoices - 1);
            Voice& v = voices[(size_t)vi];
            switch (c.kind)
            {
            case synth::VoiceCmd::Start:
            {
                const bool was = v.active;
                v.active = true; v.note = c.note; v.target = c.note; v.age = ++counter; newest = vi;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                v.gain = (1.0 - set.velocity) + set.velocity * v.vel;
                if (c.velocity >= 0.0f || !was)
                {
                    v.amp.gate(true);
                    if (!was) { for (auto& s : v.sg) { s.clear(); s.ph = s.rng.uni(); s.vibPh = s.rng.uni(); s.motionPh = s.rng.uni(); } }
                    singers(v);
                    shape(v);
                }
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.amp.gate(false); break;
            case synth::VoiceCmd::Kill: v.active = false; v.amp.kill(); break;
            }
        });
    }
    // each singer's own detune, size, vowel offset, place and vibrato
    void singers(Voice& v)
    {
        const Settings& s = set;
        v.ns = clampv(s.singers, 1, kMaxSingers);
        static const double u1[4] = { -1.0, 0.6, -0.3, 1.0 }, u2[4] = { 0.4, -1.0, 1.0, -0.5 }, pan[4] = { -1.0, 1.0, -0.4, 0.4 };
        for (int k = 0; k < v.ns; ++k)
        {
            Singer& g = v.sg[(size_t)k];
            g.cents = v.ns > 1 ? s.detune * (2.0 * k / (v.ns - 1) - 1.0) : 0.0;
            g.size = clampv(s.size * (1.0 + 0.25 * s.sizeSpread * u1[k]), 0.5, 1.8);
            const double p = v.ns > 1 ? pan[k] * s.spread : 0.0;
            g.gl = std::sqrt(0.5 * (1.0 - p)); g.gr = std::sqrt(0.5 * (1.0 + p));
            g.vibRate = s.vibRate * (1.0 + 0.08 * u2[k]);
            g.gain = 1.0 / std::sqrt((double)v.ns);
        }
    }
    void shape(Voice& v)
    {
        const Settings& s = set;
        static const double u2[4] = { 0.4, -1.0, 1.0, -0.5 };
        for (int k = 0; k < v.ns; ++k)
        {
            Singer& g = v.sg[(size_t)k];
            const double mo = 0.5 * s.motion * std::sin(kTwoPi * (motionT * s.motionRate + g.motionPh * s.vowelSpread));
            g.vowel = clampv(s.vowel + mo + 0.25 * s.vowelSpread * u2[k], 0.0, 1.0);
            double d[kSections];
            const VowelShape vs = vowelAt(g.vowel);
            g.vgain = vs.gain;
            tractShape(vs, d);
            for (int i = 1; i < kSections; ++i) { const double a0 = d[i - 1] * d[i - 1], a1 = d[i] * d[i]; g.k[i] = (a0 - a1) / (a0 + a1); }
            g.f = synth::midiToHz(v.pitch + 12.0 * s.octave + vm.pitchBend() * 2.0 + g.cents * 0.01);
        }
    }
    void control()
    {
        const double dt = kCtl / sr;
        motionT += dt;
        const double gk = set.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / set.glide) : 1.0;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            v.pitch += (v.target - v.pitch) * gk;
            shape(v);
        }
    }

    // the glottis, one tract step (at the tract's own rate)
    inline double glottis(Singer& g)
    {
        const Settings& s = set;
        const double rate = 96000.0 / g.size;
        g.vibPh += g.vibRate / rate; g.vibPh -= std::floor(g.vibPh);
        const double vib = 0.6 * s.vibrato * std::sin(kTwoPi * g.vibPh);
        g.ph += g.fNow * std::exp2(vib / 12.0) / rate;
        if (g.ph >= 1.0)
        {
            // a new period: its own length and strength (jitter, shimmer)
            g.ph -= std::floor(g.ph);
            g.fNow = g.f * (1.0 + 0.015 * s.jitter * g.rng.bi());
            g.amp = 1.0 + 0.1 * s.jitter * g.rng.bi();
        }
        const double oq = 0.85 - 0.4 * s.tense, tp = 0.6 * oq, tn = oq - tp, t = g.ph;
        double fl;
        if (t < tp) fl = 0.5 * (1.0 - std::cos(kPi * t / tp));
        else if (t < tp + tn) fl = std::cos(kPi * 0.5 * (t - tp) / tn);
        else fl = 0.0;
        g.flow = fl;
        const double dfl = (fl - g.prevFlow) * rate / std::max(1.0, g.fNow) * 0.08;   // the derivative, per period (steady across pitch)
        g.prevFlow = fl;
        // aspiration: noise, band-limited, louder while the folds are open
        const double w = g.rng.bi();
        g.nz1 += (w - g.nz1) * 0.25; g.nz2 += (g.nz1 - g.nz2) * 0.05;
        const double asp = (g.nz1 - g.nz2) * s.breath * (0.25 + 0.75 * fl) * 0.6;
        return (dfl * (1.0 - 0.6 * s.breath) + asp) * g.amp;
    }
    inline double tractStep(Singer& g, double in)
    {
        // double-buffered: read this step's waves, write the next's
        const double* R = g.Rb[g.cur]; const double* L = g.Lb[g.cur];
        double* Rn = g.Rb[g.cur ^ 1]; double* Ln = g.Lb[g.cur ^ 1];
        constexpr double damp = 0.9995;
        Rn[0] = (L[0] * 0.75 + in) * damp;
        for (int i = 1; i < kSections; ++i) { const double w = g.k[i] * (R[i - 1] + L[i]); Rn[i] = (R[i - 1] - w) * damp; Ln[i - 1] = (L[i] + w) * damp; }
        Ln[kSections - 1] = R[kSections - 1] * -0.85 * damp;
        g.cur ^= 1;
        return R[kSections - 1];
    }

    Settings set;
    double sr = 48000.0, motionT = 0.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    uint64_t ctl = 0, counter = 0;
    int newest = 0;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> vowelFeed { 0.0f }, glotFeed { 0.0f };
    std::array<std::atomic<float>, kSections> shapeFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::tract
