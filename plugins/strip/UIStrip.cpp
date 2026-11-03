// pump STRIP: interface. The console row across the top (eight models, the order of EQ and dynamics, oversampling),
// then two screens: the EQ (the whole channel's curve with the model's own voicing, its filters and its four bands
// as handles over the output's spectrum: drag a band, wheel = Q, double-click = flat; drag the filter squares at
// the edges) and the dynamics (gate + compressor transfer curve with the live level, and the IN / GATE / COMP / OUT
// meters). Below, the strip from left to right as the signal goes: input, filters, EQ, gate, compressor, output.
#include "ui/PumpUI.hpp"
#include "PluginStrip.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::strip;
using pump::BandSpec;
using pump::analogPrototype;

class UIStrip : public PumpUI {
public:
    UIStrip() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::strip(), "STRIP", 1140, 600, kBypass)
    {
        modelSeg = addSegmented(kModel, { kModelKeys[0], kModelKeys[1], kModelKeys[2], kModelKeys[3], kModelKeys[4], kModelKeys[5], kModelKeys[6], kModelKeys[7] });
        modelSeg->tip = "Console: the whole channel (preamp, transformer, filter slopes, EQ curves, compressor) changes   (keys 1-8)";
        orderSeg = addSegmented(kOrder, { "EQ > DYN", "DYN > EQ" });
        orderSeg->tip = "Order: EQ before the gate and compressor, or after them";
        osSeg = addSegmented(kOversampling, { "1x", "2x", "4x" });
        osSeg->tip = "Oversampling: the whole strip runs at this multiple of the sample rate (less aliasing from the colour, a little latency)";

        inDial = addDial(kInput, "INPUT", Dial::Large, true);
        inDial->tip = "Input: how hard the preamp is driven (and everything after it). With AUTO GAIN the output follows it back down";
        colorDial = addDial(kColor, "COLOUR");
        colorDial->tip = "Colour: how much of the console's own sound (preamp harmonics, transformer, voicing, the compressor's grit). 0 = clean";
        phaseBtn = addToggle(kPhase, "PHASE");
        phaseBtn->small = true;
        phaseBtn->tip = "Polarity invert";

        hpfDial = addDial(kHpf, "HPF", Dial::Small);
        hpfDial->tip = "High-pass (the console's own slope)";
        lpfDial = addDial(kLpf, "LPF", Dial::Small);
        lpfDial->tip = "Low-pass";

        eqBtn = addToggle(kEqOn, "IN");
        eqBtn->small = true;
        eqBtn->tip = "EQ in/out";
        static const char* bandName[4] = { "LF", "LMF", "HMF", "HF" };
        const int gainP[4] = { kLfGain, kLmfGain, kHmfGain, kHfGain }, freqP[4] = { kLfFreq, kLmfFreq, kHmfFreq, kHfFreq };
        for (int b = 0; b < 4; ++b)
        {
            gainDial[b] = addDial(gainP[b], bandName[b], Dial::Medium, true);
            gainDial[b]->tip = std::string(bandName[b]) + " gain (or drag its handle on the curve)";
            freqDial[b] = addDial(freqP[b], "FREQ", Dial::Small);
            freqDial[b]->tip = std::string(bandName[b]) + " frequency";
            gainDial[b]->tabGroup = freqDial[b]->tabGroup = 1;
        }
        qDial[1] = addDial(kLmfQ, "Q", Dial::Small);
        qDial[2] = addDial(kHmfQ, "Q", Dial::Small);
        qDial[1]->tip = qDial[2]->tip = "Bandwidth (or the wheel over its handle). The console decides how Q follows gain";
        lfBellBtn = addToggle(kLfBell, "BELL");
        hfBellBtn = addToggle(kHfBell, "BELL");
        lfBellBtn->small = hfBellBtn->small = true;
        lfBellBtn->tip = hfBellBtn->tip = "Bell instead of shelf";

        gateBtn = addToggle(kGateOn, "ON");
        gateBtn->small = true;
        gateBtn->tip = "Gate / expander on";
        gateModeSeg = addSegmented(kGateMode, { "EXPAND", "GATE" });
        gateModeSeg->tip = "Expand: 1:3 below the threshold, soft · Gate: open or shut, with hold and hysteresis";
        gThrDial = addDial(kGateThresh, "THRESH", Dial::Small);
        gRangeDial = addDial(kGateRange, "RANGE", Dial::Small);
        gRelDial = addDial(kGateRelease, "RELEASE", Dial::Small);
        scDial = addDial(kScHpf, "SC HPF", Dial::Small);
        scDial->tip = "Sidechain high-pass: the gate and compressor hear less of the lows (no pumping on bass)";
        gThrDial->tabGroup = gRangeDial->tabGroup = gRelDial->tabGroup = scDial->tabGroup = 2;

        compBtn = addToggle(kCompOn, "ON");
        compBtn->small = true;
        compBtn->tip = "Compressor on";
        cThrDial = addDial(kCompThresh, "THRESH", Dial::Small);
        cRatioDial = addDial(kCompRatio, "RATIO", Dial::Small);
        cAtkDial = addDial(kCompAttack, "ATTACK", Dial::Small);
        cRelDial = addDial(kCompRelease, "RELEASE", Dial::Small);
        cMakeDial = addDial(kCompMakeup, "MAKEUP", Dial::Small);
        cMixDial = addDial(kCompMix, "MIX", Dial::Small);
        cMixDial->tip = "Mix: compressed vs uncompressed (parallel compression)";
        autoRelBtn = addToggle(kCompAuto, "AUTO REL");
        autoRelBtn->small = true;
        autoRelBtn->tip = "Auto release: quick after short peaks, slow after long compression";
        cThrDial->tabGroup = cRatioDial->tabGroup = cAtkDial->tabGroup = cRelDial->tabGroup = cMakeDial->tabGroup = cMixDial->tabGroup = 3;

        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        autoGainBtn = addToggle(kAutoGain, "AUTO GAIN");
        autoGainBtn->small = true;
        autoGainBtn->tip = "Auto gain: the output takes back what INPUT adds, so driving the preamp changes the colour, not the level";

        if (auto* p = static_cast<PluginStrip*>(pluginPtr())) plug = p;
        spec.setup(plug ? plug->getSampleRate() : 48000.0);
    }

