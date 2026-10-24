// pump SPRING: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "SpringParams.hpp"
#include "SpringEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::spring;

class PluginSpring : public PumpPlugin {
public:
    PluginSpring() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: the output (mono sum, for the decay view) and its peaks; the engine has the springs' feeds
    pump::ScopeRing outRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    Engine engine;

    // The KICK key (never a host parameter: not saved, not automated, not undone). The editor bumps the counter; run()
    // kicks the tank once per bump it hasn't seen yet. kickStrength (0..1.5) is how hard (where the key was hit).
    std::atomic<uint32_t> kickRequest { 0 };
    std::atomic<float> kickStrength { 1.0f };

protected:
    const char* getLabel() const override { return "pump_spring"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        kickSeen = kickRequest.load(std::memory_order_relaxed);
        reportLatency();
    }
    // hosts (CLAP especially) read the latency right after activation, before the first block
    void reportLatency()
    {
        setLatency((uint32_t)engine.latency());
        bypass.setLatency(engine.latency());
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.model = (int)pv(kModel);
        s.dwell = pv(kDwell);
        s.decay = pv(kDecay);
        s.tension = pv(kTension);
        s.drip = pv(kDrip);
        s.tone = pv(kTone);
        s.lowCut = pv(kLowCut);
        s.preDelay = pv(kPreDelay);
        s.wobble = pv(kWobble);
        s.width = pv(kWidth);
        s.grit = pv(kGrit);
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        const uint32_t req = kickRequest.load(std::memory_order_acquire);
        if (req != kickSeen)
        {
            kickSeen = req;
            if (pv(kBypass) < 0.5f) engine.kick(pump::clampv((double)kickStrength.load(std::memory_order_relaxed), 0.2, 1.5));
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);

        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            // a NaN / Inf from upstream would otherwise ring in the springs for good (the bypass path, captured
            // above, stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { outRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    uint32_t kickSeen = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSpring)
};

END_NAMESPACE_DISTRHO
