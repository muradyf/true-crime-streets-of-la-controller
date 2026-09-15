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
static void __cdecl DeviceRecovered() { if (g_deviceRetries) Log("graphics device created after %d retries", g_deviceRetries); }

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
static void __cdecl LogRecreate(int after) {
    Log("device re-create %s: device %p, loop state %d, window active %d", after ? "done" : "start",
        *(void**)0x72C014, *(int*)0x6B99E0, (int)GameWindowHasFocus());
}

__declspec(naked) static void RecreateGuard() {             // replaces call 0x608D30 at 0x6125F0
    __asm {
        cmp g_inRecreate, 0
        jne nested
        mov g_inRecreate, 1
        pushad
        push 0
        call LogRecreate
        add esp, 4
        popad
        mov eax, 0x608D30
        call eax
        pushad
        push 1
        call LogRecreate
        add esp, 4
        popad
        mov g_inRecreate, 0
        mov eax, 0x6125F5
        jmp eax
    nested:
        pushad
        call LogNestedRecreate
        popad
        ret
    }
}

static void DeviceFixInstall() {
    if (!g_deviceFix) { Log("graphics device crash fix disabled in ini"); return; }
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
