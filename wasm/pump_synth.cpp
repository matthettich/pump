// pump's instruments in WebAssembly: MONO, POLY and COIL behind one C interface, for Thunder Plus's synth engines.
//
// Each Synth<Name> below is its plugin's DSP side (plugins/<name>/Plugin<Name>.hpp) without DPF: the same sync() from
// the parameter values to the engine's Settings, the same prepare / activate, and the same 256-sample chunks with the
// MIDI messages that fall in each. Keep these in step with the plugins: when a Plugin<Name>.hpp's sync() or run()
// changes, change its Synth here the same way (docs/DEVELOPING.md, "WebAssembly").
//
// Thunder Plus renders a note at a time today (the note's audio becomes the pad's sound, re-pitched for the others),
// but nothing here assumes that: an instance plays any MIDI it is given, so the same build can run live in a worklet.
//
// Build: wasm/build.sh builds this into pump-synth.wasm next to pump.wasm (no threads: -DPUMP_SYNC_KERNELS=1).
//
// Interface (all exported, "C"):
//   psyn_types()                         number of instrument types (3); ids are kTypeNames' order: mono, poly, coil
//   psyn_nparams(type)                   parameter count (parameter i is params()[i] of that plugin)
//   psyn_stage()                         float[512]: write a new instance's values here before psyn_new (values not
//                                        written are the parameter's default: psyn_new clears the stage after use)
//   psyn_new(type, sr)                   an instance (an opaque handle >= 0, wasm/Handles.hpp) with the staged
//                                        values, prepared; -1 on failure. A freed instance's handle is refused
//   psyn_free(h)
//   psyn_values(h)                       float[nparams]: the instance's requested values; write, then psyn_commit(h)
//   psyn_commit(h)                       applies the values that changed (clamped as a host's setParameterValue)
//   psyn_midi(h, frame, status, d1, d2)  queues a MIDI message for the next psyn_process, at sample `frame` of it
//                                        (at most 512 a call; later ones are dropped)
//   psyn_out(ch)                         float[4096] output buffers: 0 = L, 1 = R
//   psyn_process(h, n)                   n <= 4096 samples into psyn_out(0/1), playing the queued messages
//   psyn_latency(h)                      the latency the plugin reports (samples): MONO's half-band decimator
#include <limits>
#include "Handles.hpp"
#include "../plugins/mono/MonoEngine.hpp"
#include "../plugins/poly/PolyEngine.hpp"
#include "../plugins/coil/CoilEngine.hpp"
#include <algorithm>
#include <memory>

#if defined(__wasm__)
 #define PUMP_API extern "C" __attribute__((visibility("default"), used))
#else
 #define PUMP_API extern "C"
#endif

namespace pumpsynth {

using pump::ParamSpec;
constexpr int kMaxBlock = 4096, kStage = 512, kMaxEvents = 512, kChunk = 256;

enum Type : int { kMono = 0, kPoly, kCoil, kNumTypes };

inline const std::vector<ParamSpec>& specsOf(int type)
{
    switch (type)
    {
    case kMono: return pump::mono::params();
    case kPoly: return pump::poly::params();
    default: return pump::coil::params();
    }
}

// PumpPlugin without DPF: values with change flags, as the plugins read them on the audio thread, and the MIDI queue.
class SynthBase {
public:
    SynthBase(int t, double rate) : type(t), specs(specsOf(t).data()), numParams((int)specsOf(t).size()), sr(rate)
    {
        values.resize((size_t)numParams); requested.resize((size_t)numParams);
        for (int i = 0; i < numParams; ++i) values[(size_t)i] = requested[(size_t)i] = specs[i].def;
    }
    virtual ~SynthBase() {}
    virtual void activate() = 0;
    virtual void sync() = 0;
    virtual void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) = 0;
    virtual int latency() const { return 0; }

    void commit()
    {
        for (int i = 0; i < numParams; ++i)
            if (requested[(size_t)i] != values[(size_t)i] || !(requested[(size_t)i] == requested[(size_t)i]))
            {
                if (specs[i].flags & pump::kPOutput) continue;
                values[(size_t)i] = requested[(size_t)i] = specs[i].clamp(requested[(size_t)i]);
                anyChanged = true;
            }
    }
    void queue(int frame, int status, int d1, int d2)
    {
        if (nq >= kMaxEvents) return;
        // a frame is within one call (< kMaxBlock): a wild one would wait forever and fill the queue, note-offs too
        frame = std::max(0, std::min(frame, kMaxBlock - 1));
        q[nq++] = { (uint32_t)frame, (uint8_t)status, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F) };
    }
    // The plugin's run(): settings first (when a value changed), then 256-sample chunks with the messages in each.
    void run(float* L, float* R, int frames)
    {
        const pump::DenormalGuard dg;
        if (anyChanged) { anyChanged = false; sync(); }
        std::stable_sort(q, q + nq, [](const pump::synth::Midi& a, const pump::synth::Midi& b) { return a.offset < b.offset; });
        int done = 0, ev = 0;
        while (done < frames)
        {
            const int n = std::min(frames - done, kChunk);
            int nm = 0;
            while (ev < nq && (int)q[ev].offset < done + n)
            {
                const pump::synth::Midi& m = q[ev++];
                chunk[nm++] = { m.offset >= (uint32_t)done ? m.offset - (uint32_t)done : 0u, m.status, m.d1, m.d2 };
            }
            render(L + done, R + done, n, chunk, nm);
            done += n;
        }
        // messages past the end (a frame >= frames) wait for the next call, at their frame less this call's length
        int k = 0;
        for (; ev < nq; ++ev) { pump::synth::Midi m = q[ev]; m.offset -= (uint32_t)frames; q[k++] = m; }
        nq = k;
    }

    const int type;
    const ParamSpec* specs;
    const int numParams;
    std::vector<float> requested;   // psyn_values(): written by the host, applied by commit()

