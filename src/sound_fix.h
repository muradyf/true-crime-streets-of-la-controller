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

// ---- SoundTrace=1 (debug): log category level changes (0x5F2030) and per-voice volume sets (0x5F2800) ----
// 0x5F2030(mask, level): mask 0 -> master [0x72F0FC]; otherwise level[bit] in [0x72F078 + bit*4].
// 0x5F2800(handle, volume, pan): voice index = handle & 0xFF, voice category mask [0x72F908 + i*4];
// final = clamp(volume) * max(level[bits of mask]) (or master when mask == 0).
static int g_soundTrace = 0;
static float g_lastVoiceVol[256], g_lastVoiceLevel[256];
static DWORD g_lastVoiceMask[256];

static float CategoryLevel(DWORD mask) {
    mask &= 0xFFFF;
    if (!mask) return *(float*)0x72F0FC;
    float best = 0.0f;
    for (int i = 0; i < 16; ++i) if (mask & (1u << i)) { float v = *(float*)(0x72F078 + i * 4); if (v > best) best = v; }
    return best;
}

static void __cdecl TraceSetLevel(DWORD caller, int mask, float level) {
    Log("sound level: mask 0x%02X = %.3f (caller 0x%06lX)", mask, level, caller - 5);
}

static void __cdecl TraceVoiceVolume(DWORD caller, DWORD handle, float volume) {
    int i = handle & 0xFF;
    DWORD mask = *(DWORD*)(0x72F908 + i * 4);
    float level = CategoryLevel(mask);
    if (volume == g_lastVoiceVol[i] && level == g_lastVoiceLevel[i] && mask == g_lastVoiceMask[i]) return;
    g_lastVoiceVol[i] = volume; g_lastVoiceLevel[i] = level; g_lastVoiceMask[i] = mask;
    float v = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
    Log("sound voice %3d: volume %.3f x level %.3f (mask 0x%04lX) = %.3f, menu %d (caller 0x%06lX)",
        i, volume, level, mask, v * level, *(int*)0x70CE48, caller - 5);
}

// ---- which sound is playing
// 0x5F2C10(voiceHandle, bank, index) starts a sound: it checks the handle against -1, the bank against the count at
// [0x730708], then takes the 0x1C-byte descriptor at [bank*16 + 0x730508] + index*0x1C. Logging the three tells us
// which id a voice is carrying, which is what the category trace cannot say on its own - it only sees handles.
static void __cdecl TraceSoundPlay(DWORD handle, int bank, int index) {
    if (!g_soundTrace) return;
    int i = handle & 0xFF;
    DWORD mask = *(DWORD*)(0x72F908 + i * 4);
    DWORD table = *(DWORD*)(bank * 16 + 0x730508);
    DWORD rec = table ? table + index * 0x1C : 0;
    char desc[128] = "";
    if (rec && !IsBadReadPtr((void*)rec, 0x1C)) {
        sprintf_s(desc, "rec %08lX %08lX %08lX %08lX", *(DWORD*)rec, *(DWORD*)(rec + 4),
                  *(DWORD*)(rec + 8), *(DWORD*)(rec + 0xC));
        DWORD n = *(DWORD*)rec;                              // if the first field is a name, show it
        if (n > 0x400000 && !IsBadReadPtr((void*)n, 4)) {
            char nm[80] = ""; strncpy_s(nm, (const char*)n, 71);
            bool printable = nm[0] >= 32 && nm[0] < 127;
            if (printable) sprintf_s(desc, "\"%s\"", nm);
        }
    }
    Log("sound play: voice %3d bank %d id %d, mask 0x%04lX, level %.3f, menu %d, %s",
        i, bank, index, mask, CategoryLevel(mask), *(int*)0x70CE48, desc);
}

__declspec(naked) static void SetLevelTraceStub() {         // replaces 0x5F2030: mov edx,[esp+4]; test edx,edx
    __asm {
        pushad
        push dword ptr [esp + 0x28]                         // level (float bits)
        push dword ptr [esp + 0x28]                         // mask
        push dword ptr [esp + 0x28]                         // return address
        call TraceSetLevel
        add esp, 12
        popad
        mov edx, dword ptr [esp + 4]
        test edx, edx
        push 0x5F2036
        ret
    }
}

