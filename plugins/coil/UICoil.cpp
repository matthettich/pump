// pump COIL: interface. SPRINGS (every sounding voice as a coil), the KICK key (every ringing spring and the tank
// get a kick), SCOPE. Below: the spring (model, tension, drip, wobble, grit, tone), the voice (harmonic, spread,
// excite), the envelope, the tank and the playing controls; a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginCoil.hpp"
#include "CoilView.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::coil;

class UICoil : public PumpUI {
public:
    UICoil() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::coil(), "COIL", 1000, 800, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr, Dial::Size sz = Dial::Small) { Dial* d = addDial(p, l, sz, bi); if (tip) d->tip = tip; return d; };
        modelSeg = addSegmented(kModel, { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" });
        modelSeg->tip = "Which of SPRING's tanks: two- or three-spring, short, long, or the cheap digital ones";
        tensionD = dial(kTension, "TENSION", true, "Tight (+) or loose (-): the springs' transition frequency and dispersion");
        dripD = dial(kDrip, "DRIP", false, "More chirp in the springs and the transducer's peak");
        wobbleD = dial(kWobble, "WOBBLE", false, "The springs drift a little (a slow detune)");
        gritD = dial(kGrit, "GRIT", false, "Digital models: the converter's clock and word length");
        toneD = dial(kTone, "TONE", true, "The springs' damping: darker (-) or brighter (+)");
        harmD = dial(kHarmonic, "HARMONIC", false, "0: the springs' own clang and drip · 100: the partials line up into a clean pluck", Dial::Large);
        spreadD = dial(kSpread, "SPREAD", false, "0: every spring on the note · 100: the tank's own length ratios (an inharmonic cluster)");
        exciteD = dial(kExcite, "EXCITE", false, "The kick: more thump (0) or more crack (100)");
        decayD = dial(kDecay, "DECAY");
        relD = dial(kRelease, "RELEASE", false, "How fast a note dies once the key is up");
        velD = dial(kVelocity, "VELOCITY", false, "How much velocity sets the kick's strength");
        tankD = dial(kTank, "TANK", false, "SPRING's own tank of the same model, kicked by every note and fed the springs");
        widthD = dial(kWidth, "WIDTH");
        octD = dial(kOctave, "OCTAVE", true);
        bendD = dial(kBend, "BEND");
        volD = dial(kVolume, "VOLUME", true);

        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginCoil*>(pluginPtr())) plug = p;
    }
    ~UICoil() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kBend) { std::snprintf(b, sizeof b, "%.0f st", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 228, hs = Dial::heightFor(Dial::Small);
        springsR = Rect(14, top, 600, sh);
        kickR = Rect(626, top, 140, sh);
        scopeR = Rect(778, top, 182, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);

        const float y1 = 300, ph = 196;
        springPanel = Rect(14, y1, 520, ph);
        modelSeg->r = Rect(24, y1 + 28, 500, 22);
        Dial* sd[5] = { tensionD, dripD, wobbleD, gritD, toneD };
        for (int k = 0; k < 5; ++k) sd[k]->r = Rect(24 + k * 100, y1 + 74, 100, hs);
        voicePanel = Rect(546, y1, 440, ph);
        harmD->r = Rect(560, y1 + 34, 130, Dial::heightFor(Dial::Large));
        spreadD->r = Rect(700, y1 + 50, 90, hs); exciteD->r = Rect(800, y1 + 50, 90, hs);

        const float y2 = 508, ph2 = 192;
        envPanel = Rect(14, y2, 320, ph2);
        Dial* ed[3] = { decayD, relD, velD };
        for (int k = 0; k < 3; ++k) ed[k]->r = Rect(24 + k * 100, y2 + 64, 100, hs);
        tankPanel = Rect(346, y2, 320, ph2);
        tankD->r = Rect(366, y2 + 64, 120, hs); widthD->r = Rect(506, y2 + 64, 120, hs);
        playPanel = Rect(678, y2, 308, ph2);
        Dial* pd[3] = { octD, bendD, volD };
        for (int k = 0; k < 3; ++k) pd[k]->r = Rect(688 + k * 96, y2 + 64, 96, hs);
        keyboard.r = Rect(14, 712, 966, 56);
    }

    void tick(float dt) override
    {
        t += dt;
        gritD->enabled = value(kModel) >= 3.5f;
        if (kickHeld) kickPress = 1.0f; else kickPress = std::max(0.0f, kickPress - dt * 8.0f);
        kickAge += dt;
        if (!plug) return;
        springsView.update(&plug->engine, dt);
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
    }

    void drawBody() override
    {
        char right[48];
        const int sec = plug ? plug->engine.sectionsNow() : 0;
        if (value(kModel) >= 3.5f) std::snprintf(right, sizeof right, "%s · TUNED COMBS", kModelNames[pump::clampv((int)value(kModel), 0, 5)]);
        else std::snprintf(right, sizeof right, "%s · %d ALLPASSES", kModelNames[pump::clampv((int)value(kModel), 0, 5)], sec);
        std::string up = right; for (char& c : up) c = (char)std::toupper((unsigned char)c);
        springsView.draw(*this, springsR, kickAge, "SPRINGS", up.c_str());
        drawKick();
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(springPanel, "SPRING"); panel(voicePanel, "VOICE"); panel(envPanel, "ENVELOPE"); panel(tankPanel, "TANK"); panel(playPanel, "PLAY");
        const float h = value(kHarmonic) * 0.01f;
        textAt(voicePanel.x + 150, voicePanel.b() - 22, h < 0.2f ? "SPRING: CLANG AND DRIP" : h < 0.8f ? "IN BETWEEN" : "STRING: A CLEAN PLUCK", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        textAt(tankPanel.x + 12, tankPanel.b() - 16, "THE REAL SPRING TANK AROUND THE NOTES", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.5f);
        keyboard.draw(*this);
    }
    void drawKick()
    {
        const Rect& r = kickR;
        const float flash = std::max(kickPress, kickAge < 0.35f ? 1.0f - kickAge / 0.35f : 0.0f);
        const bool down = kickHeld || flash > 0.6f;
        Color fillC = pump::mixColor(theme.neon, theme.ink, flash);
        keyBox(r, false, down, &fillC, 6.0f);
        const Rect k = down ? Rect(r.x + 3, r.y + 3, r.w, r.h) : r;
        const Color txt = flash > 0.5f ? theme.on : theme.neonInk;
        textAt(k.cx(), k.cy() - 8.0f, "KICK", kBold, 30.0f, txt, ALIGN_CENTER | ALIGN_MIDDLE, 5.0f);
        textAt(k.cx(), k.cy() + 20.0f, "THE SPRINGS", kLabel, 10.0f, withAlpha2(txt, 0.8f), ALIGN_CENTER | ALIGN_MIDDLE, 2.0f);
        textAt(k.r() - 12, k.y + 14, "K", kMono, 10.0f, withAlpha2(txt, 0.7f), ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void kick(float strength)
    {
        if (!plug) return;
        plug->kickStrength.store(strength, std::memory_order_relaxed);
        plug->kickRequest.fetch_add(1, std::memory_order_release);
        kickAge = 0.0f;
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        const int win = 1024;
        int start = kScopeN - win - 1;
        for (int i = start; i > start - 1024 && i > 1; --i) if (scope[i - 1] < 0.0f && scope[i] >= 0.0f) { start = i; break; }
        float mx = 1e-3f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(scope[start + i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < win; i += 2)
        {
            const float x = p.x + p.w * i / (win - 1), y = p.cy() - scope[start + i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.6f); lineJoin(ROUND); stroke();
        restore();
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (kickR.contains(x, y)) { kickHeld = true; kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override { kickHeld = false; if (kbDown) { keyboard.mouseUp(); kbDown = false; } }
    void bodyHover(float x, float y) override
    {
        if (kickR.contains(x, y)) setStatus("KICK: every ringing spring (and the tank) gets a kick (Shift+click: a nudge; key K)");
        else if (springsR.contains(x, y)) setStatus("Every sounding voice as a coil: tighter for a higher note, shaking with its level");
        else if (keyboard.r.contains(x, y)) setStatus("Play: every note tunes the springs and kicks them; velocity = how hard");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (key == 'k' || key == 'K') { kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Keys / MIDI", "every note tunes the springs to it and kicks them" }, { "KICK / K", "kick the ringing springs and the tank (Shift: a nudge)" },
                 { "HARMONIC", "the springs' clang (0) to a clean pluck (100)" }, { "TANK", "SPRING's own tank around the notes" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", kModelNames[pump::clampv((int)value(kModel), 0, 5)], plug ? plug->engine.voicesNow() : 0);
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginCoil* plug = nullptr;
    Rect springsR, kickR, scopeR, meterL, meterR, springPanel, voicePanel, envPanel, tankPanel, playPanel;
    Segmented* modelSeg;
    Dial *tensionD, *dripD, *wobbleD, *gritD, *toneD, *harmD, *spreadD, *exciteD, *decayD, *relD, *velD, *tankD, *widthD, *octD, *bendD, *volD;
    KeyboardUI keyboard;
    pump::coil::SpringsView springsView;
    bool kbDown = false, kickHeld = false;
    float t = 0.0f, kickPress = 0.0f, kickAge = 10.0f, scope[kScopeN] = {}, scale = 1.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UICoil)
};

UI* createUI() { return new UICoil(); }

END_NAMESPACE_DISTRHO
