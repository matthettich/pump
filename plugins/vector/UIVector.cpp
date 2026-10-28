// pump VECTOR: interface. VECTOR: the square, its four sources at the corners, the joystick (drag it), the vector
// envelope's path (drag its four points) and where the newest voice's point is now. A to D: each source's wave as it
// plays (a sequence shows its step; a scanned string shows its ring of masses moving). Below: the four sources, the
// joystick and envelope, the sequence and the scanned strings, the filter, the amp envelope, the LFO, the voice, and a
// keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginVector.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::vector;

class UIVector : public PumpUI {
public:
    UIVector() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::vector(), "VECTOR", 1000, 640, -1)
    {
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        for (int s = 0; s < kSources; ++s)
        {
            typeSeg[s] = addSegmented(ps(s, kType), { "WAVE", "SEQ", "SCAN" });
            typeSeg[s]->tip = "A single-cycle wave · a wave sequence (from this wave) · a scanned string (struck in this wave's shape)";
            waveM[s] = addMenu(ps(s, kWaveP), "WAVE"); waveM[s]->arrows = true;
            tuneD[s] = dial(ps(s, kTune), "TUNE", true); fineD[s] = dial(ps(s, kFine), "FINE", true); levelD[s] = dial(ps(s, kLevel), "LEVEL");
        }
        xD = dial(kX, "X", false, "The joystick across: A and C (0) to B and D (100)");
        yD = dial(kY, "Y", false, "The joystick down: A and B (0) to C and D (100)");
        envD = dial(kVecAmt, "ENV", false, "How far the vector envelope takes the point from the joystick along its path");
        timeD = dial(kVecTime, "TIME", false, "The time from one path point to the next");
        vloopSeg = addSegmented(kVecLoopP, { "ONCE", "LOOP", "BOUNCE" });
        seqRateD = dial(kSeqRate, "STEP"); seqStepsD = dial(kSeqSteps, "STEPS"); seqStrideD = dial(kSeqStride, "STRIDE", true, "How many waves on in the bank each step goes");
        seqXfD = dial(kSeqXfade, "XFADE"); seqTrD = dial(kSeqTranspose, "TRANSP", true, "Semitones each step transposes by (wrapping at two octaves)");
        sloopSeg = addSegmented(kSeqLoopP, { "LOOP", "BOUNCE", "ONCE" });
        tenD = dial(kScanTension, "TENSION"); dampD = dial(kScanDamp, "DAMPING"); cenD = dial(kScanCentre, "CENTRING");
        hamD = dial(kScanHammer, "HAMMER", false, "How hard the string is struck at the note"); spdD = dial(kScanSpeed, "SPEED", false, "How fast the string moves (the timbre's motion)");
        cutD = dial(kCutoff, "CUTOFF"); resD = dial(kReso, "RESO"); fenvD = dial(kEnvCut, "ENV", true); keyD = dial(kKeytrack, "KEY");
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { fD[k] = dial(kFA + k, adsr[k]); aD[k] = dial(kAttack + k, adsr[k]); }
        lRateD = dial(kLfoRate, "RATE"); lPitchD = dial(kLfoPitch, "PITCH"); lXD = dial(kLfoX, "X"); lYD = dial(kLfoY, "Y"); lCutD = dial(kLfoCut, "CUTOFF");
        velD = dial(kVelocity, "VELOCITY"); spreadD = dial(kSpread, "SPREAD"); voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE");
        octD = dial(kOctave, "OCTAVE", true); volD = dial(kVolume, "VOLUME", true);
        vmodeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        // two pages under the square and the screens: SOURCES (the four sources, joystick, sequence, scanned) and
        // VOICE (filter, amp, LFO, voice); the keyboard stays
        srcB = addButton("SOURCES", [this] { setPage(0); }, [this] { return page == 0; });
        srcB->tip = "The four sources, the joystick and envelope, the sequence and the scanned strings";
        voiceB = addButton("VOICE", [this] { setPage(1); }, [this] { return page == 1; });
        voiceB->tip = "The filter, the amp envelope, the LFO and the voice";
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginVector*>(pluginPtr())) plug = p;
    }
    ~UIVector() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kGlide && v < 0.5f) return "OFF";
        for (int s = 0; s < kSources; ++s)
        {
            if (i == ps(s, kWaveP)) { std::string n = kWaveNames[pump::clampv((int)v, 0, kNumWaves - 1)]; for (char& c : n) c = (char)std::toupper((unsigned char)c); return n; }
            if (i == ps(s, kTune)) { std::snprintf(b, sizeof b, "%+.0f st", v); return b; }
            if (i == ps(s, kFine)) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        }
        if (i == kSeqStride || i == kSeqTranspose) { std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        // the square, the four sources' screens, the output
        vecR = Rect(14, top, 210, 186);
        for (int s = 0; s < kSources; ++s) srcR[s] = Rect(236 + (s & 1) * 266, top + (s >> 1) * 95, 258, 91);
        scopeR = Rect(780, top, 180, 186);
        meterL = Rect(966, top, 8, 174); meterR = Rect(977, top, 8, 174);
        // the keyboard, the page keys beside it
        const float ky = 550;
        srcB->r = Rect(14, ky, 96, 25);
        voiceB->r = Rect(14, ky + 31, 96, 25);
        keyboard.r = Rect(122, ky, 858, 56);
        const bool vp = page == 1;
        std::vector<Control*> srcCtl = { xD, yD, envD, timeD, vloopSeg, seqRateD, seqStepsD, seqStrideD, seqXfD, seqTrD, sloopSeg, tenD, dampD, cenD, hamD, spdD };
        for (int s = 0; s < kSources; ++s) for (Control* c : std::initializer_list<Control*>{ typeSeg[s], waveM[s], tuneD[s], fineD[s], levelD[s] }) srcCtl.push_back(c);
        std::vector<Control*> voiceCtl = { cutD, resD, fenvD, keyD, lRateD, lPitchD, lXD, lYD, lCutD, velD, spreadD, voicesD, glideD, octD, volD, vmodeSeg };
        for (int k = 0; k < 4; ++k) { voiceCtl.push_back(fD[k]); voiceCtl.push_back(aD[k]); }
        for (Control* c : srcCtl) c->visible = !vp;
        for (Control* c : voiceCtl) c->visible = vp;
        const float y0 = top + 186 + 12;          // the page's area: y0 .. 538
        const Rect none(0, 0, 0, 0);
        srcPanel = vecPanel = seqPanel = scanPanel = filtPanel = ampPanel = lfoPanel = voicePanel = none;
        if (!vp)
        {
            srcPanel = Rect(14, y0, 972, 150);
            for (int s = 0; s < kSources; ++s)
            {
                const float x = 22 + s * 243;
                typeSeg[s]->r = Rect(x, y0 + 26, 228, 22);
                waveM[s]->r = Rect(x, y0 + 52, 228, 22);
                tuneD[s]->r = Rect(x - 4, y0 + 76, 78, hs); fineD[s]->r = Rect(x + 74, y0 + 76, 78, hs); levelD[s]->r = Rect(x + 152, y0 + 76, 78, hs);
            }
            const float y1 = srcPanel.b() + 12;
            vecPanel = Rect(14, y1, 308, 120);
            vloopSeg->r = Rect(24, y1 + 26, 288, 22);
            Dial* vd[4] = { xD, yD, envD, timeD };
            for (int k = 0; k < 4; ++k) vd[k]->r = Rect(20 + k * 75, y1 + 50, 75, hs);
            seqPanel = Rect(334, y1, 326, 120);
            sloopSeg->r = Rect(344, y1 + 26, 306, 22);
            Dial* sd[5] = { seqRateD, seqStepsD, seqStrideD, seqXfD, seqTrD };
            for (int k = 0; k < 5; ++k) sd[k]->r = Rect(338 + k * 64, y1 + 50, 64, hs);
            scanPanel = Rect(672, y1, 314, 120);
            Dial* cd[5] = { tenD, dampD, cenD, hamD, spdD };
            for (int k = 0; k < 5; ++k) cd[k]->r = Rect(676 + k * 61, y1 + 40, 61, hs);
            return;
        }
        const float rh = 132;
        filtPanel = Rect(14, y0, 640, rh);
        Dial* fd[8] = { cutD, resD, fenvD, keyD, fD[0], fD[1], fD[2], fD[3] };
        for (int k = 0; k < 8; ++k) fd[k]->r = Rect(20 + k * 79, y0 + 44, 79, hs);
        ampPanel = Rect(666, y0, 320, rh);
        for (int k = 0; k < 4; ++k) aD[k]->r = Rect(672 + k * 78, y0 + 44, 78, hs);
        const float y1 = y0 + rh + 12;
        lfoPanel = Rect(14, y1, 400, rh);
        Dial* ld[5] = { lRateD, lPitchD, lXD, lYD, lCutD };
        for (int k = 0; k < 5; ++k) ld[k]->r = Rect(20 + k * 78, y1 + 44, 78, hs);
        voicePanel = Rect(426, y1, 560, rh);
        vmodeSeg->r = Rect(436, y1 + 28, 540, 22);
        Dial* od[6] = { velD, spreadD, voicesD, glideD, octD, volD };
        for (int k = 0; k < 6; ++k) od[k]->r = Rect(432 + k * 91, y1 + 56, 91, hs);
    }
    void setPage(int p)
    {
        if (p == page) return;
        page = p;
        layout();
        saveUiState(page == 1 ? "page=voice;" : "page=sources;");
        repaint();
    }
    void loadUiState(const std::string& s) override
    {
        page = uiStateGet(s, "page", "sources") == "voice" ? 1 : 0;
        layout();
        repaint();
    }

    void tick(float dt) override
    {
        // test hook for headless screenshots (unused in normal runs): open on a page
        if (!testPaged)
        {
            testPaged = true;
            if (const char* tp = pump::testEnv("PUMP_TEST_PAGE")) setPage(std::strcmp(tp, "voice") == 0 || std::strcmp(tp, "1") == 0 ? 1 : 0);
        }
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        bool anySeq = false, anyScan = false;
        for (int s = 0; s < kSources; ++s) { const int t = (int)value(ps(s, kType)); anySeq = anySeq || t == kSequence; anyScan = anyScan || t == kScanned; }
        for (Dial* d : { seqRateD, seqStepsD, seqStrideD, seqXfD, seqTrD }) d->enabled = anySeq;
        for (Dial* d : { tenD, dampD, cenD, hamD, spdD }) d->enabled = anyScan;
        timeD->enabled = value(kVecAmt) > 0.5f;
        anim += dt;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        playing = plug->engine.voicesNow() > 0;
        ptX = plug->engine.pointX(); ptY = plug->engine.pointY();
        for (int k = 0; k < kMasses; ++k) mass[k] = plug->engine.massAt(k);
        plug->engine.outRing.readLatest(scope, kScope);
    }

    Rect padOf() const { return Rect(vecR.x + 30, vecR.y + 34, vecR.w - 60, vecR.h - 64); }
    void drawVector()
    {
        const Rect& r = vecR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "VECTOR");
        const Rect p = padOf();
        strokeRect(p, theme.dim, 1.0f);
        line(p.cx(), p.y, p.cx(), p.b(), theme.grid, 1.0f); line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        const float cxs[4] = { p.x, p.r(), p.x, p.r() }, cys[4] = { p.y, p.y, p.b(), p.b() };
        for (int s = 0; s < kSources; ++s)
        {
            beginPath(); circle(cxs[s], cys[s], 9.0f); fillColor(theme.ink); fill();
            textAt(cxs[s], cys[s], kSrcName[s], kLabel, 9.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        const Color hot = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        // the path
        if (value(kVecAmt) > 0.5f)
        {
            beginPath();
            for (int k = 0; k < 4; ++k) { const float x = p.x + p.w * value(kP1x + 2 * k) * 0.01f, y = p.y + p.h * value(kP1y + 2 * k) * 0.01f; if (!k) moveTo(x, y); else lineTo(x, y); }
            if ((int)value(kVecLoopP) == kVecRepeat) closePath();
            strokeColor(withAlpha2(hot, 0.7f)); strokeWidth(1.5f); stroke();
            for (int k = 0; k < 4; ++k)
            {
                const float x = p.x + p.w * value(kP1x + 2 * k) * 0.01f, y = p.y + p.h * value(kP1y + 2 * k) * 0.01f;
                beginPath(); circle(x, y, 7.0f); fillColor(theme.screen); fill(); strokeColor(hot); strokeWidth(1.5f); stroke();
                char b[4]; std::snprintf(b, sizeof b, "%d", k + 1);
                textAt(x, y, b, kMono, 8.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        // the joystick and the point now
        const float jx = p.x + p.w * value(kX) * 0.01f, jy = p.y + p.h * value(kY) * 0.01f;
        line(jx - 8, jy, jx + 8, jy, theme.ink, 1.5f); line(jx, jy - 8, jx, jy + 8, theme.ink, 1.5f);
        if (playing) { beginPath(); circle(p.x + p.w * ptX, p.y + p.h * ptY, 6.0f); fillColor(hot); fill(); }
    }
    void drawSource(int s)
    {
        const Rect& r = srcR[s];
        screenBox(r);
        const int type = pump::clampv((int)value(ps(s, kType)), 0, kNumTypes - 1);
        char b[64]; std::snprintf(b, sizeof b, "%s  %s", kSrcName[s], type == kWave ? "WAVE" : (type == kSequence ? "SEQUENCE" : "SCANNED"));
        caption(r.x + 10, r.y + 14, b);
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        const Color c = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        // how much of the mix it is now
        const float x = playing ? ptX : value(kX) * 0.01f, y = playing ? ptY : value(kY) * 0.01f;
        const float wts[4] = { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };
        const float w = wts[s] * value(ps(s, kLevel)) * 0.01f;
        fillRect(Rect(r.r() - 8, r.b() - 6 - (r.h - 30) * w, 4, (r.h - 30) * w), c);
        beginPath();
        if (type == kScanned)
        {
            float mx = 1e-3f; for (int k = 0; k < kMasses; ++k) mx = std::max(mx, std::fabs(mass[k]));
            const bool live = playing && plug && plug->engine.scannedSource() == s;
            for (int k = 0; k <= kMasses; ++k)
            {
                float v;
                if (live) v = mass[k % kMasses] / mx;
                else { float sh[kMasses]; WaveBank::get().shape((int)value(ps(s, kWaveP)), sh, kMasses); v = sh[k % kMasses]; }
                const float px = p.x + (p.w - 10) * k / (float)kMasses, py = p.cy() - pump::clampv(v, -1.2f, 1.2f) * p.h * 0.4f;
                if (!k) moveTo(px, py); else lineTo(px, py);
            }
            strokeColor(c); strokeWidth(1.8f); stroke();
            for (int k = 0; k < kMasses; k += 4)
            {
                const float v = live ? mass[k] / mx : 0.0f;
                beginPath(); circle(p.x + (p.w - 10) * k / (float)kMasses, p.cy() - pump::clampv(v, -1.2f, 1.2f) * p.h * 0.4f, 1.8f); fillColor(theme.ink); fill();
            }
        }
        else
        {
            int wv = (int)value(ps(s, kWaveP));
            if (playing && plug) wv = plug->engine.waveNow(s);
            for (int k = 0; k <= 160; ++k)
            {
                const float v = (float)WaveBank::get().read(wv, 3, k / 160.0);
                const float px = p.x + (p.w - 10) * k / 160.0f, py = p.cy() - pump::clampv(v, -1.5f, 1.5f) * p.h * 0.32f;
                if (!k) moveTo(px, py); else lineTo(px, py);
            }
            strokeColor(c); strokeWidth(1.8f); stroke();
            std::string nm = kWaveNames[pump::clampv(wv, 0, kNumWaves - 1)]; for (char& ch : nm) ch = (char)std::toupper((unsigned char)ch);
            textAt(r.r() - 14, r.y + 14, nm, kLabel, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.4f);
            if (type == kSequence)
            {
                // the steps, the current one lit
                const int steps = (int)value(kSeqSteps), now = playing && plug ? plug->engine.seqStep(s) : -1;
                const float bw = std::min(10.0f, (p.w - 20) / steps);
                for (int k = 0; k < steps; ++k)
                {
                    const Rect sq(p.x + k * bw, p.b() - 7, bw - 2, 6);
                    if (k == now) fillRect(sq, theme.ink); else strokeRect(sq, theme.dim, 1.0f);
                }
            }
        }
    }
    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "OUT");
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        int st = 0; for (int i = 1; i < kScope / 2; ++i) if (scope[i - 1] <= 0.0f && scope[i] > 0.0f) { st = i; break; }
        float mx = 1e-3f; for (int i = 0; i < kScope; ++i) mx = std::max(mx, std::fabs(scope[i]));
        beginPath();
        const int n = kScope / 2;
        for (int i = 0; i < n; ++i) { const float x = p.x + p.w * i / (n - 1.0f), y = p.cy() - scope[st + i] / mx * p.h * 0.45f; if (!i) moveTo(x, y); else lineTo(x, y); }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.4f); stroke();
    }

    void drawBody() override
    {
        drawVector();
        for (int s = 0; s < kSources; ++s) drawSource(s);
        drawScope();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        if (page == 0) { panel(srcPanel, "SOURCES   A · B · C · D"); panel(vecPanel, "JOYSTICK · ENVELOPE"); panel(seqPanel, "SEQUENCE"); panel(scanPanel, "SCANNED"); }
        else { panel(filtPanel, "FILTER"); panel(ampPanel, "AMP"); panel(lfoPanel, "LFO"); panel(voicePanel, "VOICE"); }
        keyboard.draw(*this);
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        const Rect p = padOf();
        if (Rect(p.x - 12, p.y - 12, p.w + 24, p.h + 24).contains(x, y))
        {
            drag = -1;
            if (value(kVecAmt) > 0.5f)
                for (int k = 0; k < 4; ++k)
                {
                    const float px = p.x + p.w * value(kP1x + 2 * k) * 0.01f, py = p.y + p.h * value(kP1y + 2 * k) * 0.01f;
                    if (std::hypot(x - px, y - py) < 10.0f) { drag = k; break; }
                }
            dx = drag < 0 ? kX : kP1x + 2 * drag; dy = dx + 1;
            beginEdit(dx); beginEdit(dy);
            dragTo(x, y);
            dragging = true;
            return true;
        }
        return false;
    }
    void dragTo(float x, float y)
    {
        const Rect p = padOf();
        setParam(dx, pump::clampv((x - p.x) / p.w, 0.0f, 1.0f) * 100.0f);
        setParam(dy, pump::clampv((y - p.y) / p.h, 0.0f, 1.0f) * 100.0f);
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (dragging) dragTo(x, y);
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; }
        if (dragging) { endEdit(dx); endEdit(dy); dragging = false; }
    }
    void bodyHover(float x, float y) override
    {
        if (vecR.contains(x, y)) setStatus("Drag: the joystick (or, with the envelope on, one of its path points)");
        else if (keyboard.r.contains(x, y)) setStatus("Play VECTOR");
        else for (int s = 0; s < kSources; ++s) if (srcR[s].contains(x, y)) { setStatus("The source's wave now (a scanned string: its ring of masses); the bar is its share of the mix"); break; }
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "THE SQUARE", "drag the joystick; ENV moves the point along the path (drag its points)" }, { "SEQ", "steps through the bank from the source's wave" },
                 { "SOURCES | VOICE", "the two pages under the screens (the keyboard stays)" }, { "SCAN", "a ring of masses and springs struck in the wave's shape, then left to move" }, { "TRANSP", "transposes each sequence step: arpeggios" } };
    }
    std::string footerInfo() override
    {
        char b[64]; std::snprintf(b, sizeof b, "X %.0f  Y %.0f   %d VOICES", (playing ? ptX : value(kX) * 0.01f) * 100.0f, (playing ? ptY : value(kY) * 0.01f) * 100.0f, plug ? plug->engine.voicesNow() : 0);
        return b;
    }

