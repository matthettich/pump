// pump MASTER: interface. The display shows the song's learned balance (dim), the style's target (dotted) and the
// tone curve MASTER applies (boosts neon, cuts in theme.second) over the live output spectrum; the column beside it
// the loudness: the song as learned, the plan, the output now. Below: LEARN, STYLE and INTENSITY, then one panel per
// stage, each with its on/off key, its amount and a line or two on what it is doing to this song.
// The look is Thunder Plus's Simple rainbow: Simple grey, with every key and dial in its own colour, the hue stepping
// 30 degrees per column and per row (a diagonal rainbow), and the tone curve coloured by frequency.
#include "ui/PumpUI.hpp"
#include "PluginMaster.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::master;

class UIMaster : public PumpUI {
public:
    UIMaster() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::master(), "MASTER", 940, 560, kBypass)
    {
        if (auto* p = static_cast<PluginMaster*>(pluginPtr())) plug = p;

        learnBtn = addButton("LEARN", [this]() { toggleLearn(); }, [this]() { return learning; });
        learnBtn->tip = "Learn: play the song (or its loudest part and a quiet part), press LEARN, press again when done "
                        "(it also stops by itself when the song stops). L key";
        styleSeg = addSegmented(kStyle, { "BALANCED", "WARM", "OPEN", "PUNCH" });
        styleSeg->tip = "Style: the balance MASTER aims for. Balanced is the subtle reference; Warm darker, Open brighter "
                        "and a touch wider, Punch with glue compression";
        intDial = addDial(kIntensity, "INTENSITY", Dial::Large);
        intDial->tip = "Intensity: how much of everything (50 % = medium; 25 % light, 100 % twice medium)";
        cmpBtn = addToggle(kCompare, "COMPARE");
        cmpBtn->holdMomentary = true;
        cmpBtn->tip = "Compare: hear the original at the master's loudness, so you judge the sound, not the level "
                      "(click = latch, click-and-hold = while held; C key)";

        struct StageDef { int on, amt; const char* name; const char* tip; };
        const StageDef sd[3] = {
            { kToneOn, kToneAmt, "TONE", "Tone: broad EQ toward the style's balance (about a third of the way at medium)" },
            { kDynOn, kDynAmt, "DYNAMICS", "Dynamics: lifts quiet sections slowly (Punch also glues the loud parts)" },
            { kStereoOn, kStereoAmt, "STEREO", "Stereo: tightens a wide low end; Open and Punch nudge the width" },
        };
        for (int i = 0; i < 3; ++i)
        {
            stageBtn[i] = addToggle(sd[i].on, sd[i].name);
            stageBtn[i]->tip = sd[i].tip;
            stageAmt[i] = addDial(sd[i].amt, "AMOUNT", Dial::Small);
            stageAmt[i]->tip = std::string(sd[i].name) + " amount (on top of INTENSITY; 100 % = as the style has it)";
        }
        stageBtn[3] = addToggle(kLoudOn, "LOUDNESS");
        stageBtn[3]->tip = "Loudness: makeup gain and a true-peak limiter that only touches the peaks";
        modeSeg = addSegmented(kLoudMode, { "GENTLE", "TARGET" });
        modeSeg->tip = "Gentle: only as loud as the peaks allow (a couple of dB of peak limiting at most), never past "
                       "TARGET. Target: to TARGET, however much limiting it takes";
        targetDial = addDial(kTarget, "TARGET", Dial::Small);
        targetDial->tip = "Loudness target (LUFS, integrated). In Gentle mode the most it will reach";
        ceilDial = addDial(kCeiling, "CEILING", Dial::Small);
        ceilDial->tip = "True-peak ceiling (dBTP). -1 is safe for streaming encodes; 0 gives about 1 dB more level";
        targetDial->tabGroup = ceilDial->tabGroup = 2;

        // ADVANCED: offsets on top of what auto chose, and the details
        advBtn = addButton("ADVANCED", [this]() { advanced = !advanced; layout(); saveUi(); repaint(); }, [this]() { return advanced; });
        advBtn->tip = "Advanced: tweak what auto set (every value is an offset on top of auto: zero = auto), plus eight free EQ bands. A key";
        resetTrimBtn = addButton("RESET TRIMS", [this]() {
            beginGroup();
            for (int i = 0; i < kEq; ++i) setParamGesture(kToneTrim0 + i, 0.0f);
            endGroup();
            setStatus("Tone back to what auto set");
        });
        resetTrimBtn->small = true;
        resetTrimBtn->tip = "Put the nine auto tone points back where auto set them";
        clearBandsBtn = addButton("CLEAR BANDS", [this]() {
            beginGroup();
            for (int b = 0; b < kNumUserBands; ++b) setParamGesture(bandParam(b, kBOn), 0.0f);
            endGroup();
            setStatus("Free bands off");
        });
        clearBandsBtn->small = true;
        clearBandsBtn->tip = "Switch every free EQ band off";
        auto adv = [this](int prm, const char* label, bool bip, const char* tip) {
            Dial* d = addDial(prm, label, Dial::Small, bip); d->tip = tip; advDials.push_back(d); return d;
        };
        liftD = adv(kLiftTrim, "LIFT", true, "Leveler lift: dB added to (or taken from) what auto lifts quiet sections by");
        liftAtD = adv(kLiftAt, "LIFT AT", true, "Where lifting starts: + = sections closer to the song's own level get lifted too");
        ratioD = adv(kRatioTrim, "RATIO", true, "Glue ratio added to auto's (Balanced starts at 1:1, no compression)");
        gThrD = adv(kGlueThrTrim, "THRESH", true, "Glue threshold, dB from auto's (- = more compression)");
        atkD = adv(kGlueAttack, "ATTACK", false, "Glue attack");
        relD = adv(kGlueRelease, "RELEASE", false, "Glue release");
        monoD = adv(kMonoFreq, "MONO BELOW", false, "Low-mono crossover: the side below it is what LOW MONO folds");
        lowMonoD = adv(kLowMonoTrim, "LOW MONO", true, "Fold the lows' side toward mono, added to auto's (+100 % = fully mono below the crossover)");
        widthD = adv(kWidthTrim, "WIDTH", true, "Side level above the crossover, dB on top of auto's");
        gainD = adv(kGainTrim, "GAIN", true, "Gain into the limiter on top of auto's makeup (+ = louder, more limiting: the limiter's threshold)");
        limRelD = adv(kLimRelease, "RELEASE", false, "Limiter release");
        limStyleM = addMenu(kLimStyle);
        limStyleM->tip = "Limiter style (LIMIT's styles). Dynamic: brief peak-only limiting, the default";
        advBtn->small = false;

        // the rainbow: hue = (column + row) x 30 + 10 degrees
        auto hue = [](int col, int row) { return (float)(((col + row) % 12) * 30 + 10); };
        learnBtn->tint = hue(0, 0); intDial->tint = hue(1, 0); cmpBtn->tint = hue(0, 1); styleSeg->tint = hue(0, 2);
        for (int i = 0; i < 4; ++i) stageBtn[i]->tint = stageHue[i] = hue(2 + i, 0);
        for (int i = 0; i < 3; ++i) stageAmt[i]->tint = hue(2 + i, 1);
        modeSeg->tint = hue(5, 1); targetDial->tint = hue(5, 2); ceilDial->tint = hue(6, 2);
        advBtn->tint = hue(0, 2); styleSeg->tint = hue(0, 3);
        resetTrimBtn->tint = hue(2, 2); clearBandsBtn->tint = hue(3, 2);
        liftD->tint = hue(3, 1); liftAtD->tint = hue(4, 1); ratioD->tint = hue(3, 2); gThrD->tint = hue(4, 2); atkD->tint = hue(3, 3); relD->tint = hue(4, 3);
        monoD->tint = hue(5, 1); lowMonoD->tint = hue(6, 1); widthD->tint = hue(5, 2);
        gainD->tint = hue(6, 3); limRelD->tint = hue(7, 3); limStyleM->tint = hue(6, 4);

        // screenshots (standalone test builds only): open the advanced view with two free bands
        if (pump::testEnv("PUMP_TEST_ADV"))
        {
            advanced = true;
            setParam(bandParam(0, kBOn), 1.0f); setParam(bandParam(0, kBFreq), 3200.0f); setParam(bandParam(0, kBGain), 2.5f); setParam(bandParam(0, kBQ), 1.4f);
            setParam(bandParam(1, kBOn), 1.0f); setParam(bandParam(1, kBShape), (float)kULowCut); setParam(bandParam(1, kBFreq), 28.0f);
            setParam(kToneTrim0 + 2, -1.5f);
        }
        setupAnalyzers(plug && plug->getSampleRate() > 0 ? plug->getSampleRate() : (sampleRate() > 0 ? sampleRate() : 48000.0));
    }

