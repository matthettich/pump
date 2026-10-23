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
        bandSel->tip = "Selected band (◀ ▶ step through bands)";

        shapeSeg = addSegmented(-1, { "BELL", "LO SH", "HI SH", "LO CUT", "HI CUT", "NOTCH", "B PASS", "TILT" });
        freqDial = addDial(-1, "FREQ", Dial::Medium);
        gainDial = addDial(-1, "GAIN", Dial::Medium, true);
        qDial = addDial(-1, "Q", Dial::Medium);
        slopeMenu = addMenu(-1, "SLOPE");
        placeSeg = addSegmented(-1, { "ST", "L", "R", "M", "S" });
        placeSeg->tip = "Stereo placement: both channels, left, right, mid or side";
        dynBtn = addToggle(-1, "DYNAMIC");
        dynBtn->small = true;
        rangeDial = addDial(-1, "RANGE", Dial::Small, true);
        threshDial = addDial(-1, "THRESH", Dial::Small);
        bandBypassBtn = addToggle(-1, "BYPASS");
        bandBypassBtn->small = true;
        soloBtn = addButton("SOLO", [this]() { if (sel >= 0) setParamGesture(kSolo, (int)value(kSolo) == sel + 1 ? 0.0f : (float)(sel + 1)); },
                            [this]() { return sel >= 0 && (int)value(kSolo) == sel + 1; });
        soloBtn->small = true;
        soloBtn->tip = "Listen to just this band's region";
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
        pre.setup(sampleRate() > 0 ? sampleRate() : 48000.0);
        post.setup(sampleRate() > 0 ? sampleRate() : 48000.0);
        pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
        rebind();
    }

