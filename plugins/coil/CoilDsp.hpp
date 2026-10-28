// pump COIL: the DSP side, shared by the plugin (PluginCoil.hpp, on PumpPlugin) and the web build
// (wasm/pump_synth.cpp, on its SynthBase): the settings from the parameters, preparing, and rendering a chunk with its
// MIDI (the modulation, the engine, then the output safety limiter). Base supplies pv(), getSampleRate(),
// getBufferSize().
//
// Modulation (common/dsp/Mod.hpp): three LFOs and per-note / MIDI sources move COIL's continuous controls through a pool
// of connections (source, target, amount). Once per chunk (at most 256 samples) the sources are read, every target's
// value is its control's position plus the connections' amounts times their sources (in the control's own 0..1 range,
// so a log control moves evenly), and the engine gets those settings; per-note sources (velocity, note, random) give
// each sounding voice its own. With no connection in use nothing here runs: the sound is exactly as before.
#pragma once
#include "CoilParams.hpp"
#include "CoilEngine.hpp"
#include "dsp/Util.hpp"
#include "dsp/Mod.hpp"
#include "dsp/Tempo.hpp"
#include <atomic>
#include <utility>

namespace pump { namespace coil {

template <class Base>
class CoilDsp : public Base {
public:
    template <class... A>
    explicit CoilDsp(A&&... a) : Base(std::forward<A>(a)...) { lfo[0].seed(0x1F35A7u); lfo[1].seed(0x7C2B91u); lfo[2].seed(0x4E6D13u); }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    // the host's tempo and position (the plugin sets it each block; the web build leaves 120 BPM, free running)
    pump::Transport hostTime;

    // ---- feeds for the window ----
    // every source's value now (per-note sources: the newest note's), and the LFOs' phases
    std::atomic<float> sourceNow[pump::mod::kNumSources] {};
    std::atomic<float> lfoPhaseNow[pump::mod::kNumLfos] {};

    // the parameters into the engine
    void syncSettings()
    {
        for (int i = 0; i < kNumParams; ++i) base[i] = this->pv((uint32_t)i);
        scanSlots();
        engine.setSettings(settingsFrom(base));
    }
    // settings, then the engine and the limiter at the current rate
    void prepareDsp()
    {
        syncSettings();
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        outLimit.prepare(this->getSampleRate());
        for (auto& l : lfo) l.reset();
        notesSeen = engine.noteOns();
    }
    // one chunk (the caller splits blocks and their MIDI into chunks)
    void renderChunk(float* l, float* r, int n, const pump::synth::Midi* ev, int nev)
    {
        if (anyActive) modulate(n);
        engine.process(nullptr, nullptr, l, r, n, ev, nev);
        outLimit.process(l, r, n);
        if (anyActive) publish();
    }

private:
    float base[kNumParams] = {};
    pump::mod::Lfo lfo[pump::mod::kNumLfos];
    double lfoOut[pump::mod::kNumLfos] = {};
    uint32_t notesSeen = 0;
    // the connections in use (scanned when parameters change)
    struct Conn { int src, target; float amt; };
    Conn conn[pump::mod::kSlots];
    int numConn = 0;
    bool anyActive = false, anyPerVoice = false;

    void scanSlots()
    {
        numConn = 0; anyPerVoice = false;
        for (int k = 0; k < pump::mod::kSlots; ++k)
        {
            const int src = (int)base[slotParam(k, kSlotSrc)], dst = (int)base[slotParam(k, kSlotDst)];
            const float amt = base[slotParam(k, kSlotAmt)] * 0.01f;
            if (src <= pump::mod::kSrcNone || src >= pump::mod::kNumSources || dst <= 0 || dst >= kNumTargets || amt == 0.0f) continue;
            conn[numConn++] = { src, dst, amt };
            if (pump::mod::perVoice(src)) anyPerVoice = true;
        }
        anyActive = numConn > 0;
    }

    Settings settingsFrom(const float* v) const
    {
        Settings s;
        s.model = (int)v[kModel]; s.tension = v[kTension]; s.drip = v[kDrip]; s.decay = v[kDecay]; s.release = v[kRelease];
        s.harmonic = v[kHarmonic] * 0.01; s.spread = v[kSpread] * 0.01; s.wobble = v[kWobble] * 0.01; s.tone = v[kTone] * 0.01;
        s.excite = v[kExcite] * 0.01; s.tank = v[kTank] * 0.01; s.grit = v[kGrit]; s.width = v[kWidth] * 0.01;
        s.octave = (int)v[kOctave]; s.bend = v[kBend]; s.velocity = v[kVelocity] * 0.01; s.volumeDb = v[kVolume] + kTrimDb;
        s.voices = (int)v[kVoices];
        return s;
    }

