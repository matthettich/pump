// pump BAND: interface. Display: spectrum (in / out) behind the band regions; each band's threshold as a line on
// the level scale with the band's detector level beside it; a GAIN lane on top with the live combined response
// (sum of band magnitudes x band gains); crossover handles; per-band S / M / B keys. Below: the selected band's
// controls with its transfer curve, and the global settings.
#include "ui/MultibandUI.hpp"
#include "PluginBand.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::band;

class UIBand : public MultibandUI {
public:
    UIBand() : MultibandUI(params().data(), kNumParams, &presets(), pump::accents::band(), "BAND", 1000, 640, kBypass)
    {
        // ---- selected band ----
        bandSel = add(new MenuButton());
        bandSel->arrows = true;
        bandSel->itemsFn = [this]() {
            std::vector<std::string> v;
            for (int b = 0; b < numBands(); ++b) v.push_back("Band " + std::to_string(b + 1) + "  " + bandName(b, numBands()) + "  (" + bandRange(b) + ")");
            return v;
        };
        bandSel->getFn = [this]() { return sel; };
        bandSel->setFn = [this](int i) { select(i); };
        bandSel->textFn = [this]() { return "BAND " + std::to_string(sel + 1) + "  " + bandName(sel, numBands()); };
        bandSel->tip = "Selected band (side arrows, keys 1-4 or click a band in the display)";
        modeSeg = addSegmented(-1, { "COMPRESS", "EXPAND", "UPWARD" });
        modeSeg->tip = "Compress: turn down above the threshold · Expand: turn down below it · Upward: lift quiet parts up toward it";
        thrDial = addDial(-1, "THRESH");
        ratioDial = addDial(-1, "RATIO");
        kneeDial = addDial(-1, "KNEE");
        rangeDial = addDial(-1, "RANGE");
        atkDial = addDial(-1, "ATTACK");
        relDial = addDial(-1, "RELEASE");
        mkDial = addDial(-1, "MAKEUP", Dial::Medium, true);
        for (Dial* d : { thrDial, ratioDial, kneeDial, rangeDial, atkDial, relDial, mkDial }) d->tabGroup = 1;
        thrDial->tip = "Threshold: Compress acts above it, Expand and Upward below it (or drag the band's line in the display)";
        ratioDial->tip = "Ratio (20 = infinite: limit, gate or full lift) · Ctrl/Cmd + drag up/down in the band does the same";
        kneeDial->tip = "Knee: how gradually the band starts to act around the threshold (dB)";
        rangeDial->tip = "Range: the most this band may be turned down (Compress, Expand) or lifted (Upward)";
        atkDial->tip = "Attack: how fast the band reacts as it gets louder";
        relDial->tip = "Release: how fast the band recovers as it gets quieter";
        mkDial->tip = "Makeup gain for this band (or drag in the GAIN lane at the top of the display)";
        soloBtn = addToggle(-1, "SOLO");
        muteBtn = addToggle(-1, "MUTE");
        bypBtn = addToggle(-1, "BYPASS");
        Button* bs[3] = { soloBtn, muteBtn, bypBtn };
        for (int t = 0; t < 3; ++t)
        {
            Button* b = bs[t];
            b->small = true;
            b->holdMomentary = true;
            b->onExclusive = [this, t, b]() { exclusive(sel, t); b->pressTime = 1e300; };
        }
        soloBtn->tip = "Solo the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band · Alt-hold in the display)";
        muteBtn->tip = "Mute the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)";
        bypBtn->tip = "Bypass the selected band's dynamics (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)";

        // ---- globals ----
        xoverSeg = addSegmented(kXoverMode, { "ANALOG", "LINEAR" });
        xoverSeg->tip = "Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)";
        bandsSeg = addSegmented(kNumBandsP, { "1", "2", "3", "4" });
        bandsSeg->tip = "Number of bands";
        detSeg = addSegmented(kDetect, { "PEAK", "RMS" });
        detSeg->tip = "Detection: Peak (fast, catches transients) or RMS (average level, smoother)";
        autoBtn = addToggle(kAutoMakeup, "AUTO MAKEUP");
        autoBtn->small = true;
        autoBtn->tip = "Auto makeup (Compress bands): adds back half of what the band's curve takes off a -6 dBFS level; fixed, so it never pumps";
        laDial = addDial(kLookahead, "LOOKAHEAD");
        laDial->tip = "Lookahead: the bands' audio is delayed so the detectors see peaks coming (adds latency)";
        linkDial = addDial(kLink, "LINK");
        linkDial->tip = "Stereo link: 100% = both channels get the same gain, 0% = each channel on its own";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: parallel processing (each band blends with its own unprocessed signal, so no phase issues)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        laneBtn = addButton("GAIN ±6", [this]() {
            static const int r[] = { 6, 12, 24, 48 };
            int i = 0; for (int k = 0; k < 4; ++k) if ((int)laneRange == r[k]) i = k;
            laneRange = (float)r[(i + 1) % 4];
            laneBtn->label = "GAIN ±" + std::to_string((int)laneRange);
            saveUi();
        });
        laneBtn->small = true;
        laneBtn->tip = "Range of the GAIN lane (click to cycle ±6 / ±12 / ±24 / ±48 dB)";

        buildStrip();
        if (auto* p = static_cast<PluginBand*>(pluginPtr())) { plug = p; bindListen(&p->listenHold); }
        followRate(plug && plug->getSampleRate() > 0.0 ? plug->getSampleRate() : rate());
        rebind();
    }

protected:
    // ---- MultibandUI hooks ----
    void setupAnalyzers(double sr) override { pre.setup(sr); post.setup(sr); pre.setRange(kLo, kHi); post.setRange(kLo, kHi); }
    int pBands() const override { return kNumBandsP; }
    int pXover(int k) const override { return xoverParam(k); }
    int pSolo(int b) const override { return bandParam(b, kSolo); }
    int pMute(int b) const override { return bandParam(b, kMute); }
    int pBandBypass(int b) const override { return bandParam(b, kBandBypass); }
    int pMain(int b) const override { return bandParam(b, kThresh); }
    int pSecond(int b) const override { return bandParam(b, kRatio); }
    int bandFirstParam(int b) const override { return bandParam(b, 0); }
    int bandParamCount() const override { return kBandStride; }
    std::string mainName() const override { return "threshold"; }
    std::string secondName() const override { return "ratio"; }
    std::string stripValue(int b) override
    {
        char t[16];
        if (value(bandParam(b, kBandBypass)) >= 0.5f) return "BYP";
        if (std::fabs(grShow[b]) < 0.05f) return "0.0";
        std::snprintf(t, sizeof t, "%+.1f", grShow[b]);
        return t;
    }
    void onSelect(int) override { rebind(); saveUi(); }
    float mainFromDrag(int b, float start, float up) override { return spec(pMain(b)).clamp(start + up / pxPerDbLevel()); }
    float mainWheel(int, float v, float dir, bool fine) override { return v + dir * (fine ? 0.1f : 0.5f); }
    bool regionGrab(int b, float, float y, uint) override
    {
        if (y < laneTop() || y > laneBot()) return false;
        grabBand = b; grabStart = value(bandParam(b, kMakeup)); grabEff = 0.0f; grabLastY = y;
        beginEdit(bandParam(b, kMakeup));
        return true;
    }
    void regionGrabDrag(float, float y, uint mod) override
    {
        if (grabBand < 0) return;
        const float fine = (mod & kModifierShift) ? 0.15f : 1.0f;
        grabEff += (grabLastY - y) * fine; grabLastY = y;
        const float v = grabStart + grabEff / pxPerDbLane();
        setParam(bandParam(grabBand, kMakeup), spec(bandParam(grabBand, kMakeup)).clamp(v));
        setStatus(paramStatus(bandParam(grabBand, kMakeup)));
    }
    void regionGrabEnd() override { if (grabBand >= 0) endEdit(bandParam(grabBand, kMakeup)); grabBand = -1; }
    std::vector<std::pair<std::string, std::string>> pluginHelp() override
    {
        return {
            { "Drag in the GAIN lane", "the band's makeup gain (Shift: fine)" },
            { "GAIN ± key", "range of the GAIN lane: 6 / 12 / 24 / 48 dB" },
        };
    }

