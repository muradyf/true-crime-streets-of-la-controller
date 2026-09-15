// Fix for the "cuts out after a mission" crash (TrueCrime.exe+0x21DC2D, access violation).
//
// 0x61DBC0 creates or resets the Direct3D 8 device (this = renderer, device pointer at this+0x560, esi = this+0x560):
//   0x61DC28  call IDirect3D8::CreateDevice          (vtable +0x3C)
//   0x61DC2B  mov eax,[esi] ; mov edx,[eax] ; push eax ; call [edx+0x10] ; mov eax,[esi] ; pop edi ; pop ebx ; pop esi ; ret
// The follow-up call dereferences the device without checking that CreateDevice succeeded, so a failed device
// re-creation (device lost, 0x6125F0) crashes. The widescreen fix patches 0x61DC12 inside this function, so the
// function is NOT replaced: only the 5 bytes at 0x61DC2B jump to a stub that checks for a null device, waits while
// pumping window messages, and re-runs the game's own creation path (including the widescreen hook) from 0x61DBE2.
// After ~10 s it returns no device, which the caller at 0x608D3B already handles.
#pragma once
#include <timeapi.h>

static int g_deviceFix = 1;
static int g_deviceRetries = 0;
static DWORD g_deviceLastHr = 0;

static bool GameWindowHasFocus() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return fg && pid == GetCurrentProcessId() && !IsIconic(fg);
}

static void PumpDeviceWaitMessages() {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { Log("quit requested while the graphics device was lost"); ExitProcess((UINT)m.wParam); }
        TranslateMessage(&m); DispatchMessageA(&m);
    }
}

// Alt-tab / minimise makes CreateDevice return D3DERR_DEVICELOST (0x88760868) until the game window is active again,
// so waiting is unbounded while the window is in the background; only failures while focused count toward giving up.
static void __cdecl DeviceRetryPause() {
    if (!GameWindowHasFocus()) {
        Log("graphics device lost (hr 0x%08lX): waiting for the game window to be active again", g_deviceLastHr);
        ULONGLONG start = GetTickCount64();
        while (!GameWindowHasFocus()) { PumpDeviceWaitMessages(); Sleep(50); }
        Log("game window active again after %llu ms, recreating the device", GetTickCount64() - start);
        g_deviceRetries = 1;
        Sleep(250);                        // let the window finish activating before CreateDevice
        return;
    }
    if (g_deviceRetries == 1 || g_deviceRetries % 20 == 0)
        Log("CreateDevice failed (hr 0x%08lX), retry %d", g_deviceLastHr, g_deviceRetries);
    PumpDeviceWaitMessages();
    Sleep(100);
}

static void __cdecl DeviceGiveUp() { Log("CreateDevice still failing after %d retries (hr 0x%08lX)", g_deviceRetries, g_deviceLastHr); }

static int g_logCreateState = 0;   // set from D3DTrace
static BOOL CALLBACK LogTopmostWindow(HWND h, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() || !IsWindowVisible(h) || !(GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;
    RECT r; GetWindowRect(h, &r);
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
    char cls[64] = ""; GetClassNameA(h, cls, sizeof(cls));
    Log("  topmost window: class %s pid %lu rect %ld,%ld-%ld,%ld", cls, pid, r.left, r.top, r.right, r.bottom);
    return TRUE;
}
static void BorderlessOnDeviceCreated();   // borderless.h
static void __cdecl DeviceRecovered() {
    if (g_deviceRetries) Log("graphics device created after %d retries", g_deviceRetries);
    BorderlessOnDeviceCreated();
    if (!g_logCreateState) return;
    void* dev = *(void**)0x72C014;   // not yet stored by the caller; read the renderer's slot instead
    DWORD renderer = *(DWORD*)0x72C024;
    if (renderer) dev = *(void**)(renderer + 0x560);
    UINT* pp = renderer ? (UINT*)(renderer + 0x528) : nullptr;
    HRESULT tcl = dev ? ((HRESULT(__stdcall*)(void*))(*(void***)dev)[3])(dev) : 0;
    if (pp) Log("CreateDevice ok: TCL 0x%08lX | pp %ux%u fmt %u count %u ms %u swap %u hwnd %p windowed %u autoDS %u dsfmt %u flags 0x%X refresh %u interval 0x%X | foreground is game %d",
        tcl, pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], (void*)pp[6], pp[7], pp[8], pp[9], pp[10], pp[11], pp[12], (int)GameWindowHasFocus());
    EnumWindows(&LogTopmostWindow, 0);
}

