// pump COIL / TWANG: the SPRINGS screen, shared by both windows. Every voice is drawn the way SPRING draws its tank: a
// helix between two hooks, seen from the side, whose coils bunch and spread with the sound really running along it
// (the voice's own output, sent down the spring and reflected back), its sleeve glowing as it rings; back strands thin
// and faint, front strands in ink. A lower note is a longer, looser spring with more coils. A kick shakes the screen.
// Resting voices are drawn as still, dim springs.
#pragma once
#include <algorithm>
#include "ui/PumpUI.hpp"
#include "CoilEngine.hpp"

namespace pump { namespace coil {

class SpringsView {
public:
    static constexpr int kWaveN = 512;

    // UI tick: the newest wave and level of every voice
    void update(const Engine* e, float dt)
    {
        if (!e) return;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            e->visV[(size_t)i].readSmooth(wave[i], kWaveN);
            const float lv = e->voiceLevel(i);
            energy[i] = std::max(std::min(1.0f, std::sqrt(std::max(0.0f, lv)) * 3.0f), energy[i] * std::exp(-dt / 0.25f));
            note[i] = e->voiceNote(i);
            if (note[i] >= 0) lastNote[i] = note[i];
        }
        visRate = e->visRate();
    }
    // the same for any instrument whose voices are springs (BOING's pads): one row's wave, level and note
    void updateRow(int i, const ScopeRing& ring, float level, int noteNum, double rate, float dt)
    {
        if (i < 0 || i >= kMaxVoices) return;
        ring.readSmooth(wave[i], kWaveN);
        energy[i] = std::max(std::min(1.0f, std::sqrt(std::max(0.0f, level)) * 3.0f), energy[i] * std::exp(-dt / 0.25f));
        note[i] = noteNum; if (noteNum >= 0) lastNote[i] = noteNum;
        visRate = rate;
    }
    const char* const* labels = nullptr;   // a name per row (else the note it plays)
    bool tank = false;                     // COIL: drawn as SPRING's tank (transducers, fanned hooks), a colour per spring

