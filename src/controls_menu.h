// Options > Controls: CAMERA SPEED and AIM SPEED rows (ControlsMenuRows=1).
//
// The Controls menu is data like the Display one: header 0x6AF56C (items* at +0x14 = 0x6AF580, count at +0x18),
// rows of 20 bytes {1, textId, callback, arg, 0}, all five stock rows calling 0x557DC0. Two rows are appended with
// their own callback, which steps the value, saves it to the mod ini and rewrites the row's label. The labels use
// two blank string slots (0xD72 / 0xD73; the game's text file has empty lines there), the same trick as the
// MENU SIZE / HUD SIZE rows in display_fix.h.
#pragma once

static int g_controlsRows = 1;
static DWORD g_controlsItems[8][5];
static char g_camSpeedText[32] = "CAMERA SPEED", g_aimSpeedText[32] = "AIM SPEED";
static const DWORD kCamSpeedTextId = 0xD72, kAimSpeedTextId = 0xD73;

static const int kCamSteps[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 125, 150 };
static const int kAimSteps[] = { 256, 384, 512, 768, 1024, 1536, 2048 };

static void UpdateControlsLabels() {
    if (!g_controlsRows) return;
    sprintf_s(g_camSpeedText, "CAMERA SPEED %d%%", cfg.cameraSpeed);
    sprintf_s(g_aimSpeedText, "AIM SPEED %d%%", (int)(cfg.aimSpeed * 100.0f / 1024.0f + 0.5f));
    char** table = *(char***)0x72831C;
    if (!table) return;
    if (table[kCamSpeedTextId] != g_camSpeedText) table[kCamSpeedTextId] = g_camSpeedText;
    if (table[kAimSpeedTextId] != g_aimSpeedText) table[kAimSpeedTextId] = g_aimSpeedText;
}

template <size_t N> static int NextStep(const int (&steps)[N], int value) {
    for (int s : steps) if (s > value) return s;
    return steps[0];
}

static char __fastcall SpeedRowCallback(void*, void*, int arg, int) {     // called like 0x557DC0 (thiscall, ret 8)
    int which = (arg == 5) ? 0 : 1;                                      // args are row indices, see ControlsMenuInstall
    if (which == 0) {
        cfg.cameraSpeed = NextStep(kCamSteps, cfg.cameraSpeed);
        WriteIniInt("Controller", "CameraSpeed", cfg.cameraSpeed, g_modIniPath);
    } else {
        cfg.aimSpeed = NextStep(kAimSteps, cfg.aimSpeed);
        WriteIniInt("Controller", "AimSpeed", cfg.aimSpeed, g_modIniPath);
    }
    UpdateControlsLabels();
    ((void(__cdecl*)(int, float, float))0x4CA4B0)(0xFF, 1.0f, 1.0f);      // menu confirm sound
    Log("controls: %s", which == 0 ? g_camSpeedText : g_aimSpeedText);
    return 1;
}

static void ControlsMenuInstall() {
    if (!g_controlsRows) return;
    const DWORD* header = (const DWORD*)0x6AF56C;
    const DWORD* items = (const DWORD*)0x6AF508;
    if (header[5] != 0x6AF508 || header[6] != 5 || items[0] != 1 || items[1] != 0xD8D || items[2] != 0x557DC0) {
        Log("controls menu table differs, speed rows not added");
        return;
    }
    memcpy(g_controlsItems, items, 5 * sizeof(g_controlsItems[0]));
    const DWORD cb = (DWORD)&SpeedRowCallback;
    // The screen shows a caption for the highlighted row, looked up by the row's arg (the CONTROLLER row shows the
    // joystick name that way), so small args picked up unrelated strings. The callback tells the rows apart by their
    // text id instead, and the args are the row indices, which have no caption.
    const DWORD cam[5] = { 1, kCamSpeedTextId, cb, 5, 0 };
    const DWORD aim[5] = { 1, kAimSpeedTextId, cb, 6, 0 };
    memcpy(g_controlsItems[5], cam, sizeof(cam));
    memcpy(g_controlsItems[6], aim, sizeof(aim));
    DWORD old;
    VirtualProtect((LPVOID)0x6AF580, 8, PAGE_READWRITE, &old);
    *(DWORD*)0x6AF580 = (DWORD)&g_controlsItems[0][0];
    *(DWORD*)0x6AF584 = 7;
    VirtualProtect((LPVOID)0x6AF580, 8, old, &old);
    UpdateControlsLabels();
    Log("controls menu: added CAMERA SPEED / AIM SPEED rows (7 items)");
}
