// Adds "save game" to the single-player pause menu.
//
// Pause menu (screen 0x10) header at 0x6AF6F0: {title 0x1E6, 1, x, y, flags, items*, count, 3, help 0x1F0, 0};
// items pointer at 0x6AF704, count at 0x6AF708. Items are 20-byte records {type 1, textId, callback, arg, 0}:
//   0x6AF6A0 player stats  0x558670(0x11)   0x6AF6B4 CITY MAP 0x558670(0x12)
//   0x6AF6C8 OPTIONS       0x558670(4)      0x6AF6DC EXIT     0x557DC0(2)
// 0x558670 opens screen `arg` and records the pause screen as "previous", so Back returns to the pause menu.
// Screen 3 is the save/load screen in manual-save mode (slot selection, writes saveN.bin via 0x563540).
// Text id 0x46 = TCPC??.txt line 71 "save game". The table lives in writable .data; the new 5-item table is
// kept in this DLL and the header is pointed at it.
#pragma once

static int g_pauseSave = 1;
static DWORD g_pauseItems[5][5];

static void PauseSaveInstall() {
    if (!g_pauseSave) return;
    const DWORD* header = (const DWORD*)0x6AF6F0;
    const DWORD* items = (const DWORD*)0x6AF6A0;
    const DWORD expect[4][5] = {
        { 1, 0x1E7, 0x558670, 0x11, 0 },
        { 1, 0x1E8, 0x558670, 0x12, 0 },
        { 1, 0x1E9, 0x558670, 0x04, 0 },
        { 1, 0x1EA, 0x557DC0, 0x02, 0 },
    };
    if (header[0] != 0x1E6 || header[5] != 0x6AF6A0 || header[6] != 4 || memcmp(items, expect, sizeof(expect)) != 0) {
        Log("pause menu table differs, save option not added");
        return;
    }
    memcpy(g_pauseItems[0], expect[0], sizeof(g_pauseItems[0]));   // player stats
    memcpy(g_pauseItems[1], expect[1], sizeof(g_pauseItems[1]));   // CITY MAP
    const DWORD save[5] = { 1, 0x46, 0x558670, 0x03, 0 };            // save game -> screen 3 (manual save)
    memcpy(g_pauseItems[2], save, sizeof(save));
    memcpy(g_pauseItems[3], expect[2], sizeof(g_pauseItems[3]));   // OPTIONS
    memcpy(g_pauseItems[4], expect[3], sizeof(g_pauseItems[4]));   // EXIT
    DWORD old;
    VirtualProtect((LPVOID)0x6AF704, 8, PAGE_READWRITE, &old);
    *(DWORD*)0x6AF704 = (DWORD)&g_pauseItems[0][0];
    *(DWORD*)0x6AF708 = 5;
    VirtualProtect((LPVOID)0x6AF704, 8, old, &old);
    Log("pause menu: added \"save game\" (5 items)");
}
