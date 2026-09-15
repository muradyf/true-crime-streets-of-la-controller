// TrueCrimeDualSense.asi - native analog controller input for True Crime: Streets of LA (PC).
//
// The PC build creates a DirectInput joystick but never reads it during gameplay. Each frame the
// game's input update (thunk 0x5EA160 -> 0x5E9350, this = 0x72BAC0) fills an Xbox-style input
// block from keyboard and mouse. This plugin runs that update, then merges DualSense (HID) or
// XInput state into the same block.
//
// Input block @ 0x72BAC0 (verified by disassembly and live memory reads):
//   +0x04 action bits (held)      +0x08 system flags (0x100 confirm, 0x220 back, 0x10 pause, 0x20 map)
//   +0x0C action bits (pressed)   +0x10 camera X      +0x14 camera Y (driving: throttle/brake)
//   +0x18 move X                  +0x1C move Y (negative = up)
// Axes 0-3 are consumed as signed bytes with a deadzone and /128 scaling.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include "dualsense_hid.h"

namespace addr {
    const DWORD InputThunk   = 0x5EA160;   // mov ecx, 0x72BAC0 ; jmp 0x5E9350
    const DWORD InputUpdate  = 0x5E9350;
    const DWORD InputBlock   = 0x72BAC0;
    const DWORD PlayerPtr    = 0x6D9578;   // player object; +0x09 control state, +0xD68 == 3 free aim
    const DWORD CtrlMode     = 0x70CE48;   // < 2 = gameplay controls, >= 2 = menu controls
}

namespace bit {   // action bits, from the keyboard bindings table in 0x5E9350
    const int Jump = 0x11, Kick = 0x402, Punch = 0x804, Block = 0x20, Grab = 0x48, Frisk = 0x40,
              Arrest = 0x80, FlashBadge = 0x200, Cover = 0x4000, Reload = 0x2000, Fire = 0x1000,
              WarningShot = 0x8000100, Commandeer = 0x8000, CenterCamera = 0x10000,
              NormalMode = 0x20000, FightMode = 0x40000, GunMode = 0x80000,
              Accel = 0x100000, Brake = 0x200000, Handbrake = 0x400000, Horn = 0x800000,
              Siren = 0x1000000, CarCamera = 0x2000000, RearView = 0x4000000, SkipTrack = 0x10000000;
    const int FlagConfirm = 0x100, FlagBack = 0x220, FlagPause = 0x10, FlagMap = 0x20;
}

enum State { Menu = 0, Ped = 1, Gun = 2, Combat = 3, Stealth = 4, Driver = 5 };

struct Config {
    int stickDeadzone = 12;      // percent, applied before writing (the game adds its own)
    int triggerThreshold = 30;   // percent of trigger travel that counts as a press
    int invertCameraX = 0;
    int invertCameraY = 0;
    int invertThrottle = 0;
    int triggersDrive = 0;       // 0 = Xbox layout (right stick accelerate/brake); 1 = R2/L2 analog, fire R1, exit L1
    int aimSpeed = 1024;         // precision-aim speed at full stick; the game turns value/1024 degrees per frame (0x519647)
    int buttonPrompts = 1;       // show DualSense button icons in tutorial/help text instead of PC key names
    int debugLog = 0;
} cfg;

static FILE* g_log = nullptr;
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

// ---------------------------------------------------------------- controller sources
struct Pad {
    bool ok = false;
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1, y up = -1
    float l2 = 0, r2 = 0;                   // 0..1
    bool a = 0, b = 0, x = 0, y = 0, l1 = 0, r1 = 0, l3 = 0, r3 = 0, start = 0, back = 0, map = 0;
    bool up = 0, down = 0, left = 0, right = 0;
    const char* source = "none";
};

static DualSense g_ds;
static PadState g_dsState;
static ULONGLONG g_nextOpenTry = 0;

#include "extras.h"
#include "device_fix.h"
#include "pause_save.h"

static float Axis8(uint8_t v) { float f = (v - 128) / 127.0f; return f < -1 ? -1 : (f > 1 ? 1 : f); }

