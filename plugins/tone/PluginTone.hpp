// pump TONE: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "ToneParams.hpp"
#include "ToneEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tone;

class PluginTone : public PumpPlugin {
public:
    PluginTone() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        prepareAll();
    }

    // feeds for the UI
    pump::ScopeRing preRing { pump::kAnalyzerRing }, postRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }

    // Alt-hold listen, set by the UI while the mouse is held (direct access). Never a host parameter, so a host
    // can't save it with the session or echo a stale value back; the UI clears it when it closes. (The audition*
    // parameters stay in the table as unused placeholders: ids never shift.)
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };

protected:
    const char* getLabel() const override { return "pump_tone"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncSettings();
        engine.prepare(sr);
        bypass.prepare(sr, engine.sizeFor(1) + 16, Engine::kMaxChunk);
        audition.prepare(sr);
        // report the latency now (constructor, activate, rate change): hosts read it at activation, before the
        // first block; the bypass path is matched to it from the start
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void activate() override { prepareAll(); }

    void syncSettings()
    {
        Settings s;
        s.recover = pv(kRecover); s.tame = pv(kTame); s.bias = pv(kBias); s.brighten = pv(kBrighten); s.body = pv(kBody);
        s.speed = pv(kSpeed); s.autoLevel = pv(kAutoLevel) >= 0.5f; s.outDb = pv(kOutGain); s.mix = pv(kMix);
        s.quality = (int)pv(kQuality); s.delta = pv(kDelta) >= 0.5f;
        engine.set(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
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
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.capture(l, r, n);
            // after the capture, so bypass stays a true pass-through: one NaN/Inf from upstream must not reach the
            // engine (its analysis is recursive), the listen filter or the analyzer feed
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            const int lat2 = engine.latency();
            if (lat2 != lastLatency) { setLatency((uint32_t)lat2); bypass.setLatency(lat2); lastLatency = lat2; }
            audition.set(auditionOn.load(std::memory_order_relaxed), auditionFreq.load(std::memory_order_relaxed),
                         auditionQ.load(std::memory_order_relaxed));
            audition.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginTone)
};

END_NAMESPACE_DISTRHO
