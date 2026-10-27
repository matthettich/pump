// pump GRAIN: interface. Two screens: the capture buffer (the recent seconds as a waveform, the write head at the
// right) with the live grains drawn on it from the engine's lock-free snapshot (where each grain reads, its pitch as
// height, its envelope as opacity, the read head and its direction; reverse grains in the second colour), and the
// MORPH pad (X smooth .. glitchy, Y steady .. wild; its texture shows what the macros do) with MOTION beneath.
// Drag the buffer to place the grains (left/right = position, up/down = pitch); drag the pad's dot to morph.
// Below: GRAINS (position, size, density or sync division, spray, envelope, reverse, stereo), PITCH (pitch, spread,
// quantize scale), FX (filter, feedback, space) and OUTPUT (freeze, mix, output, level).
#include "ui/PumpUI.hpp"
#include "PluginGrain.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::grain;
using pump::kNoteNames;
using pump::kNumNotes;

class UIGrain : public PumpUI {
public:
    UIGrain() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::grain(), "GRAIN", 1000, 620, kBypass)
    {
        posDial = addDial(kPosition, "POSITION", Dial::Large);
        posDial->tip = "Position: how far behind the input the grains read, 0..4 s (or drag left/right in the buffer)";
        sizeDial = addDial(kSize, "SIZE");
        sizeDial->tip = "Grain size, 5 ms (glitch, buzz) .. 1 s (smooth, smeared)";
        rateDial = addDial(kDensity, "DENSITY");
        sprayDial = addDial(kSpray, "SPRAY");
        sprayDial->tip = "Spray: scatters where (up to 1 s further back) and when each grain starts";
        syncBtn = addToggle(kSync, "SYNC");
        syncBtn->tip = "Sync: one grain per note division, locked to the host's grid (free-runs at the host tempo when stopped)";
        envSeg = addSegmented(kEnvelope, { "SMOOTH", "SOFT", "PERC", "SWELL", "SQUARE" });
        envSeg->tip = "Grain envelope: Smooth (bell) · Soft (flat top) · Percussive (snap, decay) · Swell (rise, cut) · Square (gated)   (keys 1-5)";
        revDial = addDial(kReverse, "REVERSE", Dial::Small);
        revDial->tip = "Reverse: the chance that a grain plays backwards";
        stereoDial = addDial(kStereo, "STEREO", Dial::Small);
        stereoDial->tip = "Stereo spread: pans each grain at random across the field";
        posDial->tabGroup = sizeDial->tabGroup = rateDial->tabGroup = sprayDial->tabGroup = 1;

        pitchDial = addDial(kPitch, "PITCH", Dial::Large, true);
        pitchDial->tip = "Pitch of the grains, -24..+24 semitones (or drag up/down in the buffer)";
        spreadDial = addDial(kPitchSpread, "SPREAD");
        spreadDial->tip = "Pitch spread: a random detune per grain, up to +-24 semitones (quantized to the scale when one is set)";
        quantMenu = addMenu(kQuantize);
        quantMenu->tip = "Quantize every grain's pitch to a scale above the source: chromatic, major, minor, pentatonic, octaves, fifths";
        pitchDial->tabGroup = spreadDial->tabGroup = 2;

        filterSeg = addSegmented(kFilter, { "OFF", "LP", "BP", "HP" });
        filterSeg->tip = "Filter after the grains: off, low pass, band pass, high pass";
        cutDial = addDial(kCutoff, "CUTOFF", Dial::Small);
        cutDial->tip = "Filter cutoff";
        resDial = addDial(kResonance, "RESO", Dial::Small);
        resDial->tip = "Filter resonance";
        fbDial = addDial(kFeedback, "FEEDBACK");
        fbDial->tip = "Feedback: the grains are written back into the buffer (soft-limited) for evolving, self-similar clouds; with pitch, a rising shimmer";
        spaceDial = addDial(kSpace, "SPACE");
        spaceDial->tip = "Space: a lush diffuse reverb after the grains (more = longer and wetter)";
        cutDial->tabGroup = resDial->tabGroup = fbDial->tabGroup = spaceDial->tabGroup = 3;

        freezeBtn = addToggle(kFreeze, "FREEZE");
        freezeBtn->tip = "Freeze: stop recording and keep playing grains from what the buffer holds (F)";
        mixDial = addDial(kMix, "MIX", Dial::Small);
        mixDial->tip = "Mix: dry vs grains (equal power; no latency)";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Small, true);
        outDial->tip = "Output gain";
        mixDial->tabGroup = outDial->tabGroup = 4;

        rateRow = addRow(kMotionRate, "RATE");
        rateRow->labelW = 40; rateRow->valueW = 58;
        rateRow->tip = "Motion rate: how fast the slow random movement of position, size and pitch wanders";
        depthRow = addRow(kMotionDepth, "DEPTH");
        depthRow->labelW = 48; depthRow->valueW = 40;
        depthRow->tip = "Motion depth: how far position, size and pitch wander";

        if (auto* p = static_cast<PluginGrain*>(pluginPtr())) plug = p;
        bindControls();
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kDensity: std::snprintf(b, sizeof b, v < 9.95f ? "%.1f /s" : "%.0f /s", v); return b;
        case kPitch:
            if (std::fabs(v) < 0.5f) return "0 st";
            std::snprintf(b, sizeof b, "%+.0f st", v); return b;
        case kPitchSpread:
            if (v < 0.005f) return "OFF";
            std::snprintf(b, sizeof b, v < 9.95f ? "±%.2f" : "±%.1f", v); return b;
        case kMorphX: case kMorphY:
            if (std::fabs(v) < 0.5f) return "0%";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        case kMotionRate: std::snprintf(b, sizeof b, v < 0.995f ? "%.2f Hz" : "%.1f Hz", v); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    // DENSITY becomes the note division while synced
    void bindControls()
    {
        const bool synced = value(kSync) >= 0.5f;
        rateDial->param = synced ? kDivision : kDensity;
        rateDial->label = synced ? "DIVISION" : "DENSITY";
        rateDial->tip = synced ? "Division: one grain per note value at the host's tempo" : "Density: grains per second (1..100)";
    }
    void onParamChanged(int i, float) override { if (i == kSync) bindControls(); }
    void onPresetApplied() override { bindControls(); }

    void layout() override
    {
        bufR = Rect(14, 58, 626, 296);
        padScreen = Rect(656, 58, 330, 296);
        pad = Rect(padScreen.x + 10, padScreen.y + 28, padScreen.w - 20, 196);
        rateRow->r = Rect(padScreen.x + 10, pad.b() + 32, 148, 20);
        depthRow->r = Rect(padScreen.x + 172, pad.b() + 32, 148, 20);

        const float y0 = 370;
        grainPanel = Rect(14, y0, 430, 218);
        posDial->r = Rect(22, y0 + 28, 94, Dial::heightFor(Dial::Large));
        sizeDial->r = Rect(124, y0 + 36, 72, Dial::heightFor(Dial::Medium));
        rateDial->r = Rect(202, y0 + 36, 72, Dial::heightFor(Dial::Medium));
        sprayDial->r = Rect(280, y0 + 36, 72, Dial::heightFor(Dial::Medium));
        syncBtn->r = Rect(366, y0 + 50, 66, 26);
        envSeg->r = Rect(26, y0 + 156, 252, 26);
        revDial->r = Rect(294, y0 + 138, 64, Dial::heightFor(Dial::Small));
        stereoDial->r = Rect(364, y0 + 138, 64, Dial::heightFor(Dial::Small));

        pitchPanel = Rect(456, y0, 180, 218);
        pitchDial->r = Rect(464, y0 + 28, 86, Dial::heightFor(Dial::Large));
        spreadDial->r = Rect(556, y0 + 36, 72, Dial::heightFor(Dial::Medium));
        quantMenu->r = Rect(468, y0 + 156, 156, 26);
        scaleStrip = Rect(468, y0 + 192, 156, 12);

        fxPanel = Rect(648, y0, 204, 218);
        filterSeg->r = Rect(660, y0 + 28, 180, 26);
        cutDial->r = Rect(654, y0 + 64, 62, Dial::heightFor(Dial::Small));
        resDial->r = Rect(714, y0 + 64, 62, Dial::heightFor(Dial::Small));
        filterR = Rect(782, y0 + 70, 58, 50);
        fbDial->r = Rect(662, y0 + 138, 72, Dial::heightFor(Dial::Medium));
        spaceDial->r = Rect(762, y0 + 138, 72, Dial::heightFor(Dial::Medium));

        outPanel = Rect(864, y0, 122, 218);
        freezeBtn->r = Rect(876, y0 + 28, 98, 52);
        mixDial->r = Rect(868, y0 + 92, 58, Dial::heightFor(Dial::Small));
        outDial->r = Rect(924, y0 + 92, 58, Dial::heightFor(Dial::Small));
        meterL = Rect(876, y0 + 184, 98, 6);
        meterR = Rect(876, y0 + 194, 98, 6);
    }

    // ---- feeds ----
    void tick(float dt) override
    {
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        GrainView v;
        if (plug->engine.snapshot.read(v))
        {
            // the grains that ended since the last frame leave a short trail where they read
            for (auto& g : ghosts) if (g.life > 0.0f) g.life -= dt * 4.0f;
            const uint32_t first = v.ended - std::min<uint32_t>(v.ended - endedSeen, (uint32_t)kMaxRecent);
            for (uint32_t k = first; k != v.ended; ++k)
            {
                const GrainView::E& en = v.e[k % kMaxRecent];
                Ghost& g = ghosts[ghostW++ % kGhosts];
                g.from = en.from; g.to = en.to; g.pitch = en.pitch; g.rev = en.reverse > 0.5f; g.head = en.head; g.life = 1.0f;
            }
            endedSeen = v.ended;
            view = v;
        }
        hopSec = (float)plug->engine.hopSeconds();
        // the span shown: the reach of the grains (auto) or a fixed zoom
        float want = 1.0f;
        if (spanSel == 0)
        {
            const Shaped s = applyMacros(currentSettings());
            const double reach = value(kPosition) * 0.001 * (1.0 + 0.5 * s.motionDepth) + s.spray + s.sizeMs * 0.001 * std::max(1.0, std::exp2(std::fabs(value(kPitch)) / 12.0));
            want = pump::clampv((float)(reach * 1.25 + 0.25), 1.0f, 10.0f);
        }
        else want = kSpans[spanSel];
        span = spanInit ? span + (want - span) * std::min(1.0f, dt * 4.0f) : want;
        spanInit = true;
        const int need = std::min(kWaveN, (int)(span / std::max(1e-4f, hopSec)) + 4);
        plug->engine.wave.readSmooth(wave + kWaveN - need, need);
        waveCount = need;
        bpm = plug->bpmNow.load(std::memory_order_relaxed);
        playing = plug->playingNow.load(std::memory_order_relaxed);
    }

    Settings currentSettings() const
    {
        Settings s;
        s.positionMs = value(kPosition); s.sizeMs = value(kSize); s.density = value(kDensity); s.spray = value(kSpray);
        s.reverse = value(kReverse); s.stereo = value(kStereo); s.pitch = value(kPitch); s.pitchSpread = value(kPitchSpread);
        s.motionDepth = value(kMotionDepth); s.morphX = value(kMorphX); s.morphY = value(kMorphY);
        s.envelope = (int)value(kEnvelope);
        return s;
    }

    void drawBody() override
    {
        drawBuffer();
        drawPad();

        strokeRect(grainPanel, theme.dim, 1.0f);
        strokeRect(pitchPanel, theme.dim, 1.0f);
        strokeRect(fxPanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(grainPanel.x + 12, grainPanel.y + 16, "GRAINS");
        caption(pitchPanel.x + 12, pitchPanel.y + 16, "PITCH");
        caption(fxPanel.x + 12, fxPanel.y + 16, "FX");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        caption(envSeg->r.x, envSeg->r.y - 11, "ENVELOPE");
        caption(quantMenu->r.x, quantMenu->r.y - 11, "QUANTIZE");
        drawEnvelopePreview(Rect(envSeg->r.r() - 86, envSeg->r.y - 22, 86, 16));
        // the tempo the grains follow, under the sync key
        {
            char t[32];
            if (value(kSync) >= 0.5f) std::snprintf(t, sizeof t, "%.0f BPM%s", bpm, playing ? "" : " FREE");
            else std::snprintf(t, sizeof t, "%.0f BPM", bpm);
            textAt(syncBtn->r.cx(), syncBtn->r.b() + 15, t, kMono, 9.5f, value(kSync) >= 0.5f ? theme.ink : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        drawScaleStrip();
        drawFilterCurve();
        // output level
        textAt(meterL.x, meterL.y - 10, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
        char t[24]; std::snprintf(t, sizeof t, "%.1f dB", std::max(-60.0f, outMeter.hold[0] > outMeter.hold[1] ? outMeter.hold[0] : outMeter.hold[1]));
        textAt(meterL.r(), meterL.y - 10, outMeter.hold[0] > -60.0f || outMeter.hold[1] > -60.0f ? t : "-INF", kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        hMeter(meterL, outMeter.lvl[0], outMeter.hold[0]);
        hMeter(meterR, outMeter.lvl[1], outMeter.hold[1]);
    }

    void hMeter(const Rect& r, float levelDb, float peakDb)
    {
        const float lo = -60.0f, hi = 6.0f;
        auto xOf = [&](float d) { return r.x + (pump::clampv(d, lo, hi) - lo) / (hi - lo) * r.w; };
        fillRect(r, theme.panel2);
        if (levelDb > lo)
        {
            const float x = xOf(levelDb), x0 = xOf(0.0f);
            fillRect(Rect(r.x, r.y, std::min(x, x0) - r.x, r.h), theme.neon);
            if (x > x0) fillRect(Rect(x0, r.y, x - x0, r.h), theme.red);
        }
        if (peakDb > lo) line(xOf(peakDb), r.y - 1, xOf(peakDb), r.b() + 1, peakDb > 0.0f ? theme.red : theme.ink, 1.5f);
        strokeRect(r, theme.ink, 1.0f);
    }

    // ---- the buffer and the grains ----
    float xOfDelay(const Rect& p, float d) const { return p.r() - d / span * p.w; }
    float yOfPitch(const Rect& p, float st) const { return p.cy() - st / 24.0f * p.h * 0.42f; }
    Rect bufPlot() const { return Rect(bufR.x + 12, bufR.y + 28, bufR.w - 24, bufR.h - 50); }

    void drawBuffer()
    {
        const Rect& r = bufR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "BUFFER");
        const bool frozen = view.frozen > 0.5f;
        {
            char t[64];
            const char* gw = view.count == 1 ? "GRAIN " : "GRAINS";
            if (value(kSync) >= 0.5f) std::snprintf(t, sizeof t, "%2d %s   %s", view.count, gw, kNoteNames[pump::clampv((int)value(kDivision), 0, kNumNotes - 1)]);
            else std::snprintf(t, sizeof t, "%2d %s   %.1f /S", view.count, gw, view.rate);
            textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
            std::snprintf(t, sizeof t, spanSel == 0 ? "AUTO %.1f s" : "%.0f s", span);
            textAt(r.x + 80, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        const Rect p = bufPlot();
        // pitch lanes
        for (int st : { -24, -12, 12, 24 })
        {
            const float y = yOfPitch(p, (float)st);
            line(p.x, y, p.r(), y, theme.grid, 1.0f);
        }
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.18f), 1.0f);
        // time grid
        const float step = span <= 1.6f ? 0.25f : (span <= 3.2f ? 0.5f : (span <= 6.5f ? 1.0f : 2.0f));
        for (float d = step; d < span - step * 0.3f; d += step)
        {
            const float x = xOfDelay(p, d);
            line(x, p.y, x, p.b(), theme.grid, 1.0f);
            char b[16]; std::snprintf(b, sizeof b, step < 1.0f ? "-%.3g s" : "-%.0f s", d);
            textAt(x, r.b() - 11, b, kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        textAt(p.r(), r.b() - 11, frozen ? "FROZEN" : "NOW", kLabel, 9.5f, frozen ? theme.second : theme.ink, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);

        save();
        scissor(p.x, p.y, p.w, p.h);
        // where the grains read: position .. position + spray
        const float pos = view.position, spr = view.spray;
        const float xa = xOfDelay(p, pos), xb = xOfDelay(p, pos + spr);
        fillRect(Rect(xb, p.y, std::max(1.0f, xa - xb), p.h), withAlpha2(theme.neon, theme.dark ? 0.10f : 0.20f));
        for (float y = p.y; y < p.b(); y += 8.0f) line(xa, y, xa, std::min(p.b(), y + 4.0f), withAlpha2(theme.ink, 0.55f), 1.2f);
        drawWave(p);
        drawGrains(p);
        restore();
        // pitch lanes (semitones), over the waveform
        for (int st : { 24, 12, -12, -24 })
        {
            char b[8]; std::snprintf(b, sizeof b, "%+d", st);
            const float y = yOfPitch(p, (float)st);
            fillRect(Rect(p.x, y - 6.0f, 20.0f, 12.0f), withAlpha2(theme.screen, 0.85f));
            textAt(p.x + 2, y, b, kMono, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // the write head
        line(p.r(), p.y - 4, p.r(), p.b() + 4, frozen ? theme.second : theme.ink, 2.0f);
        textAt(xa + 4, p.y + 7, "POS", kLabel, 9.0f, theme.ink, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
        if (frozen)
        {
            const Rect b(p.r() - 66, p.y + 2, 60, 18);
            Color c = theme.second;
            keyBox(b, false, false, &c, 2.0f);
            textAt(b.cx(), b.cy() + 0.5f, "FROZEN", kLabel, 10.0f, theme.on, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        }
    }

    void drawWave(const Rect& p)
    {
        if (waveCount < 2 || hopSec <= 0.0f) return;
        const int cols = (int)p.w;
        float colMax[1024];
        float vmax = 1e-4f;
        // the newest peak (index kWaveN - 1) ends hopPhase before the head
        for (int c = 0; c < cols && c < 1024; ++c)
        {
            const float d1 = span * (float)(cols - c) / (float)cols, d0 = span * (float)(cols - c - 1) / (float)cols;
            int k0 = (int)std::floor((d0 - view.hopPhase) / hopSec), k1 = (int)std::ceil((d1 - view.hopPhase) / hopSec);
            k0 = std::max(0, k0); k1 = std::min(waveCount - 1, std::max(k1, k0 + 1));
            float m = 0.0f;
            for (int k = k0; k < k1; ++k) m = std::max(m, wave[kWaveN - 1 - k]);
            colMax[c] = m;
            vmax = std::max(vmax, m);
        }
        waveScale = vmax > waveScale ? vmax : waveScale + (vmax - waveScale) * 0.03f;
        const float s = 1.0f / std::max(0.02f, waveScale), hh = p.h * 0.44f;
        auto amp = [&](float m) { return std::pow(std::min(1.0f, m * s), 0.75f) * hh; };
        beginPath();
        moveTo(p.x, p.cy());
        for (int c = 0; c < cols && c < 1024; ++c) lineTo(p.x + (float)c + 0.5f, p.cy() - amp(colMax[c]));
        lineTo(p.r(), p.cy());
        for (int c = std::min(cols, 1024) - 1; c >= 0; --c) lineTo(p.x + (float)c + 0.5f, p.cy() + amp(colMax[c]));
        closePath();
        fillColor(withAlpha2(theme.ink, theme.dark ? 0.16f : 0.13f));
        fill();
        strokeColor(withAlpha2(theme.ink, theme.dark ? 0.35f : 0.30f));
        strokeWidth(1.0f);
        stroke();
    }

    // A grain drawn as its envelope over the span it reads (left = older audio): a bell, a decay, a swell... at the
    // height of its pitch, filled by how loud it is now. Reverse grains are in the second colour; the live ones show
    // their read head and the way it moves.
    void grainShape(const Rect& p, float from, float to, float pitch, bool rev, float alpha, bool live, float head = -1.0f)
    {
        const float xa = xOfDelay(p, from), xb = xOfDelay(p, to);
        const float y = yOfPitch(p, pitch), hh = 13.0f;
        const Color c = rev ? theme.second : (theme.dark ? theme.neon : pump::mixColor(theme.neon, theme.ink, 0.12f));
        if (std::fabs(xb - xa) < 3.0f)
        {
            const float cx = 0.5f * (xa + xb);
            fillRect(Rect(cx - 1.5f, y - hh, 3.0f, hh), withAlpha2(c, alpha));
            if (live) line(cx, y - hh, cx, y, withAlpha2(theme.ink, 0.6f), 1.0f);
            return;
        }
        // the grain's length in samples (the span read / its speed) gives the envelope's edges
        const float ratio = std::exp2(pitch / 12.0f);
        const float srShown = (float)std::max(8000.0, sampleRate());
        const int len = std::max(16, (int)(std::fabs(to - from) * srShown / std::max(0.01f, ratio)));
        const EnvSpec es = envSpec(pump::clampv((int)value(kEnvelope), 0, kNumEnvelopes - 1), len, srShown);
        const float* tab = edgeTable();
        constexpr int M = 24;
        float ys[M + 1];
        for (int i = 0; i <= M; ++i)
        {
            const int k = std::min(len - 1, i * len / M);
            float e = 1.0f;
            if (k < es.atk) e = edge(tab, (float)k / es.atk);
            if (len - k < es.rel) e *= edge(tab, (float)(len - k) / es.rel);
            if (es.body == kBodyDecay) { const float u = 1.0f - (float)k / len; e *= u * u; }
            else if (es.body == kBodyRise) { const float u = (float)k / len; e *= u * u; }
            ys[i] = i == M ? 0.0f : e;
        }
        beginPath();
        moveTo(xa, y);
        for (int i = 0; i <= M; ++i) lineTo(xa + (xb - xa) * i / M, y - ys[i] * hh);
        lineTo(xb, y);
        closePath();
        fillColor(withAlpha2(c, alpha));
        fill();
        if (!live) return;
        strokeColor(withAlpha2(theme.ink, 0.35f + 0.5f * alpha)); strokeWidth(1.0f); lineJoin(ROUND); stroke();
        line(std::min(xa, xb), y, std::max(xa, xb), y, withAlpha2(theme.ink, 0.5f), 1.0f);
        if (head >= 0.0f)
        {
            const float hx = xOfDelay(p, head);
            line(hx, y - hh - 2.0f, hx, y + 3.0f, theme.ink, 1.5f);
            // the direction it reads in (reverse grains move into the past: left)
            beginPath();
            const float dx = rev ? -5.0f : 5.0f;
            moveTo(hx, y + 2.0f); lineTo(hx + dx, y + 4.5f); lineTo(hx, y + 7.0f); closePath();
            fillColor(theme.ink); fill();
        }
    }

    void drawGrains(const Rect& p)
    {
        // ended grains: their delays have grown by what was written since they ended
        const float sr = (float)std::max(1.0, sampleRate());
        for (const auto& g : ghosts)
            if (g.life > 0.0f)
            {
                const float moved = (float)(uint32_t)(view.head - g.head) / sr;
                grainShape(p, g.from + moved, g.to + moved, g.pitch, g.rev, (theme.dark ? 0.30f : 0.40f) * g.life, false);
            }
        for (int k = 0; k < view.count; ++k)
        {
            const auto& g = view.g[k];
            grainShape(p, g.from, g.to, g.pitch, g.reverse > 0.5f, 0.25f + 0.65f * pump::clampv(g.env, 0.0f, 1.0f), true, g.head);
        }
    }

    // ---- the morph pad ----
    static float hash01(int a, int b, int s)
    {
        uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)s * 83492791u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (float)(h & 0xFFFFu) / 65535.0f;
    }
    void padDot(float& x, float& y) const
    {
        x = pad.x + (value(kMorphX) + 100.0f) / 200.0f * pad.w;
        y = pad.b() - (value(kMorphY) + 100.0f) / 200.0f * pad.h;
    }
    void drawPad()
    {
        const Rect& r = padScreen;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "MORPH");
        char t[48];
        std::snprintf(t, sizeof t, "X %s   Y %s", formatParam(kMorphX, value(kMorphX)).c_str(), formatParam(kMorphY, value(kMorphY)).c_str());
        textAt(r.r() - 10, r.y + 14, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect& p = pad;
        fillRect(p, withAlpha2(theme.panel, 0.6f));
        line(p.cx(), p.y, p.cx(), p.b(), theme.grid, 1.0f);
        line(p.x, p.cy(), p.r(), p.cy(), theme.grid, 1.0f);
        float dx, dy;
        padDot(dx, dy);
        // the texture: long, few, aligned marks at the smooth/steady corner; short, many, scattered (and some
        // reversed) toward glitchy/wild. Marks near the dot light up.
        save();
        scissor(p.x, p.y, p.w, p.h);
        lineCap(ROUND);
        const int cols = 12, rows = 7;
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < cols; ++i)
            {
                const float u = (i + 0.5f) / cols, v = 1.0f - (j + 0.5f) / rows;
                const float cx = p.x + u * p.w, cy = p.y + (j + 0.5f) / rows * p.h;
                const int nd = 1 + (int)(u * 2.99f);
                const float len = (17.0f - 13.0f * u) / (float)nd;
                for (int k = 0; k < nd; ++k)
                {
                    const float jx = (hash01(i, j, k * 3) - 0.5f) * 14.0f * v, jy = (hash01(i, j, k * 3 + 1) - 0.5f) * 18.0f * v;
                    const float mx = cx + (k - 0.5f * (nd - 1)) * (len + 4.0f) + jx, my = cy + jy;
                    const float dd = ((mx - dx) * (mx - dx) + (my - dy) * (my - dy)) / (2.0f * 46.0f * 46.0f);
                    const float a = (theme.dark ? 0.16f : 0.20f) + 0.6f * std::exp(-dd);
                    const bool rev = hash01(i, j, k * 3 + 2) < v * 0.45f;
                    const Color c = rev ? theme.second : theme.ink;
                    line(mx - 0.5f * len, my, mx + 0.5f * len, my, withAlpha2(c, a), 2.2f);
                }
            }
        lineCap(BUTT);
        restore();
        // axis words
        auto tag = [&](float x, float y, const char* s, int align) {
            const float w = textWidth(s, kLabel, 9.5f, 1.2f) + 8.0f;
            const float bx = (align & ALIGN_RIGHT) ? x - w : ((align & ALIGN_CENTER) ? x - 0.5f * w : x);
            fillRect(Rect(bx, y - 7.0f, w, 14.0f), theme.screen);
            textAt(bx + 4.0f, y, s, kLabel, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.2f);
        };
        tag(p.x + 2, p.cy(), "SMOOTH", ALIGN_LEFT);
        tag(p.r() - 2, p.cy(), "GLITCHY", ALIGN_RIGHT);
        tag(p.cx(), p.y + 9, "WILD", ALIGN_CENTER);
        tag(p.cx(), p.b() - 9, "STEADY", ALIGN_CENTER);
        strokeRect(p, withAlpha2(theme.ink, 0.35f), 1.0f);
        // the dot
        line(dx, p.y, dx, p.b(), withAlpha2(theme.ink, 0.30f), 1.0f);
        line(p.x, dy, p.r(), dy, withAlpha2(theme.ink, 0.30f), 1.0f);
        beginPath(); circle(dx + 2.5f, dy + 2.5f, 9.0f); fillColor(theme.dim); fill();
        beginPath(); circle(dx, dy, 9.0f); fillColor(theme.neon); fill(); strokeColor(theme.ink); strokeWidth(1.5f); stroke();
        beginPath(); circle(dx, dy, 2.5f); fillColor(theme.ink); fill();

        // motion: what the three slow curves are doing right now
        const float my = p.b() + 15.0f;
        caption(r.x + 10, my, "MOTION");
        static const char* const names[3] = { "POS", "SIZE", "PITCH" };
        float bx = r.x + 92.0f;
        for (int m = 0; m < 3; ++m)
        {
            textAt(bx, my, names[m], kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
            const float lw = textWidth(names[m], kLabel, 9.0f, 0.8f) + 5.0f;
            const Rect b(bx + lw, my - 3.5f, 40.0f, 7.0f);
            fillRect(b, theme.panel2);
            const float v = pump::clampv(view.motion[m], -1.0f, 1.0f);
            const float x0 = b.cx(), x1 = b.cx() + v * b.w * 0.5f;
            fillRect(Rect(std::min(x0, x1), b.y, std::fabs(x1 - x0), b.h), theme.neon);
            line(b.cx(), b.y - 1, b.cx(), b.b() + 1, theme.ink, 1.0f);
            strokeRect(b, theme.ink, 1.0f);
            bx = b.r() + 12.0f;
        }
    }

    // ---- small displays ----
    void drawEnvelopePreview(const Rect& b)
    {
        const int shape = pump::clampv((int)value(kEnvelope), 0, kNumEnvelopes - 1);
        // at the current grain size (the 1.5 ms shortest edge shows on short grains)
        const double srShown = std::max(8000.0, sampleRate());
        const int len = std::max(16, (int)(value(kSize) * 0.001 * srShown));
        const EnvSpec es = envSpec(shape, len, srShown);
        const float* tab = edgeTable();
        beginPath();
        for (int i = 0; i <= 60; ++i)
        {
            const int k = std::min(len - 1, i * len / 60);
            float e = 1.0f;
            if (k < es.atk) e = edge(tab, (float)k / es.atk);
            if (len - k < es.rel) e *= edge(tab, (float)(len - k) / es.rel);
            if (es.body == kBodyDecay) { const float u = 1.0f - (float)k / len; e *= u * u; }
            else if (es.body == kBodyRise) { const float u = (float)k / len; e *= u * u; }
            if (i == 60) e = 0.0f;
            const float x = b.x + b.w * i / 60.0f, y = b.b() - e * b.h;
            if (i == 0) moveTo(x, b.b()); else lineTo(x, y);
        }
        lineTo(b.r(), b.b());
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.5f : 0.7f)); fill();
        strokeColor(theme.ink); strokeWidth(1.2f); stroke();
    }

    void drawScaleStrip()
    {
        const Rect& b = scaleStrip;
        const int sc = pump::clampv((int)value(kQuantize), 0, kNumScales - 1);
        static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
        for (int i = 0; i < 12; ++i)
        {
            const Rect c(b.x + b.w * i / 12.0f, b.y, b.w / 12.0f, b.h);
            const bool on = sc != kScaleOff && (kScaleMask[sc] & (1u << i));
            fillRect(c, on ? (i == 0 ? theme.ink : theme.neon) : (black[i] ? theme.panel2 : theme.bg));
            strokeRect(c, sc == kScaleOff ? theme.dim : theme.ink, 1.0f);
        }
        textAt(b.x, b.b() + 9, sc == kScaleOff ? "FREE PITCH" : "ROOT = SOURCE", kLabel, 8.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 0.8f);
    }

    void drawFilterCurve()
    {
        const Rect& b = filterR;
        fillRect(b, theme.screen);
        const int mode = pump::clampv((int)value(kFilter), 0, kNumFilterModes - 1);
        const double fc = value(kCutoff), q = 0.6 * std::pow(25.0, value(kResonance) * 0.01);
        line(b.x, b.y + b.h * 0.45f, b.r(), b.y + b.h * 0.45f, theme.grid, 1.0f);
        beginPath();
        for (int i = 0; i <= 40; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, i / 40.0), w = f / fc;
            // analog prototype of the SVF outputs (LP / HP gain-compensated by 1/sqrt(Q), BP at 0 dB)
            const double re = 1.0 - w * w, im = w / q, den = std::sqrt(re * re + im * im);
            double m = 1.0;
            if (mode == kFilterLP) m = 1.0 / den / std::sqrt(std::max(1.0, q));
            else if (mode == kFilterHP) m = w * w / den / std::sqrt(std::max(1.0, q));
            else if (mode == kFilterBP) m = (w / q) / den;
            const float db = (float)pump::clampv(20.0 * std::log10(std::max(1e-6, m)), -30.0, 12.0);
            const float x = b.x + b.w * i / 40.0f, y = b.y + b.h * 0.45f - db / 30.0f * b.h * 0.55f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(mode == kFilterOff ? theme.dim : theme.ink); strokeWidth(1.6f); lineJoin(ROUND); stroke();
        if (mode == kFilterOff) textAt(b.cx(), b.b() - 9, "BYPASSED", kLabel, 8.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);
        strokeRect(b, theme.ink, 1.0f);
    }

    // ---- interaction: the buffer (position + pitch) and the pad (morph X / Y) ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        const Rect bp = bufPlot();
        if (bp.contains(x, y))
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kPosition, spec(kPosition).def); setParamGesture(kPitch, spec(kPitch).def); endGroup(); return false; }
            drag = kDragBuffer; dragX = x; dragY = y; dragA = value(kPosition); dragB = value(kPitch);
            dragFine = (mod & kModifierShift) != 0;
            beginEdit(kPosition); beginEdit(kPitch);
            if (!(mod & kModifierShift)) bufferAt(x, y, mod);
            return true;
        }
        if (pad.contains(x, y))
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kMorphX, 0.0f); setParamGesture(kMorphY, 0.0f); endGroup(); return false; }
            drag = kDragPad; dragX = x; dragY = y; dragA = value(kMorphX); dragB = value(kMorphY);
            beginEdit(kMorphX); beginEdit(kMorphY);
            if (!(mod & kModifierShift)) padAt(x, y);
            return true;
        }
        return false;
    }
    // x places the grains (absolute: where you click); dragging up/down moves the pitch from where it was, so a
    // click alone never changes it
    void bufferAt(float x, float y, uint mod)
    {
        const Rect bp = bufPlot();
        const bool fine = (mod & kModifierShift) != 0;
        if (fine != dragFine)
        {
            // Shift pressed or released mid-drag: carry on from here (the pitch keeps its unrounded drag amount)
            dragB += (dragY - y) / (bp.h * 0.42f) * 24.0f * (dragFine ? 0.25f : 1.0f);
            dragA = value(kPosition); dragX = x; dragY = y; dragFine = fine;
        }
        if (fine) setParam(kPosition, dragA - (x - dragX) / bp.w * span * 1000.0f * 0.15f);
        else setParam(kPosition, (bp.r() - pump::clampv(x, bp.x, bp.r())) / bp.w * span * 1000.0f);
        setParam(kPitch, std::round(dragB + (dragY - y) / (bp.h * 0.42f) * 24.0f * (fine ? 0.25f : 1.0f)));
        setStatus(paramStatus(kPosition) + "    " + paramStatus(kPitch));
    }
    void padAt(float x, float y)
    {
        setParam(kMorphX, (pump::clampv(x, pad.x, pad.r()) - pad.x) / pad.w * 200.0f - 100.0f);
        setParam(kMorphY, (pad.b() - pump::clampv(y, pad.y, pad.b())) / pad.h * 200.0f - 100.0f);
        setStatus(paramStatus(kMorphX) + "    " + paramStatus(kMorphY));
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        const bool fine = (mod & kModifierShift) != 0;
        if (drag == kDragBuffer) bufferAt(x, y, mod);
        else if (drag == kDragPad)
        {
            if (!fine) { padAt(x, y); return; }
            setParam(kMorphX, dragA + (x - dragX) / pad.w * 200.0f * 0.15f);
            setParam(kMorphY, dragB - (y - dragY) / pad.h * 200.0f * 0.15f);
            dragA = value(kMorphX); dragB = value(kMorphY); dragX = x; dragY = y;
            setStatus(paramStatus(kMorphX) + "    " + paramStatus(kMorphY));
        }
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == kDragBuffer) { endEdit(kPitch); endEdit(kPosition); }
        else if (drag == kDragPad) { endEdit(kMorphY); endEdit(kMorphX); }
        drag = kDragNone;
    }
    void bodyHover(float x, float y) override
    {
        if (bufPlot().contains(x, y)) setStatus("Click or drag in the buffer: where the grains read (position); drag up/down = pitch (Shift = fine, double-click = reset, wheel = zoom)");
        else if (pad.contains(x, y)) setStatus("Drag the dot: X = smooth .. glitchy (size, density, scatter), Y = steady .. wild (spray, pitch spread, reverse, width, motion)");
    }
    bool bodyScroll(float x, float y, float dy, uint) override
    {
        if (!bufR.contains(x, y)) return false;
        spanSel = pump::clampv(spanSel + (dy > 0 ? -1 : 1), 0, kNumSpans - 1);
        saveUi();
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key == 'f' || key == 'F') { setParamGesture(kFreeze, value(kFreeze) >= 0.5f ? 0.0f : 1.0f); return true; }
        if (key >= '1' && key <= '5') { setParamGesture(kEnvelope, (float)(key - '1')); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Click / drag in the buffer", "position = where you point; drag up/down = pitch (double-click resets both)" },
            { "Wheel on the buffer", "zoom: auto, 1, 2, 4, 8 s" },
            { "Drag the morph pad", "X = smooth .. glitchy, Y = steady .. wild (double-click centres)" },
            { "F", "freeze" },
            { "1 - 5", "envelope: smooth, soft, percussive, swell, square" },
        };
    }
    std::string footerInfo() override
    {
        char mode[48], buf[160];
        const float dens = value(kDensity);
        if (value(kSync) >= 0.5f)
            std::snprintf(mode, sizeof mode, "SYNC %s AT %.0f BPM", kNoteNames[pump::clampv((int)value(kDivision), 0, kNumNotes - 1)], bpm);
        else std::snprintf(mode, sizeof mode, dens < 9.95f ? "%.1f GRAINS/S" : "%.0f GRAINS/S", dens);
        const int sc = pump::clampv((int)value(kQuantize), 0, kNumScales - 1);
        std::snprintf(buf, sizeof buf, "%s   %s%s%s%s   LATENCY 0", mode, kEnvelopeNames[pump::clampv((int)value(kEnvelope), 0, kNumEnvelopes - 1)],
                      sc != kScaleOff ? "   " : "", sc != kScaleOff ? kScaleNames[sc] : "", value(kFreeze) >= 0.5f ? "   FROZEN" : "");
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }
    void saveUi() { char buf[32]; std::snprintf(buf, sizeof buf, "span=%d;", spanSel); saveUiState(buf); }
    void loadUiState(const std::string& s) override { spanSel = pump::clampv(std::atoi(uiStateGet(s, "span", "0").c_str()), 0, kNumSpans - 1); }

