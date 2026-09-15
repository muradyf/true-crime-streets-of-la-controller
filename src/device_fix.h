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

static void __cdecl DeviceRetryPause() {
    if (g_deviceRetries == 1 || g_deviceRetries % 20 == 0)
        Log("CreateDevice failed (hr 0x%08lX), retry %d", g_deviceLastHr, g_deviceRetries);
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
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

static void DeviceFixInstall() {
    if (!g_deviceFix) { Log("graphics device crash fix disabled in ini"); return; }
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