__declspec(naked) static void AfterCreateDevice() {
    __asm {
        mov g_deviceLastHr, eax          // CreateDevice HRESULT
        mov eax, [esi]
        test eax, eax
        jz no_device
        pushad
        call DeviceRecovered
        popad
        mov g_deviceRetries, 0
        mov edx, [eax]                   // original follow-up
        push eax
        call dword ptr [edx + 0x10]
        mov eax, [esi]
        pop edi
        pop ebx
        pop esi
        ret
    no_device:
        pop edi                          // stack back to the state at 0x61DBE2: [saved esi]
        pop ebx
        cmp g_deviceRetries, 100
        jae give_up
        inc g_deviceRetries
        call DeviceRetryPause            // preserves ebx, esi, edi
        lea ecx, [esi - 0x560]           // this
        mov eax, 0x61DBE2                // recompute flags and call CreateDevice again
        jmp eax
    give_up:
        call DeviceGiveUp
        mov g_deviceRetries, 0
        xor eax, eax
        pop esi
        ret
    }
}

// 0x6125F0 (release-and-recreate: call 0x608D30 = CreateDevice path, then restore resources) is called from the main
// loop (0x4E63D9), the resolution change (0x57155A) and WM_ACTIVATEAPP in the window procedure (0x6128C7).
// The wait above pumps messages, so the window procedure can re-enter it while a re-creation is already running;
// a nested call is skipped (the outer one creates the device and restores resources).
static int g_inRecreate = 0, g_nestedRecreates = 0;
static void __cdecl LogNestedRecreate() { Log("device re-creation requested while one is in progress: skipped (%d)", ++g_nestedRecreates); }
static void __cdecl LogRecreate(int after, DWORD caller) {
    Log("device re-create %s: device %p, loop state %d, window active %d, caller 0x%lX", after ? "done" : "start",
        *(void**)0x72C014, *(int*)0x6B99E0, (int)GameWindowHasFocus(), caller);
}

