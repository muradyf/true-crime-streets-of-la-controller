// Widescreen / high-resolution UI and movie fixes.
//
// 2D UI is authored in 640x480 pixels. The engine has UI scale factors [0x6AEA00]/[0x6AEA04] (set once to 1.0 by
// SetUIScale 0x5515A0 from 0x4E7B19) and a safe rect [0x7280F0..FC] (SetSafeRect 0x551570, margins 32/24 or full).
// Anchor helpers 0x4CB6D0 (X) / 0x4CB760 (Y): left/right/centre anchors add x*scale to the rect edge; unanchored X/Y
// add x*scale to 0 (0x4CB74D / 0x4CB7DD: xor ecx,ecx). Screen size: W [0x6B9B98], H [0x6B9B9C].
// Movies: 0x60E7B0 draws the Bink texture with 0x60A9F0(0,0,W,H,...) at 0x60E88B, i.e. stretched to the screen.
//
// Fix: menu scale = H/480 * MenuScale%; the 640x480 layout is centred (unanchored X/Y offsets) and the safe-rect
// anchors follow the centred 640x480 box with scaled margins, so left-, right- and unanchored items keep the 4:3
// composition (right-aligned menu items used to hug the real screen edge while the logo sat in the box).
// Movies and the loading screen (Title.xpr, same quad call) are drawn at 4:3 with the screen cleared to black first.
// The HUD has its own HUDScale% and screen-edge layout (hud_fix.h).
#pragma once

static int g_uiFix = 1, g_movieFix = 1;
static int g_menuBgAspect = 1;   // 0 = let the menu background stretch to the full screen, bars and all
static int g_menuScalePct = 90, g_hudScalePct = 75;     // percent of "fill the screen height" (100 = H/480)
static int g_subtitleScalePct = 0;                      // same, for cutscene subtitles; 0 = follow the menu size
static int g_reticleScale = 0;                          // whole-number aim reticle scale; 0 = follow the HUD size
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
        *(int*)0x7280F0 = g_uiOffX + (int)(g_rectMargins[0] * s);
        *(int*)0x7280F4 = w - g_uiOffX - (int)(g_rectMargins[2] * s);
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

// Mission objective counter ("0/10", top right, drawn at 0x4DBEE0). It positions itself against the screen -
// x = GetScreenW() - 8, y = 14 - and passes both through the anchor helpers with no anchor flags, which lands them on
// the unanchored path. That path is one of the sites UIScaleFix replaces, and the replacement adds the offset that
// centres a 640x480 layout on a wider screen. The counter's coordinate is already screen-absolute, so the offset is
// added to something that never needed it:
//
//     traced in the HUD pass, virtual screen 1280x800, offsets 320,160
//     x = (1280 - 8) * 1.0 + 320 = 1592   on a 1280 wide screen - 312 past the right edge
//     y = (14)       * 1.0 + 160 = 174
//
// which is why the counter vanished the moment UIScaleFix was turned on, while its font, transform and draw call were
// all perfectly healthy. Take the offset back off again for this one element.
static int __cdecl CounterAnchorX(int v, int flags) {
    return ((int(__cdecl*)(int, int))0x4CB6D0)(v, flags) - g_uiOffX;
}

static int __cdecl CounterAnchorY(int v, int flags) {
    return ((int(__cdecl*)(int, int))0x4CB760)(v, flags) - g_uiOffY;
}

static void ObjectiveCounterFixInstall() {
    if (!g_uiFix) return;
    const BYTE origX[] = { 0xE8, 0xC2, 0xF7, 0xFE, 0xFF };   // call 0x4CB6D0
    const BYTE origY[] = { 0xE8, 0x46, 0xF8, 0xFE, 0xFF };   // call 0x4CB760
    if (memcmp((BYTE*)0x4DBF09, origX, sizeof(origX)) || memcmp((BYTE*)0x4DBF15, origY, sizeof(origY))) {
        Log("objective counter anchors differ, not patched");
        return;
    }
    DWORD old;
    VirtualProtect((LPVOID)0x4DBF09, 17, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(0x4DBF09 + 1) = (int)((BYTE*)&CounterAnchorX - (BYTE*)(0x4DBF09 + 5));
    *(int*)(0x4DBF15 + 1) = (int)((BYTE*)&CounterAnchorY - (BYTE*)(0x4DBF15 + 5));
    VirtualProtect((LPVOID)0x4DBF09, 17, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)0x4DBF09, 17);
    Log("objective counter fix installed (0x4DBF09, 0x4DBF15)");
}

