// pump MACHINE: interface. GPL-3.0-only (see NOTICE.md). SOUNDS (seven pads: click or keys 1-7 to play; the pad you
// play is the voice you edit), SCOPE, and the voice's own pages as the machine has them: oscillator, envelope, pitch
// envelope and velocity, FM (or the mod oscillators), transient, filter, LFO, mix. The knobs are the machine's 0-127
// values and option lists.
#include "ui/PumpUI.hpp"
#include "PluginMachine.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::machine;

class UIMachine : public PumpUI {
public:
    UIMachine() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::machine(), "MACHINE", 1000, 740, -1)
    {
        for (int i = 0; i < kNumFw; ++i)
        {
            const FwParam& f = kFw[i];
            dials[i] = addDial(i, f.dial, Dial::Small, f.kind == kPm60 || f.kind == kPm63);
            dials[i]->tip = f.name;
        }
        volD = addDial(kVolume, "VOLUME", Dial::Small, true);
        kitBtn = addButton("LOAD KIT", [this]() { FileBrowserOptions o; o.title = "Load a kit file (.snd) from the machine's card"; openFileBrowser(o); });
        kitBtn->small = true;
        kitBtn->tip = "Load a kit file (.snd) from the machine's SD card: every voice's knobs";
        followB = addToggle(kFollow, "FOLLOW"); followB->tip = "Show the sound you play: every hit (MIDI or a click) selects it for editing";
        if (auto* p = static_cast<PluginMachine*>(pluginPtr())) plug = p;
        select(0);
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        if (i < kNumFw)
        {
            char b[32];
            const int x = (int)std::lround(v);
            switch (kFw[i].kind)
            {
            case kPm60: std::snprintf(b, sizeof b, "%+d", x - 60); return x == 60 ? "0" : b;
            case kPm63: std::snprintf(b, sizeof b, "%+d", x - 63); return x == 63 ? "0" : b;
            case kOnOff: return x ? "ON" : "OFF";
            case kInt: std::snprintf(b, sizeof b, "%d", x); return b;
            default: break;
            }
        }
        return PumpUI::formatParam(i, v);
    }

    void select(int s)
    {
        sel = pump::clampv(s, 0, kSounds - 1);
        const int v = voiceOfSound(sel);
        for (int i = 0; i < kNumFw; ++i) dials[i]->visible = kFw[i].voice == v;
    }

    void layout() override
    {
        const float top = 58;
        for (int s = 0; s < kSounds; ++s) padR[s] = Rect(14 + s * 104, top, 98, 92);
        scopeR = Rect(746, top, 152, 92);
        volD->r = Rect(904, top + 10, 56, Dial::heightFor(Dial::Small));
        meterL = Rect(966, top, 8, 92); meterR = Rect(977, top, 8, 92);
        kitBtn->r = Rect(886, 155, 100, 20);
        followB->r = Rect(780, 155, 100, 20);
        for (int c = 0; c < 8; ++c) secR[c] = Rect(14 + (c % 4) * 245, 178 + (c / 4) * 262, 237, 254);
        // each voice's knobs into its sections: four slots a row, a modulation target takes two
        for (int v = 0; v < kNumVoices; ++v)
        {
            int slot[kNumSections] = {};
            for (int i = 0; i < kNumFw; ++i)
            {
                const FwParam& f = kFw[i];
                if (f.voice != v) continue;
                const int w = f.kind == kTarget ? 2 : 1;
                int& sl = slot[f.section];
                if (sl % 4 + w > 4) sl += 4 - sl % 4;
                const Rect& p = secR[f.section];
                const float cw = (p.w - 12) / 4.0f;
                dials[i]->r = Rect(p.x + 6 + (sl % 4) * cw, p.y + 26 + (sl / 4) * 76, cw * w, Dial::heightFor(Dial::Small));
                sl += w;
            }
        }
    }

    void tick(float dt) override
    {
        // FOLLOW: a pad played (MIDI, keys or a click) becomes the one being edited (not while a control is dragged)
        if (plug)
        {
            const uint32_t lh = plug->engine.lastHit();
            if (lh != lastHitSeen) { lastHitSeen = lh; if (lh >> 8 && value(kFollow) >= 0.5f && !dragging()) select((int)(lh & 0xFF)); }
        }
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        for (int s = 0; s < kSounds; ++s)
        {
            const float f = plug->engine.flash(s);
            if (f > 0.0f) { glow[s] = std::max(glow[s], f); plug->engine.clearFlash(s, 0.0f); }
            glow[s] = std::max(0.0f, glow[s] - dt * 3.0f);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        if (!scopeInit || (uint32_t)(end - scopeRead) > 8000u) { scopeRead = end; scopeInit = true; }
        while ((uint32_t)(end - scopeRead) >= (uint32_t)kColN)
        {
            float lo = 0.0f, hi = 0.0f;
            for (int k = 0; k < kColN; ++k) { const float v = plug->engine.outRing.at(scopeRead + (uint32_t)k); lo = std::min(lo, v); hi = std::max(hi, v); }
            scopeRead += (uint32_t)kColN;
            colLo[colW % kCols] = lo; colHi[colW % kCols] = hi; ++colW;
        }
    }

    void drawBody() override
    {
        for (int s = 0; s < kSounds; ++s) drawPad(s);
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        const int v = voiceOfSound(sel);
        char b[96];
        std::snprintf(b, sizeof b, "%s · THE MACHINE'S OWN KNOBS (0-127)", kVoiceNames[v]);
        caption(14, 166, b);
        textAt(770, 166, "VOICES OF AN OPEN-SOURCE DRUM MACHINE FIRMWARE · MODIFIED VERSION · GPL-3.0", kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.4f);
        for (int c = 0; c < kNumSections; ++c)
        {
            strokeRect(secR[c], theme.dim, 1.0f);
            const char* name = kSectionNames[c];
            if (c == 3 && v >= 4) name = "MOD OSCILLATORS";
            caption(secR[c].x + 12, secR[c].y + 16, name);
            bool any = false;
            for (int i = 0; i < kNumFw; ++i) if (kFw[i].voice == v && kFw[i].section == c) { any = true; break; }
            if (!any) textAt(secR[c].cx(), secR[c].cy(), "NONE ON THIS VOICE", kLabel, 9.0f, theme.dim, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        }
    }
    void drawPad(int s)
    {
        const Rect& r = padR[s];
        const float g = pump::clampv(glow[s], 0.0f, 1.0f);
        fillRect(r, pump::mixColor(theme.panel2, theme.neon, 0.15f + 0.85f * std::sqrt(g)));
        const bool on = voiceOfSound(s) == voiceOfSound(sel);
        strokeRect(r, s == sel ? theme.ink : (on ? theme.ink2 : theme.dim), s == sel ? 2.0f : 1.0f);
        const Color ink = g > 0.45f ? theme.neonInk : theme.ink;
        char b[16];
        std::snprintf(b, sizeof b, "%d", s + 1);
        textAt(r.x + 8, r.y + 12, b, kMono, 10.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(r.x + 8, r.y + 34, kSoundNames[s], kLabel, 12.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int note = noteForSound(s);
        std::snprintf(b, sizeof b, "%s%d  %d", nn[note % 12], note / 12 - 2, note);
        textAt(r.r() - 8, r.b() - 10, b, kMono, 9.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "CH %d", s + 2);
        textAt(r.x + 8, r.b() - 10, b, kMono, 9.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 6, r.y + 24, r.w - 12, r.h - 30);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        const int cols = std::min(kCols, (int)p.w);
        float mx = 1e-3f; for (int i = 0; i < kCols; ++i) mx = std::max(mx, std::max(colHi[i], -colLo[i]));
        const float want = 0.95f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.03f;
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        for (int x = 0; x < cols; ++x)
        {
            if (colW < (uint32_t)cols && x < cols - (int)colW) continue;
            const uint32_t idx = colW - (uint32_t)cols + (uint32_t)x;
            const float hi = colHi[idx % kCols], lo = colLo[idx % kCols];
            const float y0 = p.cy() - hi * scale * p.h * 0.48f, y1 = p.cy() - lo * scale * p.h * 0.48f;
            line(p.x + x + 0.5f, y0, p.x + x + 0.5f, std::max(y1, y0 + 1.0f), c, 1.0f);
        }
    }

    // a kit file: 8 name bytes, then the parameters from number 0 (as the machine's card holds them)
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return;
        std::vector<unsigned char> b;
        if (FILE* f = std::fopen(filename, "rb")) { unsigned char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && b.size() < 65536) b.insert(b.end(), buf, buf + n); std::fclose(f); }
        if (b.size() < 8 + 200) { setStatus("Not a kit file (too short): " + std::string(filename)); return; }
        std::string name;
        for (int i = 0; i < 8; ++i) if (b[(size_t)i] >= 32 && b[(size_t)i] < 127) name += (char)b[(size_t)i];
        beginGroup();
        int n = 0;
        for (int i = 0; i < kNumFw; ++i)
        {
            const size_t at = 8 + (size_t)kFw[i].fw;
            if (at >= b.size()) continue;
            setParamGesture(i, (float)pump::clampv((int)b[at], kFw[i].min, kFw[i].max));
            ++n;
        }
        endGroup();
        setStatus("Loaded kit \"" + name + "\": " + std::to_string(n) + " knobs");
    }

    void play(int s, float vel)
    {
        select(s);
        if (plug) plug->pads.push((uint8_t)s, (uint8_t)pump::clampv((int)std::lround(vel * 127.0f), 1, 127));
    }
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int s = 0; s < kSounds; ++s)
            if (padR[s].contains(x, y)) { play(s, pump::clampv(1.0f - (y - padR[s].y) / padR[s].h * 0.75f, 0.2f, 1.0f)); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int s = 0; s < kSounds; ++s)
            if (padR[s].contains(x, y)) { setStatus(std::string("Play and edit ") + kSoundNames[s] + " (higher in the pad = harder; MIDI channel " + std::to_string(s + 2) + " plays it chromatically)"); return; }
    }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        if (key >= '1' && key <= '7') { play((int)(key - '1'), 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Click a pad / keys 1-7", "play it and edit its voice (higher in the pad = harder)" },
                 { "MIDI", "General MIDI drum notes; channels 2-8 play the sounds chromatically" },
                 { "Sound engine", "an open-source drum machine firmware, modified (not official); GPL-3.0, see NOTICE.md" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   MODIFIED FIRMWARE VOICES", kSoundNames[sel]);
        return b;
    }

private:
    static constexpr int kCols = 140, kColN = 512;
    PluginMachine* plug = nullptr;
    Rect padR[kSounds], scopeR, meterL, meterR, secR[kNumSections];
    Dial* dials[kNumFw];
    Dial* volD;
    Button *kitBtn, *followB;
    uint32_t lastHitSeen = 0;
    int sel = 0;
    float glow[kSounds] = {}, colLo[kCols] = {}, colHi[kCols] = {}, scale = 1.0f;
    uint32_t colW = 0, scopeRead = 0;
    bool scopeInit = false;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIMachine)
};

UI* createUI() { return new UIMachine(); }

END_NAMESPACE_DISTRHO
