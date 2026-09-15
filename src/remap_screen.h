// Options > Controls > "Mouse Controls" turned into a controller remap screen (ControllerRemapScreen=1).
//
// Screen (vtable 0x682F38): rows = action masks [+0x368 + row*0x18] (row names: string id 0xDC1 + row), columns =
// modes 0..4 (on foot, fighting, guns, driving, stealth), same order as controller_map.h. Cell refresh 0x56EB70 labels
// each existing cell via 0x559230 on its label element (Font_UI_Small, which carries the DualSense glyphs).
// Editing: a cell click sets row [+0x2E0] / mode [+0x2E4]; update 0x56EDD0 starts a capture ([0x72BAB8] = 1, type
// [0x72BAB4] = 3 mouse), the input update writes the pressed button to [0x6B6A10] (-2 = Esc cancel), and 0x56EE4A
// assigns it to the mouse mask table. DEFAULT is message 2 (jump table entry 0x580258). Titles: string ids 0xD95/0xD8C.
//
// Hooks: labels (0x56EC10) show the controller button bound to (row, mode); capture type 3 -> 4 (0x56EDF9) so the game
// stops polling the mouse while the mod polls the controller and writes the button code; assignment (0x56EE4A) and
// DEFAULT (0x580258) edit the controller map instead of the mouse table; titles read "Controller".
#pragma once

static int g_remapScreen = 1;

// controller navigation state (see RemapNavigate)
static Pad g_navPad;
static bool g_navL2 = false, g_navR2 = false;
static DWORD g_navScreen = 0;
static ULONGLONG g_navLastUpdate = 0, g_navRepeatAt = 0, g_navBlinkAt = 0;
static int g_navRow = 0, g_navMode = 0, g_navHeldDir = 0;
static bool g_navBlinkOff = false, g_navPrevCross = false, g_navPrevTriangle = false;

// Cell labels are drawn in black, and the font renderer tints every glyph except the coloured face buttons
// (0x80-0x83): the redrawn L1/R1/L2/R2/stick icons (0xA2-0xA7) come out as black shapes there, so those are text.
static const char* kCtlGlyph[CB_Count] = {
    "\x80", "\x82", "\x83", "\x81",           // cross, circle, square, triangle (coloured, untinted)
    "L1", "R1", "L2", "R2", "R3",
    "\x90", "\x91", "\x8E", "\x8F",           // D-pad up, down, left, right (black arrows)
    "L3\x80", "L3\x82", "L3\x83", "L3\x81", "L3R2" };
static const float kCtlGlyphScale[CB_Count] = {
    1, 1, 1, 1,  1, 1, 1, 1, 1,  1, 1, 1, 1,
    0.85f, 0.85f, 0.85f, 0.85f, 0.7f };      // cells are 34 units; "L3"+glyph is ~39, "L3R2" ~50

// Button shown for a cell: a button whose mask contains the row's action bits, preferring the fewest extra bits.
static int CtlButtonForRow(int mode, int rowMask) {
    int best = -1, bestExtra = 99;
    for (int b = 0; b < CB_Count; ++b) {
        int v = g_ctlMap[mode][b];
        if (!rowMask || (v & rowMask) != rowMask) continue;
        int extra = 0; for (unsigned x = (unsigned)(v & ~rowMask); x; x &= x - 1) ++extra;
        if (extra < bestExtra) { best = b; bestExtra = extra; }
    }
    return best;
}

static const char* __cdecl RemapCellLabel(DWORD screen, int rowStringId, int mode, DWORD element) {
    int row = rowStringId - 0xDC1;
    if (row < 0 || row > 24 || mode < 0 || mode >= CM_Count) return "";
    int rowMask = *(int*)(screen + 0x368 + row * 0x18);
    int b = CtlButtonForRow(mode, rowMask);
    const char* label = b >= 0 ? kCtlGlyph[b] : "";
    if (screen == g_navScreen && row == g_navRow && mode == g_navMode && *(BYTE*)0x72BAB8 != 1)
        label = g_navBlinkOff ? "" : (label[0] ? label : "?");      // blinking cursor; "?" marks an unbound cell
    // L3 combos are wider than the 34-unit cell: draw them smaller
    float* scale = (float*)(element + 0x88);
    if (*scale > 0.5f && *scale <= 1.0f) *scale = b >= 0 ? kCtlGlyphScale[b] : 1.0f;
    return label;
}

