// pump X0X: interface. Across the top, a column per instrument like a rhythm machine's front panel: its pad (click
// to play; its note as Renoise names it and the MIDI number; it lights as it sounds, in the instrument's colour), the
// machine it is modelled on, and its TUNE, DECAY, TONE (or NOISE), the instrument's own fourth control, LEVEL and
// PAN. Then the sequencer's controls (mode, PLAY, rate, the PATTERN library, clear and shift; length, swing, accent,
// velocity, drive, volume) and the step grid: the accent row and a row per instrument in its colour, sixteen steps in
// groups of four, the playhead; click a step to set or clear it, drag along a row to paint.
#include "ui/PumpUI.hpp"
#include "PluginX0x.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::x0x;

class UIX0x : public PumpUI {
public:
    UIX0x() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::x0x(), "X0X", 1000, 640, -1)
    {
        for (int i = 0; i < kInstr; ++i)
        {
            Col& c = cols[i];
            c.machine = addMenu(ip(i, kMachine));
            c.machine->tip = "The machine this instrument is modelled on: Eight, Nine, Six, Ten (lo-fi analog), Seven (eight-bit samples)";
            c.d[0] = addDial(ip(i, kTune), "TUNE", Dial::Small, true);
            c.d[1] = addDial(ip(i, kDecay), "DECAY", Dial::Small);
            c.d[2] = addDial(ip(i, kTone), kToneNames[i], Dial::Small);
            c.d[3] = addDial(ip(i, kSnap), kSnapNames[i], Dial::Small);
            c.d[4] = addDial(ip(i, kLevel), "LEVEL", Dial::Small, true);
            c.d[5] = addDial(ip(i, kPan), "PAN", Dial::Small, true);
            c.d[0]->tip = "Tune in semitones";
            c.d[1]->tip = "Decay";
            c.d[2]->tip = i == kBD ? "Bass drum: Nine and Seven: how far the pitch sweeps; the others: the click's brightness (Ten: the whole drum's)"
                        : i == kSD ? "Snare: the balance of the two heads (Nine and Seven: the noise's brightness)"
                        : i == kCP ? "Clap: the band the noise rings in"
                        : (i >= kLT && i <= kHT) ? "Tom: how much noise in the hit"
                        : i == kRS ? "Rim: the balance of the two resonances"
                        : "The band the metal rings in";
            c.d[3]->tip = i == kBD ? "Attack: the click at the start"
                        : i == kSD ? "Snappy: the snare wires' noise"
                        : i == kCP ? "Tail: the reverb-like tail after the bursts"
                        : (i >= kLT && i <= kHT) ? "Sweep: how far the pitch drops"
                        : i == kRS ? "Click: the attack"
                        : i == kCB ? "Attack: the short loud stage at the start"
                        : "Noise mixed into the metal";
            for (Dial* d : c.d) d->tabGroup = 1 + i;
        }
        modeSeg = addSegmented(kSeqMode, { "OFF", "SONG", "FREE" });
        modeSeg->tip = "Off: MIDI plays the instruments · Song: the pattern plays with the host · Free: always, at the host tempo";
        playBtn = addButton("PLAY", [this]() { if (plug) plug->preview.store(!plug->preview.load()); }, [this]() { return plug && plug->preview.load(); });
        playBtn->small = true;
        playBtn->tip = "Play the pattern now, at the host tempo (Space)";
        rateMenu = addMenu(kSeqRate, "RATE");
        rateMenu->arrows = true;
        rateMenu->tip = "Step length (T = triplets)";
        patMenu = add(new MenuButton());
        patMenu->itemsFn = []() { std::vector<std::string> v; for (const auto& p : patternLibrary()) v.push_back(p.name); return v; };
        patMenu->getFn = []() { return -1; };
        patMenu->setFn = [this](int k) { const auto& lib = patternLibrary(); if (k >= 0 && k < (int)lib.size()) writePattern(lib[(size_t)k].p, lib[(size_t)k].name); };
        patMenu->textFn = []() { return std::string("PATTERNS"); };
        patMenu->tip = "A pattern from the library: changes the steps only, the kit stays";
        clearBtn = addButton("CLEAR", [this]() { writePattern(Pattern(), "Cleared"); });
        leftBtn = addButton("<", [this]() { shift(-1); });
        rightBtn = addButton(">", [this]() { shift(1); });
        clearBtn->tip = "Clear every step"; leftBtn->tip = "Shift the pattern a step earlier (Left)"; rightBtn->tip = "Shift the pattern a step later (Right)";
        for (Button* b : { clearBtn, leftBtn, rightBtn }) b->small = true;
        const struct { int p; const char* l; float w; const char* tip; } rows[] = {
            { kSeqLength, "LENGTH", 50, "Pattern length in steps (or click a step number)" },
            { kSwing, "SWING", 44, "Swing: every second step starts later (33 % = a triplet shuffle)" },
            { kAccent, "ACCENT", 56, "How much louder (and brighter) accented steps play" },
            { kVelocity, "VELOCITY", 62, "How much MIDI velocity changes the level" },
            { kAccentVel, "ACC VEL", 56, "MIDI notes at or above this velocity are accented" },
            { kDrive, "DRIVE", 42, "Drive on the whole kit" },
            { kVolume, "VOLUME", 50, "Volume" },
        };
        for (const auto& m : rows) { SliderRow* r = addRow(m.p, m.l, m.p == kVolume); r->labelW = m.w; r->valueW = 44; r->tip = m.tip; globalRows.push_back(r); }
        if (auto* p = static_cast<PluginX0x*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kInstr * kPerInstr && i % kPerInstr == kTune) { if (std::fabs(v) < 0.005f) return "0 st"; std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.005f ? "%+.0f st" : "%+.1f st", v); return b; }
        if (i < kInstr * kPerInstr && i % kPerInstr == kPan) { if (std::fabs(v) < 0.5f) return "C"; std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b; }
        return PumpUI::formatParam(i, v);
    }
    Color rowColour(int r, bool forText = false) const
    {
        if (r == kAccentRow) return forText ? (theme.dark ? pump::hexColor(0xFF6A5C) : pump::hexColor(0xB3261E)) : pump::hexColor(0xFF4F45);
        static const int idx[kInstr] = { 0, 8, 1, 9, 2, 3, 11, 4, 5, 13, 6 };
        return forText ? pump::wheelInk(idx[r], theme.dark) : pump::wheelColour(idx[r], false);
    }

    void layout() override
    {
        const float cw = 88.0f, dh = Dial::heightFor(Dial::Small);
        for (int i = 0; i < kInstr; ++i)
        {
            Col& c = cols[i];
            const float x = 14 + i * cw;
            c.col = Rect(x, 54, cw, 284);
            c.pad = Rect(x + 4, 56, cw - 12, 42);
            c.machine->r = Rect(x + 4, 104, cw - 12, 18);
            for (int k = 0; k < 6; ++k) c.d[k]->r = Rect(x + 2 + (k % 2) * 42, 126 + (k / 2) * (dh - 2), 42, dh);
        }
        const float y1 = 346, y2 = 370;
        modeSeg->r = Rect(14, y1, 150, 18);
        playBtn->r = Rect(170, y1, 56, 18);
        rateMenu->r = Rect(232, y1, 128, 18);
        patMenu->r = Rect(366, y1, 150, 18);
        clearBtn->r = Rect(522, y1, 56, 18);
        leftBtn->r = Rect(584, y1, 26, 18); rightBtn->r = Rect(614, y1, 26, 18);
        float x = 14;
        for (size_t k = 0; k < globalRows.size(); ++k)
        {
            if (k == 2) { globalRows[k]->r = Rect(650, y1 + 1, 336, 16); continue; }   // ACCENT beside the buttons
            for (auto* r : globalRows) r->valueW = 40;
            globalRows[k]->r = Rect(x, y2, 152, 16);
            x += 152 + 12;
        }
        gridR = Rect(104, 396, 878, kRows * kRowH);
        labelR = Rect(14, 396, 86, kRows * kRowH);
        numR = Rect(gridR.x, gridR.b() + 4, gridR.w, 15);
        meterL = Rect(984 + 0, 56, 0, 0);
    }

    void tick(float dt) override
    {
        playBtn->label = (plug && plug->preview.load()) ? "STOP" : "PLAY";
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        for (int i = 0; i < kInstr; ++i) lv[i] = std::max(plug->engine.level(i), lv[i] - dt * 4.0f);
        const int st = plug->engine.stepNow();
        if (st != playStep) { playStep = st; if (st >= 0) flash[st] = 1.0f; }
        for (float& f : flash) f = std::max(0.0f, f - dt * 3.0f);
    }

    int length() const { return pump::clampv((int)value(kSeqLength), 1, kSteps); }
    uint32_t row(int r) const { return (uint32_t)pump::clampv((long)std::lround(value(rowParam(r))), 0L, 65535L); }
    float cellW() const { return gridR.w / kSteps; }
    int stepAt(float x) const { return pump::clampv((int)((x - gridR.x) / cellW()), 0, kSteps - 1); }
    int rowAt(float y) const { return pump::clampv((int)((y - gridR.y) / kRowH), 0, kRows - 1); }
    // grid rows top to bottom: the accent row, then the instruments in order
    int rowOfLine(int line) const { return line == 0 ? kAccentRow : line - 1; }
    float lineY(int r) const { return gridR.y + (r == kAccentRow ? 0 : r + 1) * kRowH; }

    void drawBody() override
    {
        char b[32];
        // the columns
        for (int i = 0; i < kInstr; ++i)
        {
            const Col& c = cols[i];
            if (i > 0) line(c.col.x, c.col.y + 4, c.col.x, c.col.b(), theme.dim, 1.0f);
            const Color col = rowColour(i);
            const float l = pump::clampv(lv[i], 0.0f, 1.0f);
            keyBox(c.pad, false, padDown == i);
            if (l > 0.01f) fillRect(c.pad.inset(2), withAlpha2(col, 0.25f + 0.75f * l));
            fillRect(Rect(c.pad.x, c.pad.y, 5, c.pad.h), col);
            textAt(c.pad.x + 10, c.pad.y + 13, kInstrInfo[i].label, kLabel, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            char nb[8]; pump::padNoteName(noteForInstr(i), nb);
            std::snprintf(b, sizeof b, "%s %d", nb, noteForInstr(i));
            textAt(c.pad.x + 10, c.pad.y + 30, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(c.pad.r() - 6, c.pad.y + 30, kInstrInfo[i].shortLabel, kBold, 12.0f, rowColour(i, true), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        line(14, 340, 986, 340, theme.dim, 1.0f);
        drawGrid();
        drawOutMeter(outMeter, Rect(988, 56, 4, 280), Rect(993, 56, 4, 280), -48.0f, 6.0f);
    }

    void drawGrid()
    {
        const int L = length();
        const float cw = cellW();
        for (int line = 0; line < kRows; ++line)
        {
            const int r = rowOfLine(line);
            const float y = gridR.y + line * kRowH;
            const Color col = rowColour(r);
            // the row's name (click: play it)
            const Rect lr(labelR.x, y + 1, labelR.w, kRowH - 2);
            fillRect(Rect(lr.x, lr.y, 4, lr.h), col);
            const float l = r < kInstr ? pump::clampv(lv[r], 0.0f, 1.0f) : 0.0f;
            if (l > 0.01f) fillRect(Rect(lr.x + 4, lr.y, (lr.w - 4) * l, lr.h), withAlpha2(col, 0.35f));
            textAt(lr.x + 10, lr.cy(), r == kAccentRow ? "ACCENT" : kInstrInfo[r].label, kLabel, 9.5f, rowColour(r, true), ALIGN_LEFT | ALIGN_MIDDLE, 0.5f);
            const uint32_t m = row(r);
            for (int k = 0; k < kSteps; ++k)
            {
                const Rect cr(gridR.x + k * cw + 2, y + 2, cw - 4, kRowH - 4);
                const bool on = (m >> k) & 1u, dim = k >= L;
                const bool group = (k / 4) % 2 == 1;
                fillRect(cr, on ? withAlpha2(col, dim ? 0.35f : 1.0f) : (group ? theme.panel2 : theme.panel));
                strokeRect(cr, dim ? theme.dim : (on ? theme.ink : withAlpha2(theme.ink, 0.35f)), on ? 1.5f : 1.0f);
                if (on && k == playStep) fillRect(cr.inset(3), withAlpha2(pump::hexColor(0xFFFFFF), 0.55f * flash[k] + 0.2f));
            }
        }
        // the playhead
        if (playStep >= 0)
        {
            const Rect ph(gridR.x + playStep * cw, gridR.y - 2, cw, gridR.h + 4);
            strokeRect(ph, theme.ink, 1.5f);
        }
        for (int k = 0; k < kSteps; ++k)
        {
            const Rect nb(gridR.x + k * cw + 2, numR.y, cw - 4, numR.h);
            const bool on = playStep == k;
            fillRect(nb, on ? theme.ink : (k < L ? theme.panel2 : theme.bg));
            char b[4]; std::snprintf(b, sizeof b, "%d", k + 1);
            textAt(nb.cx(), nb.cy(), b, kMono, 9.5f, on ? theme.on : (k < L ? theme.ink : theme.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        textAt(labelR.x + 10, numR.cy(), "LENGTH", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.5f);
    }

    // ---- editing ----
    void setRow(int r, uint32_t m) { setParamGesture(rowParam(r), (float)(m & 0xFFFFu)); }
    void writePattern(const Pattern& p, const char* what)
    {
        beginGroup();
        for (int r = 0; r < kRows; ++r) setRow(r, p.rows[r]);
        endGroup();
        setStatus(what);
    }
    void shift(int d)
    {
        const int L = length();
        Pattern p;
        for (int r = 0; r < kRows; ++r)
        {
            const uint32_t m = row(r);
            uint32_t o = m & ~((1u << L) - 1u);   // the steps past the length stay
            for (int k = 0; k < L; ++k) if ((m >> k) & 1u) o |= 1u << (((k + d) % L + L) % L);
            p.rows[r] = o;
        }
        writePattern(p, d < 0 ? "Shifted left" : "Shifted right");
    }
    void playPad(int i, float vel)
    {
        if (plug && i >= 0 && i < kInstr) plug->keys.push((uint8_t)i, (uint8_t)pump::clampv((int)(vel * 127), 1, 127));
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int i = 0; i < kInstr; ++i)
            if (cols[i].pad.contains(x, y)) { padDown = i; playPad(i, 0.45f + 0.55f * (y - cols[i].pad.y) / cols[i].pad.h); setStatus(std::string(kInstrInfo[i].name)); return true; }
        if (labelR.contains(x, y))
        {
            const int r = rowOfLine((int)((y - gridR.y) / kRowH));
            if (r < kInstr) { playPad(r, 0.85f); setStatus(std::string(kInstrInfo[r].name)); }
            return true;
        }
        if (gridR.contains(x, y))
        {
            const int r = rowOfLine((int)((y - gridR.y) / kRowH)), k = stepAt(x);
            const uint32_t m = row(r);
            paintRow = r; paintOn = !((m >> k) & 1u); paintLast = k;
            beginGroup();
            setRow(r, paintOn ? (m | (1u << k)) : (m & ~(1u << k)));
            if (paintOn && r < kInstr && (!plug || !plug->preview.load()) && playStep < 0) playPad(r, 0.8f);
            setStatus(stepStatus(r, k));
            return true;
        }
        if (numR.contains(x, y)) { setParamGesture(kSeqLength, (float)(stepAt(x) + 1)); setStatus(paramStatus(kSeqLength)); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (paintRow < 0) return;
        if (x < gridR.x || x >= gridR.r()) return;
        const int k = stepAt(x);
        if (k == paintLast) return;
        const uint32_t m = row(paintRow);
        setRow(paintRow, paintOn ? (m | (1u << k)) : (m & ~(1u << k)));
        paintLast = k;
        setStatus(stepStatus(paintRow, k));
    }
    void bodyMouseUp(float, float) override
    {
        padDown = -1;
        if (paintRow >= 0) { paintRow = -1; endGroup(); }
    }
    std::string stepStatus(int r, int k) const
    {
        return std::string(r == kAccentRow ? "Accent" : kInstrInfo[r].name) + "  step " + std::to_string(k + 1) + (((row(r) >> k) & 1u) ? "  ON" : "  off");
    }
    void bodyHover(float x, float y) override
    {
        for (int i = 0; i < kInstr; ++i)
            if (cols[i].pad.contains(x, y))
            {
                char nb[8]; pump::padNoteName(noteForInstr(i), nb);
                setStatus(std::string(kInstrInfo[i].name) + "  " + nb + " (MIDI " + std::to_string(noteForInstr(i)) + ")  " + kMachineNames[pump::clampv((int)value(ip(i, kMachine)), 0, kNumMachines - 1)] + "    click to play (lower = louder)");
                return;
            }
        if (gridR.contains(x, y)) setStatus(stepStatus(rowOfLine((int)((y - gridR.y) / kRowH)), stepAt(x)) + "    click: on / off, drag along the row to paint");
        else if (labelR.contains(x, y)) setStatus("Click a row's name to play it");
        else if (numR.contains(x, y)) setStatus("Click: the pattern ends at this step");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key == ' ') { if (plug) plug->preview.store(!plug->preview.load()); return true; }
        if (key == kKeyLeft) { shift(-1); return true; }
        if (key == kKeyRight) { shift(1); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click a pad", "play it (lower on the pad = louder)" },
            { "Click a step", "on / off; drag along the row to paint" },
            { "Click a step number", "the pattern ends there" },
            { "PATTERNS", "a pattern from the library (the kit stays)" },
            { "Left / Right", "shift the pattern" },
            { "Space", "play the pattern now" },
        };
    }
    std::string footerInfo() override
    {
        char b[96];
        std::snprintf(b, sizeof b, "SEQ %s   %d x %s   SWING %.0f%%", kSeqModeNames[pump::clampv((int)value(kSeqMode), 0, kNumSeqModes - 1)], length(),
                      kRateNames[pump::clampv((int)value(kSeqRate), 0, kNumRates - 1)], value(kSwing));
        std::string s = b;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr float kRowH = 17.0f;
    struct Col { Rect col, pad; MenuButton* machine = nullptr; Dial* d[6] = {}; };
    PluginX0x* plug = nullptr;
    Col cols[kInstr];
    Segmented* modeSeg;
    Button *playBtn, *clearBtn, *leftBtn, *rightBtn;
    MenuButton *rateMenu, *patMenu;
    std::vector<SliderRow*> globalRows;
    Rect gridR, labelR, numR, meterL;
    OutMeter outMeter;
    float lv[kInstr] = {}, flash[kSteps] = {};
    int playStep = -1, padDown = -1, paintRow = -1, paintLast = -1;
    bool paintOn = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIX0x)
};

UI* createUI() { return new UIX0x(); }

END_NAMESPACE_DISTRHO
