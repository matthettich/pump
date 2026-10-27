// pump in WebAssembly: the nine engines behind one C interface, for Thunder Plus's track-effect worklet.
//
// Each Host<Name> below is its plugin's DSP side (plugins/<name>/Plugin<Name>.hpp) without DPF: the same prepare /
// activate / sync / run code, the same chunk sizes, the same BypassMixer, sanitising and UI feeds (ScopeRings and
// MeterValues). HostBase stands in for PumpPlugin: parameter values with change flags (pv, takeAnyChanged,
// takeChanged), the sample rate, the buffer size and setLatency(). Keep these in step with the plugins: when a
// Plugin<Name>.hpp changes, change its Host here the same way (docs/DEVELOPING.md, "WebAssembly").
//
// Build: wasm/build.sh (wasi-sdk, -DPUMP_SYNC_KERNELS=1: no threads in an AudioWorklet). The same file builds natively
// for wasm/compare.cpp, which checks that the wasm and native builds give the same output.
//
// Interface (all exported, "C"):
//   pump_types()                       number of effect types (9); type ids are kTypeNames' order
//   pump_nparams(type)                 parameter count (parameter i is params()[i] of that plugin)
//   pump_stage()                       float[512]: write a new slot's values here before pump_new (values not
//                                      written are the parameter's default: pump_new clears the stage after use)
//   pump_new(type, sr)                 a slot (an opaque handle >= 0, wasm/Handles.hpp) with the staged values,
//                                      activated; -1 on failure. A freed slot's handle is refused from then on
//   pump_free(h)
//   pump_values(h)                     float[nparams]: the slot's requested values; write, then pump_commit(h)
//   pump_commit(h)                     applies the values that changed (clamped as a host's setParameterValue)
//   pump_io(ch)                        float[4096] buffers: 0 = L, 1 = R (in place), 2 = sidechain L, 3 = sidechain R
//   pump_process(h, n, sc)             n <= 4096 samples of pump_io(0/1) in place; sc != 0: COMP's external
//                                      sidechain is pump_io(2/3), else silence (an unconnected sidechain)
//   pump_latency(h)                    the latency the plugin would report now (samples)
//   pump_tel(h)                        float[]: exchanges the slot's meters into a snapshot (layout: PUMP-FX.md)
//   pump_tel_len(type)
//   pump_rings(h)                      number of sample feeds the slot has (pre/post spectra, scopes, histories)
//   pump_ring_pos(h, k)                write position of feed k (wraps at 2^32)
//   pump_ring_cap(h, k)                its size (a power of two)
//   pump_ring_read(h, k, from, count)  float[count] (count <= 32768): feed k's samples from position `from`
//   pump_hold(h, kind, a, b, c, d)     momentary listening, never saved: kind 0 = listen band / EQ solo (a = band,
//                                      -1 off), 1 = audition filter (a = on, b = Hz, c = Q, d = region, CALM),
//                                      2 = LIMIT: reset loudness
#include <limits>
#include "Handles.hpp"
#include "../plugins/eq/EQEngine.hpp"
#include "../plugins/comp/CompEngine.hpp"
#include "../plugins/limit/LimitEngine.hpp"
#include "../plugins/calm/CalmEngine.hpp"
#include "../plugins/tone/ToneEngine.hpp"
#include "../plugins/band/BandEngine.hpp"
#include "../plugins/wide/WideEngine.hpp"
#include "../plugins/heat/HeatEngine.hpp"
#include "../plugins/meter/MeterEngine.hpp"
#include "../plugins/meter/MeterParams.hpp"
#include "dsp/Bypass.hpp"
#include "dsp/Audition.hpp"
#include <memory>

#if defined(__wasm__)
 #define PUMP_API extern "C" __attribute__((visibility("default"), used))
#else
 #define PUMP_API extern "C"
#endif

namespace pumpwasm {

using pump::ParamSpec;
constexpr int kMaxBlock = 4096;   // the most samples one pump_process call takes (Thunder sends 128)
constexpr int kStage = 512;
// a number from JavaScript as an int (a float that is NaN or out of range has no int value: casting it is undefined)
inline int toInt(float a) { return a == a ? (int)std::max(-1.0e6f, std::min(a, 1.0e6f)) : -1; }

enum Type : int { kEQ = 0, kComp, kLimit, kCalm, kTone, kBand, kWide, kHeat, kMeter, kNumTypes };

inline const std::vector<ParamSpec>& specsOf(int type)
{
    switch (type)
    {
    case kEQ: return pump::eq::params();
    case kComp: return pump::comp::params();
    case kLimit: return pump::limit::params();
    case kCalm: return pump::calm::params();
    case kTone: return pump::tone::params();
    case kBand: return pump::band::params();
    case kWide: return pump::wide::params();
    case kHeat: return pump::heat::params();
    default: return pump::meter::params();
    }
}

// PumpPlugin without DPF: values with change flags, as the plugins read them on the audio thread.
class HostBase {
public:
    HostBase(int t, double rate) : type(t), specs(specsOf(t).data()), numParams((int)specsOf(t).size()), sr(rate)
    {
        values.resize((size_t)numParams); changed.assign((size_t)numParams, 1); requested.resize((size_t)numParams);
        for (int i = 0; i < numParams; ++i) values[(size_t)i] = requested[(size_t)i] = specs[i].def;
    }
    virtual ~HostBase() {}
    virtual void activate() = 0;
    virtual void run(const float** inputs, float** outputs, uint32_t frames) = 0;
    virtual int latencyNow() const = 0;
    virtual int telemetry(float* t) = 0;    // returns the number of values written
    virtual int numRings() const { return 0; }
    virtual const pump::ScopeRing* ring(int) const { return nullptr; }
    virtual void hold(int kind, float a, float b, float c, float d) { (void)kind; (void)a; (void)b; (void)c; (void)d; }

