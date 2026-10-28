// pump UI: the Windows frame clock (see WinFrameClock.hpp). Compiled into each plugin's UI through fonts.cpp.
#include "WinFrameClock.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
# define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>

namespace pump {
namespace {
const UINT kFrameMsg = WM_APP + 0x5A1;
const wchar_t* const kProp = L"pumpFrameClock";
struct Clock {
    HWND hwnd = nullptr;
    WNDPROC orig = nullptr;
    HANDLE thread = nullptr, stop = nullptr;
    std::atomic<bool> pending { false };
    void (*frame)(void*) = nullptr;
    void* user = nullptr;
};
LRESULT CALLBACK clockProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Clock* c = (Clock*)GetPropW(hwnd, kProp);
    if (c == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == kFrameMsg)
    {
        c->pending.store(false);
        c->frame(c->user);
        UpdateWindow(hwnd);   // paint now, not whenever WM_PAINT's turn comes
        return 0;
    }
    return CallWindowProcW(c->orig, hwnd, msg, wp, lp);
}
DWORD WINAPI clockThread(void* arg)
{
    Clock* c = (Clock*)arg;
    // high resolution where available (Windows 10 1803+), else an ordinary waitable timer
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (t == nullptr) t = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (t == nullptr) return 0;
    HANDLE wait[2] = { c->stop, t };
    for (;;)
    {
        LARGE_INTEGER due;
        due.QuadPart = -166667;   // 16.67 ms, relative, in 100 ns units
        SetWaitableTimer(t, &due, 0, nullptr, nullptr, FALSE);
        if (WaitForMultipleObjects(2, wait, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
        if (!c->pending.exchange(true)) PostMessageW(c->hwnd, kFrameMsg, 0, 0);
    }
    CloseHandle(t);
    return 0;
}
} // namespace

void* winFrameClockStart(uintptr_t hwnd, void (*frame)(void*), void* user)
{
    HWND h = (HWND)hwnd;
    if (h == nullptr || frame == nullptr || GetPropW(h, kProp) != nullptr) return nullptr;
    Clock* c = new Clock();
    c->hwnd = h; c->frame = frame; c->user = user;
    c->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetPropW(h, kProp, c);
    c->orig = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)clockProc);
    c->thread = CreateThread(nullptr, 0, clockThread, c, 0, nullptr);
    if (c->stop == nullptr || c->orig == nullptr || c->thread == nullptr) { winFrameClockStop(c); return nullptr; }
    return c;
}

void winFrameClockStop(void* clock)
{
    Clock* c = (Clock*)clock;
    if (c == nullptr) return;
    if (c->thread) { SetEvent(c->stop); WaitForSingleObject(c->thread, 2000); CloseHandle(c->thread); }
    if (c->stop) CloseHandle(c->stop);
    if (c->orig && IsWindow(c->hwnd) && (WNDPROC)GetWindowLongPtrW(c->hwnd, GWLP_WNDPROC) != clockProc)
    {
        // someone subclassed the window after us: our procedure must stay in the chain, so keep a pass-through
        static void (*const noFrame)(void*) = [](void*) {};
        c->frame = noFrame;
        return;
    }
    if (c->orig && IsWindow(c->hwnd)) SetWindowLongPtrW(c->hwnd, GWLP_WNDPROC, (LONG_PTR)c->orig);
    if (IsWindow(c->hwnd)) RemovePropW(c->hwnd, kProp);
    // a frame message already posted now reaches the window's own procedure, which ignores it
    delete c;
}
} // namespace pump
#else
namespace pump {
void* winFrameClockStart(uintptr_t, void (*)(void*), void*) { return nullptr; }
void winFrameClockStop(void*) {}
}
#endif

// the text-box keyboard grab rides along, so every plugin's fonts.cpp picks it up without a Makefile change
#include "WinKeyGrab.cpp"
