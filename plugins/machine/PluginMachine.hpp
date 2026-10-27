// pump MACHINE: plugin (DSP side). GPL-3.0-only (see NOTICE.md). Declared in a header so the UI can read its feeds
// and play its pads directly.
#pragma once
#include "PumpPlugin.hpp"
#include "MachineParams.hpp"
#include "MachineEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::machine;

class PluginMachine : public PumpPlugin {
public:
    PluginMachine() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        activate();
        if (std::getenv("PUMP_TEST_NOTES")) testLoop = true;   // headless screenshots: a beat plays from the start
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -5.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue pads;   // the window's pads: (sound, velocity)
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_machine"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }
    const char* getLicense() const override { return "GPL-3.0-only"; }

    void activate() override
    {
        std::vector<int> fw((size_t)kFwNumParams, -1);
        for (int i = 0; i < kNumFw; ++i) fw[(size_t)kFw[i].fw] = (int)std::lround(pv((uint32_t)i));
        engine.setVolume(pv(kVolume) + kTrimDb);
        engine.prepare(getSampleRate(), (int)getBufferSize(), fw.data()); outLimit.prepare(getSampleRate());
        for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
    }
    void sampleRateChanged(double) override { activate(); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
                if (takeChanged(i))
                {
                    if (i < (uint32_t)kNumFw) engine.setParam(kFw[i].fw, (int)std::lround(pv(i)));
                    else if (i == (uint32_t)kVolume) engine.setVolume(pv(i) + kTrimDb);
                }
        engine.setTempo(transport().bpm);
        int nh = 0;
        pads.drain([&](uint8_t sound, uint8_t vel) { if (vel && nh < 64) hits[nh++] = { sound, vel }; });
        if (testLoop) testBeat(frames, nh);
        uint32_t done = 0, ev = 0;
        bool first = true;
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
            engine.process(l, r, (int)n, midi, nm, first ? hits : nullptr, first ? nh : 0);
            outLimit.process(l, r, (int)n);
            first = false;
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += n;
        }
    }

private:
    // screenshots only: a one-bar beat at 120 BPM
    void testBeat(uint32_t frames, int& nh)
    {
        const double step = getSampleRate() * 0.125;
        static const char* const pat[kSounds] = { "x...x...x...x...", "......x.......x.", "...x......x.....", "....x.......x...", "x...............", "x.x.x.x.x.x.x.x.", "..x...x...x...x." };
        const int s = (int)std::floor(testPos / step) % 16, s2 = (int)std::floor((testPos + frames) / step) % 16;
        if (s2 != s) for (int p = 0; p < kSounds; ++p) if (pat[p][s2] == 'x' && nh < 64) hits[nh++] = { p, p == 5 ? 80 : 120 };
        testPos += frames;
    }
    pump::synth::Midi midi[512] = {};
    std::pair<int, int> hits[64] = {};
    bool testLoop = false;
    double testPos = 0.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMachine)
};

END_NAMESPACE_DISTRHO