    // what a host does: setParameterValue() for each value that changed
    void commit()
    {
        for (int i = 0; i < numParams; ++i)
            if (requested[(size_t)i] != values[(size_t)i] || !(requested[(size_t)i] == requested[(size_t)i])) setParameterValue((uint32_t)i, requested[(size_t)i]);
    }
    void setParameterValue(uint32_t index, float value)
    {
        if (specs[index].flags & pump::kPOutput) return;
        values[index] = specs[index].clamp(value);
        requested[index] = values[index];
        changed[index] = 1;
        anyChanged = true;
    }

    const int type;
    const ParamSpec* specs;
    const int numParams;
    std::vector<float> requested;   // pump_values(): written by the host, applied by commit()
    bool scConnected = false;       // COMP: Thunder feeds the external sidechain

protected:
    float pv(uint32_t i) const { return values[i]; }
    bool takeAnyChanged() { const bool a = anyChanged; anyChanged = false; return a; }
    bool takeChanged(uint32_t i) { const bool c = changed[i] != 0; changed[i] = 0; return c; }
    double getSampleRate() const { return sr; }
    uint32_t getBufferSize() const { return (uint32_t)kMaxBlock; }
    void setLatency(uint32_t l) { reported = (int)l; }
    static float lin(pump::MeterValue& m) { return m.exchange(0.0f); }

    std::vector<float> values;
    std::vector<char> changed;
    bool anyChanged = true;
    double sr;
    int reported = 0;
};

// ---------------------------------------------------------------- EQ (PluginEQ.hpp)
class HostEQ : public HostBase {
public:
    HostEQ(double rate) : HostBase(kEQ, rate)
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 17, (int)std::max<uint32_t>(getBufferSize(), 8192));
    }
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    pump::eq::Engine engine;
    std::atomic<int> soloHold { -1 };
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };
    int latencyNow() const override { return engine.latency(); }

    void activate() override
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 17, (int)std::max<uint32_t>(getBufferSize(), 8192));
        audition.prepare(getSampleRate());
        syncAll();
        engine.finishPending();
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void syncAll()
    {
        using namespace pump::eq;
        for (int b = 0; b < kNumBands; ++b) syncBand(b);
        engine.setGainScale(pv(kGainScale));
        engine.setOutput(pv(kOutGain));
        engine.setAutoGain(pv(kAutoGain) >= 0.5f);
        engine.setDynTimes(pv(kDynAttack), pv(kDynRelease));
        engine.setPhaseMode((int)pv(kPhase), (int)pv(kLinQuality));
        syncSolo();
    }
    void syncSolo()
    {
        const int h = soloHold.load(std::memory_order_relaxed);
        engine.setSolo(h >= 0 ? h : (int)pv(pump::eq::kSolo) - 1);
    }
    void syncBand(int b)
    {
        using namespace pump::eq;
        pump::BandSpec s;
        s.shape = (pump::Shape)(int)pv((uint32_t)bandParam(b, kShape));
        s.freq = pv((uint32_t)bandParam(b, kFreq));
        s.gainDb = pv((uint32_t)bandParam(b, kGain));
        s.q = pv((uint32_t)bandParam(b, kQ));
        s.slope = kSlopeValues[pump::clampv((int)pv((uint32_t)bandParam(b, kSlope)), 0, kMaxSlopeIdx)];
        engine.setBand(b, pv((uint32_t)bandParam(b, kUsed)) >= 0.5f, pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f, s,
                       (int)pv((uint32_t)bandParam(b, kPlace)), pv((uint32_t)bandParam(b, kDyn)) >= 0.5f,
                       pv((uint32_t)bandParam(b, kDynRange)), pv((uint32_t)bandParam(b, kDynThresh)));
    }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::eq;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool bandDirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) bandDirty[b] = true; else global = true;
            }
            for (int b = 0; b < kNumBands; ++b) if (bandDirty[b]) syncBand(b);
            if (global)
            {
                engine.setGainScale(pv(kGainScale));
                engine.setOutput(pv(kOutGain));
                engine.setAutoGain(pv(kAutoGain) >= 0.5f);
                engine.setDynTimes(pv(kDynAttack), pv(kDynRelease));
                engine.setPhaseMode((int)pv(kPhase), (int)pv(kLinQuality));
            }
        }
        syncSolo();
        const int lat = engine.latency();
        if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            audition.set(auditionOn.load(std::memory_order_relaxed), auditionFreq.load(std::memory_order_relaxed), auditionQ.load(std::memory_order_relaxed));
            audition.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                if (!std::isfinite(l[i])) l[i] = 0.0f;
                if (!std::isfinite(r[i])) r[i] = 0.0f;
                postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = -1.0f; t[4] = -1.0f;
        for (int b = 0; b < pump::eq::kNumBands; ++b) t[5 + b] = (float)engine.dynOffset(b);
        t[29] = (float)engine.autoGainValue();
        return 30;
    }
    int numRings() const override { return 2; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &preRing : k == 1 ? &postRing : nullptr; }
    void hold(int kind, float a, float b, float c, float) override
    {
        if (kind == 0) soloHold.store(toInt(a));
        if (kind == 1) { auditionFreq.store(b); auditionQ.store(c); auditionOn.store(a >= 0.5f); }
    }
