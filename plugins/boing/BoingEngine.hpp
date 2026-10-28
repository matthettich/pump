// pump BOING engine: eight drum pads, each a tuned spring (COIL's voice) and an analog-style drum voice, blended; a
// shared spring tank. Framework-free.
//
// A hit (MIDI note or pad): the spring is tuned to PITCH and kicked with the velocity, and left to ring for DECAY
// (retuning a ringing spring while it sounds smears it, so BEND is the analog layer's); the noise layer
// (white noise through a resonant filter at NOISE TONE, with DRIP as its resonance) runs through the spring as well,
// as snare wires would, and a little of it straight out. The analog voice: its oscillator with the same pitch drop,
// the same noise, CLICK (a 1 ms pulse), an exponential decay. HITS repeats the start 1-4 times, 11 ms apart (claps).
// The closed hat chokes the open one (CHOKE). Every pad's SEND feeds a real spring tank (SPRING's engine), returned
// at TANK LEVEL. Velocity sets loudness (VELOCITY sensitivity) and how hard the spring is kicked (brighter, more
// chirp).
#pragma once

#include "../coil/CoilEngine.hpp"
#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "BoingParams.hpp"
#include <array>
#include <atomic>

namespace pump { namespace boing {

struct PadSettings {
    int tank = 0, wave = kSine, hits = 1;
    double blend = 0.5, pitch = 48, decay = 0.3, harmonic = 0.3, spread = 0.1, drip = 50, bend = 0, bendTime = 0.03;
    double noise = 0.0, noiseTone = 0.5, click = 0.3, level = 0.0, pan = 0.0, send = 0.1;
};
struct Settings {
    PadSettings pad[kPads];
    int tankModel = 0;
    double tankDecay = 2.0, tankLevel = 0.4, velocity = 0.6, drive = 0.0, volumeDb = 0.0;
    bool choke = true;
};

class Engine {
public:
    uint32_t lastHit() const { return lastHitFeed.load(std::memory_order_relaxed); }   // pad | count << 8
    static constexpr int kCtl = spring::kTick;

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (int p = 0; p < kPads; ++p)
        {
            Pad& d = pads[(size_t)p];
            d = Pad();
            d.spring.prepare(sr, (uint32_t)(p + 11));
        }
        body.prepare(sr, maxBlock);
        bodyIn.assign((size_t)std::max(1024, maxBlock), 0.0f); bodyInR = bodyIn;
        decim = std::max(1, (int)std::lround(sr / 12000.0)); decimPos = 0;
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        dcL.set(15.0, sr); dcR.set(15.0, sr); dcL.reset(); dcR.reset();
        configure();
    }
    void setSettings(const Settings& s) { set = s; configure(); }
    void hit(int pad, double vel) { if (pad >= 0 && pad < kPads) start(pad, clampv(vel, 0.0, 1.0)); }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    std::array<ScopeRing, kPads> visV { ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024), ScopeRing(1024) };
    double visRate() const { return sr / decim; }
    float padLevel(int p) const { return levelFeed[(size_t)clampv(p, 0, kPads - 1)].load(std::memory_order_relaxed); }
    bool padActive(int p) const { return activeFeed[(size_t)clampv(p, 0, kPads - 1)].load(std::memory_order_relaxed); }
    uint32_t takeHits() { return hitFeed.exchange(0u, std::memory_order_relaxed); }

    // Any block size: a block longer than the buffers prepared for (some hosts send more than they announced, e.g.
    // when bouncing) is run in pieces, so nothing is resized on the audio thread.
    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        const int cap = (int)bodyIn.size();
        int done = 0, e = 0;
        while (done < n)
        {
            const int m = std::min(cap, n - done);
            int e2 = e; while (e2 < nev && (int)ev[e2].offset < done + m) ++e2;
            processChunk(L + done, R + done, m, ev + e, e2 - e, done);
            e = e2; done += m;
        }
    }

