// pump UI: a steady frame clock for the editor on Windows (no windows.h here: see WinFrameClock.cpp).
// Hosts drive plugin editors with WM_TIMER, which Windows delivers late and unevenly (it ranks below every other
// message, and a host busy redrawing its own window starves it, worst when the plugin isn't the front window).
// This clock wakes a thread on a high-resolution waitable timer every 1/60 s and posts a message to the editor's
// window; the window paints right away. Elsewhere it does nothing (start returns null).
#pragma once
#include <cstdint>

namespace pump {
void* winFrameClockStart(uintptr_t hwnd, void (*frame)(void* user), void* user);
void winFrameClockStop(void* clock);
}
