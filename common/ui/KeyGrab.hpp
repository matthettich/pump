// pump UI: keep the keyboard while a text box is open (Windows: WinKeyGrab.cpp, macOS: MacKeyGrab.mm).
// Some hosts never hand a plugin editor the keyboard. Live forwards key presses to the plugin; Renoise keeps them
// for its own shortcuts, and with plug-in sandboxing the editor lives in another process that a plain focus request
// can't bring forward. So typing into SING's lyrics (or any pump text box) did nothing in Renoise.
//
// While a text box is open, start() takes the keyboard the way the system allows it, then routes key presses to the
// editor ahead of the host until stop() (Enter, Esc or a click elsewhere), which gives the host its keyboard back.
//  - Windows: real focus first (borrowing the foreground window's input queue for the request), and a keyboard hook
//    that hands every key to `key` and keeps it from the host while the box is open. It only acts while the front window
//    is the host's or this plugin's, so other apps keep their keyboard; Alt and Windows-key combinations pass.
//  - macOS: the plugin's process is made active if it isn't (a sandboxed plugin lives in its own process), its
//    window key and the editor first responder; an event monitor in this process hands key events straight to the
//    editor before the host's own key handling sees them. Command shortcuts other than editing ones pass.
// Elsewhere both do nothing (start returns null).
#pragma once
#include <cstdint>

namespace pump {
// Windows only: vk is the virtual-key code, ch the typed character (0 for none). Returns whether to keep grabbing.
// (macOS gives the editor the key events themselves.)
using KeyGrabFn = bool (*)(void* user, unsigned vk, unsigned ch, bool press, bool ctrl, bool shift);
void* keyGrabStart(uintptr_t nativeView, KeyGrabFn key, void* user);
void keyGrabStop(void* grab);
}
