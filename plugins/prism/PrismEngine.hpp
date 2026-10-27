// pump PRISM engine: plays an Analysis (PrismAnalysis.hpp) back as up to 128 sine partials and twelve bands of
// noise per voice. Framework-free.
//
// Every 32 samples each voice reads the analysis at its place in the sound (between two frames), optionally a
// second sound at the same place (MORPH), and sets every partial's level and frequency for the end of the block
// (the oscillators glide there, so nothing steps):
//   level      the partial's own level (it keeps its place in the series: the timbre moves with the pitch), or with
//              FORMANT the level the sound had at that frequency (the formants stay where they were); TILT,
//              ODD / EVEN, PARTIALS; BLUR smooths every level over time
//   frequency  the note x k, with the sound's own detuning of each partial (OWN DETUNING), bent sharp by INHARMONIC;
//              FOLLOW adds the sound's own pitch movement (its melody, its vibrato) to the note
//   noise      white noise through twelve band-passes at the sound's noise levels (NOISE scales them)
// The place in the sound moves at SPEED (100 % = as recorded) between START and END: LOOP, ONE-SHOT (stays on the
// last moment), BOUNCE (back and forth), FREEZE (stands at SCAN). Poly (up to 8 voices) or mono / legato with glide.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "PrismParams.hpp"
#include "PrismAnalysis.hpp"
#include <array>
#include <atomic>

namespace pump { namespace prism {

struct Settings {
    int mode = kLoop, partials = 64, voices = 8, voiceMode = 0, octave = 0;
    double morph = 0.5, speed = 1.0, start = 0.0, end = 1.0, scan = 0.3, follow = 0.0;
    double tilt = 0.0, oddEven = 0.0, inharm = 0.0, own = 1.0, formant = 0.0, blur = 0.0, noise = 1.0, width = 0.4;
    double attack = 0.008, decay = 0.5, sustain = 1.0, release = 0.4, velocity = 0.5, glide = 0.0, volumeDb = 0.0;
};

class Engine {
public:
    static constexpr int kMaxVoices = 8, kBlk = 32, kTab = 4096;

