// Tiny UI automation for headless tests (X11 + XTest): finds a window by name and plays a script of mouse and
// key events in the window's LOGICAL coordinates (scaled by window width / logical width).
// Build: g++ -O2 tools/xdo.cpp -o xdo -lX11 /usr/lib/x86_64-linux-gnu/libXtst.so.6
// Usage: DISPLAY=:99 xdo "<window name>" <logicalWidth> cmd...
//   move X Y | down B | up B | click B | keydown KEYSYM | keyup KEYSYM | key KEYSYM | wheel N (+up/-down) | sleep MS
//   shot FILE (screenshot of the window via ImageMagick import)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

extern "C" {
int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
}

static Window findWindow(Display* d, Window w, const char* name)
{
    char* n = nullptr;
    if (XFetchName(d, w, &n) && n)
    {
        const bool match = std::strstr(n, name) != nullptr;
        XFree(n);
        if (match) return w;
    }
    Window root, parent, *kids = nullptr;
    unsigned int nk = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &nk)) return 0;
    Window found = 0;
    for (unsigned int i = 0; i < nk && !found; ++i) found = findWindow(d, kids[i], name);
    if (kids) XFree(kids);
    return found;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: xdo NAME LOGICALW cmds...\n"); return 1; }
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::fprintf(stderr, "no display\n"); return 1; }
    Window w = 0;
    for (int tries = 0; tries < 50 && !w; ++tries) { w = findWindow(d, DefaultRootWindow(d), argv[1]); if (!w) usleep(100000); }
    if (!w) { std::fprintf(stderr, "window '%s' not found\n", argv[1]); return 1; }
    XWindowAttributes a;
    XGetWindowAttributes(d, w, &a);
    int ax = 0, ay = 0; Window child;
    XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &ax, &ay, &child);
    const double scale = a.width / std::atof(argv[2]);
    for (int i = 3; i < argc; ++i)
    {
        const std::string c = argv[i];
        auto arg = [&](int k) { return argv[i + k]; };
        if (c == "move") { XTestFakeMotionEvent(d, -1, ax + (int)(std::atof(arg(1)) * scale), ay + (int)(std::atof(arg(2)) * scale), 0); i += 2; }
        else if (c == "down") { XTestFakeButtonEvent(d, std::atoi(arg(1)), True, 0); i += 1; }
        else if (c == "up") { XTestFakeButtonEvent(d, std::atoi(arg(1)), False, 0); i += 1; }
        else if (c == "click") { XTestFakeButtonEvent(d, std::atoi(arg(1)), True, 0); XFlush(d); usleep(30000); XTestFakeButtonEvent(d, std::atoi(arg(1)), False, 0); i += 1; }
        else if (c == "wheel") { const int n = std::atoi(arg(1)); for (int k = 0; k < std::abs(n); ++k) { XTestFakeButtonEvent(d, n > 0 ? 4 : 5, True, 0); XTestFakeButtonEvent(d, n > 0 ? 4 : 5, False, 0); XFlush(d); usleep(20000); } i += 1; }
        else if (c == "keydown" || c == "keyup" || c == "key")
        {
            const KeyCode kc = XKeysymToKeycode(d, XStringToKeysym(arg(1)));
            if (c != "keyup") XTestFakeKeyEvent(d, kc, True, 0);
            if (c == "key") { XFlush(d); usleep(30000); }
            if (c != "keydown") XTestFakeKeyEvent(d, kc, False, 0);
            i += 1;
        }
        else if (c == "sleep") { XFlush(d); usleep(std::atoi(arg(1)) * 1000); i += 1; }
        else if (c == "shot")
        {
            XFlush(d);
            usleep(150000);
            char cmd[512];
            std::snprintf(cmd, sizeof cmd, "import -window 0x%lx '%s' 2>/dev/null", (unsigned long)w, arg(1));
            if (std::system(cmd) != 0) std::fprintf(stderr, "screenshot failed\n");
            i += 1;
        }
        XFlush(d);
        usleep(15000);
    }
    XCloseDisplay(d);
    return 0;
}
