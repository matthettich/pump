// pump: DPF plugin base. Turns a ParamSpec table into host parameters, exposes the factory presets as host
// programs and keeps the UI's A/B and theme state and the session version.
// Subclasses prepare their engines in activate(). They don't override sampleRateChanged(): DPF deactivates, changes
// the rate and activates again around a rate change of an active plugin, and an inactive one is prepared when it is
// activated (preparing there too only did the work twice).
#pragma once

#include "DistrhoPlugin.hpp"
#include "PumpParams.hpp"
#include "dsp/Util.hpp"
#include "dsp/Tempo.hpp"
#include <atomic>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using pump::ParamSpec;
using pump::Preset;

// State keys of every plugin: the UI's (saved with the session, never read by the DSP), then "v", the version of
// pump's session format, for migrating old sessions when something saved has to change meaning. A session saved
// before "v" existed reads as version 0.
static constexpr const char* kPumpStateKeys[] = { "theme", "abA", "abB", "abSel", "preset", "ui", "v" };
static constexpr uint32_t kPumpNumStates = sizeof(kPumpStateKeys) / sizeof(kPumpStateKeys[0]);
static constexpr uint32_t kPumpStateVersion = kPumpNumStates - 1;
static constexpr int kPumpSessionVersion = 1;   // bump when a migration is added (and handle the older ones)

class PumpPlugin : public Plugin {
public:
    // extraStates: states of the plugin's own (TUNE's notes), declared by the subclass's initState / setState /
    // getState for indices from kPumpNumStates on
    PumpPlugin(const ParamSpec* specs, uint32_t numParams, const std::vector<Preset>* presets, uint32_t extraStates = 0)
        // Presets are NOT exposed as host programs: DPF's VST3 wrapper loads a program whenever the host sets its
        // hidden program parameter, and Ableton Live re-sends it around gestures, which reset everything to the
        // first preset. The plugins' own preset menu applies presets as ordinary parameter changes instead.
       #if DISTRHO_PLUGIN_WANT_PROGRAMS
        : Plugin(numParams, presets ? (uint32_t)presets->size() : 0, kPumpNumStates + extraStates),
       #else
        : Plugin(numParams, 0, kPumpNumStates + extraStates),
       #endif
          fSpecs(specs), fNumParams(numParams), fPresets(presets),
          fValues(numParams), fChanged(numParams)
    {
        for (uint32_t i = 0; i < numParams; ++i)
        {
            fValues[i].store(specs[i].def, std::memory_order_relaxed);
            fChanged[i].store(true, std::memory_order_relaxed);
        }
        for (uint32_t i = 0; i < kPumpNumStates; ++i) fStates[i] = "";
    }

    const ParamSpec* specs() const { return fSpecs; }
    uint32_t numParams() const { return fNumParams; }

protected:
    const char* getMaker() const override { return DISTRHO_PLUGIN_BRAND; }   // the host's folder: wake mastering / effects / instruments
    const char* getHomePage() const override { return "https://github.com/matthettich/pump"; }
    // Licence string the host shows. pump's own code is (c) wake, all rights reserved (see /LICENSE). A plugin that
    // is under different terms MUST override this: pump MACHINE does ("GPL-3.0-only", see plugins/machine/NOTICE.md),
    // because it is built on GPL firmware. tools/check-licence-boundary.sh keeps that GPL code out of every other plugin.
    const char* getLicense() const override { return "All rights reserved (c) wake"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const ParamSpec& s = fSpecs[index];
        p.name = s.name;
        p.shortName = s.shortName;
        p.symbol = s.symbol;
        p.ranges.min = s.min;
        p.ranges.max = s.max;
        p.ranges.def = s.def;
        p.hints = 0;
        if (s.flags & pump::kPAutomatable) p.hints |= kParameterIsAutomatable;
        if (s.flags & pump::kPInteger) p.hints |= kParameterIsInteger;
        if (s.flags & pump::kPBoolean) p.hints |= kParameterIsBoolean | kParameterIsInteger;
        if (s.flags & pump::kPOutput) p.hints = kParameterIsOutput;
        if (s.flags & pump::kPHidden) p.hints |= kParameterIsHidden;
        if (s.scale == pump::PScale::Log && s.min > 0.0f) p.hints |= kParameterIsLogarithmic;
        switch (s.unit)
        {
        case pump::PUnit::dB: p.unit = "dB"; break;
        case pump::PUnit::Hz: p.unit = "Hz"; break;
        case pump::PUnit::ms: p.unit = "ms"; break;
        case pump::PUnit::Percent: p.unit = "%"; break;
        case pump::PUnit::LU: p.unit = "LU"; break;
        case pump::PUnit::Octave: p.unit = "oct"; break;
        case pump::PUnit::Seconds: p.unit = "s"; break;
        default: break;
        }
        if (s.choices)
        {
            const int n = s.numChoices();
            ParameterEnumerationValue* const ev = new ParameterEnumerationValue[n];
            for (int k = 0; k < n; ++k) { ev[k].value = s.min + (float)k; ev[k].label = s.choices[k]; }
            p.enumValues.count = (uint8_t)n;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = ev;
            p.hints |= kParameterIsInteger;
        }
        if (s.flags & pump::kPBypass)
            p.initDesignation(kParameterDesignationBypass);
    }

