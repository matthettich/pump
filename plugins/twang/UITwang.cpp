// pump TWANG: interface. SPRINGS (the tuned springs, ringing with the input), the KICK key, SCOPE. Below: the tuning
// (a chord: root, chord, octave; or MIDI notes, held or latched), the spring (model, tension, drip, wobble, grit,
// tone), the voice (harmonic, spread, decay, release) and the output (drive, tank, width, mix, output); a keyboard for
// the MIDI tunings.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginTwang.hpp"
#include "../coil/CoilView.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::twang;

class UITwang : public PumpUI {
public:
    UITwang() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::twang(), "TWANG", 1000, 658, kBypass)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr, Dial::Size sz = Dial::Medium) { Dial* d = addDial(p, l, sz, bi); if (tip) d->tip = tip; return d; };
        tuneSeg = addSegmented(kTuning, { "CHORD", "MIDI", "LATCH" });
        tuneSeg->tip = "Chord: ROOT + CHORD tune the springs · MIDI: the notes you hold · Latch: the last notes played, until new ones";
        rootD = dial(kRoot, "ROOT");
        chordD = dial(kChord, "CHORD");
        octD = dial(kOctave, "OCTAVE", true);
        modelSeg = addSegmented(kModel, { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" });
        modelSeg->tip = "Which of SPRING's tanks: two- or three-spring, short, long, or the cheap digital ones";
        tensionD = dial(kTension, "TENSION", true);
        dripD = dial(kDrip, "DRIP");
        wobbleD = dial(kWobble, "WOBBLE");
        gritD = dial(kGrit, "GRIT");
        toneD = dial(kTone, "TONE", true);
        harmD = dial(kHarmonic, "HARMONIC", false, "0: the springs' own clang and drip · 100: clean, string-like resonances", Dial::Large);
        spreadD = dial(kSpread, "SPREAD", false, "0: every spring on its note · 100: the tank's own length ratios");
        decayD = dial(kDecay, "DECAY");
        relD = dial(kRelease, "RELEASE", false, "MIDI: how fast a note's springs die once its key is up");
        driveD = dial(kDrive, "DRIVE", false, "SPRING's driver: how hard the input hits the springs");
        tankD = dial(kTank, "TANK", false, "SPRING's own tank of the same model around the resonance");
        widthD = dial(kWidth, "WIDTH");
        mixD = dial(kMix, "MIX");
        outD = dial(kVolume, "OUTPUT", true);
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginTwang*>(pluginPtr())) plug = p;
    }
    ~UITwang() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        // top: springs, KICK, scope, meters
        const float top = 58, sh = 200, hm = Dial::heightFor(Dial::Medium), hl = Dial::heightFor(Dial::Large);
        springsR = Rect(14, top, 600, sh);
        kickR = Rect(626, top, 140, sh);
        scopeR = Rect(778, top, 182, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);
        // evenly spread n dials across [x, x + w) at y
        auto rowOf = [&](float x, float w, float y, Dial** d, int n) {
            const float cw = w / n;
            for (int k = 0; k < n; ++k) d[k]->r = Rect(x + k * cw, y, cw, hm);
        };
        // TUNING (mode + root/chord/octave) beside SPRING (model + five dials)
        const float y1 = top + sh + 12, ph = 150;
        tunePanel = Rect(14, y1, 420, ph);
        tuneSeg->r = Rect(26, y1 + 28, 396, 24);
        Dial* td[3] = { rootD, chordD, octD }; rowOf(26, 396, y1 + 62, td, 3);
        springPanel = Rect(446, y1, 540, ph);
        modelSeg->r = Rect(458, y1 + 28, 516, 24);
        Dial* sd[5] = { tensionD, dripD, wobbleD, gritD, toneD }; rowOf(458, 516, y1 + 62, sd, 5);
        // VOICE (HARMONIC + spread, decay, release) beside OUTPUT
        const float y2 = y1 + ph + 12, ph2 = 124;
        voicePanel = Rect(14, y2, 420, ph2);
        harmD->r = Rect(24, y2 + 24, 130, hl);
        Dial* vd[3] = { spreadD, decayD, relD }; rowOf(160, 262, y2 + 32, vd, 3);
        outPanel = Rect(446, y2, 540, ph2);
        Dial* od[5] = { driveD, tankD, widthD, mixD, outD }; rowOf(458, 516, y2 + 32, od, 5);
        keyboard.r = Rect(14, y2 + ph2 + 12, 966, 56);
    }

    void tick(float dt) override
    {
        t += dt;
        gritD->enabled = value(kModel) >= 3.5f;
        const bool chord = (int)value(kTuning) == pump::coil::kTuneChord;
        rootD->enabled = chordD->enabled = chord;
        relD->enabled = (int)value(kTuning) == pump::coil::kTuneMidi;
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
        const int tu = pump::clampv((int)value(kTuning), 0, 2);
        char right[64];
        if (tu == pump::coil::kTuneChord)
        {
            int notes[5]; const int n = pump::coil::Engine::chordNotes((int)value(kRoot), 0, (int)value(kChord), notes);
            std::string s = std::string(kRootNames[pump::clampv((int)value(kRoot), 0, 11)]) + " " + kChordNames[pump::clampv((int)value(kChord), 0, pump::coil::kNumChords - 1)];
            std::snprintf(right, sizeof right, "%s · %d NOTES", s.c_str(), n);
        }
        else std::snprintf(right, sizeof right, "%s", tu == pump::coil::kTuneMidi ? "TUNED BY THE NOTES HELD" : "TUNED BY THE LAST NOTES");
        std::string up = right; for (char& c : up) c = (char)std::toupper((unsigned char)c);
        springsView.draw(*this, springsR, kickAge, "SPRINGS", up.c_str());
        drawKick();
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(tunePanel, "TUNING"); panel(springPanel, "SPRING"); panel(voicePanel, "VOICE"); panel(outPanel, "OUTPUT");
        const char* hint = tu == pump::coil::kTuneChord ? "THE CHORD TUNES THE SPRINGS; THE INPUT RINGS THEM" : tu == pump::coil::kTuneMidi ? "PLAY OR SEND MIDI: THE NOTES HELD TUNE THE SPRINGS" : "PLAY OR SEND MIDI: THE LAST CHORD STAYS UNTIL A NEW ONE";
        textAt(tunePanel.r() - 12, tunePanel.y + 16, hint, kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.5f);
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
        if (kickR.contains(x, y)) setStatus("KICK: kick the tuned springs (and the tank): they ring on their own (Shift+click: a nudge; key K)");
        else if (springsR.contains(x, y)) setStatus("The tuned springs, ringing with the input");
        else if (keyboard.r.contains(x, y)) setStatus("MIDI and LATCH tunings: the notes you play here (or send) tune the springs");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (key == 'k' || key == 'K') { kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "CHORD", "ROOT + CHORD + OCTAVE tune the springs" }, { "MIDI / LATCH", "the notes you hold (or last played) tune them" },
                 { "KICK / K", "kick the tuned springs (Shift: a nudge)" }, { "HARMONIC", "the springs' clang (0) to clean strings (100)" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES", kTuningNames[pump::clampv((int)value(kTuning), 0, 2)], plug ? plug->engine.voicesNow() : 0);
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginTwang* plug = nullptr;
    Rect springsR, kickR, scopeR, meterL, meterR, tunePanel, springPanel, voicePanel, outPanel;
    Segmented *tuneSeg, *modelSeg;
    Dial *rootD, *chordD, *octD, *tensionD, *dripD, *wobbleD, *gritD, *toneD, *harmD, *spreadD, *decayD, *relD, *driveD, *tankD, *widthD, *mixD, *outD;
    KeyboardUI keyboard;
    pump::coil::SpringsView springsView;
    bool kbDown = false, kickHeld = false;
    float t = 0.0f, kickPress = 0.0f, kickAge = 10.0f, scope[kScopeN] = {}, scale = 1.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UITwang)
};

UI* createUI() { return new UITwang(); }

END_NAMESPACE_DISTRHO
