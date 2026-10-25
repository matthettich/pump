// pump AMP: plugin (DSP side). Declared in a header so the UI can read its feeds and file info directly.
// The model and the impulse response are two states holding file paths ("model", "ir"): saved with the session,
// loaded when set (never on the audio thread), loaded again when the sample rate changes.
#pragma once
#include "PumpPlugin.hpp"
#include "AmpParams.hpp"
#include "AmpEngine.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::amp;

static constexpr const char* kAmpStateKeys[] = { "model", "ir" };

class PluginAmp : public PumpPlugin {
public:
    PluginAmp() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        // headless screenshots (tools/shot.sh): files to load at start
        if (const char* m = std::getenv("PUMP_TEST_MODEL")) { modelPath = m; loadModel(); }
        if (const char* i = std::getenv("PUMP_TEST_IR")) { irPath = i; loadIr(); }
        engine.installNow();
        reportLatency();
    }

    Engine engine;
    pump::MeterValue outPeak[2];
    int latencyNow() const { return engine.latency(); }

    // what the window shows about the files (non-real-time threads only)
    ModelInfo modelInfo() const { std::lock_guard<std::mutex> g(infoLock); return mInfo; }
    IrInfo irInfo() const { std::lock_guard<std::mutex> g(infoLock); return iInfo; }
    unsigned infoSerial() const { return serial.load(); }

protected:
    const char* getLabel() const override { return "pump_amp"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        const uint32_t k = index - kPumpNumStates;
        state.key = kAmpStateKeys[k];
        state.label = k == 0 ? "Amp Model" : "Cabinet IR";
        state.defaultValue = "";
        state.hints = kStateIsFilenamePath;
    }
    void setState(const char* key, const char* value) override
    {
        const std::string v = value ? value : "";
        if (std::strcmp(key, "model") == 0) { modelPath = v; loadModel(); return; }
        if (std::strcmp(key, "ir") == 0) { irPath = v; loadIr(); return; }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "model") == 0) return String(modelPath.c_str());
        if (std::strcmp(key, "ir") == 0) return String(irPath.c_str());
        return PumpPlugin::getState(key);
    }

    // If the last attempt at this exact file never finished (the host went down inside NAM's core, which can happen on
    // a damaged model: see ModelGuard.hpp), refuse it instead of taking the project down again. The marker stays until
    // the file changes (a repaired or replaced file has a new size / time, so a new marker key) or the user clears the
    // model; a project that points at the bad file therefore opens, with the model refused, every time after the first
    // crash. Picking a different model replaces the path, and a refused file picked again by hand can be retried with
    // CLEAR (which removes the marker), then picking it.
    // (The host's restore and the user's own pick both arrive as setState("model", path): they can't be told apart.)
    void loadModel()
    {
        ModelInfo info;
        if (modelPath.empty()) { if (!refusedPath.empty()) clearLoadMarker(refusedPath); refusedPath.clear(); engine.clearModel(); }
        else if (previousLoadCrashed(modelPath))
        {
            refusedPath = modelPath;
            info.path = modelPath;
            info.name = std::filesystem::path(modelPath).stem().string();
            info.error = "not loaded: this model crashed the plugin last time (CLEAR, then pick it again to retry)";
        }
        else
        {
            markLoadStart(modelPath);
            info = engine.loadModel(modelPath);
            markLoadDone(modelPath);
            if (!info.loaded) { info.path = modelPath; }
        }
        std::lock_guard<std::mutex> g(infoLock);
        mInfo = info;
        ++serial;
    }
    void loadIr()
    {
        IrInfo info;
        if (irPath.empty()) engine.clearIr();
        else info = engine.loadIr(irPath);
        std::lock_guard<std::mutex> g(infoLock);
        iInfo = info;
        ++serial;
    }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        // the files again, at this rate
        if (!modelPath.empty()) loadModel();
        if (!irPath.empty()) loadIr();
        engine.installNow();
        reportLatency();
    }
    // the latency a loaded model will have: report it now (hosts read it right after activation)
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.inputDb = pv(kInput);
        s.gateDb = pv(kGate);
        s.normalize = pv(kNormalize) >= 0.5f;
        s.channels = (int)pv(kChannels);
        s.bass = pv(kBass);
        s.mid = pv(kMid);
        s.treble = pv(kTreble);
        s.presence = pv(kPresence);
        s.cab = pv(kCab) >= 0.5f;
        s.cabLow = pv(kCabLowCut);
        s.cabHigh = pv(kCabHighCut);
        s.outDb = pv(kOutGain);
        engine.setSettings(s);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
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
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }

private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
    std::string modelPath, irPath;
    std::string refusedPath;   // a model refused this session because its last load crashed (see loadModel)
    mutable std::mutex infoLock;
    ModelInfo mInfo;
    IrInfo iInfo;
    std::atomic<unsigned> serial { 0 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginAmp)
};

END_NAMESPACE_DISTRHO
