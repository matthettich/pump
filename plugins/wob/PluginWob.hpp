// pump WOB: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "WobParams.hpp"
#include "WobEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::wob;

class PluginWob : public PumpPlugin {
public:
    PluginWob() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = pump::testEnv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold from the start
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -8.5f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_wob"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }

    void sync()
    {
        Settings s;
        s.octave = (int)pv(kOctave); s.tune = pv(kTune); s.shape = pv(kShape) * 0.01; s.partials = (int)pv(kPartials);
        s.tilt = pv(kTilt) * 0.01; s.oddEven = pv(kOddEven) * 0.01; s.stretch = pv(kStretch) * 0.01;
        s.filterMode = (int)pv(kFilterMode); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv);
        s.filterDecay = pv(kFilterDecay) * 0.001; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.vowel = pv(kVowel) * 0.01; s.formant = pv(kFormant) * 0.01; s.combFreq = pv(kCombFreq) * 0.01; s.comb = pv(kComb) * 0.01;
        s.detune = pv(kDetune); s.width = pv(kWidth) * 0.01; s.sub = pv(kSub) * 0.01; s.subOct = (int)pv(kSubOct);
        s.wobSync = pv(kWobSync) >= 0.5f; s.wobDiv = (int)pv(kWobDiv); s.wobRate = pv(kWobRate); s.wobShape = (int)pv(kWobShape);
        s.wobRetrig = pv(kWobRetrig) >= 0.5f; s.wobPhase = pv(kWobPhase) * 0.01; s.wobSmooth = pv(kWobSmooth) * 0.01;
        s.wobDelay = pv(kWobDelay); s.wobFade = pv(kWobFade); s.wobDepth = pv(kWobDepth) * 0.01; s.wheel = pv(kWheel) * 0.01;
        s.wobCut = pv(kWobCut); s.wobVowel = pv(kWobVowel) * 0.01; s.wobComb = pv(kWobComb) * 0.01; s.wobAmp = pv(kWobAmp) * 0.01; s.wobPitch = pv(kWobPitch);
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.glide = pv(kGlide); s.legato = pv(kLegato) >= 0.5f; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume) + kTrimDb;
        s.shapeB = pv(kShapeB) * 0.01; s.tiltB = pv(kTiltB) * 0.01; s.morph = pv(kMorph) * 0.01; s.wobMorph = pv(kWobMorph) * 0.01;
        s.inharm = pv(kInharm) * 0.01; s.spray = pv(kSpray) * 0.01; s.drift = pv(kDrift) * 0.01; s.driftRate = pv(kDriftRate);
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
        pump::Transport tr = transport();
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
            engine.setTransport(tr);
            float* l = outputs[0] + done;
            float* r = outputs[1] + done;
            engine.process(l, r, (int)n, midi, nm);
            outLimit.process(l, r, (int)n);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            if (tr.valid) tr.ppq += n / getSampleRate() * tr.bpm / 60.0;
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWob)
};

END_NAMESPACE_DISTRHO
