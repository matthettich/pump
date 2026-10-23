// pump METER: plugin (DSP side). Audio passes through bit for bit; the engine only reads it. Declared in a
// header so the UI can read the readouts, history and spectrum feed directly.
#pragma once
#include "PumpPlugin.hpp"
#include "MeterParams.hpp"
#include "MeterEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::meter;

class PluginMeter : public PumpPlugin {
public:
    PluginMeter() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        engine.prepare(getSampleRate());
    }

    Engine engine;

protected:
    const char* getLabel() const override { return "pump_meter"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void activate() override
    {
        engine.prepare(getSampleRate());
        engine.setResetCounter(pv(kReset));
    }
    void sampleRateChanged(double) override { activate(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            engine.setResetCounter(pv(kReset));
        }
        // pass-through, bit-exact (in-place hosts already share the buffers)
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        engine.process(inputs[0], inputs[1], (int)frames);
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMeter)
};

END_NAMESPACE_DISTRHO
