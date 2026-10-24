// pump AMP engine: neural amp models (.nam, through the MIT-licensed NAM core) and cabinet impulse responses.
//
// Signal path:
//   input gain -> gate (threshold with 6 dB of hysteresis, fast open, smooth close) -> the model, mono (L + R) or
//   one per side (STEREO) -> normalize (the model's own loudness to -18 dB, when the file says how loud it is)
//   -> bass / mid / treble / presence -> cabinet (convolution with the impulse response; 64-sample blocks)
//   -> cab low and high cut -> output gain
// A model runs at the rate it was trained at: when the host's rate differs, a windowed-sinc resampler takes the
// signal there and back (RateBridge, a fixed latency of a few dozen samples). The latency of the whole plugin is
// that plus the cabinet's 64 samples (also with the cab off, so switching it never moves the timing).
// Models and impulse responses are loaded and prepared off the audio thread (loadModel / loadIr: file reading,
// resampling, kernel building) and handed over at a block boundary; the audio thread fades out, swaps, fades in
// (8 ms each way). Replaced models are deleted by the next load (or by the engine), never on the audio thread.
#pragma once

#include "dsp/Fx.hpp"
#include "dsp/FFT.hpp"
#include "dsp/Resampler.hpp"
#include "dsp/Wav.hpp"
#include "AmpParams.hpp"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace pump { namespace amp {

struct Settings {
    double inputDb = 0.0, gateDb = kGateOff;
    bool normalize = true;
    int channels = kMono;
    double bass = 0.0, mid = 0.0, treble = 0.0, presence = 0.0;
    bool cab = true;
    double cabLow = kLowCutOff, cabHigh = kHighCutOff, outDb = 0.0;
};

// What the window shows about the loaded files (written off the audio thread)
struct ModelInfo {
    bool loaded = false;
    std::string path, name, error;
    double rate = 0.0, loudness = 0.0;
    bool hasLoudness = false, resampled = false;
};
struct IrInfo {
    bool loaded = false;
    std::string path, name, error;
    double rate = 0.0, seconds = 0.0;
    int channels = 0;
    std::vector<float> response;   // magnitude in dB at kIrPoints log-spaced frequencies 20 Hz .. 20 kHz
};
constexpr int kIrPoints = 160;

