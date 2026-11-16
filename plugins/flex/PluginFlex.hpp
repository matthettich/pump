// pump FLEX: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "FlexParams.hpp"
#include "FlexEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::flex;

class PluginFlex : public PumpPlugin {
public:
    PluginFlex() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        setLatency(0); bypass.setLatency(0);
    }

    Engine engine;
    pump::MeterValue outPeak[2];

protected:
    const char* getLabel() const override { return "pump_flex"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        setLatency(0); bypass.setLatency(0);
    }

    // PLAY picks a slot, the slot holds a pattern (0 = empty: nothing runs); the engine gets the pattern's flx value and
    // crossfades from the one before
    void sync()
    {
        Settings s;
        const int play = (int)pv(kPlay);
        s.flx = play >= 1 && play <= kNumSlots ? slotToFlx((int)pv((uint32_t)(kSlot1 + play - 1))) : 0;
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
        engine.setTransport(transport());
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        const bool bypassed = pv(kBypass) >= 0.5f;
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, bypassed);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginFlex)
};

END_NAMESPACE_DISTRHO
