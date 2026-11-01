// pump's instruments in WebAssembly: MONO, POLY, COIL, ORBIT, FOLD, GLOSS, TRACT, STRIKE, BOING, X0X, LIFE, FREEZE and WOB behind one C interface, for Thunder Plus's synth engines.
//
// Each Synth<Name> below runs its plugin's own DSP side, plugins/<name>/<Name>Dsp.hpp (the settings from the
// parameters, the output trim, the engine and the safety limiter: the code the plugin runs, not a copy), on SynthBase,
// which does what the plugin's run() does around it: 256-sample chunks with the MIDI messages that fall in each.
// tests/parity_synth.cpp checks each against its plugin.
//
// Thunder Plus renders a note at a time today (the note's audio becomes the pad's sound, re-pitched for the others),
// but nothing here assumes that: an instance plays any MIDI it is given, so the same build can run live in a worklet.
//
// Build: wasm/build.sh builds this into pump-synth.wasm next to pump.wasm (no threads: -DPUMP_SYNC_KERNELS=1).
//
// Interface (all exported, "C"):
//   psyn_types()                         number of instrument types (13); ids are the Type enum's order: mono, poly, coil, orbit, fold, gloss, tract, strike, boing, x0x, life, freeze, wob
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
//   psyn_tempo(h, bpm)                   the host's tempo (WOB's synced wobble; 120 until set)
#include <limits>
#include <mutex>
#if defined(__wasm__) && !defined(__wasm_threads__)
// wasi has no threads, so no std::mutex: SING's engine (PRISM's sources render with it, for FREEZE) keeps one for its
// lyrics, set from another thread in the plugin. Nothing here is on another thread: a lock that does nothing.
namespace std {
struct mutex { void lock() {} void unlock() {} bool try_lock() { return true; } };
}
#endif
#include "Handles.hpp"
#include "../plugins/mono/MonoDsp.hpp"
#include "../plugins/poly/PolyDsp.hpp"
#include "../plugins/coil/CoilDsp.hpp"
#include "../plugins/orbit/OrbitParams.hpp"
#include "../plugins/orbit/OrbitEngine.hpp"
#include "../plugins/fold/FoldParams.hpp"
#include "../plugins/fold/FoldEngine.hpp"
#include "../plugins/gloss/GlossParams.hpp"
#include "../plugins/gloss/GlossEngine.hpp"
#include "../plugins/tract/TractParams.hpp"
#include "../plugins/tract/TractEngine.hpp"
#include "../plugins/strike/StrikeParams.hpp"
#include "../plugins/strike/StrikeEngine.hpp"
#include "../plugins/boing/BoingParams.hpp"
#include "../plugins/boing/BoingEngine.hpp"
#include "../plugins/x0x/X0xParams.hpp"
#include "../plugins/x0x/X0xEngine.hpp"
#include "../plugins/life/LifeParams.hpp"
#include "../plugins/life/LifeEngine.hpp"
#include "../plugins/freeze/FreezeParams.hpp"
#include "../plugins/freeze/FreezeEngine.hpp"
#include "../plugins/prism/PrismAudio.hpp"
#include "../plugins/wob/WobParams.hpp"
#include "../plugins/wob/WobEngine.hpp"
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

// (the newer types are kT<Name>: a plugin's own parameter names (FOLD's kFold) must not be hidden by these)
enum Type : int { kMono = 0, kPoly, kCoil, kOrbit, kTFold, kTGloss, kTTract, kTStrike, kTBoing, kTX0x, kTLife, kTFreeze, kTWob, kNumTypes };

