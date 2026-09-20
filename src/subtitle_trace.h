// SubtitleTrace=1 (debug): cutscene subtitles are queued by 0x4AEB10(text, font) (call sites 0x4678A0, 0x484303,
// 0x484374) onto the element at [[0x6D9554] + 0xE8]. The glyphs ignore the UI scale, so this logs the element, its
// vtable and its first fields right after the call, to find where the glyph size comes from.
#pragma once

static int g_subtitleTrace = 0;
static int g_subtitleLogged = 0;

static void __cdecl LogSubtitleElement() {
    if (!g_subtitleTrace || g_subtitleLogged > 3) return;
    DWORD mgr = *(DWORD*)0x6D9554;
    if (!mgr) return;
    DWORD el = *(DWORD*)(mgr + 0xE8);
    if (!el) return;
    char line[700] = "";
    for (int i = 0; i < 26; ++i) {
        DWORD v = *(DWORD*)(el + i * 4);
        float f = *(float*)&v;
        // Every field is dumped as a float too, and most of them are not floats: a dword like 0x7A123456 reads as
        // 7.5e35, which "%.2f" spells out over 39 digits and overruns the buffer. sprintf_s answers that by
        // calling the invalid parameter handler, which fastfails the process (0xC0000409) - so bound the text
        // with %g and truncate rather than fail.
        char one[64];
        _snprintf_s(one, sizeof(one), _TRUNCATE, "+%02X=%08lX(%g) ", i * 4, v, f);
        if (strlen(line) + strlen(one) < sizeof(line) - 1) strcat_s(line, one);
    }
    ++g_subtitleLogged;
    // 0x490B23 passes [element+0x0C] to the text routine, not [0x6D9560], so print both: if they differ, the scale
    // subtitle_fix.h writes is going into a font object the subtitles never draw with.
    DWORD elFont = *(DWORD*)(el + 0x0C);
    Log("subtitle element %08lX vtable %08lX element font %08lX global font %08lX %s screen %dx%d: %s",
        el, *(DWORD*)el, elFont, *(DWORD*)0x6D9560,
        elFont == *(DWORD*)0x6D9560 ? "(same)" : "(DIFFERENT)", ScreenW(), ScreenH(), line);
}

__declspec(naked) static void SubtitleTraceStub() {         // replaces call 0x4AEB10 (thiscall, 2 args, ret 8)
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        mov eax, 0x4AEB10
        call eax
        pushad
        call LogSubtitleElement
        popad
        ret 8
    }
}

static void SubtitleTraceInstall() {
    if (!g_subtitleTrace) return;
    const DWORD sites[] = { 0x4678A0, 0x484303, 0x484374 };
    int n = 0;
    for (DWORD site : sites) {
        BYTE* p = (BYTE*)site;
        if (p[0] != 0xE8 || (DWORD)(site + 5 + *(int*)(p + 1)) != 0x4AEB10) continue;
        DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(p + 1) = (int)((BYTE*)&SubtitleTraceStub - (p + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        ++n;
    }
    Log("subtitle trace installed (%d of 3 sites)", n);
}
