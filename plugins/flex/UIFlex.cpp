// pump FLEX: interface. EIGHT SLOTS, each holding a pattern (a bank, FLX or FLY, and a hex number; its name below).
// Click a slot to play it (click the playing one again to turn it off): that sets PLAY, the parameter to automate from
// the host to switch patterns. Each slot has a bank key (FLX / FLY), arrows and a list (grouped by family; right-click
// the slot opens it too); the wheel steps the pattern (Shift: a family). The big OFF key stops the pattern. NOW shows
// what plays and the song's position over its four bars (16 beats), with the playhead. Output: mix and level.
// Keys: 1-8 play that slot, 0 or Esc = off.
#include "ui/PumpUI.hpp"
#include "PluginFlex.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::flex;

class UIFlex : public PumpUI {
public:
    UIFlex() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::flex(), "FLEX", 1000, 372, kBypass)
    {
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: the pattern against the dry signal";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        mixDial->tabGroup = outDial->tabGroup = 1;
        if (auto* p = static_cast<PluginFlex*>(pluginPtr())) plug = p;
    }

protected:
    // ---- slot values: 0 empty, 1-255 FLX 01-FF, 256-510 FLY 01-FF ----
    int slotVal(int i) const { return pump::clampv((int)value(kSlot1 + i), 0, kNumSlotValues - 1); }
    static int bankOf(int v) { return v <= 0 ? -1 : v <= 255 ? 0 : 1; }
    static int hexOf(int v) { return v <= 255 ? v : v - 255; }
    int playing() const { return pump::clampv((int)value(kPlay), 0, kNumSlots); }   // 0 = off, else the slot + 1
    void setSlot(int i, int v) { setParamGesture(kSlot1 + i, (float)pump::clampv(v, 0, kNumSlotValues - 1)); }
    void setPlay(int p) { setParamGesture(kPlay, (float)p); }
    // the label font has no filled square: the gate patterns' steps are shown as # and .
    static std::string shown(const std::string& s)
    {
        std::string o;
        for (size_t i = 0; i < s.size();)
        {
            if ((unsigned char)s[i] == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x96 && (unsigned char)s[i + 2] == 0xA0) { o += '#'; i += 3; }
            else o += s[i++];
        }
        return o;
    }
    static std::string tagOf(int v) { return v <= 0 ? "--" : v <= 255 ? "FLX" : "FLY"; }
    static std::string hexText(int v) { char b[8]; if (v <= 0) return "--"; std::snprintf(b, sizeof b, "%02X", hexOf(v)); return b; }
    static std::string nameOf(int v) { return v <= 0 ? "EMPTY" : shown(patternName(slotToFlx(v))); }

    void toggleBank(int i)
    {
        const int v = slotVal(i);
        setSlot(i, v <= 0 ? 256 : v <= 255 ? v + 255 : v - 255);   // FLX 1A <-> FLY 1A (empty -> FLY 01)
    }
    void stepSlot(int i, int by)
    {
        const int v = slotVal(i), n = kNumSlotValues;
        setSlot(i, ((v + by) % n + n) % n);   // through the whole list: FLX FF carries on into FLY 01
    }
    void togglePlay(int i)
    {
        setPlay(playing() == i + 1 ? 0 : i + 1);
        setStatus(playing() == i + 1 ? "Slot " + std::to_string(i + 1) + " plays" : "Off");
    }

    // the list of a slot's bank, grouped by family; picking a row sets the slot
    void openList(int i, const Rect& anchor)
    {
        static const char* const fx[] = { "", "ROLL", "STUTTER", "BUILD", "REVERSE", "DOWN", "UP", "TAPE", "SCRATCH", "GATE", "FILTER", "CRUSH", "FADE REPEAT", "JUGGLE", "GROOVE", "CHAOS" };
        static const char* const fy[] = { "", "PITCH ROLL", "EUCLID", "PROB", "SLICE PITCH", "FREEZE / SMEAR", "CD SKIP", "BOUNCE", "MUTATE", "STUTTER EDIT", "POLYMETER", "WARP", "REVERSE ROLL", "GRAINS", "TEXTURE", "CHAOS 2" };
        const int v = slotVal(i);
        const bool fly = bankOf(v) == 1;
        std::vector<std::string> items; std::vector<int> vals;
        auto head = [&](const std::string& t) { items.push_back("#" + t); vals.push_back(-1); };
        auto row = [&](int hex) {
            const int sv = fly ? hex + 255 : hex;
            char b[8]; std::snprintf(b, sizeof b, "%02X  ", hex);
            items.push_back(std::string(b) + nameOf(sv)); vals.push_back(sv);
        };
        items.push_back(">EMPTY"); vals.push_back(0);
        if (!fly) head("FLX 01-10  FIRST SIXTEEN"); else head("FLY 01-0F  ONE OF EACH FAMILY");
        for (int h = 1; h <= (fly ? 15 : 16); ++h) row(h);
        for (int f = 1; f <= 15; ++f)
        {
            head(std::string(fly ? "FLY " : "FLX ") + "0123456789ABCDEF"[f] + "_  " + (fly ? fy[f] : fx[f]));
            for (int y = 0; y < 16; ++y) if (f * 16 + y > (fly ? 15 : 16)) row(f * 16 + y);
        }
        int cur = -1;
        for (size_t k = 0; k < vals.size(); ++k) if (vals[k] == v) cur = (int)k;
        openMenu(anchor, items, cur, [this, i, vals](int k) { if (k >= 0 && k < (int)vals.size() && vals[(size_t)k] >= 0) setSlot(i, vals[(size_t)k]); });
    }

    // ---- layout ----
    static constexpr float kCellW = 116.0f, kCellH = 114.0f, kGap = 6.0f, kTop = 58.0f;
    Rect cellR(int i) const { return Rect(14.0f + (float)i * (kCellW + kGap), kTop, kCellW, kCellH); }
    Rect bankR(int i) const { const Rect c = cellR(i); return Rect(c.r() - 44.0f, c.y + 3.0f, 40.0f, 16.0f); }
    Rect prevR(int i) const { const Rect c = cellR(i); return Rect(c.x + 6.0f, c.b() - 23.0f, 30.0f, 18.0f); }
    Rect listR(int i) const { const Rect c = cellR(i); return Rect(c.x + 38.0f, c.b() - 23.0f, c.w - 76.0f, 18.0f); }
    Rect nextR(int i) const { const Rect c = cellR(i); return Rect(c.r() - 36.0f, c.b() - 23.0f, 30.0f, 18.0f); }

    void layout() override
    {
        const float y0 = kTop + kCellH + 16.0f;   // 188
        offR = Rect(14, y0, 132, 146);
        nowR = Rect(162, y0, 556, 146);
        stripR = Rect(nowR.x + 14, nowR.y + 82, nowR.w - 28, 34);
        outPanel = Rect(734, y0, 250, 146);
        mixDial->r = Rect(outPanel.x + 14, y0 + 40, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(outPanel.x + 98, y0 + 40, 72, Dial::heightFor(Dial::Medium));
        meterL = Rect(outPanel.r() - 36, y0 + 30, 9, 104);
        meterR = Rect(outPanel.r() - 25, y0 + 30, 9, 104);
    }

    void tick(float dt) override
    {
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        phase = plug->engine.phaseNow();
    }

    void drawBody() override
    {
        const int pl = playing();
        for (int i = 0; i < kNumSlots; ++i) drawCell(i, pl == i + 1);
        drawOff(pl == 0);
        drawNow();
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, meterL.b() + 11, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
    }

    // the name in up to three lines, broken at spaces
    std::vector<std::string> wrap(const std::string& s, float maxW, float size)
    {
        std::vector<std::string> lines; std::string cur;
        size_t i = 0;
        while (i < s.size())
        {
            const size_t e = s.find(' ', i);
            const std::string w = s.substr(i, e == std::string::npos ? std::string::npos : e - i);
            const std::string t = cur.empty() ? w : cur + " " + w;
            if (!cur.empty() && textWidth(t, kLabel, size, 0.3f) > maxW) { lines.push_back(cur); cur = w; } else cur = t;
            if (e == std::string::npos) break;
            i = e + 1;
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    void drawCell(int i, bool on)
    {
        const Rect c = cellR(i);
        const int v = slotVal(i);
        const Color fillC = on ? theme.ink : theme.bg;
        keyBox(c, on, false, &fillC, on ? 3.0f : 4.0f);
        const Color tc = on ? theme.on : theme.ink, t2 = on ? withAlpha2(theme.on, 0.75f) : theme.ink2;
        // slot number (and its key) and the bank key
        char k[16]; std::snprintf(k, sizeof k, "%d", i + 1);
        textAt(c.x + 9, c.y + 11, k, kBold, 12.0f, tc, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect b = bankR(i);
        fillRect(b, on ? theme.on : theme.panel2);
        strokeRect(b, on ? theme.on : theme.ink, 1.0f);
        textAt(b.cx(), b.cy() + 0.5f, tagOf(v), kLabel, 10.5f, on ? theme.ink : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        // the hex number and the name
        textAt(c.cx(), c.y + 38, hexText(v), kBold, 28.0f, tc, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        const auto lines = wrap(nameOf(v), c.w - 14.0f, 10.0f);
        for (size_t l = 0; l < lines.size() && l < 3; ++l)
            textAt(c.cx(), c.y + 62 + (float)l * 12.0f, lines[l], kLabel, 10.0f, v > 0 ? tc : t2, ALIGN_CENTER | ALIGN_MIDDLE, 0.3f);
        // arrows and the list
        const Rect p = prevR(i), n = nextR(i), l = listR(i);
        for (const Rect* r : { &p, &l, &n }) { fillRect(*r, on ? withAlpha2(theme.on, 0.18f) : theme.panel2); strokeRect(*r, on ? theme.on : theme.dim, 1.0f); }
        textAt(p.cx(), p.cy() + 0.5f, "◀", kMono, 9.5f, tc, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(n.cx(), n.cy() + 0.5f, "▶", kMono, 9.5f, tc, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(l.cx(), l.cy() + 0.5f, "LIST ▾", kMono, 9.0f, tc, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawOff(bool on)
    {
        const Color fillC = on ? theme.ink : theme.bg;
        keyBox(offR, on, false, &fillC, on ? 3.0f : 6.0f);
        const Color tc = on ? theme.on : theme.ink;
        textAt(offR.cx(), offR.cy() - 6.0f, "OFF", kBold, 40.0f, tc, ALIGN_CENTER | ALIGN_MIDDLE, 5.0f);
        textAt(offR.cx(), offR.cy() + 30.0f, "0 · ESC", kMono, 10.0f, on ? withAlpha2(theme.on, 0.75f) : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }

    void drawNow()
    {
        screenBox(nowR);
        const int pl = playing(), v = pl ? slotVal(pl - 1) : 0;
        caption(nowR.x + 12, nowR.y + 16, "NOW");
        if (pl && v > 0)
        {
            char h[24]; std::snprintf(h, sizeof h, "SLOT %d", pl);
            textAt(nowR.r() - 12, nowR.y + 16, h, kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
            std::snprintf(h, sizeof h, "%s %s", tagOf(v).c_str(), hexText(v).c_str());
            textAt(nowR.x + 14, nowR.y + 46, h, kBold, 24.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
            const float hw = textWidth(h, kBold, 24.0f, 1.0f);
            textAt(nowR.x + 14 + hw + 14, nowR.y + 47, nameOf(v), kLabel, 15.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        }
        else textAt(nowR.x + 14, nowR.y + 46, pl ? "EMPTY SLOT" : "OFF", kBold, 24.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        // the song's position: four bars of four beats, the playhead
        const float cw = stripR.w / 16.0f;
        for (int j = 0; j < 16; ++j)
        {
            const bool oddBar = (j / 4) & 1;
            if (oddBar) fillRect(Rect(stripR.x + (float)j * cw, stripR.y, cw, stripR.h), withAlpha2(theme.ink, 0.06f));
        }
        const bool clock = phase >= 0.0f;
        if (clock) fillRect(Rect(stripR.x + std::floor(phase) * cw, stripR.y, cw, stripR.h), withAlpha2(theme.neon, 0.55f));
        for (int j = 0; j <= 16; ++j)
        {
            const float x = stripR.x + (float)j * cw;
            line(x, stripR.y, x, stripR.b(), j % 4 == 0 ? theme.ink : theme.grid, j % 4 == 0 ? 1.5f : 1.0f);
            if (j < 16)
            {
                char b[4]; std::snprintf(b, sizeof b, "%d", j % 4 + 1);
                textAt(x + cw * 0.5f, stripR.b() + 10.0f, b, kMono, 9.0f, j % 4 == 0 ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
                if (j % 4 == 0) { char bar[8]; std::snprintf(bar, sizeof bar, "BAR %d", j / 4 + 1); textAt(x + 4.0f, stripR.y - 8.0f, bar, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f); }
            }
        }
        line(stripR.x, stripR.y, stripR.r(), stripR.y, theme.ink, 1.5f); line(stripR.x, stripR.b(), stripR.r(), stripR.b(), theme.ink, 1.5f);
        if (clock)
        {
            const float x = stripR.x + phase / 16.0f * stripR.w;
            line(x, stripR.y - 4.0f, x, stripR.b() + 4.0f, theme.ink, 3.0f);
            beginPath(); moveTo(x - 5.0f, stripR.y - 7.0f); lineTo(x + 5.0f, stripR.y - 7.0f); lineTo(x, stripR.y - 1.0f); closePath(); fillColor(theme.ink); fill();
        }
        else
        {
            const std::string msg = "THE HOST IS STOPPED: THE SOUND PASSES THROUGH";
            const float tw = textWidth(msg, kLabel, 11.0f, 1.5f) + 20.0f;
            fillRect(Rect(stripR.cx() - tw * 0.5f, stripR.y + 6.0f, tw, stripR.h - 12.0f), theme.screen);
            textAt(stripR.cx(), stripR.cy(), msg, kLabel, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
        }
        textAt(nowR.x + 14, nowR.b() - 10.0f, "THE PATTERNS RUN ON THE HOST'S POSITION, FOUR BARS OF 4/4", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool) override
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const Rect c = cellR(i);
            if (!c.contains(x, y)) continue;
            if (button == kMouseButtonRight) { openList(i, listR(i)); return true; }
            if (button != kMouseButtonLeft) return false;
            if (bankR(i).contains(x, y)) { toggleBank(i); return true; }
            if (prevR(i).contains(x, y)) { stepSlot(i, (mod & kModifierShift) ? -16 : -1); return true; }
            if (nextR(i).contains(x, y)) { stepSlot(i, (mod & kModifierShift) ? 16 : 1); return true; }
            if (listR(i).contains(x, y)) { openList(i, listR(i)); return true; }
            togglePlay(i);
            return true;
        }
        if (button == kMouseButtonLeft && offR.contains(x, y)) { setPlay(0); setStatus("Off"); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (!cellR(i).contains(x, y)) continue;
            if (bankR(i).contains(x, y)) setStatus("Bank: FLX (the first bank) or FLY (the second): the same number in the other bank");
            else if (prevR(i).contains(x, y) || nextR(i).contains(x, y)) setStatus("Previous / next pattern (Shift: a family at a time; FLX FF goes on into FLY 01)");
            else if (listR(i).contains(x, y)) setStatus("The patterns of this bank, by family (right-click the slot does this too)");
            else setStatus("Slot " + std::to_string(i + 1) + ": click to play it (again: off), key " + std::to_string(i + 1) + ". Wheel: change its pattern (Shift: a family)");
            return;
        }
        if (offR.contains(x, y)) setStatus("OFF: no pattern plays, the sound passes through (key 0 or Esc)");
        else if (stripR.contains(x, y)) setStatus("The song's position over four bars: the patterns follow it, so they run in time with the host");
        else if (nowR.contains(x, y)) setStatus("What plays now. Automate PLAY in the host to switch slots");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        for (int i = 0; i < kNumSlots; ++i)
            if (cellR(i).contains(x, y))
            {
                stepSlot(i, (dy > 0 ? 1 : -1) * ((mod & kModifierShift) ? 16 : 1));
                setStatus(paramStatus(kSlot1 + i));
                return true;
            }
        return false;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '8') { setPlay((int)(key - '0')); setStatus("Slot " + std::to_string(key - '0') + " plays"); return true; }
        if (key == '0' || key == kKeyEscape) { setPlay(0); setStatus("Off"); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click a slot", "play it (click the playing one again: off); this is the PLAY parameter, made for host automation" },
            { "1 - 8, 0 / Esc", "play that slot, or off" },
            { "FLX | FLY, arrows, LIST", "a slot's bank, previous / next, and the list of patterns by family (right-click works too)" },
            { "Wheel on a slot", "step its pattern (Shift: a family at a time)" },
            { "Double-click a dial", "back to its default" },
        };
    }
    std::string footerInfo() override
    {
        const int pl = playing();
        std::string s = pl ? "SLOT " + std::to_string(pl) : "OFF";
        if (pl && slotVal(pl - 1) > 0) s += "   " + tagOf(slotVal(pl - 1)) + " " + hexText(slotVal(pl - 1));
        if (phase >= 0.0f) { char b[48]; std::snprintf(b, sizeof b, "   BAR %d BEAT %d", (int)phase / 4 + 1, (int)phase % 4 + 1); s += b; }
        return s;
    }

private:
    PluginFlex* plug = nullptr;
    Rect offR, nowR, stripR, outPanel, meterL, meterR;
    OutMeter outMeter;
    float phase = -1.0f;
    Dial *mixDial, *outDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIFlex)
};

UI* createUI() { return new UIFlex(); }

END_NAMESPACE_DISTRHO
