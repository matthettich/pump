// pump TUNE: plugin (DSP side). Declared in a header so the UI can read the engine's feeds directly.
// Two states of its own besides the shared ones: "notes" (the graph's notes and their edits; the DSP plays them) and
// "curve" (the captured pitch, for the UI only). Both are saved with the session.
#pragma once
#include "PumpPlugin.hpp"
#include "TuneParams.hpp"
#include "TuneEngine.hpp"
#include "TuneNotes.hpp"
#include "dsp/Bypass.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tune;

class PluginTune : public PumpPlugin {
public:
    PluginTune() : PumpPlugin(params().data(), kNumParams, &presets(), 2)
    {
        prepareAll();
    }

    Engine engine;
    int latencyNow() const { return engine.latency(); }

protected:
    const char* getLabel() const override { return "pump_tune"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        const bool notes = index == kPumpNumStates;
        state.key = notes ? "notes" : "curve";
        state.defaultValue = "";
        state.label = notes ? "Notes" : "Captured pitch";
        state.hints = notes ? 0 : kStateIsOnlyForUI;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "notes") == 0)
        {
            notesText = value ? value : "";
            auto list = std::make_shared<NoteList>();
            list->notes = notesFromText(notesText.c_str());
            engine.setNotes(std::move(list));
            return;
        }
        if (std::strcmp(key, "curve") == 0) { curveText = value ? value : ""; return; }
        PumpPlugin::setState(key, value);
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "notes") == 0) return String(notesText.c_str());
        if (std::strcmp(key, "curve") == 0) return String(curveText.c_str());
        return PumpPlugin::getState(key);
    }

    void activate() override { prepareAll(); }
    void sampleRateChanged(double) override { prepareAll(); }

    void prepareAll()
    {
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        engine.setSettings(settings());
        engine.prepare(sr, (int)getBufferSize());
        bypass.prepare(sr, Engine::latencyFor(sr, kRangeLow) + 8, kBlock);
        lastLatency = engine.latency();
        bypass.setLatency(lastLatency);
        setLatency((uint32_t)lastLatency);
    }

    Settings settings() const
    {
        Settings s;
        s.mode = (int)pv(kMode); s.key = (int)pv(kKey); s.scale = (int)pv(kScale); s.midi = (int)pv(kMidi); s.range = (int)pv(kRange);
        s.retuneMs = pv(kRetune); s.amount = pv(kAmount); s.humanize = pv(kHumanize); s.flexCents = pv(kFlex);
        s.vibDepth = pv(kVibDepth); s.vibRate = pv(kVibRate); s.transpose = (int)pv(kTranspose);
        s.formant = pv(kFormant); s.formantFollow = pv(kFormantFollow) >= 0.5f; s.harmMode = (int)pv(kHarmMode);
        for (int h = 0; h < kVoices; ++h)
        {
            s.interval[h] = (int)pv(kH1 + h); s.level[h] = pv(kH1Level + h); s.pan[h] = pv(kH1Pan + h);
        }
        s.harmFormant = pv(kHarmFormant); s.harmHumanize = pv(kHarmHumanize);
        s.lead = pv(kLead); s.harmLevelDb = pv(kHarmLevel); s.outDb = pv(kOutGain);
        return s;
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            engine.setSettings(settings());
        }
        const TimePosition& tp = getTimePosition();
        const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        engine.setTimeline(tp.playing, (double)tp.frame / sr);
        const bool bypassed = pv(kBypass) >= 0.5f;
        uint32_t ev = 0, done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            for (int i = 0; i < n; ++i) { bufL[i] = inputs[0][done + i]; bufR[i] = inputs[1][done + i]; }
            int nm = 0;
            while (ev < midiEventCount && midiEvents[ev].frame < done + (uint32_t)n)
            {
                const MidiEvent& m = midiEvents[ev++];
                if (m.size < 1 || m.size > 3 || nm >= kMaxMidi) continue;
                midi[nm++] = { m.frame >= done ? m.frame - done : 0u, m.data[0], m.size > 1 ? m.data[1] : (uint8_t)0, m.size > 2 ? m.data[2] : (uint8_t)0 };
            }
            bypass.capture(bufL, bufR, n);
            engine.process(bufL, bufR, bufL, bufR, n, midi, nm);
            const int lat = engine.latency();
            if (lat != lastLatency) { bypass.setLatency(lat); lastLatency = lat; setLatency((uint32_t)lat); }
            bypass.apply(bufL, bufR, n, bypassed);
            std::memcpy(outputs[0] + done, bufL, sizeof(float) * (size_t)n);
            std::memcpy(outputs[1] + done, bufR, sizeof(float) * (size_t)n);
            done += (uint32_t)n;
        }
    }

private:
    static constexpr int kBlock = 256, kMaxMidi = 512;
    pump::BypassMixer bypass;
    float bufL[kBlock] = {}, bufR[kBlock] = {};
    MidiMsg midi[kMaxMidi] = {};
    int lastLatency = -1;
    std::string notesText, curveText;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginTune)
};

END_NAMESPACE_DISTRHO
