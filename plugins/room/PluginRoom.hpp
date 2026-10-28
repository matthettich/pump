// pump ROOM: plugin (DSP side). Declared in a header so the UI can read the profile and the design directly.
// The profile is a state holding the profile file's text ("profile"), so a session or System EQ carries it whole and
// doesn't depend on the file staying where it was; "profile_file" remembers where it came from (for the window).
#pragma once
#include "PumpPlugin.hpp"
#include "RoomParams.hpp"
#include "RoomEngine.hpp"
#include "dsp/Bypass.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::roomfx;

static constexpr const char* kRoomStateKeys[] = { "profile", "profile_file" };

class PluginRoom : public PumpPlugin {
public:
    PluginRoom() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        sync();
        if (const char* f = pump::testEnv("PUMP_TEST_PROFILE")) loadFileForTest(f);
        engine.prepare(getSampleRate());
        bypass.prepare(getSampleRate(), 1 << 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }

    Engine engine;
    pump::MeterValue outPeak[2];

    // the profile as the window shows it (non-real-time threads only)
    pump::room::Profile profile(std::string* error = nullptr, std::string* file = nullptr) const
    {
        std::lock_guard<std::mutex> g(profileLock);
        if (error) *error = profileError;
        if (file) *file = profileFile;
        return parsed;
    }
    unsigned profileSerial() const { return serial.load(); }

protected:
    const char* getLabel() const override { return "pump_room"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        const uint32_t k = index - kPumpNumStates;
        state.key = kRoomStateKeys[k];
        state.label = k == 0 ? "Room Profile" : "Room Profile File";
        state.defaultValue = "";
        state.hints = 0;
    }
    void setState(const char* key, const char* value) override
    {
        const std::string v = value ? value : "";
        if (std::strcmp(key, "profile") == 0) { applyProfile(v); return; }
        if (std::strcmp(key, "profile_file") == 0) { std::lock_guard<std::mutex> g(profileLock); profileFile = v; ++serial; return; }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        std::lock_guard<std::mutex> g(profileLock);
        if (std::strcmp(key, "profile") == 0) return String(profileText.c_str());
        if (std::strcmp(key, "profile_file") == 0) return String(profileFile.c_str());
        return PumpPlugin::getState(key);
    }

    // a profile's text (empty = none); a file that isn't one leaves the current profile in place
    void applyProfile(const std::string& text)
    {
        pump::room::Profile p;
        std::string why;
        bool ok = true;
        if (!text.empty())
        {
            try { ok = pump::room::parseProfile(text.data(), text.size(), p, why); }
            catch (...) { ok = false; why = "could not be read"; }   // never let an exception reach the host
        }
        {
            std::lock_guard<std::mutex> g(profileLock);
            if (!ok) { profileError = why; ++serial; return; }
            profileText = text;
            parsed = p;
            profileError.clear();
            ++serial;
        }
        engine.setProfile(text.empty() ? nullptr : &p);
    }

    void loadFileForTest(const char* path)
    {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return;
        std::string s;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && s.size() < ((size_t)4 << 20)) s.append(buf, n);
        std::fclose(f);
        applyProfile(s);
    }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate());
        bypass.prepare(getSampleRate(), 1 << 16, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }

    void sync()
    {
        Settings s;
        s.amount = pv(kAmount) / 100.0;
        s.linear = (int)pv(kPhase) == kLinear;
        s.align = pv(kTimeAlign) >= 0.5f;
        engine.setSettings(s);
        engine.setOutputGain(pv(kOutGain));
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
    mutable std::mutex profileLock;
    std::string profileText, profileFile, profileError;
    pump::room::Profile parsed;
    std::atomic<unsigned> serial { 0 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginRoom)
};

END_NAMESPACE_DISTRHO
