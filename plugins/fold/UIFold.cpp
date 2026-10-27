// pump FOLD: interface. FOLDER: the folding curve for the current FOLD, SYMMETRY, EDGE and STAGES (input across,
// output up) with the span the oscillator sweeps over it now; WAVE: the newest voice after the folder; GATE: how far the
// low-pass gate is open (the light) and the function generator. Below: the complex oscillator, the folder, the gate,
// the function generator, the random source, the voice, and a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginFold.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::fold;

class UIFold : public PumpUI {
public:
    UIFold() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::fold(), "FOLD", 1000, 756, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        modSeg = addSegmented(kModWaveP, { "SINE", "TRI", "SAW", "SQUARE" });
        modSeg->tip = "The modulation oscillator's wave";
        shapeD = dial(kShape, "SHAPE", false, "The principal oscillator: sine (0), triangle (50), saw (100)");
        ratioD = dial(kModRatio, "RATIO", false, "The modulator's frequency, as a ratio of the note");
        detD = dial(kModDetune, "DETUNE", true, "Hz added to the modulator: slow beating");
        fmD = dial(kFmIndex, "FM", false, "Linear, through-zero FM of the principal by the modulator");
        amD = dial(kAmDepth, "AM", false, "Amplitude modulation by the modulator");
        mfoldD = dial(kModFold, "MOD>FOLD", false, "The modulator moves the folding (timbre modulation)");
        foldD = dial(kFold, "FOLD", false, "How hard the wave is driven into the folder");
        symD = dial(kSymmetry, "SYMMETRY", true, "Biases the folds: brings in the even harmonics");
        edgeD = dial(kEdge, "EDGE", false, "Round folds (0) to sharp ones (100)");
        stagesD = dial(kStages, "STAGES", false, "How many times it can fold");
        gateSeg = addSegmented(kGateModeP, { "COMBO", "LOW-PASS", "VCA" });
        gateSeg->tip = "The gate opens a filter and an amplifier together (COMBO), or only one";
        offD = dial(kGateOpen, "OFFSET", false, "Holds the gate partly open while the key is down");
        respD = dial(kVactrol, "RESPONSE", false, "How slowly the gate lets go: a short strike rings longer");
        resoD = dial(kGateReso, "RESO");
        funcSeg = addSegmented(kFuncModeP, { "STRIKE", "SUSTAIN", "CYCLE" });
        funcSeg->tip = "Rise and fall once per note · rise, hold while the key is down, fall · rise and fall over and over while held";
        riseD = dial(kRise, "RISE"); fallD = dial(kFall, "FALL");
        fGateD = dial(kFuncGate, ">GATE"); fFoldD = dial(kFuncFold, ">FOLD", true); fFmD = dial(kFuncFm, ">FM", true); fPitchD = dial(kFuncPitch, ">PITCH", true);
        rRateD = dial(kRandRate, "RATE"); rSmoothD = dial(kRandSmooth, "SMOOTH", false, "Stepped (0) to gliding (100)");
        rFoldD = dial(kRandFold, ">FOLD"); rFmD = dial(kRandFm, ">FM"); rPitchD = dial(kRandPitch, ">PITCH");
        velD = dial(kVelocity, "VELOCITY", false, "How much the velocity strikes the gate"); spreadD = dial(kSpread, "SPREAD");
        voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); fineD = dial(kFine, "FINE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginFold*>(pluginPtr())) plug = p;
    }
    ~UIFold() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kFine) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kModRatio) { std::snprintf(b, sizeof b, v < 1.0f ? "x%.3f" : "x%.2f", v); return b; }
        if (i == kFuncPitch || i == kRandPitch) { std::snprintf(b, sizeof b, i == kFuncPitch ? "%+.1f st" : "%.1f st", v); return b; }
        if (i == kShape) return v < 3 ? "SINE" : (std::fabs(v - 50) < 3 ? "TRIANGLE" : (v > 97 ? "SAW" : PumpUI::formatParam(i, v)));
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        transR = Rect(14, top, 300, 220);
        waveR = Rect(326, top, 454, 220);
        gateR = Rect(792, top, 168, 220);
        meterL = Rect(966, top, 8, 208); meterR = Rect(977, top, 8, 208);
        oscPanel = Rect(14, 290, 626, 116);
        modSeg->r = Rect(24, 312, 606, 22);
        Dial* od[6] = { shapeD, ratioD, detD, fmD, amD, mfoldD };
        for (int k = 0; k < 6; ++k) od[k]->r = Rect(20 + k * 102, 336, 102, hs);
        foldPanel = Rect(652, 290, 334, 116);
        Dial* fd[4] = { foldD, symD, edgeD, stagesD };
        for (int k = 0; k < 4; ++k) fd[k]->r = Rect(658 + k * 81, 324, 81, hs);
        gatePanel = Rect(14, 416, 366, 116);
        gateSeg->r = Rect(24, 438, 346, 22);
        Dial* gd[3] = { offD, respD, resoD };
        for (int k = 0; k < 3; ++k) gd[k]->r = Rect(22 + k * 117, 462, 117, hs);
        funcPanel = Rect(392, 416, 594, 116);
        funcSeg->r = Rect(402, 438, 574, 22);
        Dial* nd[6] = { riseD, fallD, fGateD, fFoldD, fFmD, fPitchD };
        for (int k = 0; k < 6; ++k) nd[k]->r = Rect(400 + k * 96, 462, 96, hs);
        randPanel = Rect(14, 542, 470, 116);
        Dial* rd[5] = { rRateD, rSmoothD, rFoldD, rFmD, rPitchD };
        for (int k = 0; k < 5; ++k) rd[k]->r = Rect(22 + k * 91, 576, 91, hs);
        voicePanel = Rect(496, 542, 490, 116);
        vmodeSeg->r = Rect(506, 564, 470, 22);
        Dial* vd[7] = { velD, spreadD, voicesD, glideD, octD, fineD, volD };
        for (int k = 0; k < 7; ++k) vd[k]->r = Rect(500 + k * 69, 588, 69, hs);
        keyboard.r = Rect(14, 668, 966, 56);
    }

    void tick(float dt) override
    {
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        detD->enabled = ratioD->enabled = value(kFmIndex) > 0.05f || value(kAmDepth) > 0.05f || value(kModFold) > 0.05f;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        plug->engine.foldRing.readLatest(wave, kWave);
        gate = plug->engine.gateNow(); func = plug->engine.funcNow(); amt = plug->engine.foldNow();
        playing = plug->engine.voicesNow() > 0;
    }

    void drawTransfer()
    {
        const Rect& r = transR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "FOLDER");
        const Rect p(r.x + 14, r.y + 28, r.w - 28, r.h - 40);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f); line(p.cx(), p.y, p.cx(), p.b(), theme.grid, 1.0f);
        const float a = playing ? amt : value(kFold) * 0.01f;
        const double g = foldGain(a, (int)value(kStages)), sym = value(kSymmetry) * 0.01, edge = value(kEdge) * 0.01;
        const int st = (int)value(kStages);
        // input -1..1 across (the oscillator's span); output up
        beginPath();
        for (int i = 0; i <= 240; ++i)
        {
            const double x = -1.0 + 2.0 * i / 240.0;
            const double y = foldCell(x * g + sym, edge, st) / std::min(1.0, g);
            const float px = p.x + p.w * i / 240.0f, py = p.cy() - (float)y * p.h * 0.45f;
            if (!i) moveTo(px, py); else lineTo(px, py);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(2.0f); stroke();
        char b[48]; std::snprintf(b, sizeof b, "GAIN x%.1f", g);
        textAt(r.r() - 10, r.y + 14, b, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
    }
    void drawWave()
    {
        const Rect& r = waveR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "WAVE");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 36);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        if (!playing) { textAt(p.cx(), p.cy() - 14, "PLAY A NOTE", kLabel, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f); return; }
        int st = 0;
        for (int i = 1; i < kWave / 2; ++i) if (wave[i - 1] <= 0.0f && wave[i] > 0.0f) { st = i; break; }
        const int n = kWave / 2;
        beginPath();
        for (int i = 0; i < n; ++i)
        {
            const float x = p.x + p.w * i / (n - 1.0f), y = p.cy() - pump::clampv(wave[st + i], -1.2f, 1.2f) * p.h * 0.42f;
            if (!i) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.6f); stroke();
    }
    void drawGate()
    {
        const Rect& r = gateR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "GATE");
        const Rect p(r.x + 14, r.y + 30, r.w - 28, r.h - 54);
        // the light: a glow as bright as the gate is open
        const float g = playing ? pump::clampv(gate, 0.0f, 1.0f) : 0.0f;
        const float cx = p.x + p.w * 0.35f, cy = p.cy();
        beginPath(); circle(cx, cy, 26.0f); fillColor(withAlpha2(theme.neon, 0.12f + 0.88f * std::sqrt(g))); fill();
        strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        // the function's level
        const Rect bar(p.r() - 22, p.y, 14, p.h);
        strokeRect(bar, theme.dim, 1.0f);
        const float fh = (playing ? pump::clampv(func, 0.0f, 1.0f) : 0.0f) * (bar.h - 4);
        fillRect(Rect(bar.x + 2, bar.b() - 2 - fh, bar.w - 4, fh), theme.ink);
        textAt(cx, r.b() - 14, "LIGHT", kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        textAt(bar.cx(), r.b() - 14, "FUNC", kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
    }

    void drawBody() override
    {
        drawTransfer(); drawWave(); drawGate();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 13, c); };
        panel(oscPanel, "COMPLEX OSCILLATOR · MODULATOR"); panel(foldPanel, "FOLDER"); panel(gatePanel, "LOW-PASS GATE");
        panel(funcPanel, "FUNCTION"); panel(randPanel, "RANDOM"); panel(voicePanel, "VOICE");
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override { if (kbDown) { keyboard.mouseUp(); kbDown = false; } }
    void bodyHover(float x, float y) override
    {
        if (transR.contains(x, y)) setStatus("The folding curve now: the oscillator sweeps across it, the curve is what comes out");
        else if (waveR.contains(x, y)) setStatus("The newest voice after the folder");
        else if (gateR.contains(x, y)) setStatus("How open the low-pass gate is, and the function generator's level");
        else if (keyboard.r.contains(x, y)) setStatus("Play FOLD");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "FOLD / SYMMETRY", "how hard it folds; the bias that brings in even harmonics" }, { "STRIKE + RESPONSE", "a short strike and a slow gate: it rings" },
                 { "MOD>FOLD", "the modulator moves the timbre" }, { "CYCLE", "the function loops while held: rhythmic strikes" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s · %s   %d VOICES", kGateModeNames[pump::clampv((int)value(kGateModeP), 0, 2)], kFuncModeNames[pump::clampv((int)value(kFuncModeP), 0, 2)], plug ? plug->engine.voicesNow() : 0);
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kWave = 1024;
    PluginFold* plug = nullptr;
    Rect transR, waveR, gateR, meterL, meterR, oscPanel, foldPanel, gatePanel, funcPanel, randPanel, voicePanel;
    Segmented *modSeg, *gateSeg, *funcSeg, *vmodeSeg;
    Dial *shapeD, *ratioD, *detD, *fmD, *amD, *mfoldD, *foldD, *symD, *edgeD, *stagesD, *offD, *respD, *resoD;
    Dial *riseD, *fallD, *fGateD, *fFoldD, *fFmD, *fPitchD, *rRateD, *rSmoothD, *rFoldD, *rFmD, *rPitchD;
    Dial *velD, *spreadD, *voicesD, *glideD, *octD, *fineD, *volD;
    KeyboardUI keyboard;
    bool kbDown = false, playing = false;
    float wave[kWave] = {}, gate = 0.0f, func = 0.0f, amt = 0.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIFold)
};

UI* createUI() { return new UIFold(); }

END_NAMESPACE_DISTRHO
