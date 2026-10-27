// pump FREEZE: plugin (DSP side). Declared in a header so the UI can read its feeds, its sound's spectra and play its
// keyboard directly. A loaded sound is a state holding a file path ("file"); it is read and cut into spectra in
// setState (not the audio thread) and kept until the plugin goes (the audio thread may still read an old one).
#pragma once
#include "PumpPlugin.hpp"
#include "FreezeParams.hpp"
#include "FreezeEngine.hpp"
#include "FreezeSources.hpp"
#include "dsp/Wav.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::freeze;

class PluginFreeze : public PumpPlugin {
public:
    PluginFreeze() : PumpPlugin(params().data(), kNumParams, &presets(), 1)
    {
        Builtins::get().start();
        if (std::getenv("PUMP_TEST_NOTES") || std::getenv("PUMP_RENDER_DIR") || std::getenv("PUMP_WAIT_SOURCES")) Builtins::get().waitReady();
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate());
        if (const char* n = std::getenv("PUMP_TEST_NOTES")) testNotes = n;
    }

    // gain staging (the volume pass): VOLUME 0 dB is a typical voice at about -20 dBFS RMS
    static constexpr float kTrimDb = -8.0f;
    Engine engine;
    pump::SafetyLimiter outLimit;
    pump::synth::NoteQueue keys;
    pump::MeterValue outPeak[2];

    const Spectra* soundNow() const { return now.load(std::memory_order_acquire); }
    std::string fileInfo() const { std::lock_guard<std::mutex> g(lock); return info; }

protected:
    const char* getLabel() const override { return "pump_freeze"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        state.key = "file"; state.label = "Sound"; state.defaultValue = ""; state.hints = kStateIsFilenamePath;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "file") != 0) { PumpPlugin::setState(key, value); return; }
        path = value ? value : "";
        std::shared_ptr<Spectra> s;
        std::string inf;
        if (!path.empty())
        {
            const pump::WavData w = pump::readWav(path, (size_t)(48000.0 * 30.0));
            if (!w.ok()) inf = "couldn't load: " + w.error;
            else
            {
                const size_t n = std::min(w.channels[0].size(), (size_t)(w.sampleRate * 20.0));
                std::vector<float> mono(n, 0.0f);
                for (const auto& c : w.channels) for (size_t i = 0; i < n; ++i) mono[i] += c[i] / (float)w.channels.size();
                std::string nm = path;
                const size_t sl = nm.find_last_of("/\\"); if (sl != std::string::npos) nm = nm.substr(sl + 1);
                s = makeSpectra(mono, w.sampleRate, nm, (int)pv(kRoot));
                inf = nm;
            }
        }
        std::lock_guard<std::mutex> g(lock);
        if (s) keep.push_back(s);
        file.store(s.get(), std::memory_order_release);
        info = inf;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "file") == 0) return String(path.c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { sync(); engine.prepare(getSampleRate(), (int)getBufferSize()); outLimit.prepare(getSampleRate()); }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.position = pv(kPosition) * 0.01; s.speed = pv(kSpeed) * 0.01; s.spray = pv(kSpray) * 0.01; s.grains = (int)pv(kGrains); s.blur = pv(kBlur) * 0.01;
        s.root = (int)pv(kRoot); s.track = pv(kTrack) * 0.01; s.stretch = pv(kStretch) * 0.01; s.tilt = pv(kTilt) * 0.01; s.shimmer = pv(kShimmer) * 0.01;
        s.low = pv(kLow); s.high = pv(kHigh); s.phase = pv(kPhase) * 0.01; s.width = pv(kWidth) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide); s.octave = (int)pv(kOctave);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
        // a file's root is ROOT (its spectra keep the note they were cut with; ROOT is read again here)
        if (const Spectra* f = file.load(std::memory_order_acquire)) const_cast<Spectra*>(f)->root = s.root;
    }
    void pickSource()
    {
        const int a = (int)pv(kSource);
        const Spectra* S = a == 0 ? file.load(std::memory_order_acquire) : Builtins::get().source(a);
        now.store(S, std::memory_order_release);
        engine.setSource(S);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        pickSource();
        if (!testNotes.empty() && (testClock += frames) > (uint64_t)(0.3 * getSampleRate()))
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
    std::string path, info, testNotes;
    std::atomic<const Spectra*> file { nullptr }, now { nullptr };
    std::vector<std::shared_ptr<Spectra>> keep;
    mutable std::mutex lock;
    uint64_t testClock = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginFreeze)
};

END_NAMESPACE_DISTRHO
