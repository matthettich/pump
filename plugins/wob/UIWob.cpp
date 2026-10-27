// pump WOB: interface. SCOPE (the output), PARTIALS (every partial's level: the source spectrum outlined, what
// the filter, formants and comb leave of it filled, and the filter's curve; drag: left / right = cutoff, up / down =
// resonance) and WOBBLE (one cycle of its shape, the dot where it is now). Below: the source, the filter, the
// formant / comb / reese colour; the wobble, where it goes, and the amp. A keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginWob.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::wob;

class UIWob : public PumpUI {
public:
    UIWob() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::wob(), "WOB", 1000, 850, -1)
    {
        auto tog = [&](int p, const char* l, const char* tip) { Button* b = addToggle(p, l); b->small = true; b->tip = tip; return b; };
        auto dial = [&](int p, const char* l, bool bi = false, const char* tip = nullptr) { Dial* d = addDial(p, l, Dial::Small, bi); if (tip) d->tip = tip; return d; };
        octD = dial(kOctave, "OCT", true);
        tuneD = dial(kTune, "TUNE", true);
        shapeD = dial(kShape, "SHAPE", false, "Source spectrum: saw (0) -> square (50) -> triangle (100)");
        partsD = dial(kPartials, "PARTS", false, "How many partials (the first N harmonics)");
        tiltD = dial(kTilt, "TILT", true, "Lean the spectrum brighter (+) or darker (-)");
        oddD = dial(kOddEven, "ODD/EVEN", true, "+ takes out the even partials (hollow), - the odd ones above the fundamental");
        stretchD = dial(kStretch, "STRETCH", false, "Push the upper partials sharp: bars, bells, metal");

        modeSeg = addSegmented(kFilterMode, { "LOW-PASS", "BAND-PASS", "HIGH-PASS" });
        modeSeg->tip = "The spectral filter: a gain on each partial (24 dB low- or high-pass, or a band-pass)";
        cutD = addDial(kCutoff, "CUTOFF");
        resD = dial(kRes, "RES");
        envD = dial(kFilterEnv, "ENV", true, "Filter envelope, in octaves (it decays from this to 0)");
        fdecD = dial(kFilterDecay, "DECAY");
        keyD = dial(kKeyTrack, "KEY");

        vowelD = dial(kVowel, "VOWEL", false, "a - e - i - o - u");
        formD = dial(kFormant, "FORMANT", false, "How much the two vowel resonances shape the spectrum");
        combFD = dial(kCombFreq, "COMB F", false, "The comb's spacing");
        combD = dial(kComb, "COMB", false, "A cosine comb across the partials: hollow, phasey");
        detD = dial(kDetune, "DETUNE", false, "Reese: a second set of partials, detuned (cents), crossed in stereo");
        widthD = dial(kWidth, "WIDTH", false, "Upper partials alternate left and right");
        subD = dial(kSub, "SUB", false, "A sine one or two octaves down");
        subOct = addSegmented(kSubOct, { "-1", "-2" });
        subOct->tip = "Sub octave";

        shapeSeg = addSegmented(kWobShape, { "SINE", "TRI", "DOWN", "UP", "SQR", "STEP" });
        shapeSeg->tip = "Wobble shape: sine, triangle, saw down, saw up, square, random steps";
        syncB = tog(kWobSync, "SYNC", "On: a note division of the host tempo · off: free, in hertz");
        retrigB = tog(kWobRetrig, "RETRIG", "On: restarts with each note, at its start phase · off: a synced wobble follows the song");
        divD = dial(kWobDiv, "DIV");
        rateD = dial(kWobRate, "RATE");
        phaseD = dial(kWobPhase, "PHASE", false, "Where the wobble starts on each note (with retrigger)");
        smoothD = dial(kWobSmooth, "SMOOTH", false, "Rounds off the square and the steps");
        delayD = dial(kWobDelay, "DELAY", false, "No wobble for this long after each note");
        fadeD = dial(kWobFade, "FADE", false, "Then its depth rises over this long");
        depthD = dial(kWobDepth, "DEPTH");
        wheelD = dial(kWheel, "WHEEL", false, "The mod wheel adds this much depth");

        toCut = dial(kWobCut, "CUTOFF", true, "Wobble to cutoff, in octaves");
        toVowel = dial(kWobVowel, "VOWEL", true);
        toComb = dial(kWobComb, "COMB", true);
        toAmp = dial(kWobAmp, "LEVEL", false, "Wobble to level (a tremolo, or gated chops with the square)");
        toPitch = dial(kWobPitch, "PITCH", true, "Wobble to pitch, in semitones");

        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) envAD[k] = dial(kAttack + k, adsr[k]);
        glideD = dial(kGlide, "GLIDE");
        driveD = dial(kDrive, "DRIVE");
        volD = dial(kVolume, "VOLUME", true);
        legatoB = tog(kLegato, "LEGATO", "Overlapping notes slide (with glide) without restarting the envelope or the wobble");

        shapeBD = dial(kShapeB, "SHAPE B", false, "Spectrum B: saw (0) -> square (50) -> triangle (100)");
        tiltBD = dial(kTiltB, "TILT B", true, "Spectrum B leaned brighter (+) or darker (-)");
        morphD = dial(kMorph, "MORPH", false, "From spectrum A (0) to spectrum B (100)");
        toMorph = dial(kWobMorph, "TO MORPH", true, "Wobble to morph");
        inharmD = dial(kInharm, "INHARM", false, "Bends the partial series sharp by a power law: bells, gongs, glass");
        sprayD = dial(kSpray, "SPRAY", false, "Every partial detuned by its own fixed amount: a dense, chorused body");
        driftD = dial(kDrift, "DRIFT", false, "Each partial's pitch and level wander on their own: the spectrum lives");
        driftRD = dial(kDriftRate, "RATE", false, "How fast the drift wanders");
        keyboard.lowNote = 24;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginWob*>(pluginPtr())) plug = p;
    }
    ~UIWob() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOctave) { if (std::fabs(v) < 0.5f) return "0"; std::snprintf(b, sizeof b, "%+.0f", v); return b; }
        if (i == kTune || i == kWobPitch) { if (std::fabs(v) < 0.005f) return "0 st"; std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.01f ? "%+.0f st" : "%+.2f st", v); return b; }
        if (i == kPartials) { std::snprintf(b, sizeof b, "%.0f", v); return b; }
        if (i == kFilterEnv || i == kWobCut) { if (std::fabs(v) < 0.005f) return "0 oct"; std::snprintf(b, sizeof b, "%+.1f oct", v); return b; }
        if (i == kDetune) { if (v < 0.5f) return "OFF"; std::snprintf(b, sizeof b, "%.0f ct", v); return b; }
        if (i == kCombFreq) { const double hz = Engine::combHz(v * 0.01); if (hz >= 1000) std::snprintf(b, sizeof b, "%.1f kHz", hz / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", hz); return b; }
        if (i == kVowel) { static const char* const vw = "aeiou"; const float p = pump::clampv(v * 0.04f, 0.0f, 4.0f); const int a = (int)std::floor(p + 0.001f); const float f = p - a;
            if (f < 0.12f || a >= 4) { std::snprintf(b, sizeof b, "%c", vw[std::min(a, 4)]); return b; } if (f > 0.88f) { std::snprintf(b, sizeof b, "%c", vw[a + 1]); return b; }
            std::snprintf(b, sizeof b, "%c-%c", vw[a], vw[a + 1]); return b; }
        if ((i == kGlide || i == kWobDelay || i == kWobFade) && v < 0.5f) return "OFF";
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 166, hs = Dial::heightFor(Dial::Small);
        scopeR = Rect(14, top, 220, sh);
        specR = Rect(246, top, 460, sh);
        wobR = Rect(718, top, 240, sh);
        meterL = Rect(966, top, 8, sh - 12); meterR = Rect(977, top, 8, sh - 12);

        const float y1 = 238, ph = 196;
        srcPanel = Rect(14, y1, 300, ph);
        Dial* sa[4] = { octD, tuneD, shapeD, partsD };
        for (int k = 0; k < 4; ++k) sa[k]->r = Rect(20 + k * 72, y1 + 26, 72, hs);
        Dial* sb[3] = { tiltD, oddD, stretchD };
        for (int k = 0; k < 3; ++k) sb[k]->r = Rect(56 + k * 72, y1 + 112, 72, hs);
        filtPanel = Rect(326, y1, 300, ph);
        modeSeg->r = Rect(336, y1 + 28, 280, 22);
        cutD->r = Rect(334, y1 + 78, 84, Dial::heightFor(Dial::Medium));
        resD->r = Rect(424, y1 + 56, 64, hs); envD->r = Rect(492, y1 + 56, 64, hs);
        fdecD->r = Rect(424, y1 + 124, 64, hs); keyD->r = Rect(492, y1 + 124, 64, hs);
        colPanel = Rect(638, y1, 348, ph);
        Dial* ca[4] = { vowelD, formD, combFD, combD };
        for (int k = 0; k < 4; ++k) ca[k]->r = Rect(646 + k * 84, y1 + 26, 80, hs);
        Dial* cb[3] = { detD, widthD, subD };
        for (int k = 0; k < 3; ++k) cb[k]->r = Rect(646 + k * 84, y1 + 112, 80, hs);
        subOct->r = Rect(904, y1 + 136, 72, 22);

        const float yM = 446, phM = 106;
        morphPanel = Rect(14, yM, 486, phM);
        Dial* ma[4] = { shapeBD, tiltBD, morphD, toMorph };
        for (int k = 0; k < 4; ++k) ma[k]->r = Rect(30 + k * 116, yM + 30, 116, hs);
        motionPanel = Rect(512, yM, 474, phM);
        Dial* mb[4] = { inharmD, sprayD, driftD, driftRD };
        for (int k = 0; k < 4; ++k) mb[k]->r = Rect(526 + k * 114, yM + 30, 114, hs);
        const float y2 = 564, ph2 = 192;
        wobPanel = Rect(14, y2, 420, ph2);
        shapeSeg->r = Rect(24, y2 + 28, 260, 22);
        syncB->r = Rect(292, y2 + 28, 64, 22); retrigB->r = Rect(360, y2 + 28, 64, 22);
        Dial* wd[7] = { divD, phaseD, smoothD, delayD, fadeD, depthD, wheelD };
        for (int k = 0; k < 7; ++k) wd[k]->r = Rect(20 + k * 58, y2 + 66, 58, hs);
        rateD->r = divD->r;
        toPanel = Rect(446, y2, 270, ph2);
        Dial* td[5] = { toCut, toVowel, toComb, toAmp, toPitch };
        for (int k = 0; k < 5; ++k) td[k]->r = Rect(452 + k * 52, y2 + 66, 52, hs);
        ampPanel = Rect(728, y2, 258, ph2);
        for (int k = 0; k < 4; ++k) envAD[k]->r = Rect(734 + k * 62, y2 + 26, 62, hs);
        glideD->r = Rect(734, y2 + 112, 62, hs); driveD->r = Rect(796, y2 + 112, 62, hs); volD->r = Rect(858, y2 + 112, 62, hs);
        legatoB->r = Rect(922, y2 + 136, 58, 22);
        keyboard.r = Rect(14, 768, 966, 56);
    }

    void tick(float dt) override
    {
        const bool sync = value(kWobSync) >= 0.5f;
        divD->visible = sync; rateD->visible = !sync;
        phaseD->enabled = value(kWobRetrig) >= 0.5f;
        smoothD->enabled = true;
        subOct->enabled = value(kSub) > 0.5f;
        vowelD->enabled = value(kFormant) > 0.5f || std::fabs(value(kWobVowel)) > 0.5f;
        combFD->enabled = value(kComb) > 0.5f;
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        sounding = plug->engine.soundingNow();
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
        // the partials, eased so the bars move smoothly
        const double f0 = sounding ? plug->engine.fundamentalNow() : pump::synth::midiToHz(36.0 + 12.0 * value(kOctave) + value(kTune));
        const double fc = sounding ? plug->engine.cutoffNow() : previewCutoff(f0);
        const float k = std::min(1.0f, dt * 24.0f);
        for (int i = 1; i <= kMaxPartials; ++i)
        {
            src[i - 1] = plug->engine.sourceLevel(i);
            const float want = sounding ? plug->engine.partialLevel(i) : (float)(src[i - 1] * previewGain(i * f0 * (1.0 + value(kStretch) * 0.01 * 0.0015 * i * i), fc));
            out[i - 1] += (want - out[i - 1]) * k;
        }
        shownF0 = (float)f0; shownCut += ((float)fc - shownCut) * k;
        wobPh = plug->engine.wobblePhase(); wobVal = plug->engine.wobbleNow();
    }
    double previewCutoff(double f0) const
    {
        return pump::clampv(value(kCutoff) * std::pow(f0 / 65.406, value(kKeyTrack) * 0.01), 20.0, 22000.0);
    }
    double previewGain(double fk, double fc) const
    {
        double g = Engine::filterGain((int)value(kFilterMode), fk, fc, value(kRes) * 0.01) * Engine::formantGain(fk, value(kVowel) * 0.01, value(kFormant) * 0.01);
        const double c = value(kComb) * 0.01;
        if (c > 0.0) g *= 1.0 - c * (0.5 + 0.5 * std::cos(pump::kTwoPi * fk / Engine::combHz(value(kCombFreq) * 0.01)));
        return g * (fk < 21000.0 ? 1.0 : 0.0);
    }

    void drawBody() override
    {
        drawScope();
        drawPartials();
        drawWobble();
        drawOutMeter(outMeter, meterL, meterR);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(srcPanel, "SOURCE"); panel(filtPanel, "FILTER"); panel(colPanel, "FORMANT · COMB · REESE · SUB"); panel(morphPanel, "SPECTRUM B · MORPH"); panel(motionPanel, "MOTION");
        panel(wobPanel, "WOBBLE"); panel(toPanel, "WOBBLE TO"); panel(ampPanel, "AMP");
        // the wobble's speed, in words
        char b[96];
        const double bpm = 120.0;
        if (value(kWobSync) >= 0.5f)
        {
            const int d = pump::clampv((int)value(kWobDiv), 0, pump::kNumNotes - 1);
            std::snprintf(b, sizeof b, "%s = %.2f HZ AT 120 BPM · %s", pump::kNoteNames[d], bpm / 60.0 / pump::noteQuarters(d),
                value(kWobRetrig) >= 0.5f ? "RESTARTS EACH NOTE" : "FOLLOWS THE SONG");
        }
        else std::snprintf(b, sizeof b, "FREE · %s", value(kWobRetrig) >= 0.5f ? "RESTARTS EACH NOTE" : "RUNS ON");
        textAt(wobPanel.x + 12, wobPanel.b() - 16, b, kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        const bool any = std::fabs(value(kWobCut)) > 0.01f || std::fabs(value(kWobVowel)) > 0.5f || std::fabs(value(kWobComb)) > 0.5f || value(kWobAmp) > 0.5f || std::fabs(value(kWobPitch)) > 0.01f;
        textAt(toPanel.cx(), toPanel.b() - 16, any ? "FROM THE KNOB'S SETTING, UP" : "OFF (TURN ONE UP)", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.6f);
        caption(subOct->r.x, subOct->r.y - 10, "SUB OCT");
        keyboard.draw(*this);
    }

    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        // two periods of the note (low bass notes are long)
        const int win = pump::clampv((int)(2.0 * getSampleRate() / std::max(20.0f, shownF0)), 512, kScopeN / 2 - 8);
        int start = kScopeN - win - 1;
        for (int i = start; i > start - kScopeN / 2 + 8 && i > 1; --i) if (scope[i - 1] < 0.0f && scope[i] >= 0.0f) { start = i; break; }
        float mx = 1e-3f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(scope[start + i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < win; i += std::max(1, win / 400))
        {
            const float x = p.x + p.w * i / (win - 1), y = p.cy() - scope[start + i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        restore();
    }
    void drawPartials()
    {
        const Rect& r = specR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "PARTIALS");
        const int m = pump::clampv((int)value(kFilterMode), 0, kNumModes - 1);
        static const char* const mn[3] = { "LOW-PASS", "BAND-PASS", "HIGH-PASS" };
        textAt(r.r() - 10, r.y + 14, mn[m], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 44);
        auto yOf = [&](double lv) { const double db = 20.0 * std::log10(std::max(1e-6, lv)); return (float)(p.y + (12.0 - pump::clampv(db, -60.0, 12.0)) / 72.0 * p.h); };
        for (int db = 0; db >= -48; db -= 24) { const float y = yOf(std::pow(10.0, db / 20.0)); line(p.x, y, p.r(), y, withAlpha2(theme.ink, db == 0 ? 0.25f : 0.12f), 1.0f); }
        const int np = pump::clampv((int)value(kPartials), 8, kMaxPartials);
        const float bw = p.w / np;
        const Color barC = pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.35f);
        for (int i = 0; i < np; ++i)
        {
            const float x = p.x + i * bw + (bw > 3.0f ? 1.0f : 0.0f), w = std::max(1.0f, bw - (bw > 3.0f ? 2.0f : 0.0f));
            if (src[i] > 1e-4f)
            {
                const float ys = yOf(src[i]);
                strokeRect(Rect(x, ys, w, p.b() - ys), withAlpha2(theme.ink, 0.35f), 1.0f);
            }
            if (out[i] > 1e-4f) { const float yo = yOf(out[i]); fillRect(Rect(x, yo, w, p.b() - yo), barC); }
        }
        // the filter's curve across the partials
        beginPath();
        const double st = value(kStretch) * 0.01 * 0.0015;
        for (int i = 0; i <= 128; ++i)
        {
            const double kk = 1.0 + (np - 1) * i / 128.0;
            const double fk = shownF0 * kk * (1.0 + st * kk * kk);
            const double g = Engine::filterGain(m, fk, std::max(20.0f, shownCut), value(kRes) * 0.01);
            const float x = p.x + (float)(kk - 0.5) * bw, y = yOf(g);
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.6f); lineJoin(ROUND); stroke();
        char b[64];
        const double fc = shownCut;
        if (fc >= 1000) std::snprintf(b, sizeof b, "CUT %.2f kHz", fc / 1000); else std::snprintf(b, sizeof b, "CUT %.0f Hz", fc);
        textAt(r.x + 10, r.b() - 9, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, sounding ? "%.1f Hz" : "%.1f Hz (C2 PREVIEW)", (double)shownF0);
        textAt(r.r() - 10, r.b() - 9, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    double shapeAt(double ph, int seed) const
    {
        switch ((int)value(kWobShape))
        {
        case kTriangle: return ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
        case kSawDown: return 1.0 - ph;
        case kSawUp: return ph;
        case kSquare: return ph < 0.5 ? 1.0 : 0.0;
        case kSteps: { pump::fx::Rng rr((uint32_t)(seed * 7919 + 13)); return rr.uni(); }
        default: return 0.5 - 0.5 * std::cos(pump::kTwoPi * ph);
        }
    }
    void drawWobble()
    {
        const Rect& r = wobR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "WOBBLE");
        char b[48];
        if (value(kWobSync) >= 0.5f) std::snprintf(b, sizeof b, "%s", pump::kNoteNames[pump::clampv((int)value(kWobDiv), 0, pump::kNumNotes - 1)]);
        else std::snprintf(b, sizeof b, "%.2f Hz", (double)value(kWobRate));
        textAt(r.r() - 10, r.y + 14, b, kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 12, r.y + 30, r.w - 24, r.h - 52);
        line(p.x, p.b(), p.r(), p.b(), withAlpha2(theme.ink, 0.25f), 1.0f);
        // two cycles, smoothed the way the engine smooths it
        const double depth = value(kWobDepth) * 0.01, sm = value(kWobSmooth) * 0.01;
        const int steps = 240;
        double y = shapeAt(0.0, 0);
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            for (int i = 0; i <= steps; ++i)
            {
                const double t = 2.0 * i / steps, ph = t - std::floor(t);
                const double raw = shapeAt(ph, (int)std::floor(t));
                const double c = sm > 0.001 ? 1.0 - std::exp(-(2.0 / steps) / (sm * 0.4)) : 1.0;
                y += (raw - y) * c;
                if (pass == 0) continue;
                const float x = p.x + p.w * (float)(t / 2.0), yy = p.b() - (float)(y * std::max(0.08, depth)) * p.h;
                if (i == 0) moveTo(x, yy); else lineTo(x, yy);
            }
            if (pass == 1) { strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke(); }
        }
        // where it is now
        const float dx = p.x + p.w * 0.5f * wobPh, dy = p.b() - wobVal * p.h;
        line(dx, p.y, dx, p.b(), withAlpha2(theme.neon, 0.6f), 1.0f);
        beginPath(); circle(dx, dy, 4.5f); fillColor(theme.neon); fill();
        std::snprintf(b, sizeof b, "NOW %3.0f %%", wobVal * 100.0);
        textAt(r.x + 10, r.b() - 10, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "DEPTH %.0f %%", depth * 100.0);
        textAt(r.r() - 10, r.b() - 10, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (specR.contains(x, y))
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
        setParamNorm(kCutoff, dCut + (x - dx) / specR.w * f);
        setParamNorm(kRes, dRes + (dy - y) / specR.h * f);
        setStatus(paramStatus(kCutoff) + "    " + paramStatus(kRes));
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; return; }
        if (dragging) { dragging = false; endEdit(kRes); endEdit(kCutoff); }
    }
    void bodyHover(float x, float y) override
    {
        if (specR.contains(x, y)) setStatus("Each bar is a partial: outline = the source, filled = what's left. Drag: left / right = cutoff, up / down = resonance");
        else if (wobR.contains(x, y)) setStatus("The wobble's shape (with its smoothing and depth); the dot is where it is now");
        else if (keyboard.r.contains(x, y)) setStatus("Play: velocity follows where you strike the key; drag across keys");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Drag the partials screen", "left / right = cutoff, up / down = resonance" }, { "Keyboard", "click to play, drag across" },
                 { "Mod wheel", "adds wobble depth (WHEEL)" } };
    }
    std::string footerInfo() override
    {
        static const char* const mn[3] = { "LOW-PASS", "BAND-PASS", "HIGH-PASS" };
        char b[96];
        if (value(kWobSync) >= 0.5f) std::snprintf(b, sizeof b, "%s   WOBBLE %s", mn[pump::clampv((int)value(kFilterMode), 0, 2)], pump::kNoteNames[pump::clampv((int)value(kWobDiv), 0, pump::kNumNotes - 1)]);
        else std::snprintf(b, sizeof b, "%s   WOBBLE %.2f HZ", mn[pump::clampv((int)value(kFilterMode), 0, 2)], (double)value(kWobRate));
        return b;
    }

private:
    static constexpr int kScopeN = 8000;   // the ring holds 8192
    PluginWob* plug = nullptr;
    Rect scopeR, specR, wobR, meterL, meterR, srcPanel, filtPanel, colPanel, wobPanel, toPanel, ampPanel, morphPanel, motionPanel;
    Dial *octD, *tuneD, *shapeD, *partsD, *tiltD, *oddD, *stretchD, *cutD, *resD, *envD, *fdecD, *keyD;
    Dial *vowelD, *formD, *combFD, *combD, *detD, *widthD, *subD;
    Dial *divD, *rateD, *phaseD, *smoothD, *delayD, *fadeD, *depthD, *wheelD, *toCut, *toVowel, *toComb, *toAmp, *toPitch;
    Dial *envAD[4], *glideD, *driveD, *volD;
    Dial *shapeBD, *tiltBD, *morphD, *toMorph, *inharmD, *sprayD, *driftD, *driftRD;
    Segmented *modeSeg, *subOct, *shapeSeg;
    Button *syncB, *retrigB, *legatoB;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false, sounding = false;
    float dx = 0, dy = 0, dCut = 0, dRes = 0, scope[kScopeN] = {}, scale = 1.0f;
    float src[kMaxPartials] = {}, out[kMaxPartials] = {}, shownF0 = 65.4f, shownCut = 400.0f, wobPh = 0.0f, wobVal = 0.0f;
    OutMeter outMeter;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIWob)
};

UI* createUI() { return new UIWob(); }

END_NAMESPACE_DISTRHO
