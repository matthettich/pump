// pump BOING: plugin (DSP side). Declared in a header so the UI can read its feeds and play its pads directly.
#pragma once
#include "PumpPlugin.hpp"
#include "BoingParams.hpp"
#include "BoingEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::boing;

class PluginBoing : public PumpPlugin {
public:
    PluginBoing() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (pump::testEnv("PUMP_TEST_NOTES")) testLoop = true;   // headless screenshots: a beat plays from the start
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical kit at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;   // the window's pads: (pad, velocity)
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_boing"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }

    void sync()
    {
        Settings s;
        for (int p = 0; p < kPads; ++p)
        {
            PadSettings& q = s.pad[p];
            q.tank = (int)pv(pp(p, kTank)); q.wave = (int)pv(pp(p, kWave)); q.blend = pv(pp(p, kBlend)) * 0.01; q.pitch = pv(pp(p, kPitch));
            q.decay = pv(pp(p, kDecay)) * 0.001; q.harmonic = pv(pp(p, kHarmonic)) * 0.01; q.spread = pv(pp(p, kSpread)) * 0.01; q.drip = pv(pp(p, kDrip));
            q.bend = pv(pp(p, kBend)); q.bendTime = pv(pp(p, kBendTime)) * 0.001; q.noise = pv(pp(p, kNoise)) * 0.01; q.noiseTone = pv(pp(p, kNoiseTone)) * 0.01;
            q.click = pv(pp(p, kClick)) * 0.01; q.hits = (int)pv(pp(p, kHits)); q.level = pv(pp(p, kLevel)); q.pan = pv(pp(p, kPan)) * 0.01; q.send = pv(pp(p, kSend)) * 0.01;
        }
        s.tankModel = (int)pv(kTankModel); s.tankDecay = pv(kTankDecay); s.tankLevel = pv(kTankLevel) * 0.01; s.choke = pv(kChoke) >= 0.5f;
        s.velocity = pv(kVelocity) * 0.01; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume) + kTrimDb;
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
        keys.drain([&](uint8_t pad, uint8_t vel) { if (vel) engine.hit(pad, vel / 127.0); });
        if (testLoop) testBeat(frames);
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
    // screenshots only: a two-bar beat at 120 BPM, hit at block starts
    void testBeat(uint32_t frames)
    {
        const double step = getSampleRate() * 0.125;
        static const char* const pat[kPads] = { "x..x..x...x..x..", "....x.......x...", "............x..x", "..........x.....", "..............x.", "x.x.x.x.x.x.x.x.", "...x.......x....", "x..............." };
        const int s = (int)std::floor(testPos / step) % 16, s2 = (int)std::floor((testPos + frames) / step) % 16;
        if (s2 != s) for (int p = 0; p < kPads; ++p) if (pat[p][s2] == 'x') engine.hit(p, p == 5 ? 0.7 : 1.0);
        testPos += frames;
    }
    pump::synth::Midi midi[512] = {};
    bool testLoop = false;
    double testPos = 0.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBoing)
};

END_NAMESPACE_DISTRHO