__declspec(naked) static void VoiceVolumeTraceStub() {      // replaces 0x5F2800: push ecx; mov al,[0x6B9B91]
    __asm {
        pushad
        push dword ptr [esp + 0x28]                         // volume
        push dword ptr [esp + 0x28]                         // handle
        push dword ptr [esp + 0x28]                         // return address
        call TraceVoiceVolume
        add esp, 12
        popad
        push ecx
        mov al, byte ptr ds:[0x6B9B91]
        push 0x5F2806
        ret
    }
}

// Lowest audio layer: 0x661840(packed sound id, flags) starts a sound; 0x6619A0(handle, property, value) sets a
// buffer property (property 1 = volume in 1/100 dB, clamped at -10000).
static void __cdecl TracePlay(DWORD caller, DWORD id, DWORD flags) {
    Log("sound play: id 0x%08lX flags 0x%lX, menu %d (caller 0x%06lX)", id, flags, *(int*)0x70CE48, caller - 5);
}
static void __cdecl TraceProperty(DWORD caller, DWORD handle, int prop, int value) {
    Log("sound prop: handle 0x%08lX prop %d = %d (caller 0x%06lX)", handle, prop, value, caller - 5);
}

__declspec(naked) static void PlayTraceStub() {             // replaces 0x661840: push ebx; mov ebx,[esp+8]; push ebp
    __asm {
        pushad
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        push dword ptr [esp + 0x28]
        call TracePlay
        add esp, 12
        popad
        push ebx
        mov ebx, dword ptr [esp + 8]
        push ebp
        push 0x661846
        ret
    }
}

__declspec(naked) static void PropertyTraceStub() {         // replaces 0x6619A0: mov ecx,[esp+4]; push ebx; mov ebx,ecx
    __asm {
        pushad
        push dword ptr [esp + 0x2C]                         // value
        push dword ptr [esp + 0x2C]                         // property
        push dword ptr [esp + 0x2C]                         // handle
        push dword ptr [esp + 0x2C]                         // return address
        call TraceProperty
        add esp, 16
        popad
        mov ecx, dword ptr [esp + 4]
        push ebx
        mov ebx, ecx
        push 0x6619A7
        ret
    }
}

// ---- Start-of-sound volume fix ----
// 0x5F2C10(handle, bank, index) starts a voice: 0x661840 Stops, rewinds and Plays the DirectSound buffer
// (IDirectSoundBuffer::Play at 0x66191C), and only then 0x5F2800 applies the category volume through 0x6619A0.
// A buffer that has never had its volume set (first use of each sound, or a new duplicate) therefore starts at
// 0 dB (full volume) - traced: first menu move/enter/back plays had "volume before Play 0", later ones -1863.
// The voice's category mask is set at allocation (0x5F2330 -> [0x72F908 + i*4]), so the final volume is known
// before Play: 0x5F2800 with volume 1.0 = 2000*log10(clamp(level, 0.0001, 1)), truncated, clamped to -10000..0.
static DWORD g_pendingVoiceHandle = 0xFFFFFFFF;
static int g_startVolumeFixes = 0;

static void __stdcall BeforeBufferPlay(void* buf) {
    LONG before = 1;
    if (g_soundTrace) ((HRESULT(__stdcall*)(void*, LONG*))(*(void***)buf)[6])(buf, &before);   // GetVolume
    if (!g_soundFix || g_pendingVoiceHandle == 0xFFFFFFFF) {
        if (g_soundTrace) Log("dsound Play: buffer %p volume before Play %ld (no pending voice)", buf, before);
        return;
    }
    int i = g_pendingVoiceHandle & 0xFF;
    float level = CategoryLevel(*(DWORD*)(0x72F908 + i * 4));
    if (level < 0.0001f) level = 0.0001f;
    if (level > 1.0f) level = 1.0f;
    float db = (float)(2000.0 * log10((double)level));
    LONG vol = (LONG)db;
    if (vol < -10000) vol = -10000;
    if (vol > 0) vol = 0;
    ((HRESULT(__stdcall*)(void*, LONG))(*(void***)buf)[15])(buf, vol);                        // SetVolume
    if (before != vol) ++g_startVolumeFixes;
    if (g_soundTrace) Log("dsound Play: buffer %p volume before Play %ld -> set %ld (voice %d)", buf, before, vol, i);
}

