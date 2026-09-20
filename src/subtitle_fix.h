// Cutscene subtitle size (SubtitleScale).
//
// Subtitles are queued by 0x4AEB10 and drawn by the element callback 0x490A80, which calls the text routine 0x60B9C0
// through the subtitle font [0x6D9560], whose glyph scale is 1.0, so at high resolutions the text stays at its
// 640x480 size. Scaling it needs three patches that agree with each other:
//   - draw scale: the glyph size comes from the subtitle font object's 2x2 transform [font + 0x10] (identity 1 0 0 1),
//     which 0x60B9C0 multiplies into every glyph; it is set to the subtitle scale. 0x60B9C0 only reads that transform
//     when bit 0 of the font's flags [font + 0x22] is set (test cl,1 at 0x60B9E8; without it the whole matrix block
//     is skipped by the jump at 0x60BA36) and on the subtitle font it is clear, so the bit is set here too. Writing
//     the transform on its own changed the line breaks and the rise, which are computed here, but never the glyphs;
//   - rise: the layout (0x490A21) places the text block's bottom on the element's y and moves up by
//     font line height * lines at 1x, so that height is multiplied by the scale too;
//   - wrap width: 0x4AEB10 sizes the element W/640 * 512 (mulss [0x67AEC4] at 0x4AEB6A, a shared constant), and
//     lines are broken to that width at 1x; it now points at 512 / scale so scaled lines still fit the screen.
// SubtitleScale: 0 = follow MenuScale, otherwise percent of filling the screen height (like MenuScale).
#pragma once

static float g_subScale = 1.0f;
static float g_subWrapWidth = 512.0f;
static float g_subWrittenScale = 0.0f;
static bool g_subtitleFix = false;

__declspec(naked) static void SubtitleRiseStub() {         // replaces 0x490A21: movzx eax,[eax+17h]; imul eax,[esp+18h]; neg eax
    __asm {
        movzx eax, byte ptr [eax + 0x17]
        imul eax, dword ptr [esp + 0x18]
        push eax
        fild dword ptr [esp]
        fmul dword ptr [g_subScale]
        fistp dword ptr [esp]
        pop eax
        neg eax
        push 0x490A2C
        ret
    }
}

static void SubtitleFixUpdate() {                           // follows the resolution and MenuScale
    if (!g_subtitleFix) return;
    DWORD font = *(DWORD*)0x6D9560;                         // subtitle font object (Font_Subtitles.fnt)
    if (!font) return;
    float s = FitScale(g_subtitleScalePct > 0 ? g_subtitleScalePct : g_menuScalePct);
    if (s < 1.0f) s = 1.0f;
    float* m = (float*)(font + 0x10);                       // per-font 2x2 glyph transform (identity 1 0 0 1), used by 0x60B9C0
    if (s == g_subWrittenScale && m[0] == s && m[3] == s && m[1] == 0.0f && m[2] == 0.0f) return;
    static DWORD loggedFont = 0;
    if (font != loggedFont) {                               // a reload can build a new font object; notice it
        loggedFont = font;
        Log("subtitles: font %08lX transform was %.3f %.3f %.3f %.3f", font, m[0], m[1], m[2], m[3]);
    }
    m[0] = s; m[1] = 0.0f;
    m[2] = 0.0f; m[3] = s;
    WORD* flags = (WORD*)(font + 0x22);                     // 0x60B9C0 ignores the transform unless bit 0 is set
    if (!(*flags & 1)) {
        *flags |= 1;
        Log("subtitles: font flags %04X had the transform bit clear, set it", (unsigned)*flags);
    }
    g_subScale = s;
    g_subWrapWidth = 512.0f / s;
    g_subWrittenScale = s;
    Log("subtitles: scale %.3f, wrap width %.1f layout units", s, g_subWrapWidth);
}

static void SubtitleFixInstall() {
    if (!g_uiFix) return;
    const BYTE rise[] = { 0x0F, 0xB6, 0x40, 0x17, 0x0F, 0xAF, 0x44, 0x24, 0x18, 0xF7, 0xD8 };
    const BYTE wrap[] = { 0xF3, 0x0F, 0x59, 0x05, 0xC4, 0xAE, 0x67, 0x00 };
    if (memcmp((BYTE*)0x490A21, rise, sizeof(rise)) || memcmp((BYTE*)0x4AEB6A, wrap, sizeof(wrap))) {
        Log("subtitle code differs, subtitle scale fix not installed");
        return;
    }
    WriteJmp(0x490A21, &SubtitleRiseStub, sizeof(rise));
    DWORD old;
    VirtualProtect((LPVOID)0x4AEB6E, 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)0x4AEB6E = (DWORD)&g_subWrapWidth;
    VirtualProtect((LPVOID)0x4AEB6E, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPVOID)0x4AEB6A, 8);
    g_subtitleFix = true;
    Log("subtitle scale fix installed (font scale, rise 0x490A21, wrap width 0x4AEB6A)");
}
