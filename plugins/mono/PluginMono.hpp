// pump MONO: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "MonoDsp.hpp"   // the DSP itself (shared with the web build)

START_NAMESPACE_DISTRHO

using namespace pump::mono;

class PluginMono : public pump::mono::MonoDsp<PumpPlugin> {
public:
    PluginMono() : MonoDsp(params().data(), kNumParams, &presets())
    {
        prepareDsp();
        setLatency((uint32_t)engine.latency());
        if (const char* n = pump::testEnv("PUMP_TEST_NOTES"))   // headless screenshots: notes to hold from the start
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak;

protected:
    const char* getLabel() const override { return "pump_mono"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { prepareDsp(); setLatency((uint32_t)engine.latency()); }


    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            syncSettings();
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
            renderChunk(l, r, (int)n, midi, nm);
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
