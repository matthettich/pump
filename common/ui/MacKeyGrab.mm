// pump UI: keep the keyboard while a text box is open, on macOS (see KeyGrab.hpp). Built into every plugin's UI by
// plugins/Plugin.mk.
#include "KeyGrab.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#if __has_feature(objc_arc)
# define PUMP_RETAIN(x) (x)
# define PUMP_RELEASE(x) ((void)0)
#else
# define PUMP_RETAIN(x) [(x) retain]
# define PUMP_RELEASE(x) [(x) release]
#endif

namespace pump {
namespace {
struct Grab {
    NSView* view = nil;            // the editor (pugl's view); not retained: the UI stops the grab before it goes
    id monitor = nil;              // the local key event monitor
    NSRunningApplication* prev = nil;   // the app that was in front, if we brought this process forward
    bool done = false;
};
Grab* gGrab = nullptr;

// Command shortcuts the text box handles itself (paste, copy, cut, select all, undo); others stay the host's
bool editingShortcut(NSEvent* e)
{
    NSString* c = [[e charactersIgnoringModifiers] lowercaseString];
    if ([c length] != 1) return false;
    const unichar ch = [c characterAtIndex:0];
    return ch == 'v' || ch == 'c' || ch == 'x' || ch == 'a' || ch == 'z';
}

void focusEditor(Grab* g)
{
    NSWindow* w = [g->view window];
    if (w == nil) return;
    if (![w isKeyWindow]) [w makeKeyWindow];
    if ([w firstResponder] != g->view) [w makeFirstResponder:g->view];
}
} // namespace

void* keyGrabStart(uintptr_t nativeView, KeyGrabFn, void*)
{
    NSView* view = (NSView*)nativeView;
    if (view == nil || [view window] == nil) return nullptr;
    if (gGrab) keyGrabStop(gGrab);   // another instance's box: it lost the click already

    Grab* g = new Grab();
    g->view = view;

    // A sandboxed plugin runs in a process of its own that stays in the background while the host is in front:
    // its window can't be key until the process is active. The user has just clicked our window, so this is allowed.
    if (![NSApp isActive])
    {
        g->prev = PUMP_RETAIN([[NSWorkspace sharedWorkspace] frontmostApplication]);
        if (@available(macOS 14.0, *)) [NSApp activate];
        else
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        }
    }
    focusEditor(g);

    // Every key event this process gets goes to the editor while the box is open, before the host's own key
    // handling (window, menu shortcuts, its own monitors added earlier) can take it. Events for other apps never
    // reach this process, so they are untouched.
    Grab* const self = g;
    g->monitor = PUMP_RETAIN([NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
                                                                   handler:^NSEvent*(NSEvent* e) {
        if (gGrab != self || self->done) return e;
        NSView* v = self->view;
        if ([v window] == nil) return e;   // the editor closed under us
        if (([e modifierFlags] & NSEventModifierFlagCommand) != 0 && !editingShortcut(e)) return e;
        focusEditor(self);
        if ([e type] == NSEventTypeKeyDown) [v keyDown:e];
        else [v keyUp:e];
        return nil;   // handled: the host doesn't see it
    }]);
    gGrab = g;
    return g;
}

void keyGrabStop(void* grab)
{
    Grab* g = (Grab*)grab;
    if (g == nullptr) return;
    g->done = true;
    if (gGrab == g) gGrab = nullptr;
    if (g->monitor != nil) { [NSEvent removeMonitor:g->monitor]; PUMP_RELEASE(g->monitor); g->monitor = nil; }
    // give the host back the front if we took it and the user is still with us
    if (g->prev != nil)
    {
        if ([NSApp isActive] && ![g->prev isTerminated]) [g->prev activateWithOptions:0];
        PUMP_RELEASE(g->prev);
        g->prev = nil;
    }
    delete g;
}

// ---- the native text field (KeyGrab.hpp) ---------------------------------------------------------------------------
namespace {
BOOL yesImp(id, SEL) { return YES; }
BOOL noImp(id, SEL) { return NO; }
// A borderless NSPanel won't become key unless its class says so: a subclass made at run time, named for this binary
// (every pump plugin is its own bundle, and one may be unloaded while others still use their class).
Class panelClass()
{
    static Class c = nil;
    if (c != nil) return c;
    static int anchor = 0;
    char name[64]; std::snprintf(name, sizeof name, "PumpTextPanel_%p", (void*)&anchor);
    c = objc_getClass(name);
    if (c == nil)
    {
        c = objc_allocateClassPair([NSPanel class], name, 0);
        class_addMethod(c, @selector(canBecomeKeyWindow), (IMP)yesImp, "c@:");
        class_addMethod(c, @selector(canBecomeMainWindow), (IMP)noImp, "c@:");
        objc_registerClassPair(c);
    }
    return c;
}
NSColor* rgb(unsigned v) { return [NSColor colorWithSRGBRed:((v >> 16) & 255) / 255.0 green:((v >> 8) & 255) / 255.0 blue:(v & 255) / 255.0 alpha:1.0]; }

struct Field {
    NSPanel* panel = nil;
    NSTextField* field = nil;
    NSWindow* parent = nil;
    id monitor = nil, changed = nil, ended = nil;
    TextFieldFn fn = nullptr;
    void* user = nullptr;
    unsigned maxLen = 40;
    bool done = false;   // Enter or Esc already reported
    void fire(int how)
    {
        if (done) return;
        if (how != 0) done = true;
        NSString* t = [field stringValue];
        if (fn) fn(user, t ? [t UTF8String] : "", how);
    }
};
} // namespace

