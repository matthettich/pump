// pump COMP: the DSP side, shared by the plugin (PluginComp.hpp, on PumpPlugin) and the web build
// (wasm/pump_wasm.cpp, on its HostBase). Base supplies pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run().
#pragma once
#include "CompParams.hpp"
#include "CompEngine.hpp"
#include "dsp/Bypass.hpp"
#include <cstring>
#include <utility>

namespace pump { namespace comp {

template <class Base>
class CompDsp : public Base {
public:
    template <class... A>
    explicit CompDsp(A&&... a) : Base(std::forward<A>(a)...) { prepareAll(); }

    Engine engine;
    // audio inputs the host gave us: 4 with the external sidechain (inputs 3-4), 2 when it chose plain stereo (AU)
    uint16_t numInputs = 4;

    void activate() override
    {
        prepareAll();
        lastLatency = -1;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) this->takeChanged(i);
            syncValues();
            engine.setSettings(settingsFromParams(values));
        }
        const bool bypassed = this->pv(kBypass) >= 0.5f;
        const bool hasSc = numInputs >= 4 && inputs[2] != nullptr && inputs[3] != nullptr;

        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            // copy everything first: hosts may hand us aliased in/out (and even sidechain) buffers
            for (int i = 0; i < n; ++i)
            {
                bufL[i] = inputs[0][done + i]; bufR[i] = inputs[1][done + i];
                scL[i] = hasSc ? inputs[2][done + i] : 0.0f; scR[i] = hasSc ? inputs[3][done + i] : 0.0f;
            }
            bypass.capture(bufL, bufR, n);
            engine.process(bufL, bufR, scL, scR, bufL, bufR, n);
            // the engine applies latency changes between chunks: keep the bypass path matched
            const int lat = engine.latency();
            if (lat != lastLatency) { bypass.setLatency(lat); lastLatency = lat; this->setLatency((uint32_t)lat); }
            bypass.apply(bufL, bufR, n, bypassed);
            std::memcpy(outputs[0] + done, bufL, sizeof(float) * (size_t)n);
            std::memcpy(outputs[1] + done, bufR, sizeof(float) * (size_t)n);
            done += (uint32_t)n;
        }
    }

private:
    void prepareAll()
    {
        const double sr = this->getSampleRate() > 0 ? this->getSampleRate() : 48000.0;
        syncValues();
        engine.setSettings(settingsFromParams(values));
        engine.prepare(sr, (int)this->getBufferSize());
        bypass.prepare(sr, engine.maxLatencySamples() + 8, kBlock);
        bypass.setLatency(engine.latency());
        this->setLatency((uint32_t)engine.latency());
    }
    void syncValues() { for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) values[i] = this->pv(i); }

    static constexpr int kBlock = 256;
    pump::BypassMixer bypass;
    float values[kNumParams] = {};
    float bufL[kBlock] = {}, bufR[kBlock] = {}, scL[kBlock] = {}, scR[kBlock] = {};
    int lastLatency = -1;
};

}} // namespace pump::comp
