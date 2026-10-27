// pump ORBIT: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "OrbitParams.hpp"
#include "OrbitEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::orbit;

class PluginOrbit : public PumpPlugin {
public:
    PluginOrbit() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = std::getenv("PUMP_TEST_NOTES")) testNotes = n;
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -4.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_orbit"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.system = (int)pv(kSystem); s.chaos = pv(kChaos) * 0.01; s.sync = pv(kSync) * 0.01; s.axis = pv(kAxis) * 0.01; s.width = pv(kWidth) * 0.01;
        s.drive = pv(kDrive) * 0.01; s.cutoff = pv(kCutoff); s.reso = pv(kReso) * 0.01; s.keytrack = pv(kKeytrack) * 0.01;
        s.envCut = pv(kEnvCut) * 0.01; s.envChaos = pv(kEnvChaos) * 0.01;
        s.modA = pv(kModA) * 0.001; s.modD = pv(kModD) * 0.001; s.modS = pv(kModS) * 0.01; s.modR = pv(kModR) * 0.001;
        s.driftRate = pv(kDriftRate); s.driftPitch = pv(kDriftPitch) * 0.01; s.driftCut = pv(kDriftCut) * 0.01; s.driftChaos = pv(kDriftChaos) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide);
        s.octave = (int)pv(kOctave); s.fine = pv(kFine);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginOrbit)
};

END_NAMESPACE_DISTRHO
