// pump MONO: the DSP side, shared by the plugin (PluginMono.hpp, on PumpPlugin) and the web build
// (wasm/pump_synth.cpp, on its SynthBase): the settings from the parameters, preparing, and rendering a chunk with its
// MIDI (the engine, then the output safety limiter). Base supplies pv(), getSampleRate(), getBufferSize().
#pragma once
#include "MonoParams.hpp"
#include "MonoEngine.hpp"
#include "dsp/Util.hpp"
#include <utility>

namespace pump { namespace mono {

template <class Base>
class MonoDsp : public Base {
public:
    template <class... A>
    explicit MonoDsp(A&&... a) : Base(std::forward<A>(a)...) {}

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -4.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;

    // the parameters into the engine
    void syncSettings()
    {
        Settings s;
        const int wv[3] = { kOsc1Wave, kOsc2Wave, kOsc3Wave }, oc[3] = { kOsc1Oct, kOsc2Oct, kOsc3Oct }, lv[3] = { kOsc1Level, kOsc2Level, kOsc3Level };
        for (int o = 0; o < 3; ++o) { s.wave[o] = (int)this->pv(wv[o]); s.oct[o] = (int)this->pv(oc[o]); s.level[o] = this->pv(lv[o]) * 0.01; }
        s.pitch[0] = 0.0; s.pitch[1] = this->pv(kOsc2Pitch); s.pitch[2] = this->pv(kOsc3Pitch);
        s.sync = this->pv(kSync) >= 0.5f; s.fm = this->pv(kFm) * 0.01; s.pw = this->pv(kPw) * 0.01; s.noise = this->pv(kNoise) * 0.01; s.overdriveDb = this->pv(kOverdrive);
        s.filterType = (int)this->pv(kFilterType); s.cutoff = this->pv(kCutoff); s.res = this->pv(kRes) * 0.01; s.filterEnv = this->pv(kFilterEnv) * 0.01;
        s.keyTrack = this->pv(kKeyTrack) * 0.01; s.mode = this->pv(kFilterMode) * 0.01; s.velFilter = this->pv(kVelFilter) * 0.01;
        s.fA = this->pv(kFA) * 0.001; s.fD = this->pv(kFD) * 0.001; s.fS = this->pv(kFS) * 0.01; s.fR = this->pv(kFR) * 0.001;
        s.aA = this->pv(kAA) * 0.001; s.aD = this->pv(kAD) * 0.001; s.aS = this->pv(kAS) * 0.01; s.aR = this->pv(kAR) * 0.001; s.velAmp = this->pv(kVelAmp) * 0.01;
        s.lfoShape = (int)this->pv(kLfoShape); s.lfoRate = this->pv(kLfoRate); s.lfoPitch = this->pv(kLfoPitch) * 0.01; s.lfoFilter = this->pv(kLfoFilter) * 0.01;
        s.lfoPw = this->pv(kLfoPw) * 0.01; s.wheelVib = this->pv(kWheelVib) * 0.01;
        s.glide = this->pv(kGlide); s.glideMode = (int)this->pv(kGlideMode); s.priority = (int)this->pv(kPriority); s.legato = this->pv(kLegato) >= 0.5f;
        s.drift = this->pv(kDrift) * 0.01; s.bendRange = this->pv(kBendRange); s.volumeDb = this->pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    // settings, then the engine and the limiter at the current rate
    void prepareDsp()
    {
        syncSettings();
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        outLimit.prepare(this->getSampleRate());
    }
    // one chunk (the caller splits blocks and their MIDI into chunks)
    void renderChunk(float* l, float* r, int n, const pump::synth::Midi* ev, int nev)
    {
        engine.process(l, r, n, ev, nev);
        outLimit.process(l, r, n);
    }
};

}} // namespace pump::mono
