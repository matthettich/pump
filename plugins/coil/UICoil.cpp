// pump COIL: interface. SPRINGS (every sounding voice as a coil), beside them VOICE (harmonic, spread, excite), the
// KICK key (every ringing spring and the tank get a kick) over the SCOPE. Below, two pages: SOUND (the spring: model,
// tension, drip, wobble, grit, tone; the envelope and tank) or MOD (the selected modulation source's settings and the
// connections); then PLAY and MODULATION (the sources, each with a grip to drag onto any dial); a keyboard.
// A modulated dial wears a ring in its source's colour: how far it swings, and a dot where it is now.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginCoil.hpp"
#include "dsp/ModShape.hpp"
#include "CoilView.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::coil;

class UICoil : public PumpUI {
public:
    UICoil() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::coil(), "COIL", 1000, 676, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr, Dial::Size sz = Dial::Medium) { Dial* d = addDial(p, l, sz, bi); if (tip) d->tip = tip; return d; };
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
        voicesD = dial(kVoices, "VOICES", false, "How many notes can ring at once (each is a whole spring tank: more voices, more CPU)");

        // modulation: the pages, and each LFO's settings
        soundB = addButton("SOUND", [this] { setPage(0); }, [this] { return page == 0; });
        modB = addButton("MOD", [this] { setPage(1); }, [this] { return page == 1; });
        soundB->small = modB->small = true;
        soundB->tip = "The spring, envelope and tank"; modB->tip = "The selected source's settings, and every connection";
        for (int L = 0; L < pump::mod::kNumLfos; ++L)
        {
            lShape[L] = addSegmented(lfoParam(L, kLfoShape), { "SINE", "TRI", "SAW", "SQR", "S&H", "DRIFT" });
            lRate[L] = dial(lfoParam(L, kLfoRate), "RATE", false, "How fast (Hz)");
            lDiv[L] = addMenu(lfoParam(L, kLfoDiv), "");
            lDiv[L]->arrows = true;
            lDiv[L]->tip = "How fast, in notes of the song's tempo";
            lSync[L] = addToggle(lfoParam(L, kLfoSync), "SYNC");
            lSync[L]->tip = "Follow the song's tempo (and its position while it plays)";
            lRetrig[L] = addToggle(lfoParam(L, kLfoRetrig), "RETRIG");
            lRetrig[L]->tip = "Every new note starts the LFO from the beginning (a drawn shape: each note gets its own)";
            lDraw[L] = addToggle(lfoDrawParam(L), "DRAW");
            lDraw[L]->tip = "Play a shape you draw instead: points, bent lines, a loop";
        }
        for (int L = 0; L < pump::mod::kNumLfos; ++L) shapes[L] = pump::mod::DrawnShape::defaultShape(L);

        springsView.tank = true;
        const std::pair<int, Dial*> dl[] = { { kTension, tensionD }, { kDrip, dripD }, { kWobble, wobbleD }, { kGrit, gritD }, { kTone, toneD },
                                             { kHarmonic, harmD }, { kSpread, spreadD }, { kExcite, exciteD }, { kDecay, decayD }, { kRelease, relD },
                                             { kVelocity, velD }, { kTank, tankD }, { kWidth, widthD }, { kVolume, volD },
                                             { lfoParam(0, kLfoRate), lRate[0] }, { lfoParam(1, kLfoRate), lRate[1] },
                                             { lfoParam(2, kLfoRate), lRate[2] } };
        for (const auto& d : dl) targetDials.push_back(d);
        applyPage();
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
        const float top = 58, sh = 240, hm = Dial::heightFor(Dial::Medium), hl = Dial::heightFor(Dial::Large);
        // top: the springs; beside them VOICE (the dials that change the springs most); KICK over the scope; meters
        springsR = Rect(14, top, 600, sh);
        voicePanel = Rect(626, top, 170, sh);
        harmD->r = Rect(641, top + 24, 140, hl);
        spreadD->r = Rect(631, top + 126, 80, hm); exciteD->r = Rect(711, top + 126, 80, hm);
        kickR = Rect(808, top, 152, 84);
        scopeR = Rect(808, top + 96, 152, sh - 96);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);

        // SPRING (model + its five dials) beside ENVELOPE · TANK (the envelope drawn, then its five dials)
        const float y1 = top + sh + 12, ph = 150;
        springPanel = Rect(14, y1, 500, ph);
        modelSeg->r = Rect(26, y1 + 28, 476, 24);
        Dial* sd[5] = { tensionD, dripD, wobbleD, gritD, toneD };
        for (int k = 0; k < 5; ++k) sd[k]->r = Rect(26 + k * 95.2f, y1 + 62, 95.2f, hm);
        envPanel = Rect(526, y1, 460, ph);
        envR = Rect(538, y1 + 26, 436, 32);
        Dial* ed[5] = { decayD, relD, velD, tankD, widthD };
        for (int k = 0; k < 5; ++k) ed[k]->r = Rect(536 + k * 88.0f, y1 + 64, 88.0f, hm);

        // PLAY, and beside it the modulation sources
        const float y2 = y1 + ph + 12, ph2 = 110;
        playPanel = Rect(14, y2, 460, ph2);
        Dial* pd[4] = { voicesD, octD, bendD, volD };
        for (int k = 0; k < 4; ++k) pd[k]->r = Rect(24 + k * 110.0f, y2 + 24, 110.0f, hm);
        modPanel = Rect(486, y2, 500, ph2);
        keyboard.r = Rect(14, y2 + ph2 + 12, 966, 44);

        // MODULATION: the page keys, then the sources (two rows of tabs, each with its grip)
        soundB->r = Rect(modPanel.r() - 12 - 148, y2 + 6, 72, 20);
        modB->r = Rect(modPanel.r() - 12 - 72, y2 + 6, 72, 20);
        const float tw = (modPanel.w - 24 - 3 * 8) / 4.0f;
        for (int k = 0; k < pump::mod::kNumSources - 1; ++k)
        {
            const int s = pump::mod::kSourceOrder[k], row = k / 4, col = k % 4;
            tabR[s] = Rect(modPanel.x + 12 + col * (tw + 8), y2 + 34 + row * 38, tw, 28);
            gripR[s] = Rect(tabR[s].r() - 22, tabR[s].y, 22, tabR[s].h);
        }
        // the MOD page: the source's settings | the connections
        editPanel = Rect(14, y1, 500, ph);
        connPanel = Rect(526, y1, 460, ph);
        lfoScreenR = Rect(26, y1 + 28, 140, 110);
        for (int L = 0; L < pump::mod::kNumLfos; ++L)
        {
            lShape[L]->r = Rect(178, y1 + 28, 256, 22);
            lRate[L]->r = Rect(178, y1 + 58, 96, hm);
            lDiv[L]->r = Rect(184, y1 + 84, 100, 24);
            lSync[L]->r = Rect(296, y1 + 66, 96, 22);
            lRetrig[L]->r = Rect(406, y1 + 66, 96, 22);
        }
    }

    // ---- modulation: pages, sources, connections ----
    void setPage(int p) { page = p; applyPage(); saveUi(); }
    void selectSource(int s) { selSrc = pump::clampv(s, 1, pump::mod::kNumSources - 1); page = 1; applyPage(); saveUi(); }
    void saveUi() { char b[48]; std::snprintf(b, sizeof b, "page=%d;src=%d", page, selSrc); saveUiState(b); }
    void loadUiState(const std::string& st) override
    {
        page = pump::clampv(std::atoi(uiStateGet(st, "page", "0").c_str()), 0, 1);
        selSrc = pump::clampv(std::atoi(uiStateGet(st, "src", "1").c_str()), 1, pump::mod::kNumSources - 1);
        applyPage();
    }
    void applyPage()
    {
        const bool snd = page == 0;
        Control* sound[] = { modelSeg, tensionD, dripD, wobbleD, gritD, toneD, decayD, relD, velD, tankD, widthD };
        for (Control* c : sound) c->visible = snd;
        for (int L = 0; L < pump::mod::kNumLfos; ++L)
        {
            const bool on = !snd && pump::mod::lfoOf(selSrc) == L, sync = value(lfoParam(L, kLfoSync)) >= 0.5f;
            const bool dr = value(lfoDrawParam(L)) >= 0.5f;
            lSync[L]->visible = lRetrig[L]->visible = lDraw[L]->visible = on;
            lShape[L]->visible = on && !dr;
            lRate[L]->visible = on && !sync && !dr;
            lDiv[L]->visible = on && sync;
            // drawn: the shape takes the panel; its settings stack up on the right
            const float y1 = editPanel.y, cx = editPanel.r() - 116, cw = 104;
            if (dr)
            {
                lDraw[L]->r = Rect(cx, y1 + 26, cw, 20);
                lDiv[L]->r = Rect(cx, y1 + 50, cw, 22);
                lSync[L]->r = Rect(cx, y1 + 76, cw, 20);
                lRetrig[L]->r = Rect(cx, y1 + 100, cw, 20);
            }
            else
            {
                lDraw[L]->r = Rect(440, y1 + 28, 62, 22);
                lDiv[L]->r = Rect(184, y1 + 84, 100, 24);
                lSync[L]->r = Rect(296, y1 + 66, 96, 22);
                lRetrig[L]->r = Rect(406, y1 + 66, 96, 22);
            }
        }
    }
    // every source in a spring's colour (the springs' palette, in the order the tabs show them): the light shade
    // fills, the deep one draws lines
    int srcIndex(int s) const { for (int k = 0; k < pump::mod::kNumSources - 1; ++k) if (pump::mod::kSourceOrder[k] == s) return k; return 0; }
    Color srcColour(int s) const { return pump::coil::SpringsView::springColour(srcIndex(s) * 2 + 1, false); }
    Color srcInk(int s) const
    {
        const Color c = pump::coil::SpringsView::springColour(srcIndex(s) * 2 + 1, false);
        return theme.dark ? pump::mixColor(c, pump::hexColor(0xFFFFFF), 0.18f) : pump::coil::SpringsView::springColour(srcIndex(s) * 2 + 1, true);
    }
    int slotSrc(int k) const { return (int)value(slotParam(k, kSlotSrc)); }
    int slotDst(int k) const { return (int)value(slotParam(k, kSlotDst)); }
    float slotAmt(int k) const { return value(slotParam(k, kSlotAmt)); }
    int usedCount(int src) const { int n = 0; for (int k = 0; k < pump::mod::kSlots; ++k) if (slotSrc(k) == src) ++n; return n; }
    Dial* dialFor(int param) const { for (const auto& d : targetDials) if (d.first == param) return d.second; return nullptr; }
    int targetParam(int t) const { return paramOfTarget(t); }
    // a connection from src to the target of `param`: the one already there (selected), or the first free slot
    void connect(int src, int param, float amt = 25.0f)
    {
        const int t = targetOfParam(param);
        if (t <= 0) return;
        for (int k = 0; k < pump::mod::kSlots; ++k)
            if (slotSrc(k) == src && slotDst(k) == t) { setStatus(std::string(pump::mod::kSourceKeys[src]) + " already moves " + kTargetNames[t] + ": change its amount under MOD"); return; }
        for (int k = 0; k < pump::mod::kSlots; ++k)
            if (slotSrc(k) == pump::mod::kSrcNone)
            {
                beginGroup();
                setParam(slotParam(k, kSlotSrc), (float)src); setParam(slotParam(k, kSlotDst), (float)t); setParam(slotParam(k, kSlotAmt), amt);
                endGroup();
                std::string up = kTargetNames[t]; for (char& c : up) c = (char)std::toupper((unsigned char)c);
                setStatus(std::string(pump::mod::kSourceKeys[src]) + " > " + up + " +" + std::to_string((int)amt) + " %  (change it under MOD)");
                return;
            }
        setStatus("All 32 connections are in use: remove one under MOD");
    }
    void removeSlot(int k)
    {
        beginGroup();
        setParam(slotParam(k, kSlotSrc), 0.0f); setParam(slotParam(k, kSlotDst), 0.0f); setParam(slotParam(k, kSlotAmt), 0.0f);
        endGroup();
    }
    // the connections in use, in slot order
    std::vector<int> usedSlots() const { std::vector<int> v; for (int k = 0; k < pump::mod::kSlots; ++k) if (slotSrc(k) != pump::mod::kSrcNone) v.push_back(k); return v; }


    void tick(float dt) override
    {
        t += dt;
        gritD->enabled = value(kModel) >= 3.5f;
        applyPage();
        if (shapesDirty && shapeDrag == kNoShapeDrag) sendShapes();
        for (int L = 0; L < pump::mod::kNumLfos; ++L)
        {
            const double hz = value(lfoParam(L, kLfoSync)) >= 0.5f ? 1.0 / pump::noteSeconds((int)value(lfoParam(L, kLfoDiv)), 120.0) : value(lfoParam(L, kLfoRate));
            if (value(lfoDrawParam(L)) >= 0.5f) { previewPhase[L] = shapes[L].wrap(previewPhase[L] + hz * dt); if (!shapes[L].loop && previewPhase[L] >= 1.0) previewPhase[L] = 0.0; }
            else previewPhase[L] = std::fmod(previewPhase[L] + hz * dt, 1.0);
        }
        if (kickHeld) kickPress = 1.0f; else kickPress = std::max(0.0f, kickPress - dt * 8.0f);
        kickAge += dt;
        if (!plug) return;
        springsView.setModel((int)value(kModel), value(kGrit));
        springsView.update(&plug->engine, dt);
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
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
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(voicePanel, "VOICE"); panel(playPanel, "PLAY");
        if (page == 0) { panel(springPanel, "SPRING"); panel(envPanel, "ENVELOPE  ·  TANK"); }
        const float h = value(kHarmonic) * 0.01f;
        textAt(voicePanel.cx(), voicePanel.b() - 12, h < 0.2f ? "CLANG AND DRIP" : h < 0.8f ? "IN BETWEEN" : "A CLEAN PLUCK", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        if (page == 0) drawEnvelope();
        else { drawEditor(); drawConnections(); }
        drawSources();
        keyboard.draw(*this);
    }
    void drawSources()
    {
        strokeRect(modPanel, theme.dim, 1.0f);
        caption(modPanel.x + 12, modPanel.y + 16, "MODULATION");
        for (int s = 1; s < pump::mod::kNumSources; ++s)
        {
            const Rect& r = tabR[s];
            const bool on = page == 1 && s == selSrc, used = usedCount(s) > 0;
            keyBox(r, on, false, nullptr, 3.0f);
            const Color tc = on ? theme.on : theme.ink;
            const Rect sw(r.x + 8, r.cy() - 5, 10, 10);
            fillRect(sw, srcColour(s)); strokeRect(sw, on ? tc : srcInk(s), 1.0f);
            textAt(r.x + 24, r.cy(), pump::mod::kSourceKeys[s], kLabel, 9.5f, tc, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
            if (used) { char b[8]; std::snprintf(b, sizeof b, "%d", usedCount(s)); textAt(gripR[s].x - 4, r.cy(), b, kMono, 9.5f, on ? theme.on : theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE); }
            // the grip: drag it onto a dial
            const Color gc = on ? theme.on : theme.ink2;
            for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j) { beginPath(); circle(gripR[s].x + 8 + i * 5.0f, r.cy() - 5 + j * 5.0f, 1.5f); fillColor(gc); fill(); }
        }
    }
    void drawEditor()
    {
        const Rect& r = editPanel;
        strokeRect(r, theme.dim, 1.0f);
        const int n = usedCount(selSrc);
        char b[64];
        caption(r.x + 12, r.y + 16, pump::mod::kSourceKeys[selSrc]);
        std::snprintf(b, sizeof b, n == 1 ? "IN 1 CONNECTION" : "IN %d CONNECTIONS", n);
        textAt(r.r() - 12, r.y + 16, b, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        if (pump::mod::lfoOf(selSrc) >= 0 && value(lfoDrawParam(pump::mod::lfoOf(selSrc))) >= 0.5f) { drawShapeEditor(); return; }
        if (pump::mod::lfoOf(selSrc) >= 0)
        {
            const int L = pump::mod::lfoOf(selSrc), shape = (int)value(lfoParam(L, kLfoShape));
            const Rect& w = lfoScreenR;
            screenBox(w);
            const Rect p(w.x + 6, w.y + 8, w.w - 12, w.h - 16);
            line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.2f), 1.0f);
            pump::mod::Lfo shapeOf; shapeOf.seed(0xC0FFEEu + L);
            for (int k = 0; k < 4; ++k) shapeOf.advance(1.0);
            beginPath();
            for (int i = 0; i <= 120; ++i) { const float u = i / 120.0f, x = p.x + p.w * u, y = p.cy() - (float)shapeOf.valueAt(shape, u) * p.h * 0.42f; if (i == 0) moveTo(x, y); else lineTo(x, y); }
            const Color c = srcColour(selSrc);
            strokeColor(c); strokeWidth(1.8f); lineJoin(shape == pump::mod::kSquare || shape == pump::mod::kSampleHold ? MITER : ROUND); stroke();
            const bool live = plug && usedCount(selSrc) > 0;
            const float ph = live ? plug->lfoPhaseNow[L].load() : (float)previewPhase[L];
            const float px = p.x + p.w * ph, py = p.cy() - (float)shapeOf.valueAt(shape, ph) * p.h * 0.42f;
            line(px, p.y, px, p.b(), withAlpha2(theme.ink, 0.25f), 1.0f);
            beginPath(); circle(px, py, 4.0f); fillColor(c); fill(); strokeColor(theme.ink); strokeWidth(1.2f); stroke();
            const bool sync = value(lfoParam(L, kLfoSync)) >= 0.5f;
            if (sync) textAt(lDiv[L]->r.cx(), lDiv[L]->r.b() + 14, "OF THE SONG", kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
            textAt(296, r.y + 108, value(lfoParam(L, kLfoRetrig)) >= 0.5f ? "RESTARTS WITH EVERY NOTE" : "RUNS ON ITS OWN", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            textAt(296, r.y + 126, sync ? "LOCKED TO THE SONG WHILE IT PLAYS" : "FREE: SET IN HZ", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        }
        else
        {
            textAt(r.x + 12, r.y + 46, pump::mod::kSourceAbout[selSrc], kLabel, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
            // its value now: a bar (bipolar ones from the middle)
            const Rect bar(r.x + 12, r.y + 70, r.w - 24, 14);
            fillRect(bar, theme.panel2);
            const float v = plug ? plug->sourceNow[selSrc].load() : 0.0f;
            if (pump::mod::unipolar(selSrc)) fillRect(Rect(bar.x, bar.y, bar.w * pump::clampv(v, 0.0f, 1.0f), bar.h), srcColour(selSrc));
            else { const float m = bar.cx(), e = m + bar.w * 0.5f * pump::clampv(v, -1.0f, 1.0f); fillRect(Rect(std::min(m, e), bar.y, std::fabs(e - m), bar.h), srcColour(selSrc)); line(m, bar.y - 2, m, bar.b() + 2, theme.ink, 1.0f); }
            strokeRect(bar, theme.ink, 1.0f);
            textAt(r.x + 12, r.y + 104, plug && usedCount(selSrc) ? "NOW (THE NEWEST NOTE'S, FOR PER-NOTE SOURCES)" : "SHOWS ITS VALUE ONCE IT IS CONNECTED", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            textAt(r.x + 12, r.y + 126, "DRAG ITS GRIP (BELOW) ONTO ANY DIAL TO CONNECT IT", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        }
    }
    // ---- drawn shapes ----
    int drawnLfo() const { const int L = pump::mod::lfoOf(selSrc); return page == 1 && L >= 0 && value(lfoDrawParam(L)) >= 0.5f ? L : -1; }
    float shapeX(float u) const { return shapePlot.x + shapePlot.w * u; }
    float shapeYv(float v) const { return shapePlot.cy() - v * shapePlot.h * 0.5f; }
    float shapeU(float x) const { return pump::clampv((x - shapePlot.x) / std::max(1.0f, shapePlot.w), 0.0f, 1.0f); }
    float shapeV(float y) const { return pump::clampv((shapePlot.cy() - y) / std::max(1.0f, shapePlot.h * 0.5f), -1.0f, 1.0f); }
    void sendShapes() { setState("shapes", pump::mod::shapesToText(shapes, pump::mod::kNumLfos).c_str()); shapesDirty = false; lastShapeSend = t; }
    // a change while dragging: sent at most every 50 ms (and when the drag ends)
    void shapesChanged() { shapesDirty = true; if (t - lastShapeSend > 0.05) sendShapes(); repaint(); }
    std::string serializeExtra() const override { return pump::mod::shapesToText(shapes, pump::mod::kNumLfos); }
    void deserializeExtra(const std::string& s) override { pump::mod::shapesFromText(s.c_str(), shapes, pump::mod::kNumLfos); sendShapes(); }
    void onExtraState(const char* key, const char* v) override
    {
        if (std::strcmp(key, "shapes") == 0) pump::mod::shapesFromText(v, shapes, pump::mod::kNumLfos);
    }
    // what is under the mouse: a point (0..n-1), a bend handle (100 + the line's end point), a loop end (200 / 201)
    int shapeHit(int L, float x, float y) const
    {
        const pump::mod::DrawnShape& s = shapes[L];
        for (int k = 0; k < s.n; ++k) if (std::fabs(x - shapeX(s.u[k])) <= 7 && std::fabs(y - shapeYv(s.v[k])) <= 7) return k;
        for (int k = 1; k < s.n; ++k)
        {
            const float um = 0.5f * (s.u[k - 1] + s.u[k]);
            if (shapeX(s.u[k]) - shapeX(s.u[k - 1]) < 14) continue;
            if (std::fabs(x - shapeX(um)) <= 6 && std::fabs(y - shapeYv((float)s.at(um))) <= 6) return 100 + k;
        }
        if (s.loop && y >= shapePan.y && y <= shapePan.b())
        {
            const bool top = y < shapePan.y + 12;
            if (std::fabs(x - shapeX(s.u[s.loopA])) <= (top ? 6 : 3)) return 200;
            if (std::fabs(x - shapeX(s.u[s.loopB])) <= (top ? 6 : 3)) return 201;
        }
        return -1;
    }
    bool shapeMouseDown(float x, float y, bool dbl)
    {
        const int L = drawnLfo();
        if (L < 0) return false;
        pump::mod::DrawnShape& s = shapes[L];
        if (loopKeyR.contains(x, y)) { beginGroup(); s.loop = !s.loop; endGroup(); sendShapes(); return true; }
        if (rateBoxR.w > 0 && rateBoxR.contains(x, y))
        {
            const int p = lfoParam(L, kLfoRate);
            if (dbl) { setParamGesture(p, params()[(size_t)p].def); return true; }
            shapeDrag = kDragRate; shapeY0 = y; rateN0 = params()[(size_t)p].toNorm(value(p)); beginEdit(p);
            return true;
        }
        if (!shapePan.contains(x, y)) return false;
        const int h = shapeHit(L, x, y);
        beginGroup();
        if (h >= 0 && h < 100)
        {
            if (dbl && h > 0 && h < s.n - 1 && s.n > 2)
            {
                for (int k = h; k < s.n - 1; ++k) { s.u[k] = s.u[k + 1]; s.v[k] = s.v[k + 1]; s.c[k] = s.c[k + 1]; }
                --s.n;
                if (h < s.loopA) --s.loopA;
                if (h <= s.loopB) --s.loopB;
                if (s.loopA >= s.loopB) s.loopA = s.loopB - 1;
                s.fix(); endGroup(); sendShapes();
                setStatus("Point removed");
                return true;
            }
            shapeDrag = kDragPoint; shapeK = h;
            return true;
        }
        if (h >= 100 && h < 200) { shapeDrag = kDragBend; shapeK = h - 100; shapeY0 = y; shapeC0 = s.c[shapeK]; return true; }
        if (h == 200 || h == 201) { shapeDrag = h == 200 ? kDragLoopA : kDragLoopB; return true; }
        // a new point where the click is
        const float u = shapeU(x);
        if (s.n >= pump::mod::DrawnShape::kMaxPoints) { endGroup(); setStatus("32 points is the most a shape can have"); return true; }
        if (u <= 0.0f || u >= 1.0f) { endGroup(); return true; }
        int at = 1; while (at < s.n - 1 && s.u[at] < u) ++at;
        for (int k = s.n; k > at; --k) { s.u[k] = s.u[k - 1]; s.v[k] = s.v[k - 1]; s.c[k] = s.c[k - 1]; }
        s.u[at] = u; s.v[at] = shapeV(y); s.c[at] = 0.0f; ++s.n;
        if (at <= s.loopA) ++s.loopA;
        if (at <= s.loopB) ++s.loopB;
        s.fix(); shapesChanged();
        shapeDrag = kDragPoint; shapeK = at;
        return true;
    }
    void shapeMouseDrag(float x, float y, uint mod)
    {
        const int L = pump::mod::lfoOf(selSrc);
        if (L < 0) return;
        pump::mod::DrawnShape& s = shapes[L];
        const float fine = (mod & kModifierShift) ? 0.2f : 1.0f;
        switch (shapeDrag)
        {
        case kDragPoint:
        {
            const int k = shapeK;
            if (k < 0 || k >= s.n) return;
            if (k > 0 && k < s.n - 1) s.u[k] = pump::clampv(shapeU(x), s.u[k - 1] + 0.002f, s.u[k + 1] - 0.002f);
            s.v[k] = shapeV(y);
            if (mod & kModifierShift) s.v[k] = std::round(s.v[k] * 8.0f) / 8.0f;   // Shift: eighths
            break;
        }
        case kDragBend:
        {
            const int k = shapeK;
            if (k < 1 || k >= s.n) return;
            const float dir = s.v[k] >= s.v[k - 1] ? 1.0f : -1.0f;   // dragging up bows the line upward either way
            s.c[k] = pump::clampv(shapeC0 + dir * (y - shapeY0) / 60.0f * fine, -1.0f, 1.0f);
            break;
        }
        case kDragLoopA: case kDragLoopB:
        {
            // the loop's ends sit on points: the nearest one, keeping start before end
            const float u = shapeU(x);
            int best = 0; for (int k = 1; k < s.n; ++k) if (std::fabs(s.u[k] - u) < std::fabs(s.u[best] - u)) best = k;
            if (shapeDrag == kDragLoopA) s.loopA = pump::clampv(best, 0, s.loopB - 1);
            else s.loopB = pump::clampv(best, s.loopA + 1, s.n - 1);
            break;
        }
        case kDragRate:
        {
            const int p = lfoParam(L, kLfoRate);
            setParam(p, params()[(size_t)p].fromNorm(pump::clampv(rateN0 + (shapeY0 - y) / 200.0f * fine, 0.0f, 1.0f)));
            return;
        }
        default: return;
        }
        shapesChanged();
    }
    void shapeMouseUp()
    {
        const int L = pump::mod::lfoOf(selSrc);
        if (shapeDrag == kDragRate) { if (L >= 0) endEdit(lfoParam(L, kLfoRate)); }
        else { endGroup(); sendShapes(); }
        shapeDrag = kNoShapeDrag; shapeK = -1;
    }
    // the drawn shape: the line in its source's colour over a soft glow (brighter inside the loop), its points and
    // bend handles, the loop's two ends, and where it is now
    void drawShapeEditor()
    {
        const int L = pump::mod::lfoOf(selSrc);
        const pump::mod::DrawnShape& s = shapes[L];
        const Rect& r = editPanel;
        const Color neon = srcColour(selSrc), ink = srcInk(selSrc);
        screenBox(r);
        caption(r.x + 10, r.y + 14, std::string(pump::mod::kSourceKeys[selSrc]) + "  ·  DRAWN");
        const int nc = usedCount(selSrc);
        char b[96];
        std::snprintf(b, sizeof b, nc == 1 ? "IN 1 CONNECTION" : "IN %d CONNECTIONS", nc);
        textAt(r.r() - 10, r.y + 14, b, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        shapePan = Rect(r.x + 12, r.y + 26, r.w - 140, r.h - 50);
        shapePlot = Rect(shapePan.x + 10, shapePan.y + 9, shapePan.w - 20, shapePan.h - 18);
        const Rect& pan = shapePan;
        strokeRect(pan, theme.dim, 1.0f);
        line(shapePlot.x, shapePlot.cy(), shapePlot.r(), shapePlot.cy(), withAlpha2(theme.ink, 0.15f), 1.0f);
        if (s.loop)
        {
            const float lx0 = shapeX(s.u[s.loopA]), lx1 = shapeX(s.u[s.loopB]);
            fillRect(Rect(lx0, pan.y + 1, lx1 - lx0, pan.h - 2), withAlpha2(neon, theme.dark ? 0.07f : 0.12f));
            for (int e = 0; e < 2; ++e)
            {
                const float lx = e ? lx1 : lx0;
                const bool hot = shapeDrag == (e ? kDragLoopB : kDragLoopA) || shapeHover == 200 + e;
                line(lx, pan.y + 1, lx, pan.b() - 1, ink, hot ? 2.0f : 1.2f);
                fillRect(Rect(lx - 3, pan.y + 1, 6, 5), ink);
            }
            textAt((lx0 + lx1) * 0.5f, pan.y + 7, "LOOP", kLabel, 8.0f, ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        // the glow, then the line
        auto path = [&](float u0, float u1) {
            beginPath();
            const int steps = std::max(2, (int)((u1 - u0) * 300.0f));
            for (int i = 0; i <= steps; ++i) { const float u = u0 + (u1 - u0) * i / steps; if (i == 0) moveTo(shapeX(u), shapeYv((float)s.at(u))); else lineTo(shapeX(u), shapeYv((float)s.at(u))); }
        };
        const float la = s.loop ? s.u[s.loopA] : 0.0f, lb = s.loop ? s.u[s.loopB] : 1.0f;
        const float glowIn = theme.dark ? 0.42f : 0.62f, glowOut = theme.dark ? 0.16f : 0.26f;
        if (la > 0.0f) { path(0.0f, la); strokeColor(withAlpha2(neon, glowOut)); strokeWidth(9.0f); lineCap(ROUND); lineJoin(ROUND); stroke(); }
        path(la, lb); strokeColor(withAlpha2(neon, s.loop ? glowIn : glowOut)); strokeWidth(9.0f); lineCap(ROUND); lineJoin(ROUND); stroke();
        if (lb < 1.0f) { path(lb, 1.0f); strokeColor(withAlpha2(neon, glowOut)); strokeWidth(9.0f); stroke(); }
        path(0.0f, 1.0f); strokeColor(ink); strokeWidth(2.0f); lineCap(ROUND); lineJoin(ROUND); stroke();
        for (int k = 1; k < s.n; ++k)
        {
            if (shapeX(s.u[k]) - shapeX(s.u[k - 1]) < 14) continue;
            const float um = 0.5f * (s.u[k - 1] + s.u[k]);
            const bool hot = (shapeDrag == kDragBend && shapeK == k) || shapeHover == 100 + k;
            beginPath(); circle(shapeX(um), shapeYv((float)s.at(um)), hot ? 4.2f : 3.2f); fillColor(hot ? neon : theme.bg); fill(); strokeColor(ink); strokeWidth(1.2f); stroke();
        }
        for (int k = 0; k < s.n; ++k)
        {
            const bool hot = (shapeDrag == kDragPoint && shapeK == k) || shapeHover == k;
            const Rect q(shapeX(s.u[k]) - 4.5f, shapeYv(s.v[k]) - 4.5f, 9, 9);
            fillRect(q, hot ? neon : theme.bg); strokeRect(q, theme.ink, 1.4f);
        }
        // where it is now (the newest note's, when it runs per note)
        const bool live = plug && nc > 0;
        const float ph = live ? plug->lfoPhaseNow[L].load() : (float)previewPhase[L];
        const float px = shapeX(ph), py = shapeYv((float)s.at(ph));
        line(px, pan.y + 1, px, pan.b() - 1, withAlpha2(theme.ink, 0.3f), 1.0f);
        beginPath(); circle(px, py, 4.5f); fillColor(neon); fill(); strokeColor(theme.ink); strokeWidth(1.3f); stroke();
        // under the pan
        fillRect(Rect(r.x + 12, r.b() - 17, 8, 8), neon); strokeRect(Rect(r.x + 12, r.b() - 17, 8, 8), ink, 1.0f);
        if (s.loop) std::snprintf(b, sizeof b, "%d POINTS  ·  LOOPS %d TO %d  ·  CLICK: ADD  ·  DOUBLE-CLICK: REMOVE  ·  DRAG A DOT: BEND", s.n, s.loopA + 1, s.loopB + 1);
        else std::snprintf(b, sizeof b, "%d POINTS  ·  PLAYS ONCE  ·  CLICK: ADD  ·  DOUBLE-CLICK: REMOVE  ·  DRAG A DOT: BEND", s.n);
        textAt(r.x + 26, r.b() - 13, b, kLabel, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        // the settings column: DRAW, the rate, SYNC, RETRIG (widgets), LOOP
        const float cx = r.r() - 116, cw = 104;
        const bool sync = value(lfoParam(L, kLfoSync)) >= 0.5f;
        rateBoxR = sync ? Rect() : Rect(cx, r.y + 50, cw, 22);
        if (!sync)
        {
            keyBox(rateBoxR, false, shapeDrag == kDragRate, nullptr, 3.0f);
            const float hz = value(lfoParam(L, kLfoRate));
            if (hz >= 1.0f) std::snprintf(b, sizeof b, "%.2f HZ", hz); else std::snprintf(b, sizeof b, "%.1f S / CYCLE", 1.0f / std::max(0.01f, hz));
            textAt(rateBoxR.cx(), rateBoxR.cy(), b, kMono, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        loopKeyR = Rect(cx, r.y + 124, cw, 20);
        keyBox(loopKeyR, s.loop, false, nullptr, 3.0f);
        textAt(loopKeyR.cx(), loopKeyR.cy(), "LOOP", kLabel, 9.0f, s.loop ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }
    void drawConnections()
    {
        const Rect& r = connPanel;
        strokeRect(r, theme.dim, 1.0f);
        caption(r.x + 12, r.y + 16, "CONNECTIONS");
        const std::vector<int> used = usedSlots();
        char b[64]; std::snprintf(b, sizeof b, "%d OF %d", (int)used.size(), pump::mod::kSlots);
        textAt(r.r() - 12, r.y + 16, b, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const int maxScroll = std::max(0, (int)used.size() + 1 - kRows);
        listScroll = pump::clampv(listScroll, 0, maxScroll);
        nRows = 0; addR = Rect();
        const float x0 = r.x + 12, rh = 19.0f, y0 = r.y + 26;
        for (int row = 0; row < kRows; ++row)
        {
            const int i = row + listScroll;
            const float y = y0 + row * rh;
            if (i == (int)used.size())
            {
                addR = Rect(x0, y + 1, r.w - 24, rh - 4);
                strokeRect(addR, theme.dim, 1.0f);
                std::string t = std::string("+ ADD A CONNECTION FROM ") + pump::mod::kSourceKeys[selSrc];
                textAt(addR.x + 8, addR.cy(), t, kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
                break;
            }
            if (i > (int)used.size()) break;
            const int k = used[(size_t)i], src = slotSrc(k), t = slotDst(k);
            const float amt = slotAmt(k);
            const Color c = srcColour(src);
            rowSlot[nRows] = k;
            Rect& sB = rowSrc[nRows]; Rect& tB = rowDst[nRows]; Rect& aB = rowAmt[nRows]; Rect& xB = rowX[nRows];
            sB = Rect(x0, y + 1, 84, rh - 4);
            tB = Rect(sB.r() + 16, y + 1, 112, rh - 4);
            aB = Rect(tB.r() + 10, y + 4, 128, rh - 10);
            xB = Rect(r.r() - 26, y + 1, 14, rh - 4);
            const bool sel = src == selSrc;
            beginPath(); roundedRect(sB.x, sB.y, sB.w, sB.h, sB.h * 0.5f); fillColor(withAlpha2(c, theme.dark ? 0.35f : 0.6f)); fill(); strokeColor(srcInk(src)); strokeWidth(sel ? 1.6f : 1.0f); stroke();
            textAt(sB.cx(), sB.cy(), pump::mod::kSourceKeys[src], kLabel, 8.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
            textAt(sB.r() + 8, sB.cy(), ">", kMono, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            fillRect(tB, theme.bg); strokeRect(tB, theme.ink, 1.0f);
            std::string tn = kTargetNames[pump::clampv(t, 0, kNumTargets - 1)]; for (char& ch : tn) ch = (char)std::toupper((unsigned char)ch);
            textAt(tB.x + 6, tB.cy(), t ? tn : std::string("PICK A TARGET"), kLabel, 8.5f, t ? theme.ink : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
            textAt(tB.r() - 7, tB.cy(), "v", kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            fillRect(aB, theme.panel2);
            const float mid = aB.cx(), end = mid + aB.w * 0.5f * amt * 0.01f;
            fillRect(Rect(std::min(mid, end), aB.y, std::fabs(end - mid), aB.h), c);
            line(mid, aB.y - 2, mid, aB.b() + 2, theme.ink, 1.2f);
            strokeRect(aB, theme.ink, 1.0f);
            std::snprintf(b, sizeof b, "%+.0f%%", amt);
            textAt(aB.r() + 8, sB.cy(), b, kMono, 10.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
            textAt(xB.cx(), xB.cy(), "x", kMono, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            ++nRows;
        }
        if (maxScroll > 0)
        {
            const Rect sb(r.r() - 8, y0, 3, kRows * rh - 4);
            fillRect(sb, theme.panel2);
            fillRect(Rect(sb.x, sb.y + sb.h * listScroll / (float)(maxScroll + kRows), sb.w, sb.h * kRows / (float)(maxScroll + kRows)), theme.ink2);
        }
    }
    // the rings on modulated dials (every connection: its swing and where it is now), and a drag in progress
    void drawOverlay() override
    {
        std::vector<int> count(kNumParams, 0);
        for (int k = 0; k < pump::mod::kSlots; ++k)
        {
            const int src = slotSrc(k), p = targetParam(slotDst(k));
            if (src == pump::mod::kSrcNone || p < 0) continue;
            Dial* d = dialFor(p);
            if (!d || !d->visible) continue;
            const float rad = d->radius(), cx = d->r.cx(), cy = d->r.y + 16.0f + rad + 2.0f, rr = rad + 4.5f + 3.5f * (float)std::min(2, count[(size_t)p]++);
            const float a0 = (float)(pump::kPi * 0.75), a1 = (float)(pump::kPi * 2.25);
            const float n = norm(p), amt = slotAmt(k) * 0.01f;
            const bool uni = pump::mod::unipolar(src);
            const float lo = pump::clampv(uni ? std::min(n, n + amt) : n - std::fabs(amt), 0.0f, 1.0f), hi = pump::clampv(uni ? std::max(n, n + amt) : n + std::fabs(amt), 0.0f, 1.0f);
            const Color c = srcColour(src);
            if (hi > lo) { beginPath(); arc(cx, cy, rr, a0 + (a1 - a0) * lo, a0 + (a1 - a0) * hi, CW); strokeColor(withAlpha2(c, 0.9f)); strokeWidth(2.2f); stroke(); }
            const float now = plug ? plug->sourceNow[src].load() : 0.0f;
            const float an = a0 + (a1 - a0) * pump::clampv(n + amt * now, 0.0f, 1.0f);
            beginPath(); circle(cx + std::cos(an) * rr, cy + std::sin(an) * rr, 2.7f); fillColor(c); fill();
        }
        if (dragSrc > 0)
        {
            const Color c = srcColour(dragSrc);
            Dial* over = nullptr;
            for (const auto& d : targetDials)
            {
                if (!d.second->visible) continue;
                const float rad = d.second->radius(), cx = d.second->r.cx(), cy = d.second->r.y + 16.0f + rad + 2.0f;
                const bool hit = d.second->r.contains(dragX, dragY);
                if (hit) over = d.second;
                beginPath(); circle(cx, cy, rad + 6.0f); strokeColor(withAlpha2(c, hit ? 1.0f : 0.6f)); strokeWidth(hit ? 3.5f : 1.8f); stroke();
            }
            const Rect& g = gripR[dragSrc];
            line(g.cx(), g.cy(), dragX, dragY, c, 2.0f);
            beginPath(); circle(dragX, dragY, 5.0f); fillColor(c); fill(); strokeColor(theme.ink); strokeWidth(1.2f); stroke();
            if (over)
            {
                std::string t = std::string("DROP: ") + pump::mod::kSourceKeys[dragSrc] + " > " + over->label;
                const float w = textWidth(t, kLabel, 10.0f, 1.0f) + 20;
                const Rect tip(std::min(dragX + 14, (float)W - w - 6), dragY - 30, w, 22);
                fillRect(tip, theme.ink); textAt(tip.cx(), tip.cy(), t, kLabel, 10.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
            }
        }
    }
    // the envelope a note gets: struck at full, dying over DECAY while held, then over RELEASE once the key is up
    void drawEnvelope()
    {
        const Rect& r = envR;
        screenBox(r);
        const float dec = std::max(0.1f, value(kDecay)), rel = std::max(0.02f, value(kRelease));
        const float hold = std::min(dec * 0.6f, 3.0f), span = hold + rel * 1.4f;
        auto x = [&](float t) { return r.x + 6 + (r.w - 12) * t / span; };
        auto y = [&](float a) { return r.b() - 5 - (r.h - 10) * a; };
        beginPath();
        const float aHold = std::exp(-3.0f * hold / dec);
        for (int i = 0; i <= 120; ++i)
        {
            const float t = span * i / 120.0f;
            const float a = t <= hold ? std::exp(-3.0f * t / dec) : aHold * std::exp(-3.0f * (t - hold) / rel);
            if (i == 0) { moveTo(x(0), y(0)); lineTo(x(0), y(1)); } else lineTo(x(t), y(a));
        }
        lineTo(x(span), y(0)); closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.25f : 0.45f)); fill();
        strokeColor(theme.ink); strokeWidth(1.4f); stroke();
        line(x(hold), r.y + 4, x(hold), r.b() - 4, withAlpha2(theme.ink, 0.35f), 1.0f);
        textAt(x(hold) + 4, r.y + 9, "KEY UP", kLabel, 8.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
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
        textAt(k.cx(), k.cy() - 6.0f, "KICK", kBold, 24.0f, txt, ALIGN_CENTER | ALIGN_MIDDLE, 4.0f);
        textAt(k.cx(), k.cy() + 16.0f, "THE SPRINGS", kLabel, 9.0f, withAlpha2(txt, 0.8f), ALIGN_CENTER | ALIGN_MIDDLE, 1.6f);
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

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (kickR.contains(x, y)) { kickHeld = true; kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        if (shapeMouseDown(x, y, dbl)) return true;
        // the sources: a grip starts a drag onto a dial (on the SOUND page, where the dials are), a tab selects
        for (int s = 1; s < pump::mod::kNumSources; ++s)
        {
            if (!tabR[s].contains(x, y)) continue;
            if (gripR[s].contains(x, y)) { dragSrc = s; dragX = x; dragY = y; if (page != 0) setPage(0); return true; }
            selectSource(s);
            return true;
        }
        if (page == 1)
        {
            for (int i = 0; i < nRows; ++i)
            {
                const int k = rowSlot[i];
                if (rowX[i].contains(x, y)) { removeSlot(k); return true; }
                if (rowSrc[i].contains(x, y))
                {
                    std::vector<std::string> it; int cur = 0;
                    for (int j = 0; j < pump::mod::kNumSources - 1; ++j) { it.push_back(pump::mod::kSourceNames[pump::mod::kSourceOrder[j]]); if (pump::mod::kSourceOrder[j] == slotSrc(k)) cur = j; }
                    openMenu(rowSrc[i], it, cur, [this, k](int j) { setParamGesture(slotParam(k, kSlotSrc), (float)pump::mod::kSourceOrder[j]); });
                    return true;
                }
                if (rowDst[i].contains(x, y))
                {
                    std::vector<std::string> it; for (int t = 1; t < kNumTargets; ++t) it.push_back(kTargetNames[t]);
                    openMenu(rowDst[i], it, slotDst(k) - 1, [this, k](int t) { setParamGesture(slotParam(k, kSlotDst), (float)(t + 1)); });
                    return true;
                }
                if (Rect(rowAmt[i].x - 4, rowAmt[i].y - 4, rowAmt[i].w + 8, rowAmt[i].h + 8).contains(x, y))
                {
                    const int p = slotParam(k, kSlotAmt);
                    if (dbl) { setParamGesture(p, 0.0f); return true; }
                    amtSlot = k; beginEdit(p);
                    setParam(p, pump::clampv((x - rowAmt[i].cx()) / (rowAmt[i].w * 0.5f), -1.0f, 1.0f) * 100.0f);
                    amtRow = rowAmt[i];
                    return true;
                }
            }
            if (addR.w > 0 && addR.contains(x, y))
            {
                for (int k = 0; k < pump::mod::kSlots; ++k)
                    if (slotSrc(k) == pump::mod::kSrcNone)
                    {
                        beginGroup(); setParam(slotParam(k, kSlotSrc), (float)selSrc); setParam(slotParam(k, kSlotAmt), 25.0f); endGroup();
                        listScroll = 1000;   // show the new row (clamped when drawn)
                        setStatus("Now pick its target (or drag the source's grip onto a dial instead)");
                        return true;
                    }
                setStatus("All 32 connections are in use: remove one first");
                return true;
            }
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragSrc > 0) { dragX = x; dragY = y; return; }
        if (shapeDrag != kNoShapeDrag) { shapeMouseDrag(x, y, mod); return; }
        if (amtSlot >= 0)
        {
            const float v = pump::clampv((x - amtRow.cx()) / (amtRow.w * 0.5f), -1.0f, 1.0f) * 100.0f;
            setParam(slotParam(amtSlot, kSlotAmt), (mod & kModifierShift) ? std::round(v) : std::round(v / 5.0f) * 5.0f);
        }
    }
    void bodyMouseUp(float x, float y) override
    {
        kickHeld = false;
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (amtSlot >= 0) { endEdit(slotParam(amtSlot, kSlotAmt)); amtSlot = -1; }
        if (shapeDrag != kNoShapeDrag) shapeMouseUp();
        if (dragSrc > 0)
        {
            for (const auto& d : targetDials) if (d.second->visible && d.second->r.contains(x, y)) { connect(dragSrc, d.first); break; }
            dragSrc = 0;
        }
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (page == 1 && connPanel.contains(x, y)) { listScroll += dy > 0 ? -1 : 1; return true; }
        return false;
    }
    void bodyHover(float x, float y) override
    {
        const int dl = drawnLfo();
        const int hv = dl >= 0 && shapePan.contains(x, y) ? shapeHit(dl, x, y) : -1;
        if (hv != shapeHover) { shapeHover = hv; repaint(); }
        if (dl >= 0 && shapePan.contains(x, y))
        {
            setStatus(hv >= 0 && hv < 100 ? "A point: drag it (Shift: values in eighths); double-click removes it"
                      : hv >= 100 && hv < 200 ? "Drag up or down to bend this line (Shift: finer)"
                      : hv >= 200 ? "An end of the loop: drag it to another point"
                      : "Click to add a point (and drag it)");
            return;
        }
        if (dl >= 0 && rateBoxR.w > 0 && rateBoxR.contains(x, y)) { setStatus("How long one pass through the shape takes: drag up or down (double-click: default)"); return; }
        if (dl >= 0 && loopKeyR.contains(x, y)) { setStatus("LOOP: after the start, go round the loop for as long as it runs (off: once through, then hold the end)"); return; }
        if (kickR.contains(x, y)) setStatus("KICK: every ringing spring (and the tank) gets a kick (Shift+click: a nudge; key K)");
        else if (springsR.contains(x, y)) setStatus("Every sounding voice as a coil: tighter for a higher note, shaking with its level");
        else if (keyboard.r.contains(x, y)) setStatus("Play: every note tunes the springs and kicks them; velocity = how hard");
        else for (int s = 1; s < pump::mod::kNumSources; ++s)
            if (tabR[s].contains(x, y))
            {
                setStatus(gripR[s].contains(x, y) ? std::string("Drag ") + pump::mod::kSourceKeys[s] + " onto any dial to connect it"
                                                  : std::string("Click: ") + pump::mod::kSourceKeys[s] + "'s settings and connections · drag its grip onto a dial");
                return;
            }
        if (page == 1 && connPanel.contains(x, y)) setStatus("A connection: source > target, amount (drag; Shift fine; double-click 0), x removes. Wheel scrolls");
    }
    bool keyPress(uint key, uint mod) override
    {
        if (key == 'k' || key == 'K') { kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Keys / MIDI", "every note tunes the springs to it and kicks them" }, { "KICK / K", "kick the ringing springs and the tank (Shift: a nudge)" },
                 { "HARMONIC", "the springs' clang (0) to a clean pluck (100)" }, { "TANK", "SPRING's own tank around the notes" },
                 { "MODULATION", "drag a source's grip onto any dial; its ring shows the swing" }, { "MOD page", "the source's settings and every connection (x removes)" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "%s   %d / %d VOICES", kModelNames[pump::clampv((int)value(kModel), 0, 5)], plug ? plug->engine.voicesNow() : 0, (int)value(kVoices));
        std::string s = b; for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginCoil* plug = nullptr;
    Rect springsR, kickR, scopeR, meterL, meterR, springPanel, voicePanel, envPanel, envR, playPanel, modPanel;
    // modulation
    static constexpr int kRows = 6;
    int page = 0, selSrc = pump::mod::kSrcLfo1, dragSrc = 0, amtSlot = -1, listScroll = 0, nRows = 0;
    float dragX = 0.0f, dragY = 0.0f;
    double previewPhase[pump::mod::kNumLfos] = {};
    Button *soundB, *modB, *lSync[pump::mod::kNumLfos], *lRetrig[pump::mod::kNumLfos];
    Segmented* lShape[pump::mod::kNumLfos];
    Dial* lRate[pump::mod::kNumLfos];
    MenuButton* lDiv[pump::mod::kNumLfos];
    Button* lDraw[pump::mod::kNumLfos];
    // the drawn shapes (the window's copies: edited here, sent to the plugin as the "shapes" state)
    pump::mod::DrawnShape shapes[pump::mod::kNumLfos];
    Rect shapePan, shapePlot, loopKeyR, rateBoxR;
    enum ShapeDrag { kNoShapeDrag, kDragPoint, kDragBend, kDragLoopA, kDragLoopB, kDragRate } shapeDrag = kNoShapeDrag;
    int shapeK = -1, shapeHover = -1;
    float shapeY0 = 0.0f, shapeC0 = 0.0f, rateN0 = 0.0f;
    bool shapesDirty = false;
    double lastShapeSend = 0.0;
    Rect tabR[pump::mod::kNumSources], gripR[pump::mod::kNumSources], editPanel, connPanel, lfoScreenR, addR, amtRow;
    Rect rowSrc[kRows], rowDst[kRows], rowAmt[kRows], rowX[kRows];
    int rowSlot[kRows] = {};
    std::vector<std::pair<int, Dial*>> targetDials;
    Segmented* modelSeg;
    Dial *tensionD, *dripD, *wobbleD, *gritD, *toneD, *harmD, *spreadD, *exciteD, *decayD, *relD, *velD, *tankD, *widthD, *octD, *bendD, *volD, *voicesD;
    KeyboardUI keyboard;
    pump::coil::SpringsView springsView;
    bool kbDown = false, kickHeld = false;
    float t = 0.0f, kickPress = 0.0f, kickAge = 10.0f, scope[kScopeN] = {}, scale = 1.0f;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UICoil)
};

UI* createUI() { return new UICoil(); }

END_NAMESPACE_DISTRHO