protected:
    static constexpr double kLo = 15.0, kHi = 22000.0;

    // ---------------------------------------------------------------- layout
    void layout() override
    {
        display = Rect(14, 58, W - 28, 392);
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
        x = xOfF(bandFreq(b));
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
        rebind();
        repaint();
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
            // shelves only have 6/12/24
            slopeMenu->itemsFn = nullptr;
            freqDial->label = "FREQ";
            qDial->label = (s == pump::Shape::LowCut || s == pump::Shape::HighCut || s == pump::Shape::LowShelf || s == pump::Shape::HighShelf || s == pump::Shape::TiltShelf) ? "RESO" : "Q";
        }
        qualMenu->enabled = (int)value(kPhase) == 1;
    }

    int freeBand() const { for (int b = 0; b < kNumBands; ++b) if (!bandUsed(b)) return b; return -1; }

    int createBand(double f, float gainDb)
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
        set(kShape, (float)shape); set(kFreq, (float)f); set(kGain, std::round(g * 10.0f) / 10.0f); set(kQ, q);
        set(kSlope, (float)slope); set(kPlace, 0); set(kDyn, 0); set(kBandBypass, 0);
        set(kDynRange, -6.0f); set(kDynThresh, -30.0f);
        set(kUsed, 1.0f);
        select(b);
        return b;
    }
    void deleteBand(int b)
    {
        if ((int)value(kSolo) == b + 1) setParamGesture(kSolo, 0);
        setParamGesture(bandParam(b, kUsed), 0.0f);
        if (sel == b) { sel = -1; for (int k = 0; k < kNumBands; ++k) if (bandUsed(k)) { sel = k; break; } }
        rebind();
    }

    void onParamChanged(int i, float) override
    {
        const int b = bandOf(i);
        if (b >= 0 && fieldOf(i) == kUsed && !bandUsed(b) && sel == b) sel = -1;
        if (sel < 0) for (int k = 0; k < kNumBands; ++k) if (bandUsed(k)) { sel = k; break; }
        rebind();
    }
    void onPresetApplied() override { sel = -1; for (int k = 0; k < kNumBands; ++k) if (bandUsed(k)) { sel = k; break; } rebind(); }

    // ---------------------------------------------------------------- drawing
    void tick(float dt) override
    {
        if (!plug) return;
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
            const bool isSel = b == sel, byp = bandBypassed(b), hov = b == hover;
            const float rr = isSel ? 9.0f : 8.0f;
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
        else if (sel < 0)
            textAt(display.cx(), display.cy() - 40, "DOUBLE-CLICK TO ADD A BAND", kLabel, 14.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 2.0f);
        restore();

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
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!display.contains(x, y)) return false;
        const int b = nodeAt(x, y);
        if (button == kMouseButtonRight)
        {
            if (b >= 0) { select(b); bandMenu(b, x, y); }
            return false;
        }
        if (b >= 0)
        {
            if (dbl) { setParamGesture(bandParam(b, kBandBypass), bandBypassed(b) ? 0.0f : 1.0f); return false; }
            if (mod & (kModifierControl | kModifierSuper)) { deleteBand(b); return false; }
            select(b);
            dragBand = b;
            dragX = x; dragY = y;
            dragF = bandFreq(b); dragG = bandGain(b); dragQ = bandQ(b);
            dragQMode = (mod & kModifierAlt) != 0;
            beginEdit(bandParam(b, kFreq)); beginEdit(bandParam(b, kGain)); beginEdit(bandParam(b, kQ));
            return true;
        }
        if (dbl)
        {
            const double scale = std::max(0.05f, std::fabs(value(kGainScale) * 0.01f));
            const int nb = createBand(fOfX(x), dbOfY(y) / (float)scale);
            if (nb >= 0)
            {
                dragBand = nb; dragX = x; dragY = y; dragF = bandFreq(nb); dragG = bandGain(nb); dragQ = bandQ(nb); dragQMode = false;
                beginEdit(bandParam(nb, kFreq)); beginEdit(bandParam(nb, kGain)); beginEdit(bandParam(nb, kQ));
                return true;
            }
            return false;
        }
        select(-1);
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (dragBand < 0) return;
        const int b = dragBand;
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        if (dragQMode)
        {
            const float q = dragQ * std::pow(2.0f, -(y - dragY) / 60.0f * fine);
            setParam(bandParam(b, kQ), q);
            return;
        }
        // frequency from x (log), gain from y; Shift = fine
        const double f = dragF * std::pow(kHi / kLo, (double)((x - dragX) / display.w * fine));
        setParam(bandParam(b, kFreq), (float)f);
        if (pump::shapeHasGain(bandShape(b)))
        {
            const double scale = std::max(0.05f, std::fabs(value(kGainScale) * 0.01f));
            setParam(bandParam(b, kGain), dragG + (dbOfY(y) - dbOfY(dragY)) * fine / (float)scale);
        }
        else
        {
            const float q = dragQ * std::pow(2.0f, -(y - dragY) / 60.0f * fine);
            setParam(bandParam(b, kQ), q);
        }
        if (mod & kModifierShift) { dragX = x; dragY = y; dragF = bandFreq(b); dragG = bandGain(b); dragQ = bandQ(b); }
    }
    void bodyMouseUp(float, float) override
    {
        if (dragBand >= 0)
        {
            endEdit(bandParam(dragBand, kFreq)); endEdit(bandParam(dragBand, kGain)); endEdit(bandParam(dragBand, kQ));
        }
        dragBand = -1;
    }
    void bodyHover(float x, float y) override
    {
        hover = display.contains(x, y) ? nodeAt(x, y) : -1;
        if (hover >= 0) setStatus("Band " + std::to_string(hover + 1) + ": drag to move, Alt-drag or wheel for Q, double-click to bypass, right-click for options");
        else if (display.contains(x, y))
        {
            char buf[64]; std::snprintf(buf, sizeof buf, "%s   %+.1f dB", spec(bandParam(0, kFreq)).format((float)fOfX(x)).c_str(), dbOfY(y));
            setStatus(buf);
        }
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        const int b = nodeAt(x, y);
        if (b < 0) return false;
        const float k = (mod & kModifierShift) ? 0.02f : 0.1f;
        setParamGesture(bandParam(b, kQ), bandQ(b) * std::pow(2.0f, dy > 0 ? k * 4 : -k * 4));
        setStatus(paramStatus(bandParam(b, kQ)));
        return true;
    }
    bool keyPress(uint key, uint) override
    {
        if ((key == kKeyDelete || key == kKeyBackspace) && sel >= 0) { deleteBand(sel); return true; }
        return false;
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
        it.push_back("Delete band");
        openMenu(Rect(x, y, 10, 10), it, -1, [this, b](int i) {
            if (i >= 1 && i <= 8) setParamGesture(bandParam(b, kShape), (float)(i - 1));
            else if (i >= 10 && i <= 14) setParamGesture(bandParam(b, kPlace), (float)(i - 10));
            else if (i == 16) setParamGesture(bandParam(b, kDyn), bandDyn(b) ? 0.0f : 1.0f);
            else if (i == 17) setParamGesture(bandParam(b, kBandBypass), bandBypassed(b) ? 0.0f : 1.0f);
            else if (i == 18) deleteBand(b);
            rebind();
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
    Rect display, bandPanel, globalPanel;
    float dbRange = 12.0f;
    int sel = -1, hover = -1, dragBand = -1;
    float dragX = 0, dragY = 0, dragF = 0, dragG = 0, dragQ = 1;
    bool dragQMode = false;

    MenuButton *bandSel, *slopeMenu, *qualMenu, *anaMenu;
    Segmented *shapeSeg, *placeSeg, *phaseSeg;
    Dial *freqDial, *gainDial, *qDial, *rangeDial, *threshDial, *scaleDial, *outDial, *atkDial, *relDial;
    Button *dynBtn, *bandBypassBtn, *soloBtn, *delBtn, *autoBtn, *rangeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIEQ)
};

UI* createUI() { return new UIEQ(); }

END_NAMESPACE_DISTRHO
