// pump BOUNCE: interface. The ball: the bounces of one drop along time (the floor), every hit a bar as tall as its
// level (a dB scale) and coloured by its pitch (the neutral colour at 0 st, greener as the bounces climb, pinker as
// they fall; in Drop mode as wide as the hit is long), the ball's arcs between them (the gaps shrink by GRAVITY, or grow
// with RISE), a band around each bar for JITTER, and the ball itself arcing across when a drop fires (Drop: a trigger;
// Taps: an onset in the input). Drag the display: left/right = the first gap (a note division with SYNC), up/down =
// gravity. Below: the ball (mode, sync, rise, fit, time, gravity, bounces, slice, the DROP key), what changes on every
// bounce (damp, pitch, tone, spread, jitter, feedback) and the trigger and output (threshold, every, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginBounce.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::bounce;

class UIBounce : public PumpUI {
public:
    UIBounce() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::bounce(), "BOUNCE", 860, 496, kBypass)
    {
        modeSeg = addSegmented(kMode, { "TAPS", "DROP" });
        modeSeg->tip = "Mode: Taps (a multitap delay on the signal) · Drop (a trigger captures a slice and re-fires it as a bouncing ball)   (keys 1-2)";
        syncBtn = addToggle(kSync, "SYNC");
        syncBtn->small = true;
        syncBtn->tip = "Sync: the first gap is a note division at the host tempo (120 BPM without one)   (S)";
        riseBtn = addToggle(kRise, "RISE");
        riseBtn->small = true;
        riseBtn->tip = "Rise: the gaps grow instead of shrinking (by 1 / gravity), a ball thrown up   (R)";
        fitBtn = addToggle(kFit, "FIT");
        fitBtn->small = true;
        fitBtn->tip = "Drop: every hit is shortened to fit its gap, so fast bounces ratchet instead of smearing   (F)";

        timeDial = addDial(kTime, "TIME");
        gravDial = addDial(kGravity, "GRAVITY");
        gravDial->tip = "Gravity: each gap is this fraction of the one before (0.50 = half; 0.98 = a slow settle). Drag the display up/down";
        bounceDial = addDial(kBounces, "BOUNCES");
        bounceDial->tip = "Bounces: how many hits after the first. The ball also settles when a gap falls under 2 ms, or at 8 s";
        sliceDial = addDial(kSlice, "SLICE");
        sliceDial->tip = "Drop: how much of the input is captured (5-500 ms) and re-fired on every bounce";
        timeDial->tabGroup = gravDial->tabGroup = bounceDial->tabGroup = sliceDial->tabGroup = 1;

        dampDial = addDial(kDamp, "DAMP");
        dampDial->tip = "Damp: dB lost on every bounce (0-12)";
        pitchDial = addDial(kPitch, "PITCH", Dial::Medium, true);
        pitchDial->tip = "Drop: semitones per bounce, accumulating (the slice plays back faster or slower): bounces climb or fall (up to two octaves)";
        toneDial = addDial(kTone, "TONE", Dial::Medium, true);
        toneDial->tip = "Drop: tilt per bounce, accumulating: negative = each hit darker, positive = brighter";
        spreadDial = addDial(kSpread, "SPREAD");
        spreadDial->tip = "Spread: the bounces swing across the stereo field, alternating sides";
        jitterDial = addDial(kJitter, "JITTER");
        jitterDial->tip = "Jitter: humanize, every bounce lands a little early or late (up to 40% of its gap)";
        fbDial = addDial(kFeedback, "FEEDBACK");
        fbDial->tip = "Feedback: the settled ball drops again, softer. Taps: the last tap feeds the line · Drop: the whole bounce comes back";
        dampDial->tabGroup = pitchDial->tabGroup = toneDial->tabGroup = spreadDial->tabGroup = jitterDial->tabGroup = fbDial->tabGroup = 2;

        trigSeg = addSegmented(kTrigger, { "TRANSIENT", "SYNC", "MIDI", "BUTTON" });
        trigSeg->vertical = true;
        trigSeg->tip = "Drop trigger: a transient over the threshold · every note division of the host tempo · a MIDI note-on (velocity = level) · the DROP key (it works with any trigger)";
        threshDial = addDial(kThreshold, "THRESH");
        threshDial->tip = "Transient trigger: the level that fires a drop (it re-arms 6 dB below)";
        everyDial = addDial(kEvery, "EVERY");
        everyDial->tip = "Sync trigger: a drop every note division, locked to the host's beat";
        threshDial->tabGroup = everyDial->tabGroup = 3;
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: the dry signal stays at full level up to 50 %, the bounces reach full level at 50 %";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        mixDial->tabGroup = outDial->tabGroup = 4;

        if (auto* p = static_cast<PluginBounce*>(pluginPtr())) plug = p;
        bindControls();
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kPitch:
            if (std::fabs(v) < 0.05f) return "0 st";
            std::snprintf(b, sizeof b, "%+.1f st", v); return b;
        case kTone:
            if (std::fabs(v) < 0.5f) return "FLAT";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        case kDamp: std::snprintf(b, sizeof b, "%.1f dB", v); return b;
        case kThreshold: std::snprintf(b, sizeof b, "%.0f dB", v); return b;
        case kJitter: case kFeedback: if (v < 0.5f) return "OFF"; break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    // SYNC turns the time dial into a note dial; the drop controls follow the mode and the trigger
    void bindControls()
    {
        const bool sync = value(kSync) >= 0.5f, drop = (int)value(kMode) == kModeDrop;
        const int trig = (int)value(kTrigger);
        timeDial->param = sync ? kNote : kTime;
        timeDial->label = sync ? "NOTE" : "TIME";
        timeDial->tip = sync ? "The first gap: a note division at the host tempo (or drag the display left/right)" : "The first gap (10 ms - 4 s); drop the ball on every one of these in turn (or drag the display left/right)";
        sliceDial->enabled = pitchDial->enabled = toneDial->enabled = fitBtn->enabled = trigSeg->enabled = drop;
        threshDial->enabled = drop && trig == kTrigTransient;
        everyDial->enabled = drop && trig == kTrigSync;
    }
    void onParamChanged(int, float) override { bindControls(); }
    void onPresetApplied() override { bindControls(); }
    void releaseAllHeld() override { dropKey(false); }

    void layout() override
    {
        dispR = Rect(14, 58, 806, 178);
        plotR = Rect(dispR.x + 18, dispR.y + 30, dispR.w - 36, dispR.h - 62);
        meterL = Rect(828, 58, 9, 158);
        meterR = Rect(839, 58, 9, 158);
        const float y0 = 248;
        ballPanel = Rect(14, y0, 292, 214);
        modeSeg->r = Rect(26, y0 + 24, 268, 26);
        syncBtn->r = Rect(26, y0 + 58, 84, 22);
        riseBtn->r = Rect(118, y0 + 58, 84, 22);
        fitBtn->r = Rect(210, y0 + 58, 84, 22);
        timeDial->r = Rect(24, y0 + 86, 66, Dial::heightFor(Dial::Medium));
        gravDial->r = Rect(92, y0 + 86, 66, Dial::heightFor(Dial::Medium));
        bounceDial->r = Rect(160, y0 + 86, 66, Dial::heightFor(Dial::Medium));
        sliceDial->r = Rect(228, y0 + 86, 66, Dial::heightFor(Dial::Medium));
        dropR = Rect(26, y0 + 172, 268, 34);

        hitPanel = Rect(314, y0, 276, 214);
        const float dy1 = y0 + 24, dy2 = y0 + 106;
        dampDial->r = Rect(320, dy1, 86, Dial::heightFor(Dial::Medium));
        pitchDial->r = Rect(408, dy1, 86, Dial::heightFor(Dial::Medium));
        toneDial->r = Rect(496, dy1, 86, Dial::heightFor(Dial::Medium));
        spreadDial->r = Rect(320, dy2, 86, Dial::heightFor(Dial::Medium));
        jitterDial->r = Rect(408, dy2, 86, Dial::heightFor(Dial::Medium));
        fbDial->r = Rect(496, dy2, 86, Dial::heightFor(Dial::Medium));

        trigPanel = Rect(598, y0, 250, 214);
        trigSeg->r = Rect(610, y0 + 26, 104, 80);
        threshDial->r = Rect(724, y0 + 22, 56, Dial::heightFor(Dial::Medium));
        everyDial->r = Rect(784, y0 + 22, 56, Dial::heightFor(Dial::Medium));
        mixDial->r = Rect(620, y0 + 126, 80, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(738, y0 + 126, 80, Dial::heightFor(Dial::Medium));
    }

    // ---- the bounce at the current settings ----
    double bpmNow() const { return plug ? (double)plug->engine.tempo() : 120.0; }
    double firstGap() const { return firstGapSeconds(value(kSync) >= 0.5f, (int)value(kNote), value(kTime), bpmNow()); }
    int computeTimes(double* t) const
    {
        return bounceTimes(firstGap(), gapRatio(value(kGravity), value(kRise) >= 0.5f), (int)value(kBounces), kMaxSeconds, t);
    }
    bool dropMode() const { return (int)value(kMode) == kModeDrop; }
    // the length of hit k in seconds (Drop; k = 0 is the input itself)
    double hitSeconds(int k, double gapAfter) const
    {
        const double r = k == 0 ? 1.0 : pitchRatio(value(kPitch), k);
        double len = 0.001 * value(kSlice);
        if (value(kFit) >= 0.5f) len = std::min(len, 0.92 * gapAfter * r);
        return len / r;
    }
    pump::Color pitchColour(int k) const
    {
        if (!dropMode()) return theme.neon;
        const double st = pump::clampv((double)value(kPitch) * k, -24.0, 24.0);
        if (std::fabs(st) < 0.05) return theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.15f);
        const float hue = 250.0f - (float)((st > 0 ? 100.0 : 80.0) * st / 24.0);
        return theme.dark ? pump::oklchColor(0.74f, 0.15f, hue) : pump::oklchColor(0.62f, 0.16f, hue);
    }

    void tick(float dt) override
    {
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        const uint32_t ev = plug->engine.events();
        if (ev != lastEvents) { lastEvents = ev; age = plug->engine.ballAge(); }
        else age = std::min(1000.0f, age + dt);
        const float flash = age < 0.35f ? 1.0f - age / 0.35f : 0.0f;
        dropFlash = std::max(flash, dropHeld ? 1.0f : 0.0f);
    }

    void drawBody() override
    {
        drawBall();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, meterL.b() + 10, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        drawDropKey();

        strokeRect(ballPanel, theme.dim, 1.0f);
        strokeRect(hitPanel, theme.dim, 1.0f);
        strokeRect(trigPanel, theme.dim, 1.0f);
        caption(ballPanel.x + 12, ballPanel.y + 13, "BALL");
        caption(hitPanel.x + 12, hitPanel.y + 13, "EVERY BOUNCE");
        const Color* tc = dropMode() ? nullptr : &theme.dim;
        caption(trigPanel.x + 12, trigPanel.y + 13, "TRIGGER", tc);
        caption(trigPanel.x + 12, trigPanel.y + 123, "OUTPUT");
        // what the last hit of the sequence ends up as
        double t[kMaxBounces + 2];
        const int m = computeTimes(t);
        char b[96];
        const double st = pump::clampv((double)value(kPitch) * m, -24.0, 24.0), tilt = tiltDb(value(kTone), m);
        std::snprintf(b, sizeof b, "HIT %d   %.1f dB   %+.1f st   %+.1f dB TILT", m, -value(kDamp) * m, dropMode() ? st : 0.0, dropMode() ? tilt : 0.0);
        textAt(hitPanel.x + 12, hitPanel.b() - 12, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    float tToX(double t, double span) const { return plotR.x + (float)(t / span) * plotR.w; }

    void drawBall()
    {
        const Rect& r = dispR;
        const Rect& p = plotR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "BOUNCE");
        const bool drop = dropMode();
        double t[kMaxBounces + 2];
        const int m = computeTimes(t);
        const double T = firstGap(), rho = gapRatio(value(kGravity), value(kRise) >= 0.5f);
        const bool rise = value(kRise) >= 0.5f;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s%s   %.2f %s", kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)],
                      drop ? (std::string("   ") + kTriggerNames[pump::clampv((int)value(kTrigger), 0, kNumTriggers - 1)]).c_str() : "", rise ? 1.0 / rho : rho, rise ? "GROWS" : "SHRINKS");
        std::string head = buf;
        for (char& ch : head) ch = (char)std::toupper((unsigned char)ch);
        textAt(r.r() - 10, r.y + 14, head, kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);

        const double span = std::max(0.04, m > 0 ? t[m] * 1.04 + 0.012 : T * 1.5);
        const float floorY = p.b() - 16.0f;
        const float barMax = std::min(floorY - p.y - 8.0f, 70.0f);
        const float ballR = 8.0f;
        // time grid on the floor
        {
            static const double nice[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0 };
            double step = 5.0;
            for (double s : nice) if (s / span * p.w >= 70.0) { step = s; break; }
            for (int i = 1; i * step <= span * 1.0001 && i < 200; ++i)
            {
                const float x = tToX(i * step, span);
                line(x, p.y, x, floorY, theme.grid, 1.0f);
                if (step >= 1.0) std::snprintf(buf, sizeof buf, "%.3g s", i * step); else std::snprintf(buf, sizeof buf, "%.0f ms", i * step * 1000.0);
                textAt(x, floorY + 11.0f, buf, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        // the arcs between the hits (heights grow with the gap, so the shrinking is what you see)
        double maxGap = 1e-6;
        for (int k = 0; k < m; ++k) maxGap = std::max(maxGap, t[k + 1] - t[k]);
        const float arcMax = floorY - p.y - ballR - 4.0f;
        auto arcH = [&](int k) { return std::max(3.0f, arcMax * (float)std::pow((t[k + 1] - t[k]) / maxGap, 0.55)); };
        for (int k = 0; k < m; ++k)
        {
            const float x0 = tToX(t[k], span), x1 = tToX(t[k + 1], span), h = arcH(k);
            beginPath();
            constexpr int N = 24;
            for (int i = 0; i <= N; ++i)
            {
                const float u = (float)i / N;
                const float x = x0 + (x1 - x0) * u, y = floorY - ballR - 4.0f * u * (1.0f - u) * std::max(0.0f, h - ballR);
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(withAlpha2(theme.second, theme.dark ? 0.35f : 0.45f)); strokeWidth(1.0f); lineJoin(ROUND); stroke();
        }
        line(p.x, floorY, p.r(), floorY, theme.ink, 1.5f);
        // the hits: bars as tall as their level
        const double jit = value(kJitter) * 0.01 * 0.4;
        for (int k = 0; k <= m; ++k)
        {
            const float x = tToX(t[k], span);
            const double db = -value(kDamp) * k;
            const float h = std::max(3.0f, barMax * (float)pump::clampv(1.0 + db / 36.0, 0.0, 1.0));
            float w = 3.0f;
            if (drop)
            {
                const double gapAfter = k < m ? t[k + 1] - t[k] : (k > 0 ? (t[k] - t[k - 1]) * rho : T);
                w = pump::clampv((float)(hitSeconds(k, gapAfter) / span) * p.w, 3.0f, 16.0f);
            }
            // when it hit: the bar flashes
            const double since = (double)age - t[k];
            const float flash = since >= 0.0 && since < 0.25 ? 1.0f - (float)since / 0.25f : 0.0f;
            const Color c = k == 0 ? theme.ink : pitchColour(k);
            if (jit > 0.0 && k > 0)
            {
                const double before = t[k] - t[k - 1], after = k < m ? t[k + 1] - t[k] : before * rho;
                const float jw = (float)(jit * std::min(before, after) / span) * p.w;
                fillRect(Rect(x - jw, floorY - 3.0f, 2.0f * jw, 3.0f), withAlpha2(theme.ink, 0.35f));
            }
            const Rect bar(x - 0.5f * w, floorY - h, w, h);
            fillRect(bar, withAlpha2(c, 0.55f + 0.45f * flash));
            strokeRect(bar, theme.ink, 1.0f);
            if (flash > 0.0f) fillRect(Rect(bar.x - 1, bar.y - 1, bar.w + 2, bar.h + 2), withAlpha2(theme.ink, 0.25f * flash));
            // the pan: a tick under the floor, left or right of the hit
            if (k > 0 && value(kSpread) > 1.0f)
            {
                const float pan = (float)panFor(k, value(kSpread));
                line(x, floorY + 2.0f, x + pan * 5.0f, floorY + 2.0f, theme.ink2, 1.5f);
            }
        }
        textAt(tToX(0.0, span) + 6.0f, floorY - barMax - 6.0f, "IN", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        // the ball
        {
            float bx = tToX(0.0, span), by = floorY - ballR;
            float squash = 0.0f;
            const double a = (double)age;
            if (m > 0 && a >= 0.0 && a < t[m] + 1.5)
            {
                int k = 0;
                while (k < m && a >= t[k + 1]) ++k;
                if (k >= m) { bx = tToX(t[m], span); }
                else
                {
                    const double u = (a - t[k]) / (t[k + 1] - t[k]);
                    bx = tToX(a, span);
                    by = floorY - ballR - (float)(4.0 * u * (1.0 - u)) * std::max(0.0f, arcH(k) - ballR);
                    // squashed for a moment on every impact
                    const double near = std::min(a - t[k], t[k + 1] - a);
                    squash = near < 0.012 ? (float)(1.0 - near / 0.012) * 0.35f : 0.0f;
                }
            }
            else if (m > 0 && a >= t[m] + 1.5) { bx = tToX(0.0, span); }
            // shadow on the floor: smaller the higher the ball is
            const float lift = pump::clampv((floorY - ballR - by) / std::max(1.0f, arcMax), 0.0f, 1.0f);
            beginPath(); ellipse(bx, floorY, ballR * (1.2f - 0.6f * lift), 2.2f); fillColor(withAlpha2(theme.ink, 0.35f - 0.2f * lift)); fill();
            beginPath(); ellipse(bx, by + ballR * squash * 0.5f, ballR * (1.0f + squash * 0.6f), ballR * (1.0f - squash)); fillColor(theme.second); fill();
            strokeColor(theme.ink); strokeWidth(1.5f); stroke();
            beginPath(); circle(bx - 2.5f, by - 2.5f, 2.2f); fillColor(withAlpha2(theme.on, 0.55f)); fill();
        }
        // read-out
        std::snprintf(buf, sizeof buf, "%d HITS   %s   FIRST GAP %.0f MS", m, m > 0 ? (std::to_string((int)std::lround(t[m] * 1000.0)) + " MS").c_str() : "0 MS", T * 1000.0);
        textAt(r.x + 10, r.b() - 11, buf, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        const bool capped = m < (int)value(kBounces);
        if (capped)
        {
            const double gapNext = T * std::pow(rho, m);
            std::snprintf(buf, sizeof buf, gapNext < kMinGapSeconds ? "SETTLES" : "CAPPED AT 8 S");
            textAt(r.r() - 10, r.b() - 11, buf, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }

    // ---- the DROP key ----
    void drawDropKey()
    {
        const Rect& r = dropR;
        const bool down = dropHeld || dropFlash > 0.6f;
        Color fillC = pump::mixColor(theme.neon, theme.ink, dropFlash);
        keyBox(r, false, down, &fillC, 4.0f);
        const Rect k = down ? Rect(r.x + 3, r.y + 3, r.w, r.h) : r;
        const Color txt = dropFlash > 0.5f ? theme.on : theme.neonInk;
        textAt(k.cx(), k.cy() - 1.0f, "DROP", kBold, 20.0f, txt, ALIGN_CENTER | ALIGN_MIDDLE, 4.0f);
        textAt(k.r() - 10, k.y + 10, "D", kMono, 9.5f, withAlpha2(txt, 0.7f), ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void dropKey(bool down)
    {
        if (down == dropHeld) return;
        dropHeld = down;
        if (down) { beginEdit(kDrop); setParam(kDrop, 1.0f); setStatus(dropMode() ? "Dropped the ball" : "The ball moves (Drop mode drops the slice)"); }
        else { setParam(kDrop, 0.0f); endEdit(kDrop); }
        repaint();
    }

    // ---- mouse: the display (first gap, gravity), the DROP key ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (dropR.contains(x, y)) { dropKey(true); drag = DragKey; return true; }
        if (!dispR.contains(x, y)) return false;
        const int pa = value(kSync) >= 0.5f ? kNote : kTime;
        if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(pa, spec(pa).def); setParamGesture(kGravity, spec(kGravity).def); endGroup(); return false; }
        drag = DragBall;
        dragX = x; dragY = y;
        dragA = norm(pa); dragNote = value(kNote); dragG = value(kGravity);
        beginEdit(pa); beginEdit(kGravity);
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (drag != DragBall) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        if (value(kSync) >= 0.5f) setParam(kNote, std::round(dragNote + (x - dragX) * 0.035f * f));
        else setParamNorm(kTime, dragA + (x - dragX) * 0.0016f * f);
        setParam(kGravity, dragG + (dragY - y) * 0.0022f * f);
        if (mod & kModifierShift) { dragA = norm(kTime); dragNote = value(kNote); dragG = value(kGravity); dragX = x; dragY = y; }
        setStatus(paramStatus(value(kSync) >= 0.5f ? kNote : kTime) + "    " + paramStatus(kGravity));
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == DragBall) { endEdit(kGravity); endEdit(value(kSync) >= 0.5f ? kNote : kTime); if (value(kSync) >= 0.5f) endEdit(kTime); else endEdit(kNote); }
        else if (drag == DragKey) dropKey(false);
        drag = DragNone;
    }
    void bodyHover(float x, float y) override
    {
        if (dispR.contains(x, y)) setStatus("Drag the display: left/right = the first gap, up/down = gravity (Shift = fine, double-click = reset; wheel = gravity)");
        else if (dropR.contains(x, y)) setStatus("DROP: drop the ball now, in Drop mode with any trigger (key D). A host-automatable parameter");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!dispR.contains(x, y)) return false;
        const float step = (mod & kModifierShift) ? 0.004f : 0.03f;
        beginEdit(kGravity); setParamNorm(kGravity, norm(kGravity) + (dy > 0 ? step : -step)); endEdit(kGravity);
        setStatus(paramStatus(kGravity));
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (key == '1' || key == '2') { setParamGesture(kMode, (float)(key - '1')); return true; }
        if (k == 'd') { dropKey(true); return true; }
        if (k == 'r') { setParamGesture(kRise, value(kRise) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (k == 's') { setParamGesture(kSync, value(kSync) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (k == 'f') { setParamGesture(kFit, value(kFit) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (k == 't') { setParamGesture(kTrigger, (float)(((int)value(kTrigger) + 1) % kNumTriggers)); return true; }
        return false;
    }
    bool keyRelease(uint key, uint) override
    {
        if (key == 'd' || key == 'D') { dropKey(false); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the display", "left/right = the first gap (a note with SYNC), up/down = gravity (double-click resets both)" },
            { "DROP / D", "drop the ball now (Drop mode, any trigger; hold to see it move)" },
            { "1 - 2", "mode: taps, drop" },
            { "R / S / F", "rise, sync, fit" },
            { "T", "next trigger: transient, sync, MIDI, button" },
        };
    }
    std::string footerInfo() override
    {
        double t[kMaxBounces + 2];
        const int m = computeTimes(t);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s   %d HITS   %.0f MS   LATENCY 0", kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)], m, m > 0 ? t[m] * 1000.0 : 0.0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    enum Drag { DragNone, DragBall, DragKey };
    PluginBounce* plug = nullptr;
    Rect dispR, plotR, meterL, meterR, ballPanel, hitPanel, trigPanel, dropR;
    OutMeter outMeter;
    float age = 1000.0f, dropFlash = 0.0f;
    uint32_t lastEvents = 0;
    bool dropHeld = false;
    Drag drag = DragNone;
    float dragX = 0, dragY = 0, dragA = 0, dragNote = 0, dragG = 0;

    Segmented *modeSeg, *trigSeg;
    Button *syncBtn, *riseBtn, *fitBtn;
    Dial *timeDial, *gravDial, *bounceDial, *sliceDial, *dampDial, *pitchDial, *toneDial, *spreadDial, *jitterDial, *fbDial, *threshDial, *everyDial, *mixDial, *outDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIBounce)
};

UI* createUI() { return new UIBounce(); }

END_NAMESPACE_DISTRHO