protected:
    static constexpr double kLo = 20.0, kHi = 20000.0;

    void setupAnalyzers(double sr) { anaRate = sr; post.setup(sr); post.setRange(kLo, kHi); }
    void onSampleRateChanged(double sr) override { if (sr > 0.0 && sr != anaRate) setupAnalyzers(sr); }

    std::string formatParam(int i, float v) const override
    {
        char b[48];
        if (i == kTarget) { std::snprintf(b, sizeof b, "%.1f LUFS", v); return b; }
        if (i == kCeiling) { std::snprintf(b, sizeof b, "%.1f dBTP", v); return b; }
        if (i == kIntensity)
        {
            const char* w = v < 18.0f ? "SUBTLE" : v < 38.0f ? "LIGHT" : v < 63.0f ? "MEDIUM" : v < 85.0f ? "STRONG" : "MAX";
            std::snprintf(b, sizeof b, "%.0f %% %s", v, w);
            return b;
        }
        if (i == kRatioTrim) { std::snprintf(b, sizeof b, "%+.2f", v); return b; }
        if (i == kLiftAt) { std::snprintf(b, sizeof b, "%+.1f LU", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void toggleLearn()
    {
        if (!plug) return;
        if (learning) { plug->learnFinish(); setStatus("Learned: the master follows this song now"); }
        else { plug->learnStart(); learning = true; setStatus("Learning: play the song; press LEARN again when it has played (or stop the song)"); }
        repaint();
    }

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        const float dh = advanced ? 200.0f : 262.0f;
        info = Rect(W - 14 - 208, 58, 208, dh);
        display = Rect(14, 58, info.x - 12 - 14, dh);
        const float y0 = display.b() + 12, ph = H - 24 - 12 - y0;
        mainP = Rect(14, y0, 262, ph);
        const float sx = mainP.r() + 10, sw = (W - 14 - sx - 3 * 10) / 4.0f;
        for (int i = 0; i < 4; ++i) stageP[i] = Rect(sx + i * (sw + 10), y0, sw, ph);

        learnBtn->r = Rect(mainP.x + 14, y0 + 30, 110, 40);
        cmpBtn->r = Rect(mainP.x + 14, y0 + 78, 110, 24);
        advBtn->r = Rect(mainP.x + 14, y0 + 108, 110, 24);
        const float lh = Dial::heightFor(Dial::Large);
        intDial->r = Rect(mainP.x + 140, y0 + 26, 108, lh);
        styleSeg->r = Rect(mainP.x + 14, mainP.b() - 38, mainP.w - 28, 26);

        const float sh = Dial::heightFor(Dial::Small);
        for (int i = 0; i < 4; ++i)
        {
            const Rect& p = stageP[i];
            stageBtn[i]->r = Rect(p.x + 10, p.y + 10, p.w - 20, 26);
            if (i < 3) stageAmt[i]->r = Rect(p.cx() - 36, p.y + 46, 72, sh);
        }
        const Rect& lp = stageP[3];
        modeSeg->r = Rect(lp.x + 10, lp.y + 44, lp.w - 20, 22);
        targetDial->r = Rect(lp.x + 6, lp.y + 72, lp.w * 0.5f - 6, sh);
        ceilDial->r = Rect(lp.cx(), lp.y + 72, lp.w * 0.5f - 6, sh);

        // ADVANCED: two columns of small dials under each stage's key
        for (Control* c : advDials) c->visible = advanced;
        limStyleM->visible = resetTrimBtn->visible = clearBandsBtn->visible = advanced;
        for (int i = 0; i < 3; ++i) stageAmt[i]->visible = !(advanced && i == 1);
        auto cell = [&](int stage, int col, int row) {
            const Rect& p = stageP[stage];
            return Rect(col == 0 ? p.x + 6 : p.cx(), p.y + 44 + row * (sh + 2), p.w * 0.5f - 6, sh);
        };
        if (advanced)
        {
            const Rect& tp = stageP[0];
            stageAmt[0]->r = Rect(tp.cx() - 36, tp.y + 44, 72, sh);
            resetTrimBtn->r = Rect(tp.x + 10, tp.y + 44 + sh + 6, tp.w - 20, 22);
            clearBandsBtn->r = Rect(tp.x + 10, tp.y + 44 + sh + 34, tp.w - 20, 22);
            liftD->r = cell(1, 0, 0); liftAtD->r = cell(1, 1, 0); ratioD->r = cell(1, 0, 1); gThrD->r = cell(1, 1, 1);
            atkD->r = cell(1, 0, 2); relD->r = cell(1, 1, 2);
            stageAmt[2]->r = cell(2, 0, 0); monoD->r = cell(2, 1, 0); lowMonoD->r = cell(2, 0, 1); widthD->r = cell(2, 1, 1);
            gainD->r = Rect(lp.x + 6, lp.y + 72 + sh + 2, lp.w * 0.5f - 6, sh);
            limRelD->r = Rect(lp.cx(), lp.y + 72 + sh + 2, lp.w * 0.5f - 6, sh);
            limStyleM->r = Rect(lp.x + 10, lp.b() - 32, lp.w - 20, 22);
        }
    }

    // ---------------------------------------------------------------- helpers
    float xOfF(double f) const { return freqToX(display, f, kLo, kHi); }
    float yOfDb(float db) const { return display.cy() - db / dbRange * (display.h * 0.5f - 16.0f); }
    float yOfSpec(float db) const { return display.y + (specTop - db) / (specTop - specBottom) * display.h; }
    float curveAt(double f) const
    {
        const double x = std::log(f / 20.0) / std::log(1000.0) * (kCurvePts - 1);
        const int k = pump::clampv((int)x, 0, kCurvePts - 2);
        const double t = pump::clampv(x - k, 0.0, 1.0);
        return (float)(curve[k] * (1.0 - t) + curve[k + 1] * t);
    }
    float bandsAt(double f) const
    {
        const double x = std::log(f / 20.0) / std::log(1000.0) * (kCurvePts - 1);
        const int k = pump::clampv((int)x, 0, kCurvePts - 2);
        const double t = pump::clampv(x - k, 0.0, 1.0);
        return (float)(bandsCurve[k] * (1.0 - t) + bandsCurve[k + 1] * t);
    }
    float totalAt(double f) const { return curveAt(f) + bandsAt(f); }
    static double autoPointFreq(int i) { static const double f[kEq] = { 60, 125, 250, 500, 1000, 2000, 4000, 8000, 12000 }; return f[i]; }
    float pxPerDb() const { return (display.h * 0.5f - 16.0f) / dbRange; }
    bool bandIsCut(int b) const { const int sh = (int)value(bandParam(b, kBShape)); return sh == kULowCut || sh == kUHighCut; }
    float bandY(int b) const { return yOfDb(bandIsCut(b) ? 0.0f : value(bandParam(b, kBGain))); }
    static Color pointColour(double f, bool dark, float a = 1.0f)
    {
        const float h = 10.0f + 330.0f * (float)(std::log(f / 20.0) / std::log(1000.0));
        return pump::oklchColor(dark ? 0.78f : 0.55f, 0.16f, h, a);
    }
    static std::string dbs(float v, const char* fmt = "%+.1f dB") { char b[32]; std::snprintf(b, sizeof b, fmt, v); return b; }

    void tick(float dt) override
    {
        if (!plug) return;
        const double srNow = plug->getSampleRate();
        if (srNow > 0.0 && srNow != anaRate) setupAnalyzers(srNow);
        outMeter.tick(plug->outPeak, dt);
        post.update(&plug->postRing, nullptr, dt);
        Engine& e = plug->engine;
        learning = e.uiLearning.load(std::memory_order_relaxed);
        learnSec = e.uiLearnSeconds.load(std::memory_order_relaxed);
        Analysis a;
        if (e.published.load(a)) ana = a;
        planValid = e.uiPlanValid.load(std::memory_order_relaxed);
        const float k = 1.0f - std::exp(-dt / 0.12f);
        for (int i = 0; i < kCurvePts; ++i)
        {
            curve[i] += (e.uiCurve[i].load(std::memory_order_relaxed) - curve[i]) * k;
            bandsCurve[i] += (e.uiBandsCurve[i].load(std::memory_order_relaxed) - bandsCurve[i]) * k;
        }
        for (int b = 0; b < kBands; ++b) { target[b] = e.uiTarget[b].load(std::memory_order_relaxed); measured[b] = e.uiMeasured[b].load(std::memory_order_relaxed); }
        for (int i = 0; i < kEq; ++i) { eqGain[i] = e.uiEqGain[i].load(std::memory_order_relaxed); autoEq[i] = e.uiAutoEq[i].load(std::memory_order_relaxed); }
        ratio = e.uiRatio.load(); thr = e.uiThr.load(); liftMax = e.uiLiftMax.load(); lift = e.uiLift.load();
        sideLo = e.uiSideLoDb.load(); sideHi = e.uiSideHiDb.load(); makeup = e.uiMakeup.load(); outPred = e.uiOutLufs.load();
        cmpDb = e.uiCompare.load(); peakOver = e.uiPeakOver.load(); limLoss = e.uiLimitLoss.load(); eqDelta = e.uiEqDelta.load();
        inST = e.uiInST.load(); outST = e.uiOutST.load();
        const float cg = e.compGR.exchange(0.0f), lg = e.limGR.exchange(0.0f);
        compGr = std::max(cg, compGr - 12.0f * dt);
        limGr = std::max(lg, limGr - 12.0f * dt);
        learnBtn->label = learning ? "STOP " + clock(learnSec) : (ana.valid ? "RE-LEARN" : "LEARN");
        // the spectrum scale follows the learned song
        if (ana.valid)
        {
            float mx = -200.0f;
            for (int b = 0; b < kBands; ++b) if (bandFreq(b) < 16000.0) mx = std::max(mx, measured[b]);
            specTop = mx + 6.0f; specBottom = mx - 30.0f;
        }
    }
    static std::string clock(float s)
    {
        const int t = (int)s;
        char b[16]; std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        return b;
    }

    // ---------------------------------------------------------------- drawing
    void drawBody() override
    {
        screenBox(display);
        save();
        scissor(display.x + 1, display.y + 1, display.w - 2, display.h - 2);
        for (int k = -2; k <= 2; ++k)
        {
            const float y = yOfDb(dbRange * k / 2.0f);
            line(display.x, y, display.r(), y, k == 0 ? withAlpha2(theme.ink, 0.28f) : theme.grid, 1.0f);
        }
        drawFreqGrid(display, kLo, kHi);
        // the output now (its own scale, faint)
        drawSpectrum(post, display, 6.0f, -84.0f, withAlpha2(theme.neon, theme.dark ? 0.12f : 0.18f),
                     withAlpha2(theme.ink, 0.18f), kLo, kHi, 1.0f);
        if (ana.valid && planValid)
        {
            // the song as learned and the style's target (spectrum scale, aligned in the mids)
            auto spec = [&](const float* v, const Color& c, float w, bool dots) {
                beginPath();
                bool first = true;
                for (int b = 1; b < kBands; ++b)
                {
                    const double f = bandFreq(b);
                    if (f > 0.47 * anaRate) break;
                    const float x = xOfF(f), y = std::max(display.y + 2, std::min(display.b() - 2, yOfSpec(v[b])));
                    if (first) { moveTo(x, y); first = false; } else lineTo(x, y);
                }
                strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
                if (dots)
                    for (int b = 2; b < kBands; b += 2)
                    {
                        const double f = bandFreq(b);
                        if (f > 0.47 * anaRate) break;
                        beginPath(); circle(xOfF(f), yOfSpec(v[b]), 2.0f); fillColor(c); fill();
                    }
            };
            spec(measured, withAlpha2(theme.ink, 0.55f), 1.6f, false);
            spec(target, withAlpha2(theme.second, 0.75f), 1.2f, true);

            // the tone curve
            const int NP = 200;
            float xs[NP], ys[NP];
            const float y0 = yOfDb(0.0f);
            for (int p = 0; p < NP; ++p)
            {
                xs[p] = display.x + display.w * p / (NP - 1);
                ys[p] = std::max(display.y + 2.0f, std::min(display.b() - 2.0f, yOfDb(totalAt(xToFreq(display, xs[p], kLo, kHi)))));
            }
            // coloured by frequency, round the wheel from the lows to the top; cuts in the lighter shade
            for (int p = 0; p + 1 < NP; ++p)
            {
                const float h = 10.0f + 330.0f * p / (NP - 1);
                const float ya = ys[p], yb = ys[p + 1];
                const bool boost = ya + yb < 2.0f * y0;
                beginPath();
                moveTo(xs[p], y0); lineTo(xs[p], ya); lineTo(xs[p + 1], yb); lineTo(xs[p + 1], y0); closePath();
                fillColor(boost ? pump::oklchColor(theme.dark ? 0.66f : 0.74f, 0.15f, h, theme.dark ? 0.75f : 0.85f)
                                : pump::oklchColor(theme.dark ? 0.45f : 0.87f, 0.08f, h, 0.85f));
                fill();
            }
            beginPath();
            for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
            strokeColor(theme.ink); strokeWidth(2.2f); lineJoin(ROUND); stroke();
            if (advanced) drawHandles();
            for (int k = -2; k <= 2; ++k)
            {
                if (k == 2) continue;
                char b[16]; std::snprintf(b, sizeof b, k == 0 ? "0" : "%+.0f", dbRange * k / 2.0f);
                textAt(display.r() - 8, yOfDb(dbRange * k / 2.0f) - 7, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
            }
            // legend
            float lx = display.x + 12;
            auto key = [&](const Color& c, const char* t, bool lineKey) {
                if (lineKey) line(lx, display.y + 14, lx + 14, display.y + 14, c, 1.8f);
                else { fillRect(Rect(lx, display.y + 9, 12, 10), c); strokeRect(Rect(lx, display.y + 9, 12, 10), theme.ink, 1.0f); }
                textAt(lx + 18, display.y + 14.5f, t, kLabel, 10.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
                lx += 18 + textWidth(t, kLabel, 10.5f, 1.0f) + 14;
            };
            key(pump::oklchColor(theme.dark ? 0.66f : 0.74f, 0.15f, 70.0f), "BOOST", false);
            key(pump::oklchColor(theme.dark ? 0.45f : 0.87f, 0.08f, 70.0f), "CUT", false);
            key(withAlpha2(theme.ink, 0.6f), "SONG", true);
            key(withAlpha2(theme.second, 0.8f), "TARGET", true);
        }
        // hover readout
        if (hoverX >= 0 && planValid && display.contains(hoverX, hoverY))
        {
            const double f = xToFreq(display, hoverX, kLo, kHi);
            const float g = curveAt(f);
            line(hoverX, display.y, hoverX, display.b(), withAlpha2(theme.ink, 0.35f), 1.0f);
            char b[64];
            if (f < 1000.0) std::snprintf(b, sizeof b, "%.0f Hz  %+.1f dB", f, g); else std::snprintf(b, sizeof b, "%.1f kHz  %+.1f dB", f / 1000.0, g);
            const float tw = textWidth(b, kMono, 11.0f) + 14.0f;
            const float yy = std::max(display.y + 30.0f, std::min(display.b() - 30.0f, yOfDb(g) - 26.0f));
            const Rect tb(std::max(display.x + 6, std::min(display.r() - tw - 6, hoverX + 10)), yy, tw, 22);
            fillRect(tb, theme.ink);
            textAt(tb.cx(), tb.cy(), b, kMono, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // banners
        auto banner = [&](const std::string& s, const Color& bgc, const Color& fg, float y) {
            const float tw = textWidth(s, kLabel, 12.0f, 1.0f) + 22.0f;
            const Rect lb(display.cx() - tw * 0.5f, y, tw, 24);
            fillRect(lb, bgc);
            textAt(lb.cx(), lb.cy(), s, kLabel, 12.0f, fg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        };
        if (learning) banner("LEARNING  " + clock(learnSec) + "  ·  PLAY THE SONG, THEN PRESS STOP", theme.ink, theme.on, display.cy() - 12);
        else if (!ana.valid) banner("PLAY THE SONG AND PRESS LEARN  ·  UNTIL THEN MASTER CHANGES NOTHING", theme.ink, theme.on, display.cy() - 12);
        if (value(kCompare) >= 0.5f)
        {
            char b[96]; std::snprintf(b, sizeof b, "COMPARE  ·  THE ORIGINAL, %+.1f dB TO MATCH THE MASTER'S LOUDNESS", cmpDb);
            banner(b, theme.second, theme.bg, display.b() - 36);
        }
        restore();

        drawInfo();

        strokeRect(mainP, theme.dim, 1.0f);
        caption(mainP.x + 14, mainP.y + 15, "MASTER");
        caption(styleSeg->r.x, styleSeg->r.y - 10, "STYLE");
        for (int i = 0; i < 4; ++i)
        {
            const bool on = value(i == 0 ? kToneOn : i == 1 ? kDynOn : i == 2 ? kStereoOn : kLoudOn) >= 0.5f;
            const Color c = stageDim(i);
            strokeRect(stageP[i], on ? c : withAlpha2(c, 0.45f), on ? 1.5f : 1.0f);
        }
        drawStageText();
    }

    // ADVANCED: auto's nine tone points (squares, dragged up and down) and the free bands (numbered circles)
    void drawHandles()
    {
        for (int i = 0; i < kEq; ++i)
        {
            const double f = autoPointFreq(i);
            const float x = xOfF(f), y = std::max(display.y + 6, std::min(display.b() - 6, yOfDb(curveAt(f))));
            const bool hot = drag == dTrim && dragIdx == i;
            const float r = hot ? 6.5f : 5.0f;
            fillRect(Rect(x - r, y - r, 2 * r, 2 * r), pointColour(f, theme.dark));
            strokeRect(Rect(x - r, y - r, 2 * r, 2 * r), theme.ink, 1.2f);
            if (std::fabs(value(kToneTrim0 + i)) > 0.05f) { beginPath(); circle(x, y, 1.6f); fillColor(theme.bg); fill(); }
        }
        for (int b = 0; b < kNumUserBands; ++b)
        {
            if (value(bandParam(b, kBOn)) < 0.5f) continue;
            const double f = value(bandParam(b, kBFreq));
            const float x = xOfF(f), y = std::max(display.y + 8, std::min(display.b() - 8, bandY(b)));
            const bool hot = drag == dBand && dragIdx == b;
            beginPath(); circle(x, y, hot ? 10.0f : 8.5f); fillColor(pointColour(f, theme.dark)); fill();
            strokeColor(theme.ink); strokeWidth(1.4f); stroke();
            char n[4]; std::snprintf(n, sizeof n, "%d", b + 1);
            textAt(x, y + 0.5f, n, kBold, 10.0f, theme.dark ? theme.bg : pump::hexColor(0xFFFFFF), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }
    int hitTrim(float x, float y) const
    {
        for (int i = 0; i < kEq; ++i)
        {
            const double f = autoPointFreq(i);
            if (std::fabs(x - xOfF(f)) < 9 && std::fabs(y - yOfDb(curveAt(f))) < 9) return i;
        }
        return -1;
    }
    int hitBand(float x, float y) const
    {
        for (int b = kNumUserBands - 1; b >= 0; --b)
        {
            if (value(bandParam(b, kBOn)) < 0.5f) continue;
            const float dx = x - xOfF(value(bandParam(b, kBFreq))), dy = y - bandY(b);
            if (dx * dx + dy * dy < 12 * 12) return b;
        }
        return -1;
    }

    void drawInfo()
    {
        const Rect& r = info;
        strokeRect(r, theme.dim, 1.0f);
        float y = r.y + 15;
        caption(r.x + 12, y, "SONG (LEARNED)");
        y += 18;
        auto row = [&](const char* k, const std::string& v, const Color* c = nullptr) {
            textAt(r.x + 12, y, k, kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
            textAt(r.r() - 12, y, v, kMono, 11.0f, c ? *c : theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            y += 16;
        };
        if (ana.valid)
        {
            row("LOUDNESS", dbs(ana.lufs, "%.1f LUFS"));
            row("TRUE PEAK", dbs(ana.truePeak, "%.1f dBTP"));
            if (!advanced) { row("RANGE", dbs(ana.lra, "%.1f LU")); row("LENGTH", clock(ana.seconds)); }
        }
        else { row("LOUDNESS", "--"); row("TRUE PEAK", "--"); if (!advanced) { row("RANGE", "--"); row("LENGTH", "--"); } }
        y += 6;
        caption(r.x + 12, y, "MASTER");
        y += 18;
        if (ana.valid && planValid)
        {
            row("WILL BE", dbs(outPred, "%.1f LUFS"));
            row("CHANGE", dbs(outPred - ana.lufs, "%+.1f LU"));
        }
        else { row("WILL BE", "--"); row("CHANGE", "--"); }
        row("NOW", std::isfinite(outST) && outST > -70.0f ? dbs(outST, "%.1f LUFS") : "--");
        // meters: output level, compressor and limiter
        const float my = r.b() - 46;
        caption(r.x + 12, my - 2, "OUTPUT");
        const Rect ml(r.x + 12, my + 6, r.w - 24, 7), mr(r.x + 12, my + 15, r.w - 24, 7);
        hMeter(ml, outMeter.lvl[0], -48.0f, 0.0f, theme.neon);
        hMeter(mr, outMeter.lvl[1], -48.0f, 0.0f, theme.neon);
        char b[48];
        std::snprintf(b, sizeof b, "LIMIT %.1f dB", limGr);
        textAt(r.x + 12, r.b() - 12, b, kMono, 10.0f, limGr > 3.0f ? theme.red : theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "LIFT %+.1f", lift);
        textAt(r.r() - 12, r.b() - 12, b, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void hMeter(const Rect& m, float db, float lo, float hi, const Color& c)
    {
        fillRect(m, theme.screen);
        const float t = pump::clampv((db - lo) / (hi - lo), 0.0f, 1.0f);
        fillRect(Rect(m.x, m.y, m.w * t, m.h), c);
        strokeRect(m, theme.ink, 1.0f);
    }

    Color stageInk(int i) const { return theme.dark ? pump::oklchColor(0.80f, 0.14f, stageHue[i]) : pump::oklchColor(0.50f, 0.17f, stageHue[i]); }
    Color stageDim(int i) const { return theme.dark ? pump::oklchColor(0.45f, 0.09f, stageHue[i]) : pump::oklchColor(0.80f, 0.10f, stageHue[i]); }

    void drawStageText()
    {
        auto lines = [&](int i, const std::string& a, const std::string& b, const Color* bc = nullptr) {
            const Rect& p = stageP[i];
            const bool on = value(i == 0 ? kToneOn : i == 1 ? kDynOn : i == 2 ? kStereoOn : kLoudOn) >= 0.5f;
            const Color c = on ? stageInk(i) : theme.ink2;
            textAt(p.cx(), p.b() - 34, a, kMono, 10.5f, c, ALIGN_CENTER | ALIGN_MIDDLE);
            textAt(p.cx(), p.b() - 18, b, kMono, 10.5f, bc ? *bc : c, ALIGN_CENTER | ALIGN_MIDDLE);
        };
        if (!(ana.valid && planValid))
        {
            for (int i = 0; i < 4; ++i) lines(i, "WAITING FOR", "LEARN");
            return;
        }
        // TONE: the largest move and where
        {
            double bestF = 1000.0, bestG = 0.0;
            for (int k = 0; k < kCurvePts; ++k) if (std::fabs(curve[k]) > std::fabs(bestG)) { bestG = curve[k]; bestF = Engine::curveFreq(k); }
            char b[48];
            if (std::fabs(bestG) < 0.05) std::snprintf(b, sizeof b, "FLAT");
            else if (bestF < 1000.0) std::snprintf(b, sizeof b, "%+.1f dB @ %.0f Hz", bestG, bestF);
            else std::snprintf(b, sizeof b, "%+.1f dB @ %.1f kHz", bestG, bestF / 1000.0);
            lines(0, b, "LEVEL " + dbs(eqDelta));
        }
        // DYNAMICS
        {
            char a[48], b[48];
            std::snprintf(a, sizeof a, "QUIET PARTS +%.1f dB", liftMax);
            if (ratio > 1.001) std::snprintf(b, sizeof b, "GLUE %.2f:1  GR %.1f", ratio, compGr);
            else std::snprintf(b, sizeof b, "NO COMPRESSION");
            if (!advanced) lines(1, a, b);
        }
        // STEREO
        {
            char a[48], b[48];
            if (sideLo < -0.05f) std::snprintf(a, sizeof a, "LOWS %.1f dB SIDE", sideLo); else std::snprintf(a, sizeof a, "LOWS UNCHANGED");
            if (std::fabs(sideHi) > 0.05f) std::snprintf(b, sizeof b, "WIDTH %+.1f dB", sideHi); else std::snprintf(b, sizeof b, "WIDTH UNCHANGED");
            lines(2, a, b);
        }
        // LOUDNESS
        {
            char a[48], b[48];
            std::snprintf(a, sizeof a, "GAIN %+.1f dB", makeup);
            const bool heavy = -limLoss > 1.0f || peakOver > 6.0f;
            if (peakOver < 0.05f) std::snprintf(b, sizeof b, "NO LIMITING");
            else std::snprintf(b, sizeof b, heavy ? "PEAKS -%.1f dB · HEAVY" : "PEAKS -%.1f dB", peakOver);
            if (!advanced) lines(3, a, b, heavy ? &theme.red : nullptr);
        }
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        const double sr = anaRate > 0 ? anaRate : 48000.0;
        char buf[96];
        std::snprintf(buf, sizeof buf, "LATENCY %.1f ms  ·  %s", 1000.0 * lat / sr, ana.valid ? "LEARNED" : "NOT LEARNED YET");
        return buf;
    }

    // ---------------------------------------------------------------- mouse and keys
    void bodyHover(float x, float y) override
    {
        hoverX = x; hoverY = y;
        if (display.contains(x, y))
        {
            const double f = xToFreq(display, x, kLo, kHi);
            char b[96]; std::snprintf(b, sizeof b, "%.0f Hz  %+.1f dB    right-click: curve scale", f, curveAt(f));
            setStatus(b);
        }
        else hoverX = -1;
    }
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (advanced && planValid && display.contains(x, y))
        {
            const int b = hitBand(x, y), t = b < 0 ? hitTrim(x, y) : -1;
            if (button == kMouseButtonRight && b >= 0) { bandMenu(b, x, y); return true; }
            if (button == kMouseButtonLeft)
            {
                if (b >= 0 && dbl) { setParamGesture(bandParam(b, kBOn), 0.0f); setStatus("Band " + std::to_string(b + 1) + " off"); return true; }
                if (t >= 0 && dbl) { setParamGesture(kToneTrim0 + t, 0.0f); setStatus("Back to what auto set"); return true; }
                if (b >= 0)
                {
                    drag = dBand; dragIdx = b;
                    beginEdit(bandParam(b, kBFreq)); beginEdit(bandParam(b, kBGain));
                    return true;
                }
                if (t >= 0)
                {
                    drag = dTrim; dragIdx = t; dragY0 = y; dragV0 = value(kToneTrim0 + t);
                    beginEdit(kToneTrim0 + t);
                    return true;
                }
                // empty curve: a new band here
                int free = -1;
                for (int k = 0; k < kNumUserBands; ++k) if (value(bandParam(k, kBOn)) < 0.5f) { free = k; break; }
                if (free < 0) { setStatus("All eight bands are in use: double-click one to remove it"); return true; }
                const float f = (float)xToFreq(display, x, kLo, kHi);
                const float g = pump::clampv((display.cy() - y) / pxPerDb(), -18.0f, 18.0f);
                beginGroup();
                setParamGesture(bandParam(free, kBShape), (float)kUBell);
                setParamGesture(bandParam(free, kBQ), 1.0f);
                setParamGesture(bandParam(free, kBFreq), f);
                setParamGesture(bandParam(free, kBGain), (mod & kModifierShift) ? 0.0f : g);
                setParamGesture(bandParam(free, kBOn), 1.0f);
                endGroup();
                drag = dBand; dragIdx = free;
                beginEdit(bandParam(free, kBFreq)); beginEdit(bandParam(free, kBGain));
                return true;
            }
        }
        if (button == kMouseButtonRight && display.contains(x, y))
        {
            openMenu(Rect(x, y, 10, 10), { "Curve scale ±3 dB", "Curve scale ±6 dB", "Curve scale ±12 dB" }, dbRange == 3 ? 0 : dbRange == 6 ? 1 : 2,
                     [this](int i) { static const float rr[] = { 3, 6, 12 }; dbRange = rr[i]; saveUi(); });
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        hoverX = x; hoverY = y;
        const float fine = (mod & kModifierShift) ? 0.2f : 1.0f;
        if (drag == dTrim)
        {
            const float v = pump::clampv(dragV0 + (dragY0 - y) / pxPerDb() * fine, -6.0f, 6.0f);
            setParam(kToneTrim0 + dragIdx, v);
            char b[96]; std::snprintf(b, sizeof b, "%s: auto %+.1f dB, trim %+.1f dB", spec(kToneTrim0 + dragIdx).name, autoEq[dragIdx], v);
            setStatus(b);
        }
        else if (drag == dBand)
        {
            const int b = dragIdx;
            const float f = (float)xToFreq(display, pump::clampv(x, display.x, display.r()), kLo, kHi);
            setParam(bandParam(b, kBFreq), f);
            if (!bandIsCut(b)) setParam(bandParam(b, kBGain), pump::clampv((display.cy() - y) / pxPerDb(), -18.0f, 18.0f));
            char t[96];
            std::snprintf(t, sizeof t, "Band %d  %s  %s  %+.1f dB  Q %.2f", b + 1, kUserShapeNames[(int)value(bandParam(b, kBShape))],
                          formatParam(bandParam(b, kBFreq), value(bandParam(b, kBFreq))).c_str(), value(bandParam(b, kBGain)), value(bandParam(b, kBQ)));
            setStatus(t);
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == dTrim) endEdit(kToneTrim0 + dragIdx);
        else if (drag == dBand) { endEdit(bandParam(dragIdx, kBGain)); endEdit(bandParam(dragIdx, kBFreq)); }
        drag = dNone; dragIdx = -1;
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!advanced || !display.contains(x, y)) return false;
        const int b = hitBand(x, y);
        if (b < 0) return false;
        const int qp = bandParam(b, kBQ);
        const float q = pump::clampv(value(qp) * std::pow(2.0f, (dy > 0 ? 1.0f : -1.0f) * ((mod & kModifierShift) ? 0.05f : 0.2f)), 0.2f, 10.0f);
        setParamGesture(qp, q);
        char t[48]; std::snprintf(t, sizeof t, "Band %d  Q %.2f", b + 1, q); setStatus(t);
        return true;
    }
    void bandMenu(int b, float x, float y)
    {
        std::vector<std::string> it(kUserShapeNames, kUserShapeNames + kNumUserShapes);
        it.push_back("-"); it.push_back("Remove band");
        openMenu(Rect(x, y, 10, 10), it, (int)value(bandParam(b, kBShape)), [this, b](int i) {
            if (i < kNumUserShapes) setParamGesture(bandParam(b, kBShape), (float)i);
            else if (i == kNumUserShapes + 1) setParamGesture(bandParam(b, kBOn), 0.0f);
        });
    }

    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (k == 'l') { toggleLearn(); return true; }
        if (k == 'c') { setParamGesture(kCompare, value(kCompare) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (k == 'a') { advanced = !advanced; layout(); saveUi(); repaint(); return true; }
        if (k >= '1' && k <= '4')
        {
            static const int on[4] = { kToneOn, kDynOn, kStereoOn, kLoudOn };
            const int p = on[k - '1'];
            setParamGesture(p, value(p) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "L", "learn: start, and stop when the song has played (stopping the song also finishes it)" },
            { "C", "compare with the original at the same loudness (COMPARE key: hold = only while held)" },
            { "1  2  3  4", "tone · dynamics · stereo · loudness on/off" },
            { "INTENSITY", "everything at once: 25 % light, 50 % medium, 100 % strong" },
            { "AMOUNT", "one stage on top of INTENSITY" },
            { "A", "advanced view: everything auto set, as offsets you can tweak, and eight free EQ bands" },
            { "Advanced: drag a square", "trim auto's tone at that point (double-click: back to auto; Shift: fine)" },
            { "Advanced: click the curve", "a new band there (drag: frequency and gain, wheel: Q, right-click: shape, double-click: remove)" },
            { "Right-click the display", "curve scale" },
        };
    }
    void saveUi() { char b[48]; std::snprintf(b, sizeof b, "range=%d;adv=%d;", (int)dbRange, advanced ? 1 : 0); saveUiState(b); }
    void loadUiState(const std::string& s) override
    {
        const int r = std::atoi(uiStateGet(s, "range", "6").c_str());
        dbRange = (float)(r == 3 || r == 12 ? r : 6);
        const bool a = uiStateGet(s, "adv", "0") == "1";
        if (a != advanced) { advanced = a; layout(); }
    }

private:
    PluginMaster* plug = nullptr;
    SpectrumView post;
    OutMeter outMeter;
    Rect display, info, mainP, stageP[4];
    Button *learnBtn, *cmpBtn, *stageBtn[4];
    Segmented *styleSeg, *modeSeg;
    Dial *intDial, *stageAmt[3], *targetDial, *ceilDial;

    float stageHue[4] = {};
    bool advanced = false;
    Button *advBtn, *resetTrimBtn, *clearBandsBtn;
    Dial *liftD, *liftAtD, *ratioD, *gThrD, *atkD, *relD, *monoD, *lowMonoD, *widthD, *gainD, *limRelD;
    MenuButton* limStyleM;
    std::vector<Control*> advDials;
    float bandsCurve[kCurvePts] = {}, autoEq[kEq] = {};
    enum DragMode { dNone = 0, dTrim, dBand };
    DragMode drag = dNone;
    int dragIdx = -1;
    float dragY0 = 0.0f, dragV0 = 0.0f;
    Analysis ana;
    bool learning = false, planValid = false;
    float learnSec = 0.0f;
    float curve[kCurvePts] = {}, target[kBands] = {}, measured[kBands] = {}, eqGain[kEq] = {};
    float ratio = 1.0f, thr = 0.0f, liftMax = 0.0f, lift = 0.0f, sideLo = 0.0f, sideHi = 0.0f, makeup = 0.0f, outPred = -70.0f,
          cmpDb = 0.0f, peakOver = 0.0f, limLoss = 0.0f, eqDelta = 0.0f, inST = -INFINITY, outST = -INFINITY, compGr = 0.0f, limGr = 0.0f;
    float specTop = 0.0f, specBottom = -36.0f, dbRange = 6.0f, hoverX = -1, hoverY = -1;
    double anaRate = 48000.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIMaster)
};

UI* createUI() { return new UIMaster(); }

END_NAMESPACE_DISTRHO
