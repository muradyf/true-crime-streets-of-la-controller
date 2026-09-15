// Borderless fullscreen: BorderlessFullscreen=1 (default) at resolutions other than the desktop's, 2 = always.
//
// Exclusive fullscreen at a non-desktop mode renders black: the device comes back from CreateDevice already lost
// (TestCooperativeLevel D3DERR_DEVICENOTRESET immediately after 0x61DC28, game in the foreground, no other topmost
// windows; also with an X8R8G8B8 back buffer), and every Present fails with D3DERR_DEVICELOST. Re-creating the
// device is lost again the same way.
// At the desktop resolution exclusive fullscreen works but is slow to come back from alt-tab: the game releases the
// device on deactivate and re-creates it on activate, and the exclusive CreateDevice alone takes 3.2-4.9 s (2560x1600,
// no refresh-rate change; measured with the device step log). A windowed device is created in ~100 ms and is not
// released on alt-tab at all.
//
// So when enabled: run windowed (ForceWindowed patch, shot.h), make the window a borderless popup covering the
// monitor, create the device with D3DSWAPEFFECT_COPY (required for a destination rectangle), and Present the back
// buffer scaled into an aspect-correct rectangle with black bars (1:1 at the desktop resolution).
#pragma once

static int g_borderless = 1;
static bool g_borderlessActive = false;
static int g_deskW = 0, g_deskH = 0;
static RECT g_presentDst = {};
static PresentFn g_borderlessOrigPresent = nullptr;
static void** g_borderlessVtable = nullptr;
static int g_borderlessFrames = 0;

