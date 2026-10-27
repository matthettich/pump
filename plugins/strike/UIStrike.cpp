// pump STRIKE: interface. HITS: every pad's level over the last few seconds, a lane each, newest on the right; PADS
// (click or keys 1-8; higher in the pad = harder; the pad you play is the one you edit). Below: the selected pad (its
// engine; pitch, decay, the engine's TONE and two characters, named for what they do in that engine; bend, noise,
// level, pan), then the kit (choke, velocity, drive, volume) and a scope.
#include "ui/PumpUI.hpp"
#include "PluginStrike.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::strike;

class UIStrike : public PumpUI {
public:
    UIStrike() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::strike(), "STRIKE", 1000, 690, -1)
    {
        static const char* const lab[kPerPad] = { "ENGINE", "PITCH", "DECAY", "TONE", "CHAR A", "CHAR B", "BEND", "B-TIME", "NOISE", "LEVEL", "PAN" };
        for (int p = 0; p < kPads; ++p)
        {
            engSeg[p] = addSegmented(pp(p, kEngine), { kEngineKeys, kEngineKeys + kNumEngines });
            engSeg[p]->tip = "Modal objects · a waveguide string · an analog circuit · three-operator FM · west-coast fold and gate · additive partials · chaos";
            for (int f = kPitch; f < kPerPad; ++f) edit[p][f] = addDial(pp(p, f), lab[f], Dial::Small, f == kPan || f == kLevel);
            edit[p][kBend]->tip = "The pitch starts this many semitones up and falls back in B-TIME";
            edit[p][kNoise]->tip = "Noise that bursts and then rattles with the body (snare wires, sizzle, snap)";
        }
        auto dial = [&](int p, const char* l, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small); if (tip) d->tip = tip; return d; };
        velD = dial(kVelocity, "VELOCITY"); driveD = dial(kDrive, "DRIVE"); volD = addDial(kVolume, "VOLUME", Dial::Small, true);
        chokeB = addToggle(kChoke, "CHOKE");
        chokeB->tip = "The closed hat cuts the open hat";
        followB = addToggle(kFollow, "FOLLOW"); followB->tip = "Show the pad you play: every hit (MIDI, keys, a click) selects it for editing";
        if (auto* p = static_cast<PluginStrike*>(pluginPtr())) plug = p;
        select(0);
    }