__declspec(naked) static void RemapCellLabelStub() {        // replaces 0x56EC10 (10 bytes); exit: push label, jmp 0x56EC58
    __asm {
        pushad
        push edi                                            // label element
        push ebx                                            // mode
        push dword ptr [esp + 0x3C]                         // row string id ([esp+0x14] at entry)
        push ebp                                            // screen
        call RemapCellLabel
        add esp, 16
        mov dword ptr [esp + 0x1C], eax                     // returned label into the saved eax
        popad
        push eax
        push 0x56EC58
        ret
    }
}

// ---- capture: poll the controller while the game waits for a "mouse" button
static bool g_remapArmed = false;
static int g_remapLatched = -1;

static bool RemapCaptureUpdate(const Pad& p, bool l2, bool r2) {
    if (!g_remapScreen) return false;
    if (*(BYTE*)0x72BAB8 != 1 || *(int*)0x72BAB4 != 4) { g_remapArmed = false; g_remapLatched = -1; return false; }
    bool held[CB_DRight + 1] = { p.a, p.b, p.x, p.y, p.l1, p.r1, l2, r2, p.r3, p.up, p.down, p.left, p.right };
    bool anyBase = false; for (bool h : held) anyBase |= h;
    if (p.start) { *(int*)0x6B6A10 = -2; g_remapArmed = false; g_remapLatched = -1; return true; }   // Options cancels
    if (!g_remapArmed) { if (!anyBase && !p.l3) g_remapArmed = true; return true; }                 // release the ✕ that opened it
    if (g_remapLatched < 0) {
        for (int b = 0; b <= CB_DRight; ++b) if (held[b]) {
            int code = b;
            if (p.l3) {
                if (b == CB_Cross) code = CB_L3Cross; else if (b == CB_Circle) code = CB_L3Circle;
                else if (b == CB_Square) code = CB_L3Square; else if (b == CB_Triangle) code = CB_L3Triangle;
                else if (b == CB_R2) code = CB_L3R2;
            }
            g_remapLatched = code;
            break;
        }
    } else if (!anyBase) {
        *(int*)0x6B6A10 = g_remapLatched;                   // assigned on release, like the game's own capture
        g_remapLatched = -1;
        g_remapArmed = false;
    }
    return true;
}

// ---- assignment: swap like the game does for mouse buttons (the target's old actions move to the old button)
static int __cdecl RemapAssign(DWORD screen, int code) {
    if (code < 0 || code >= CB_Count) return 0;
    int row = *(int*)(screen + 0x2E0), mode = *(int*)(screen + 0x2E4);
    if (row < 0 || row > 24 || mode < 0 || mode >= CM_Count) return 0;
    int rowMask = *(int*)(screen + 0x368 + row * 0x18);
    int oldButton = CtlButtonForRow(mode, rowMask);
    if (oldButton == code) return 1;
    int displaced = g_ctlMap[mode][code];
    g_ctlMap[mode][code] = rowMask;
    if (oldButton >= 0) g_ctlMap[mode][oldButton] = (g_ctlMap[mode][oldButton] & ~rowMask) | displaced;
    Log("controller remap: %s row %d (0x%X) -> %s (was %s)", kCtlModeKey[mode], row, rowMask, kCtlButtonKey[code],
        oldButton >= 0 ? kCtlButtonKey[oldButton] : "none");
    CtlSaveMap();
    return 1;
}

