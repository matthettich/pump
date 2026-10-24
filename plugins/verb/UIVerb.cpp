// pump VERB: interface. The mode chooser runs across the top in its five families (studio, late-70s digital, 80s
// digital, 90s rack, special). Two screens: DECAY, the reverb time across frequency (the bass multiplier below the
// crossover, the damping above the damping frequency; drag its low, middle and high parts), and ENVELOPE, the
// schematic impulse response of the current settings (pre-delay gap, early reflections, the late build-up and decay,
// or the gate / reverse shapes) with the live output caught after every hit drawn over it. Below: space (pre-delay
// with tempo sync, size, decay, diffusion, early / late), damping, character (modulation, width and the mode's own
// controls: age, gate / reverse time, shimmer pitch and amount) and output (cuts, duck, freeze, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginVerb.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::verb;

class UIVerb : public PumpUI {
public:
    UIVerb() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::verb(), "VERB", 1000, 620, kBypass)
    {
        // the mode chooser: one key row per family, sharing the one mode parameter
        struct Fam { const char* caption; std::vector<std::string> keys; int first; };
        const Fam fams[kNumFamilies] = {
            { "STUDIO", { "HALL", "ROOM", "CHAMBER", "PLATE" }, kHall },
            { "LATE-70S DIGITAL", { "HALL", "PLATE" }, kVintageHall },
            { "80S DIGITAL", { "HALL", "PLATE" }, kClassicHall },
            { "90S RACK", { "RACK" }, kRack },
            { "SPECIAL", { "GATED", "REVERSE", "SHIMMER" }, kGated },
        };
        static const char* const famTips[kNumFamilies] = {
            "Studio: smooth, dense, natural tails with realistic early reflections and almost no audible modulation   ([ ] step modes)",
            "Late-70s digital: about 10 kHz of bandwidth, a low internal rate, gain-ranging converters, random chorus; grainy, lush and dark",
            "80s digital: big, wide and lush, slow random wander and spin in the tank, a strong bass multiplier",
            "90s rack: 16-bit, about 15 kHz, quick build-up, a little metallic and ringy, less dense",
            "Special: Gated (dense burst cut at TIME), Reverse (builds up to TIME, then cuts), Shimmer (pitch-shifted feedback)",
        };
        for (int f = 0; f < kNumFamilies; ++f)
        {
            famCaption[f] = fams[f].caption;
            Segmented* s = addSegmented(-1, fams[f].keys);
            const int first = fams[f].first, count = (int)fams[f].keys.size();
            s->getFn = [this, first, count]() { const int m = mode(); return m >= first && m < first + count ? m - first : -1; };
            s->setFn = [this, first](int i) { setParamGesture(kMode, (float)(first + i)); };
            s->tip = famTips[f];
            famSeg[f] = s;
        }

        preDial = addDial(kPredelay, "PRE-DELAY");
        preDial->tip = "Pre-delay: the gap before the reverb starts, 0..500 ms (or a note value with SYNC; drag its edge on the envelope)";
        sizeDial = addDial(kSize, "SIZE");
        sizeDial->tip = "Size: scales every delay in the tank and the room of the early reflections";
        decayDial = addDial(kDecay, "DECAY", Dial::Large);
        decayDial->tip = "Decay: the reverb time (RT60) in the mids, 0.1..30 s; Gated / Reverse: the slope of the burst";
        diffDial = addDial(kDiffusion, "DIFFUSION", Dial::Small);
        diffDial->tip = "Diffusion: how fast echoes smear into a dense wash (low = grainy, discrete repeats)";
        balDial = addDial(kBalance, "EARLY/LATE", Dial::Small, true);
        balDial->tip = "Early / late balance: early reflections only (left) .. both .. late tail only (right)";
        syncBtn = addToggle(kSync, "SYNC");
        syncBtn->small = true;
        syncBtn->tip = "Sync the pre-delay to the host tempo (the dial then picks a note value; 120 BPM without a host tempo)";
        preDial->tabGroup = sizeDial->tabGroup = decayDial->tabGroup = diffDial->tabGroup = balDial->tabGroup = 1;

        bassDial = addDial(kBassMult, "BASS MULT");
        bassDial->tip = "Bass multiplier: the decay below the crossover, 0.5..2x the mid decay (or drag the curve's left part)";
        xoverDial = addDial(kCrossover, "CROSSOVER");
        xoverDial->tip = "Crossover: where the bass multiplier takes over";
        hdDial = addDial(kHighDamp, "HIGH DAMP");
        hdDial->tip = "High damping: the decay above the damping frequency, 0.1..1x the mid decay (or drag the curve's right part)";
        dfDial = addDial(kDampFreq, "DAMP FREQ");
        dfDial->tip = "Damping frequency: where the highs start to die away faster";
        bassDial->tabGroup = xoverDial->tabGroup = hdDial->tabGroup = dfDial->tabGroup = 2;

        depthDial = addDial(kModDepth, "MOD DEPTH", Dial::Small);
        depthDial->tip = "Modulation depth: how far the tank's delays wander (studio: just enough to break up ringing; vintage: chorus)";
        rateDial = addDial(kModRate, "MOD RATE", Dial::Small);
        rateDial->tip = "Modulation rate";
        widthDial = addDial(kWidth, "WIDTH", Dial::Small);
        widthDial->tip = "Stereo width of the reverb: 0 = mono, 100% = as built, 150% = wider";
        ageDial = addDial(kAge, "AGE");
        ageDial->tip = "Age: fewer bits, a lower internal rate and less bandwidth (late-70s, 80s and rack modes)";
        timeDial = addDial(kEnvTime, "TIME");
        timeDial->tip = "Time: where the gate cuts (Gated) or the build-up peaks and cuts (Reverse), 50..1500 ms";
        pitchDial = addDial(kPitch, "PITCH", Dial::Medium, true);
        pitchDial->tip = "Shimmer pitch: each pass through the tank shifts by this many semitones (+12 = an octave up)";
        amountDial = addDial(kShimmer, "AMOUNT");
        amountDial->tip = "Shimmer amount: how much of the tank is pitch-shifted on each pass";
        depthDial->tabGroup = rateDial->tabGroup = widthDial->tabGroup = ageDial->tabGroup = timeDial->tabGroup = 3;
        pitchDial->tabGroup = amountDial->tabGroup = 3;

        lowDial = addDial(kLowCut, "LOW CUT", Dial::Small);
        lowDial->tip = "Low cut on the reverb (keeps the low end of the mix clear)";
        highDial = addDial(kHighCut, "HIGH CUT", Dial::Small);
        highDial->tip = "High cut on the reverb";
        duckDial = addDial(kDuck, "DUCK", Dial::Small);
        duckDial->tip = "Duck: the reverb dips (up to 30 dB) while the dry signal plays and blooms in the gaps";
        freezeBtn = addToggle(kFreeze, "FREEZE");
        freezeBtn->tip = "Freeze: holds the tail forever and stops new input reaching it (F)";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: dry vs reverb (equal power; 100% = reverb only, for sends)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        lowDial->tabGroup = highDial->tabGroup = duckDial->tabGroup = mixDial->tabGroup = outDial->tabGroup = 4;

        if (auto* p = static_cast<PluginVerb*>(pluginPtr())) plug = p;
        updateControls();
    }

