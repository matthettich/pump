// Room Profiles: pump ROOM's room and headphone profiles on everything Windows plays, switched from a tray menu.
//
// The system-wide equalizer for Windows does the processing: it reads config.txt in its config folder and reloads the
// moment a file there changes. Room Profiles adds one line to config.txt (Include: pump-room-active.txt) once, and to
// switch it writes the chosen profile's filters (ApoProfile.hpp) and rewrites pump-room-active.txt.
//
//  - The library: .pumproom files in %APPDATA%\wake\pump ROOM\Profiles, listed under Room Profiles or Headphones
//    (from each file's "kind"), the active one ticked. Add Profiles..., Show Profiles Folder, No Profile.
//  - Opening a .pumproom file (a double click: the app registers the type for the current user) adds it to the
//    library and switches to it; a second copy of the app hands the file to the running one.
//  - Separate Profile for Each Output (on by default): each output device keeps its own choice, and the active file
//    follows Windows' default output.
//  - When the config folder can't be written, Allow Changes... asks once (an administrator prompt) to let users edit it.
//
// Settings: %APPDATA%\wake\pump ROOM\room-profiles.ini.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include "ApoProfile.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

const wchar_t* kClass = L"pumpRoomProfiles";
const wchar_t* kTitle = L"Room Profiles";
const UINT WM_TRAY = WM_APP + 1, WM_DEVICE = WM_APP + 2, WM_BUILT = WM_APP + 3;
const UINT ID_ROOM = 1000, ID_PHONES = 2000, ID_NONE = 3001, ID_ADD = 3002, ID_FOLDER = 3003, ID_PERDEVICE = 3004,
           ID_LOGIN = 3005, ID_ALLOW = 3006, ID_QUIT = 3007, ID_SETUP = 3008;

HWND gWnd = nullptr;
NOTIFYICONDATAW gIcon = {};
UINT gTaskbarCreated = 0;
std::wstring gLib, gIni, gApo;        // the library, the settings file, the equalizer's config folder ("" if none)
std::wstring gProblem;                 // shown in the menu
std::atomic<bool> gBusy { false };
IMMDeviceEnumerator* gEnum = nullptr;

// --- strings and files

std::wstring widen(const std::string& s)
{
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool readFile(const std::wstring& path, std::string& out, size_t limit = 16u << 20)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    if (size.QuadPart < 0 || (unsigned long long)size.QuadPart > limit) { CloseHandle(h); return false; }
    out.assign((size_t)size.QuadPart, '\0');
    DWORD got = 0;
    const BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    return ok && got == out.size();
}

// written to a temporary file next to it, then swapped in (the equalizer never reads half a file); the error code
DWORD writeFile(const std::wstring& path, const void* data, size_t size)
{
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD put = 0;
    const BOOL ok = size == 0 || WriteFile(h, data, (DWORD)size, &put, nullptr);
    CloseHandle(h);
    if (!ok || put != size) { const DWORD e = GetLastError(); DeleteFileW(tmp.c_str()); return e ? e : ERROR_WRITE_FAULT; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        return e;
    }
    return 0;
}

void makeDirs(const std::wstring& dir) { SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr); }

std::wstring fileNameOf(const std::wstring& path)
{
    const size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

// --- settings

std::wstring iniGet(const wchar_t* section, const std::wstring& key, const wchar_t* def = L"")
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key.c_str(), def, buf, 1024, gIni.c_str());
    return buf;
}
void iniSet(const wchar_t* section, const std::wstring& key, const std::wstring& value)
{
    WritePrivateProfileStringW(section, key.c_str(), value.c_str(), gIni.c_str());
}
bool perDevice() { return iniGet(L"options", L"perDevice", L"1") != L"0"; }

// --- the equalizer's config folder

std::wstring findApo()
{
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", L"ConfigPath", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, buf, &size) == ERROR_SUCCESS && buf[0])
        return buf;
    const std::wstring guess = knownFolder(FOLDERID_ProgramFiles) + L"\\EqualizerAPO\\config";
    const DWORD a = GetFileAttributesW(guess.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? guess : L"";
}

// config.txt gets the include line (once); false and the error when it can't be written
bool ensureInclude(DWORD* err)
{
    *err = 0;
    const std::wstring cfg = gApo + L"\\config.txt";
    std::string text;
    readFile(cfg, text);
    bool changed = false;
    const std::string with = pump::apo::withInclude(text, &changed);
    if (!changed) return true;
    *err = writeFile(cfg, with.data(), with.size());
    return *err == 0;
}

// --- output devices

std::wstring defaultOutput(std::wstring* name = nullptr)
{
    std::wstring id;
    if (name) name->clear();
    if (gEnum == nullptr) return id;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED(gEnum->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev)
    {
        LPWSTR s = nullptr;
        if (SUCCEEDED(dev->GetId(&s)) && s) { id = s; CoTaskMemFree(s); }
        if (name)
        {
            IPropertyStore* props = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props)
            {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) *name = v.pwszVal;
                PropVariantClear(&v);
                props->Release();
            }
        }
        dev->Release();
    }
    return id;
}

