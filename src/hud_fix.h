// In-game HUD scaling.
//
// The HUD master render 0x4DA780 (reached only through the cdecl thunk 0x4DD720, draws only when p2 == 4) positions
// elements with the anchor helpers (scale [0x6AEA00] + safe rect) or with GetScreenW/H (0x6089F0/0x608A00) minus
// 640x480-pixel offsets, but every size is a raw 640x480 pixel constant. So at high resolutions it stays tiny.
//
// Fix: run the HUD pass in a virtual space of height 480 (width W/s, scale 1.0, safe rect /s), then multiply the
// pre-transformed vertices by s when each 2D batch is flushed (0x609FD0; batch +0x10 = vertex buffer whose +0x28 is
// the locked start, batch +0x14 = write pointer, 0x24 bytes per vertex, x at +0, y at +4, writers subtract 0.5).
#pragma once

static int g_hudFix = 1;
static int g_inHud = 0;
static float g_hudScale = 1.0f;
static bool g_hudLogged = false;

static void __cdecl ScaleHudBatch(DWORD* batch) {
    DWORD* vb = (DWORD*)batch[0x10 / 4];
    if (!vb) return;
    float* p = (float*)vb[0x28 / 4];
    float* end = (float*)batch[0x14 / 4];
    if (!p || end <= p || (BYTE*)end - (BYTE*)p > 0x10000 * 0x24) return;
    float s = g_hudScale;
    for (; (BYTE*)p + 0x24 <= (BYTE*)end; p = (float*)((BYTE*)p + 0x24)) {
        p[0] = (p[0] + 0.5f) * s - 0.5f;
        p[1] = (p[1] + 0.5f) * s - 0.5f;
    }
}

__declspec(naked) static void HudFlushStub() {              // replaces 0x609FD0: push ebx; push ebp; push esi; push edi; mov edi,ecx
    __asm {
        cmp g_inHud, 0
        je original
        pushad
        push ecx
        call ScaleHudBatch
        add esp, 4
        popad
    original:
        push ebx
        push ebp
        push esi
        push edi
        mov edi, ecx
        mov eax, 0x609FD6
        jmp eax
    }
}

static void __cdecl HudRenderHook(void* self, int a, int b, int c, int d) {   // replaces cdecl thunk 0x4DD720
    auto render = (void(__fastcall*)(void*, void*, int, int, int, int))0x4DA780;   // thiscall, ret 0x10
    int w = ScreenW(), h = ScreenH();
    if (!g_hudFix || a != 4 || h <= 480) { render(self, nullptr, a, b, c, d); return; }

    float s = h / 480.0f;
    float scaleX = *(float*)0x6AEA00, scaleY = *(float*)0x6AEA04;
    int rect[4]; memcpy(rect, (void*)0x7280F0, sizeof(rect));
    int offX = g_uiOffX;

    *(float*)0x6AEA00 = 1.0f;
    *(float*)0x6AEA04 = 1.0f;
    for (int i = 0; i < 4; ++i) *(int*)(0x7280F0 + i * 4) = (int)(rect[i] / s + 0.5f);
    *(int*)0x6B9B98 = (int)(w / s + 0.5f);
    *(int*)0x6B9B9C = 480;
    g_uiOffX = (int)(offX / s + 0.5f);
    g_hudScale = s;

    if (!g_hudLogged) {
        g_hudLogged = true;
        Log("HUD pass: virtual %dx480, safe rect %d,%d-%d,%d, scale %.3f",
            *(int*)0x6B9B98, *(int*)0x7280F0, *(int*)0x7280F8, *(int*)0x7280F4, *(int*)0x7280FC, s);
    }

    g_inHud = 1;
    render(self, nullptr, a, b, c, d);
    g_inHud = 0;

    *(int*)0x6B9B98 = w;
    *(int*)0x6B9B9C = h;
    memcpy((void*)0x7280F0, rect, sizeof(rect));
    *(float*)0x6AEA00 = scaleX;
    *(float*)0x6AEA04 = scaleY;
    g_uiOffX = offX;
}

static void HudFixInstall() {
    if (!g_hudFix) return;
    const BYTE thunkOrig[] = { 0x8B, 0x44, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x10, 0x8B, 0x54, 0x24, 0x0C, 0x50, 0x8B, 0x44,
                               0x24, 0x0C, 0x51, 0x8B, 0x4C, 0x24, 0x0C, 0x52, 0x50, 0xE8, 0x43, 0xD0, 0xFF, 0xFF, 0xC3 };
    const BYTE flushOrig[] = { 0x53, 0x55, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x18 };
    if (memcmp((BYTE*)0x4DD720, thunkOrig, sizeof(thunkOrig)) || memcmp((BYTE*)0x609FD0, flushOrig, sizeof(flushOrig))) {
        Log("HUD site bytes differ, HUD fix not installed");
        g_hudFix = 0;
        return;
    }
    WriteJmp(0x4DD720, &HudRenderHook, 5);
    WriteJmp(0x609FD0, &HudFlushStub, 6);
    Log("HUD scale fix installed (0x4DD720, 0x609FD0)");
}