protected:
    void onSampleRateChanged(double sr) override { spec.setup(sr); }

    std::string formatParam(int i, float v) const override
    {
        switch (i)
        {
        case kHpf: if (!hpfActive(v)) return "OFF"; break;
        case kLpf: if (!lpfActive(v)) return "OFF"; break;
        case kScHpf: if (!scActive(v)) return "OFF"; break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        modelSeg->r = Rect(14, 54, 846, 28);
        orderSeg->r = Rect(874, 54, 150, 28);
        osSeg->r = Rect(1036, 54, 90, 28);

        eqR = Rect(14, 94, 760, 198);
        dynR = Rect(788, 94, 338, 198);

        const float y0 = 306, ph = 262;
        inPanel = Rect(14, y0, 118, ph);
        inDial->r = Rect(25, y0 + 26, 96, Dial::heightFor(Dial::Large));
        colorDial->r = Rect(37, y0 + 128, 72, Dial::heightFor(Dial::Medium));
        phaseBtn->r = Rect(26, y0 + 222, 94, 24);

        filtPanel = Rect(142, y0, 86, ph);
        hpfDial->r = Rect(153, y0 + 30, 64, Dial::heightFor(Dial::Small));
        lpfDial->r = Rect(153, y0 + 116, 64, Dial::heightFor(Dial::Small));

        eqPanel = Rect(238, y0, 312, ph);
        eqBtn->r = Rect(eqPanel.r() - 52, y0 + 6, 42, 20);
        for (int b = 0; b < 4; ++b)
        {
            const float cx = 246 + b * 74.0f;
            gainDial[b]->r = Rect(cx + 1, y0 + 28, 72, Dial::heightFor(Dial::Medium));
            freqDial[b]->r = Rect(cx + 5, y0 + 112, 64, Dial::heightFor(Dial::Small));
        }
        qDial[1]->r = Rect(246 + 74 + 5, y0 + 186, 64, Dial::heightFor(Dial::Small));
        qDial[2]->r = Rect(246 + 148 + 5, y0 + 186, 64, Dial::heightFor(Dial::Small));
        lfBellBtn->r = Rect(252, y0 + 210, 62, 22);
        hfBellBtn->r = Rect(246 + 222 + 6, y0 + 210, 62, 22);

        gatePanel = Rect(560, y0, 150, ph);
        gateBtn->r = Rect(gatePanel.r() - 52, y0 + 6, 42, 20);
        gateModeSeg->r = Rect(570, y0 + 34, 130, 24);
        gThrDial->r = Rect(568, y0 + 70, 64, Dial::heightFor(Dial::Small));
        gRangeDial->r = Rect(638, y0 + 70, 64, Dial::heightFor(Dial::Small));
        gRelDial->r = Rect(568, y0 + 156, 64, Dial::heightFor(Dial::Small));
        scDial->r = Rect(638, y0 + 156, 64, Dial::heightFor(Dial::Small));

        compPanel = Rect(720, y0, 296, ph);
        compBtn->r = Rect(compPanel.r() - 52, y0 + 6, 42, 20);
        const float cx0 = 728;
        cThrDial->r = Rect(cx0, y0 + 30, 64, Dial::heightFor(Dial::Small));
        cRatioDial->r = Rect(cx0 + 72, y0 + 30, 64, Dial::heightFor(Dial::Small));
        cAtkDial->r = Rect(cx0 + 144, y0 + 30, 64, Dial::heightFor(Dial::Small));
        cRelDial->r = Rect(cx0 + 216, y0 + 30, 64, Dial::heightFor(Dial::Small));
        cMakeDial->r = Rect(cx0, y0 + 112, 64, Dial::heightFor(Dial::Small));
        cMixDial->r = Rect(cx0 + 72, y0 + 112, 64, Dial::heightFor(Dial::Small));
        autoRelBtn->r = Rect(cx0 + 150, y0 + 134, 128, 24);
        grBar = Rect(cx0 + 4, y0 + 226, 272, 12);

        outPanel = Rect(1026, y0, 100, ph);
        outDial->r = Rect(1040, y0 + 30, 72, Dial::heightFor(Dial::Medium));
        autoGainBtn->r = Rect(1034, y0 + 124, 84, 24);
    }

    // ---- live values ----
    void tick(float dt) override
    {
        if (!plug) return;
        inMeter.tick(plug->inPeak, dt);
        outMeter.tick(plug->outPeak, dt);
        const float cg = plug->compGr.exchange(0.0f), gg = plug->gateGr.exchange(0.0f);
        const float lv = plug->compLevel.exchange(0.0f);
        const float k = std::min(1.0f, dt * 14.0f);
        compShow += (cg - compShow) * (cg > compShow ? 1.0f : k);
        gateShow += (gg - gateShow) * (gg > gateShow ? 1.0f : k);
        if (lv > 0.0f) lvlShow += ((lv - 200.0f) - lvlShow) * std::min(1.0f, dt * 20.0f);
        else lvlShow = std::max(-120.0f, lvlShow - 40.0f * dt);
        spec.update(&plug->outRing, nullptr, dt);
    }

    Settings uiSettings() const
    {
        Settings s;
        s.model = pump::clampv((int)value(kModel), 0, kNumModels - 1);
        s.color = value(kColor);
        s.hpf = value(kHpf); s.lpf = value(kLpf);
        s.eqOn = value(kEqOn) >= 0.5f;
        s.g[0] = value(kLfGain); s.f[0] = value(kLfFreq);
        s.g[1] = value(kLmfGain); s.f[1] = value(kLmfFreq); s.q[1] = value(kLmfQ);
        s.g[2] = value(kHmfGain); s.f[2] = value(kHmfFreq); s.q[2] = value(kHmfQ);
        s.g[3] = value(kHfGain); s.f[3] = value(kHfFreq);
        s.lfBell = value(kLfBell) >= 0.5f; s.hfBell = value(kHfBell) >= 0.5f;
        return s;
    }
    double uiRate() const { return plug ? plug->getSampleRate() : 48000.0; }

    // ---- drawing ----
    void drawBody() override
    {
        drawEq();
        drawDyn();
        for (const Rect* p : { &inPanel, &filtPanel, &eqPanel, &gatePanel, &compPanel, &outPanel }) strokeRect(*p, theme.dim, 1.0f);
        caption(inPanel.x + 12, inPanel.y + 16, "INPUT");
        caption(filtPanel.x + 12, filtPanel.y + 16, "FILTERS");
        caption(eqPanel.x + 12, eqPanel.y + 16, "EQ");
        caption(gatePanel.x + 12, gatePanel.y + 16, "GATE");
        caption(compPanel.x + 12, compPanel.y + 16, "COMPRESSOR");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");

        const Profile& P = profile(model());
        char t[64];
        std::snprintf(t, sizeof t, "%d dB/OCT", P.hpfSlope);
        textAt(filtPanel.cx(), hpfDial->r.b() + 6, t, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "%d dB/OCT", P.lpfSlope);
        textAt(filtPanel.cx(), lpfDial->r.b() + 6, t, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(filtPanel.cx(), filtPanel.b() - 46, "CURVES", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        textAt(filtPanel.cx(), filtPanel.b() - 30, kEqFamilyShort[P.eq], kMono, 9.5f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);

        // the compressor: its design, and the gain reduction across the panel
        textAt(compPanel.x + 12, compPanel.y + 206, kCompName[P.comp], kLabel, 10.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        std::snprintf(t, sizeof t, "GR %.1f dB", compShow);
        textAt(compPanel.r() - 12, compPanel.y + 206, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        fillRect(grBar, theme.panel2);
        const float w = std::min(1.0f, compShow / 24.0f) * grBar.w;
        if (w > 0.5f) fillRect(Rect(grBar.r() - w, grBar.y, w, grBar.h), theme.second);
        for (int d = 6; d < 24; d += 6) line(grBar.r() - grBar.w * d / 24.0f, grBar.y, grBar.r() - grBar.w * d / 24.0f, grBar.b(), withAlpha2(theme.ink, 0.3f), 1.0f);
        strokeRect(grBar, theme.ink, 1.0f);
        textAt(grBar.x, grBar.b() + 9, "24", kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(grBar.r(), grBar.b() + 9, "0", kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);

        if (plug)
        {
            std::snprintf(t, sizeof t, "LATENCY %d", plug->latencyNow());
            textAt(outPanel.cx(), outPanel.b() - 22, t, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    int model() const { return pump::clampv((int)value(kModel), 0, kNumModels - 1); }

    // the channel's curve (filter stage + EQ stage) at f
    double curveDb(const BandSpec* b, const bool* used, double f, int from, int to) const
    {
        const double sr = std::max(44100.0, uiRate());
        const double w = pump::kTwoPi * f / (sr * 2.0);   // the strip runs oversampled: analog curves to 20 kHz
        double db = 0.0;
        for (int i = from; i < to; ++i)
            if (used[i]) db += 10.0 * std::log10(std::max(1e-30, analogPrototype(b[i], sr * 2.0).magSq(w)));
        return db;
    }

    float dbToY(const Rect& p, double db) const { return p.cy() - (float)(db / kRange) * p.h * 0.5f; }

    void drawEq()
    {
        const Rect& r = eqR;
        screenBox(r);
        const int m = model();
        textAt(r.x + 10, r.y + 14, kModelNames[m], kBold, 11.5f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(r.x + 14 + textWidth(kModelNames[m], kBold, 11.5f), r.y + 14, kModelAbout[m], kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.4f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        plot = p;
        save();
        scissor(p.x, p.y, p.w, p.h);
        drawFreqGrid(p, kLo, kHi, true);
        for (int d = -12; d <= 12; d += 6)
        {
            line(p.x, dbToY(p, d), p.r(), dbToY(p, d), d == 0 ? withAlpha2(theme.ink, 0.28f) : theme.grid, 1.0f);
            if (d != 0) { char b[8]; std::snprintf(b, sizeof b, "%+d", d); textAt(p.x + 4, dbToY(p, d) - 6, b, kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE); }
        }
        drawSpectrum(spec, p, -6.0f, -96.0f, withAlpha2(theme.ink, theme.dark ? 0.06f : 0.045f), withAlpha2(theme.ink, 0.22f), kLo, kHi, 1.0f);

        const Settings s = uiSettings();
        BandSpec b[kNumSlots]; bool used[kNumSlots];
        buildBands(s, b, used);
        constexpr int M = 260;
        float ys[M + 1], yv[M + 1];
        for (int i = 0; i <= M; ++i)
        {
            const double f = kLo * std::pow(kHi / kLo, (double)i / M);
            ys[i] = dbToY(p, pump::clampv(curveDb(b, used, f, 0, kNumSlots), -40.0, 40.0));
            // the console's own voicing alone (what COLOUR adds), faint
            yv[i] = dbToY(p, pump::clampv(curveDb(b, used, f, kSlotV0, kSlotV2 + 1), -40.0, 40.0));
        }
        auto xOf = [&](int i) { return p.x + p.w * i / M; };
        // fill between the curve and 0 dB
        beginPath();
        moveTo(p.x, dbToY(p, 0));
        for (int i = 0; i <= M; ++i) lineTo(xOf(i), ys[i]);
        lineTo(p.r(), dbToY(p, 0));
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.20f : 0.32f));
        fill();
        beginPath();
        for (int i = 0; i <= M; ++i) { if (i == 0) moveTo(xOf(i), yv[i]); else lineTo(xOf(i), yv[i]); }
        strokeColor(withAlpha2(theme.second, 0.8f)); strokeWidth(1.2f); stroke();
        beginPath();
        for (int i = 0; i <= M; ++i) { if (i == 0) moveTo(xOf(i), ys[i]); else lineTo(xOf(i), ys[i]); }
        strokeColor(s.eqOn ? theme.ink : withAlpha2(theme.ink, 0.45f)); strokeWidth(2.2f); lineJoin(ROUND); stroke();

        // handles: the filters (squares at the edge when off) and the four bands
        for (int h = 0; h < kHandles; ++h)
        {
            const Rect hr = handleRect(h);
            const bool active = handleActive(h);
            const bool hot = h == dragHandle || h == hoverHandle;
            if (h < 2)
            {
                fillRect(hr, active ? theme.ink : theme.panel);
                strokeRect(hr, theme.ink, 1.5f);
                textAt(hr.cx(), hr.y - 8, h == 0 ? "HP" : "LP", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            }
            else
            {
                beginPath(); circle(hr.cx(), hr.cy(), hot ? 9.0f : 7.5f);
                fillColor(active ? theme.neon : theme.panel); fill();
                strokeColor(theme.ink); strokeWidth(1.5f); stroke();
                static const char* n[4] = { "L", "LM", "HM", "H" };
                textAt(hr.cx(), hr.cy() + 0.5f, n[h - 2], kBold, 8.5f, active ? theme.neonInk : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        restore();
        char t[48];
        std::snprintf(t, sizeof t, "COLOUR %.0f%%", value(kColor));
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---- EQ handles: 0 HPF, 1 LPF, 2..5 LF LMF HMF HF ----
    static constexpr int kHandles = 6;
    static constexpr double kLo = 20.0, kHi = 20000.0, kRange = 18.0;
    int handleFreqParam(int h) const { static const int p[kHandles] = { kHpf, kLpf, kLfFreq, kLmfFreq, kHmfFreq, kHfFreq }; return p[h]; }
    int handleGainParam(int h) const { static const int p[kHandles] = { -1, -1, kLfGain, kLmfGain, kHmfGain, kHfGain }; return p[h]; }
    bool handleActive(int h) const
    {
        if (h == 0) return hpfActive(value(kHpf));
        if (h == 1) return lpfActive(value(kLpf));
        return value(kEqOn) >= 0.5f && std::fabs(value(handleGainParam(h))) >= 0.05f;
    }
    Rect handleRect(int h) const
    {
        const Rect& p = plot;
        if (h < 2)
        {
            const double f = h == 0 ? std::max(kLo, (double)value(kHpf)) : std::min(kHi, (double)value(kLpf));
            const float x = pump::clampv(freqToX(p, f, kLo, kHi), p.x + 7, p.r() - 7);
            return Rect(x - 6, p.b() - 34, 12, 12);
        }
        const float x = freqToX(p, value(handleFreqParam(h)), kLo, kHi);
        const float y = dbToY(p, pump::clampv((double)value(handleGainParam(h)), -kRange + 1, kRange - 1));
        return Rect(x - 9, y - 9, 18, 18);
    }
    int handleAt(float x, float y) const
    {
        int best = -1; float bd = 1e9f;
        for (int h = kHandles - 1; h >= 0; --h)
        {
            const Rect hr = handleRect(h);
            const float d = std::hypot(x - hr.cx(), y - hr.cy());
            if (d < 14.0f && d < bd) { bd = d; best = h; }
        }
        return best;
    }

    void drawDyn()
    {
        const Rect& r = dynR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "DYNAMICS");
        const Profile& P = profile(model());
        textAt(r.r() - 10, r.y + 14, kCompName[P.comp], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        const Rect b(r.x + 12, r.y + 28, 160, 160);
        tfR = b;
        auto xOf = [&](double db) { return b.x + (float)((db + 60.0) / 60.0) * b.w; };
        auto yOf = [&](double db) { return b.b() - (float)((pump::clampv(db, -60.0, 0.0) + 60.0) / 60.0) * b.h; };
        fillRect(b, theme.panel2);
        for (int d = -48; d < 0; d += 12) { line(xOf(d), b.y, xOf(d), b.b(), theme.grid, 1.0f); line(b.x, yOf(d), b.r(), yOf(d), theme.grid, 1.0f); }
        line(b.x, b.b(), b.r(), b.y, withAlpha2(theme.ink, 0.25f), 1.0f);
        const bool gOn = value(kGateOn) >= 0.5f, cOn = value(kCompOn) >= 0.5f;
        const double T = value(kCompThresh), ratio = value(kCompRatio), gT = value(kGateThresh), gRange = value(kGateRange);
        const int gMode = (int)value(kGateMode);
        auto transfer = [&](double in) {
            double out = in;
            if (gOn) out += gateStatic(gMode, in, gT, gRange);
            if (cOn) out += compStatic(P.comp, in, T, ratio);
            return out;
        };
        save(); scissor(b.x, b.y, b.w, b.h);
        beginPath();
        for (int i = 0; i <= 120; ++i) { const double in = -60.0 + i * 0.5; if (i == 0) moveTo(xOf(in), yOf(transfer(in))); else lineTo(xOf(in), yOf(transfer(in))); }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        if (cOn) line(xOf(T), b.y, xOf(T), b.b(), withAlpha2(theme.second, 0.6f), 1.0f);
        if (gOn) line(xOf(gT), b.y, xOf(gT), b.b(), withAlpha2(theme.neon, 0.9f), 1.0f);
        if (lvlShow > -60.0f && (gOn || cOn))
        {
            beginPath(); circle(xOf(lvlShow), yOf(transfer(lvlShow)), 4.5f);
            fillColor(theme.second); fill(); strokeColor(theme.ink); strokeWidth(1.2f); stroke();
        }
        restore();
        strokeRect(b, theme.ink, 1.0f);
        textAt(b.x + 3, b.b() + 9, "-60", kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(b.r(), b.b() + 9, "IN 0", kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);

        // meters: IN (L R), GATE, COMP, OUT (L R)
        const float mx = b.r() + 18, mh = 150, my = b.y;
        auto label = [&](float x, const char* s) { textAt(x, my + mh + 12, s, kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f); };
        drawOutMeter(inMeter, Rect(mx, my, 8, mh), Rect(mx + 10, my, 8, mh));
        label(mx + 9, "IN");
        drawGRMeter(Rect(mx + 38, my, 14, mh), -gateShow, 40.0f);
        label(mx + 45, "GATE");
        drawGRMeter(Rect(mx + 74, my, 14, mh), -compShow, 24.0f);
        label(mx + 81, "COMP");
        drawOutMeter(outMeter, Rect(mx + 108, my, 8, mh), Rect(mx + 118, my, 8, mh));
        label(mx + 117, "OUT");
    }

    // ---- interaction on the screens ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!plot.contains(x, y) || button != kMouseButtonLeft) return false;
        const int h = handleAt(x, y);
        if (h < 0) return false;
        if (dbl || cmdKey(mod))
        {
            beginGroup();
            if (h == 0) setParamGesture(kHpf, kHpfOff);
            else if (h == 1) setParamGesture(kLpf, kLpfOff);
            else setParamGesture(handleGainParam(h), 0.0f);
            endGroup();
            return false;
        }
        dragHandle = h;
        dragX = x; dragY = y;
        dragF = value(handleFreqParam(h));
        if (h == 0 && !hpfActive(dragF)) dragF = 20.0f;
        if (h == 1 && !lpfActive(dragF)) dragF = 20000.0f;
        dragG = h >= 2 ? value(handleGainParam(h)) : 0.0f;
        beginEdit(handleFreqParam(h));
        if (h >= 2) beginEdit(handleGainParam(h));
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (dragHandle < 0) return;
        const float fine = (mod & kModifierShift) ? 0.2f : 1.0f;
        const int h = dragHandle;
        const double oct = (x - dragX) / plot.w * std::log2(kHi / kLo) * fine;
        double f = dragF * std::pow(2.0, oct);
        if (h == 0) f = f < 19.0 ? kHpfOff : f;          // drag off the left edge: off
        if (h == 1) f = f > 21000.0 ? kLpfOff : f;       // off the right edge: off
        setParam(handleFreqParam(h), (float)f);
        if (h >= 2) setParam(handleGainParam(h), dragG + (dragY - y) / (plot.h * 0.5f) * (float)kRange * fine);
        if (mod & kModifierShift) { dragF = value(handleFreqParam(h)); if (h >= 2) dragG = value(handleGainParam(h)); dragX = x; dragY = y; }
        setStatus(h >= 2 ? paramStatus(handleGainParam(h)) + "    " + paramStatus(handleFreqParam(h)) : paramStatus(handleFreqParam(h)));
    }
    void bodyMouseUp(float, float) override
    {
        if (dragHandle < 0) return;
        if (dragHandle >= 2) endEdit(handleGainParam(dragHandle));
        endEdit(handleFreqParam(dragHandle));
        dragHandle = -1;
    }
    void bodyHover(float x, float y) override
    {
        hoverHandle = plot.contains(x, y) ? handleAt(x, y) : -1;
        if (hoverHandle >= 2) setStatus("Drag: frequency and gain · wheel: Q (mids) · double-click: flat · Shift: fine");
        else if (hoverHandle >= 0) setStatus("Drag the filter left/right (off at the edge) · double-click: off");
        else if (eqR.contains(x, y)) setStatus(std::string(kModelNames[model()]) + ": " + kModelAbout[model()] + "   (the faint line is the console's own voicing)");
        else if (dynR.contains(x, y)) setStatus("Gate + compressor transfer curve with the live level; meters: in, gate, compressor gain reduction, out");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        if (!plot.contains(x, y)) return false;
        const int h = handleAt(x, y);
        if (h == 3 || h == 4)
        {
            const int qp = h == 3 ? kLmfQ : kHmfQ;
            const float step = (mod & kModifierShift) ? 1.02f : 1.12f;
            setParamGesture(qp, value(qp) * (dy > 0 ? step : 1.0f / step));
            setStatus(paramStatus(qp));
            return true;
        }
        return false;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '8') { setParamGesture(kModel, (float)(key - '1')); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "1 - 8", "console: clean, class A, console 79, console 87, discrete, valve, studio 68, broadcast" },
            { "Drag a band", "frequency and gain (Shift = fine); wheel = Q on the mids; double-click = flat" },
            { "Drag HP / LP", "filter frequency; past the edge = off; double-click = off" },
        };
    }
    std::string footerInfo() override
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s   %s   %s OVERSAMPLING   LATENCY %d", kModelNames[model()],
                      kOrderNames[pump::clampv((int)value(kOrder), 0, 1)], kOsNames[pump::clampv((int)value(kOversampling), 0, 2)], plug ? plug->latencyNow() : 0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr const char* kEqFamilyShort[5] = { "CONSTANT Q", "PROPORTIONAL", "INDUCTOR", "PASSIVE", "WIDE" };
    static constexpr const char* kCompName[8] = { "CLEAN FEED-FORWARD", "VCA", "VCA · HARD KNEE", "FEEDBACK · THRUST", "DIODE BRIDGE", "OPTICAL", "VARIABLE-MU", "LIMITER" };

    PluginStrip* plug = nullptr;
    SpectrumView spec;
    OutMeter inMeter, outMeter;
    float compShow = 0.0f, gateShow = 0.0f, lvlShow = -120.0f;
    Rect eqR, dynR, plot, tfR, grBar, inPanel, filtPanel, eqPanel, gatePanel, compPanel, outPanel;
    int dragHandle = -1, hoverHandle = -1;
    float dragX = 0, dragY = 0, dragF = 0, dragG = 0;

    Segmented *modelSeg, *orderSeg, *osSeg, *gateModeSeg;
    Dial *inDial, *colorDial, *hpfDial, *lpfDial, *gainDial[4], *freqDial[4], *qDial[4] = {}, *gThrDial, *gRangeDial, *gRelDial, *scDial;
    Dial *cThrDial, *cRatioDial, *cAtkDial, *cRelDial, *cMakeDial, *cMixDial, *outDial;
    Button *phaseBtn, *eqBtn, *lfBellBtn, *hfBellBtn, *gateBtn, *compBtn, *autoRelBtn, *autoGainBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIStrip)
};

UI* createUI() { return new UIStrip(); }

END_NAMESPACE_DISTRHO
