// PS2 look for the city map (CityMapPS2Look=1).
//
// The pause-menu city map is shell screen 0x12 (pause item "CITY MAP" opens it via 0x558670(0x12); screen objects
// [shell + 0x10 + id*4], current screen [shell], shell [0x728320]). Its render 0x4D8460 draws a black quad above the
// map and the UI_Map texture (map, grid, fade at its bottom) on top of the shell background, so on PC the ShellBG
// photo shows through below the map. The PS2 version shows black there. While screen 0x12 is current, the shell
// background draw (ui_fix.h, BackgroundRectStub at 0x55DEBA/0x55DFAC) clears to black instead of drawing the photo.
#pragma once

static int g_cityMapPs2 = 1;
static DWORD g_cityMapLoggedScreen = 0xFFFFFFFF;

static bool __cdecl CityMapHidesShellBackground() {
    if (!g_cityMapPs2) return false;
    DWORD shell = *(DWORD*)0x728320;
    if (!shell) return false;
    DWORD cur = *(DWORD*)shell;
    bool map = cur && cur == *(DWORD*)(shell + 0x10 + 0x12 * 4);
    if (cur != g_cityMapLoggedScreen) {
        g_cityMapLoggedScreen = cur;
        Log("shell screen %p (vtable %p)%s", (void*)cur, cur ? *(void**)cur : nullptr, map ? ": city map, background hidden" : "");
    }
    return map;
}
