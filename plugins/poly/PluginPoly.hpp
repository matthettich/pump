// pump POLY: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "PolyParams.hpp"
#include "PolyEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::poly;

class PluginPoly : public PumpPlugin {
public:
    PluginPoly() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = pump::testEnv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold from the start
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.5f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_poly"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }

    void sync()
    {
        Settings s;
        s.aSaw = pv(kASaw) >= 0.5f; s.aPulse = pv(kAPulse) >= 0.5f; s.aFreq = pv(kAFreq); s.aPw = pv(kAPw) * 0.01; s.sync = pv(kSync) >= 0.5f;
        s.bSaw = pv(kBSaw) >= 0.5f; s.bTri = pv(kBTri) >= 0.5f; s.bPulse = pv(kBPulse) >= 0.5f; s.bFreq = pv(kBFreq); s.bFine = pv(kBFine);
        s.bPw = pv(kBPw) * 0.01; s.bLow = pv(kBLow) >= 0.5f;
        s.levelA = pv(kLevelA) * 0.01; s.levelB = pv(kLevelB) * 0.01; s.noise = pv(kNoise) * 0.01; s.sub = pv(kSub) * 0.01;
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.keyTrack = pv(kKeyTrack) * 0.01; s.hpf = pv(kHpf); s.velFilter = pv(kVelFilter) * 0.01;
        s.fA = pv(kFA) * 0.001; s.fD = pv(kFD) * 0.001; s.fS = pv(kFS) * 0.01; s.fR = pv(kFR) * 0.001;
        s.aA = pv(kAA) * 0.001; s.aD = pv(kAD) * 0.001; s.aS = pv(kAS) * 0.01; s.aR = pv(kAR) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.pmEnv = pv(kPmEnv) * 0.01; s.pmOscB = pv(kPmOscB) * 0.01; s.pmFreqA = pv(kPmFreqA) >= 0.5f; s.pmPwA = pv(kPmPwA) >= 0.5f; s.pmFilter = pv(kPmFilter) >= 0.5f;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoDelay = pv(kLfoDelay); s.lfoPitch = pv(kLfoPitch) * 0.01;
        s.lfoPw = pv(kLfoPw) * 0.01; s.lfoFilter = pv(kLfoFilter) * 0.01; s.wheelMod = pv(kWheelMod) * 0.01;
        s.voiceMode = (int)pv(kVoiceMode); s.voices = (int)pv(kVoices); s.unisonDetune = pv(kUnisonDetune) * 0.01; s.glide = pv(kGlide);
        s.slop = pv(kSlop) * 0.01; s.bendRange = pv(kBendRange); s.chorus = (int)pv(kChorus); s.volumeDb = pv(kVolume) + kTrimDb;
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPoly)
};

END_NAMESPACE_DISTRHO
