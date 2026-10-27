// pump POLY: interface. SCOPE (the output), FILTER (the voicing's response where the latest voice has it; drag:
// left / right = cutoff, up / down = resonance), VOICES (a lamp per voice: lit while it sounds) and the voice
// controls. Below: oscillator A, B, the mixer and the filter; the envelopes, poly-mod and the LFO; a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginPoly.hpp"
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::poly;

class UIPoly : public PumpUI {
public:
    UIPoly() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::poly(), "POLY", 1000, 740, -1)
    {
        auto tog = [&](int p, const char* l, const char* tip) { Button* b = addToggle(p, l); b->small = true; b->tip = tip; return b; };
        aSaw = tog(kASaw, "SAW", "Oscillator A: saw (on together with pulse, they add)");
        aPulse = tog(kAPulse, "PULSE", "Oscillator A: pulse");
        syncBtn = tog(kSync, "SYNC A>B", "Hard sync: A restarts with B (sweep A's frequency, or poly-mod it)");
        aFreq = addDial(kAFreq, "FREQ", Dial::Small, true);
        aPw = addDial(kAPw, "PW", Dial::Small);
        bSaw = tog(kBSaw, "SAW", "Oscillator B: saw");
        bTri = tog(kBTri, "TRI", "Oscillator B: triangle");
        bPulse = tog(kBPulse, "PULSE", "Oscillator B: pulse");
        bLow = tog(kBLow, "LOW FREQ", "Low frequency: B runs at a few hertz, off the keyboard (a poly-mod source)");
        bFreq = addDial(kBFreq, "FREQ", Dial::Small, true);
        bFine = addDial(kBFine, "FINE", Dial::Small, true);
        bPw = addDial(kBPw, "PW", Dial::Small);
        lvA = addDial(kLevelA, "OSC A", Dial::Small);
        lvB = addDial(kLevelB, "OSC B", Dial::Small);
        noiseD = addDial(kNoise, "NOISE", Dial::Small);
        subD = addDial(kSub, "SUB", Dial::Small);
        subD->tip = "Sub: a square an octave under A";

        model = addSegmented(kFilterType, { "FIVE 24", "EIGHT 24", "EIGHT 12", "OB 12" });
        model->tip = "Five 24: keeps its bass, punchy · Eight 24: brighter, sings · Eight 12: open, buzzy · OB 12: soft, wide";
        cutD = addDial(kCutoff, "CUTOFF");
        resD = addDial(kRes, "RES", Dial::Small);
        envD = addDial(kFilterEnv, "ENV", Dial::Small, true);
        velfD = addDial(kVelFilter, "VEL", Dial::Small);
        keyD = addDial(kKeyTrack, "KEY", Dial::Small);
        hpfD = addDial(kHpf, "HPF", Dial::Small);
        hpfD->tip = "High-pass before the low-pass (thins the bottom out of pads)";

        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { fD[k] = addDial(kFA + k, adsr[k], Dial::Small); aD[k] = addDial(kAA + k, adsr[k], Dial::Small); }
        velaD = addDial(kVelAmp, "VEL", Dial::Small);
        pmEnv = addDial(kPmEnv, "F-ENV", Dial::Small);
        pmEnv->tip = "Poly-mod source: the filter envelope (per voice)";
        pmB = addDial(kPmOscB, "OSC B", Dial::Small);
        pmB->tip = "Poly-mod source: oscillator B, at audio rate (FM-like growl and bells)";
        pmFreq = tog(kPmFreqA, "FREQ A", "Poly-mod destination: oscillator A's frequency");
        pmPw = tog(kPmPwA, "PW A", "Poly-mod destination: oscillator A's pulse width");
        pmFilt = tog(kPmFilter, "FILTER", "Poly-mod destination: the cutoff");
        lfoShape = addSegmented(kLfoShape, { "TRI", "SAW", "SQR", "S&H" });
        lfoRate = addDial(kLfoRate, "RATE", Dial::Small);
        lfoDelay = addDial(kLfoDelay, "DELAY", Dial::Small);
        lfoDelay->tip = "Delay: the LFO fades in after each note starts";
        lfoPitch = addDial(kLfoPitch, "PITCH", Dial::Small);
        lfoPw = addDial(kLfoPw, "PW", Dial::Small);
        lfoFilter = addDial(kLfoFilter, "FILTER", Dial::Small);
        wheelD = addDial(kWheelMod, "WHEEL", Dial::Small);
        wheelD->tip = "The mod wheel adds this much LFO";

        modeSeg = addSegmented(kVoiceMode, { "POLY", "UNISON", "MONO" });
        chorusSeg = addSegmented(kChorus, { "OFF", "I", "II", "I+II" });
        chorusSeg->tip = "Ensemble chorus: I slow, II faster, I+II fast and shallow";
        voicesD = addDial(kVoices, "VOICES", Dial::Small);
        detD = addDial(kUnisonDetune, "DETUNE", Dial::Small);
        detD->tip = "Unison detune";
        glideD = addDial(kGlide, "GLIDE", Dial::Small);
        slopD = addDial(kSlop, "SLOP", Dial::Small);
        slopD->tip = "Slop: each voice slightly off in pitch and cutoff, like old analog voice cards";
        bendD = addDial(kBendRange, "BEND", Dial::Small);
        volD = addDial(kVolume, "VOLUME", Dial::Small, true);

        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginPoly*>(pluginPtr())) plug = p;
    }
    ~UIPoly() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kAFreq || i == kBFreq) { if (std::fabs(v) < 0.005f) return "0 st"; std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.01f ? "%+.0f st" : "%+.2f st", v); return b; }
        if (i == kBFine) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        if (i == kHpf && !hpfActive(v)) return "OFF";
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kLfoDelay && v < 0.5f) return "OFF";
        if (i == kVoices || i == kBendRange) { std::snprintf(b, sizeof b, "%.0f", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 166;
        scopeR = Rect(14, top, 250, sh);
        filterR = Rect(276, top, 250, sh);
        voicesR = Rect(538, top, 170, sh);
        voicePanel = Rect(720, top, 240, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);
        modeSeg->r = Rect(730, top + 26, 220, 22);
        chorusSeg->r = Rect(790, top + 54, 160, 22);
        Dial* vd[6] = { voicesD, detD, glideD, slopD, bendD, volD };
        for (int k = 0; k < 6; ++k) vd[k]->r = Rect(722 + k * 39.5f, top + 86, 40, Dial::heightFor(Dial::Small));

        const float y1 = 238, ph = 196;
        oscAPanel = Rect(14, y1, 214, ph);
        aSaw->r = Rect(26, y1 + 28, 92, 22); aPulse->r = Rect(124, y1 + 28, 92, 22);
        syncBtn->r = Rect(26, y1 + 58, 190, 22);
        aFreq->r = Rect(40, y1 + 112, 64, Dial::heightFor(Dial::Small)); aPw->r = Rect(130, y1 + 112, 64, Dial::heightFor(Dial::Small));
        oscBPanel = Rect(240, y1, 276, ph);
        bSaw->r = Rect(252, y1 + 28, 80, 22); bTri->r = Rect(336, y1 + 28, 80, 22); bPulse->r = Rect(420, y1 + 28, 84, 22);
        bLow->r = Rect(252, y1 + 58, 252, 22);
        bFreq->r = Rect(256, y1 + 112, 76, Dial::heightFor(Dial::Small)); bFine->r = Rect(340, y1 + 112, 76, Dial::heightFor(Dial::Small)); bPw->r = Rect(424, y1 + 112, 76, Dial::heightFor(Dial::Small));
        mixPanel = Rect(528, y1, 176, ph);
        lvA->r = Rect(544, y1 + 40, 64, Dial::heightFor(Dial::Small)); lvB->r = Rect(624, y1 + 40, 64, Dial::heightFor(Dial::Small));
        noiseD->r = Rect(544, y1 + 116, 64, Dial::heightFor(Dial::Small)); subD->r = Rect(624, y1 + 116, 64, Dial::heightFor(Dial::Small));
        filterPanel = Rect(716, y1, 270, ph);
        model->r = Rect(726, y1 + 28, 250, 22);
        cutD->r = Rect(724, y1 + 62, 76, Dial::heightFor(Dial::Medium));
        resD->r = Rect(806, y1 + 56, 58, Dial::heightFor(Dial::Small)); envD->r = Rect(864, y1 + 56, 58, Dial::heightFor(Dial::Small)); velfD->r = Rect(922, y1 + 56, 58, Dial::heightFor(Dial::Small));
        keyD->r = Rect(806, y1 + 124, 58, Dial::heightFor(Dial::Small)); hpfD->r = Rect(864, y1 + 124, 58, Dial::heightFor(Dial::Small));

        const float y2 = 446, ph2 = 192;
        fenvPanel = Rect(14, y2, 190, ph2);
        for (int k = 0; k < 4; ++k) fD[k]->r = Rect(20 + k * 45, y2 + 112, 45, Dial::heightFor(Dial::Small));
        aenvPanel = Rect(216, y2, 228, ph2);
        for (int k = 0; k < 4; ++k) aD[k]->r = Rect(222 + k * 43, y2 + 112, 43, Dial::heightFor(Dial::Small));
        velaD->r = Rect(394, y2 + 112, 46, Dial::heightFor(Dial::Small));
        pmPanel = Rect(456, y2, 200, ph2);
        pmEnv->r = Rect(470, y2 + 30, 64, Dial::heightFor(Dial::Small)); pmB->r = Rect(574, y2 + 30, 64, Dial::heightFor(Dial::Small));
        pmFreq->r = Rect(466, y2 + 120, 58, 22); pmPw->r = Rect(528, y2 + 120, 58, 22); pmFilt->r = Rect(590, y2 + 120, 58, 22);
        lfoPanel = Rect(668, y2, 318, ph2);
        lfoShape->r = Rect(680, y2 + 28, 294, 22);
        Dial* ld[6] = { lfoRate, lfoDelay, lfoPitch, lfoPw, lfoFilter, wheelD };
        for (int k = 0; k < 6; ++k) ld[k]->r = Rect(672 + k * 52, y2 + 112, 52, Dial::heightFor(Dial::Small));
        keyboard.r = Rect(14, 650, 966, 56);
    }

    void tick(float dt) override
    {
        const int mode = (int)value(kVoiceMode);
        voicesD->enabled = mode != kModeMono;
        detD->enabled = mode == kModeUnison;
        aPw->enabled = value(kAPulse) >= 0.5f;
        bPw->enabled = value(kBPulse) >= 0.5f;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        const float c = plug->engine.voicesNow() > 0 ? plug->engine.cutoffNow() : value(kCutoff);
        cutShow += (std::log2(std::max(15.0f, c)) - cutShow) * std::min(1.0f, dt * 20.0f);
        mask = plug->engine.voiceMask();
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
    }

    void drawBody() override
    {
        drawScope();
        drawFilter();
        drawVoices();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(voicePanel, "VOICE"); panel(oscAPanel, "OSC A"); panel(oscBPanel, "OSC B"); panel(mixPanel, "MIXER"); panel(filterPanel, "FILTER");
        panel(fenvPanel, "FILTER ENVELOPE"); panel(aenvPanel, "AMP ENVELOPE"); panel(pmPanel, "POLY-MOD"); panel(lfoPanel, "LFO");
        caption(730, chorusSeg->r.cy(), "CHORUS");
        caption(pmFreq->r.x, pmFreq->r.y - 10, "TO");
        drawEnv(Rect(fenvPanel.x + 12, fenvPanel.y + 28, fenvPanel.w - 24, 76), kFA, plug ? plug->engine.filterEnvNow() : 0.0f);
        drawEnv(Rect(aenvPanel.x + 12, aenvPanel.y + 28, aenvPanel.w - 24, 76), kAA, plug ? plug->engine.ampNow() : 0.0f);
        // LFO picture, with the delay drawn as a fade-in
        const Rect lp(lfoPanel.x + 12, lfoPanel.y + 58, lfoPanel.w - 24, 46);
        fillRect(lp, theme.screen); strokeRect(lp, theme.ink, 1.0f);
        beginPath();
        pump::fx::Rng rr(5); double held = rr.bi();
        const double dfrac = pump::clampv(value(kLfoDelay) / 2500.0, 0.0, 0.8);
        for (int i = 0; i <= 100; ++i)
        {
            const double t = i / 100.0, p = 3.0 * t, f = p - std::floor(p);
            if (i % 17 == 0) held = rr.bi();
            double v;
            switch ((int)value(kLfoShape)) { case kLfoSaw: v = 2 * f - 1; break; case kLfoSquare: v = f < 0.5 ? 1 : -1; break; case kLfoSh: v = held; break; default: v = 1 - 4 * std::fabs(f - 0.5); }
            v *= dfrac > 0 ? pump::clampv(t / dfrac, 0.0, 1.0) : 1.0;
            const float x = lp.x + 4 + (lp.w - 8) * (float)t, y = lp.cy() - (float)v * (lp.h * 0.4f);
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.6f); stroke();
        // poly-mod's signal flow, as words
        const bool any = value(kPmEnv) > 0.5f || value(kPmOscB) > 0.5f;
        textAt(pmPanel.cx(), pmPanel.y + 108, any ? "SOURCES -> DESTINATIONS" : "OFF (TURN UP A SOURCE)", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        keyboard.draw(*this);
    }

    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
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
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        restore();
    }
    void drawFilter()
    {
        const Rect& r = filterR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "FILTER");
        const int t = pump::clampv((int)value(kFilterType), 0, kNumFilters - 1);
        textAt(r.r() - 10, r.y + 14, kFilterNames[t], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        drawFreqGrid(p, 20.0, 20000.0, false);
        const double fc = std::exp2((double)cutShow), res = value(kRes) * 0.01, hp = value(kHpf);
        auto yOf = [&](double db) { return (float)(p.y + (24.0 - pump::clampv(db, -48.0, 24.0)) / 72.0 * p.h); };
        line(p.x, yOf(0), p.r(), yOf(0), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        for (int i = 0; i <= 120; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, i / 120.0);
            const std::complex<double> s(0.0, f / fc);
            std::complex<double> H;
            if (t == kOb12) { const double q = 0.5 + 4.5 * res * res; H = 1.0 / (s * s + s / q + 1.0); }
            else
            {
                const auto g = 1.0 / (1.0 + s);
                const double k = 5.2 * res * (t == kFive24 ? 0.97 : 1.0);
                const auto g4 = g * g * g * g;
                H = (t == kEight12 ? g * g : g4) * (1.0 + 0.5 * k) / (1.0 + k * g4) * (t == kFive24 ? 1.0 + 0.45 * res : 1.0);
            }
            if (hpfActive(hp)) { const std::complex<double> sh(0.0, f / hp); const auto h1 = sh / (1.0 + sh); H *= h1 * h1; }
            const float x = p.x + p.w * i / 120.0f, y = yOf(20.0 * std::log10(std::max(1e-9, std::abs(H))));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        const float cx = freqToX(p, pump::clampv(fc, 20.0, 20000.0));
        line(cx, p.y, cx, p.b(), withAlpha2(theme.neon, 0.9f), 1.5f);
        char b[32];
        if (fc >= 1000) std::snprintf(b, sizeof b, "%.2f kHz", fc / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", fc);
        textAt(r.x + 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawVoices()
    {
        const Rect& r = voicesR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VOICES");
        const int mode = (int)value(kVoiceMode);
        const int n = mode == kModeMono ? 1 : (int)value(kVoices);
        const float cw = (r.w - 20) / 4.0f, chh = (r.h - 40) / 4.0f;
        int lit = 0;
        for (int i = 0; i < 16; ++i)
        {
            const Rect c(r.x + 10 + (i % 4) * cw + 3, r.y + 28 + (i / 4) * chh + 3, cw - 6, chh - 6);
            const bool on = (mask >> i) & 1u, used = i < n;
            if (on) ++lit;
            fillRect(c, on ? theme.neon : (used ? theme.panel2 : theme.screen));
            strokeRect(c, used ? theme.ink : theme.dim, 1.0f);
            char b[4]; std::snprintf(b, sizeof b, "%d", i + 1);
            textAt(c.cx(), c.cy(), b, kMono, 9.0f, on ? theme.neonInk : (used ? theme.ink2 : theme.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        static const char* const mn[3] = { "POLY", "UNISON", "MONO" };
        textAt(r.r() - 10, r.y + 14, mn[pump::clampv(mode, 0, 2)], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        (void)lit;
    }
    void drawEnv(const Rect& r, int base, float now)
    {
        fillRect(r, theme.screen);
        strokeRect(r, theme.ink, 1.0f);
        auto tx = [](float ms) { return std::log10(1.0f + ms) / std::log10(1.0f + 10000.0f); };
        const float a = tx(value(base)), d = tx(value(base + 1)), s = value(base + 2) * 0.01f, rr = tx(value(base + 3));
        const float tot = a + d + 0.35f + rr;
        const Rect p = r.inset(4);
        auto X = [&](float t) { return p.x + p.w * t / tot; };
        auto Y = [&](float v) { return p.b() - v * p.h; };
        beginPath();
        moveTo(X(0), Y(0)); lineTo(X(a), Y(1));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d * t), Y(s + (1 - s) * std::pow(1e-4f, t))); }
        lineTo(X(a + d + 0.35f), Y(s));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d + 0.35f + rr * t), Y(s * std::pow(1e-4f, t))); }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        const float h = r.h * pump::clampv(now, 0.0f, 1.0f);
        fillRect(Rect(r.r() - 6, r.b() - h, 4, h), theme.neon);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (filterR.contains(x, y))
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kCutoff, spec(kCutoff).def); setParamGesture(kRes, spec(kRes).def); endGroup(); return false; }
            dragging = true; dx = x; dy = y; dCut = norm(kCutoff); dRes = norm(kRes);
            beginEdit(kCutoff); beginEdit(kRes);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (!dragging) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParamNorm(kCutoff, dCut + (x - dx) / filterR.w * f);
        setParamNorm(kRes, dRes + (dy - y) / filterR.h * f);
        setStatus(paramStatus(kCutoff) + "    " + paramStatus(kRes));
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; return; }
        if (dragging) { dragging = false; endEdit(kRes); endEdit(kCutoff); }
    }
    void bodyHover(float x, float y) override
    {
        if (filterR.contains(x, y)) setStatus("Drag: left / right = cutoff, up / down = resonance (double-click = reset)");
        else if (voicesR.contains(x, y)) setStatus("A lamp per voice: lit while it sounds");
        else if (keyboard.r.contains(x, y)) setStatus("Play: velocity follows where you strike the key; drag across keys");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Drag the filter screen", "left / right = cutoff, up / down = resonance" }, { "Keyboard", "click to play, drag across" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d VOICES SOUNDING", kFilterNames[pump::clampv((int)value(kFilterType), 0, kNumFilters - 1)], __builtin_popcount(mask));
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginPoly* plug = nullptr;
    Rect scopeR, filterR, voicesR, voicePanel, meterL, meterR, oscAPanel, oscBPanel, mixPanel, filterPanel, fenvPanel, aenvPanel, pmPanel, lfoPanel;
    Button *aSaw, *aPulse, *syncBtn, *bSaw, *bTri, *bPulse, *bLow, *pmFreq, *pmPw, *pmFilt;
    Dial *aFreq, *aPw, *bFreq, *bFine, *bPw, *lvA, *lvB, *noiseD, *subD, *cutD, *resD, *envD, *velfD, *keyD, *hpfD, *velaD, *pmEnv, *pmB;
    Dial *fD[4], *aD[4];
    Dial *lfoRate, *lfoDelay, *lfoPitch, *lfoPw, *lfoFilter, *wheelD, *voicesD, *detD, *glideD, *slopD, *bendD, *volD;
    Segmented *model, *lfoShape, *modeSeg, *chorusSeg;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false;
    float dx = 0, dy = 0, dCut = 0, dRes = 0, scope[kScopeN] = {}, scale = 1.0f, cutShow = 11.1f;
    uint32_t mask = 0;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIPoly)
};

UI* createUI() { return new UIPoly(); }

END_NAMESPACE_DISTRHO
