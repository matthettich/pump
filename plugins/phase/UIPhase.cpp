// pump PHASE: interface. The ENGINE row; ALGORITHM (who modulates whom; click its left or right half for the previous
// or next one), OPERATORS (each one's level as it plays; click one to edit it), SCOPE. Below: the selected operator
// (its wave, ratio or fixed frequency, fine, detune, level, envelope, velocity, LFO), the voice (feedback, chip,
// tone, pitch envelope, voices, glide), and the LFO with the selected operator's envelope, or for phase distortion
// the lines' waves, bend and its envelope. A keyboard to play.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginPhase.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::phase;

class UIPhase : public PumpUI {
public:
    UIPhase() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::phase(), "PHASE", 1000, 740, -1)
    {
        engineSeg = addSegmented(kEngine, { "CONSOLE", "ARCADE", "PC 2-OP", "PC 4-OP", "STUDIO", "PHASE DIST" });
        engineSeg->tip = "Console and arcade 4-op chips, the PC card chip (2- and 4-op), a 2-6 operator studio engine, phase distortion";
        static const char* const lab[kOpFields] = { "WAVE", "RATIO", "FINE", "DETUNE", "LEVEL", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "VELOCITY", "AM", "FIXED" };
        for (int k = 0; k < kOps; ++k)
        {
            for (int f = 0; f < kOpFields; ++f)
            {
                if (f == kOpAm || f == kOpFixed) { opTog[k][f == kOpAm ? 0 : 1] = addToggle(op(k, f), f == kOpAm ? "LFO AM" : "FIXED"); continue; }
                Dial* d = addDial(op(k, f), lab[f], Dial::Small, f == kOpDetune);
                opDial[k][f] = d;
            }
            opDial[k][kOpLevel]->tip = "Level: a carrier's loudness, a modulator's depth (0.75 dB a step, like the chips)";
            opDial[k][kOpRatio]->tip = "Frequency ratio to the note (0 = x0.5); with FIXED: 1, 10, 100 or 1000 Hz (times FINE)";
            opTog[k][0]->tip = "The LFO's tremolo (AM) on this operator";
            opTog[k][1]->tip = "A fixed frequency instead of following the note: drums, clangs, formants";
        }
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        voicesD = dial(kVoices, "VOICES"); glideD = dial(kGlide, "GLIDE"); bendD = dial(kBend, "BEND"); transD = dial(kTranspose, "TRANSPOSE", true); volD = dial(kVolume, "VOLUME", true);
        fbD = dial(kFeedback, "FEEDBACK", false, "The top operator fed back into itself (0-7, the chips' steps): a sine toward a saw, then noise");
        toneD = dial(kTone, "TONE", false, "The board's low-pass after the chip (100 = open)");
        penvD = dial(kPitchEnv, "P-ENV", true, "Pitch envelope: semitones at the start of a note, decaying to the note");
        pdecD = dial(kPitchDecay, "P-DECAY");
        modeSeg = addSegmented(kVoiceMode, { "POLY", "MONO", "LEGATO" });
        chipB = addToggle(kChip, "CHIP");
        chipB->tip = "On: the chip's integer sine, envelope steps, its own sample rate and DAC. Off: the same voice, clean";
        rateD = dial(kLfoRate, "RATE"); amD = dial(kLfoAm, "AM", false, "Tremolo depth on the operators with LFO AM on");
        pmD = dial(kLfoPm, "PM", false, "Vibrato depth"); wheelD = dial(kWheelVib, "WHEEL", false, "Vibrato from the mod wheel");
        static const int pdP[14] = { kPdWave1, kPdWave1b, kPdWave2, kPdWave2b, kPdDcw1, kPdDcw2, kPdKey, kPdMod, kPdA, kPdD, kPdS, kPdR, kPdDetune, kPdOctave };
        static const char* const pdL[14] = { "WAVE 1", "WAVE 1B", "WAVE 2", "WAVE 2B", "BEND 1", "BEND 2", "KEY", "MOD", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "DETUNE", "OCTAVE" };
        for (int i = 0; i < 14; ++i) pdD[i] = dial(pdP[i], pdL[i], pdP[i] == kPdDetune || pdP[i] == kPdOctave);
        pdD[1]->tip = pdD[3]->tip = "A second wave: the line alternates the two, cycle by cycle";
        pdD[4]->tip = pdD[5]->tip = "DCW: how far the phase is bent (0 = a plain cosine)";
        pdD[6]->tip = "Higher notes bend less (as the originals did)";
        pdD[7]->tip = "Line 2: ring-modulated by line 1, or its pitch shaken by noise";
        pdPm = dial(kLfoPm, "VIBRATO"); pdRate = dial(kLfoRate, "RATE");
        keyboard.lowNote = 36;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginPhase*>(pluginPtr())) plug = p;
        select(0);
    }
    ~UIPhase() override { keyboard.mouseUp(); }