    // ---- layout ----
    void layout() override
    {
        disp = Rect(14, 58, W - 28 - 34, 334);
        meterL = Rect(disp.r() + 12, disp.y, 9, disp.h - 18);
        meterR = Rect(meterL.r() + 2, disp.y, 9, disp.h - 18);
        laneBtn->r = Rect(disp.r() - 82, laneTop() + 5, 74, 18);
        const float y0 = 408;
        bandPanel = Rect(14, y0, 626, 192);
        bandSel->r = Rect(26, y0 + 12, 170, 26);
        modeSeg->r = Rect(206, y0 + 12, 254, 26);
        Dial* ds[7] = { thrDial, ratioDial, kneeDial, rangeDial, atkDial, relDial, mkDial };
        for (int i = 0; i < 7; ++i) ds[i]->r = Rect(22.0f + 63.0f * (float)i, y0 + 50, 62, 84);
        soloBtn->r = Rect(26, y0 + 150, 58, 26);
        muteBtn->r = Rect(92, y0 + 150, 58, 26);
        bypBtn->r = Rect(158, y0 + 150, 66, 26);
        grBar = Rect(242, y0 + 162, 222, 12);
        inset = Rect(484, y0 + 34, 144, 144);

        globalPanel = Rect(654, y0, W - 668, 192);
        const float gx = 666;
        xoverSeg->r = Rect(gx, y0 + 30, 170, 24);
        bandsSeg->r = Rect(gx + 182, y0 + 30, 126, 24);
        detSeg->r = Rect(gx, y0 + 80, 120, 24);
        autoBtn->r = Rect(gx + 182, y0 + 80, 126, 24);
        Dial* gs[4] = { laDial, linkDial, mixDial, outDial };
        for (int i = 0; i < 4; ++i) gs[i]->r = Rect(gx - 4.0f + 80.0f * (float)i, y0 + 110, 72, 80);
    }

