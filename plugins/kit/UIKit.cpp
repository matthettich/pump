// pump KIT: interface. PADS (click or keys 1-8 to play; higher in the pad = harder; the pad you play is the one you
// edit), SCOPE, and the selected pad's ENVELOPES (level and pitch over the hit). Below: that pad's model and sound;
// then the mixer (each pad's level and pan) and the kit's drive, velocity, humanize and volume.
#include "ui/PumpUI.hpp"
#include "PluginKit.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::kit;

class UIKit : public PumpUI {
public:
    UIKit() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::kit(), "KIT", 1000, 664, -1)
    {
        static const char* const lab[9] = { "TUNE", "DECAY", "PITCH", "P-DECAY", "TONE", "NOISE", "CLICK", "CHAR", "DRIVE" };
        static const int what[9] = { kTune, kDecay, kPitch, kPitchDecay, kTone, kNoise, kClick, kChar, kDrive };
        for (int p = 0; p < kPads; ++p)
        {
            model[p] = addSegmented(pp(p, kModel), { "KICK", "SNARE", "CLAP", "HAT", "TOM", "METAL", "FM", "NOISE" });
            model[p]->tip = "The voice model for this pad (any pad can be any model)";
            for (int k = 0; k < 9; ++k) { edit[p][k] = addDial(pp(p, what[k]), lab[k], Dial::Medium, what[k] == kTune); }
            levelD[p] = addDial(pp(p, kLevel), "LEVEL", Dial::Small, true);
            panD[p] = addDial(pp(p, kPan), "PAN", Dial::Small, true);
        }
        kitDrive = addDial(kKitDrive, "DRIVE", Dial::Small);
        kitDrive->tip = "Saturation on the whole kit";
        velD = addDial(kVelocity, "VELOCITY", Dial::Small);
        velD->tip = "How much a hit's velocity changes its level (0 = every hit full)";
        humD = addDial(kHumanize, "HUMANIZE", Dial::Small);
        humD->tip = "Every hit varies a little in pitch, length and level";
        volD = addDial(kVolume, "VOLUME", Dial::Small, true);
        followB = addToggle(kFollow, "FOLLOW"); followB->tip = "Show the pad you play: every hit (MIDI, keys, a click) selects it for editing";
        if (auto* p = static_cast<PluginKit*>(pluginPtr())) plug = p;
        select(0);
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kPads * kPerPad)
        {
            const int w = i % kPerPad;
            const int m = (int)value(i - w + kModel);
            if (w == kTune) { if (std::fabs(v) < 0.005f) return "0 st"; std::snprintf(b, sizeof b, "%+.1f st", v); return b; }
            if (w == kPitch) { if (m == kFm) { std::snprintf(b, sizeof b, "IDX %.1f", v / 4.0f); return b; } if (v < 0.05f) return "OFF"; std::snprintf(b, sizeof b, "+%.0f st", v); return b; }
            if (w == kPan) { if (std::fabs(v) < 0.5f) return "C"; std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b; }
            if (w == kChar && m == kFm) { std::snprintf(b, sizeof b, "x%.2f", 0.5 * std::exp2(v * 0.04)); return b; }
        }
        return PumpUI::formatParam(i, v);
    }

    void select(int p)
    {
        sel = pump::clampv(p, 0, kPads - 1);
        for (int q = 0; q < kPads; ++q) { model[q]->visible = q == sel; for (auto* d : edit[q]) d->visible = q == sel; }
    }

    void layout() override
    {
        const float top = 58, sh = 212, hs = Dial::heightFor(Dial::Small), hm = Dial::heightFor(Dial::Medium);
        padsR = Rect(14, top, 480, sh);
        for (int p = 0; p < kPads; ++p) padR[p] = Rect(24 + (p % 4) * 117, top + 28 + (p / 4) * 92, 109, 84);
        scopeR = Rect(506, top, 454, (sh - 12) / 2);
        envR = Rect(506, top + (sh + 12) / 2, 454, (sh - 12) / 2);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);
        // the selected pad: model + FOLLOW, then its nine dials
        const float y1 = top + sh + 12, eh = 148;
        editR = Rect(14, y1, 972, eh);
        for (int p = 0; p < kPads; ++p)
        {
            model[p]->r = Rect(26, y1 + 28, 820, 24);
            for (int k = 0; k < 9; ++k) edit[p][k]->r = Rect(24 + k * 105.8f, y1 + 62, 105.8f, hm);
        }
        followB->r = Rect(858, y1 + 28, 116, 24);
        // MIXER (level over pan, per pad) beside KIT
        const float y2 = y1 + eh + 12, mh = 188;
        mixR = Rect(14, y2, 760, mh);
        for (int p = 0; p < kPads; ++p)
        {
            levelD[p]->r = Rect(22 + p * 94, y2 + 28, 88, hs);
            panD[p]->r = Rect(22 + p * 94, y2 + 96, 88, hs);
        }
        kitR = Rect(786, y2, 200, mh);
        velD->r = Rect(794, y2 + 28, 92, hs); humD->r = Rect(886, y2 + 28, 92, hs);
        kitDrive->r = Rect(794, y2 + 96, 92, hs); volD->r = Rect(886, y2 + 96, 92, hs);
    }

    void tick(float dt) override
    {
        // FOLLOW: a pad played (MIDI, keys or a click) becomes the one being edited (not while a control is dragged)
        if (plug)
        {
            const uint32_t lh = plug->engine.lastHit();
            if (lh != lastHitSeen) { lastHitSeen = lh; if (lh >> 8 && value(kFollow) >= 0.5f && !dragging()) select((int)(lh & 0xFF)); }
        }
        for (int p = 0; p < kPads; ++p)
        {
            const int m = (int)value(pp(p, kModel));
            edit[p][2]->label = m == kFm ? "INDEX" : (m == kNoiseModel ? "SWEEP" : "PITCH");
            edit[p][3]->label = m == kFm ? "I-DECAY" : (m == kNoiseModel ? "S-DECAY" : "P-DECAY");
        }
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        // the scope scrolls: new samples become min / max columns of kColN samples each
        const uint32_t end = plug->engine.outRing.smoothPosition();
        if (!scopeInit || (uint32_t)(end - scopeRead) > 8000u) { scopeRead = end; scopeInit = true; }
        while ((uint32_t)(end - scopeRead) >= (uint32_t)kColN)
        {
            float lo = 0.0f, hi = 0.0f;
            for (int k = 0; k < kColN; ++k) { const float v = plug->engine.outRing.at(scopeRead + (uint32_t)k); lo = std::min(lo, v); hi = std::max(hi, v); }
            scopeRead += (uint32_t)kColN;
            colLo[colW % kCols] = lo; colHi[colW % kCols] = hi; ++colW;
        }
        for (int p = 0; p < kPads; ++p) { const float v = plug->engine.padLevel(p); glow[p] = std::max(v, glow[p] - dt * 4.0f); }
    }

    void drawBody() override
    {
        screenBox(padsR);
        caption(padsR.x + 10, padsR.y + 14, "PADS");
        textAt(padsR.r() - 10, padsR.y + 14, "CLICK OR 1-8 · HIGHER = HARDER", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        for (int p = 0; p < kPads; ++p) drawPad(p);
        drawScope();
        drawEnvelopes();
        drawOutMeter(outMeter, meterL, meterR);
        strokeRect(editR, theme.dim, 1.0f);
        char b[64];
        std::snprintf(b, sizeof b, "PAD %d · %s", sel + 1, kPadInfo[sel].label);
        caption(editR.x + 12, editR.y + 16, b);
        textAt(editR.r() - 12, editR.y + 16, modelHint((int)value(pp(sel, kModel))), kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        strokeRect(mixR, theme.dim, 1.0f); caption(mixR.x + 12, mixR.y + 16, "MIXER");
        for (int p = 0; p < kPads; ++p)
        {
            const Rect& lr = levelD[p]->r;
            textAt(lr.cx(), mixR.b() - 12, kPadInfo[p].label, kLabel, 9.5f, p == sel ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            if (p == sel) line(lr.x + 14, mixR.b() - 4, lr.r() - 14, mixR.b() - 4, theme.neon, 2.0f);
        }
        strokeRect(kitR, theme.dim, 1.0f); caption(kitR.x + 12, kitR.y + 16, "KIT");
    }
    static const char* modelHint(int m)
    {
        switch (m)
        {
        case kKick: return "SINE WITH A PITCH DROP · CHAR = HARDER · TONE = LOW-PASS";
        case kSnare: return "TWO BODY MODES + NOISE · NOISE = MIX · CHAR = NOISE LENGTH · TONE = NOISE HIGH-PASS";
        case kClap: return "NOISE BURSTS THEN A TAIL · CHAR = BURST SPREAD · TONE = BAND · PITCH SWEEPS THE BAND";
        case kHat: return "SIX SQUARE PARTIALS + NOISE · CHAR = METALLIC SPREAD · TONE = BAND";
        case kTom: return "SINE / TRIANGLE WITH A PITCH DROP · CHAR = TRIANGLE · TONE = LOW-PASS";
        case kMetal: return "SQUARE PARTIALS, LOWER AND LONGER · CHAR = SPREAD · TONE = BAND";
        case kFm: return "TWO-OPERATOR FM · INDEX FALLS OVER I-DECAY · CHAR = RATIO · TONE = LOW-PASS";
        default: return "RESONANT FILTERED NOISE · SWEEP FALLS OVER S-DECAY · CHAR = RESONANCE · TONE = BASE";
        }
    }
    void drawPad(int p)
    {
        const Rect& r = padR[p];
        const float g = pump::clampv(glow[p], 0.0f, 1.0f);
        fillRect(r, pump::mixColor(theme.panel2, theme.neon, 0.15f + 0.85f * std::sqrt(g)));
        strokeRect(r, p == sel ? theme.ink : theme.dim, p == sel ? 2.0f : 1.0f);
        const Color ink = g > 0.45f ? theme.neonInk : theme.ink;
        char b[32];
        std::snprintf(b, sizeof b, "%d", p + 1);
        textAt(r.x + 8, r.y + 12, b, kMono, 10.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(r.x + 8, r.y + 32, kPadInfo[p].label, kLabel, 12.5f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.x + 8, r.y + 50, kModelKeys[pump::clampv((int)value(pp(p, kModel)), 0, kNumModels - 1)], kLabel, 9.5f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int note = kPadInfo[p].note;   // the note that plays it: name and number
        std::snprintf(b, sizeof b, "%s%d", nn[note % 12], note / 12 - 1);
        textAt(r.x + 8, r.b() - 10, b, kMono, 10.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "%d", note);
        textAt(r.r() - 8, r.b() - 10, b, kMono, 9.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 7, r.y + 24, r.w - 14, r.h - 30);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        // the last couple of seconds, scrolling: a min / max column per kColN samples
        const int cols = std::min(kCols, (int)p.w);
        float mx = 1e-3f; for (int i = 0; i < kCols; ++i) mx = std::max(mx, std::max(colHi[i], -colLo[i]));
        const float want = 0.95f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.03f;
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        for (int x = 0; x < cols; ++x)
        {
            const uint32_t idx = colW - (uint32_t)cols + (uint32_t)x;
            if (colW < (uint32_t)cols && x < cols - (int)colW) continue;
            const float hi = colHi[idx % kCols], lo = colLo[idx % kCols];
            const float y0 = p.cy() - hi * scale * p.h * 0.48f, y1 = p.cy() - lo * scale * p.h * 0.48f;
            line(p.x + x + 0.5f, y0, p.x + x + 0.5f, std::max(y1, y0 + 1.0f), c, 1.0f);
        }
    }
    void drawEnvelopes()
    {
        const Rect& r = envR;
        screenBox(r);
        char b[64];
        std::snprintf(b, sizeof b, "%s · ENVELOPES", kPadInfo[sel].label);
        caption(r.x + 10, r.y + 14, b);
        const Rect p(r.x + 10, r.y + 24, r.w - 20, r.h - 40);
        const double decay = value(pp(sel, kDecay)) * 0.001, pdec = value(pp(sel, kPitchDecay)) * 0.001, pitch = value(pp(sel, kPitch)) / 48.0;
        const double span = std::max(0.05, decay * 1.1);
        line(p.x, p.b(), p.r(), p.b(), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        for (int i = 0; i <= 160; ++i)
        {
            const double t = span * i / 160.0, a = std::pow(10.0, -3.0 * t / decay);
            const float x = p.x + p.w * i / 160.0f, y = p.b() - (float)a * p.h;
            if (i == 0) moveTo(x, p.b()); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        if (pitch > 0.001)
        {
            beginPath();
            for (int i = 0; i <= 160; ++i)
            {
                const double t = span * i / 160.0, v = pitch * std::exp(-t / pdec);
                const float x = p.x + p.w * i / 160.0f, y = p.b() - (float)v * p.h;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(theme.neon); strokeWidth(1.6f); stroke();
        }
        const int m = (int)value(pp(sel, kModel));
        textAt(r.x + 10, r.b() - 9, "LEVEL", kLabel, 9.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        textAt(r.x + 56, r.b() - 9, m == kFm ? "FM INDEX" : (m == kNoiseModel ? "SWEEP" : "PITCH"), kLabel, 9.0f, pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.4f), ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        if (span >= 1.0) std::snprintf(b, sizeof b, "%.2f s", span); else std::snprintf(b, sizeof b, "%.0f ms", span * 1000.0);
        textAt(r.r() - 10, r.b() - 9, b, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void play(int p, float vel)
    {
        select(p);
        if (plug) plug->keys.push((uint8_t)noteForPad(p), (uint8_t)pump::clampv((int)std::lround(vel * 127.0f), 1, 127));
    }
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int p = 0; p < kPads; ++p)
            if (padR[p].contains(x, y)) { play(p, pump::clampv(1.0f - (y - padR[p].y) / padR[p].h * 0.75f, 0.2f, 1.0f)); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int p = 0; p < kPads; ++p) if (padR[p].contains(x, y)) { setStatus(std::string("Play and edit ") + kPadInfo[p].name + " (higher in the pad = harder)"); return; }
        if (envR.contains(x, y)) setStatus("The selected pad's level over the hit, and its pitch (or FM index / sweep) envelope");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        if (key >= '1' && key <= '8') { play((int)(key - '1'), 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Click a pad / keys 1-8", "play it and edit it (higher in the pad = harder)" }, { "MIDI", "C4 up in whole steps (C4 kick ... D5 cymbal); General MIDI drum notes too, toms 41-50 at their own pitches" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "PAD %d %s   %s", sel + 1, kPadInfo[sel].label, kModelKeys[pump::clampv((int)value(pp(sel, kModel)), 0, kNumModels - 1)]);
        return b;
    }

private:
    static constexpr int kCols = 440, kColN = 192;   // ~1.8 s at 48 kHz
    PluginKit* plug = nullptr;
    Rect padsR, padR[kPads], scopeR, envR, meterL, meterR, editR, mixR, kitR;
    Segmented* model[kPads];
    Button* followB;
    uint32_t lastHitSeen = 0;
    Dial* edit[kPads][9];
    Dial *levelD[kPads], *panD[kPads], *kitDrive, *velD, *humD, *volD;
    int sel = 0;
    float colLo[kCols] = {}, colHi[kCols] = {}, scale = 1.0f, glow[kPads] = {};
    uint32_t colW = 0, scopeRead = 0;
    bool scopeInit = false;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIKit)
};

UI* createUI() { return new UIKit(); }

END_NAMESPACE_DISTRHO
