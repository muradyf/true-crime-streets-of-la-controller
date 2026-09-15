// Widescreen / high-resolution UI and movie fixes.
//
// 2D UI is authored in 640x480 pixels. The engine has UI scale factors [0x6AEA00]/[0x6AEA04] (set once to 1.0 by
// SetUIScale 0x5515A0 from 0x4E7B19) and a safe rect [0x7280F0..FC] (SetSafeRect 0x551570, margins 32/24 or full).
// Anchor helpers 0x4CB6D0 (X) / 0x4CB760 (Y): left/right/centre anchors add x*scale to the rect edge; unanchored X
// adds x*scale to 0 (0x4CB74D: xor ecx,ecx). Screen size: W [0x6B9B98], H [0x6B9B9C].
// Movies: 0x60E7B0 draws the Bink texture with 0x60A9F0(0,0,W,H,...) at 0x60E88B, i.e. stretched to the screen.
//
// Fix: scale = H/480; safe-rect margins scaled by the same factor (edge-anchored HUD stays at the real edges);
// unanchored X offset by (W - H*4/3)/2 so full-layout screens are centred in a 4:3 area; movies drawn at 4:3 with
// the screen cleared to black first.
#pragma once

static int g_uiFix = 1, g_movieFix = 1;
static int g_uiLastW = 0, g_uiLastH = 0;
static int g_uiOffX = 0;
static int g_rectMargins[4] = { 32, 24, 32, 24 };   // left, top, right, bottom as passed to SetSafeRect (last call)
static bool g_rectKnown = false;

static int  ScreenW() { return *(int*)0x6B9B98; }
static int  ScreenH() { return *(int*)0x6B9B9C; }
static float UiScale() { int h = ScreenH(); return h > 0 ? h / 480.0f : 1.0f; }

static void ApplyUiScale() {
    float s = UiScale();
    *(float*)0x6AEA00 = s;
    *(float*)0x6AEA04 = s;
    int w = ScreenW(), h = ScreenH();
    int off = (int)((w - h * 4.0f / 3.0f) / 2.0f);
    g_uiOffX = off > 0 ? off : 0;
    if (g_rectKnown) {
        *(int*)0x7280F0 = (int)(g_rectMargins[0] * s);
        *(int*)0x7280F8 = (int)(g_rectMargins[1] * s);
        *(int*)0x7280F4 = w - (int)(g_rectMargins[2] * s);
        *(int*)0x7280FC = h - (int)(g_rectMargins[3] * s);
    }
}

static void __cdecl SetUIScaleHook(float, float) {       // replaces 0x5515A0
    ApplyUiScale();
}

static void __cdecl SetSafeRectHook(int l, int t, int r, int b) {   // replaces 0x551570
    int w = ScreenW(), h = ScreenH();
    g_rectMargins[0] = l; g_rectMargins[1] = t; g_rectMargins[2] = w - r; g_rectMargins[3] = h - b;
    g_rectKnown = true;
    ApplyUiScale();
}

static void UiFixUpdate() {                                 // called every input update
    if (!g_uiFix) return;
    int w = ScreenW(), h = ScreenH();
    if (w != g_uiLastW || h != g_uiLastH) {
        g_uiLastW = w; g_uiLastH = h;
        ApplyUiScale();
        Log("UI scale %.3f for %dx%d (4:3 offset %d)", UiScale(), w, h, g_uiOffX);
    }
}

__declspec(naked) static void UnanchoredXStub() {           // replaces 0x4CB74D: xor ecx,ecx; cvttss2si eax,xmm0; add eax,ecx; ret
    __asm {
        mov ecx, g_uiOffX
        cvttss2si eax, xmm0
        add eax, ecx
        ret
    }
}

static void __cdecl AdjustMovieRect(float* a) {             // a = x0, y0, x1, y1
    float w = a[2] - a[0], h = a[3] - a[1];
    float target = h * 4.0f / 3.0f;
    static float s_loggedW = 0, s_loggedH = 0;
    if (cfg.debugLog && (w != s_loggedW || h != s_loggedH)) {
        s_loggedW = w; s_loggedH = h;
        Log("movie quad %.0f,%.0f-%.0f,%.0f -> x %.0f-%.0f", a[0], a[1], a[2], a[3],
            target < w - 1.0f ? a[0] + (w - target) / 2.0f : a[0], target < w - 1.0f ? a[0] + (w + target) / 2.0f : a[2]);
    }
    if (target >= w - 1.0f) return;
    float x0 = a[0] + (w - target) / 2.0f;
    a[0] = x0; a[2] = x0 + target;
    void* dev = *(void**)0x72C014;
    if (dev) {
        void** vt = *(void***)dev;
        ((HRESULT(__stdcall*)(void*, DWORD, void*, DWORD, DWORD, float, DWORD))vt[0x90 / 4])(dev, 0, nullptr, 1 /*TARGET*/, 0xFF000000, 1.0f, 0);
    }
}

__declspec(naked) static void MovieQuadStub() {             // replaces call 0x60A9F0 at 0x60E88B (thiscall, 9 float args)
    __asm {
        pushad
        lea eax, [esp + 0x24]                               // x0 (after pushad 32 bytes + return address)
        push eax
        call AdjustMovieRect
        add esp, 4
        popad
        mov eax, 0x60A9F0
        jmp eax
    }
}

static bool WriteJmp(DWORD site, void* target, size_t len) {
    DWORD old;
    if (!VirtualProtect((LPVOID)site, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    BYTE* p = (BYTE*)site;
    p[0] = 0xE9;
    *(int*)(p + 1) = (int)((BYTE*)target - (p + 5));
    for (size_t i = 5; i < len; ++i) p[i] = 0x90;
    VirtualProtect((LPVOID)site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, len);
    return true;
}

static void UiFixInstall() {
    if (g_uiFix) {
        const BYTE scaleOrig[] = { 0xF3, 0x0F, 0x10, 0x44, 0x24, 0x04 };
        const BYTE rectOrig[]  = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08 };
        const BYTE anchOrig[]  = { 0x33, 0xC9, 0xF3, 0x0F, 0x2C, 0xC0, 0x03, 0xC1, 0xC3 };
        if (memcmp((BYTE*)0x5515A0, scaleOrig, sizeof(scaleOrig)) || memcmp((BYTE*)0x551570, rectOrig, sizeof(rectOrig)) ||
            memcmp((BYTE*)0x4CB74D, anchOrig, sizeof(anchOrig))) {
            Log("UI scale site bytes differ, UI fix not installed");
            g_uiFix = 0;
        } else {
            WriteJmp(0x5515A0, &SetUIScaleHook, 5);
            WriteJmp(0x551570, &SetSafeRectHook, 5);
            WriteJmp(0x4CB74D, &UnanchoredXStub, 9);
            Log("UI scale fix installed (0x5515A0, 0x551570, 0x4CB74D)");
        }
    }
    if (g_movieFix) {
        BYTE* p = (BYTE*)0x60E88B;
        const BYTE callOrig[] = { 0xE8, 0x60, 0xC1, 0xFF, 0xFF };
        if (memcmp(p, callOrig, sizeof(callOrig))) { Log("movie draw call bytes differ, movie fix not installed"); return; }
        DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(p + 1) = (int)((BYTE*)&MovieQuadStub - (p + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        Log("movie 4:3 fix installed (0x60E88B)");
    }
}
