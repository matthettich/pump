// pump PRISM: plugin (DSP side). Declared in a header so the UI can read its feeds, its sounds' analyses and play its
// keyboard directly. Loaded sounds are states holding file paths ("file_a", "file_b"); they are read and analysed
// in setState (not the audio thread), and kept until the plugin goes (the audio thread may still read an old one).
#pragma once
#include "PumpPlugin.hpp"
#include "PrismParams.hpp"
#include "PrismEngine.hpp"
#include "PrismSources.hpp"
#include "dsp/Wav.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::prism;

class PluginPrism : public PumpPlugin {
public:
    PluginPrism() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        Builtins::get().start();
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = std::getenv("PUMP_TEST_NOTES")) testNotes = n;
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -4.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

    // what plays now (for the window): A and B (nullptr = none / not ready yet)
    const Analysis* soundA() const { return nowA.load(std::memory_order_acquire); }
    const Analysis* soundB() const { return nowB.load(std::memory_order_acquire); }
    std::string fileInfo(int o) const { std::lock_guard<std::mutex> g(lock); return o ? infoB : infoA; }

protected:
    const char* getLabel() const override { return "pump_prism"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        const uint32_t k = index - kPumpNumStates;
        state.key = k == 0 ? "file_a" : "file_b";
        state.label = k == 0 ? "Sound A" : "Sound B";
        state.defaultValue = "";
        state.hints = kStateIsFilenamePath;
    }
    void setState(const char* key, const char* value) override
    {
        const int o = std::strcmp(key, "file_a") == 0 ? 0 : (std::strcmp(key, "file_b") == 0 ? 1 : -1);
        if (o < 0) { PumpPlugin::setState(key, value); return; }
        path[o] = value ? value : "";
        std::shared_ptr<Analysis> a;
        std::string info;
        if (!path[o].empty())
        {
            const pump::WavData w = pump::readWav(path[o], (size_t)(48000.0 * 30.0));
            if (!w.ok()) info = "couldn't load: " + w.error;
            else
            {
                // mono, at most 20 seconds
                const size_t n = std::min(w.channels[0].size(), (size_t)(w.sampleRate * 20.0));
                std::vector<float> mono(n, 0.0f);
                for (const auto& c : w.channels) for (size_t i = 0; i < n; ++i) mono[i] += c[i] / (float)w.channels.size();
                std::string nm = path[o];
                const size_t sl = nm.find_last_of("/\\"); if (sl != std::string::npos) nm = nm.substr(sl + 1);
                a = analyse(mono, w.sampleRate, nm);
                info = nm;
            }
        }
        std::lock_guard<std::mutex> g(lock);
        if (a) keep.push_back(a);
        file[o] = a.get();
        (o ? infoB : infoA) = info;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "file_a") == 0) return String(path[0].c_str());
        if (std::strcmp(key, "file_b") == 0) return String(path[1].c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.morph = pv(kMorph) * 0.01; s.mode = (int)pv(kMode); s.speed = pv(kSpeed) * 0.01; s.start = pv(kStart) * 0.01; s.end = pv(kEnd) * 0.01;
        s.scan = pv(kScan) * 0.01; s.follow = pv(kFollow) * 0.01; s.partials = (int)pv(kPartials); s.tilt = pv(kTilt) * 0.01; s.oddEven = pv(kOddEven) * 0.01;
        s.inharm = pv(kInharm) * 0.01; s.own = pv(kTrue) * 0.01; s.formant = pv(kFormant) * 0.01; s.blur = pv(kBlur) * 0.01; s.noise = pv(kNoise) * 0.01;
        s.width = pv(kWidth) * 0.01; s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide); s.octave = (int)pv(kOctave);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    // the sounds for the current SOURCE settings (built-ins become ready a moment after the first PRISM opens)
    void pickSources()
    {
        const int a = (int)pv(kSourceA), b = (int)pv(kSourceB);
        const Analysis* A = a == 0 ? file[0].load(std::memory_order_acquire) : Builtins::get().source(a);
        const Analysis* B = b == 0 ? nullptr : (b == 1 ? file[1].load(std::memory_order_acquire) : Builtins::get().source(b - 1));
        nowA.store(A, std::memory_order_release); nowB.store(B, std::memory_order_release);
        engine.setSources(A, B);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        pickSources();
        if (!testNotes.empty() && (testClock += frames) > (uint64_t)(1.5 * getSampleRate()))
        {
            for (const char* p = testNotes.c_str(); *p; ) { const int v = std::atoi(p); if (v > 0 && v < 128) keys.push((uint8_t)v, 100); while (*p && *p != ',') ++p; if (*p) ++p; }
            testNotes.clear();
        }
        keys.drain([&](uint8_t note, uint8_t vel) { engine.uiNote(note, vel); });
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            const uint32_t n = std::min<uint32_t>(frames - done, 256u);
            int nm = 0;
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
    pump::synth::Midi midi[512] = {};
    std::string path[2], infoA, infoB, testNotes;
    std::atomic<const Analysis*> file[2] { { nullptr }, { nullptr } };
    std::atomic<const Analysis*> nowA { nullptr }, nowB { nullptr };
    std::vector<std::shared_ptr<Analysis>> keep;
    mutable std::mutex lock;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPrism)
};

END_NAMESPACE_DISTRHO
