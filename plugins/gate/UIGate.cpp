// pump GATE: interface. The step grid is the main screen: one bar per step (height = level, slots as wide as their
// swung duration), the current step lit, beat and bar lines, the gate's envelope drawn over the bars (left in ink,
// the offset right channel in the second colour), the playhead, and under the bars the output's level along the
// last cycle. Click a step to turn it on or off, drag up/down to set its level, drag across steps to paint on/off,
// Alt- or right-drag to draw levels freehand. Pattern keys below it (clear, fill, invert, random, shift), then the
// rhythm (rate, steps, swing, stereo offset), the envelope (length, attack, release, smooth, depth), the mode
// (volume / filter / both, with the filter's range and resonance) and the output (mix, gain, the gate's gain now).
#include "ui/PumpUI.hpp"
#include "PluginGate.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::gate;

class UIGate : public PumpUI {
public:
    UIGate() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::gate(), "GATE", 1000, 620, kBypass)
    {
        clearBtn = addButton("CLEAR", [this]() { patternOp(kOpClear); });
        clearBtn->tip = "Clear: every step of the pattern to 0 %";
        fillBtn = addButton("FILL", [this]() { patternOp(kOpFill); });
        fillBtn->tip = "Fill: every step of the pattern to 100 %";
        invertBtn = addButton("INVERT", [this]() { patternOp(kOpInvert); });
        invertBtn->tip = "Invert: open steps close and closed steps open (levels flip around 50 %)";
        randomBtn = addButton("RANDOM", [this]() { patternOp(kOpRandom); });
        randomBtn->tip = "Random: a new pattern (mostly on and off steps, a few accents; the first step stays on)";
        shiftLBtn = addButton("", [this]() { patternOp(kOpShiftLeft); });
        shiftLBtn->tip = "Shift the pattern one step earlier (Left arrow)";
        shiftRBtn = addButton("", [this]() { patternOp(kOpShiftRight); });
        shiftRBtn->tip = "Shift the pattern one step later (Right arrow)";
        for (Button* b : { clearBtn, fillBtn, invertBtn, randomBtn, shiftLBtn, shiftRBtn }) b->small = true;

        rateMenu = addMenu(kRate, "RATE");
        rateMenu->arrows = true;
        rateMenu->tip = "Rate: the length of one step as a note value (T = triplet, D = dotted), locked to the host tempo";
        stepsDial = addDial(kSteps, "STEPS");
        stepsDial->tip = "Steps: the pattern's length, 1..32 (odd lengths drift against the bar for polyrhythms)";
        swingDial = addDial(kSwing, "SWING");
        swingDial->tip = "Swing: every second step starts later (33 % = a triplet shuffle)";
        offsetDial = addDial(kOffset, "OFFSET");
        offsetDial->tip = "Stereo offset: the right channel plays the pattern this many steps later (stereo motion; OFF = both the same)";
        stepsDial->tabGroup = swingDial->tabGroup = offsetDial->tabGroup = 1;

        lengthDial = addDial(kLength, "LENGTH", Dial::Large);
        lengthDial->tip = "Gate length: how much of each step the gate stays open (100 % = legato)";
        attackDial = addDial(kAttack, "ATTACK");
        attackDial->tip = "Attack: the time to open fully (a minimum ramp always keeps it click-free)";
        releaseDial = addDial(kRelease, "RELEASE");
        releaseDial->tip = "Release: the time to close fully (a minimum ramp always keeps it click-free)";
        smoothDial = addDial(kSmooth, "SMOOTH");
        smoothDial->tip = "Smooth: glides between step levels, from hard chops to a rolling tremolo";
        depthDial = addDial(kDepth, "DEPTH");
        depthDial->tip = "Depth: how far closed steps close (100 % = silence in Volume mode)";
        lengthDial->tabGroup = attackDial->tabGroup = releaseDial->tabGroup = smoothDial->tabGroup = depthDial->tabGroup = 2;

        modeSeg = addSegmented(kMode, { "VOLUME", "FILTER", "BOTH" });
        modeSeg->tip = "Mode: closed steps turn down (Volume), close a resonant low-pass (Filter), or both   (keys 1-3)";
        lowDial = addDial(kFilterLow, "LOW", Dial::Small);
        lowDial->tip = "Filter low: the cutoff of a fully closed step";
        highDial = addDial(kFilterHigh, "HIGH", Dial::Small);
        highDial->tip = "Filter high: the cutoff of a fully open step";
        resoDial = addDial(kResonance, "RESO", Dial::Small);
        resoDial->tip = "Resonance of the step filter";
        lowDial->tabGroup = highDial->tabGroup = resoDial->tabGroup = 3;

        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: gated vs dry (no latency, so it lines up exactly)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        if (auto* p = static_cast<PluginGate*>(pluginPtr())) plug = p;
        rng.seed((uint32_t)(nowSeconds() * 1000.0) | 1u);
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kAttack: case kRelease: if (v < 0.005f) return "0 ms"; break;
        case kOffset:
            if (v < 0.5f) return "OFF";
            std::snprintf(b, sizeof b, "%.0f STEP%s", v, v > 1.5f ? "S" : ""); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        gridR = Rect(14, 58, 934, 292);
        bars = Rect(gridR.x + 14, gridR.y + 30, gridR.w - 28, gridR.h - 92);
        lane = Rect(bars.x, bars.b() + 20, bars.w, 24);
        meterL = Rect(962, 58, 9, 272);
        meterR = Rect(973, 58, 9, 272);

        const float ky = 364, kh = 24;
        clearBtn->r = Rect(14, ky, 70, kh);
        fillBtn->r = Rect(92, ky, 60, kh);
        invertBtn->r = Rect(160, ky, 74, kh);
        randomBtn->r = Rect(242, ky, 78, kh);
        shiftLBtn->r = Rect(384, ky, 32, kh);
        shiftRBtn->r = Rect(422, ky, 32, kh);

        const float y0 = 404, ph = 182;
        rhythmP = Rect(14, y0, 226, ph);
        rateMenu->r = Rect(26, y0 + 30, 202, 26);
        stepsDial->r = Rect(26, y0 + 74, 64, Dial::heightFor(Dial::Medium));
        swingDial->r = Rect(95, y0 + 74, 64, Dial::heightFor(Dial::Medium));
        offsetDial->r = Rect(164, y0 + 74, 64, Dial::heightFor(Dial::Medium));

        envP = Rect(254, y0, 346, ph);
        lengthDial->r = Rect(264, y0 + 34, 86, Dial::heightFor(Dial::Large));
        const float my = y0 + 34 + 45 - 38;   // medium dials centred on the large one
        attackDial->r = Rect(356, my, 60, Dial::heightFor(Dial::Medium));
        releaseDial->r = Rect(416, my, 60, Dial::heightFor(Dial::Medium));
        smoothDial->r = Rect(476, my, 60, Dial::heightFor(Dial::Medium));
        depthDial->r = Rect(536, my, 60, Dial::heightFor(Dial::Medium));

        modeP = Rect(614, y0, 226, ph);
        modeSeg->r = Rect(626, y0 + 30, 202, 26);
        lowDial->r = Rect(626, y0 + 92, 64, Dial::heightFor(Dial::Small));
        highDial->r = Rect(695, y0 + 92, 64, Dial::heightFor(Dial::Small));
        resoDial->r = Rect(764, y0 + 92, 64, Dial::heightFor(Dial::Small));

        outP = Rect(854, y0, 132, ph);
        mixDial->r = Rect(858, y0 + 30, 62, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(920, y0 + 30, 62, Dial::heightFor(Dial::Medium));
    }

    // ---- the pattern as the UI sees it -------------------------------------------------------------------------------
    int steps() const { return pump::clampv((int)value(kSteps), 1, kMaxSteps); }
    double stepQ() const { return pump::noteQuarters((int)value(kRate)); }
    double swing() const { return pump::clampv((double)value(kSwing), 0.0, 75.0) * 0.01; }
    float level(int k) const { return value(kStep1 + k); }
    Settings uiSettings() const
    {
        Settings s;
        s.steps = steps(); s.rate = (int)value(kRate); s.swing = value(kSwing); s.length = value(kLength);
        s.attack = value(kAttack); s.release = value(kRelease); s.smooth = value(kSmooth); s.depth = value(kDepth);
        s.mode = (int)value(kMode); s.filterLow = value(kFilterLow); s.filterHigh = value(kFilterHigh);
        s.resonance = value(kResonance); s.offset = (int)value(kOffset); s.mix = value(kMix); s.outDb = value(kOutGain);
        for (int k = 0; k < kMaxSteps; ++k) s.level[k] = level(k);
        return s;
    }
    // A step's slot in the cycle (quarter notes from the cycle's start; pairs swung, the cycle drawn as one that starts
    // on the first step of a pair)
    double slotStart(int k) const
    {
        const double q = stepQ();
        return std::floor(k * 0.5) * 2.0 * q + ((k & 1) ? (1.0 + swing()) * q : 0.0);
    }
    double slotEnd(int k) const { return k + 1 >= steps() ? steps() * stepQ() : slotStart(k + 1); }
    float xOfQ(double q) const { return bars.x + (float)(q / (steps() * stepQ())) * bars.w; }
    int stepAt(float x) const
    {
        if (x < bars.x || x >= bars.r()) return -1;
        const double q = (double)(x - bars.x) / bars.w * steps() * stepQ();
        for (int k = 0; k < steps(); ++k) if (q < slotEnd(k)) return k;
        return steps() - 1;
    }
    int stepAtClamped(float x) const { return stepAt(pump::clampv(x, bars.x, bars.r() - 0.01f)); }
    float levelAtY(float y) const { return pump::clampv((bars.b() - y) / bars.h * 100.0f, 0.0f, 100.0f); }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
            const float o = plug->engine.openNow(c);
            openShow[c] += (o - openShow[c]) * std::min(1.0f, dt * 40.0f);
        }
        posQ = plug->engine.positionQ();
        bpm = plug->engine.tempo();
        barQ = plug->engine.barQuarters();
        synced = plug->engine.hostLocked();
        const int nb = steps() * Engine::kBinsPerStep;
        const float k = std::min(1.0f, dt * 10.0f);
        for (int b = 0; b < nb; ++b)
        {
            const float d = pump::gainToDbF(plug->engine.bin(b));
            laneDb[b] += (std::max(-60.0f, d) - laneDb[b]) * k;
        }
        const bool filt = (int)value(kMode) != kVolume;
        lowDial->enabled = highDial->enabled = resoDial->enabled = filt;
    }

    void drawBody() override
    {
        drawGrid();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, gridR.b() - 9, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        caption(shiftLBtn->r.x - 52, shiftLBtn->r.cy(), "SHIFT");
        textAt(gridR.r(), shiftLBtn->r.cy(), "CLICK ON/OFF  ·  DRAG LEVEL  ·  ALT/RIGHT-DRAG DRAW  ·  WHEEL ±10%", kLabel, 10.0f, theme.ink2,
               ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);

        for (const Rect* p : { &rhythmP, &envP, &modeP, &outP }) strokeRect(*p, theme.dim, 1.0f);
        caption(rhythmP.x + 12, rhythmP.y + 16, "RHYTHM");
        caption(envP.x + 12, envP.y + 16, "ENVELOPE");
        caption(modeP.x + 12, modeP.y + 16, "MODE");
        caption(modeP.x + 12, lowDial->r.y - 10, "FILTER", (int)value(kMode) == kVolume ? &theme.dim : nullptr);
        caption(outP.x + 12, outP.y + 16, "OUTPUT");

        drawStepShape();
        char t[64];

        // what the gate does right now, per channel: its gain (Volume, Both) or the filter's cutoff (Filter)
        const int mode = (int)value(kMode);
        caption(outP.x + 12, outP.y + 124, mode == kFilter ? "CUTOFF Hz" : "GATE dB");
        for (int c = 0; c < 2; ++c)
        {
            const float o = pump::clampv(openShow[c], 0.0f, 1.0f);
            const Rect b(outP.x + 26, outP.y + 140 + 16 * c, 52, 8);
            textAt(outP.x + 12, b.cy(), c ? "R" : "L", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            fillRect(b, theme.panel2);
            fillRect(Rect(b.x, b.y, b.w * o, b.h), theme.neon);
            strokeRect(b, theme.ink, 1.0f);
            if (mode == kFilter)
            {
                const double lo = std::log(value(kFilterLow)), hi = std::log(value(kFilterHigh));
                const double f = std::exp(hi + (lo - hi) * (1.0 - o));
                std::snprintf(t, sizeof t, f >= 1000.0 ? "%.1fk" : "%.0f", f >= 1000.0 ? f / 1000.0 : f);
            }
            else if (o < 0.0011f) std::snprintf(t, sizeof t, "-INF");
            else std::snprintf(t, sizeof t, "%.1f", std::min(0.0f, pump::gainToDbF(o)));
            textAt(outP.r() - 12, b.cy(), t, kMono, 10.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }
    // One open step between closed ones, close up: the shape ATTACK, LENGTH, RELEASE and SMOOTH give a single hit
    void drawStepShape()
    {
        const Rect r(envP.x + 12, envP.y + 142, envP.w - 24, 30);
        fillRect(r, theme.screen);
        const double sq = stepQ();
        Settings s = uiSettings();
        s.steps = 4; s.swing = 0; s.offset = 0; s.depth = 100;
        for (int k = 0; k < kMaxSteps; ++k) s.level[k] = (k % 4) == 1 ? 100.0f : 0.0f;
        const double q0 = 0.5 * sq, q1 = 3.0 * sq;   // half a closed step, the open step, a step and a half after it
        const int pts = std::min(kEnvPoints, (int)r.w);
        simulate(s, bpm, q0, q1, pts, false, envBuf);
        auto xq = [&](double q) { return r.x + (float)((q - q0) / (q1 - q0)) * r.w; };
        // the open step's slot and its gate length
        const float xa = xq(sq), xb = xq(2.0 * sq), xl = xq(sq * (1.0 + value(kLength) * 0.01));
        fillRect(Rect(xa, r.y, xl - xa, r.h), withAlpha2(theme.neon, theme.dark ? 0.18f : 0.30f));
        line(xa, r.y, xa, r.b(), theme.grid, 1.0f);
        line(xb, r.y, xb, r.b(), theme.grid, 1.0f);
        beginPath();
        for (int i = 0; i < pts; ++i)
        {
            const float x = r.x + r.w * (i + 0.5f) / pts, y = r.b() - 2.0f - pump::clampv(envBuf[i], 0.0f, 1.0f) * (r.h - 5.0f);
            if (i == 0) moveTo(r.x, y);
            lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        strokeRect(r, theme.ink, 1.0f);
        // the step and the time the gate stays open at this tempo
        char t[64];
        const double stepMs = sq * 60000.0 / std::max(20.0f, bpm);
        std::snprintf(t, sizeof t, "STEP %s   OPEN %s", msText(stepMs).c_str(), msText(stepMs * value(kLength) * 0.01).c_str());
        caption(r.x, r.y - 10, "ONE STEP");
        textAt(r.r(), r.y - 10, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    static std::string msText(double ms)
    {
        char b[24];
        if (ms >= 1000.0) std::snprintf(b, sizeof b, "%.2f s", ms / 1000.0);
        else std::snprintf(b, sizeof b, ms >= 100.0 ? "%.0f ms" : "%.1f ms", ms);
        return b;
    }

    void drawOverlay() override
    {
        // the shift keys' arrows (the label font has no arrow glyphs)
        textAt(shiftLBtn->r.cx(), shiftLBtn->r.cy(), "◀", kMono, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(shiftRBtn->r.cx(), shiftRBtn->r.cy(), "▶", kMono, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawGrid()
    {
        const Rect& r = gridR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "PATTERN");
        const int n = steps();
        const double sq = stepQ(), cycQ = n * sq;
        const int offset = (int)value(kOffset);
        // legend and info
        line(r.x + 84, r.y + 14, r.x + 100, r.y + 14, theme.ink, 2.0f);
        textAt(r.x + 105, r.y + 14, offset > 0 ? "ENVELOPE L" : "ENVELOPE", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        if (offset > 0)
        {
            line(r.x + 196, r.y + 14, r.x + 212, r.y + 14, theme.second, 2.0f);
            char rs[24]; std::snprintf(rs, sizeof rs, "R  +%d", offset);
            textAt(r.x + 217, r.y + 14, rs, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        }
        char t[96];
        std::snprintf(t, sizeof t, "%d x %s   %.1f BPM   %s", n, pump::kNoteNames[pump::clampv((int)value(kRate), 0, pump::kNumNotes - 1)], bpm,
                      synced ? "HOST SYNC" : "FREE RUN");
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);

        // beats: alternate shading, beat lines, stronger bar lines (thinned out when they'd crowd)
        double beatQ = 1.0;
        while (cycQ / beatQ > 64.0) beatQ *= 2.0;
        const double bq = barQ > 0.0f ? barQ : 4.0;
        for (int b = 0; b * beatQ < cycQ - 1e-9; ++b)
        {
            const double q0 = b * beatQ, q1 = std::min(cycQ, q0 + beatQ);
            if (b & 1) fillRect(Rect(xOfQ(q0), bars.y, xOfQ(q1) - xOfQ(q0), bars.h), withAlpha2(theme.ink, theme.dark ? 0.035f : 0.03f));
        }
        for (int b = 1; b * beatQ < cycQ - 1e-9; ++b)
        {
            const double q = b * beatQ;
            const bool isBar = std::fabs(q / bq - std::round(q / bq)) < 1e-6;
            const float x = xOfQ(q);
            line(x, bars.y, x, lane.b(), isBar ? withAlpha2(theme.ink, theme.dark ? 0.30f : 0.26f) : theme.grid, 1.0f);
        }

        // where the playhead is (the left channel's step; the right one's when offset)
        const double sw = swing();
        const StepPos sp = locate(posQ, sq, sw);
        const int curL = patternStep(sp.global, n, 0), curR = patternStep(sp.global, n, offset);
        save();
        scissor(bars.x, bars.y - 2, bars.w, bars.h + 4);
        for (int k = 0; k < n; ++k)
        {
            const float x0 = xOfQ(slotStart(k)), x1 = xOfQ(slotEnd(k));
            const float gap = (x1 - x0) > 10.0f ? 2.0f : 1.0f;
            const Rect slot(x0 + gap, bars.y, std::max(1.0f, x1 - x0 - 2.0f * gap), bars.h);
            const bool cur = k == curL;
            if (cur) fillRect(slot, withAlpha2(theme.neon, theme.dark ? 0.16f : 0.22f));
            else if (k == hoverStep) fillRect(slot, withAlpha2(theme.ink, 0.05f));
            const float lv = pump::clampv(level(k), 0.0f, 100.0f) * 0.01f;
            const float h = lv * bars.h;
            if (h >= 1.0f)
            {
                const Rect bar(slot.x, bars.b() - h, slot.w, h);
                fillRect(bar, cur ? theme.neon : pump::mixColor(theme.neon, theme.screen, theme.dark ? 0.55f : 0.45f));
                strokeRect(bar, cur ? theme.ink : withAlpha2(theme.ink, 0.55f), cur ? 1.5f : 1.0f);
            }
            else fillRect(Rect(slot.x, bars.b() - 3.0f, slot.w, 3.0f), theme.dim);
            if (k == curR && offset > 0) fillRect(Rect(slot.x, bars.y, slot.w, 4.0f), theme.second);
        }
        // the envelope: what the gate does over one cycle (left in ink; the offset right channel in the second colour)
        const int pts = std::min(kEnvPoints, (int)bars.w);
        const Settings s = uiSettings();
        auto curve = [&](bool right, const Color& c, float w) {
            simulate(s, bpm, 0.0, cycQ, pts, right, envBuf);
            beginPath();
            for (int i = 0; i < pts; ++i)
            {
                const float x = bars.x + bars.w * (i + 0.5f) / pts, y = bars.b() - pump::clampv(envBuf[i], 0.0f, 1.0f) * (bars.h - 2.0f) - 1.0f;
                if (i == 0) moveTo(bars.x, y);
                lineTo(x, y);
            }
            lineTo(bars.r(), bars.b() - pump::clampv(envBuf[pts - 1], 0.0f, 1.0f) * (bars.h - 2.0f) - 1.0f);
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        if (offset > 0) curve(true, theme.second, 1.6f);
        curve(false, theme.ink, 2.0f);
        restore();

        // the playhead (through the bars and the output lane)
        const float px = xOfQ(slotStart(curL) + sp.phase * (slotEnd(curL) - slotStart(curL)));
        line(px, bars.y, px, lane.b(), withAlpha2(theme.ink, 0.75f), 1.5f);

        // step numbers (thinned out when the slots are narrow)
        const float sw1 = bars.w / n;
        const int every = sw1 >= 18.0f ? 1 : (sw1 >= 9.0f ? 2 : 4);
        for (int k = 0; k < n; ++k)
        {
            if (k % every != 0 && k != curL) continue;
            char b[8]; std::snprintf(b, sizeof b, "%d", k + 1);
            const float cx = 0.5f * (xOfQ(slotStart(k)) + xOfQ(slotEnd(k)));
            textAt(cx, bars.b() + 10, b, k == curL ? kBold : kMono, 9.5f, k == curL ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // the output's level along the last cycle (mirrored, -48..0 dBFS)
        fillRect(lane, withAlpha2(theme.ink, 0.04f));
        const int nb = n * Engine::kBinsPerStep;
        beginPath();
        for (int b = 0; b < nb; ++b)
        {
            const float a = pump::clampv((laneDb[b] + 48.0f) / 48.0f, 0.0f, 1.0f) * lane.h * 0.5f;
            const float x0 = lane.x + lane.w * b / nb, x1 = lane.x + lane.w * (b + 1) / nb;
            if (a > 0.3f) rect(x0, lane.cy() - a, std::max(0.8f, x1 - x0 - 0.6f), 2.0f * a);
        }
        fillColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.15f : 0.45f));
        fill();
        line(lane.x, lane.cy(), lane.r(), lane.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        textAt(lane.x, lane.b() + 9, "OUTPUT LEVEL, LAST CYCLE", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);

        // the level of the step under the mouse (or being edited)
        const int shown = dragStep >= 0 ? dragStep : hoverStep;
        if (shown >= 0 && shown < n)
        {
            const float cx = 0.5f * (xOfQ(slotStart(shown)) + xOfQ(slotEnd(shown)));
            const float y = std::max(bars.y + 9.0f, bars.b() - pump::clampv(level(shown), 0.0f, 100.0f) * 0.01f * bars.h - 10.0f);
            std::snprintf(t, sizeof t, "%.0f%%", level(shown));
            const float tw = textWidth(t, kMono, 10.0f) + 8.0f;
            const Rect tag(cx - tw * 0.5f, y - 8.0f, tw, 16.0f);
            fillRect(tag, theme.ink);
            textAt(cx, y, t, kMono, 10.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    // ---- editing steps --------------------------------------------------------------------------------------------------
    void touch(int k)
    {
        const int p = kStep1 + k;
        if (std::find(touched.begin(), touched.end(), p) == touched.end()) { beginEdit(p); touched.push_back(p); }
    }
    void setStep(int k, float v)
    {
        if (k < 0 || k >= steps()) return;
        touch(k);
        setParam(kStep1 + k, std::round(pump::clampv(v, 0.0f, 100.0f)));
    }
    std::string stepStatus(int k) const
    {
        char b[64]; std::snprintf(b, sizeof b, "Step %d  %.0f%%", k + 1, level(k));
        return b;
    }
    bool editArea(float x, float y) const { return x >= bars.x && x < bars.r() && y >= bars.y - 4 && y < bars.b() + 4; }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool) override
    {
        if (!editArea(x, y)) return false;
        const int k = stepAt(x);
        if (k < 0) return false;
        beginGroup();
        if (button == kMouseButtonRight || (button == kMouseButtonLeft && (mod & kModifierAlt)))
        {
            drag = kDraw;
            dragStep = k;
            lastX = x; lastY = y;
            setStep(k, levelAtY(y));
        }
        else if (button == kMouseButtonLeft)
        {
            drag = kToggle;
            dragStep = paintFrom = k;
            dragLevel0 = level(k);
            paintValue = dragLevel0 >= 50.0f ? 0.0f : 100.0f;
            dragY0 = y;
            setStep(k, paintValue);
        }
        else { endGroup(); return false; }
        setStatus(stepStatus(k));
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (drag == kNone) return;
        if (drag == kDraw)
        {
            // freehand: every step the mouse crossed since the last event gets the level along the way
            const int nSeg = std::max(1, (int)(std::fabs(x - lastX) / 2.0f));
            for (int i = 1; i <= nSeg; ++i)
            {
                const float t = (float)i / nSeg, xi = lastX + (x - lastX) * t, yi = lastY + (y - lastY) * t;
                const int k = stepAtClamped(xi);
                setStep(k, levelAtY(yi));
                dragStep = k;
            }
            lastX = x; lastY = y;
            setStatus(stepStatus(dragStep));
            return;
        }
        const int j = stepAtClamped(x);
        if (drag == kToggle)
        {
            if (j != dragStep) drag = kPaint;
            else if (std::fabs(y - dragY0) > 3.0f) drag = kLevel;   // a vertical drag adjusts the level it had before the click
        }
        if (drag == kPaint)
        {
            // drum-machine style: every step crossed takes the clicked step's new state
            const int a = std::min(paintFrom, j), b = std::max(paintFrom, j);
            for (int k = a; k <= b; ++k) setStep(k, paintValue);
            paintFrom = j; dragStep = j;
        }
        else if (drag == kLevel)
        {
            const bool fine = (mod & kModifierShift) != 0;
            const float perPx = (fine ? 0.1f : 1.0f) * 100.0f / bars.h;
            setStep(dragStep, dragLevel0 + (dragY0 - y) * perPx);
            if (fine) { dragLevel0 = level(dragStep); dragY0 = y; }
            else if (lastFine) { dragLevel0 = level(dragStep); dragY0 = y; }
            lastFine = fine;
        }
        setStatus(stepStatus(dragStep));
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == kNone) return;
        for (int p : touched) endEdit(p);
        touched.clear();
        endGroup();
        drag = kNone; dragStep = -1; lastFine = false;
    }
    void bodyHover(float x, float y) override
    {
        hoverStep = editArea(x, y) ? stepAt(x) : -1;
        if (hoverStep >= 0)
            setStatus(stepStatus(hoverStep) + "   (click on/off, drag level, Alt/right-drag draw)");
        else if (lane.contains(x, y)) setStatus("The output's level along the last cycle of the pattern");
        else if (gridR.contains(x, y)) setStatus("The pattern: bars = step levels, line = the gate's envelope, lit = the current step");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!editArea(x, y)) return false;
        const int k = stepAt(x);
        if (k < 0) return false;
        const float st = (mod & kModifierShift) ? 1.0f : 10.0f;
        setParamGesture(kStep1 + k, pump::clampv(std::round(level(k) + (dy > 0 ? st : -st)), 0.0f, 100.0f));
        setStatus(stepStatus(k));
        return true;
    }

    // ---- pattern keys: each is one undo step ----------------------------------------------------------------------------
    enum Op { kOpClear, kOpFill, kOpInvert, kOpRandom, kOpShiftLeft, kOpShiftRight };
    void patternOp(Op op)
    {
        const int n = steps();
        float now[kMaxSteps], next[kMaxSteps];
        for (int k = 0; k < kMaxSteps; ++k) now[k] = next[k] = level(k);
        for (int k = 0; k < n; ++k)
        {
            switch (op)
            {
            case kOpClear: next[k] = 0.0f; break;
            case kOpFill: next[k] = 100.0f; break;
            case kOpInvert: next[k] = 100.0f - now[k]; break;
            case kOpRandom:
            {
                const double u = rng.uni();
                if (k == 0 || u < 0.45) next[k] = 100.0f;
                else if (u < 0.6) next[k] = (float)(10 * (3 + (int)(rng.uni() * 7)));   // an accent: 30..90 %
                else next[k] = 0.0f;
                break;
            }
            case kOpShiftLeft: next[k] = now[(k + 1) % n]; break;
            case kOpShiftRight: next[k] = now[(k + n - 1) % n]; break;
            }
        }
        beginGroup();
        for (int k = 0; k < n; ++k) setParamGesture(kStep1 + k, next[k]);
        endGroup();
        static const char* const names[] = { "Cleared", "Filled", "Inverted", "Random pattern", "Shifted one step earlier", "Shifted one step later" };
        setStatus(names[op]);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float y = (float)ev.pos.getY() / fScale;
        if (y <= kHeaderH || y >= H - kFooterH) hoverStep = -1;
        return PumpUI::onMotion(ev);
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '3') { setParamGesture(kMode, (float)(key - '1')); return true; }
        if (key == kKeyLeft) { patternOp(kOpShiftLeft); return true; }
        if (key == kKeyRight) { patternOp(kOpShiftRight); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click a step", "on / off (drag across steps to paint the same)" },
            { "Drag a step up / down", "its level (Shift = fine)" },
            { "Alt + drag, right-drag", "draw levels freehand across the steps" },
            { "Wheel on a step", "level +-10 % (Shift +-1 %)" },
            { "Left / Right", "shift the pattern one step" },
            { "1 - 3", "mode: volume, filter, both" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s   %d x %s   SWING %.0f%%   %s %.1f BPM", kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)],
                      steps(), pump::kNoteNames[pump::clampv((int)value(kRate), 0, pump::kNumNotes - 1)], value(kSwing),
                      synced ? "SYNC" : "FREE", bpm);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr int kEnvPoints = 1024;
    enum DragMode { kNone, kToggle, kLevel, kPaint, kDraw };
    PluginGate* plug = nullptr;
    Rect gridR, bars, lane, meterL, meterR, rhythmP, envP, modeP, outP;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float openShow[2] = { 1.0f, 1.0f };
    float laneDb[Engine::kMaxBins] = {};
    float envBuf[kEnvPoints] = {};
    double posQ = 0.0;
    float bpm = 120.0f, barQ = 4.0f;
    bool synced = false;
    pump::fx::Rng rng;

    DragMode drag = kNone;
    int dragStep = -1, paintFrom = -1, hoverStep = -1;
    float paintValue = 100.0f, dragY0 = 0.0f, dragLevel0 = 0.0f, lastX = 0.0f, lastY = 0.0f;
    bool lastFine = false;
    std::vector<int> touched;

    Button *clearBtn, *fillBtn, *invertBtn, *randomBtn, *shiftLBtn, *shiftRBtn;
    MenuButton* rateMenu;
    Dial *stepsDial, *swingDial, *offsetDial, *lengthDial, *attackDial, *releaseDial, *smoothDial, *depthDial;
    Segmented* modeSeg;
    Dial *lowDial, *highDial, *resoDial, *mixDial, *outDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIGate)
};

UI* createUI() { return new UIGate(); }

END_NAMESPACE_DISTRHO