protected:
    float pv(int i) const { return values[(size_t)i]; }
    double getSampleRate() const { return sr; }
    int getBufferSize() const { return kMaxBlock; }

    std::vector<float> values;
    bool anyChanged = true;
    double sr;
    pump::synth::Midi q[kMaxEvents] = {}, chunk[kMaxEvents] = {};
    int nq = 0;
};

// ---------------------------------------------------------------- MONO (PluginMono.hpp)
class SynthMono : public SynthBase {
public:
    SynthMono(double rate) : SynthBase(kMono, rate) {}
    pump::mono::Engine engine;
    void activate() override { sync(); engine.prepare(getSampleRate(), getBufferSize()); anyChanged = false; }
    int latency() const override { return engine.latency(); }
    void sync() override
    {
        using namespace pump::mono;
        Settings s;
        const int wv[3] = { kOsc1Wave, kOsc2Wave, kOsc3Wave }, oc[3] = { kOsc1Oct, kOsc2Oct, kOsc3Oct }, lv[3] = { kOsc1Level, kOsc2Level, kOsc3Level };
        for (int o = 0; o < 3; ++o) { s.wave[o] = (int)pv(wv[o]); s.oct[o] = (int)pv(oc[o]); s.level[o] = pv(lv[o]) * 0.01; }
        s.pitch[0] = 0.0; s.pitch[1] = pv(kOsc2Pitch); s.pitch[2] = pv(kOsc3Pitch);
        s.sync = pv(kSync) >= 0.5f; s.fm = pv(kFm) * 0.01; s.pw = pv(kPw) * 0.01; s.noise = pv(kNoise) * 0.01; s.overdriveDb = pv(kOverdrive);
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.keyTrack = pv(kKeyTrack) * 0.01; s.mode = pv(kFilterMode) * 0.01; s.velFilter = pv(kVelFilter) * 0.01;
        s.fA = pv(kFA) * 0.001; s.fD = pv(kFD) * 0.001; s.fS = pv(kFS) * 0.01; s.fR = pv(kFR) * 0.001;
        s.aA = pv(kAA) * 0.001; s.aD = pv(kAD) * 0.001; s.aS = pv(kAS) * 0.01; s.aR = pv(kAR) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoPitch = pv(kLfoPitch) * 0.01; s.lfoFilter = pv(kLfoFilter) * 0.01;
        s.lfoPw = pv(kLfoPw) * 0.01; s.wheelVib = pv(kWheelVib) * 0.01;
        s.glide = pv(kGlide); s.glideMode = (int)pv(kGlideMode); s.priority = (int)pv(kPriority); s.legato = pv(kLegato) >= 0.5f;
        s.drift = pv(kDrift) * 0.01; s.bendRange = pv(kBendRange); s.volumeDb = pv(kVolume);
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); }
};

// ---------------------------------------------------------------- POLY (PluginPoly.hpp)
class SynthPoly : public SynthBase {
public:
    SynthPoly(double rate) : SynthBase(kPoly, rate) {}
    pump::poly::Engine engine;
    void activate() override { sync(); engine.prepare(getSampleRate(), getBufferSize()); anyChanged = false; }
    void sync() override
    {
        using namespace pump::poly;
        Settings s;
        s.aSaw = pv(kASaw) >= 0.5f; s.aPulse = pv(kAPulse) >= 0.5f; s.aFreq = pv(kAFreq); s.aPw = pv(kAPw) * 0.01; s.sync = pv(kSync) >= 0.5f;
        s.bSaw = pv(kBSaw) >= 0.5f; s.bTri = pv(kBTri) >= 0.5f; s.bPulse = pv(kBPulse) >= 0.5f; s.bFreq = pv(kBFreq); s.bFine = pv(kBFine);
        s.bPw = pv(kBPw) * 0.01; s.bLow = pv(kBLow) >= 0.5f;
        s.levelA = pv(kLevelA) * 0.01; s.levelB = pv(kLevelB) * 0.01; s.noise = pv(kNoise) * 0.01; s.sub = pv(kSub) * 0.01;
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.keyTrack = pv(kKeyTrack) * 0.01; s.hpf = pv(kHpf); s.velFilter = pv(kVelFilter) * 0.01;
        s.fA = pv(kFA) * 0.001; s.fD = pv(kFD) * 0.001; s.fS = pv(kFS) * 0.01; s.fR = pv(kFR) * 0.001;
        s.aA = pv(kAA) * 0.001; s.aD = pv(kAD) * 0.001; s.aS = pv(kAS) * 0.01; s.aR = pv(kAR) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.pmEnv = pv(kPmEnv) * 0.01; s.pmOscB = pv(kPmOscB) * 0.01; s.pmFreqA = pv(kPmFreqA) >= 0.5f; s.pmPwA = pv(kPmPwA) >= 0.5f; s.pmFilter = pv(kPmFilter) >= 0.5f;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoDelay = pv(kLfoDelay); s.lfoPitch = pv(kLfoPitch) * 0.01;
        s.lfoPw = pv(kLfoPw) * 0.01; s.lfoFilter = pv(kLfoFilter) * 0.01; s.wheelMod = pv(kWheelMod) * 0.01;
        s.voiceMode = (int)pv(kVoiceMode); s.voices = (int)pv(kVoices); s.unisonDetune = pv(kUnisonDetune) * 0.01; s.glide = pv(kGlide);
        s.slop = pv(kSlop) * 0.01; s.bendRange = pv(kBendRange); s.chorus = (int)pv(kChorus); s.volumeDb = pv(kVolume);
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); }
};

