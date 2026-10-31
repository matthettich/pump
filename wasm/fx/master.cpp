// pump MASTER in pump.wasm: plugins/master/PluginMaster.hpp itself, on the DPF stand-in (wasm/shim). See Runner.hpp.
// Its one try / catch (reading a saved analysis; parseAnalysis itself never throws) builds without exceptions here: the
// headers it uses are included first, then `try` / `catch` stand for plain blocks while the plugin header is read.
#define PUMP_SHIM_NS pumpshim_master
#include "PumpPlugin.hpp"
#include "MasterParams.hpp"
#include "MasterEngine.hpp"
#include "dsp/Bypass.hpp"
#include <mutex>
#define try if (true)
#define catch(...) if (false)
#include "PluginMaster.hpp"
#undef try
#undef catch
#include "Runner.hpp"

namespace pumpshim_master {
// The window (Thunder Plus's pump/ui/master.js). Command (pump_hold kind 3) a = 1 LEARN, 2 finish, 3 cancel.
// Feeds after the common five (t[5 + i]): 0 learning, 1 learned (an analysis in use), 2 its LUFS, 3 seconds learned so far,
// 4 plan valid, 5 analysis seconds, 6 its LRA, 7 its true peak; 8..103 the tone curve (dB, 96 points 20 Hz..20 kHz),
// 104..199 the free bands' curve, 200..230 target, 231..261 measured (31 third octaves), 262..270 tone EQ gains,
// 271..279 auto EQ gains (9 filters), then 280.. ratio, thr, liftMax, lift, sideLo, sideHi, makeup, outLufs (planned),
// compare, peakOver, limitLoss, eqDelta, inST, outST, autoRatio, autoThr, autoLift, autoMakeup, autoSideLo, autoSideHi,
// compGR, limGR (max since the last read), outI. Rings: 0 input, 1 output (mono sums: the spectra).
template <> struct RunnerExtra<PluginMaster> {
    static constexpr int kTel = 303;
    static void command(PluginMaster& p, int c, float, float) { if (c == 1) p.learnStart(); else if (c == 2) p.learnFinish(); else if (c == 3) p.learnCancel(); }
    static int status(PluginMaster& p, float* t)
    {
        using namespace pump::master;
        Engine& e = p.engine;
        Analysis a; bool got = false;
        for (int i = 0; i < 8 && !got; ++i) got = e.published.load(a);
        const bool ok = got && a.valid;
        const auto L = [](const std::atomic<float>& v) { return v.load(std::memory_order_relaxed); };
        t[0] = e.uiLearning.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        t[1] = ok ? 1.0f : 0.0f; t[2] = ok ? a.lufs : -200.0f; t[3] = L(e.uiLearnSeconds); t[4] = e.uiPlanValid.load() ? 1.0f : 0.0f;
        t[5] = ok ? a.seconds : 0.0f; t[6] = ok ? a.lra : 0.0f; t[7] = ok ? a.truePeak : -200.0f;
        for (int i = 0; i < kCurvePts; ++i) { t[8 + i] = L(e.uiCurve[i]); t[104 + i] = L(e.uiBandsCurve[i]); }
        for (int b = 0; b < kBands; ++b) { t[200 + b] = L(e.uiTarget[b]); t[231 + b] = L(e.uiMeasured[b]); }
        for (int i = 0; i < kEq; ++i) { t[262 + i] = L(e.uiEqGain[i]); t[271 + i] = L(e.uiAutoEq[i]); }
        const float v[] = { L(e.uiRatio), L(e.uiThr), L(e.uiLiftMax), L(e.uiLift), L(e.uiSideLoDb), L(e.uiSideHiDb), L(e.uiMakeup), L(e.uiOutLufs),
            L(e.uiCompare), L(e.uiPeakOver), L(e.uiLimitLoss), L(e.uiEqDelta), L(e.uiInST), L(e.uiOutST), L(e.uiAutoRatio), L(e.uiAutoThr),
            L(e.uiAutoLift), L(e.uiAutoMakeup), L(e.uiAutoSideLo), L(e.uiAutoSideHi), e.compGR.exchange(0.0f), e.limGR.exchange(0.0f), L(e.uiOutI) };
        for (int i = 0; i < 23; ++i) t[280 + i] = v[i];
        return kTel;
    }
    static int rings(const PluginMaster&) { return 2; }
    static const pump::ScopeRing* ring(const PluginMaster& p, int k) { return k == 0 ? &p.preRing : k == 1 ? &p.postRing : nullptr; }
};
} // namespace pumpshim_master

PUMP_RUNNER(master, PluginMaster)
