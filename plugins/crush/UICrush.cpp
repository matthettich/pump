// pump CRUSH: interface. Three screens: the converter (rate across, bits up; drag the point, or pick a voicing's
// point), the spectrum of the output over the input (with the new Nyquist marked: everything right of it that
// wasn't there before is aliasing), and a close-up of the waveform where the held, quantised steps show. Below:
// the sampler (voicing, drive, bits, rate, jitter), the media (wow, flutter, age, hiss, crackle, dropouts, and
// whether the media comes after or before the sampler) and the output (low cut, high cut, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginCrush.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::crush;

// Voicing keys: a click picks the voicing with its rate and word length; Shift-click changes only the character.
struct VoicingKeys : Segmented {
    std::function<void(int, bool)> pick;
    bool mouseDown(PumpUI& ui, float x, float y, int button, uint mod) override
    {
        if (button != kMouseButtonLeft) return false;
        const int n = count(ui);
        const int i = std::min(n - 1, std::max(0, (int)((x - r.x) / r.w * n)));
        if (pick) pick(i, (mod & kModifierShift) != 0);
        return true;
    }
};

class UICrush : public PumpUI {
public:
    UICrush() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::crush(), "CRUSH", 1000, 620, kBypass)
    {
        modeSeg = add(new VoicingKeys());
        modeSeg->param = kMode;
        for (const char* k : kModeKeys) modeSeg->options.push_back(k);
        modeSeg->pick = [this](int i, bool characterOnly) { pickVoicing(i, characterOnly); };
        modeSeg->tip = "Voicing: the converter's character (quantiser and filters). Click = with its rate and bits; Shift-click = character only   (keys 1-7)";
        driveDial = addDial(kDrive, "DRIVE", Dial::Large);
        driveDial->tip = "Drive into the converter: its warmth, then a hard clip at full scale";
        bitsDial = addDial(kBits, "BITS");
        bitsDial->tip = "Word length, 1..24 (fractional values sweep smoothly; 24 = off)";
        rateDial = addDial(kRate, "RATE");
        rateDial->tip = "Sample rate of the converter (the top of the range = off). Below the input's top frequency it aliases";
        jitterDial = addDial(kJitter, "JITTER");
        jitterDial->tip = "Clock jitter: the converter's samples land unevenly (grain, then noise)";
        driveDial->tabGroup = bitsDial->tabGroup = rateDial->tabGroup = jitterDial->tabGroup = 1;

        orderSeg = addSegmented(kOrder, { "AFTER", "BEFORE" });
        orderSeg->tip = "Media after the sampler (a worn copy of a crushed sample) or before it (sampling something already worn: the noise gets crushed too)";
        wowDial = addDial(kWow, "WOW", Dial::Small);
        wowDial->tip = "Wow: slow pitch drift of a worn transport";
        flDial = addDial(kFlutter, "FLUTTER", Dial::Small);
        flDial->tip = "Flutter: fast pitch wobble";
        ageDial = addDial(kAge, "AGE", Dial::Small);
        ageDial->tip = "Age: lost highs and lows, a little saturation, slow level drift";
        hissDial = addDial(kHiss, "HISS", Dial::Small);
        hissDial->tip = "Hiss: tape noise";
        crackleDial = addDial(kCrackle, "CRACKLE", Dial::Small);
        crackleDial->tip = "Crackle: surface noise, clicks and the odd pop";
        dropDial = addDial(kDropout, "DROPOUTS", Dial::Small);
        dropDial->tip = "Dropouts: random dips in level, darker while they last";
        wowDial->tabGroup = flDial->tabGroup = ageDial->tabGroup = hissDial->tabGroup = crackleDial->tabGroup = dropDial->tabGroup = 2;

        lowDial = addDial(kLowCut, "LOW CUT", Dial::Small);
        lowDial->tip = "Low cut on the output";
        highDial = addDial(kHighCut, "HIGH CUT", Dial::Small);
        highDial->tip = "High cut on the output";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: crushed vs clean (latency-aligned, so parallel crushing stays in phase)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        lowDial->tabGroup = highDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 3;

        if (auto* p = static_cast<PluginCrush*>(pluginPtr())) plug = p;
        const double sr = plug && plug->getSampleRate() > 0.0 ? plug->getSampleRate() : sampleRate();
        onSampleRateChanged(sr);
    }

