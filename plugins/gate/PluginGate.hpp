// pump GATE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "GateParams.hpp"
#include "GateEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::gate;

class PluginGate : public PumpPlugin {
public:
    PluginGate() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: output peaks (the engine has the position, steps, openness and the pattern's audio lane)
    pump::MeterValue outPeak[2];
    Engine engine;

protected:
    const char* getLabel() const override { return "pump_gate"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    // no latency, but hosts (CLAP especially) read it right after activation
    void reportLatency()
    {
        setLatency(0);
        bypass.setLatency(0);
    }

    void sync()
    {
        Settings s;
        s.steps = (int)pv(kSteps);
        s.rate = (int)pv(kRate);
        s.swing = pv(kSwing);
        s.length = pv(kLength);
        s.attack = pv(kAttack);
        s.release = pv(kRelease);
        s.smooth = pv(kSmooth);
        s.depth = pv(kDepth);
        s.mode = (int)pv(kMode);
        s.filterLow = pv(kFilterLow);
        s.filterHigh = pv(kFilterHigh);
        s.resonance = pv(kResonance);
        s.offset = (int)pv(kOffset);
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        for (int k = 0; k < kMaxSteps; ++k) s.level[k] = pv((uint32_t)(kStep1 + k));
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        engine.setTransport(transport());
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);

        // (one engine call per host block: the position runs on from the block's start; chunks only bound the
        // bypass buffers)
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            // a NaN / Inf from upstream would otherwise stay in the filters for good (the bypass path, captured
            // above, stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginGate)
};

END_NAMESPACE_DISTRHO
