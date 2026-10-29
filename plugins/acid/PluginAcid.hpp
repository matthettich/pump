// pump ACID: plugin (DSP side). Declared in a header so the UI can read its feeds, play its keyboard and run the
// pattern (PLAY) directly.
#pragma once
#include "PumpPlugin.hpp"
#include "AcidParams.hpp"
#include "AcidEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::acid;

class PluginAcid : public PumpPlugin {
public:
    PluginAcid() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        setLatency((uint32_t)engine.latency());
        if (pump::testEnv("PUMP_TEST_NOTES")) preview.store(true);   // headless screenshots: the pattern runs from the start
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical line at about -20 dBFS RMS
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;    // the window's keyboard
    pump::MeterValue outPeak;
    std::atomic<bool> preview { false };   // the window's PLAY: the pattern runs at the host tempo (never saved)

protected:
    const char* getLabel() const override { return "pump_acid"; }
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
        s.model = (int)pv(kModel); s.tune = pv(kTune); s.wave = pv(kWave) * 0.01; s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01;
        s.envMod = pv(kEnvMod) * 0.01; s.decay = pv(kDecay) * 0.001; s.accent = pv(kAccent) * 0.01; s.slide = pv(kSlide) * 0.001;
        s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume);
        s.attack = pv(kAttack) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001; s.pw = pv(kPw) * 0.01;
        s.sub = pv(kSub) * 0.01; s.noise = pv(kNoise) * 0.01; s.detune = pv(kDetune); s.osc2 = pv(kOsc2) * 0.01; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.accentVel = (int)pv(kAccentVel); s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoPitch = pv(kLfoPitch) * 0.01;
        s.lfoCutoff = pv(kLfoCutoff) * 0.01; s.lfoPw = pv(kLfoPw) * 0.01; s.bendRange = pv(kBendRange); s.slope = (int)pv(kSlope);
        s.seqMode = (int)pv(kSeqMode); s.rate = (int)pv(kSeqRate); s.swing = pv(kSwing) * 0.01; s.gateLen = pv(kGateLen) * 0.01;
        s.transpose = (int)pv(kTranspose);
        s.pattern = patternFrom([this](int i) { return pv((uint32_t)i); });
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
        engine.setPreview(preview.load(std::memory_order_relaxed));
        keys.drain([&](uint8_t note, uint8_t vel) { engine.uiNote(note, vel); });
        pump::Transport tr = transport();
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
            float pk = 0.0f;
            for (uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(l[i]));
            outPeak.setMax(pk);
            tr.ppq += n * tr.bpm / 60.0 / getSampleRate();
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginAcid)
};

END_NAMESPACE_DISTRHO