// Timing of each step of the release (0x6125C0) and re-create (0x6125F0) paths, with the display mode after each step,
// so a slow alt-tab can be attributed to CreateDevice / the display mode change or to the resource restore calls.
static LARGE_INTEGER g_qpcFreq = {};
static double g_stepBase = 0, g_stepLast = 0;
static int g_firstFrameAfterRecreate = 0;
static double QpcMs() {
    if (!g_qpcFreq.QuadPart) QueryPerformanceFrequency(&g_qpcFreq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / g_qpcFreq.QuadPart;
}
// Exclusive fullscreen CreateDevice (and re-create after alt-tab) spends 3 s in Windows' d3d8.dll: when DWM composition
// is on, it resets the named event DWM_DX_FULLSCREEN_TRANSITION_EVENT (opened at d3d8+0x242F4), starts the fullscreen
// transition, then waits WaitForSingleObject(event, 3000) (d3d8+0x25802). Stack samples put 95% of CreateDevice there.
// The d3d8 import of WaitForSingleObject is wrapped; only that call site (return address after the pattern) is
// affected. Measured on Windows 11 26100 (Intel Iris Xe display, 2560x1600@240): the event is never signaled, the
// wait always times out at 3000 ms, so every exclusive CreateDevice costs 3 s (5 s at startup). The wait itself is
// still needed for part of that time: with 0 ms, DXGI screen captures after startup and after alt-tab were black
// (5 of 5); with 250, 500, 1000 or 2000 ms they showed the menu (4 of 4), like Windows' 3000 ms. Default 500 ms:
// alt-tab back 4.5 s -> ~1.7-2 s to the first frame. FullscreenTransitionWait (ms, read on every call; 3000 =
// Windows' behaviour) caps the wait; the result and duration are logged.
static char g_dwmIniPath[MAX_PATH] = "";
static DWORD (WINAPI* g_origD3d8Wait)(HANDLE, DWORD) = nullptr;
static DWORD g_d3d8WaitReturn = 0;
static bool g_d3d8WaitTried = false;
static DWORD WINAPI D3d8TransitionWait(HANDLE h, DWORD ms) {
    if (ms != 3000 || (DWORD)_ReturnAddress() != g_d3d8WaitReturn) return g_origD3d8Wait(h, ms);
    int cap = (int)GetPrivateProfileIntA("Controller", "FullscreenTransitionWait", 500, g_dwmIniPath);
    if (cap < 0 || cap > 3000) cap = 3000;
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    DWORD r = g_origD3d8Wait(h, (DWORD)cap);
    QueryPerformanceCounter(&b);
    Log("d3d8 DWM fullscreen transition wait: %s after %.0f ms (limit %d ms)",
        r == WAIT_OBJECT_0 ? "signaled" : r == WAIT_TIMEOUT ? "timed out" : "failed", (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart, cap);
    return r;
}
static void D3d8TransitionWaitInstall() {
    if (g_d3d8WaitTried) return;
    g_d3d8WaitTried = true;
    HMODULE m = GetModuleHandleA("d3d8.dll");
    if (!m) { Log("d3d8 transition wait: d3d8.dll not loaded"); return; }
    BYTE* base = (BYTE*)m;
    DWORD size = ((IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew))->OptionalHeader.SizeOfImage;
    const BYTE pat[] = { 0x68, 0xB8, 0x0B, 0x00, 0x00, 0xFF, 0xB3, 0xAC, 0x01, 0x00, 0x00, 0xFF, 0x15 };   // push 3000; push [ebx+1ACh]; call [imp]
    for (DWORD i = 0x1000; i + sizeof(pat) + 4 < size; ++i) {
        MEMORY_BASIC_INFORMATION mbi;
        if ((i & 0xFFF) == 0 && (!VirtualQuery(base + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))) { i += 0xFFF; continue; }
        if (memcmp(base + i, pat, sizeof(pat))) continue;
        void** slot = *(void***)(base + i + sizeof(pat));
        g_d3d8WaitReturn = (DWORD)(base + i + sizeof(pat) + 4);
        DWORD old;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
        g_origD3d8Wait = (DWORD(WINAPI*)(HANDLE, DWORD))*slot;
        *slot = (void*)&D3d8TransitionWait;
        VirtualProtect(slot, sizeof(void*), old, &old);
        Log("d3d8 transition wait hook installed (d3d8+0x%lX)", i);
        return;
    }
    Log("d3d8 transition wait pattern not found, hook not installed");
}

// Restore after a re-create (0x610070) reloads each device resource from disk: 0x607150 opens an async stream slot
// (0x60E230), queues the read (0x60DF30) and waits in 0x60E0A0, which polls the slot's pending count with Sleep(5).
// Each poll sleeps at least one scheduler tick, so ~60 resources cost ~0.7 s while the reads themselves are cached.
// RestorePollFix=1 replaces that Sleep(5) (0x60E0CE, also used while loading levels) with a yield for the first 2 ms
// of a wait and Sleep(1) after that. The pending count is still polled the same way, so behaviour is unchanged.
static int g_restorePollFix = 1;
static char g_restorePollIni[MAX_PATH] = "";
static volatile LONG g_pollCalls = 0;
static LARGE_INTEGER g_pollFreq = {}, g_pollLast = {}, g_pollWaitStart = {};
static volatile LONG g_pollTotalCalls = 0;
static LONGLONG g_pollTotalQpc = 0;                           // time spent inside the poll (game thread only)
static void __cdecl RestorePoll() {
    InterlockedIncrement(&g_pollCalls);
    InterlockedIncrement(&g_pollTotalCalls);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!g_pollFreq.QuadPart) QueryPerformanceFrequency(&g_pollFreq);
    // Sleep(1) with 1 ms timer resolution, not a yield loop: the poll takes the same critical section (0x60DFE0) that
    // the reading thread needs to update the slot, so spinning here starved the reader and the restore phase stayed at
    // ~690 ms however fast this side polled.
    if (!g_restorePollFix) Sleep(5);
    else Sleep(1);
    QueryPerformanceCounter(&g_pollLast);
    g_pollTotalQpc += g_pollLast.QuadPart - now.QuadPart;
}
// Level loads use the same poll: report the polls and the time spent in them each time gameplay controls start
// ([0x70CE48] from >= 2 to < 2), and re-read RestorePollFix once a second so a load can be A/B tested in one run.
static ULONGLONG g_pollNextIni = 0;
static int g_pollLastCtrl = -1;
static void RestorePollUpdate() {
    if (GetTickCount64() >= g_pollNextIni) {
        g_pollNextIni = GetTickCount64() + 1000;
        g_restorePollFix = (int)GetPrivateProfileIntA("Controller", "RestorePollFix", 1, g_dwmIniPath);
    }
    int ctrl = *(int*)0x70CE48;
    if (g_pollLastCtrl >= 2 && ctrl < 2 && g_pollFreq.QuadPart)
        Log("gameplay started: %ld polls since the last report, %.0f ms inside them (RestorePollFix=%d)",
            (long)InterlockedExchange(&g_pollTotalCalls, 0), g_pollTotalQpc * 1000.0 / g_pollFreq.QuadPart, g_restorePollFix),
        g_pollTotalQpc = 0;
    g_pollLastCtrl = ctrl;
}
// The other half of the wait: the file-reading thread (0x60DCF0) takes a request from the queue, reads it, and when
// the queue is empty sleeps 10 ms (0x60DE4C) before looking again. So each restored resource costs up to a 10 ms
// sleep, whatever the reader does. Measured under dgVoodoo: the restore phase took ~690 ms either way until this
// site was patched too. While requests keep coming the thread now yields instead, and it backs off to 1 ms and then
// 10 ms sleeps once it has been idle for a while, so an idle game does not spin.
static LARGE_INTEGER g_workIdleStreak = {}, g_workIdleLast = {};
static void __cdecl WorkerIdle() {
    if (!g_restorePollFix) { Sleep(10); return; }
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!g_pollFreq.QuadPart) QueryPerformanceFrequency(&g_pollFreq);
    LONGLONG ms = g_pollFreq.QuadPart / 1000;
    if (!g_workIdleStreak.QuadPart || now.QuadPart - g_workIdleLast.QuadPart > 20 * ms) g_workIdleStreak = now;   // work happened in between
    g_workIdleLast = now;
    LONGLONG idle = now.QuadPart - g_workIdleStreak.QuadPart;
    if (idle < 300 * ms) { if (!SwitchToThread()) YieldProcessor(); }
    else if (idle < 2000 * ms) Sleep(1);
    else Sleep(10);
    QueryPerformanceCounter(&g_workIdleLast);
}

