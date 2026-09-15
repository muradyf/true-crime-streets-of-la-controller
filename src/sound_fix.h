// Fix: the first menu sound (Pause_On.wav, id 0x6F) ignores the volume settings.
//
// Game init 0x4E7A20: sound init 0x4E7140 hard-codes the menu/SFX category 0x20 to 0.65 at 0x4E71E8
// (call 0x5F2030(mask, level)); the first menu screen opens at 0x4E7F31 and plays Pause_On; only then 0x4E7F61
// applies the ini volumes through the setters 0x4AAFE0 (music), 0x4AB080 (SFX), 0x4AB140 (voice), 0x4AB1E0 (movie).
// The ini floats (0x752810, 0x752684, 0x752688, 0x7527A8) are loaded earlier (0x61FB50 from 0x6130B9).
// The call at 0x4E71E8 is redirected here: run it, then apply the loaded volumes immediately.
#pragma once

static int g_soundFix = 1;

static void __cdecl SetCategoryLevelThenApplyVolumes(int mask, float level) {
    ((void(__cdecl*)(int, float))0x5F2030)(mask, level);                 // original call
    ((void(__stdcall*)(float))0x4AAFE0)(*(float*)0x752810);             // music
    ((void(__stdcall*)(float))0x4AB080)(*(float*)0x752684);             // SFX (menu sounds, category 0x20)
    ((void(__stdcall*)(float))0x4AB140)(*(float*)0x752688);             // voice
    ((void(__stdcall*)(float))0x4AB1E0)(*(float*)0x7527A8);             // movie
    Log("sound: applied ini volumes at init (music %.2f, sfx %.2f, voice %.2f, movie %.2f)",
        *(float*)0x752810, *(float*)0x752684, *(float*)0x752688, *(float*)0x7527A8);
}

static void SoundFixInstall() {
    if (!g_soundFix) return;
    BYTE* p = (BYTE*)0x4E71E8;
    const BYTE orig[] = { 0xE8, 0x43, 0xAE, 0x10, 0x00 };               // call 0x5F2030
    if (memcmp(p, orig, sizeof(orig))) { Log("sound init call bytes differ, sound fix not installed"); return; }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(p + 1) = (int)((BYTE*)&SetCategoryLevelThenApplyVolumes - (p + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("menu sound volume fix installed (0x4E71E8)");
}