static void ApplyStickDeadzone(float& x, float& y) {
    float dz = cfg.stickDeadzone / 100.0f, m = std::sqrt(x * x + y * y);
    if (m <= dz) { x = y = 0; return; }
    float s = (m - dz) / (1 - dz) / m; if (m > 1) s /= m;
    x *= s; y *= s;
}

static bool ReadDualSense(Pad& p) {
    if (!g_ds.IsOpen()) {
        // Device enumeration can take tens of ms, so retry rarely while another controller is in use.
        if (GetTickCount64() < g_nextOpenTry) return false;
        XINPUT_STATE xs{};
        bool xinputPresent = false;
        for (DWORD i = 0; i < 4 && !xinputPresent; ++i) xinputPresent = XInputGetState(i, &xs) == ERROR_SUCCESS;
        g_nextOpenTry = GetTickCount64() + (xinputPresent ? 30000 : 3000);
        if (!g_ds.Open()) return false;
        Log("DualSense opened (bluetooth %d, full-report switch %d, error %lu)",
            g_ds.IsBluetooth(), g_ds.FeatureResult(), g_ds.FeatureError());
    }
    if (!g_ds.Poll(g_dsState)) { Log("DualSense lost"); return false; }
    if (!g_dsState.connected) return false;
    const PadState& s = g_dsState;
    p.lx = Axis8(s.lx); p.ly = Axis8(s.ly); p.rx = Axis8(s.rx); p.ry = Axis8(s.ry);
    p.l2 = s.l2 / 255.0f; p.r2 = s.r2 / 255.0f;
    p.a = s.cross; p.b = s.circle; p.x = s.square; p.y = s.triangle;
    p.l1 = s.l1; p.r1 = s.r1; p.l3 = s.l3; p.r3 = s.r3;
    p.start = s.options; p.back = s.create; p.map = s.touchpad;
    p.up = s.hat == 7 || s.hat == 0 || s.hat == 1; p.right = s.hat >= 1 && s.hat <= 3;
    p.down = s.hat >= 3 && s.hat <= 5;            p.left = s.hat >= 5 && s.hat <= 7;
    p.source = s.bluetooth ? "DualSense (Bluetooth)" : "DualSense (USB)";
    return p.ok = true;
}

static bool ReadXInput(Pad& p) {
    for (DWORD i = 0; i < 4; ++i) {
        XINPUT_STATE xs{};
        if (XInputGetState(i, &xs) != ERROR_SUCCESS) continue;
        const XINPUT_GAMEPAD& g = xs.Gamepad;
        p.lx = g.sThumbLX / 32767.0f; p.ly = -g.sThumbLY / 32767.0f;
        p.rx = g.sThumbRX / 32767.0f; p.ry = -g.sThumbRY / 32767.0f;
        p.l2 = g.bLeftTrigger / 255.0f; p.r2 = g.bRightTrigger / 255.0f;
        WORD w = g.wButtons;
        p.a = w & XINPUT_GAMEPAD_A; p.b = w & XINPUT_GAMEPAD_B; p.x = w & XINPUT_GAMEPAD_X; p.y = w & XINPUT_GAMEPAD_Y;
        p.l1 = w & XINPUT_GAMEPAD_LEFT_SHOULDER; p.r1 = w & XINPUT_GAMEPAD_RIGHT_SHOULDER;
        p.l3 = w & XINPUT_GAMEPAD_LEFT_THUMB; p.r3 = w & XINPUT_GAMEPAD_RIGHT_THUMB;
        p.start = w & XINPUT_GAMEPAD_START; p.back = w & XINPUT_GAMEPAD_BACK; p.map = false;
        p.up = w & XINPUT_GAMEPAD_DPAD_UP; p.down = w & XINPUT_GAMEPAD_DPAD_DOWN;
        p.left = w & XINPUT_GAMEPAD_DPAD_LEFT; p.right = w & XINPUT_GAMEPAD_DPAD_RIGHT;
        p.source = "XInput";
        return p.ok = true;
    }
    return false;
}

// ---------------------------------------------------------------- merge into the game's input block
static int ToByteAxis(float v) { int i = (int)std::lround(v * 127.0f); return i < -127 ? -127 : (i > 127 ? 127 : i); }