static void RestorePollInstall() {
    if (g_restorePollFix) timeBeginPeriod(1);       // otherwise Sleep(1) is a 15.6 ms scheduler tick
    {
        BYTE* w = (BYTE*)0x60DE4C;
        const BYTE orig[] = { 0x6A, 0x0A, 0xFF, 0x15, 0x54, 0x60, 0x67, 0x00, 0xE9 };   // push 10; call [Sleep]; jmp 0x60DD00
        if (memcmp(w, orig, sizeof(orig))) Log("file thread idle bytes differ, fix not installed");
        else {
            DWORD old;
            VirtualProtect(w, 8, PAGE_EXECUTE_READWRITE, &old);
            w[0] = 0xE8; *(int*)(w + 1) = (int)((BYTE*)&WorkerIdle - (w + 5));
            w[5] = w[6] = w[7] = 0x90;
            VirtualProtect(w, 8, old, &old);
            FlushInstructionCache(GetCurrentProcess(), w, 8);
            Log("file thread idle hook installed (0x60DE4C)");
        }
    }
    BYTE* p = (BYTE*)0x60E0CE;
    const BYTE orig[] = { 0x6A, 0x05, 0xFF, 0x15, 0x54, 0x60, 0x67, 0x00, 0xEB, 0xD8 };   // push 5; call [Sleep]; jmp 0x60E0B0
    if (memcmp(p, orig, sizeof(orig))) { Log("restore poll bytes differ, fix not installed"); return; }
    DWORD old;
    VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE8; *(int*)(p + 1) = (int)((BYTE*)&RestorePoll - (p + 5));
    p[5] = p[6] = p[7] = 0x90;
    VirtualProtect(p, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 8);
    Log("restore poll hook installed (0x60E0CE), RestorePollFix=%d", g_restorePollFix);
}

