// One pump plugin, built from its own Plugin<Name>.hpp (wasm/shim stands in for DPF), behind a small interface that
// wasm/pump_wasm.cpp hosts. Each wasm/fx/<name>.cpp is its own translation unit (the plugins' `using namespace` lines
// would clash in one) and ends with PUMP_RUNNER(name, PluginName), which defines `PumpRunner* pumpfx_new_<name>(sr)`.
#pragma once
#include <cstdint>

struct PumpRunnerMidi { uint32_t frame; uint8_t status, d1, d2; };

struct PumpRunner {
    virtual ~PumpRunner() {}
    virtual void setParam(uint32_t index, float value) = 0;   // a host's setParameterValue
    virtual void activate() = 0;
    // inputs: L, R, then (plugins with four inputs) the sidechain L, R; outputs: L, R
    virtual void run(const float** inputs, float** outputs, uint32_t frames, const PumpRunnerMidi* ev, uint32_t nev) = 0;
    virtual int latency() const = 0;   // what the plugin last reported
    // the host transport for the next run(): tempo, the song position in quarter notes, playing
    virtual void transport(double bpm, double ppq, bool playing) = 0;
    virtual const char* getState(const char* key) = 0;   // valid until the next call
    virtual void setState(const char* key, const char* value) = 0;
};

#ifdef PUMP_SHIM_NS
#include <string>
#include <cmath>

namespace PUMP_SHIM_NS {
template <class P>
class RunnerOf final : public PumpRunner {
public:
    explicit RunnerOf(double sr) { shimRate() = sr; p = new P(); }
    ~RunnerOf() override { delete p; }
    void setParam(uint32_t i, float v) override { static_cast<Plugin*>(p)->setParameterValue(i, v); }
    void activate() override { static_cast<Plugin*>(p)->activate(); }
    void run(const float** in, float** out, uint32_t n, const PumpRunnerMidi* ev, uint32_t nev) override
    {
        uint32_t k = 0;
        for (uint32_t i = 0; i < nev && k < kMaxEvents; ++i)
        {
            MidiEvent& m = midi[k++];
            m.frame = ev[i].frame < n ? ev[i].frame : n - 1; m.size = (ev[i].status & 0xF0) == 0xC0 || (ev[i].status & 0xF0) == 0xD0 ? 2 : 3;
            m.data[0] = ev[i].status; m.data[1] = ev[i].d1 & 0x7F; m.data[2] = ev[i].d2 & 0x7F; m.data[3] = 0; m.dataExt = nullptr;
        }
        static_cast<Plugin*>(p)->run(in, out, n, midi, k);
        // the position moves on with the audio, as a host's does (the next transport() call sets it again)
        TimePosition& t = p->shimTime();
        t.frame += t.playing ? n : 0;
        if (t.playing && t.bbt.valid) setPos(ppq + (double)n * bpm / 60.0 / p->getSampleRate());
    }
    int latency() const override { return p->shimLatency(); }
    void transport(double b, double q, bool playing) override
    {
        bpm = b > 1.0 && std::isfinite(b) ? b : 120.0;
        TimePosition& t = p->shimTime();
        t.playing = playing;
        t.bbt.beatsPerMinute = bpm;
        t.bbt.valid = playing && std::isfinite(q);
        setPos(std::isfinite(q) ? q : 0.0);
        t.frame = (uint64_t)std::max(0.0, ppq * 60.0 / bpm * p->getSampleRate());
    }
    const char* getState(const char* key) override { st = static_cast<Plugin*>(p)->getState(key).c_str(); return st.c_str(); }
    void setState(const char* key, const char* value) override { static_cast<Plugin*>(p)->setState(key, value); }

private:
    void setPos(double q)
    {
        ppq = q < 0.0 ? 0.0 : q;
        TimePosition& t = p->shimTime();
        const double bar = std::floor(ppq / 4.0), inBar = ppq - bar * 4.0, beat = std::floor(inBar);
        t.bbt.bar = (int32_t)bar + 1; t.bbt.beat = (int32_t)beat + 1;
        t.bbt.tick = (inBar - beat) * t.bbt.ticksPerBeat; t.bbt.barStartTick = bar * 4.0 * t.bbt.ticksPerBeat;
        t.bbt.beatsPerBar = 4.0f; t.bbt.beatType = 4.0f;
    }
    static constexpr uint32_t kMaxEvents = 512;
    P* p = nullptr;
    MidiEvent midi[kMaxEvents];
    double bpm = 120.0, ppq = 0.0;
    std::string st;
};
} // namespace PUMP_SHIM_NS

#if defined(__wasm__)
 #define PUMP_RUNNER_API __attribute__((visibility("hidden")))
#else
 #define PUMP_RUNNER_API
#endif
#define PUMP_RUNNER(name, Cls) PUMP_RUNNER_API PumpRunner* pumpfx_new_##name(double sr) { return new PUMP_SHIM_NS::RunnerOf<PUMP_SHIM_NS::Cls>(sr); }
#endif
