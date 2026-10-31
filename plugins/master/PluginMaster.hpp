// pump MASTER: plugin (DSP side). Declared in a header so the UI can read its feeds directly.
// The learned analysis is a state ("analysis", text), so a session keeps the master it was made with; the window
// starts and finishes LEARN through the engine's atomics (never a host parameter: nothing to save or echo).
#pragma once
#include "PumpPlugin.hpp"
#include "MasterParams.hpp"
#include "MasterEngine.hpp"
#include "dsp/Bypass.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::master;

class PluginMaster : public PumpPlugin {
public:
    PluginMaster() : PumpPlugin(params().data(), kNumParams, &presets(), 1)
    {
        prepareAll();
        // screenshots: learn the first PUMP_TEST_LEARN seconds of the test audio (standalone test builds only)
        if (const char* t = pump::testEnv("PUMP_TEST_LEARN")) { testLearnLeft = (int64_t)(std::atof(t) * getSampleRate()); learnStart(); }
    }

    Engine engine;
    pump::ScopeRing preRing { pump::kAnalyzerRing }, postRing { pump::kAnalyzerRing };
    pump::MeterValue outPeak[2];

    int latencyNow() const { return engine.latency(); }

    // the window's LEARN key
    void learnStart() { engine.learnCmd.store(1); }
    void learnFinish() { engine.learnCmd.store(2); }
    void learnCancel() { engine.learnCmd.store(3); }

protected:
    const char* getLabel() const override { return "pump_master"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        state.key = "analysis";
        state.label = "Learned Analysis";
        state.defaultValue = "";
        state.hints = 0;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "analysis") == 0)
        {
            Analysis a;   // empty or unreadable text: no analysis (the plugin passes the audio through)
            try { if (value && *value) parseAnalysis(value, a); } catch (...) {}
            std::lock_guard<std::mutex> g(stateLock);
            pendingText = a.valid ? serializeAnalysis(a) : std::string();
            engine.incoming.store(a);
            pendingSerial = engine.incomingSerial.fetch_add(1) + 1;
            return;
        }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "analysis") == 0)
        {
            {
                // loaded but not yet picked up by the audio thread: what was loaded
                std::lock_guard<std::mutex> g(stateLock);
                if (pendingSerial != 0 && engine.consumedSerial.load() != pendingSerial) return String(pendingText.c_str());
            }
            Analysis a;
            for (int i = 0; i < 64; ++i) if (engine.published.load(a)) break;
            return String(serializeAnalysis(a).c_str());
        }
        return PumpPlugin::getState(key);
    }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        sync();
        engine.prepare(sr);
        bypass.prepare(sr, engine.latency() + 16, Engine::kMaxChunk);
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void activate() override { prepareAll(); }

    void sync()
    {
        Settings s;
        s.style = (int)pv(kStyle); s.intensity = pv(kIntensity);
        s.toneOn = pv(kToneOn) >= 0.5f; s.toneAmt = pv(kToneAmt);
        s.dynOn = pv(kDynOn) >= 0.5f; s.dynAmt = pv(kDynAmt);
        s.stereoOn = pv(kStereoOn) >= 0.5f; s.stereoAmt = pv(kStereoAmt);
        s.loudOn = pv(kLoudOn) >= 0.5f; s.target = pv(kTarget); s.ceiling = pv(kCeiling); s.loudMode = (int)pv(kLoudMode);
        s.compare = pv(kCompare) >= 0.5f;
        engine.set(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        // LEARN finishes by itself when the song stops (it started, or was already, playing)
        const bool playing = transport().playing;
        const bool learning = engine.uiLearning.load(std::memory_order_relaxed);
        if (learning && playing) sawPlaying = true;
        if (learning && sawPlaying && !playing) engine.learnCmd.store(2);
        if (!learning) sawPlaying = false;

        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        if (testLearnLeft > 0 && (testLearnLeft -= frames) <= 0) learnFinish();
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)Engine::kMaxChunk);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
    bool sawPlaying = false;
    int64_t testLearnLeft = 0;
    mutable std::mutex stateLock;
    std::string pendingText;
    unsigned pendingSerial = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMaster)
};

END_NAMESPACE_DISTRHO