    Engine()
    {
        for (int i = 0; i <= kTab; ++i) sinT[(size_t)i] = (float)std::sin(kTwoPi * i / kTab);
    }
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) { v = Voice(); v.env.setSampleRate(sr); }
        for (int i = 0; i < kMaxVoices; ++i) { voices[(size_t)i].rng.seed(0x9157u + 977u * (uint32_t)i); for (int k = 0; k < kK; ++k) voices[(size_t)i].ph[k] = std::fmod((k + 1) * 0.618034 + i * 0.37, 1.0); }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        blkPos = 0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        set.partials = clampv(set.partials, 1, kK);
        if (re) configure();
        for (auto& v : voices) v.env.set(set.attack, set.decay, set.sustain, set.release);
        // the partials' stereo places: alternating, wider further up
        for (int k = 1; k <= kK; ++k)
        {
            const double pan = ((k & 1) ? -1.0 : 1.0) * set.width * std::min(1.0, (k - 1) / 4.0);
            panL[k] = std::sqrt((1.0 - pan) * 0.5) * 1.4142135623730951; panR[k] = std::sqrt((1.0 + pan) * 0.5) * 1.4142135623730951;
        }
        // the noise bands
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = bandLo(b), hi = std::min(bandHi(b), 0.45 * sr), fc = std::sqrt(lo * std::max(lo * 1.01, hi));
            bandFc[b] = fc; bandQ[b] = fc / std::max(1.0, hi - lo);
            bandGain[b] = std::sqrt(std::max(1.0, sr * 0.5) / std::max(1.0, hi - lo)) * 26.3;   // white noise through a band of that width -> its level
        }
    }
    // the sounds to play (owned elsewhere, never freed while the engine may read them); nullptr = none
    void setSources(const Analysis* a, const Analysis* b) { srcA.store(a, std::memory_order_release); srcB.store(b, std::memory_order_release); }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    // the newest voice's place in the sound (0..1), -1 when none sounds
    float positionNow() const { return posFeed.load(std::memory_order_relaxed); }
    float partialLevel(int k) const { return partFeed[(size_t)clampv(k, 1, kK) - 1].load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        const Analysis* A = srcA.load(std::memory_order_acquire);
        const Analysis* B = srcB.load(std::memory_order_acquire);
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if (blkPos == 0) for (auto& v : voices) if (v.active) control(v, A, B);
            const double t = (blkPos + 1) * (1.0 / kBlk);
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                voiceSample(v, t, l, r);
                if (!v.env.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
            if (++blkPos == kBlk) { blkPos = 0; for (auto& v : voices) if (v.active) { v.a0 = v.a1; v.i0 = v.i1; for (int b = 0; b < kBands; ++b) v.g0[b] = v.g1[b]; } }
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; v.env.kill(); for (auto& s : v.bp) s.reset(); } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        // feeds
        int act = 0; uint64_t lit[2] = { 0, 0 }; float pos = -1.0f; uint64_t newest = 0; const Voice* nv = nullptr;
        for (const Voice& v : voices)
        {
            if (!v.active) continue;
            ++act;
            if (v.env.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
            if (v.age >= newest) { newest = v.age; nv = &v; }
        }
        if (nv && A) pos = (float)(nv->pos / std::max(1, A->frames - 1));
        for (int k = 1; k <= kK; ++k) partFeed[(size_t)k - 1].store(nv ? (float)nv->a1[k] : 0.0f, std::memory_order_relaxed);
        activeFeed.store(act); posFeed.store(pos); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Voice {
        bool active = false;
        int note = 60;
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, pos = 0.0, dir = 1.0;
        std::array<double, kK + 1> a0 {}, a1 {}, i0 {}, i1 {}, blurA {};
        double ph[kK] = {};
        double g0[kBands] = {}, g1[kBands] = {};
        fx::Svf bp[kBands];
        fx::Rng rng { 1u };
        synth::Adsr env;
        bool fresh = true;
    };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        for (auto& v : voices) { v.active = false; v.env.kill(); }
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
                v.active = true; v.note = c.note; v.target = c.note; v.age = ++counter;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                if (c.velocity >= 0.0f)
                {
                    // a new note starts at the start of the sound (FREEZE: at SCAN), from silence
                    v.pos = startFrame(); v.dir = 1.0;
                    if (!was) { v.a0.fill(0.0); v.a1.fill(0.0); v.blurA.fill(0.0); for (auto& s : v.bp) s.reset(); for (int b = 0; b < kBands; ++b) v.g0[b] = v.g1[b] = 0.0; }
                    v.fresh = true;
                }
                v.env.gate(true);
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.env.gate(false); break;
            case synth::VoiceCmd::Kill: v.env.kill(); v.active = false; break;
            }
        });
    }
    double framesA() const { const Analysis* A = srcA.load(std::memory_order_acquire); return A ? (double)std::max(1, A->frames - 1) : 1.0; }
    double startFrame() const
    {
        const double F = framesA();
        if (set.mode == kFreeze) return clampv(set.scan, 0.0, 1.0) * F;
        return clampv(std::min(set.start, set.end), 0.0, 1.0) * F;
    }

    // a level and a frequency ratio of partial k in analysis S at frame position p (linear between frames); with
    // FORMANT, the level at the frequency the partial now sits at (index j, fractional)
    static void frameAt(const Analysis& S, double p, double& f0, int& i0, double& t)
    {
        const double pp = clampv(p, 0.0, (double)(S.frames - 1));
        i0 = std::min(S.frames - 1, (int)pp); t = pp - i0;
        const int i1 = std::min(S.frames - 1, i0 + 1);
        f0 = S.f0[(size_t)i0] + (S.f0[(size_t)i1] - S.f0[(size_t)i0]) * t;
    }
    static double ampAt(const Analysis& S, int i0, double t, double j)
    {
        if (j < 1.0 || j > kK) return 0.0;
        const int i1 = std::min(S.frames - 1, i0 + 1);
        const int k0 = (int)j, k1 = std::min(kK, k0 + 1);
        const double u = j - k0;
        auto a = [&](int fr, int k) { return (double)S.amp[(size_t)fr * kK + k - 1]; };
        const double x0 = a(i0, k0) + (a(i0, k1) - a(i0, k0)) * u, x1 = a(i1, k0) + (a(i1, k1) - a(i1, k0)) * u;
        return x0 + (x1 - x0) * t;
    }

    void control(Voice& v, const Analysis* A, const Analysis* B)
    {
        const Settings& s = set;
        const double dt = kBlk / sr;
        if (s.glide > 0.0) v.pitch += (v.target - v.pitch) * (1.0 - std::exp(-dt * 3000.0 / s.glide)); else v.pitch = v.target;
        if (!A) { v.a1.fill(0.0); for (int b = 0; b < kBands; ++b) v.g1[b] = 0.0; return; }
        // the place in the sound
        const double F = (double)std::max(1, A->frames - 1);
        double lo = clampv(std::min(s.start, s.end), 0.0, 1.0) * F, hi = clampv(std::max(s.start, s.end), 0.0, 1.0) * F;
        if (hi - lo < 1.0) hi = std::min(F, lo + 1.0);
        const double step = s.speed * dt / A->hopS;
        switch (s.mode)
        {
        case kFreeze: v.pos += (clampv(s.scan, 0.0, 1.0) * F - v.pos) * 0.15; break;
        case kOneShot: v.pos = std::min(hi, v.pos + step); break;
        case kBounce:
            v.pos += step * v.dir;
            if (v.pos >= hi) { v.pos = hi - (v.pos - hi); v.dir = -1.0; }
            if (v.pos <= lo) { v.pos = lo + (lo - v.pos); v.dir = 1.0; }
            v.pos = clampv(v.pos, lo, hi);
            break;
        default:
            v.pos += step;
            if (v.pos >= hi) v.pos = lo + std::fmod(v.pos - lo, std::max(1.0, hi - lo));
            if (v.pos < lo) v.pos = lo;
            break;
        }
        double f0A; int iA; double tA;
        frameAt(*A, v.pos, f0A, iA, tA);
        const double posN = v.pos / F;
        double f0B = 0.0; int iB = 0; double tB = 0.0;
        const bool useB = B && s.morph > 0.0005;
        if (useB) frameAt(*B, posN * std::max(1, B->frames - 1), f0B, iB, tB);
        const double m = useB ? clampv(s.morph, 0.0, 1.0) : 0.0;
        // the pitch: the note, and the sound's own movement around its median pitch (FOLLOW)
        const double f0src = f0A * (1.0 - m) + (useB ? f0B * m : 0.0);
        const double med = A->f0med * (1.0 - m) + (useB ? B->f0med * m : 0.0);
        const double hz = synth::midiToHz(v.pitch + 12.0 * s.octave + vm.pitchBend() * 2.0) * std::pow(std::max(1e-3, f0src / std::max(1.0, med)), s.follow);
        const double nyq = sr * 0.45;
        const double ih = s.inharm * 0.35, blurK = s.blur > 0.0005 ? 1.0 - std::exp(-dt / (s.blur * s.blur * 1.5 + 0.002)) : 1.0;
        const double vgain = (1.0 - s.velocity) + s.velocity * v.vel;
        const int K = s.partials;
        double e0 = 0.0, e1 = 0.0;   // the levels' energy before and after TILT / ODD-EVEN: the shaping keeps it
        for (int k = 1; k <= kK; ++k)
        {
            double a = 0.0, ratio = 1.0;
            if (k <= K)
            {
                auto one = [&](const Analysis& S, int i0, double t, double f0s, double& rr) {
                    const int i1 = std::min(S.frames - 1, i0 + 1);
                    const double ai = S.amp[(size_t)i0 * kK + k - 1] + (S.amp[(size_t)i1 * kK + k - 1] - S.amp[(size_t)i0 * kK + k - 1]) * t;
                    rr = S.ratio[(size_t)i0 * kK + k - 1] + (S.ratio[(size_t)i1 * kK + k - 1] - S.ratio[(size_t)i0 * kK + k - 1]) * t;
                    if (s.formant <= 0.0005) return ai;
                    const double af = ampAt(S, i0, t, k * hz / std::max(20.0, f0s));
                    return ai + (af - ai) * s.formant;
                };
                double rA = 1.0, rB = 1.0;
                a = one(*A, iA, tA, f0A, rA);
                if (useB) { const double b = one(*B, iB, tB, f0B, rB); a = a + (b - a) * m; ratio = rA + (rB - rA) * m; }
                else ratio = rA;
                e0 += a * a;
                a *= std::pow((double)k, s.tilt);
                const bool odd = (k & 1) != 0;
                if (s.oddEven > 0.0 && !odd) a *= 1.0 - s.oddEven;
                else if (s.oddEven < 0.0 && odd && k > 1) a *= 1.0 + s.oddEven;
            }
            e1 += a * a;
            double mul = k * (1.0 + s.own * (ratio - 1.0));
            if (ih > 0.0) mul *= std::pow((double)k, ih);
            const double fk = hz * mul;
            if (fk >= nyq) a = 0.0;
            else if (fk > nyq * 0.85) a *= (nyq - fk) / (nyq * 0.15);
            v.a1[k] = a;
            v.i1[k] = fk / sr;
        }
        const double keep = (e1 > 1e-20 && (s.tilt != 0.0 || s.oddEven != 0.0)) ? std::sqrt(e0 / e1) : 1.0;
        for (int k = 1; k <= kK; ++k)
        {
            const double a = v.a1[k] * keep;
            v.blurA[k] = v.fresh ? a : v.blurA[k] + (a - v.blurA[k]) * blurK;
            v.a1[k] = v.blurA[k] * vgain;
        }
        if (v.fresh) { v.a0 = v.a1; v.i0 = v.i1; }
        // the noise bands
        for (int b = 0; b < kBands; ++b)
        {
            auto nb = [&](const Analysis& S, int i0, double t) { const int i1 = std::min(S.frames - 1, i0 + 1); return S.noise[(size_t)i0 * kBands + b] + (S.noise[(size_t)i1 * kBands + b] - S.noise[(size_t)i0 * kBands + b]) * t; };
            double g = nb(*A, iA, tA);
            if (useB) g += (nb(*B, iB, tB) - g) * m;
            v.g1[b] = g * s.noise * vgain * bandGain[b];
            if (v.fresh) v.g0[b] = v.g1[b];
            v.bp[b].set(bandFc[b], bandQ[b], sr);
        }
        v.fresh = false;
    }

    inline double tab(double ph) const
    {
        const double x = ph * kTab;
        const int j = (int)x;
        return sinT[(size_t)j] + (x - j) * (sinT[(size_t)j + 1] - sinT[(size_t)j]);
    }
    inline void voiceSample(Voice& v, double t, double& l, double& r)
    {
        const double env = v.env.next();
        double sl = 0.0, srr = 0.0;
        for (int k = 1; k <= kK; ++k)
        {
            const double a0 = v.a0[k], a1 = v.a1[k];
            if (a0 < 1e-7 && a1 < 1e-7) continue;
            const double amp = a0 + (a1 - a0) * t, inc = v.i0[k] + (v.i1[k] - v.i0[k]) * t;
            const double y = tab(v.ph[k - 1]) * amp;
            sl += y * panL[k]; srr += y * panR[k];
            v.ph[k - 1] += inc; if (v.ph[k - 1] >= 1.0) v.ph[k - 1] -= std::floor(v.ph[k - 1]);
        }
        if (set.noise > 0.0005)
        {
            const double nz = v.rng.bi();
            double nsum = 0.0;
            for (int b = 0; b < kBands; ++b)
            {
                const double g = v.g0[b] + (v.g1[b] - v.g0[b]) * t;
                const double y = v.bp[b].bandpass(nz);
                nsum += y * g;
            }
            sl += nsum; srr += nsum;
        }
        l += sl * env; r += srr * env;
    }

    Settings set;
    double sr = 48000.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    std::array<float, kTab + 1> sinT {};
    std::array<double, kK + 1> panL {}, panR {};
    double bandFc[kBands] = {}, bandQ[kBands] = {}, bandGain[kBands] = {};
    std::atomic<const Analysis*> srcA { nullptr }, srcB { nullptr };
    int blkPos = 0;
    uint64_t counter = 0;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> posFeed { -1.0f };
    std::array<std::atomic<float>, kK> partFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::prism