    // ---- scales ----
    float laneTop() const { return disp.y + kStripH + 1.0f; }
    float laneBot() const { return disp.y + kStripH + 76.0f; }
    float laneZero() const { return 0.5f * (laneTop() + laneBot()); }
    float pxPerDbLane() const { return (laneBot() - laneTop() - 12.0f) * 0.5f / laneRange; }
    float lvTop() const { return laneBot() + 12.0f; }
    float lvBot() const { return disp.b() - 46.0f; }
    float pxPerDbLevel() const { return (lvBot() - lvTop()) / 60.0f; }
    float yOfLevel(float db) const { return lvTop() - db * pxPerDbLevel(); }

    // ---- binding ----
    void rebind()
    {
        sel = pump::clampv(sel, 0, numBands() - 1);
        auto bind = [&](Control* c, int field) { c->param = bandParam(sel, field); };
        bind(modeSeg, kMode); bind(thrDial, kThresh); bind(ratioDial, kRatio); bind(kneeDial, kKnee); bind(rangeDial, kRange);
        bind(atkDial, kAttack); bind(relDial, kRelease); bind(mkDial, kMakeup); bind(soloBtn, kSolo); bind(muteBtn, kMute); bind(bypBtn, kBandBypass);
        const int mode = (int)value(bandParam(sel, kMode));
        rangeDial->tip = mode == kUpward ? "Range: the most this band may be lifted" : "Range: the most this band may be turned down";
        const bool byp = value(bandParam(sel, kBandBypass)) >= 0.5f;
        for (Dial* d : { thrDial, ratioDial, kneeDial, rangeDial, atkDial, relDial, mkDial }) d->enabled = !byp;
        laDial->enabled = true;
    }
    void onParamChanged(int i, float) override
    {
        if (i == kNumBandsP && sel >= numBands()) sel = numBands() - 1;
        if (i == kNumBandsP || bandOf(i) == sel) rebind();
    }
    void onPresetApplied() override { stopListening(); sel = pump::clampv(sel, 0, numBands() - 1); rebind(); }

    // ---- feeds ----
    void tick(float dt) override
    {
        if (!plug) return;
        followRate(plug->getSampleRate());   // (re-sets the analyzers after a rate change)
        outMeter.tick(plug->outPeak, dt);
        const float k = std::min(1.0f, dt * 18.0f);
        for (int b = 0; b < kBands; ++b)
        {
            const float g = plug->engine.gainDb(b);
            grShow[b] += (g - grShow[b]) * k;
            const float l = plug->engine.levelDb(b);
            lvShow[b] = l > lvShow[b] ? l : lvShow[b] + (l - lvShow[b]) * std::min(1.0f, dt * 8.0f);
            mkShow[b] = plug->engine.makeupDb(b);
        }
        pre.update(&plug->preRing, nullptr, dt);
        post.update(&plug->postRing, nullptr, dt);
    }

    // ---- drawing ----
    Color mixDeep() const { return pump::mixColor(theme.neon, theme.ink, 0.45f); }

