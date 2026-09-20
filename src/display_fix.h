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
static int g_resCap = 16;
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

static void BuildResTable(int maxEntries) {
    if (maxEntries < 1) maxEntries = 1;
    if (maxEntries > 16) maxEntries = 16;
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
    for (int r = 0; r <= 4 && kept < maxEntries; ++r)
        for (int i = n - 1; i >= 0 && kept < maxEntries; --i)   // larger sizes first within a rank
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

// ---- the pop-up lists vs the settings rows
// Opening RESOLUTION drew its entries across SUBTITLE SIZE and RETICLE SIZE. Neither the text elements nor the
// screen object carry coordinates - the layout is resolved at draw time - so the geometry came from hooking the one
// text draw the whole UI goes through, 0x60B9C0(string, x, y), and logging where every label landed at 2560x1600
// with the menu at scale 2.0 (offset 384,320). In the 640x480 units the UI is laid out in:
//   settings rows   85 + i*16          (screen 490 + i*32), ending at 254 across for the longest label
//   pop-up entries  233 + line*16      (screen 788, 820, 852), filling right to left, smallest sizes first
//   screen title    321                (screen 962)
// Stock is 7 rows ending at 181, and 5 resolutions fit one line, so nothing met. This mod appends four size rows,
// taking the column to 245, and lists every mode the adapter reports, which wraps - and the entries run leftwards
// until they reach the safe rect, which MenuSpread widens to most of the screen, so they cross the column.
// The wrap is decided in the list's own layout (0x55D330): 0x55D482 loads the safe rect's left edge as the limit a
// line may reach, and the entries break the moment the next one would pass it. ebx still holds the container there,
// so replacing that load with a call that answers per container gives the Display screen's own five lists a limit
// just right of the settings column while every other list in the game keeps the rect's edge. The entries then sit
// beside the settings instead of under them, and can stay at the y the game chose.
static bool PatchDword(DWORD site, DWORD expected, DWORD value) {
    if (*(DWORD*)site != expected) return false;
    DWORD old;
    VirtualProtect((LPVOID)site, 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)site = value;
    VirtualProtect((LPVOID)site, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, 4);
    return true;
}

static int g_displayTrace = 0;
static const int kRowTopUnits = 85, kRowPitchUnits = 16, kPopupTopUnits = 233;
// Where the settings column ends: its text starts at 57 units and the longest label it can show,
// "SUBTITLE SIZE 1.25X", runs to about 254, measured from the text draws at 2560x1600. In layout units, so it holds
// at any resolution, menu size or spread - a fraction of the screen or of the safe rect would not.
static const int kColumnRightUnits = 270;
// A pop-up line takes 16 units and the screen title sits at 321, so five lines fit below 233 with the last line's
// glyphs still clear of it. An entry takes at most 136 units across ("1680x1050 Res" at the widest, from the six
// that fitted 818 units of usable width), so the list can only show as many sizes as five lines of those hold.
// Below about a third of the screen - MenuSpread=0 leaves 338 units - sixteen sizes would need seven lines and run
// into the title, so the table is rebuilt smaller. It is rebuilt rather than truncated because the entries are
// stored in size order while the choice of which to keep is ranked (desktop size first, then its shape, then 16:9),
// and cutting the tail would drop the desktop size, which is the one that matters most.
static const int kPopupLines = 5, kEntryWidthUnits = 136;

static int ResCapacity() {
    float sx = *(float*)0x6AEA00;
    if (sx <= 0) return 16;
    int usable = (int)((*(int*)0x7280F4 - (g_uiOffX + (int)(kColumnRightUnits * sx))) / sx);
    int perLine = usable / kEntryWidthUnits;
    if (perLine < 1) perLine = 1;
    int cap = perLine * kPopupLines;
    return cap > 16 ? 16 : cap;
}

static int g_popupShift = 0;
static DWORD g_dispScreen = 0;   // the Display screen, stashed by DisplayOpenHook

// With the lists held to the right of the column they can stay where the game puts them, side by side with the
// settings. The downward shift is only the fallback for when the line limit could not be hooked, and then it has to
// put the first line below the last settings row instead.
static void PopupShiftInstall(int rows, bool limitHooked) {
    int want = kRowTopUnits + rows * kRowPitchUnits;         // one row below the last settings row
    g_popupShift = limitHooked || want <= kPopupTopUnits ? 0 : want - kPopupTopUnits;
    Log("display: pop-up first line %d (%d settings rows, line limit %s)",
        kPopupTopUnits + g_popupShift, rows, limitHooked ? "hooked" : "not hooked");
}

// The position is only read while the list lays itself out, which happens once when the screen is built - writing
// the field afterwards changes nothing, as a first attempt from the input update proved. Each of the five lists runs
// that layout the same way: ecx holds the container, eax its vtable, then "push 0; call [eax+0x2C]" - five bytes,
// exactly a call, so replacing it with one that nudges the y first needs no trampoline. 233 itself is computed
// somewhere in the screen's construction rather than stored as a literal, so the nudge is guarded on that value and
// applies once however often the screen is rebuilt.
static const DWORD kListLayoutSites[] = { 0x58DDFA, 0x58DFB7, 0x58E149, 0x58E499, 0x58E594 };

// The same layout (0x55D330) decides where to break a line: it takes the safe rect's left edge as the limit a line
// may reach (0x55D482) and wraps the moment the next entry would pass it. The entries are right anchored and fill
// leftwards, so with the rect spanning the whole screen - which is what MenuSpread widens it to - a long list runs
// left until it is under the settings column. Reading that limit from here instead lets these five lists stop at the
// middle of the rect, leaving the settings the left half and the entries the right, while every other list in the
// game still gets the rect's own edge.
// A list is laid out again after it is built - opening the screen refreshes it - so the limit cannot be set around
// the five construction calls; it has to be decided inside the layout. At 0x55D482 ebx still holds the container, so
// the load becomes a call that answers for that container: the middle of the rect for the Display screen's own five,
// the rect's edge for every other list in the game. eax is live across the site, hence pushad.
static DWORD g_dispLists[5] = {};
static int g_listLimitOut = 0;

static void __fastcall ListLimitFor(DWORD container) {
    g_listLimitOut = *(int*)0x7280F0;
    for (DWORD c : g_dispLists)
        if (c && c == container) {
            g_listLimitOut = g_uiOffX + (int)(kColumnRightUnits * *(float*)0x6AEA00);
            return;
        }
}

__declspec(naked) static void ListLimitStub() {             // replaces "mov ecx, [0x7280F0]" at 0x55D482
    __asm {
        pushad
        mov ecx, ebx                                        // the container being laid out
        call ListLimitFor
        popad
        mov ecx, g_listLimitOut
        ret
    }
}

static void __fastcall BeforeListLayout(DWORD container) {
    int* y = (int*)(container + 0x28);
    if (*y == kPopupTopUnits) *y = kPopupTopUnits + g_popupShift;
    int slot = -1;
    for (int i = 0; i < 5; ++i) {
        if (g_dispLists[i] == container) { slot = i; break; }
        if (!g_dispLists[i] && slot < 0) slot = i;
    }
    if (slot >= 0) g_dispLists[slot] = container;
    if (g_displayTrace) {
        int left = *(int*)0x7280F0, right = *(int*)0x7280F4;
        float sx = *(float*)0x6AEA00;
        int limit = g_uiOffX + (int)(kColumnRightUnits * sx);
        Log("display: list %08lX y %d, rect %d..%d, limit %d, usable %.0f units (menu %d%%, spread %d, %dx%d)",
            container, *y, left, right, limit, sx > 0 ? (right - limit) / sx : 0.0f,
            g_menuScalePct, g_menuSpread, ScreenW(), ScreenH());
    }
}

__declspec(naked) static void ListLayoutStub() {            // replaces "push 0; call [eax+0x2C]"
    __asm {
        pushad
        call BeforeListLayout                               // __fastcall: ecx is already the container
        popad
        push 0
        call dword ptr [eax + 0x2C]
        ret
    }
}

static bool ListLayoutInstall() {
    bool limitHooked = false;
    const BYTE limitOrig[] = { 0x8B, 0x0D, 0xF0, 0x80, 0x72, 0x00 };   // mov ecx, [0x7280F0] at 0x55D482
    if (memcmp((BYTE*)0x55D482, limitOrig, sizeof(limitOrig)) == 0) {
        DWORD old;
        VirtualProtect((LPVOID)0x55D482, 6, PAGE_EXECUTE_READWRITE, &old);
        *(BYTE*)0x55D482 = 0xE8;
        *(int*)(0x55D482 + 1) = (int)((BYTE*)&ListLimitStub - (BYTE*)(0x55D482 + 5));
        *(BYTE*)(0x55D482 + 5) = 0x90;
        VirtualProtect((LPVOID)0x55D482, 6, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (LPVOID)0x55D482, 6);
        Log("display: list line limit hooked (0x55D482)");
        limitHooked = true;
    } else Log("display list line limit differs, lists keep the safe rect");
    const BYTE orig[] = { 0x6A, 0x00, 0xFF, 0x50, 0x2C };
    for (DWORD site : kListLayoutSites)
        if (memcmp((BYTE*)site, orig, sizeof(orig))) {
            Log("display list layout differs at %08lX, pop-ups not moved", site);
            return limitHooked;
        }
    for (DWORD site : kListLayoutSites) {
        DWORD old;
        VirtualProtect((LPVOID)site, 5, PAGE_EXECUTE_READWRITE, &old);
        *(BYTE*)site = 0xE8;
        *(int*)(site + 1) = (int)((BYTE*)&ListLayoutStub - (BYTE*)(site + 5));
        VirtualProtect((LPVOID)site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, 5);
    }
    Log("display: list layout hooked (%d sites)", (int)(sizeof(kListLayoutSites) / sizeof(kListLayoutSites[0])));
    return limitHooked;
}

// ---- DisplayTrace=1 (debug): log where every label is drawn
// 0x60B9C0(string, x, y) with the font in ecx is the single text draw, so a burst of its calls is a map of the screen.
static int g_textBudget = 0;
static int g_dispPass = 0;
static int g_dispDump = 0;

static void __cdecl LogTextDraw(const char* str, float x, float y) {
    if (g_textBudget <= 0) return;
    --g_textBudget;
    Log("  text (%7.1f,%7.1f)", x, y);
}

__declspec(naked) static void TextDrawStub() {              // replaces the prologue of 0x60B9C0
    __asm {
        pushad
        push dword ptr [esp + 0x2C]                         // y
        push dword ptr [esp + 0x2C]                         // x
        push dword ptr [esp + 0x2C]                         // string
        call LogTextDraw
        add esp, 12
        popad
        push ebp                                            // the original prologue, then back into the body
        mov ebp, esp
        and esp, 0xFFFFFFF0
        sub esp, 0x354
        mov eax, 0x60B9CC
        jmp eax
    }
}

static void DisplayDumpElements() {
    if (!g_dispScreen) return;
    Log("display trace pass %d: %d resolutions, scale %.3f offset %d,%d, %dx%d",
        ++g_dispPass, g_resCount, UiScale(), g_uiOffX, g_uiOffY, ScreenW(), ScreenH());
    g_textBudget = 48;
}

static DWORD g_displayOpenOrig = 0;
__declspec(naked) static void DisplayOpenHook() {           // thiscall(screen, arg), ret 4
    __asm {
        mov g_dispScreen, ecx
        mov g_dispDump, 120
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
static DWORD g_displayItems[11][5];
static char g_menuSizeText[32] = "MENU SIZE", g_hudSizeText[32] = "HUD SIZE", g_subSizeText[32] = "SUBTITLE SIZE";
static char g_retSizeText[32] = "RETICLE SIZE";
static const DWORD kMenuSizeTextId = 0xD70, kHudSizeTextId = 0xD71, kSubSizeTextId = 0xD74, kRetSizeTextId = 0xD75;   // 0xD72/0xD73 are the Controls screen's CAMERA SPEED / AIM SPEED
// The reticle takes whole multiples only: a stroke one unit thick has to land on a whole number of pixels or it
// rasterises unevenly around the box. AUTO rounds the HUD size to the nearest whole multiple.
static const int kReticleSteps[] = { 1, 2, 3, 4, 5 };
// Subtitles step in multiples of their own size instead, x100, because a line of dialogue wants to be a little
// bigger than the game drew it, not a fraction of the screen height.
static const int kSubtitleSteps[] = { 100, 125, 150, 175, 200, 250, 300 };

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
    if (g_subtitleScalePct > 0) {
        char v[16]; sprintf_s(v, "%.2f", g_subtitleScalePct / 100.0f);
        size_t len = strlen(v);
        while (len && v[len - 1] == '0') v[--len] = 0;
        if (len && v[len - 1] == '.') v[--len] = 0;
        sprintf_s(g_subSizeText, sizeof(g_subSizeText), "SUBTITLE SIZE %sX", v);
    } else sprintf_s(g_subSizeText, sizeof(g_subSizeText), "SUBTITLE SIZE AUTO");
    if (g_reticleScale > 0) sprintf_s(g_retSizeText, sizeof(g_retSizeText), "RETICLE SIZE %dX", g_reticleScale);
    else sprintf_s(g_retSizeText, sizeof(g_retSizeText), "RETICLE SIZE AUTO");
    char** table = *(char***)0x72831C;
    if (!table) return;
    if (table[kMenuSizeTextId] != g_menuSizeText) table[kMenuSizeTextId] = g_menuSizeText;
    if (table[kHudSizeTextId] != g_hudSizeText) table[kHudSizeTextId] = g_hudSizeText;
    if (table[kSubSizeTextId] != g_subSizeText) table[kSubSizeTextId] = g_subSizeText;
    if (table[kRetSizeTextId] != g_retSizeText) table[kRetSizeTextId] = g_retSizeText;
}

static int NextSizeStep(int pct) {
    for (int s : kSizeSteps) if (s > pct) return s;
    return kSizeSteps[0];
}

static const char* const kSizeRowName[4] = { "menu size", "HUD size", "subtitle size", "reticle size" };
static const char* const kSizeRowKey[4]  = { "MenuScale", "HUDScale", "SubtitleScale", "ReticleScale" };

static char __fastcall SizeRowCallback(void*, void*, int which, int) {   // called like 0x557DC0 (thiscall, ret 8)
    int& pct = which == 0 ? g_menuScalePct : which == 1 ? g_hudScalePct :
               which == 2 ? g_subtitleScalePct : g_reticleScale;
    // the subtitle and reticle rows have one step the others do not: 0 = AUTO, back to following another size
    if (which == 3) {
        int next = 0;
        for (int v : kReticleSteps) if (v > pct) { next = v; break; }
        pct = next;
    } else if (which == 2) {
        int next = 0;                                       // 0 = AUTO, then the multiples in order
        for (int v : kSubtitleSteps) if (v > pct) { next = v; break; }
        pct = next;
    } else {
        pct = NextSizeStep(pct);
    }
    WriteIniInt("Controller", kSizeRowKey[which], pct, g_modIniPath);
    ApplyUiScale();
    UpdateSizeLabels();
    ((void(__cdecl*)(int, float, float))0x4CA4B0)(0xFF, 1.0f, 1.0f);   // menu confirm sound
    if (which == 3)
        Log("display: reticle size %s (HUD %.3f at %dx%d)", pct > 0 ? "fixed" : "AUTO",
            FitScale(g_hudScalePct), ScreenW(), ScreenH());
    else if (which == 2)
        Log("display: subtitle size %s (%.3f at %dx%d)", pct > 0 ? "fixed" : "AUTO",
            pct > 0 ? pct / 100.0f : FitScale(g_menuScalePct), ScreenW(), ScreenH());
    else
        Log("display: %s %d%% (%.3f at %dx%d)", kSizeRowName[which], pct,
            FitScale(pct > 0 ? pct : g_menuScalePct), ScreenW(), ScreenH());
    return 1;
}

static void DisplayFixLoadConfig(const char* iniPath) {
    strcpy_s(g_modIniPath, iniPath);
    g_displayFix = (int)GetPrivateProfileIntA("Controller", "DisplayFix", 1, iniPath);
    g_displayTrace = (int)GetPrivateProfileIntA("Controller", "DisplayTrace", 0, iniPath);
    if (g_displayFix) BuildResTable(16);                     // after BorderlessDecide (desktop size)
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
    int cap = ResCapacity();                                 // before the screen is built, which is when Options opens
    if (cap != g_resCap) {
        g_resCap = cap;
        BuildResTable(cap);
        Log("display: resolution list holds %d of %d lines' worth (%d entries): %s",
            cap, kPopupLines, g_resCount, g_resListText);
    }
    if (g_displayTrace && g_dispDump > 0 && --g_dispDump == 0) { DisplayDumpElements(); g_dispDump = 180; }
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
    if (g_hudFix) { const DWORD r[5] = { 1, kRetSizeTextId, cb, 3, 0 }; memcpy(g_displayItems[count++], r, sizeof(r)); }
    DWORD old;
    VirtualProtect((LPVOID)0x6AF690, 8, PAGE_READWRITE, &old);
    *(DWORD*)0x6AF690 = (DWORD)&g_displayItems[0][0];
    *(DWORD*)0x6AF694 = (DWORD)count;
    VirtualProtect((LPVOID)0x6AF690, 8, old, &old);
    Log("display menu: added size rows (%d items)", count);
    PopupShiftInstall(count, ListLayoutInstall());
    if (g_displayTrace) {
        const BYTE textOrig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x54, 0x03, 0x00, 0x00 };
        if (memcmp((BYTE*)0x60B9C0, textOrig, sizeof(textOrig)) == 0) {
            WriteJmp(0x60B9C0, &TextDrawStub, sizeof(textOrig));
            Log("display trace: text draw hooked (0x60B9C0)");
        } else Log("display trace: text draw differs, not hooked");
    }
}