// ---------------------------------------------------------------- COIL (PluginCoil.hpp; no KICK key, no test notes)
class SynthCoil : public SynthBase {
public:
    SynthCoil(double rate) : SynthBase(kCoil, rate) {}
    pump::coil::Engine engine;
    void activate() override { sync(); engine.prepare(getSampleRate(), getBufferSize()); anyChanged = false; }
    void sync() override
    {
        using namespace pump::coil;
        Settings s;
        s.model = (int)pv(kModel); s.tension = pv(kTension); s.drip = pv(kDrip); s.decay = pv(kDecay); s.release = pv(kRelease);
        s.harmonic = pv(kHarmonic) * 0.01; s.spread = pv(kSpread) * 0.01; s.wobble = pv(kWobble) * 0.01; s.tone = pv(kTone) * 0.01;
        s.excite = pv(kExcite) * 0.01; s.tank = pv(kTank) * 0.01; s.grit = pv(kGrit); s.width = pv(kWidth) * 0.01;
        s.octave = (int)pv(kOctave); s.bend = pv(kBend); s.velocity = pv(kVelocity) * 0.01; s.volumeDb = pv(kVolume);
        s.voices = (int)pv(kVoices);
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(nullptr, nullptr, l, r, n, ev, nev); }
};

inline SynthBase* make(int type, double sr)
{
    switch (type)
    {
    case kMono: return new SynthMono(sr);
    case kPoly: return new SynthPoly(sr);
    case kCoil: return new SynthCoil(sr);
    default: return nullptr;
    }
}

pumpwasm_handles::HandleTable<SynthBase> inst;
float stage[kStage];
const bool stageCleared = [] { for (float& v : stage) v = std::numeric_limits<float>::quiet_NaN(); return true; }();   // unwritten = default
float out[2][kMaxBlock];

inline SynthBase* at(int h) { return inst.at(h); }

} // namespace pumpsynth

using namespace pumpsynth;

PUMP_API int psyn_types() { return kNumTypes; }
PUMP_API int psyn_nparams(int type) { return type >= 0 && type < kNumTypes ? (int)specsOf(type).size() : 0; }
PUMP_API float* psyn_stage() { return stage; }
PUMP_API int psyn_new(int type, double sr)
{
    if (type < 0 || type >= kNumTypes || !(sr > 1000.0 && sr <= 768000.0)) return -1;
    SynthBase* s = make(type, sr);
    if (!s) return -1;
    // a host restores the state, then activates
    for (int i = 0; i < s->numParams && i < kStage; ++i) if (stage[i] == stage[i]) s->requested[(size_t)i] = stage[i];
    for (float& v : stage) v = std::numeric_limits<float>::quiet_NaN();   // unwritten = default, for the next one too
    s->commit();
    s->activate();
    return inst.add(s);
}
PUMP_API void psyn_free(int h) { inst.remove(h); }
PUMP_API float* psyn_values(int h) { SynthBase* s = at(h); return s ? s->requested.data() : nullptr; }
PUMP_API void psyn_commit(int h) { if (SynthBase* s = at(h)) s->commit(); }
PUMP_API void psyn_midi(int h, int frame, int status, int d1, int d2) { if (SynthBase* s = at(h)) s->queue(frame, status, d1, d2); }
PUMP_API float* psyn_out(int ch) { return out[ch ? 1 : 0]; }
PUMP_API void psyn_process(int h, int n)
{
    SynthBase* s = at(h);
    if (!s || n <= 0) return;
    s->run(out[0], out[1], n > kMaxBlock ? kMaxBlock : n);
}
PUMP_API int psyn_latency(int h) { SynthBase* s = at(h); return s ? s->latency() : 0; }