void* textFieldOpen(uintptr_t nativeView, float fx, float fy, float fw, float fh, const char* initial, unsigned maxLen,
                    unsigned bg, unsigned ink, TextFieldFn fn, void* user)
{
    NSView* view = (NSView*)nativeView;
    if (view == nil || fn == nullptr) return nullptr;
    NSWindow* win = [view window];
    if (win == nil) return nullptr;
    if (gGrab) keyGrabStop(gGrab);

    // the box, from the view's top left (fractions) to the screen
    const NSRect b = [view bounds];
    NSRect box;
    box.size.width = fw * b.size.width; box.size.height = fh * b.size.height;
    box.origin.x = b.origin.x + fx * b.size.width;
    box.origin.y = [view isFlipped] ? b.origin.y + fy * b.size.height : b.origin.y + (1.0 - fy - fh) * b.size.height;
    const NSRect onScreen = [win convertRectToScreen:[view convertRect:box toView:nil]];

    Field* f = new Field();
    f->fn = fn; f->user = user; f->maxLen = maxLen ? maxLen : 40; f->parent = win;

    NSPanel* panel = [[panelClass() alloc] initWithContentRect:onScreen
                                                     styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                                                       backing:NSBackingStoreBuffered defer:NO];
    [panel setReleasedWhenClosed:NO];
    [panel setHidesOnDeactivate:NO];      // a sandboxed plugin's process is never the active app
    [panel setBecomesKeyOnlyIfNeeded:NO];
    [panel setOpaque:YES];
    [panel setBackgroundColor:rgb(bg)];
    [panel setHasShadow:NO];

    const NSRect inner = NSMakeRect(4, 0, onScreen.size.width - 8, onScreen.size.height);
    NSTextField* field = [[NSTextField alloc] initWithFrame:inner];
    [field setBezeled:NO]; [field setBordered:NO]; [field setDrawsBackground:YES];
    [field setBackgroundColor:rgb(bg)]; [field setTextColor:rgb(ink)];
    [field setFocusRingType:NSFocusRingTypeNone];
    const CGFloat pt = std::max<CGFloat>(10.0, std::min<CGFloat>(18.0, onScreen.size.height * 0.5));
    NSFont* font = nil;
    if (@available(macOS 10.15, *)) font = [NSFont monospacedSystemFontOfSize:pt weight:NSFontWeightRegular];
    if (font == nil) font = [NSFont userFixedPitchFontOfSize:pt];
    [field setFont:font];
    [[field cell] setScrollable:YES]; [[field cell] setWraps:NO]; [[field cell] setUsesSingleLineMode:YES];
    // centred vertically: the field as tall as one line of its font
    const CGFloat lh = std::ceil([font ascender] - [font descender] + [font leading]) + 2.0;
    [field setFrame:NSMakeRect(inner.origin.x, std::max<CGFloat>(0.0, (inner.size.height - lh) * 0.5), inner.size.width, lh)];
    [field setStringValue:[NSString stringWithUTF8String:initial ? initial : ""] ?: @""];
    [[panel contentView] addSubview:field];
    f->panel = panel; f->field = field;

    Field* const self = f;
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    f->changed = PUMP_RETAIN([nc addObserverForName:NSControlTextDidChangeNotification object:field queue:nil usingBlock:^(NSNotification*) {
        NSString* t = [self->field stringValue];
        if ([t length] > self->maxLen) { [self->field setStringValue:[t substringToIndex:self->maxLen]]; NSBeep(); }
        self->fire(0);
    }]);
    f->ended = PUMP_RETAIN([nc addObserverForName:NSControlTextDidEndEditingNotification object:field queue:nil usingBlock:^(NSNotification* n) {
        // Enter ends it; a click elsewhere only takes the keyboard away (click the field to go on typing)
        if ([[[n userInfo] objectForKey:@"NSTextMovement"] intValue] == 0x10) self->fire(1);   // 0x10: NSTextMovementReturn
    }]);
    // Esc (the field itself ignores it) and Enter, before anything of the host's can take them
    f->monitor = PUMP_RETAIN([NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* e) {
        if ([e window] != self->panel || self->done) return e;
        const unsigned short k = [e keyCode];
        if (k == 53) { self->fire(-1); return nil; }            // Esc
        if (k == 36 || k == 76) { self->fire(1); return nil; }  // Return, keypad Enter
        return e;
    }]);

    [win addChildWindow:panel ordered:NSWindowAbove];
    [panel makeKeyAndOrderFront:nil];
    [panel makeFirstResponder:field];
    if (NSText* ed = [field currentEditor]) [ed selectAll:nil];   // typing replaces it, as in the drawn box
    return f;
}

void textFieldClose(void* field)
{
    Field* f = (Field*)field;
    if (f == nullptr) return;
    f->done = true; f->fn = nullptr;
    if (f->monitor != nil) { [NSEvent removeMonitor:f->monitor]; PUMP_RELEASE(f->monitor); f->monitor = nil; }
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    if (f->changed != nil) { [nc removeObserver:f->changed]; PUMP_RELEASE(f->changed); f->changed = nil; }
    if (f->ended != nil) { [nc removeObserver:f->ended]; PUMP_RELEASE(f->ended); f->ended = nil; }
    if (f->panel != nil)
    {
        if (NSWindow* p = [f->panel parentWindow]) [p removeChildWindow:f->panel];
        [f->panel orderOut:nil];
        [f->panel close];
        PUMP_RELEASE(f->field);
        PUMP_RELEASE(f->panel);
        f->panel = nil; f->field = nil;
    }
    // hand the keyboard back to the editor's window if the user is still with it
    if (f->parent != nil && [f->parent isVisible] && [NSApp isActive]) [f->parent makeKeyWindow];
    delete f;
}
} // namespace pump
#endif
