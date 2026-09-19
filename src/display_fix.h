// Options > Display fixes (DisplayFix=1): full resolution list, working Apply, MENU SIZE / HUD SIZE rows.
//
// Resolutions: the renderer builds each adapter's mode list (0x61D800; renderer [0x72C024], list [+0x1C], nodes
// {w, h, format, bits, flags, sortKey +0x14, next +0x3C}) only from D3D modes that match a 15-entry table at 0x68D5A8
// (640x480, 800x600, 1024x768, 1152x864, 1280x960 x 16/24/32 bits). The menu then lists a second 5-entry table
// {w, h} at 0x6B16C8 (count [0x6B16F0]) and greys entries the adapter lacks (0x61C650 lookup). So 16:10/16:9 and
// anything above 1280x960 could never be picked.
//   - the table search at 0x61D995 is replaced: every mode >= 640x480 with 16/24/32 bits is kept, sorted by size;
//   - the menu table references (26 displacements) point at a table here, rebuilt from the adapter's list
//     (unique sizes, largest 16 - the Display screen has 16 text slots, esi+0x1264 / +0x12A4).
// Apply (0x571430): the widescreen fix NOPs the width/height writes at 0x5714B4/0x5714BD and forces the back buffer
// size inside 0x61D630 (hook at 0x61D646) and CreateDevice (0x61DC12), so Apply changed nothing. The call to 0x61D630
// (fullscreen present parameters) at 0x571555 now saves the size (TrueCrime.ini and the widescreen fix's ResX/ResY,
// which decide the next start). With the widescreen fix the new size is used from the next start; without it the
// size is applied live, through the windowed present parameters 0x61D4C0 when borderless.h is active.
// Adapter: greyed by the game when D3D reports one adapter ([0x729B14] == 1, 0x5784C9). Nothing to fix there.
// Menu rows: the Display menu is data (header 0x6AF67C: items* +0x14, count +0x18; 20-byte rows {1, textId,
// callback, arg, 0}). Two rows are appended; their text ids are blank lines of TCPC??.txt (0xD70/0xD71) pointed at
// strings here. Cross/Enter steps the size; the value shows as the scale factor at the current resolution.
#pragma once

static int g_displayFix = 1;
static char g_modIniPath[MAX_PATH] = "";

struct ResEntry { int w, h; };
static ResEntry g_res[16] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 960 } };
static int g_resCount = 5;
static char g_resListText[16 * 12] = "";
static DWORD g_loggedRenderer = 0;

// ---- mode enumeration filter
// Sort key [node+0x14]: the insert at 0x61DAE4 links a node before the first larger key but never updates the list
// head, so keys must grow in enumeration order (nodes are always appended). The menu table sorts by size itself.
static int g_modeKey = 16;                                  // never 15 (the game's "not found" index)
static int __cdecl ModeSortKey(int w, int h, int bits) {
    if (w < 640 || h < 480 || (bits != 16 && bits != 24 && bits != 32)) return -1;
    return g_modeKey++;
}

__declspec(naked) static void ModeEnumStub() {              // replaces 0x61D995..0x61D9D0 (table search)
    __asm {
        push edx                                            // dl = format bits, compared after 0x61D9D1
        push ecx
        push ebp                                            // colour bits
        push dword ptr [esp + 0x44]                         // mode.Height ([esp+0x38] before the pushes)
        push dword ptr [esp + 0x44]                         // mode.Width  ([esp+0x34])
        call ModeSortKey
        add esp, 12
        pop ecx
        pop edx
        cmp eax, -1
        je skip
        mov edi, eax
        push 0x61D9D1                                       // found: dedupe and insert
        ret
    skip:
        mov edi, 0x0F
        push 0x61D9CB                                       // not found: mov [esp+18h],edi; jmp 61DA36
        ret
    }
}

// ---- menu resolution table
// The Display screen writes its resolution texts when it is constructed (0x58DEBB, from 0x5601ED), before the renderer
// has enumerated modes, so the table is built at load from the Windows display modes (the same list D3D8 reports for
// the primary adapter at 32 bits).
static void AddRes(ResEntry* all, int& n, int w, int h) {
    if (w < 640 || h < 480 || n >= 256) return;
    for (int i = 0; i < n; ++i) if (all[i].w == w && all[i].h == h) return;
    all[n++] = { w, h };
}

