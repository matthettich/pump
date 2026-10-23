// pump METER engine: loudness (BS.1770-4 / R128: momentary, short-term, integrated, LRA, maxima), true peak,
// PLR / PSR, L/R sample peak and RMS, correlation, balance, a 10 Hz loudness history and a spectrum feed.
// Pure analysis: process() never touches the audio. Framework-free.
#pragma once

#include "dsp/Util.hpp"
#include "dsp/Loudness.hpp"
#include "dsp/Oversampler.hpp"
#include <atomic>

namespace pump { namespace meter {

struct Readout {
    std::atomic<float> momentary { -INFINITY }, shortTerm { -INFINITY }, integrated { -INFINITY }, range { -INFINITY },
        maxMomentary { -INFINITY }, maxShortTerm { -INFINITY }, truePeak { -INFINITY }, shortTP { -INFINITY },
        plr { -INFINITY }, psr { -INFINITY }, rmsDb[2] { { -INFINITY }, { -INFINITY } }, correlation { 0.0f },
        balance { 0.0f }, elapsed { 0.0f };
    std::atomic<bool> silent { true };
};

class Engine {
public:
    static constexpr int kHistRate = 10;           // history points per second (one per 100 ms sub-block)

    // ---- UI feeds ----
    ScopeRing spectrum { 1 << 15 };
    ScopeRing histM { 1 << 15 }, histS { 1 << 15 }, histI { 1 << 15 };   // 32768 points = 54 min
    std::atomic<uint32_t> histStart { 0 };         // history ring position at the last reset
    MeterValue peak[2], tpPeak[2];                 // linear, max-hold since the UI last exchanged them
    std::atomic<uint32_t> resetCount { 0 };        // incremented by every reset (UI clears its holds)
    Readout out;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        meter.prepare(sr);
        sub = std::max(1, (int)std::lround(sr * 0.1));   // same sub-block as LoudnessMeter
        cRms = (float)tauCoef(0.3, sr);
        cCorr = (float)tauCoef(0.3, sr);
        reset();
    }

    // The reset counter parameter: resets when it rises above the highest value seen.
    void setResetCounter(float v)
    {
        if (v > resetHigh.load(std::memory_order_relaxed)) { resetHigh.store(v, std::memory_order_relaxed); reset(); }
    }
    // The UI's next reset value must be above this (undo / A/B can bring older, lower values back).
    float resetHighWater() const { return resetHigh.load(std::memory_order_relaxed); }

    void reset()
    {
        meter.reset();
        for (auto& t : tpd) t.reset();
        std::fill(std::begin(tpRing), std::end(tpRing), 0.0f);
        tpRingPos = 0;
        subCount = 0; subTP = 0.0f;
        samplePeak = 0.0f;
        samples = 0;
        histStart.store(histM.position(), std::memory_order_release);
        resetCount.fetch_add(1, std::memory_order_acq_rel);
        publish();
    }

    // Analysis only: the buffers are read, never written.
    void process(const float* L, const float* R, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int k = std::min(n - done, sub - subCount);
            analyse(L + done, R + done, k);
            meter.process(L + done, R + done, k);
            done += k;
            subCount += k;
            samples += (uint64_t)k;
            if (subCount >= sub) { subCount = 0; endSubBlock(); }
        }
        // fast values every block
        out.rmsDb[0].store(10.0f * std::log10(std::max(pw[0], 1e-20f)), std::memory_order_relaxed);
        out.rmsDb[1].store(10.0f * std::log10(std::max(pw[1], 1e-20f)), std::memory_order_relaxed);
        const float e = pw[0] * pw[1];
        const bool silent = e < 1e-14f;
        out.silent.store(silent, std::memory_order_relaxed);
        out.correlation.store(silent ? 0.0f : clampv(xlr / std::sqrt(e), -1.0f, 1.0f), std::memory_order_relaxed);
        out.balance.store(silent ? 0.0f : clampv(10.0f * std::log10(std::max(pw[0], 1e-20f) / std::max(pw[1], 1e-20f)), -24.0f, 24.0f), std::memory_order_relaxed);
        out.elapsed.store((float)((double)samples / sr), std::memory_order_relaxed);
    }

    // direct access for tests
    LoudnessMeter& loudness() { return meter; }
    double sampleRate() const { return sr; }

private:
    void analyse(const float* L, const float* R, int n)
    {
        float p0 = 0.0f, p1 = 0.0f, t0 = 0.0f, t1 = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            p0 = std::max(p0, std::fabs(l)); p1 = std::max(p1, std::fabs(r));
            t0 = std::max(t0, tpd[0].process(l)); t1 = std::max(t1, tpd[1].process(r));
            pw[0] = l * l + (pw[0] - l * l) * cRms;
            pw[1] = r * r + (pw[1] - r * r) * cRms;
            xlr = l * r + (xlr - l * r) * cCorr;
            spectrum.push(0.5f * (l + r));
        }
        t0 = std::max(t0, p0); t1 = std::max(t1, p1);   // a true peak is never below the sample peak
        peak[0].setMax(p0); peak[1].setMax(p1);
        tpPeak[0].setMax(t0); tpPeak[1].setMax(t1);
        samplePeak = std::max(samplePeak, std::max(p0, p1));
        subTP = std::max(subTP, std::max(t0, t1));
    }

    void endSubBlock()
    {
        tpRing[tpRingPos] = subTP;
        tpRingPos = (tpRingPos + 1) % 30;
        subTP = 0.0f;
        publish();
        histM.push((float)meter.getMomentary());
        histS.push((float)meter.getShortTerm());
        histI.push((float)meter.getIntegrated());
    }

    void publish()
    {
        const double m = meter.getMomentary(), s = meter.getShortTerm(), i = meter.getIntegrated(), lra = meter.getRange();
        const double tp = std::max(meter.getTruePeakDb(), gainToDb(samplePeak));
        float stp = 0.0f;
        for (float v : tpRing) stp = std::max(stp, v);
        const double stpDb = gainToDb(stp);
        out.momentary.store((float)m, std::memory_order_relaxed);
        out.shortTerm.store((float)s, std::memory_order_relaxed);
        out.integrated.store((float)i, std::memory_order_relaxed);
        out.range.store((float)lra, std::memory_order_relaxed);
        out.maxMomentary.store((float)meter.getMaxMomentary(), std::memory_order_relaxed);
        out.maxShortTerm.store((float)meter.getMaxShortTerm(), std::memory_order_relaxed);
        const bool anyPeak = samplePeak > 0.0f;
        out.truePeak.store(anyPeak ? (float)tp : -INFINITY, std::memory_order_relaxed);
        out.shortTP.store(stp > 0.0f ? (float)stpDb : -INFINITY, std::memory_order_relaxed);
        // PLR = true-peak maximum - integrated loudness; PSR = true peak of the last 3 s - short-term loudness
        out.plr.store(anyPeak && std::isfinite(i) ? (float)(tp - i) : -INFINITY, std::memory_order_relaxed);
        out.psr.store(stp > 0.0f && std::isfinite(s) && s > -70.0 ? (float)(stpDb - s) : -INFINITY, std::memory_order_relaxed);
    }

    double sr = 48000.0;
    LoudnessMeter meter;
    TruePeakDetector tpd[2];
    float tpRing[30] = {};
    int tpRingPos = 0;
    int sub = 4800, subCount = 0;
    float subTP = 0.0f, samplePeak = 0.0f;
    float pw[2] = {}, xlr = 0.0f, cRms = 0.999f, cCorr = 0.999f;
    uint64_t samples = 0;
    std::atomic<float> resetHigh { 0.0f };
};

}} // namespace pump::meter
