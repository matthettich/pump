// pump WIDE: interface. Left: mid (fill) and side (line) spectra behind the band regions, each band's width as a
// draggable bar (100% = unchanged) with its stereoize amount; crossover handles; per-band S / M / B keys. Right: a
// polar-sample vectorscope (half disc, standard style: mono straight up, L and R on the diagonals, out-of-phase
// content toward the flat edge) with phosphor persistence, and correlation meters, overall and per band.
#include "ui/MultibandUI.hpp"
#include "PluginWide.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::wide;

class UIWide : public MultibandUI {
public:
    UIWide() : MultibandUI(params().data(), kNumParams, &presets(), pump::accents::wide(), "WIDE", 1000, 620, kBypass)
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
        widthDial = addDial(-1, "WIDTH", Dial::Large);
        widthDial->tip = "Width: 0% = mono, 100% = unchanged, 200% = twice the side (or drag the band's bar in the display)";
        stDial = addDial(-1, "STEREOIZE", Dial::Large);
        stDial->tip = "Stereoize: adds a decorrelated copy of the band's mid to its side; cancels in mono (Ctrl/Cmd + drag in the band)";
        widthDial->tabGroup = stDial->tabGroup = 1;
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
        bypBtn->tip = "Bypass the selected band's width and stereoize (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)";

        xoverSeg = addSegmented(kXoverMode, { "ANALOG", "LINEAR" });
        xoverSeg->tip = "Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)";
        bandsSeg = addSegmented(kNumBandsP, { "1", "2", "3", "4" });
        bandsSeg->tip = "Number of bands";
        monoBtn = addToggle(kMonoBass, "MONO BASS");
        monoBtn->small = true;
        monoBtn->tip = "Mono bass: removes the side below the frequency (phase-coherent Linkwitz-Riley split)";
        slopeSeg = addSegmented(kMonoSlope, { "24 dB", "48 dB" });
        slopeSeg->vertical = true;
        slopeSeg->tip = "Mono bass slope: 24 dB/oct (LR4) or 48 dB/oct (LR8)";
        monoDial = addDial(kMonoFreq, "MONO FREQ");
        monoDial->tip = "Mono bass frequency: the side is removed below it (drag the dashed line in the display)";
        balDial = addDial(kBalance, "BALANCE", Dial::Medium, true);
        balDial->tip = "Balance: turns the opposite channel down";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";

        buildStrip();
        if (auto* p = static_cast<PluginWide*>(pluginPtr())) plug = p;
        mid.setup(rate()); side.setup(rate());
        mid.setRange(kLo, kHi); side.setRange(kLo, kHi);
        rebind();
    }