static void __cdecl DeviceStep(int id) {
    if (id == 10) D3d8TransitionWaitInstall();
    if (id == 0) {
        g_restorePollFix = (int)GetPrivateProfileIntA("Controller", "RestorePollFix", 1, g_dwmIniPath);   // re-read for A/B tests
        g_pollCalls = 0;
    }
    if (id == 6) Log("restore polls during re-create: %ld (RestorePollFix=%d)", (long)g_pollCalls, g_restorePollFix);
    static const char* const names[] = {
        /*0*/ "re-create start", "0x608D30 create + render states", "0x5EC120 restore", "0x60FEF0", "0x4E5740 restore",
        /*5*/ "0x610070 restore", "0x60FF20", "", "", "",
        /*10*/ "0x61DBC0 CreateDevice start", "0x61DBC0 CreateDevice done", "0x5ED5A0 start", "0x5ED5A0 done", "", "", "", "", "", "",
        /*20*/ "release start", "release done" };
    double now = QpcMs();
    if (id == 0 || id == 20 || (id == 10 && !g_inRecreate)) g_stepBase = g_stepLast = now;
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm);
    char extra[160] = "";
    if (id == 11) {
        DWORD renderer = *(DWORD*)0x72C024;
        UINT* pp = renderer ? (UINT*)(renderer + 0x528) : nullptr;
        if (pp) sprintf_s(extra, " | pp %ux%u fmt %u windowed %u refresh %u interval 0x%X swap %u, hr 0x%08lX",
            pp[0], pp[1], pp[2], pp[7], pp[11], pp[12], pp[5], g_deviceLastHr);
    }
    Log("device step %-32s +%6.0f ms (total %6.0f ms) display %lux%lu@%lu%s", names[id], now - g_stepLast, now - g_stepBase,
        dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, extra);
    g_stepLast = now;
    if (id == 6 || id == 1) g_firstFrameAfterRecreate = 1;
    if (id == 10) DeviceProfilePhase(1);
    else if (id == 11 || id == 6) DeviceProfilePhase(0);
    else if (id == 1) DeviceProfilePhase(2);
}
// Any stall the player actually feels shows up as a gap between input updates, whether or not the device was rebuilt.
static double g_lastFrameMs = 0;
static void FrameGapCheck() {
    double now = QpcMs();
    if (g_lastFrameMs && now - g_lastFrameMs > 250) Log("frame gap %.0f ms", now - g_lastFrameMs);
    g_lastFrameMs = now;
}
static void DeviceFirstFrameCheck() {                        // called from the per-frame input update
    if (!g_firstFrameAfterRecreate || g_inRecreate) return;
    g_firstFrameAfterRecreate = 0;
    Log("device step first frame after re-create      total %6.0f ms", QpcMs() - g_stepBase);
}