static void BorderlessDecide() {
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return;
    g_deskW = (int)dm.dmPelsWidth; g_deskH = (int)dm.dmPelsHeight;

    char dir[MAX_PATH]; GetModuleFileNameA(nullptr, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\'); if (slash) slash[1] = 0;
    char wsfAsi[MAX_PATH], wsfIni[MAX_PATH], gameIni[MAX_PATH];
    sprintf_s(wsfAsi, "%sscripts\\TrueCrimeStreetsofLA.WidescreenFix.asi", dir);
    sprintf_s(wsfIni, "%sscripts\\TrueCrimeStreetsofLA.WidescreenFix.ini", dir);
    sprintf_s(gameIni, "%sTrueCrime.ini", dir);
    int w, h;
    if (GetFileAttributesA(wsfAsi) != INVALID_FILE_ATTRIBUTES) {          // the widescreen fix decides the resolution
        w = GetPrivateProfileIntA("MAIN", "ResX", 0, wsfIni); h = GetPrivateProfileIntA("MAIN", "ResY", 0, wsfIni);
        if (w <= 0 || h <= 0) { w = g_deskW; h = g_deskH; }
    } else {
        w = GetPrivateProfileIntA("Renderer", "ScreenWidth", g_deskW, gameIni); h = GetPrivateProfileIntA("Renderer", "ScreenHeight", g_deskH, gameIni);
    }
    if (!g_borderless || (g_borderless == 1 && w == g_deskW && h == g_deskH)) return;   // 2 = also at the desktop resolution
    g_borderlessActive = true;
    g_forceWindowed = 1;
    Log("borderless fullscreen: %dx%d on a %dx%d desktop", w, h, g_deskW, g_deskH);
}

static void ApplyBorderlessWindow(HWND h) {
    const LONG style = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    if (GetWindowLongA(h, GWL_STYLE) != style) SetWindowLongA(h, GWL_STYLE, style);
    LONG ex = GetWindowLongA(h, GWL_EXSTYLE) & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
    SetWindowLongA(h, GWL_EXSTYLE, ex);
    RECT r; GetWindowRect(h, &r);
    if (r.left != 0 || r.top != 0 || r.right != g_deskW || r.bottom != g_deskH)
        SetWindowPos(h, nullptr, 0, 0, g_deskW, g_deskH, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

static void FillBars(HWND h) {
    HDC dc = GetDC(h);
    if (!dc) return;
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    const RECT& d = g_presentDst;
    RECT bars[4] = { { 0, 0, g_deskW, d.top }, { 0, d.bottom, g_deskW, g_deskH }, { 0, d.top, d.left, d.bottom }, { d.right, d.top, g_deskW, d.bottom } };
    for (const RECT& b : bars) if (b.right > b.left && b.bottom > b.top) FillRect(dc, &b, black);
    ReleaseDC(h, dc);
}

static HRESULT __stdcall BorderlessPresent(void* dev, const RECT* src, const RECT* dst, HWND wnd, void* dirty) {
    HWND h = *(HWND*)0x75119C;
    if (h && (++g_borderlessFrames % 60) == 1) ApplyBorderlessWindow(h);   // the game may restyle/resize its window
    if (h && (g_borderlessFrames % 30) == 1) FillBars(h);
    return g_borderlessOrigPresent(dev, src, dst ? dst : &g_presentDst, wnd, dirty);
}

// Called before CreateDevice (0x61DBE2 stub): windowed devices need D3DSWAPEFFECT_COPY for a destination rectangle.
static void __cdecl BorderlessAdjustPresentParams(DWORD renderer) {
    if (!g_borderlessActive) return;
    UINT* pp = (UINT*)(renderer + 0x528);
    if (pp[7] != 1) { Log("borderless: device requested fullscreen (windowed %u), not adjusting", pp[7]); return; }
    pp[5] = 3;   // D3DSWAPEFFECT_COPY
    HWND h = *(HWND*)0x75119C;
    if (h) ApplyBorderlessWindow(h);
}

__declspec(naked) static void BorderlessBeforeCreateStub() {   // replaces 0x61DBE2: test byte [ecx+564h],10h
    __asm {
        pushad
        push ecx
        call BorderlessAdjustPresentParams
        add esp, 4
        popad
        test byte ptr [ecx + 0x564], 0x10
        push 0x61DBE9
        ret
    }
}

static void BorderlessOnDeviceCreated() {                  // from DeviceRecovered, after every successful CreateDevice
    if (!g_borderlessActive) return;
    DWORD renderer = *(DWORD*)0x72C024;
    void* dev = renderer ? *(void**)(renderer + 0x560) : nullptr;
    if (!dev) return;
    // Aspect-correct destination rectangle for the back buffer on the desktop. Computed after CreateDevice: before it
    // (0x61DBE2) the parameters still hold the old window's client size (2538x1544 logged for a 2560x1600 device);
    // the widescreen fix's hook at 0x61DC12 sets the final size.
    UINT* pp = (UINT*)(renderer + 0x528);
    if (pp[0] && pp[1]) {
        double sx = (double)g_deskW / pp[0], sy = (double)g_deskH / pp[1];
        double s = sx < sy ? sx : sy;
        int w = (int)std::lround(pp[0] * s), hgt = (int)std::lround(pp[1] * s);
        RECT r = { (g_deskW - w) / 2, (g_deskH - hgt) / 2, 0, 0 };
        r.right = r.left + w; r.bottom = r.top + hgt;
        if (memcmp(&r, &g_presentDst, sizeof(r))) {
            g_presentDst = r;
            Log("borderless: back buffer %ux%u presented at %ld,%ld-%ld,%ld (swap effect %u)", pp[0], pp[1], r.left, r.top, r.right, r.bottom, pp[5]);
        }
    }
    void** vt = *(void***)dev;
    if (vt == g_borderlessVtable) return;                  // vtable is shared by re-created devices
    DWORD old;
    if (!VirtualProtect(&vt[15], sizeof(void*), PAGE_READWRITE, &old)) return;
    g_borderlessOrigPresent = (PresentFn)vt[15];
    vt[15] = (void*)&BorderlessPresent;
    VirtualProtect(&vt[15], sizeof(void*), old, &old);
    g_borderlessVtable = vt;
    Log("borderless: Present hook installed");
}

static void BorderlessInstall() {
    if (!g_borderlessActive) return;
    const BYTE t[] = { 0xF6, 0x81, 0x64, 0x05, 0x00, 0x00, 0x10 };
    if (memcmp((BYTE*)0x61DBE2, t, sizeof(t))) { Log("create device entry bytes differ, borderless fullscreen not installed"); g_borderlessActive = false; return; }
    WriteJmp(0x61DBE2, &BorderlessBeforeCreateStub, sizeof(t));
    Log("borderless fullscreen installed (0x61DBE2)");
}
