// pump MONO: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "MonoParams.hpp"
#include "MonoEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::mono;

class PluginMono : public PumpPlugin {
public:
    PluginMono() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        setLatency((uint32_t)engine.latency());
        if (const char* n = std::getenv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold from the start
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    Engine engine;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak;

protected:
    const char* getLabel() const override { return "pump_mono"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); setLatency((uint32_t)engine.latency()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        const int wv[3] = { kOsc1Wave, kOsc2Wave, kOsc3Wave }, oc[3] = { kOsc1Oct, kOsc2Oct, kOsc3Oct }, lv[3] = { kOsc1Level, kOsc2Level, kOsc3Level };
        for (int o = 0; o < 3; ++o) { s.wave[o] = (int)pv(wv[o]); s.oct[o] = (int)pv(oc[o]); s.level[o] = pv(lv[o]) * 0.01; }
        s.pitch[0] = 0.0; s.pitch[1] = pv(kOsc2Pitch); s.pitch[2] = pv(kOsc3Pitch);
        s.sync = pv(kSync) >= 0.5f; s.fm = pv(kFm) * 0.01; s.pw = pv(kPw) * 0.01; s.noise = pv(kNoise) * 0.01; s.overdriveDb = pv(kOverdrive);
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.keyTrack = pv(kKeyTrack) * 0.01; s.mode = pv(kFilterMode) * 0.01; s.velFilter = pv(kVelFilter) * 0.01;
        s.fA = pv(kFA) * 0.001; s.fD = pv(kFD) * 0.001; s.fS = pv(kFS) * 0.01; s.fR = pv(kFR) * 0.001;
        s.aA = pv(kAA) * 0.001; s.aD = pv(kAD) * 0.001; s.aS = pv(kAS) * 0.01; s.aR = pv(kAR) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoPitch = pv(kLfoPitch) * 0.01; s.lfoFilter = pv(kLfoFilter) * 0.01;
        s.lfoPw = pv(kLfoPw) * 0.01; s.wheelVib = pv(kWheelVib) * 0.01;
        s.glide = pv(kGlide); s.glideMode = (int)pv(kGlideMode); s.priority = (int)pv(kPriority); s.legato = pv(kLegato) >= 0.5f;
        s.drift = pv(kDrift) * 0.01; s.bendRange = pv(kBendRange); s.volumeDb = pv(kVolume);
        engine.setSettings(s);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        keys.drain([&](uint8_t note, uint8_t vel) { engine.uiNote(note, vel); });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, 256u);
            int nm = 0;
            while (ev < midiEventCount && midiEvents[ev].frame < done + n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= 512) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            float* l = outputs[0] + done;
            float* r = outputs[1] + done;
            engine.process(l, r, (int)n, midi, nm);
            float pk = 0.0f;
            for (uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(l[i]));
            outPeak.setMax(pk);
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMono)
};

END_NAMESPACE_DISTRHO
