// Widescreen / high-resolution UI and movie fixes.
//
// 2D UI is authored in 640x480 pixels. The engine has UI scale factors [0x6AEA00]/[0x6AEA04] (set once to 1.0 by
// SetUIScale 0x5515A0 from 0x4E7B19) and a safe rect [0x7280F0..FC] (SetSafeRect 0x551570, margins 32/24 or full).
// Anchor helpers 0x4CB6D0 (X) / 0x4CB760 (Y): left/right/centre anchors add x*scale to the rect edge; unanchored X/Y
// add x*scale to 0 (0x4CB74D / 0x4CB7DD: xor ecx,ecx). Screen size: W [0x6B9B98], H [0x6B9B9C].
// Movies: 0x60E7B0 draws the Bink texture with 0x60A9F0(0,0,W,H,...) at 0x60E88B, i.e. stretched to the screen.
//
// Fix: menu scale = H/480 * MenuScale%; the 640x480 layout is centred (unanchored X/Y offsets), horizontal safe-rect
// anchors stay at the real screen edges and vertical ones follow the centred 480-line box, margins scaled.
// Movies drawn at 4:3 with the screen cleared to black first. The HUD has its own HUDScale% (hud_fix.h).
#pragma once

static int g_uiFix = 1, g_movieFix = 1;
static int g_menuScalePct = 90, g_hudScalePct = 75;     // percent of "fill the screen height" (100 = H/480)
static int g_uiLastW = 0, g_uiLastH = 0;
static int g_uiOffX = 0, g_uiOffY = 0;
static int g_rectMargins[4] = { 32, 24, 32, 24 };   // left, top, right, bottom as passed to SetSafeRect (last call)
static bool g_rectKnown = false;

static int  ScreenW() { return *(int*)0x6B9B98; }
static int  ScreenH() { return *(int*)0x6B9B9C; }
static float FitScale(int pct) {
    int h = ScreenH();
    float s = h > 0 ? h / 480.0f * pct / 100.0f : 1.0f;
    return s < 1.0f ? 1.0f : s;
}
static float UiScale() { return FitScale(g_menuScalePct); }

