// pump LIMIT: the DSP side, shared by the plugin (PluginLimit.hpp, on PumpPlugin) and the web build
// (wasm/pump_wasm.cpp, on its HostBase). Base supplies pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run().
#pragma once
#include "LimitParams.hpp"
#include "LimitEngine.hpp"
#include "dsp/Bypass.hpp"
#include <cstring>
#include <utility>

namespace pump { namespace limit {

template <class Base>
class LimitDsp : public Base {
public:
    template <class... A>
    explicit LimitDsp(A&&... a) : Base(std::forward<A>(a)...)
    {
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 14, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
        this->setLatency((uint32_t)engine.latency());
    }

    Engine engine;

    void activate() override
    {
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 14, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
        sync();
        // report the latency now: hosts read it at activation, before the first block
        lastLatency = engine.latency();
        this->setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) this->takeChanged(i);
            sync();
        }
        const int lat = engine.latency();
        if (lat != lastLatency) { this->setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }

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
            bypass.apply(l, r, n, this->pv(kBypass) >= 0.5f);
            done += (uint32_t)n;
        }
    }

private:
    void sync()
    {
        engine.setGain(this->pv(kGain));
        engine.setCeiling(this->pv(kCeiling));
        engine.setStyle((int)this->pv(kStyle));
        engine.setLookahead(this->pv(kLookahead));
        engine.setAttack(this->pv(kAttack));
        engine.setRelease(this->pv(kRelease));
        engine.setLinks(this->pv(kLinkTrans), this->pv(kLinkRel));
        engine.setTruePeak(this->pv(kTruePeak) >= 0.5f);
        engine.setOversampling((int)this->pv(kOversampling));
        engine.setDC(this->pv(kDC) >= 0.5f);
        engine.setDither((int)this->pv(kDither), (int)this->pv(kShaping));
        engine.setUnity(this->pv(kUnity) >= 0.5f);
    }

    pump::BypassMixer bypass;
    int lastLatency = -1;
};

}} // namespace pump::limit