    void draw(DISTRHO::PumpUI& ui, const Rect& r, float kickAgeS, const char* title, const char* right)
    {
        if (tank) { drawTank(ui, r, kickAgeS, title, right); return; }
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        ui.screenBox(r);
        ui.caption(r.x + 10, r.y + 14, title);
        if (right) ui.textAt(r.r() - 10, r.y + 14, right, P::kLabel, 10.0f, th.ink2, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, 0.8f);
        // a kick shakes the drawing for a moment
        float sx = 0.0f, sy = 0.0f;
        if (kickAgeS < 1.2f)
        {
            const float e = std::exp(-kickAgeS / 0.18f) * 6.0f;
            sx = e * std::sin(kickAgeS * 71.0f);
            sy = e * 0.6f * std::sin(kickAgeS * 53.0f + 1.0f);
        }
        const Rect p(r.x + 12, r.y + 26, r.w - 24, r.h - 32);
        ui.save();
        ui.scissor(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        ui.translate(sx, sy);
        const float rowH = p.h / kMaxVoices;
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int shown = 0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const float yc = p.y + rowH * (i + 0.5f);
            const bool on = note[i] >= 0;
            if (on) ++shown;
            const int nv = lastNote[i] >= 0 ? (lastNote[i] >= 1000 ? lastNote[i] - 1000 : lastNote[i]) : 48 + 5 * i;
            // the note, then the hooks
            if (labels) ui.textAt(p.x, yc, labels[i], P::kLabel, 8.5f, on ? th.ink : th.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 0.4f);
            else if (on)
            {
                char b[12]; std::snprintf(b, sizeof b, "%s%d", nn[((nv % 12) + 12) % 12], nv / 12 - 1);
                ui.textAt(p.x, yc, b, P::kMono, 9.5f, th.ink, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
            }
            const float hookX0 = p.x + (labels ? 52 : 34), hookX1 = p.r() - 4;
            // lower notes: longer springs (the drawing spans the screen; length shows as the number of coils)
            const double f0 = 440.0 * std::exp2((nv - 69) / 12.0);
            const double travel = 0.5 / std::max(20.0, f0);   // one way along the spring: half the period
            const int coils = clampv((int)std::lround(78.0 - 0.7 * (nv - 24)), 26, 72);   // a lower note: more turns, a longer spring
            const float tS = (float)std::min((double)kWaveN / 2 - 4, travel * visRate);
            const float R = std::min(rowH * 0.36f, 10.0f);
            const float x0 = hookX0 + 10, x1 = hookX1 - 10;
            ui.line(hookX0, yc, x0, yc, th.ink2, 1.1f);
            ui.line(x1, yc, hookX1, yc, th.ink2, 1.1f);
            ui.fillRect(Rect(hookX0 - 3, yc - R - 2, 3, 2 * R + 4), on ? th.ink2 : th.dim);
            ui.fillRect(Rect(hookX1, yc - R - 2, 3, 2 * R + 4), on ? th.ink2 : th.dim);
            drawCoil(ui, i, x0, x1, yc, R, coils, tS, on);
        }
        ui.restore();
        if (!shown) ui.textAt(r.cx(), r.b() - 14, "PLAY A NOTE (OR KICK)", P::kLabel, 9.5f, th.dim, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
    }

private:
    // a colour per spring, around the wheel: [0] the light neon (sleeve), [1] its deep shade (strands in light mode)
    static Color springColour(int i, bool deep)
    {
        static const unsigned neon[kMaxVoices] = { 0xFF7466, 0xFFD04A, 0xC8FF4D, 0x5CFFB4, 0x4DF3FF, 0x73B4FF, 0xB9A0FF, 0xFF6FB5 };
        static const unsigned ink[kMaxVoices]  = { 0x8C2318, 0x6B4700, 0x3B5C00, 0x006E44, 0x00636D, 0x1443AC, 0x4528B5, 0x8C1257 };
        return hexColor(deep ? ink[i % kMaxVoices] : neon[i % kMaxVoices]);
    }
    Color strandColour(const Theme& th, int i) const
    {
        return th.dark ? mixColor(springColour(i, false), hexColor(0xFFFFFF), 0.18f) : springColour(i, true);
    }

    // SPRING's tank: the transducers either side, the hooks fanning out to every spring, each spring a helix in its
    // glowing sleeve; under the pan, the notes sounding (each in its spring's colour)
    void drawTank(DISTRHO::PumpUI& ui, const Rect& r, float kickAgeS, const char* title, const char* right)
    {
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        ui.screenBox(r);
        ui.caption(r.x + 10, r.y + 14, title);
        if (right) ui.textAt(r.r() - 10, r.y + 14, right, P::kLabel, 10.0f, th.ink2, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, 0.8f);
        float sx = 0.0f, sy = 0.0f;
        if (kickAgeS < 1.2f)
        {
            const float e = std::exp(-kickAgeS / 0.18f) * 7.0f;
            sx = e * std::sin(kickAgeS * 71.0f);
            sy = e * 0.6f * std::sin(kickAgeS * 53.0f + 1.0f);
        }
        const Rect pan(r.x + 14, r.y + 30, r.w - 28, r.h - 56);
        float eMax = 0.0f;
        for (int i = 0; i < kMaxVoices; ++i) if (note[i] >= 0) eMax = std::max(eMax, energy[i]);
        ui.save();
        ui.scissor(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        ui.translate(sx, sy);
        ui.strokeRect(pan, th.dim, 1.0f);
        const float tw = 46.0f, tH = std::min(pan.h - 30.0f, 150.0f);
        const Rect inT(pan.x + 14, pan.cy() - tH * 0.5f, tw, tH), outT(pan.r() - 14 - tw, pan.cy() - tH * 0.5f, tw, tH);
        drawTransducer(ui, inT, clampv(eMax, 0.0f, 1.0f), "IN");
        drawTransducer(ui, outT, clampv(eMax, 0.0f, 1.0f), "OUT");
        const float x0 = inT.r() + 22.0f, x1 = outT.x - 22.0f;
        // only the voices sounding get a spring, highest note on top (a voice keeps its colour while it rings)
        int order[kMaxVoices], n = 0;
        auto pitch = [&](int i) { return note[i] >= 1000 ? note[i] - 1000 : note[i]; };
        for (int i = 0; i < kMaxVoices; ++i) if (note[i] >= 0) order[n++] = i;
        std::sort(order, order + n, [&](int a, int b) { return pitch(a) != pitch(b) ? pitch(a) > pitch(b) : a < b; });
        const float gap = std::min(78.0f, (pan.h - 20.0f) / std::max(1, n));
        const float R = std::min(11.0f, gap * 0.29f);
        for (int k = 0; k < n; ++k)
        {
            const int i = order[k];
            const float yc = pan.cy() + gap * ((float)k - 0.5f * (float)(n - 1));
            const bool on = true;
            const int nv = pitch(i);
            const double f0 = 440.0 * std::exp2((nv - 69) / 12.0);
            const double travel = 0.5 / std::max(20.0, f0);
            const int coils = clampv((int)std::lround(46.0 - 0.42 * (nv - 24)), 16, 43);   // SPRING's spacing of turns
            const float tS = (float)std::min((double)kWaveN / 2 - 4, travel * visRate);
            const Color hook = on ? th.ink2 : withAlpha(th.ink2, 0.5f);
            ui.line(inT.r(), inT.cy() + (yc - inT.cy()) * 0.35f, x0, yc, hook, 1.2f);
            ui.line(outT.x, outT.cy() + (yc - outT.cy()) * 0.35f, x1, yc, hook, 1.2f);
            drawCoil(ui, i, x0, x1, yc, R, coils, tS, on);
        }
        ui.restore();
        // the read-out under the pan: the notes sounding, each in its spring's colour
        static const char* const nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        float x = r.x + 10;
        int shown = 0;
        for (int k = n - 1; k >= 0; --k)
        {
            const int i = order[k];
            const int nv = pitch(i);
            char b[12]; std::snprintf(b, sizeof b, "%s%d", nn[((nv % 12) + 12) % 12], nv / 12 - 1);
            ui.fillRect(Rect(x, r.b() - 17, 8, 8), springColour(i, false));
            ui.strokeRect(Rect(x, r.b() - 17, 8, 8), strandColour(th, i), 1.0f);
            ui.textAt(x + 12, r.b() - 13, b, P::kMono, 10.0f, th.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
            x += 16 + ui.textWidth(b, P::kMono, 10.0f, 0.0f) + 14;
            ++shown;
        }
        if (!shown) ui.textAt(r.x + 10, r.b() - 13, "PLAY A NOTE (OR KICK)", P::kLabel, 9.5f, th.ink2, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, 1.0f);
        char t[32]; std::snprintf(t, sizeof t, shown == 1 ? "%d SPRING SOUNDING" : "%d SPRINGS SOUNDING", shown);
        ui.textAt(r.r() - 10, r.b() - 13, t, P::kMono, 10.0f, th.ink2, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
    }
    // SPRING's transducer: a magnet in a coil, lit by the drive
    static void drawTransducer(DISTRHO::PumpUI& ui, const Rect& b, float e, const char* name)
    {
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        ui.fillRect(Rect(b.x + 3, b.y + 3, b.w, b.h), th.bg);
        ui.strokeRect(Rect(b.x + 3, b.y + 3, b.w, b.h), th.dim, 1.2f);
        ui.fillRect(b, th.bg);
        ui.fillRect(b, withAlpha(th.neon, 0.15f + 0.85f * e));
        ui.strokeRect(b, th.ink, 1.5f);
        const Rect mg(b.cx() - 7, b.y + 16, 14, b.h - 46);
        ui.fillRect(mg, th.ink);
        for (float yy = mg.y + 6; yy < mg.b() - 3; yy += 7) ui.line(mg.x - 6, yy, mg.r() + 6, yy + 3, th.ink2, 1.2f);
        const Color lc = th.dark ? (e > 0.45f ? th.neonInk : th.ink) : th.neonInk;
        ui.textAt(b.cx(), b.b() - 13, name, P::kLabel, 10.0f, lc, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, 1.0f);
    }

    // the displacement shown for a sample value: log-compressed, so a quiet tail still visibly moves
    static float shownV(float v)
    {
        const float a = std::log10(1.0f + 1000.0f * std::fabs(v)) / 3.0f;
        return std::copysign(std::min(1.0f, a), v);
    }
    void drawCoil(DISTRHO::PumpUI& ui, int l, float x0, float x1, float yc, float R, int coils, float tS, bool on)
    {
        using P = DISTRHO::PumpUI;
        const Theme& th = ui.theme;
        const float len = x1 - x0;
        const float e = on ? clampv(energy[l], 0.0f, 1.0f) : 0.0f;
        // the sleeve lights up as it rings
        ui.beginPath();
        const float pad = (tank && R >= 10.0f) ? 4.0f : 3.0f;
        ui.roundedRect(x0 - 4.0f, yc - R - pad, len + 8.0f, 2.0f * R + 2.0f * pad, R + pad);
        if (tank) ui.fillColor(P::withAlpha2(springColour(l, false), (th.dark ? 0.10f : 0.22f) + (th.dark ? 0.32f : 0.55f) * e));
        else ui.fillColor(P::withAlpha2(th.neon, (th.dark ? 0.04f : 0.08f) + (th.dark ? 0.38f : 0.62f) * e));
        ui.fill();
        // the wave along the spring: the voice's sound leaving one end, and its reflection coming back from the other
        auto at = [&](float pos) {
            const int i = clampv((int)pos, 0, kWaveN - 2);
            const float f = pos - (float)i;
            return wave[l][i] + (wave[l][i + 1] - wave[l][i]) * f;
        };
        auto waveAt = [&](float u) {
            if (!on) return 0.0f;
            const float pf = (float)(kWaveN - 1) - u * tS, pb = (float)(kWaveN - 1) - (2.0f - u) * tS;
            return shownV(at(pf) + 0.7f * at(pb));
        };
        static thread_local float px[600], py[600];
        static thread_local bool front[600];
        const int pts = std::min(std::max(48, coils * 8), 599);
        for (int i = 0; i <= pts; ++i)
        {
            const float u = (float)i / (float)pts;
            const float w = (i == 0 || i == pts) ? 0.0f : waveAt(u);
            const float a = (float)kTwoPi * (float)coils * u;
            const float taper = std::min(1.0f, std::min(u, 1.0f - u) * 30.0f);
            px[i] = x0 + (u + 0.016f * w * taper) * len + R * 0.45f * std::cos(a) * taper;
            py[i] = yc + R * (1.0f + 0.25f * std::fabs(w)) * std::sin(a) * taper + 3.5f * w * taper;
            front[i] = std::cos(a) >= 0.0f;
        }
        const Color base = tank ? strandColour(th, l) : th.ink;
        const Color ink = on ? base : P::withAlpha2(base, 0.45f);
        for (int pass = 0; pass < 2; ++pass)
        {
            ui.beginPath();
            bool open = false;
            for (int i = 1; i <= pts; ++i)
            {
                if (front[i - 1] != (pass == 1)) { open = false; continue; }
                if (!open) { ui.moveTo(px[i - 1], py[i - 1]); open = true; }
                ui.lineTo(px[i], py[i]);
            }
            ui.strokeColor(pass ? ink : P::withAlpha2(base, on ? 0.32f : 0.16f));
            ui.strokeWidth(pass ? (tank ? 1.7f : 1.6f) : 1.0f);
            ui.lineJoin(NanoVG::ROUND);
            ui.stroke();
        }
    }

    float wave[kMaxVoices][kWaveN] = {};
    float energy[kMaxVoices] = {};
    int note[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int lastNote[kMaxVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    double visRate = 12000.0;
};

}} // namespace pump::coil
