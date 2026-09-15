// Borderless fullscreen for resolutions other than the desktop's (BorderlessFullscreen=1, default).
//
// Exclusive fullscreen at a non-desktop mode renders black: the device comes back from CreateDevice already lost
// (TestCooperativeLevel D3DERR_DEVICENOTRESET immediately after 0x61DC28, game in the foreground, no other topmost
// windows; also with an X8R8G8B8 back buffer), and every Present fails with D3DERR_DEVICELOST. Re-creating the
// device is lost again the same way. At the desktop resolution there is no mode switch and it works.
//
// So when the configured resolution differs from the desktop: run windowed (ForceWindowed patch, shot.h), make the
// window a borderless popup covering the monitor, create the device with D3DSWAPEFFECT_COPY (required for a
// destination rectangle), and Present the back buffer scaled into an aspect-correct rectangle with black bars.
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
    if (!g_borderless || (w == g_deskW && h == g_deskH)) return;
    g_borderlessActive = true;
    g_forceWindowed = 1;
    Log("borderless fullscreen: %dx%d on a %dx%d desktop (exclusive fullscreen at a non-desktop mode loses the device)", w, h, g_deskW, g_deskH);
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
    // aspect-correct destination rectangle for the back buffer on the desktop
    double sx = (double)g_deskW / pp[0], sy = (double)g_deskH / pp[1];
    double s = sx < sy ? sx : sy;
    int w = (int)std::lround(pp[0] * s), hgt = (int)std::lround(pp[1] * s);
    g_presentDst.left = (g_deskW - w) / 2; g_presentDst.top = (g_deskH - hgt) / 2;
    g_presentDst.right = g_presentDst.left + w; g_presentDst.bottom = g_presentDst.top + hgt;
    HWND h = *(HWND*)0x75119C;
    if (h) ApplyBorderlessWindow(h);
    Log("borderless: back buffer %ux%u presented at %ld,%ld-%ld,%ld (swap effect COPY)", pp[0], pp[1],
        g_presentDst.left, g_presentDst.top, g_presentDst.right, g_presentDst.bottom);
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