private:
    static constexpr int kWaveN = 1 << 13, kGhosts = 512, kNumSpans = 5;
    static constexpr float kSpans[kNumSpans] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f };
    enum Drag { kDragNone, kDragBuffer, kDragPad };
    struct Ghost { float from = 0, to = 0, pitch = 0, life = 0; uint32_t head = 0; bool rev = false; };

    PluginGrain* plug = nullptr;
    Rect bufR, padScreen, pad, grainPanel, pitchPanel, fxPanel, outPanel, scaleStrip, filterR, meterL, meterR;
    GrainView view;
    Ghost ghosts[kGhosts];
    uint32_t ghostW = 0, endedSeen = 0;
    float wave[kWaveN] = {};
    int waveCount = 0, spanSel = 0;
    float hopSec = 0.00133f, span = 2.0f, waveScale = 0.1f, bpm = 120.0f;
    bool spanInit = false, playing = false;
    OutMeter outMeter;
    Drag drag = kDragNone;
    float dragX = 0, dragY = 0, dragA = 0, dragB = 0;
    bool dragFine = false;

    Dial *posDial, *sizeDial, *rateDial, *sprayDial, *revDial, *stereoDial, *pitchDial, *spreadDial;
    Dial *cutDial, *resDial, *fbDial, *spaceDial, *mixDial, *outDial;
    Segmented *envSeg, *filterSeg;
    MenuButton* quantMenu;
    Button *syncBtn, *freezeBtn;
    SliderRow *rateRow, *depthRow;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIGrain)
};

constexpr float UIGrain::kSpans[];

UI* createUI() { return new UIGrain(); }

END_NAMESPACE_DISTRHO