private:
    static constexpr int kScope = 1024;
    PluginVector* plug = nullptr;
    Rect vecR, srcR[kSources], scopeR, meterL, meterR, srcPanel, vecPanel, seqPanel, scanPanel, filtPanel, ampPanel, lfoPanel, voicePanel;
    Segmented *typeSeg[kSources], *vloopSeg, *sloopSeg, *vmodeSeg;
    MenuButton* waveM[kSources];
    Dial *tuneD[kSources], *fineD[kSources], *levelD[kSources];
    Dial *xD, *yD, *envD, *timeD, *seqRateD, *seqStepsD, *seqStrideD, *seqXfD, *seqTrD, *tenD, *dampD, *cenD, *hamD, *spdD;
    Dial *cutD, *resD, *fenvD, *keyD, *fD[4], *aD[4], *lRateD, *lPitchD, *lXD, *lYD, *lCutD, *velD, *spreadD, *voicesD, *glideD, *octD, *volD;
    KeyboardUI keyboard;
    Button *srcB, *voiceB;
    int page = 0;              // 0 SOURCES, 1 VOICE
    bool testPaged = false;
    bool kbDown = false, dragging = false, playing = false;
    int drag = -1, dx = kX, dy = kY;
    float ptX = 0.5f, ptY = 0.5f, mass[kMasses] = {}, scope[kScope] = {}, anim = 0.0f;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIVector)
};

UI* createUI() { return new UIVector(); }

END_NAMESPACE_DISTRHO
