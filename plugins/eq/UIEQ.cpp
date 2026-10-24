// pump EQ: interface.
#include "ui/PumpUI.hpp"
#include "PluginEQ.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::eq;

class UIEQ : public PumpUI {
public:
    UIEQ() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::eq(), "EQ", 1040, 660, kBypass)
    {
        // ---- selected band panel ----
        bandSel = add(new MenuButton());
        bandSel->arrows = true;
        bandSel->itemsFn = [this]() {
            std::vector<std::string> v;
            for (int b = 0; b < kNumBands; ++b)
                if (bandUsed(b)) v.push_back("Band " + std::to_string(b + 1) + "  " + bandSummary(b));
            return v;
        };
        bandSel->getFn = [this]() { int k = 0; for (int b = 0; b < kNumBands; ++b) { if (!bandUsed(b)) continue; if (b == sel) return k; ++k; } return -1; };
        bandSel->setFn = [this](int i) { int k = 0; for (int b = 0; b < kNumBands; ++b) { if (!bandUsed(b)) continue; if (k == i) { select(b); return; } ++k; } };
        bandSel->textFn = [this]() { return sel >= 0 ? "BAND " + std::to_string(sel + 1) : std::string("NO BAND"); };
        bandSel->tip = "Selected band (side arrows step through bands)";

        shapeSeg = addSegmented(-1, { "BELL", "LO SH", "HI SH", "LO CUT", "HI CUT", "NOTCH", "B PASS", "TILT" });
        freqDial = addDial(-1, "FREQ", Dial::Medium);
        gainDial = addDial(-1, "GAIN", Dial::Medium, true);
        qDial = addDial(-1, "Q", Dial::Medium);
        freqDial->tabGroup = gainDial->tabGroup = qDial->tabGroup = 1;
        slopeMenu = addMenu(-1, "SLOPE");
        placeSeg = addSegmented(-1, { "ST", "L", "R", "M", "S" });
        placeSeg->tip = "Stereo placement: both channels, left, right, mid or side";
        dynBtn = addToggle(-1, "DYNAMIC");
        dynBtn->small = true;
        rangeDial = addDial(-1, "RANGE", Dial::Small, true);
        threshDial = addDial(-1, "THRESH", Dial::Small);
        rangeDial->tabGroup = threshDial->tabGroup = 2;
        bandBypassBtn = addToggle(-1, "BYPASS");
        bandBypassBtn->small = true;
        soloBtn = addButton("SOLO", [this]() { if (sel >= 0) setParamGesture(kSolo, (int)value(kSolo) == sel + 1 ? 0.0f : (float)(sel + 1)); },
                            [this]() { return sel >= 0 && (int)value(kSolo) == sel + 1; });
        soloBtn->small = true;
        soloBtn->holdMomentary = true;
        soloBtn->tip = "Hear only this band's region (click = latch, click-and-hold = while held; Alt-hold a node does the same)";
        delBtn = addButton("DELETE", [this]() { if (sel >= 0) deleteBand(sel); });
        delBtn->small = true;

        // ---- globals ----
        phaseSeg = addSegmented(kPhase, { "ZERO LATENCY", "LINEAR" });
        qualMenu = addMenu(kLinQuality, "QUALITY");
        anaMenu = addMenu(kAnalyzer, "ANALYZER");
        scaleDial = addDial(kGainScale, "SCALE", Dial::Small);
        outDial = addDial(kOutGain, "OUTPUT", Dial::Small, true);
        autoBtn = addToggle(kAutoGain, "AUTO GAIN");
        autoBtn->small = true;
        atkDial = addDial(kDynAttack, "ATTACK", Dial::Small);
        relDial = addDial(kDynRelease, "RELEASE", Dial::Small);

        rangeBtn = addButton("±12", [this]() {
            static const int r[] = { 3, 6, 12, 24, 30 };
            std::vector<std::string> it;
            int cur = 2;
            for (int i = 0; i < 5; ++i) { it.push_back("±" + std::to_string(r[i]) + " dB"); if ((int)dbRange == r[i]) cur = i; }
            openMenu(rangeBtn->r, it, cur, [this](int i) { static const int rr[] = { 3, 6, 12, 24, 30 }; dbRange = (float)rr[i]; rangeBtn->label = "±" + std::to_string(rr[i]); saveUi(); });
        });
        rangeBtn->small = true;
        rangeBtn->tip = "Display range of the EQ curve";

        if (auto* p = static_cast<PluginEQ*>(pluginPtr())) plug = p;
        anaRate = plug && plug->getSampleRate() > 0 ? plug->getSampleRate() : (sampleRate() > 0 ? sampleRate() : 48000.0);
        pre.setup(anaRate);
        post.setup(anaRate);
        pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
        rebind();
    }
    ~UIEQ() override
    {
        // closing the editor mid-hold must not leave the plugin soloed or auditioning
        if (plug) { plug->soloHold.store(-1); plug->auditionOn.store(false); }
    }

