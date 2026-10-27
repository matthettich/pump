// pump FREEZE engine. Framework-free.
//
// The sound is cut into spectra: 4096-point Hann windows every 1024 samples, magnitudes kept (Spectra). A voice makes
// a new spectrum every 1024 samples (a hop): GRAINS frames are picked around its position (SPRAY: random offsets up to
// a quarter of the sound either way) and averaged in power, blurred into the last hop's (BLUR), mapped to the note
// (each output bin reads the source at k / ratio, the ratio from the note against the source's root, stretched about
// the root by STRETCH), tilted (6 dB an octave at 100 %), jittered (SHIMMER), band-limited (LOW, HIGH). Its phases:
// PHASE 0 draws every bin's phase afresh each hop (the classic frozen wash); toward 100 each bin's phase advances as a
// steady tone at its own frequency would (a phase vocoder: each bin's true frequency is kept from the analysis, from
// how its phase moved hop to hop), with less and less jitter. The right channel's phases jitter on their own by
// WIDTH. Each channel is inverse transformed, windowed again and overlap-added (the windows sum to 1.5 at this hop:
// divided out). The amp envelope follows. A note sounds a hop or so after it is played.
#pragma once

#include "dsp/Synth.hpp"
#include "dsp/Fx.hpp"
#include "dsp/FFT.hpp"
#include "FreezeParams.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace pump { namespace freeze {

constexpr int kN = 4096, kHop = 1024, kBins = kN / 2 + 1;

struct Spectra {
    std::string name;
    double sr = 48000.0, seconds = 0.0;
    int frames = 0, root = 60;
    std::vector<float> mag;          // frames x kBins
    std::vector<float> freq;         // frames x kBins: each bin's own frequency (Hz, from its phase hop to hop)
    std::vector<float> overview;     // 1024 points of the waveform's peak (for the window)
    float maxMag = 1e-9f;
    const float* frame(int f) const { return mag.data() + (size_t)clampv(f, 0, std::max(0, frames - 1)) * kBins; }
    const float* freqs(int f) const { return freq.data() + (size_t)clampv(f, 0, std::max(0, frames - 1)) * kBins; }
};

inline std::shared_ptr<Spectra> makeSpectra(const std::vector<float>& x, double sr, const std::string& name, int root)
{
    auto s = std::make_shared<Spectra>();
    s->name = name; s->sr = sr; s->root = root; s->seconds = x.size() / sr;
    const int frames = std::max(1, (int)((x.size() + kHop - 1) / kHop));
    s->frames = frames;
    s->mag.assign((size_t)frames * kBins, 0.0f);
    s->freq.assign((size_t)frames * kBins, 0.0f);
    std::vector<double> lastPh(kBins, 0.0);
    RealFFT fft(kN);
    std::vector<float> in(kN), out(kN), win(kN);
    for (int i = 0; i < kN; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * i / kN));
    for (int f = 0; f < frames; ++f)
    {
        const long c = (long)f * kHop - kN / 2;   // the frame is centred on hop f
        for (int i = 0; i < kN; ++i) { const long j = c + i; in[(size_t)i] = (j >= 0 && j < (long)x.size()) ? x[(size_t)j] * win[(size_t)i] : 0.0f; }
        fft.forward(in.data(), out.data());
        float* m = s->mag.data() + (size_t)f * kBins;
        m[0] = std::fabs(out[0]); m[kBins - 1] = std::fabs(out[1]);
        float* fq = s->freq.data() + (size_t)f * kBins;
        for (int k = 1; k < kBins - 1; ++k)
        {
            const double re = out[(size_t)(2 * k)], im = out[(size_t)(2 * k + 1)];
            m[k] = (float)std::sqrt(re * re + im * im);
            // the phase vocoder's frequency: the bin's centre plus how far its phase strayed from it over the hop
            const double ph = std::atan2(im, re), expect = kTwoPi * k * kHop / kN;
            double d = ph - lastPh[(size_t)k] - expect;
            d -= kTwoPi * std::round(d / kTwoPi);
            lastPh[(size_t)k] = ph;
            fq[k] = (float)((expect + d) / kHop * sr / kTwoPi);
            if (f == 0) fq[k] = (float)(k * sr / kN);
        }
    }
    // every moment as loud as the loudest (up to 30 dB of make-up): a frozen moment late in a bell's ring is as full
    // as its strike (the overview below keeps the real shape)
    {
        std::vector<double> e((size_t)frames, 0.0);
        double emax = 1e-20;
        for (int f = 0; f < frames; ++f) { const float* m = s->mag.data() + (size_t)f * kBins; for (int k = 0; k < kBins; ++k) e[(size_t)f] += (double)m[k] * m[k]; emax = std::max(emax, e[(size_t)f]); }
        s->maxMag = 1e-9f;
        for (int f = 0; f < frames; ++f)
        {
            const double g = std::min(31.6, std::sqrt(emax / std::max(1e-30, e[(size_t)f])));
            float* m = s->mag.data() + (size_t)f * kBins;
            for (int k = 0; k < kBins; ++k) { m[k] = (float)(m[k] * g); s->maxMag = std::max(s->maxMag, m[k]); }
        }
    }
    s->overview.assign(1024, 0.0f);
    for (size_t i = 0; i < x.size(); ++i) { const size_t b = i * 1024 / std::max<size_t>(1, x.size()); s->overview[b] = std::max(s->overview[b], std::fabs(x[i])); }
    return s;
}

