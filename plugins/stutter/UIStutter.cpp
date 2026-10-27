// pump STUTTER: interface. TIMELINE (the last few seconds of output, coloured where a move played, named), eight
// PADS (hold one to play its move; the small AUTO box in a pad lets the dice pick it), and NOW (the move playing,
// its repeat count and progress, and the bar on the grid). Below: auto (trigger, interval, chance, length,
// variation), repeats (grid, gate, pitch, decay, filter, tape stop time) and output (mix, output). PATTERN: the step
// editor (a row per move, a column per step: click to place, drag right for the length; each step's transposition
// and own grid underneath), which pattern, its step and length.
#include "ui/PumpUI.hpp"
#include "PluginStutter.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::stutter;
using pump::kNoteNames;
using pump::kNumNotes;

class UIStutter : public PumpUI {
public:
    UIStutter() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::stutter(), "STUTTER", 1000, 800, kBypass)
    {
        trigSeg = addSegmented(kTrigger, { "AUTO", "MANUAL" });
        trigSeg->tip = "Auto: the dice roll at every interval (pads and MIDI always work). Manual: only pads and MIDI";
        intMenu = addMenu(kInterval);
        intMenu->arrows = true;
        intMenu->tip = "Interval: how often the dice roll (on the song's grid)";
        lenMenu = addMenu(kLength);
        lenMenu->arrows = true;
        lenMenu->tip = "Length of an auto move";
        chanceDial = addDial(kChance, "CHANCE");
        chanceDial->tip = "Chance that a move starts at each interval";
        varDial = addDial(kVariation, "VARIATION");
        varDial->tip = "Variation: each auto move may get a half or double grid, pitch up or down, reversed repeats, wilder glitches";
        chanceDial->tabGroup = varDial->tabGroup = 1;

        gridMenu = addMenu(kGrid);
        gridMenu->arrows = true;
        gridMenu->tip = "Grid: the slice that repeats (and the step of roll, shuffle, gate and glitch)";
        gateDial = addDial(kGate, "GATE", Dial::Small);
        gateDial->tip = "Gate: how much of each slice sounds";
        pitchDial = addDial(kPitch, "PITCH", Dial::Small, true);
        pitchDial->tip = "Pitch per repeat, in semitones (repeat and roll)";
        decayDial = addDial(kDecay, "DECAY", Dial::Small);
        decayDial->tip = "Level lost per repeat";
        filterDial = addDial(kFilter, "FILTER", Dial::Small, true);
        filterDial->tip = "Filter per repeat: left = a low-pass closing, right = a high-pass opening";
        tapeDial = addDial(kTape, "TAPE", Dial::Small);
        tapeDial->tip = "Tape stop: how much of the move the slow-down takes";
        gateDial->tabGroup = pitchDial->tabGroup = decayDial->tabGroup = filterDial->tabGroup = tapeDial->tabGroup = 2;

        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: how much a move replaces the live signal (100% = all of it)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        patOn = addToggle(kPatternOn, "PATTERN");
        patOn->tip = "Play the pattern below (instead of the dice) while the song plays";
        patMenu = addMenu(kPatternSel); patMenu->arrows = true;
        patMenu->tip = "A factory pattern, or USER (yours: editing a factory one copies it to USER)";
        stepMenu = addMenu(kPatStep); stepMenu->arrows = true;
        stepMenu->tip = "The pattern's step";
        lenDown = addButton("-", [this] { editPattern([](Pattern& q) { q.length = std::max(1, q.length - 1); }); });
        lenUp = addButton("+", [this] { editPattern([](Pattern& q) { q.length = std::min(kMaxSteps, q.length + 1); }); });
        clearB = addButton("CLEAR", [this] { editPattern([](Pattern& q) { for (Step& st : q.s) st = Step { -1, 1, 0, 0 }; }); });
        lenDown->tip = lenUp->tip = "The pattern's length in steps";
        if (auto* p = static_cast<PluginStutter*>(pluginPtr())) { plug = p; user = p->userPattern(); }
    }
    ~UIStutter() override { if (plug) plug->padHeld.store(-1); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kPitch:
            if (std::fabs(v) < 0.05f) return "0 ST";
            std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.05f ? "%+.0f ST" : "%+.1f ST", v); return b;
        case kFilter:
            if (std::fabs(v) < 0.5f) return "OFF";
            std::snprintf(b, sizeof b, v < 0 ? "LP %.0f%%" : "HP %.0f%%", std::fabs(v)); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        // top: TIMELINE over the eight pads, NOW and the meters beside them
        const float top = 58, th = 130, pads = 140, sh = th + 12 + pads;
        timeR = Rect(14, top, 708, th);
        padsR = Rect(14, top + th + 12, 708, pads);
        nowR = Rect(738, top, 210, sh);
        meterL = Rect(962, top, 9, sh - 18);
        meterR = Rect(973, top, 9, sh - 18);
        const float pw = (padsR.w - 3 * 12) / 4, ph = (padsR.h - 12) / 2;
        for (int m = 0; m < kNumMoves; ++m)
        {
            pad[m] = Rect(padsR.x + (m % 4) * (pw + 12), padsR.y + (m / 4) * (ph + 12), pw, ph);
            useBox[m] = Rect(pad[m].r() - 52, pad[m].y + 8, 44, 16);
        }
        // AUTO | REPEATS | OUTPUT: menus on top (captioned), dials under
        const float y0 = top + sh + 12, h0 = 164, hm = Dial::heightFor(Dial::Medium), hs = Dial::heightFor(Dial::Small);
        autoPanel = Rect(14, y0, 440, h0);
        trigSeg->r = Rect(26, y0 + 44, 128, 24);
        intMenu->r = Rect(166, y0 + 44, 132, 24);
        lenMenu->r = Rect(310, y0 + 44, 132, 24);
        chanceDial->r = Rect(170, y0 + 78, 72, hm);
        varDial->r = Rect(314, y0 + 78, 72, hm);

        repPanel = Rect(468, y0, 308, h0);
        gridMenu->r = Rect(480, y0 + 44, 140, 24);
        const float c0 = 476, cw = 59;
        gateDial->r = Rect(c0, y0 + 84, 56, hs);
        pitchDial->r = Rect(c0 + cw, y0 + 84, 56, hs);
        decayDial->r = Rect(c0 + 2 * cw, y0 + 84, 56, hs);
        filterDial->r = Rect(c0 + 3 * cw, y0 + 84, 56, hs);
        tapeDial->r = Rect(c0 + 4 * cw, y0 + 84, 56, hs);

        outPanel = Rect(790, y0, 196, h0);
        mixDial->r = Rect(802, y0 + 50, 72, hm);
        outDial->r = Rect(902, y0 + 50, 72, hm);

        // PATTERN: its controls, then the step editor (moves, transposition and grid rows under it)
        const float y1 = y0 + h0 + 12;
        patPanel = Rect(14, y1, 972, 240);
        patOn->r = Rect(26, y1 + 28, 110, 24);
        patMenu->r = Rect(148, y1 + 28, 210, 24);
        stepMenu->r = Rect(370, y1 + 28, 110, 24);
        lenDown->r = Rect(560, y1 + 28, 28, 24); lenUp->r = Rect(650, y1 + 28, 28, 24);
        clearB->r = Rect(900, y1 + 28, 76, 24);
        gridR = Rect(110, y1 + 62, 866, 8 * 16);
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
        const uint32_t end = plug->engine.levelRing.smoothPosition();
        for (int i = 0; i < kStrip; ++i)
        {
            const uint32_t idx = end - (uint32_t)kStrip + (uint32_t)i;
            strip[i] = plug->engine.levelRing.at(idx);
            stripMove[i] = plug->engine.moveRing.at(idx);
        }
        moveShow = plug->engine.moveNow();
        heldShow = plug->engine.heldNow();
        repShow = plug->engine.repeatNow();
        progShow = plug->engine.progressNow();
        barShow = plug->engine.barPhase();
        for (int m = 0; m < kNumMoves; ++m) flash[m] = m == moveShow ? 1.0f : std::max(0.0f, flash[m] - dt * 4.0f);
    }

    void drawBody() override
    {
        drawTimeline();
        drawPads();
        drawNow();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, nowR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(autoPanel, theme.dim, 1.0f);
        strokeRect(repPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(autoPanel.x + 12, autoPanel.y + 16, "AUTO");
        caption(trigSeg->r.x, trigSeg->r.y - 10, "TRIGGER");
        caption(intMenu->r.x, intMenu->r.y - 10, "EVERY");
        caption(lenMenu->r.x, lenMenu->r.y - 10, "LENGTH");
        caption(repPanel.x + 12, repPanel.y + 16, "REPEATS");
        caption(gridMenu->r.x, gridMenu->r.y - 10, "GRID");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        drawPattern();
        // the auto summary, in words (on the caption line); how MIDI plays the moves, beside the dials
        char t[96];
        std::snprintf(t, sizeof t, "%.0f%% EVERY %s, FOR %s", value(kChance), kNoteNames[pump::clampv((int)value(kInterval), 0, kNumNotes - 1)],
                      kNoteNames[pump::clampv((int)value(kLength), 0, kNumNotes - 1)]);
        if (value(kTrigger) >= 0.5f) std::snprintf(t, sizeof t, "PADS, KEYS 1-8 AND MIDI ONLY");
        textAt(autoPanel.r() - 12, autoPanel.y + 16, t, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        textAt(26, chanceDial->r.cy() - 9, "MIDI: C C# D D# E F F# G", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        textAt(26, chanceDial->r.cy() + 9, "(ANY OCTAVE) HOLD MOVES", kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
    }

    void drawTimeline()
    {
        const Rect& r = timeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "TIMELINE");
        const double secs = plug ? kStrip * 64.0 / plug->getSampleRate() : 4.0;
        char t[24]; std::snprintf(t, sizeof t, "LAST %.1f s", secs);
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p(r.x + 10, r.y + 28, r.w - 20, r.h - 38);
        const float cy = p.cy();
        line(p.x, cy, p.r(), cy, theme.grid, 1.0f);
        float mx = 0.05f;
        for (int i = 0; i < kStrip; ++i) mx = std::max(mx, strip[i]);
        const float k = 0.95f / mx;
        // move spans behind, then the level as mirrored bars
        int i = 0;
        while (i < kStrip)
        {
            const int mv = (int)std::lround(stripMove[i]);
            int j = i;
            while (j < kStrip && (int)std::lround(stripMove[j]) == mv) ++j;
            if (mv >= 0 && mv < kNumMoves)
            {
                const float x0 = p.x + p.w * i / kStrip, x1 = p.x + p.w * j / kStrip;
                fillRect(Rect(x0, p.y, x1 - x0, p.h), withAlpha2(theme.neon, theme.dark ? 0.22f : 0.40f));
                line(x0, p.y, x0, p.b(), theme.ink, 1.2f);
                if (x1 - x0 > 30.0f) textAt(x0 + 4, p.y + 8, kMoveKeys[mv], kLabel, 9.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            }
            i = j;
        }
        const float bw = p.w / kStrip;
        beginPath();
        for (int s = 0; s < kStrip; ++s)
        {
            const float h = std::min(1.0f, strip[s] * k) * p.h * 0.5f;
            rect(p.x + bw * s, cy - h, std::max(1.0f, bw), 2 * h);
        }
        fillColor(withAlpha2(theme.ink, 0.75f));
        fill();
    }

    void drawPads()
    {
        static const char* const notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G" };
        for (int m = 0; m < kNumMoves; ++m)
        {
            const Rect& r = pad[m];
            const bool playing = moveShow == m, pressed = padDown == m;
            const Color fillC = playing ? theme.ink : pump::mixColor(theme.bg, theme.neon, 0.35f * flash[m]);
            keyBox(r, playing, pressed, &fillC, 4.0f);
            const Color tc = playing ? theme.on : theme.ink;
            textAt(r.x + 12, r.cy() + 6, kMoveKeys[m], kBold, 15.0f, tc, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
            char k[16]; std::snprintf(k, sizeof k, "%d  ·  %s", m + 1, notes[m]);
            textAt(r.x + 12, r.y + 16, k, kMono, 9.5f, playing ? theme.on : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            // the AUTO switch of this move
            const bool use = value(kUse0 + m) >= 0.5f;
            const Rect& u = useBox[m];
            fillRect(u, use ? (playing ? theme.on : theme.ink) : (playing ? theme.ink : theme.bg));
            strokeRect(u, playing ? theme.on : theme.ink, 1.0f);
            textAt(u.cx(), u.cy() + 0.5f, "AUTO", kLabel, 8.5f, use ? (playing ? theme.ink : theme.on) : (playing ? theme.on : theme.ink2), ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
            if (playing)
            {
                const Rect pb(r.x + 12, r.b() - 12, r.w - 24, 4);
                fillRect(pb, withAlpha2(theme.on, 0.3f));
                fillRect(Rect(pb.x, pb.y, pb.w * progShow, pb.h), theme.on);
            }
        }
    }

    void drawNow()
    {
        const Rect& r = nowR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "NOW");
        textAt(r.r() - 10, r.y + 14, moveShow >= 0 ? (heldShow ? "HELD" : "AUTO") : "LIVE", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const bool on = moveShow >= 0 && moveShow < kNumMoves;
        const Rect big(r.x + 12, r.y + 34, r.w - 24, 64);
        fillRect(big, on ? theme.neon : theme.panel);
        strokeRect(big, theme.ink, 1.5f);
        textAt(big.cx(), big.cy(), on ? kMoveKeys[moveShow] : "LIVE", kBold, on ? 20.0f : 18.0f, on ? theme.neonInk : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
        char t[48];
        if (on && (moveShow == kRepeat || moveShow == kRoll || moveShow == kShuffle || moveShow == kGlitch))
        {
            std::snprintf(t, sizeof t, "REPEAT %d", repShow + 1);
            textAt(r.x + 12, big.b() + 16, t, kMono, 11.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // the bar on the grid: a cell per grid step (up to 32), the interval boundaries thicker
        const double gq = pump::noteQuarters((int)value(kGrid)), iq = pump::noteQuarters((int)value(kInterval));
        const int cells = pump::clampv((int)std::lround(4.0 / gq), 1, 32);
        const Rect bar(r.x + 12, r.y + 150, r.w - 24, 28);
        caption(bar.x, bar.y - 10, "BAR / GRID");
        const float cw = bar.w / cells;
        const int cur = pump::clampv((int)(barShow * cells), 0, cells - 1);
        for (int c = 0; c < cells; ++c)
        {
            const Rect cr(bar.x + c * cw, bar.y, cw, bar.h);
            fillRect(cr, c == cur ? theme.neon : theme.panel2);
            strokeRect(cr, theme.ink, 0.8f);
        }
        if (iq < 4.0)
            for (double q = iq; q < 4.0 - 1e-6; q += iq) { const float x = bar.x + bar.w * (float)(q / 4.0); line(x, bar.y - 3, x, bar.b() + 3, theme.ink, 2.0f); }
        // what auto may pick
        caption(r.x + 12, r.y + 204, "AUTO PICKS FROM");
        float y = r.y + 222;
        int shown = 0;
        for (int m = 0; m < kNumMoves; ++m)
            if (value(kUse0 + m) >= 0.5f)
            {
                textAt(r.x + 12 + (shown % 2) * 96, y + (shown / 2) * 16, kMoveKeys[m], kLabel, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
                ++shown;
            }
        if (!shown) textAt(r.x + 12, y, "NOTHING (PADS ONLY)", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
    }

    // ---- the pattern editor ----
    const Pattern& shown() const { const int sel = (int)value(kPatternSel); return sel >= kUserPattern ? user : factoryPattern(sel); }
    // an edit: a factory pattern is copied to USER first; the result is sent to the plugin as text
    template <class F> void editPattern(F&& f)
    {
        const int sel = (int)value(kPatternSel);
        if (sel < kUserPattern) { user = factoryPattern(sel); setParamGesture(kPatternSel, (float)kUserPattern); }
        f(user);
        setState("pattern", patternToText(user).c_str());
    }
    void onExtraState(const char* key, const char* value) override { if (std::strcmp(key, "pattern") == 0) user = patternFromText(value); }
    float colW(const Pattern& p) const { return gridR.w / p.length; }
    // which step a cell belongs to (the step whose move covers it), or -1
    int owner(const Pattern& p, int col) const
    {
        for (int i = col; i >= 0; --i) if (p.s[i].move >= 0) return i + p.s[i].len > col ? i : -1;
        return -1;
    }
    void drawPattern()
    {
        strokeRect(patPanel, theme.dim, 1.0f);
        caption(patPanel.x + 12, patPanel.y + 16, "PATTERN");
        const Pattern& p = shown();
        char b[48]; std::snprintf(b, sizeof b, "%d STEPS", p.length);
        textAt((lenDown->r.r() + lenUp->r.x) * 0.5f, lenUp->r.cy(), b, kLabel, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        textAt(lenUp->r.r() + 12, lenUp->r.cy(), "CLICK A CELL · DRAG RIGHT FOR LENGTH", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        const float cw = colW(p), rh = gridR.h / kNumMoves;
        const int now = plug ? plug->engine.patternStepNow() : -1;
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.35f);
        for (int m = 0; m < kNumMoves; ++m) textAt(gridR.x - 8, gridR.y + rh * (m + 0.5f), kMoveKeys[m], kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        for (int c = 0; c < p.length; ++c)
        {
            const float x = gridR.x + c * cw;
            if (c == now && value(kPatternOn) >= 0.5f) fillRect(Rect(x, gridR.y, cw, gridR.h + 40), withAlpha2(theme.ink, 0.10f));
            for (int m = 0; m < kNumMoves; ++m) strokeRect(Rect(x, gridR.y + m * rh, cw, rh), (c % 4 == 0) ? theme.dim : withAlpha2(theme.dim, 0.5f), 1.0f);
        }
        // the moves: a block from their step for their length
        for (int c = 0; c < p.length; ++c)
        {
            const Step& st = p.s[c];
            if (st.move < 0) continue;
            const int len = std::min(st.len, p.length - c);
            const Rect blk(gridR.x + c * cw + 1, gridR.y + st.move * rh + 1, len * cw - 2, rh - 2);
            fillRect(blk, hot);
            if (blk.w > 34) textAt(blk.x + 4, blk.cy(), kMoveKeys[st.move], kLabel, 8.0f, theme.neonInk, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        }
        // the rows under: each step's transposition and its own grid
        const float ty = gridR.b() + 4, gy = gridR.b() + 22;
        textAt(gridR.x - 8, ty + 8, "TRANSP", kLabel, 8.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        textAt(gridR.x - 8, gy + 8, "GRID", kLabel, 8.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
        for (int c = 0; c < p.length; ++c)
        {
            const Step& st = p.s[c];
            const float x = gridR.x + c * cw;
            strokeRect(Rect(x, ty, cw, 16), theme.dim, 1.0f); strokeRect(Rect(x, gy, cw, 16), theme.dim, 1.0f);
            if (st.move < 0) continue;
            char t[12];
            if (st.transpose) { std::snprintf(t, sizeof t, "%+d", st.transpose); textAt(x + cw * 0.5f, ty + 8, t, kMono, 8.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE); }
            textAt(x + cw * 0.5f, gy + 8, st.grid ? kNoteNames[pump::clampv(st.grid - 1, 0, kNumNotes - 1)] : "-", kMono, 8.0f, st.grid ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }
    bool patternDown(float x, float y, int button)
    {
        const Pattern& p = shown();
        const float cw = colW(p), rh = gridR.h / kNumMoves;
        const int c = (int)((x - gridR.x) / cw);
        if (x < gridR.x || c < 0 || c >= p.length) return false;
        const float ty = gridR.b() + 4, gy = gridR.b() + 22;
        if (gridR.contains(x, y))
        {
            const int m = pump::clampv((int)((y - gridR.y) / rh), 0, kNumMoves - 1);
            const int o = owner(p, c);
            if (button == kMouseButtonRight) { if (o >= 0) editPattern([o](Pattern& q) { q.s[o].move = -1; }); return true; }
            if (o >= 0 && p.s[o].move == m)
            {
                // on its own block: a click on its first step clears it; anywhere: drag right to set the length
                dragStep = o; dragMoved = false; clearOnUp = o == c && p.s[o].len == 1;
                return true;
            }
            editPattern([c, m](Pattern& q) { q.s[c].move = m; q.s[c].len = 1; });
            dragStep = c; dragMoved = false; clearOnUp = false;
            return true;
        }
        if (y >= ty && y < ty + 16 && p.s[c].move >= 0)
        {
            if (button == kMouseButtonRight) { editPattern([c](Pattern& q) { q.s[c].transpose = 0; }); return true; }
            trStep = c; trY = y; trStart = p.s[c].transpose; return true;
        }
        if (y >= gy && y < gy + 16 && p.s[c].move >= 0)
        {
            // cycle the step's own grid: the GRID control's, 1/32, 1/16, 1/8, 1/4
            static const int cyc[5] = { 0, pump::kNote32 + 1, pump::kNote16 + 1, pump::kNote8 + 1, pump::kNote4 + 1 };
            int k = 0; for (int i = 0; i < 5; ++i) if (cyc[i] == p.s[c].grid) k = i;
            const int nx = cyc[(k + (button == kMouseButtonRight ? 4 : 1)) % 5];
            editPattern([c, nx](Pattern& q) { q.s[c].grid = nx; });
            return true;
        }
        return false;
    }
    void patternDrag(float x, float y)
    {
        const Pattern& p = shown();
        if (dragStep >= 0)
        {
            const int c = pump::clampv((int)((x - gridR.x) / colW(p)), 0, p.length - 1);
            const int len = std::max(1, c - dragStep + 1);
            if (len != p.s[dragStep].len) { dragMoved = true; const int d = dragStep; editPattern([d, len](Pattern& q) { q.s[d].len = len; }); }
        }
        if (trStep >= 0)
        {
            const int v = pump::clampv(trStart + (int)std::lround((trY - y) / 8.0f), -24, 24);
            if (v != p.s[trStep].transpose) { const int d = trStep; editPattern([d, v](Pattern& q) { q.s[d].transpose = v; }); }
        }
    }
    void patternUp()
    {
        if (dragStep >= 0 && clearOnUp && !dragMoved) { const int d = dragStep; editPattern([d](Pattern& q) { q.s[d].move = -1; }); }
        dragStep = trStep = -1;
    }

    // ---- pads: hold to play; the AUTO box toggles that move for auto ----
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (patPanel.contains(x, y) && patternDown(x, y, button)) return true;
        if (button != kMouseButtonLeft) return false;
        for (int m = 0; m < kNumMoves; ++m)
        {
            if (useBox[m].contains(x, y)) { setParamGesture(kUse0 + m, value(kUse0 + m) >= 0.5f ? 0.0f : 1.0f); return false; }
            if (pad[m].contains(x, y)) { press(m); return true; }
        }
        return false;
    }
    void bodyMouseUp(float, float) override { release(); patternUp(); }
    void bodyMouseDrag(float x, float y, uint) override { patternDrag(x, y); }
    void press(int m) { padDown = m; if (plug) plug->padHeld.store(m); setStatus(std::string(kMoveNames[m]) + " (held)"); }
    void release() { padDown = -1; keyDown = -1; if (plug) plug->padHeld.store(-1); }
    void bodyHover(float x, float y) override
    {
        for (int m = 0; m < kNumMoves; ++m)
        {
            if (useBox[m].contains(x, y)) { setStatus(std::string("Let auto pick ") + kMoveNames[m]); return; }
            if (pad[m].contains(x, y)) { setStatus(std::string("Hold to play ") + kMoveNames[m] + " (also key " + std::to_string(m + 1) + " or its MIDI note)"); return; }
        }
        if (timeR.contains(x, y)) setStatus("Timeline: the last seconds of output; shaded where a move played");
        else if (nowR.contains(x, y)) setStatus("Now: the move playing, its repeat, and the bar on the grid (thick lines = where auto rolls the dice)");
        else if (gridR.contains(x, y)) setStatus("Click a cell: that move starts on that step (click it again to clear; right-click clears). Drag right: how many steps it lasts");
        else if (patPanel.contains(x, y) && y > gridR.b() && y < gridR.b() + 20) setStatus("The step's transposition: drag up or down (right-click: 0). Repeat and roll start transposing from their second repeat");
        else if (patPanel.contains(x, y) && y > gridR.b() + 20) setStatus("The step's own grid: click to cycle (-: the GRID control's)");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '8') { keyDown = (int)(key - '1'); press(keyDown); return true; }
        return false;
    }
    bool keyRelease(uint key, uint) override
    {
        if (key >= '1' && key <= '8' && keyDown == (int)(key - '1')) { release(); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Hold a pad, or keys 1 - 8", "play that move while held" },
            { "MIDI C C# D D# E F F# G", "the same eight moves, any octave" },
            { "AUTO box on a pad", "let the dice pick that move" },
            { "PATTERN", "click cells to place moves, drag right for their length" },
        };
    }
    std::string footerInfo() override
    {
        std::string s = value(kPatternOn) >= 0.5f ? std::string("PATTERN ") + kPatternNames[pump::clampv((int)value(kPatternSel), 0, kUserPattern)] : (value(kTrigger) >= 0.5f ? "MANUAL" : "AUTO");
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        s += std::string("   GRID ") + kNoteNames[pump::clampv((int)value(kGrid), 0, kNumNotes - 1)];
        s += moveShow >= 0 ? std::string("   ") + kMoveKeys[moveShow] : std::string("   LIVE");
        return s;
    }

private:
    static constexpr int kStrip = 3000;
    PluginStutter* plug = nullptr;
    Rect timeR, padsR, nowR, meterL, meterR, autoPanel, repPanel, outPanel, patPanel, gridR;
    Button *patOn, *lenDown, *lenUp, *clearB;
    MenuButton *patMenu, *stepMenu;
    Pattern user;
    int dragStep = -1, trStep = -1, trStart = 0;
    float trY = 0.0f;
    bool dragMoved = false, clearOnUp = false;
    Rect pad[kNumMoves], useBox[kNumMoves];
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float strip[kStrip] = {}, stripMove[kStrip] = {}, flash[kNumMoves] = {};
    int moveShow = -1, repShow = 0, padDown = -1, keyDown = -1;
    bool heldShow = false;
    float progShow = 0.0f, barShow = 0.0f;

    Segmented* trigSeg;
    MenuButton *intMenu, *lenMenu, *gridMenu;
    Dial *chanceDial, *varDial, *gateDial, *pitchDial, *decayDial, *filterDial, *tapeDial, *mixDial, *outDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIStutter)
};

UI* createUI() { return new UIStutter(); }

END_NAMESPACE_DISTRHO
