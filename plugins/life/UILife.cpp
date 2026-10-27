// pump LIFE: interface. THE GRID (the board as it lives; click or drag to draw cells, which becomes the DRAWN
// pattern), its pattern menu and RANDOM / CLEAR / STEP / RESET. TONES: six bar charts (pitch, kick, FM, ring, decay,
// level) with a bar per voice: click or drag across them to set them; below, the voices' pads (click to play) and
// mutes. SOUND: global FM, drive, decay, the LFO, width, volume. SEQUENCER: mode, rate, trigger, the cycle, the
// colours, the grid size, the rule. FX: the pitch-shifting delay and its filter.
#include "ui/PumpUI.hpp"
#include "PluginLife.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::life;

static Color toneColor(int t, bool dark)
{
    static const unsigned c[kTones] = { 0xFF5E57, 0xFFA23D, 0xF2E24A, 0x8CFF5A, 0x3DF5D0, 0x5CB8FF, 0x9C8CFF, 0xF06BFF };
    const Color base = pump::hexColor(c[t & 7]);
    return dark ? base : pump::mixColor(base, pump::hexColor(0x000000), 0.28f);
}

class UILife : public PumpUI {
public:
    UILife() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::life(), "LIFE", 1000, 800, -1)
    {
        patternMenu = addMenu(kPattern, "PATTERN");
        patternMenu->arrows = true;
        for (int t = 0; t < kTones; ++t) { muteB[t] = addToggle(tone(t, kTMute), "M"); muteB[t]->small = true; muteB[t]->tip = "Mute this voice"; }
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        snd[0] = dial(kFmAll, "FM", false, "Scales every voice's FM");
        snd[1] = dial(kDrive, "DRIVE");
        snd[2] = dial(kDecayAll, "DECAY", false, "Scales every voice's decay");
        snd[3] = dial(kPitchRange, "RANGE", true, "How far the LFO moves the pitches (semitones, either way)");
        snd[4] = dial(kLfoRate, "LFO RATE");
        snd[5] = dial(kLfoDepth, "LFO DEPTH");
        snd[6] = dial(kWidth, "WIDTH");
        snd[7] = dial(kVolume, "VOLUME", true);
        modeSeg = addSegmented(kMode, { "LOOP", "EVOLVE", "FREE" });
        modeSeg->tip = "Loop: the cycle repeats with the song · Evolve: runs on while the host plays · Free: runs on its own at the host tempo";
        rateSeg = addSegmented(kRate, { "1/4", "1/8", "1/16", "1/32" });
        trigSeg = addSegmented(kTrigger, { "BORN", "LIVING", "DYING" });
        trigSeg->tip = "Which cells play their voice each generation";
        seq[0] = dial(kLength, "LENGTH", false, "Steps in a cycle (LOOP: then back to the pattern)");
        seq[1] = dial(kOffset, "OFFSET", false, "Generations to skip from the pattern: where the cycle starts");
        seq[2] = dial(kSens, "SENSITIVITY", false, "How few cells of a colour it takes to play its voice (100: one)");
        seq[3] = dial(kXWrap, "X-WRAP", false, "Colour step from one column to the next");
        seq[4] = dial(kYWrap, "Y-WRAP", false, "Colour step from one row to the next");
        seq[5] = dial(kColour, "COLOUR", false, "Turns every cell's colour");
        seq[6] = dial(kCols, "WIDTH"); seq[7] = dial(kRows, "HEIGHT");
        ruleMenu = addMenu(kRule, "RULE"); ruleMenu->arrows = true;
        reseedB = addToggle(kReseed, "RESEED");
        reseedB->tip = "Evolve / Free: back to the pattern when the board dies or stops changing";
        fx[0] = dial(kShift, "SHIFT", true, "Pitch shift in the delay's loop (each echo climbs or falls)");
        fx[1] = dial(kGrain, "GRAIN", false, "The pitch shifter's grain: short = buzzy, long = smooth");
        fx[2] = dial(kTime, "TIME");
        fx[3] = dial(kFeedback, "FEEDBACK");
        fx[4] = dial(kFilter, "FILTER", true, "Left: low-pass closing · right: high-pass opening");
        fx[5] = dial(kFxDecay, "FX DECAY", false, "How much of each hit goes into the FX: short keeps busy patterns tight");
        fx[6] = dial(kMix, "MIX");
        if (auto* p = static_cast<PluginLife*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i >= kToneBase)
        {
            const int f = (i - kToneBase) % kToneFields;
            if (f == kTPitch)
            {
                static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const int n = (int)std::lround(v); std::snprintf(b, sizeof b, "%s%d", nn[n % 12], n / 12 - 1); return b;
            }
        }
        if (i == kPitchRange || i == kShift) { if (std::fabs(v) < 0.05f) return "0"; std::snprintf(b, sizeof b, "%+.1f st", v); return b; }
        if (i == kFilter) { if (std::fabs(v) < 2.0f) return "OFF"; std::snprintf(b, sizeof b, v < 0 ? "LP %.0f" : "HP %.0f", std::fabs(v)); return b; }
        if (i == kRule) return kRuleKeys[pump::clampv((int)v, 0, kNumRules - 1)];
        if (i == kPattern) { std::string s = kPatternNames[pump::clampv((int)v, 0, kNumPatterns - 1)]; for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
        if (i == kLength) { std::snprintf(b, sizeof b, "%d", (int)v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float hs = Dial::heightFor(Dial::Small);
        gridR = Rect(14, 58, 460, 460);
        patternMenu->r = Rect(14, 526, 214, 24);
        const char* const bl[4] = { "RANDOM", "CLEAR", "STEP", "RESET" };
        for (int k = 0; k < 4; ++k) { btnR[k] = Rect(236 + k * 60, 526, 56, 24); btnLabel[k] = bl[k]; }
        tonesR = Rect(486, 58, 500, 354);
        for (int f = 0; f < 6; ++f) barRow[f] = Rect(556, 82 + f * 44, 420, 38);
        for (int t = 0; t < kTones; ++t) { padR[t] = Rect(556 + t * 52.5f + 2, 350, 48.5f, 26); muteB[t]->r = Rect(556 + t * 52.5f + 8, 382, 36.5f, 20); }
        soundR = Rect(486, 422, 500, 110);
        for (int k = 0; k < 8; ++k) snd[k]->r = Rect(492 + k * 61.5f, 446, 61.5f, hs);
        seqR = Rect(14, 562, 560, 206);
        modeSeg->r = Rect(24, 590, 190, 22); rateSeg->r = Rect(224, 590, 190, 22); trigSeg->r = Rect(424, 590, 140, 22);
        for (int k = 0; k < 8; ++k) seq[k]->r = Rect(20 + k * 68.5f, 622, 68.5f, hs);
        ruleMenu->r = Rect(24, 704, 260, 24); reseedB->r = Rect(294, 704, 110, 24);
        fxR = Rect(586, 542, 400, 226);
        for (int k = 0; k < 4; ++k) fx[k]->r = Rect(592 + k * 97, 570, 97, hs);
        for (int k = 4; k < 7; ++k) fx[k]->r = Rect(592 + (k - 4) * 97, 668, 97, hs);
    }

    void tick(float dt) override
    {
        t += dt;
        if (!plug) return;
        board = plug->engine.boardNow();
        gen = plug->engine.generationNow();
        const uint32_t h = plug->engine.takeHits();
        for (int k = 0; k < kTones; ++k) { if (h >> k & 1) flash[k] = 1.0f; else flash[k] = std::max(0.0f, flash[k] - dt * 5.0f); lv[k] = plug->engine.toneLevel(k); }
    }
    int W() const { return pump::clampv((int)value(kCols), 4, kMaxGrid); }
    int H() const { return pump::clampv((int)value(kRows), 4, kMaxGrid); }
    int colourAt(int x, int y) const { return colourOf(x, y, (int)value(kXWrap), (int)value(kYWrap), (int)value(kColour)); }
    Rect cellRect(int x, int y) const
    {
        const float cs = std::floor((gridR.w - 24) / (float)std::max(W(), H()));
        const float ox = gridR.x + (gridR.w - cs * W()) * 0.5f, oy = gridR.y + 22 + (gridR.h - 22 - cs * H()) * 0.5f;
        return Rect(ox + x * cs, oy + y * cs, cs, cs);
    }

    void drawBody() override
    {
        // the grid
        screenBox(gridR);
        char b[96];
        std::snprintf(b, sizeof b, "GEN %d", gen);
        caption(gridR.x + 10, gridR.y + 14, "THE GRID");
        const int mode = (int)value(kMode), L = (int)value(kLength);
        if (mode == kModeLoop) std::snprintf(b, sizeof b, "STEP %d / %d   GEN %d", ((gen - (int)value(kOffset)) % L + L) % L + 1, L, gen);
        textAt(gridR.r() - 10, gridR.y + 14, b, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int y = 0; y < H(); ++y)
            for (int x = 0; x < W(); ++x)
            {
                const Rect c = cellRect(x, y);
                const Rect in(c.x + 1.5f, c.y + 1.5f, c.w - 3, c.h - 3);
                const Color col = toneColor(colourAt(x, y), theme.dark);
                if ((board[(size_t)y] >> x) & 1) fillRect(in, col);
                else { beginPath(); rect(in.x, in.y, in.w, in.h); fillColor(withAlpha2(col, theme.dark ? 0.10f : 0.12f)); fill(); }
            }
        for (int k = 0; k < 4; ++k)
        {
            keyBox(btnR[k], false, pressedBtn == k);
            const Rect r = pressedBtn == k ? Rect(btnR[k].x + 3, btnR[k].y + 3, btnR[k].w, btnR[k].h) : btnR[k];
            textAt(r.cx(), r.cy(), btnLabel[k], kLabel, 9.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        // tones
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(tonesR, "TONES");
        static const char* const rowName[6] = { "PITCH", "KICK", "FM", "RING", "DECAY", "LEVEL" };
        static const int rowField[6] = { kTPitch, kTKick, kTFm, kTRing, kTDecay, kTLevel };
        for (int f = 0; f < 6; ++f)
        {
            const Rect& rr = barRow[f];
            textAt(tonesR.x + 12, rr.cy(), rowName[f], kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
            line(rr.x, rr.b(), rr.r(), rr.b(), theme.dim, 1.0f);
            for (int k = 0; k < kTones; ++k)
            {
                const int p = tone(k, rowField[f]);
                const float n = norm(p);
                const Rect bar(rr.x + k * (rr.w / kTones) + 6, rr.b() - rr.h * n, rr.w / kTones - 12, rr.h * n);
                fillRect(bar, toneColor(k, theme.dark));
                if (hoverBar == p) textAt(bar.cx(), rr.y + 8, formatParam(p, value(p)), kMono, 8.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        for (int k = 0; k < kTones; ++k)
        {
            const Color col = toneColor(k, theme.dark);
            const float a = std::max(flash[k], std::min(1.0f, lv[k] * 1.5f));
            Color fillC = pump::mixColor(withAlpha2(col, 0.25f), col, a);
            keyBox(padR[k], true, a > 0.6f, &fillC, 3.0f);
            char nb[4]; std::snprintf(nb, sizeof nb, "%d", k + 1);
            textAt(padR[k].cx(), padR[k].cy(), nb, kBold, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        panel(soundR, "SOUND");
        panel(seqR, "SEQUENCER");
        panel(fxR, "FX");
    }

    // bars: click or drag across them
    int barAt(float x, float y) const
    {
        static const int rowField[6] = { kTPitch, kTKick, kTFm, kTRing, kTDecay, kTLevel };
        for (int f = 0; f < 6; ++f)
        {
            const Rect& rr = barRow[f];
            if (y >= rr.y - 3 && y <= rr.b() + 3 && x >= rr.x && x < rr.r()) return tone(pump::clampv((int)((x - rr.x) / (rr.w / kTones)), 0, kTones - 1), rowField[f]);
        }
        return -1;
    }
    void setBar(int p, float y)
    {
        static const int rowField[6] = { kTPitch, kTKick, kTFm, kTRing, kTDecay, kTLevel };
        const int f = (p - kToneBase) % kToneFields;
        int row = 0; for (int k = 0; k < 6; ++k) if (rowField[k] == f) row = k;
        const Rect& rr = barRow[row];
        if (std::find(editing.begin(), editing.end(), p) == editing.end()) { editParameter((uint32_t)p, true); editing.push_back(p); }
        setParamNorm(p, pump::clampv((rr.b() - y) / rr.h, 0.0f, 1.0f));
    }
    bool cellAt(float x, float y, int& cx, int& cy) const
    {
        for (int yy = 0; yy < H(); ++yy) for (int xx = 0; xx < W(); ++xx) if (cellRect(xx, yy).contains(x, y)) { cx = xx; cy = yy; return true; }
        return false;
    }
    void commitDrawn()
    {
        if ((int)value(kPattern) != 0) setParamGesture(kPattern, 0.0f);
        setState("board", boardToText(drawnB).c_str());
    }
    void paintCell(int cx, int cy)
    {
        const uint16_t bit = (uint16_t)(1u << cx);
        if (paintOn) drawnB[(size_t)cy] |= bit; else drawnB[(size_t)cy] &= (uint16_t)~bit;
        board[(size_t)cy] = drawnB[(size_t)cy];
        commitDrawn();
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        int cx, cy;
        if (cellAt(x, y, cx, cy))
        {
            // drawing starts from what is on the grid now
            drawnB = board;
            paintOn = !((board[(size_t)cy] >> cx) & 1);
            painting = true;
            beginGroup();
            paintCell(cx, cy);
            return true;
        }
        const int p = barAt(x, y);
        if (p >= 0) { beginGroup(); dragBars = true; setBar(p, y); return true; }
        for (int k = 0; k < kTones; ++k) if (padR[k].contains(x, y)) { if (plug) plug->keys.push((uint8_t)k, 110); flash[k] = 1.0f; return true; }
        for (int k = 0; k < 4; ++k)
            if (btnR[k].contains(x, y))
            {
                pressedBtn = k;
                if (k == 0)
                {
                    uint32_t s = (uint32_t)(t * 1000.0f) * 2654435761u + 12345u;
                    drawnB.fill(0);
                    for (int yy = 0; yy < H(); ++yy) for (int xx = 0; xx < W(); ++xx) { s = s * 1664525u + 1013904223u; if ((s >> 8) % 100 < 32) drawnB[(size_t)yy] |= (uint16_t)(1u << xx); }
                    beginGroup(); commitDrawn(); endGroup();
                }
                else if (k == 1) { drawnB.fill(0); beginGroup(); commitDrawn(); endGroup(); }
                else if (k == 2 && plug) plug->engine.requestStep();
                else if (k == 3 && plug) plug->engine.requestReset();
                return true;
            }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (painting) { int cx, cy; if (cellAt(x, y, cx, cy) && (((drawnB[(size_t)cy] >> cx) & 1) != (paintOn ? 1 : 0))) paintCell(cx, cy); return; }
        if (dragBars) { const int p = barAt(x, std::min(std::max(y, barRow[0].y), barRow[5].b())); if (p >= 0) setBar(p, y); else { /* between rows: keep the last row */ } }
    }
    void bodyMouseUp(float, float) override
    {
        if (painting) { painting = false; endGroup(); }
        if (dragBars) { dragBars = false; for (int p : editing) editParameter((uint32_t)p, false); editing.clear(); endGroup(); }
        pressedBtn = -1;
    }
    void bodyHover(float x, float y) override
    {
        hoverBar = barAt(x, y);
        int cx, cy;
        if (cellAt(x, y, cx, cy)) setStatus("Click or drag to draw cells (the drawing becomes the DRAWN pattern); a cell's colour is the voice it plays");
        else if (hoverBar >= 0) setStatus("Click or drag across the bars to set them");
        else if (gridR.contains(x, y)) setStatus("The board as it lives");
        for (int k = 0; k < 4; ++k) if (btnR[k].contains(x, y))
        {
            static const char* const tip[4] = { "A random drawing", "Clear the drawing", "One generation on, now", "Back to the pattern (and the cycle's start)" };
            setStatus(tip[k]);
        }
        for (int k = 0; k < kTones; ++k) if (padR[k].contains(x, y)) setStatus("Play this voice (MIDI note " + std::to_string(36 + k) + ")");
    }
    void onExtraState(const char* key, const char* value) override { if (std::strcmp(key, "board") == 0) drawnB = boardFromText(value); }
    bool keyPress(uint key, uint) override
    {
        if (key >= '1' && key <= '8') { const int k = (int)(key - '1'); if (plug) plug->keys.push((uint8_t)k, 110); flash[k] = 1.0f; return true; }
        if (key == ' ' && plug) { plug->engine.requestStep(); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "1-8", "play a voice" }, { "SPACE", "one generation on" }, { "DRAW", "click or drag on the grid" },
                 { "BARS", "click or drag across them" }, { "MIDI 36-43", "the voices" } };
    }
    std::string footerInfo() override
    {
        char b[96]; std::snprintf(b, sizeof b, "%s   %s   GEN %d", kModeNames[pump::clampv((int)value(kMode), 0, 2)], kRuleKeys[pump::clampv((int)value(kRule), 0, kNumRules - 1)], gen);
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    PluginLife* plug = nullptr;
    Rect gridR, tonesR, soundR, seqR, fxR, barRow[6], padR[kTones], btnR[4];
    const char* btnLabel[4] = {};
    MenuButton *patternMenu, *ruleMenu;
    Button* muteB[kTones] = {};
    Button* reseedB;
    Segmented *modeSeg, *rateSeg, *trigSeg;
    Dial *snd[8] = {}, *seq[8] = {}, *fx[7] = {};
    Board board {}, drawnB {};
    int gen = 0, hoverBar = -1, pressedBtn = -1;
    bool painting = false, paintOn = true, dragBars = false;
    std::vector<int> editing;
    float t = 0.0f, flash[kTones] = {}, lv[kTones] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UILife)
};

UI* createUI() { return new UILife(); }

END_NAMESPACE_DISTRHO