    void drawBody() override
    {
        screenBox(disp);
        save();
        scissor(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2);
        drawRegionsUnder();
        const int nb = numBands();
        double e[3]; effXo(e);
        const double sr = rate();

        // level grid and spectrum
        for (int db = 0; db >= -60; db -= 12)
        {
            const float y = yOfLevel((float)db);
            line(disp.x, y, disp.r(), y, db == 0 ? withAlpha2(theme.ink, 0.18f) : theme.grid, 1.0f);
        }
        drawFreqGrid(Rect(disp.x, laneBot(), disp.w, disp.b() - laneBot()), kLo, kHi);
        const Rect sp(disp.x, lvTop(), disp.w, disp.b() - lvTop());
        const float dbBottom = -(disp.b() - lvTop()) / pxPerDbLevel();
        drawSpectrum(pre, sp, 0.0f, dbBottom, withAlpha2(theme.ink, theme.dark ? 0.08f : 0.05f), withAlpha2(theme.ink, 0.22f), kLo, kHi, 1.0f);
        drawSpectrum(post, sp, 0.0f, dbBottom, withAlpha2(theme.neon, theme.dark ? 0.24f : 0.34f), withAlpha2(mixDeep(), 0.75f), kLo, kHi, 1.1f);
        for (int db = -12; db >= -48; db -= 12)
        {
            char t[8]; std::snprintf(t, sizeof t, "%d", db);
            textAt(disp.x + 5, yOfLevel((float)db) - 6, t, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }

        // per band: action zone, threshold line, level meter
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            if (x1 - x0 < 8) continue;
            const int mode = (int)value(bandParam(b, kMode));
            const float thr = value(bandParam(b, kThresh));
            const float yt = yOfLevel(thr);
            const bool isSel = b == sel;
            // the zone the band acts in: above the threshold (Compress) or below it (Expand, Upward), fading away
            const Color zc = withAlpha2(theme.second, theme.dark ? 0.16f : 0.13f), z0 = withAlpha2(theme.second, 0.0f);
            const float zt = mode == kCompress ? lvTop() : yt, zb = mode == kCompress ? yt : lvBot();
            if (zb - zt > 1.0f)
            {
                beginPath(); rect(x0, zt, x1 - x0, zb - zt);
                fillPaint(mode == kCompress ? linearGradient(x0, zt, x0, zb, z0, zc) : linearGradient(x0, zt, x0, zb, zc, z0));
                fill();
            }
            // band level (detector) bar at the right edge of the band
            const float lv = std::max(-80.0f, lvShow[b]);
            const Rect bar(x1 - 13.0f, lvTop(), 6.0f, lvBot() - lvTop());
            if (x1 - x0 > 30)
            {
                fillRect(bar, withAlpha2(theme.ink, 0.06f));
                const float yl = std::max(bar.y, std::min(bar.b(), yOfLevel(lv)));
                const bool active = mode == kCompress ? lv > thr : lv < thr;
                fillRect(Rect(bar.x, yl, bar.w, bar.b() - yl), active ? theme.second : withAlpha2(mixDeep(), 0.8f));
                strokeRect(bar, withAlpha2(theme.ink, 0.5f), 1.0f);
            }
            // threshold line + value tag
            const float lx0 = x0 + (b == 0 ? 28.0f : 4.0f), lx1 = x1 - (x1 - x0 > 30 ? 17.0f : 4.0f);
            line(lx0, yt, lx1, yt, isSel ? theme.ink : withAlpha2(theme.ink, 0.7f), isSel ? 2.2f : 1.5f);
            if (lx1 - lx0 > 50)
            {
                char t[16]; std::snprintf(t, sizeof t, "%.1f", thr);
                const float tw = textWidth(t, kMono, 10.0f) + 10.0f;
                const Rect tag(lx0 + 2.0f, yt - 17.0f, tw, 15.0f);
                fillRect(tag, isSel ? theme.ink : theme.bg);
                strokeRect(tag, theme.ink, 1.0f);
                textAt(tag.cx(), tag.cy() + 0.5f, t, kMono, 10.0f, isSel ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
                if (lx1 - lx0 > 130)
                {
                    static const char* mn[] = { "COMP", "EXP", "UP" };
                    char r[24];
                    const float ra = value(bandParam(b, kRatio));
                    if (ra >= 19.99f) std::snprintf(r, sizeof r, "%s INF", mn[mode]); else std::snprintf(r, sizeof r, "%s %.1f:1", mn[mode], ra);
                    textAt(tag.r() + 6.0f, tag.cy() + 0.5f, r, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
                }
            }
        }

        // GAIN lane: the combined response, sum of band magnitudes x band gains
        {
            const Rect lane(disp.x, laneTop(), disp.w, laneBot() - laneTop());
            fillRect(lane, withAlpha2(theme.panel, theme.dark ? 0.55f : 0.65f));
            line(disp.x, laneBot(), disp.r(), laneBot(), theme.dim, 1.0f);
            const float y0 = laneZero();
            for (int k = -1; k <= 1; k += 2)
                line(disp.x, y0 + k * laneRange * 0.5f * pxPerDbLane(), disp.r(), y0 + k * laneRange * 0.5f * pxPerDbLane(), theme.grid, 1.0f);
            line(disp.x, y0, disp.r(), y0, withAlpha2(theme.ink, 0.30f), 1.0f);
            const float mix = value(kMix) * 0.01f;
            double gainLin[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const bool byp = value(bandParam(b, kBandBypass)) >= 0.5f;
                const double w = byp ? 1.0 : pump::dbToGain(grShow[b] + mkShow[b]);
                gainLin[b] = 1.0 + mix * (w - 1.0);
            }
            const int NP = 240;
            float ys[NP], xs[NP];
            for (int p = 0; p < NP; ++p)
            {
                xs[p] = disp.x + disp.w * p / (NP - 1);
                const double f = fOf(xs[p]);
                double g = 0.0;
                for (int b = 0; b < nb; ++b) g += pump::mb::bandMag(nb, e, b, f, sr) * gainLin[b];
                const float db = (float)pump::gainToDb(std::max(g, 1e-6));
                ys[p] = std::max(lane.y + 2.0f, std::min(lane.b() - 2.0f, y0 - db * pxPerDbLane()));
            }
            // fill toward 0 dB: cuts in the second colour, lifts in neon
            beginPath(); moveTo(xs[0], y0);
            for (int p = 0; p < NP; ++p) lineTo(xs[p], std::max(ys[p], y0));
            lineTo(xs[NP - 1], y0); closePath(); fillColor(withAlpha2(theme.second, theme.dark ? 0.40f : 0.32f)); fill();
            beginPath(); moveTo(xs[0], y0);
            for (int p = 0; p < NP; ++p) lineTo(xs[p], std::min(ys[p], y0));
            lineTo(xs[NP - 1], y0); closePath(); fillColor(withAlpha2(theme.neon, theme.dark ? 0.55f : 0.75f)); fill();
            beginPath();
            for (int p = 0; p < NP; ++p) { if (p == 0) moveTo(xs[p], ys[p]); else lineTo(xs[p], ys[p]); }
            strokeColor(theme.ink); strokeWidth(1.6f); lineJoin(ROUND); stroke();
            char t[16];
            std::snprintf(t, sizeof t, "+%d", (int)(laneRange * 0.5f));
            textAt(disp.x + 6, y0 - laneRange * 0.5f * pxPerDbLane(), t, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(t, sizeof t, "-%d", (int)(laneRange * 0.5f));
            textAt(disp.x + 6, y0 + laneRange * 0.5f * pxPerDbLane(), t, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        drawRegionsOver();
        restore();

        // output meter
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, disp.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        // panels
        strokeRect(bandPanel, theme.dim, 1.0f);
        strokeRect(globalPanel, theme.dim, 1.0f);
        drawBandExtras();
        caption(globalPanel.x + 12, globalPanel.y + 16, "CROSSOVER");
        caption(bandsSeg->r.x, globalPanel.y + 16, "BANDS");
        caption(globalPanel.x + 12, globalPanel.y + 66, "DETECTION");
    }

    void drawBandExtras()
    {
        const int b = sel;
        // gain readout bar (bipolar, ±24 dB)
        const float g = grShow[b] + mkShow[b];
        fillRect(grBar, theme.panel2);
        const float cx = grBar.cx(), w = std::min(1.0f, std::fabs(g) / 24.0f) * grBar.w * 0.5f;
        if (w > 0.5f) fillRect(Rect(g < 0 ? cx - w : cx, grBar.y, w, grBar.h), g < 0 ? theme.second : theme.neon);
        line(cx, grBar.y - 2, cx, grBar.b() + 2, theme.ink, 1.2f);
        strokeRect(grBar, theme.ink, 1.0f);
        char t[24]; std::snprintf(t, sizeof t, "%+.1f dB", std::fabs(g) < 0.05f ? 0.0f : g);
        textAt(grBar.r(), grBar.y - 10, t, kMono, 10.5f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        caption(grBar.x, grBar.y - 10, "GAIN");
        textAt(grBar.x + 40, grBar.y - 10, bandRange(b), kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);

        // transfer curve: input level -> output level (-60..0 dBFS), with the live detector level as a dot
        const Rect r = inset;
        caption(r.x, r.y - 14, "CURVE");
        fillRect(r, theme.screen);
        for (int d = -12; d > -60; d -= 12)
        {
            const float u = (d + 60) / 60.0f;
            line(r.x + u * r.w, r.y, r.x + u * r.w, r.b(), theme.grid, 1.0f);
            line(r.x, r.b() - u * r.h, r.r(), r.b() - u * r.h, theme.grid, 1.0f);
        }
        line(r.x, r.b(), r.r(), r.y, withAlpha2(theme.ink, 0.25f), 1.0f);
        const int mode = (int)value(bandParam(b, kMode));
        const float thr = value(bandParam(b, kThresh)), ra = value(bandParam(b, kRatio)), kn = value(bandParam(b, kKnee)), rg = value(bandParam(b, kRange));
        const float mk = mkShow[b];
        auto px = [&](float in) { return r.x + (in + 60.0f) / 60.0f * r.w; };
        auto py = [&](float out) { return std::max(r.y, std::min(r.b(), r.b() - (out + 60.0f) / 60.0f * r.h)); };
        const float tx = px(thr);
        line(tx, r.y, tx, r.b(), withAlpha2(theme.second, 0.6f), 1.0f);
        save();
        scissor(r.x, r.y, r.w, r.h);
        beginPath();
        for (int i = 0; i <= 120; ++i)
        {
            const float in = -60.0f + 60.0f * i / 120.0f;
            const float out = in + (float)staticGain(mode, in, thr, ra, kn, rg) + mk;
            if (i == 0) moveTo(px(in), py(out)); else lineTo(px(in), py(out));
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        const float lv = lvShow[b];
        if (lv > -60.0f)
        {
            const float out = lv + (float)staticGain(mode, lv, thr, ra, kn, rg) + mk;
            beginPath(); circle(px(lv), py(out), 4.5f); fillColor(theme.neon); fill(); strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        }
        restore();
        strokeRect(r, theme.ink, 1.2f);
        textAt(r.r() - 4, r.b() - 8, "IN", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        textAt(r.x + 4, r.y + 8, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%d BAND%s   %s   LATENCY %d (%.1f ms)", numBands(), numBands() > 1 ? "S" : "",
                      (int)value(kXoverMode) ? "LINEAR PHASE" : "ANALOG", lat, 1000.0 * lat / rate());
        return buf;
    }

    // ---- UI state ----
    void saveUi()
    {
        char buf[64]; std::snprintf(buf, sizeof buf, "lane=%d;sel=%d;", (int)laneRange, sel);
        saveUiState(buf);
    }
    void loadUiState(const std::string& s) override
    {
        laneRange = (float)std::max(6, std::atoi(uiStateGet(s, "lane", "6").c_str()));
        laneBtn->label = "GAIN ±" + std::to_string((int)laneRange);
        sel = pump::clampv(std::atoi(uiStateGet(s, "sel", "0").c_str()), 0, kBands - 1);
        rebind();
    }

private:
    PluginBand* plug = nullptr;
    SpectrumView pre, post;
    Rect meterL, meterR, bandPanel, globalPanel, grBar, inset;
    OutMeter outMeter;
    float grShow[kBands] = {}, lvShow[kBands] = { -120, -120, -120, -120 }, mkShow[kBands] = {};
    float laneRange = 6.0f;
    int grabBand = -1;
    float grabStart = 0.0f, grabEff = 0.0f, grabLastY = 0.0f;

    MenuButton* bandSel;
    Segmented *modeSeg, *xoverSeg, *bandsSeg, *detSeg;
    Dial *thrDial, *ratioDial, *kneeDial, *rangeDial, *atkDial, *relDial, *mkDial, *laDial, *linkDial, *mixDial, *outDial;
    Button *soloBtn, *muteBtn, *bypBtn, *autoBtn, *laneBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIBand)
};

UI* createUI() { return new UIBand(); }

END_NAMESPACE_DISTRHO
