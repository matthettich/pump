// pump TUBE: interface. Three screens: the transfer curve of the whole chain at the current settings (it moves with
// the live grid bias and the supply sag; drag it: up/down = drive, left/right = bias), a triggered scope of the
// input and output, and the harmonics the chain makes at the input's current level (even in the second colour, odd
// in neon). Below: the circuit (six types, drive, bias, sag, voltage, stages), tone (focus, low cut, tilt, high
// cut) and output (oversampling, auto gain, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginTube.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::tube;

class UITube : public PumpUI {
public:
    UITube() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::tube(), "TUBE", 1000, 620, kBypass)
    {
        modeSeg = addSegmented(kMode, { "GLOW", "TRIODE", "PENTODE", "PUSH-PULL", "STARVE", "FUZZ" });
        modeSeg->tip = "Circuit: Glow (soft warmth) · Triode (2nd harmonic) · Pentode (bite, odd) · Push-Pull (odd only; Bias = class) · Starve (low voltage, sputter) · Fuzz (hard; Bias -/+ = gate/octave)   (keys 1-6)";
        driveDial = addDial(kDrive, "DRIVE", Dial::Large);
        driveDial->tip = "Drive into the first stage, 0..36 dB (or drag the curve up and down)";
        biasDial = addDial(kBias, "BIAS", Dial::Medium, true);
        biasDial->tip = "Bias: moves the operating point (more asymmetry, even harmonics). Push-Pull: hot = class A, cold = crossover. Fuzz: - gates, + rectifies (octave)";
        sagDial = addDial(kSag, "SAG");
        sagDial->tip = "Supply sag: loud passages pull the supply down, clip earlier and bloom back";
        voltDial = addDial(kVoltage, "VOLTAGE");
        voltDial->tip = "Supply voltage: lower = less headroom and a colder bias, toward a starved, sputtering tube";
        stageSeg = addSegmented(kStages, { "1", "2", "3" });
        stageSeg->tip = "Stages: more tubes in series (+10 dB between them) for thicker, denser distortion";
        driveDial->tabGroup = biasDial->tabGroup = sagDial->tabGroup = voltDial->tabGroup = 1;

        focusSeg = addSegmented(kFocus, { "FULL", "LOW", "MID", "HIGH" });
        focusSeg->tip = "Focus: where the tube bites first (+9 dB into the tube there, the exact inverse after it)";
        lowDial = addDial(kLowCut, "LOW CUT", Dial::Small);
        lowDial->tip = "Low cut before the tube (tightens the bass going in)";
        toneDial = addDial(kTone, "TONE", Dial::Small, true);
        toneDial->tip = "Tone: tilt after the tube, darker or brighter (+-6 dB)";
        highDial = addDial(kHighCut, "HIGH CUT", Dial::Small);
        highDial->tip = "High cut after the tube (smooths fizz)";
        lowDial->tabGroup = toneDial->tabGroup = highDial->tabGroup = 2;

        osSeg = addSegmented(kOversampling, { "1x", "2x", "4x", "8x" });
        osSeg->tip = "Oversampling: the tubes run at this multiple of the sample rate (less aliasing, a little latency)";
        autoBtn = addToggle(kAutoGain, "AUTO GAIN");
        autoBtn->small = true;
        autoBtn->tip = "Auto gain: keeps the level steady as you change drive, stages, bias and voltage";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: distorted vs clean (latency-aligned, so parallel distortion stays in phase)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        if (auto* p = static_cast<PluginTube*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kHighCut: if (!highCutActive(v)) return "OFF"; break;
        case kBias: case kTone:
            if (std::fabs(v) < 0.5f) return "0%";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        curveR = Rect(14, 58, 300, 322);
        scopeR = Rect(330, 58, 392, 322);
        harmR = Rect(738, 58, 210, 322);
        meterL = Rect(962, 58, 9, 304);
        meterR = Rect(973, 58, 9, 304);
        const float y0 = 396;
        circuitPanel = Rect(14, y0, 508, 192);
        modeSeg->r = Rect(26, y0 + 30, 484, 28);
        driveDial->r = Rect(22, y0 + 72, 96, Dial::heightFor(Dial::Large));
        biasDial->r = Rect(130, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        sagDial->r = Rect(210, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        voltDial->r = Rect(290, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        stageSeg->r = Rect(390, y0 + 112, 110, 26);

        tonePanel = Rect(536, y0, 226, 192);
        focusSeg->r = Rect(548, y0 + 30, 202, 26);
        lowDial->r = Rect(548, y0 + 86, 64, Dial::heightFor(Dial::Small));
        toneDial->r = Rect(617, y0 + 86, 64, Dial::heightFor(Dial::Small));
        highDial->r = Rect(686, y0 + 86, 64, Dial::heightFor(Dial::Small));

        outPanel = Rect(776, y0, 210, 192);
        osSeg->r = Rect(788, y0 + 30, 186, 26);
        autoBtn->r = Rect(788, y0 + 66, 186, 24);
        mixDial->r = Rect(792, y0 + 100, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(898, y0 + 100, 72, Dial::heightFor(Dial::Medium));
    }

    // ---- the static chain at the current settings (for the curve and the harmonics) ----
    struct Chain {
        int mode = kTriode, stages = 1;
        double g = 1.0, liveBias = 0.0;
        Operating op;
        double s0 = 0.0;
        double eval(double x) const
        {
            double u = x * g, y = 0.0;
            for (int s = 0; s < stages; ++s)
            {
                double grid = 0.0;
                y = stageEval(mode, u, s == 0 ? liveBias : 0.0, op, s0, grid);
                u = y * kInterstage;
            }
            return y;
        }
    };
    Chain chain() const
    {
        Chain c;
        c.mode = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        c.stages = pump::clampv((int)value(kStages), 1, 3);
        c.g = pump::dbToGain(value(kDrive));
        c.op = operating(c.mode, value(kBias), value(kVoltage));
        c.op.V *= sagShow;
        c.liveBias = biasShow;
        c.s0 = shape(c.mode, c.op.biasX, c.op);
        return c;
    }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const float k = std::min(1.0f, dt * 12.0f);
        inShow += (plug->engine.inLevelDb() - inShow) * k;
        sagShow += (plug->engine.sagNow() - sagShow) * k;
        biasShow += (plug->engine.biasShift() - biasShow) * k;
        computeHarmonics();
        // the curve shows the range the input actually uses (twice its peak), gliding as the level moves
        const float want = pump::clampv(2.0f * pump::dbToGainF(testDb), 0.02f, 1.0f);
        curveRange += (want - curveRange) * std::min(1.0f, dt * 3.0f);
        // scope: the newest input/output, the output shifted by the latency so the two line up
        plug->inRing.readSmooth(scopeIn, kScopeN);
        plug->outRing.readSmooth(scopeOut, kScopeN);
    }

    // harmonics of the chain for a sine at the input's level (peak = RMS * sqrt 2; -12 dBFS when silent)
    void computeHarmonics()
    {
        const Chain c = chain();
        testDb = inShow > -90.0f ? pump::clampv(inShow + 3.0f, -40.0f, 0.0f) : -12.0f;
        const double a = pump::dbToGain(testDb);
        constexpr int N = 256;
        double y[N];
        for (int i = 0; i < N; ++i) y[i] = c.eval(a * std::sin(pump::kTwoPi * i / N));
        double mag[11] = {};
        for (int h = 1; h <= 10; ++h)
        {
            double re = 0, im = 0;
            for (int i = 0; i < N; ++i) { re += y[i] * std::cos(pump::kTwoPi * h * i / N); im += y[i] * std::sin(pump::kTwoPi * h * i / N); }
            mag[h] = std::sqrt(re * re + im * im);
        }
        double even = 0, odd = 0;
        for (int h = 2; h <= 10; ++h)
        {
            const double r = mag[h] / std::max(1e-12, mag[1]);
            harm[h] = (float)(20.0 * std::log10(std::max(1e-9, r)));
            (h % 2 ? odd : even) += r * r;
        }
        thd = (float)(100.0 * std::sqrt(even + odd));
        evenShare = (float)(even / std::max(1e-18, even + odd));
    }

    void drawBody() override
    {
        drawCurve();
        drawScope();
        drawHarmonics();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, curveR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(circuitPanel, theme.dim, 1.0f);
        strokeRect(tonePanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(circuitPanel.x + 12, circuitPanel.y + 16, "CIRCUIT");
        caption(stageSeg->r.x, stageSeg->r.y - 14, "STAGES");
        caption(tonePanel.x + 12, tonePanel.y + 16, "FOCUS");
        caption(tonePanel.x + 12, tonePanel.y + 70, "EQ");
        caption(outPanel.x + 12, outPanel.y + 16, "OVERSAMPLING");
        // the supply, under the stage keys: how far it sags right now
        const Rect sb(stageSeg->r.x, stageSeg->r.b() + 22, stageSeg->r.w, 8);
        caption(sb.x, sb.y - 8, "SUPPLY");
        fillRect(sb, theme.panel2);
        const float v = pump::clampv(sagShow, 0.0f, 1.0f);
        fillRect(Rect(sb.x, sb.y, sb.w * v, sb.h), pump::mixColor(theme.red, theme.neon, v));
        strokeRect(sb, theme.ink, 1.0f);
        char t[24]; std::snprintf(t, sizeof t, "%.0f%%", 100.0f * v * (0.12f + 0.88f * std::pow(value(kVoltage) * 0.01f, 1.5f)));
        textAt(sb.r(), sb.b() + 10, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawCurve()
    {
        const Rect& r = curveR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "CURVE");
        textAt(r.r() - 10, r.y + 14, kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)], kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Rect p(r.x + 14, r.y + 30, r.w - 28, r.h - 44);
        const float cx = p.cx(), cy = p.cy(), sx = p.w * 0.5f, sy = p.h * 0.5f;
        for (float g : { -1.0f, -0.5f, 0.5f, 1.0f })
        {
            line(cx + g * sx, p.y, cx + g * sx, p.b(), theme.grid, 1.0f);
            line(p.x, cy - g * sy, p.r(), cy - g * sy, theme.grid, 1.0f);
        }
        line(p.x, cy, p.r(), cy, withAlpha2(theme.ink, 0.3f), 1.0f);
        line(cx, p.y, cx, p.b(), withAlpha2(theme.ink, 0.3f), 1.0f);
        // the input's level as a band
        if (inShow > -100.0f)
        {
            const float a = std::min(1.0f, pump::dbToGainF(inShow) * 1.41421f / curveRange);
            fillRect(Rect(cx - a * sx, p.y, 2 * a * sx, p.h), withAlpha2(theme.neon, theme.dark ? 0.14f : 0.22f));
        }
        const Chain c = chain();
        constexpr int M = 200;
        float ys[M + 1];
        double peak = 1e-9;
        for (int i = 0; i <= M; ++i) { const double x = curveRange * (-1.0 + 2.0 * i / M); const double y = c.eval(x); ys[i] = (float)y; peak = std::max(peak, std::fabs(y)); }
        const float k = (float)(0.9 / peak);
        save();
        scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i <= M; ++i)
        {
            const float px = p.x + p.w * i / M, py = cy - ys[i] * k * sy;
            if (i == 0) moveTo(px, py); else lineTo(px, py);
        }
        strokeColor(theme.ink); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        restore();
        textAt(p.r() - 2, cy + 9, "IN", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        textAt(cx + 5, p.y + 8, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        char t[48];
        std::snprintf(t, sizeof t, "DRIVE %.1f dB   BIAS %+.0f%%", value(kDrive), value(kBias));
        textAt(r.x + 10, r.b() - 10, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "IN RANGE %.0f dBFS", pump::gainToDbF(curveRange));
        textAt(r.r() - 10, r.b() - 10, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        char z[16]; std::snprintf(z, sizeof z, "x%d", 1 << scopeZoom);
        textAt(r.r() - 10, r.y + 14, z, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p(r.x + 10, r.y + 30, r.w - 20, r.h - 40);
        const float cy = p.cy(), sy = scopeScale * p.h * 0.42f * (float)(1 << scopeZoom);
        for (float g : { -0.5f, 0.5f }) line(p.x, cy - g * p.h * 0.9f, p.r(), cy - g * p.h * 0.9f, theme.grid, 1.0f);
        line(p.x, cy, p.r(), cy, withAlpha2(theme.ink, 0.25f), 1.0f);
        if (!plug) return;
        const int lat = plug->latencyNow();
        // 20 ms window, triggered on a rising zero crossing of the input
        const int win = std::max(64, std::min(kScopeN / 2, (int)(0.020 * plug->getSampleRate())));
        int start = kScopeN - win - lat - 1;
        for (int i = start; i > start - win && i > 1; --i)
            if (scopeIn[i - 1] < 0.0f && scopeIn[i] >= 0.0f) { start = i; break; }
        // auto scale: the louder trace fills ~85 % (fast to shrink, slow to grow), the wheel zooms on top
        float mx = 1e-4f;
        for (int i = 0; i < win; ++i)
        {
            mx = std::max(mx, std::fabs(scopeIn[pump::clampv(start + i, 0, kScopeN - 1)]));
            mx = std::max(mx, std::fabs(scopeOut[pump::clampv(start + lat + i, 0, kScopeN - 1)]));
        }
        const float wantScale = 1.0f / std::max(mx, 0.003f);
        scopeScale = wantScale < scopeScale ? wantScale : scopeScale + (wantScale - scopeScale) * 0.05f;
        save();
        scissor(p.x, p.y, p.w, p.h);
        auto trace = [&](const float* buf, int off, const Color& c, float w) {
            beginPath();
            for (int i = 0; i < win; ++i)
            {
                const int k = pump::clampv(start + off + i, 0, kScopeN - 1);
                const float px = p.x + p.w * i / (win - 1), py = cy - pump::clampv(buf[k], -2.0f, 2.0f) * sy;
                if (i == 0) moveTo(px, py); else lineTo(px, py);
            }
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        trace(scopeIn, 0, withAlpha2(theme.ink, 0.30f), 1.2f);
        trace(scopeOut, lat, pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.35f), 2.0f);
        restore();
        textAt(p.x + 2, p.b() - 4, "IN", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 24, p.b() - 4, "OUT", kLabel, 9.0f, pump::mixColor(theme.neon, theme.ink, 0.5f), ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    void drawHarmonics()
    {
        const Rect& r = harmR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "HARMONICS");
        char t[48];
        std::snprintf(t, sizeof t, "AT %.0f dBFS", testDb);
        textAt(r.r() - 10, r.y + 14, t, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p(r.x + 26, r.y + 34, r.w - 36, r.h - 112);
        auto yOf = [&](float db) { return p.y + (std::min(0.0f, std::max(-80.0f, db)) / -80.0f) * p.h; };
        for (int d = 0; d >= -80; d -= 20)
        {
            line(p.x, yOf((float)d), p.r(), yOf((float)d), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%d", d);
            textAt(p.x - 4, yOf((float)d), b, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        const float bw = p.w / 9.0f;
        for (int h = 2; h <= 10; ++h)
        {
            const float x = p.x + bw * (h - 2) + 2.0f;
            const float y = yOf(harm[h]);
            const bool even = h % 2 == 0;
            if (harm[h] > -80.0f)
            {
                fillRect(Rect(x, y, bw - 4.0f, p.b() - y), even ? withAlpha2(theme.second, 0.85f) : theme.neon);
                strokeRect(Rect(x, y, bw - 4.0f, p.b() - y), theme.ink, 1.0f);
            }
            char b[4]; std::snprintf(b, sizeof b, "%d", h);
            textAt(x + (bw - 4.0f) * 0.5f, p.b() + 9, b, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        caption(r.x + 10, r.b() - 46, "THD");
        std::snprintf(t, sizeof t, thd < 10.0f ? "%.2f%%" : "%.0f%%", thd);
        textAt(r.r() - 10, r.b() - 46, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        // even / odd balance
        const Rect eb(r.x + 10, r.b() - 26, r.w - 20, 9);
        fillRect(eb, theme.neon);
        fillRect(Rect(eb.x, eb.y, eb.w * evenShare, eb.h), theme.second);
        strokeRect(eb, theme.ink, 1.0f);
        textAt(eb.x, eb.b() + 8, "EVEN", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(eb.r(), eb.b() + 8, "ODD", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
    }

    // ---- the curve is a control: up/down = drive, left/right = bias ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (curveR.contains(x, y) && button == kMouseButtonLeft)
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kDrive, spec(kDrive).def); setParamGesture(kBias, spec(kBias).def); endGroup(); return false; }
            dragging = true; dragX = x; dragY = y;
            dragDrive = value(kDrive); dragBias = value(kBias);
            beginEdit(kDrive); beginEdit(kBias);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (!dragging) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParam(kDrive, dragDrive + (dragY - y) * 0.12f * f);
        setParam(kBias, dragBias + (x - dragX) * 0.7f * f);
        if (mod & kModifierShift) { dragDrive = value(kDrive); dragBias = value(kBias); dragX = x; dragY = y; }
        setStatus(paramStatus(kDrive) + "    " + paramStatus(kBias));
    }
    void bodyMouseUp(float, float) override
    {
        if (!dragging) return;
        dragging = false;
        endEdit(kBias); endEdit(kDrive);
    }
    void bodyHover(float x, float y) override
    {
        if (curveR.contains(x, y)) setStatus("Drag the curve: up/down = drive, left/right = bias (Shift = fine, double-click = reset)");
        else if (scopeR.contains(x, y)) setStatus("Scope: input (faint) and output, 20 ms (wheel = zoom)");
        else if (harmR.contains(x, y)) setStatus("Harmonics the chain makes at the input's level: even (2, 4, ...) and odd (3, 5, ...)");
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (!scopeR.contains(x, y)) return false;
        scopeZoom = pump::clampv(scopeZoom + (dy > 0 ? 1 : -1), 0, 4);
        saveUi();
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '6') { setParamGesture(kMode, (float)(key - '1')); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the curve", "up/down = drive, left/right = bias (double-click resets both)" },
            { "Wheel on the scope", "zoom" },
            { "1 - 6", "circuit: glow, triode, pentode, push-pull, starve, fuzz" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        const int st = pump::clampv((int)value(kStages), 1, 3);
        std::snprintf(buf, sizeof buf, "%s   %d STAGE%s   %s OVERSAMPLING   LATENCY %d", kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)],
                      st, st > 1 ? "S" : "", kOsNames[pump::clampv((int)value(kOversampling), 0, 3)], plug ? plug->latencyNow() : 0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }
    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "zoom=%d;", scopeZoom); saveUiState(buf); }
    void loadUiState(const std::string& s) override { scopeZoom = pump::clampv(std::atoi(uiStateGet(s, "zoom", "0").c_str()), 0, 4); }

private:
    static constexpr int kScopeN = 8192;
    PluginTube* plug = nullptr;
    Rect curveR, scopeR, harmR, meterL, meterR, circuitPanel, tonePanel, outPanel;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float inShow = -120.0f, sagShow = 1.0f, biasShow = 0.0f, curveRange = 0.25f, scopeScale = 1.0f;
    float harm[11] = {}, thd = 0.0f, evenShare = 0.0f, testDb = -12.0f;
    float scopeIn[kScopeN] = {}, scopeOut[kScopeN] = {};
    int scopeZoom = 0;
    bool dragging = false;
    float dragX = 0, dragY = 0, dragDrive = 0, dragBias = 0;

    Segmented *modeSeg, *stageSeg, *focusSeg, *osSeg;
    Dial *driveDial, *biasDial, *sagDial, *voltDial, *lowDial, *toneDial, *highDial, *mixDial, *outDial;
    Button* autoBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITube)
};

UI* createUI() { return new UITube(); }

END_NAMESPACE_DISTRHO
