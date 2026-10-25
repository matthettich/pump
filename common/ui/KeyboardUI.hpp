// pump UI: the instruments' on-screen keyboard. Draw it with draw(); feed mouse events to it from the window's
// bodyMouseDown / Drag / Up; it sends notes through `send(note, velocity)` (velocity 0 = off). Velocity follows
// where the key is struck (top = soft, bottom = hard). Dragging plays across the keys. `lit` marks sounding notes.
#pragma once

#include "PumpUI.hpp"
#include <functional>

START_NAMESPACE_DISTRHO

struct KeyboardUI {
    Rect r;
    int lowNote = 36, octaves = 4;          // C2 .. B5
    int down = -1;                          // the note held by the mouse
    std::function<void(int, int)> send;
    bool lit[128] = {};

    int whiteCount() const { return octaves * 7; }
    // the note at a point (black keys first), or -1
    int noteAt(float x, float y) const
    {
        if (!r.contains(x, y)) return -1;
        const float ww = r.w / whiteCount();
        static const int whiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int blackAfter[7] = { 1, 1, 0, 1, 1, 1, 0 };   // a black key after this white key
        const int wi = std::min(whiteCount() - 1, (int)((x - r.x) / ww));
        if (y < r.y + r.h * 0.6f)
        {
            const float local = (x - r.x) - wi * ww;
            const int w7 = wi % 7, oct = wi / 7;
            if (local > ww * 0.65f && blackAfter[w7]) return lowNote + oct * 12 + whiteSemis[w7] + 1;
            if (local < ww * 0.35f && w7 > 0 && blackAfter[w7 - 1]) return lowNote + oct * 12 + whiteSemis[w7] - 1;
        }
        return lowNote + (wi / 7) * 12 + whiteSemis[wi % 7];
    }
    int velocityAt(float y) const { return pump::clampv((int)(30 + 97 * (y - r.y) / r.h), 1, 127); }

    void draw(PumpUI& ui)
    {
        const Theme& t = ui.theme;
        const float ww = r.w / whiteCount();
        static const int whiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int blackAfter[7] = { 1, 1, 0, 1, 1, 1, 0 };
        ui.fillRect(Rect(r.x + 4, r.y + 4, r.w, r.h), t.bg);
        ui.strokeRect(Rect(r.x + 4, r.y + 4, r.w, r.h), t.dim, 1.5f);
        for (int i = 0; i < whiteCount(); ++i)
        {
            const int note = lowNote + (i / 7) * 12 + whiteSemis[i % 7];
            const Rect k(r.x + i * ww, r.y, ww, r.h);
            ui.fillRect(k, lit[note] || note == down ? t.neon : t.bg);
            ui.strokeRect(k, t.ink, 1.0f);
            if (i % 7 == 0)
            {
                char b[8]; std::snprintf(b, sizeof b, "C%d", pump::clampv((note / 12) - 1, -1, 9));
                ui.textAt(k.cx(), k.b() - 8, b, PumpUI::kMono, 8.5f, t.ink2, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
            }
        }
        for (int i = 0; i < whiteCount(); ++i)
        {
            if (!blackAfter[i % 7] || i == whiteCount() - 1) continue;
            const int note = lowNote + (i / 7) * 12 + whiteSemis[i % 7] + 1;
            const Rect k(r.x + (i + 1) * ww - ww * 0.32f, r.y, ww * 0.64f, r.h * 0.6f);
            ui.fillRect(k, lit[note] || note == down ? t.neon : t.ink);
            ui.strokeRect(k, t.ink, 1.0f);
        }
        ui.strokeRect(r, t.ink, 1.5f);
    }
    bool mouseDown(float x, float y)
    {
        const int n = noteAt(x, y);
        if (n < 0) return false;
        down = n;
        if (send) send(n, velocityAt(y));
        return true;
    }
    void mouseDrag(float x, float y)
    {
        if (down < 0) return;
        const int n = noteAt(pump::clampv(x, r.x + 1, r.r() - 1), pump::clampv(y, r.y + 1, r.b() - 1));
        if (n >= 0 && n != down) { if (send) { send(down, 0); send(n, velocityAt(y)); } down = n; }
    }
    void mouseUp() { if (down >= 0 && send) send(down, 0); down = -1; }
};

END_NAMESPACE_DISTRHO
