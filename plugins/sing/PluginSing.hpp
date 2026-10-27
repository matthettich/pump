// pump SING: plugin (DSP side). Declared in a header so the UI can read its feeds and play its keyboard directly.
#pragma once
#include "PumpPlugin.hpp"
#include "SingParams.hpp"
#include "SingEngine.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::sing;

class PluginSing : public PumpPlugin {
public:
    PluginSing() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* t = std::getenv("PUMP_TEST_LYRICS")) lyrics = t;
        engine.setLyrics(lyrics);
        if (const char* n = std::getenv("PUMP_TEST_NOTES")) testNotes = n;   // headless screenshots: notes to hold (after 0.5 s)
        if (const char* b = std::getenv("PUMP_TEST_BANK")) setState("voicebank", b);        // headless screenshots: a voicebank
    }

    // gain staging (the volume pass, 2026-10-26): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -2.5f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];
    std::atomic<bool> restartRequest { false };
    // the voicebank loaded (nullptr: none) and what to say about it
    const Bank* bankNow() const { return bankPtr.load(std::memory_order_acquire); }
    std::string bankInfo() const { std::lock_guard<std::mutex> g(bankLock); return bankMsg; }

protected:
    const char* getLabel() const override { return "pump_sing"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        if (index - kPumpNumStates == 1) { state.key = "voicebank"; state.label = "Voicebank"; state.defaultValue = ""; state.hints = kStateIsFilenamePath; return; }
        state.key = "lyrics";
        state.label = "Lyrics";
        state.defaultValue = kDefaultLyrics;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "lyrics") == 0) { lyrics = value ? value : ""; engine.setLyrics(lyrics); return; }
        if (std::strcmp(key, "voicebank") == 0)
        {
            // read here (not on the audio thread); every bank loaded is kept until the plugin goes
            bankPath = value ? value : "";
            std::shared_ptr<Bank> b;
            std::string msg;
            if (!bankPath.empty())
            {
                try   // setState() must never throw into the host
                {
                    b = Bank::load(bankPath);
                    if (!b->ok()) { msg = "couldn't load: " + b->error; b.reset(); }
                    else msg = b->name + " (" + std::to_string(b->entries.size()) + " syllables)";
                }
                catch (...) { b.reset(); msg = "couldn't load the voicebank"; }
            }
            std::lock_guard<std::mutex> g(bankLock);
            if (b) keepBanks.push_back(b);
            bankPtr.store(b.get(), std::memory_order_release);
            bankMsg = msg;
            return;
        }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "lyrics") == 0) return String(lyrics.c_str());
        if (std::strcmp(key, "voicebank") == 0) return String(bankPath.c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.mode = (int)pv(kMode); s.choir = (int)pv(kChoir); s.octave = (int)pv(kOctave); s.size = pv(kSize) * 0.01;
        s.robot = pv(kRobot) * 0.01; s.voice = pv(kVoice) * 0.01; s.breath = pv(kBreath) * 0.01; s.bright = pv(kBright) * 0.01;
        s.soft = pv(kSoft) * 0.01; s.clarity = pv(kClarity) * 0.01; s.rough = pv(kRough) * 0.01;
        s.vib = pv(kVib) * 0.01; s.vibRate = pv(kVibRate); s.vibDelay = pv(kVibDelay); s.glide = pv(kGlide); s.wheel = pv(kWheel) * 0.01;
        s.hiss = pv(kHiss) * 0.01; s.diction = pv(kDiction) * 0.01; s.attack = pv(kAttack); s.release = pv(kRelease);
        s.spread = pv(kSpread) * 0.01; s.width = pv(kWidth) * 0.01; s.velocity = pv(kVelocity) * 0.01; s.drive = pv(kDrive) * 0.01;
        s.volumeDb = pv(kVolume) + kTrimDb;
        s.engine = (int)pv(kEngine); s.gender = pv(kGender) * 0.01;
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
        engine.setTransport(transport().playing);
        engine.setBank(bankPtr.load(std::memory_order_acquire));
        if (restartRequest.exchange(false)) engine.restart();
        if (!testNotes.empty() && (testWait += frames) > getSampleRate() * 0.5)
        {
            for (const char* p = testNotes.c_str(); *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
            testNotes.clear();
        }
        // the window's keys join the first chunk's MIDI (so they are handled after new lyrics are in place)
        int nk = 0;
        keys.drain([&](uint8_t note, uint8_t vel) { if (nk < 64) uiKeys[nk++] = { 0u, (uint8_t)(vel ? 0x90 : 0x80), note, vel }; });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, 256u);
            int nm = 0;
            if (done == 0) for (int k = 0; k < nk; ++k) midi[nm++] = uiKeys[k];
            while (ev < midiEventCount && midiEvents[ev].frame < done + n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= 512) continue;
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
    pump::synth::Midi midi[512 + 64] = {}, uiKeys[64] = {};
    std::string lyrics = kDefaultLyrics, testNotes, bankPath, bankMsg;
    std::atomic<const Bank*> bankPtr { nullptr };
    std::vector<std::shared_ptr<Bank>> keepBanks;
    mutable std::mutex bankLock;
    double testWait = 0.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSing)
};

END_NAMESPACE_DISTRHO