static void ApplyUiScale() {
    float s = UiScale();
    *(float*)0x6AEA00 = s;
    *(float*)0x6AEA04 = s;
    int w = ScreenW(), h = ScreenH();
    int offX = (int)((w - 640.0f * s) / 2.0f), offY = (int)((h - 480.0f * s) / 2.0f);
    g_uiOffX = offX > 0 ? offX : 0;
    g_uiOffY = offY > 0 ? offY : 0;
    if (g_rectKnown) {
        *(int*)0x7280F0 = (int)(g_rectMargins[0] * s);
        *(int*)0x7280F4 = w - (int)(g_rectMargins[2] * s);
        *(int*)0x7280F8 = g_uiOffY + (int)(g_rectMargins[1] * s);
        *(int*)0x7280FC = h - g_uiOffY - (int)(g_rectMargins[3] * s);
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
        Log("UI scale %.3f (menus %d%%, HUD %d%% = %.3f) for %dx%d (layout offset %d,%d)",
            UiScale(), g_menuScalePct, g_hudScalePct, FitScale(g_hudScalePct), w, h, g_uiOffX, g_uiOffY);
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

__declspec(naked) static void UnanchoredYStub() {           // replaces 0x4CB7DD: same instructions for Y
    __asm {
        mov ecx, g_uiOffY
        cvttss2si eax, xmm0
        add eax, ecx
        ret
    }
}

static bool WriteJmp(DWORD site, void* target, size_t len);

// Menu art sizes. The layout rect builder 0x4CB7F0 (used by images, sprites and the logo) scales positions through
// the anchor helpers but takes width as w*[0x6A6FDC] (always 1.0) and height raw. Width now uses [0x6AEA00]
// (displacement at 0x4CB812) and height goes through a stub at 0x4CB7F9. HUD/episode-map passes run with scale 1.0,
// so they are unchanged.
__declspec(naked) static void RectHeightStub() {            // replaces 0x4CB7F9: mov ebx,[esi+0Ch]; push edi; mov edi,[esi+10h]
    __asm {
        cvtsi2ss xmm0, dword ptr [esi + 0x0C]
        mulss xmm0, dword ptr ds:[0x6AEA04]
        cvttss2si ebx, xmm0
        push edi
        mov edi, dword ptr [esi + 0x10]
        push 0x4CB800
        ret
    }
}

// Shell streak lines (0x55AEA0, drawn by the shell manager 0x55D940): Y = y*[0x6AEA04] with no layout offset,
// thickness [obj+34h]*32 raw, length t*1920*[0x6A6FDC] raw. Add the vertical layout offset and scale thickness and
// length by the menu scale.
__declspec(naked) static void StreakThicknessStub() {       // replaces 0x55B0DD: mulss xmm0,[0x67BE10]; cvttss2si esi,xmm0
    __asm {
        mulss xmm0, dword ptr ds:[0x67BE10]
        mulss xmm0, dword ptr ds:[0x6AEA04]
        cvttss2si esi, xmm0
        push 0x55B0E9
        ret
    }
}

__declspec(naked) static void StreakYStub() {               // replaces 0x55B10D: cvttss2si esi,xmm0; cvtsi2ss xmm0,edx
    __asm {
        cvttss2si esi, xmm0
        add esi, g_uiOffY
        cvtsi2ss xmm0, edx
        push 0x55B115
        ret
    }
}

static bool PatchBytes(DWORD site, const BYTE* bytes, size_t len) {
    DWORD old;
    if (!VirtualProtect((LPVOID)site, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)site, bytes, len);
    VirtualProtect((LPVOID)site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, len);
    return true;
}

static void MenuArtFixInstall() {
    const BYTE rectH[]   = { 0x8B, 0x5E, 0x0C, 0x57, 0x8B, 0x7E, 0x10 };
    const BYTE rectW[]   = { 0xF3, 0x0F, 0x59, 0x05, 0xDC, 0x6F, 0x6A, 0x00 };
    const BYTE strkH[]   = { 0xF3, 0x0F, 0x59, 0x05, 0x10, 0xBE, 0x67, 0x00, 0xF3, 0x0F, 0x2C, 0xF0 };
    const BYTE strkY[]   = { 0xF3, 0x0F, 0x2C, 0xF0, 0xF3, 0x0F, 0x2A, 0xC2 };
    const BYTE strkW[]   = { 0xF3, 0x0F, 0x59, 0x05, 0xDC, 0x6F, 0x6A, 0x00 };
    const BYTE scaleXDisp[] = { 0x00, 0xEA, 0x6A, 0x00 };  // [0x6AEA00]
    if (memcmp((BYTE*)0x4CB7F9, rectH, sizeof(rectH)) || memcmp((BYTE*)0x4CB80E, rectW, sizeof(rectW))) {
        Log("layout rect builder bytes differ, menu art size fix not installed");
    } else {
        WriteJmp(0x4CB7F9, &RectHeightStub, sizeof(rectH));
        PatchBytes(0x4CB812, scaleXDisp, 4);
        Log("menu art size fix installed (0x4CB7F9, 0x4CB812)");
    }
    if (memcmp((BYTE*)0x55B0DD, strkH, sizeof(strkH)) || memcmp((BYTE*)0x55B10D, strkY, sizeof(strkY)) ||
        memcmp((BYTE*)0x55B115, strkW, sizeof(strkW))) {
        Log("streak draw bytes differ, streak fix not installed");
    } else {
        WriteJmp(0x55B0DD, &StreakThicknessStub, sizeof(strkH));
        WriteJmp(0x55B10D, &StreakYStub, sizeof(strkY));
        PatchBytes(0x55B119, scaleXDisp, 4);
        Log("streak line fix installed (0x55B0DD, 0x55B10D, 0x55B119)");
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
            memcmp((BYTE*)0x4CB74D, anchOrig, sizeof(anchOrig)) || memcmp((BYTE*)0x4CB7DD, anchOrig, sizeof(anchOrig))) {
            Log("UI scale site bytes differ, UI fix not installed");
            g_uiFix = 0;
        } else {
            WriteJmp(0x5515A0, &SetUIScaleHook, 5);
            WriteJmp(0x551570, &SetSafeRectHook, 5);
            WriteJmp(0x4CB74D, &UnanchoredXStub, 9);
            WriteJmp(0x4CB7DD, &UnanchoredYStub, 9);
            Log("UI scale fix installed (0x5515A0, 0x551570, 0x4CB74D, 0x4CB7DD)");
            MenuArtFixInstall();
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