    // a source's value: global ones from the LFOs and MIDI, per-note ones from voice `vi` (-1: none sounding)
    double sourceValue(int src, int vi) const
    {
        using namespace pump::mod;
        switch (src)
        {
        case kSrcLfo1: return lfoOut[0];
        case kSrcLfo2: return lfoOut[1];
        case kSrcLfo3: return lfoOut[2];
        case kSrcWheel: return engine.wheel();
        case kSrcAftertouch: return engine.aftertouch();
        case kSrcVelocity: return vi >= 0 ? engine.voiceVelocity(vi) : 0.0;
        case kSrcNote: return vi >= 0 ? clampv((engine.voiceNoteNum(vi) - 60) / 24.0, -1.0, 1.0) : 0.0;
        case kSrcRandom: return vi >= 0 ? engine.voiceRandom(vi) : 0.0;
        default: return 0.0;
        }
    }
    // the targets' offsets (in their 0..1 ranges) from the connections whose sources are global, or per note too
    void offsets(double* off, int vi, bool perVoiceToo) const
    {
        for (int t = 0; t < kNumTargets; ++t) off[t] = 0.0;
        for (int k = 0; k < numConn; ++k)
        {
            const Conn& c = conn[k];
            if (pump::mod::perVoice(c.src) && !perVoiceToo) continue;
            off[c.target] += c.amt * sourceValue(c.src, vi);
        }
    }
    void applyOffsets(float* v, const double* off) const
    {
        const auto& sp = params();
        for (int t = 1; t < kNumTargets; ++t)
        {
            const int p = kTargetParams[t];
            if (p < 0 || off[t] == 0.0) continue;
            const ParamSpec& s = sp[(size_t)p];
            v[p] = s.fromNorm((float)clampv((double)s.toNorm(base[p]) + off[t], 0.0, 1.0));
        }
    }

    void modulate(int n)
    {
        const double sr = this->getSampleRate() > 0.0 ? this->getSampleRate() : 48000.0;
        // RETRIG: a new note restarts the LFO
        const uint32_t ons = engine.noteOns();
        const bool newNote = ons != notesSeen;
        notesSeen = ons;
        // the LFOs (rate targets from the global sources; the last first, so LFO 3 can move 2 and 1, and 2 can move 1)
        double off[kNumTargets];
        for (int L = pump::mod::kNumLfos - 1; L >= 0; --L)
        {
            offsets(off, -1, false);
            const int shape = (int)base[lfoParam(L, kLfoShape)];
            const bool sync = base[lfoParam(L, kLfoSync)] >= 0.5f, retrig = base[lfoParam(L, kLfoRetrig)] >= 0.5f;
            const double rateOff = off[lfoRateTarget(L)];
            if (retrig && newNote) lfo[L].reset();
            if (sync)
            {
                const double q = pump::noteQuarters((int)base[lfoParam(L, kLfoDiv)]) * std::exp2(-4.0 * rateOff);
                if (hostTime.valid && hostTime.playing && !retrig && rateOff == 0.0) lfo[L].lockTo(hostTime.ppq / q);
                else lfo[L].advance(n / (q * 60.0 / clampv(hostTime.bpm, 20.0, 999.0) * sr));
            }
            else
            {
                const ParamSpec& s = params()[(size_t)lfoParam(L, kLfoRate)];
                const double hz = s.fromNorm((float)clampv((double)s.toNorm(base[lfoParam(L, kLfoRate)]) + rateOff, 0.0, 1.0));
                lfo[L].advance(hz * n / sr);
            }
            lfoOut[L] = lfo[L].value(shape);
        }
        // every voice: the global connections
        float v[kNumParams];
        for (int i = 0; i < kNumParams; ++i) v[i] = base[i];
        offsets(off, -1, false);
        applyOffsets(v, off);
        engine.setSettings(settingsFrom(v));
        // and each sounding voice its own (per-note sources)
        if (anyPerVoice)
            for (int i = 0; i < kMaxVoices; ++i)
            {
                if (!engine.voiceActive(i)) continue;
                float w[kNumParams];
                for (int p = 0; p < kNumParams; ++p) w[p] = base[p];
                offsets(off, i, true);
                applyOffsets(w, off);
                engine.setVoiceSettings(i, settingsFrom(w));
            }
    }
    void publish()
    {
        const int vi = engine.lastVoice();
        for (int s = 1; s < pump::mod::kNumSources; ++s) sourceNow[s].store((float)sourceValue(s, vi), std::memory_order_relaxed);
        for (int L = 0; L < pump::mod::kNumLfos; ++L) lfoPhaseNow[L].store((float)lfo[L].phase, std::memory_order_relaxed);
    }
};

}} // namespace pump::coil
