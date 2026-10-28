// pump POLY: the DSP side, shared by the plugin (PluginPoly.hpp, on PumpPlugin) and the web build
// (wasm/pump_synth.cpp, on its SynthBase): the settings from the parameters, preparing, and rendering a chunk with its
// MIDI (the engine, then the output safety limiter). Base supplies pv(), getSampleRate(), getBufferSize().
#pragma once
#include "PolyParams.hpp"
#include "PolyEngine.hpp"
#include "dsp/Util.hpp"
#include <utility>

namespace pump { namespace poly {

template <class Base>
class PolyDsp : public Base {
public:
    template <class... A>
    explicit PolyDsp(A&&... a) : Base(std::forward<A>(a)...) {}

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.5f;
    Engine engine;
    pump::SafetyLimiter outLimit;

    // the parameters into the engine
    void syncSettings()
    {
        Settings s;
        s.aSaw = this->pv(kASaw) >= 0.5f; s.aPulse = this->pv(kAPulse) >= 0.5f; s.aFreq = this->pv(kAFreq); s.aPw = this->pv(kAPw) * 0.01; s.sync = this->pv(kSync) >= 0.5f;
        s.bSaw = this->pv(kBSaw) >= 0.5f; s.bTri = this->pv(kBTri) >= 0.5f; s.bPulse = this->pv(kBPulse) >= 0.5f; s.bFreq = this->pv(kBFreq); s.bFine = this->pv(kBFine);
        s.bPw = this->pv(kBPw) * 0.01; s.bLow = this->pv(kBLow) >= 0.5f;
        s.levelA = this->pv(kLevelA) * 0.01; s.levelB = this->pv(kLevelB) * 0.01; s.noise = this->pv(kNoise) * 0.01; s.sub = this->pv(kSub) * 0.01;
        s.filterType = (int)this->pv(kFilterType); s.cutoff = this->pv(kCutoff); s.res = this->pv(kRes) * 0.01; s.filterEnv = this->pv(kFilterEnv) * 0.01;
        s.keyTrack = this->pv(kKeyTrack) * 0.01; s.hpf = this->pv(kHpf); s.velFilter = this->pv(kVelFilter) * 0.01;
        s.fA = this->pv(kFA) * 0.001; s.fD = this->pv(kFD) * 0.001; s.fS = this->pv(kFS) * 0.01; s.fR = this->pv(kFR) * 0.001;
        s.aA = this->pv(kAA) * 0.001; s.aD = this->pv(kAD) * 0.001; s.aS = this->pv(kAS) * 0.01; s.aR = this->pv(kAR) * 0.001; s.velAmp = this->pv(kVelAmp) * 0.01;
        s.pmEnv = this->pv(kPmEnv) * 0.01; s.pmOscB = this->pv(kPmOscB) * 0.01; s.pmFreqA = this->pv(kPmFreqA) >= 0.5f; s.pmPwA = this->pv(kPmPwA) >= 0.5f; s.pmFilter = this->pv(kPmFilter) >= 0.5f;
        s.lfoShape = (int)this->pv(kLfoShape); s.lfoRate = this->pv(kLfoRate); s.lfoDelay = this->pv(kLfoDelay); s.lfoPitch = this->pv(kLfoPitch) * 0.01;
        s.lfoPw = this->pv(kLfoPw) * 0.01; s.lfoFilter = this->pv(kLfoFilter) * 0.01; s.wheelMod = this->pv(kWheelMod) * 0.01;
        s.voiceMode = (int)this->pv(kVoiceMode); s.voices = (int)this->pv(kVoices); s.unisonDetune = this->pv(kUnisonDetune) * 0.01; s.glide = this->pv(kGlide);
        s.slop = this->pv(kSlop) * 0.01; s.bendRange = this->pv(kBendRange); s.chorus = (int)this->pv(kChorus); s.volumeDb = this->pv(kVolume) + kTrimDb;
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

}} // namespace pump::poly
