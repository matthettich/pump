// pump EQ: the DSP side, shared by the plugin (PluginEQ.hpp, on PumpPlugin) and the web build (wasm/pump_wasm.cpp,
// on its HostBase). Base supplies the host side: pv(), takeAnyChanged(), takeChanged(), getSampleRate(),
// getBufferSize(), setLatency(), and virtual activate() / run(). One copy, so the plugin and Thunder Plus can't drift.
#pragma once
#include "EQParams.hpp"
#include "EQEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"
#include <atomic>
#include <cstring>
#include <utility>

namespace pump { namespace eq {

template <class Base>
class EQDsp : public Base {
public:
    template <class... A>
    explicit EQDsp(A&&... a) : Base(std::forward<A>(a)...)
    {
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 17, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
    }

    // feeds for the UI (analyzers)
    pump::ScopeRing preRing { pump::kAnalyzerRing }, postRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2];   // max-hold since the UI last read it
    Engine engine;

    // Listening driven by the editor's Alt-hold (never host parameters: not saved with the session, not undone,
    // not touched by host gesture echoes). -1 / false = off.
    std::atomic<int> soloHold { -1 };
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };

    void activate() override
    {
        engine.prepare(this->getSampleRate(), (int)this->getBufferSize());
        bypass.prepare(this->getSampleRate(), 1 << 17, (int)std::max<uint32_t>(this->getBufferSize(), 8192));
        audition.prepare(this->getSampleRate());
        syncAll();
        engine.finishPending();   // a session saved in linear phase: build it now, not on the audio thread later
        // report latency here: hosts (CLAP especially) read it right after activation
        lastLatency = engine.latency();
        this->setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (this->takeAnyChanged())
        {
            bool bandDirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!this->takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) bandDirty[b] = true; else global = true;
            }
            for (int b = 0; b < kNumBands; ++b) if (bandDirty[b]) syncBand(b);
            if (global) syncGlobals();
        }
        syncSolo();   // every block: the editor's Alt-hold solo isn't a parameter
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
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            audition.set(auditionOn.load(std::memory_order_relaxed), auditionFreq.load(std::memory_order_relaxed), auditionQ.load(std::memory_order_relaxed));
            audition.process(l, r, n);
            bypass.apply(l, r, n, this->pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                // never hand a non-finite sample to the host (a NaN input, or a state that went bad)
                if (!std::isfinite(l[i])) l[i] = 0.0f;
                if (!std::isfinite(r[i])) r[i] = 0.0f;
                postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    void syncGlobals()
    {
        engine.setGainScale(this->pv(kGainScale));
        engine.setOutput(this->pv(kOutGain));
        engine.setAutoGain(this->pv(kAutoGain) >= 0.5f);
        engine.setDynTimes(this->pv(kDynAttack), this->pv(kDynRelease));
        engine.setPhaseMode((int)this->pv(kPhase), (int)this->pv(kLinQuality));
    }
    void syncAll()
    {
        for (int b = 0; b < kNumBands; ++b) syncBand(b);
        syncGlobals();
        syncSolo();
    }
    void syncSolo()
    {
        const int hold = soloHold.load(std::memory_order_relaxed);
        engine.setSolo(hold >= 0 ? hold : (int)this->pv(kSolo) - 1);
    }
    void syncBand(int b)
    {
        auto p = [&](int field) { return this->pv((uint32_t)bandParam(b, field)); };
        pump::BandSpec s;
        s.shape = (pump::Shape)(int)p(kShape);
        s.freq = p(kFreq);
        s.gainDb = p(kGain);
        s.q = p(kQ);
        s.slope = kSlopeValues[pump::clampv((int)p(kSlope), 0, kMaxSlopeIdx)];
        engine.setBand(b, p(kUsed) >= 0.5f, p(kBandBypass) >= 0.5f, s, (int)p(kPlace), p(kDyn) >= 0.5f, p(kDynRange), p(kDynThresh));
    }

    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;
};

}} // namespace pump::eq