private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- COMP (PluginComp.hpp)
class HostComp : public HostBase {
public:
    HostComp(double rate) : HostBase(kComp, rate) { prepareAll(); }
    pump::comp::Engine engine;
    int latencyNow() const override { return engine.latency(); }
    void activate() override { prepareAll(); lastLatency = -1; }
    void prepareAll()
    {
        const double rate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncValues();
        engine.setSettings(pump::comp::settingsFromParams(vals));
        engine.prepare(rate, (int)getBufferSize());
        bypass.prepare(rate, engine.maxLatencySamples() + 8, kBlock);
        bypass.setLatency(engine.latency());
        setLatency((uint32_t)engine.latency());
    }
    void syncValues() { for (uint32_t i = 0; i < (uint32_t)pump::comp::kNumParams; ++i) vals[i] = pv(i); }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::comp;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            syncValues();
            engine.setSettings(settingsFromParams(vals));
        }
        const bool bypassed = pv(kBypass) >= 0.5f;
        const bool hasSc = inputs[2] != nullptr && inputs[3] != nullptr;
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)kBlock);
            for (int i = 0; i < n; ++i)
            {
                bufL[i] = inputs[0][done + i]; bufR[i] = inputs[1][done + i];
                scL[i] = hasSc ? inputs[2][done + i] : 0.0f; scR[i] = hasSc ? inputs[3][done + i] : 0.0f;
            }
            bypass.capture(bufL, bufR, n);
            engine.process(bufL, bufR, scL, scR, bufL, bufR, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { bypass.setLatency(lat); lastLatency = lat; setLatency((uint32_t)lat); }
            bypass.apply(bufL, bufR, n, bypassed);
            std::memcpy(outputs[0] + done, bufL, sizeof(float) * (size_t)n);
            std::memcpy(outputs[1] + done, bufR, sizeof(float) * (size_t)n);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        auto& e = engine;
        t[0] = (float)latencyNow(); t[1] = lin(e.outPeak[0]); t[2] = lin(e.outPeak[1]); t[3] = lin(e.inPeak[0]); t[4] = lin(e.inPeak[1]);
        t[5] = e.grMin[0].exchange(0.0f); t[6] = e.grMin[1].exchange(0.0f);
        t[7] = e.detLevel.exchange(-200.0f); t[8] = e.scPeak.exchange(0.0f);
        t[9] = e.makeupNow.load(std::memory_order_relaxed); t[10] = scConnected ? 1.0f : 0.0f;
        return 11;
    }
    int numRings() const override { return 4; }
    const pump::ScopeRing* ring(int k) const override
    {
        return k == 0 ? &engine.histIn : k == 1 ? &engine.histOut : k == 2 ? &engine.histGr : k == 3 ? &engine.histDet : nullptr;
    }
private:
    static constexpr int kBlock = 256;
    pump::BypassMixer bypass;
    float vals[pump::comp::kNumParams] = {};
    float bufL[kBlock] = {}, bufR[kBlock] = {}, scL[kBlock] = {}, scR[kBlock] = {};
    int lastLatency = -1;
};