static int g_prevButtons = 0;
static bool g_prevStart = false, g_prevBack = false, g_prevA = false;
static const char* g_lastSource = nullptr;
static int g_lastState = -1;
static int g_lastFreeAim = -1;
static ULONGLONG g_nextDebug = 0;

static void MergePad() {
    Pad p;
    if (!ReadDualSense(p) && !ReadXInput(p)) {
        if (g_lastSource) { Log("no controller"); g_lastSource = nullptr; }
        g_prevButtons = *(int*)(addr::InputBlock + 0x04);
        return;
    }
    if (g_lastSource != p.source) { Log("controller: %s", p.source); g_lastSource = p.source; }
    ApplyStickDeadzone(p.lx, p.ly);
    ApplyStickDeadzone(p.rx, p.ry);
    const float trig = cfg.triggerThreshold / 100.0f;
    bool l2 = p.l2 > trig, r2 = p.r2 > trig;

    int* blk = (int*)addr::InputBlock;
    DWORD player = *(DWORD*)addr::PlayerPtr;
    int ctrlMode = *(int*)addr::CtrlMode;
    int state = (player && ctrlMode < 2) ? *(BYTE*)(player + 0x09) : Menu;
    bool freeAim = player && ctrlMode < 2 && *(int*)(player + 0xD68) == 3;
    if (state != g_lastState || (int)freeAim != g_lastFreeAim) {
        Log("control state %d (ctrlMode %d, freeAim %d)", state, ctrlMode, freeAim);
        g_lastState = state; g_lastFreeAim = freeAim;
    }

    int bits = 0, flags = 0;
    int moveX = ToByteAxis(p.lx), moveY = ToByteAxis(p.ly);
    // The game's own mouse path (0x5E987D) stores camera X as -mouseX and camera Y (+0x14) so that up is positive.
    int camX = ToByteAxis(cfg.invertCameraX ? p.rx : -p.rx), camY = ToByteAxis(cfg.invertCameraY ? p.ry : -p.ry);

    // system buttons (all states)
    if (p.start && !g_prevStart) flags |= (state == Menu ? bit::FlagBack : 0);
    if (!p.start && g_prevStart && state != Menu) flags |= bit::FlagPause;   // game pauses on ESC release
    if (p.back || p.map) flags |= bit::FlagMap;

    switch (state) {
    case Menu:
        if (p.up) moveY = -127; if (p.down) moveY = 127; if (p.left) moveX = -127; if (p.right) moveX = 127;
        if (p.a) flags |= bit::FlagConfirm;
        if (p.b && !g_prevBack) flags |= bit::FlagBack;
        break;
    // Layout follows the original Xbox manual (A=Cross, B=Circle, X=Square, Y=Triangle, White=L1, Black=R1,
    // LT=L2, RT=R2, left-thumbstick click=L3). Badge / warning shot / arrest+frisk are L3 combos in the manual.
    case Ped: case Combat: case Gun:
        if (p.l3) {                                                   // "left thumbstick button +" combos
            if (p.x) bits |= bit::FlashBadge;                         // L3 + X
            if (r2)  bits |= bit::WarningShot;                        // L3 + RT
            if (p.b) bits |= bit::Arrest | bit::Frisk;                // L3 + B
        } else {
            if (p.x) bits |= bit::Punch;
            if (p.b) bits |= bit::Grab;                               // grapple / throw / pick up / human shield
            if (r2)  bits |= bit::Fire;                               // tap draws guns, hold = precision targeting
        }
        if (p.a) bits |= (state == Gun ? bit::Cover : bit::Kick);     // shooting: hold A = take cover
        if (p.y) bits |= bit::Jump;                                   // jump kick / roll / dive
        if (p.l1 && state != Gun) bits |= bit::Block;                 // White (hold)
        if (p.r1) bits |= bit::Reload;                                // Black
        if (l2) bits |= bit::Commandeer;                              // LT: get in / commandeer
        if (p.r3) bits |= bit::CenterCamera;                          // not in the manual; PC action
        if (p.down) bits |= bit::NormalMode;
        if (p.left) bits |= bit::FightMode; if (p.right) bits |= bit::GunMode;
        break;
    case Stealth:
        if (p.a) bits |= bit::Cover;  if (p.y) bits |= bit::Jump;    // hold A cover, Y roll
        // stun on Punch, deadly attack on Grab (TCPCUS.txt line 520; Xbox manual: X / B)
        if (p.x) bits |= bit::Punch;  if (p.b) bits |= bit::Grab;
        if (r2)  bits |= bit::Fire;   if (l2) bits |= bit::Commandeer;
        if (p.down) bits |= bit::NormalMode; if (p.left) bits |= bit::FightMode; if (p.right) bits |= bit::GunMode;
        break;
    case Driver: {
        float throttle = -p.ry;                       // Xbox: right thumbstick accelerate / brake
        if (cfg.triggersDrive) {                      // optional alternative: R2/L2 analog, fire on R1, exit on L1
            throttle = p.r2 - p.l2;
            if (r2) bits |= bit::Accel;
            if (l2) bits |= bit::Brake;
            if (p.r1) bits |= bit::Fire;
            if (p.l1) bits |= bit::Commandeer;
        } else {
            if (r2) bits |= bit::Fire;                // RT fire weapon
            if (l2) bits |= bit::Commandeer;          // LT get in / out / commandeer
        }
        if (cfg.invertThrottle) throttle = -throttle;
        camY = ToByteAxis(throttle);
        camX = 0;
        if (p.a) bits |= bit::Accel;      if (p.x) bits |= bit::Brake;
        if (p.b) bits |= bit::Handbrake;  if (p.y) bits |= bit::RearView;
        if (p.up) bits |= bit::Siren;     if (p.down) bits |= bit::Horn;
        if (p.left || p.right) bits |= bit::CarCamera;   // tap D-pad: change view
        if (p.r3) bits |= bit::SkipTrack;                // not in the manual; PC action
        break;
    }
    default: break;
    }

    // Precision targeting: the manual moves the reticule with the LEFT analog stick. The game reads these axes as
    // full-range per-frame deltas (0x4FBAB0 callers at 0x519615), the same values its mouse path writes.
    if (freeAim) {
        auto curve = [](float v) { return v * std::fabs(v); };   // finer control near centre, full speed at the edge
        int ax = (int)std::lround(curve(p.lx) * cfg.aimSpeed), ay = (int)std::lround(curve(p.ly) * cfg.aimSpeed);
        if (ax || ay) { blk[6] = ax; blk[7] = ~(-ay); }
        moveX = moveY = 0;
    }

    // held and newly-pressed action bits; keyboard/mouse input stays active
    int held = blk[1] | bits;
    blk[3] = held & ~g_prevButtons;
    blk[1] = held;
    g_prevButtons = held;
    blk[2] |= flags;

    // analog axes: the stick wins when deflected, otherwise keep keyboard/mouse values
    if (!freeAim) {
        if (moveX) blk[6] = moveX;
        if (moveY) blk[7] = moveY;
    }
    if (camX) blk[4] = camX;
    if (camY) blk[5] = camY;

    g_prevStart = p.start; g_prevBack = p.b; g_prevA = p.a;

    ExtrasUpdate(state, p.source && p.source[0] == 'D');   // DualSense only: rumble, lightbar, trigger effects

    if (cfg.debugLog && GetTickCount64() >= g_nextDebug) {
        g_nextDebug = GetTickCount64() + 500;
        Log("state=%d L(%.2f,%.2f) R(%.2f,%.2f) L2=%.2f R2=%.2f -> bits=%08X flags=%03X axes cam(%d,%d) move(%d,%d)",
            state, p.lx, p.ly, p.rx, p.ry, p.l2, p.r2, blk[1], blk[2], blk[4], blk[5], blk[6], blk[7]);
    }
}