protected:
    // ---- MultibandUI hooks ----
    int pBands() const override { return kNumBandsP; }
    int pXover(int k) const override { return xoverParam(k); }
    int pListen() const override { return kListen; }
    int pSolo(int b) const override { return bandParam(b, kSolo); }
    int pMute(int b) const override { return bandParam(b, kMute); }
    int pBandBypass(int b) const override { return bandParam(b, kBandBypass); }
    int pMain(int b) const override { return bandParam(b, kWidth); }
    int pSecond(int b) const override { return bandParam(b, kStereoize); }
    int bandFirstParam(int b) const override { return bandParam(b, 0); }
    int bandParamCount() const override { return kBandStride; }
    std::string mainName() const override { return "width"; }
    std::string secondName() const override { return "stereoize"; }
    std::string stripValue(int b) override
    {
        if (value(bandParam(b, kBandBypass)) >= 0.5f) return "BYP";
        char t[16]; std::snprintf(t, sizeof t, "%.0f%%", value(bandParam(b, kWidth)));
        return t;
    }
    void onSelect(int) override { rebind(); saveUi(); }
    float mainFromDrag(int b, float start, float up) override { return spec(pMain(b)).clamp(start + up / pxPerPct()); }
    float secondFromDrag(int b, float start, float up) override { return spec(pSecond(b)).clamp(start + up * 0.5f); }
    float mainWheel(int, float v, float dir, bool fine) override { return v + dir * (fine ? 1.0f : 5.0f); }
    // the mono-bass line in the display can be dragged too
    bool regionGrab(int, float x, float y, uint) override
    {
        if (value(kMonoBass) < 0.5f || std::fabs(x - xOf(value(kMonoFreq))) > 5.0f || y < wTop()) return false;
        grabMono = true; grabStart = value(kMonoFreq); grabEff = 0.0f; grabLastX = x;
        beginEdit(kMonoFreq);
        return true;
    }
    void regionGrabDrag(float x, float, uint mod) override
    {
        if (!grabMono) return;
        grabEff += (x - grabLastX) * ((mod & kModifierShift) ? 0.15f : 1.0f); grabLastX = x;
        setParam(kMonoFreq, spec(kMonoFreq).clamp((float)(grabStart * std::pow(kHi / kLo, (double)(grabEff / disp.w)))));
        setStatus(paramStatus(kMonoFreq));
    }
    void regionGrabEnd() override { if (grabMono) endEdit(kMonoFreq); grabMono = false; }
    std::vector<std::pair<std::string, std::string>> pluginHelp() override
    {
        return {
            { "Drag the dashed MONO line", "mono bass frequency (when MONO BASS is on)" },
            { "Vectorscope", "mono straight up · L / R on the diagonals · out of phase: toward the flat edge" },
            { "Correlation", "+1 mono · 0 wide · below 0 (red): phase trouble, it will thin out in mono" },
        };
    }

    // ---- layout ----
    void layout() override
    {
        disp = Rect(14, 58, 606, 330);
        scope = Rect(638, 58, 348, 204);
        corr = Rect(638, 276, 348, 112);
        const float y0 = 404;
        bandPanel = Rect(14, y0, 500, 184);
        bandSel->r = Rect(26, y0 + 12, 170, 26);
        soloBtn->r = Rect(206, y0 + 12, 58, 26);
        muteBtn->r = Rect(272, y0 + 12, 58, 26);
        bypBtn->r = Rect(338, y0 + 12, 66, 26);
        widthDial->r = Rect(26, y0 + 58, 96, 100);
        stDial->r = Rect(130, y0 + 58, 96, 100);
        bandInfo = Rect(250, y0 + 58, 252, 110);

        globalPanel = Rect(528, y0, W - 542, 184);
        const float gx = 540;
        xoverSeg->r = Rect(gx, y0 + 30, 160, 24);
        bandsSeg->r = Rect(gx + 172, y0 + 30, 112, 24);
        monoBtn->r = Rect(gx + 300, y0 + 30, 134, 24);
        balDial->r = Rect(gx - 4, y0 + 82, 72, 84);
        outDial->r = Rect(gx + 76, y0 + 82, 72, 84);
        slopeSeg->r = Rect(gx + 222, y0 + 90, 62, 56);
        monoDial->r = Rect(gx + 318, y0 + 82, 100, 84);
    }
    float wTop() const { return disp.y + kStripH + 16.0f; }
    float wBot() const { return disp.b() - 52.0f; }
    float pxPerPct() const { return (wBot() - wTop()) / 200.0f; }
    float yOfWidth(float pct) const { return wBot() - pct * pxPerPct(); }

    void rebind()
    {
        sel = pump::clampv(sel, 0, numBands() - 1);
        widthDial->param = bandParam(sel, kWidth);
        stDial->param = bandParam(sel, kStereoize);
        soloBtn->param = bandParam(sel, kSolo);
        muteBtn->param = bandParam(sel, kMute);
        bypBtn->param = bandParam(sel, kBandBypass);
        const bool byp = value(bandParam(sel, kBandBypass)) >= 0.5f;
        widthDial->enabled = stDial->enabled = !byp;
        const bool mono = value(kMonoBass) >= 0.5f;
        monoDial->enabled = slopeSeg->enabled = mono;
    }
    void onParamChanged(int i, float) override
    {
        if (i == kNumBandsP && sel >= numBands()) sel = numBands() - 1;
        if (i == kNumBandsP || i == kMonoBass || bandOf(i) == sel) rebind();
    }
    void onPresetApplied() override { stopListening(); sel = pump::clampv(sel, 0, numBands() - 1); rebind(); }

    // ---- feeds ----
    void tick(float dt) override
    {
        if (!plug) return;
        mid.update(&plug->midRing, nullptr, dt);
        side.update(&plug->sideRing, nullptr, dt);
        const float k = std::min(1.0f, dt * 6.0f);
        for (int b = 0; b < kBands; ++b)
        {
            corrShow[b] += (plug->engine.correlation(b) - corrShow[b]) * k;
            lvShow[b] = plug->engine.bandLevel(b);
        }
        corrAllShow += (plug->engine.correlationAll() - corrAllShow) * k;
        lvAll = plug->engine.levelAll();
        if (corrAllShow < corrMin || corrMinT <= 0.0f) { corrMin = corrAllShow; corrMinT = 2.0f; } else corrMinT -= dt;
        scopeTick(dt);
    }

    // Phosphor vectorscope: new samples are splatted into an intensity buffer that decays every frame.
    void scopeTick(float dt)
    {
        if (fb.empty()) { fb.assign((size_t)(kImgW * kImgH), 0.0f); px.assign((size_t)(kImgW * kImgH * 4), 0); }
        const float decay = std::exp(-dt / 0.16f);
        for (float& v : fb) v *= decay;
        const uint32_t pos = plug->scopeL.position();
        uint32_t n = pos - lastPos;
        if (n > 6000) { lastPos = pos - 6000; n = 6000; }
        const float cx = kImgW * 0.5f, cy = kImgH - 22.0f * 2.0f, R = radiusImg();
        for (uint32_t k = 0; k < n; ++k)
        {
            const float l = plug->scopeL.at(lastPos + k), r = plug->scopeR.at(lastPos + k);
            float m = 0.5f * (l + r), s = 0.5f * (l - r);
            if (m < 0.0f) { m = -m; s = -s; }
            const float a = std::sqrt(m * m + s * s);
            if (a < 1e-5f) continue;
            const float rd = R * std::sqrt(std::min(1.0f, a));
            const int x = (int)(cx - rd * s / a), y = (int)(cy - rd * m / a);
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= kImgW || yy >= kImgH) continue;
                    float& v = fb[(size_t)(yy * kImgW + xx)];
                    v = std::min(1.0f, v + 0.22f);
                }
        }
        lastPos = pos;
        const Color c = theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.55f);
        const uint8_t cr = (uint8_t)(c.red * 255), cg = (uint8_t)(c.green * 255), cb = (uint8_t)(c.blue * 255);
        for (size_t i = 0; i < fb.size(); ++i)
        {
            px[i * 4 + 0] = cr; px[i * 4 + 1] = cg; px[i * 4 + 2] = cb;
            px[i * 4 + 3] = (uint8_t)(255.0f * std::sqrt(fb[i]));
        }
        imgDirty = true;
    }
    float radiusImg() const { return std::min(kImgW * 0.5f - 18.0f * 2.0f, kImgH - 22.0f * 2.0f - 26.0f * 2.0f); }

    // ---- drawing ----
    Color mixDeep() const { return pump::mixColor(theme.neon, theme.ink, 0.45f); }
    Color corrColor(float c) const
    {
        if (c < 0.0f) return theme.red;
        return c < 0.25f ? pump::mixColor(theme.red, theme.dark ? theme.neon : mixDeep(), c / 0.25f) : (theme.dark ? theme.neon : mixDeep());
    }

    void drawBody() override
    {
        screenBox(disp);
        save();
        scissor(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2);
        drawRegionsUnder();
        const int nb = numBands();
        drawFreqGrid(Rect(disp.x, disp.y + kStripH, disp.w, disp.h - kStripH), kLo, kHi);
        const Rect sp(disp.x, disp.y + kStripH, disp.w, disp.h - kStripH);
        drawSpectrum(mid, sp, -6.0f, -96.0f, withAlpha2(theme.neon, theme.dark ? 0.22f : 0.30f), withAlpha2(mixDeep(), 0.7f), kLo, kHi, 1.0f);
        drawSpectrum(side, sp, -6.0f, -96.0f, withAlpha2(theme.second, theme.dark ? 0.06f : 0.05f), withAlpha2(theme.second, theme.dark ? 0.65f : 0.60f), kLo, kHi, 1.0f);
        textAt(disp.r() - 8, disp.y + kStripH + 10, "MID", kLabel, 9.5f, mixDeep(), ALIGN_RIGHT | ALIGN_MIDDLE, 1.2f);
        textAt(disp.r() - 40, disp.y + kStripH + 10, "SIDE", kLabel, 9.5f, theme.second, ALIGN_RIGHT | ALIGN_MIDDLE, 1.2f);

        // width scale: 0 / 100 / 200%
        const float y100 = yOfWidth(100.0f);
        for (float p : { 0.0f, 50.0f, 150.0f, 200.0f }) line(disp.x, yOfWidth(p), disp.r(), yOfWidth(p), theme.grid, 1.0f);
        for (float x = disp.x; x < disp.r(); x += 8.0f) line(x, y100, std::min(disp.r(), x + 4.0f), y100, withAlpha2(theme.ink, 0.35f), 1.0f);
        textAt(disp.x + 6, yOfWidth(200.0f) - 7, "200%", kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(disp.x + 6, y100 - 7, "100%", kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(disp.x + 6, yOfWidth(0.0f) - 7, "MONO", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);

        // mono bass: everything left of the line is mono
        if (value(kMonoBass) >= 0.5f)
        {
            const float xm = xOf(value(kMonoFreq));
            fillRect(Rect(disp.x, disp.y + kStripH, xm - disp.x, disp.h - kStripH), withAlpha2(theme.ink, theme.dark ? 0.10f : 0.06f));
            for (float y = disp.y + kStripH; y < disp.b(); y += 7.0f) line(xm, y, xm, std::min(disp.b(), y + 4.0f), theme.ink, 1.5f);
            const std::string t = "MONO < " + spec(kMonoFreq).format(value(kMonoFreq));
            const float tw = textWidth(t, kLabel, 10.0f, 0.8f) + 12.0f;
            const Rect tag(std::max(disp.x + 4, std::min(disp.r() - tw - 4, xm + 5.0f)), disp.y + kStripH + 5.0f, tw, 16.0f);
            fillRect(tag, theme.ink);
            textAt(tag.cx(), tag.cy() + 0.5f, t, kLabel, 10.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        }
        // per band: width bar from the 100% line, handle, stereoize
        for (int b = 0; b < nb; ++b)
        {
            float x0, x1; regionX(b, x0, x1);
            if (x1 - x0 < 8) continue;
            const float w = value(bandParam(b, kWidth)), st = value(bandParam(b, kStereoize));
            const bool isSel = b == sel, byp = value(bandParam(b, kBandBypass)) >= 0.5f;
            const float yw = yOfWidth(byp ? 100.0f : w);
            const float bx0 = x0 + (b == 0 ? 40.0f : 6.0f), bx1 = x1 - 6.0f;
            if (bx1 - bx0 < 4) continue;
            if (std::fabs(yw - y100) > 0.5f)
                fillRect(Rect(bx0, std::min(yw, y100), bx1 - bx0, std::fabs(yw - y100)),
                         yw < y100 ? withAlpha2(theme.neon, theme.dark ? 0.30f : 0.42f) : withAlpha2(theme.second, theme.dark ? 0.26f : 0.20f));
            // stereoize: a hatched cap above the width bar, as tall as the amount
            if (st > 0.5f && !byp)
            {
                const float h = st * 0.01f * 28.0f;
                save();
                scissor(bx0, yw - h, bx1 - bx0, h);
                for (float x = bx0 - h; x < bx1; x += 6.0f) line(x, yw, x + h, yw - h, withAlpha2(theme.second, 0.7f), 1.2f);
                restore();
            }
            line(bx0, yw, bx1, yw, isSel ? theme.ink : withAlpha2(theme.ink, 0.75f), isSel ? 2.4f : 1.6f);
            if (bx1 - bx0 > 46)
            {
                char t[24]; std::snprintf(t, sizeof t, "%.0f%%", byp ? 100.0f : w);
                const float tw = textWidth(t, kMono, 10.0f) + 10.0f;
                // the tag sits above the line, or below it when the stereoize hatch is there (or no room above)
                const bool below = (st > 0.5f && !byp) || yw < wTop() + 20;
                const Rect tag(bx0 + 2.0f, below ? yw + 4.0f : yw - 17.0f, tw, 15.0f);
                fillRect(tag, isSel ? theme.ink : theme.bg);
                strokeRect(tag, theme.ink, 1.0f);
                textAt(tag.cx(), tag.cy() + 0.5f, t, kMono, 10.0f, isSel ? theme.on : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE);
                if (st > 0.5f && bx1 - bx0 > 110)
                {
                    std::snprintf(t, sizeof t, "ST %.0f%%", st);
                    textAt(tag.r() + 6.0f, tag.cy() + 0.5f, t, kLabel, 10.0f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
                }
            }
        }
        drawRegionsOver();
        restore();

        drawScope();
        drawCorrelation();
        strokeRect(bandPanel, theme.dim, 1.0f);
        strokeRect(globalPanel, theme.dim, 1.0f);
        caption(globalPanel.x + 12, globalPanel.y + 16, "CROSSOVER");
        caption(bandsSeg->r.x, globalPanel.y + 16, "BANDS");
        caption(monoBtn->r.x, globalPanel.y + 16, "LOW END");
        caption(slopeSeg->r.x, slopeSeg->r.y - 10, "SLOPE");
        drawBandInfo();
    }

    void drawScope()
    {
        screenBox(scope);
        const float cx = scope.cx(), cy = scope.b() - 22.0f, R = radiusImg() * 0.5f;
        // guides: half disc, half radius, the L / M / R directions and the out-of-phase edge
        beginPath(); arc(cx, cy, R, (float)pump::kPi, 0.0f, CW); strokeColor(withAlpha2(theme.ink, 0.45f)); strokeWidth(1.2f); stroke();
        beginPath(); arc(cx, cy, R * 0.5f, (float)pump::kPi, 0.0f, CW); strokeColor(theme.grid); strokeWidth(1.0f); stroke();
        line(cx - R - 6, cy, cx + R + 6, cy, withAlpha2(theme.ink, 0.45f), 1.2f);
        const float d = R * 0.70710678f;
        line(cx, cy, cx, cy - R, theme.grid, 1.0f);
        line(cx, cy, cx - d, cy - d, theme.grid, 1.0f);
        line(cx, cy, cx + d, cy - d, theme.grid, 1.0f);
        textAt(cx - d - 9, cy - d - 9, "L", kBold, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(cx + d + 9, cy - d - 9, "R", kBold, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(cx, cy - R - 10, "M", kBold, 11.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(cx - R - 4, cy + 11, "-S", kMono, 9.5f, theme.red, ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(cx + R + 4, cy + 11, "+S", kMono, 9.5f, theme.red, ALIGN_RIGHT | ALIGN_MIDDLE);
        caption(scope.x + 10, scope.y + 12, "VECTORSCOPE");
        // the phosphor image
        if (!fb.empty())
        {
            if (!img.isValid()) img = createImageFromRGBA((uint)kImgW, (uint)kImgH, px.data(), 0);
            else if (imgDirty) img.update(px.data());
            imgDirty = false;
            const float ox = cx - kImgW * 0.25f, oy = cy + 22.0f - kImgH * 0.5f;
            if (img.isValid())
            {
                const Paint p = imagePattern(ox, oy, kImgW * 0.5f, kImgH * 0.5f, 0.0f, img, 1.0f);
                beginPath(); rect(scope.x + 1, scope.y + 1, scope.w - 2, scope.h - 2); fillPaint(p); fill();
            }
        }
    }

    void corrMeter(const Rect& r, float c, bool big, bool silent)
    {
        fillRect(r, theme.panel2);
        const float zx = r.cx();
        if (!silent)
        {
            const float x = r.x + (c + 1.0f) * 0.5f * r.w;
            fillRect(Rect(std::min(zx, x), r.y, std::fabs(x - zx), r.h), corrColor(c));
            line(x, r.y - (big ? 3 : 1), x, r.b() + (big ? 3 : 1), theme.ink, big ? 2.0f : 1.4f);
        }
        line(zx, r.y, zx, r.b(), withAlpha2(theme.ink, 0.6f), 1.0f);
        strokeRect(r, theme.ink, 1.0f);
    }
    void drawCorrelation()
    {
        screenBox(corr);
        caption(corr.x + 10, corr.y + 12, "CORRELATION");
        const bool silent = lvAll < -90.0f;
        char t[24];
        if (silent) std::snprintf(t, sizeof t, "--");
        else std::snprintf(t, sizeof t, "%+.2f", corrAllShow);
        textAt(corr.r() - 10, corr.y + 12, t, kBold, 13.0f, silent ? theme.ink2 : (corrAllShow < 0 ? theme.red : theme.ink), ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect big(corr.x + 28, corr.y + 26, corr.w - 56, 14);
        corrMeter(big, corrAllShow, true, silent);
        if (!silent && corrMin < 0.0f)
        {
            const float x = big.x + (corrMin + 1.0f) * 0.5f * big.w;
            line(x, big.y - 2, x, big.b() + 2, theme.red, 1.5f);
        }
        textAt(big.x - 6, big.cy(), "-1", kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        textAt(big.r() + 6, big.cy(), "+1", kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        // per band
        const int nb = numBands();
        for (int b = 0; b < nb; ++b)
        {
            const float y = corr.y + 52.0f + 14.0f * (float)b;
            const bool bs = lvShow[b] < -90.0f;
            textAt(corr.x + 14, y + 3, std::to_string(b + 1), kBold, 9.5f, b == sel ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            corrMeter(Rect(corr.x + 28, y, corr.w - 90, 7), corrShow[b], false, bs);
            if (bs) std::snprintf(t, sizeof t, "--"); else std::snprintf(t, sizeof t, "%+.2f", corrShow[b]);
            textAt(corr.r() - 12, y + 3.5f, t, kMono, 9.5f, bs ? theme.ink2 : (corrShow[b] < 0 ? theme.red : theme.ink), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }

    void drawBandInfo()
    {
        const Rect r = bandInfo;
        const int b = sel;
        caption(r.x, r.y + 4, "BAND " + std::to_string(b + 1) + "  " + bandName(b, numBands()));
        textAt(r.r() - 4, r.y + 4, bandRange(b), kLabel, 10.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.6f);
        // the band's image as a little M/S diagram: side scaled by width, stereoize as the hatched extra
        const float w = value(bandParam(b, kWidth)) * 0.01f, st = value(bandParam(b, kStereoize)) * 0.01f;
        const Rect d(r.x, r.y + 18, 120, 84);
        fillRect(d, theme.screen);
        strokeRect(d, theme.ink, 1.2f);
        save();
        scissor(d.x + 1, d.y + 1, d.w - 2, d.h - 2);
        const float cx = d.cx(), cy = d.b() - 10.0f, R = 54.0f;
        beginPath(); arc(cx, cy, R, (float)pump::kPi, 0.0f, CW); strokeColor(theme.grid); strokeWidth(1.0f); stroke();
        const float ang = std::atan(std::min(4.0f, w)) ;   // 100% -> 45 degrees
        const float ang2 = std::atan(std::min(4.0f, w + st * 0.8f));
        beginPath(); moveTo(cx, cy);
        lineTo(cx - R * std::sin(ang2), cy - R * std::cos(ang2));
        arc(cx, cy, R, (float)(-pump::kPi * 0.5 - ang2), (float)(-pump::kPi * 0.5 + ang2), CW);
        closePath(); fillColor(withAlpha2(theme.second, 0.25f)); fill();
        beginPath(); moveTo(cx, cy);
        lineTo(cx - R * std::sin(ang), cy - R * std::cos(ang));
        arc(cx, cy, R, (float)(-pump::kPi * 0.5 - ang), (float)(-pump::kPi * 0.5 + ang), CW);
        closePath(); fillColor(withAlpha2(theme.neon, theme.dark ? 0.55f : 0.65f)); fill();
        strokeColor(theme.ink); strokeWidth(1.4f); stroke();
        line(cx - R - 4, cy, cx + R + 4, cy, withAlpha2(theme.ink, 0.5f), 1.0f);
        restore();
        // correlation of the band and its numbers
        const float tx = d.r() + 14.0f;
        char t[40];
        std::snprintf(t, sizeof t, "%.0f%%", w * 100.0f);
        textAt(tx, d.y + 12, "WIDTH", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.r() - 4, d.y + 12, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "%.0f%%", st * 100.0f);
        textAt(tx, d.y + 32, "STEREOIZE", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.r() - 4, d.y + 32, t, kMono, 11.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        const bool silent = lvShow[b] < -90.0f;
        if (silent) std::snprintf(t, sizeof t, "--"); else std::snprintf(t, sizeof t, "%+.2f", corrShow[b]);
        textAt(tx, d.y + 52, "CORRELATION", kLabel, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(r.r() - 4, d.y + 52, t, kMono, 11.0f, !silent && corrShow[b] < 0 ? theme.red : theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
        corrMeter(Rect(tx, d.y + 66, r.r() - 4 - tx, 9), corrShow[b], false, silent);
    }

    std::string footerInfo() override
    {
        const int lat = plug ? plug->latencyNow() : 0;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%d BAND%s   %s   %s   LATENCY %d", numBands(), numBands() > 1 ? "S" : "",
                      (int)value(kXoverMode) ? "LINEAR PHASE" : "ANALOG", value(kMonoBass) >= 0.5f ? ("MONO < " + spec(kMonoFreq).format(value(kMonoFreq))).c_str() : "STEREO BASS", lat);
        return buf;
    }

    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "sel=%d;", sel); saveUiState(buf); }
    void loadUiState(const std::string& s) override { sel = pump::clampv(std::atoi(uiStateGet(s, "sel", "0").c_str()), 0, kBands - 1); rebind(); }

private:
    static constexpr int kImgW = 696, kImgH = 408;   // 2x the scope's logical size
    PluginWide* plug = nullptr;
    SpectrumView mid, side;
    Rect scope, corr, bandPanel, globalPanel, bandInfo;
    float corrShow[kBands] = {}, lvShow[kBands] = { -120, -120, -120, -120 }, corrAllShow = 0.0f, lvAll = -120.0f, corrMin = 1.0f, corrMinT = 0.0f;
    std::vector<float> fb;
    std::vector<uint8_t> px;
    NanoImage img;
    bool imgDirty = false;
    uint32_t lastPos = 0;
    bool grabMono = false;
    float grabStart = 0.0f, grabEff = 0.0f, grabLastX = 0.0f;

    MenuButton* bandSel;
    Segmented *xoverSeg, *bandsSeg, *slopeSeg;
    Dial *widthDial, *stDial, *monoDial, *balDial, *outDial;
    Button *soloBtn, *muteBtn, *bypBtn, *monoBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIWide)
};

UI* createUI() { return new UIWide(); }

END_NAMESPACE_DISTRHO