// ---------------------------------------------------------------- LIMIT (PluginLimit.hpp)
class HostLimit : public HostBase {
public:
    HostLimit(double rate) : HostBase(kLimit, rate)
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 14, (int)std::max<uint32_t>(getBufferSize(), 8192));
        setLatency((uint32_t)engine.latency());
    }
    pump::limit::Engine engine;
    int latencyNow() const override { return engine.latency(); }
    void activate() override
    {
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 14, (int)std::max<uint32_t>(getBufferSize(), 8192));
        sync();
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void sync()
    {
        using namespace pump::limit;
        engine.setGain(pv(kGain));
        engine.setCeiling(pv(kCeiling));
        engine.setStyle((int)pv(kStyle));
        engine.setLookahead(pv(kLookahead));
        engine.setAttack(pv(kAttack));
        engine.setRelease(pv(kRelease));
        engine.setLinks(pv(kLinkTrans), pv(kLinkRel));
        engine.setTruePeak(pv(kTruePeak) >= 0.5f);
        engine.setOversampling((int)pv(kOversampling));
        engine.setDC(pv(kDC) >= 0.5f);
        engine.setDither((int)pv(kDither), (int)pv(kShaping));
        engine.setUnity(pv(kUnity) >= 0.5f);
    }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::limit;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            sync();
        }
        const int lat = engine.latency();
        if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            engine.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        auto& e = engine;
        t[0] = (float)latencyNow(); t[1] = lin(e.outPeak[0]); t[2] = lin(e.outPeak[1]); t[3] = lin(e.inPeak[0]); t[4] = lin(e.inPeak[1]);
        t[5] = e.grMax.exchange(0.0f);
        const pump::limit::LoudnessReadout* rd[2] = { &e.outLoud, &e.inLoud };
        for (int k = 0; k < 2; ++k)
        {
            float* o = t + 6 + k * 7;
            o[0] = rd[k]->momentary.load(); o[1] = rd[k]->shortTerm.load(); o[2] = rd[k]->integrated.load(); o[3] = rd[k]->range.load();
            o[4] = rd[k]->truePeak.load(); o[5] = rd[k]->maxMomentary.load(); o[6] = rd[k]->maxShortTerm.load();
        }
        t[20] = (float)e.oversamplingFactor(); t[21] = e.currentGainDb();
        return 22;
    }
    int numRings() const override { return 4; }
    const pump::ScopeRing* ring(int k) const override
    {
        return k == 0 ? &engine.histIn : k == 1 ? &engine.histOut : k == 2 ? &engine.histGR : k == 3 ? &engine.histST : nullptr;
    }
    void hold(int kind, float, float, float, float) override { if (kind == 2) engine.resetLoudness.store(true); }
private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- CALM (PluginCalm.hpp)
class HostCalm : public HostBase {
public:
    HostCalm(double rate) : HostBase(kCalm, rate) { prepareAll(); }
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2], inPeak[2];
    pump::calm::Engine engine;
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };
    std::atomic<int> auditionRegion { pump::Audition::kBand };
    int latencyNow() const override { return engine.latency(); }
    void prepareAll()
    {
        const double rate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncSettings();
        engine.prepare(rate);
        bypass.prepare(rate, engine.sizeFor(3) + 16, pump::calm::Engine::kMaxChunk);
        audition.prepare(rate);
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void activate() override { prepareAll(); }
    void syncSettings()
    {
        using namespace pump::calm;
        Settings s;
        s.depth = pv(kDepth); s.sharpness = pv(kSharpness); s.selectivity = pv(kSelectivity);
        s.attackMs = pv(kAttack); s.releaseMs = pv(kRelease); s.mode = (int)pv(kMode);
        s.focusLo = pv(kFocusLow); s.focusHi = pv(kFocusHigh);
        s.stereo = (int)pv(kStereo); s.balance = pv(kBalance); s.quality = (int)pv(kQuality);
        s.delta = pv(kDelta) >= 0.5f; s.mix = pv(kMix); s.trimDb = pv(kTrim);
        for (int n = 0; n < kNumNodes; ++n)
        {
            s.nodes[n].freq = pv((uint32_t)nodeParam(n, kNFreq));
            s.nodes[n].gain = pv((uint32_t)nodeParam(n, kNGain));
            s.nodes[n].q = pv((uint32_t)nodeParam(n, kNQ));
        }
        engine.set(s);
    }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::calm;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            syncSettings();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)Engine::kMaxChunk);
            float* l = L + done;
            float* r = R + done;
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            float il = 0.0f, ir = 0.0f;
            for (int i = 0; i < n; ++i) { preRing.push(0.5f * (l[i] + r[i])); il = std::max(il, std::fabs(l[i])); ir = std::max(ir, std::fabs(r[i])); }
            inPeak[0].setMax(il); inPeak[1].setMax(ir);
            engine.process(l, r, n);
            const int lat2 = engine.latency();
            if (lat2 != lastLatency) { setLatency((uint32_t)lat2); bypass.setLatency(lat2); lastLatency = lat2; }
            const bool aud = auditionOn.load(std::memory_order_acquire);
            audition.set(aud, auditionFreq.load(std::memory_order_relaxed), auditionQ.load(std::memory_order_relaxed),
                         (pump::Audition::Region)pump::clampv(auditionRegion.load(std::memory_order_relaxed), 0, 2));
            audition.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                if (!std::isfinite(l[i])) l[i] = 0.0f;
                if (!std::isfinite(r[i])) r[i] = 0.0f;
                postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        using pump::calm::Engine;
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = lin(inPeak[0]); t[4] = lin(inPeak[1]);
        t[5] = engine.grMax.exchange(0.0f);
        for (int c = 0; c < 2; ++c) for (int p = 0; p < Engine::kUiPoints; ++p) t[6 + c * Engine::kUiPoints + p] = engine.uiReduction(c, p);
        t[6 + 2 * Engine::kUiPoints] = (float)engine.settings().stereo;
        t[7 + 2 * Engine::kUiPoints] = (float)engine.fftSize();
        return 8 + 2 * Engine::kUiPoints;
    }
    int numRings() const override { return 2; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &preRing : k == 1 ? &postRing : nullptr; }
    void hold(int kind, float a, float b, float c, float d) override
    {
        if (kind == 1) { auditionRegion.store((int)d); auditionFreq.store(b); auditionQ.store(c); auditionOn.store(a >= 0.5f, std::memory_order_release); }
    }