__declspec(naked) static void BeforePlayStub() {            // replaces 0x661916: push edx; push 0; push 0; push eax; call [ecx+30h]
    __asm {
        pushad
        push eax
        call BeforeBufferPlay
        popad
        push edx
        push 0
        push 0
        push eax
        call dword ptr [ecx + 0x30]
        push 0x66191F
        ret
    }
}

// The id trace goes through here rather than taking 0x5F2C10 for itself. It did once, and because the trace is
// installed first, the start volume fix then found bytes it did not recognise and skipped - which put the loud
// menu transition back. One address, one stub.
__declspec(naked) static void StartVoiceStub() {            // replaces 0x5F2C10: mov al,[0x6B9B91]; test al,al
    __asm {
        mov eax, dword ptr [esp + 4]
        mov g_pendingVoiceHandle, eax
        pushad
        push dword ptr [esp + 0x2C]                         // index
        push dword ptr [esp + 0x2C]                         // bank
        push dword ptr [esp + 0x2C]                         // voice handle
        call TraceSoundPlay
        add esp, 12
        popad
        mov al, byte ptr ds:[0x6B9B91]
        test al, al
        push 0x5F2C17
        ret
    }
}

static void SoundStartVolumeInstall() {
    const BYTE playCall[] = { 0x52, 0x6A, 0x00, 0x6A, 0x00, 0x50, 0xFF, 0x51, 0x30 };
    const BYTE startVoice[] = { 0xA0, 0x91, 0x9B, 0x6B, 0x00, 0x84, 0xC0 };
    if (memcmp((BYTE*)0x661916, playCall, sizeof(playCall)) || memcmp((BYTE*)0x5F2C10, startVoice, sizeof(startVoice))) {
        Log("sound start site bytes differ, start volume fix not installed");
        return;
    }
    WriteJmp(0x5F2C10, &StartVoiceStub, sizeof(startVoice));
    WriteJmp(0x661916, &BeforePlayStub, sizeof(playCall));
    Log("sound start volume fix installed (0x5F2C10, 0x661916)");
}

static void SoundTraceInstall() {
    if (!g_soundTrace) return;
    const BYTE setOrig[] = { 0x8B, 0x54, 0x24, 0x04, 0x85, 0xD2 };
    const BYTE volOrig[] = { 0x51, 0xA0, 0x91, 0x9B, 0x6B, 0x00 };
    const BYTE playOrig[] = { 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55 };
    const BYTE propOrig[] = { 0x8B, 0x4C, 0x24, 0x04, 0x53, 0x8B, 0xD9 };
    if (memcmp((BYTE*)0x5F2030, setOrig, 6) || memcmp((BYTE*)0x5F2800, volOrig, 6) ||
        memcmp((BYTE*)0x661840, playOrig, 6) || memcmp((BYTE*)0x6619A0, propOrig, 7)) { Log("sound trace site bytes differ"); return; }
    WriteJmp(0x5F2030, &SetLevelTraceStub, 6);
    WriteJmp(0x5F2800, &VoiceVolumeTraceStub, 6);
    WriteJmp(0x661840, &PlayTraceStub, 6);
    WriteJmp(0x6619A0, &PropertyTraceStub, 7);
    Log("sound trace installed (0x5F2030, 0x5F2800, 0x661840, 0x6619A0)");
}

static void SoundFixInstall() {
    SoundTraceInstall();
    if (!g_soundFix) return;
    SoundStartVolumeInstall();
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