protected:
    // what TONE, CHAR A and CHAR B do in each engine
    static constexpr const char* kToneName[kNumEngines] = { "HARDNESS", "BRIGHT", "PULSE", "INDEX", "RING", "TILT", "CENTRE" };
    static constexpr const char* kAName[kNumEngines] = { "OBJECT", "STIFFNESS", "SQUEEZE", "RATIO", "FOLD", "DENSITY", "CHAOS" };
    static constexpr const char* kBName[kNumEngines] = { "MATERIAL", "EXCITE", "SECOND", "FEEDBACK", "SYMMETRY", "SHIMMER", "RESONANCE" };
    static constexpr const char* kToneTip[kNumEngines] = { "The mallet: soft felt (0) to a hard stick (100)", "The loop's brightness: how fast the highs die", "The pulse that pings the circuit: dull to bright",
        "The FM depth (it falls faster than the level)", "How bright the gate opens", "The partials' tilt: dark to bright", "The band-pass's centre" };
    static constexpr const char* kATip[kNumEngines] = { "The object: membrane (0-24), plate (25-49), bar (50-74), bell (75-100)", "Dispersion in the loop: toward a bell or a steel drum",
        "The pitch rises with the level, as the circuit's does", "The modulator's ratio to the pitch", "How hard the triangle folds", "A few bell-like partials (0) to a dense cymbal (100)", "From a clean orbit to chaos" };
    static constexpr const char* kBTip[kNumEngines] = { "Wood (0: the upper modes die fast) to metal (100)", "A click (0) to a burst of noise (100)", "A second resonator a fifth up (cowbell, clave)",
        "The third operator's feedback: toward noise", "The fold's bias: even harmonics", "How differently the partials die", "The band-pass's resonance" };

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kPads * kPerPad)
        {
            const int f = i % kPerPad, p = i / kPerPad;
            if (f == kPitch)
            {
                static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const int n = (int)std::lround(v); std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1); return b;
            }
            if (f == kPan) { if (std::fabs(v) < 0.5f) return "C"; std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b; }
            if (f == kBend) { if (v < 0.05f) return "OFF"; std::snprintf(b, sizeof b, "+%.0f st", v); return b; }
            if (f == kCharA && (int)value(pp(p, kEngine)) == kModal) { static const char* const o[4] = { "MEMBRANE", "PLATE", "BAR", "BELL" }; return o[std::min(3, (int)(v / 25.0f))]; }
            if (f == kCharA && (int)value(pp(p, kEngine)) == kFm) { std::snprintf(b, sizeof b, "x%.2f", 0.5f + 7.5f * (v * 0.01f) * (v * 0.01f)); return b; }
        }
        return PumpUI::formatParam(i, v);
    }
    void select(int p)
    {
        sel = pump::clampv(p, 0, kPads - 1);
        for (int q = 0; q < kPads; ++q)
        {
            engSeg[q]->visible = q == sel;
            for (int f = kPitch; f < kPerPad; ++f) edit[q][f]->visible = q == sel;
        }
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        hitsR = Rect(14, top, 560, 228);
        padsR = Rect(586, top, 400, 228);
        for (int p = 0; p < kPads; ++p) padR[p] = Rect(596 + (p % 4) * 96, top + 28 + (p / 4) * 98, 90, 92);
        editR = Rect(14, 298, 972, 210);
        static const int order[10] = { kPitch, kDecay, kTone, kCharA, kCharB, kBend, kBendTime, kNoise, kLevel, kPan };
        for (int p = 0; p < kPads; ++p)
        {
            engSeg[p]->r = Rect(24, 326, 952, 22);
            for (int k = 0; k < 10; ++k) edit[p][order[k]]->r = k < 5 ? Rect(22 + k * 192, 360, 192, hs) : Rect(22 + (k - 5) * 192, 432, 192, hs);
        }
        kitR = Rect(14, 520, 560, 130);
        Dial* kd[3] = { velD, driveD, volD };
        for (int k = 0; k < 3; ++k) kd[k]->r = Rect(22 + k * 120, 556, 120, hs);
        chokeB->r = Rect(400, 576, 140, 26);
        followB->r = Rect(866, 304, 110, 20);
        scopeR = Rect(586, 520, 372, 130);
        meterL = Rect(964, 520, 8, 118); meterR = Rect(975, 520, 8, 118);
    }

    void tick(float dt) override
    {
        // TONE / CHAR A / CHAR B are named for the selected pad's engine
        const int eng = pump::clampv((int)value(pp(sel, kEngine)), 0, kNumEngines - 1);
        edit[sel][kTone]->label = kToneName[eng]; edit[sel][kTone]->tip = kToneTip[eng];
        edit[sel][kCharA]->label = kAName[eng]; edit[sel][kCharA]->tip = kATip[eng];
        edit[sel][kCharB]->label = kBName[eng]; edit[sel][kCharB]->tip = kBTip[eng];
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        // FOLLOW: a pad played (MIDI, keys or a click) becomes the one being edited (not while a control is dragged)
        if (plug)
        {
            const uint32_t lh = plug->engine.lastHit();
            if (lh != lastHitSeen) { lastHitSeen = lh; if (lh >> 8 && value(kFollow) >= 0.5f && !dragging()) select((int)(lh & 0xFF)); }
        }
        const uint32_t hits = plug->engine.takeHits();
        lanePos = (lanePos + 1) % kLane;
        for (int p = 0; p < kPads; ++p)
        {
            if (hits >> p & 1) glow[p] = 1.0f; else glow[p] = std::max(0.0f, glow[p] - dt * 4.0f);
            lane[p][lanePos] = plug->engine.padLevel(p);
            hit[p][lanePos] = (hits >> p & 1) != 0;
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
    }

    void drawHits()
    {
        const Rect& r = hitsR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "HITS");
        textAt(r.r() - 10, r.y + 14, "EVERY PAD, THE LAST FEW SECONDS", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const float x0 = r.x + 76, w = r.w - 86, lh = (r.h - 34) / kPads;
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        for (int p = 0; p < kPads; ++p)
        {
            const float y0 = r.y + 28 + p * lh, cy = y0 + lh * 0.5f;
            textAt(r.x + 10, cy, kPadInfo[p].key, kLabel, 8.5f, p == sel ? theme.ink : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            line(x0, cy, x0 + w, cy, theme.grid, 1.0f);
            beginPath();
            for (int i = 0; i < kLane; ++i)
            {
                const int k = (lanePos + 1 + i) % kLane;
                const float db = 20.0f * std::log10(lane[p][k] + 1e-6f), h = pump::clampv((db + 48.0f) / 48.0f, 0.0f, 1.0f) * lh * 0.45f;
                const float x = x0 + w * i / (kLane - 1.0f);
                if (!i) moveTo(x, cy - h); else lineTo(x, cy - h);
            }
            for (int i = kLane - 1; i >= 0; --i)
            {
                const int k = (lanePos + 1 + i) % kLane;
                const float db = 20.0f * std::log10(lane[p][k] + 1e-6f), h = pump::clampv((db + 48.0f) / 48.0f, 0.0f, 1.0f) * lh * 0.45f;
                lineTo(x0 + w * i / (kLane - 1.0f), cy + h);
            }
            closePath(); fillColor(withAlpha2(c, p == sel ? 0.9f : 0.55f)); fill();
            for (int i = 0; i < kLane; ++i) if (hit[p][(lanePos + 1 + i) % kLane]) { const float x = x0 + w * i / (kLane - 1.0f); line(x, y0 + 2, x, y0 + lh - 2, theme.ink, 1.0f); }
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
        textAt(r.x + 8, r.y + 32, kPadInfo[p].key, kLabel, 12.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.x + 8, r.y + 50, kEngineKeys[pump::clampv((int)value(pp(p, kEngine)), 0, kNumEngines - 1)], kLabel, 8.5f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        std::snprintf(b, sizeof b, "%d", kPadInfo[p].note);
        textAt(r.r() - 8, r.b() - 10, b, kMono, 9.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        float mx = 1e-3f; for (int i = 0; i < kScopeN; ++i) mx = std::max(mx, std::fabs(scope[i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < kScopeN; i += 4)
        {
            const float x = p.x + p.w * i / (kScopeN - 1), y = p.cy() - scope[i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.4f); lineJoin(ROUND); stroke();
        restore();
    }

    void drawBody() override
    {
        drawHits();
        screenBox(padsR);
        caption(padsR.x + 10, padsR.y + 14, "PADS");
        textAt(padsR.r() - 10, padsR.y + 14, "CLICK OR 1-8 · HIGHER = HARDER", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        for (int p = 0; p < kPads; ++p) drawPad(p);
        strokeRect(editR, theme.dim, 1.0f);
        char b[64];
        std::snprintf(b, sizeof b, "PAD %d · %s", sel + 1, kPadInfo[sel].key);
        caption(editR.x + 12, editR.y + 16, b);
        strokeRect(kitR, theme.dim, 1.0f); caption(kitR.x + 12, kitR.y + 16, "KIT");
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
    }

    void play(int p, float vel)
    {
        select(p);
        if (plug) plug->keys.push((uint8_t)p, (uint8_t)pump::clampv((int)std::lround(vel * 127.0f), 1, 127));
    }
    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        for (int p = 0; p < kPads; ++p)
            if (padR[p].contains(x, y)) { play(p, pump::clampv(1.0f - (y - padR[p].y) / padR[p].h * 0.75f, 0.2f, 1.0f)); return true; }
        // a lane in HITS selects its pad
        const float lh = (hitsR.h - 34) / kPads;
        if (hitsR.contains(x, y) && y > hitsR.y + 28) { select((int)((y - hitsR.y - 28) / lh)); return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int p = 0; p < kPads; ++p) if (padR[p].contains(x, y)) { setStatus(std::string("Play and edit ") + kPadInfo[p].name + " (higher in the pad = harder)"); return; }
        if (hitsR.contains(x, y)) setStatus("Every pad's level over the last few seconds (click a lane to edit that pad)");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        if (key >= '1' && key <= '8') { play((int)(key - '1'), 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Click a pad / keys 1-8", "play it and edit it (higher in the pad = harder)" }, { "MIDI", "General MIDI drum notes; C3 up plays the pads in order" },
                 { "TONE / CHAR A / CHAR B", "named for what they do in the pad's engine" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "PAD %d %s   %s", sel + 1, kPadInfo[sel].key, kEngineKeys[pump::clampv((int)value(pp(sel, kEngine)), 0, kNumEngines - 1)]);
        return b;
    }

private:
    static constexpr int kScopeN = 4096, kLane = 180;
    PluginStrike* plug = nullptr;
    Rect hitsR, padsR, padR[kPads], editR, kitR, scopeR, meterL, meterR;
    Segmented* engSeg[kPads];
    Dial* edit[kPads][kPerPad] = {};
    Dial *velD, *driveD, *volD;
    Button *chokeB, *followB;
    uint32_t lastHitSeen = 0;
    int sel = 0, lanePos = 0;
    float glow[kPads] = {}, scope[kScopeN] = {}, scale = 1.0f, lane[kPads][kLane] = {};
    bool hit[kPads][kLane] = {};
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIStrike)
};

UI* createUI() { return new UIStrike(); }

END_NAMESPACE_DISTRHO
