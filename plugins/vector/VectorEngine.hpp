// pump VECTOR engine. Framework-free.
//
// Per voice: four sources, mixed by a point on the square (bilinear: A (1-x)(1-y), B x(1-y), C (1-x)y, D xy).
// The point is the joystick (X, Y), moved toward the vector envelope's path by VECTOR ENVELOPE and swayed by the LFO.
// A WAVE source reads its wave from the bank at the band limit for its pitch. A SEQUENCE source steps through the bank
// from its wave, STRIDE waves a step, RATE ms each, crossfading over the last XFADE of each step, transposing each
// step by TRANSPOSE semitones (wrapped to two octaves). A SCANNED source is a ring of 64 masses joined by springs
// (TENSION), pulled to rest (CENTRING) and losing energy (DAMPING); at the note it is struck into the shape of its
// wave (HAMMER: how much, and how hard), then it moves at SPEED (3 to 120 steps a second) while it is read round once per
// period of the note: every fourth control tick its shape is turned into up to 32 harmonics (fewer at high
// notes, so nothing aliases) and a 256-point cycle, crossfaded from the last one until the next.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "VectorWaves.hpp"
#include <array>
#include <atomic>

namespace pump { namespace vector {

struct SrcSettings { int type = kWave, wave = 2, tune = 0; double fine = 0.0, level = 1.0; };
struct Settings {
    std::array<SrcSettings, kSources> src {};
    double x = 0.5, y = 0.5, vecAmt = 0.0, vecTime = 0.8; int vecLoop = kVecOnce;
    double px[4] = { 0, 1, 1, 0 }, py[4] = { 0, 0, 1, 1 };
    double seqRate = 0.18, seqXfade = 0.5; int seqSteps = 8, seqStride = 1, seqLoop = kSeqLoop, seqTranspose = 0;
    double scanTension = 0.5, scanDamp = 0.3, scanCentre = 0.2, scanHammer = 0.6, scanSpeed = 0.4;
    double cutoff = 9000.0, reso = 0.1, envCut = 0.0, keytrack = 0.5, fA = 0.005, fD = 0.5, fS = 0.3, fR = 0.4;
    double attack = 0.008, decay = 0.8, sustain = 0.8, release = 0.5;
    double lfoRate = 0.5, lfoPitch = 0.0, lfoX = 0.0, lfoY = 0.0, lfoCut = 0.0;
    double velocity = 0.5, spread = 0.5, glide = 0.0; int voices = 8, voiceMode = 0, octave = 0;
    double volumeDb = 0.0;
};

constexpr int kMasses = 64, kScanH = 31, kScanTab = 256, kScanEvery = 4;

class Engine {
public:
    static constexpr int kMaxVoices = 8, kCtl = 32;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        (void)WaveBank::get();
        for (int k = 0; k < kScanTab; ++k) for (int h = 0; h < kScanH; ++h) { cosT[k][h] = (float)std::cos(kTwoPi * (h + 1) * k / kScanTab); sinT[k][h] = (float)std::sin(kTwoPi * (h + 1) * k / kScanTab); }
        for (int i = 0; i < kMaxVoices; ++i) { voices[(size_t)i] = Voice(); voices[(size_t)i].amp.setSampleRate(sr); voices[(size_t)i].fenv.setSampleRate(sr); }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
        lfoPh = 0.0; ctl = 0;
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        for (auto& v : voices) { v.amp.set(s.attack, s.decay, s.sustain, s.release); v.fenv.set(s.fA, s.fD, s.fS, s.fR); }
        if (re) configure();
    }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    float pointX() const { return pxFeed.load(std::memory_order_relaxed); }
    float pointY() const { return pyFeed.load(std::memory_order_relaxed); }
    int seqStep(int s) const { return stepFeed[(size_t)clampv(s, 0, kSources - 1)].load(std::memory_order_relaxed); }
    int waveNow(int s) const { return waveFeed[(size_t)clampv(s, 0, kSources - 1)].load(std::memory_order_relaxed); }
    float massAt(int i) const { return massFeed[(size_t)clampv(i, 0, kMasses - 1)].load(std::memory_order_relaxed); }
    int scannedSource() const { return scanSrcFeed.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            if ((ctl++ % kCtl) == 0) control();
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                double sl = 0.0, srr = 0.0;
                const double tf = (double)(v.scanTick * kCtl + v.tick) / (kScanEvery * kCtl);   // through the scanned crossfade
                for (int s = 0; s < kSources; ++s)
                {
                    Src& q = v.src[(size_t)s];
                    if (q.w <= 1e-5) { q.ph += q.inc; q.ph -= std::floor(q.ph); continue; }
                    double y;
                    if (set.src[(size_t)s].type == kScanned)
                    {
                        const double x = q.ph * kScanTab; const int k = (int)x; const double f = x - k;
                        const int k1 = (k + 1) & (kScanTab - 1);
                        const double a = q.scanPrev[(size_t)k] + (q.scanPrev[(size_t)k1] - q.scanPrev[(size_t)k]) * f;
                        const double b = q.scanCur[(size_t)k] + (q.scanCur[(size_t)k1] - q.scanCur[(size_t)k]) * f;
                        y = a + (b - a) * tf;
                    }
                    else
                    {
                        const WaveBank& wb = WaveBank::get();
                        y = wb.read(q.wave, q.mip, q.ph);
                        if (q.xf > 0.0) y += (wb.read(q.wave2, q.mip, q.ph) - y) * q.xf;
                    }
                    q.ph += q.inc; q.ph -= std::floor(q.ph);
                    sl += y * q.w * q.gl; srr += y * q.w * q.gr;
                }
                ++v.tick;
                const double a = v.amp.next() * v.gain;
                v.fenv.next();
                // into the ladder gently (it saturates), and back up after
                l += v.fL.process(sl * 0.25, 0, 1.0) * 4.0 * a;
                r += v.fR.process(srr * 0.25, 0, 1.0) * 4.0 * a;
                if (!v.amp.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol * 0.5); R[i] = (float)(r * vol * 0.5);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; v.amp.kill(); v.fL.reset(); v.fR.reset(); } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (const Voice& v : voices) { if (!v.active) continue; ++act; if (v.amp.state() != synth::Adsr::Release) lit[(v.note >> 6) & 1] |= 1ull << (v.note & 63); }
        activeFeed.store(act); litFeed[0].store(lit[0]); litFeed[1].store(lit[1]);
        const Voice& nv = voices[(size_t)newest];
        pxFeed.store((float)(nv.active ? nv.px : set.x)); pyFeed.store((float)(nv.active ? nv.py : set.y));
        int scanSrc = -1;
        for (int s = 0; s < kSources; ++s)
        {
            stepFeed[(size_t)s].store(nv.active ? nv.src[(size_t)s].step : -1);
            waveFeed[(size_t)s].store(nv.active ? nv.src[(size_t)s].wave : set.src[(size_t)s].wave);
            if (scanSrc < 0 && set.src[(size_t)s].type == kScanned) scanSrc = s;
        }
        scanSrcFeed.store(scanSrc);
        if (scanSrc >= 0) for (int k = 0; k < kMasses; ++k) massFeed[(size_t)k].store(nv.active ? (float)nv.src[(size_t)scanSrc].mx[(size_t)k] : 0.0f);
        for (int i = 0; i < n; ++i) outRing.push(0.5f * (L[i] + R[i]));
    }
    void uiNote(uint8_t nn, uint8_t vel) { synth::Midi m { 0, (uint8_t)(vel ? 0x90 : 0x80), nn, vel }; handle(m); }

private:
    struct Src {
        double ph = 0.0, inc = 0.0, w = 0.0, gl = 0.7071, gr = 0.7071, xf = 0.0;
        int wave = 0, wave2 = 0, mip = 0, step = 0, dir = 1;
        double stepT = 0.0, scanAcc = 0.0, scanNorm = 1.0;
        std::array<double, kMasses> mx {}, mv {};
        std::array<float, kScanTab> scanPrev {}, scanCur {};
    };
    struct Voice {
        bool active = false;
        int note = 60, tick = 0, scanTick = 0;
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, gain = 1.0, vt = 0.0, px = 0.5, py = 0.5;
        std::array<Src, kSources> src {};
        synth::Adsr amp, fenv;
        synth::Ladder fL, fR;
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
                    if (!was) { v.fL.reset(); v.fR.reset(); }
                    v.amp.gate(true); v.fenv.gate(true);
                    v.vt = 0.0;
                    for (int s = 0; s < kSources; ++s) strike(v.src[(size_t)s], s, !was);
                    v.tick = kCtl; v.scanTick = kScanEvery - 1;
                }
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.amp.gate(false); v.fenv.gate(false); break;
            case synth::VoiceCmd::Kill: v.active = false; v.amp.kill(); break;
            }
        });
    }
    // a source at the start of a note: the sequence from its first step, the scanned string struck
    void strike(Src& q, int s, bool freshVoice)
    {
        const SrcSettings& ss = set.src[(size_t)s];
        if (freshVoice) q.ph = 0.25 * s;
        q.step = 0; q.dir = 1; q.stepT = 0.0; q.xf = 0.0; q.wave = q.wave2 = ss.wave;
        if (ss.type == kScanned)
        {
            float sh[kMasses];
            WaveBank::get().shape(ss.wave, sh, kMasses);
            const double h = clampv(set.scanHammer, 0.0, 1.0);
            double pk = 1e-6;
            for (int k = 0; k < kMasses; ++k)
            {
                // the hammer: the wave's shape, plus (harder) a kick to the masses under it
                q.mx[(size_t)k] = (1.0 - 0.5 * h) * q.mx[(size_t)k] * (freshVoice ? 0.0 : 1.0) + (0.4 + 0.6 * h) * sh[k];
                q.mv[(size_t)k] = h * 0.3 * sh[(k + kMasses / 4) % kMasses];
                pk = std::max(pk, std::fabs(q.mx[(size_t)k]));
            }
            q.scanNorm = 0.9 / pk;
            q.scanAcc = 0.0;
            scanTable(q, 1e9);
            q.scanPrev = q.scanCur;
        }
    }
    // the ring's shape -> up to H harmonics -> a 256-point cycle
    void scanTable(Src& q, double f)
    {
        const int H = clampv((int)(0.45 * sr / std::max(1.0, f)), 1, kScanH);
        double re[kScanH], im[kScanH];
        for (int h = 0; h < H; ++h)
        {
            double a = 0.0, b = 0.0;
            for (int k = 0; k < kMasses; ++k) { const int j = (k * kScanTab / kMasses) & (kScanTab - 1); a += q.mx[(size_t)k] * cosT[j][h]; b += q.mx[(size_t)k] * sinT[j][h]; }
            re[h] = 2.0 * a / kMasses; im[h] = 2.0 * b / kMasses;
        }
        q.scanPrev = q.scanCur;
        for (int k = 0; k < kScanTab; ++k)
        {
            double y = 0.0;
            for (int h = 0; h < H; ++h) y += re[h] * cosT[k][h] + im[h] * sinT[k][h];
            q.scanCur[(size_t)k] = (float)(y * q.scanNorm);
        }
    }
    // the ring moves: symplectic Euler (stable for these springs: 4 k + c < 4)
    void scanStep(Src& q, double stepsPerSecond)
    {
        const double k = 0.02 + 0.9 * set.scanTension * set.scanTension, c = 0.001 + 0.25 * set.scanCentre * set.scanCentre;
        // DAMPING: the ring loses 0.05 to 8 nepers a second, whatever its speed
        const double d = std::exp(-(0.05 + 8.0 * set.scanDamp * set.scanDamp) / stepsPerSecond);
        for (int i = 0; i < kMasses; ++i)
        {
            const double l = q.mx[(size_t)((i + kMasses - 1) % kMasses)], r = q.mx[(size_t)((i + 1) % kMasses)], x = q.mx[(size_t)i];
            q.mv[(size_t)i] = (q.mv[(size_t)i] + k * (l + r - 2.0 * x) - c * x) * d;
        }
        for (int i = 0; i < kMasses; ++i) q.mx[(size_t)i] += q.mv[(size_t)i];
    }

    // the vector envelope's point at time t (seconds)
    void pathAt(double t, double& x, double& y) const
    {
        const double T = std::max(0.01, set.vecTime);
        double u = t / T;   // segments: 0 P1->P2, 1 P2->P3, 2 P3->P4, then (loop) 3 P4->P1 ...
        int a, b;
        if (set.vecLoop == kVecOnce) { u = std::min(u, 3.0); const int k = std::min(2, (int)u); a = k; b = k + 1; u -= k; }
        else if (set.vecLoop == kVecRepeat) { u = std::fmod(u, 4.0); const int k = (int)u; a = k; b = (k + 1) & 3; u -= k; }
        else { u = std::fmod(u, 6.0); if (u < 3.0) { const int k = (int)u; a = k; b = k + 1; u -= k; } else { u -= 3.0; const int k = (int)u; a = 3 - k; b = 2 - k; u -= k; } }
        x = set.px[a] + (set.px[b] - set.px[a]) * u;
        y = set.py[a] + (set.py[b] - set.py[a]) * u;
    }

    void control()
    {
        const Settings& s = set;
        const double dt = kCtl / sr;
        lfoPh += s.lfoRate * dt; lfoPh -= std::floor(lfoPh);
        const double lfo = std::sin(kTwoPi * lfoPh);
        const double gk = s.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / s.glide) : 1.0;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            v.tick = 0;
            const bool rebuild = ++v.scanTick >= kScanEvery;
            if (rebuild) v.scanTick = 0;
            v.pitch += (v.target - v.pitch) * gk;
            v.vt += dt;
            // the point
            double ex, ey; pathAt(v.vt, ex, ey);
            v.px = clampv(s.x + (ex - s.x) * s.vecAmt + 0.5 * s.lfoX * lfo, 0.0, 1.0);
            v.py = clampv(s.y + (ey - s.y) * s.vecAmt + 0.5 * s.lfoY * std::cos(kTwoPi * lfoPh), 0.0, 1.0);
            const double wts[4] = { (1 - v.px) * (1 - v.py), v.px * (1 - v.py), (1 - v.px) * v.py, v.px * v.py };
            const double base = v.pitch + 12.0 * s.octave + vm.pitchBend() * 2.0 + 0.5 * s.lfoPitch * lfo;
            for (int si = 0; si < kSources; ++si)
            {
                Src& q = v.src[(size_t)si];
                const SrcSettings& ss = s.src[(size_t)si];
                double semis = base + ss.tune + ss.fine * 0.01;
                if (ss.type == kSequence)
                {
                    // the sequence's clock
                    q.stepT += dt;
                    const double st = std::max(0.01, s.seqRate);
                    const int steps = clampv(s.seqSteps, 1, 16);
                    while (q.stepT >= st)
                    {
                        q.stepT -= st;
                        if (s.seqLoop == kSeqLoop) q.step = (q.step + 1) % steps;
                        else if (s.seqLoop == kSeqOnce) q.step = std::min(steps - 1, q.step + 1);
                        else { if (steps > 1) { q.step += q.dir; if (q.step >= steps - 1) { q.step = steps - 1; q.dir = -1; } else if (q.step <= 0) { q.step = 0; q.dir = 1; } } }
                    }
                    auto waveOf = [&](int k) { return ((ss.wave + k * s.seqStride) % kNumWaves + kNumWaves) % kNumWaves; };
                    int next = q.step + q.dir;
                    if (s.seqLoop == kSeqLoop) next = (q.step + 1) % steps; else if (s.seqLoop == kSeqOnce) next = std::min(steps - 1, q.step + 1);
                    next = clampv(next, 0, steps - 1);
                    q.wave = waveOf(q.step); q.wave2 = waveOf(next);
                    const double xf = clampv(s.seqXfade, 0.0, 1.0), ph = q.stepT / st;
                    q.xf = xf > 0.0 ? clampv((ph - (1.0 - xf)) / xf, 0.0, 1.0) : 0.0;
                    int tr = q.step * s.seqTranspose;
                    while (tr > 24) tr -= 24;
                    while (tr < -24) tr += 24;
                    semis += tr;
                }
                else { q.wave = q.wave2 = ss.wave; q.xf = 0.0; q.step = -1; }
                const double f = synth::midiToHz(semis);
                q.inc = std::min(f, 0.45 * sr) / sr;
                q.mip = WaveBank::mipFor(q.inc);
                q.w = wts[si] * ss.level;
                const double pan = ((si & 1) ? 1.0 : -1.0) * s.spread * 0.8;
                q.gl = std::sqrt(0.5 * (1.0 - pan)); q.gr = std::sqrt(0.5 * (1.0 + pan));
                if (ss.type == kScanned && rebuild)
                {
                    // SPEED: the ring takes 3 to 120 steps a second, so its slowest motion stays below about 10 Hz
                    // (faster, and the motion would become a pitch of its own)
                    const double sps = 3.0 + 117.0 * s.scanSpeed * s.scanSpeed;
                    q.scanAcc += sps * dt * kScanEvery;
                    while (q.scanAcc >= 1.0) { q.scanAcc -= 1.0; scanStep(q, sps); }
                    scanTable(q, f);
                }
            }
            // the filter
            double fc = s.cutoff * std::exp2(s.keytrack * (v.pitch + 12.0 * s.octave - 60.0) / 12.0) * std::exp2(5.0 * s.envCut * v.fenv.level()) * std::exp2(2.0 * s.lfoCut * lfo);
            fc = clampv(fc, 20.0, 0.45 * sr);
            v.fL.set(fc, s.reso, sr); v.fR.set(fc, s.reso, sr);
        }
    }

    Settings set;
    double sr = 48000.0, lfoPh = 0.0;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    uint64_t ctl = 0, counter = 0;
    int newest = 0;
    float cosT[kScanTab][kScanH] = {}, sinT[kScanTab][kScanH] = {};
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 }, scanSrcFeed { -1 };
    std::atomic<float> pxFeed { 0.5f }, pyFeed { 0.5f };
    std::array<std::atomic<int>, kSources> stepFeed {}, waveFeed {};
    std::array<std::atomic<float>, kMasses> massFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::vector