// CounterTrace=1 (debug): the mission objective counter ("0/10", drawn at 0x4DBEE0) is reached - its position code
// runs - but no pixels appear anywhere on screen. 0x60B9C0 only builds its glyph matrix from the font's 2x2 transform
// when bit 0 of [font+0x22] is set, the same gate that turned out to be behind the subtitles, and 0x4DBECE clears
// that bit on a neighbouring font every frame. So log what the counter's own font [0x6D957C] actually holds at the
// moment of the draw: the flags, the transform, and the position it was handed.
static int g_counterTrace = 0;

static void __cdecl LogCounterFont(DWORD font, float x, float y) {
    if (!g_counterTrace) return;
    static int logged = 0;
    if (logged >= 6) return;
    ++logged;
    if (!font) { Log("counter text: font pointer is null"); return; }
    float* m = (float*)(font + 0x10);
    Log("counter text: font %08lX flags %04X transform %.3f %.3f %.3f %.3f at (%.1f,%.1f), UI scale %.3f, screen %dx%d",
        font, (unsigned)*(WORD*)(font + 0x22), m[0], m[1], m[2], m[3], x, y,
        *(float*)0x6AEA00, ScreenW(), ScreenH());
}

__declspec(naked) static void CounterTextStub() {           // replaces call 0x60B9C0 at 0x4DBF5C
    __asm {
        pushad
        // At entry: [esp] return address, +4 batch, +8 x, +12 y. pushad moves all of that down by 32, so y is at
        // +0x2C and x at +0x28; after y is pushed the second read is +0x2C again.
        push dword ptr [esp + 0x2C]                         // y
        push dword ptr [esp + 0x2C]                         // x
        push ecx                                            // font (this)
        call LogCounterFont
        add esp, 12
        popad
        mov eax, 0x60B9C0
        jmp eax                                             // tail call: 0x60B9C0 returns straight to 0x4DBF61
    }
}

static void CounterTraceInstall() {
    if (!g_counterTrace) return;
    const BYTE orig[] = { 0xE8, 0x5F, 0xFA, 0x12, 0x00 };   // call 0x60B9C0
    if (memcmp((BYTE*)0x4DBF5C, orig, sizeof(orig))) {
        Log("counter text call site differs, trace not installed");
        return;
    }
    DWORD old;
    VirtualProtect((LPVOID)0x4DBF5C, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(0x4DBF5C + 1) = (int)((BYTE*)&CounterTextStub - (BYTE*)(0x4DBF5C + 5));
    VirtualProtect((LPVOID)0x4DBF5C, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)0x4DBF5C, 5);
    Log("counter text trace installed (0x4DBF5C)");
}

// Shell background (ShellBG.xpr, [0x728338]): the shell manager draws it with 0x4CB1A0(dst, src, 1.0) at 0x55DEBA and
// 0x55DFAC, dst = {0, 0, W+1, H+1} (short x, y, w, h), i.e. stretched. The art is 4:3 and composed with the
// 640x480 layout, so draw it in the centred 4:3 box and clear the side bars to black.
static void __cdecl AdjustBackgroundRect(short* r) {
    int x = r[0], y = r[1], w = r[2], h = r[3];
    int target = (h * 4 + 1) / 3;
    if (target >= w - 1) return;
    int nx = x + (w - target) / 2;
    r[0] = (short)nx;
    r[2] = (short)target;
    void* dev = *(void**)0x72C014;
    if (dev) {
        LONG bars[8] = { x, y, nx, y + h, nx + target, y, x + w, y + h };
        void** vt = *(void***)dev;
        ((HRESULT(__stdcall*)(void*, DWORD, void*, DWORD, DWORD, float, DWORD))vt[0x90 / 4])(dev, 2, bars, 1 /*TARGET*/, 0xFF000000, 1.0f, 0);
    }
}