protected:
    static constexpr int kNumFamilies = 5;
    int mode() const { return pump::clampv((int)value(kMode), 0, kNumModes - 1); }
    double sr() const { const double s = sampleRate(); return s > 1000.0 ? s : 48000.0; }
    double bpm() const { return plug ? (double)plug->bpmNow.load(std::memory_order_relaxed) : 120.0; }
    double predelayMs() const { return predelayFor(value(kSync) >= 0.5f, (int)value(kNote), value(kPredelay), bpm()); }

    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kHighCut: if (!highCutActive(v)) return "OFF"; break;
        case kDecay: std::snprintf(b, sizeof b, v < 10.0f ? "%.2f s" : "%.1f s", v); return b;
        case kBassMult: case kHighDamp: std::snprintf(b, sizeof b, "%.2fx", v); return b;
        case kModRate: std::snprintf(b, sizeof b, v < 1.0f ? "%.2f Hz" : "%.1f Hz", v); return b;
        case kPitch: if (std::fabs(v) < 0.5f) return "0 st"; std::snprintf(b, sizeof b, "%+.0f st", v); return b;
        case kBalance:
        {
            if (std::fabs(v) < 0.5f) return "BOTH";
            double e, l;
            e = v > 0.0f ? 100.0 - v : 100.0;
            l = v < 0.0f ? 100.0 + v : 100.0;
            std::snprintf(b, sizeof b, "E%.0f L%.0f", e, l); return b;
        }
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    // which controls the mode uses (and the pre-delay dial's binding: a note value when synced)
    void updateControls()
    {
        const int m = mode();
        const bool burst = isBurst(m), shim = m == kShimmerMode;
        preDial->param = value(kSync) >= 0.5f ? kNote : kPredelay;
        ageDial->visible = !burst && !shim;
        ageDial->enabled = hasAge(m);
        timeDial->visible = burst;
        pitchDial->visible = amountDial->visible = shim;
        depthDial->enabled = rateDial->enabled = !burst;
        bassDial->enabled = xoverDial->enabled = hdDial->enabled = dfDial->enabled = !burst;
        freezeBtn->enabled = !burst;
    }
    void onParamChanged(int, float) override { updateControls(); }
    void onPresetApplied() override { updateControls(); }

    void layout() override
    {
        // mode strip
        float x = 14.0f;
        const float keyW = 76.0f, gap = 15.0f;
        for (int f = 0; f < kNumFamilies; ++f)
        {
            const float w = keyW * (float)famSeg[f]->options.size();
            famSeg[f]->r = Rect(x, 74, w, 28);
            x += w + gap;
        }
        decayR = Rect(14, 118, 452, 232);
        envR = Rect(480, 118, 466, 232);
        meterL = Rect(962, 118, 9, 214);
        meterR = Rect(973, 118, 9, 214);

        const float y0 = 366;
        spacePanel = Rect(14, y0, 280, 222);
        preDial->r = Rect(22, y0 + 26, 72, Dial::heightFor(Dial::Medium));
        sizeDial->r = Rect(100, y0 + 26, 72, Dial::heightFor(Dial::Medium));
        decayDial->r = Rect(184, y0 + 22, 96, Dial::heightFor(Dial::Large));
        diffDial->r = Rect(26, y0 + 130, 64, Dial::heightFor(Dial::Small));
        balDial->r = Rect(104, y0 + 130, 64, Dial::heightFor(Dial::Small));
        syncBtn->r = Rect(186, y0 + 152, 92, 24);

        dampPanel = Rect(308, y0, 190, 222);
        bassDial->r = Rect(320, y0 + 26, 72, Dial::heightFor(Dial::Medium));
        xoverDial->r = Rect(412, y0 + 26, 72, Dial::heightFor(Dial::Medium));
        hdDial->r = Rect(320, y0 + 122, 72, Dial::heightFor(Dial::Medium));
        dfDial->r = Rect(412, y0 + 122, 72, Dial::heightFor(Dial::Medium));

        charPanel = Rect(512, y0, 220, 222);
        depthDial->r = Rect(520, y0 + 26, 64, Dial::heightFor(Dial::Small));
        rateDial->r = Rect(590, y0 + 26, 64, Dial::heightFor(Dial::Small));
        widthDial->r = Rect(660, y0 + 26, 64, Dial::heightFor(Dial::Small));
        const Rect slotA(524, y0 + 122, 72, Dial::heightFor(Dial::Medium));
        ageDial->r = slotA;
        timeDial->r = slotA;
        pitchDial->r = slotA;
        amountDial->r = Rect(606, y0 + 122, 72, Dial::heightFor(Dial::Medium));
        readout = Rect(612, y0 + 132, 110, 64);

        outPanel = Rect(746, y0, 240, 222);
        lowDial->r = Rect(756, y0 + 26, 64, Dial::heightFor(Dial::Small));
        highDial->r = Rect(830, y0 + 26, 64, Dial::heightFor(Dial::Small));
        duckDial->r = Rect(904, y0 + 26, 64, Dial::heightFor(Dial::Small));
        freezeBtn->r = Rect(758, y0 + 152, 70, 28);
        mixDial->r = Rect(836, y0 + 122, 68, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(910, y0 + 122, 68, Dial::heightFor(Dial::Medium));
        updateControls();
    }

    void tick(float dt) override
    {
        updateControls();
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            lvl[c] = std::max(pk, lvl[c] - 26.0f * dt);
            if (pk >= hold[c]) { hold[c] = pk; holdT[c] = 1.5f; }
            else if ((holdT[c] -= dt) <= 0.0f) hold[c] = std::max(pk, hold[c] - 18.0f * dt);
        }
        // the live envelope: a hit on the input (a jump of 9 dB over its recent level) restarts the capture of the
        // wet level, so each hit's tail is drawn over the schematic from the dry hit on
        const uint32_t end = std::min(plug->inLevel.smoothPosition(), plug->wetLevel.position());
        const int cap = plug->inLevel.capacity();
        if (end - livePos > (uint32_t)cap) livePos = end - (uint32_t)cap;
        const float hop = (float)(PluginVerb::kLevelHop / sr());
        for (; livePos != end; ++livePos)
        {
            const float inDb = pump::gainToDbF(plug->inLevel.at(livePos));
            const float wetDb = pump::gainToDbF(plug->wetLevel.at(livePos));
            sinceHit += hop;
            if (inDb > -50.0f && inDb > inAvg + 9.0f && sinceHit > 0.09f) { capN = 0; sinceHit = 0.0f; }
            inAvg += (std::max(inDb, -90.0f) - inAvg) * 0.06f;
            if (capN < kCap) capDb[capN++] = wetDb;
        }
        liveAge = sinceHit;
    }

    void drawBody() override
    {
        for (int f = 0; f < kNumFamilies; ++f)
        {
            const bool lit = famSeg[f]->getFn() >= 0;
            caption(famSeg[f]->r.x, 62, famCaption[f], lit ? &theme.ink : nullptr);
        }
        drawDecay();
        drawEnvelope();
        drawLevelMeter(meterL, lvl[0], hold[0], -48.0f, 6.0f);
        drawLevelMeter(meterR, lvl[1], hold[1], -48.0f, 6.0f);
        textAt(meterL.r() + 1, decayR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(spacePanel, theme.dim, 1.0f);
        strokeRect(dampPanel, theme.dim, 1.0f);
        strokeRect(charPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(spacePanel.x + 12, spacePanel.y + 16, "SPACE");
        caption(dampPanel.x + 12, dampPanel.y + 16, "DAMPING");
        caption(charPanel.x + 12, charPanel.y + 16, "CHARACTER");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        // the tempo under the sync key
        char t[48];
        if (value(kSync) >= 0.5f) std::snprintf(t, sizeof t, "%.0f ms @ %.0f BPM", predelayMs(), bpm());
        else std::snprintf(t, sizeof t, "%.0f BPM", bpm());
        caption(syncBtn->r.x, syncBtn->r.y - 12, "TEMPO");
        textAt(syncBtn->r.cx(), syncBtn->r.b() + 13, t, kMono, 10.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        drawReadout();
    }

    // what the mode's colour does at this AGE (next to the AGE dial), or the shimmer / gate / reverse summary
    void drawReadout()
    {
        const int m = mode();
        const Rect& r = readout;
        if (m == kShimmerMode) return;
        std::vector<std::string> lines;
        char b[48];
        if (isBurst(m))
        {
            std::snprintf(b, sizeof b, m == kGated ? "GATE %.0f ms" : "PEAK %.0f ms", value(kEnvTime)); lines.push_back(b);
            lines.push_back(m == kGated ? "DENSE, THEN CUT" : "BUILDS, THEN CUTS");
        }
        else if (!hasAge(m)) { lines.push_back("CLEAN"); lines.push_back("FLOAT, FULL BAND"); }
        else
        {
            const Voice v = voice(m);
            const ColourParams c = colourFor(v.colour, value(kAge) * 0.01);
            std::snprintf(b, sizeof b, "%.0f-BIT%s", c.convBits, c.ranges > 0 ? " FLOAT" : ""); lines.push_back(b);
            std::snprintf(b, sizeof b, "%.1f kHz RATE", c.rate / 1000.0); lines.push_back(b);
            std::snprintf(b, sizeof b, "%.1f kHz BW", c.bw / 1000.0); lines.push_back(b);
        }
        float y = r.y + 10.0f;
        for (const auto& l : lines) { textAt(r.x, y, l, kMono, 10.0f, ageDial->enabled || isBurst(m) ? theme.ink2 : theme.dim, ALIGN_LEFT | ALIGN_MIDDLE); y += 16.0f; }
    }

    // ---- DECAY: RT60 across frequency ----
    Rect decayPlot() const { return Rect(decayR.x + 44, decayR.y + 30, decayR.w - 58, decayR.h - 56); }
    static constexpr double kRtLo = 0.1, kRtHi = 50.0;
    float rtY(const Rect& p, double t) const
    {
        const double n = std::log(pump::clampv(t, kRtLo, kRtHi) / kRtLo) / std::log(kRtHi / kRtLo);
        return p.b() - (float)n * p.h;
    }
    double rtAt(double f) const
    {
        if (isBurst(mode())) return value(kDecay);
        return rt60At(f, value(kDecay), value(kBassMult), value(kCrossover), value(kHighDamp), value(kDampFreq), sr());
    }
    // the three drag handles: low (crossover, bass decay), mid (between, mid decay), high (damp freq, high decay)
    void handlePos(int h, float& x, float& y) const
    {
        const Rect p = decayPlot();
        const double xo = value(kCrossover), df = value(kDampFreq), d = value(kDecay);
        if (h == 0) { x = freqToX(p, xo); y = rtY(p, d * value(kBassMult)); }
        else if (h == 2) { x = freqToX(p, df); y = rtY(p, d * value(kHighDamp)); }
        else { const double fm = std::sqrt(xo * df); x = freqToX(p, fm); y = rtY(p, rtAt(fm)); }
    }
    void drawDecay()
    {
        const Rect& r = decayR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "DECAY");
        const Rect p = decayPlot();
        const int m = mode();
        const bool burst = isBurst(m), frozen = value(kFreeze) >= 0.5f && !burst;
        char t[96];
        if (burst) std::snprintf(t, sizeof t, "SLOPE %.2f s (NO FREQUENCY DEPENDENCE)", value(kDecay));
        else std::snprintf(t, sizeof t, "LOW %.2f s   MID %.2f s   HIGH %.2f s", value(kDecay) * value(kBassMult), value(kDecay), value(kDecay) * value(kHighDamp));
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);

        drawFreqGrid(p, 20.0, 20000.0, false);
        static const struct { double f; const char* t; } fl[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
            { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (const auto& l : fl) textAt(freqToX(p, l.f), p.b() + 10, l.t, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        static const struct { double s; const char* t; } yl[] = { { 0.1, "0.1s" }, { 0.3, "0.3s" }, { 1, "1s" }, { 3, "3s" }, { 10, "10s" }, { 30, "30s" } };
        for (const auto& l : yl)
        {
            const float y = rtY(p, l.s);
            line(p.x, y, p.r(), y, l.s == 1 || l.s == 10 ? withAlpha2(theme.ink, theme.dark ? 0.22f : 0.20f) : theme.grid, 1.0f);
            textAt(p.x - 5, y, l.t, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        // the band limit of the vintage voices: no reverb above it
        const Voice v = voice(m);
        if (v.colour != kColourClean)
        {
            const double bw = colourFor(v.colour, value(kAge) * 0.01).bw;
            const float xb = freqToX(p, bw);
            fillRect(Rect(xb, p.y, p.r() - xb, p.h), withAlpha2(theme.ink, theme.dark ? 0.10f : 0.07f));
            line(xb, p.y, xb, p.b(), theme.dim, 1.0f);
            std::snprintf(t, sizeof t, "BW %.1fk", bw / 1000.0);
            textAt(xb - 4, p.y + 9, t, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        // the curve (filled to the bottom); frozen = endless
        constexpr int M = 160;
        float ys[M + 1];
        for (int i = 0; i <= M; ++i) ys[i] = frozen ? p.y + 1.0f : rtY(p, rtAt(20.0 * std::pow(1000.0, (double)i / M)));
        save();
        scissor(p.x, p.y, p.w, p.h);
        beginPath();
        moveTo(p.x, p.b());
        for (int i = 0; i <= M; ++i) lineTo(p.x + p.w * i / M, ys[i]);
        lineTo(p.r(), p.b());
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.22f : 0.38f));
        fill();
        beginPath();
        for (int i = 0; i <= M; ++i) { const float x = p.x + p.w * i / M; if (i == 0) moveTo(x, ys[i]); else lineTo(x, ys[i]); }
        strokeColor(theme.ink); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        restore();
        if (frozen)
        {
            textAt(p.cx(), p.y + 16, "FROZEN: ENDLESS DECAY", kLabel, 11.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.2f);
            return;
        }
        // handles
        for (int h = 0; h < 3; ++h)
        {
            if (burst && h != 1) continue;
            float x, y;
            handlePos(h, x, y);
            if (h != 1) line(x, y + 6, x, p.b(), withAlpha2(theme.ink, 0.35f), 1.0f);
            const bool hot = h == dragHandle || (dragHandle < 0 && h == hoverHandle);
            const Rect hr(x - 5.5f, y - 5.5f, 11, 11);
            fillRect(hr, hot ? theme.ink : theme.neon);
            strokeRect(hr, theme.ink, 1.5f);
        }
    }
    // the handle under (x, y), else the part of the curve the x falls in
    int pickHandle(float x, float y) const
    {
        if (isBurst(mode())) return 1;
        int best = -1; float bd = 14.0f * 14.0f;
        for (int h = 0; h < 3; ++h)
        {
            float hx, hy; handlePos(h, hx, hy);
            const float d = (hx - x) * (hx - x) + (hy - y) * (hy - y);
            if (d < bd) { bd = d; best = h; }
        }
        if (best >= 0) return best;
        float x0, x2, yy;
        handlePos(0, x0, yy); handlePos(2, x2, yy);
        return x < x0 ? 0 : (x > x2 ? 2 : 1);
    }

    // ---- ENVELOPE: the schematic impulse response ----
    Rect envPlot() const { return Rect(envR.x + 36, envR.y + 30, envR.w - 50, envR.h - 68); }
    static constexpr double kT0 = 0.04;   // the time axis is warped: x ~ log(1 + t / kT0)
    static constexpr float kDbRange = 66.0f;
    static constexpr double kSchematicPeak = -8.0;   // where the late part peaks on the envelope (dB below the hit)
    double envWindow() const
    {
        const int m = mode();
        const double pre = predelayMs() * 0.001;
        if (isBurst(m)) return pre + value(kEnvTime) * 0.001 * 1.25 + 0.04;
        if (value(kFreeze) >= 0.5f) return pre + 3.0;
        return pre + std::max(0.3, (double)value(kDecay)) * 1.1 + 0.03;
    }
    float tX(const Rect& p, double t, double W) const
    {
        return p.x + p.w * (float)(std::log1p(std::max(0.0, t) / kT0) / std::log1p(W / kT0));
    }
    double xT(const Rect& p, float x, double W) const
    {
        return kT0 * std::expm1((double)((x - p.x) / p.w) * std::log1p(W / kT0));
    }
    float dbY(const Rect& p, double db) const { return p.y + (float)(pump::clampv(-db, 0.0, (double)kDbRange) / kDbRange) * p.h; }
    void drawEnvelope()
    {
        const Rect& r = envR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "ENVELOPE");
        const int m = mode();
        textAt(r.r() - 10, r.y + 14, kModeNames[m], kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const Rect p = envPlot();
        const double W = dragging == 2 ? dragW : envWindow();
        const double pre = predelayMs() * 0.001;
        // grid: dB and (warped) time
        for (int d = 0; d <= 60; d += 12)
        {
            const float y = dbY(p, -d);
            line(p.x, y, p.r(), y, theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%d", -d);
            textAt(p.x - 5, y, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        static const double ticks[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 30 };
        float lastX = -100.0f;
        for (double tk : ticks)
        {
            if (tk > W) break;
            const float x = tX(p, tk, W);
            line(x, p.y, x, p.b(), tk == 0.1 || tk == 1 || tk == 10 ? withAlpha2(theme.ink, theme.dark ? 0.22f : 0.20f) : theme.grid, 1.0f);
            if (x - lastX < 30.0f) continue;
            char b[12];
            if (tk < 1.0) std::snprintf(b, sizeof b, "%.0fms", tk * 1000.0); else std::snprintf(b, sizeof b, "%.0fs", tk);
            textAt(x, p.b() + 10, b, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            lastX = x;
        }
        save();
        scissor(p.x, p.y, p.w + 1, p.h);
        // pre-delay gap
        const float xp = tX(p, pre, W);
        fillRect(Rect(p.x, p.y, xp - p.x, p.h), withAlpha2(theme.second, theme.dark ? 0.14f : 0.12f));
        // the levels of the parts
        double eg, lg;
        {
            const double b = pump::clampv((double)value(kBalance), -100.0, 100.0) * 0.01;
            eg = b > 0.0 ? 1.0 - b : 1.0;
            lg = b < 0.0 ? 1.0 + b : 1.0;
        }
        const Voice v = voice(m);
        const double lateDb = pump::gainToDb(std::max(1e-6, lg)) + kSchematicPeak, s = sizeScale(value(kSize));
        // late part
        constexpr int M = 220;
        float ly[M + 1];
        const bool frozen = value(kFreeze) >= 0.5f && !isBurst(m);
        const double T = value(kEnvTime) * 0.001, d = value(kDecay);
        double peakBurst = 1e-9;
        if (isBurst(m)) for (int i = 0; i <= 64; ++i) peakBurst = std::max(peakBurst, burstEnvelope(m, i / 64.0, T, d));
        const double tb = (v.topo == kTopoPlate ? 0.010 : 0.006 + 0.03 * s) * (1.15 - 0.5 * value(kDiffusion) * 0.01);
        for (int i = 0; i <= M; ++i)
        {
            const double t = xT(p, p.x + p.w * i / M, W) - pre;
            double db = -200.0;
            if (lg > 1e-4 && t >= 0.0)
            {
                if (isBurst(m)) db = t < T ? lateDb + pump::gainToDb(burstEnvelope(m, t / T, T, d) / peakBurst) : -200.0;
                else
                {
                    const double build = 10.0 * std::log10(std::max(1e-9, 1.0 - std::exp(-t / tb)));
                    db = lateDb + build - (frozen ? 0.0 : 60.0 * t / std::max(0.1, d));
                }
            }
            ly[i] = dbY(p, db);
        }
        beginPath();
        moveTo(p.x, p.b());
        for (int i = 0; i <= M; ++i) lineTo(p.x + p.w * i / M, ly[i]);
        lineTo(p.r(), p.b());
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.22f : 0.38f));
        fill();
        beginPath();
        bool pen = false;
        for (int i = 0; i <= M; ++i)
        {
            const float x = p.x + p.w * i / M;
            if (ly[i] >= p.b() - 0.5f)
            {
                if (pen) lineTo(x, p.b());   // a cut drops to the floor
                pen = false;
                continue;
            }
            if (!pen) { moveTo(x, p.b()); pen = true; }
            lineTo(x, ly[i]);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        // early reflections
        if (v.erLevel > 0.0 && eg > 1e-4)
        {
            if (erMode != m) { er.build(v.room[0], v.room[1], v.room[2], v.refl, erSeed(m)); erMode = m; }
            for (int side = 0; side < 2; ++side)
                for (int k = 0; k < er.n[side]; ++k)
                {
                    const double t = pre + er.tap[side][k].t * s;
                    const double db = pump::gainToDb(std::fabs(er.tap[side][k].g) * eg * v.erLevel) + 2.0;
                    const float x = tX(p, t, W) + (side ? 1.5f : -1.5f);
                    line(x, p.b(), x, dbY(p, db), side ? theme.ink2 : theme.ink, 1.2f);
                }
        }
        // the dry hit
        const float x0 = tX(p, 0.0, W);
        line(x0 + 1.0f, p.b(), x0 + 1.0f, dbY(p, 0.0), withAlpha2(theme.ink, 0.5f), 2.0f);
        // the live output after the last hit: its shape, the peak lined up with the schematic's (3-hop smoothing);
        // its true level is printed in the corner
        livePeakDb = -200.0f;
        if (capN > 2 && liveAge < 6.0f)
        {
            const double hop = PluginVerb::kLevelHop / sr();
            const float a = pump::clampv(1.6f - liveAge * 0.25f, 0.3f, 1.0f);
            float sm[kCap];
            int n = 0;
            for (int k = 1; k + 1 < capN && (k + 0.5) * hop <= W; ++k)
            {
                const float e = (pump::dbToGainF(2.0f * capDb[k - 1]) + pump::dbToGainF(2.0f * capDb[k]) + pump::dbToGainF(2.0f * capDb[k + 1])) / 3.0f;
                sm[n] = 0.5f * pump::gainToDbF(e + 1e-20f);
                livePeakDb = std::max(livePeakDb, sm[n]);
                ++n;
            }
            if (livePeakDb > -90.0f)
            {
                const float off = (float)kSchematicPeak - livePeakDb;
                beginPath();
                for (int k = 0; k < n; ++k)
                {
                    const float x = tX(p, (k + 1.5) * hop, W), y = dbY(p, std::max(-kDbRange, sm[k] + off));
                    if (k == 0) moveTo(x, y); else lineTo(x, y);
                }
                strokeColor(withAlpha2(theme.second, a)); strokeWidth(1.8f); lineJoin(ROUND); stroke();
            }
        }
        restore();
        // the pre-delay edge (a handle)
        line(xp, p.y, xp, p.b(), theme.second, dragging == 2 && dragPart == 0 ? 2.5f : 1.5f);
        char t[48];
        if (value(kSync) >= 0.5f) std::snprintf(t, sizeof t, "PRE %s", pump::kNoteNames[pump::clampv((int)value(kNote), 0, kMaxSyncNote)]);
        else std::snprintf(t, sizeof t, "PRE %.0f ms", predelayMs());
        textAt(xp + 5, p.y + 9, t, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        // legend
        float lx = r.x + 10;
        const float ly0 = r.b() - 10;
        auto legend = [&](const char* s, const Color& c) {
            fillRect(Rect(lx, ly0 - 3, 12, 6), c);
            textAt(lx + 16, ly0, s, kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
            lx += 30.0f + textWidth(s, kLabel, 9.0f, 1.0f);
        };
        legend("DRY HIT", withAlpha2(theme.ink, 0.5f));
        legend("LATE", withAlpha2(theme.neon, 0.8f));
        legend("EARLY L/R", theme.ink);
        legend("PRE", withAlpha2(theme.second, 0.5f));
        legend("LIVE", theme.second);
        if (livePeakDb > -90.0f)
        {
            char lv[32]; std::snprintf(lv, sizeof lv, "LIVE PEAK %.0f dBFS", livePeakDb);
            textAt(r.r() - 10, ly0, lv, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
    }

    // ---- interaction ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (decayR.contains(x, y) && value(kFreeze) < 0.5f)
        {
            const int h = pickHandle(x, y);
            const int pa = h == 0 ? kBassMult : (h == 2 ? kHighDamp : kDecay);
            const int pb = h == 0 ? kCrossover : (h == 2 ? kDampFreq : -1);
            if (dbl || cmdKey(mod))
            {
                beginGroup(); setParamGesture(pa, spec(pa).def); if (pb >= 0) setParamGesture(pb, spec(pb).def); endGroup();
                return false;
            }
            dragging = 1; dragHandle = h; dragX = x; dragY = y;
            dragA = value(pa); dragB = pb >= 0 ? value(pb) : 0.0f;
            beginEdit(pa); if (pb >= 0) beginEdit(pb);
            return true;
        }
        if (envR.contains(x, y))
        {
            const Rect p = envPlot();
            const double W = envWindow();
            const float xp = tX(p, predelayMs() * 0.001, W);
            const bool onPre = std::fabs(x - xp) < 8.0f;
            if (onPre && value(kSync) >= 0.5f) { setStatus("The pre-delay is synced to the tempo: pick the note on the dial, or turn SYNC off to drag it"); return false; }
            const int pa = onPre ? kPredelay : (isBurst(mode()) ? kEnvTime : kDecay);
            if (dbl || cmdKey(mod)) { setParamGesture(pa, spec(pa).def); return false; }
            dragging = 2; dragPart = onPre ? 0 : 1; dragX = x; dragY = y; dragW = W;
            dragA = value(pa);
            beginEdit(pa);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        if (dragging == 1)
        {
            const Rect p = decayPlot();
            const int pa = dragHandle == 0 ? kBassMult : (dragHandle == 2 ? kHighDamp : kDecay);
            const int pb = dragHandle == 0 ? kCrossover : (dragHandle == 2 ? kDampFreq : -1);
            // vertical: the decay of that part (the axis is logarithmic in seconds); horizontal: its frequency
            const double perPx = std::log(kRtHi / kRtLo) / p.h;
            setParam(pa, dragA * (float)std::exp((dragY - y) * perPx * f));
            if (pb >= 0) setParam(pb, dragB * (float)std::pow(1000.0, (x - dragX) / p.w * f));
            if (mod & kModifierShift) { dragA = value(pa); if (pb >= 0) dragB = value(pb); dragX = x; dragY = y; }
            setStatus(pb >= 0 ? paramStatus(pa) + "    " + paramStatus(pb) : paramStatus(pa));
        }
        else if (dragging == 2)
        {
            const Rect p = envPlot();
            if (dragPart == 0)
            {
                const double t = xT(p, std::max(p.x, x), dragW);
                setParam(kPredelay, (float)(t * 1000.0));
                setStatus(paramStatus(kPredelay));
            }
            else
            {
                const int pa = isBurst(mode()) ? kEnvTime : kDecay;
                setParam(pa, dragA * (float)std::pow(2.0, (x - dragX) / 110.0 * f));
                if (mod & kModifierShift) { dragA = value(pa); dragX = x; }
                setStatus(paramStatus(pa));
            }
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (dragging == 1)
        {
            const int pa = dragHandle == 0 ? kBassMult : (dragHandle == 2 ? kHighDamp : kDecay);
            const int pb = dragHandle == 0 ? kCrossover : (dragHandle == 2 ? kDampFreq : -1);
            if (pb >= 0) endEdit(pb);
            endEdit(pa);
        }
        else if (dragging == 2) endEdit(dragPart == 0 ? kPredelay : (isBurst(mode()) ? kEnvTime : kDecay));
        dragging = 0; dragHandle = -1;
    }
    void bodyHover(float x, float y) override
    {
        hoverHandle = -1;
        if (decayR.contains(x, y))
        {
            if (value(kFreeze) >= 0.5f) { setStatus("Frozen: the tail holds forever (FREEZE or F to release)"); return; }
            hoverHandle = pickHandle(x, y);
            static const char* const t[3] = {
                "Drag the low part: up/down = bass multiplier, left/right = crossover (Shift = fine, double-click = reset)",
                "Drag the middle: up/down = decay (Shift = fine, double-click = reset)",
                "Drag the high part: up/down = high damping, left/right = damping frequency (Shift = fine, double-click = reset)" };
            setStatus(isBurst(mode()) ? "Drag up/down: the burst's slope (decay)" : t[hoverHandle]);
        }
        else if (envR.contains(x, y))
        {
            const Rect p = envPlot();
            const float xp = tX(p, predelayMs() * 0.001, envWindow());
            if (std::fabs(x - xp) < 8.0f) setStatus(value(kSync) >= 0.5f ? "Pre-delay (synced to the tempo)" : "Drag the pre-delay edge left/right (double-click = reset)");
            else setStatus(isBurst(mode()) ? "Envelope: drag left/right = gate / reverse time. The live output after each hit is drawn over it"
                                           : "Envelope: drag left/right = decay. The live output after each hit is drawn over it (dBFS)");
        }
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key == '[' || key == ']') { setParamGesture(kMode, (float)((mode() + (key == ']' ? 1 : kNumModes - 1)) % kNumModes)); return true; }
        if ((key == 'f' || key == 'F') && !isBurst(mode())) { setParamGesture(kFreeze, value(kFreeze) >= 0.5f ? 0.0f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the decay curve", "low part = bass mult + crossover, middle = decay, high part = damping + frequency" },
            { "Drag the envelope", "pre-delay edge = pre-delay, elsewhere = decay (gate / reverse time)" },
            { "[  ]", "previous / next mode" },
            { "F", "freeze" },
        };
    }
    std::string footerInfo() override
    {
        char buf[160];
        const int m = mode();
        char pre[48];
        if (value(kSync) >= 0.5f) std::snprintf(pre, sizeof pre, "PRE %s = %.0f MS", pump::kNoteNames[pump::clampv((int)value(kNote), 0, kMaxSyncNote)], predelayMs());
        else std::snprintf(pre, sizeof pre, "PRE %.0f MS", predelayMs());
        if (isBurst(m)) std::snprintf(buf, sizeof buf, "%s   TIME %.0f MS   %s", kModeNames[m], value(kEnvTime), pre);
        else std::snprintf(buf, sizeof buf, "%s   RT60 %.2f S   %s%s", kModeNames[m], value(kDecay), pre, value(kFreeze) >= 0.5f ? "   FROZEN" : "");
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr int kCap = 1024;
    PluginVerb* plug = nullptr;
    Segmented* famSeg[kNumFamilies] = {};
    const char* famCaption[kNumFamilies] = {};
    Rect decayR, envR, meterL, meterR, spacePanel, dampPanel, charPanel, outPanel, readout;
    float lvl[2] = { -120.0f, -120.0f }, hold[2] = { -120.0f, -120.0f }, holdT[2] = { 0.0f, 0.0f };
    // live envelope capture
    uint32_t livePos = 0;
    float capDb[kCap] = {}, inAvg = -90.0f, sinceHit = 100.0f, liveAge = 100.0f, livePeakDb = -200.0f;
    int capN = 0;
    // early reflections of the shown mode
    ImagePattern er;
    int erMode = -1;
    // dragging: 1 = decay curve, 2 = envelope
    int dragging = 0, dragHandle = -1, hoverHandle = -1, dragPart = 0;
    float dragX = 0, dragY = 0, dragA = 0, dragB = 0;
    double dragW = 1.0;

    Dial *preDial, *sizeDial, *decayDial, *diffDial, *balDial, *bassDial, *xoverDial, *hdDial, *dfDial;
    Dial *depthDial, *rateDial, *widthDial, *ageDial, *timeDial, *pitchDial, *amountDial, *lowDial, *highDial, *duckDial, *mixDial, *outDial;
    Button *syncBtn, *freezeBtn;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIVerb)
};

UI* createUI() { return new UIVerb(); }

END_NAMESPACE_DISTRHO