private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- TONE (PluginTone.hpp)
class HostTone : public HostBase {
public:
    HostTone(double rate) : HostBase(kTone, rate) { prepareAll(); }
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    pump::tone::Engine engine;
    std::atomic<bool> auditionOn { false };
    std::atomic<float> auditionFreq { 1000.0f }, auditionQ { 2.0f };
    int latencyNow() const override { return engine.latency(); }
    void prepareAll()
    {
        const double rate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        syncSettings();
        engine.prepare(rate);
        bypass.prepare(rate, engine.sizeFor(1) + 16, pump::tone::Engine::kMaxChunk);
        audition.prepare(rate);
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void activate() override { prepareAll(); }
    void syncSettings()
    {
        using namespace pump::tone;
        Settings s;
        s.recover = pv(kRecover); s.tame = pv(kTame); s.bias = pv(kBias); s.brighten = pv(kBrighten); s.body = pv(kBody);
        s.speed = pv(kSpeed); s.autoLevel = pv(kAutoLevel) >= 0.5f; s.outDb = pv(kOutGain); s.mix = pv(kMix);
        s.quality = (int)pv(kQuality); s.delta = pv(kDelta) >= 0.5f;
        engine.set(s);
    }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::tone;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i) takeChanged(i);
            syncSettings();
        }
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, (uint32_t)Engine::kMaxChunk);
            float* l = L + done;
            float* r = R + done;
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            const int lat2 = engine.latency();
            if (lat2 != lastLatency) { setLatency((uint32_t)lat2); bypass.setLatency(lat2); lastLatency = lat2; }
            audition.set(auditionOn.load(std::memory_order_relaxed), auditionFreq.load(std::memory_order_relaxed),
                         auditionQ.load(std::memory_order_relaxed));
            audition.process(l, r, n);
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        constexpr int B = pump::tone::Engine::kBands;
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = -1.0f; t[4] = -1.0f;
        t[5] = engine.uiMakeup();
        for (int b = 0; b < B; ++b)
        {
            t[6 + b] = engine.uiGain(b); t[6 + B + b] = engine.uiTarget(b); t[6 + 2 * B + b] = engine.uiLevel(b);
            t[6 + 3 * B + b] = (float)engine.bandFreq(b); t[6 + 4 * B + b] = (float)engine.bandErb(b);
        }
        t[6 + 5 * B] = (float)engine.fftSize();
        return 7 + 5 * B;
    }
    int numRings() const override { return 2; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &preRing : k == 1 ? &postRing : nullptr; }
    void hold(int kind, float a, float b, float c, float) override
    {
        if (kind == 1) { auditionFreq.store(b); auditionQ.store(c); auditionOn.store(a >= 0.5f); }
    }
private:
    pump::BypassMixer bypass;
    pump::Audition audition;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- BAND (PluginBand.hpp)
class HostBand : public HostBase {
public:
    HostBand(double rate) : HostBase(kBand, rate)
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2], inPeak[2];
    pump::band::Engine engine;
    std::atomic<int> listenHold { -1 };
    int latencyNow() const override { return engine.latency(); }
    void activate() override
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void syncBand(int b)
    {
        using namespace pump::band;
        BandSettings s;
        s.mode = (int)pv((uint32_t)bandParam(b, kMode));
        s.thresh = pv((uint32_t)bandParam(b, kThresh));
        s.ratio = pv((uint32_t)bandParam(b, kRatio));
        s.attack = pv((uint32_t)bandParam(b, kAttack));
        s.release = pv((uint32_t)bandParam(b, kRelease));
        s.knee = pv((uint32_t)bandParam(b, kKnee));
        s.range = pv((uint32_t)bandParam(b, kRange));
        s.makeup = pv((uint32_t)bandParam(b, kMakeup));
        s.solo = pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        using namespace pump::band;
        engine.setOutput(pv(kOutGain));
        engine.setMix(pv(kMix));
        engine.setXoverMode((int)pv(kXoverMode));
        engine.setBands((int)pv(kNumBandsP));
        engine.setCrossovers(pv(kXover1), pv(kXover2), pv(kXover3));
        engine.setLookahead(pv(kLookahead));
        engine.setDetection((int)pv(kDetect));
        engine.setLink(pv(kLink));
        engine.setAutoMakeup(pv(kAutoMakeup) >= 0.5f);
    }
    void syncAll() { syncGlobals(); for (int b = 0; b < pump::band::kNumBands; ++b) syncBand(b); }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::band;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool dirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) dirty[b] = true; else global = true;
            }
            if (global) syncGlobals();
            for (int b = 0; b < kNumBands; ++b) if (dirty[b]) syncBand(b);
        }
        engine.setListen(listenHold.load(std::memory_order_relaxed));
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            float il = 0.0f, ir = 0.0f;
            for (int i = 0; i < n; ++i) { preRing.push(0.5f * (l[i] + r[i])); il = std::max(il, std::fabs(l[i])); ir = std::max(ir, std::fabs(r[i])); }
            inPeak[0].setMax(il); inPeak[1].setMax(ir);
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = lin(inPeak[0]); t[4] = lin(inPeak[1]);
        for (int b = 0; b < 4; ++b) { t[5 + b] = engine.gainDb(b); t[9 + b] = engine.levelDb(b); t[13 + b] = engine.makeupDb(b); }
        double e[3]; engine.effectiveCrossovers(e);
        for (int k = 0; k < 3; ++k) t[17 + k] = (float)e[k];
        t[20] = (float)engine.bands();
        return 21;
    }
    int numRings() const override { return 2; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &preRing : k == 1 ? &postRing : nullptr; }
    void hold(int kind, float a, float, float, float) override { if (kind == 0) listenHold.store(toInt(a)); }
