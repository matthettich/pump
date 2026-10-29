// pump BREAK: interface. BREAK: the break's wave cut into its sixteen slices (each in a colour of its own, numbered;
// click one to hear it), the slice playing lit and the play position on it. Beside it the break: the built-in ones
// or your own (LOAD), FIT, the file's length in bars, the CHOPS library. Then the STEPS: per step its slice (in the
// slice's colour: drag up / down, wheel, right-click for none), PITCH and BEND (bipolar bars: drag), LEVEL and DECAY
// (DECAY drawn as the step's envelope), REVERSE, and the step numbers (the playhead; click one to end the pattern
// there). At the bottom the eight momentary effects (hold a pad, or keys 1-8; the bar under each is its amount), and
// the sequencer and output.
#include "ui/PumpUI.hpp"
#include "PluginBreak.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::brk;

class UIBreak : public PumpUI {
public:
    UIBreak() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::brk(), "BREAK", 1000, 640, -1)
    {
        srcMenu = addMenu(kSource);
        srcMenu->arrows = true;
        srcMenu->tip = "The break: a built-in one, or File (LOAD a .wav of your own)";
        loadBtn = addButton("LOAD", [this] { FileBrowserOptions op; op.title = "Load a break (.wav, up to 30 s)"; openFileBrowser(op); });
        loadBtn->small = true; loadBtn->tip = "Load a .wav of your own (BREAK then shows File)";
        fitBtn = addToggle(kFit, "FIT");
        fitBtn->small = true; fitBtn->tip = "Fit: the break plays at the song's tempo (faster and higher, or slower and lower, as a sampler does)";
        barsSeg = addSegmented(kFileBars, { "1", "2", "4" });
        barsSeg->tip = "How many bars long your file is (for FIT)";
        chopMenu = add(new MenuButton());
        chopMenu->itemsFn = []() { std::vector<std::string> v; for (const auto& c : kChops) v.push_back(c.name); return v; };
        chopMenu->getFn = []() { return -1; };
        chopMenu->setFn = [this](int k) { if (k >= 0 && k < kNumChops) writeChop(parseChop(kChops[k].text), kChops[k].name); };
        chopMenu->textFn = []() { return std::string("CHOPS"); };
        chopMenu->tip = "A chop from the library: changes the steps only";
        clearBtn = addButton("STRAIGHT", [this] { writeChop(parseChop(kStraight), "Straight"); });
        clearBtn->small = true; clearBtn->tip = "Every step back to its own slice, no pitch, bend or decay";
        for (int k = 0; k < kFx; ++k)
        {
            SliderRow* r = addRow(kFxAmount1 + k, "");
            r->labelW = 0; r->valueW = 34; r->tip = std::string(kFxNames[k]) + " amount";
            fxAmt[k] = r;
        }
        modeSeg = addSegmented(kSeqMode, { "OFF", "SONG", "FREE" });
        modeSeg->tip = "Off: MIDI plays the slices · Song: the steps play with the host · Free: always, at the host tempo";
        playBtn = addButton("PLAY", [this] { if (plug) plug->preview.store(!plug->preview.load()); }, [this] { return plug && plug->preview.load(); });
        playBtn->small = true; playBtn->tip = "Play the steps now, at the host tempo (Space)";
        rateMenu = addMenu(kSeqRate, "RATE");
        rateMenu->arrows = true;
        const struct { int p; const char* l; bool bip; const char* tip; } rows[] = {
            { kSeqLength, "LENGTH", false, "Pattern length in steps (or click a step number)" },
            { kSwing, "SWING", false, "Swing: every second step starts later (33 % = a triplet shuffle)" },
            { kTranspose, "TRANSP", true, "Transpose every step, in semitones" },
            { kTone, "TONE", false, "Tone: a low-pass on the output (all the way up = open)" },
            { kDrive, "DRIVE", false, "Drive on the output" },
            { kVolume, "VOLUME", true, "Volume" },
        };
        for (const auto& m : rows) { SliderRow* r = addRow(m.p, m.l, m.bip); r->labelW = 50; r->valueW = 46; r->tip = m.tip; outRows.push_back(r); }
        if (auto* p = static_cast<PluginBreak*>(pluginPtr())) plug = p;
    }
    ~UIBreak() override { if (plug) plug->uiHeld.store(0u); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kTranspose || (i >= kStep1 && ((i - kStep1) % kPerStep == kPitch || (i - kStep1) % kPerStep == kBend)))
        { if (std::fabs(v) < 0.05f) return "0 st"; std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.05f ? "%+.0f st" : "%+.1f st", v); return b; }
        if (i >= kStep1 && (i - kStep1) % kPerStep == kSlice) { if (v < 0.5f) return "none"; std::snprintf(b, sizeof b, "slice %.0f", v); return b; }
        if (i >= kStep1 && (i - kStep1) % kPerStep == kDecay && v >= 99.5f) return "HOLD";
        if (i == kTone && v >= 99.5f) return "OPEN";
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        waveR = Rect(14, 54, 740, 150);
        srcP = Rect(766, 54, 220, 150);
        srcMenu->r = Rect(774, 74, 204, 20);
        loadBtn->r = Rect(774, 100, 60, 18); fitBtn->r = Rect(840, 100, 50, 18); barsSeg->r = Rect(896, 100, 82, 18);
        chopMenu->r = Rect(774, 150, 120, 20); clearBtn->r = Rect(900, 150, 78, 20);
        // the steps
        const float x0 = 98, w = 888;
        colW = w / kSteps;
        float y = 216;
        const float hs[7] = { 26, 46, 46, 34, 34, 15, 15 };
        for (int k = 0; k < 7; ++k) { laneR[k] = Rect(x0, y, w, hs[k]); y += hs[k] + 4; }
        // the effects and the rest
        const float fy = 476;
        for (int k = 0; k < kFx; ++k) { padR[k] = Rect(14 + k * 80.0f, fy + 18, 74, 56); fxAmt[k]->r = Rect(14 + k * 80.0f, fy + 80, 74, 14); }
        const float cx = 664;
        modeSeg->r = Rect(cx, fy + 18, 130, 18); playBtn->r = Rect(cx + 136, fy + 18, 56, 18); rateMenu->r = Rect(cx + 198, fy + 18, 124, 18);
        for (size_t k = 0; k < outRows.size(); ++k) outRows[k]->r = Rect(cx + (k % 2) * 164.0f, fy + 44 + (k / 2) * 20.0f, 158, 16);
    }

    void tick(float dt) override
    {
        barsSeg->enabled = (int)value(kSource) == kSourceFile;
        playBtn->label = (plug && plug->preview.load()) ? "STOP" : "PLAY";
        if (!plug) return;
        outMeter.tick(plug->outPeak, dt);
        const int st = plug->engine.stepNow();
        if (st != playStep) { playStep = st; if (st >= 0) flash[st] = 1.0f; }
        for (float& f : flash) f = std::max(0.0f, f - dt * 3.0f);
        playPos = plug->engine.playPos(); playSl = plug->engine.sliceNow();
        const Source* s = plug->sourceNow();
        if (s != shown) { shown = s; buildPeaks(); }
    }

    // ---- the wave ----
    static constexpr int kPeaks = 740;
    void buildPeaks()
    {
        for (int i = 0; i < kPeaks; ++i) { pkHi[i] = 0.0f; pkLo[i] = 0.0f; }
        if (!shown || shown->size() == 0) return;
        const size_t n = shown->size();
        float mx = 1e-6f;
        for (int i = 0; i < kPeaks; ++i)
        {
            const size_t a = n * (size_t)i / kPeaks, b = std::max(a + 1, n * (size_t)(i + 1) / kPeaks);
            float hi = -1.0f, lo = 1.0f;
            for (size_t k = a; k < b && k < n; ++k) { const float v = 0.5f * (shown->L[k] + shown->R[k]); hi = std::max(hi, v); lo = std::min(lo, v); }
            pkHi[i] = hi; pkLo[i] = lo; mx = std::max(mx, std::max(std::fabs(hi), std::fabs(lo)));
        }
        for (int i = 0; i < kPeaks; ++i) { pkHi[i] /= mx; pkLo[i] /= mx; }
    }
    Color sliceColour(int s, bool forText = false) const { return forText ? pump::wheelInk(s, theme.dark) : pump::wheelColour(s, false); }

    void drawBody() override
    {
        drawWave();
        // the break panel
        screenBox(srcP);
        caption(srcP.x + 10, srcP.y + 12, "BREAK");
        if (plug)
        {
            const Source* s = plug->sourceNow();
            char b[96];
            if (s) std::snprintf(b, sizeof b, "%.0f BPM  %d BAR%s  %.1f S", s->bars > 0 ? s->bpm : 240.0 * (1 << pump::clampv((int)value(kFileBars), 0, 2)) / (s->size() / s->sr), s->bars > 0 ? 1 : (1 << pump::clampv((int)value(kFileBars), 0, 2)), (s->bars > 1 || (s->bars == 0 && (int)value(kFileBars) > 0)) ? "S" : "", s->size() / s->sr);
            else std::snprintf(b, sizeof b, "%s", (int)value(kSource) == kSourceFile ? "NO FILE LOADED" : "MAKING THE BREAKS...");
            textAt(srcP.x + 10, 132, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
            if ((int)value(kSource) == kSourceFile)
            {
                std::string f = plug->fileInfo(); if (f.size() > 30) f = f.substr(0, 28) + "..";
                textAt(srcP.x + 10, 118, f, kLabel, 9.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 0.3f);
            }
        }
        caption(srcP.x + 10, 142, "CHOP");
        drawSteps();
        // the effects
        caption(14, 484, "MOMENTARY EFFECTS  (HOLD, OR KEYS 1-8)");
        for (int k = 0; k < kFx; ++k)
        {
            const bool on = (plug && plug->engine.fxOn(k)) || padDown == k;
            keyBox(padR[k], on, padDown == k, on ? &theme.neon : nullptr);
            textAt(padR[k].cx(), padR[k].y + 18, kFxKeys[k], kLabel, 11.0f, on ? theme.neonInk : theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
            char nb[8]; pump::padNoteName(fxNote(k), nb);
            char b[32]; std::snprintf(b, sizeof b, "%d   %s %d", k + 1, nb, fxNote(k));
            textAt(padR[k].cx(), padR[k].y + 40, b, kMono, 9.0f, on ? theme.neonInk : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        caption(664, 484, "SEQUENCER AND OUTPUT");
        drawOutMeter(outMeter, Rect(990, 54, 4, 150), Rect(995, 54, 4, 150), -48.0f, 6.0f);
    }

    void drawWave()
    {
        const Rect& r = waveR;
        screenBox(r);
        const Rect p(r.x + 1, r.y + 18, r.w - 2, r.h - 20);
        const float sw = p.w / kSlices;
        for (int s = 0; s < kSlices; ++s)
        {
            const Rect sr(p.x + s * sw, p.y, sw, p.h);
            fillRect(sr, withAlpha2(sliceColour(s), playSl == s ? (theme.dark ? 0.38f : 0.5f) : (theme.dark ? 0.12f : 0.16f)));
            if (s > 0) line(sr.x, p.y, sr.x, p.b(), withAlpha2(theme.ink, 0.25f), 1.0f);
            char b[4]; std::snprintf(b, sizeof b, "%d", s + 1);
            textAt(sr.x + 4, r.y + 10, b, kBold, 10.0f, sliceColour(s, true), ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (shown)
        {
            beginPath();
            for (int i = 0; i < kPeaks; ++i) { const float x = p.x + p.w * i / (kPeaks - 1); if (i == 0) moveTo(x, p.cy() - pkHi[i] * p.h * 0.46f); else lineTo(x, p.cy() - pkHi[i] * p.h * 0.46f); }
            for (int i = kPeaks - 1; i >= 0; --i) lineTo(p.x + p.w * i / (kPeaks - 1), p.cy() - pkLo[i] * p.h * 0.46f);
            closePath();
            fillColor(theme.dark ? withAlpha2(theme.neon, 0.85f) : theme.ink); fill();
        }
        else textAt(p.cx(), p.cy(), (int)value(kSource) == kSourceFile ? "LOAD A .WAV" : "MAKING THE BREAKS...", kLabel, 12.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        if (playPos >= 0.0f) { const float x = p.x + p.w * playPos; line(x, p.y, x, p.b(), theme.second, 2.0f); }
        strokeRect(r, theme.ink, 1.5f);
    }

    // ---- the steps ----
    int stepAt(float x) const { return pump::clampv((int)((x - laneR[0].x) / colW), 0, kSteps - 1); }
    float sv(int k, int f) const { return value(stepParam(k, f)); }
    int length() const { return pump::clampv((int)value(kSeqLength), 1, kSteps); }

    void drawSteps()
    {
        static const char* const names[7] = { "SLICE", "PITCH", "BEND", "LEVEL", "DECAY", "REVERSE", "LENGTH" };
        for (int l = 0; l < 7; ++l) textAt(14, laneR[l].cy(), names[l], kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.6f);
        caption(14, 211, "STEPS");
        const int L = length();
        for (int k = 0; k < kSteps; ++k)
        {
            const float x = laneR[0].x + k * colW;
            const bool dim = k >= L, now = playStep == k;
            const int sl = (int)std::lround(sv(k, kSlice));
            const Color c = sl > 0 ? sliceColour(sl - 1) : theme.panel;
            // the column: a faint band, brighter where the playhead is
            if (now) fillRect(Rect(x, laneR[0].y - 2, colW, laneR[5].b() - laneR[0].y + 4), withAlpha2(theme.neon, 0.18f + 0.25f * flash[k]));
            // SLICE
            const Rect s(x + 2, laneR[0].y, colW - 4, laneR[0].h);
            fillRect(s, sl > 0 ? withAlpha2(c, dim ? 0.4f : 1.0f) : theme.panel);
            strokeRect(s, dim ? theme.dim : theme.ink, 1.0f);
            char b[16]; if (sl > 0) std::snprintf(b, sizeof b, "%d", sl); else std::snprintf(b, sizeof b, "-");
            textAt(s.cx(), s.cy(), b, kBold, 12.0f, sl > 0 ? pump::hexColor(0x111114) : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
            // PITCH and BEND: bipolar bars
            for (int lane = 1; lane <= 2; ++lane)
            {
                const Rect rr(x + 2, laneR[lane].y, colW - 4, laneR[lane].h);
                fillRect(rr, theme.panel); strokeRect(rr, dim ? theme.dim : withAlpha2(theme.ink, 0.5f), 1.0f);
                line(rr.x, rr.cy(), rr.r(), rr.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
                const float v = sv(k, lane == 1 ? kPitch : kBend) / 24.0f;
                const float y0 = rr.cy(), y1 = rr.cy() - v * (rr.h * 0.5f - 2);
                if (lane == 1) fillRect(Rect(rr.x + 3, std::min(y0, y1), rr.w - 6, std::fabs(y1 - y0) + 1), dim ? theme.dim : theme.neon);
                else
                {
                    // the bend as a line across the step, from the step's pitch to where it ends
                    beginPath(); moveTo(rr.x + 3, y0); lineTo(rr.r() - 3, y1);
                    strokeColor(dim ? theme.dim : theme.second); strokeWidth(2.2f); lineCap(ROUND); stroke();
                }
                if (std::fabs(v) > 0.001f)
                {
                    std::snprintf(b, sizeof b, "%+.0f", v * 24.0f);
                    textAt(rr.cx(), lane == 1 ? (v > 0 ? rr.b() - 7 : rr.y + 7) : (v > 0 ? rr.b() - 7 : rr.y + 7), b, kMono, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
                }
            }
            // LEVEL
            {
                const Rect rr(x + 2, laneR[3].y, colW - 4, laneR[3].h);
                fillRect(rr, theme.panel); strokeRect(rr, dim ? theme.dim : withAlpha2(theme.ink, 0.5f), 1.0f);
                const float h = (rr.h - 4) * sv(k, kLevel) * 0.01f;
                fillRect(Rect(rr.x + 3, rr.b() - 2 - h, rr.w - 6, h), dim ? theme.dim : theme.ink2);
            }
            // DECAY: the step's volume envelope
            {
                const Rect rr(x + 2, laneR[4].y, colW - 4, laneR[4].h);
                fillRect(rr, theme.panel); strokeRect(rr, dim ? theme.dim : withAlpha2(theme.ink, 0.5f), 1.0f);
                const float d = sv(k, kDecay) * 0.01f, lv = sv(k, kLevel) * 0.01f;
                const float tau = d >= 0.995f ? 1e9f : 0.05f * std::pow(40.0f, d);
                beginPath();
                for (int i = 0; i <= 16; ++i)
                {
                    const float t = i / 16.0f, e = lv * std::exp(-t / tau);
                    const float px = rr.x + 3 + (rr.w - 6) * t, py = rr.b() - 3 - e * (rr.h - 6);
                    if (i == 0) moveTo(px, py); else lineTo(px, py);
                }
                strokeColor(dim ? theme.dim : theme.ink); strokeWidth(1.6f); stroke();
            }
            // REVERSE
            {
                const bool rv = sv(k, kReverseStep) >= 0.5f;
                const Rect rr(x + 2, laneR[5].y, colW - 4, laneR[5].h);
                fillRect(rr, rv ? (dim ? theme.dim : theme.second) : theme.panel); strokeRect(rr, dim ? theme.dim : theme.ink, 1.0f);
                if (rv) textAt(rr.cx(), rr.cy(), "REV", kLabel, 8.5f, pump::hexColor(0x111114), ALIGN_CENTER | ALIGN_MIDDLE, 0.5f);
            }
            // the step numbers
            {
                const Rect nb(x + 2, laneR[6].y, colW - 4, laneR[6].h);
                fillRect(nb, now ? theme.ink : (k < L ? theme.panel2 : theme.bg));
                std::snprintf(b, sizeof b, "%d", k + 1);
                textAt(nb.cx(), nb.cy(), b, kMono, 9.5f, now ? theme.on : (k < L ? theme.ink : theme.dim), ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
    }

    // ---- editing ----
    void writeChop(const Pattern& p, const char* what)
    {
        beginGroup();
        for (int k = 0; k < kSteps; ++k)
        {
            const Step& st = p.s[k];
            setParamGesture(stepParam(k, kSlice), (float)st.slice); setParamGesture(stepParam(k, kPitch), (float)st.pitch);
            setParamGesture(stepParam(k, kBend), (float)st.bend); setParamGesture(stepParam(k, kLevel), (float)(st.level * 100.0));
            setParamGesture(stepParam(k, kDecay), (float)(st.decay * 100.0)); setParamGesture(stepParam(k, kReverseStep), st.reverse ? 1.0f : 0.0f);
        }
        endGroup();
        setStatus(what);
    }
    // a value lane's value at y (pitch / bend: -24..24, rounded; level / decay: 0..100)
    float laneValue(int lane, float y) const
    {
        const Rect& rr = laneR[lane];
        float t = pump::clampv((rr.b() - 2 - y) / (rr.h - 4), 0.0f, 1.0f);
        if (lane == 1 || lane == 2) return std::round(-24.0f + 48.0f * t);
        return std::round(t * 100.0f);
    }
    static int laneField(int lane) { static const int f[5] = { kSlice, kPitch, kBend, kLevel, kDecay }; return f[pump::clampv(lane, 0, 4)]; }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft && button != kMouseButtonRight) return false;
        for (int k = 0; k < kFx; ++k)
            if (padR[k].contains(x, y) && button == kMouseButtonLeft) { padDown = k; setUiHeld(k, true); setStatus(std::string(kFxNames[k]) + " (held)"); return true; }
        if (waveR.contains(x, y) && plug && button == kMouseButtonLeft)
        {
            const int s = pump::clampv((int)((x - waveR.x) / (waveR.w / kSlices)), 0, kSlices - 1);
            plug->padSlice.store(s);
            setStatus("Slice " + std::to_string(s + 1));
            return true;
        }
        for (int lane = 0; lane < 7; ++lane)
        {
            if (!laneR[lane].contains(x, y)) continue;
            const int k = stepAt(x);
            if (lane == 6) { setParamGesture(kSeqLength, (float)(k + 1)); setStatus(paramStatus(kSeqLength)); return true; }
            if (lane == 5) { setParamGesture(stepParam(k, kReverseStep), sv(k, kReverseStep) >= 0.5f ? 0.0f : 1.0f); setStatus(paramStatus(stepParam(k, kReverseStep))); return true; }
            const int p = stepParam(k, laneField(lane));
            if (button == kMouseButtonRight || dbl || cmdKey(mod))
            {
                // reset: no slice / the default
                setParamGesture(p, lane == 0 ? 0.0f : spec(p).def);
                setStatus(paramStatus(p));
                return true;
            }
            dragLane = lane; dragStep = k; dragY0 = y; dragV0 = value(p);
            beginGroup();
            if (lane == 0)
            {
                // a click on an empty slice cell gives it the step's own slice; then drag up / down
                if (dragV0 < 0.5f) { setParamGesture(p, (float)(k + 1)); dragV0 = (float)(k + 1); }
                if (plug && playStep < 0) plug->padSlice.store((int)value(p) - 1);
            }
            else setParamGesture(p, laneValue(lane, y));
            setStatus(paramStatus(p));
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint) override
    {
        if (dragLane < 0) return;
        if (dragLane == 0)
        {
            const int p = stepParam(dragStep, kSlice);
            const float v = pump::clampv(std::round(dragV0 + (dragY0 - y) / 8.0f), 0.0f, (float)kSlices);
            if (v != value(p)) { setParamGesture(p, v); if (plug && v > 0.5f && playStep < 0) plug->padSlice.store((int)v - 1); }
            setStatus(paramStatus(p));
            return;
        }
        // paint the value lanes across steps
        const int k = stepAt(x);
        const int p = stepParam(k, laneField(dragLane));
        setParamGesture(p, laneValue(dragLane, y));
        setStatus(paramStatus(p));
    }
    void bodyMouseUp(float, float) override
    {
        if (padDown >= 0) { setUiHeld(padDown, false); padDown = -1; }
        if (dragLane >= 0) { dragLane = -1; endGroup(); }
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        for (int lane = 0; lane < 5; ++lane)
        {
            if (!laneR[lane].contains(x, y)) continue;
            const int p = stepParam(stepAt(x), laneField(lane));
            const float st = lane == 0 ? 1.0f : ((lane <= 2) ? ((mod & kModifierShift) ? 12.0f : 1.0f) : ((mod & kModifierShift) ? 1.0f : 5.0f));
            setParamGesture(p, value(p) + (dy > 0 ? st : -st));
            setStatus(paramStatus(p));
            return true;
        }
        return false;
    }
    void setUiHeld(int k, bool on)
    {
        if (!plug) return;
        unsigned v = plug->uiHeld.load();
        v = on ? (v | (1u << k)) : (v & ~(1u << k));
        plug->uiHeld.store(v);
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '8') { setUiHeld((int)(key - '1'), true); return true; }
        if (key == ' ') { if (plug) plug->preview.store(!plug->preview.load()); return true; }
        return false;
    }
    bool keyRelease(uint key, uint) override
    {
        if (key >= '1' && key <= '8') { setUiHeld((int)(key - '1'), false); return true; }
        return false;
    }
    void releaseAllHeld() override { if (plug) plug->uiHeld.store(0u); padDown = -1; }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename || !*filename) return;
        setState("file", filename);
        if (plug) { /* the plugin reads it in setState */ }
        setParamGesture(kSource, (float)kSourceFile);
        setStatus("Loaded: " + std::string(filename));
    }
    void bodyHover(float x, float y) override
    {
        if (waveR.contains(x, y)) setStatus("Click a slice to hear it");
        else if (laneR[0].contains(x, y)) setStatus("Slice: drag up / down, wheel; click an empty one for its own slice; right-click: none (the slice before plays on)");
        else if (laneR[1].contains(x, y)) setStatus("Pitch of the step in semitones: click or drag (paint across steps), wheel; right-click resets");
        else if (laneR[2].contains(x, y)) setStatus("Bend: the pitch moves this many semitones across the step (the pitch envelope)");
        else if (laneR[3].contains(x, y)) setStatus("Level of the step");
        else if (laneR[4].contains(x, y)) setStatus("Decay: how fast the step fades (the volume envelope; the top holds until the next step)");
        else if (laneR[5].contains(x, y)) setStatus("Reverse the step's slice");
        else if (laneR[6].contains(x, y)) setStatus("Click: the pattern ends at this step");
        else for (int k = 0; k < kFx; ++k) if (padR[k].contains(x, y)) { setStatus(std::string(kFxNames[k]) + ": hold (or key " + std::to_string(k + 1) + ", or MIDI " + std::to_string(fxNote(k)) + ")"); break; }
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click the wave", "hear a slice" },
            { "Steps", "drag the slice cell; click / drag the lanes (paint across); wheel; right-click resets" },
            { "Hold a pad, keys 1-8", "the momentary effects" },
            { "MIDI", "slices on the white keys from C-4 (48), effects from C-2 (24)" },
            { "Space", "play the steps now" },
        };
    }
    std::string footerInfo() override
    {
        char b[96];
        std::snprintf(b, sizeof b, "%s   SEQ %s   %d x %s", (int)value(kSource) == kSourceFile ? "FILE" : kBreaks[pump::clampv((int)value(kSource), 0, kNumBreaks - 1)].name,
                      kSeqModeNames[pump::clampv((int)value(kSeqMode), 0, kNumSeqModes - 1)], length(), kRateNames[pump::clampv((int)value(kSeqRate), 0, kNumRates - 1)]);
        std::string s = b;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    PluginBreak* plug = nullptr;
    const Source* shown = nullptr;
    float pkHi[kPeaks] = {}, pkLo[kPeaks] = {};
    Rect waveR, srcP, laneR[7], padR[kFx];
    float colW = 55.0f;
    MenuButton *srcMenu, *chopMenu, *rateMenu;
    Button *loadBtn, *fitBtn, *clearBtn, *playBtn;
    Segmented *barsSeg, *modeSeg;
    SliderRow* fxAmt[kFx];
    std::vector<SliderRow*> outRows;
    OutMeter outMeter;
    int playStep = -1, playSl = -1, padDown = -1, dragLane = -1, dragStep = 0;
    float playPos = -1.0f, dragY0 = 0, dragV0 = 0, flash[kSteps] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIBreak)
};

UI* createUI() { return new UIBreak(); }

END_NAMESPACE_DISTRHO
