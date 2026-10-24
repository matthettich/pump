// pump ECHO: interface. The timeline shows the echoes of one hit as bars along time, L above the centre and R below,
// at the real delay times (synced times follow the host tempo) and as tall as the loop leaves them (a dB scale, so
// long tails stay visible); the bars take the mode's look: crisp (Digital), wobbling with the live wow (Tape), built
// of buckets (BBD), labelled with their pitch (Pitch), or swelling backwards into the tap (Reverse). Drag the first
// L or R echo: left/right = time (snapping to note divisions with SYNC), up/down = feedback (away from the centre =
// more); the wheel zooms. Below: the delay (mode, times, feedback, sync, link, freeze), character (age, drive,
// modulation, pitch), the echoes' filters, diffusion and ducking, and the output (stereo mode, width, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginEcho.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::echo;

class UIEcho : public PumpUI {
public:
    UIEcho() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::echo(), "ECHO", 1000, 620, kBypass)
    {
        modeSeg = addSegmented(kMode, { "DIGITAL", "TAPE", "BBD", "PITCH", "REVERSE" });
        modeSeg->tip = "Mode: Digital (clean) · Tape (varispeed, wow, saturation) · Bucket-brigade (darker as it gets longer) · Pitch (shifted repeats) · Reverse (keys 1-5)";
        timeLDial = addDial(kTimeL, "TIME L", Dial::Large);
        timeRDial = addDial(kTimeR, "TIME R", Dial::Large);
        fbDial = addDial(kFeedback, "FEEDBACK", Dial::Large);
        fbDial->tip = "Feedback: how much of each echo comes back (above 100% the loop runs away into its saturation)";
        timeLDial->tabGroup = timeRDial->tabGroup = fbDial->tabGroup = 1;
        syncBtn = addToggle(kSync, "SYNC");
        syncBtn->small = true;
        syncBtn->tip = "Sync: times are note divisions at the host tempo (120 BPM without one)";
        linkBtn = addToggle(kLink, "LINK");
        linkBtn->small = true;
        linkBtn->tip = "Link: the right side follows the left";
        freezeBtn = addToggle(kFreeze, "FREEZE");
        freezeBtn->small = true;
        freezeBtn->tip = "Freeze: the loop holds what it has forever and the input stays out of it (F)";

        ageDial = addDial(kAge, "AGE");
        ageDial->tip = "Age: wear for the mode. Digital/Pitch/Reverse: an early converter · Tape: duller, more wow and hiss · Bucket-brigade: noise, aliasing, clock whine";
        driveDial = addDial(kDrive, "DRIVE");
        driveDial->tip = "Drive: the loop saturates earlier (repeats grow denser and grittier)";
        modDial = addDial(kModDepth, "MOD");
        modDial->tip = "Modulation depth: the delay time sways (chorus-like; up to 4 ms)";
        rateDial = addDial(kModRate, "RATE");
        rateDial->tip = "Modulation rate";
        ageDial->tabGroup = driveDial->tabGroup = modDial->tabGroup = rateDial->tabGroup = 2;
        pitchDial = addDial(kPitch, "PITCH", Dial::Small, true);
        pitchDial->tip = "Pitch mode: the shift of the repeats in semitones";
        fineDial = addDial(kFine, "FINE", Dial::Small, true);
        fineDial->tip = "Pitch mode: fine shift in cents (small values make a doubler)";
        pitchModeSeg = addSegmented(kPitchMode, { "ONCE", "CLIMB" });
        pitchModeSeg->vertical = true;
        pitchModeSeg->tip = "Once: every repeat shifted the same · Climb: each repeat shifts further (+12 = shimmer stairs, -12 = falling)";
        pitchDial->tabGroup = fineDial->tabGroup = 3;

        lowDial = addDial(kLowCut, "LOW CUT");
        lowDial->tip = "Low cut in the loop: each repeat gets thinner";
        highDial = addDial(kHighCut, "HIGH CUT");
        highDial->tip = "High cut in the loop: each repeat gets darker";
        diffDial = addDial(kDiffuse, "DIFFUSE", Dial::Small);
        diffDial->tip = "Diffuse: allpass smear in the loop, toward washy, reverb-like repeats (times from 50 ms)";
        duckDial = addDial(kDuck, "DUCK", Dial::Small);
        duckDial->tip = "Duck: the echoes dip while the input plays and come back when it stops (up to 30 dB)";
        lowDial->tabGroup = highDial->tabGroup = diffDial->tabGroup = duckDial->tabGroup = 4;

        stereoSeg = addSegmented(kStereo, { "STEREO", "PING-PONG", "WIDE" });
        stereoSeg->tip = "Stereo: each side its own echo · Ping-Pong: the repeats bounce L, R, L... · Wide: mono in, spread echoes";
        stereoSeg->vertical = true;
        widthDial = addDial(kWidth, "WIDTH");
        widthDial->tip = "Width of the echoes (0 = mono)";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: the dry signal stays at full level up to 50%, the echoes reach full level at 50%";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        widthDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 5;

        if (auto* p = static_cast<PluginEcho*>(pluginPtr())) plug = p;
        bindControls();
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kHighCut: if (!highCutActive(v)) return "OFF"; break;
        case kDuck: case kDiffuse: case kModDepth: case kAge: if (v < 0.5f) return "OFF"; break;
        case kPitch:
            if (std::fabs(v) < 0.5f) return "0 st";
            std::snprintf(b, sizeof b, "%+.0f st", v); return b;
        case kFine:
            if (std::fabs(v) < 0.5f) return "0 ct";
            std::snprintf(b, sizeof b, "%+.0f ct", v); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    // SYNC turns the time dials into note dials; LINK greys out the right one; the pitch controls are for Pitch mode
    void bindControls()
    {
        const bool sync = value(kSync) >= 0.5f, link = value(kLink) >= 0.5f;
        timeLDial->param = sync ? kNoteL : kTimeL;
        timeRDial->param = sync ? kNoteR : kTimeR;
        timeLDial->label = sync ? "NOTE L" : "TIME L";
        timeRDial->label = sync ? "NOTE R" : "TIME R";
        timeLDial->tip = sync ? "Left echo: a note division at the host tempo" : "Left echo time (or drag the first L echo on the timeline)";
        timeRDial->tip = link ? "Linked: the right side follows the left (LINK)" : (sync ? "Right echo: a note division at the host tempo" : "Right echo time (or drag the first R echo)");
        timeRDial->enabled = !link;
        const bool pitch = (int)value(kMode) == kModePitch;
        pitchDial->enabled = fineDial->enabled = pitchModeSeg->enabled = pitch;
    }
    void onParamChanged(int, float) override { bindControls(); }
    // a disabled key row still shows its selection in ink: wash it out
    void drawOverlay() override
    {
        if (!pitchModeSeg->enabled)
        {
            const Rect& r = pitchModeSeg->r;
            fillRect(Rect(r.x - 1, r.y - 1, r.w + 5, r.h + 5), withAlpha2(theme.bg, 0.6f));
        }
    }
    void onPresetApplied() override { bindControls(); }

    void layout() override
    {
        timeR = Rect(14, 58, 904, 300);
        plotR = Rect(timeR.x + 16, timeR.y + 30, timeR.w - 32, timeR.h - 64);
        for (int c = 0; c < 2; ++c)
        {
            meterIn[c] = Rect(934 + 11 * c, 58, 9, 284);
            meterWet[c] = Rect(966 + 11 * c, 58, 9, 284);
        }
        const float y0 = 374;
        delayPanel = Rect(14, y0, 316, 214);
        modeSeg->r = Rect(26, y0 + 28, 292, 28);
        timeLDial->r = Rect(26, y0 + 64, 92, Dial::heightFor(Dial::Large));
        timeRDial->r = Rect(126, y0 + 64, 92, Dial::heightFor(Dial::Large));
        fbDial->r = Rect(226, y0 + 64, 92, Dial::heightFor(Dial::Large));
        syncBtn->r = Rect(26, y0 + 176, 88, 24);
        linkBtn->r = Rect(128, y0 + 176, 88, 24);
        freezeBtn->r = Rect(230, y0 + 176, 88, 24);

        charPanel = Rect(344, y0, 244, 214);
        ageDial->r = Rect(350, y0 + 26, 58, Dial::heightFor(Dial::Medium));
        driveDial->r = Rect(408, y0 + 26, 58, Dial::heightFor(Dial::Medium));
        modDial->r = Rect(466, y0 + 26, 58, Dial::heightFor(Dial::Medium));
        rateDial->r = Rect(524, y0 + 26, 58, Dial::heightFor(Dial::Medium));
        pitchDial->r = Rect(350, y0 + 134, 58, Dial::heightFor(Dial::Small));
        fineDial->r = Rect(408, y0 + 134, 58, Dial::heightFor(Dial::Small));
        pitchModeSeg->r = Rect(476, y0 + 146, 98, 48);

        echoPanel = Rect(602, y0, 150, 214);
        lowDial->r = Rect(610, y0 + 26, 64, Dial::heightFor(Dial::Medium));
        highDial->r = Rect(680, y0 + 26, 64, Dial::heightFor(Dial::Medium));
        diffDial->r = Rect(610, y0 + 134, 64, Dial::heightFor(Dial::Small));
        duckDial->r = Rect(680, y0 + 134, 64, Dial::heightFor(Dial::Small));

        outPanel = Rect(766, y0, 220, 214);
        stereoSeg->r = Rect(778, y0 + 28, 112, 72);
        widthDial->r = Rect(904, y0 + 24, 72, Dial::heightFor(Dial::Medium));
        mixDial->r = Rect(792, y0 + 129, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(888, y0 + 129, 72, Dial::heightFor(Dial::Medium));
    }

    // ---- the echoes of one hit at the current settings ----
    struct Tap { double t, db; int side, n; };
    double bpmNow() const { return plug ? (double)plug->engine.tempo() : 120.0; }
    void sideSeconds(double out[2]) const
    {
        const double bpm = bpmNow();
        const bool sync = value(kSync) >= 0.5f;
        out[0] = delaySeconds(sync, (int)value(kNoteL), value(kTimeL), bpm);
        out[1] = value(kLink) >= 0.5f ? out[0] : delaySeconds(sync, (int)value(kNoteR), value(kTimeR), bpm);
    }
    // up to `span` seconds; levels in dB (0 = the input's level; above 100 % feedback the loop's ceiling holds them)
    int computeTaps(Tap* taps, int maxTaps, double span) const
    {
        double T[2];
        sideSeconds(T);
        const bool frozen = value(kFreeze) >= 0.5f;
        const double fb = frozen ? 1.0 : value(kFeedback) * 0.01;
        const double fbDb = fb > 1e-4 ? 20.0 * std::log10(fb) : -200.0;
        const int route = pump::clampv((int)value(kStereo), 0, kNumRoutes - 1);
        int n = 0;
        auto push = [&](double t, int side, int k) {
            if (n >= maxTaps || t > span) return false;
            const double d = std::min(0.0, (k - 1) * fbDb);
            if (d < -kDbRange) return false;
            taps[n++] = { t, d, side, k };
            return true;
        };
        if (route == kRoutePingPong)
        {
            double t = 0.0;
            for (int k = 1; k <= 256; ++k)
            {
                const int side = (k - 1) % 2;
                t += T[side];
                if (!push(t, side, k)) break;
            }
            return n;
        }
        const double off[2] = { 0.0, route == kRouteWide ? kWideOffsetSeconds : 0.0 };
        for (int side = 0; side < 2; ++side)
            for (int k = 1; k <= 256; ++k)
                if (!push(off[side] + k * T[side], side, k)) break;
        return n;
    }
    double spanSeconds() const
    {
        double T[2];
        sideSeconds(T);
        const bool pp = (int)value(kStereo) == kRoutePingPong;
        const double base = pp ? 2.2 * (T[0] + T[1]) : 4.4 * std::max(T[0], T[1]);
        return pump::clampv(base * zoom, 0.02, 40.0);
    }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pi = pump::gainToDbF(plug->engine.inPeak[c].exchange(0.0f));
            const float pw = pump::gainToDbF(plug->engine.wetPeak[c].exchange(0.0f));
            lvlIn[c] = std::max(pi, lvlIn[c] - 26.0f * dt);
            lvlWet[c] = std::max(pw, lvlWet[c] - 26.0f * dt);
            if (pi >= holdIn[c]) { holdIn[c] = pi; holdInT[c] = 1.5f; } else if ((holdInT[c] -= dt) <= 0.0f) holdIn[c] = std::max(pi, holdIn[c] - 18.0f * dt);
            if (pw >= holdWet[c]) { holdWet[c] = pw; holdWetT[c] = 1.5f; } else if ((holdWetT[c] -= dt) <= 0.0f) holdWet[c] = std::max(pw, holdWet[c] - 18.0f * dt);
            lag[c] = plug->engine.lagMs(c);
        }
        const float k = std::min(1.0f, dt * 10.0f);
        wobble += (plug->engine.wobbleMs() - wobble) * k;
        duck += (plug->engine.duckDb() - duck) * k;
        phase += dt;
        if (phase > 1000.0f) phase -= 1000.0f;
        // the view follows the times (gliding), but holds still while a tap is dragged
        if (!dragging) { const float want = (float)spanSeconds(); span = span <= 0.0f ? want : span + (want - span) * std::min(1.0f, dt * 8.0f); }
    }

    void drawBody() override
    {
        drawTimeline();
        for (int c = 0; c < 2; ++c)
        {
            drawLevelMeter(meterIn[c], lvlIn[c], holdIn[c], -48.0f, 6.0f);
            drawLevelMeter(meterWet[c], lvlWet[c], holdWet[c], -48.0f, 6.0f);
        }
        textAt(meterIn[0].r() + 1, meterIn[0].b() + 10, "IN", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        textAt(meterWet[0].r() + 1, meterWet[0].b() + 10, "ECHO", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(delayPanel, theme.dim, 1.0f);
        strokeRect(charPanel, theme.dim, 1.0f);
        strokeRect(echoPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(delayPanel.x + 12, delayPanel.y + 16, "DELAY");
        caption(charPanel.x + 12, charPanel.y + 16, "CHARACTER");
        const Color* pc = (int)value(kMode) == kModePitch ? nullptr : &theme.dim;
        caption(charPanel.x + 12, charPanel.y + 124, "PITCH", pc);
        caption(echoPanel.x + 12, echoPanel.y + 16, "ECHOES");
        caption(outPanel.x + 12, outPanel.y + 16, "STEREO");
        caption(outPanel.x + 12, outPanel.y + 116, "OUTPUT");
    }

    Color modeColor(int mode) const
    {
        switch (mode)
        {
        case kModeTape: return pump::mixColor(theme.neon, theme.red, 0.30f);
        case kModeBbd: return pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.25f : 0.35f);
        case kModePitch: return theme.second;
        case kModeReverse: return pump::mixColor(theme.neon, theme.second, 0.45f);
        default: return theme.neon;
        }
    }

    float tToX(double t) const { return plotR.x + (float)(t / std::max(1e-6f, span)) * plotR.w; }
    // the tallest bar (room above it for the handle, and the pitch labels in Pitch mode)
    float barMax() const { return 0.5f * plotR.h - ((int)value(kMode) == kModePitch ? 30.0f : 16.0f); }
    float dbToH(double d) const { return (float)pump::clampv(1.0 + d / kDbRange, 0.0, 1.0) * barMax(); }

    void drawTimeline()
    {
        const Rect& r = timeR;
        const Rect& p = plotR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "TIMELINE");
        const int mode = pump::clampv((int)value(kMode), 0, kNumModes - 1);
        const bool sync = value(kSync) >= 0.5f;
        char t[96];
        std::snprintf(t, sizeof t, "%s   %.1f BPM%s", kModeNames[mode], bpmNow(), sync ? "" : " (FREE)");
        std::string head = t;
        for (char& ch : head) ch = (char)std::toupper((unsigned char)ch);
        textAt(r.r() - 10, r.y + 14, head, kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);

        const float cy = p.cy();
        // grid: beats when synced (bars stronger), round times when free
        const double bpm = bpmNow();
        double step;
        bool beats = sync;
        if (beats)
        {
            const double beat = 60.0 / pump::clampv(bpm, 20.0, 999.0);
            step = beat;
            while (step / span * p.w < 36.0) step *= 2.0;
            while (step / span * p.w > 140.0 && step > beat / 8.0) step *= 0.5;
        }
        else
        {
            static const double nice[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0 };
            step = 10.0;
            for (double s : nice) if (s / span * p.w >= 60.0) { step = s; break; }
        }
        const double beatLen = 60.0 / pump::clampv(bpm, 20.0, 999.0);
        for (int i = 1; i * step <= span * 1.0001 && i < 400; ++i)
        {
            const double tt = i * step;
            const float x = tToX(tt);
            bool major = false;
            if (beats) { const double b = tt / beatLen; major = std::fabs(b / 4.0 - std::round(b / 4.0)) < 1e-6; }
            line(x, p.y, x, p.b(), major ? withAlpha2(theme.ink, theme.dark ? 0.22f : 0.20f) : theme.grid, 1.0f);
            if (beats)
            {
                const double b = tt / beatLen;
                if (std::fabs(b - std::round(b)) < 1e-6) std::snprintf(t, sizeof t, "%.0f", b);
                else std::snprintf(t, sizeof t, "%.2g", b);
            }
            else if (tt >= 1.0) std::snprintf(t, sizeof t, "%.3g s", tt);
            else std::snprintf(t, sizeof t, "%.0f ms", tt * 1000.0);
            textAt(x, p.b() + 10, t, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        textAt(p.x, p.b() + 10, beats ? "BEATS" : "0", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        line(p.x, cy, p.r(), cy, withAlpha2(theme.ink, 0.35f), 1.0f);
        textAt(p.x + 4, p.y + 8, "L", kBold, 11.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(p.x + 4, p.b() - 8, "R", kBold, 11.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);

        // the hit itself at 0
        const float hFull = barMax();
        fillRect(Rect(p.x - 1.5f, cy - hFull, 3.0f, 2.0f * hFull), withAlpha2(theme.ink, 0.55f));

        Tap taps[kMaxTaps];
        const int n = computeTaps(taps, kMaxTaps, span);
        const Color fillC = modeColor(mode);
        const bool frozen = value(kFreeze) >= 0.5f;
        const double fbv = value(kFeedback);
        const double pitchSt = value(kPitch) + 0.01 * value(kFine);
        const bool climb = (int)value(kPitchMode) == kShiftClimb;
        double T[2];
        sideSeconds(T);

        save();
        scissor(p.x, p.y, p.w + 1.0f, p.h);
        // decay envelope through the tops of each side's echoes
        for (int side = 0; side < 2; ++side)
        {
            beginPath();
            bool first = true;
            for (int i = 0; i < n; ++i)
            {
                if (taps[i].side != side) continue;
                const float x = tToX(taps[i].t), h = dbToH(taps[i].db);
                const float y = side == 0 ? cy - h : cy + h;
                if (first) { moveTo(x, y); first = false; } else lineTo(x, y);
            }
            if (!first) { strokeColor(withAlpha2(theme.ink, 0.25f)); strokeWidth(1.0f); stroke(); }
        }
        struct Handle { float x, y; bool hot; } handles[2];
        int nh = 0;
        const float wob = mode == kModeTape ? pump::clampv(std::fabs(wobble) * 4.0f + 0.8f + 0.025f * value(kAge), 0.8f, 5.0f) : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const Tap& tp = taps[i];
            const float x = tToX(tp.t), h = std::max(2.0f, dbToH(tp.db));
            const float dir = tp.side == 0 ? -1.0f : 1.0f;
            const bool firstTap = tp.n == 1 || ((int)value(kStereo) == kRoutePingPong && tp.n == 2);
            const bool hot = (firstTap && (hoverSide == tp.side || (dragging && dragSide == tp.side)));
            const float bw = firstTap ? 7.0f : 5.0f;
            const bool clipped = !frozen && fbv > 100.0 && tp.n > 1;
            const Color fc = clipped ? pump::mixColor(fillC, theme.red, 0.45f) : fillC;
            switch (mode)
            {
            case kModeTape:
            {
                // a bar that wobbles with the tape's wow (the live wobble, exaggerated)
                beginPath();
                const int segs = 10;
                for (int s = 0; s <= segs; ++s)
                {
                    const float yy = cy + dir * h * s / segs;
                    const float dx = wob * std::sin(phase * 5.2f + 0.9f * tp.n + 0.6f * s);
                    if (s == 0) moveTo(x + dx - 0.5f * bw, yy); else lineTo(x + dx - 0.5f * bw, yy);
                }
                for (int s = segs; s >= 0; --s)
                {
                    const float yy = cy + dir * h * s / segs;
                    const float dx = wob * std::sin(phase * 5.2f + 0.9f * tp.n + 0.6f * s);
                    lineTo(x + dx + 0.5f * bw, yy);
                }
                closePath();
                fillColor(fc); fill();
                strokeColor(theme.ink); strokeWidth(1.0f); stroke();
                break;
            }
            case kModeBbd:
            {
                // stacked buckets
                const float cell = 5.0f;
                const int cells = std::max(1, (int)(h / cell));
                for (int c = 0; c < cells; ++c)
                {
                    const float y0 = tp.side == 0 ? cy - (c + 1) * cell : cy + c * cell;
                    fillRect(Rect(x - 0.5f * bw, y0 + 0.5f, bw, cell - 1.0f), fc);
                }
                strokeRect(Rect(x - 0.5f * bw - 1.0f, tp.side == 0 ? cy - cells * cell - 1.0f : cy - 1.0f, bw + 2.0f, cells * cell + 2.0f), theme.ink, 1.0f);
                break;
            }
            case kModeReverse:
            {
                // the repeat plays backwards: a wedge swelling into the tap
                const float w = std::min(0.45f * (float)(T[tp.side] / std::max(1e-6f, span)) * p.w, 60.0f);
                beginPath();
                moveTo(x - w, cy);
                lineTo(x, cy + dir * h);
                lineTo(x, cy);
                closePath();
                fillColor(withAlpha2(fc, 0.85f)); fill();
                strokeColor(theme.ink); strokeWidth(1.0f); stroke();
                fillRect(Rect(x - 1.0f, std::min(cy, cy + dir * h), 2.0f, h), theme.ink);
                break;
            }
            default:
            {
                const Rect b(x - 0.5f * bw, tp.side == 0 ? cy - h : cy, bw, h);
                fillRect(b, fc);
                strokeRect(b, theme.ink, 1.0f);
                if (mode == kModePitch)
                {
                    const double st = climb ? pitchSt * tp.n : pitchSt;
                    if (std::fabs(st) >= 0.05 && (firstTap || (tToX(tp.t) - tToX(tp.t - T[tp.side]) > 26.0f)))
                    {
                        std::snprintf(t, sizeof t, std::fabs(st - std::round(st)) < 0.05 ? "%+.0f" : "%+.1f", st);
                        textAt(x, tp.side == 0 ? cy - h - 9 : cy + h + 9, t, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
                    }
                }
                break;
            }
            }
            if (firstTap && nh < 2)
            {
                handles[nh++] = { x, cy + dir * (h + (mode == kModePitch ? 22.0f : 8.0f)), hot };
                // a gliding head (Tape / bucket-brigade) on its way to a new time: where it is now
                if (std::fabs(lag[tp.side]) > 1.0f)
                {
                    const float gx = tToX(tp.t + 0.001 * lag[tp.side]);
                    line(gx, cy, gx, cy + dir * h, withAlpha2(theme.ink, 0.5f), 1.5f);
                    line(gx, cy + dir * h, x, cy + dir * h, withAlpha2(theme.ink, 0.5f), 1.0f);
                }
            }
        }
        restore();
        // the handles: a square knob past the end of each first echo
        for (int i = 0; i < nh; ++i)
        {
            if (handles[i].x > p.r() + 2.0f) continue;
            const Rect k(handles[i].x - 5.0f, handles[i].y - 5.0f, 10.0f, 10.0f);
            fillRect(k, handles[i].hot ? theme.ink : theme.screen);
            strokeRect(k, theme.ink, 1.5f);
        }
        if (frozen)
        {
            const Rect b(p.r() - 74, p.y + 2, 70, 18);
            fillRect(b, theme.ink);
            textAt(b.cx(), b.cy() + 0.5f, "FROZEN", kLabel, 10.5f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
        }
        if (value(kDuck) > 0.5f && duck < -0.5f)
        {
            std::snprintf(t, sizeof t, "DUCK %.0f dB", duck);
            textAt(p.r() - (frozen ? 84 : 4), p.y + 11, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }

        // readout
        auto sideText = [&](int c) {
            char b[48];
            if (sync) std::snprintf(b, sizeof b, "%s  %s", pump::kNoteNames[pump::clampv((int)value(c ? kNoteR : kNoteL), 0, pump::kNumNotes - 1)], formatMs(T[c]).c_str());
            else std::snprintf(b, sizeof b, "%s", formatMs(T[c]).c_str());
            return std::string(b);
        };
        std::string ro = "L " + sideText(0) + "    R " + (value(kLink) >= 0.5f ? std::string("= L") : sideText(1));
        textAt(r.x + 10, r.b() - 11, ro, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "FEEDBACK %.0f%%   %s", fbv, kRouteNames[pump::clampv((int)value(kStereo), 0, kNumRoutes - 1)]);
        std::string fr = t;
        for (char& ch : fr) ch = (char)std::toupper((unsigned char)ch);
        textAt(r.r() - 10, r.b() - 11, fr, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    static std::string formatMs(double s)
    {
        char b[24];
        if (s >= 1.0) std::snprintf(b, sizeof b, "%.2f s", s);
        else if (s >= 0.1) std::snprintf(b, sizeof b, "%.0f ms", s * 1000.0);
        else std::snprintf(b, sizeof b, "%.1f ms", s * 1000.0);
        return b;
    }

    // ---- the first echoes are handles: left/right = time, up/down (away from the centre) = feedback ----
    // which side's first echo is at (x, y) (-1: none); `exact` = on the handle itself, else the half it is in
    int sideAt(float x, float y, bool& exact)
    {
        exact = false;
        if (!plotR.contains(x, y) && !timeR.contains(x, y)) return -1;
        const bool link = value(kLink) >= 0.5f;
        const bool pp = (int)value(kStereo) == kRoutePingPong;
        double T[2];
        sideSeconds(T);
        const double off = (int)value(kStereo) == kRouteWide ? kWideOffsetSeconds : 0.0;
        const float xl = tToX(T[0]), xr = tToX(pp ? T[0] + T[1] : off + T[1]);
        const int side = y < plotR.cy() ? 0 : 1;
        const float xs = side == 0 ? xl : xr;
        exact = std::fabs(x - xs) < 12.0f;
        if (side == 1 && link && !pp) return 0;   // linked: the right echo moves the left time
        return side;
    }
    // free times go to 4 s; synced ones to the longest note (2/1 at 60 BPM = 8 s)
    double maxSeconds() const { return value(kSync) >= 0.5f ? kMaxSeconds : 0.001 * kTimeMaxMs; }
    int timeParam(int side) const { return value(kSync) >= 0.5f ? (side ? kNoteR : kNoteL) : (side ? kTimeR : kTimeL); }
    void setTimeFromSeconds(int side, double secs)
    {
        if (value(kSync) >= 0.5f)
        {
            // the nearest note division at the tempo (on a log scale)
            const double bpm = bpmNow();
            int best = 0; double bd = 1e9;
            for (int i = 0; i < pump::kNumNotes; ++i)
            {
                const double d = std::fabs(std::log(pump::noteSeconds(i, bpm) / std::max(1e-4, secs)));
                if (d < bd) { bd = d; best = i; }
            }
            setParam(side ? kNoteR : kNoteL, (float)best);
        }
        else setParam(side ? kTimeR : kTimeL, (float)(secs * 1000.0));
    }
    // the time the first echo of `side` should sit at for a handle at x (ping-pong: R comes after L)
    double secondsAtX(int side, float x) const
    {
        double t = (x - plotR.x) / plotR.w * span;
        double T[2];
        sideSeconds(T);
        if (side == 1 && (int)value(kStereo) == kRoutePingPong) t -= T[0];
        if (side == 1 && (int)value(kStereo) == kRouteWide) t -= kWideOffsetSeconds;
        return pump::clampv(t, 0.001, maxSeconds());
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!timeR.contains(x, y) || button != kMouseButtonLeft) return false;
        bool exact = false;
        const int side = sideAt(x, y, exact);
        if (side < 0) return false;
        if (dbl || cmdKey(mod))
        {
            beginGroup();
            setParamGesture(side ? kTimeR : kTimeL, spec(side ? kTimeR : kTimeL).def);
            setParamGesture(side ? kNoteR : kNoteL, spec(side ? kNoteR : kNoteL).def);
            setParamGesture(kFeedback, spec(kFeedback).def);
            endGroup();
            return false;
        }
        dragging = true; dragSide = side; dragX = x; dragY = y;
        dragFb = value(kFeedback);
        beginEdit(timeParam(side)); beginEdit(kFeedback);
        // clicking off the handle puts the echo where the mouse is
        if (!exact) setTimeFromSeconds(side, secondsAtX(side, x));
        double T[2];
        sideSeconds(T);
        dragT = T[side];
        dragX = x;
        setStatus(paramStatus(timeParam(side)) + "    " + paramStatus(kFeedback));
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (!dragging) return;
        const bool fine = (mod & kModifierShift) != 0;
        const float f = fine ? 0.15f : 1.0f;
        const double secs = pump::clampv(dragT + (x - dragX) / plotR.w * span * f, 0.001, maxSeconds());
        setTimeFromSeconds(dragSide, secs);
        const float dy = dragSide == 0 ? (dragY - y) : (y - dragY);
        setParam(kFeedback, dragFb + dy * 0.45f * f);
        if (fine) { dragT = secs; dragFb = value(kFeedback); dragX = x; dragY = y; }
        setStatus(paramStatus(timeParam(dragSide)) + "    " + paramStatus(kFeedback));
    }
    void bodyMouseUp(float, float) override
    {
        if (!dragging) return;
        dragging = false;
        endEdit(kFeedback); endEdit(timeParam(dragSide));
    }
    void bodyHover(float x, float y) override
    {
        hoverSide = -1;
        if (timeR.contains(x, y))
        {
            bool exact = false;
            const int side = sideAt(x, y, exact);
            if (exact) hoverSide = side;
            setStatus(std::string(side == 1 ? "R" : "L") + " echo: drag sideways = time" + (value(kSync) >= 0.5f ? " (snaps to notes)" : "")
                      + ", up/down = feedback; wheel = zoom");
        }
        else if (meterIn[0].contains(x, y) || meterIn[1].contains(x, y)) setStatus("Input level, L and R");
        else if (meterWet[0].contains(x, y) || meterWet[1].contains(x, y)) setStatus("Echo level, L and R (after duck and width)");
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (!timeR.contains(x, y)) return false;
        zoom = pump::clampv(zoom * (dy > 0 ? 0.8f : 1.25f), 0.25f, 4.0f);
        saveUi();
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '5') { setParamGesture(kMode, (float)(key - '1')); return true; }
        if (key == 'f' || key == 'F') { setParamGesture(kFreeze, value(kFreeze) >= 0.5f ? 0.0f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag a first echo", "left/right = time (snaps to notes with SYNC), up/down = feedback" },
            { "Click the timeline", "put the first L (upper half) or R (lower half) echo there" },
            { "Double-click an echo", "reset its time and the feedback" },
            { "Wheel on the timeline", "zoom" },
            { "1 - 5", "mode: digital, tape, bucket-brigade, pitch, reverse" },
            { "F", "freeze" },
        };
    }
    std::string footerInfo() override
    {
        double T[2];
        sideSeconds(T);
        const bool sync = value(kSync) >= 0.5f;
        std::string times;
        if (sync)
        {
            times = pump::kNoteNames[pump::clampv((int)value(kNoteL), 0, pump::kNumNotes - 1)];
            if (value(kLink) < 0.5f) times += std::string(" / ") + pump::kNoteNames[pump::clampv((int)value(kNoteR), 0, pump::kNumNotes - 1)];
            char b[24]; std::snprintf(b, sizeof b, " AT %.0f BPM", bpmNow()); times += b;
        }
        else times = formatMs(T[0]) + (value(kLink) < 0.5f ? " / " + formatMs(T[1]) : std::string());
        std::string s = std::string(kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)]) + "   "
                        + kRouteNames[pump::clampv((int)value(kStereo), 0, kNumRoutes - 1)] + "   " + times;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }
    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "zoom=%.3g;", zoom); saveUiState(buf); }
    void loadUiState(const std::string& s) override { zoom = pump::clampv((float)std::atof(uiStateGet(s, "zoom", "1").c_str()), 0.25f, 4.0f); }

private:
    static constexpr int kMaxTaps = 160;
    static constexpr double kDbRange = 48.0;   // the bars show the echoes down to -48 dB
    PluginEcho* plug = nullptr;
    Rect timeR, plotR, meterIn[2], meterWet[2], delayPanel, charPanel, echoPanel, outPanel;
    float lvlIn[2] = { -120.0f, -120.0f }, holdIn[2] = { -120.0f, -120.0f }, holdInT[2] = { 0.0f, 0.0f };
    float lvlWet[2] = { -120.0f, -120.0f }, holdWet[2] = { -120.0f, -120.0f }, holdWetT[2] = { 0.0f, 0.0f };
    float lag[2] = { 0.0f, 0.0f }, wobble = 0.0f, duck = 0.0f, phase = 0.0f, span = 0.0f, zoom = 1.0f;
    bool dragging = false;
    int dragSide = 0, hoverSide = -1;
    float dragX = 0, dragY = 0, dragFb = 0;
    double dragT = 0.5;

    Segmented *modeSeg, *pitchModeSeg, *stereoSeg;
    Dial *timeLDial, *timeRDial, *fbDial, *ageDial, *driveDial, *modDial, *rateDial, *pitchDial, *fineDial;
    Dial *lowDial, *highDial, *diffDial, *duckDial, *widthDial, *mixDial, *outDial;
    Button *syncBtn, *linkBtn, *freezeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIEcho)
};

UI* createUI() { return new UIEcho(); }

END_NAMESPACE_DISTRHO
