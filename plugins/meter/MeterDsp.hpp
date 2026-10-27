// pump METER: the DSP side, shared by the plugin (PluginMeter.hpp, on PumpPlugin) and the web build
// (wasm/pump_wasm.cpp, on its HostBase). Base supplies pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run().
#pragma once
#include "MeterParams.hpp"
#include "MeterEngine.hpp"
#include <atomic>
#include <cstring>
#include <utility>

namespace pump { namespace meter {

template <class Base>
class MeterDsp : public Base {
public:
    template <class... A>
    explicit MeterDsp(A&&... a) : Base(std::forward<A>(a)...)
    {
        engine.prepare(this->getSampleRate());
    }

    Engine engine;

protected:
    void activate() override
    {
        engine.prepare(this->getSampleRate());
        engine.setResetCounter(this->pv(kReset));
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) this->takeChanged(i);
            engine.setResetCounter(this->pv(kReset));
        }
        // pass-through, bit-exact (in-place hosts already share the buffers)
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        engine.process(inputs[0], inputs[1], (int)frames);
    }

private:
};

}} // namespace pump::meter
