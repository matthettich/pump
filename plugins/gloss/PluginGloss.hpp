// pump GLOSS: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "GlossParams.hpp"
#include "GlossEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::gloss;

class PluginGloss : public PumpPlugin {
public:
    PluginGloss() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        setLatency((uint32_t)engine.latency());
        if (const char* n = pump::testEnv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold from the start
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_gloss"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        setLatency((uint32_t)engine.latency());
    }

    void sync()
    {
        Settings s;
        s.machine = (int)pv(kMachine); s.a = pv(kA) * 0.01; s.b = pv(kB) * 0.01; s.c = pv(kC) * 0.01; s.d = pv(kD) * 0.01;
        s.tune = pv(kTune); s.pitchEnv = pv(kPitchEnv); s.pitchTime = pv(kPitchTime) * 0.001; s.glide = pv(kGlide); s.mode = (int)pv(kMode);
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.filterDecay = pv(kFilterDecay) * 0.001; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.drive = pv(kDrive) * 0.01; s.driveType = (int)pv(kDriveType); s.crushBits = pv(kCrushBits); s.crushRate = pv(kCrushRate); s.width = pv(kWidth) * 0.01;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoDepth = pv(kLfoDepth) * 0.01; s.lfoDest = (int)pv(kLfoDest);
        s.bendRange = pv(kBendRange); s.volumeDb = pv(kVolume);
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
            outLimit.process(l, r, (int)n);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginGloss)
};

END_NAMESPACE_DISTRHO
