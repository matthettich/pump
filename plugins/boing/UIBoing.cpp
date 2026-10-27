// pump BOING: interface. SPRINGS (every pad's spring, drawn as SPRING draws its tank, ringing with what it plays),
// PADS (click or keys 1-8; higher in the pad = harder; the pad you play is the one you edit). Below: the selected pad
// (its spring tank and analog wave, the blend between them, pitch, decay, harmonic, spread, drip, bend, noise, click,
// hits, level, pan, send), then the kit (the shared tank, choke, velocity, drive, volume) and a scope.
#include "ui/PumpUI.hpp"
#include "PluginBoing.hpp"
#include "../coil/CoilView.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::boing;

class UIBoing : public PumpUI {
public:
    UIBoing() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::boing(), "BOING", 1000, 800, -1)
    {
        static const char* const lab[kPerPad] = { "TANK", "WAVE", "BLEND", "PITCH", "DECAY", "HARMONIC", "SPREAD", "DRIP", "BEND", "B-TIME", "NOISE", "N-TONE", "CLICK", "HITS", "LEVEL", "PAN", "SEND" };
        for (int p = 0; p < kPads; ++p)
        {
            tankSeg[p] = addSegmented(pp(p, kTank), { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" });
            tankSeg[p]->tip = "The spring layer's tank (SPRING's six models)";
            waveSeg[p] = addSegmented(pp(p, kWave), { "SINE", "TRI", "FOLD", "SQUARE", "SAW", "METAL" });
            waveSeg[p]->tip = "The analog layer's oscillator";
            for (int f = kBlend; f < kPerPad; ++f) edit[p][f] = addDial(pp(p, f), lab[f], Dial::Small, f == kPan || f == kLevel);
            edit[p][kBlend]->tip = "0: all analog · 100: all spring";
            edit[p][kHarmonic]->tip = "Spring: its clang (0) to a clean tone (100) · Analog: the fold / square's edge";
            edit[p][kSpread]->tip = "Spring: its springs apart · Analog: a second, detuned oscillator (Metal: the partials' spread)";
            edit[p][kDrip]->tip = "Spring: the chirp · Noise: the filter's resonance";
            edit[p][kBend]->tip = "The analog layer starts this many semitones above the pitch and falls back in B-TIME";
            edit[p][kHits]->tip = "Repeats the start 1-4 times, 11 ms apart (a clap)";
            edit[p][kSend]->tip = "Into the kit's spring tank";
        }
        tankModel = addSegmented(kTankModel, { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" });
        tankModel->tip = "The kit's tank: a real spring reverb every pad's SEND feeds";
        auto dial = [&](int p, const char* l, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small); if (tip) d->tip = tip; return d; };
        tankDecay = dial(kTankDecay, "DECAY"); tankLevel = dial(kTankLevel, "RETURN");
        velD = dial(kVelocity, "VELOCITY", "How much a hit's velocity changes its level (it always kicks the spring harder)");
        driveD = dial(kDrive, "DRIVE"); volD = addDial(kVolume, "VOLUME", Dial::Small, true);
        chokeB = addToggle(kChoke, "CHOKE");
        chokeB->tip = "The closed hat cuts the open hat";
        springsView.labels = kLabels;
        if (auto* p = static_cast<PluginBoing*>(pluginPtr())) plug = p;
        select(0);
    }

protected:
    static constexpr const char* kLabels[kPads] = { "KICK", "SNARE", "CLAP", "LO TOM", "HI TOM", "C-HAT", "O-HAT", "CYMBAL" };

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i < kPads * kPerPad)
        {
            const int f = i % kPerPad;
            if (f == kPitch)
            {
                static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const int n = (int)std::lround(v); std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1); return b;
            }
            if (f == kPan) { if (std::fabs(v) < 0.5f) return "C"; std::snprintf(b, sizeof b, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v)); return b; }
            if (f == kBend) { if (v < 0.05f) return "OFF"; std::snprintf(b, sizeof b, "+%.0f st", v); return b; }
            if (f == kBlend) { if (v < 0.5f) return "ANALOG"; if (v > 99.5f) return "SPRING"; std::snprintf(b, sizeof b, "%.0f%%", v); return b; }
            if (f == kHits) { std::snprintf(b, sizeof b, "%d", (int)v); return b; }
        }
        return PumpUI::formatParam(i, v);
    }
    void select(int p)
    {
        sel = pump::clampv(p, 0, kPads - 1);
        for (int q = 0; q < kPads; ++q)
        {
            tankSeg[q]->visible = waveSeg[q]->visible = q == sel;
            for (int f = kBlend; f < kPerPad; ++f) edit[q][f]->visible = q == sel;
        }
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        springsR = Rect(14, top, 560, 228);
        padsR = Rect(586, top, 400, 228);
        for (int p = 0; p < kPads; ++p) padR[p] = Rect(596 + (p % 4) * 96, top + 28 + (p / 4) * 98, 90, 92);
        editR = Rect(14, 298, 972, 224);
        static const int order[15] = { kBlend, kPitch, kDecay, kHarmonic, kSpread, kDrip, kBend, kBendTime, kNoise, kNoiseTone, kClick, kHits, kLevel, kPan, kSend };
        for (int p = 0; p < kPads; ++p)
        {
            tankSeg[p]->r = Rect(118, 326, 380, 22);
            waveSeg[p]->r = Rect(596, 326, 380, 22);
            for (int k = 0; k < 15; ++k)
            {
                const int f = order[k];
                edit[p][f]->r = k < 8 ? Rect(22 + k * 120, 360, 120, hs) : Rect(22 + (k - 8) * 137, 440, 137, hs);
            }
        }
        kitR = Rect(14, 534, 560, 234);
        tankModel->r = Rect(24, 562, 540, 22);
        Dial* kd[5] = { tankDecay, tankLevel, velD, driveD, volD };
        for (int k = 0; k < 5; ++k) kd[k]->r = Rect(22 + k * 108, 600, 108, hs);
        chokeB->r = Rect(24, 694, 120, 24);
        scopeR = Rect(586, 534, 372, 234);
        meterL = Rect(964, 534, 8, 222); meterR = Rect(975, 534, 8, 222);
    }

    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const uint32_t hits = plug->engine.takeHits();
        for (int p = 0; p < kPads; ++p)
        {
            if (hits >> p & 1) { glow[p] = 1.0f; if (p == 0) shakeAge = 0.0f; }
            else glow[p] = std::max(0.0f, glow[p] - dt * 4.0f);
            const bool on = plug->engine.padActive(p);
            springsView.updateRow(p, plug->engine.visV[(size_t)p], plug->engine.padLevel(p), on ? (int)std::lround(value(pp(p, kPitch))) : -1, plug->engine.visRate(), dt);
        }
        shakeAge += dt;
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
    }

    void drawBody() override
    {
        springsView.draw(*this, springsR, shakeAge, "SPRINGS", "THE PADS' SPRINGS");
        screenBox(padsR);
        caption(padsR.x + 10, padsR.y + 14, "PADS");
        textAt(padsR.r() - 10, padsR.y + 14, "CLICK OR 1-8 · HIGHER = HARDER", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        for (int p = 0; p < kPads; ++p) drawPad(p);
        strokeRect(editR, theme.dim, 1.0f);
        char b[64];
        std::snprintf(b, sizeof b, "PAD %d · %s", sel + 1, kPadInfo[sel].label);
        caption(editR.x + 12, editR.y + 16, b);
        textAt(118, 316, "SPRING TANK", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(596, 316, "ANALOG WAVE", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        strokeRect(kitR, theme.dim, 1.0f); caption(kitR.x + 12, kitR.y + 16, "KIT · THE SHARED TANK");
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
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
        textAt(r.x + 8, r.y + 32, kPadInfo[p].label, kLabel, 12.0f, ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        const int bl = (int)std::lround(value(pp(p, kBlend)));
        std::snprintf(b, sizeof b, "%s · %s", kTankKeys[pump::clampv((int)value(pp(p, kTank)), 0, 5)], kWaveKeys[pump::clampv((int)value(pp(p, kWave)), 0, kNumWaves - 1)]);
        textAt(r.x + 8, r.y + 50, b, kLabel, 8.0f, g > 0.45f ? theme.neonInk : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        // the blend as a bar: analog left, spring right
        const Rect bar(r.x + 8, r.y + 62, r.w - 16, 4);
        fillRect(bar, withAlpha2(theme.ink, 0.15f));
        fillRect(Rect(bar.x, bar.y, bar.w * bl / 100.0f, bar.h), g > 0.45f ? theme.neonInk : theme.ink);
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
        return false;
    }
    void bodyHover(float x, float y) override
    {
        for (int p = 0; p < kPads; ++p) if (padR[p].contains(x, y)) { setStatus(std::string("Play and edit ") + kPadInfo[p].name + " (higher in the pad = harder)"); return; }
        if (springsR.contains(x, y)) setStatus("Every pad's spring, ringing with what it plays");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (mod & (kModifierControl | kModifierSuper | kModifierAlt)) return false;
        if (key >= '1' && key <= '8') { play((int)(key - '1'), 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Click a pad / keys 1-8", "play it and edit it (higher in the pad = harder)" }, { "MIDI", "General MIDI drum notes (35-51, 57)" },
                 { "BLEND", "0 = the analog voice, 100 = the spring" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "PAD %d %s   BLEND %.0f%%", sel + 1, kPadInfo[sel].label, value(pp(sel, kBlend)));
        return b;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginBoing* plug = nullptr;
    Rect springsR, padsR, padR[kPads], editR, kitR, scopeR, meterL, meterR;
    Segmented *tankSeg[kPads], *waveSeg[kPads], *tankModel;
    Dial* edit[kPads][kPerPad] = {};
    Dial *tankDecay, *tankLevel, *velD, *driveD, *volD;
    Button* chokeB;
    pump::coil::SpringsView springsView;
    int sel = 0;
    float glow[kPads] = {}, shakeAge = 10.0f, scope[kScopeN] = {}, scale = 1.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIBoing)
};

UI* createUI() { return new UIBoing(); }

END_NAMESPACE_DISTRHO