__declspec(naked) static void BackgroundRectStub() {        // replaces call 0x4CB1A0 (thiscall, dst*, src*, float)
    __asm {
        pushad
        push dword ptr [esp + 0x24]                         // dst (after pushad 32 bytes + return address)
        call AdjustBackgroundRect
        add esp, 4
        popad
        mov eax, 0x4CB1A0
        jmp eax
    }
}

// New-game name entry ("enter name"): the licence-plate field is placed at x = screen width / 2 - 90 in real pixels
// (0x56405A), then the widget puts that through the unanchored anchor, which now scales and offsets it. At 3x the
// plate landed off-screen, so the typed name was invisible. Convert the pixel position into layout units first.
static int __cdecl NamePlateX() {
    float s = *(float*)0x6AEA00;
    if (s < 0.01f) s = 1.0f;
    return (int)((ScreenW() / 2 - 90 - g_uiOffX) / s);
}

__declspec(naked) static void NamePlateXStub() {           // replaces 0x56405A: call GetScreenW; cdq; sub; sar; sub 5Ah
    __asm {
        call NamePlateX
        push 0x564067
        ret
    }
}

// City map marker origin: x0 (int, screen px) / sx. Stack slots as at the replaced instruction.
__declspec(naked) static void MapMarkerOriginStubA() {      // replaces 0x4D5ED0 in render 0x4D5BAD: x0 [esp+38h], sx [esp+34h]
    __asm {
        cvtsi2ss xmm1, dword ptr [esp + 0x38]
        divss xmm1, dword ptr [esp + 0x34]
        push 0x4D5ED6
        ret
    }
}

