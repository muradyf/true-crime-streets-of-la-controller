// Remappable controller layout: action bits per (game mode, controller button), like the game's own [Mouse2] masks.
// Modes follow the Mouse Controls columns: on foot, fighting, guns, driving, stealth. Defaults are the original Xbox
// layout (see README). Stored in [ControllerMap] of TrueCrimeDualSense.ini as "<Mode>.<Button>=0x<bits>".
// Sticks, Options (pause/back), Create/touchpad (map) and the driving throttle axis are not remappable.
#pragma once

enum CtlMode { CM_Ped = 0, CM_Combat, CM_Gun, CM_Driver, CM_Stealth, CM_Count };
enum CtlButton {
    CB_Cross = 0, CB_Circle, CB_Square, CB_Triangle, CB_L1, CB_R1, CB_L2, CB_R2, CB_R3,
    CB_DUp, CB_DDown, CB_DLeft, CB_DRight,
    CB_L3Cross, CB_L3Circle, CB_L3Square, CB_L3Triangle, CB_L3R2,
    CB_Count
};
static const char* kCtlModeKey[CM_Count] = { "OnFoot", "Fighting", "Guns", "Driving", "Stealth" };
static const char* kCtlButtonKey[CB_Count] = {
    "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "R3",
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight",
    "L3+Cross", "L3+Circle", "L3+Square", "L3+Triangle", "L3+R2" };

static int g_ctlMap[CM_Count][CB_Count];
static char g_ctlIniPath[MAX_PATH] = "";

static int CtlModeForState(int state) {
    switch (state) {
    case Ped: return CM_Ped; case Combat: return CM_Combat; case Gun: return CM_Gun;
    case Driver: return CM_Driver; case Stealth: return CM_Stealth; default: return -1;
    }
}

static void CtlDefaultMap() {
    memset(g_ctlMap, 0, sizeof(g_ctlMap));
    static const int onFootModes[] = { CM_Ped, CM_Combat, CM_Gun };
    for (int m : onFootModes) {
        int* b = g_ctlMap[m];
        b[CB_L3Square] = bit::FlashBadge;                 // L3 + X (Xbox manual)
        b[CB_L3R2] = bit::WarningShot;                    // L3 + RT
        b[CB_L3Circle] = bit::Arrest | bit::Frisk;        // L3 + B
        b[CB_Square] = bit::Punch;
        b[CB_Circle] = bit::Grab;                         // grapple / throw / pick up / human shield
        b[CB_R2] = bit::Fire;                             // tap draws guns, hold = precision targeting
        b[CB_Cross] = (m == CM_Gun) ? bit::Cover : bit::Kick;
        b[CB_Triangle] = bit::Jump;
        if (m != CM_Gun) b[CB_L1] = bit::Block;
        b[CB_R1] = bit::Reload;
        b[CB_L2] = bit::Commandeer;
        b[CB_R3] = bit::CenterCamera;
        b[CB_DDown] = bit::NormalMode; b[CB_DLeft] = bit::FightMode; b[CB_DRight] = bit::GunMode;
    }
    {
        int* b = g_ctlMap[CM_Stealth];
        b[CB_Cross] = bit::Cover; b[CB_Triangle] = bit::Jump;
        b[CB_Square] = bit::Punch; b[CB_Circle] = bit::Grab;   // stun / deadly attack (TCPCUS.txt line 520)
        b[CB_R2] = bit::Fire; b[CB_L2] = bit::Commandeer;
        b[CB_DDown] = bit::NormalMode; b[CB_DLeft] = bit::FightMode; b[CB_DRight] = bit::GunMode;
    }
    {
        int* b = g_ctlMap[CM_Driver];
        if (cfg.triggersDrive) { b[CB_R2] = bit::Accel; b[CB_L2] = bit::Brake; b[CB_R1] = bit::Fire; b[CB_L1] = bit::Commandeer; }
        else { b[CB_R2] = bit::Fire; b[CB_L2] = bit::Commandeer; }
        b[CB_Cross] = bit::Accel; b[CB_Square] = bit::Brake;
        b[CB_Circle] = bit::Handbrake; b[CB_Triangle] = bit::RearView;
        b[CB_DUp] = bit::Siren; b[CB_DDown] = bit::Horn;
        b[CB_DLeft] = bit::CarCamera; b[CB_DRight] = bit::CarCamera;
        b[CB_R3] = bit::SkipTrack;
    }
}

static void CtlLoadMap(const char* iniPath) {
    strcpy_s(g_ctlIniPath, iniPath);
    CtlDefaultMap();
    int overrides = 0;
    for (int m = 0; m < CM_Count; ++m)
        for (int b = 0; b < CB_Count; ++b) {
            char key[48], val[32];
            sprintf_s(key, "%s.%s", kCtlModeKey[m], kCtlButtonKey[b]);
            GetPrivateProfileStringA("ControllerMap", key, "", val, sizeof(val), iniPath);
            if (val[0]) { g_ctlMap[m][b] = (int)strtoul(val, nullptr, 0); ++overrides; }
        }
    if (overrides) Log("controller map: %d custom bindings from [ControllerMap]", overrides);
}

// Writes only bindings that differ from the defaults, so a reset clears the section.
static void CtlSaveMap() {
    if (!g_ctlIniPath[0]) return;
    int saved[CM_Count][CB_Count];
    memcpy(saved, g_ctlMap, sizeof(saved));
    CtlDefaultMap();
    int defaults[CM_Count][CB_Count];
    memcpy(defaults, g_ctlMap, sizeof(defaults));
    memcpy(g_ctlMap, saved, sizeof(saved));
    WritePrivateProfileStringA("ControllerMap", nullptr, nullptr, g_ctlIniPath);   // clear the section
    int written = 0;
    for (int m = 0; m < CM_Count; ++m)
        for (int b = 0; b < CB_Count; ++b)
            if (g_ctlMap[m][b] != defaults[m][b]) {
                char key[48], val[32];
                sprintf_s(key, "%s.%s", kCtlModeKey[m], kCtlButtonKey[b]);
                sprintf_s(val, "0x%X", (unsigned)g_ctlMap[m][b]);
                WritePrivateProfileStringA("ControllerMap", key, val, g_ctlIniPath);
                ++written;
            }
    Log("controller map saved (%d bindings differ from the default layout)", written);
}

// Action bits for the pressed buttons in this mode. L3 combos replace the base button while L3 is held.
static int CtlBitsForPad(int mode, const Pad& p, bool l2, bool r2) {
    const int* map = g_ctlMap[mode];
    bool held[CB_Count] = {};
    held[CB_Cross] = p.a; held[CB_Circle] = p.b; held[CB_Square] = p.x; held[CB_Triangle] = p.y;
    held[CB_L1] = p.l1; held[CB_R1] = p.r1; held[CB_L2] = l2; held[CB_R2] = r2; held[CB_R3] = p.r3;
    held[CB_DUp] = p.up; held[CB_DDown] = p.down; held[CB_DLeft] = p.left; held[CB_DRight] = p.right;
    static const int comboOf[][2] = { { CB_Cross, CB_L3Cross }, { CB_Circle, CB_L3Circle }, { CB_Square, CB_L3Square },
                                       { CB_Triangle, CB_L3Triangle }, { CB_R2, CB_L3R2 } };
    int bits = 0;
    if (p.l3)
        for (auto& c : comboOf)
            if (held[c[0]] && map[c[1]]) { bits |= map[c[1]]; held[c[0]] = false; }
    for (int b = 0; b <= CB_DRight; ++b) if (held[b]) bits |= map[b];
    return bits;
}
