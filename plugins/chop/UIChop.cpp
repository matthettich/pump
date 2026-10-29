// pump CHOP: interface. REGION: the captured bar's waveform cut into its slices, the slices the held pads play lit, the
// playhead, and whether it follows the song or is held. NOW: the pad playing (or LIVE). PADS: hold one (mouse, its
// key, or its MIDI note) to play it; each shows its slice, its note and key, and a LEARN box (then press a note or a
// key). Below: the selected pad (slice, pitch, reverse, effect and amount, level) and the chopper (slices, length,
// quantise, hold, two-pad loops, tempo without a host, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginChop.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::chop;

class UIChop : public PumpUI {
public:
    UIChop() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::chop(), "CHOP", 1000, 628, kBypass)
    {
        for (int p = 0; p < kPads; ++p)
        {
            sliceD[p] = addDial(pp(p, kSlice), "SLICE", Dial::Medium); sliceD[p]->tip = "Which slice of the bar this pad plays";
            pitchD[p] = addDial(pp(p, kPitch), "PITCH", Dial::Medium, true);
            revB[p] = addToggle(pp(p, kReverse), "REVERSE");
            fxSeg[p] = addSegmented(pp(p, kFx), { kFxKeys, kFxKeys + kNumFx });
            fxSeg[p]->tip = "The pad's effect: low-pass, high-pass, crush, ratchet (back to the slice's start), tape stop, gate, drive";
            amtD[p] = addDial(pp(p, kFxAmount), "AMOUNT", Dial::Medium);
            levelD[p] = addDial(pp(p, kLevel), "LEVEL", Dial::Medium, true);
        }
        slicesSeg = addSegmented(kSlices, { "8", "16" });
        slicesSeg->tip = "How many slices the bar is cut into";
        lenMenu = addMenu(kLengthP); lenMenu->arrows = true; lenMenu->tip = "How much of the past the pads chop";
        quantMenu = addMenu(kQuantise); quantMenu->arrows = true; quantMenu->tip = "A press waits for the next grid line (a release never waits)";
        holdB = addToggle(kHold, "HOLD"); holdB->tip = "Keep the captured bar after the pads are up (chop it again and again)";
        notesB = addToggle(kMidiNotes, "PADS ON MIDI NOTES"); notesB->tip = "Off: the pads ignore MIDI notes (map Pad 1-8 Trigger in your host instead)";
        loopB = addToggle(kLoopTwo, "TWO-PAD LOOP"); loopB->tip = "Holding two or more pads loops from the lowest slice to the end of the highest";
        bpmD = addDial(kBpm, "TEMPO", Dial::Medium); bpmD->tip = "The tempo when the host sends none";
        mixD = addDial(kMix, "MIX", Dial::Medium); outD = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        if (auto* p = static_cast<PluginChop*>(pluginPtr())) { plug = p; noteSeen = p->lastNote.load() >> 8; }
        select(0);
    }
    ~UIChop() override { for (int p = 0; p < kPads; ++p) if (heldBits >> p & 1) setHeldBit(p, false); }