__declspec(naked) static void RemapAssignStub() {           // replaces 0x56EE4A: mov ecx,[esi+2E0h]; push edi
    __asm {
        pushad
        push eax                                            // captured code
        push esi                                            // screen
        call RemapAssign
        add esp, 8
        test eax, eax
        popad
        je original
        push edi
        push 0x56EE9D                                       // save, refresh cells, end capture
        ret
    original:
        mov ecx, dword ptr [esi + 0x2E0]
        push edi
        push 0x56EE51
        ret
    }
}

static void __cdecl RemapDefaults() {
    CtlDefaultMap();
    CtlSaveMap();
    Log("controller map reset to the default layout");
}

__declspec(naked) static void RemapDefaultStub() {          // jump table entry 0x580258 (message 2)
    __asm {
        pushad
        call RemapDefaults
        popad
        push 0x580169                                       // save, rebuild cells, refresh, ret 8
        ret
    }
}

static void RemapTitlesUpdate() {                            // string table [0x72831C] is (re)loaded with the language
    if (!g_remapScreen) return;
    char** table = *(char***)0x72831C;
    if (!table) return;
    static char title[] = "Controller";
    if (table[0xD95] != title) table[0xD95] = title;
    if (table[0xD8C] != title) table[0xD8C] = title;
}

// ---- controller navigation: the grid only reacts to mouse clicks, so the mod keeps its own cell cursor.
// D-pad / left stick move it (the selected cell blinks), Cross edits it (same fields a click sets: row [+0x2E0],
// mode [+0x2E4], request [+0x2DC]), Triangle = DEFAULT (message 2 through the handler 0x57FFB0), Circle / Options = back.
// Screen update 0x56EDD0 (vtable 0x682F38 slot +0x1C, thiscall, ret 4) runs only while the screen is shown.

static bool RemapNavActive() { return g_remapScreen && g_navScreen && GetTickCount64() - g_navLastUpdate < 250; }

static bool CellExists(DWORD screen, int row, int mode) {
    return row >= 0 && row <= 24 && mode >= 0 && mode < CM_Count && *(BYTE*)(screen + 0x2E8 + mode * 25 + row) != 0;
}

static void RefreshCells(DWORD screen) { ((void(__fastcall*)(DWORD, void*))0x56EB70)(screen, nullptr); }

static void NavClampToCell(DWORD screen) {
    if (CellExists(screen, g_navRow, g_navMode)) return;
    for (int d = 1; d < CM_Count; ++d) {                    // nearest existing column in this row
        if (CellExists(screen, g_navRow, g_navMode - d)) { g_navMode -= d; return; }
        if (CellExists(screen, g_navRow, g_navMode + d)) { g_navMode += d; return; }
    }
}

static void NavMove(DWORD screen, int dir) {                 // 1 up, 2 down, 3 left, 4 right
    if (dir == 1 || dir == 2) {
        int step = dir == 1 ? -1 : 1;
        for (int r = g_navRow + step; r >= 0 && r <= 24; r += step) {
            bool any = false; for (int m = 0; m < CM_Count; ++m) any |= CellExists(screen, r, m);
            if (any) { g_navRow = r; NavClampToCell(screen); break; }
        }
    } else {
        int step = dir == 3 ? -1 : 1;
        for (int m = g_navMode + step; m >= 0 && m < CM_Count; m += step)
            if (CellExists(screen, g_navRow, m)) { g_navMode = m; break; }
    }
    int* scroll = (int*)(screen + 0x2D4);
    int maxScroll = *(int*)(screen + 0x2D8);
    int s = *scroll;
    if (g_navRow < s) s = g_navRow;
    if (g_navRow > s + 18) s = g_navRow - 18;
    if (s < 0) s = 0; if (s > maxScroll) s = maxScroll;
    *scroll = s;
    g_navBlinkOff = false; g_navBlinkAt = GetTickCount64() + 450;
    RefreshCells(screen);
}