private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- WIDE (PluginWide.hpp)
class HostWide : public HostBase {
public:
    HostWide(double rate) : HostBase(kWide, rate)
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    pump::ScopeRing scopeL { 1 << 15 }, scopeR { 1 << 15 }, midRing { 1 << 15 }, sideRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    pump::wide::Engine engine;
    std::atomic<int> listenHold { -1 };
    int latencyNow() const override { return engine.latency(); }
    void activate() override
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void syncBand(int b)
    {
        using namespace pump::wide;
        BandSettings s;
        s.width = pv((uint32_t)bandParam(b, kWidth));
        s.stereoize = pv((uint32_t)bandParam(b, kStereoize));
        s.solo = pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        using namespace pump::wide;
        engine.setOutput(pv(kOutGain));
        engine.setBalance(pv(kBalance));
        engine.setMonoBass(pv(kMonoBass) >= 0.5f, pv(kMonoFreq), (int)pv(kMonoSlope));
        engine.setXoverMode((int)pv(kXoverMode));
        engine.setBands((int)pv(kNumBandsP));
        engine.setCrossovers(pv(kXover1), pv(kXover2), pv(kXover3));
    }
    void syncAll() { syncGlobals(); for (int b = 0; b < pump::wide::kNumBands; ++b) syncBand(b); }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::wide;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool dirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) dirty[b] = true; else global = true;
            }
            if (global) syncGlobals();
            for (int b = 0; b < kNumBands; ++b) if (dirty[b]) syncBand(b);
        }
        engine.setListen(listenHold.load(std::memory_order_relaxed));
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                scopeL.push(l[i]); scopeR.push(r[i]);
                midRing.push(0.5f * (l[i] + r[i])); sideRing.push(0.5f * (l[i] - r[i]));
                pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i]));
            }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = -1.0f; t[4] = -1.0f;
        for (int b = 0; b < 4; ++b) { t[5 + b] = engine.correlation(b); t[9 + b] = engine.bandLevel(b); }
        t[13] = engine.correlationAll(); t[14] = engine.levelAll();
        double e[3]; engine.effectiveCrossovers(e);
        for (int k = 0; k < 3; ++k) t[15 + k] = (float)e[k];
        return 18;
    }
    int numRings() const override { return 4; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &scopeL : k == 1 ? &scopeR : k == 2 ? &midRing : k == 3 ? &sideRing : nullptr; }
    void hold(int kind, float a, float, float, float) override { if (kind == 0) listenHold.store(toInt(a)); }