protected:
    static constexpr double kLo = 15.0, kHi = 22000.0;

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        display = Rect(14, 58, W - 28 - 34, 392);
        meterL = Rect(display.r() + 12, display.y, 9, display.h - 18);
        meterR = Rect(meterL.r() + 2, display.y, 9, display.h - 18);
        rangeBtn->r = Rect(display.r() - 52, display.y + 8, 44, 22);
        const float y0 = 470;
        // band panel (left)
        bandPanel = Rect(14, y0, 676, 164);
        bandSel->r = Rect(26, y0 + 10, 150, 26);
        shapeSeg->r = Rect(190, y0 + 10, 488, 26);
        freqDial->r = Rect(28, y0 + 52, 64, 84);
        gainDial->r = Rect(104, y0 + 52, 64, 84);
        qDial->r = Rect(180, y0 + 52, 64, 84);
        slopeMenu->r = Rect(262, y0 + 56, 160, 26);
        placeSeg->r = Rect(262, y0 + 96, 160, 26);
        dynBtn->r = Rect(440, y0 + 56, 88, 26);
        rangeDial->r = Rect(436, y0 + 88, 54, 70);
        threshDial->r = Rect(494, y0 + 88, 54, 70);
        bandBypassBtn->r = Rect(562, y0 + 56, 52, 26);
        soloBtn->r = Rect(620, y0 + 56, 52, 26);
        delBtn->r = Rect(562, y0 + 96, 110, 26);
        // globals (right)
        globalPanel = Rect(706, y0, W - 720, 164);
        const float gx = 718;
        phaseSeg->r = Rect(gx, y0 + 10, 196, 26);
        qualMenu->r = Rect(gx + 206, y0 + 10, 102, 26);
        anaMenu->r = Rect(gx, y0 + 46, 196, 26);
        autoBtn->r = Rect(gx + 206, y0 + 46, 102, 26);
        scaleDial->r = Rect(gx - 4, y0 + 84, 60, 72);
        outDial->r = Rect(gx + 58, y0 + 84, 60, 72);
        atkDial->r = Rect(gx + 128, y0 + 84, 60, 72);
        relDial->r = Rect(gx + 190, y0 + 84, 60, 72);
        relDial->r.x = std::min(relDial->r.x, globalPanel.r() - 66);
        atkDial->tip = "Dynamic bands: how fast they react";
        relDial->tip = "Dynamic bands: how fast they recover";
        scaleDial->tip = "Gain Scale: scales every band's gain (negative inverts the curve)";
    }

    // ---------------------------------------------------------------- helpers
    bool bandUsed(int b) const { return value(bandParam(b, kUsed)) >= 0.5f; }
    bool bandBypassed(int b) const { return value(bandParam(b, kBandBypass)) >= 0.5f; }
    pump::Shape bandShape(int b) const { return (pump::Shape)(int)value(bandParam(b, kShape)); }
    float bandFreq(int b) const { return value(bandParam(b, kFreq)); }
    float bandGain(int b) const { return value(bandParam(b, kGain)); }
    float bandQ(int b) const { return value(bandParam(b, kQ)); }
    int bandPlace(int b) const { return (int)value(bandParam(b, kPlace)); }
    bool bandDyn(int b) const { return value(bandParam(b, kDyn)) >= 0.5f && pump::shapeHasGain(bandShape(b)); }
    float effGain(int b) const { return bandGain(b) * value(kGainScale) * 0.01f; }
    // Gain Scale as a factor for mouse -> band gain (signed: negative scales invert; never near zero)
    float mouseGainScale() const { const float s = value(kGainScale) * 0.01f; return std::fabs(s) < 0.05f ? (s < 0.0f ? -0.05f : 0.05f) : s; }

    std::string bandSummary(int b) const
    {
        return std::string(kShapeNames[(int)bandShape(b)]) + "  " + spec(bandParam(b, kFreq)).format(bandFreq(b));
    }

    pump::BandSpec bandSpecOf(int b, float extraGain = 0.0f) const
    {
        pump::BandSpec s;
        s.shape = bandShape(b);
        s.freq = bandFreq(b);
        s.gainDb = effGain(b) + extraGain;
        s.q = bandQ(b);
        s.slope = kSlopeValues[pump::clampv((int)value(bandParam(b, kSlope)), 0, 8)];
        return s;
    }

    float yOfDb(float db) const { return display.cy() - db / dbRange * (display.h * 0.5f - 14.0f); }
    float dbOfY(float y) const { return (display.cy() - y) / (display.h * 0.5f - 14.0f) * dbRange; }
    float xOfF(double f) const { return freqToX(display, f, kLo, kHi); }
    double fOfX(float x) const { return xToFreq(display, x, kLo, kHi); }

    // node position: gain shapes sit at their gain; the rest on the 0 dB line
    void nodePos(int b, float& x, float& y) const
    {
        x = std::max(display.x + 8.0f, std::min(display.r() - 8.0f, xOfF(bandFreq(b))));
        if (pump::shapeHasGain(bandShape(b))) y = yOfDb(effGain(b) + (float)(plug ? plug->dynOffset(b) : 0.0));
        else y = yOfDb(0.0f);
        y = std::max(display.y + 8.0f, std::min(display.b() - 8.0f, y));
    }
    int nodeAt(float x, float y) const
    {
        int best = -1; float bd = 13.0f * 13.0f;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (!bandUsed(b)) continue;
            float nx, ny; nodePos(b, nx, ny);
            const float d = (nx - x) * (nx - x) + (ny - y) * (ny - y);
            if (d < bd) { bd = d; best = b; }
        }
        return best;
    }

    void select(int b)
    {
        sel = b;
        if (b >= 0 && !selMask[b]) { for (bool& m : selMask) m = false; selMask[b] = true; }
        rebind();
        repaint();
    }
    void selectOnly(int b)
    {
        for (bool& m : selMask) m = false;
        if (b >= 0) selMask[b] = true;
        sel = b;
        rebind();
        repaint();
    }
    std::vector<int> selectedBands() const
    {
        std::vector<int> v;
        for (int b = 0; b < kNumBands; ++b) if (selMask[b] && bandUsed(b)) v.push_back(b);
        if (v.empty() && sel >= 0 && bandUsed(sel)) v.push_back(sel);
        return v;
    }
    void rebind()
    {
        const bool has = sel >= 0 && bandUsed(sel);
        if (!has) sel = -1;
        auto bindp = [&](Control* c, int field) { c->param = has ? bandParam(sel, field) : -1; c->visible = has; };
        bindp(freqDial, kFreq); bindp(gainDial, kGain); bindp(qDial, kQ); bindp(slopeMenu, kSlope);
        bindp(placeSeg, kPlace); bindp(dynBtn, kDyn); bindp(rangeDial, kDynRange); bindp(threshDial, kDynThresh);
        bindp(bandBypassBtn, kBandBypass); bindp(shapeSeg, kShape);
        soloBtn->visible = has; delBtn->visible = has;
        if (has)
        {
            const pump::Shape s = bandShape(sel);
            gainDial->enabled = pump::shapeHasGain(s);
            slopeMenu->enabled = pump::shapeHasSlope(s);
            dynBtn->enabled = pump::shapeHasGain(s);
            rangeDial->enabled = threshDial->enabled = bandDyn(sel);
            // shelves only have 6/12/24 dB/oct (steeper settings act as 24)
            if (s == pump::Shape::LowShelf || s == pump::Shape::HighShelf || s == pump::Shape::TiltShelf)
            {
                const int b = sel;
                auto shelfIdx = [this, b]() { const int v = (int)value(bandParam(b, kSlope)); return v <= 0 ? 0 : (v <= 1 ? 1 : 2); };
                slopeMenu->itemsFn = []() { return std::vector<std::string> { kSlopeNames[0], kSlopeNames[1], kSlopeNames[3] }; };
                slopeMenu->getFn = shelfIdx;
                slopeMenu->setFn = [this, b](int i) { static const int m[] = { 0, 1, 3 }; setParamGesture(bandParam(b, kSlope), (float)m[pump::clampv(i, 0, 2)]); };
                slopeMenu->textFn = [shelfIdx]() { static const int m[] = { 0, 1, 3 }; return std::string(kSlopeNames[m[shelfIdx()]]); };
            }
            else { slopeMenu->itemsFn = nullptr; slopeMenu->getFn = nullptr; slopeMenu->setFn = nullptr; slopeMenu->textFn = nullptr; }
            freqDial->label = "FREQ";
            qDial->label = (s == pump::Shape::LowCut || s == pump::Shape::HighCut || s == pump::Shape::LowShelf || s == pump::Shape::HighShelf || s == pump::Shape::TiltShelf) ? "RESO" : "Q";
        }
        qualMenu->enabled = (int)value(kPhase) == 1;
    }

    int freeBand() const { for (int b = 0; b < kNumBands; ++b) if (!bandUsed(b)) return b; return -1; }

    int createBand(double f, float gainDb, bool dynamic = false)
    {
        const int b = freeBand();
        if (b < 0) { setStatus("All 24 bands are in use"); return -1; }
        int shape = 0, slope = 1;
        float q = 1.0f, g = gainDb;
        if (f < 30.0) { shape = 3; slope = 3; q = 0.707f; g = 0; }
        else if (f > 17000.0) { shape = 4; slope = 3; q = 0.707f; g = 0; }
        else if (f < 70.0) { shape = 1; q = 0.707f; }
        else if (f > 9000.0) { shape = 2; q = 0.707f; }
        auto set = [&](int field, float v) { setParamGesture(bandParam(b, field), v); };
        beginGroup();   // one undo step for the whole band
        set(kShape, (float)shape); set(kFreq, (float)f); set(kGain, std::round(g * 10.0f) / 10.0f); set(kQ, q);
        set(kSlope, (float)slope); set(kPlace, 0); set(kDyn, dynamic && shape <= 2 ? 1.0f : 0.0f); set(kBandBypass, 0);
        set(kDynRange, -6.0f); set(kDynThresh, -30.0f);
        set(kUsed, 1.0f);
        endGroup();
        selectOnly(b);
        return b;
    }
    void deleteBand(int b)
    {
        beginGroup();   // one undo step (solo off + band off)
        if ((int)value(kSolo) == b + 1) setParamGesture(kSolo, 0);
        setParamGesture(bandParam(b, kUsed), 0.0f);
        endGroup();
        // nothing gets selected in its place: a second Delete must not remove a band the user never picked
        selMask[b] = false;
        if (sel == b) sel = -1;
        rebind();
    }

    void onParamChanged(int i, float) override
    {
        const int b = bandOf(i);
        if (b >= 0 && fieldOf(i) == kUsed && !bandUsed(b)) { selMask[b] = false; if (sel == b) sel = -1; }
        rebind();
    }
    // after a preset, undo/redo or A/B: keep the selection if that band still exists; the panel and the
    // highlighted nodes always agree; a solo on a band that's gone is cleared
    void onPresetApplied() override
    {
        if (sel >= 0 && !bandUsed(sel)) sel = -1;
        for (int k = 0; k < kNumBands; ++k) selMask[k] = (k == sel);
        const int so = (int)value(kSolo);
        if (so > 0 && !bandUsed(so - 1)) setParamGesture(kSolo, 0.0f);
        rebind();
    }

    // ---------------------------------------------------------------- drawing
    void tick(float dt) override
    {
        if (!initialSelect) { initialSelect = true; if (sel < 0) for (int k = 0; k < kNumBands; ++k) if (bandUsed(k)) { selectOnly(k); break; } }
        if (!plug) return;
        const double srNow = plug->getSampleRate();
        if (srNow > 0.0 && srNow != anaRate)
        {
            anaRate = srNow;
            pre.setup(srNow); post.setup(srNow);
            pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
        }
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const int an = (int)value(kAnalyzer);
        if (an == 1 || an == 3) pre.update(&plug->preRing, nullptr, dt);
        if (an >= 2) post.update(&plug->postRing, nullptr, dt);
    }

    void drawBody() override
    {
        screenBox(display);
        save();
        scissor(display.x + 1, display.y + 1, display.w - 2, display.h - 2);
        // grid: dB lines
        for (int k = -6; k <= 6; ++k)
        {
            const float db = dbRange * k / 6.0f;
            const float y = yOfDb(db);
            line(display.x, y, display.r(), y, k == 0 ? withAlpha2(theme.ink, 0.28f) : theme.grid, 1.0f);
        }
        drawFreqGrid(display, kLo, kHi);
        // analyzers (spectrum dB: 0 dBFS at the top, -96 at the bottom)
        const int an = (int)value(kAnalyzer);
        const float top = 6.0f, bottom = -90.0f;
        if (an == 1 || an == 3)
            drawSpectrum(pre, display, top, bottom, withAlpha2(theme.ink, theme.dark ? 0.10f : 0.06f), withAlpha2(theme.ink, 0.25f), kLo, kHi, 1.0f);
        if (an >= 2)
            drawSpectrum(post, display, top, bottom, withAlpha2(theme.neon, theme.dark ? 0.26f : 0.34f), withAlpha2(mixDeep(), 0.7f), kLo, kHi, 1.1f);
        // per-band curves (filled toward 0 dB) and dynamic ranges
        const int NP = 220;
        static float xs[NP];
        double total[NP];
        for (int p = 0; p < NP; ++p) { total[p] = 0.0; xs[p] = display.x + display.w * p / (NP - 1); }
        const double sr = sampleRate() > 0 ? sampleRate() : 48000.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (!bandUsed(b) || bandBypassed(b)) continue;
            const double dynOff = plug ? plug->dynOffset(b) : 0.0;
            const pump::AnalogBand a = pump::analogPrototype(bandSpecOf(b, (float)dynOff), sr);
            const bool isSel = b == sel;
            Color c = bandColor(b);
            // dynamic range shadow: between the static curve and static + range
            if (bandDyn(b))
            {
                const pump::AnalogBand a0 = pump::analogPrototype(bandSpecOf(b), sr);
                const pump::AnalogBand a1 = pump::analogPrototype(bandSpecOf(b, value(bandParam(b, kDynRange))), sr);
                beginPath();
                for (int p = 0; p < NP; ++p) { const double f = fOfX(xs[p]); const float y = yOfDb((float)(10 * std::log10(a0.magSq(pump::kTwoPi * f / sr)))); if (p == 0) moveTo(xs[p], y); else lineTo(xs[p], y); }
                for (int p = NP - 1; p >= 0; --p) { const double f = fOfX(xs[p]); lineTo(xs[p], yOfDb((float)(10 * std::log10(a1.magSq(pump::kTwoPi * f / sr))))); }
                closePath();
                fillColor(withAlpha2(c, 0.16f));
                fill();
            }
            beginPath();
            moveTo(xs[0], yOfDb(0));
            for (int p = 0; p < NP; ++p)
            {
                const double f = fOfX(xs[p]);
                const double db = 10.0 * std::log10(std::max(a.magSq(pump::kTwoPi * f / sr), 1e-30));
                // the channel a band acts on decides which total it joins (display: everything summed)
                total[p] += db;
                lineTo(xs[p], std::max(display.y - 2.0f, std::min(display.b() + 2.0f, yOfDb((float)db))));
            }
            lineTo(xs[NP - 1], yOfDb(0));
            closePath();
            fillColor(withAlpha2(c, isSel ? 0.30f : 0.14f));
            fill();
            if (isSel)
            {
                beginPath();
                for (int p = 0; p < NP; ++p)
                {
                    const double f = fOfX(xs[p]);
                    const double db = 10.0 * std::log10(std::max(a.magSq(pump::kTwoPi * f / sr), 1e-30));
                    const float y = std::max(display.y - 2.0f, std::min(display.b() + 2.0f, yOfDb((float)db)));
                    if (p == 0) moveTo(xs[p], y); else lineTo(xs[p], y);
                }
                strokeColor(withAlpha2(c, 0.9f)); strokeWidth(1.2f); stroke();
            }
        }
        for (int p = 0; p < NP; ++p) { curveX[p] = xs[p]; curveDb[p] = (float)total[p]; }
        // the combined curve
        if (theme.dark)
        {
            beginPath();
            for (int p = 0; p < NP; ++p) { const float y = yOfDb((float)total[p]); if (p == 0) moveTo(xs[p], y); else lineTo(xs[p], y); }
            strokeColor(theme.glow); strokeWidth(7.0f); stroke();
        }
        beginPath();
        for (int p = 0; p < NP; ++p) { const float y = std::max(display.y - 2.0f, std::min(display.b() + 2.0f, yOfDb((float)total[p]))); if (p == 0) moveTo(xs[p], y); else lineTo(xs[p], y); }
        strokeColor(theme.ink); strokeWidth(2.4f); lineJoin(ROUND); stroke();
        // nodes
        for (int b = 0; b < kNumBands; ++b)
        {
            if (!bandUsed(b)) continue;
            float x, y; nodePos(b, x, y);
            const bool isSel = b == sel || selMask[b], byp = bandBypassed(b), hov = b == hover;
            const float rr = b == sel ? 9.0f : 8.0f;
            beginPath(); circle(x + 2.0f, y + 2.0f, rr); fillColor(theme.dim); fill();
            beginPath(); circle(x, y, rr);
            fillColor(byp ? theme.bg : (isSel ? theme.neon : (hov ? theme.panel2 : theme.bg)));
            fill();
            strokeColor(byp ? theme.dim : theme.ink); strokeWidth(isSel ? 2.0f : 1.5f); stroke();
            textAt(x, y + 0.5f, std::to_string(b + 1), kBold, 9.5f, byp ? theme.dim : (isSel ? theme.neonInk : theme.ink), ALIGN_CENTER | ALIGN_MIDDLE);
            const int pl = bandPlace(b);
            if (pl != kStereo)
            {
                static const char* t[] = { "", "L", "R", "M", "S" };
                textAt(x, y - rr - 7.0f, t[pl], kBold, 9.0f, bandColor(b), ALIGN_CENTER | ALIGN_MIDDLE);
            }
            if (bandDyn(b))
            {
                beginPath(); circle(x, y, rr + 3.5f); strokeColor(withAlpha2(bandColor(b), 0.8f)); strokeWidth(1.0f); stroke();
            }
        }
        // dB labels (EQ scale) at the right edge
        for (int k = -3; k <= 3; ++k)
        {
            if (k == 3) continue;
            const float db = dbRange * k / 3.0f;
            char buf[16]; std::snprintf(buf, sizeof buf, "%+.0f", db);
            if (k == 0) std::snprintf(buf, sizeof buf, "0");
            textAt(display.r() - 8, yOfDb(db) - 7, buf, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        // hover readout near a node, or a hint on an empty EQ
        const int info = dragBand >= 0 ? dragBand : hover;
        if (info >= 0 && bandUsed(info))
        {
            float x, y; nodePos(info, x, y);
            std::string s = std::to_string(info + 1) + "  " + spec(bandParam(info, kFreq)).format(bandFreq(info));
            if (pump::shapeHasGain(bandShape(info))) s += "  " + spec(bandParam(info, kGain)).format(bandGain(info));
            char qb[24]; std::snprintf(qb, sizeof qb, "  Q %.2f", bandQ(info)); s += qb;
            const float tw = textWidth(s, kMono, 11.0f) + 14.0f;
            Rect tb(std::min(display.r() - tw - 6, x + 14), y < display.y + 40 ? y + 14 : y - 34, tw, 22);
            fillRect(tb, theme.ink);
            textAt(tb.cx(), tb.cy(), s, kMono, 11.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        else if (sel < 0 && dmode == dmNone)
        {
            textAt(display.cx(), display.cy() - 40, "DOUBLE-CLICK TO ADD A BAND", kLabel, 14.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 2.0f);
            textAt(display.cx(), display.cy() - 18, "HOLD ALT AND DRAG TO LISTEN TO A FREQUENCY RANGE", kLabel, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.5f);
        }
        if (dmode == dmRect)
        {
            const Rect rr(std::min(pressX, curX), std::min(pressY, curY), std::fabs(curX - pressX), std::fabs(curY - pressY));
            fillRect(rr, withAlpha2(theme.ink, 0.06f));
            strokeRect(rr, theme.ink2, 1.0f);
        }
        if (dmode == dmAudition || soloHeld)
        {
            double f, q;
            std::string label;
            int side = 0;   // what the engine hears (designDetector): 0 a band around f, -1 everything below f, +1 above
            if (dmode == dmAudition) { f = audF; q = audQ; label = "LISTEN  " + spec(kAuditionFreq).format((float)f); }
            else
            {
                f = bandFreq(soloBand); q = pump::clampv((double)bandQ(soloBand), 0.3, 12.0); label = "SOLO BAND " + std::to_string(soloBand + 1);
                const pump::Shape sh = bandShape(soloBand);
                if (sh == pump::Shape::LowShelf || sh == pump::Shape::LowCut) side = -1;
                else if (sh == pump::Shape::HighShelf || sh == pump::Shape::HighCut || sh == pump::Shape::TiltShelf) side = 1;
            }
            const double half = std::asinh(1.0 / (2.0 * q)) / std::log(2.0);   // half bandwidth in octaves
            float x0 = xOfF(f * std::pow(2.0, -half)), x1 = xOfF(f * std::pow(2.0, half));
            if (side < 0) { x0 = display.x; x1 = xOfF(f); }
            else if (side > 0) { x0 = xOfF(f); x1 = display.r(); }
            fillRect(Rect(display.x, display.y, std::max(0.0f, x0 - display.x), display.h), withAlpha2(theme.bg, 0.55f));
            fillRect(Rect(x1, display.y, std::max(0.0f, display.r() - x1), display.h), withAlpha2(theme.bg, 0.55f));
            line(x0, display.y, x0, display.b(), theme.second, 1.5f);
            line(x1, display.y, x1, display.b(), theme.second, 1.5f);
            const float tw = textWidth(label, kLabel, 12.0f, 1.0f) + 16.0f;
            // keep clear of the ±dB range button in the top-right corner
            const Rect lb(std::max(display.x + 6, std::min(rangeBtn->r.x - tw - 8, xOfF(f) - tw * 0.5f)), display.y + 10, tw, 22);
            fillRect(lb, theme.second);
            textAt(lb.cx(), lb.cy(), label, kLabel, 12.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
        restore();

        // output meter
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, display.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        // bottom panels
        strokeRect(bandPanel, theme.dim, 1.0f);
        strokeRect(globalPanel, theme.dim, 1.0f);
        if (sel < 0)
            textAt(bandPanel.cx(), bandPanel.cy() + 10, "No band selected", kLabel, 13.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }
    Color mixDeep() const { return pump::mixColor(theme.neon, theme.ink, 0.45f); }
    Color bandColor(int b) const
    {
        switch (bandPlace(b))
        {
        case kMid: return pump::mixColor(theme.neon, theme.ink, 0.35f);
        case kSide: return theme.second;
        case kLeft: case kRight: return pump::mixColor(theme.second, theme.ink, 0.3f);
        default: return theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.55f);
        }
    }

    std::string footerInfo() override
    {
        int used = 0; for (int b = 0; b < kNumBands; ++b) used += bandUsed(b);
        char buf[96];
        const int lat = plug ? plug->latencyNow() : 0;
        std::snprintf(buf, sizeof buf, "%d/%d BANDS   %s   LATENCY %d", used, kNumBands, (int)value(kPhase) ? "LINEAR PHASE" : "ZERO LATENCY", lat);
        return buf;
    }

    // ---------------------------------------------------------------- mouse on the display
    // Node conventions, plus Alt-solo:
    //   double-click / Ctrl-click (Cmd-click on macOS, where Ctrl-click is a right-click) empty: new band (Alt + double-click: a dynamic band); drag the curve: new band
    //   drag a node: frequency + gain of all selected bands; Shift: fine; Alt while dragging: lock to one axis;
    //   Ctrl/Cmd + vertical drag: Q; wheel: Q (cuts: slope), Ctrl/Cmd + wheel: gain, Alt + wheel: dynamic range
    //   Alt + click-and-hold a node: solo that band while held (drag, still soloed: left/right frequency, up/down Q);
    //   Alt + hold empty space: listen to the region under the mouse (drag to sweep, up/down or wheel for width)
    //   Ctrl/Cmd + click a node: add to selection; Shift + click: select a range; drag empty space: select
    //   Ctrl/Cmd + Alt + click a node: next shape; Alt + Shift + click: next slope; double-click a node: type values
    enum DragMode { dmNone = 0, dmNodes, dmRect, dmAudition, dmSoloHold, dmPending };

    bool nearCurve(float x, float y) const
    {
        for (int p = 0; p < kCurvePts - 1; ++p)
            if (x >= curveX[p] && x <= curveX[p + 1])
            {
                const float t = (x - curveX[p]) / std::max(1e-3f, curveX[p + 1] - curveX[p]);
                const float yc = yOfDb(curveDb[p] + (curveDb[p + 1] - curveDb[p]) * t);
                return std::fabs(yc - y) < 7.0f;
            }
        return false;
    }

    void startNodeDrag(float x, float y)
    {
        dragSet = selectedBands();
        for (int b : dragSet)
            if (b >= 0 && b < kNumBands) { orig[b][0] = bandFreq(b); orig[b][1] = bandGain(b); orig[b][2] = bandQ(b); }
        beginGroup();
        for (int b : dragSet) { beginEdit(bandParam(b, kFreq)); beginEdit(bandParam(b, kGain)); beginEdit(bandParam(b, kQ)); }
        dmode = dmNodes;
        pressX = lastX = x; pressY = lastY = y;
        effX = effY = 0.0f;
        axis = 0;
        dragBand = sel;
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!display.contains(x, y)) return false;
        const bool alt = (mod & kModifierAlt) != 0, cmd = cmdKey(mod), shift = (mod & kModifierShift) != 0;
        const int b = nodeAt(x, y);
        pressX = curX = x; pressY = curY = y;
        if (button == kMouseButtonRight)
        {
            if (b >= 0) { if (!selMask[b]) selectOnly(b); else { sel = b; rebind(); } bandMenu(b, x, y); }
            else backgroundMenu(x, y);
            return false;
        }
        if (button != kMouseButtonLeft) return false;
        const double scale = mouseGainScale();
        if (b >= 0)
        {
            if (alt && cmd) { cycleShape(b); return false; }
            if (alt && shift) { cycleSlope(b); return false; }
            if (alt)
            {
                // Alt-solo on a node: hear this band's region while the button is held; drag to move it
                // (still soloed) like an ordinary node drag
                if (!selMask[b]) selectOnly(b);
                else { sel = b; rebind(); }
                soloBand = b;
                soloHeld = true;
                if (plug) plug->soloHold.store(b);
                dmode = dmSoloHold;
                return true;
            }
            if (cmd) { selMask[b] = !selMask[b]; sel = selMask[b] ? b : (selectedBands().empty() ? -1 : selectedBands().front()); rebind(); return false; }
            if (shift && sel >= 0)
            {
                const double f0 = std::min(bandFreq(sel), bandFreq(b)), f1 = std::max(bandFreq(sel), bandFreq(b));
                for (int k = 0; k < kNumBands; ++k) if (bandUsed(k) && bandFreq(k) >= f0 && bandFreq(k) <= f1) selMask[k] = true;
                sel = b; rebind();
                return false;
            }
            if (dbl) { selectOnly(b); openNodeEntry(b); return false; }
            if (!selMask[b]) selectOnly(b);
            else { sel = b; rebind(); }
            startNodeDrag(x, y);
            return true;
        }
        // empty space
        if (alt && !dbl)
        {
            dmode = dmAudition;
            lastY = y;
            audF = (float)fOfX(x);
            pushAudition(true);
            return true;
        }
        if (dbl || cmd)
        {
            const int nb = createBand(fOfX(x), dbOfY(y) / (float)scale, alt);
            if (nb >= 0) { startNodeDrag(x, y); return true; }
            return false;
        }
        if (nearCurve(x, y)) { dmode = dmPending; return true; }
        dmode = dmRect;
        return true;
    }

    void bodyMouseDrag(float x, float y, uint mod) override
    {
        curX = x; curY = y;
        const bool alt = (mod & kModifierAlt) != 0, cmd = cmdKey(mod);
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        switch (dmode)
        {
        case dmAudition: {
            // left/right: frequency; up: narrower, down: wider; the wheel still sets the width too
            audF = (float)fOfX(std::max(display.x, std::min(display.r(), x)));
            const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
            audQ = spec(kAuditionQ).clamp(audQ * std::pow(2.0f, -(y - lastY) * fine / 60.0f));
            lastY = y;
            pushAudition(true);
            char q[24]; std::snprintf(q, sizeof q, "  Q %.2f", audQ);
            setStatus("Listening around " + spec(kAuditionFreq).format(audF) + q + "  (up/down or wheel: width)");
            return;
        }
        case dmSoloHold:
            // moved far enough: keep the solo and turn the hold into a node drag
            if (std::fabs(x - pressX) + std::fabs(y - pressY) > 3.0f)
            {
                startNodeDrag(pressX, pressY);
                bodyMouseDrag(x, y, mod);
            }
            return;
        case dmPending:
            if (std::fabs(x - pressX) + std::fabs(y - pressY) > 4.0f)
            {
                const double scale = mouseGainScale();
                const int nb = createBand(fOfX(pressX), dbOfY(pressY) / (float)scale);
                if (nb >= 0) { startNodeDrag(pressX, pressY); bodyMouseDrag(x, y, mod); }
                else dmode = dmNone;
            }
            return;
        case dmNodes: {
            effX += (x - lastX) * fine;
            effY += (y - lastY) * fine;
            lastX = x; lastY = y;
            float dx = effX, dy = effY;
            if (alt && !soloHeld)   // lock to the dominant axis; Alt held for a solo-drag doesn't count
            {
                if (axis == 0 && std::fabs(dx) + std::fabs(dy) > 6.0f) axis = std::fabs(dx) >= std::fabs(dy) ? 1 : 2;
                if (axis == 1) dy = 0.0f; else if (axis == 2) dx = 0.0f;
            }
            else axis = 0;
            const double scale = mouseGainScale();
            for (int b : dragSet)
            {
                setParam(bandParam(b, kFreq), (float)std::min(kHi, std::max(kLo, orig[b][0] * std::pow(kHi / kLo, (double)(dx / display.w)))));
                // Q: Ctrl/Cmd-drag, shapes without gain, and Alt-solo drags (up = narrower, down = wider)
                if (cmd || soloHeld || !pump::shapeHasGain(bandShape(b)))
                    setParam(bandParam(b, kQ), orig[b][2] * std::pow(2.0f, -dy / 60.0f));
                else
                    setParam(bandParam(b, kGain), orig[b][1] - dy / (display.h * 0.5f - 14.0f) * dbRange / (float)scale);
            }
            if (dragBand >= 0) setStatus(bandReadout(dragBand));
            return;
        }
        default:
            return;
        }
    }

    void bodyMouseUp(float x, float y) override
    {
        switch (dmode)
        {
        case dmNodes:
            for (int b : dragSet) { endEdit(bandParam(b, kFreq)); endEdit(bandParam(b, kGain)); endEdit(bandParam(b, kQ)); }
            endGroup();
            break;
        case dmAudition:
            pushAudition(false);
            break;
        case dmRect: {
            const Rect rr(std::min(pressX, x), std::min(pressY, y), std::fabs(x - pressX), std::fabs(y - pressY));
            if (rr.w < 4 && rr.h < 4) { selectOnly(-1); break; }
            for (bool& m : selMask) m = false;
            sel = -1;
            for (int b = 0; b < kNumBands; ++b)
            {
                if (!bandUsed(b)) continue;
                float nx, ny; nodePos(b, nx, ny);
                if (rr.contains(nx, ny)) { selMask[b] = true; if (sel < 0) sel = b; }
            }
            rebind();
            break;
        }
        case dmPending:
            selectOnly(-1);
            break;
        default: break;
        }
        if (soloHeld) { if (plug) plug->soloHold.store(-1); soloHeld = false; }   // Alt-solo hold (and solo-drag) over
        dmode = dmNone;
        dragBand = -1;
        dragSet.clear();
    }

    void bodyHover(float x, float y) override
    {
        hover = display.contains(x, y) ? nodeAt(x, y) : -1;
        if (hover >= 0) setStatus(bandReadout(hover) + "    drag · Alt: solo · wheel: Q · right-click: menu");
        else if (display.contains(x, y))
        {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s  %+.1f dB    double-click: add band · Alt-hold: listen · drag: select", spec(kAuditionFreq).format((float)fOfX(x)).c_str(), dbOfY(y));
            setStatus(buf);
        }
    }

    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (dmode == dmAudition)
        {
            audQ = spec(kAuditionQ).clamp(audQ * std::pow(2.0f, dy > 0 ? 0.2f : -0.2f));
            pushAudition(true);
            char b[48]; std::snprintf(b, sizeof b, "Listen width: Q %.2f", audQ); setStatus(b);
            return true;
        }
        if (!display.contains(x, y)) return false;
        if (dmode == dmNodes || dmode == dmPending) return true;
        int b = nodeAt(x, y);
        if (b < 0 && sel >= 0 && bandUsed(sel) && std::fabs(x - xOfF(bandFreq(sel))) < 30.0f) b = sel;
        if (b < 0) return false;
        const bool alt = (mod & kModifierAlt) != 0, cmd = cmdKey(mod), shift = (mod & kModifierShift) != 0;
        const float dir = dy > 0 ? 1.0f : -1.0f;
        const pump::Shape sh = bandShape(b);
        if (alt && bandDyn(b)) setParamGesture(bandParam(b, kDynRange), value(bandParam(b, kDynRange)) + dir * (shift ? 0.1f : 0.5f));
        else if (cmd && pump::shapeHasGain(sh)) setParamGesture(bandParam(b, kGain), bandGain(b) + dir * (shift ? 0.1f : 0.5f));
        else if ((sh == pump::Shape::LowCut || sh == pump::Shape::HighCut) && !shift)
            setParamGesture(bandParam(b, kSlope), value(bandParam(b, kSlope)) + dir);
        else setParamGesture(bandParam(b, kQ), bandQ(b) * std::pow(2.0f, dir * (shift ? 0.02f : 0.1f)));
        setStatus(bandReadout(b));
        return true;
    }

    std::string bandReadout(int b) const
    {
        std::string s = "Band " + std::to_string(b + 1) + "  " + kShapeNames[(int)bandShape(b)] + "  " + spec(bandParam(b, kFreq)).format(bandFreq(b));
        if (pump::shapeHasGain(bandShape(b))) s += "  " + spec(bandParam(b, kGain)).format(bandGain(b));
        char q[32]; std::snprintf(q, sizeof q, "  Q %.2f", bandQ(b)); s += q;
        if (pump::shapeHasSlope(bandShape(b)) && !pump::shapeHasGain(bandShape(b))) s += std::string("  ") + kSlopeNames[pump::clampv((int)value(bandParam(b, kSlope)), 0, 8)];
        return s;
    }

    void openNodeEntry(int b)
    {
        float nx, ny; nodePos(b, nx, ny);
        const Rect r(std::max(display.x + 4, std::min(display.r() - 104, nx - 50)), ny < display.y + 50 ? ny + 16 : ny - 38, 100, 22);
        std::vector<std::pair<int, Rect>> ring = { { bandParam(b, kFreq), r } };
        if (pump::shapeHasGain(bandShape(b))) ring.push_back({ bandParam(b, kGain), r });
        ring.push_back({ bandParam(b, kQ), r });
        openTextEntry(bandParam(b, kFreq), r, ring);
        setStatus("Type a value (1k, A4, C#3+13); Tab: next field (frequency, gain, Q); Enter: done");
    }

    void cycleShape(int b)
    {
        static const int order[] = { 0, 1, 2, 7, 3, 4, 5, 6 };
        int k = 0; for (int i = 0; i < 8; ++i) if (order[i] == (int)bandShape(b)) k = i;
        setParamGesture(bandParam(b, kShape), (float)order[(k + 1) % 8]);
        setStatus(bandReadout(b));
        rebind();
    }
    void cycleSlope(int b)
    {
        const pump::Shape sh = bandShape(b);
        int v = (int)value(bandParam(b, kSlope));
        if (sh == pump::Shape::LowShelf || sh == pump::Shape::HighShelf || sh == pump::Shape::TiltShelf) v = v <= 0 ? 1 : (v <= 1 ? 3 : 0);   // 6 -> 12 -> 24 dB/oct
        else v = (v + 1) % 9;
        setParamGesture(bandParam(b, kSlope), (float)v);
        setStatus(bandReadout(b));
    }

    // copy/paste: band settings, shared between instances in this process
    static std::vector<std::vector<float>>& clip() { static std::vector<std::vector<float>> c; return c; }
    void copyBands(const std::vector<int>& bands)
    {
        if (bands.empty()) { setStatus("No band selected"); return; }
        clip().clear();
        for (int b : bands) { std::vector<float> v; for (int f = 0; f < kBandStride; ++f) v.push_back(value(bandParam(b, f))); clip().push_back(v); }
        setStatus("Copied " + std::to_string(bands.size()) + (bands.size() == 1 ? " band" : " bands"));
    }
    void pasteBands()
    {
        if (clip().empty()) { setStatus("Nothing copied yet"); return; }
        beginGroup();
        for (bool& m : selMask) m = false;
        int last = -1, pasted = 0;
        for (const auto& v : clip())
        {
            const int b = freeBand();
            if (b < 0) break;
            ++pasted;
            for (int f = 0; f < kBandStride; ++f) if (f != kUsed) setParamGesture(bandParam(b, f), v[(size_t)f]);
            setParamGesture(bandParam(b, kUsed), 1.0f);
            selMask[b] = true; last = b;
        }
        endGroup();
        if (last >= 0) sel = last;
        rebind();
        if (pasted == 0) setStatus("All 24 bands are in use");
        else setStatus("Pasted " + std::to_string(pasted) + (pasted == 1 ? " band" : " bands") + (pasted < (int)clip().size() ? " (no free bands for the rest)" : ""));
    }
    void deleteSelected()
    {
        const auto v = selectedBands();
        if (v.empty()) return;
        beginGroup();
        for (int b : v) deleteBand(b);
        endGroup();
    }

    bool keyPress(uint key, uint mod) override
    {
        const bool cmd = cmdKey(mod);
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (key == kKeyDelete || key == kKeyBackspace) { deleteSelected(); return true; }
        if (cmd && k == 'a') { for (int b = 0; b < kNumBands; ++b) selMask[b] = bandUsed(b); if (sel < 0) for (int b = 0; b < kNumBands; ++b) if (bandUsed(b)) { sel = b; break; } rebind(); repaint(); return true; }
        if (cmd && k == 'c') { copyBands(selectedBands()); return true; }
        if (cmd && k == 'v') { pasteBands(); return true; }
        if (cmd && k == 'd') { if (selectedBands().empty()) return true; copyBands(selectedBands()); pasteBands(); return true; }
        if (!cmd && k == 'b') { beginGroup(); for (int b : selectedBands()) setParamGesture(bandParam(b, kBandBypass), bandBypassed(b) ? 0.0f : 1.0f); endGroup(); return true; }
        if (key == kKeyEscape) { selectOnly(-1); return true; }
        if (key == kKeyLeft || key == kKeyRight)
        {
            // previous/next band by frequency
            std::vector<int> v; for (int b = 0; b < kNumBands; ++b) if (bandUsed(b)) v.push_back(b);
            if (v.empty()) return true;
            std::sort(v.begin(), v.end(), [this](int a, int b) { return bandFreq(a) < bandFreq(b); });
            int i = -1; for (size_t j = 0; j < v.size(); ++j) if (v[j] == sel) i = (int)j;
            if (i < 0) i = key == kKeyLeft ? (int)v.size() - 1 : 0;
            else i = key == kKeyLeft ? (i + (int)v.size() - 1) % (int)v.size() : (i + 1) % (int)v.size();
            selectOnly(v[(size_t)i]);
            return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
           #ifdef __APPLE__
            { "Double-click / Cmd+click", "add a band there (Alt + double-click: a dynamic band); or drag the curve" },
           #else
            { "Double-click / Ctrl+click", "add a band there (Alt + double-click: a dynamic band); or drag the curve" },
           #endif
            { "Drag a node", "frequency and gain (all selected bands); Alt while dragging: one axis" },
            { "Ctrl/Cmd + drag up/down", "Q (width); mouse wheel on a node: Q, on cuts: slope" },
            { "Ctrl/Cmd + wheel  ·  Alt + wheel", "gain  ·  dynamic range" },
            { "Alt + hold a node", "solo it while held; drag: left/right frequency, up = narrower, down = wider" },
            { "Alt + hold / drag empty space", "listen to the region under the mouse; up/down (or wheel): width" },
            { "Ctrl/Cmd+click · Shift+click · drag", "add to selection · select a range · box-select" },
            { "Ctrl/Cmd+Alt+click · Alt+Shift+click", "next shape · next slope" },
            { "Double-click a node", "type frequency, Tab to gain and Q (1k, A4, C#3+13)" },
           #ifdef __APPLE__
            { "Right-click / Ctrl+click", "band menu (shape, placement, dynamic, copy, delete) or display menu (paste)" },
           #else
            { "Right-click", "band menu (shape, placement, dynamic, copy, delete) or display menu (paste)" },
           #endif
            { "Delete · B · Esc · Left/Right", "delete · bypass · deselect · previous/next band" },
            { "Ctrl/Cmd + A / C / V / D", "select all / copy / paste / duplicate bands" },
        };
    }

    void bandMenu(int b, float x, float y)
    {
        std::vector<std::string> it;
        it.push_back("#SHAPE");
        for (int s = 0; s < 8; ++s) it.push_back(std::string((int)bandShape(b) == s ? "● " : "   ") + kShapeNames[s]);
        it.push_back("#PLACEMENT");
        for (int p = 0; p < 5; ++p) it.push_back(std::string(bandPlace(b) == p ? "● " : "   ") + kPlaceNames[p]);
        it.push_back("-");
        it.push_back(bandDyn(b) ? "Dynamic: off" : "Dynamic: on");
        it.push_back(bandBypassed(b) ? "Enable band" : "Bypass band");
        it.push_back("Copy");
        it.push_back("Delete band");
        openMenu(Rect(x, y, 10, 10), it, -1, [this, b](int i) {
            const auto selv = selectedBands();
            beginGroup();
            if (i >= 1 && i <= 8) for (int k : selv) setParamGesture(bandParam(k, kShape), (float)(i - 1));
            else if (i >= 10 && i <= 14) for (int k : selv) setParamGesture(bandParam(k, kPlace), (float)(i - 10));
            else if (i == 16) setParamGesture(bandParam(b, kDyn), bandDyn(b) ? 0.0f : 1.0f);
            else if (i == 17) { const float v = bandBypassed(b) ? 0.0f : 1.0f; for (int k : selv) setParamGesture(bandParam(k, kBandBypass), v); }
            else if (i == 18) copyBands(selv);
            else if (i == 19) deleteSelected();
            endGroup();
            rebind();
        });
    }
    void backgroundMenu(float x, float y)
    {
        std::vector<std::string> it = { "Paste", "Copy all bands", "Select all", "-", "Delete all bands", "-", "Range ±3 dB", "Range ±6 dB", "Range ±12 dB", "Range ±24 dB", "Range ±30 dB" };
        openMenu(Rect(x, y, 10, 10), it, -1, [this](int i) {
            static const int rr[] = { 3, 6, 12, 24, 30 };
            if (i == 0) pasteBands();
            else if (i == 1) { std::vector<int> v; for (int b = 0; b < kNumBands; ++b) if (bandUsed(b)) v.push_back(b); copyBands(v); }
            else if (i == 2) { for (int b = 0; b < kNumBands; ++b) selMask[b] = bandUsed(b); rebind(); }
            else if (i == 4) { beginGroup(); for (int b = 0; b < kNumBands; ++b) if (bandUsed(b)) deleteBand(b); endGroup(); }
            else if (i >= 6) { dbRange = (float)rr[i - 6]; rangeBtn->label = "±" + std::to_string(rr[i - 6]); saveUi(); }
        });
    }

    void saveUi()
    {
        char buf[64]; std::snprintf(buf, sizeof buf, "range=%d;", (int)dbRange);
        saveUiState(buf);
    }
    void loadUiState(const std::string& s) override
    {
        const std::string r = uiStateGet(s, "range", "12");
        dbRange = (float)std::max(3, std::atoi(r.c_str()));
        rangeBtn->label = "±" + std::to_string((int)dbRange);
    }

private:
    PluginEQ* plug = nullptr;
    SpectrumView pre, post;
    Rect display, bandPanel, globalPanel, meterL, meterR;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float dbRange = 12.0f;
    int sel = -1, hover = -1, dragBand = -1;
    bool selMask[kNumBands] = {};
    DragMode dmode = dmNone;
    std::vector<int> dragSet;
    float orig[kNumBands][3] = {};
    float pressX = 0, pressY = 0, curX = 0, curY = 0, lastX = 0, lastY = 0, effX = 0, effY = 0;
    int axis = 0, soloBand = 0;
    float audF = 1000.0f, audQ = 2.0f;   // Alt-hold audition (sent to the plugin's atomics; the width is remembered)
    double anaRate = 48000.0;            // sample rate the analyzers are set up for
    bool initialSelect = false;
    void pushAudition(bool on)
    {
        if (!plug) return;
        plug->auditionFreq.store(audF); plug->auditionQ.store(audQ);
        plug->auditionOn.store(on);
    }
    bool soloHeld = false;   // Alt-hold solo active (stays on when the hold turns into a drag)
    static constexpr int kCurvePts = 220;
    float curveX[kCurvePts] = {}, curveDb[kCurvePts] = {};

    MenuButton *bandSel, *slopeMenu, *qualMenu, *anaMenu;
    Segmented *shapeSeg, *placeSeg, *phaseSeg;
    Dial *freqDial, *gainDial, *qDial, *rangeDial, *threshDial, *scaleDial, *outDial, *atkDial, *relDial;
    Button *dynBtn, *bandBypassBtn, *soloBtn, *delBtn, *autoBtn, *rangeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIEQ)
};

UI* createUI() { return new UIEQ(); }

END_NAMESPACE_DISTRHO