static void __cdecl RemapNavigate(DWORD screen) {
    ULONGLONG now = GetTickCount64();
    if (screen != g_navScreen || now - g_navLastUpdate > 1000) {   // screen (re)opened
        g_navScreen = screen; g_navRow = 0; g_navMode = 0; g_navHeldDir = 0;
        g_navPrevCross = g_navPrevTriangle = true;          // ignore the press that opened the screen
        NavClampToCell(screen);
    }
    g_navLastUpdate = now;
    if (*(BYTE*)0x72BAB8 == 1) return;                      // capturing: RemapCaptureUpdate owns the pad
    const Pad& p = g_navPad;
    int dir = (p.up || p.ly < -0.6f) ? 1 : (p.down || p.ly > 0.6f) ? 2 : (p.left || p.lx < -0.6f) ? 3 : (p.right || p.lx > 0.6f) ? 4 : 0;
    if (dir != g_navHeldDir) { g_navHeldDir = dir; if (dir) { NavMove(screen, dir); g_navRepeatAt = now + 350; } }
    else if (dir && now >= g_navRepeatAt) { NavMove(screen, dir); g_navRepeatAt = now + 110; }
    if (p.a && !g_navPrevCross && CellExists(screen, g_navRow, g_navMode)) {
        *(int*)(screen + 0x2E0) = g_navRow;
        *(int*)(screen + 0x2E4) = g_navMode;
        *(BYTE*)(screen + 0x2DC) = 1;                       // the game's update starts the capture this frame
        g_navBlinkOff = false;
        RefreshCells(screen);
    }
    if (p.y && !g_navPrevTriangle)
        ((char(__fastcall*)(DWORD, void*, int, int))0x57FFB0)(screen, nullptr, 2, 0);   // DEFAULT
    g_navPrevCross = p.a; g_navPrevTriangle = p.y;
    if (now >= g_navBlinkAt) { g_navBlinkOff = !g_navBlinkOff; g_navBlinkAt = now + (g_navBlinkOff ? 250 : 450); RefreshCells(screen); }
}

static void __fastcall RemapUpdateHook(DWORD screen, void*, int arg) {
    RemapNavigate(screen);
    ((void(__fastcall*)(DWORD, void*, int))0x56EDD0)(screen, nullptr, arg);
}

static void RemapScreenInstall() {
    if (!g_remapScreen) return;
    const BYTE labelOrig[] = { 0x8B, 0x44, 0x24, 0x18, 0x03, 0xC3, 0x8B, 0x44, 0x85, 0x00 };
    const BYTE typeOrig[] = { 0x03, 0x00, 0x00, 0x00 };
    const BYTE assignOrig[] = { 0x8B, 0x8E, 0xE0, 0x02, 0x00, 0x00, 0x57 };
    const DWORD defaultEntry = 0x5800AD;
    if (memcmp((BYTE*)0x56EC10, labelOrig, sizeof(labelOrig)) || memcmp((BYTE*)0x56EDF9, typeOrig, 4) ||
        memcmp((BYTE*)0x56EE4A, assignOrig, sizeof(assignOrig)) || *(DWORD*)0x580258 != defaultEntry ||
        *(BYTE*)0x56EDF3 != 0xC7) {
        Log("remap screen site bytes differ, controller remap screen not installed");
        g_remapScreen = 0;
        return;
    }
    WriteJmp(0x56EC10, &RemapCellLabelStub, sizeof(labelOrig));
    WriteJmp(0x56EE4A, &RemapAssignStub, sizeof(assignOrig));
    const BYTE type4[] = { 0x04, 0x00, 0x00, 0x00 };
    PatchBytes(0x56EDF9, type4, 4);
    DWORD stub = (DWORD)&RemapDefaultStub;
    PatchBytes(0x580258, (const BYTE*)&stub, 4);
    if (*(DWORD*)0x682F54 == 0x56EDD0) {
        DWORD upd = (DWORD)&RemapUpdateHook;
        PatchBytes(0x682F54, (const BYTE*)&upd, 4);
    } else {
        Log("remap screen update slot differs, controller navigation not installed");
    }
    Log("controller remap screen installed (labels 0x56EC10, capture 0x56EDF9/0x56EE4A, default 0x580258)");
}