private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- HEAT (PluginHeat.hpp)
class HostHeat : public HostBase {
public:
    HostHeat(double rate) : HostBase(kHeat, rate)
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    pump::ScopeRing preRing { 1 << 15 }, postRing { 1 << 15 };
    pump::MeterValue outPeak[2];
    pump::heat::Engine engine;
    std::atomic<int> listenHold { -1 };
    int latencyNow() const override { return engine.latency(); }
    void activate() override
    {
        syncAll();
        engine.prepare(getSampleRate(), (int)getBufferSize());
        bypass.prepare(getSampleRate(), 1 << 15, (int)std::max<uint32_t>(getBufferSize(), 8192));
        reportLatency();
    }
    void reportLatency()
    {
        lastLatency = engine.latency();
        setLatency((uint32_t)lastLatency);
        bypass.setLatency(lastLatency);
    }
    void syncBand(int b)
    {
        using namespace pump::heat;
        BandSettings s;
        s.mode = (int)pv((uint32_t)bandParam(b, kMode));
        s.drive = pv((uint32_t)bandParam(b, kDrive));
        s.mix = pv((uint32_t)bandParam(b, kBandMix));
        s.level = pv((uint32_t)bandParam(b, kLevel));
        s.solo = pv((uint32_t)bandParam(b, kSolo)) >= 0.5f;
        s.mute = pv((uint32_t)bandParam(b, kMute)) >= 0.5f;
        s.bypass = pv((uint32_t)bandParam(b, kBandBypass)) >= 0.5f;
        engine.setBand(b, s);
    }
    void syncGlobals()
    {
        using namespace pump::heat;
        engine.setOutput(pv(kOutGain));
        engine.setMix(pv(kMix));
        engine.setOversampling((int)pv(kOversampling));
        engine.setAutoGain(pv(kAutoGain) >= 0.5f);
        engine.setXoverMode((int)pv(kXoverMode));
        engine.setBands((int)pv(kNumBandsP));
        engine.setCrossovers(pv(kXover1), pv(kXover2), pv(kXover3));
    }
    void syncAll() { syncGlobals(); for (int b = 0; b < pump::heat::kNumBands; ++b) syncBand(b); }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        using namespace pump::heat;
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            bool dirty[kNumBands] = {};
            bool global = false;
            for (uint32_t i = 0; i < (uint32_t)kNumParams; ++i)
            {
                if (!takeChanged(i)) continue;
                const int b = bandOf((int)i);
                if (b >= 0) dirty[b] = true; else global = true;
            }
            if (global) syncGlobals();
            for (int b = 0; b < kNumBands; ++b) if (dirty[b]) syncBand(b);
        }
        engine.setListen(listenHold.load(std::memory_order_relaxed));
        float* L = outputs[0];
        float* R = outputs[1];
        if (L != inputs[0]) std::memcpy(L, inputs[0], sizeof(float) * frames);
        if (R != inputs[1]) std::memcpy(R, inputs[1], sizeof(float) * frames);
        uint32_t done = 0;
        while (done < frames)
        {
            const int n = (int)std::min<uint32_t>(frames - done, 4096);
            float* l = L + done;
            float* r = R + done;
            bypass.capture(l, r, n);
            pump::sanitizeBlock(l, n);
            pump::sanitizeBlock(r, n);
            for (int i = 0; i < n; ++i) preRing.push(0.5f * (l[i] + r[i]));
            engine.process(l, r, n);
            const int lat = engine.latency();
            if (lat != lastLatency) { setLatency((uint32_t)lat); bypass.setLatency(lat); lastLatency = lat; }
            bypass.apply(l, r, n, pv(kBypass) >= 0.5f);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < n; ++i) { postRing.push(0.5f * (l[i] + r[i])); pl = std::max(pl, std::fabs(l[i])); pr = std::max(pr, std::fabs(r[i])); }
            outPeak[0].setMax(pl); outPeak[1].setMax(pr);
            done += (uint32_t)n;
        }
    }
    int telemetry(float* t) override
    {
        t[0] = (float)latencyNow(); t[1] = lin(outPeak[0]); t[2] = lin(outPeak[1]); t[3] = -1.0f; t[4] = -1.0f;
        for (int b = 0; b < 4; ++b) { t[5 + b] = engine.autoGainDb(b); t[9 + b] = engine.inLevelDb(b); t[13 + b] = engine.outLevelDb(b); }
        double e[3]; engine.effectiveCrossovers(e);
        for (int k = 0; k < 3; ++k) t[17 + k] = (float)e[k];
        t[20] = (float)engine.oversampling();
        return 21;
    }
    int numRings() const override { return 2; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &preRing : k == 1 ? &postRing : nullptr; }
    void hold(int kind, float a, float, float, float) override { if (kind == 0) listenHold.store(toInt(a)); }
private:
    pump::BypassMixer bypass;
    int lastLatency = -1;
};

// ---------------------------------------------------------------- METER (PluginMeter.hpp)
class HostMeter : public HostBase {
public:
    HostMeter(double rate) : HostBase(kMeter, rate) { engine.prepare(getSampleRate()); }
    pump::meter::Engine engine;
    int latencyNow() const override { return 0; }
    void activate() override
    {
        engine.prepare(getSampleRate());
        engine.setResetCounter(pv(pump::meter::kReset));
    }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const pump::DenormalGuard dg;
        if (takeAnyChanged())
        {
            for (uint32_t i = 0; i < (uint32_t)pump::meter::kNumParams; ++i) takeChanged(i);
            engine.setResetCounter(pv(pump::meter::kReset));
        }
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        engine.process(inputs[0], inputs[1], (int)frames);
    }
    int telemetry(float* t) override
    {
        auto& e = engine; auto& o = e.out;
        t[0] = 0.0f; t[1] = lin(e.peak[0]); t[2] = lin(e.peak[1]); t[3] = lin(e.tpPeak[0]); t[4] = lin(e.tpPeak[1]);
        const float v[] = { o.momentary.load(), o.shortTerm.load(), o.integrated.load(), o.range.load(), o.maxMomentary.load(),
            o.maxShortTerm.load(), o.truePeak.load(), o.shortTP.load(), o.plr.load(), o.psr.load(), o.rmsDb[0].load(), o.rmsDb[1].load(),
            o.correlation.load(), o.balance.load(), o.elapsed.load(), o.silent.load() ? 1.0f : 0.0f,
            (float)e.resetCount.load(), e.resetHighWater(), (float)e.histStart.load() };
        for (int k = 0; k < 19; ++k) t[5 + k] = v[k];
        return 24;
    }
    int numRings() const override { return 4; }
    const pump::ScopeRing* ring(int k) const override { return k == 0 ? &engine.spectrum : k == 1 ? &engine.histM : k == 2 ? &engine.histS : k == 3 ? &engine.histI : nullptr; }
};

