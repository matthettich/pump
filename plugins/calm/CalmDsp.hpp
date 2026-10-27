// pump CALM: the DSP side, shared by the plugin (PluginCalm.hpp, on PumpPlugin) and the web build
// (wasm/pump_wasm.cpp, on its HostBase). Base supplies pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run().
#pragma once
#include "CalmParams.hpp"
#include "CalmEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"
#include <atomic>
#include <cstring>
#include <utility>

namespace pump { namespace calm {

template <class Base>
class CalmDsp : public Base {
public:
    template <class... A>
    explicit CalmDsp(A&&... a) : Base(std::forward<A>(a)...)
    {
        prepareAll();
    }

    // feeds for the UI
    pump::ScopeRing preRing { pump::kAnalyzerRing }, postRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2], inPeak[2];
    Engine engine;
    int stereoMode() const { return engine.settings().stereo; }

    // Alt-hold listen and node solo, driven by the editor while the mouse button is held (never host parameters:
    // not saved with the session, not undone, not touched by host echoes). The UI writes the region first, then
    // auditionOn; false = off.
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };
    std::atomic<int> auditionRegion { pump::Audition::kBand };

protected:
    void prepareAll()
    {
        const double sr = this->getSampleRate() > 0 ? this->getSampleRate() : 48000.0;
        syncSettings();
        engine.prepare(sr);
        bypass.prepare(sr, engine.sizeFor(3) + 16, Engine::kMaxChunk);
        audition.prepare(sr);
        // report the latency now (constructor and activation): hosts, CLAP especially, read it before the first block
        lastLatency = engine.latency();
        this->setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void activate() override { prepareAll(); }

    void syncSettings()
    {
        Settings s;
        s.depth = this->pv(kDepth); s.sharpness = this->pv(kSharpness); s.selectivity = this->pv(kSelectivity);
        s.attackMs = this->pv(kAttack); s.releaseMs = this->pv(kRelease); s.mode = (int)this->pv(kMode);
        s.focusLo = this->pv(kFocusLow); s.focusHi = this->pv(kFocusHigh);
        s.stereo = (int)this->pv(kStereo); s.balance = this->pv(kBalance); s.quality = (int)this->pv(kQuality);
        s.delta = this->pv(kDelta) >= 0.5f; s.mix = this->pv(kMix); s.trimDb = this->pv(kTrim);
        for (int n = 0; n < kNumNodes; ++n)
        {
            s.nodes[n].freq = this->pv((uint32_t)nodeParam(n, kNFreq));
            s.nodes[n].gain = this->pv((uint32_t)nodeParam(n, kNGain));
            s.nodes[n].q = this->pv((uint32_t)nodeParam(n, kNQ));
        }
        engine.set(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) this->takeChanged(i);
            syncSettings();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);

        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)Engine::kMaxChunk);
            float* l = L + done;
            float* r = R + done;
            // latency can change (quality) inside the engine at a chunk boundary
            const int lat = engine.latency();
            if (lat != lastLatency) { this->setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.capture(l, r, n);
            // the bypass keeps the input as it came; everything after it hears a NaN / Inf sample as silence
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            float il = 0.0f, ir = 0.0f;
            for (int i = 0; i < n; ++i) { preRing.push(0.5f * (l[i] + r[i])); il = std::max(il, std::fabs(l[i])); ir = std::max(ir, std::fabs(r[i])); }
            inPeak[0].setMax(il); inPeak[1].setMax(ir);
            engine.process(l, r, n);
            const int lat2 = engine.latency();
            if (lat2 != lastLatency) { this->setLatency((uint32_t)lat2); bypass.setLatency(lat2); lastLatency = lat2; }
            const bool aud = auditionOn.load(std::memory_order_acquire);
            audition.set(aud, auditionFreq.load(std::memory_order_relaxed), auditionQ.load(std::memory_order_relaxed),
                         (pump::Audition::Region)pump::clampv(auditionRegion.load(std::memory_order_relaxed), 0, 2));
            audition.process(l, r, n);
            bypass.apply(l, r, n, this->pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                // never hand a non-finite sample to the host (a NaN input while bypassed, or a state that went bad)
                if (!std::isfinite(l[i])) l[i] = 0.0f;
                if (!std::isfinite(r[i])) r[i] = 0.0f;
                postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;

};

}} // namespace pump::calm
