// pump FOLD: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "FoldParams.hpp"
#include "FoldEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::fold;

class PluginFold : public PumpPlugin {
public:
    PluginFold() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = pump::testEnv("PUMP_TEST_NOTES")) testNotes = n;
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = 2.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_fold"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }

    void sync()
    {
        Settings s;
        s.shape = pv(kShape) * 0.01; s.modWave = (int)pv(kModWaveP); s.modRatio = pv(kModRatio); s.modDetune = pv(kModDetune);
        s.fm = pv(kFmIndex) * 0.01; s.am = pv(kAmDepth) * 0.01; s.modFold = pv(kModFold) * 0.01;
        s.fold = pv(kFold) * 0.01; s.symmetry = pv(kSymmetry) * 0.01; s.edge = pv(kEdge) * 0.01; s.stages = (int)pv(kStages);
        s.gateMode = (int)pv(kGateModeP); s.gateOpen = pv(kGateOpen) * 0.01; s.vactrol = pv(kVactrol) * 0.01; s.gateReso = pv(kGateReso) * 0.01;
        s.funcMode = (int)pv(kFuncModeP); s.rise = pv(kRise) * 0.001; s.fall = pv(kFall) * 0.001; s.funcGate = pv(kFuncGate) * 0.01;
        s.funcFold = pv(kFuncFold) * 0.01; s.funcFm = pv(kFuncFm) * 0.01; s.funcPitch = pv(kFuncPitch);
        s.randRate = pv(kRandRate); s.randSmooth = pv(kRandSmooth) * 0.01; s.randFold = pv(kRandFold) * 0.01; s.randFm = pv(kRandFm) * 0.01; s.randPitch = pv(kRandPitch);
        s.velocity = pv(kVelocity) * 0.01; s.spread = pv(kSpread) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode);
        s.glide = pv(kGlide); s.octave = (int)pv(kOctave); s.fine = pv(kFine);
        s.volumeDb = pv(kVolume) + kTrimDb;
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
        if (!testNotes.empty() && (testClock += frames) > (uint64_t)(0.5 * getSampleRate()))
        {
            for (const char* p = testNotes.c_str(); *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
            testNotes.clear();
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
    std::string testNotes;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginFold)
};

END_NAMESPACE_DISTRHO
