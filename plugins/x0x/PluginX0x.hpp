// pump X0X: plugin (DSP side). Declared in a header so the UI can read its feeds, play its pads and run the
// pattern (PLAY) directly.
#pragma once
#include "PumpPlugin.hpp"
#include "X0xParams.hpp"
#include "X0xEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::x0x;

class PluginX0x : public PumpPlugin {
public:
    PluginX0x() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (pump::testEnv("PUMP_TEST_NOTES")) preview.store(true);   // headless screenshots: the pattern runs from the start
    }

    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;   // the window's pads: (instrument, velocity)
    pump::MeterValue outPeak[2];
    std::atomic<bool> preview { false };   // the window's PLAY (never saved)

protected:
    const char* getLabel() const override { return "pump_x0x"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }

    void sync()
    {
        Settings s;
        for (int i = 0; i < kInstr; ++i)
        {
            InstrSettings& q = s.in[i];
            q.machine = (int)pv(ip(i, kMachine)); q.tune = pv(ip(i, kTune)); q.decay = pv(ip(i, kDecay)) * 0.01; q.tone = pv(ip(i, kTone)) * 0.01;
            q.snap = pv(ip(i, kSnap)) * 0.01; q.levelDb = pv(ip(i, kLevel)); q.pan = pv(ip(i, kPan)) * 0.01;
        }
        s.accent = pv(kAccent) * 0.01; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume); s.velocity = pv(kVelocity) * 0.01;
        s.accentVel = (int)pv(kAccentVel); s.seqMode = (int)pv(kSeqMode); s.length = (int)pv(kSeqLength); s.rate = (int)pv(kSeqRate);
        s.swing = pv(kSwing) * 0.01;
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
        keys.drain([&](uint8_t instr, uint8_t vel) { if (vel) engine.hit(instr, (vel & 127) / 127.0, false); });
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
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            tr.ppq += n * tr.bpm / 60.0 / getSampleRate();
            done += n;
        }
    }

private:
    pump::synth::Midi midi[512] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginX0x)
};

END_NAMESPACE_DISTRHO