private:
    void processChunk(float* L, float* R, int n, const synth::Midi* ev, int nev, int evBase)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        const double drvG = 1.0 + set.drive * 6.0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset - evBase <= i) handle(ev[e++]);
            if (tickPos == 0) for (int p = 0; p < kPads; ++p) control(p);
            const double frac = tickPos / (double)kCtl;
            tickPos = (tickPos + 1) % kCtl;
            const bool visNow = ++decimPos >= decim;
            if (visNow) decimPos = 0;
            double l = 0.0, r = 0.0, sl = 0.0, sr2 = 0.0;
            for (int p = 0; p < kPads; ++p)
            {
                Pad& d = pads[(size_t)p];
                double sprOut = 0.0;
                const double y = padSample(p, d, frac, sprOut);
                if (visNow) visV[(size_t)p].push((float)sprOut);
                l += y * d.panL; r += y * d.panR;
                sl += y * d.panL * d.send; sr2 += y * d.panR * d.send;
            }
            bodyIn[(size_t)i] = (float)sl; bodyInR[(size_t)i] = (float)sr2;
            L[i] = (float)l; R[i] = (float)r;
        }
        while (e < nev) handle(ev[e++]);
        if (set.tankLevel > 0.001 || bodyOn)
        {
            body.process(bodyIn.data(), bodyInR.data(), n);
            bodyOn = set.tankLevel > 0.001 || ++bodyTail < (int)(8.0 * sr / std::max(1, n));
            if (set.tankLevel > 0.001) bodyTail = 0;
            for (int i = 0; i < n; ++i) { L[i] += (float)(bodyIn[(size_t)i] * set.tankLevel); R[i] += (float)(bodyInR[(size_t)i] * set.tankLevel); }
        }
        for (int i = 0; i < n; ++i)
        {
            double l = dcL.process(L[i]), r = dcR.process(R[i]);
            if (set.drive > 0.001) { l = std::tanh(l * drvG) / std::sqrt(drvG); r = std::tanh(r * drvG) / std::sqrt(drvG); }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            for (auto& d : pads) { d.spring.reset(); d.env = d.nenv = 0.0; d.nf.reset(); }
            body.recover(); dcL.reset(); dcR.reset();
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
        for (int p = 0; p < kPads; ++p)
        {
            const Pad& d = pads[(size_t)p];
            levelFeed[(size_t)p].store((float)std::max(d.env * 0.3, d.spring.level() * 40.0));
            activeFeed[(size_t)p].store(d.env > 1e-4 || d.spring.isActive());
        }
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }

    struct Pad {
        coil::Voice spring;
        coil::Settings cs;
        double vel = 1.0, amp = 1.0, t = 1e9, env = 0.0, envK = 0.999, nenv = 0.0, nenvK = 0.99, benv = 0.0, benvK = 0.99;
        double ph = 0.0, ph2 = 0.0, mph[6] = {}, panL = 0.7071, panR = 0.7071, send = 0.0, sprGain = 1.0;
        int hitsLeft = 0, hitGap = 0, hitCount = 0;
        bool bending = false;
        fx::Svf nf;
        fx::Rng rng { 1u };
    };

    void configure()
    {
        spring::Settings b;
        b.model = clampv(set.tankModel, 0, spring::kNumModels - 1); b.dwell = 50.0; b.decay = clampv(set.tankDecay, 0.3, 8.0);
        b.tension = 0.0; b.drip = 50.0; b.tone = 0.0; b.lowCut = 120.0; b.preDelay = 0.0; b.wobble = 15.0; b.width = 80.0; b.grit = 35.0;
        b.mix = 100.0; b.outDb = 0.0;
        body.setSettings(b);
        for (int p = 0; p < kPads; ++p)
        {
            Pad& d = pads[(size_t)p];
            const PadSettings& s = set.pad[p];
            coil::Settings& c = d.cs;
            c.model = clampv(s.tank, 0, spring::kNumModels - 1);
            c.harmonic = clampv(s.harmonic, 0.0, 1.0); c.spread = clampv(s.spread, 0.0, 1.0); c.drip = clampv(s.drip, 0.0, 100.0);
            c.decay = c.release = clampv(s.decay, 0.02, 10.0) * 1.2;
            c.wobble = 0.08; c.tone = 0.0; c.excite = clampv(0.3 + 0.7 * s.click, 0.0, 1.0); c.grit = 45.0; c.tension = 0.0;
            const double pan = clampv(s.pan, -1.0, 1.0);
            d.panL = std::cos(0.25 * kPi * (pan + 1.0)); d.panR = std::sin(0.25 * kPi * (pan + 1.0));
            d.send = clampv(s.send, 0.0, 1.0);
            d.envK = std::exp(std::log(1e-3) / std::max(1.0, s.decay * sr));
            d.nenvK = std::exp(std::log(1e-3) / std::max(1.0, s.decay * 0.6 * sr));
            d.benvK = std::exp(-1.0 / std::max(1.0, s.bendTime * sr / kCtl));   // per control tick
            const double q = 0.6 + clampv(s.drip, 0.0, 100.0) * 0.01 * 9.0;
            d.nf.set(250.0 * std::pow(56.0, clampv(s.noiseTone, 0.0, 1.0)), q, sr);
            if (d.spring.isActive()) d.spring.update(c);
        }
    }
    void handle(const synth::Midi& m)
    {
        const uint8_t st = m.status & 0xF0;
        if (st != 0x90 || m.d2 == 0) return;
        for (int p = 0; p < kPads; ++p) if (kPadInfo[p].note == m.d1) { start(p, m.d2 / 127.0); return; }
        // the General MIDI drum notes (the pads' notes before 2026-10-27, so older patterns still play), and a few
        // common alternatives: 35 kick, 40 snare, 41/45/47 low toms, 50 high tom, 44 pedal hat, 51/57 cymbals
        static const int alt[][2] = { { 36, 0 }, { 38, 1 }, { 39, 2 }, { 43, 3 }, { 48, 4 }, { 42, 5 }, { 46, 6 }, { 49, 7 },
                                      { 35, 0 }, { 40, 1 }, { 41, 3 }, { 45, 3 }, { 47, 4 }, { 50, 4 }, { 44, 5 }, { 51, 7 }, { 57, 7 } };
        for (const auto& a : alt) if (a[0] == m.d1) { start(a[1], m.d2 / 127.0); return; }
    }
    double hz(const Pad& d, const PadSettings& s) const { return synth::midiToHz(s.pitch + s.bend * d.benv); }
    void start(int p, double vel)
    {
        Pad& d = pads[(size_t)p];
        const PadSettings& s = set.pad[p];
        d.vel = vel;
        d.amp = (1.0 - set.velocity) + set.velocity * vel;
        d.hitsLeft = clampv(s.hits, 1, 4) - 1; d.hitGap = (int)(0.011 * sr); d.hitCount = 0;
        trigger(p, true);
        hitFeed.fetch_or(1u << p, std::memory_order_relaxed);
        noteLastHit(p);
        // the closed hat chokes the open one
        if (set.choke && p == 5)
        {
            Pad& o = pads[6];
            o.env = std::min(o.env, 0.02); o.nenv = std::min(o.nenv, 0.02);
            coil::Settings c = o.cs; c.decay = c.release = 0.03;
            if (o.spring.isActive()) o.spring.release(c);
        }
    }
    void trigger(int p, bool first)
    {
        Pad& d = pads[(size_t)p];
        const PadSettings& s = set.pad[p];
        d.t = 0.0; d.env = 1.0; d.nenv = 1.0;
        if (first) { d.benv = 1.0; d.bending = s.bend > 0.01; d.ph = 0.0; d.ph2 = 0.0; for (auto& m : d.mph) m = 0.0; }
        const double strength = (0.35 + 0.95 * d.vel) * (first ? 1.0 : 0.7);
        if (s.blend > 0.001)
        {
            if (first || !d.spring.isActive()) d.spring.start(36 + p, synth::midiToHz(s.pitch), strength, d.cs);
            else d.spring.kick(strength);
            d.spring.release(d.cs);   // one-shot: it rings for DECAY, not while a key is held
        }
    }
    void control(int p)
    {
        Pad& d = pads[(size_t)p];
        d.spring.tick();
        if (d.bending)
        {
            d.benv *= d.benvK;
            if (d.benv < 0.002) { d.benv = 0.0; d.bending = false; }
        }
    }

    inline double padSample(int p, Pad& d, double frac, double& sprOut)
    {
        const PadSettings& s = set.pad[p];
        if (d.hitsLeft > 0 && d.t * sr >= d.hitGap * (d.hitCount + 1)) { --d.hitsLeft; ++d.hitCount; trigger(p, false); d.t = d.hitCount * d.hitGap / sr; }
        const bool live = d.env > 1e-5 || d.nenv > 1e-5;
        // noise through its resonant filter (band-pass below the middle of NOISE TONE's range, high-pass above)
        double nz = 0.0;
        if (s.noise > 0.001 && d.nenv > 1e-5)
        {
            double lp, bp, hp;
            d.nf.process(d.rng.bi(), lp, bp, hp);
            const double tone = clampv(s.noiseTone, 0.0, 1.0);
            nz = (bp * d.nf.k * (1.0 - tone) + hp * tone) * d.nenv * s.noise;
            d.nenv *= d.nenvK;
        }
        // the spring layer (it hears the noise)
        double spr = 0.0;
        if (s.blend > 0.001 && d.spring.isActive())
        {
            double a = 0.0, b = 0.0;
            d.spring.process(nz * 0.6, frac, a, b);
            spr = 0.5 * (a + b);
        }
        sprOut = spr * 3.0;
        // the analog layer
        double ana = 0.0;
        if (live && s.blend < 0.999)
        {
            const double f = synth::midiToHz(s.pitch + s.bend * d.benv) / sr;
            const double h = clampv(s.harmonic, 0.0, 1.0), sp = clampv(s.spread, 0.0, 1.0);
            double o = 0.0;
            switch (s.wave)
            {
            case kTri: o = 1.0 - 4.0 * std::fabs(d.ph - 0.5); break;
            case kFold: o = std::sin(kPi * (1.0 + 4.0 * h) * std::sin(kTwoPi * d.ph)); break;
            case kSquare: o = std::tanh(std::sin(kTwoPi * d.ph) * (1.5 + 10.0 * h)); break;
            case kSaw: o = synth::blepSaw(d.ph, f); break;
            case kMetal:
            {
                static const double ratio[6] = { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
                for (int k = 0; k < 6; ++k)
                {
                    const double rr = 1.0 + (ratio[k] - 1.0) * (0.4 + 1.2 * sp);
                    d.mph[k] += f * rr; d.mph[k] -= std::floor(d.mph[k]);
                    o += d.mph[k] < 0.5 ? 1.0 : -1.0;
                }
                o /= 6.0;
                break;
            }
            default: o = std::sin(kTwoPi * d.ph); break;
            }
            if (s.wave != kMetal && sp > 0.001)
            {
                // a second oscillator, detuned up by SPREAD (a beating, fatter tone)
                d.ph2 += f * (1.0 + sp * 0.06); d.ph2 -= std::floor(d.ph2);
                o = 0.7 * o + 0.5 * sp * std::sin(kTwoPi * d.ph2);
            }
            d.ph += f; d.ph -= std::floor(d.ph);
            const double click = d.t < 0.0012 ? s.click * 0.5 * (1.0 - std::cos(kTwoPi * d.t / 0.0012)) : 0.0;
            ana = o * d.env * 0.8 + click;
        }
        if (live) { d.env *= d.envK; d.t += 1.0 / sr; }
        const double y = ((1.0 - s.blend) * ana + s.blend * spr * d.sprGain * 3.0 + nz * (1.0 - 0.6 * s.blend)) * d.amp * dbToGainFast(s.level);
        return y;
    }
    static inline double dbToGainFast(double db) { return std::exp(db * 0.11512925464970229); }

    Settings set;
    double sr = 48000.0;
    std::array<Pad, kPads> pads {};
    spring::Engine body;
    std::vector<float> bodyIn, bodyInR;
    bool bodyOn = false;
    int bodyTail = 0, tickPos = 0, decim = 4, decimPos = 0;
    fx::DcBlocker dcL, dcR;
    LinearSmoother volS;
    std::array<std::atomic<float>, kPads> levelFeed {};
    std::array<std::atomic<bool>, kPads> activeFeed {};
    std::atomic<uint32_t> hitFeed { 0 };
    // the last pad hit (by MIDI or the window): pad | count << 8, for the window to follow
    std::atomic<uint32_t> lastHitFeed { 0u };
    void noteLastHit(int p) { lastHitFeed.store((uint32_t)p | (((lastHitFeed.load(std::memory_order_relaxed) >> 8) + 1u) << 8), std::memory_order_relaxed); }
};

}} // namespace pump::boing