inline HostBase* make(int type, double sr)
{
    switch (type)
    {
    case kEQ: return new HostEQ(sr);
    case kComp: return new HostComp(sr);
    case kLimit: return new HostLimit(sr);
    case kCalm: return new HostCalm(sr);
    case kTone: return new HostTone(sr);
    case kBand: return new HostBand(sr);
    case kWide: return new HostWide(sr);
    case kHeat: return new HostHeat(sr);
    case kMeter: return new HostMeter(sr);
    default: return nullptr;
    }
}
inline int telLen(int type)
{
    switch (type)
    {
    case kEQ: return 30;
    case kComp: return 11;
    case kLimit: return 22;
    case kCalm: return 8 + 2 * pump::calm::Engine::kUiPoints;
    case kTone: return 7 + 5 * pump::tone::Engine::kBands;
    case kBand: return 21;
    case kWide: return 18;
    case kHeat: return 21;
    case kMeter: return 24;
    default: return 0;
    }
}

pumpwasm_handles::HandleTable<HostBase> slots;
float stage[kStage];
const bool stageCleared = [] { for (float& v : stage) v = std::numeric_limits<float>::quiet_NaN(); return true; }();   // unwritten = default
float io[4][kMaxBlock];
float tel[512];
float ringBuf[1 << 15];

inline HostBase* at(int h) { return slots.at(h); }

} // namespace pumpwasm

using namespace pumpwasm;

PUMP_API int pump_types() { return kNumTypes; }
PUMP_API int pump_nparams(int type) { return type >= 0 && type < kNumTypes ? (int)specsOf(type).size() : 0; }
PUMP_API float* pump_stage() { return stage; }
PUMP_API int pump_new(int type, double sr)
{
    if (type < 0 || type >= kNumTypes || !(sr > 1000.0 && sr <= 768000.0)) return -1;
    HostBase* s = make(type, sr);
    if (!s) return -1;
    // a host restores the state, then activates
    for (int i = 0; i < s->numParams && i < kStage; ++i) if (stage[i] == stage[i]) s->requested[(size_t)i] = stage[i];
    for (float& v : stage) v = std::numeric_limits<float>::quiet_NaN();   // unwritten = default, for the next one too
    s->commit();
    s->activate();
    return slots.add(s);
}
PUMP_API void pump_free(int h) { slots.remove(h); }
PUMP_API float* pump_values(int h) { HostBase* s = at(h); return s ? s->requested.data() : nullptr; }
PUMP_API void pump_commit(int h) { if (HostBase* s = at(h)) s->commit(); }
PUMP_API float* pump_io(int ch) { return io[ch < 0 ? 0 : (ch > 3 ? 3 : ch)]; }
PUMP_API void pump_process(int h, int n, int sc)
{
    HostBase* s = at(h);
    if (!s || n <= 0) return;
    n = n > kMaxBlock ? kMaxBlock : n;
    s->scConnected = sc != 0;
    const float* in[4] = { io[0], io[1], sc ? io[2] : nullptr, sc ? io[3] : nullptr };
    float* out[2] = { io[0], io[1] };
    s->run(in, out, (uint32_t)n);
}
PUMP_API int pump_latency(int h) { HostBase* s = at(h); return s ? s->latencyNow() : 0; }
PUMP_API float* pump_tel(int h) { HostBase* s = at(h); if (s) s->telemetry(tel); return tel; }
PUMP_API int pump_tel_len(int type) { return telLen(type); }
PUMP_API int pump_rings(int h) { HostBase* s = at(h); return s ? s->numRings() : 0; }
PUMP_API unsigned pump_ring_pos(int h, int k) { HostBase* s = at(h); const pump::ScopeRing* r = s ? s->ring(k) : nullptr; return r ? r->position() : 0u; }
PUMP_API int pump_ring_cap(int h, int k) { HostBase* s = at(h); const pump::ScopeRing* r = s ? s->ring(k) : nullptr; return r ? r->capacity() : 0; }
PUMP_API float* pump_ring_read(int h, int k, unsigned from, int count)
{
    HostBase* s = at(h);
    const pump::ScopeRing* r = s ? s->ring(k) : nullptr;
    count = count < 0 ? 0 : (count > (1 << 15) ? (1 << 15) : count);
    for (int i = 0; i < count; ++i) ringBuf[i] = r ? r->at(from + (unsigned)i) : 0.0f;
    return ringBuf;
}
PUMP_API void pump_hold(int h, int kind, float a, float b, float c, float d) { if (HostBase* s = at(h)) s->hold(kind, a, b, c, d); }