__declspec(naked) static void MapMarkerOriginStubB() {      // replaces 0x4D879D in render 0x4D8460: x0 [esp+7Ch], sx [esp+3Ch]
    __asm {
        cvtsi2ss xmm1, dword ptr [esp + 0x7C]
        divss xmm1, dword ptr [esp + 0x3C]
        push 0x4D87A3
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
    int bgSites = 0;
    // The art is 4:3, so filling a wider screen stretches it. Keeping its shape means black bars down the sides;
    // which of the two is worse is a matter of taste, so it is a setting rather than a decision made here.
    for (DWORD site : { 0x55DEBAul, 0x55DFACul }) {
        if (!g_menuBgAspect) break;
        BYTE* p = (BYTE*)site;
        if (p[0] != 0xE8 || (DWORD)(site + 5 + *(int*)(p + 1)) != 0x4CB1A0) {
            Log("shell background draw call at 0x%08lX differs, not patched", site);
            continue;
        }
        DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(p + 1) = (int)((BYTE*)&BackgroundRectStub - (p + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        ++bgSites;
    }
    if (g_menuBgAspect) Log("shell background 4:3 fix installed (%d of 2 sites)", bgSites);
    else Log("menu background left stretched to the screen (MenuBackgroundAspect=0)");

    // City map (UI_Map.xpr, [0x6D95E8]): the renders at 0x4D5BAD and 0x4D8460 size and centre the map with
    // sx = W/640, sy = H/480 (call 0x6089F0 = GetScreenW; mulss [0x679944] = 1/640), so it stretches on wide screens.
    // Using H/480 for sx too keeps the map's aspect; its X centring already uses (W - texW*sx)/2.
    int mapSites = 0;
    for (DWORD site : { 0x4D5BCEul, 0x4D8495ul }) {
        BYTE* p = (BYTE*)site;
        const BYTE mulss[] = { 0xF3, 0x0F, 0x2A, 0xC0, 0xF3, 0x0F, 0x59, 0x05 };   // cvtsi2ss xmm0,eax; mulss xmm0,[disp]
        if (p[0] != 0xE8 || (DWORD)(site + 5 + *(int*)(p + 1)) != 0x6089F0 || memcmp(p + 5, mulss, sizeof(mulss)) ||
            *(DWORD*)(p + 13) != 0x679944) {
            Log("city map scale site 0x%08lX differs, not patched", site);
            continue;
        }
        DWORD old;
        VirtualProtect(p, 17, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(p + 1) = (int)(0x608A00 - (site + 5));      // GetScreenH
        *(DWORD*)(p + 13) = 0x6786A8;                        // 1/480
        VirtualProtect(p, 17, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 17);
        ++mapSites;
    }
    Log("city map aspect fix installed (%d of 2 sites)", mapSites);

    {
        const BYTE plateOrig[] = { 0xE8, 0x91, 0x49, 0x0A, 0x00, 0x99, 0x2B, 0xC2, 0xD1, 0xF8, 0x83, 0xE8, 0x5A };
        if (memcmp((BYTE*)0x56405A, plateOrig, sizeof(plateOrig))) {
            Log("name entry plate bytes differ, name plate fix not installed");
        } else {
            WriteJmp(0x56405A, &NamePlateXStub, sizeof(plateOrig));
            Log("name entry plate fix installed (0x56405A)");
        }
    }

    // Map markers (player, destinations) are placed at ((x0 + c + u) * sx, ...), where x0 is the map's left edge in
    // screen pixels. With sx = W/640 x0 was always 0, so the pixel/unit mix never showed; with the centred map it
    // pushed markers off the right side. Divide x0 by sx where it enters the origin vector.
    if (mapSites == 2) {
        const BYTE x0Ped[] = { 0xF3, 0x0F, 0x2A, 0x4C, 0x24, 0x38 };     // 0x4D5ED0: cvtsi2ss xmm1,[esp+38h]
        const BYTE x0Pause[] = { 0xF3, 0x0F, 0x2A, 0x4C, 0x24, 0x7C };   // 0x4D879D: cvtsi2ss xmm1,[esp+7Ch]
        if (memcmp((BYTE*)0x4D5ED0, x0Ped, sizeof(x0Ped)) || memcmp((BYTE*)0x4D879D, x0Pause, sizeof(x0Pause))) {
            Log("city map marker origin bytes differ, marker fix not installed");
        } else {
            WriteJmp(0x4D5ED0, &MapMarkerOriginStubA, sizeof(x0Ped));
            WriteJmp(0x4D879D, &MapMarkerOriginStubB, sizeof(x0Pause));
            Log("city map marker origin fix installed (0x4D5ED0, 0x4D879D)");
        }
    }
}

static void __cdecl AdjustMovieRect(float* a) {             // a = x0, y0, x1, y1
    float w = a[2] - a[0], h = a[3] - a[1];
    float target = h * 4.0f / 3.0f;
    static float s_loggedW = 0, s_loggedH = 0;
    if (cfg.debugLog && (w != s_loggedW || h != s_loggedH)) {
        s_loggedW = w; s_loggedH = h;
        Log("full-screen quad %.0f,%.0f-%.0f,%.0f -> x %.0f-%.0f", a[0], a[1], a[2], a[3],
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
        // 0x60E88B: Bink movie frame. The others draw Title.xpr (loading screen after the intro movies / between
        // levels) at 0,0,W,H: startup 0x4E7CBE and 0x4E7D43, loading state 0x610AC9 and 0x6113C5.
        const DWORD sites[] = { 0x60E88B, 0x4E7CBE, 0x4E7D43, 0x610AC9, 0x6113C5 };
        int installed = 0;
        for (DWORD site : sites) {
            BYTE* p = (BYTE*)site;
            if (p[0] != 0xE8 || (DWORD)(site + 5 + *(int*)(p + 1)) != 0x60A9F0) {
                Log("full-screen quad call at 0x%08lX differs, not patched", site);
                continue;
            }
            DWORD old;
            VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
            *(int*)(p + 1) = (int)((BYTE*)&MovieQuadStub - (p + 5));
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            ++installed;
        }
        Log("movie / loading screen 4:3 fix installed (%d of 5 sites)", installed);
    }
}