protected:
    static std::string noteName(int n)
    {
        char b[8];   // as Renoise writes it (C-4 = 48), like the drum plugins' pads
        return pump::padNoteName(n, b);
    }
    static std::string keyName(int k)
    {
        if (k <= 32 || k > 126) return "-";
        return std::string(1, (char)std::toupper(k));
    }
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kPads * kPerPad)
        {
            const int f = i % kPerPad;
            if (f == kPitch) { if (std::fabs(v) < 0.5f) return "0 ST"; std::snprintf(b, sizeof b, "%+.0f ST", v); return b; }
            if (f == kSlice) { std::snprintf(b, sizeof b, "%d", (int)v); return b; }
            if (f == kNote) return noteName((int)v);
            if (f == kKey) return keyName((int)v);
        }
        if (i == kBpm) { std::snprintf(b, sizeof b, "%.0f BPM", v); return b; }
        return PumpUI::formatParam(i, v);
    }
    void select(int p)
    {
        sel = pump::clampv(p, 0, kPads - 1);
        for (int q = 0; q < kPads; ++q)
        {
            const bool on = q == sel;
            sliceD[q]->visible = pitchD[q]->visible = revB[q]->visible = fxSeg[q]->visible = amtD[q]->visible = levelD[q]->visible = on;
        }
    }

    void layout() override
    {
        const float top = 58, sh = 176, hm = Dial::heightFor(Dial::Medium);
        regionR = Rect(14, top, 708, sh);
        nowR = Rect(734, top, 226, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);
        // the eight pads, edge to edge
        const float y1 = top + sh + 12, padH = 92, pw = (972 - 7 * 10) / 8.0f;
        padsR = Rect(14, y1, 972, padH);
        for (int p = 0; p < kPads; ++p) { padR[p] = Rect(14 + p * (pw + 10), y1, pw, padH); learnR[p] = Rect(padR[p].r() - 50, padR[p].y + 8, 44, 16); }
        // the selected pad in one row: SLICE PITCH REVERSE | EFFECT | AMOUNT LEVEL
        const float y2 = y1 + padH + 12, ph = 116;
        editR = Rect(14, y2, 972, ph);
        for (int p = 0; p < kPads; ++p)
        {
            sliceD[p]->r = Rect(22, y2 + 28, 80, hm); pitchD[p]->r = Rect(102, y2 + 28, 80, hm);
            revB[p]->r = Rect(194, y2 + 54, 94, 24);
            fxSeg[p]->r = Rect(302, y2 + 54, 486, 24);
            amtD[p]->r = Rect(794, y2 + 28, 90, hm); levelD[p]->r = Rect(884, y2 + 28, 90, hm);
        }
        // the chopper in one row: SLICES LENGTH QUANTISE HOLD LOOP NOTES | TEMPO MIX OUTPUT
        const float y3 = y2 + ph + 12, cy = y3 + 54;
        kitR = Rect(14, y3, 972, ph);
        slicesSeg->r = Rect(26, cy, 90, 24);
        lenMenu->r = Rect(128, cy, 118, 24);
        quantMenu->r = Rect(258, cy, 108, 24);
        holdB->r = Rect(378, cy, 70, 24);
        loopB->r = Rect(458, cy, 124, 24);
        notesB->r = Rect(592, cy, 168, 24);
        bpmD->r = Rect(766, y3 + 28, 70, hm); mixD->r = Rect(836, y3 + 28, 70, hm); outD->r = Rect(906, y3 + 28, 70, hm);
    }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        plug->engine.overview(wave, kWave);
        plug->engine.liveOverview(live, kWave);
        ls = plug->engine.liveStartNow(); ll = plug->engine.liveLenNow(); wNow = (double)plug->engine.writtenNow();
        rs = plug->engine.regionStartNow(); rl = plug->engine.regionLenNow(); ph = plug->engine.playheadNow();
        padNow = plug->engine.padNow(); heldNow = plug->engine.heldNow(); pendNow = plug->engine.pendingNow();
        for (int p = 0; p < kPads; ++p) glow[p] = (heldNow >> p & 1) ? 1.0f : std::max(0.0f, glow[p] - dt * 5.0f);
        // MIDI learn: a new note while a pad is learning
        const uint32_t ln = plug->lastNote.load();
        if ((ln >> 8) != noteSeen)
        {
            noteSeen = ln >> 8;
            if (learn >= 0) { setParamGesture(pp(learn, kNote), (float)(ln & 0xFF)); setStatus("Pad " + std::to_string(learn + 1) + ": MIDI " + noteName((int)(ln & 0xFF))); learn = -1; }
        }
    }

    void drawRegion()
    {
        const Rect& r = regionR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "REGION");
        const bool held = heldNow || pendNow || value(kHold) >= 0.5f;
        std::string t = std::string(kLengthNames[pump::clampv((int)value(kLengthP), 0, kNumLengths - 1)]) + (held ? "  ·  HELD" : "  ·  FOLLOWING THE SONG");
        for (char& c : t) c = (char)std::toupper((unsigned char)c);
        textAt(r.r() - 10, r.y + 14, t, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 36);
        const int slices = value(kSlices) >= 0.5f ? 16 : 8;
        // the slices the held pads play
        for (int q = 0; q < kPads; ++q)
            if ((heldNow | pendNow) >> q & 1)
            {
                const int s = pump::clampv((int)value(pp(q, kSlice)), 1, slices) - 1;
                fillRect(Rect(p.x + p.w * s / slices, p.y, p.w / slices, p.h), withAlpha2(theme.neon, (heldNow >> q & 1) ? 0.35f : 0.15f));
            }
        const float cy = p.cy();
        float mx = 0.05f; for (int i = 0; i < kWave; ++i) mx = std::max(mx, std::max(wave[i], live[i]));
        beginPath();
        for (int i = 0; i < kWave; ++i) { const float h = std::min(1.0f, wave[i] / mx) * p.h * 0.46f; rect(p.x + p.w * i / kWave, cy - h, std::max(1.0f, p.w / kWave), 2 * h); }
        fillColor(withAlpha2(theme.ink, 0.7f)); fill();
        for (int s = 0; s <= slices; ++s)
        {
            const float x = p.x + p.w * s / slices;
            line(x, p.y, x, p.b(), s % 4 == 0 ? theme.dim : withAlpha2(theme.dim, 0.5f), 1.0f);
            if (s < slices) { char b[4]; std::snprintf(b, sizeof b, "%d", s + 1); textAt(x + 4, p.y + 8, b, kMono, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE); }
        }
        // the bar coming in now, over the captured one: its outline in pink, growing as it is heard, and the write head
        // in green (the captured bar is the last whole one; at the end of this bar it becomes the region)
        {
            const Color pink = pump::mixColor(pump::hexColor(0xFF4FA0), theme.ink, theme.dark ? 0.0f : 0.15f);
            const Color green = theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.35f);
            int last = -1; for (int i = 0; i < kWave; ++i) if (live[i] >= 0.0f) last = i;
            if (last > 0)
            {
                for (int side = 0; side < 2; ++side)
                {
                    beginPath();
                    for (int i = 0; i <= last; ++i)
                    {
                        const float h = std::min(1.0f, live[i] / mx) * p.h * 0.46f, x = p.x + p.w * (i + 0.5f) / kWave;
                        const float y = side ? cy + h : cy - h;
                        if (i == 0) moveTo(x, y); else lineTo(x, y);
                    }
                    strokeColor(withAlpha2(pink, 0.9f)); strokeWidth(1.4f); lineJoin(ROUND); stroke();
                }
            }
            if (ll > 0.0)
            {
                const float x = p.x + p.w * (float)pump::clampv((wNow - ls) / ll, 0.0, 1.0);
                line(x, p.y, x, p.b(), withAlpha2(green, 0.95f), 1.5f);
                beginPath(); circle(x, p.y + 3, 3.0f); fillColor(green); fill();
            }
            textAt(p.x + 4, p.b() - 8, "IN NOW", kLabel, 8.0f, pink, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        }
        if (ph >= 0.0 && rl > 0.0)
        {
            const float x = p.x + p.w * (float)pump::clampv((ph - rs) / rl, 0.0, 1.0);
            line(x, p.y, x, p.b(), pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.5f), 2.5f);
        }
    }
    void drawNow()
    {
        const Rect& r = nowR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "NOW");
        const Rect b(r.x + 12, r.y + 30, r.w - 24, 70);
        if (padNow >= 0)
        {
            fillRect(b, theme.neon);
            char t[32]; std::snprintf(t, sizeof t, "PAD %d", padNow + 1);
            textAt(b.cx(), b.cy() - 8, t, kLabel, 16.0f, theme.neonInk, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
            std::snprintf(t, sizeof t, "SLICE %d", (int)value(pp(padNow, kSlice)));
            textAt(b.cx(), b.cy() + 14, t, kLabel, 10.0f, theme.neonInk, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        }
        else { strokeRect(b, theme.dim, 1.5f); textAt(b.cx(), b.cy(), pendNow ? "WAITING FOR THE GRID" : "LIVE", kLabel, 13.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f); }
        int n = 0; for (int q = 0; q < kPads; ++q) n += heldNow >> q & 1;
        char t[48]; std::snprintf(t, sizeof t, n >= 2 && value(kLoopTwo) >= 0.5f ? "%d PADS: LOOPING" : "%d PAD%s HELD", n, n == 1 ? "" : "S");
        textAt(r.x + 12, r.y + 122, t, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        std::snprintf(t, sizeof t, "QUANTISE %s", kQuantNames[pump::clampv((int)value(kQuantise), 0, kNumQuant - 1)]);
        std::string u = t; for (char& c : u) c = (char)std::toupper((unsigned char)c);
        textAt(r.x + 12, r.y + 142, u, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        if (learn >= 0) { std::snprintf(t, sizeof t, "LEARNING PAD %d: A NOTE OR A KEY", learn + 1); textAt(r.x + 12, r.y + 166, t, kLabel, 9.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.5f); }
    }
    void drawPad(int p)
    {
        const Rect& r = padR[p];
        const float g = pump::clampv(glow[p], 0.0f, 1.0f);
        const bool waiting = pendNow >> p & 1;
        fillRect(r, pump::mixColor(theme.panel2, theme.neon, 0.12f + 0.88f * std::sqrt(g)));
        strokeRect(r, p == sel ? theme.ink : theme.dim, p == sel ? 2.0f : (waiting ? 2.0f : 1.0f));
        const Color ink = g > 0.45f ? theme.neonInk : theme.ink, ink2 = g > 0.45f ? theme.neonInk : theme.ink2;
        char b[48];
        std::snprintf(b, sizeof b, "%d", p + 1);
        textAt(r.x + 8, r.y + 16, b, kLabel, 14.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.0f);
        std::snprintf(b, sizeof b, "SLICE %d", (int)value(pp(p, kSlice)));
        textAt(r.x + 8, r.y + 42, b, kLabel, 9.5f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        std::string fx = kFxKeys[pump::clampv((int)value(pp(p, kFx)), 0, kNumFx - 1)];
        const int pit = (int)value(pp(p, kPitch));
        std::snprintf(b, sizeof b, "%s%s%s", value(pp(p, kReverse)) >= 0.5f ? "REV " : "", pit ? (std::to_string(pit > 0 ? pit : pit) + "ST ").c_str() : "", fx == "-" ? "" : fx.c_str());
        textAt(r.x + 8, r.y + 58, b, kLabel, 8.5f, ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        std::snprintf(b, sizeof b, "%s · %s", value(kMidiNotes) >= 0.5f ? noteName((int)value(pp(p, kNote))).c_str() : "HOST", keyName((int)value(pp(p, kKey))).c_str());
        textAt(r.x + 8, r.b() - 12, b, kMono, 9.0f, ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect& lr = learnR[p];
        if (learn == p) fillRect(lr, theme.ink);
        strokeRect(lr, ink2, 1.0f);
        textAt(lr.cx(), lr.cy(), "LEARN", kLabel, 7.5f, learn == p ? theme.bg : ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.4f);
    }

    void drawBody() override
    {
        drawRegion(); drawNow();
        drawOutMeter(outMeter, meterL, meterR);
        for (int p = 0; p < kPads; ++p) drawPad(p);
        strokeRect(editR, theme.dim, 1.0f);
        char b[32]; std::snprintf(b, sizeof b, "PAD %d", sel + 1);
        caption(editR.x + 12, editR.y + 16, b);
        caption(fxSeg[sel]->r.x, fxSeg[sel]->r.y - 10, "EFFECT");
        strokeRect(kitR, theme.dim, 1.0f);
        caption(kitR.x + 12, kitR.y + 16, "CHOPPER");
        caption(slicesSeg->r.x, slicesSeg->r.y - 10, "SLICES"); caption(lenMenu->r.x, lenMenu->r.y - 10, "LENGTH"); caption(quantMenu->r.x, quantMenu->r.y - 10, "QUANTISE");
    }

    // a pad held from the window: its Trigger parameter, inside one edit gesture, so the host can record it
    void setHeldBit(int p, bool on)
    {
        const bool was = heldBits >> p & 1;
        if (was == on) return;
        heldBits = on ? (heldBits | 1u << p) : (heldBits & ~(1u << p));
        const uint32_t i = (uint32_t)(kTrigger0 + p);
        if (on) { openGesture(i); setParam((int)i, 1.0f); }
        else { setParam((int)i, 0.0f); closeGesture(i); }
    }
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int p = 0; p < kPads; ++p)
        {
            if (learnR[p].contains(x, y)) { learn = learn == p ? -1 : p; select(p); setStatus(learn >= 0 ? "Pad " + std::to_string(p + 1) + ": press a MIDI note or a key" : ""); return false; }
            if (padR[p].contains(x, y)) { select(p); mousePad = p; setHeldBit(p, true); return true; }
        }
        return false;
    }
    void bodyMouseUp(float, float) override { if (mousePad >= 0) { setHeldBit(mousePad, false); mousePad = -1; } }
    void releaseAllHeld() override { for (int p = 0; p < kPads; ++p) setHeldBit(p, false); mousePad = -1; }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        const int k = (int)std::tolower((int)key);
        if (k <= 32 || k > 126) return false;
        if (learn >= 0) { setParamGesture(pp(learn, kKey), (float)k); setStatus("Pad " + std::to_string(learn + 1) + ": key " + keyName(k)); learn = -1; return true; }
        bool any = false;
        for (int p = 0; p < kPads; ++p) if ((int)value(pp(p, kKey)) == k) { setHeldBit(p, true); any = true; }
        return any;
    }
    bool keyRelease(uint key, uint) override
    {
        const int k = (int)std::tolower((int)key);
        bool any = false;
        for (int p = 0; p < kPads; ++p) if ((int)value(pp(p, kKey)) == k) { setHeldBit(p, false); any = true; }
        return any;
    }
    void bodyHover(float x, float y) override
    {
        for (int p = 0; p < kPads; ++p)
        {
            if (learnR[p].contains(x, y)) { setStatus("Learn: then press a MIDI note or a computer key for pad " + std::to_string(p + 1)); return; }
            if (padR[p].contains(x, y)) { setStatus("Hold to play pad " + std::to_string(p + 1) + " (its key, its MIDI note, or map Pad " + std::to_string(p + 1) + " Trigger in the host); two held loop"); return; }
        }
        if (regionR.contains(x, y)) setStatus("The captured bar, cut into slices: it follows the song until a pad goes down");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Hold a pad", "its slice plays (mouse, its key, its MIDI note)" }, { "Two pads", "loop between their slices" },
                 { "LEARN", "then a MIDI note or a computer key: that pad's switch" }, { "HOLD", "keep the captured bar after letting go" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d SLICES", padNow >= 0 ? ("PAD " + std::to_string(padNow + 1)).c_str() : "LIVE", value(kSlices) >= 0.5f ? 16 : 8);
        return b;
    }

private:
    static constexpr int kWave = 512;
    PluginChop* plug = nullptr;
    Rect regionR, nowR, meterL, meterR, padsR, padR[kPads], learnR[kPads], editR, kitR;
    Dial *sliceD[kPads], *pitchD[kPads], *amtD[kPads], *levelD[kPads], *bpmD, *mixD, *outD;
    Button *revB[kPads], *notesB, *holdB, *loopB;
    Segmented *fxSeg[kPads], *slicesSeg;
    MenuButton *lenMenu, *quantMenu;
    int sel = 0, learn = -1, mousePad = -1, padNow = -1;
    uint32_t heldBits = 0, heldNow = 0, pendNow = 0, noteSeen = 0;
    double rs = 0.0, rl = 1.0, ph = -1.0, ls = 0.0, ll = 1.0, wNow = 0.0;
    float wave[kWave] = {}, live[kWave] = {}, glow[kPads] = {};
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIChop)
};

UI* createUI() { return new UIChop(); }

END_NAMESPACE_DISTRHO