protected:
    int engineNow() const { return pump::clampv((int)value(kEngine), 0, kNumEngines - 1); }
    int algoNow() const { return pump::clampv((int)value(kAlgo), 0, algoCount(engineNow()) - 1); }

    std::string formatParam(int i, float v) const override
    {
        char b[48];
        const int eng = engineNow();
        if (i >= kOpBase && i < kPdWave1)
        {
            const int k = (i - kOpBase) / kOpFields, f = (i - kOpBase) % kOpFields;
            const bool fixed = value(op(k, kOpFixed)) >= 0.5f;
            if (f == kOpRatio)
            {
                if (fixed) { std::snprintf(b, sizeof b, "%g Hz", std::pow(10.0, (int)v % 4)); return b; }
                const double r = ratioFor(eng, (int)v);
                std::snprintf(b, sizeof b, r < 1.0 ? "x%.1f" : "x%.0f", r); return b;
            }
            if (f == kOpFine)
            {
                if (fixed) { std::snprintf(b, sizeof b, "x%.2f", std::pow(10.0, v / 100.0)); return b; }
                std::snprintf(b, sizeof b, "+%.0f %%", v); return b;
            }
            if (f == kOpWave)
            {
                if (eng == kArcade && k == 3) return v >= 0.5f ? "NOISE" : "SINE";
                return kWaveKeys[pump::clampv((int)v, 0, 7)];
            }
            if (f == kOpLevel) { if (v <= 0.0f) return "OFF"; std::snprintf(b, sizeof b, "%.0f", v); return b; }
            if (f == kOpDetune) { std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        }
        if (i == kAlgo) { std::snprintf(b, sizeof b, "%d", (int)v + 1); return b; }
        if (i == kTranspose || i == kPdOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kPitchEnv) { if (std::fabs(v) < 0.05f) return "OFF"; std::snprintf(b, sizeof b, "%+.1f st", v); return b; }
        if (i == kPdDetune) { std::snprintf(b, sizeof b, "%+.0f ct", v); return b; }
        if (i == kPdWave1 || i == kPdWave2)
        {
            static const char* const k[kNumPdWaves] = { "SAW", "SQUARE", "PULSE", "DOUBLE", "SAW-PULSE", "RES SAW", "RES TRI", "RES TRAP" };
            return k[pump::clampv((int)v, 0, (int)kNumPdWaves - 1)];
        }
        if (i == kPdWave1b || i == kPdWave2b)
        {
            static const char* const k[kNumPdWaves + 1] = { "NONE", "SAW", "SQUARE", "PULSE", "DOUBLE", "SAW-PULSE", "RES SAW", "RES TRI", "RES TRAP" };
            return k[pump::clampv((int)v, 0, (int)kNumPdWaves)];
        }
        if (i == kPdMod) { static const char* const k[3] = { "OFF", "RING", "NOISE" }; return k[pump::clampv((int)v, 0, 2)]; }
        return PumpUI::formatParam(i, v);
    }

    void select(int k) { sel = pump::clampv(k, 0, kOps - 1); refresh(); }
    void refresh()
    {
        const int eng = engineNow(), n = opsFor(eng, algoNow());
        if (sel >= n) sel = n - 1;
        const bool pd = eng == kPd;
        for (int k = 0; k < kOps; ++k)
        {
            const bool show = k == sel;
            for (int f = 0; f < kOpFields; ++f) if (opDial[k][f]) opDial[k][f]->visible = show;
            opTog[k][0]->visible = opTog[k][1]->visible = show;
            if (!show) continue;
            const bool waves = engineHasWaves(eng) || (eng == kArcade && k == 3);
            opDial[k][kOpWave]->visible = !pd;
            opDial[k][kOpWave]->enabled = waves;
            opDial[k][kOpRatio]->visible = opDial[k][kOpFine]->visible = opDial[k][kOpDetune]->visible = !pd;
            opTog[k][1]->visible = !pd;
            // phase distortion has no wave / ratio / fine / detune per line: the rest move left
            const float hs = Dial::heightFor(Dial::Small);
            for (int f = 0; f < kOpFields; ++f)
                if (opDial[k][f]) { const int slot = pd ? std::max(0, f - kOpLevel) : f; opDial[k][f]->r = Rect(22 + slot * 86, 300, 86, hs); }
            opTog[k][0]->r = pd ? Rect(22 + 6 * 86 + 10, 318, 88, 24) : Rect(888, 304, 88, 24);
        }
        for (auto* d : { rateD, amD, pmD, wheelD }) d->visible = !pd;
        for (auto* d : pdD) d->visible = pd;
        pdPm->visible = pdRate->visible = pd;
        fbD->enabled = !pd;
        voicesD->enabled = (int)value(kVoiceMode) == 0;
        glideD->enabled = true;
    }

    void layout() override
    {
        const float top = 58, hs = Dial::heightFor(Dial::Small);
        engineSeg->r = Rect(14, top, 972, 24);
        algoR = Rect(14, top + 34, 390, 160);
        opsR = Rect(416, top + 34, 348, 160);
        scopeR = Rect(776, top + 34, 182, 160);
        meterL = Rect(964, top + 34, 8, 148); meterR = Rect(975, top + 34, 8, 148);
        opPanel = Rect(14, 264, 972, 138);
        for (int k = 0; k < kOps; ++k)
        {
            for (int f = 0; f < kOpFields; ++f) if (opDial[k][f]) opDial[k][f]->r = Rect(22 + f * 86, 300, 86, hs);
            opTog[k][0]->r = Rect(888, 304, 88, 24);
            opTog[k][1]->r = Rect(888, 336, 88, 24);
        }
        voicePanel = Rect(14, 412, 390, 228);
        Dial* r1[5] = { voicesD, glideD, bendD, transD, volD };
        for (int i = 0; i < 5; ++i) r1[i]->r = Rect(20 + i * 76, 440, 76, hs);
        modeSeg->r = Rect(26, 518, 270, 22);
        chipB->r = Rect(306, 518, 88, 22);
        Dial* r2[4] = { fbD, toneD, penvD, pdecD };
        for (int i = 0; i < 4; ++i) r2[i]->r = Rect(20 + i * 95, 556, 95, hs);
        rightPanel = Rect(416, 412, 570, 228);
        Dial* lf[4] = { rateD, amD, pmD, wheelD };
        for (int i = 0; i < 4; ++i) lf[i]->r = Rect(424 + i * 80, 440, 80, hs);
        envR = Rect(426, 524, 550, 106);
        for (int i = 0; i < 8; ++i) pdD[i]->r = Rect(422 + i * 70, 440, 70, hs);
        for (int i = 8; i < 14; ++i) pdD[i]->r = Rect(422 + (i - 8) * 70, 548, 70, hs);
        pdRate->r = Rect(422 + 6 * 70, 548, 70, hs);
        pdPm->r = Rect(422 + 7 * 70, 548, 70, hs);
        keyboard.r = Rect(14, 650, 972, 56);
    }

    void tick(float dt) override
    {
        t += dt;
        refresh();
        if (!plug) return;
        const float pkAll = pump::gainToDbF(plug->outPeak.exchange(0.0f));   // mono: both meters read it
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pkAll;
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; } else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        for (int k = 0; k < kOps; ++k) opLv[k] = std::max(plug->engine.opLevel(k), opLv[k] * std::exp(-dt * 6.0f));
    }

    void drawBody() override
    {
        drawAlgo();
        drawOps();
        drawScope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        const bool pd = engineNow() == kPd;
        auto panel = [&](const Rect& r, const std::string& c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        char b[48];
        if (pd) std::snprintf(b, sizeof b, "DCA %d  (LINE %d'S LEVEL)", sel + 1, sel + 1);
        else std::snprintf(b, sizeof b, "OPERATOR %d%s", sel + 1, (algoFor(engineNow(), algoNow()).car >> sel & 1) ? "  ·  CARRIER" : "  ·  MODULATOR");
        panel(opPanel, b);
        panel(voicePanel, "VOICE");
        panel(rightPanel, pd ? "PHASE DISTORTION" : "LFO");
        if (pd) caption(rightPanel.x + 12, 534, "BEND ENVELOPE  ·  LINE 2  ·  LFO");
        else drawEnvelope();
        keyboard.draw(*this);
    }

    // the operators as boxes: carriers on the bottom row, each modulator above what it modulates
    void drawAlgo()
    {
        const Rect& r = algoR;
        screenBox(r);
        const int eng = engineNow(), a = algoNow();
        char b[64]; std::snprintf(b, sizeof b, "ALGORITHM %d / %d", a + 1, algoCount(eng));
        caption(r.x + 10, r.y + 14, b);
        textAt(r.r() - 10, r.y + 14, "<   >", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Color ink = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f);
        if (eng == kPd)
        {
            static const char* const nm[4] = { "LINE 1", "LINE 2", "LINE 1 + LINE 1' (DETUNED)", "LINE 1 + LINE 2' (DETUNED)" };
            textAt(r.cx(), r.b() - 16, nm[a], kLabel, 10.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
            const bool two = a >= 2;
            const float bw = 120, bh = 40, y = r.y + 50;
            const float x1 = two ? r.cx() - bw - 14 : r.cx() - bw * 0.5f;
            auto box = [&](float x, const char* t1, const char* t2) {
                const Rect bx(x, y, bw, bh);
                strokeRect(bx, ink, 1.6f);
                textAt(bx.cx(), bx.y + 13, t1, kBold, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
                textAt(bx.cx(), bx.y + 29, t2, kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
                line(bx.cx(), bx.b(), bx.cx(), r.b() - 36, ink, 1.4f);
            };
            const int w1 = (int)value(kPdWave1), w2 = (int)value(kPdWave2);
            static const char* const k[kNumPdWaves] = { "SAW", "SQUARE", "PULSE", "DOUBLE", "SAW-PULSE", "RES SAW", "RES TRI", "RES TRAP" };
            if (a == 1) box(x1, "DCO 2", k[pump::clampv(w2, 0, 7)]);
            else box(x1, "DCO 1", k[pump::clampv(w1, 0, 7)]);
            if (two) box(r.cx() + 14, a == 2 ? "DCO 1'" : "DCO 2'", k[pump::clampv(a == 2 ? w1 : w2, 0, 7)]);
            line(r.x + 40, r.b() - 36, r.r() - 40, r.b() - 36, ink, 2.0f);
            const int mod = (int)value(kPdMod);
            if (two && mod > 0) textAt(r.cx(), y + bh * 0.5f, mod == 1 ? "RING" : "NOISE", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
            return;
        }
        const Algo& A = algoFor(eng, a);
        textAt(r.cx(), r.b() - 14, A.name, kLabel, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        // depth: carriers 0, a modulator one more than the deepest thing it modulates
        int depth[kOps] = {};
        for (int pass = 0; pass < kOps; ++pass)
            for (int i = 0; i < A.n; ++i)
                for (int j = 0; j < A.n; ++j)
                    if (A.mod[i] >> j & 1) depth[j] = std::max(depth[j], depth[i] + 1);
        int maxD = 0; for (int i = 0; i < A.n; ++i) maxD = std::max(maxD, depth[i]);
        const float bw = 30, bh = 22, area = r.h - 70;
        const float rowH = maxD > 0 ? std::min(34.0f, area / (float)maxD) : 0.0f;
        float bx[kOps], by[kOps];
        for (int d = 0; d <= maxD; ++d)
        {
            int cnt = 0; for (int i = 0; i < A.n; ++i) if (depth[i] == d) ++cnt;
            int at = 0;
            for (int i = 0; i < A.n; ++i)
            {
                if (depth[i] != d) continue;
                const float span = std::min(r.w - 40.0f, cnt * 56.0f);
                bx[i] = r.cx() - span * 0.5f + span * (at + 0.5f) / cnt - bw * 0.5f;
                by[i] = r.y + 26 + area - d * rowH - bh * 0.5f + 8.0f;
                ++at;
            }
        }
        const float outY = r.b() - 34;
        line(r.x + 40, outY, r.r() - 40, outY, ink, 2.0f);
        for (int i = 0; i < A.n; ++i)
        {
            for (int j = 0; j < A.n; ++j) if (A.mod[i] >> j & 1) line(bx[j] + bw * 0.5f, by[j] + bh, bx[i] + bw * 0.5f, by[i], ink, 1.4f);
            if (A.car >> i & 1) line(bx[i] + bw * 0.5f, by[i] + bh, bx[i] + bw * 0.5f, outY, ink, 1.4f);
        }
        for (int i = 0; i < A.n; ++i)
        {
            const Rect bxr(bx[i], by[i], bw, bh);
            const bool on = i == sel;
            fillRect(bxr, on ? theme.neon : theme.bg);
            strokeRect(bxr, on ? theme.ink : ink, on ? 2.0f : 1.4f);
            char nb[4]; std::snprintf(nb, sizeof nb, "%d", i + 1);
            textAt(bxr.cx(), bxr.cy(), nb, kBold, 11.0f, on ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
            if (i == A.fb && (int)value(kFeedback) > 0)
            {
                // the feedback loop: out of the right side and back into the top
                const float x0 = bxr.r(), ym = bxr.cy();
                line(x0, ym, x0 + 8, ym, ink, 1.2f); line(x0 + 8, ym, x0 + 8, bxr.y - 6, ink, 1.2f);
                line(x0 + 8, bxr.y - 6, bxr.cx(), bxr.y - 6, ink, 1.2f); line(bxr.cx(), bxr.y - 6, bxr.cx(), bxr.y, ink, 1.2f);
            }
            opBox[i] = bxr;
        }
        for (int i = A.n; i < kOps; ++i) opBox[i] = Rect();
    }
    // each operator's level as it plays, its ratio and wave; click one to edit it
    void drawOps()
    {
        const Rect& r = opsR;
        screenBox(r);
        const int eng = engineNow(), n = opsFor(eng, algoNow());
        caption(r.x + 10, r.y + 14, eng == kPd ? "LINES" : "OPERATORS");
        const float cw = (r.w - 20) / kOps;
        for (int k = 0; k < kOps; ++k)
        {
            const Rect c(r.x + 10 + k * cw, r.y + 26, cw - 6, r.h - 34);
            opCol[k] = k < n ? c : Rect();
            if (k >= n) { textAt(c.cx(), c.cy(), "·", kLabel, 12.0f, theme.dim, ALIGN_CENTER | ALIGN_MIDDLE); continue; }
            const bool on = k == sel;
            if (on) strokeRect(c, theme.ink, 1.5f);
            char b[16]; std::snprintf(b, sizeof b, eng == kPd ? "DCA %d" : "OP %d", k + 1);
            textAt(c.cx(), c.y + 10, b, kBold, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.5f);
            // the bar: the level knob (outline) and the live level (fill)
            const Rect bar(c.cx() - 7, c.y + 22, 14, c.h - 60);
            strokeRect(bar, theme.dim, 1.0f);
            const float setLv = value(op(k, kOpLevel)) <= 0.0f ? 0.0f : std::exp2(-(99.0f - value(op(k, kOpLevel))) * 0.75f / 6.0206f);
            const float live = std::min(1.0f, opLv[k]);
            auto yOf = [&](float a) { const float db = a > 1e-5f ? 20.0f * std::log10(a) : -96.0f; return bar.b() - bar.h * pump::clampv((db + 72.0f) / 72.0f, 0.0f, 1.0f); };
            fillRect(Rect(bar.x, yOf(live), bar.w, bar.b() - yOf(live)), pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.25f));
            line(bar.x - 3, yOf(setLv), bar.r() + 3, yOf(setLv), theme.ink, 1.5f);
            if (eng != kPd)
            {
                const bool fixed = value(op(k, kOpFixed)) >= 0.5f;
                std::string rt = formatParam(op(k, kOpRatio), value(op(k, kOpRatio)));
                textAt(c.cx(), c.b() - 24, rt, kMono, 9.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
                const bool waves = engineHasWaves(eng) || (eng == kArcade && k == 3);
                textAt(c.cx(), c.b() - 10, fixed ? "FIXED" : (waves ? formatParam(op(k, kOpWave), value(op(k, kOpWave))) : "SINE"), kLabel, 7.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.3f);
            }
        }
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
    // the selected operator's envelope (amplitude over time; the chips' dB-linear decay shows as a curve)
    void drawEnvelope()
    {
        const Rect& r = envR;
        screenBox(r);
        char b[48]; std::snprintf(b, sizeof b, "OPERATOR %d ENVELOPE", sel + 1);
        caption(r.x + 10, r.y + 14, b);
        const double A = value(op(sel, kOpA)) * 0.001, D = value(op(sel, kOpD)) * 0.001, S = value(op(sel, kOpS)) * 0.01, R = value(op(sel, kOpR)) * 0.001;
        const double sAtt = S <= 0.0 ? 97.0 : 96.0 * (1.0 - S) * (1.0 - S) * 0.5;
        const double hold = 0.25 * (A + D + R) + 0.05, total = A + D * std::min(1.0, sAtt / 96.0) + hold + R;
        const Rect p(r.x + 12, r.y + 24, r.w - 24, r.h - 32);
        beginPath();
        const int N = 240;
        double att = 96.0; int stage = 1; double tm = 0.0;
        for (int i = 0; i <= N; ++i)
        {
            const double tt = total * i / N;
            while (tm < tt)
            {
                const double dt = total / N / 8.0; tm += dt;
                if (stage == 1) { att -= (att + 4.0) * (1.0 - std::pow(25.0, -dt / std::max(1e-4, A))); if (att <= 0) { att = 0; stage = 2; } }
                else if (stage == 2) { att += 96.0 * dt / std::max(1e-3, D); if (att >= sAtt) { att = std::min(sAtt, 96.0); stage = 3; } }
                else if (stage == 3) { if (tm > A + D * std::min(1.0, sAtt / 96.0) + hold) stage = 4; }
                else att = std::min(96.0, att + 96.0 * dt / std::max(1e-3, R));
            }
            const double amp = std::exp2(-att / 6.0206);
            const float x = p.x + p.w * i / N, y = p.b() - (float)amp * p.h;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.8f); lineJoin(ROUND); stroke();
    }

    bool bodyMouseDown(float x, float y, int button, uint, bool) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        for (int k = 0; k < kOps; ++k)
            if (opCol[k].contains(x, y) || opBox[k].contains(x, y)) { select(k); return true; }
        if (algoR.contains(x, y))
        {
            const int n = algoCount(engineNow());
            const int a = (algoNow() + (x < algoR.cx() ? n - 1 : 1)) % n;
            setParamGesture(kAlgo, (float)a);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override { if (kbDown) keyboard.mouseDrag(x, y); }
    void bodyMouseUp(float, float) override { if (kbDown) { keyboard.mouseUp(); kbDown = false; } }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (!algoR.contains(x, y)) return false;
        const int n = algoCount(engineNow());
        setParamGesture(kAlgo, (float)((algoNow() + (dy < 0 ? 1 : n - 1)) % n));
        return true;
    }
    void bodyHover(float x, float y) override
    {
        if (algoR.contains(x, y)) setStatus("Click the left / right half (or scroll) for the previous / next algorithm; click an operator to edit it");
        else if (opsR.contains(x, y)) setStatus("Each operator's level as it plays (the line: its LEVEL knob); click one to edit it");
        else if (keyboard.r.contains(x, y)) setStatus("Play PHASE");
    }
    bool keyPress(uint key, uint) override
    {
        if (key >= '1' && key <= '6') { select((int)(key - '1')); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "1-6", "edit that operator" }, { "ALGORITHM", "click its halves or scroll: previous / next" },
                 { "CHIP", "the chip's integer maths, rate and DAC (off: clean)" }, { "FIXED", "an operator at 1/10/100/1000 Hz x FINE" } };
    }
    std::string footerInfo() override
    {
        char b[96]; std::snprintf(b, sizeof b, "%s   %s   %d VOICES", kEngineKeys[engineNow()], value(kChip) >= 0.5f ? "CHIP" : "CLEAN", plug ? plug->engine.voicesNow() : 0);
        return b;
    }
    void onParamChanged(int i, float) override { if (i == kEngine || i == kAlgo) refresh(); }

private:
    static constexpr int kScopeN = 4096;
    PluginPhase* plug = nullptr;
    int sel = 0;
    Rect algoR, opsR, scopeR, meterL, meterR, opPanel, voicePanel, rightPanel, envR, opCol[kOps], opBox[kOps];
    Segmented *engineSeg, *modeSeg;
    Dial* opDial[kOps][kOpFields] = {};
    Button* opTog[kOps][2] = {};
    Dial *voicesD, *glideD, *bendD, *transD, *volD, *fbD, *toneD, *penvD, *pdecD, *rateD, *amD, *pmD, *wheelD, *pdPm, *pdRate;
    Dial* pdD[14] = {};
    Button* chipB;
    KeyboardUI keyboard;
    bool kbDown = false;
    float t = 0.0f, scope[kScopeN] = {}, scale = 1.0f, opLv[kOps] = {};
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIPhase)
};

UI* createUI() { return new UIPhase(); }

END_NAMESPACE_DISTRHO
