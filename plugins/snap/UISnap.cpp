// pump SNAP: interface. Left: a 3-second history of the input (faint) and output (neon) levels over the gain the shaper
// applies (attack and sustain gain around a 0 dB centre line, in the second colour). Middle: the gain meter and the
// output meters. Right: the shape of a stylised drum hit before and after the current settings; drag it (its peak:
// up/down = attack, left/right = attack time; its tail: up/down = sustain, left/right = release). Below: transients
// (attack, sustain, time, release), detector (focus, link, lookahead), band (full / low / high, crossover) and output
// (clip, delta, ceiling, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginSnap.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::snap;

class UISnap : public PumpUI {
public:
    UISnap() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::snap(), "SNAP", 1000, 620, kBypass)
    {
        atkDial = addDial(kAttack, "ATTACK", Dial::Large, true);
        atkDial->tip = "Attack: boost (+) or soften (-) the start of every hit, whatever its level (or drag the shape's peak up and down)";
        susDial = addDial(kSustain, "SUSTAIN", Dial::Large, true);
        susDial->tip = "Sustain: lengthen (+) or shorten (-) what follows the hit: ring, body and room (or drag the shape's tail)";
        timeDial = addDial(kAttackTime, "TIME");
        timeDial->tip = "Attack time: how long the attack counts as the attack (short = just the click, long = the whole front of the note)";
        relDial = addDial(kRelease, "RELEASE");
        relDial->tip = "Release: the decay the sustain control measures against (+100% sustain decays no faster than this)";
        atkDial->tabGroup = susDial->tabGroup = timeDial->tabGroup = relDial->tabGroup = 1;

        focusSeg = addSegmented(kFocus, { "FULL", "LOW", "MID", "HIGH" });
        focusSeg->tip = "Detector focus: which part of the signal triggers the shaping (Low = the kick drives it). The audio is never filtered by it   (keys 1-4)";
        linkDial = addDial(kLink, "LINK");
        linkDial->tip = "Stereo link: 100% both channels follow the louder detector (stable image), 0% each channel on its own";
        laDial = addDial(kLookahead, "LOOKAHEAD");
        laDial->tip = "Lookahead: the detector runs ahead of the audio so an attack boost is in place on the hit's first sample (adds latency)";
        linkDial->tabGroup = laDial->tabGroup = 2;

        bandSeg = addSegmented(kBand, { "FULL", "LOW", "HIGH" });
        bandSeg->tip = "Band: shape the whole signal, or only the lows or highs of a phase-coherent split (the other band passes untouched)";
        xoDial = addDial(kCrossover, "CROSSOVER");
        xoDial->tip = "Crossover of the band split (24 dB/oct, sums flat)";

        clipBtn = addToggle(kClip, "CLIP");
        clipBtn->small = true;
        clipBtn->tip = "Soft clipper after the output gain: catches boosted attacks at the ceiling";
        deltaBtn = addToggle(kDelta, "DELTA");
        deltaBtn->small = true;
        deltaBtn->holdMomentary = true;
        deltaBtn->tip = "Delta: hear only what the shaper adds or removes (click = latch, click-and-hold = while held; D key)";
        ceilDial = addDial(kCeiling, "CEILING");
        ceilDial->tip = "Clip ceiling: the clipper never lets a peak past this (linear up to 3 dB below it)";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: shaped vs dry (latency-aligned, so parallel shaping stays in phase)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        ceilDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 3;

        before.assign((size_t)kDemoN, 0.0f);
        after.assign((size_t)kDemoN, 0.0f);
        if (auto* p = static_cast<PluginSnap*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kAttack: case kSustain:
            if (std::fabs(v) < 0.5f) return "0%";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        case kLookahead:
            if (v < 0.005f) return "OFF";
            break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        histR = Rect(14, 58, 560, 322);
        gainM = Rect(590, 72, 12, 284);
        outM[0] = Rect(628, 72, 9, 284);
        outM[1] = Rect(639, 72, 9, 284);
        shapeR = Rect(664, 58, 322, 322);
        shapeP = Rect(shapeR.x + 36, shapeR.y + 30, shapeR.w - 46, shapeR.h - 54);

        const float y0 = 396, ph = 192;
        tranPanel = Rect(14, y0, 336, ph);
        // every dial centre on one line (y0 + 117)
        atkDial->r = Rect(26, y0 + 72, 92, Dial::heightFor(Dial::Large));
        susDial->r = Rect(122, y0 + 72, 92, Dial::heightFor(Dial::Large));
        timeDial->r = Rect(214, y0 + 79, 64, Dial::heightFor(Dial::Medium));
        relDial->r = Rect(280, y0 + 79, 64, Dial::heightFor(Dial::Medium));
        atkBar = Rect(92, y0 + 34, 176, 8);
        susBar = Rect(92, y0 + 52, 176, 8);

        detPanel = Rect(364, y0, 214, ph);
        focusSeg->r = Rect(376, y0 + 30, 190, 26);
        linkDial->r = Rect(386, y0 + 79, 72, Dial::heightFor(Dial::Medium));
        laDial->r = Rect(484, y0 + 79, 72, Dial::heightFor(Dial::Medium));

        bandPanel = Rect(592, y0, 140, ph);
        bandSeg->r = Rect(604, y0 + 30, 116, 26);
        xoDial->r = Rect(626, y0 + 79, 72, Dial::heightFor(Dial::Medium));

        outPanel = Rect(746, y0, 240, ph);
        clipBtn->r = Rect(758, y0 + 30, 104, 26);
        deltaBtn->r = Rect(870, y0 + 30, 104, 26);
        ceilDial->r = Rect(756, y0 + 79, 72, Dial::heightFor(Dial::Medium));
        mixDial->r = Rect(830, y0 + 79, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(904, y0 + 79, 72, Dial::heightFor(Dial::Medium));
    }

    // ---- feeds ----
    void tick(float dt) override
    {
        updateShape();
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        Engine& e = plug->engine;
        const float hi = e.gainHi.exchange(0.0f), lo = e.gainLo.exchange(0.0f);
        gHi = std::max(hi, gHi - 40.0f * dt);
        gLo = std::min(lo, gLo + 40.0f * dt);
        if (hi >= gHiHold) { gHiHold = hi; gHiT = 1.5f; } else if ((gHiT -= dt) <= 0.0f) gHiHold = std::max(hi, gHiHold - 12.0f * dt);
        if (lo <= gLoHold) { gLoHold = lo; gLoT = 1.5f; } else if ((gLoT -= dt) <= 0.0f) gLoHold = std::min(lo, gLoHold + 12.0f * dt);
        // attack and sustain parts: the larger excursion since the last frame, falling back at 40 dB/s
        auto part = [&](pump::MeterValue& h, pump::MeterValue& l, float& show) {
            const float a = h.exchange(0.0f), b = l.exchange(0.0f);
            const float v = std::fabs(b) > a ? b : a;
            const float back = 40.0f * dt;
            const float decayed = show > 0.0f ? std::max(0.0f, show - back) : std::min(0.0f, show + back);
            show = std::fabs(v) >= std::fabs(decayed) ? v : decayed;
        };
        part(e.atkHi, e.atkLo, atkShow);
        part(e.susHi, e.susLo, susShow);
    }

    // the stylised hit through the current settings (recomputed when they change)
    void updateShape()
    {
        Settings s;
        s.attack = value(kAttack); s.sustain = value(kSustain); s.attackMs = value(kAttackTime); s.releaseMs = value(kRelease);
        s.lookaheadMs = value(kLookahead); s.mix = value(kMix);
        const float key[6] = { (float)s.attack, (float)s.sustain, (float)s.attackMs, (float)s.releaseMs, (float)s.lookaheadMs, (float)s.mix };
        if (shapeValid && std::equal(key, key + 6, shapeKey)) return;
        std::copy(key, key + 6, shapeKey);
        shapeValid = true;
        shapeDemo(s, kDemoRate, kT0, kDemoN, before.data(), after.data());
        // per display column: the loudest sample it covers (dB), so the click shows at any width
        const int cols = std::min((int)shapeP.w, kMaxCols);
        for (int c = 0; c < cols; ++c)
        {
            const int i0 = pump::clampv(idxOfT(tOfX(shapeP.x + c)), 0, kDemoN - 1);
            const int i1 = pump::clampv(std::max(i0 + 1, idxOfT(tOfX(shapeP.x + c + 1.0f))), 1, kDemoN);
            float b = 0.0f, a = 0.0f;
            for (int i = i0; i < i1; ++i) { b = std::max(b, before[(size_t)i]); a = std::max(a, after[(size_t)i]); }
            colB[c] = pump::gainToDbF(b); colA[c] = pump::gainToDbF(a);
        }
        float pb = 0.0f, pa = 0.0f;
        for (int i = 0; i <= idxOfT(0.020f); ++i) { pb = std::max(pb, before[(size_t)i]); pa = std::max(pa, after[(size_t)i]); }
        peakDb = pump::gainToDbF(pa / std::max(1e-9f, pb));
        const int it = idxOfT(kTailT);
        tailDb = pump::gainToDbF(after[(size_t)it] / std::max(1e-9f, before[(size_t)it]));
        // the handles: the shaped peak (within the first 40 ms) and the shaped tail at kTailT
        int pc = 0;
        for (int c = 0; c < cols && tOfX(shapeP.x + c) < 0.040f; ++c) if (colA[c] > colA[pc]) pc = c;
        peakH[0] = shapeP.x + pc + 0.5f; peakH[1] = yOfShapeDb(colA[pc]);
        tailH[0] = xOfT(kTailT); tailH[1] = yOfShapeDb(pump::gainToDbF(after[(size_t)it]));
    }

    // ---- drawing ----
    void drawBody() override
    {
        drawHistory();
        drawGainMeter();
        drawLevelMeter(outM[0], lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(outM[1], lvl[1], hold[1], -48.0f, 6.0f);
        textAt(gainM.cx(), shapeR.b() - 10, "GAIN", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        textAt(outM[0].r() + 1, shapeR.b() - 10, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        drawShape();

        strokeRect(tranPanel, theme.dim, 1.0f);
        strokeRect(detPanel, theme.dim, 1.0f);
        strokeRect(bandPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(tranPanel.x + 12, tranPanel.y + 16, "TRANSIENTS");
        caption(detPanel.x + 12, detPanel.y + 16, "DETECTOR");
        caption(bandPanel.x + 12, bandPanel.y + 16, "BAND");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        static const char* const focusText[] = { "FULL RANGE", "BELOW 150 HZ", "300 HZ - 3 KHZ", "ABOVE 3 KHZ" };
        textAt(detPanel.r() - 12, detPanel.y + 16, focusText[pump::clampv((int)value(kFocus), 0, kNumFocus - 1)], kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        drawPartBar(atkBar, "ATTACK", atkShow, true);
        drawPartBar(susBar, "SUSTAIN", susShow, false);
        const int band = pump::clampv((int)value(kBand), 0, kNumBands - 1);
        textAt(bandPanel.r() - 12, bandPanel.y + 16, band == kBandFull ? "ALL" : (band == kBandLow ? "LOWS" : "HIGHS"), kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
    }

    // a live bipolar bar for the attack or the sustain part of the gain (same scale as the gain meter)
    void drawPartBar(const Rect& r, const char* label, float db, bool solid)
    {
        textAt(r.x - 10, r.cy(), label, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        fillRect(r, theme.panel2);
        const float cx = r.cx(), w = gainScale(db) * r.w * 0.5f;
        if (std::fabs(w) > 0.5f) fillRect(Rect(std::min(cx, cx + w), r.y, std::fabs(w), r.h), solid ? theme.second : withAlpha2(theme.second, 0.45f));
        strokeRect(r, theme.ink, 1.0f);
        line(cx, r.y - 3, cx, r.b() + 3, theme.ink, 1.0f);
        char b[16];
        std::snprintf(b, sizeof b, std::fabs(db) < 0.05f ? "0.0 dB" : "%+.1f dB", db);
        textAt(r.r() + 54, r.cy(), b, kMono, 10.0f, theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // history geometry: levels on top (dBFS), gain below (dB around 0)
    Rect plotR() const { return Rect(histR.x + 10, histR.y + 28, histR.w - 20, histR.h - 38); }
    Rect levelR() const { const Rect p = plotR(); return Rect(p.x, p.y, p.w, std::floor(p.h * 0.54f)); }
    Rect gainR() const { const Rect p = plotR(), l = levelR(); return Rect(p.x, l.b() + 10, p.w, p.b() - l.b() - 10); }
    static constexpr float kLevelTop = 0.0f, kLevelBottom = -54.0f, kGainRange = 24.0f;
    float yOfLevel(float db) const { const Rect l = levelR(); return l.y + (kLevelTop - pump::clampv(db, kLevelBottom - 3.0f, kLevelTop + 1.0f)) / (kLevelTop - kLevelBottom) * l.h; }
    // the gain scale is compressed (log(1 + |dB| / 4)): a 2 dB nudge is visible, a 24 dB swing still fits
    static float gainScale(float db) { const float a = std::log1p(std::min(std::fabs(db), kGainRange) * 0.25f) / std::log1p(kGainRange * 0.25f); return db < 0.0f ? -a : a; }
    float yOfGain(float db) const { const Rect g = gainR(); return g.cy() - gainScale(db) * (g.h * 0.5f - 2.0f); }

    void drawHistory()
    {
        const Rect& r = histR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "HISTORY");
        // legend (top right)
        {
            float lx = r.r() - 10.0f;
            auto legend = [&](const char* s, const Color& c, bool solid) {
                const float tw = textWidth(s, kLabel, 9.5f, 0.8f);
                textAt(lx, r.y + 14, s, kLabel, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
                lx -= tw + 6.0f;
                const Rect sw(lx - 12.0f, r.y + 10, 12.0f, 8.0f);
                fillRect(sw, solid ? c : withAlpha2(c, 0.35f));
                strokeRect(sw, solid ? theme.ink : c, 1.0f);
                lx -= 24.0f;
            };
            legend("SUSTAIN", theme.second, false);
            legend("ATTACK", theme.second, true);
            legend("OUT", theme.neon, true);
            legend("IN", withAlpha2(theme.ink, theme.dark ? 0.25f : 0.15f), true);
        }
        const Rect lr = levelR(), gr = gainR();
        // grids
        for (float db = kLevelTop; db > kLevelBottom; db -= 12.0f) line(lr.x, yOfLevel(db), lr.r(), yOfLevel(db), db == 0.0f ? withAlpha2(theme.ink, 0.20f) : theme.grid, 1.0f);
        line(lr.x, lr.b() + 4, lr.r(), lr.b() + 4, theme.dim, 1.0f);
        for (float db : { -12.0f, -3.0f, 3.0f, 12.0f }) line(gr.x, yOfGain(db), gr.r(), yOfGain(db), theme.grid, 1.0f);
        const int cols = std::min((int)lr.w, kMaxCols), step = 2;
        uint32_t endIdx = 0;
        bool have = false;
        save();
        scissor(lr.x, lr.y, lr.w, gr.b() - lr.y);
        if (plug)
        {
            Engine& e = plug->engine;
            endIdx = e.histIn.smoothPosition();
            endIdx -= endIdx % step;
            const uint32_t need = (uint32_t)(cols * step);
            const float decay = 120.0f * step / (float)Engine::kHistRate;   // display release, dB per column
            for (int k = 0; k < cols; ++k)
            {
                float a = -200.0f, b = -200.0f, ga = 0.0f, gs = 0.0f;
                for (int j = 0; j < step; ++j)
                {
                    const int64_t idx = (int64_t)endIdx - (int64_t)need + (int64_t)k * step + j;
                    if (idx < 0) continue;
                    a = std::max(a, e.histIn.at((uint32_t)idx));
                    b = std::max(b, e.histOut.at((uint32_t)idx));
                    const float va = e.histAtk.at((uint32_t)idx), vs = e.histSus.at((uint32_t)idx);
                    if (std::fabs(va) > std::fabs(ga)) ga = va;
                    if (std::fabs(vs) > std::fabs(gs)) gs = vs;
                }
                if (k > 0) { a = std::max(a, hin[k - 1] - decay); b = std::max(b, hout[k - 1] - decay); }
                hin[k] = a; hout[k] = b; hatk[k] = ga; hsus[k] = gs;
            }
            have = true;
            // one faint line per second, scrolling with the audio
            for (int k = 0; k < cols; ++k)
            {
                const int64_t i0 = (int64_t)endIdx - (int64_t)need + (int64_t)k * step;
                if (i0 >= 0 && (i0 % (int64_t)Engine::kHistRate) < step) line(lr.x + k + 0.5f, lr.y, lr.x + k + 0.5f, gr.b(), theme.grid, 1.0f);
            }
        }
        if (have)
        {
            auto trace = [&](const float* v, auto yf) {
                beginPath();
                for (int k = 0; k < cols; ++k) { const float y = yf(v[k]); if (k == 0) moveTo(lr.x + 0.5f, y); else lineTo(lr.x + k + 0.5f, y); }
            };
            auto area = [&](const float* v, auto yf, float base) {
                beginPath(); moveTo(lr.x, base);
                for (int k = 0; k < cols; ++k) lineTo(lr.x + k + 0.5f, yf(v[k]));
                lineTo(lr.x + cols, base); closePath();
            };
            auto yl = [&](float d) { return yOfLevel(d); };
            auto yg = [&](float d) { return yOfGain(d); };
            const float lb = lr.b() + 2.0f;
            area(hin, yl, lb); fillColor(withAlpha2(theme.ink, theme.dark ? 0.13f : 0.09f)); fill();
            area(hout, yl, lb); fillColor(withAlpha2(theme.neon, theme.dark ? 0.30f : 0.45f)); fill();
            trace(hout, yl); strokeColor(theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.40f)); strokeWidth(1.4f); lineJoin(ROUND); stroke();
            trace(hin, yl); strokeColor(withAlpha2(theme.ink, theme.dark ? 0.45f : 0.38f)); strokeWidth(1.0f); stroke();
            // gain: sustain (light) under attack (solid), both around the centre line
            const float g0 = yOfGain(0.0f);
            area(hsus, yg, g0); fillColor(withAlpha2(theme.second, theme.dark ? 0.30f : 0.28f)); fill();
            trace(hsus, yg); strokeColor(withAlpha2(theme.second, 0.9f)); strokeWidth(1.2f); stroke();
            area(hatk, yg, g0); fillColor(withAlpha2(theme.second, 0.85f)); fill();
        }
        restore();
        line(gr.x, yOfGain(0.0f), gr.r(), yOfGain(0.0f), withAlpha2(theme.ink, 0.45f), 1.0f);
        // scale labels (left edge, on a small backing so they read over the traces)
        auto tag = [&](float y, const std::string& t) {
            const float tw = textWidth(t, kMono, 9.0f);
            fillRect(Rect(lr.x + 2, y - 6.0f, tw + 6.0f, 12.0f), withAlpha2(theme.screen, 0.85f));
            textAt(lr.x + 5, y, t, kMono, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        };
        for (float db = -12.0f; db > kLevelBottom; db -= 12.0f) { char b[8]; std::snprintf(b, sizeof b, "%.0f", db); tag(yOfLevel(db), b); }
        tag(yOfLevel(0.0f) + 7.0f, "0 dBFS");
        tag(yOfGain(12.0f), "+12");
        tag(yOfGain(0.0f), "0 dB");
        tag(yOfGain(-12.0f), "-12");
        textAt(gr.r() - 4, gr.y + 7, "GAIN", kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        if (value(kDelta) >= 0.5f)
        {
            const std::string s = "DELTA: HEARING ONLY WHAT THE SHAPER CHANGES";
            const float tw = textWidth(s, kLabel, 11.0f, 1.2f) + 20.0f;
            const Rect b(lr.cx() - tw * 0.5f, lr.y + 8.0f, tw, 24);
            fillRect(b, theme.second);
            textAt(b.cx(), b.cy() + 0.5f, s, kLabel, 11.0f, theme.bg, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
        }
    }

    // the gain meter: what the shaper does right now, boosts up and cuts down from 0 dB
    void drawGainMeter()
    {
        const Rect& r = gainM;
        fillRect(r, theme.panel2);
        auto yOf = [&](float db) { return r.cy() - gainScale(db) * r.h * 0.5f; };
        const float y0 = yOf(0.0f);
        if (gHi > 0.05f) fillRect(Rect(r.x, yOf(gHi), r.w, y0 - yOf(gHi)), theme.neon);
        if (gLo < -0.05f) fillRect(Rect(r.x, y0, r.w, yOf(gLo) - y0), theme.second);
        if (gHiHold > 0.1f) line(r.x, yOf(gHiHold), r.r(), yOf(gHiHold), theme.ink, 1.5f);
        if (gLoHold < -0.1f) line(r.x, yOf(gLoHold), r.r(), yOf(gLoHold), theme.ink, 1.5f);
        line(r.x - 3, y0, r.r() + 3, y0, theme.ink, 1.0f);
        strokeRect(r, theme.ink, 1.0f);
        for (float db : { 24.0f, 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -24.0f })
        {
            char b[8]; std::snprintf(b, sizeof b, db > 0.0f ? "+%.0f" : "%.0f", db);
            textAt(r.r() + 13, yOf(db), b, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    // shape geometry: a warped time axis (asinh: the first milliseconds get room, the tail is compressed) and dB
    static constexpr float kT0 = -0.006f, kT1 = 0.700f, kTau = 0.008f, kTailT = 0.150f;
    static constexpr float kShapeTop = 18.0f, kShapeBottom = -48.0f;
    static constexpr double kDemoRate = 8000.0;
    static constexpr int kDemoN = (int)((kT1 - kT0) * kDemoRate) + 1, kMaxCols = 600;
    float xOfT(float t) const
    {
        const float a = std::asinh(kT0 / kTau), b = std::asinh(kT1 / kTau);
        return shapeP.x + (std::asinh(t / kTau) - a) / (b - a) * shapeP.w;
    }
    float tOfX(float x) const
    {
        const float a = std::asinh(kT0 / kTau), b = std::asinh(kT1 / kTau);
        return kTau * std::sinh(a + (x - shapeP.x) / shapeP.w * (b - a));
    }
    int idxOfT(float t) const { return pump::clampv((int)std::lround((t - kT0) * kDemoRate), 0, kDemoN - 1); }
    float yOfShapeDb(float db) const { return shapeP.y + (kShapeTop - pump::clampv(db, kShapeBottom - 6.0f, kShapeTop)) / (kShapeTop - kShapeBottom) * shapeP.h; }

    void drawShape()
    {
        const Rect& r = shapeR;
        const Rect& p = shapeP;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SHAPE");
        char t[64];
        std::snprintf(t, sizeof t, "PEAK %+.1f dB   TAIL %+.1f dB", peakDb, tailDb);
        textAt(r.r() - 10, r.y + 14, t, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        // grid: dB lines, time lines
        for (float db = 12.0f; db >= kShapeBottom; db -= 12.0f)
        {
            line(p.x, yOfShapeDb(db), p.r(), yOfShapeDb(db), db == 0.0f ? withAlpha2(theme.ink, 0.22f) : theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, db > 0.0f ? "+%.0f" : "%.0f", db);
            textAt(p.x - 5, yOfShapeDb(db), b, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        static const struct { float t; const char* s; } tl[] = { { 0.0f, "0" }, { 0.005f, "5" }, { 0.020f, "20" }, { 0.100f, "100" }, { 0.500f, "500 ms" } };
        for (const auto& l : tl)
        {
            const float x = xOfT(l.t);
            line(x, p.y, x, p.b(), l.t == 0.0f ? withAlpha2(theme.ink, 0.22f) : theme.grid, 1.0f);
            textAt(x, p.b() + 10, l.s, kMono, 8.5f, theme.ink2, l.t >= 0.5f ? (ALIGN_RIGHT | ALIGN_MIDDLE) : (ALIGN_CENTER | ALIGN_MIDDLE));
        }
        if (!shapeValid) return;
        const int cols = std::min((int)p.w, kMaxCols);
        save();
        scissor(p.x, p.y, p.w, p.h);
        auto area = [&](const float* v) {
            beginPath(); moveTo(p.x, p.b());
            for (int c = 0; c < cols; ++c) lineTo(p.x + c + 0.5f, yOfShapeDb(v[c]));
            lineTo(p.x + cols, p.b()); closePath();
        };
        auto trace = [&](const float* v) {
            beginPath();
            for (int c = 0; c < cols; ++c) { const float y = yOfShapeDb(v[c]); if (c == 0) moveTo(p.x + 0.5f, y); else lineTo(p.x + c + 0.5f, y); }
        };
        area(colB); fillColor(withAlpha2(theme.ink, theme.dark ? 0.13f : 0.09f)); fill();
        area(colA); fillColor(withAlpha2(theme.neon, theme.dark ? 0.26f : 0.40f)); fill();
        trace(colB); strokeColor(withAlpha2(theme.ink, theme.dark ? 0.50f : 0.42f)); strokeWidth(1.2f); stroke();
        trace(colA); strokeColor(theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.45f)); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        restore();
        // handles
        auto handle = [&](const float* h, bool hot, const char* label, bool labelLeft) {
            const Rect b(h[0] - 5.0f, h[1] - 5.0f, 10.0f, 10.0f);
            Color n = hot ? theme.ink : theme.neon;
            fillRect(b, n);
            strokeRect(b, theme.ink, 1.5f);
            if (hot) textAt(labelLeft ? b.x - 6 : b.r() + 6, b.cy(), label, kLabel, 9.5f, theme.ink, (labelLeft ? ALIGN_RIGHT : ALIGN_LEFT) | ALIGN_MIDDLE, 0.8f);
        };
        const float pk[2] = { peakH[0], pump::clampv(peakH[1], p.y + 5.0f, p.b() - 5.0f) };
        const float tl2[2] = { tailH[0], pump::clampv(tailH[1], p.y + 5.0f, p.b() - 5.0f) };
        handle(tl2, drag == kDragTail || (drag == kDragNone && hover == kDragTail), "SUSTAIN / RELEASE", true);
        handle(pk, drag == kDragPeak || (drag == kDragNone && hover == kDragPeak), "ATTACK / TIME", false);
        // legend (top right of the plot, where the hit never reaches)
        {
            float lx = p.r() - 6.0f;
            const float ly = p.y + 10.0f;
            auto legend = [&](const char* s, const Color& fillC, const Color& edge) {
                textAt(lx, ly, s, kLabel, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
                lx -= textWidth(s, kLabel, 9.0f, 0.8f) + 6.0f;
                const Rect sw(lx - 12.0f, ly - 4.0f, 12.0f, 8.0f);
                fillRect(sw, fillC); strokeRect(sw, edge, 1.0f);
                lx -= 24.0f;
            };
            legend("BEFORE", withAlpha2(theme.ink, theme.dark ? 0.13f : 0.09f), withAlpha2(theme.ink, 0.5f));
            legend("AFTER", withAlpha2(theme.neon, theme.dark ? 0.5f : 0.6f), theme.ink);
        }
    }

    // ---- the shape is a control: its peak = attack (up/down) and time (left/right), its tail = sustain and release ----
    enum Drag { kDragNone = 0, kDragPeak, kDragTail };
    int pickHandle(float x, float y) const
    {
        const float dp = std::hypot(x - peakH[0], y - peakH[1]), dt = std::hypot(x - tailH[0], y - tailH[1]);
        if (std::min(dp, dt) < 14.0f) return dp <= dt ? kDragPeak : kDragTail;
        return tOfX(x) < 0.030f ? kDragPeak : kDragTail;
    }
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (!shapeR.contains(x, y) || button != kMouseButtonLeft) return false;
        const int h = pickHandle(x, y);
        const int pv = h == kDragPeak ? kAttack : kSustain, ph = h == kDragPeak ? kAttackTime : kRelease;
        if (dbl || cmdKey(mod) || (mod & kModifierAlt))
        {
            beginGroup(); setParamGesture(pv, spec(pv).def); setParamGesture(ph, spec(ph).def); endGroup();
            return false;
        }
        drag = h; dragX = x; dragY = y;
        dragV = value(pv); dragH = spec(ph).toNorm(value(ph));
        beginEdit(pv); beginEdit(ph);
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (drag == kDragNone) return;
        const int pv = drag == kDragPeak ? kAttack : kSustain, ph = drag == kDragPeak ? kAttackTime : kRelease;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParam(pv, dragV + (dragY - y) * 0.6f * f);
        setParamNorm(ph, dragH + (x - dragX) * 0.0035f * f);
        if (mod & kModifierShift) { dragV = value(pv); dragH = spec(ph).toNorm(value(ph)); dragX = x; dragY = y; }
        setStatus(paramStatus(pv) + "    " + paramStatus(ph));
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == kDragNone) return;
        const int pv = drag == kDragPeak ? kAttack : kSustain, ph = drag == kDragPeak ? kAttackTime : kRelease;
        drag = kDragNone;
        endEdit(ph); endEdit(pv);
    }
    void bodyHover(float x, float y) override
    {
        const int was = hover;
        hover = kDragNone;
        if (shapeR.contains(x, y))
        {
            hover = pickHandle(x, y);
            setStatus(hover == kDragPeak ? "Drag the peak: up/down = attack, left/right = attack time (Shift = fine, double-click = reset)"
                                         : "Drag the tail: up/down = sustain, left/right = release (Shift = fine, double-click = reset)");
        }
        else if (histR.contains(x, y)) setStatus("History, 3 s: input (faint) and output levels; below, the attack (solid) and sustain (light) gain");
        else if (Rect(gainM.x - 4, histR.y, outM[1].r() - gainM.x + 8, histR.h).contains(x, y)) setStatus("Gain meter: the boost (up) and cut (down) right now, with held peaks; output level L/R");
        if (hover != was) repaint();
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        const uint k = key >= 'A' && key <= 'Z' ? key + 32 : key;
        if (k == 'd') { setParamGesture(kDelta, value(kDelta) >= 0.5f ? 0.0f : 1.0f); setStatus(paramStatus(kDelta)); return true; }
        if (key >= '1' && key <= '4') { setParamGesture(kFocus, (float)(key - '1')); setStatus(paramStatus(kFocus)); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the shape's peak", "up/down = attack, left/right = attack time (double-click resets both)" },
            { "Drag the shape's tail", "up/down = sustain, left/right = release (double-click resets both)" },
            { "D", "delta on/off (hear only what the shaper changes)" },
            { "1 - 4", "detector focus: full, low, mid, high" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        drawPartBar(atkBar, "ATTACK", atkShow, true);
        drawPartBar(susBar, "SUSTAIN", susShow, false);
        const int band = pump::clampv((int)value(kBand), 0, kNumBands - 1);
        char bandText[48];
        if (band == kBandFull) std::snprintf(bandText, sizeof bandText, "FULL BAND");
        else std::snprintf(bandText, sizeof bandText, "%s BAND %s %s", kBandNames[band], band == kBandLow ? "BELOW" : "ABOVE", formatParam(kCrossover, value(kCrossover)).c_str());
        std::snprintf(buf, sizeof buf, "%s FOCUS   %s   LATENCY %d", kFocusNames[pump::clampv((int)value(kFocus), 0, kNumFocus - 1)], bandText, plug ? plug->latencyNow() : 0);
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    PluginSnap* plug = nullptr;
    Rect histR, gainM, outM[2], shapeR, shapeP, tranPanel, detPanel, bandPanel, outPanel, atkBar, susBar;
    float atkShow = 0.0f, susShow = 0.0f;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    float gHi = 0.0f, gLo = 0.0f, gHiHold = 0.0f, gLoHold = 0.0f, gHiT = 0.0f, gLoT = 0.0f;
    float hin[kMaxCols] = {}, hout[kMaxCols] = {}, hatk[kMaxCols] = {}, hsus[kMaxCols] = {};
    std::vector<float> before, after;
    float colB[kMaxCols] = {}, colA[kMaxCols] = {};
    float shapeKey[6] = {}, peakDb = 0.0f, tailDb = 0.0f, peakH[2] = { 0, 0 }, tailH[2] = { 0, 0 };
    bool shapeValid = false;
    int drag = kDragNone, hover = kDragNone;
    float dragX = 0, dragY = 0, dragV = 0, dragH = 0;

    Dial *atkDial, *susDial, *timeDial, *relDial, *linkDial, *laDial, *xoDial, *ceilDial, *mixDial, *outDial;
    Segmented *focusSeg, *bandSeg;
    Button *clipBtn, *deltaBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UISnap)
};

UI* createUI() { return new UISnap(); }

END_NAMESPACE_DISTRHO
