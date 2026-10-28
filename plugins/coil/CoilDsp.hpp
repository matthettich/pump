// pump COIL: the DSP side, shared by the plugin (PluginCoil.hpp, on PumpPlugin) and the web build
// (wasm/pump_synth.cpp, on its SynthBase): the settings from the parameters, preparing, and rendering a chunk with its
// MIDI (the engine, then the output safety limiter). Base supplies pv(), getSampleRate(), getBufferSize().
#pragma once
#include "CoilParams.hpp"
#include "CoilEngine.hpp"
#include "dsp/Util.hpp"
#include <utility>

namespace pump { namespace coil {

template <class Base>
class CoilDsp : public Base {
public:
    template <class... A>
    explicit CoilDsp(A&&... a) : Base(std::forward<A>(a)...) {}

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;

    // the parameters into the engine
    void syncSettings()
    {
        Settings s;
        s.model = (int)this->pv(kModel); s.tension = this->pv(kTension); s.drip = this->pv(kDrip); s.decay = this->pv(kDecay); s.release = this->pv(kRelease);
        s.harmonic = this->pv(kHarmonic) * 0.01; s.spread = this->pv(kSpread) * 0.01; s.wobble = this->pv(kWobble) * 0.01; s.tone = this->pv(kTone) * 0.01;
        s.excite = this->pv(kExcite) * 0.01; s.tank = this->pv(kTank) * 0.01; s.grit = this->pv(kGrit); s.width = this->pv(kWidth) * 0.01;
        s.octave = (int)this->pv(kOctave); s.bend = this->pv(kBend); s.velocity = this->pv(kVelocity) * 0.01; s.volumeDb = this->pv(kVolume) + kTrimDb;
        s.voices = (int)this->pv(kVoices);
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
        engine.process(nullptr, nullptr, l, r, n, ev, nev);
        outLimit.process(l, r, n);
    }
};

}} // namespace pump::coil
