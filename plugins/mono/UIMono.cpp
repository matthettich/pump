// pump MONO: interface. SCOPE (the output, two cycles of the note playing), FILTER (the model's response where it
// is now; drag it: left / right = cutoff, up / down = resonance) and NOTE (the note, glide, priority). Below: the
// three oscillators, the mixer, the filter; the two envelopes, the LFO and the playing controls; a keyboard.
#include "ui/PumpUI.hpp"
#include "ui/KeyboardUI.hpp"
#include "PluginMono.hpp"
#include <complex>

START_NAMESPACE_DISTRHO

using namespace pump::mono;

class UIMono : public PumpUI {
public:
    UIMono() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::mono(), "MONO", 1000, 740, -1)
    {
        const int wv[3] = { kOsc1Wave, kOsc2Wave, kOsc3Wave }, oc[3] = { kOsc1Oct, kOsc2Oct, kOsc3Oct }, lv[3] = { kOsc1Level, kOsc2Level, kOsc3Level };
        for (int o = 0; o < 3; ++o)
        {
            wave[o] = addSegmented(wv[o], { "TRI", "SAW", "SQR", "PLS" });
            wave[o]->tip = "Waveform: triangle, saw, square, pulse (PW in the mixer)";
            range[o] = addSegmented(oc[o], { "32", "16", "8", "4", "2" });
            range[o]->tip = "Range in feet: 8' = concert pitch, 16' an octave down";
            level[o] = addDial(lv[o], "LEVEL", Dial::Small);
            pitch[o] = o == 0 ? nullptr : addDial(o == 1 ? kOsc2Pitch : kOsc3Pitch, "PITCH", Dial::Small, true);
            if (pitch[o]) pitch[o]->tip = "Pitch in semitones (+-12, fine in between)";
        }
        syncBtn = addToggle(kSync, "SYNC 2>1");
        syncBtn->small = true;
        syncBtn->tip = "Hard sync: oscillator 2 restarts with 1 (sweep 2's pitch for the classic sync sound)";
        fmDial = addDial(kFm, "FM 3>1", Dial::Small);
        fmDial->tip = "Oscillator 3 modulates 1's frequency (bells, growls, metal)";
        pwDial = addDial(kPw, "PW", Dial::Small);
        noiseDial = addDial(kNoise, "NOISE", Dial::Small);
        driveDial = addDial(kOverdrive, "DRIVE", Dial::Small);
        driveDial->tip = "Overdrive: the mix hits the filter hot (soft clipping before it)";

        model = addSegmented(kFilterType, { "LADDER", "SVF", "DIODE", "SALLEN" });
        model->tip = "Ladder 24: round and fat · SVF 12: vocal, MODE sweeps LP / notch / HP · Diode 18: squelchy · Sallen 12: bright, screams";
        cutDial = addDial(kCutoff, "CUTOFF");
        resDial = addDial(kRes, "RES", Dial::Small);
        envDial = addDial(kFilterEnv, "ENV", Dial::Small, true);
        keyDial = addDial(kKeyTrack, "KEY", Dial::Small);
        modeDial = addDial(kFilterMode, "MODE", Dial::Small);
        modeDial->tip = "SVF only: low-pass -> notch -> high-pass";
        velfDial = addDial(kVelFilter, "VEL", Dial::Small);
        velfDial->tip = "Velocity to the filter envelope";

        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int k = 0; k < 4; ++k) { fD[k] = addDial(kFA + k, adsr[k], Dial::Small); aD[k] = addDial(kAA + k, adsr[k], Dial::Small); }
        velaDial = addDial(kVelAmp, "VEL", Dial::Small);
        lfoShape = addSegmented(kLfoShape, { "TRI", "SAW", "SQR", "S&H" });
        lfoRate = addDial(kLfoRate, "RATE", Dial::Small);
        lfoPitch = addDial(kLfoPitch, "PITCH", Dial::Small);
        lfoFilter = addDial(kLfoFilter, "FILTER", Dial::Small);
        lfoPw = addDial(kLfoPw, "PW", Dial::Small);
        wheelDial = addDial(kWheelVib, "WHEEL", Dial::Small);
        wheelDial->tip = "Mod wheel: vibrato depth (from the LFO)";
        glideDial = addDial(kGlide, "GLIDE", Dial::Small);
        glideSeg = addSegmented(kGlideMode, { "ALWAYS", "LEGATO" });
        glideSeg->tip = "Glide on every note, or only between overlapping notes";
        prioSeg = addSegmented(kPriority, { "LAST", "LOW", "HIGH" });
        prioSeg->tip = "Which held note sounds: the last pressed, the lowest or the highest";
        legBtn = addToggle(kLegato, "LEGATO");
        legBtn->small = true;
        legBtn->tip = "Legato: overlapping notes don't restart the envelopes";
        driftDial = addDial(kDrift, "DRIFT", Dial::Small);
        driftDial->tip = "Analog drift: oscillators and cutoff wander a little";
        bendDial = addDial(kBendRange, "BEND", Dial::Small);
        volDial = addDial(kVolume, "VOLUME", Dial::Small, true);

        keyboard.lowNote = 24;
        keyboard.send = [this](int note, int vel) { if (plug) plug->keys.push((uint8_t)note, (uint8_t)vel); };
        if (auto* p = static_cast<PluginMono*>(pluginPtr())) plug = p;
    }
    ~UIMono() override { keyboard.mouseUp(); }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        if (i == kOsc2Pitch || i == kOsc3Pitch) { std::snprintf(b, sizeof b, std::fabs(v - std::round(v)) < 0.005f ? "%+.0f st" : "%+.2f st", v); return std::fabs(v) < 0.005f ? std::string("0 st") : std::string(b); }
        if (i == kGlide && v < 0.5f) return "OFF";
        if (i == kBendRange) { std::snprintf(b, sizeof b, "%.0f st", v); return b; }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        const float top = 58, sh = 166;
        scopeR = Rect(14, top, 470, sh);
        filterR = Rect(496, top, 300, sh);
        noteR = Rect(808, top, 146, sh);
        meterR = Rect(966, top, 14, sh - 12);
        const float y1 = 238, ph = 196;
        oscPanel = Rect(14, y1, 470, ph);
        for (int o = 0; o < 3; ++o)
        {
            const float x = 24 + o * 154;
            oscCol[o] = Rect(x - 4, y1 + 22, 150, ph - 30);
            wave[o]->r = Rect(x, y1 + 30, 138, 22);
            range[o]->r = Rect(x, y1 + 60, 138, 22);
            if (pitch[o]) { pitch[o]->r = Rect(x, y1 + 92, 64, Dial::heightFor(Dial::Small)); level[o]->r = Rect(x + 72, y1 + 92, 64, Dial::heightFor(Dial::Small)); }
            else level[o]->r = Rect(x + 36, y1 + 92, 64, Dial::heightFor(Dial::Small));
        }
        mixPanel = Rect(496, y1, 180, ph);
        syncBtn->r = Rect(506, y1 + 28, 160, 22);
        fmDial->r = Rect(502, y1 + 56, 56, Dial::heightFor(Dial::Small));
        pwDial->r = Rect(558, y1 + 56, 56, Dial::heightFor(Dial::Small));
        noiseDial->r = Rect(614, y1 + 56, 56, Dial::heightFor(Dial::Small));
        driveDial->r = Rect(558, y1 + 124, 56, Dial::heightFor(Dial::Small));
        filterPanel = Rect(688, y1, 298, ph);
        model->r = Rect(698, y1 + 28, 278, 22);
        cutDial->r = Rect(700, y1 + 62, 76, Dial::heightFor(Dial::Medium));
        resDial->r = Rect(786, y1 + 56, 60, Dial::heightFor(Dial::Small));
        envDial->r = Rect(850, y1 + 56, 60, Dial::heightFor(Dial::Small));
        velfDial->r = Rect(914, y1 + 56, 60, Dial::heightFor(Dial::Small));
        keyDial->r = Rect(786, y1 + 124, 60, Dial::heightFor(Dial::Small));
        modeDial->r = Rect(850, y1 + 124, 60, Dial::heightFor(Dial::Small));

        const float y2 = 446, ph2 = 192;
        fenvPanel = Rect(14, y2, 200, ph2);
        for (int k = 0; k < 4; ++k) fD[k]->r = Rect(22 + k * 46, y2 + 112, 46, Dial::heightFor(Dial::Small));
        aenvPanel = Rect(226, y2, 236, ph2);
        for (int k = 0; k < 4; ++k) aD[k]->r = Rect(232 + k * 44, y2 + 112, 44, Dial::heightFor(Dial::Small));
        velaDial->r = Rect(408, y2 + 112, 48, Dial::heightFor(Dial::Small));
        lfoPanel = Rect(474, y2, 250, ph2);
        lfoShape->r = Rect(486, y2 + 28, 226, 22);
        lfoRate->r = Rect(480, y2 + 112, 48, Dial::heightFor(Dial::Small));
        lfoPitch->r = Rect(528, y2 + 112, 48, Dial::heightFor(Dial::Small));
        lfoFilter->r = Rect(576, y2 + 112, 48, Dial::heightFor(Dial::Small));
        lfoPw->r = Rect(624, y2 + 112, 48, Dial::heightFor(Dial::Small));
        wheelDial->r = Rect(672, y2 + 112, 48, Dial::heightFor(Dial::Small));
        playPanel = Rect(736, y2, 250, ph2);
        glideSeg->r = Rect(746, y2 + 28, 132, 22);
        legBtn->r = Rect(886, y2 + 28, 90, 22);
        prioSeg->r = Rect(746, y2 + 70, 230, 22);
        glideDial->r = Rect(742, y2 + 112, 58, Dial::heightFor(Dial::Small));
        driftDial->r = Rect(802, y2 + 112, 58, Dial::heightFor(Dial::Small));
        bendDial->r = Rect(862, y2 + 112, 58, Dial::heightFor(Dial::Small));
        volDial->r = Rect(922, y2 + 112, 58, Dial::heightFor(Dial::Small));
        keyboard.r = Rect(14, 650, 966, 56);
        keyboard.octaves = 5;
    }

    void tick(float dt) override
    {
        modeDial->enabled = (int)value(kFilterType) == kSvf;
        pwDial->enabled = (int)value(kOsc1Wave) == kPulse || (int)value(kOsc2Wave) == kPulse || (int)value(kOsc3Wave) == kPulse;
        lfoPw->enabled = pwDial->enabled;
        if (!plug) return;
        const float pk = pump::gainToDbF(plug->outPeak.exchange(0.0f));
        lvl = std::max(pk, lvl - 26.0f * dt);
        if (pk >= hold) { hold = pk; holdT = 1.5f; } else if ((holdT -= dt) <= 0.0f) hold = std::max(pk, hold - 18.0f * dt);
        const uint32_t end = plug->engine.outRing.smoothPosition();
        for (int i = 0; i < kScopeN; ++i) scope[i] = plug->engine.outRing.at(end - (uint32_t)kScopeN + (uint32_t)i);
        cutShow += (std::log2(std::max(15.0f, plug->engine.cutoffNow())) - cutShow) * std::min(1.0f, dt * 25.0f);
        pitchShow = plug->engine.pitchNow();
        gate = plug->engine.gateNow();
        for (int n = 0; n < 128; ++n) keyboard.lit[n] = plug->engine.noteLit(n);
    }

    void drawBody() override
    {
        drawScope();
        drawFilter();
        drawNote();
        drawLevelMeter(meterR, lvl, hold, -48.0f, 6.0f);
        auto panel = [&](const Rect& r, const char* c) { strokeRect(r, theme.dim, 1.0f); caption(r.x + 12, r.y + 16, c); };
        panel(oscPanel, "OSCILLATORS"); panel(mixPanel, "MIXER"); panel(filterPanel, "FILTER");
        panel(fenvPanel, "FILTER ENVELOPE"); panel(aenvPanel, "AMP ENVELOPE"); panel(lfoPanel, "LFO"); panel(playPanel, "PLAY");
        for (int o = 0; o < 3; ++o)
        {
            char b[4]; std::snprintf(b, sizeof b, "%d", o + 1);
            textAt(oscCol[o].r() - 10, oscPanel.y + 16, b, kBold, 12.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        caption(prioSeg->r.x, prioSeg->r.y - 10, "PRIORITY");
        drawEnv(Rect(fenvPanel.x + 12, fenvPanel.y + 28, fenvPanel.w - 24, 76), kFA, plug ? plug->engine.filterEnvNow() : 0.0f);
        drawEnv(Rect(aenvPanel.x + 12, aenvPanel.y + 28, aenvPanel.w - 24, 76), kAA, plug ? plug->engine.ampNow() : 0.0f);
        // LFO picture
        const Rect lp(lfoPanel.x + 12, lfoPanel.y + 58, lfoPanel.w - 24, 46);
        fillRect(lp, theme.screen); strokeRect(lp, theme.ink, 1.0f);
        beginPath();
        pump::fx::Rng rr(5); double held = rr.bi();
        for (int i = 0; i <= 80; ++i)
        {
            const double p = 2.0 * i / 80.0, f = p - std::floor(p);
            if (i % 20 == 0) held = rr.bi();
            double v;
            switch ((int)value(kLfoShape)) { case kLfoSaw: v = 2 * f - 1; break; case kLfoSquare: v = f < 0.5 ? 1 : -1; break; case kLfoSh: v = held; break; default: v = 1 - 4 * std::fabs(f - 0.5); }
            const float x = lp.x + 4 + (lp.w - 8) * i / 80.0f, y = lp.cy() - (float)v * (lp.h * 0.4f);
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.6f); stroke();
        keyboard.draw(*this);
    }

    void drawScope()
    {
        const Rect& r = scopeR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "SCOPE");
        const Rect p(r.x + 10, r.y + 26, r.w - 20, r.h - 34);
        line(p.x, p.cy(), p.r(), p.cy(), withAlpha2(theme.ink, 0.25f), 1.0f);
        if (!plug) return;
        // two cycles of the note playing, triggered on a rising zero crossing
        const double sr = plug->getSampleRate();
        const int cyc = pump::clampv((int)(sr / pump::synth::midiToHz(pump::clampv((double)pitchShow, 12.0, 120.0))), 8, kScopeN / 4);
        const int win = std::min(kScopeN / 2, 2 * cyc);
        int start = kScopeN - win - 1;
        for (int i = start; i > start - 2 * cyc && i > 1; --i) if (scope[i - 1] < 0.0f && scope[i] >= 0.0f) { start = i; break; }
        float mx = 1e-3f; for (int i = 0; i < win; ++i) mx = std::max(mx, std::fabs(scope[start + i]));
        const float want = 0.9f / mx;
        scale = want < scale ? want : scale + (want - scale) * 0.05f;
        save(); scissor(p.x, p.y, p.w, p.h);
        beginPath();
        for (int i = 0; i < win; ++i)
        {
            const float x = p.x + p.w * i / (win - 1), y = p.cy() - scope[start + i] * scale * p.h * 0.48f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(pump::mixColor(theme.neon, theme.ink, theme.dark ? 0.0f : 0.45f)); strokeWidth(2.2f); lineJoin(ROUND); stroke();
        restore();
    }

    void drawFilter()
    {
        const Rect& r = filterR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "FILTER");
        const int t = pump::clampv((int)value(kFilterType), 0, kNumFilters - 1);
        textAt(r.r() - 10, r.y + 14, kFilterNames[t], kLabel, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 0.8f);
        const Rect p(r.x + 8, r.y + 26, r.w - 16, r.h - 34);
        drawFreqGrid(p, 20.0, 20000.0, false);
        const double fc = std::exp2((double)cutShow), res = value(kRes) * 0.01, m = value(kFilterMode) * 0.01;
        auto yOf = [&](double db) { return (float)(p.y + (24.0 - pump::clampv(db, -48.0, 24.0)) / 72.0 * p.h); };
        line(p.x, yOf(0), p.r(), yOf(0), withAlpha2(theme.ink, 0.25f), 1.0f);
        beginPath();
        for (int i = 0; i <= 140; ++i)
        {
            const double f = 20.0 * std::pow(1000.0, i / 140.0);
            const std::complex<double> s(0.0, f / fc);
            std::complex<double> H;
            if (t == kLadder) { const auto g = 1.0 / (1.0 + s); const double k = 5.2 * res; H = g * g * g * g * (1.0 + 0.5 * k) / (1.0 + k * g * g * g * g); }
            else if (t == kDiode)
            {
                const std::complex<double> s3 = s * 1.7320508;
                const auto g = 1.0 / (1.0 + s3); const double k = 10.5 * res;
                const std::complex<double> sh = s * (fc / 120.0);   // the 120 Hz high-pass in the resonance loop
                const std::complex<double> hp = sh / (1.0 + sh);
                H = g * g * g * (1.0 + 0.3 * k / 10.5) / (1.0 + k * hp * g * g * g);
            }
            else
            {
                const double q = t == kSvf ? 0.5 + 9.5 * res * res : 1.0 / std::max(0.05, 2.0 - 2.15 * res);
                const auto den = s * s + s / q + 1.0;
                const auto lp = 1.0 / den, hp = s * s / den;
                if (t == kSallen) H = lp;
                else H = m < 0.5 ? lp * (1.0 - 2 * m) + (lp + hp) * (2 * m) : (lp + hp) * (1.0 - (2 * m - 1)) + hp * (2 * m - 1);
            }
            const float x = p.x + p.w * i / 140.0f, y = yOf(20.0 * std::log10(std::max(1e-9, std::abs(H))));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(2.0f); lineJoin(ROUND); stroke();
        const float cx = freqToX(p, pump::clampv(fc, 20.0, 20000.0));
        line(cx, p.y, cx, p.b(), withAlpha2(theme.neon, 0.9f), 1.5f);
        char b[32];
        if (fc >= 1000) std::snprintf(b, sizeof b, "%.2f kHz", fc / 1000); else std::snprintf(b, sizeof b, "%.0f Hz", fc);
        textAt(r.x + 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(b, sizeof b, "RES %.0f%%", value(kRes));
        textAt(r.r() - 10, r.b() - 8, b, kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawNote()
    {
        const Rect& r = noteR;
        screenBox(r);
        caption(r.x + 10, r.y + 14, "NOTE");
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (int)std::lround(pitchShow);
        char b[24]; std::snprintf(b, sizeof b, "%s%d", nn[((n % 12) + 12) % 12], n / 12 - 1);
        const Rect big(r.x + 10, r.y + 28, r.w - 20, 54);
        fillRect(big, gate ? theme.neon : theme.panel);
        strokeRect(big, theme.ink, 1.5f);
        textAt(big.cx(), big.cy(), b, kBold, 24.0f, gate ? theme.neonInk : theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        const double cents = (pitchShow - n) * 100.0;
        std::snprintf(b, sizeof b, "%+.0f CENTS", cents);
        textAt(r.cx(), big.b() + 14, std::fabs(cents) < 0.5 ? std::string("IN TUNE") : std::string(b), kMono, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
        static const char* const pn[3] = { "LAST", "LOW", "HIGH" };
        std::snprintf(b, sizeof b, "%s NOTE", pn[pump::clampv((int)value(kPriority), 0, 2)]);
        textAt(r.cx(), r.y + 120, b, kLabel, 10.0f, theme.ink, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        textAt(r.cx(), r.y + 138, value(kLegato) >= 0.5f ? "LEGATO" : "RETRIGGER", kLabel, 9.5f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
        std::snprintf(b, sizeof b, "LATENCY %d", plug ? plug->engine.latency() : 0);
        textAt(r.cx(), r.b() - 10, b, kMono, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE);
    }
    void drawEnv(const Rect& r, int base, float now)
    {
        fillRect(r, theme.screen);
        strokeRect(r, theme.ink, 1.0f);
        auto tx = [](float ms) { return std::log10(1.0f + ms) / std::log10(1.0f + 10000.0f); };
        const float a = tx(value(base)), d = tx(value(base + 1)), s = value(base + 2) * 0.01f, rr = tx(value(base + 3));
        const float tot = a + d + 0.35f + rr;
        const Rect p = r.inset(4);
        auto X = [&](float t) { return p.x + p.w * t / tot; };
        auto Y = [&](float v) { return p.b() - v * p.h; };
        beginPath();
        moveTo(X(0), Y(0)); lineTo(X(a), Y(1));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d * t), Y(s + (1 - s) * std::pow(1e-4f, t))); }
        lineTo(X(a + d + 0.35f), Y(s));
        for (int i = 1; i <= 16; ++i) { const float t = (float)i / 16; lineTo(X(a + d + 0.35f + rr * t), Y(s * std::pow(1e-4f, t))); }
        strokeColor(theme.ink); strokeWidth(1.8f); lineJoin(ROUND); stroke();
        const float h = r.h * pump::clampv(now, 0.0f, 1.0f);
        fillRect(Rect(r.r() - 6, r.b() - h, 4, h), theme.neon);
    }

    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (keyboard.mouseDown(x, y)) { kbDown = true; return true; }
        if (filterR.contains(x, y))
        {
            if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(kCutoff, spec(kCutoff).def); setParamGesture(kRes, spec(kRes).def); endGroup(); return false; }
            dragging = true; dx = x; dy = y; dCut = norm(kCutoff); dRes = norm(kRes);
            beginEdit(kCutoff); beginEdit(kRes);
            return true;
        }
        return false;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (kbDown) { keyboard.mouseDrag(x, y); return; }
        if (!dragging) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        setParamNorm(kCutoff, dCut + (x - dx) / filterR.w * f);
        setParamNorm(kRes, dRes + (dy - y) / filterR.h * f);
        setStatus(paramStatus(kCutoff) + "    " + paramStatus(kRes));
    }
    void bodyMouseUp(float, float) override
    {
        if (kbDown) { keyboard.mouseUp(); kbDown = false; return; }
        if (dragging) { dragging = false; endEdit(kRes); endEdit(kCutoff); }
    }
    void bodyHover(float x, float y) override
    {
        if (filterR.contains(x, y)) setStatus("Drag: left / right = cutoff, up / down = resonance (double-click = reset)");
        else if (keyboard.r.contains(x, y)) setStatus("Play: velocity follows where you strike the key; drag across keys");
        else if (scopeR.contains(x, y)) setStatus("The output: two cycles of the note playing");
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return { { "Drag the filter screen", "left / right = cutoff, up / down = resonance" }, { "Keyboard", "click to play, drag across" } };
    }
    std::string footerInfo() override
    {
        std::string s = kFilterNames[pump::clampv((int)value(kFilterType), 0, kNumFilters - 1)];
        s += value(kLegato) >= 0.5f ? "   LEGATO" : "   RETRIGGER";
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

private:
    static constexpr int kScopeN = 4096;
    PluginMono* plug = nullptr;
    Rect scopeR, filterR, noteR, meterR, oscPanel, mixPanel, filterPanel, fenvPanel, aenvPanel, lfoPanel, playPanel, oscCol[3];
    Segmented *wave[3], *range[3], *model, *lfoShape, *glideSeg, *prioSeg;
    Dial *level[3], *pitch[3];
    Button *syncBtn, *legBtn;
    Dial *fmDial, *pwDial, *noiseDial, *driveDial, *cutDial, *resDial, *envDial, *keyDial, *modeDial, *velfDial, *velaDial;
    Dial *fD[4], *aD[4];
    Dial *lfoRate, *lfoPitch, *lfoFilter, *lfoPw, *wheelDial, *glideDial, *driftDial, *bendDial, *volDial;
    KeyboardUI keyboard;
    bool kbDown = false, dragging = false, gate = false;
    float dx = 0, dy = 0, dCut = 0, dRes = 0;
    float scope[kScopeN] = {}, scale = 1.0f, cutShow = 9.6f, pitchShow = 60.0f;
    float lvl = -120.0f, hold = -120.0f, holdT = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIMono)
};

UI* createUI() { return new UIMono(); }

END_NAMESPACE_DISTRHO