inline const std::vector<ParamSpec>& specsOf(int type)
{
    switch (type)
    {
    case kMono: return pump::mono::params();
    case kPoly: return pump::poly::params();
    case kOrbit: return pump::orbit::params();
    case kTFold: return pump::fold::params();
    case kTGloss: return pump::gloss::params();
    case kTTract: return pump::tract::params();
    case kTStrike: return pump::strike::params();
    case kTBoing: return pump::boing::params();
    case kTX0x: return pump::x0x::params();
    case kTLife: return pump::life::params();
    case kTFreeze: return pump::freeze::params();
    case kTWob: return pump::wob::params();
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
    virtual void tempo(double) {}   // the host's tempo (WOB's wobble); the rest ignore it

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

// Each instrument below runs its plugin's own DSP side (plugins/<name>/<Name>Dsp.hpp: the settings, the trim, the
// engine and the output limiter), on SynthBase instead of PumpPlugin. tests/parity_synth.cpp checks each against its
// plugin.
// ---------------------------------------------------------------- MONO
class SynthMono : public pump::mono::MonoDsp<SynthBase> {
public:
    explicit SynthMono(double rate) : MonoDsp(kMono, rate) {}
    void activate() override { prepareDsp(); anyChanged = false; }
    int latency() const override { return engine.latency(); }
    void sync() override { syncSettings(); }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { renderChunk(l, r, n, ev, nev); }
};

// ---------------------------------------------------------------- POLY
class SynthPoly : public pump::poly::PolyDsp<SynthBase> {
public:
    explicit SynthPoly(double rate) : PolyDsp(kPoly, rate) {}
    void activate() override { prepareDsp(); anyChanged = false; }
    void sync() override { syncSettings(); }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { renderChunk(l, r, n, ev, nev); }
};

// ---------------------------------------------------------------- COIL (no KICK key, no test notes)
class SynthCoil : public pump::coil::CoilDsp<SynthBase> {
public:
    explicit SynthCoil(double rate) : CoilDsp(kCoil, rate) {}
    void activate() override { prepareDsp(); anyChanged = false; }
    void sync() override { syncSettings(); }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { renderChunk(l, r, n, ev, nev); }
};

// ---------------------------------------------------------------- ORBIT (no OrbitDsp split: PluginOrbit's sync() and
// the body of its run(), the engine then the safety limiter on each 256-sample chunk; keep in step with PluginOrbit.hpp)
class SynthOrbit : public SynthBase {
public:
    explicit SynthOrbit(double rate) : SynthBase(kOrbit, rate) {}
    static constexpr float kTrimDb = -4.0f;   // PluginOrbit::kTrimDb
    // the engine's prepare() starts its voices afresh (their envelopes too): the settings again after it, as the plugin's
    // first run() does (its parameters are still flagged as changed after activate())
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::orbit;
        Settings s;
        s.system = (int)pv(kSystem); s.chaos = pv(kChaos) * 0.01; s.sync = pv(kSync) * 0.01; s.axis = pv(kAxis) * 0.01; s.width = pv(kWidth) * 0.01;
        s.drive = pv(kDrive) * 0.01; s.cutoff = pv(kCutoff); s.reso = pv(kReso) * 0.01; s.keytrack = pv(kKeytrack) * 0.01;
        s.envCut = pv(kEnvCut) * 0.01; s.envChaos = pv(kEnvChaos) * 0.01;
        s.modA = pv(kModA) * 0.001; s.modD = pv(kModD) * 0.001; s.modS = pv(kModS) * 0.01; s.modR = pv(kModR) * 0.001;
        s.driftRate = pv(kDriftRate); s.driftPitch = pv(kDriftPitch) * 0.01; s.driftCut = pv(kDriftCut) * 0.01; s.driftChaos = pv(kDriftChaos) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide);
        s.octave = (int)pv(kOctave); s.fine = pv(kFine);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::orbit::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- FOLD (PluginFold.hpp's sync() and run() body: keep in step)
class SynthFold : public SynthBase {
public:
    explicit SynthFold(double rate) : SynthBase(kTFold, rate) {}
    static constexpr float kTrimDb = 2.0f;   // PluginFold::kTrimDb
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::fold;
        Settings s;
        s.shape = pv(kShape) * 0.01; s.modWave = (int)pv(kModWaveP); s.modRatio = pv(kModRatio); s.modDetune = pv(kModDetune);
        s.fm = pv(kFmIndex) * 0.01; s.am = pv(kAmDepth) * 0.01; s.modFold = pv(kModFold) * 0.01;
        s.fold = pv(kFold) * 0.01; s.symmetry = pv(kSymmetry) * 0.01; s.edge = pv(kEdge) * 0.01; s.stages = (int)pv(kStages);
        s.gateMode = (int)pv(kGateModeP); s.gateOpen = pv(kGateOpen) * 0.01; s.vactrol = pv(kVactrol) * 0.01; s.gateReso = pv(kGateReso) * 0.01;
        s.funcMode = (int)pv(kFuncModeP); s.rise = pv(kRise) * 0.001; s.fall = pv(kFall) * 0.001; s.funcGate = pv(kFuncGate) * 0.01;
        s.funcFold = pv(kFuncFold) * 0.01; s.funcFm = pv(kFuncFm) * 0.01; s.funcPitch = pv(kFuncPitch);
        s.randRate = pv(kRandRate); s.randSmooth = pv(kRandSmooth) * 0.01; s.randFold = pv(kRandFold) * 0.01; s.randFm = pv(kRandFm) * 0.01; s.randPitch = pv(kRandPitch);
        s.velocity = pv(kVelocity) * 0.01; s.spread = pv(kSpread) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode);
        s.glide = pv(kGlide); s.octave = (int)pv(kOctave); s.fine = pv(kFine);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::fold::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- GLOSS (PluginGloss.hpp's sync() and run() body: keep in step)
class SynthGloss : public SynthBase {
public:
    explicit SynthGloss(double rate) : SynthBase(kTGloss, rate) {}
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    int latency() const override { return engine.latency(); }
    void sync() override
    {
        using namespace pump::gloss;
        Settings s;
        s.machine = (int)pv(kMachine); s.a = pv(kA) * 0.01; s.b = pv(kB) * 0.01; s.c = pv(kC) * 0.01; s.d = pv(kD) * 0.01;
        s.tune = pv(kTune); s.pitchEnv = pv(kPitchEnv); s.pitchTime = pv(kPitchTime) * 0.001; s.glide = pv(kGlide); s.mode = (int)pv(kMode);
        s.filterType = (int)pv(kFilterType); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv) * 0.01;
        s.filterDecay = pv(kFilterDecay) * 0.001; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001; s.velAmp = pv(kVelAmp) * 0.01;
        s.drive = pv(kDrive) * 0.01; s.driveType = (int)pv(kDriveType); s.crushBits = pv(kCrushBits); s.crushRate = pv(kCrushRate); s.width = pv(kWidth) * 0.01;
        s.lfoShape = (int)pv(kLfoShape); s.lfoRate = pv(kLfoRate); s.lfoDepth = pv(kLfoDepth) * 0.01; s.lfoDest = (int)pv(kLfoDest);
        s.bendRange = pv(kBendRange); s.volumeDb = pv(kVolume);
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::gloss::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- TRACT (PluginTract.hpp's sync() and run() body: keep in step)
class SynthTract : public SynthBase {
public:
    explicit SynthTract(double rate) : SynthBase(kTTract, rate) {}
    static constexpr float kTrimDb = -5.0f;   // PluginTract::kTrimDb
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::tract;
        Settings s;
        s.vowel = pv(kVowel) * 0.01; s.motion = pv(kMotion) * 0.01; s.motionRate = pv(kMotionRate); s.size = pv(kSize) * 0.01;
        s.tense = pv(kTense) * 0.01; s.breath = pv(kBreath) * 0.01; s.vibrato = pv(kVibrato) * 0.01; s.vibRate = pv(kVibRate); s.jitter = pv(kJitter) * 0.01;
        s.singers = (int)pv(kSingers); s.detune = pv(kDetune); s.sizeSpread = pv(kSizeSpread) * 0.01; s.vowelSpread = pv(kVowelSpread) * 0.01;
        s.spread = pv(kSpread) * 0.01; s.bright = pv(kBright) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide); s.octave = (int)pv(kOctave);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::tract::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- STRIKE (PluginStrike.hpp's sync() and run() body: keep in step)
class SynthStrike : public SynthBase {
public:
    explicit SynthStrike(double rate) : SynthBase(kTStrike, rate) {}
    static constexpr float kTrimDb = 1.0f;   // PluginStrike::kTrimDb
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::strike;
        Settings s;
        for (int p = 0; p < kPads; ++p)
        {
            PadSettings& q = s.pad[p];
            q.engine = (int)pv(pp(p, kEngine)); q.pitch = pv(pp(p, kPitch)); q.decay = pv(pp(p, kDecay)) * 0.001; q.tone = pv(pp(p, kTone)) * 0.01;
            q.a = pv(pp(p, kCharA)) * 0.01; q.b = pv(pp(p, kCharB)) * 0.01; q.bend = pv(pp(p, kBend)); q.bendTime = pv(pp(p, kBendTime)) * 0.001;
            q.noise = pv(pp(p, kNoise)) * 0.01; q.level = pv(pp(p, kLevel)); q.pan = pv(pp(p, kPan)) * 0.01;
        }
        s.choke = pv(kChoke) >= 0.5f; s.velocity = pv(kVelocity) * 0.01; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::strike::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- BOING (PluginBoing.hpp's sync() and run() body: keep in step)
class SynthBoing : public SynthBase {
public:
    explicit SynthBoing(double rate) : SynthBase(kTBoing, rate) {}
    static constexpr float kTrimDb = -2.0f;   // PluginBoing::kTrimDb
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::boing;
        Settings s;
        for (int p = 0; p < kPads; ++p)
        {
            PadSettings& q = s.pad[p];
            q.tank = (int)pv(pp(p, kTank)); q.wave = (int)pv(pp(p, kWave)); q.blend = pv(pp(p, kBlend)) * 0.01; q.pitch = pv(pp(p, kPitch));
            q.decay = pv(pp(p, kDecay)) * 0.001; q.harmonic = pv(pp(p, kHarmonic)) * 0.01; q.spread = pv(pp(p, kSpread)) * 0.01; q.drip = pv(pp(p, kDrip));
            q.bend = pv(pp(p, kBend)); q.bendTime = pv(pp(p, kBendTime)) * 0.001; q.noise = pv(pp(p, kNoise)) * 0.01; q.noiseTone = pv(pp(p, kNoiseTone)) * 0.01;
            q.click = pv(pp(p, kClick)) * 0.01; q.hits = (int)pv(pp(p, kHits)); q.level = pv(pp(p, kLevel)); q.pan = pv(pp(p, kPan)) * 0.01; q.send = pv(pp(p, kSend)) * 0.01;
        }
        s.tankModel = (int)pv(kTankModel); s.tankDecay = pv(kTankDecay); s.tankLevel = pv(kTankLevel) * 0.01; s.choke = pv(kChoke) >= 0.5f;
        s.velocity = pv(kVelocity) * 0.01; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::boing::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- X0X (PluginX0x.hpp's sync() and run() body: keep in step)
class SynthX0x : public SynthBase {
public:
    explicit SynthX0x(double rate) : SynthBase(kTX0x, rate) {}
    // the engine's prepare() starts its voices afresh: the settings again after it, as the plugin's first run() does
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::x0x;
        Settings s;
        for (int i = 0; i < kInstr; ++i)
        {
            InstrSettings& q = s.in[i];
            q.machine = (int)pv(ip(i, kMachine)); q.tune = pv(ip(i, kTune)); q.decay = pv(ip(i, kDecay)) * 0.01; q.tone = pv(ip(i, kTone)) * 0.01;
            q.snap = pv(ip(i, kSnap)) * 0.01; q.levelDb = pv(ip(i, kLevel)); q.pan = pv(ip(i, kPan)) * 0.01;
        }
        s.accent = pv(kAccent) * 0.01; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume); s.velocity = pv(kVelocity) * 0.01;
        s.accentVel = (int)pv(kAccentVel); s.seqMode = (int)pv(kSeqMode); s.length = (int)pv(kSeqLength); s.rate = (int)pv(kSeqRate);
        s.swing = pv(kSwing) * 0.01;
        s.pattern = patternFrom([this](int i) { return pv((uint32_t)i); });
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.setTransport(tr); engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); tr.ppq += n * tr.bpm / 60.0 / sr; }

private:
    pump::x0x::Engine engine;
    pump::SafetyLimiter outLimit;
    pump::Transport tr;   // stopped: a host that isn't playing (the sequencer is off in Thunder Plus)
};

// ---------------------------------------------------------------- LIFE (PluginLife.hpp's sync() and run() body: keep in step;
// a stopped host, so the grid waits: in Thunder Plus the tracker plays its tones, C-4 up the white keys)
class SynthLife : public SynthBase {
public:
    explicit SynthLife(double rate) : SynthBase(kTLife, rate) {}
    static constexpr float kTrimDb = +2.0f;   // PluginLife::kTrimDb
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::life;
        Settings s;
        s.volumeDb = pv(kVolume) + kTrimDb; s.drive = pv(kDrive) * 0.01; s.fmAll = pv(kFmAll) * 0.01; s.decayAll = pv(kDecayAll) * 0.01;
        s.pitchRange = pv(kPitchRange); s.lfoRate = pv(kLfoRate); s.lfoDepth = pv(kLfoDepth) * 0.01; s.width = pv(kWidth) * 0.01;
        s.mode = (int)pv(kMode); s.rate = (int)pv(kRate); s.length = (int)pv(kLength); s.offset = (int)pv(kOffset); s.sens = pv(kSens) * 0.01;
        s.xWrap = (int)pv(kXWrap); s.yWrap = (int)pv(kYWrap); s.colour = (int)pv(kColour); s.cols = (int)pv(kCols); s.rows = (int)pv(kRows);
        s.rule = (int)pv(kRule); s.trigger = (int)pv(kTrigger); s.pattern = (int)pv(kPattern); s.reseed = pv(kReseed) >= 0.5f;
        s.shift = pv(kShift); s.grain = pv(kGrain) * 0.001; s.time = pv(kTime) * 0.001; s.feedback = pv(kFeedback) * 0.01;
        s.filter = pv(kFilter) * 0.01; s.fxDecay = pv(kFxDecay) * 0.001; s.mix = pv(kMix) * 0.01;
        s.fmLoop = pv(kFmLoop) >= 0.5f;
        for (int t = 0; t < kTones; ++t)
        {
            ToneSettings& o = s.tone[t];
            o.pitch = pv(tone(t, kTPitch)); o.kick = pv(tone(t, kTKick)) * 0.01; o.fm = pv(tone(t, kTFm)) * 0.01; o.ring = pv(tone(t, kTRing)) * 0.01;
            o.decay = pv(tone(t, kTDecay)) * 0.001; o.level = pv(tone(t, kTLevel)) * 0.01; o.mute = pv(tone(t, kTMute)) >= 0.5f;
            o.fxSend = pv(fxSend(t)) * 0.01;
        }
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.setTransport(tr); engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); tr.ppq += n * tr.bpm / 60.0 / sr; }

private:
    pump::life::Engine engine;
    pump::SafetyLimiter outLimit;
    pump::Transport tr;
};

// ---------------------------------------------------------------- FREEZE (PluginFreeze.hpp's sync() and run() body: keep in
// step). Its built-in sounds are PRISM's (PrismAudio.hpp) cut into spectra as FreezeSources.hpp does, but here, with no
// threads, made the first time one is asked for (about a second's work) and kept. No file source (0) here: silent.
class SynthFreeze : public SynthBase {
public:
    explicit SynthFreeze(double rate) : SynthBase(kTFreeze, rate) {}
    static constexpr float kTrimDb = -8.0f;   // PluginFreeze::kTrimDb
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void sync() override
    {
        using namespace pump::freeze;
        Settings s;
        s.position = pv(kPosition) * 0.01; s.speed = pv(kSpeed) * 0.01; s.spray = pv(kSpray) * 0.01; s.grains = (int)pv(kGrains); s.blur = pv(kBlur) * 0.01;
        s.root = (int)pv(kRoot); s.track = pv(kTrack) * 0.01; s.stretch = pv(kStretch) * 0.01; s.tilt = pv(kTilt) * 0.01; s.shimmer = pv(kShimmer) * 0.01;
        s.low = pv(kLow); s.high = pv(kHigh); s.phase = pv(kPhase) * 0.01; s.width = pv(kWidth) * 0.01;
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.velocity = pv(kVelocity) * 0.01; s.voices = (int)pv(kVoices); s.voiceMode = (int)pv(kVoiceMode); s.glide = pv(kGlide); s.octave = (int)pv(kOctave);
        s.volumeDb = pv(kVolume) + kTrimDb;
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override
    {
        engine.setSource(source((int)pv(pump::freeze::kSource)));
        engine.process(l, r, n, ev, nev); outLimit.process(l, r, n);
    }

private:
    // FreezeSources.hpp's Builtins, made on this thread: the notes they were played at, as there
    static const pump::freeze::Spectra* source(int id)
    {
        static std::shared_ptr<pump::freeze::Spectra> set[pump::freeze::kNumSources];
        static const int roots[8] = { 60, 57, 48, 60, 45, 45, 69, 43 };
        if (id < 1 || id >= pump::freeze::kNumSources || id > 7) return nullptr;
        if (!set[id]) set[id] = pump::freeze::makeSpectra(pump::prism::detail::builtinAudio(id, 48000.0), 48000.0, pump::freeze::kSourceNames[id], roots[id]);
        return set[id].get();
    }
    pump::freeze::Engine engine;
    pump::SafetyLimiter outLimit;
};

// ---------------------------------------------------------------- WOB (PluginWob.hpp's sync() and run() body: keep in step).
// Its wobble follows the tempo: psyn_tempo() sets it (the host's, 120 until then); the transport is stopped, so the
// wobble runs free at that tempo, as in a host that isn't playing.
class SynthWob : public SynthBase {
public:
    explicit SynthWob(double rate) : SynthBase(kTWob, rate) {}
    static constexpr float kTrimDb = -8.5f;   // PluginWob::kTrimDb
    void activate() override { sync(); engine.prepare(sr, kMaxBlock); outLimit.prepare(sr); sync(); anyChanged = false; }
    void tempo(double bpm) override { if (bpm > 1.0 && bpm < 1000.0) tr.bpm = bpm; }
    void sync() override
    {
        using namespace pump::wob;
        Settings s;
        s.octave = (int)pv(kOctave); s.tune = pv(kTune); s.shape = pv(kShape) * 0.01; s.partials = (int)pv(kPartials);
        s.tilt = pv(kTilt) * 0.01; s.oddEven = pv(kOddEven) * 0.01; s.stretch = pv(kStretch) * 0.01;
        s.filterMode = (int)pv(kFilterMode); s.cutoff = pv(kCutoff); s.res = pv(kRes) * 0.01; s.filterEnv = pv(kFilterEnv);
        s.filterDecay = pv(kFilterDecay) * 0.001; s.keyTrack = pv(kKeyTrack) * 0.01;
        s.vowel = pv(kVowel) * 0.01; s.formant = pv(kFormant) * 0.01; s.combFreq = pv(kCombFreq) * 0.01; s.comb = pv(kComb) * 0.01;
        s.detune = pv(kDetune); s.width = pv(kWidth) * 0.01; s.sub = pv(kSub) * 0.01; s.subOct = (int)pv(kSubOct);
        s.wobSync = pv(kWobSync) >= 0.5f; s.wobDiv = (int)pv(kWobDiv); s.wobRate = pv(kWobRate); s.wobShape = (int)pv(kWobShape);
        s.wobRetrig = pv(kWobRetrig) >= 0.5f; s.wobPhase = pv(kWobPhase) * 0.01; s.wobSmooth = pv(kWobSmooth) * 0.01;
        s.wobDelay = pv(kWobDelay); s.wobFade = pv(kWobFade); s.wobDepth = pv(kWobDepth) * 0.01; s.wheel = pv(kWheel) * 0.01;
        s.wobCut = pv(kWobCut); s.wobVowel = pv(kWobVowel) * 0.01; s.wobComb = pv(kWobComb) * 0.01; s.wobAmp = pv(kWobAmp) * 0.01; s.wobPitch = pv(kWobPitch);
        s.attack = pv(kAttack) * 0.001; s.decay = pv(kDecay) * 0.001; s.sustain = pv(kSustain) * 0.01; s.release = pv(kRelease) * 0.001;
        s.glide = pv(kGlide); s.legato = pv(kLegato) >= 0.5f; s.drive = pv(kDrive) * 0.01; s.volumeDb = pv(kVolume) + kTrimDb;
        s.shapeB = pv(kShapeB) * 0.01; s.tiltB = pv(kTiltB) * 0.01; s.morph = pv(kMorph) * 0.01; s.wobMorph = pv(kWobMorph) * 0.01;
        s.inharm = pv(kInharm) * 0.01; s.spray = pv(kSpray) * 0.01; s.drift = pv(kDrift) * 0.01; s.driftRate = pv(kDriftRate);
        engine.setSettings(s);
    }
    void render(float* l, float* r, int n, const pump::synth::Midi* ev, int nev) override { engine.setTransport(tr); engine.process(l, r, n, ev, nev); outLimit.process(l, r, n); }

private:
    pump::wob::Engine engine;
    pump::SafetyLimiter outLimit;
    pump::Transport tr;
};

inline SynthBase* make(int type, double sr)
{
    switch (type)
    {
    case kMono: return new SynthMono(sr);
    case kPoly: return new SynthPoly(sr);
    case kCoil: return new SynthCoil(sr);
    case kOrbit: return new SynthOrbit(sr);
    case kTFold: return new SynthFold(sr);
    case kTGloss: return new SynthGloss(sr);
    case kTTract: return new SynthTract(sr);
    case kTStrike: return new SynthStrike(sr);
    case kTBoing: return new SynthBoing(sr);
    case kTX0x: return new SynthX0x(sr);
    case kTLife: return new SynthLife(sr);
    case kTFreeze: return new SynthFreeze(sr);
    case kTWob: return new SynthWob(sr);
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
PUMP_API void psyn_tempo(int h, double bpm) { if (SynthBase* s = at(h)) s->tempo(bpm); }
