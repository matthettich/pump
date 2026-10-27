// pump SPRING: interface. The tank: the springs drawn between their transducers, every coil moving with the wave that
// is really travelling along that spring (the forward and backward waves from the engine, delayed along the length;
// drag it: left/right = decay, up/down = dwell). The chirps: when each frequency of each echo arrives, from the
// current design (the drip's rising chirps below the transition, the high path's falling ones above it, fading with
// the decay; the digital models show their few, barely bent echoes and the converter's Nyquist; drag: up/down =
// tension, left/right = drip). The KICK key crashes the tank. Below: the tank (model, dwell, decay, tension, drip,
// wobble, grit), tone (the wet path's EQ curve, tone, low cut, pre-delay) and output (width, mix, output).
#include "ui/PumpUI.hpp"
#include "PluginSpring.hpp"

START_NAMESPACE_DISTRHO

using namespace pump::spring;

class UISpring : public PumpUI {
public:
    UISpring() : PumpUI(params().data(), kNumParams, &presets(), pump::accents::spring(), "SPRING", 1000, 620, kBypass)
    {
        modelSeg = addSegmented(kModel, { "TWO", "THREE", "SHORT", "LONG", "DIGITAL", "LO-FI" });
        modelSeg->tip = "Model: two-spring, three-spring, short or long tank (dispersive springs) · Digital, Lo-fi digital (a cheap early-digital spring)   (keys 1-6)";
        dwellDial = addDial(kDwell, "DWELL", Dial::Large);
        dwellDial->tip = "Dwell: how hard the driver hits the springs (more drip, more grit), level-compensated (or drag the tank up/down)";
        decayDial = addDial(kDecay, "DECAY");
        decayDial->tip = "Decay: reverb time (T60), 0.3..8 s (or drag the tank left/right)";
        tensionDial = addDial(kTension, "TENSION", Dial::Medium, true);
        tensionDial->tip = "Tension: tighter = higher transition, shorter springs, shorter chirps; looser = lower, longer, more boing (or drag the chirps up/down)";
        dripDial = addDial(kDrip, "DRIP");
        dripDial->tip = "Drip: the transient chirp (dispersion, transient drive and the chirp band's peak). Digital: flutter and chirp";
        wobbleDial = addDial(kWobble, "WOBBLE");
        wobbleDial->tip = "Wobble: slow random movement of the springs (a tank being nudged). Digital: the flutter tap jumps (zipper)";
        gritDial = addDial(kGrit, "GRIT");
        gritDial->tip = "Grit (digital models): converter clock and word length, from slightly cheap to broken (Digital 32 -> 8 kHz, 16 -> 8 bits; Lo-fi 20 -> 6 kHz, 12 -> 6 bits)";
        dwellDial->tabGroup = decayDial->tabGroup = tensionDial->tabGroup = dripDial->tabGroup = wobbleDial->tabGroup = gritDial->tabGroup = 1;

        toneDial = addDial(kTone, "TONE", Dial::Small, true);
        toneDial->tip = "Tone of the reverb: dark (2 kHz) .. bright (open, +6 dB shelf)";
        lowDial = addDial(kLowCut, "LOW CUT", Dial::Small);
        lowDial->tip = "Low cut on the reverb (12 dB/oct; keeps the tank out of the bass)";
        preDial = addDial(kPreDelay, "PRE-DELAY", Dial::Small);
        preDial->tip = "Pre-delay before the tank, 0..250 ms";
        toneDial->tabGroup = lowDial->tabGroup = preDial->tabGroup = 2;

        widthRow = addRow(kWidth, "WIDTH");
        widthRow->labelW = 58; widthRow->valueW = 44;
        widthRow->tip = "Width of the reverb (0 = mono; the springs are panned across the stereo field)";
        mixDial = addDial(kMix, "MIX");
        mixDial->tip = "Mix: the dry signal stays at full level up to 50 %, the reverb reaches full level at 50 %";
        outDial = addDial(kOutGain, "OUTPUT", Dial::Medium, true);
        outDial->tip = "Output gain";
        mixDial->tabGroup = outDial->tabGroup = 3;

        if (auto* p = static_cast<PluginSpring*>(pluginPtr())) plug = p;
    }

protected:
    std::string formatParam(int i, float v) const override
    {
        char b[32];
        switch (i)
        {
        case kLowCut: if (!lowCutActive(v)) return "OFF"; break;
        case kTension: case kTone:
            if (std::fabs(v) < 0.5f) return "0%";
            std::snprintf(b, sizeof b, "%+.0f%%", v); return b;
        case kDecay: std::snprintf(b, sizeof b, v < 0.995f ? "%.2f s" : "%.1f s", v); return b;
        case kPreDelay: std::snprintf(b, sizeof b, v < 9.95f ? "%.1f ms" : "%.0f ms", v); return b;
        default: break;
        }
        return PumpUI::formatParam(i, v);
    }