struct Settings {
    double position = 0.3, speed = 0.0, spray = 0.1, blur = 0.3, track = 1.0, stretch = 0.0, tilt = 0.0, shimmer = 0.1, low = 30.0, high = 16000.0, phase = 0.0, width = 0.7;
    int grains = 3, root = 60, voices = 8, voiceMode = 0, octave = 0;
    double attack = 0.3, decay = 0.8, sustain = 1.0, release = 1.5, velocity = 0.4, glide = 0.0, volumeDb = 0.0;
};

class Engine {
public:
    static constexpr int kMaxVoices = 8;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        fft.setup(kN);
        bufA.assign(kN, 0.0f); bufB.assign(kN, 0.0f); tmpA.assign(kN, 0.0f); tmpB.assign(kN, 0.0f);
        win.assign(kN, 0.0f); for (int i = 0; i < kN; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * i / kN));
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t)i];
            v = Voice();
            v.amp.setSampleRate(sr);
            v.rng.seed(0xF2EEu + 7919u * (uint32_t)i);
            v.cur.assign(kBins, 0.0f); v.phL.assign(kBins, 0.0f); v.phR.assign(kBins, 0.0f);
            v.olaL.assign(kN, 0.0f); v.olaR.assign(kN, 0.0f);
        }
        configure();
        volS.setRamp(std::max(1, (int)(0.02 * sr))); volS.reset(dbToGain(set.volumeDb));
    }
    void setSettings(const Settings& s)
    {
        const bool re = s.voiceMode != set.voiceMode || s.voices != set.voices;
        set = s;
        for (auto& v : voices) v.amp.set(s.attack, s.decay, s.sustain, s.release);
        if (re) configure();
    }
    // the sound to play (nullptr: silence); it must stay alive while it may be in use
    void setSource(const Spectra* s) { src = s; }

    // ---- feeds (UI) ----
    ScopeRing outRing { 1 << 13 };
    int voicesNow() const { return activeFeed.load(std::memory_order_relaxed); }
    bool noteLit(int n) const { return n >= 0 && n < 128 && ((litFeed[(n >> 6) & 1].load(std::memory_order_relaxed) >> (n & 63)) & 1); }
    float positionOf(int v) const { return posFeed[(size_t)clampv(v, 0, kMaxVoices - 1)].load(std::memory_order_relaxed); }   // -1: off
    // the newest voice's spectrum now, at 256 log-spaced points from 20 Hz to 20 kHz (0..1, relative to the sound's loudest)
    float spectrumAt(int i) const { return specFeed[(size_t)clampv(i, 0, 255)].load(std::memory_order_relaxed); }

    void process(float* L, float* R, int n, const synth::Midi* ev, int nev)
    {
        volS.setTarget(dbToGain(set.volumeDb));
        int e = 0;
        const Spectra* S = src;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && (int)ev[e].offset <= i) handle(ev[e++]);
            double l = 0.0, r = 0.0;
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t)vi];
                if (!v.active) continue;
                if (v.hopPos >= kHop) { v.hopPos = 0; if (S && S->frames > 0) hop(v, *S, vi == newest); }
                const double a = v.amp.next() * v.gain;
                l += v.olaL[(size_t)v.hopPos] * a; r += v.olaR[(size_t)v.hopPos] * a;
                ++v.hopPos;
                if (!v.amp.active()) { v.active = false; if (vi < vm.voices()) vm.voiceFinished(vi); }
            }
            const double vol = volS.next();
            L[i] = (float)(l * vol); R[i] = (float)(r * vol);
        }
        while (e < nev) handle(ev[e++]);
        double chk = 0.0; for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk)) { for (auto& v : voices) { v.active = false; v.amp.kill(); std::fill(v.olaL.begin(), v.olaL.end(), 0.0f); std::fill(v.olaR.begin(), v.olaR.end(), 0.0f); std::fill(v.cur.begin(), v.cur.end(), 0.0f); } sanitizeBlock(L, n); sanitizeBlock(R, n); }
        int act = 0; uint64_t lit[2] = { 0, 0 };
        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            const Voice& v = voices[(size_t)vi];
            posFeed[(size_t)vi].store(v.active ? (float)v.pos : -1.0f);
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
        bool active = false, fresh = true;
        int note = 60, hopPos = kHop;
        uint64_t age = 0;
        double vel = 1.0, pitch = 60.0, target = 60.0, gain = 1.0, pos = 0.3;
        std::vector<float> cur, phL, phR, olaL, olaR;
        synth::Adsr amp;
        fx::Rng rng { 1u };
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
                    if (!was)
                    {
                        v.fresh = true; v.hopPos = kHop; v.pos = clampv(set.position, 0.0, 1.0);
                        std::fill(v.olaL.begin(), v.olaL.end(), 0.0f); std::fill(v.olaR.begin(), v.olaR.end(), 0.0f);
                        for (int k = 0; k < kBins; ++k) { v.phL[(size_t)k] = (float)(kTwoPi * v.rng.uni()); v.phR[(size_t)k] = v.phL[(size_t)k]; }
                    }
                }
                break;
            }
            case synth::VoiceCmd::Glide: v.target = c.note; v.note = c.note; break;
            case synth::VoiceCmd::Release: v.amp.gate(false); break;
            case synth::VoiceCmd::Kill: v.active = false; v.amp.kill(); break;
            }
        });
    }

    // one hop of a voice: shift its overlap-add buffers on, make the next spectrum, add its two frames in
    void hop(Voice& v, const Spectra& S, bool feed)
    {
        const Settings& s = set;
        const double dt = (double)kHop / sr;
        // glide, and the position moving on
        const double gk = s.glide > 0.0 ? 1.0 - std::exp(-dt * 3000.0 / s.glide) : 1.0;
        v.pitch += (v.target - v.pitch) * gk;
        if (S.seconds > 0.0) { v.pos += s.speed * dt / S.seconds; v.pos -= std::floor(v.pos); }
        // the buffers: drop the hop just played
        std::memmove(v.olaL.data(), v.olaL.data() + kHop, sizeof(float) * (kN - kHop)); std::fill(v.olaL.begin() + (kN - kHop), v.olaL.end(), 0.0f);
        std::memmove(v.olaR.data(), v.olaR.data() + kHop, sizeof(float) * (kN - kHop)); std::fill(v.olaR.begin() + (kN - kHop), v.olaR.end(), 0.0f);
        // the grains: frames around the position, averaged in power
        const int G = clampv(s.grains, 1, 8);
        std::array<const float*, 8> fr {};
        const float* fq0 = nullptr;
        for (int g = 0; g < G; ++g)
        {
            const double off = (g == 0 && G == 1) ? 0.0 : s.spray * 0.25 * v.rng.bi();
            double p = v.pos + off; p -= std::floor(p);
            fr[(size_t)g] = S.frame((int)std::lround(p * (S.frames - 1)));
            if (g == 0) fq0 = S.freqs((int)std::lround(p * (S.frames - 1)));
        }
        // the pitch: the note against the source's root, by TRACK; the source's rate against ours
        const double semis = (v.pitch + 12.0 * s.octave + vm.pitchBend() * 2.0 - S.root) * clampv(s.track, 0.0, 1.0) + 12.0 * s.octave * (1.0 - clampv(s.track, 0.0, 1.0));
        const double binHz = sr / kN, srcBinHz = S.sr / kN, rootIn = synth::midiToHz(S.root), rootOut = rootIn * std::exp2(semis / 12.0);
        const double st = 1.0 / (1.0 + 0.3 * clampv(s.stretch, -1.0, 1.0));
        const double keep = 0.95 * clampv(s.blur, 0.0, 1.0), take = v.fresh ? 1.0 : 1.0 - keep;
        v.fresh = false;
        const double jitL = (1.0 - clampv(s.phase, 0.0, 1.0)) * kPi, jitR = jitL + clampv(s.width, 0.0, 1.0) * (kPi - jitL);
        const bool coherent = s.phase > 0.0;
        // build the spectrum, its phases, and both channels' frames
        bufA[0] = bufA[1] = bufB[0] = bufB[1] = 0.0f;
        // pffft's inverse is N times; the windows overlap to 1.5; the magnitude is one side; measured: a frozen sine comes back at its level
        const double sc = 1.0 / (kN * 1.5) * 2.0 * 0.631;
        for (int k = 1; k < kBins - 1; ++k)
        {
            const double f = k * binHz;
            // which source bin feeds this one (stretched about the root)
            const double kin = rootIn * std::pow(f / rootOut, st) / srcBinHz;
            double m = 0.0, fOut = f;
            if (kin >= 0.0 && kin < kBins - 1)
            {
                const int k0 = (int)kin; const double fr0 = kin - k0;
                // the frequency it plays at: the source bin's own, carried over by the same map
                const double fin = fq0[std::min(kBins - 1, (int)std::lround(kin))];
                fOut = fin > 0.0 ? rootOut * std::pow(fin / rootIn, 1.0 / st) : f;
                double pw = 0.0;
                for (int g = 0; g < G; ++g) { const double a = fr[(size_t)g][k0] + (fr[(size_t)g][k0 + 1] - fr[(size_t)g][k0]) * fr0; pw += a * a; }
                m = std::sqrt(pw / G);
            }
            // the shaping
            m *= std::pow(std::max(1.0, f) / 1000.0, clampv(s.tilt, -1.0, 1.0));
            m *= 1.0 / (1.0 + std::pow(s.low / std::max(1.0, f), 4.0)) / (1.0 + std::pow(f / std::max(1.0, s.high), 4.0));
            if (f > 0.47 * sr) m = 0.0;
            v.cur[(size_t)k] = (float)(v.cur[(size_t)k] + (m - v.cur[(size_t)k]) * take);
            double mm = v.cur[(size_t)k];
            if (s.shimmer > 0.0) mm *= std::max(0.0, 1.0 + 1.5 * s.shimmer * (v.rng.uni() - 0.5));
            mm *= 1.0 + 0.57 * (1.0 - clampv(s.phase, 0.0, 1.0));   // random phases add up 3.9 dB quieter
            // the phases
            const double adv = coherent ? kTwoPi * fOut * kHop / sr : 0.0;
            double pl, pr;
            if (coherent) { pl = v.phL[(size_t)k] + adv + jitL * v.rng.bi(); pr = v.phR[(size_t)k] + adv + jitR * v.rng.bi(); }
            else { pl = kTwoPi * v.rng.uni(); pr = pl + clampv(s.width, 0.0, 1.0) * kPi * v.rng.bi(); }
            pl -= kTwoPi * std::floor(pl / kTwoPi); pr -= kTwoPi * std::floor(pr / kTwoPi);
            v.phL[(size_t)k] = (float)pl; v.phR[(size_t)k] = (float)pr;
            bufA[(size_t)(2 * k)] = (float)(mm * std::cos(pl)); bufA[(size_t)(2 * k + 1)] = (float)(mm * std::sin(pl));
            bufB[(size_t)(2 * k)] = (float)(mm * std::cos(pr)); bufB[(size_t)(2 * k + 1)] = (float)(mm * std::sin(pr));
        }
        std::vector<float>& outA = tmpA; std::vector<float>& outB = tmpB;   // sized in prepare()
        fft.inverse(bufA.data(), outA.data()); fft.inverse(bufB.data(), outB.data());
        for (int i = 0; i < kN; ++i) { v.olaL[(size_t)i] += (float)(outA[(size_t)i] * win[(size_t)i] * sc); v.olaR[(size_t)i] += (float)(outB[(size_t)i] * win[(size_t)i] * sc); }
        if (feed)
        {
            const double ref = std::max(1e-9f, S.maxMag);
            for (int j = 0; j < 256; ++j)
            {
                const double f = 20.0 * std::pow(1000.0, j / 255.0);
                const int k = clampv((int)(f / binHz), 1, kBins - 2);
                const double db = 20.0 * std::log10(v.cur[(size_t)k] / ref + 1e-9);
                specFeed[(size_t)j].store((float)clampv((db + 72.0) / 72.0, 0.0, 1.0));
            }
        }
    }

    Settings set;
    double sr = 48000.0;
    const Spectra* src = nullptr;
    RealFFT fft;
    std::vector<float> bufA, bufB, tmpA, tmpB, win;
    std::array<Voice, kMaxVoices> voices {};
    synth::VoiceManager vm;
    uint64_t counter = 0;
    int newest = 0;
    LinearSmoother volS;
    std::atomic<int> activeFeed { 0 };
    std::array<std::atomic<float>, kMaxVoices> posFeed {};
    std::array<std::atomic<float>, 256> specFeed {};
    std::atomic<uint64_t> litFeed[2] { { 0 }, { 0 } };
};

}} // namespace pump::freeze