// the key a choice is kept under: the output's id, or "all"
std::wstring choiceKey()
{
    if (!perDevice()) return L"all";
    std::wstring id = defaultOutput();
    for (wchar_t& c : id) if (c == L'=' || c == L'[' || c == L']') c = L'_';
    return id.empty() ? L"all" : id;
}

class DeviceWatch final : public IMMNotificationClient {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --refs; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole && gWnd) PostMessageW(gWnd, WM_DEVICE, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
private:
    std::atomic<ULONG> refs { 1 };
};
DeviceWatch* gWatch = nullptr;

// --- the library

struct Entry { std::wstring file, name; bool headphones = false; };

std::vector<Entry> library()
{
    std::vector<Entry> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((gLib + L"\\*.pumproom").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string text;
        if (!readFile(gLib + L"\\" + fd.cFileName, text)) continue;
        pump::room::Profile p;
        std::string why;
        if (!pump::room::parseProfile(text.data(), text.size(), p, why)) continue;
        Entry e;
        e.file = fd.cFileName;
        e.name = p.name.empty() ? e.file.substr(0, e.file.size() - 9) : widen(p.name);
        e.headphones = p.headphones;
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.name.c_str(), -1,
                               b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return out;
}

// --- tray icon and messages

HICON makeIcon(int size)
{
    // three sliders: ink lines with amber knobs
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return LoadIcon(nullptr, IDI_APPLICATION);
    uint32_t* px = (uint32_t*)bits;
    std::fill(px, px + size * size, 0u);
    auto rect = [&](int x0, int y0, int x1, int y1, uint32_t c) {
        for (int y = std::max(0, y0); y < std::min(size, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(size, x1); ++x) px[y * size + x] = c;
    };
    const uint32_t ink = 0xFF6E4300, amber = 0xFFFFC86B;
    const float u = size / 16.0f;
    const int knob[3] = { 9, 4, 11 };
    for (int i = 0; i < 3; ++i)
    {
        const int cx = (int)((3 + i * 5) * u);
        rect(cx - (int)std::max(1.0f, u * 0.6f), (int)(1 * u), cx + (int)std::max(1.0f, u * 0.6f) + 1, (int)(15 * u), ink);
        rect(cx - (int)(2 * u), (int)((knob[i] - 1.5f) * u), cx + (int)(2 * u) + 1, (int)((knob[i] + 1.5f) * u), ink);
        rect(cx - (int)(1.2f * u), (int)((knob[i] - 0.8f) * u), cx + (int)(1.2f * u) + 1, (int)((knob[i] + 0.8f) * u), amber);
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
}

void addIcon()
{
    gIcon.cbSize = sizeof(gIcon);
    gIcon.hWnd = gWnd;
    gIcon.uID = 1;
    gIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    gIcon.uCallbackMessage = WM_TRAY;
    gIcon.hIcon = makeIcon(GetSystemMetrics(SM_CXSMICON) >= 24 ? 32 : 16);
    lstrcpynW(gIcon.szTip, kTitle, ARRAYSIZE(gIcon.szTip));
    Shell_NotifyIconW(NIM_ADD, &gIcon);
    gIcon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &gIcon);
}

void notify(const std::wstring& title, const std::wstring& text)
{
    NOTIFYICONDATAW n = gIcon;
    n.uFlags = NIF_INFO;
    lstrcpynW(n.szInfoTitle, title.c_str(), ARRAYSIZE(n.szInfoTitle));
    lstrcpynW(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo));
    n.dwInfoFlags = NIIF_NONE;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void setTip(const std::wstring& active)
{
    std::wstring tip = std::wstring(kTitle) + (active.empty() ? L": no profile" : L": " + active);
    lstrcpynW(gIcon.szTip, tip.c_str(), ARRAYSIZE(gIcon.szTip));
    gIcon.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &gIcon);
}

// --- switching

struct Built {
    std::wstring file, name, error;
    DWORD code = 0;
};

std::wstring describe(DWORD code)
{
    if (code == ERROR_ACCESS_DENIED) return L"Windows didn't allow changes to the equalizer's config folder: choose Allow Changes\x2026 in the menu.";
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   (LPWSTR)&buf, 0, nullptr);
    std::wstring s = buf ? buf : L"error";
    LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L'.')) s.pop_back();
    return s;
}

