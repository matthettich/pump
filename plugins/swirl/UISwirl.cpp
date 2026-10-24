// pump SWIRL: interface. Three screens: MOTION (the swept delay or frequency over one LFO cycle, left and right,
// with where each side is now; drag it: left/right = rate, up/down = depth), RESPONSE (the frequency response the
// effect has at this instant: the comb of a chorus or flanger, the notches of a phaser, moving as it sweeps) and
// STEREO (a goniometer of the output with its correlation). Below: the type and its variant, rate / sync, depth,
// manual, feedback; spread, tone, warmth; mix and output.
#include "ui/PumpUI.hpp"
#include "PluginSwirl.hpp"
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::swirl;
using pump::kNoteNames;
using pump::kNumNotes;

// Type / variant keys: a click also loads the classic settings for it; Shift-click changes only the type / variant.
struct ClassicKeys : Segmented {
    std::function<void(int, bool)> pick;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override
    {
        if (button != kMouseButtonLeft) return false;
        const int n = count(ui);
        const int i = std::min(n - 1, std::max(0, (int)((x - r.x) / r.w * n)));
        if (pick) pick(i, (mod & kModifierShift) != 0);
        return true;
    }
};

class UISwirl : public PumpUI {
public:
    UISwirl() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::swirl(), "SWIRL", 1000, 620, kBypass)
    {
        modeSeg = add(new ClassicKeys());
        modeSeg->param = kMode;
        for (const char* k : kModeKeys) modeSeg->options.push_back(k);
        modeSeg->pick = [this](int i, bool only) { pick(i, (int)value(kVariant), only); };
        modeSeg->tip = "Type. Click = with its classic rate, depth, manual, feedback and mix; Shift-click = type only   (keys 1-6)";
        varSeg = add(new ClassicKeys());
        varSeg->param = kVariant;
        for (int i = 0; i < 4; ++i) varSeg->options.push_back(variantKey(kAnalog, i));
        varSeg->pick = [this](int i, bool only) { pick((int)value(kMode), i, only); };
        varSeg->tip = "Variant of the type. Click = with its classic settings; Shift-click = variant only";

        rateDial = addDial(kRate, "RATE");
        rateDial->tip = "LFO rate (with SYNC on, the division sets it)";
        depthDial = addDial(kDepth, "DEPTH");
        depthDial->tip = "Depth of the sweep";
        manDial = addDial(kManual, "MANUAL");
        manDial->tip = "Manual: the centre of the sweep (delay time, or frequency for the phasers)";
        fbDial = addDial(kFeedback, "FEEDBACK", Dial::Medium, true);
        fbDial->tip = "Feedback: positive or negative (stronger combs, resonant notches)";
        rateDial->tabGroup = depthDial->tabGroup = manDial->tabGroup = fbDial->tabGroup = 1;

        syncBtn = addToggle(kSync, "SYNC");
        syncBtn->small = true;
        syncBtn->tip = "Lock the LFO to the host tempo and song position";
        divMenu = addMenu(kDivision);
        divMenu->arrows = true;
        divMenu->tip = "LFO cycle length when synced";
        spreadDial = addDial(kSpread, "SPREAD");
        spreadDial->tip = "Stereo: how far apart the left and right LFOs run (0 = mono sweep, 180 = opposite)";
        toneDial = addDial(kTone, "TONE");
        toneDial->tip = "High cut on the wet signal";
        warmDial = addDial(kWarmth, "WARMTH");
        warmDial->tip = "Warmth: bucket-brigade bandwidth, saturation and clock hiss (gentle saturation on the other types)";
        spreadDial->tabGroup = toneDial->tabGroup = warmDial->tabGroup = 2;

        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix. Chorus and flanger: 50% = dry and wet at full level, 100% = wet only (vibrato). Phasers: 50% = deepest notches";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        if (auto* p = static_cast<PluginSwirl*>(pluginPtr())) plug = p;
    }

