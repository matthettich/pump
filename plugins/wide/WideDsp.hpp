// pump WIDE: the DSP side, shared by the plugin (PluginWide.hpp, on PumpPlugin) and the web build
// (wasm/pump_wasm.cpp, on its HostBase). Base supplies pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run().
#pragma once
#include "WideParams.hpp"
#include "WideEngine.hpp"
#include "dsp/Bypass.hpp"
#include <atomic>
#include <cstring>
#include <utility>

namespace pump { namespace wide {

template <class Base>
class WideDsp : public Base {
public:
    template <class... A>
    explicit WideDsp(A&&... a) : Base(std::forward<A>(a)...)
    {
        syncAll();
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 15, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
        reportLatency();
    }

    // feeds for the UI: output L/R (vectorscope) and mid/side (spectra)
    pump::ScopeRing scopeL { 1 << 15 }, scopeR { 1 << 15 }, midRing { pump::kAnalyzerRing }, sideRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }

    // The editor's Alt-hold listen (never a host parameter: not saved with the session, not undone, not touched by
    // host gesture echoes). -1 = off, 0..3 = hear only that band (a band >= the band count counts as off).
    std::atomic<int> listenHold { -1 };

protected:
    void activate() override
    {
        syncAll();
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 15, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
        reportLatency();
    }
    // hosts (CLAP especially) read the latency right after activation, before the first block
    void reportLatency()
    {
        lastLatency = engine.latency();
        this->setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void syncBand(int b)
    {
        BandSettings s;
        s.width = this->pv((uint32_t)bandParam(b, kWidth));
        s.stereoize = this->pv((uint32_t)bandParam(b, kStereoize));
        s.solo = this->pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = this->pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = this->pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        engine.setOutput(this->pv(kOutGain));
        engine.setBalance(this->pv(kBalance));
        engine.setMonoBass(this->pv(kMonoBass) >= 0.5f, this->pv(kMonoFreq), (int)this->pv(kMonoSlope));
        engine.setXoverMode((int)this->pv(kXoverMode));
        engine.setBands((int)this->pv(kNumBandsP));
        engine.setCrossovers(this->pv(kXover1), this->pv(kXover2), this->pv(kXover3));
        // (kListen is an unused placeholder: the listen band is listenHold, read in run())
    }
    void syncAll()
    {
        syncGlobals();
        for (int b = 0; b < kNumBands; ++b) syncBand(b);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            bool dirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!this->takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) dirty[b] = true; else global = true;
            }
            if (global) syncGlobals();
            for (int b = 0; b < kNumBands; ++b) if (dirty[b]) syncBand(b);
        }
        engine.setListen(listenHold.load(std::memory_order_relaxed));   // every block: the editor's Alt-hold
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
            // a NaN / Inf from upstream would otherwise stay in the filters and decorrelators for good (the bypass
            // path, captured above, stays a plain pass-through)
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { this->setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, this->pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                scopeL.push(l[i]); scopeR.push(r[i]);
                midRing.push(0.5f * (l[i] + r[i])); sideRing.push(0.5f * (l[i] - r[i]));
                pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;

};

}} // namespace pump::wide