// ---------------------------------------------------------------- button prompts
// Text placeholders "%<c>" are expanded by 0x557BC0: action index = c - 'a', binding = table[index + 8] (0x56B920),
// then name = 0x61E5C0(dik). The call to 0x61E5C0 at 0x557C0F is redirected here; the stack still holds the index.
// Glyph codes are the Xbox button icons in Font_UI_Small.fnt, redrawn as DualSense symbols by the font patch.
namespace glyph {
    const char* Cross = "\x80", *Triangle = "\x81", *Circle = "\x82", *Square = "\x83";
    const char* L2 = "\xA2", *LStick = "\xA3", *RStick = "\xA4", *R2 = "\xA5", *L1 = "\xA6", *R1 = "\xA7";
    const char* Left = "\x8E", *Right = "\x8F", *Up = "\x90", *Down = "\x91";
}

static const char* PadLabelForBinding(int b) {
    using namespace glyph;
    switch (b) {
    case 0:  return cfg.triggersDrive ? R2 : RStick;          // MoveUp (accelerate in the driving tutorial)
    case 1:  return cfg.triggersDrive ? L2 : RStick;          // MoveDown (brake)
    case 2: case 3: return LStick;                            // MoveLeft / MoveRight
    case 4: case 5: case 6: case 7: return RStick;            // Zoom, pan (switch targets)
    case 8:  return "\xA4 click";                             // CenterCamera (R3)
    case 9:  return R2;                                       // FireAimGuns
    case 10: return "\xA3+\xA5";                              // WarningShot: L3 + R2 (Xbox manual)
    case 11: return L2;                                       // Commandeer
    case 12: return "TOUCHPAD";                               // ShowMap
    case 13: return Left;                                     // FightMode
    case 14: return Down;                                     // NormalMode
    case 15: return Right;                                    // GunMode
    case 16: return Triangle;                                 // JumpHighAttack
    case 17: return Cross;                                    // KickStealthKill
    case 18: return Square;                                   // PunchStealthStun
    case 19: return L1;                                       // Block
    case 20: return Circle;                                   // GrabPickupThrowFrisk
    case 21: return "\xA3+\x82";                              // Frisk: L3 + Circle (Xbox manual: L-stick button + B)
    case 22: return "\xA3+\x82";                              // Arrest: L3 + Circle
    case 23: return "\xA3+\x83";                              // FlashBadge: L3 + Square (L-stick button + X)
    case 24: return Cross;                                    // Cover
    case 25: return R1;                                       // Reload
    case 26: return cfg.triggersDrive ? R2 : Cross;           // CarAccel
    case 27: return cfg.triggersDrive ? L2 : Square;          // CarBrake
    case 28: return Circle;                                   // CarHandbrake
    case 29: return Down;                                     // CarHorn
    case 30: return Up;                                       // CarSiren
    case 31: return Triangle;                                 // CarRearView
    case 32: return "\x8E\x8F";                               // CarCamera
    case 33: return "\xA4 click";                             // SkipTrack
    default: return nullptr;
    }
}