protected:
    void pick(int mode, int var, bool only)
    {
        mode = pump::clampv(mode, 0, kNumModes - 1); var = pump::clampv(var, 0, 3);
        beginGroup();
        setParamGesture(kMode, (float)mode);
        setParamGesture(kVariant, (float)var);
        if (!only)
        {
            const Classic c = classic(mode, var);
            setParamGesture(kRate, c.rate); setParamGesture(kDepth, c.depth); setParamGesture(kManual, c.manual);
            setParamGesture(kFeedback, c.feedback); setParamGesture(kMix, c.mix);
        }
        endGroup();
    }
    int mode() const { return pump::clampv((int)value(kMode), 0, kNumModes - 1); }
    int var() const { return pump::clampv((int)value(kVariant), 0, 3); }

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kSpread: std::snprintf(b, sizeof b, "%.0f DEG", v); return b;
        case kTone: if (!toneActive(v)) return "OFF"; break;
        case kManual:
        {
            const double m = v * 0.01;
            const int md = mode();
            if (md == kPhaser) return hz(phaserHz(m, 0.0, 0.0));
            if (md == kLamp) return hz(lampHz(m, 0.0, 0.3));
            std::snprintf(b, sizeof b, "%.2f ms", isThroughZero(md, var()) ? kTzMs + (m - 0.5) * kTzMs : centreMs(md, m));
            return b;
        }
        case kFeedback:
            if (std::fabs(v) < 0.5f) return "0%";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        case kVariant: return variantKey(mode(), (int)v);
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }
    static std::string hz(double f)
    {
        char b[24];
        if (f >= 1000.0) std::snprintf(b, sizeof b, "%.2f kHz", f / 1000.0); else std::snprintf(b, sizeof b, "%.0f Hz", f);
        return b;
    }

    void layout() override
    {
        motionR = Rect(14, 58, 300, 322);
        respR = Rect(330, 58, 392, 322);
        stereoR = Rect(738, 58, 210, 322);
        meterL = Rect(962, 58, 9, 304);
        meterR = Rect(973, 58, 9, 304);
        const float y0 = 396;
        typePanel = Rect(14, y0, 440, 192);
        modeSeg->r = Rect(26, y0 + 28, 416, 26);
        varSeg->r = Rect(26, y0 + 62, 416, 22);
        rateDial->r = Rect(30, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        depthDial->r = Rect(136, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        manDial->r = Rect(242, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        fbDial->r = Rect(348, y0 + 100, 72, Dial::heightFor(Dial::Medium));

        stereoPanel = Rect(468, y0, 308, 192);
        syncBtn->r = Rect(480, y0 + 30, 70, 24);
        divMenu->r = Rect(558, y0 + 30, 206, 24);
        spreadDial->r = Rect(486, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        toneDial->r = Rect(586, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        warmDial->r = Rect(686, y0 + 100, 72, Dial::heightFor(Dial::Medium));

        outPanel = Rect(790, y0, 196, 192);
        mixDial->r = Rect(802, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(902, y0 + 100, 72, Dial::heightFor(Dial::Medium));
    }

    void tick(float dt) override
    {
        for (int i = 0; i < 4; ++i) varSeg->options[(size_t)i] = variantKey(mode(), i);
        const bool sync = value(kSync) >= 0.5f;
        rateDial->enabled = !sync;
        divMenu->enabled = sync;
        fbDial->enabled = mode() != kEnsemble;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        phaseShow = plug->engine.lfoPhase();
        light[0] = plug->engine.lightNow(0); light[1] = plug->engine.lightNow(1);
        const uint32_t end = plug->outL.smoothPosition();
        for (int i = 0; i < kGonioN; ++i)
        {
            const uint32_t idx = end - (uint32_t)kGonioN + (uint32_t)i;
            gl[i] = plug->outL.at(idx); gr[i] = plug->outR.at(idx);
        }
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < kGonioN; ++i) { lr += (double)gl[i] * gr[i]; ll += (double)gl[i] * gl[i]; rr += (double)gr[i] * gr[i]; }
        const float c = (ll > 1e-9 && rr > 1e-9) ? (float)(lr / std::sqrt(ll * rr)) : 1.0f;
        corr += (c - corr) * std::min(1.0f, dt * 6.0f);
    }

    // ---- the swept quantity for an LFO phase, as the engine computes it ----
    bool isAllpass() const { return mode() == kPhaser || mode() == kLamp; }
    double lfoAt(int md, double p) const
    {
        if (md == kAnalog && var() == 3) return sineShape(p);
        if (md == kEnsemble || md == kAnalog || md == kFlanger) return roundTri(p);
        return sineShape(p);
    }
    // delay (ms) or frequency (Hz) for a side at phase p; the Lamp uses its photocell (the static, lag-free curve
    // when drawing a cycle, the live value when `liveLight` >= 0)
    double sweptAt(double p, double liveLight = -1.0) const
    {
        const int md = mode(), v = var();
        const double m = value(kManual) * 0.01, d = value(kDepth) * 0.01;
        if (md == kPhaser) return phaserHz(m, d, sineShape(p));
        if (md == kLamp)
        {
            double l = liveLight;
            if (l < 0.0) { const double on = 0.5 + 0.5 * sineShape(p); l = on * on; }
            return lampHz(m, v == 2 ? std::min(1.0, d * 1.25) : d, l);
        }
        return delayMs(md, v, m, d, lfoAt(md, p));
    }
    double sidePhase(int ch) const
    {
        if (ch == 0) return phaseShow;
        return phaseShow + (mode() == kEnsemble ? 0.5 : value(kSpread) / 360.0);
    }

    void drawBody() override
    {
        drawMotion();
        drawResponse();
        drawStereo();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, motionR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(typePanel, theme.dim, 1.0f);
        strokeRect(stereoPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(typePanel.x + 12, typePanel.y + 16, "TYPE");
        caption(stereoPanel.x + 12, stereoPanel.y + 16, "TEMPO");
        caption(stereoPanel.x + 12, stereoPanel.y + 76, "STEREO AND TONE");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        char t[48];
        const double cyc = value(kSync) >= 0.5f ? 0.0 : 1.0 / std::max(0.02f, value(kRate));
        if (cyc > 0.0) { std::snprintf(t, sizeof t, cyc >= 1.0 ? "CYCLE %.2f s" : "CYCLE %.0f ms", cyc >= 1.0 ? cyc : 1000.0 * cyc); }
        else std::snprintf(t, sizeof t, "CYCLE %s (HOST)", kNoteNames[pump::clampv((int)value(kDivision), 0, kNumNotes - 1)]);
        textAt(outPanel.x + 12, outPanel.y + 40, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "LATENCY %d", plug ? plug->latencyNow() : 0);
        textAt(outPanel.x + 12, outPanel.y + 58, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    Rect motionPlot() const { return Rect(motionR.x + 44, motionR.y + 30, motionR.w - 56, motionR.h - 60); }
    void drawMotion()
    {
        const Rect& r = motionR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "MOTION");
        std::string head = kModeNames[mode()]; head += "  "; head += variantKey(mode(), var());
        textAt(r.r() - 10, r.y + 14, head, kLabel, 10.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p = motionPlot();
        // range: the whole sweep a full DEPTH could reach at this MANUAL (so DEPTH visibly widens the trace)
        constexpr int M = 160;
        double lo = 1e9, hi = -1e9;
        double vals[2][M + 1];
        const bool logY = isAllpass();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i <= M; ++i)
            {
                const double ph = (double)i / M + (ch ? sidePhase(1) - sidePhase(0) : 0.0);
                const double v = sweptAt(ph);
                vals[ch][i] = logY ? std::log2(std::max(1.0, v)) : v;
                lo = std::min(lo, vals[ch][i]); hi = std::max(hi, vals[ch][i]);
            }
        const double pad = std::max(logY ? 0.5 : 0.25, 0.15 * (hi - lo));
        lo -= pad; hi += pad;
        if (!logY) lo = std::max(0.0, lo);
        auto yOf = [&](double v) { return (float)(p.b() - (v - lo) / (hi - lo) * p.h); };
        for (int k = 0; k <= 4; ++k)
        {
            const double v = lo + (hi - lo) * k / 4.0;
            const float y = yOf(v);
            line(p.x, y, p.r(), y, theme.grid, 1.0f);
            char b[16];
            if (logY) { const double f = std::exp2(v); if (f >= 1000) std::snprintf(b, sizeof b, "%.1fk", f / 1000); else std::snprintf(b, sizeof b, "%.0f", f); }
            else std::snprintf(b, sizeof b, "%.1f", v);
            textAt(p.x - 6, y, b, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        textAt(p.x - 6, p.b() + 12, logY ? "HZ" : "MS", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        if (isThroughZero(mode(), var()))
        {
            const float y = yOf(kTzMs);
            for (float x = p.x; x < p.r(); x += 6) line(x, y, x + 3, y, theme.second, 1.2f);
            textAt(p.r() - 2, y - 8, "ZERO", kLabel, 8.5f, theme.second, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        }
        save();
        scissor(p.x, p.y, p.w, p.h);
        for (int ch = 1; ch >= 0; --ch)
        {
            beginPath();
            for (int i = 0; i <= M; ++i)
            {
                const float x = p.x + p.w * i / M, y = yOf(vals[ch][i]);
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(ch ? withAlpha2(theme.second, 0.85f) : theme.ink); strokeWidth(ch ? 1.6f : 2.2f); lineJoin(ROUND); stroke();
        }
        restore();
        // where each side is now (the playheads)
        for (int ch = 1; ch >= 0; --ch)
        {
            const double xph = phaseShow;   // both sides travel along the same time axis (R's curve is drawn offset)
            const double v = mode() == kLamp ? sweptAt(0.0, light[ch]) : sweptAt(ch ? sidePhase(1) : sidePhase(0));
            const float x = p.x + p.w * (float)(xph - std::floor(xph)), y = yOf(logY ? std::log2(std::max(1.0, v)) : v);
            if (ch == 0) line(x, p.y, x, p.b(), withAlpha2(theme.ink, 0.35f), 1.0f);
            fillRect(Rect(x - 5, y - 5, 10, 10), ch ? theme.second : theme.neon);
            strokeRect(Rect(x - 5, y - 5, 10, 10), theme.ink, 1.2f);
        }
        textAt(p.x, p.b() + 12, "L", kLabel, 9.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 14, p.b() + 12, "R", kLabel, 9.5f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        const std::string s = (value(kSync) >= 0.5f ? std::string("SYNC ") + kNoteNames[pump::clampv((int)value(kDivision), 0, kNumNotes - 1)] : formatParam(kRate, value(kRate)))
            + "   DEPTH " + formatParam(kDepth, value(kDepth));
        textAt(r.r() - 10, r.b() - 10, s, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // the response at this instant (left side, for a signal in both inputs), as the engine's paths add up
    std::complex<double> response(double f) const
    {
        using C = std::complex<double>;
        const double sr = plug ? plug->getSampleRate() : 48000.0;
        const double w = pump::kTwoPi * f / sr;
        const int md = mode(), v = var();
        const double fb = pump::clampv(value(kFeedback), -95.0f, 95.0f) * 0.01, mx = value(kMix) * 0.01;
        const C z1 = std::polar(1.0, -w);
        auto delayZ = [&](double ms) { return std::polar(1.0, -w * ms * 0.001 * sr); };
        C wet, dry = 1.0;
        if (md == kPhaser || md == kLamp)
        {
            C a = 1.0;
            if (md == kPhaser)
            {
                const double c = apCoef(sweptAt(sidePhase(0)), sr);
                const C one = (c + z1) / (1.0 + c * z1);
                for (int k = 0; k < phaserStages(v); ++k) a *= one;
            }
            else
            {
                const double f0 = sweptAt(0.0, light[0]);
                for (int k = 0; k < 4; ++k) { const double c = apCoef(f0 * lampRatio(k), sr); a *= (c + z1) / (1.0 + c * z1); }
            }
            wet = a / (1.0 - fb * a);
            return dry + mx * (wet - dry);
        }
        if (md == kRack)
        {
            const int nv = rackVoices(v);
            const double m = value(kManual) * 0.01, d = value(kDepth) * 0.01;
            C s = 0.0;
            for (int k = 0; k < nv; ++k) s += delayZ(delayMs(md, v, m, d, sineShape(phaseShow + (double)k / nv)));
            wet = (s / std::sqrt((double)nv)) / (1.0 - fb * s / (double)nv);   // (the loop feeds back the average)
        }
        else if (md == kEnsemble)
        {
            const double cross = -(0.20 + 0.10 * v);
            wet = delayZ(sweptAt(sidePhase(0))) + cross * delayZ(sweptAt(sidePhase(0) + 0.5));
        }
        else
        {
            const C e = delayZ(sweptAt(sidePhase(0)));
            wet = e / (1.0 - fb * e);
            if (md == kFlanger && v == 2) wet = -wet;
        }
        if (isThroughZero(md, v)) dry = delayZ(kTzMs);
        return std::min(1.0, 2.0 * (1.0 - mx)) * dry + std::min(1.0, 2.0 * mx) * wet;
    }

    void drawResponse()
    {
        const Rect& r = respR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "RESPONSE");
        textAt(r.r() - 10, r.y + 14, "NOW", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        drawFreqGrid(p, kLo, kHi);
        const Rect g(p.x, p.y + 6, p.w, p.h - 22);
        const float top = 18.0f, bot = -30.0f;
        auto yOf = [&](float db) { return g.y + (top - pump::clampv(db, bot, top)) / (top - bot) * g.h; };
        for (float d : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f })
        {
            line(g.x, yOf(d), g.r(), yOf(d), d == 0.0f ? withAlpha2(theme.ink, 0.3f) : theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%+.0f", d);
            textAt(g.r() - 4, yOf(d) - 6, b, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        constexpr int M = 360;
        float ys[M + 1];
        for (int i = 0; i <= M; ++i)
        {
            const double f = kLo * std::pow(kHi / kLo, (double)i / M);
            ys[i] = (float)(20.0 * std::log10(std::max(1e-6, std::abs(response(f)))));
        }
        save();
        scissor(g.x, g.y, g.w, g.h);
        beginPath();
        moveTo(g.x, g.b());
        for (int i = 0; i <= M; ++i) lineTo(g.x + g.w * i / M, yOf(ys[i]));
        lineTo(g.r(), g.b());
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.22f : 0.32f));
        fill();
        beginPath();
        for (int i = 0; i <= M; ++i) { const float x = g.x + g.w * i / M, y = yOf(ys[i]); if (i == 0) moveTo(x, y); else lineTo(x, y); }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        restore();
    }

    void drawStereo()
    {
        const Rect& r = stereoR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "STEREO");
        const float s = std::min(r.w - 24, r.h - 90) * 0.5f;
        const float cx = r.cx(), cy = r.y + 30 + s;
        // the diamond (L and R axes at 45 degrees; mono is vertical)
        beginPath(); moveTo(cx, cy - s); lineTo(cx + s, cy); lineTo(cx, cy + s); lineTo(cx - s, cy); closePath();
        strokeColor(theme.grid); strokeWidth(1.0f); stroke();
        line(cx, cy - s, cx, cy + s, theme.grid, 1.0f);
        line(cx - s, cy, cx + s, cy, theme.grid, 1.0f);
        textAt(cx - s * 0.5f - 6, cy - s * 0.5f - 6, "L", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(cx + s * 0.5f + 6, cy - s * 0.5f - 6, "R", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        float mx = 1e-4f;
        for (int i = 0; i < kGonioN; ++i) mx = std::max(mx, std::max(std::fabs(gl[i]), std::fabs(gr[i])));
        const float want = 0.7f / mx;
        gScale = want < gScale ? want : gScale + (want - gScale) * 0.04f;
        save();
        scissor(r.x + 2, r.y + 24, r.w - 4, 2 * s + 12);
        const Color c = withAlpha2(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.4f), 0.55f);
        for (int i = 0; i < kGonioN; i += 2)
        {
            const float m = (gl[i] + gr[i]) * 0.7071f * gScale, sd = (gr[i] - gl[i]) * 0.7071f * gScale;
            fillRect(Rect(cx + sd * s - 0.8f, cy - m * s - 0.8f, 1.6f, 1.6f), c);
        }
        restore();
        // correlation
        const Rect cb(r.x + 12, r.b() - 30, r.w - 24, 8);
        fillRect(cb, theme.panel2);
        const float x0 = cb.cx(), xv = cb.x + (corr + 1.0f) * 0.5f * cb.w;
        fillRect(Rect(std::min(x0, xv), cb.y, std::fabs(xv - x0), cb.h), corr < 0.0f ? theme.red : theme.neon);
        line(x0, cb.y - 2, x0, cb.b() + 2, theme.ink, 1.0f);
        strokeRect(cb, theme.ink, 1.0f);
        char t[24]; std::snprintf(t, sizeof t, "%+.2f", corr);
        caption(cb.x, cb.y - 10, "CORRELATION");
        textAt(cb.r(), cb.y - 10, t, kMono, 10.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        textAt(cb.x, cb.b() + 9, "-1", kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(cb.r(), cb.b() + 9, "+1", kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---- MOTION is a control: left/right = rate (or division when synced), up/down = depth ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (motionR.contains(x, y) && button == kMouseButtonLeft)
        {
            const int rp = value(kSync) >= 0.5f ? kDivision : kRate;
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(rp, spec(rp).def); setParamGesture(kDepth, spec(kDepth).def); endGroup(); return false; }
            dragging = true; dragParam = rp; dragX = x; dragY = y; dragRate = norm(rp); dragDepth = norm(kDepth);
            beginEdit(rp); beginEdit(kDepth);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (!dragging) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParamNorm(dragParam, dragRate + (x - dragX) * 0.0035f * f);
        setParamNorm(kDepth, dragDepth + (dragY - y) * 0.004f * f);
        if (mod & kModifierShift) { dragRate = norm(dragParam); dragDepth = norm(kDepth); dragX = x; dragY = y; }
        setStatus(paramStatus(dragParam) + "    " + paramStatus(kDepth));
    }
    void bodyMouseUp(float, float) override
    {
        if (!dragging) return;
        dragging = false;
        endEdit(kDepth); endEdit(dragParam);
    }
    void bodyHover(float x, float y) override
    {
        if (motionR.contains(x, y)) setStatus("Drag: left/right = rate (division when synced), up/down = depth (Shift = fine, double-click = reset)");
        else if (respR.contains(x, y)) setStatus("The frequency response right now: the comb (chorus, flanger) or notches (phaser) as they sweep");
        else if (stereoR.contains(x, y)) setStatus("Stereo: the output's left/right image and correlation (+1 mono, 0 wide, below 0 out of phase)");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '6') { pick((int)(key - '1'), var(), (mod & kModifierShift) != 0); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag MOTION", "left/right = rate (division when synced), up/down = depth" },
            { "Type / variant keys", "click = with the classic settings; Shift-click = type or variant only" },
            { "1 - 6", "type: ensemble, analog, rack, flanger, phaser, lamp" },
        };
    }
    std::string footerInfo() override
    {
        std::string s = std::string(kModeNames[mode()]) + "   " + variantKey(mode(), var()) + "   ";
        s += value(kSync) >= 0.5f ? std::string("SYNC ") + kNoteNames[pump::clampv((int)value(kDivision), 0, kNumNotes - 1)] : formatParam(kRate, value(kRate));
        char b[32]; std::snprintf(b, sizeof b, "   LATENCY %d", plug ? plug->latencyNow() : 0);
        s += b;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr int kGonioN = 2048;
    static constexpr double kLo = 20.0, kHi = 20000.0;
    PluginSwirl* plug = nullptr;
    Rect motionR, respR, stereoR, meterL, meterR, typePanel, stereoPanel, outPanel;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float gl[kGonioN] = {}, gr[kGonioN] = {}, gScale = 1.0f, corr = 1.0f;
    double phaseShow = 0.0, light[2] = { 0.0, 0.0 };
    bool dragging = false;
    int dragParam = kRate;
    float dragX = 0, dragY = 0, dragRate = 0, dragDepth = 0;

    ClassicKeys *modeSeg, *varSeg;
    Dial *rateDial, *depthDial, *manDial, *fbDial, *spreadDial, *toneDial, *warmDial, *mixDial, *outDial;
    Button* syncBtn;
    MenuButton* divMenu;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UISwirl)
};

UI* createUI() { return new UISwirl(); }

END_NAMESPACE_DISTRHO
