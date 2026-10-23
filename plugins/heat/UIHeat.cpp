// pump HEAT: interface. Display: input (faint) and output spectra behind the band regions; each band glows with how
// hard it drives its curve (its RMS level plus drive), shows its drive as a draggable bar (0..24 dB) with the curve
// name and band mix; crossover handles; per-band S / M / B keys. Below: the selected band (curve, drive, mix,
// level) with its transfer curve, and the global settings (oversampling, auto gain, crossover, mix, output).
#include "ui/MultibandUI.hpp"
#include "PluginHeat.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::heat;

class UIHeat : public MultibandUI {
public:
    UIHeat() : MultibandUI(params().data(), kNumParams, &presets(), pump::accents::heat(), "HEAT", 1000, 620, kBypass)
    {
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
        bandSel->tip = "Selected band (◀ ▶, keys 1-4 or click a band in the display)";
        modeSeg = addSegmented(-1, { "WARM", "TAPE", "TUBE", "TRIODE", "RETRO", "CLIP" });
        modeSeg->tip = "Curve: Warm (smooth, odd) · Tape (highs first, HF loss) · Tube (even) · Triode (asymmetric) · Retro (transformer) · Clip (rounded clipper)";
        driveDial = addDial(-1, "DRIVE", Dial::Large);
        driveDial->tip = "Drive into the curve, 0..24 dB (or drag the band's bar in the display / mouse wheel on the band)";
        mixDial = addDial(-1, "BAND MIX");
        mixDial->tip = "Band mix: saturated vs clean for this band (Ctrl/Cmd + drag up/down in the band)";
        levelDial = addDial(-1, "LEVEL", Dial::Medium, true);
        levelDial->tip = "Band output level";
        driveDial->tabGroup = mixDial->tabGroup = levelDial->tabGroup = 1;
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
        bypBtn->tip = "Bypass the selected band's saturation (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)";

        osSeg = addSegmented(kOversampling, { "1x", "2x", "4x", "8x" });
        osSeg->tip = "Oversampling: each band is saturated at this multiple of the sample rate (less aliasing, a little latency)";
        autoBtn = addToggle(kAutoGain, "AUTO GAIN");
        autoBtn->small = true;
        autoBtn->tip = "Auto gain: keeps the level steady as you change drive (divides by the drive, then a slow level match)";
        xoverSeg = addSegmented(kXoverMode, { "ANALOG", "LINEAR" });
        xoverSeg->tip = "Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)";
        bandsSeg = addSegmented(kNumBandsP, { "1", "2", "3", "4" });
        bandsSeg->tip = "Number of bands";
        gmixDial = addDial(kMix, "MIX");
        gmixDial->tip = "Global mix: blends the whole processed signal with the clean one (latency-aligned)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        buildStrip();
        if (auto* p = static_cast<PluginHeat*>(pluginPtr())) { plug = p; bindListen(&p->listenHold); }
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
    int pMain(int b) const override { return bandParam(b, kDrive); }
    int pSecond(int b) const override { return bandParam(b, kBandMix); }
    int bandFirstParam(int b) const override { return bandParam(b, 0); }
    int bandParamCount() const override { return kBandStride; }
    std::string mainName() const override { return "drive"; }
    std::string secondName() const override { return "band mix"; }
    std::string stripValue(int b) override
    {
        if (value(bandParam(b, kBandBypass)) >= 0.5f) return "BYP";
        char t[16]; std::snprintf(t, sizeof t, "%.1f dB", value(bandParam(b, kDrive)));
        return t;
    }
    void onSelect(int) override { rebind(); saveUi(); }
    float mainFromDrag(int b, float start, float up) override { return spec(pMain(b)).clamp(start + up / pxPerDb()); }
    float secondFromDrag(int b, float start, float up) override { return spec(pSecond(b)).clamp(start + up * 0.5f); }
    float mainWheel(int, float v, float dir, bool fine) override { return v + dir * (fine ? 0.1f : 0.5f); }
    std::vector<std::pair<std::string, std::string>> pluginHelp() override
    {
        return {
            { "Band glow", "how hard the band hits its curve (its level plus drive)" },
        };
    }

    // ---- layout ----
    void layout() override
    {
        disp = Rect(14, 58, W - 28 - 34, 322);
        meterL = Rect(disp.r() + 12, disp.y, 9, disp.h - 18);
        meterR = Rect(meterL.r() + 2, disp.y, 9, disp.h - 18);
        const float y0 = 396;
        bandPanel = Rect(14, y0, 626, 192);
        bandSel->r = Rect(26, y0 + 12, 170, 26);
        soloBtn->r = Rect(206, y0 + 12, 58, 26);
        muteBtn->r = Rect(272, y0 + 12, 58, 26);
        bypBtn->r = Rect(338, y0 + 12, 66, 26);
        modeSeg->r = Rect(26, y0 + 50, 440, 26);
        driveDial->r = Rect(22, y0 + 86, 96, 100);
        mixDial->r = Rect(126, y0 + 92, 72, 84);
        levelDial->r = Rect(204, y0 + 92, 72, 84);
        readout = Rect(292, y0 + 96, 174, 80);
        inset = Rect(484, y0 + 34, 144, 144);

        globalPanel = Rect(654, y0, W - 668, 192);
        const float gx = 666;
        osSeg->r = Rect(gx, y0 + 30, 180, 24);
        autoBtn->r = Rect(gx + 192, y0 + 30, 116, 24);
        xoverSeg->r = Rect(gx, y0 + 80, 180, 24);
        bandsSeg->r = Rect(gx + 192, y0 + 80, 116, 24);
        gmixDial->r = Rect(gx - 4, y0 + 110, 72, 80);
        outDial->r = Rect(gx + 74, y0 + 110, 72, 80);
        latInfo = Rect(gx + 160, y0 + 120, 148, 60);
    }
    float bTop() const { return disp.y + kStripH + 22.0f; }
    float bBot() const { return disp.b() - 46.0f; }
    float pxPerDb() const { return (bBot() - bTop()) / 24.0f; }
    float yOfDrive(float db) const { return bBot() - db * pxPerDb(); }

    void rebind()
    {
        sel = pump::clampv(sel, 0, numBands() - 1);
        modeSeg->param = bandParam(sel, kMode);
        driveDial->param = bandParam(sel, kDrive);
        mixDial->param = bandParam(sel, kBandMix);
        levelDial->param = bandParam(sel, kLevel);
        soloBtn->param = bandParam(sel, kSolo);
        muteBtn->param = bandParam(sel, kMute);
        bypBtn->param = bandParam(sel, kBandBypass);
        const bool byp = value(bandParam(sel, kBandBypass)) >= 0.5f;
        driveDial->enabled = mixDial->enabled = levelDial->enabled = modeSeg->enabled = !byp;
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
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        const float k = std::min(1.0f, dt * 10.0f);
        for (int b = 0; b < kBands; ++b)
        {
            inShow[b] += (plug->engine.inLevelDb(b) - inShow[b]) * k;
            agShow[b] += (plug->engine.autoGainDb(b) - agShow[b]) * k;
        }
        pre.update(&plug->preRing, nullptr, dt);
        post.update(&plug->postRing, nullptr, dt);
    }
    // 0..1: how hard the band drives its curve (RMS into the curve from -30 to -6 dBFS)
    float heat(int b) const
    {
        if (value(bandParam(b, kBandBypass)) >= 0.5f || value(bandParam(b, kBandMix)) < 0.5f) return 0.0f;
        const float into = inShow[b] + value(bandParam(b, kDrive));
        return pump::clampv((into + 30.0f) / 24.0f, 0.0f, 1.0f);
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
        // glow: each band warms up from the bottom with how hard it drives its curve
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            const float h = heat(b);
            if (h <= 0.01f) continue;
            const float top = disp.y + kStripH + (1.0f - h) * (disp.h - kStripH) * 0.55f;
            beginPath(); rect(x0, top, x1 - x0, disp.b() - top);
            fillPaint(linearGradient(x0, top, x0, disp.b(), withAlpha2(theme.neon, 0.0f), withAlpha2(theme.neon, (theme.dark ? 0.26f : 0.34f) * (0.30f + 0.70f * h))));
            fill();
        }
        drawFreqGrid(Rect(disp.x, disp.y + kStripH, disp.w, disp.h - kStripH), kLo, kHi);
        const Rect sp(disp.x, disp.y + kStripH, disp.w, disp.h - kStripH);
        drawSpectrum(pre, sp, -6.0f, -96.0f, withAlpha2(theme.ink, theme.dark ? 0.07f : 0.05f), withAlpha2(theme.ink, 0.25f), kLo, kHi, 1.0f);
        drawSpectrum(post, sp, -6.0f, -96.0f, withAlpha2(theme.neon, theme.dark ? 0.20f : 0.28f), withAlpha2(mixDeep(), 0.75f), kLo, kHi, 1.1f);
        textAt(disp.r() - 8, disp.y + kStripH + 10, "OUT", kLabel, 9.5f, mixDeep(), ALIGN_RIGHT | ALIGN_MIDDLE, 1.2f);
        textAt(disp.r() - 38, disp.y + kStripH + 10, "IN", kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.2f);
        // drive scale
        for (int d = 0; d <= 24; d += 6) line(disp.x, yOfDrive((float)d), disp.r(), yOfDrive((float)d), theme.grid, 1.0f);
        for (int d = 6; d <= 24; d += 6)
        {
            char t[8]; std::snprintf(t, sizeof t, "%d", d);
            textAt(disp.x + 5, yOfDrive((float)d) - 6, t, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        textAt(disp.x + 5, yOfDrive(24.0f) - 18, "DRIVE dB", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        // per band: the drive bar, curve name and band mix
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            if (x1 - x0 < 8) continue;
            const float drv = value(bandParam(b, kDrive)), bm = value(bandParam(b, kBandMix));
            const int mode = (int)value(bandParam(b, kMode));
            const bool isSel = b == sel, byp = value(bandParam(b, kBandBypass)) >= 0.5f;
            const float yd = yOfDrive(drv);
            const float bx0 = x0 + (b == 0 ? 36.0f : 8.0f), bx1 = x1 - 8.0f;
            if (bx1 - bx0 < 4) continue;
            // the bar: filled to the drive, with the band mix as its opacity
            const float op = byp ? 0.08f : (0.12f + 0.38f * bm * 0.01f) * (theme.dark ? 0.8f : 1.0f);
            fillRect(Rect(bx0, yd, bx1 - bx0, bBot() - yd), withAlpha2(theme.neon, op));
            line(bx0, bBot(), bx1, bBot(), withAlpha2(theme.ink, 0.35f), 1.0f);
            line(bx0, yd, bx1, yd, byp ? theme.dim : (isSel ? theme.ink : withAlpha2(theme.ink, 0.75f)), isSel ? 2.4f : 1.6f);
            if (bx1 - bx0 > 50)
            {
                char t[24]; std::snprintf(t, sizeof t, "%.1f", drv);
                const float tw = textWidth(t, kMono, 10.0f) + 10.0f;
                const Rect tag(bx0 + 2.0f, yd < bTop() + 18 ? yd + 4.0f : yd - 17.0f, tw, 15.0f);
                fillRect(tag, isSel ? theme.ink : theme.bg);
                strokeRect(tag, theme.ink, 1.0f);
                textAt(tag.cx(), tag.cy() + 0.5f, t, kMono, 10.0f, isSel ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
                std::string m = byp ? "BYPASS" : std::string(kModeNames[mode]);
                for (char& c : m) c = (char)std::toupper((unsigned char)c);
                if (!byp && bx1 - bx0 > 140) { char mm[16]; std::snprintf(mm, sizeof mm, "  MIX %.0f%%", bm); m += mm; }
                if (bx1 - bx0 > 90) textAt(tag.r() + 6.0f, tag.cy() + 0.5f, m, kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
            }
        }
        drawRegionsOver();
        restore();

        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, disp.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(bandPanel, theme.dim, 1.0f);
        strokeRect(globalPanel, theme.dim, 1.0f);
        caption(globalPanel.x + 12, globalPanel.y + 16, "OVERSAMPLING");
        caption(globalPanel.x + 12, globalPanel.y + 66, "CROSSOVER");
        caption(bandsSeg->r.x, globalPanel.y + 66, "BANDS");
        drawReadout();
        drawCurve();
        // latency / oversampling summary
        const int lat = plug ? plug->latencyNow() : 0;
        char t[48];
        caption(latInfo.x, latInfo.y + 6, "LATENCY");
        std::snprintf(t, sizeof t, "%d smp", lat);
        textAt(latInfo.r(), latInfo.y + 6, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "%.2f ms", 1000.0 * lat / rate());
        textAt(latInfo.r(), latInfo.y + 24, t, kMono, 10.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "%.0f kHz inside", rate() * (1 << (int)value(kOversampling)) / 1000.0);
        textAt(latInfo.r(), latInfo.y + 42, t, kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
    }

    void drawReadout()
    {
        const Rect r = readout;
        const int b = sel;
        char t[32];
        caption(r.x, r.y + 4, "INTO CURVE");
        const float into = inShow[b] + value(bandParam(b, kDrive));
        if (inShow[b] < -100.0f) std::snprintf(t, sizeof t, "--"); else std::snprintf(t, sizeof t, "%.1f dB", into);
        textAt(r.r(), r.y + 4, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        // heat bar
        const Rect hb(r.x, r.y + 16, r.w, 9);
        fillRect(hb, theme.panel2);
        const float h = heat(b);
        if (h > 0.0f) fillRect(Rect(hb.x, hb.y, hb.w * h, hb.h), pump::mixColor(theme.neon, theme.red, h * h * 0.6f));
        strokeRect(hb, theme.ink, 1.0f);
        caption(r.x, r.y + 42, "AUTO GAIN");
        if (value(kAutoGain) < 0.5f) std::snprintf(t, sizeof t, "OFF");
        else std::snprintf(t, sizeof t, "%+.1f dB", agShow[b] - value(bandParam(b, kDrive)));
        textAt(r.r(), r.y + 42, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        textAt(r.x, r.y + 62, bandRange(b), kLabel, 10.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
    }

    // transfer curve of the selected band at its drive (with the static part of auto gain when it's on)
    void drawCurve()
    {
        const Rect r = inset;
        const int b = sel;
        caption(r.x, r.y - 14, "CURVE");
        const int mode = (int)value(bandParam(b, kMode));
        textAt(r.r(), r.y - 14, kModeNames[mode], kLabel, 10.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        fillRect(r, theme.screen);
        const float cx = r.cx(), cy = r.cy(), sx = r.w * 0.5f, sy = r.h * 0.5f / 1.25f;
        for (float v : { -1.0f, -0.5f, 0.5f, 1.0f })
        {
            line(cx + v * sx, r.y, cx + v * sx, r.b(), theme.grid, 1.0f);
            line(r.x, cy - v * sy, r.r(), cy - v * sy, theme.grid, 1.0f);
        }
        line(r.x, cy, r.r(), cy, withAlpha2(theme.ink, 0.3f), 1.0f);
        line(cx, r.y, cx, r.b(), withAlpha2(theme.ink, 0.3f), 1.0f);
        line(r.x, r.b() - (r.h - 2 * sy) * 0.5f, r.r(), r.y + (r.h - 2 * sy) * 0.5f, withAlpha2(theme.ink, 0.18f), 1.0f);
        const double g = pump::dbToGain(value(bandParam(b, kDrive)));
        const bool ag = value(kAutoGain) >= 0.5f;
        // the band's RMS level (into the curve, before drive) as markers
        if (inShow[b] > -100.0f)
        {
            const float a = std::min(1.0f, pump::dbToGainF(inShow[b]) * 1.41421f);
            fillRect(Rect(cx - a * sx, r.y + 1, 2 * a * sx, r.h - 2), withAlpha2(theme.neon, theme.dark ? 0.14f : 0.22f));
        }
        save();
        scissor(r.x, r.y, r.w, r.h);
        beginPath();
        for (int i = 0; i <= 160; ++i)
        {
            const double x = -1.0 + 2.0 * i / 160.0;
            double y = curve(mode, g * x);
            if (ag) y /= g;
            const float px = cx + (float)x * sx, py = cy - (float)y * sy;
            if (i == 0) moveTo(px, py); else lineTo(px, py);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        restore();
        strokeRect(r, theme.ink, 1.2f);
        textAt(r.r() - 4, cy + 8, "IN", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        textAt(cx + 5, r.y + 8, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%d BAND%s   %s   %s OVERSAMPLING   LATENCY %d", numBands(), numBands() > 1 ? "S" : "",
                      (int)value(kXoverMode) ? "LINEAR PHASE" : "ANALOG", kOsNames[pump::clampv((int)value(kOversampling), 0, 3)], lat);
        return buf;
    }

    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "sel=%d;", sel); saveUiState(buf); }
    void loadUiState(const std::string& s) override { sel = pump::clampv(std::atoi(uiStateGet(s, "sel", "0").c_str()), 0, kBands - 1); rebind(); }

private:
    PluginHeat* plug = nullptr;
    SpectrumView pre, post;
    Rect meterL, meterR, bandPanel, globalPanel, readout, inset, latInfo;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float inShow[kBands] = { -120, -120, -120, -120 }, agShow[kBands] = {};

    MenuButton* bandSel;
    Segmented *modeSeg, *osSeg, *xoverSeg, *bandsSeg;
    Dial *driveDial, *mixDial, *levelDial, *gmixDial, *outDial;
    Button *soloBtn, *muteBtn, *bypBtn, *autoBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIHeat)
};

UI* createUI() { return new UIHeat(); }

END_NAMESPACE_DISTRHO
