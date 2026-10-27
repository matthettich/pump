// pump TABLE: plugin (DSP side). Declared in a header so the UI can read its feeds, play its keyboard and see the
// user tables directly. User wavetables are states holding file paths ("table_a", "table_b").
#pragma once
#include "PumpPlugin.hpp"
#include "TableParams.hpp"
#include "TableEngine.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::table;

class PluginTable : public PumpPlugin {
public:
    PluginTable() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        // headless screenshots (tools/shot.sh): notes to hold from the start, e.g. PUMP_TEST_NOTES=48,55,60,64
        if (const char* n = std::getenv("PUMP_TEST_NOTES"))
            for (const char* p = n; *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = +4.5f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;              // the window's keyboard
    pump::MeterValue outPeak[2];
    // the user table loaded on oscillator o (nullptr = none); its name / error for the window
    const Table* userTable(int o) const { return userNow[o & 1]; }
    std::string userInfo(int o) const { std::lock_guard<std::mutex> g(lock); return o ? infoB : infoA; }

protected:
    const char* getLabel() const override { return "pump_table"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        const uint32_t k = index - kPumpNumStates;
        state.key = k == 0 ? "table_a" : "table_b";
        state.label = k == 0 ? "Oscillator A wavetable" : "Oscillator B wavetable";
        state.defaultValue = "";
        state.hints = kStateIsFilenamePath;
    }
    void setState(const char* key, const char* value) override
    {
        const int o = std::strcmp(key, "table_a") == 0 ? 0 : (std::strcmp(key, "table_b") == 0 ? 1 : -1);
        if (o < 0) { PumpPlugin::setState(key, value); return; }
        path[o] = value ? value : "";
        std::string err;
        std::shared_ptr<Table> t;
        try   // setState() must never throw into the host
        {
            if (!path[o].empty()) t = loadUserTable(path[o], err);
        }
        catch (...) { t.reset(); err = "couldn't load the file"; }
        std::lock_guard<std::mutex> g(lock);
        if (t) { keep.push_back(t); userNow[o] = t.get(); }   // kept until the plugin goes (the audio thread may still read the old one)
        else userNow[o] = nullptr;
        engine.setUserTable(o, userNow[o]);
        (o ? infoB : infoA) = path[o].empty() ? "" : (t ? t->name : "couldn't load: " + err);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "table_a") == 0) return String(path[0].c_str());
        if (std::strcmp(key, "table_b") == 0) return String(path[1].c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        for (int o = 0; o < 2; ++o)
        {
            const int b = o * kOscStride;
            OscSettings& x = s.osc[o];
            x.table = (int)pv(kTableA + b); x.pos = pv(kPosA + b) * 0.01; x.voices = (int)pv(kVoicesA + b);
            x.detune = pv(kDetuneA + b) * 0.01; x.spread = pv(kSpreadA + b) * 0.01; x.oct = (int)pv(kOctA + b);
            x.semi = (int)pv(kSemiA + b); x.fine = pv(kFineA + b); x.level = pv(kLevelA + b) * 0.01; x.pan = pv(kPanA + b) * 0.01;
        }
        s.sub = pv(kSub) * 0.01; s.noise = pv(kNoise) * 0.01;
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.driveDb = pv(kDrive);
        s.filterEnv = pv(kFilterEnv) * 0.01; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.ampA = pv(kAmpA) * 0.001; s.ampD = pv(kAmpD) * 0.001; s.ampS = pv(kAmpS) * 0.01; s.ampR = pv(kAmpR) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.modA = pv(kModA) * 0.001; s.modD = pv(kModD) * 0.001; s.modS = pv(kModS) * 0.01; s.modR = pv(kModR) * 0.001;
        s.lfo1Shape = (int)pv(kLfo1Shape); s.lfo1Rate = pv(kLfo1Rate);
        s.lfo2Shape = (int)pv(kLfo2Shape); s.lfo2Rate = pv(kLfo2Rate); s.lfo2Sync = pv(kLfo2Sync) >= 0.5f; s.lfo2Div = (int)pv(kLfo2Div);
        for (int k = 0; k < 4; ++k)
        {
            s.mod[k].src = (int)pv(kSrc1 + 3 * k); s.mod[k].dst = (int)pv(kDst1 + 3 * k); s.mod[k].amt = pv(kAmt1 + 3 * k) * 0.01;
        }
        s.voiceMode = (int)pv(kVoiceMode); s.polyphony = (int)pv(kPolyphony); s.glide = pv(kGlide);
        s.bendRange = pv(kBendRange); s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        engine.setTransport(transport());
        keys.drain([&](uint8_t note, uint8_t vel) { engine.uiNote(note, vel); });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            int nm = 0;
            while (ev < midiEventCount && midiEvents[ev].frame < done + n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= kMaxMidi) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            float* l = outputs[0] + done;
            float* r = outputs[1] + done;
            engine.process(l, r, (int)n, midi, nm);
            outLimit.process(l, r, (int)n);
            float pl = 0.0f, pr = 0.0f;
            for (uint32_t i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += n;
        }
    }

private:
    static constexpr uint32_t kBlock = 256;
    static constexpr int kMaxMidi = 512;
    pump::synth::Midi midi[kMaxMidi] = {};
    std::string path[2];
    mutable std::mutex lock;
    std::vector<std::shared_ptr<Table>> keep;
    const Table* userNow[2] = { nullptr, nullptr };
    std::string infoA, infoB;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginTable)
};

END_NAMESPACE_DISTRHO