#define DEVICE_STEP(n) __asm { pushad } __asm { pushfd } __asm { push n } __asm { call DeviceStep } __asm { add esp, 4 } __asm { popfd } __asm { popad }
static DWORD fn_608D30 = 0x608D30, fn_5EC120 = 0x5EC120, fn_60FEF0 = 0x60FEF0, fn_4E5740 = 0x4E5740, fn_610070 = 0x610070,
             fn_60FF20 = 0x60FF20, fn_61DBC0 = 0x61DBC0, fn_5ED5A0 = 0x5ED5A0, fn_6125C0 = 0x6125C0, g_ret5ED5A0 = 0;

__declspec(naked) static void RecreateGuard() {             // replaces 0x6125F0 (same calls, with step timing)
    __asm {
        cmp g_inRecreate, 0
        jne nested
        mov g_inRecreate, 1
    }
    DEVICE_STEP(0)
    __asm {
        pushad
        push dword ptr [esp + 32]                // caller
        push 0
        call LogRecreate
        add esp, 8
        popad
        call fn_608D30
    }
    DEVICE_STEP(1)
    __asm {
        mov eax, dword ptr ds:[0x72C014]
        test eax, eax
        jz done
        mov ecx, 0x72C010
        call fn_5EC120
    }
    DEVICE_STEP(2)
    __asm { call fn_60FEF0 }
    DEVICE_STEP(3)
    __asm { call fn_4E5740 }
    DEVICE_STEP(4)
    __asm { call fn_610070 }
    DEVICE_STEP(5)
    __asm { call fn_60FF20 }
    DEVICE_STEP(6)
    __asm {
    done:
        pushad
        push 0
        push 1
        call LogRecreate
        add esp, 8
        popad
        mov g_inRecreate, 0
        ret
    nested:
        pushad
        call LogNestedRecreate
        popad
        ret
    }
}

__declspec(naked) static void TimedCreateDevice() {         // call 0x61DBC0 at 0x608D36
    DEVICE_STEP(10)
    __asm { call fn_61DBC0 }
    DEVICE_STEP(11)
    __asm { ret }
}

__declspec(naked) static void TimedRestore5ED5A0() {         // call 0x5ED5A0 (thiscall, ret 4) at 0x608DDE
    DEVICE_STEP(12)
    __asm {
        pop g_ret5ED5A0                          // argument is now at [esp], as for a direct call
        call fn_5ED5A0
    }
    DEVICE_STEP(13)
    __asm {
        push g_ret5ED5A0
        ret
    }
}