static const char* __cdecl PromptName(int dik, int actionIndex) {
    if (cfg.buttonPrompts) {
        if (const char* s = PadLabelForBinding(actionIndex + 8)) return s;
    }
    return ((const char* (__cdecl*)(int))0x61E5C0)(dik);
}

static bool PatchCall(DWORD site, BYTE* expectedTarget4, void* newTarget, const char* what) {
    BYTE* p = (BYTE*)site;
    if (p[0] != 0xE8 || memcmp(p + 1, expectedTarget4, 4) != 0) { Log("%s: call site bytes differ, not patching", what); return false; }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(p + 1) = (int)((BYTE*)newTarget - (p + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("%s: patched", what);
    return true;
}

// Diagnostic: the UI text element at 0x5596FE draws with font [[edi+0x78]+0x28]; log which font (by cell height) renders
// prompt text, so icon glyphs can be confirmed to exist in that font.
static int g_loggedFonts = 0;
static void __cdecl LogPromptFont(DWORD element, const char* src) {
    if (!cfg.debugLog || g_loggedFonts > 20 || !element || !src) return;
    bool hasIcon = false;
    for (const unsigned char* s = (const unsigned char*)src; *s; ++s) if (*s == '%' || (*s >= 0x80 && *s <= 0xA7)) { hasIcon = true; break; }
    if (!hasIcon) return;
    DWORD font = *(DWORD*)(element + 0x78);
    BYTE* fnt = font ? *(BYTE**)(font + 0x28) : nullptr;
    if (!fnt) return;
    Log("prompt text font: first=0x%02X count=%u cellH=%u text=\"%.60s\"", fnt[0x15], fnt[0x14], fnt[0x16], src);
    ++g_loggedFonts;
}
__declspec(naked) static void TextBuildWithFontLog() {
    __asm {
        push dword ptr [esp + 8]    // src (arg 2 of 0x557BC0)
        push edi                    // UI text element
        call LogPromptFont
        add esp, 8
        mov eax, 0x557BC0
        jmp eax
    }
}

// ---------------------------------------------------------------- hook
__declspec(naked) static void CallOriginalUpdate() {
    __asm {
        mov ecx, 0x72BAC0
        mov eax, 0x5E9350
        jmp eax
    }
}

static void __cdecl HookedInputUpdate() {
    CallOriginalUpdate();
    MergePad();
}

static bool InstallHook() {
    BYTE expected[10] = { 0xB9, 0xC0, 0xBA, 0x72, 0x00, 0xE9, 0xE6, 0xF1, 0xFF, 0xFF };
    BYTE* site = (BYTE*)addr::InputThunk;
    if (memcmp(site, expected, sizeof(expected)) != 0) {
        Log("hook site bytes differ - unsupported TrueCrime.exe, not patching");
        return false;
    }
    DWORD old;
    if (!VirtualProtect(site, 10, PAGE_EXECUTE_READWRITE, &old)) return false;
    site[0] = 0xE9;
    *(int*)(site + 1) = (int)((BYTE*)&HookedInputUpdate - (site + 5));
    memset(site + 5, 0x90, 5);
    VirtualProtect(site, 10, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 10);
    return true;
}

static void LoadConfig(HMODULE self) {
    char path[MAX_PATH]; GetModuleFileNameA(self, path, MAX_PATH);
    char* dot = strrchr(path, '.'); if (dot) strcpy_s(dot, path + MAX_PATH - dot, ".ini");
    auto get = [&](const char* k, int def) { return (int)GetPrivateProfileIntA("Controller", k, def, path); };
    cfg.stickDeadzone = get("StickDeadzone", cfg.stickDeadzone);
    cfg.triggerThreshold = get("TriggerThreshold", cfg.triggerThreshold);
    cfg.invertCameraX = get("InvertCameraX", cfg.invertCameraX);
    cfg.invertCameraY = get("InvertCameraY", cfg.invertCameraY);
    cfg.invertThrottle = get("InvertThrottle", cfg.invertThrottle);
    cfg.triggersDrive = get("TriggersDrive", cfg.triggersDrive);
    cfg.aimSpeed = get("AimSpeed", cfg.aimSpeed);
    cfg.buttonPrompts = get("ButtonPrompts", cfg.buttonPrompts);
    ExtrasLoadConfig(path);
    g_deviceFix = (int)GetPrivateProfileIntA("Controller", "GraphicsCrashFix", 1, path);
    g_pauseSave = (int)GetPrivateProfileIntA("Controller", "PauseMenuSave", 1, path);
    cfg.debugLog = get("DebugLog", cfg.debugLog);
    if (dot) { strcpy_s(dot, path + MAX_PATH - dot, ".log"); g_log = _fsopen(path, "w", _SH_DENYNO); }   // readable while the game runs
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        LoadConfig(mod);
        Log("TrueCrimeDualSense loaded");
        Log(InstallHook() ? "input hook installed at 0x5EA160" : "input hook NOT installed");
        // call 0x61E5C0 at 0x557C0F (rel32 from 0x557C14 = 0x000C69AC)
        BYTE nameCall[4] = { 0xAC, 0x69, 0x0C, 0x00 };
        PatchCall(0x557C0F, nameCall, &PromptName, "button prompt names (0x557C0F)");
        // call 0x557BC0 at 0x5596FE (rel32 from 0x559703 = 0xFFFFE4BD)
        BYTE textCall[4] = { 0xBD, 0xE4, 0xFF, 0xFF };
        if (cfg.debugLog) PatchCall(0x5596FE, textCall, &TextBuildWithFontLog, "prompt font diagnostic (0x5596FE)");
        if (xcfg.rumble) ExtrasInstallHooks();
        DeviceFixInstall();
        PauseSaveInstall();
    } else if (reason == DLL_PROCESS_DETACH) {
        ExtrasShutdown();
    }
    return TRUE;
}