    void layout() override
    {
        tankR = Rect(14, 58, 590, 322);
        chirpR = Rect(620, 58, 330, 206);
        kickR = Rect(620, 282, 330, 94);
        meterL = Rect(962, 58, 9, 304);
        meterR = Rect(973, 58, 9, 304);
        const float y0 = 396;
        tankPanel = Rect(14, y0, 500, 192);
        modelSeg->r = Rect(26, y0 + 30, 476, 28);
        dwellDial->r = Rect(24, y0 + 74, 92, Dial::heightFor(Dial::Large));
        const float dy = y0 + 82;
        decayDial->r = Rect(124, dy, 72, Dial::heightFor(Dial::Medium));
        tensionDial->r = Rect(200, dy, 72, Dial::heightFor(Dial::Medium));
        dripDial->r = Rect(276, dy, 72, Dial::heightFor(Dial::Medium));
        wobbleDial->r = Rect(352, dy, 72, Dial::heightFor(Dial::Medium));
        gritDial->r = Rect(434, dy, 72, Dial::heightFor(Dial::Medium));

        tonePanel = Rect(528, y0, 226, 192);
        eqR = Rect(540, y0 + 28, 202, 62);
        toneDial->r = Rect(540, y0 + 104, 64, Dial::heightFor(Dial::Small));
        lowDial->r = Rect(609, y0 + 104, 64, Dial::heightFor(Dial::Small));
        preDial->r = Rect(678, y0 + 104, 64, Dial::heightFor(Dial::Small));

        outPanel = Rect(768, y0, 218, 192);
        widthRow->r = Rect(780, y0 + 36, 194, 24);
        mixDial->r = Rect(786, y0 + 80, 72, Dial::heightFor(Dial::Medium));
        outDial->r = Rect(890, y0 + 80, 72, Dial::heightFor(Dial::Medium));
    }

    int model() const { return pump::clampv((int)value(kModel), 0, kNumModels - 1); }
    double rate() const { return plug && plug->getSampleRate() > 0.0 ? plug->getSampleRate() : 48000.0; }

    void tick(float dt) override
    {
        gritDial->enabled = isDigital(model());
        if (!plug) return;
        for (int c = 0; c < 2; ++c)
        {
            const float pk = pump::gainToDbF(plug->outPeak[c].exchange(0.0f));
            outMeter.tickDb(c, pk, dt);
        }
        for (int l = 0; l < kLanes; ++l)
        {
            const float v = plug->engine.laneLevel[l].exchange(0.0f);
            energy[l] = std::max(v, energy[l] * std::exp(-dt / 0.25f));
            plug->engine.visF[l].readSmooth(waveF[l], kWaveN);
            plug->engine.visB[l].readSmooth(waveB[l], kWaveN);
        }
        const float d = plug->engine.driverLevel.exchange(0.0f);
        drvShow = std::max(d, drvShow * std::exp(-dt / 0.2f));
        kickAgeS = (float)(plug->engine.kickAge.load(std::memory_order_relaxed) / rate());
        if (kickHeld) kickPress = 1.0f; else kickPress = std::max(0.0f, kickPress - dt * 8.0f);
    }

    void drawBody() override
    {
        drawTank();
        drawChirps();
        drawKick();
        drawOutMeter(outMeter, meterL, meterR);
        textAt(meterL.r() + 1, tankR.b() - 7, "OUT", kLabel, 9.0f, theme.ink2, ALIGN_CENTER | ALIGN_MIDDLE, 0.8f);

        strokeRect(tankPanel, theme.dim, 1.0f);
        strokeRect(tonePanel, theme.dim, 1.0f);
        strokeRect(outPanel, theme.dim, 1.0f);
        caption(tankPanel.x + 12, tankPanel.y + 16, "TANK");
        caption(tonePanel.x + 12, tonePanel.y + 16, "TONE");
        caption(outPanel.x + 12, outPanel.y + 16, "OUTPUT");
        // GRIT belongs to the digital models: a divider and a note
        const float gx = gritDial->r.x - 4.0f;
        line(gx, gritDial->r.y + 2, gx, gritDial->r.b() - 4, theme.dim, 1.0f);
        drawEq();
    }

