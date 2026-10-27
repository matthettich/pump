// pump STUTTER: plugin (DSP side). Declared in a header so the UI can read its feeds and hold its pads directly.
#pragma once
#include "PumpPlugin.hpp"
#include "StutterParams.hpp"
#include "StutterEngine.hpp"
#include "dsp/Bypass.hpp"
#include <mutex>

START_NAMESPACE_DISTRHO

using namespace pump::stutter;

class PluginStutter : public PumpPlugin {
public:
    PluginStutter() : PumpPlugin(params().data(), kNumParams, &presets(), 1)
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        setLatency(0); bypass.setLatency(0);
    }

    Engine engine;
    pump::MeterValue outPeak[2];
    // the pad the window holds (-1 = none); a momentary state, never a parameter (not saved, not automated)
    std::atomic<int> padHeld { -1 };
    int latencyNow() const { return 0; }
    // the USER pattern as the window last set it (the window edits a copy and sends it back as text)
    Pattern userPattern() const { std::lock_guard<std::mutex> g(patLock); return userPat; }

protected:
    const char* getLabel() const override { return "pump_stutter"; }
    const char* getDescription() const override { return PUMP_PLUGIN_ABOUT; }

    void initState(uint32_t index, State& state) override
    {
        if (index < kPumpNumStates) { PumpPlugin::initState(index, state); return; }
        state.key = "pattern"; state.label = "User Pattern"; state.defaultValue = ""; state.hints = 0;
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "pattern") != 0) { PumpPlugin::setState(key, value); return; }
        const Pattern p = patternFromText(value);
        std::lock_guard<std::mutex> g(patLock);
        userPat = p; patText = value ? value : ""; patDirty = true;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "pattern") == 0) { std::lock_guard<std::mutex> g(patLock); return String(patText.c_str()); }
        return PumpPlugin::getState(key);
    }

    void activate() override
    {
        sync();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        nHeld = 0;
    }
    void sampleRateChanged(double) override { activate(); }

    void sync()
    {
        Settings s;
        s.trigger = (int)pv(kTrigger);
        s.interval = (int)pv(kInterval);
        s.chance = pv(kChance);
        s.length = (int)pv(kLength);
        s.grid = (int)pv(kGrid);
        s.gate = pv(kGate);
        s.pitch = pv(kPitch);
        s.decay = pv(kDecay);
        s.filter = pv(kFilter);
        s.variation = pv(kVariation);
        s.tape = pv(kTape);
        s.mix = pv(kMix);
        s.outDb = pv(kOutGain);
        for (int m = 0; m < kNumMoves; ++m) s.use[m] = pv((uint32_t)(kUse0 + m)) >= 0.5f;
        s.patternOn = pv(kPatternOn) >= 0.5f; s.patternSel = (int)pv(kPatternSel); s.patStep = (int)pv(kPatStep);
        engine.setSettings(s);
    }

    // MIDI notes: C, C#, D, D#, E, F, F#, G of any octave = the eight moves; the last one pressed and still held plays
    void midi(const MidiEvent& m)
    {
        if (m.size < 3) return;
        const uint8_t st = m.data[0] & 0xF0, note = m.data[1], vel = m.data[2];
        const int move = note % 12;
        if (st == 0x90 && vel > 0 && move < kNumMoves)
        {
            removeHeld(move);
            if (nHeld < 16) held[nHeld++] = move;
        }
        else if ((st == 0x80 || (st == 0x90 && vel == 0)) && move < kNumMoves) removeHeld(move);
        else if (st == 0xB0 && (note == 123 || note == 120)) nHeld = 0;   // all notes / sound off
    }
    void removeHeld(int move)
    {
        int k = 0;
        for (int i = 0; i < nHeld; ++i) if (held[i] != move) held[k++] = held[i];
        nHeld = k;
    }
    int heldMove() const
    {
        const int p = padHeld.load(std::memory_order_relaxed);
        if (p >= 0) return p;
        return nHeld > 0 ? held[nHeld - 1] : -1;
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        engine.setTransport(transport());
        // a new USER pattern from the window (never wait for it on the audio thread)
        if (patDirty.load(std::memory_order_acquire)) { std::unique_lock<std::mutex> g(patLock, std::try_to_lock); if (g.owns_lock()) { engine.setUserPattern(userPat); patDirty = false; } }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        const bool bypassed = pv(kBypass) >= 0.5f;
        uint32_t done = 0, ev = 0;
        while (done < frames)
        {
            // split at MIDI events (a held move starts on its sample)
            while (ev < midiEventCount && midiEvents[ev].frame <= done) midi(midiEvents[ev++]);
            uint32_t end = std::min<uint32_t>(frames, done + 4096);
            if (ev < midiEventCount) end = std::min<uint32_t>(end, std::max<uint32_t>(midiEvents[ev].frame, done + 1));
            const int n = (int)(end - done);
            float* l = L + done;
            float* r = R + done;
            engine.setHeld(heldMove());
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, bypassed);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done = end;
        }
        while (ev < midiEventCount) midi(midiEvents[ev++]);
    }

private:
    pump::BypassMixer bypass;
    int held[16] = {}, nHeld = 0;
    Pattern userPat;
    std::string patText;
    std::atomic<bool> patDirty { false };
    mutable std::mutex patLock;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginStutter)
};

END_NAMESPACE_DISTRHO