// write the profile in `file` (in the library; empty: none) as the active one, on a worker thread
void applyFile(const std::wstring& file, bool announce)
{
    if (gApo.empty()) { gProblem = L"The system-wide equalizer for Windows isn't installed."; return; }
    if (gBusy.exchange(true)) return;
    const std::wstring lib = gLib, apo = gApo;
    std::thread([file, lib, apo, announce]() {
        auto* r = new Built();
        r->file = file;
        std::string active = pump::apo::emptyActive();
        if (!file.empty())
        {
            std::string text, why;
            pump::room::Profile p;
            if (!readFile(lib + L"\\" + file, text) || !pump::room::parseProfile(text.data(), text.size(), p, why))
                r->error = L"That file isn't a pump ROOM profile.";
            else
            {
                r->name = widen(p.name.empty() ? narrow(file) : p.name);
                const std::string folder = pump::apo::folderName(p.name.empty() ? narrow(file) : p.name);
                const pump::apo::ApoFiles out = pump::apo::buildApoFiles(p, folder);
                makeDirs(apo + L"\\pump-room\\" + widen(folder));
                for (const auto& f : out.files)
                    if (!r->code) r->code = writeFile(apo + L"\\" + widen(f.first), f.second.data(), f.second.size());
                active = out.active;
            }
        }
        if (r->error.empty() && !r->code)
            r->code = writeFile(apo + L"\\" + widen(pump::apo::kActiveFile), active.data(), active.size());
        if (r->code) r->error = describe(r->code);
        if (!PostMessageW(gWnd, WM_BUILT, announce ? 1 : 0, (LPARAM)r)) delete r;
    }).detach();
}

// the choice for the current output, applied
void applyChoice(bool announce)
{
    const std::wstring file = iniGet(L"choices", choiceKey());
    if (file == iniGet(L"state", L"applied", L"\x01")) return;   // already what the equalizer has
    applyFile(file, announce);
}

void choose(const std::wstring& file)
{
    iniSet(L"choices", choiceKey(), file);
    applyFile(file, true);
}

// copy files into the library (same name: replaced); the names copied
std::vector<std::wstring> addToLibrary(const std::vector<std::wstring>& paths)
{
    std::vector<std::wstring> added;
    makeDirs(gLib);
    for (const std::wstring& src : paths)
    {
        std::string text, why;
        pump::room::Profile p;
        if (!readFile(src, text) || !pump::room::parseProfile(text.data(), text.size(), p, why))
        {
            notify(L"Not a profile", fileNameOf(src) + L" isn't a pump ROOM profile.");
            continue;
        }
        const std::wstring name = fileNameOf(src), dst = gLib + L"\\" + name;
        if (CompareStringOrdinal(src.c_str(), -1, dst.c_str(), -1, TRUE) == CSTR_EQUAL || CopyFileW(src.c_str(), dst.c_str(), FALSE))
            added.push_back(name);
    }
    return added;
}

void openFiles(const std::vector<std::wstring>& paths)
{
    const std::vector<std::wstring> added = addToLibrary(paths);
    if (!added.empty()) choose(added.back());
}

