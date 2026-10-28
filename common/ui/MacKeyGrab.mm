// pump UI: keep the keyboard while a text box is open, on macOS (see KeyGrab.hpp). Built into every plugin's UI by
// plugins/Plugin.mk.
#include "KeyGrab.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

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
} // namespace pump
#endif
