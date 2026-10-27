// pump ORBIT engine. Framework-free.
//
// A voice integrates its system (RK4) with a time step of turn * f / sr, so one turn of the orbit takes one period of
// the note (turn: the calibrated mean time of a turn at this CHAOS); above about 1.2 kHz it takes several smaller steps
// a sample. SYNC: every period of the note the state is pulled back towards where it was at the start of the first
// period, over the first eighth of the period (smoothly, so the waveform stays continuous), by SYNC (at 100 % all the
// way: the same turn every period). The state, centred and scaled by the calibration, is listened to along a
// direction: AXIS goes x -> y -> z for the left channel, the right one is WIDTH further round (x -> y -> z -> x). Then a
// DC blocker, the drive and the ladder low-pass (per channel), the amp envelope. A second envelope (MOD) and DRIFT (a
// slow Lorenz system per voice, each starting elsewhere) move the cutoff, the chaos and (drift) the pitch.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "OrbitSystems.hpp"
#include <array>
#include <atomic>

namespace pump { namespace orbit {

struct Settings {
    int system = kRossler, voices = 8, voiceMode = 0, octave = 0;
    double chaos = 0.25, sync = 0.6, axis = 0.0, width = 0.3, drive = 0.2, cutoff = 6000.0, reso = 0.15, keytrack = 0.5;
    double envCut = 0.0, envChaos = 0.0, modA = 0.005, modD = 0.4, modS = 0.0, modR = 0.3;
    double driftRate = 0.3, driftPitch = 0.0, driftCut = 0.0, driftChaos = 0.0;
    double attack = 0.004, decay = 0.6, sustain = 0.8, release = 0.3, velocity = 0.5, glide = 0.0, fine = 0.0, volumeDb = 0.0;
};

// the direction along which the state is heard: phi 0 = x, 1 = y, 2 = z, 3 = x again (equal power between)
inline void direction(double phi, double w[3])
{
    phi = phi - 3.0 * std::floor(phi / 3.0);
    const int a = std::min(2, (int)phi), b = (a + 1) % 3;
    const double f = (phi - a) * kPi * 0.5;
    w[0] = w[1] = w[2] = 0.0;
    w[a] = std::cos(f); w[b] = std::sin(f);
}

class Engine {
public:
    static constexpr int kMaxVoices = 8, kCtl = 32, kTrail = 2048;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        (void)calTable();
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t)i];
            v = Voice();
            v.amp.setSampleRate(sr); v.mod.setSampleRate(sr);
            v.dcL.set(8.0, sr); v.dcR.set(8.0, sr);
            v.drift = { 1.0 + 3.1 * i, 2.0 - 1.7 * i, 20.0 + 0.9 * i };   // each voice drifts its own way
        }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        ctl = 0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        const bool sys = s.system != set.system;
        set = s;
        for (auto& v : voices) { v.amp.set(s.attack, s.decay, s.sustain, s.release); v.mod.set(s.modA, s.modD, s.modS, s.modR); }
        if (re) configure();
        if (sys) for (auto& v : voices) if (v.active) restart(v);
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    float chaosNow() const { return chaosFeed.load(std::memory_order_relaxed); }
    // the newest voice's path (centred and scaled x, y, z), oldest first; returns how many points were copied
    int trail(float (*dst)[3], int count) const
    {
        const uint32_t end = trailW.load(std::memory_order_acquire);
        count = std::min(count, (int)std::min<uint32_t>(end, kTrail - 64));
        for (int k = 0; k < count; ++k)
        {
            const uint32_t i = (end - (uint32_t)count + (uint32_t)k) & (kTrail - 1);
            dst[k][0] = trailBuf[i][0]; dst[k][1] = trailBuf[i][1]; dst[k][2] = trailBuf[i][2];
        }
        return count;
    }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        const double dg = 1.0 + 7.0 * set.drive * set.drive, dcomp = std::pow(dg, -0.3);
        double wl[3], wr[3];
        direction(2.0 * set.axis, wl); direction(2.0 * set.axis + 1.5 * set.width, wr);
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                step(v);
                const double nx = (v.s.x - v.cal.mean[0]) / v.cal.sd[0], ny = (v.s.y - v.cal.mean[1]) / v.cal.sd[1], nz = (v.s.z - v.cal.mean[2]) / v.cal.sd[2];
                if (vi == newest && (++trailDec >= trailEvery))
                {
                    trailDec = 0;
                    const uint32_t w = trailW.load(std::memory_order_relaxed);
                    float* t = trailBuf[w & (kTrail - 1)];
                    t[0] = (float)nx; t[1] = (float)ny; t[2] = (float)nz;
                    trailW.store(w + 1, std::memory_order_release);
                }
                double xl = 0.32 * (wl[0] * nx + wl[1] * ny + wl[2] * nz), xr = 0.32 * (wr[0] * nx + wr[1] * ny + wr[2] * nz);
                xl = v.dcL.process(xl); xr = v.dcR.process(xr);
                const double a = v.amp.next() * v.gain;
                v.mod.next();
                l += v.fL.process(xl, 0, dg) * dcomp * a;
                r += v.fR.process(xr, 0, dg) * dcomp * a;
                if (!v.amp.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; v.amp.kill(); v.fL.reset(); v.fR.reset(); } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (const Voice& v : voices)
        {
            if (!v.active) continue;
            ++act;
            if (v.amp.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
        }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Voice {
        bool active = false, haveRef = false;
        int note = 60, pullLeft = 0;
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, gain = 1.0, f = 261.6, chaos = 0.25, refChaos = -1.0, ph = 0.0, pullQ = 1.0, param = 2.5;
        double h = 0.0; int sub = 1;
        State s, ref, drift;
        Cal cal {};
        synth::Adsr amp, mod;
        synth::Ladder fL, fR;
        fx::DcBlocker dcL, dcR;
    };

    void configure()
    {
        if (set.voiceMode == 0) vm.setup(clampv(set.voices, 1, kMaxVoices), synth::kPoly);
        else vm.setup(1, set.voiceMode == 2 ? synth::kLegato : synth::kMono);
        vm.reset();
        for (auto& v : voices) { v.active = false; v.amp.kill(); }
    }
    // a voice starts on the attractor, at the calibration's point for its chaos
    void restart(Voice& v)
    {
        v.chaos = effChaos(v);
        v.cal = calAt(set.system, v.chaos);
        v.s = v.cal.on;
        v.haveRef = false; v.ph = 0.0; v.pullLeft = 0;
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
                v.active = true; v.note = c.note; v.target = c.note; v.age = ++counter;
                newest = vi;
                if (c.velocity >= 0.0f) v.vel = c.velocity;
                if (!(set.glide > 0.0 && was)) v.pitch = c.note;
                v.gain = (1.0 - set.velocity) + set.velocity * v.vel;
                if (c.velocity >= 0.0f || !was)
                {
                    v.amp.gate(true); v.mod.gate(true);
                    if (!was) { v.fL.reset(); v.fR.reset(); v.dcL.reset(); v.dcR.reset(); v.mod.kill(); v.mod.gate(true); }
                    restart(v);
                    tune(v);
                }
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.amp.gate(false); v.mod.gate(false); break;
            case synth::VoiceCmd::Kill: v.active = false; v.amp.kill(); break;
            }
        });
    }

    double driftN(const Voice& v, int k) const
    {
        // the drift system is Lorenz at rho 28: centre and spread from its known statistics
        static const double mean[3] = { 0.0, 0.0, 23.5 }, sd[3] = { 7.9, 9.0, 8.6 };
        return clampv((comp(v.drift, k) - mean[k]) / sd[k], -2.5, 2.5);
    }
    double effChaos(const Voice& v) const
    {
        return clampv(set.chaos + set.envChaos * v.mod.level() + 0.5 * set.driftChaos * driftN(v, 2), 0.0, 1.0);
    }
    void tune(Voice& v)
    {
        const Settings& s = set;
        const double semis = v.pitch + 12.0 * s.octave + s.fine * 0.01 + vm.pitchBend() * 2.0 + 2.0 * s.driftPitch * driftN(v, 0);
        v.f = std::min(synth::midiToHz(semis), 0.3 * sr);
        v.chaos = effChaos(v);
        v.cal = calAt(s.system, v.chaos);
        v.param = sysParam(s.system, v.chaos);
        const double dts = v.cal.turn * v.f / sr, hmax = v.cal.turn / 40.0;
        v.sub = clampv((int)std::ceil(dts / hmax), 1, 8);
        v.h = dts / v.sub;
        // the filter
        double fc = s.cutoff * std::exp2(s.keytrack * (semis - 60.0) / 12.0) * std::exp2(5.0 * s.envCut * v.mod.level()) * std::exp2(2.0 * s.driftCut * driftN(v, 1));
        fc = clampv(fc, 20.0, 0.45 * sr);
        v.fL.set(fc, s.reso, sr); v.fR.set(fc, s.reso, sr);
        // a new reference for SYNC when the chaos has moved on
        if (v.haveRef && std::fabs(v.chaos - v.refChaos) > 0.03) v.haveRef = false;
    }
    void control()
    {
        const double dt = kCtl / sr;
        const double gk = set.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / set.glide) : 1.0;
        const double dh = 0.78 * set.driftRate * dt;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            v.pitch += (v.target - v.pitch) * gk;
            rk4(0, v.drift, 28.0, dh);
            tune(v);
        }
        const Voice& nv = voices[(size_t)clampv(newest, 0, kMaxVoices - 1)];
        chaosFeed.store((float)(nv.active ? nv.chaos : set.chaos));
        trailEvery = std::max(1, (int)std::lround(sr / std::max(1.0, nv.f) / 96.0));
    }

    inline void step(Voice& v)
    {
        for (int k = 0; k < v.sub; ++k) rk4(set.system, v.s, v.param, v.h);
        if (!(std::fabs(v.s.x) + std::fabs(v.s.y) + std::fabs(v.s.z) < 1e4) || !std::isfinite(v.s.x + v.s.y + v.s.z)) { v.s = v.cal.on; v.haveRef = false; }
        // SYNC
        v.ph += v.f / sr;
        if (v.ph >= 1.0)
        {
            v.ph -= 1.0;
            if (!v.haveRef) { v.ref = v.s; v.refChaos = v.chaos; v.haveRef = true; }
            else if (set.sync > 0.0)
            {
                const int M = std::max(1, (int)(sr / v.f / 8.0));
                v.pullLeft = M;
                v.pullQ = std::pow(1.0 - std::min(set.sync, 0.999), 1.0 / M);
            }
        }
        if (v.pullLeft > 0)
        {
            --v.pullLeft;
            const double q = v.pullQ;
            v.s.x = v.ref.x + (v.s.x - v.ref.x) * q;
            v.s.y = v.ref.y + (v.s.y - v.ref.y) * q;
            if (set.system == kDuffing) v.s.z = v.ref.z + std::remainder(v.s.z - v.ref.z, kTwoPi) * q;
            else v.s.z = v.ref.z + (v.s.z - v.ref.z) * q;
        }
    }

    Settings set;
    double sr = 48000.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    uint64_t ctl = 0, counter = 0;
    int newest = 0, trailDec = 0, trailEvery = 2;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::atomic<float> chaosFeed { 0.0f };
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
    float trailBuf[kTrail][3] = {};
    std::atomic<uint32_t> trailW { 0 };
};

}} // namespace pump::orbit