void addDialog()
{
    std::vector<wchar_t> buf(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gWnd;
    ofn.lpstrFilter = L"pump ROOM profiles (*.pumproom)\0*.pumproom\0All files\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = L"Add pump ROOM profiles";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    SetForegroundWindow(gWnd);
    if (!GetOpenFileNameW(&ofn)) return;
    std::vector<std::wstring> paths;
    const std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (*p == L'\0') paths.push_back(first);   // one file: the whole path
    else for (; *p; p += wcslen(p) + 1) paths.push_back(first + L"\\" + p);
    const std::vector<std::wstring> added = addToLibrary(paths);
    if (added.size() == 1) choose(added[0]);
    else if (!added.empty()) notify(L"Profiles added", std::to_wstring(added.size()) + L" profiles are in the menu now.");
}

// lets users change the config folder (the administrator prompt), then sets up and applies again
void allowChanges()
{
    if (gApo.empty()) return;
    const std::wstring params = L"\"" + gApo + L"\" /grant *S-1-5-32-545:(OI)(CI)M /T /Q";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = gWnd;
    sei.lpVerb = L"runas";
    sei.lpFile = L"icacls.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return;
    WaitForSingleObject(sei.hProcess, 30000);
    CloseHandle(sei.hProcess);
    DWORD err = 0;
    if (ensureInclude(&err)) { gProblem.clear(); iniSet(L"state", L"applied", L"\x01"); applyChoice(true); }
    else gProblem = describe(err);
}

// --- login item and file type

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring exePath()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return std::wstring(buf, n);
}
bool loginOn()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kTitle, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}
void setLogin(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on)
    {
        const std::wstring v = L"\"" + exePath() + L"\"";
        RegSetValueExW(k, kTitle, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    else RegDeleteValueW(k, kTitle);
    RegCloseKey(k);
}

void setKey(const std::wstring& path, const std::wstring& value)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

void registerFileType()
{
    const std::wstring exe = exePath();
    setKey(L"Software\\Classes\\.pumproom", L"wake.pumproom");
    setKey(L"Software\\Classes\\wake.pumproom", L"pump ROOM profile");
    setKey(L"Software\\Classes\\wake.pumproom\\DefaultIcon", L"\"" + exe + L"\",0");
    setKey(L"Software\\Classes\\wake.pumproom\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// --- the menu

void showMenu()
{
    const std::vector<Entry> lib = library();
    const std::wstring current = iniGet(L"choices", choiceKey());
    HMENU menu = CreatePopupMenu();
    for (int phones = 0; phones < 2; ++phones)
    {
        HMENU sub = CreatePopupMenu();
        int n = 0;
        for (size_t i = 0; i < lib.size(); ++i)
        {
            if (lib[i].headphones != (phones == 1)) continue;
            std::wstring label = lib[i].name;
            for (size_t p = 0; (p = label.find(L'&', p)) != std::wstring::npos; p += 2) label.insert(p, L"&");
            AppendMenuW(sub, MF_STRING | (lib[i].file == current ? MF_CHECKED : 0), (phones ? ID_PHONES : ID_ROOM) + (UINT)i, label.c_str());
            ++n;
        }
        if (n == 0) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, phones ? L"No headphone profiles yet" : L"No room profiles yet");
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING | (current.empty() ? MF_GRAYED : 0), ID_NONE, L"No Profile");
        AppendMenuW(sub, MF_STRING, ID_ADD, L"Add Profiles\x2026");
        AppendMenuW(sub, MF_STRING, ID_FOLDER, L"Show Profiles Folder");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, phones ? L"Headphones" : L"Room Profiles");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring outName;
    defaultOutput(&outName);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"Output: " + (outName.empty() ? std::wstring(L"none") : outName)).c_str());
    if (gApo.empty())
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Install the free system-wide equalizer for Windows first");
    else if (!gProblem.empty())
    {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, gProblem.substr(0, 120).c_str());
        AppendMenuW(menu, MF_STRING, ID_ALLOW, L"Allow Changes\x2026");
        AppendMenuW(menu, MF_STRING, ID_SETUP, L"Try Again");
    }
    AppendMenuW(menu, MF_STRING | (perDevice() ? MF_CHECKED : 0), ID_PERDEVICE, L"Separate Profile for Each Output");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (loginOn() ? MF_CHECKED : 0), ID_LOGIN, L"Open at Login");
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit Room Profiles");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(gWnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, gWnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(gWnd, WM_NULL, 0, 0);

    if (cmd >= ID_ROOM && cmd < ID_ROOM + lib.size()) choose(lib[cmd - ID_ROOM].file);
    else if (cmd >= ID_PHONES && cmd < ID_PHONES + lib.size()) choose(lib[cmd - ID_PHONES].file);
    else if (cmd == ID_NONE) choose(L"");
    else if (cmd == ID_ADD) addDialog();
    else if (cmd == ID_FOLDER) { makeDirs(gLib); ShellExecuteW(nullptr, L"open", gLib.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    else if (cmd == ID_PERDEVICE)
    {
        // the current choice carries over into the other mode when it has none there yet
        const std::wstring now = iniGet(L"choices", choiceKey());
        iniSet(L"options", L"perDevice", perDevice() ? L"0" : L"1");
        if (iniGet(L"choices", choiceKey(), L"\x01") == L"\x01") iniSet(L"choices", choiceKey(), now);
        applyChoice(false);
    }
    else if (cmd == ID_LOGIN) setLogin(!loginOn());
    else if (cmd == ID_ALLOW) allowChanges();
    else if (cmd == ID_SETUP) { DWORD err = 0; if (ensureInclude(&err)) { gProblem.clear(); iniSet(L"state", L"applied", L"\x01"); applyChoice(true); } else gProblem = describe(err); }
    else if (cmd == ID_QUIT) DestroyWindow(gWnd);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == gTaskbarCreated && gTaskbarCreated) { addIcon(); return 0; }
    switch (msg)
    {
    case WM_TRAY:
        switch (LOWORD(lp))
        {
        case WM_CONTEXTMENU: case NIN_SELECT: case NIN_KEYSELECT: showMenu(); break;
        default: break;
        }
        return 0;
    case WM_DEVICE:
        applyChoice(false);
        return 0;
    case WM_BUILT:
    {
        std::unique_ptr<Built> r((Built*)lp);
        gBusy = false;
        if (!r->error.empty())
        {
            gProblem = r->error;
            notify(L"Couldn't switch profiles", r->error);
            return 0;
        }
        gProblem.clear();
        iniSet(L"state", L"applied", r->file);
        setTip(r->name);
        if (wp) notify(r->file.empty() ? L"No profile" : r->name, r->file.empty() ? L"Room correction is off for this output." : L"Now playing through this profile.");
        // a choice made while this one was being written: catch up
        if (iniGet(L"choices", choiceKey()) != r->file) applyChoice(false);
        return 0;
    }
    case WM_COPYDATA:
    {
        const COPYDATASTRUCT* cd = (const COPYDATASTRUCT*)lp;
        if (cd && cd->dwData == 0x70756D70 && cd->lpData && cd->cbData >= sizeof(wchar_t))
        {
            std::wstring all((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t));
            std::vector<std::wstring> paths;
            for (size_t a = 0, b; a < all.size(); a = b + 1)
            {
                b = all.find(L'\n', a);
                if (b == std::wstring::npos) b = all.size();
                if (b > a) paths.push_back(all.substr(a, b - a));
            }
            openFiles(paths);
        }
        return TRUE;
    }
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &gIcon);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(h, msg, wp, lp);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> files;
    for (int i = 1; argv && i < argc; ++i) files.push_back(argv[i]);
    LocalFree(argv);

    // one at a time: a second start hands its files to the first
    HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\pumpRoomProfiles");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND other = FindWindowW(kClass, nullptr))
        {
            std::wstring all;
            for (const auto& f : files) all += f + L"\n";
            COPYDATASTRUCT cd = { 0x70756D70, (DWORD)(all.size() * sizeof(wchar_t)), all.data() };
            if (!all.empty()) SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cd);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const std::wstring base = knownFolder(FOLDERID_RoamingAppData) + L"\\wake\\pump ROOM";
    gLib = base + L"\\Profiles";
    gIni = base + L"\\room-profiles.ini";
    makeDirs(gLib);
    gApo = findApo();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // a hidden top-level window (not message-only: those miss the taskbar's restart broadcast and FindWindow)
    gWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, kTitle, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addIcon();
    registerFileType();

    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&gEnum)))
    {
        gWatch = new DeviceWatch();
        gEnum->RegisterEndpointNotificationCallback(gWatch);
    }

    if (gApo.empty())
        notify(kTitle, L"Install the free system-wide equalizer for Windows, then start Room Profiles again.");
    else
    {
        DWORD err = 0;
        if (!ensureInclude(&err))
        {
            gProblem = describe(err);
            notify(L"One more step", L"Choose Allow Changes\x2026 in Room Profiles' menu (by the clock) so it can switch profiles.");
        }
        else if (files.empty() && !iniGet(L"options", L"started", L"").empty())
        {
            iniSet(L"state", L"applied", L"\x01");
            applyChoice(false);
        }
        else if (files.empty())
            notify(kTitle, L"Room Profiles is by the clock: add profiles from pump ROOM, or double-click a .pumproom file.");
    }
    iniSet(L"options", L"started", L"1");
    if (!files.empty()) openFiles(files);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (gEnum)
    {
        if (gWatch) gEnum->UnregisterEndpointNotificationCallback(gWatch);
        gEnum->Release();
    }
    if (gWatch) gWatch->Release();
    CoUninitialize();
    if (once) CloseHandle(once);
    return 0;
}