static void BuildResTable() {
    ResEntry all[256]; int n = 0;
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i)
        if (dm.dmBitsPerPel == 32) AddRes(all, n, (int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
    if (g_deskW > 0) AddRes(all, n, g_deskW, g_deskH);
    if (!n) return;
    for (int i = 1; i < n; ++i)                              // sort by width, then height
        for (int j = i; j > 0 && (all[j].w < all[j - 1].w || (all[j].w == all[j - 1].w && all[j].h < all[j - 1].h)); --j) {
            ResEntry t = all[j]; all[j] = all[j - 1]; all[j - 1] = t;
        }
    // More than 16 sizes: keep the desktop size, then sizes with the desktop's shape, 16:9, the classic 4:3 sizes, rest.
    auto shape = [](const ResEntry& r, double aspect) { return std::fabs((double)r.w / r.h - aspect) < 0.01; };
    auto rank = [&](const ResEntry& r) {
        if (r.w == g_deskW && r.h == g_deskH) return 0;
        if (g_deskH > 0 && shape(r, (double)g_deskW / g_deskH)) return 1;
        if (shape(r, 16.0 / 9.0)) return 2;
        if ((r.w == 640 && r.h == 480) || (r.w == 800 && r.h == 600) || (r.w == 1024 && r.h == 768) || (r.w == 1280 && r.h == 960)) return 3;
        return 4;
    };
    bool keep[256] = {};
    int kept = 0;
    for (int r = 0; r <= 4 && kept < 16; ++r)
        for (int i = n - 1; i >= 0 && kept < 16; --i)           // larger sizes first within a rank
            if (!keep[i] && rank(all[i]) == r) { keep[i] = true; ++kept; }
    g_resCount = 0;
    for (int i = 0; i < n; ++i) if (keep[i]) g_res[g_resCount++] = all[i];
    g_resListText[0] = 0;
    for (int i = 0; i < g_resCount; ++i) {
        char e[16]; sprintf_s(e, "%s%dx%d", i ? " " : "", g_res[i].w, g_res[i].h); strcat_s(g_resListText, e);
    }
}

// ---- Apply
static void WriteIniInt(const char* section, const char* key, int v, const char* path) {
    char s[16]; sprintf_s(s, "%d", v);
    WritePrivateProfileStringA(section, key, s, path);
}

static int __cdecl DisplayApplyMode(DWORD node) {           // returns 1 = use windowed (borderless) parameters
    // The widescreen fix makes the 0x61C650 lookup return the first mode (jmp at 0x61C656), so the node is not the
    // chosen size; take it from the selected menu entry.
    int sel = *(int*)0x729B0C;
    int w = *(int*)node, h = *(int*)(node + 4);
    if (sel >= 0 && sel < g_resCount) { w = g_res[sel].w; h = g_res[sel].h; }
    char dir[MAX_PATH]; GetModuleFileNameA(nullptr, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\'); if (slash) slash[1] = 0;
    char gameIni[MAX_PATH], wsfAsi[MAX_PATH], wsfIni[MAX_PATH];
    sprintf_s(gameIni, "%sTrueCrime.ini", dir);
    sprintf_s(wsfAsi, "%sscripts\\TrueCrimeStreetsofLA.WidescreenFix.asi", dir);
    sprintf_s(wsfIni, "%sscripts\\TrueCrimeStreetsofLA.WidescreenFix.ini", dir);
    WriteIniInt("Renderer", "ScreenWidth", w, gameIni);
    WriteIniInt("Renderer", "ScreenHeight", h, gameIni);
    bool desktop = w == g_deskW && h == g_deskH;
    if (GetFileAttributesA(wsfAsi) != INVALID_FILE_ATTRIBUTES) {
        // The widescreen fix sets the back buffer size inside CreateDevice (0x61DC12) and its aspect values from
        // ResX/ResY once at start (verified: a live 1920x1080 apply kept a 2560x1600 back buffer with the frame drawn
        // into its corner), so a new size is saved for the next start and the re-create keeps the current size.
        WriteIniInt("MAIN", "ResX", desktop ? 0 : w, wsfIni);        // 0 = desktop resolution
        WriteIniInt("MAIN", "ResY", desktop ? 0 : h, wsfIni);
        Log("display: %dx%d saved, used from the next start (the widescreen fix sets the size at start)", w, h);
        return g_borderlessActive ? 1 : 0;
    }
    *(int*)0x6B9B98 = w;
    *(int*)0x6B9B9C = h;
    Log("display: apply %dx%d (%s)", w, h, g_borderlessActive ? "borderless" : "fullscreen");
    return g_borderlessActive ? 1 : 0;
}

__declspec(naked) static void ApplyModeStub() {             // replaces call 0x61D630 at 0x571555 (thiscall, node)
    __asm {
        pushad
        push dword ptr [esp + 0x24]                         // node
        call DisplayApplyMode
        add esp, 4
        mov [esp + 0x1C], eax                               // popad restores it into eax
        popad
        test eax, eax
        jz fullscreen
        push ecx
        push dword ptr ds:[0x6B9B9C]
        push dword ptr ds:[0x6B9B98]
        mov eax, 0x61D4C0                                   // windowed present parameters (thiscall w, h)
        call eax
        pop ecx
        pushad
        push ecx
        call BorderlessAdjustPresentParams
        add esp, 4
        popad
        ret 4
    fullscreen:
        mov eax, 0x61D630
        jmp eax
    }
}

// Opening the Display screen (vtable 0x683218 slot 3, 0x56BEE0) never points the resolution/adapter/depth lists at
// the current settings; only Cancel (0x571430 with 0, from 0x578C1B) does. The lists started on entry 0, so the screen
// showed "640x480 Res" at any resolution. Run that refresh first; 0x56BEE0 then backs up the selections as before.
//
// That refresh (0x571628) walks the resolution table for the current size and stores the loop counter whether or not
// it matched, so a size that is not in the list leaves the index one past the end. 0x55CE60 then looks that entry up,
// gets nothing, and dereferences the null it just produced (0x55CE93). A DPI-virtualised desktop reaches the screen
// that way - at 150% scaling an unaware process renders 2560x1600 as 1707x1067, which no display enumerates - so the
// refresh only runs when the current size really is in the list; otherwise the lists stay where they were.
static bool __cdecl CurrentModeListed() {
    int w = *(int*)0x6B9B98, h = *(int*)0x6B9B9C;
    int n = g_resCount, max = (int)(sizeof(g_res) / sizeof(g_res[0]));
    if (n > max) n = max;
    for (int i = 0; i < n; ++i) if (g_res[i].w == w && g_res[i].h == h) return true;
    return false;
}

static DWORD g_displayOpenOrig = 0;
__declspec(naked) static void DisplayOpenHook() {           // thiscall(screen, arg), ret 4
    __asm {
        push ecx
        call CurrentModeListed
        test al, al
        pop ecx
        je keep                                             // unlisted size: the refresh would select a missing entry
        push ecx
        push 0
        mov eax, 0x571430                                   // thiscall(screen, 0): selections and list cursors
        call eax
        pop ecx
    keep:
        jmp dword ptr [g_displayOpenOrig]
    }
}

// ---- MENU SIZE / HUD SIZE / SUBTITLE SIZE rows
// Subtitles get their own row because the size that suits a menu is not the size that suits a line of dialogue at
// the bottom of the screen. Its first step is AUTO, which is SubtitleScale=0: follow the menu size, as before.
static const int kSizeSteps[] = { 50, 60, 70, 75, 80, 90, 100 };
static DWORD g_displayItems[10][5];
static char g_menuSizeText[32] = "MENU SIZE", g_hudSizeText[32] = "HUD SIZE", g_subSizeText[32] = "SUBTITLE SIZE";
static const DWORD kMenuSizeTextId = 0xD70, kHudSizeTextId = 0xD71, kSubSizeTextId = 0xD74;   // 0xD72/0xD73 are the Controls screen's CAMERA SPEED / AIM SPEED

static void FormatScale(char* out, size_t n, const char* label, int pct) {
    char v[16]; sprintf_s(v, "%.2f", FitScale(pct));
    size_t len = strlen(v);
    while (len && v[len - 1] == '0') v[--len] = 0;
    if (len && v[len - 1] == '.') v[--len] = 0;
    sprintf_s(out, n, "%s %sX", label, v);
}

static void UpdateSizeLabels() {
    FormatScale(g_menuSizeText, sizeof(g_menuSizeText), "MENU SIZE", g_menuScalePct);
    FormatScale(g_hudSizeText, sizeof(g_hudSizeText), "HUD SIZE", g_hudScalePct);
    if (g_subtitleScalePct > 0) FormatScale(g_subSizeText, sizeof(g_subSizeText), "SUBTITLE SIZE", g_subtitleScalePct);
    else sprintf_s(g_subSizeText, sizeof(g_subSizeText), "SUBTITLE SIZE AUTO");
    char** table = *(char***)0x72831C;
    if (!table) return;
    if (table[kMenuSizeTextId] != g_menuSizeText) table[kMenuSizeTextId] = g_menuSizeText;
    if (table[kHudSizeTextId] != g_hudSizeText) table[kHudSizeTextId] = g_hudSizeText;
    if (table[kSubSizeTextId] != g_subSizeText) table[kSubSizeTextId] = g_subSizeText;
}

static int NextSizeStep(int pct) {
    for (int s : kSizeSteps) if (s > pct) return s;
    return kSizeSteps[0];
}

static const char* const kSizeRowName[3] = { "menu size", "HUD size", "subtitle size" };
static const char* const kSizeRowKey[3]  = { "MenuScale", "HUDScale", "SubtitleScale" };

static char __fastcall SizeRowCallback(void*, void*, int which, int) {   // called like 0x557DC0 (thiscall, ret 8)
    int& pct = which == 0 ? g_menuScalePct : which == 1 ? g_hudScalePct : g_subtitleScalePct;
    // the subtitle row has one step the others do not: 0 = AUTO, back to following the menu size
    pct = (which == 2 && pct >= kSizeSteps[_countof(kSizeSteps) - 1]) ? 0 : NextSizeStep(pct);
    WriteIniInt("Controller", kSizeRowKey[which], pct, g_modIniPath);
    ApplyUiScale();
    UpdateSizeLabels();
    ((void(__cdecl*)(int, float, float))0x4CA4B0)(0xFF, 1.0f, 1.0f);   // menu confirm sound
    Log("display: %s %d%% (%.3f at %dx%d)", kSizeRowName[which], pct,
        FitScale(pct > 0 ? pct : g_menuScalePct), ScreenW(), ScreenH());
    return 1;
}

static bool PatchDword(DWORD site, DWORD expected, DWORD value) {
    if (*(DWORD*)site != expected) return false;
    DWORD old;
    VirtualProtect((LPVOID)site, 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)site = value;
    VirtualProtect((LPVOID)site, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, 4);
    return true;
}

static void DisplayFixLoadConfig(const char* iniPath) {
    strcpy_s(g_modIniPath, iniPath);
    g_displayFix = (int)GetPrivateProfileIntA("Controller", "DisplayFix", 1, iniPath);
    if (g_displayFix) BuildResTable();                       // after BorderlessDecide (desktop size)
}

static void DisplayFixUpdate() {                             // every input update
    if (!g_displayFix) return;
    DWORD renderer = *(DWORD*)0x72C024;
    if (renderer && renderer != g_loggedRenderer && *(int*)(renderer + 0x20) > 0) {
        g_loggedRenderer = renderer;
        DWORD adapter = *(DWORD*)0x72C020;
        Log("display: adapter \"%s\", %d modes enumerated", adapter ? (const char*)(adapter + 0x228) : "?", *(int*)(renderer + 0x20));
    }
    UpdateSizeLabels();
    if (g_borderlessActive) BorderlessOnDeviceCreated();     // after a runtime switch the device may have been Reset
}

static void DisplayFixInstall() {
    if (!g_displayFix) return;
    const BYTE enumOrig[] = { 0x33, 0xC0, 0xEB, 0x07, 0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4C, 0x24, 0x34,
        0x3B, 0x88, 0xA8, 0xD5, 0x68, 0x00, 0x75, 0x14, 0x8B, 0x4C, 0x24, 0x38, 0x3B, 0x88, 0xAC, 0xD5, 0x68, 0x00, 0x75,
        0x08, 0x3B, 0xA8, 0xB0, 0xD5, 0x68, 0x00, 0x74, 0x11, 0x83, 0xC0, 0x0C, 0x47, 0x3D, 0xB4, 0x00, 0x00, 0x00, 0x72,
        0xD5, 0x89, 0x7C, 0x24, 0x18, 0xEB, 0x65 };
    const DWORD wSites[] = { 0x56BDF3, 0x571463, 0x571495, 0x571663, 0x5787D0, 0x5788E9, 0x578934, 0x578975, 0x58DEFA, 0x58DF2B };
    const DWORD hSites[] = { 0x56BDFC, 0x571472, 0x57148E, 0x57166C, 0x5787C9, 0x5788E2, 0x57892D, 0x57896E, 0x58DEF3, 0x58DF24 };
    const DWORD nSites[] = { 0x56BDD2, 0x57163F, 0x57879F, 0x5787F9, 0x58DEBD, 0x58DF91 };
    const BYTE applyCall[] = { 0xE8, 0xD6, 0xC0, 0x0A, 0x00 };   // call 0x61D630 at 0x571555
    bool ok = memcmp((BYTE*)0x61D995, enumOrig, sizeof(enumOrig)) == 0 && memcmp((BYTE*)0x571555, applyCall, 5) == 0;
    for (DWORD s : wSites) ok = ok && *(DWORD*)s == 0x6B16C8;
    for (DWORD s : hSites) ok = ok && *(DWORD*)s == 0x6B16CC;
    for (DWORD s : nSites) ok = ok && *(DWORD*)s == 0x6B16F0;
    if (!ok) {
        Log("display settings code differs, resolution fix not installed");
    } else {
        WriteJmp(0x61D995, &ModeEnumStub, sizeof(enumOrig));
        for (DWORD s : wSites) PatchDword(s, 0x6B16C8, (DWORD)&g_res[0].w);
        for (DWORD s : hSites) PatchDword(s, 0x6B16CC, (DWORD)&g_res[0].h);
        for (DWORD s : nSites) PatchDword(s, 0x6B16F0, (DWORD)&g_resCount);
        PatchDword(0x571556, 0x000AC0D6, (DWORD)((BYTE*)&ApplyModeStub - (BYTE*)0x57155A));
        DWORD* openSlot = (DWORD*)(0x683218 + 3 * 4);
        if (*openSlot == 0x56BEE0) {
            DWORD old;
            VirtualProtect(openSlot, 4, PAGE_READWRITE, &old);
            g_displayOpenOrig = *openSlot;
            *openSlot = (DWORD)&DisplayOpenHook;
            VirtualProtect(openSlot, 4, old, &old);
        }
        Log("display resolution fix installed (modes 0x61D995, menu table, apply 0x571555, open %s); list: %s",
            g_displayOpenOrig ? "0x683224" : "not hooked", g_resListText);
    }

    const DWORD* header = (const DWORD*)0x6AF67C;
    const DWORD* items = (const DWORD*)0x6AF5F0;
    if (header[0] != 0x1C8 || header[5] != 0x6AF5F0 || header[6] != 7 || items[0] != 1 || items[1] != 0xD7D) {
        Log("display menu table differs, size rows not added");
        return;
    }
    if (!g_uiFix && !g_hudFix) return;
    int count = 7;
    memcpy(g_displayItems, items, 7 * sizeof(g_displayItems[0]));
    const DWORD cb = (DWORD)&SizeRowCallback;
    if (g_uiFix)  { const DWORD r[5] = { 1, kMenuSizeTextId, cb, 0, 0 }; memcpy(g_displayItems[count++], r, sizeof(r)); }
    if (g_hudFix) { const DWORD r[5] = { 1, kHudSizeTextId, cb, 1, 0 }; memcpy(g_displayItems[count++], r, sizeof(r)); }
    if (g_uiFix)  { const DWORD r[5] = { 1, kSubSizeTextId, cb, 2, 0 }; memcpy(g_displayItems[count++], r, sizeof(r)); }
    DWORD old;
    VirtualProtect((LPVOID)0x6AF690, 8, PAGE_READWRITE, &old);
    *(DWORD*)0x6AF690 = (DWORD)&g_displayItems[0][0];
    *(DWORD*)0x6AF694 = (DWORD)count;
    VirtualProtect((LPVOID)0x6AF690, 8, old, &old);
    Log("display menu: added size rows (%d items)", count);
}
