// Rumble and lightbar for TrueCrimeDualSense.
// Included from tcla_dualsense.cpp after g_ds, cfg, Log and State are defined.
//
// Rumble: the game's vibration manager (0x551350) calls 0x5FE480(motorA, motorB) on the input block with
// 0..255 values, only when the Vibration option (0x7527AC) is on, and (0,0) when pausing/stopping.
// On PC 0x5FE480 is just "ret 8"; it is replaced with a jump to OnGameVibrate.
#pragma once

struct ExtrasConfig {
    int rumble = 1;              // forward the game's vibration to the DualSense motors
    int rumbleStrength = 100;    // percent
    int lightbar = 1;
    int lightR = 0, lightG = 40, lightB = 255;   // police blue
} xcfg;

static PadOutput g_out, g_sentOut;
static ULONGLONG g_nextOutputRefresh = 0;
static int g_gameMotorA = 0, g_gameMotorB = 0;
static bool g_loggedWrite = false;

static int g_vibCalls = 0, g_vibNonZero = 0, g_vibMaxA = 0, g_vibMaxB = 0;
static int g_outWrites = 0, g_outErrors = 0;
static ULONGLONG g_nextExtrasReport = 0;

static void __stdcall OnGameVibrate(int motorA, int motorB) {   // replaces 0x5FE480 (ret 8)
    g_gameMotorA = motorA < 0 ? 0 : (motorA > 255 ? 255 : motorA);
    g_gameMotorB = motorB < 0 ? 0 : (motorB > 255 ? 255 : motorB);
    ++g_vibCalls;
    if (g_gameMotorA || g_gameMotorB) {
        ++g_vibNonZero;
        if (g_gameMotorA > g_vibMaxA) g_vibMaxA = g_gameMotorA;
        if (g_gameMotorB > g_vibMaxB) g_vibMaxB = g_gameMotorB;
    }
}

// Debug evidence: once per second when something happened, log what the game asked for and what was sent.
static void ExtrasReport() {
    if (!cfg.debugLog || GetTickCount64() < g_nextExtrasReport) return;
    g_nextExtrasReport = GetTickCount64() + 1000;
    if (!g_vibNonZero && !g_outErrors) { g_vibCalls = 0; g_outWrites = 0; return; }
    Log("extras: game vibration calls=%d nonzero=%d max=(%d,%d) | output writes=%d errors=%d (last %lu)",
        g_vibCalls, g_vibNonZero, g_vibMaxA, g_vibMaxB, g_outWrites, g_outErrors, g_ds.LastWriteError());
    g_vibCalls = g_vibNonZero = g_vibMaxA = g_vibMaxB = g_outWrites = g_outErrors = 0;
}

static void ExtrasLoadConfig(const char* iniPath) {
    auto get = [&](const char* k, int def) { return (int)GetPrivateProfileIntA("Controller", k, def, iniPath); };
    xcfg.rumble = get("Rumble", xcfg.rumble);
    xcfg.rumbleStrength = get("RumbleStrength", xcfg.rumbleStrength);
    xcfg.lightbar = get("Lightbar", xcfg.lightbar);
    xcfg.lightR = get("LightbarRed", xcfg.lightR);
    xcfg.lightG = get("LightbarGreen", xcfg.lightG);
    xcfg.lightB = get("LightbarBlue", xcfg.lightB);
}

static void ExtrasInstallHooks() {
    BYTE* site = (BYTE*)0x5FE480;
    const BYTE expected[] = { 0xC2, 0x08, 0x00, 0xCC, 0xCC };   // ret 8 ; int3 padding
    if (memcmp(site, expected, sizeof(expected)) != 0) { Log("vibration stub bytes differ, rumble disabled"); xcfg.rumble = 0; return; }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE9;
    *(int*)(site + 1) = (int)((BYTE*)&OnGameVibrate - (site + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("vibration hook installed at 0x5FE480");
}

static void ExtrasUpdate(int, bool padActive) {
    if (!g_ds.IsOpen() || !padActive) return;
    PadOutput o;
    if (xcfg.rumble) {
        o.motorLeft = (uint8_t)(g_gameMotorA * xcfg.rumbleStrength / 100 > 255 ? 255 : g_gameMotorA * xcfg.rumbleStrength / 100);
        o.motorRight = (uint8_t)(g_gameMotorB * xcfg.rumbleStrength / 100 > 255 ? 255 : g_gameMotorB * xcfg.rumbleStrength / 100);
    }
    if (xcfg.lightbar) { o.red = (uint8_t)xcfg.lightR; o.green = (uint8_t)xcfg.lightG; o.blue = (uint8_t)xcfg.lightB; }
    o.playerLeds = 0x04;                                  // centre LED = player 1
    g_out = o;

    // Send on change, and refresh every 2 s in case the controller dropped the state (e.g. reconnect).
    if (g_out != g_sentOut || GetTickCount64() >= g_nextOutputRefresh) {
        if (g_ds.Send(g_out)) {
            ++g_outWrites;
            g_sentOut = g_out;
            g_nextOutputRefresh = GetTickCount64() + 2000;
            if (!g_loggedWrite) {
                // one-time check of the real result: wait for the first write to complete
                DWORD err = g_ds.WaitWriteResult(200);
                Log("output report %s (bluetooth %d, length %u, error %lu)", err ? "FAILED" : "accepted",
                    g_ds.IsBluetooth(), g_ds.OutputLength(), err);
                g_loggedWrite = true;
            }
        } else if (g_ds.LastWriteError() && !g_loggedWrite) {
            Log("output report failed, error %lu", g_ds.LastWriteError());
            g_loggedWrite = true;
        }
    }
}

static void ExtrasShutdown() {
    if (!g_ds.IsOpen()) return;
    PadOutput off;
    g_ds.Send(off);
}