// Alt-tab rebuild. The main loop (0x4E6380) releases every device resource when the window is deactivated (state 0:
// drain streams 0x60E290, release 0x6125C0) and, when it is activated again (state 2), drains, re-creates the device
// and reloads every resource from disk (0x6125F0). WM_ACTIVATEAPP (0x61288D / 0x6128C7) has the same pair. A device
// that is still usable after the window was in the background does not need any of that.
// KeepDeviceOnAltTab=1 (default): deactivate keeps the device and resources; on activate TestCooperativeLevel decides:
// D3D_OK skips the rebuild, anything else (a lost exclusive-fullscreen device) runs the original release and re-create
// then, which is the same work in the same order, only later.
static int g_keepDevice = 1;
static bool g_releaseSkipped = false, g_keepOk = false;
static HRESULT DeviceTcl() {
    void* dev = *(void**)0x72C014;
    if (!dev) return E_FAIL;
    return ((HRESULT(__stdcall*)(void*))(*(void***)dev)[3])(dev);
}
static void ReleaseTimed() { DeviceStep(20); ((void(__cdecl*)())0x6125C0)(); DeviceStep(21); }
static void __cdecl KeepDeactivateDrain() {                  // call 0x60E290 at 0x4E63B9
    if (g_keepDevice && *(void**)0x72C014) return;
    ((void(__cdecl*)())0x60E290)();
}
// Only a windowed device survives the window going to the background; an exclusive-fullscreen one is always lost, so
// keeping it buys nothing and deferring the release to the activate side crashed native d3d8. Exclusive devices
// therefore take the unmodified path.
static bool DeviceIsWindowed() {
    DWORD renderer = *(DWORD*)0x72C024;
    UINT* pp = renderer ? (UINT*)(renderer + 0x528) : nullptr;
    return pp && pp[7] == 1;
}
static void __cdecl KeepDeactivateRelease() {                // call 0x6125C0 at 0x4E63BE and 0x61288D
    if (g_keepDevice && *(void**)0x72C014 && DeviceIsWindowed()) {
        if (!g_releaseSkipped) Log("alt-tab out: device and resources kept (TestCooperativeLevel 0x%08lX)", DeviceTcl());
        g_releaseSkipped = true;
        return;
    }
    ReleaseTimed();
}
static void KeepDecide() {
    g_keepOk = false;
    if (!g_releaseSkipped) return;
    HRESULT tcl = DeviceTcl();
    g_keepOk = tcl == 0;
    Log("alt-tab back: TestCooperativeLevel 0x%08lX -> %s", tcl, g_keepOk ? "device kept, no rebuild" : "device lost, release and re-create");
}
static void __cdecl KeepActivateDrain() {                    // call 0x60E290 at 0x4E63D4
    KeepDecide();
    if (!g_keepOk) ((void(__cdecl*)())0x60E290)();
}
// When the device is only D3DERR_DEVICENOTRESET it can be Reset, and 0x61DBC0 already does that when the device
// pointer is still there. Because the resources were never released, their live flag (bit 1 at +4) is still set, so
// the restore pass (0x610070) skips them all and nothing is reloaded from disk - which is the 4.2 s in gameplay.
// If the Reset does not take (for example a resource in the default pool blocks it), the original release and
// re-create run as before.
static void __cdecl KeepActivateRecreate() {                 // call 0x6125F0 at 0x4E63D9
    if (g_releaseSkipped) {
        g_releaseSkipped = false;
        if (g_keepOk) return;
        // No reset attempt: 0x61DBC0 would Reset the kept device, and that never succeeded (the game's vertex and
        // index buffers are DYNAMIC|WRITEONLY in POOL_DEFAULT, which blocks a reset). Worse, on native d3d8 keeping
        // the device and then falling back to release + CreateDevice crashed the process inside d3d8.dll with an
        // access violation (2026-09-16, Windows 11 26100, d3d8.dll 10.0.26100.9278). So when the device did not
        // survive, release and re-create exactly like the unmodified game.
        ReleaseTimed();
    }
    ((void(__cdecl*)())0x6125F0)();
}
static void __cdecl KeepWndRecreate() {                      // call 0x6125F0 at 0x6128C7 (WM_ACTIVATEAPP activate)
    KeepDecide();
    KeepActivateRecreate();
}

static void RedirectCall(DWORD site, DWORD expectedTarget, void* fn, const char* what) {
    BYTE* s = (BYTE*)site;
    if (s[0] != 0xE8 || (DWORD)(site + 5 + *(int*)(s + 1)) != expectedTarget) { Log("%s: call bytes differ, timing not installed", what); return; }
    DWORD old;
    VirtualProtect(s, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(s + 1) = (int)((BYTE*)fn - (s + 5));
    VirtualProtect(s, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), s, 5);
}

// Crash at TrueCrime.exe+0x14EFAD (also without mods) at resolutions other than the desktop's: 0x54E850 fills a
// locked vertex buffer (0x608710 create, 0x6082B0 lock) with movaps, which requires 16-byte alignment. The lock
// pointer is only 8-byte aligned in some allocations (e.g. 0x04208068), so the write faults (reported as a read of
// 0xFFFFFFFF). The four writes become movups (0F 29 -> 0F 11); same operands, no alignment requirement.
static void VertexAlignFixInstall() {
    const DWORD sites[] = { 0x54EFAD, 0x54EFB5, 0x54EFBD, 0x54EFD1 };
    const BYTE expected[][3] = { { 0x0F, 0x29, 0x02 }, { 0x0F, 0x29, 0x0A }, { 0x0F, 0x29, 0x12 }, { 0x0F, 0x29, 0x12 } };
    for (int i = 0; i < 4; ++i)
        if (memcmp((BYTE*)sites[i], expected[i], 3)) { Log("vertex write bytes differ at 0x%lX, alignment fix not installed", sites[i]); return; }
    for (DWORD site : sites) {
        DWORD old;
        VirtualProtect((LPVOID)(site + 1), 1, PAGE_EXECUTE_READWRITE, &old);
        *(BYTE*)(site + 1) = 0x11;
        VirtualProtect((LPVOID)(site + 1), 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (LPVOID)site, 3);
    }
    Log("vertex buffer alignment crash fix installed (0x54EFAD..0x54EFD1)");
}