// RBJ biquad (direct form 2 transposed), for the tone stack
struct Biquad2 {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0.0; }
    // type 0 low shelf, 1 peak, 2 high shelf
    void set(int type, double f, double q, double gainDb, double sr)
    {
        const double A = std::pow(10.0, gainDb / 40.0), w = kTwoPi * clampv(f, 10.0, 0.45 * sr) / sr, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / (2.0 * q);
        double B0, B1, B2, A0, A1, A2;
        if (type == 1) { B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; }
        else
        {
            const double sq = 2.0 * std::sqrt(A) * alpha;
            if (type == 0)
            {
                B0 = A * ((A + 1) - (A - 1) * cw + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sq);
                A0 = (A + 1) + (A - 1) * cw + sq; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sq;
            }
            else
            {
                B0 = A * ((A + 1) + (A - 1) * cw + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sq);
                A0 = (A + 1) - (A - 1) * cw + sq; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sq;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};
// the tone stack's bands (shared with the window's curve)
struct ToneBand { int type; double f, q; };
inline const ToneBand& toneBand(int k)
{
    static const ToneBand b[4] = { { 0, 110.0, 0.7 }, { 1, 700.0, 0.8 }, { 2, 2800.0, 0.7 }, { 2, 5500.0, 0.9 } };
    return b[clampv(k, 0, 3)];
}

class Engine {
public:
    static constexpr int kChunk = 256;
    static constexpr int kCabBlock = 64, kCabMaxTaps = 16384;

    ~Engine() { delete cur; delete incoming.exchange(nullptr); collect(); }

    // ---- non-real-time ----------------------------------------------------------------------------------------
    // (re)prepare for a sample rate; models / IRs must be loaded again after this (the plugin does, from its states)
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        conv.prepare(kCabBlock, kCabMaxTaps);
        cabDelay[0].setMaxDelay(kCabBlock + 4); cabDelay[1].setMaxDelay(kCabBlock + 4);
        cabDelay[0].setDelay(kCabBlock); cabDelay[1].setDelay(kCabBlock);
        const int ramp = std::max(1, (int)(0.02 * sr));
        inS.setRamp(ramp); inS.reset(dbToGain(set.inputDb));
        outS.setRamp(ramp); outS.reset(dbToGain(set.outDb));
        normS.setRamp(ramp); normS.reset(1.0);
        cabOn.setRamp(ramp); cabOn.reset(set.cab ? 1.0 : 0.0);
        lcOn.setRamp(ramp); lcOn.reset(lowCutActive(set.cabLow) ? 1.0 : 0.0);
        hcOn.setRamp(ramp); hcOn.reset(highCutActive(set.cabHigh) ? 1.0 : 0.0);
        trans.setRamp(std::max(1, (int)(0.008 * sr))); trans.reset(1.0);
        gateGain = 1.0; gateEnv = 0.0; gateOpen = true;
        gateAtt = 1.0 - std::exp(-1.0 / (0.0005 * sr)); gateRel = 1.0 - std::exp(-1.0 / (0.050 * sr));
        envRel = 1.0 - std::exp(-1.0 / (0.020 * sr));
        for (auto& c : tone) for (auto& b : c) b.reset();
        for (int ch = 0; ch < 2; ++ch) { lc[ch].reset(); hc[ch].reset(); }
        lastTone[0] = 1e9;
        delete cur; cur = nullptr;
        delete incoming.exchange(nullptr);
        collect();
        latencyNow.store(kCabBlock);
    }

    // Load a .nam file. Returns the info (error filled when it failed; the current model then stays).
    ModelInfo loadModel(const std::string& path)
    {
        ModelInfo info;
        info.path = path;
        info.name = std::filesystem::path(path).stem().string();
        auto slot = std::make_unique<Slot>();
        try
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                nam::DspLoadOptions opt; opt.prewarm = false;
                slot->dsp[ch] = nam::get_dsp(std::filesystem::path(path), opt);
                if (!slot->dsp[ch]) throw std::runtime_error("not a model");
            }
        }
        catch (const std::exception& e) { info.error = std::string("couldn't load: ") + e.what(); return info; }
        catch (...) { info.error = "couldn't load the model"; return info; }
        nam::DSP& d = *slot->dsp[0];
        const double want = d.GetExpectedSampleRate() > 0.0 ? d.GetExpectedSampleRate() : 48000.0;
        const double inner = d.SupportsArbitrarySampleRate() ? sr : want;
        slot->hasLoudness = d.HasLoudness();
        slot->loudness = slot->hasLoudness ? d.GetLoudness() : 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            slot->bridge[ch].prepare(sr, inner, kChunk);
            const int maxInner = std::max(kChunk, slot->bridge[ch].maxInner());
            slot->dsp[ch]->ResetAndPrewarm(inner, maxInner);
            slot->bufIn[ch].assign((size_t)maxInner, 0.0f);
            slot->bufOut[ch].assign((size_t)maxInner, 0.0f);
        }
        slot->latency = slot->bridge[0].latency();
        info.loaded = true;
        info.rate = want;
        info.hasLoudness = slot->hasLoudness;
        info.loudness = slot->loudness;
        info.resampled = std::fabs(inner - sr) > 0.5;
        collect();
        delete incoming.exchange(slot.release());   // (a load the audio thread never took is simply replaced)
        return info;
    }
    void clearModel()
    {
        collect();
        auto* empty = new Slot();
        empty->empty = true;
        delete incoming.exchange(empty);
    }

    // Load a cabinet impulse response (.wav, mono or stereo). Returns the info (error filled when it failed).
    IrInfo loadIr(const std::string& path)
    {
        IrInfo info;
        info.path = path;
        info.name = std::filesystem::path(path).stem().string();
        WavData w = readWav(path, (size_t)1 << 20);
        if (!w.ok()) { info.error = w.error.empty() ? "empty file" : w.error; return info; }
        info.rate = w.sampleRate;
        info.channels = (int)w.channels.size();
        info.seconds = w.channels[0].size() / w.sampleRate;
        std::vector<float> irL = resampleOffline(w.channels[0], w.sampleRate, sr);
        std::vector<float> irR = w.channels.size() > 1 ? resampleOffline(w.channels[1], w.sampleRate, sr) : irL;
        const int taps = (int)std::min<size_t>(irL.size(), (size_t)kCabMaxTaps);
        if (taps <= 0) { info.error = "empty file"; return info; }
        irL.resize((size_t)taps); irR.resize((size_t)taps);
        // energy normalisation: the same broadband level from every IR
        double e = 0; for (int i = 0; i < taps; ++i) e += 0.5 * ((double)irL[(size_t)i] * irL[(size_t)i] + (double)irR[(size_t)i] * irR[(size_t)i]);
        const float g = e > 1e-12 ? (float)(1.0 / std::sqrt(e)) : 1.0f;
        for (int i = 0; i < taps; ++i) { irL[(size_t)i] *= g; irR[(size_t)i] *= g; }
        const float* k4[4] = { irL.data(), nullptr, nullptr, irR.data() };
        conv.setKernel(conv.makeKernel(k4, taps));
        irLoaded.store(true);
        // the response for the window
        info.response.resize(kIrPoints);
        for (int p = 0; p < kIrPoints; ++p)
        {
            const double f = 20.0 * std::pow(1000.0, (p + 0.5) / kIrPoints), w2 = kTwoPi * f / sr;
            double re = 0, im = 0;
            for (int i = 0; i < taps; ++i) { re += irL[(size_t)i] * std::cos(w2 * i); im -= irL[(size_t)i] * std::sin(w2 * i); }
            info.response[(size_t)p] = (float)(10.0 * std::log10(re * re + im * im + 1e-12));
        }
        info.loaded = true;
        return info;
    }
    void clearIr()
    {
        const float one = 1.0f;
        const float* k4[4] = { &one, nullptr, nullptr, &one };
        conv.setKernel(conv.makeKernel(k4, 1));
        irLoaded.store(false);
    }

    // Take a loaded model in at once (only while the audio thread isn't running: activation)
    void installNow()
    {
        Slot* s = incoming.exchange(nullptr);
        if (!s) return;
        if (s->empty) { delete s; s = nullptr; }
        delete cur;
        cur = s;
        modelOn.store(cur != nullptr);
        latencyNow.store((cur ? cur->latency : 0) + kCabBlock);
        trans.reset(1.0);
    }

    // ---- audio thread -------------------------------------------------------------------------------------------
    void setSettings(const Settings& s) { set = s; set.channels = clampv(s.channels, 0, 1); }
    int latency() const { return latencyNow.load(std::memory_order_relaxed); }
    bool hasModel() const { return modelOn.load(std::memory_order_relaxed); }
    bool irOn() const { return irLoaded.load(std::memory_order_relaxed); }

    // feeds (UI)
    MeterValue inPeak, gateFeed;                 // input peak after the input gain; gate gain (min hold)
    ScopeRing outRing { 1 << 15 };               // the output (mono sum), for the spectrum

    void process(float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    struct Slot {
        std::unique_ptr<nam::DSP> dsp[2];
        RateBridge bridge[2];
        std::vector<float> bufIn[2], bufOut[2];
        double loudness = 0.0;
        bool hasLoudness = false, empty = false;
        int latency = 0;
    };

    // deletes the models the audio thread has finished with (non-real-time only)
    void collect()
    {
        for (auto& r : retired) delete r.exchange(nullptr);
        delete overflow.exchange(nullptr);
    }
    void retire(Slot* s)
    {
        if (!s) return;
        for (auto& r : retired) { Slot* expected = nullptr; if (r.compare_exchange_strong(expected, s)) return; }
        // all slots taken (several loads with no collect in between): keep it until the engine goes
        Slot* expected = nullptr;
        if (!overflow.compare_exchange_strong(expected, s)) { /* leak rather than free on the audio thread */ }
    }

    void updateTone()
    {
        const double g[4] = { set.bass, set.mid, set.treble, set.presence };
        if (g[0] == lastTone[0] && g[1] == lastTone[1] && g[2] == lastTone[2] && g[3] == lastTone[3]) return;
        for (int k = 0; k < 4; ++k)
        {
            lastTone[k] = g[k];
            const ToneBand& b = toneBand(k);
            for (int ch = 0; ch < 2; ++ch)
            {
                // keep the state, change the coefficients (no click on small moves)
                const double z1 = tone[ch][k].z1, z2 = tone[ch][k].z2;
                tone[ch][k].set(b.type, b.f, b.q, g[k], sr);
                tone[ch][k].z1 = z1; tone[ch][k].z2 = z2;
            }
        }
    }

    void chunk(float* L, float* R, int n)
    {
        // a new model: fade out, swap, fade back in
        if (incoming.load(std::memory_order_acquire) != nullptr)
        {
            trans.setTarget(0.0);
            if (!trans.isSmoothing() && trans.current() <= 0.0)
            {
                Slot* s = incoming.exchange(nullptr, std::memory_order_acq_rel);
                if (s && s->empty) { retire(s); s = nullptr; }
                retire(cur);
                cur = s;
                modelOn.store(cur != nullptr, std::memory_order_relaxed);
                latencyNow.store((cur ? cur->latency : 0) + kCabBlock, std::memory_order_relaxed);
                trans.setTarget(1.0);
            }
        }
        else trans.setTarget(1.0);

        inS.setTarget(dbToGain(set.inputDb));
        outS.setTarget(dbToGain(set.outDb));
        normS.setTarget(set.normalize && cur && cur->hasLoudness ? dbToGain(kTargetLoudness - cur->loudness) : 1.0);
        cabOn.setTarget(set.cab ? 1.0 : 0.0);
        lcOn.setTarget(lowCutActive(set.cabLow) ? 1.0 : 0.0);
        hcOn.setTarget(highCutActive(set.cabHigh) ? 1.0 : 0.0);
        updateTone();
        for (int ch = 0; ch < 2; ++ch)
        {
            lc[ch].set(std::max(10.0, set.cabLow), 0.70710678, sr);
            hc[ch].set(std::min(set.cabHigh, 0.45 * sr), 0.70710678, sr);
        }

        // input gain and gate (stereo-linked)
        const bool gateOnNow = gateActive(set.gateDb);
        const double thrOpen = dbToGain(set.gateDb), thrClose = dbToGain(set.gateDb - 6.0);
        float pk = 0.0f;
        double gmin = 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double g = inS.next();
            const double l = L[i] * g, r = R[i] * g;
            const double a = std::max(std::fabs(l), std::fabs(r));
            pk = std::max(pk, (float)a);
            gateEnv = a > gateEnv ? a : gateEnv + (a - gateEnv) * envRel;
            if (!gateOnNow) gateOpen = true;
            else if (gateOpen && gateEnv < thrClose) gateOpen = false;
            else if (!gateOpen && gateEnv > thrOpen) gateOpen = true;
            const double tgt = gateOpen ? 1.0 : 0.0;
            gateGain += (tgt - gateGain) * (tgt > gateGain ? gateAtt : gateRel);
            gmin = std::min(gmin, gateGain);
            L[i] = (float)(l * gateGain); R[i] = (float)(r * gateGain);
        }
        inPeak.setMax(pk);
        gateFeed.setMin((float)gmin);

        // the model(s)
        if (cur)
        {
            const bool stereo = set.channels == kStereo;
            float mono[kChunk];
            if (!stereo) for (int i = 0; i < n; ++i) mono[i] = 0.5f * (L[i] + R[i]);
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            {
                Slot& s = *cur;
                nam::DSP& d = *s.dsp[ch];
                float* io = stereo ? (ch ? R : L) : mono;
                s.bridge[ch].process(io, io, n, [&](const float* in, float* out, int m) {
                    float* src = s.bufIn[ch].data();
                    float* dst = s.bufOut[ch].data();
                    std::memcpy(src, in, sizeof(float) * (size_t)m);
                    NAM_SAMPLE* ip[1] = { src };
                    NAM_SAMPLE* op[1] = { dst };
                    d.process(ip, op, m);
                    std::memcpy(out, dst, sizeof(float) * (size_t)m);
                });
            }
            if (!stereo) for (int i = 0; i < n; ++i) L[i] = R[i] = mono[i];
        }

        // normalize, tone, cabinet
        for (int i = 0; i < n; ++i)
        {
            const double g = normS.next();
            double x[2] = { L[i] * g, R[i] * g };
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 4; ++k) x[ch] = tone[ch][k].process(x[ch]);
            L[i] = (float)x[0]; R[i] = (float)x[1];
        }
        float dl[kChunk], dr[kChunk];
        for (int i = 0; i < n; ++i) { dl[i] = cabDelay[0].process(L[i]); dr[i] = cabDelay[1].process(R[i]); }
        conv.process(L, R, n);
        for (int i = 0; i < n; ++i)
        {
            const double cm = cabOn.next(), lcm = lcOn.next(), hcm = hcOn.next(), og = outS.next(), tr = trans.next();
            double y[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                double v = (ch ? R : L)[i];
                const double dly = ch ? dr[i] : dl[i];
                v = dly + (v - dly) * cm;
                const double h = lc[ch].highpass(v);
                v = v + (h - v) * lcm * cm;
                const double l2 = hc[ch].lowpass(v);
                v = v + (l2 - v) * hcm * cm;
                y[ch] = v * og * tr;
            }
            L[i] = (float)y[0]; R[i] = (float)y[1];
            outRing.push(0.5f * (L[i] + R[i]));
        }
        double chk = gateEnv + gateGain;
        for (int i = 0; i < n; ++i) chk += L[i] + R[i];
        if (!std::isfinite(chk))
        {
            gateEnv = 0.0; gateGain = 1.0;
            for (auto& c : tone) for (auto& b : c) b.reset();
            for (int ch = 0; ch < 2; ++ch) { lc[ch].reset(); hc[ch].reset(); }
            conv.reset();
            if (cur) for (int ch = 0; ch < 2; ++ch) { cur->bridge[ch].reset(); }
            sanitizeBlock(L, n); sanitizeBlock(R, n);
        }
    }

    double sr = 48000.0;
    Settings set;
    Slot* cur = nullptr;
    std::atomic<Slot*> incoming { nullptr }, retired[4] { { nullptr }, { nullptr }, { nullptr }, { nullptr } }, overflow { nullptr };
    std::atomic<bool> modelOn { false }, irLoaded { false };
    std::atomic<int> latencyNow { kCabBlock };
    StereoConvolver conv;
    DelayLine cabDelay[2];
    Biquad2 tone[2][4];
    double lastTone[4] = { 1e9, 1e9, 1e9, 1e9 };
    fx::Svf lc[2], hc[2];
    double gateGain = 1.0, gateEnv = 0.0, gateAtt = 0.1, gateRel = 0.001, envRel = 0.001;
    bool gateOpen = true;
    LinearSmoother inS, outS, normS, cabOn, lcOn, hcOn, trans;
};

}} // namespace pump::amp
