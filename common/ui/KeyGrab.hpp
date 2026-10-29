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

// macOS only (null elsewhere): a real one-line text field floating over a typed-name box (SING's lyrics, preset
// names). It lives in a non-activating panel, the kind of window that takes the keyboard without bringing its app to
// the front: a sandboxed plugin's process can't come forward on macOS 14 and later (activation became cooperative,
// and the host doesn't yield), and an in-process editor shares its window with the host's own key handling. The
// field gets the keys either way, with the system's own editing (cursor, selection, Cmd+V/C/X/Z, accents).
// fx, fy, fw, fh: the box as fractions of the editor view (from its top left). how: 0 the text changed, 1 Enter,
// -1 Esc. bg / ink: 0xRRGGBB.
using TextFieldFn = void (*)(void* user, const char* utf8, int how);
void* textFieldOpen(uintptr_t nativeView, float fx, float fy, float fw, float fh, const char* initial, unsigned maxLen,
                    unsigned bg, unsigned ink, TextFieldFn fn, void* user);
void textFieldClose(void* field);
}
