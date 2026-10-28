// pump UI: keep the keyboard while a text box is open (see KeyGrab.hpp). Compiled into each plugin's UI through
// WinFrameClock.cpp (which every plugin's fonts.cpp includes).
#include "KeyGrab.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
# define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pump {
namespace {
struct Grab {
    HWND hwnd = nullptr, root = nullptr, hostFg = nullptr;
    DWORD ownPid = 0, hostPid = 0, ownerPid = 0;
    KeyGrabFn key = nullptr;
    void* user = nullptr;
    bool done = false;        // stopped, or the editor said it is finished: let every key through
    bool inCall = false;      // inside key(): stop() must not free us yet
    bool freeLater = false;
    BYTE swallowed[256] = {}; // keys whose press we kept: their release is kept too
};
// One text box is open at a time per process (a click elsewhere closes it), so one hook serves all.
Grab* gGrab = nullptr;
HHOOK gHook = nullptr;

void unhook()
{
    if (gHook) { UnhookWindowsHookEx(gHook); gHook = nullptr; }
}

// Ask Windows for the foreground and the focus. A thread that isn't the foreground one is refused, so share the
// foreground thread's input queue for the request (the user has just clicked our window, which allows it).
void takeFocus(HWND win, HWND focus)
{
    const DWORD me = GetCurrentThreadId();
    const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attach = fgThread != 0 && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(win);
    if (focus) SetFocus(focus);
    if (attach) AttachThreadInput(me, fgThread, FALSE);
}

bool isModifier(DWORD vk)
{
    switch (vk)
    {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK hookProc(int code, WPARAM wp, LPARAM lp)
{
    Grab* g = gGrab;
    if (code != HC_ACTION || g == nullptr || g->done) return CallNextHookEx(gHook, code, wp, lp);
    const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
    const DWORD vk = k->vkCode & 0xFF;
    const bool press = (k->flags & LLKHF_UP) == 0;

    // a release of a key we kept is kept too, wherever the focus went since
    if (!press && g->swallowed[vk]) { g->swallowed[vk] = 0; return 1; }
    if (isModifier(vk) || (k->flags & LLKHF_INJECTED) != 0 || (k->flags & LLKHF_ALTDOWN) != 0)
        return CallNextHookEx(gHook, code, wp, lp);
    if (!IsWindow(g->hwnd)) { g->done = true; return CallNextHookEx(gHook, code, wp, lp); }

    // only while the front window is the host's or ours: other apps keep their keyboard
    const HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    const DWORD fgThread = GetWindowThreadProcessId(fg, &fgPid);
    if (fgPid != g->ownPid && fgPid != g->hostPid && fgPid != g->ownerPid) return CallNextHookEx(gHook, code, wp, lp);
    // the editor already has the keyboard: the window delivers keys as usual
    if (GetFocus() == g->hwnd) return CallNextHookEx(gHook, code, wp, lp);

    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    unsigned ch = 0;
    if (press && !ctrl)
    {
        BYTE ks[256] = {};
        if (shift) ks[VK_SHIFT] = 0x80;
        if (GetKeyState(VK_CAPITAL) & 1) ks[VK_CAPITAL] = 0x01;
        WCHAR buf[4] = {};
        // flag 4: don't disturb the keyboard state (dead keys) of the app that would have had the key
        if (ToUnicodeEx(vk, k->scanCode, ks, buf, 4, 0x4, GetKeyboardLayout(fgThread)) == 1) ch = buf[0];
    }
    if (press) g->swallowed[vk] = 1;

    g->inCall = true;
    const bool keep = g->key(g->user, vk, ch, press, ctrl, shift);
    g->inCall = false;
    if (!keep) g->done = true;
    if (g->freeLater) { if (gGrab == g) { gGrab = nullptr; unhook(); } delete g; }
    return 1;
}
} // namespace

void* keyGrabStart(uintptr_t hwndValue, KeyGrabFn key, void* user)
{
    HWND hwnd = (HWND)hwndValue;
    if (!hwnd || !key || !IsWindow(hwnd)) return nullptr;
    if (gGrab) keyGrabStop(gGrab);   // another instance's box: it lost the click already

    Grab* g = new Grab();
    g->hwnd = hwnd;
    g->root = GetAncestor(hwnd, GA_ROOT);
    g->key = key;
    g->user = user;
    g->ownPid = GetCurrentProcessId();
    g->hostFg = GetForegroundWindow();
    GetWindowThreadProcessId(g->hostFg, &g->hostPid);
    if (HWND owner = GetWindow(g->root, GW_OWNER)) GetWindowThreadProcessId(owner, &g->ownerPid);
    if (HWND parent = GetAncestor(g->root, GA_PARENT); parent && parent != GetDesktopWindow() && !g->ownerPid)
        GetWindowThreadProcessId(parent, &g->ownerPid);

    takeFocus(g->root ? g->root : hwnd, hwnd);

    gGrab = g;
    if (!gHook) gHook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    return g;
}

void keyGrabStop(void* grab)
{
    Grab* g = (Grab*)grab;
    if (!g) return;
    g->done = true;
    // give the host the keyboard back if we took its window and the user is still on ours
    if (g->hostFg && g->hostFg != g->root && IsWindow(g->hostFg) && GetForegroundWindow() == g->root)
        takeFocus(g->hostFg, nullptr);
    if (gGrab != g) { if (!g->inCall) delete g; else g->freeLater = true; return; }
    if (g->inCall) { g->freeLater = true; return; }
    gGrab = nullptr;
    unhook();
    delete g;
}
} // namespace pump
#elif !defined(__APPLE__)   // macOS: MacKeyGrab.mm
namespace pump {
void* keyGrabStart(uintptr_t, KeyGrabFn, void*) { return nullptr; }
void keyGrabStop(void*) {}
}
#endif
