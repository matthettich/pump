// pump LIMIT: plugin (DSP side). Declared in a header so the UI can read its meters and history directly.
#pragma once
#include "PumpPlugin.hpp"
#include "LimitParams.hpp"
#include "LimitEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::limit;

class PluginLimit : public PumpPlugin {
public:
    PluginLimit() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 14, (int)std::max<uint32_t>(getBufferSize(), 8192));
        setLatency((uint32_t)engine.latency());
    }

    Engine engine;
    int latencyNow() const { return engine.latency(); }
    double rate() { return getSampleRate(); }

protected:
    const char* getLabel() const override { return "pump_limit"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 14, (int)std::max<uint32_t>(getBufferSize(), 8192));
        sync();
        // report the latency now: hosts read it at activation, before the first block
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void sync()
    {
        engine.setGain(pv(kGain));
        engine.setCeiling(pv(kCeiling));
        engine.setStyle((int)pv(kStyle));
        engine.setLookahead(pv(kLookahead));
        engine.setAttack(pv(kAttack));
        engine.setRelease(pv(kRelease));
        engine.setLinks(pv(kLinkTrans), pv(kLinkRel));
        engine.setTruePeak(pv(kTruePeak) >= 0.5f);
        engine.setOversampling((int)pv(kOversampling));
        engine.setDC(pv(kDC) >= 0.5f);
        engine.setDither((int)pv(kDither), (int)pv(kShaping));
        engine.setUnity(pv(kUnity) >= 0.5f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        const int lat = engine.latency();
        if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }

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
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLimit)
};

END_NAMESPACE_DISTRHO
