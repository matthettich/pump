// pump SNAP: plugin (DSP side). Declared in a header so the UI can read the engine's history and gain meter directly.
#pragma once
#include "PumpPlugin.hpp"
#include "SnapParams.hpp"
#include "SnapEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::snap;

class PluginSnap : public PumpPlugin {
public:
    PluginSnap() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        prepareAll();
    }

    // feeds for the UI: the engine's history rings and gain extremes, and the output peaks
    Engine engine;
    pump::MeterValue outPeak[2];
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_snap"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override { prepareAll(); }
    void sampleRateChanged(double) override { prepareAll(); }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        sync();
        engine.prepare(sr, (int)getBufferSize());
        bypass.prepare(sr, engine.maxLatency() + 8, (int)std::max<uint32_t>(getBufferSize(), 8192));
        // hosts (CLAP especially) read the latency right after activation, before the first block
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void sync()
    {
        Settings s;
        s.attack = pv(kAttack);
        s.sustain = pv(kSustain);
        s.attackMs = pv(kAttackTime);
        s.releaseMs = pv(kRelease);
        s.link = pv(kLink);
        s.focus = (int)pv(kFocus);
        s.band = (int)pv(kBand);
        s.crossover = pv(kCrossover);
        s.lookaheadMs = pv(kLookahead);
        s.clip = pv(kClip) >= 0.5f;
        s.ceilingDb = pv(kCeiling);
        s.delta = pv(kDelta) >= 0.5f;
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
            // a NaN / Inf from upstream would otherwise stay in the detector and the filters for good (the bypass
            // path, captured above, stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            // latency follows the lookahead (it changes only after the output has faded out)
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSnap)
};

END_NAMESPACE_DISTRHO
