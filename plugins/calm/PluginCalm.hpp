// pump CALM: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
#pragma once
#include "PumpPlugin.hpp"
#include "CalmParams.hpp"
#include "CalmEngine.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::calm;

class PluginCalm : public PumpPlugin {
public:
    PluginCalm() : PumpPlugin(params().data(), kNumParams, &presets())
    {
        prepareAll();
    }

    // feeds for the UI
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2], inPeak[2];
    Engine engine;
    int latencyNow() const { return engine.latency(); }
    int stereoMode() const { return engine.settings().stereo; }

protected:
    const char* getLabel() const override { return "pump_calm"; }
    const char* getDescription() const override { return "Dynamic resonance suppressor: finds and calms harsh, ringing frequencies as they happen."; }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncSettings();
        engine.prepare(sr);
        bypass.prepare(sr, engine.sizeFor(3) + 16, Engine::kMaxChunk);
        audition.prepare(sr);
        lastLatency = -1;
    }
    void activate() override { prepareAll(); }
    void sampleRateChanged(double) override { prepareAll(); }

    void syncSettings()
    {
        Settings s;
        s.depth = pv(kDepth); s.sharpness = pv(kSharpness); s.selectivity = pv(kSelectivity);
        s.attackMs = pv(kAttack); s.releaseMs = pv(kRelease); s.mode = (int)pv(kMode);
        s.focusLo = pv(kFocusLow); s.focusHi = pv(kFocusHigh);
        s.stereo = (int)pv(kStereo); s.balance = pv(kBalance); s.quality = (int)pv(kQuality);
        s.delta = pv(kDelta) >= 0.5f; s.mix = pv(kMix); s.trimDb = pv(kTrim);
        for (int n = 0; n < kNumNodes; ++n)
        {
            s.nodes[n].freq = pv((uint32_t)nodeParam(n, kNFreq));
            s.nodes[n].gain = pv((uint32_t)nodeParam(n, kNGain));
            s.nodes[n].q = pv((uint32_t)nodeParam(n, kNQ));
        }
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
            // latency can change (quality) inside the engine at a chunk boundary
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.capture(l, r, n);
            float il = 0.0f, ir = 0.0f;
            for (int i = 0; i < n; ++i) { preRing.push(0.5f * (l[i] + r[i])); il = std::max(il, std::fabs(l[i])); ir = std::max(ir, std::fabs(r[i])); }
            inPeak[0].setMax(il); inPeak[1].setMax(ir);
            engine.process(l, r, n);
            const int lat2 = engine.latency();
            if (lat2 != lastLatency) { setLatency((uint32_t)lat2); bypass.setLatency(lat2); lastLatency = lat2; }
            audition.set(pv(kAudition) >= 0.5f, pv(kAuditionFreq), pv(kAuditionQ), (pump::Audition::Region)pump::clampv((int)pv(kAuditionRegion), 0, 2));
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCalm)
};

END_NAMESPACE_DISTRHO