    float getParameterValue(uint32_t index) const override { return fValues[index].load(std::memory_order_relaxed); }

    void setParameterValue(uint32_t index, float value) override
    {
        if (fSpecs[index].flags & pump::kPOutput) return;
        fValues[index].store(fSpecs[index].clamp(value), std::memory_order_relaxed);
        fChanged[index].store(true, std::memory_order_release);
        fAnyChanged.store(true, std::memory_order_release);
    }

   #if DISTRHO_PLUGIN_WANT_PROGRAMS
    void initProgramName(uint32_t index, String& name) override
    {
        if (fPresets && index < fPresets->size()) name = (*fPresets)[index].name;
    }

    void loadProgram(uint32_t index) override
    {
        if (!fPresets || index >= fPresets->size()) return;
        for (uint32_t i = 0; i < fNumParams; ++i)
            if (!(fSpecs[i].flags & (pump::kPOutput | pump::kPBypass))) setParameterValue(i, fSpecs[i].def);
        for (const auto& pv : (*fPresets)[index].values)
            for (uint32_t i = 0; i < fNumParams; ++i)
                if (std::strcmp(fSpecs[i].symbol, pv.symbol) == 0) { setParameterValue(i, pv.value); break; }
    }
   #endif

    void initState(uint32_t index, State& state) override
    {
        state.key = kPumpStateKeys[index];
        state.defaultValue = "";
        state.label = kPumpStateKeys[index];
        state.hints = kStateIsOnlyForUI;
        if (index == kPumpStateVersion) { state.label = "Session version"; state.defaultValue = "0"; state.hints = 0; }
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "v") == 0) { fSessionVersion = value ? std::atoi(value) : 0; return; }
        for (uint32_t i = 0; i < kPumpNumStates; ++i)
            if (std::strcmp(key, kPumpStateKeys[i]) == 0) fStates[i] = value;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "v") == 0) return String(kPumpSessionVersion);
        for (uint32_t i = 0; i < kPumpNumStates; ++i)
            if (std::strcmp(key, kPumpStateKeys[i]) == 0) return String(fStates[i].c_str());
        return String();
    }

    // The session format the loaded session was saved in (0: none loaded: a session from before versions, or a
    // fresh instance at its defaults; kPumpSessionVersion: this one).
    int sessionVersion() const { return fSessionVersion; }

    // For subclasses: the current value of a parameter (any thread).
    float pv(uint32_t i) const { return fValues[i].load(std::memory_order_relaxed); }
    // On the audio thread: true once if anything changed since the last call.
    bool takeAnyChanged() { return fAnyChanged.exchange(false, std::memory_order_acq_rel); }
    bool takeChanged(uint32_t i) { return fChanged[i].exchange(false, std::memory_order_acq_rel); }
    // Output (meter) parameters
    void setOutput(uint32_t i, float v) { fValues[i].store(v, std::memory_order_relaxed); }

   #if DISTRHO_PLUGIN_WANT_TIMEPOS
    // On the audio thread, in run(): the host transport for this block (tempo, position in quarter notes, playing).
    // Hosts that send no tempo read as 120 BPM; no musical position leaves `valid` false.
    pump::Transport transport() const
    {
        const TimePosition& t = getTimePosition();
        pump::Transport tr;
        tr.playing = t.playing;
        if (t.bbt.beatsPerMinute > 1.0 && std::isfinite(t.bbt.beatsPerMinute)) tr.bpm = t.bbt.beatsPerMinute;
        if (t.bbt.valid && t.bbt.ticksPerBeat > 0.0 && t.bbt.beatType > 0.0f && t.bbt.beatsPerBar > 0.0f)
        {
            const double beats = (double)(t.bbt.bar - 1) * t.bbt.beatsPerBar + (double)(t.bbt.beat - 1) + t.bbt.tick / t.bbt.ticksPerBeat;
            tr.ppq = beats * 4.0 / t.bbt.beatType;
            tr.barQuarters = t.bbt.beatsPerBar * 4.0 / t.bbt.beatType;
            tr.valid = std::isfinite(tr.ppq);
        }
        return tr;
    }
   #endif

    const ParamSpec* fSpecs;
    uint32_t fNumParams;
    const std::vector<Preset>* fPresets;
    std::vector<std::atomic<float>> fValues;
    std::vector<std::atomic<bool>> fChanged;
    std::atomic<bool> fAnyChanged { true };
    std::string fStates[kPumpNumStates];
    int fSessionVersion = 0;   // 0: no version was loaded (a session from before versions, or a fresh instance)
};

END_NAMESPACE_DISTRHO