protected:
    void onSampleRateChanged(double sr) override
    {
        pre.setup(sr); post.setup(sr);
        pre.setRange(kLo, kHi); post.setRange(kLo, kHi);
    }

    void pickVoicing(int i, bool characterOnly)
    {
        i = pump::clampv(i, 0, kNumModes - 1);
        if (characterOnly) { setParamGesture(kMode, (float)i); return; }
        const Voicing& v = voicing(i);
        beginGroup();
        setParamGesture(kMode, (float)i);
        setParamGesture(kBits, (float)v.bits);
        setParamGesture(kRate, (float)v.rate);
        endGroup();
    }

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kRate: if (!rateActive(v)) return "OFF"; break;
        case kBits:
            if (!bitsActive(v)) return "OFF";
            std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.05f ? "%.0f BIT" : "%.1f BIT", v); return b;
        case kLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kHighCut: if (!highCutActive(v)) return "OFF"; break;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        convR = Rect(14, 58, 300, 322);
        specR = Rect(330, 58, 392, 322);
        waveR = Rect(738, 58, 210, 322);
        meterL = Rect(962, 58, 9, 304);
        meterR = Rect(973, 58, 9, 304);
        const float y0 = 396;
        samplerPanel = Rect(14, y0, 440, 192);
        modeSeg->r = Rect(26, y0 + 30, 416, 28);
        driveDial->r = Rect(26, y0 + 72, 96, Dial::heightFor(Dial::Large));
        bitsDial->r = Rect(146, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        rateDial->r = Rect(240, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        jitterDial->r = Rect(334, y0 + 80, 72, Dial::heightFor(Dial::Medium));

        mediaPanel = Rect(468, y0, 308, 192);
        orderSeg->r = Rect(mediaPanel.r() - 12 - 128, y0 + 6, 128, 20);
        const float c0 = 478, cw = 68;
        wowDial->r = Rect(c0, y0 + 30, 64, Dial::heightFor(Dial::Small));
        flDial->r = Rect(c0 + cw, y0 + 30, 64, Dial::heightFor(Dial::Small));
        ageDial->r = Rect(c0 + 2 * cw, y0 + 30, 64, Dial::heightFor(Dial::Small));
        hissDial->r = Rect(c0, y0 + 108, 64, Dial::heightFor(Dial::Small));
        crackleDial->r = Rect(c0 + cw, y0 + 108, 64, Dial::heightFor(Dial::Small));
        dropDial->r = Rect(c0 + 2 * cw, y0 + 108, 64, Dial::heightFor(Dial::Small));
        transportR = Rect(c0 + 3 * cw + 4, y0 + 46, 82, 128);

        outPanel = Rect(790, y0, 196, 192);
        lowDial->r = Rect(806, y0 + 30, 64, Dial::heightFor(Dial::Small));
        highDial->r = Rect(906, y0 + 30, 64, Dial::heightFor(Dial::Small));
        mixDial->r = Rect(802, y0 + 106, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(902, y0 + 106, 72, Dial::heightFor(Dial::Medium));
    }

    void tick(float dt) override
    {
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        pre.update(&plug->inRing, nullptr, dt);
        post.update(&plug->outRing, nullptr, dt);
        // both traces end at the same sample (the rings are written together)
        const uint32_t end = plug->inRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i)
        {
            const uint32_t idx = end - (uint32_t)kScopeN + (uint32_t)i;
            scopeIn[i] = plug->inRing.at(idx);
            scopeOut[i] = plug->outRing.at(idx);
        }
        const float k = std::min(1.0f, dt * 14.0f);
        speedShow += (plug->engine.wowNow() - speedShow) * k;
        const float dr = plug->engine.dropNow();
        dropShow = dr < dropShow ? dr : dropShow + (dr - dropShow) * k;
        if (plug->engine.clicksNow() > 0.0f) clickFlash = 1.0f;
        else clickFlash = std::max(0.0f, clickFlash - dt * 8.0f);
    }

    void drawBody() override
    {
        drawConverter();
        drawSpectrumScreen();
        drawWave();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, convR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(samplerPanel, theme.dim, 1.0f);
        strokeRect(mediaPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(samplerPanel.x + 12, samplerPanel.y + 16, "SAMPLER");
        caption(mediaPanel.x + 12, mediaPanel.y + 16, "MEDIA");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        // what the voicing is made of
        const Voicing& v = voicing(pump::clampv((int)value(kMode), 0, kNumModes - 1));
        static const char* const qn[] = { "LINEAR", "MU-LAW", "UNEVEN BITS" };
        char t[96];
        std::snprintf(t, sizeof t, "%s  ·  IN %s  ·  OUT %s%s", qn[pump::clampv(v.quant, 0, 2)],
                      v.aaPoles ? (v.aaPoles == 8 ? "8-POLE" : (v.aaPoles == 4 ? "4-POLE" : "2-POLE")) : "UNFILTERED",
                      v.reconPoles ? (v.reconPoles == 4 ? "4-POLE" : "2-POLE") : "UNFILTERED",
                      v.reconQ > 1.0 ? " RESONANT" : "");
        textAt(samplerPanel.x + 12, samplerPanel.b() - 12, t, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        drawTransport();
    }

    // ---- the converter: rate (log, across) x bits (up) ----
    Rect convPlot() const { return Rect(convR.x + 34, convR.y + 30, convR.w - 48, convR.h - 62); }
    float rateX(const Rect& p, float rate) const { return p.x + spec(kRate).toNorm(rate) * p.w; }
    float bitsY(const Rect& p, float bits) const { return p.b() - spec(kBits).toNorm(bits) * p.h; }
    void drawConverter()
    {
        const Rect& r = convR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "CONVERTER");
        textAt(r.r() - 10, r.y + 14, kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)], kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Rect p = convPlot();
        for (float rt : { 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = rateX(p, rt);
            line(x, p.y, x, p.b(), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, rt >= 1000.0f ? "%.0fk" : "%.0f", rt >= 1000.0f ? rt / 1000.0f : rt);
            textAt(x, p.b() + 10, b, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        textAt(p.r(), p.b() + 10, "OFF", kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int b : { 4, 8, 12, 16, 20 })
        {
            const float y = bitsY(p, (float)b);
            line(p.x, y, p.r(), y, theme.grid, 1.0f);
            char s[8]; std::snprintf(s, sizeof s, "%d", b);
            textAt(p.x - 6, y, s, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        textAt(p.x - 6, p.y + 2, "OFF", kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        strokeRect(p, withAlpha2(theme.ink, 0.35f), 1.0f);
        // the voicings' own points
        for (int m = 0; m < kNumModes; ++m)
        {
            const Voicing& v = voicing(m);
            const float x = rateX(p, (float)v.rate), y = bitsY(p, (float)v.bits);
            fillRect(Rect(x - 2.5f, y - 2.5f, 5, 5), theme.dim);
            // numbered like the keys 1-7; close neighbours alternate above and below
            const bool below = m == kDark12 || m == kPhone;
            char k[4]; std::snprintf(k, sizeof k, "%d", m + 1);
            textAt(x, below ? y + 10 : y - 9, k, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        // the crushed area: everything below and left of the point is gone
        const float px = rateX(p, value(kRate)), py = bitsY(p, value(kBits));
        fillRect(Rect(px, p.y, p.r() - px, p.h), withAlpha2(theme.neon, theme.dark ? 0.10f : 0.18f));
        fillRect(Rect(p.x, p.y, px - p.x, py - p.y), withAlpha2(theme.neon, theme.dark ? 0.10f : 0.18f));
        line(px, p.y, px, p.b(), withAlpha2(theme.ink, 0.5f), 1.0f);
        line(p.x, py, p.r(), py, withAlpha2(theme.ink, 0.5f), 1.0f);
        keyBox(Rect(px - 7, py - 7, 14, 14), false, dragging, &theme.neon, 3.0f);
        const std::string s = formatParam(kRate, value(kRate)) + "   " + formatParam(kBits, value(kBits));
        textAt(r.x + 10, r.b() - 10, s, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        char t[32]; std::snprintf(t, sizeof t, "JITTER %.0f%%", value(kJitter));
        textAt(r.r() - 10, r.b() - 10, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawSpectrumScreen()
    {
        const Rect& r = specR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SPECTRUM");
        textAt(r.r() - 10, r.y + 14, "IN / OUT", kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        save();
        scissor(p.x, p.y, p.w, p.h);
        drawFreqGrid(p, kLo, kHi);
        const Rect sp(p.x, p.y, p.w, p.h - 16);
        drawSpectrum(pre, sp, -6.0f, -100.0f, withAlpha2(theme.ink, theme.dark ? 0.07f : 0.05f), withAlpha2(theme.ink, 0.28f), kLo, kHi, 1.0f);
        drawSpectrum(post, sp, -6.0f, -100.0f, withAlpha2(theme.neon, theme.dark ? 0.24f : 0.36f), withAlpha2(pump::mixColor(theme.neon, theme.ink, 0.55f), 0.9f), kLo, kHi, 1.2f);
        restore();
        // the converter's Nyquist: above it, what the output has is folded back (aliasing) or filtered away
        if (rateActive(value(kRate)))
        {
            const double nyq = 0.5 * std::min<double>(value(kRate), sampleRate());
            if (nyq > kLo && nyq < kHi)
            {
                const float x = freqToX(p, nyq, kLo, kHi);
                for (float y = p.y; y < p.b() - 16; y += 6) line(x, y, x, y + 3, theme.second, 1.5f);
                textAt(x + 4, p.y + 10, "NYQUIST", kLabel, 9.0f, theme.second, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
            }
        }
    }

    void drawWave()
    {
        const Rect& r = waveR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "WAVE");
        const float ms = waveMs();
        char z[16]; std::snprintf(z, sizeof z, ms < 10 ? "%.1f MS" : "%.0f MS", ms);
        textAt(r.r() - 10, r.y + 14, z, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p(r.x + 8, r.y + 28, r.w - 16, r.h - 40);
        const float cy = p.cy();
        line(p.x, cy, p.r(), cy, withAlpha2(theme.ink, 0.25f), 1.0f);
        if (!plug) return;
        const int lat = plug->latencyNow();
        const int win = pump::clampv((int)(0.001 * ms * plug->getSampleRate()), 16, kScopeN / 4);
        int start = kScopeN - win - lat - 1;
        for (int i = start; i > start - 4 * win && i > 1; --i)
            if (scopeIn[i - 1] < 0.0f && scopeIn[i] >= 0.0f) { start = i; break; }
        float mx = 1e-4f;
        for (int i = 0; i < win; ++i)
        {
            mx = std::max(mx, std::fabs(scopeIn[pump::clampv(start + i, 0, kScopeN - 1)]));
            mx = std::max(mx, std::fabs(scopeOut[pump::clampv(start + lat + i, 0, kScopeN - 1)]));
        }
        const float want = 1.0f / std::max(mx, 0.003f);
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        const float sy = p.h * 0.45f * scale;
        save();
        scissor(p.x, p.y, p.w, p.h);
        auto trace = [&](const float* buf, int off, const Color& c, float w, bool steps) {
            beginPath();
            for (int i = 0; i < win; ++i)
            {
                const int k = pump::clampv(start + off + i, 0, kScopeN - 1);
                const float px = p.x + p.w * i / (win - 1), py = cy - pump::clampv(buf[k] * sy, -p.h, p.h);
                if (i == 0) moveTo(px, py);
                else if (steps) { const float pp = cy - pump::clampv(buf[k - 1] * sy, -p.h, p.h); lineTo(px, pp); lineTo(px, py); }
                else lineTo(px, py);
            }
            strokeColor(c); strokeWidth(w); lineJoin(ROUND); stroke();
        };
        trace(scopeIn, 0, withAlpha2(theme.ink, 0.30f), 1.2f, false);
        trace(scopeOut, lat, pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f), 1.8f, true);
        restore();
        textAt(p.x + 2, p.b() - 4, "IN", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        textAt(p.x + 24, p.b() - 4, "OUT", kLabel, 9.0f, pump::mixColor(theme.neon, theme.ink, 0.5f), ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }
    float waveMs() const { return 2.0f * (float)(1 << waveZoom); }

    // the media's state: transport speed (wow and flutter), dropouts and crackle
    void drawTransport()
    {
        const Rect& t = transportR;
        caption(t.x, t.y - 8, "SPEED");
        const Rect bar(t.x, t.y, t.w, 10);
        fillRect(bar, theme.panel2);
        const float dev = pump::clampv(speedShow / 2.0f, -1.0f, 1.0f);   // +-2 % full scale
        const float cx = bar.cx();
        fillRect(Rect(std::min(cx, cx + dev * bar.w * 0.5f), bar.y, std::fabs(dev) * bar.w * 0.5f, bar.h), theme.neon);
        line(cx, bar.y - 2, cx, bar.b() + 2, theme.ink, 1.0f);
        strokeRect(bar, theme.ink, 1.0f);
        char b[16]; std::snprintf(b, sizeof b, "%+.2f%%", speedShow);
        textAt(bar.r(), bar.b() + 10, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        auto lamp = [&](float y, const char* label, float on) {
            const Rect l(t.x, y, 12, 12);
            fillRect(l, on > 0.02f ? pump::mixColor(theme.panel2, theme.second, std::min(1.0f, on)) : theme.panel2);
            strokeRect(l, theme.ink, 1.0f);
            textAt(t.x + 18, y + 6, label, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        };
        lamp(t.y + 40, "DROPOUT", pump::clampv((1.0f - dropShow) * 3.0f, 0.0f, 1.0f));
        lamp(t.y + 62, "CRACKLE", clickFlash);
    }

    // ---- the converter point is a control: left/right = rate, up/down = bits ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (convR.contains(x, y) && button == kMouseButtonLeft)
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kRate, spec(kRate).def); setParamGesture(kBits, spec(kBits).def); endGroup(); return false; }
            dragging = true;
            beginEdit(kRate); beginEdit(kBits);
            fine = (mod & kModifierShift) != 0;
            dragX = x; dragY = y; dragRate = norm(kRate); dragBits = norm(kBits);
            if (!fine) moveTo2(x, y);
            return true;
        }
        return false;
    }
    void moveTo2(float x, float y)
    {
        const Rect p = convPlot();
        setParamNorm(kRate, (x - p.x) / p.w);
        setParamNorm(kBits, (p.b() - y) / p.h);
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (!dragging) return;
        if (mod & kModifierShift)
        {
            const Rect p = convPlot();
            setParamNorm(kRate, dragRate + 0.15f * (x - dragX) / p.w);
            setParamNorm(kBits, dragBits + 0.15f * (dragY - y) / p.h);
        }
        else moveTo2(x, y);
        setStatus(paramStatus(kRate) + "    " + paramStatus(kBits));
    }
    void bodyMouseUp(float, float) override
    {
        if (!dragging) return;
        dragging = false;
        endEdit(kBits); endEdit(kRate);
    }
    void bodyHover(float x, float y) override
    {
        if (convR.contains(x, y)) setStatus("Drag the point: left/right = sample rate, up/down = bits (Shift = fine, double-click = reset)");
        else if (specR.contains(x, y)) setStatus("Spectrum: output over input. Right of the Nyquist line, new content is aliasing");
        else if (waveR.contains(x, y)) setStatus("Wave: input (faint) and output up close: the held, quantised steps (wheel = zoom)");
        else if (transportR.contains(x, y)) setStatus("Transport speed (wow and flutter) and the dropout / crackle lamps");
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (!waveR.contains(x, y)) return false;
        waveZoom = pump::clampv(waveZoom + (dy > 0 ? -1 : 1), 0, 4);
        saveUi();
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '7') { pickVoicing((int)(key - '1'), (mod & kModifierShift) != 0); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the converter point", "left/right = rate, up/down = bits (double-click resets both)" },
            { "Voicing keys", "click = voicing with its rate and bits; Shift-click = character only" },
            { "1 - 7", "voicing: clean, grit 12, hi-fi 12, dark 12, 8 bit, toy, phone (Shift = character only)" },
            { "Wheel on the wave", "zoom" },
        };
    }
    std::string footerInfo() override
    {
        std::string s = kModeNames[pump::clampv((int)value(kMode), 0, kNumModes - 1)];
        s += "   " + formatParam(kBits, value(kBits)) + "   " + (rateActive(value(kRate)) ? formatParam(kRate, value(kRate)) : std::string("FULL RATE"));
        s += value(kOrder) >= 0.5f ? "   MEDIA BEFORE" : "   MEDIA AFTER";
        char b[32]; std::snprintf(b, sizeof b, "   LATENCY %d", plug ? plug->latencyNow() : 0);
        s += b;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }
    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "zoom=%d;", waveZoom); saveUiState(buf); }
    void loadUiState(const std::string& s) override { waveZoom = pump::clampv(std::atoi(uiStateGet(s, "zoom", "1").c_str()), 0, 4); }

private:
    static constexpr int kScopeN = 8192;
    static constexpr double kLo = 20.0, kHi = 20000.0;
    PluginCrush* plug = nullptr;
    SpectrumView pre, post;
    Rect convR, specR, waveR, meterL, meterR, samplerPanel, mediaPanel, outPanel, transportR;
    OutMeter outMeter;
    float scopeIn[kScopeN] = {}, scopeOut[kScopeN] = {};
    float scale = 1.0f, speedShow = 0.0f, dropShow = 1.0f, clickFlash = 0.0f;
    int waveZoom = 1;
    bool dragging = false, fine = false;
    float dragX = 0, dragY = 0, dragRate = 0, dragBits = 0;

    VoicingKeys* modeSeg;
    Segmented* orderSeg;
    Dial *driveDial, *bitsDial, *rateDial, *jitterDial, *wowDial, *flDial, *ageDial, *hissDial, *crackleDial, *dropDial;
    Dial *lowDial, *highDial, *mixDial, *outDial;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UICrush)
};

UI* createUI() { return new UICrush(); }

END_NAMESPACE_DISTRHO