static void DeviceFixInstall() {
    if (!g_deviceFix) { Log("graphics device crash fix disabled in ini"); return; }
    VertexAlignFixInstall();
    RestorePollInstall();
    {
        BYTE* g = (BYTE*)0x6125F0;
        const BYTE callCreate[] = { 0xE8, 0x3B, 0x67, 0xFF, 0xFF };                // call 0x608D30
        if (memcmp(g, callCreate, sizeof(callCreate)) == 0) {
            DWORD o;
            VirtualProtect(g, 5, PAGE_EXECUTE_READWRITE, &o);
            g[0] = 0xE9;
            *(int*)(g + 1) = (int)((BYTE*)&RecreateGuard - (g + 5));
            VirtualProtect(g, 5, o, &o);
            FlushInstructionCache(GetCurrentProcess(), g, 5);
        } else Log("device re-create entry bytes differ, re-entry guard not installed");
        RedirectCall(0x608D36, 0x61DBC0, &TimedCreateDevice, "CreateDevice timing (0x608D36)");
        RedirectCall(0x608DDE, 0x5ED5A0, &TimedRestore5ED5A0, "restore timing (0x608DDE)");
        // Default off: on the native d3d8 this crashes (0xC0000005 inside d3d8.dll right after CreateDevice) when the
        // kept device is not resettable and the code falls back to release + re-create. Only dgVoodoo survived that path.
        g_keepDevice = (int)GetPrivateProfileIntA("Controller", "KeepDeviceOnAltTab", 0, g_dwmIniPath);
        RedirectCall(0x61288D, 0x6125C0, &KeepDeactivateRelease, "alt-tab release (0x61288D)");
        RedirectCall(0x6128C7, 0x6125F0, &KeepWndRecreate, "alt-tab re-create (0x6128C7)");
        RedirectCall(0x4E63B9, 0x60E290, &KeepDeactivateDrain, "alt-tab drain (0x4E63B9)");
        RedirectCall(0x4E63BE, 0x6125C0, &KeepDeactivateRelease, "alt-tab release (0x4E63BE)");
        RedirectCall(0x4E63D4, 0x60E290, &KeepActivateDrain, "alt-tab drain (0x4E63D4)");
        RedirectCall(0x4E63D9, 0x6125F0, &KeepActivateRecreate, "alt-tab re-create (0x4E63D9)");
        Log("alt-tab handling installed, KeepDeviceOnAltTab=%d", g_keepDevice);
    }
    BYTE* site = (BYTE*)0x61DC2B;
    const BYTE expected[] = { 0x8B, 0x06, 0x8B, 0x10, 0x50, 0xFF, 0x52, 0x10 };   // mov eax,[esi]; mov edx,[eax]; push eax; call [edx+10]
    const BYTE entry[] = { 0xF6, 0x81, 0x64, 0x05, 0x00, 0x00, 0x10 };             // 0x61DBE2: test byte [ecx+0x564],10h
    if (memcmp(site, expected, sizeof(expected)) != 0 || memcmp((BYTE*)0x61DBE2, entry, sizeof(entry)) != 0) {
        Log("device create site bytes differ, crash fix not installed");
        return;
    }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE9;
    *(int*)(site + 1) = (int)((BYTE*)&AfterCreateDevice - (site + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("graphics device crash fix installed at 0x61DC2B");
}