    // ---- the tank ----
    // the displacement shown for a sample value: log-compressed, so a quiet tail still visibly moves
    static float shown(float v)
    {
        const float a = std::log10(1.0f + 1000.0f * std::fabs(v)) / 3.0f;
        return std::copysign(std::min(1.0f, a), v);
    }
    void drawTank()
    {
        const Rect& r = tankR;
        screenBox(r);
        const int m = model();
        const bool digital = isDigital(m);
        caption(r.x + 10, r.y + 14, digital ? "CONVERTER" : "TANK");
        textAt(r.r() - 10, r.y + 14, kModelNames[m], kLabel, 11.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
        const double sr = rate();
        // a kick shakes the drawing for a moment
        float sx = 0.0f, sy = 0.0f;
        if (kickAgeS < 1.2f)
        {
            const float e = std::exp(-kickAgeS / 0.18f) * 7.0f;
            sx = e * std::sin(kickAgeS * 71.0f);
            sy = e * 0.6f * std::sin(kickAgeS * 53.0f + 1.0f);
        }
        const Rect pan(r.x + 14, r.y + 30, r.w - 28, r.h - 66);
        save();
        scissor(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        translate(sx, sy);
        strokeRect(pan, theme.dim, 1.0f);
        // the transducers (digital: the converters)
        const float tw = 46.0f, th = std::min(pan.h - 40.0f, 150.0f);
        const Rect inT(pan.x + 14, pan.cy() - th * 0.5f, tw, th), outT(pan.r() - 14 - tw, pan.cy() - th * 0.5f, tw, th);
        const float outE = pump::clampv((std::max(outMeter.lvl[0], outMeter.lvl[1]) + 48.0f) / 48.0f, 0.0f, 1.0f);
        drawTransducer(inT, pump::clampv(shown(drvShow), 0.0f, 1.0f), digital ? "ADC" : "IN", digital);
        drawTransducer(outT, outE, digital ? "DAC" : "OUT", digital);
        // the springs
        const int n = digital ? digitalModel(m).combs : tankModel(m).springs;
        const float x0 = inT.r() + 18.0f, x1 = outT.x - 18.0f;
        const float gap = std::min(78.0f, (pan.h - 46.0f) / std::max(1, n));
        const Design d = design(digital ? kTwoSpring : m, value(kTension), value(kDrip), sr);
        double clock = digital ? clockHz(m, value(kGrit)) : sr;
        for (int l = 0; l < n; ++l)
        {
            const float yc = pan.cy() + gap * ((float)l - 0.5f * (float)(n - 1));
            // travel time (vis samples) and coil count from the spring's length
            double travel;
            if (digital) travel = digitalModel(m).comb[l] * digitalScale(value(kTension));
            else travel = d.travel[l];
            const double visRate = plug ? plug->engine.visRate() : 12000.0;
            const float tS = (float)std::min((double)kWaveN - 4.0, travel * visRate);
            const int coils = (int)std::lround(travel * 1000.0 * (digital ? 1.4 : 2.0));
            // the hooks
            line(inT.r(), inT.cy() + (yc - inT.cy()) * 0.35f, x0, yc, theme.ink2, 1.2f);
            line(outT.x, outT.cy() + (yc - outT.cy()) * 0.35f, x1, yc, theme.ink2, 1.2f);
            drawCoil(l, x0, x1, yc, coils, tS, digital, clock);
            // the spring's name and length
            char t[24];
            std::snprintf(t, sizeof t, "%.1f ms", travel * 1000.0);
            textAt(x1, yc - 22.0f, t, kMono, 9.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        restore();
        // the read-out under the pan
        char t[96];
        if (digital)
            std::snprintf(t, sizeof t, "CLOCK %.1f kHz   %.0f BIT   %d COMBS", clock / 1000.0, std::round(wordBits(m, value(kGrit))), digitalModel(m).combs);
        else
            std::snprintf(t, sizeof t, "%d SPRINGS   TRANSITION %.2f kHz   DWELL %.0f%%", d.springs, d.fc / 1000.0, value(kDwell));
        textAt(r.x + 10, r.b() - 12, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "DECAY %s", formatParam(kDecay, value(kDecay)).c_str());
        textAt(r.r() - 10, r.b() - 12, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void drawTransducer(const Rect& b, float e, const char* name, bool digital)
    {
        fillRect(Rect(b.x + 3, b.y + 3, b.w, b.h), theme.bg);
        strokeRect(Rect(b.x + 3, b.y + 3, b.w, b.h), theme.dim, 1.2f);
        fillRect(b, theme.bg);
        fillRect(b, withAlpha2(theme.neon, 0.15f + 0.85f * e));
        strokeRect(b, theme.ink, 1.5f);
        if (digital)
        {
            // a chip: pins along the sides
            for (int k = 0; k < 7; ++k)
            {
                const float py = b.y + 12.0f + (b.h - 24.0f) * k / 6.0f;
                line(b.x - 5, py, b.x, py, theme.ink, 1.5f);
                line(b.r(), py, b.r() + 5, py, theme.ink, 1.5f);
            }
        }
        else
        {
            // a magnet in a coil
            const Rect mg(b.cx() - 7, b.y + 16, 14, b.h - 46);
            fillRect(mg, theme.ink);
            for (float yy = mg.y + 6; yy < mg.b() - 3; yy += 7) line(mg.x - 6, yy, mg.r() + 6, yy + 3, theme.ink2, 1.2f);
        }
        // the label reads on whatever the fill is (dark ink on a lit transducer, the ink colour on an idle one)
        const Color lc = theme.dark ? (e > 0.45f ? theme.neonInk : theme.ink) : theme.neonInk;
        textAt(b.cx(), b.b() - 13, name, kLabel, 10.0f, lc, ALIGN_CENTER | ALIGN_MIDDLE, 1.0f);
    }
    // One spring as a helix seen from the side: the coils bunch and spread with the longitudinal wave and the axis
    // sways with it (back strands thin and faint, front strands in ink). Digital: the same, snapped to a pixel grid.
    void drawCoil(int l, float x0, float x1, float yc, int coils, float tS, bool digital, double clock)
    {
        const int M = std::max(32, coils * 12);
        const float R = 11.0f, len = x1 - x0;
        const float e = pump::clampv(shown(energy[l]), 0.0f, 1.0f);
        // the spring's sleeve lights up as it rings
        beginPath();
        roundedRect(x0 - 4.0f, yc - R - 4.0f, len + 8.0f, 2.0f * R + 8.0f, R + 4.0f);
        fillColor(withAlpha2(theme.neon, (theme.dark ? 0.05f : 0.10f) + (theme.dark ? 0.35f : 0.60f) * e));
        fill();
        const float grid = digital ? (float)(2.0 + 5.0 * (1.0 - std::min(1.0, (clock - 6000.0) / 26000.0))) : 0.0f;
        auto wave = [&](float u) {
            const float pf = (float)(kWaveN - 1) - u * tS, pb = (float)(kWaveN - 1) - (1.0f - u) * tS;
            auto at = [](const float* w, float p) {
                const int i = pump::clampv((int)p, 0, kWaveN - 2);
                const float f = p - (float)i;
                return w[i] + (w[i + 1] - w[i]) * f;
            };
            return shown(at(waveF[l], pf) + at(waveB[l], pb));
        };
        float px[1200], py[1200];
        bool front[1200];
        const int pts = std::min(M, 1199);
        for (int i = 0; i <= pts; ++i)
        {
            const float u = (float)i / (float)pts;
            const float w = (i == 0 || i == pts) ? 0.0f : wave(u);
            const float th = (float)pump::kTwoPi * (float)coils * u;
            const float taper = std::min(1.0f, std::min(u, 1.0f - u) * 30.0f);
            float x = x0 + (u + 0.016f * w * taper) * len + R * 0.45f * std::cos(th) * taper;
            float y = yc + R * (1.0f + 0.25f * std::fabs(w)) * std::sin(th) * taper + 4.0f * w * taper;
            if (grid > 0.0f) { x = std::round(x / grid) * grid; y = std::round(y / grid) * grid; }
            px[i] = x; py[i] = y;
            front[i] = std::cos(th) >= 0.0f;
        }
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            bool open = false;
            for (int i = 1; i <= pts; ++i)
            {
                const bool f = front[i - 1];
                if (f != (pass == 1)) { open = false; continue; }
                if (!open) { moveTo(px[i - 1], py[i - 1]); open = true; }
                lineTo(px[i], py[i]);
            }
            strokeColor(pass ? theme.ink : withAlpha2(theme.ink, 0.32f));
            strokeWidth(pass ? 1.7f : 1.0f);
            lineJoin(digital ? MITER : ROUND);
            stroke();
        }
    }

    // ---- the chirps ----
    void drawChirps()
    {
        const Rect& r = chirpR;
        screenBox(r);
        const int m = model();
        const bool digital = isDigital(m);
        caption(r.x + 10, r.y + 14, digital ? "ECHOES" : "CHIRPS");
        textAt(r.r() - 10, r.y + 14, "kHz / ms", kMono, 9.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        const Rect p(r.x + 34, r.y + 28, r.w - 44, r.h - 64);
        const double sr = rate();
        const double fTop = digital ? 16000.0 : 8000.0, tView = 0.200;
        auto X = [&](double t) { return p.x + (float)(t / tView) * p.w; };
        auto Y = [&](double f) { return p.b() - (float)(f / fTop) * p.h; };
        for (int k = 0; k <= 4; ++k)
        {
            if (k > 0 && k < 4) line(X(0.05 * k), p.y, X(0.05 * k), p.b(), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%d", 50 * k);
            textAt(X(0.05 * k), p.b() + 8, b, kMono, 8.5f, theme.ink2, (k == 0 ? ALIGN_LEFT : (k == 4 ? ALIGN_RIGHT : ALIGN_CENTER)) | ALIGN_MIDDLE);
        }
        const double step = digital ? 4000.0 : 2000.0;
        for (double f = step; f < fTop; f += step)
        {
            line(p.x, Y(f), p.r(), Y(f), theme.grid, 1.0f);
            char b[8]; std::snprintf(b, sizeof b, "%.0fk", f / 1000.0);
            textAt(p.x - 4, Y(f), b, kMono, 8.5f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        strokeRect(p, withAlpha2(theme.ink, 0.35f), 1.0f);
        const double t60 = value(kDecay);
        save();
        scissor(p.x, p.y, p.w, p.h);
        char t[64];
        if (!digital)
        {
            const Design d = design(m, value(kTension), value(kDrip), sr);
            // the transition
            const float yfc = Y(d.fc);
            for (float xx = p.x; xx < p.r(); xx += 8.0f) line(xx, yfc, std::min(p.r(), xx + 4.0f), yfc, theme.second, 1.2f);
            for (int l = 0; l < d.springs; ++l)
            {
                // echo n of the low path: (2n + 1) travels, chirping up to the spring's transition
                for (int n = 0; n < 12; ++n)
                {
                    const double k = 2.0 * n + 1.0;
                    if (k * d.travel[l] > tView) break;
                    const float lvlDb = (float)(-60.0 * 2.0 * n * d.travel[l] / t60);
                    const float a = pump::clampv(1.0f + lvlDb / 40.0f, 0.06f, 1.0f);
                    beginPath();
                    constexpr int N = 40;
                    for (int i = 0; i <= N; ++i)
                    {
                        const double f = 60.0 + (d.fcl[l] * 0.985 - 60.0) * std::pow((double)i / N, 0.6);
                        const float x = X(k * d.travelDelay(l, f, sr)), y = Y(f);
                        if (i == 0) moveTo(x, y); else lineTo(x, y);
                    }
                    strokeColor(withAlpha2(theme.ink, a)); strokeWidth(n == 0 ? 2.0f : 1.5f); lineJoin(ROUND); stroke();
                }
                // the high path: its echoes fall from the top (high frequencies first)
                for (int n = 1; n < 12; ++n)
                {
                    if (n * d.hfLoop(l) > tView) break;
                    const float lvlDb = (float)(-60.0 * n * d.hfLoop(l) / (0.4 * t60)) - 8.0f;
                    const float a = pump::clampv(1.0f + lvlDb / 40.0f, 0.0f, 0.8f);
                    if (a <= 0.02f) break;
                    beginPath();
                    constexpr int N = 16;
                    for (int i = 0; i <= N; ++i)
                    {
                        const double f = d.fc + (std::min(fTop, 0.45 * sr) - d.fc) * i / N;
                        const float x = X(n * d.hfDelay(l, f, sr)), y = Y(f);
                        if (i == 0) moveTo(x, y); else lineTo(x, y);
                    }
                    strokeColor(withAlpha2(theme.second, a)); strokeWidth(1.2f); stroke();
                }
            }
            restore();
            std::snprintf(t, sizeof t, "TRANSITION %.2f kHz", d.fc / 1000.0);
        }
        else
        {
            const DigitalModel& dm = digitalModel(m);
            const double clock = clockHz(m, value(kGrit)), ts = digitalScale(value(kTension));
            // above the converter's Nyquist: nothing real, only mirrors
            const float yn = Y(0.5 * clock);
            fillRect(Rect(p.x, p.y, p.w, std::max(0.0f, yn - p.y)), withAlpha2(theme.second, theme.dark ? 0.14f : 0.12f));
            line(p.x, yn, p.r(), yn, theme.second, 1.5f);
            textAt(p.r() - 4, yn - 7, "NYQUIST", kLabel, 9.0f, theme.second, ALIGN_RIGHT | ALIGN_MIDDLE, 1.0f);
            const double ca = 0.35 + 0.30 * value(kDrip) * 0.01;
            // the token chirp: 6 allpasses (a + z^-2) / (1 + a z^-2) at the clock rate
            auto chirp = [&](double f) {
                const double w = pump::kTwoPi * std::min(f, 0.5 * clock) / clock;
                return 6.0 * 2.0 * (1.0 - ca * ca) / (1.0 + 2.0 * ca * std::cos(2.0 * w) + ca * ca) / clock;
            };
            for (int c = 0; c < dm.combs; ++c)
            {
                const double len = dm.comb[c] * ts;
                const double g = std::pow(10.0, -3.0 * len / t60);
                for (int k = 1; k < 30; ++k)
                {
                    if (k * len > tView) break;
                    const float a = pump::clampv(1.0f + (float)(20.0 * std::log10(std::pow(g, k - 1))) / 40.0f, 0.06f, 1.0f);
                    beginPath();
                    constexpr int N = 12;
                    for (int i = 0; i <= N; ++i)
                    {
                        const double f = 0.5 * clock * i / N;
                        const float x = X(k * len + chirp(f)), y = Y(f);
                        if (i == 0) moveTo(x, y); else lineTo(x, y);
                    }
                    strokeColor(withAlpha2(theme.ink, a)); strokeWidth(1.5f); stroke();
                }
            }
            restore();
            std::snprintf(t, sizeof t, "CLOCK %.1f kHz", clock / 1000.0);
        }
        textAt(r.x + 10, r.b() - 11, t, kMono, 10.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(t, sizeof t, "T60 %s", formatParam(kDecay, value(kDecay)).c_str());
        textAt(r.r() - 10, r.b() - 11, t, kMono, 10.0f, theme.ink2, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ---- the KICK key ----
    void drawKick()
    {
        const Rect& r = kickR;
        // flashes when the tank is hit (also from the K key)
        const float flash = std::max(kickPress, kickAgeS < 0.35f ? 1.0f - kickAgeS / 0.35f : 0.0f);
        const bool down = kickHeld || flash > 0.6f;
        Color fillC = pump::mixColor(theme.neon, theme.ink, flash);
        keyBox(r, false, down, &fillC, 6.0f);
        const Rect k = down ? Rect(r.x + 3, r.y + 3, r.w, r.h) : r;
        const Color txt = flash > 0.5f ? theme.on : theme.neonInk;
        textAt(k.cx(), k.cy() - 8.0f, "KICK", kBold, 34.0f, txt, ALIGN_CENTER | ALIGN_MIDDLE, 6.0f);
        textAt(k.cx(), k.cy() + 22.0f, "CRASH THE TANK", kLabel, 10.5f, withAlpha2(txt, 0.8f), ALIGN_CENTER | ALIGN_MIDDLE, 2.0f);
        textAt(k.r() - 12, k.y + 14, "K", kMono, 10.0f, withAlpha2(txt, 0.7f), ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    void kick(float strength)
    {
        if (!plug) return;
        plug->kickStrength.store(strength, std::memory_order_relaxed);
        plug->kickRequest.fetch_add(1, std::memory_order_release);
        setStatus(strength < 0.8f ? "Nudged the tank" : "Kicked the tank");
    }

    // ---- the wet path's EQ (low cut, tone low-pass, bright shelf) ----
    void drawEq()
    {
        const Rect& r = eqR;
        fillRect(r, theme.screen);
        const double lo = 30.0, hi = 20000.0;
        for (double f : { 100.0, 1000.0, 10000.0 }) line(freqToX(r, f, lo, hi), r.y, freqToX(r, f, lo, hi), r.b(), theme.grid, 1.0f);
        const float top = 9.0f, bot = -27.0f;
        auto yOf = [&](double db) { return r.y + (float)((top - db) / (top - bot)) * r.h; };
        line(r.x, yOf(0.0), r.r(), yOf(0.0), withAlpha2(theme.ink, 0.25f), 1.0f);
        const double lc = value(kLowCut), tone = value(kTone);
        const double t = pump::clampv(tone, -100.0, 100.0) * 0.01;
        const double lp = t < 0.0 ? 9000.0 * std::pow(2000.0 / 9000.0, -t) : 9000.0 * std::pow(20000.0 / 9000.0, t);
        const double shelfDb = 0.06 * std::max(0.0, tone);
        auto mag = [&](double f) {
            double db = 0.0;
            if (lowCutActive(lc)) { const double x = f / lc, x2 = x * x; db += 10.0 * std::log10(x2 * x2 / ((1.0 - x2) * (1.0 - x2) + 2.0 * x2)); }
            { const double x = f / lp, x2 = x * x; db -= 10.0 * std::log10((1.0 - x2) * (1.0 - x2) + 2.0 * x2); }
            if (shelfDb > 0.01) { const double x = f / 2500.0; db += shelfDb * (x * x * x * x) / (1.0 + x * x * x * x); }
            return db;
        };
        save();
        scissor(r.x, r.y, r.w, r.h);
        beginPath();
        moveTo(r.x, r.b());
        constexpr int N = 80;
        for (int i = 0; i <= N; ++i) { const double f = lo * std::pow(hi / lo, (double)i / N); lineTo(r.x + r.w * i / N, std::min(r.b(), yOf(mag(f)))); }
        lineTo(r.r(), r.b());
        closePath();
        fillColor(withAlpha2(theme.neon, theme.dark ? 0.30f : 0.45f)); fill();
        beginPath();
        for (int i = 0; i <= N; ++i)
        {
            const double f = lo * std::pow(hi / lo, (double)i / N);
            const float x = r.x + r.w * i / N, y = std::min(r.b() + 2.0f, yOf(mag(f)));
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(theme.ink); strokeWidth(1.6f); lineJoin(ROUND); stroke();
        restore();
        strokeRect(r, theme.ink, 1.0f);
        textAt(r.x + 5, r.y + 9, "WET EQ", kLabel, 9.0f, theme.ink2, ALIGN_LEFT | ALIGN_MIDDLE, 1.0f);
    }

    // ---- mouse: the tank (decay / dwell), the chirps (tension / drip), the KICK key ----
    bool bodyMouseDown(float x, float y, int button, uint mod, bool dbl) override
    {
        if (button != kMouseButtonLeft) return false;
        if (kickR.contains(x, y))
        {
            kickHeld = true;
            kick((mod & kModifierShift) ? 0.45f : 1.0f);
            drag = DragKick;
            return true;
        }
        const bool onTank = tankR.contains(x, y), onChirp = chirpR.contains(x, y);
        if (!onTank && !onChirp) return false;
        const int pa = onTank ? kDecay : kDrip, pb = onTank ? kDwell : kTension;
        if (dbl || cmdKey(mod)) { beginGroup(); setParamGesture(pa, spec(pa).def); setParamGesture(pb, spec(pb).def); endGroup(); return false; }
        drag = onTank ? DragTank : DragChirp;
        dragX = x; dragY = y;
        dragA = norm(pa); dragB = value(pb);
        beginEdit(pa); beginEdit(pb);
        return true;
    }
    void bodyMouseDrag(float x, float y, uint mod) override
    {
        if (drag != DragTank && drag != DragChirp) return;
        const float f = (mod & kModifierShift) ? 0.15f : 1.0f;
        const int pa = drag == DragTank ? kDecay : kDrip, pb = drag == DragTank ? kDwell : kTension;
        setParamNorm(pa, dragA + (x - dragX) * 0.0028f * f);
        setParam(pb, dragB + (dragY - y) * (drag == DragTank ? 0.45f : 0.9f) * f);
        if (mod & kModifierShift) { dragA = norm(pa); dragB = value(pb); dragX = x; dragY = y; }
        setStatus(paramStatus(pa) + "    " + paramStatus(pb));
    }
    void bodyMouseUp(float, float) override
    {
        if (drag == DragTank) { endEdit(kDwell); endEdit(kDecay); }
        else if (drag == DragChirp) { endEdit(kTension); endEdit(kDrip); }
        kickHeld = false;
        drag = DragNone;
    }
    void bodyHover(float x, float y) override
    {
        if (tankR.contains(x, y)) setStatus("Drag the tank: left/right = decay, up/down = dwell (Shift = fine, double-click = reset; wheel = decay)");
        else if (chirpR.contains(x, y)) setStatus("When each frequency of each echo arrives. Drag: up/down = tension, left/right = drip (wheel = tension)");
        else if (kickR.contains(x, y)) setStatus("KICK: crash the tank (Shift+click: a gentle nudge; key K)");
    }
    bool bodyScroll(float x, float y, float dy, uint mod) override
    {
        const int p = tankR.contains(x, y) ? kDecay : (chirpR.contains(x, y) ? kTension : -1);
        if (p < 0) return false;
        const float step = (mod & kModifierShift) ? 0.004f : 0.03f;
        beginEdit(p); setParamNorm(p, norm(p) + (dy > 0 ? step : -step)); endEdit(p);
        setStatus(paramStatus(p));
        return true;
    }
    bool keyPress(uint key, uint mod) override
    {
        if (cmdKey(mod)) return false;
        if (key >= '1' && key <= '6') { setParamGesture(kModel, (float)(key - '1')); return true; }
        if (key == 'k' || key == 'K') { kick((mod & kModifierShift) ? 0.45f : 1.0f); return true; }
        return false;
    }
    std::vector<std::pair<std::string, std::string>> helpLines() override
    {
        return {
            { "Drag the tank", "left/right = decay, up/down = dwell (double-click resets both)" },
            { "Drag the chirps", "up/down = tension, left/right = drip (double-click resets both)" },
            { "KICK / K", "crash the tank (Shift: a gentle nudge)" },
            { "1 - 6", "model: two, three, short, long, digital, lo-fi" },
        };
    }
    std::string footerInfo() override
    {
        char buf[128];
        const int m = model();
        if (isDigital(m))
            std::snprintf(buf, sizeof buf, "%s   CLOCK %.1f KHZ   %.0f BIT", kModelNames[m], clockHz(m, value(kGrit)) / 1000.0, std::round(wordBits(m, value(kGrit))));
        else
            std::snprintf(buf, sizeof buf, "%s   T60 %s   LATENCY 0", kModelNames[m], formatParam(kDecay, value(kDecay)).c_str());
        std::string s = buf;
        for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
        return s;
    }

private:
    static constexpr int kWaveN = 1024;
    enum Drag { DragNone, DragTank, DragChirp, DragKick };
    PluginSpring* plug = nullptr;
    Rect tankR, chirpR, kickR, meterL, meterR, tankPanel, tonePanel, outPanel, eqR;
    OutMeter outMeter;
    float energy[kLanes] = {}, drvShow = 0.0f, kickAgeS = 100.0f, kickPress = 0.0f;
    float waveF[kLanes][kWaveN] = {}, waveB[kLanes][kWaveN] = {};
    bool kickHeld = false;
    Drag drag = DragNone;
    float dragX = 0, dragY = 0, dragA = 0, dragB = 0;

    Segmented* modelSeg;
    Dial *dwellDial, *decayDial, *tensionDial, *dripDial, *wobbleDial, *gritDial, *toneDial, *lowDial, *preDial, *mixDial, *outDial;
    SliderRow* widthRow;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UISpring)
};

UI* createUI() { return new UISpring(); }

END_NAMESPACE_DISTRHO
