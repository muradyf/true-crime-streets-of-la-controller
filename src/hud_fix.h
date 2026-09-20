// In-game HUD and episode-map scaling.
//
// The HUD master render 0x4DA780 (reached only through the cdecl thunk 0x4DD720, draws only when p2 == 4) positions
// elements with the anchor helpers (scale [0x6AEA00] + safe rect) or with GetScreenW/H (0x6089F0/0x608A00) minus
// 640x480-pixel offsets, but every size is a raw 640x480 pixel constant. So at high resolutions it stays tiny.
// The episode-select screen (render 0x568390, vtable slot 0x68207C) has the same bug for its episode map: node
// icons (0x566D70) and bullet/trail images (0x559D10) go through the rect builder 0x4CB7F0, which scales positions
// by [0x6AEA00] but not sizes.
//
// Fix: run these passes in a virtual space of height 480 (width W/s, scale 1.0, safe rect /s), then multiply the
// pre-transformed vertices by s when each 2D batch is flushed (0x609FD0; batch +0x10 = vertex buffer whose +0x28 is
// the locked start, batch +0x14 = write pointer, 0x24 bytes per vertex, x at +0, y at +4, writers subtract 0.5).
// Text inside a pass takes its size from [0x6AEA00] and its position from the anchors, so it keeps its size.
#pragma once

static int g_hudFix = 1;
static int g_inHud = 0;
static float g_hudScale = 1.0f;
static float g_hudRealW = 0.0f, g_hudRealH = 0.0f;      // the pass's real pixel size, for centring the reticle

struct VirtualUiState {
    bool active = false;
    int w = 0, h = 0, offX = 0, offY = 0;
    float scaleX = 1.0f, scaleY = 1.0f;
    int rect[4] = {};
};

// The 2D batches are shared, so a flush during the pass can carry vertices queued before it (the aim reticle is drawn
// that way: parts of it landed in the batch before the HUD pass started and were scaled too, which moved its cross
// off centre and left a stray line at one corner). The write pointers are noted when the pass begins, and only
// vertices added after that are scaled.
static DWORD* const kBatchObjects[] = { (DWORD*)0x6B9710, (DWORD*)0x7511E0 };
static float* g_batchPassStart[2] = {};

static void NoteBatchWritePointers() {
    for (int i = 0; i < 2; ++i) g_batchPassStart[i] = (float*)kBatchObjects[i][0x14 / 4];
}

// ReticleTrace=1 (debug): log the vertices carrying the reticle's own colour before they are scaled, so its geometry
// (the centre dot, the box, the cross) can be checked independently of the scaling. The colour is the one the game
// packs at [0x6D54F4] from the three Options sliders and loads into every reticle primitive at 0x4DD346 - matching on
// that is exact, where rebuilding it from the ini globals is not. Each line also says which batch carried the
// vertices and where the pass's scaling window began, because the reticle is queued into the same shared batches as
// the rest of the HUD and only what was added after the pass started is meant to be scaled.
static int g_reticleTrace = 0;
static int g_reticleLogged = 0;

// [0x6D54F4] packs the three Options sliders in the order the screen stores them, which is red and blue the other way
// round from the D3DCOLOR the vertices carry (traced: global FF8000 against vertices 0080FF).
static int g_reticleFix = 1;

static DWORD ReticleColour() {
    DWORD packed = *(DWORD*)0x6D54F4;
    return ((packed & 0xFF) << 16) | (packed & 0xFF00) | ((packed >> 16) & 0xFF);
}

static void TraceReticleVertices(DWORD* batch, float* p, float* end, float* from, DWORD stride) {
    if (!g_reticleTrace || g_reticleLogged > 40) return;
    int count = (int)(((BYTE*)end - (BYTE*)p) / stride);
    if (count <= 0 || count > 20000) return;
    DWORD want = ReticleColour();
    // ReticleTrace=2: select by position instead of colour - the reticle is the only HUD element sitting on the exact
    // centre of the screen, so everything within 30 units of it is its own geometry, whatever colour or vertex layout
    // it uses. Both candidate colour offsets are dumped so the layout can be read off the data rather than assumed.
    if (g_reticleTrace >= 2) {
        float cx = ScreenW() * 0.5f, cy = ScreenH() * 0.5f;
        if (count < 6) return;                              // 4-vertex quads are loading-screen glyphs, not the reticle
        char raw[850] = "";
        int hits = 0;
        for (int i = 0; i < count; ++i) {
            BYTE* v = (BYTE*)p + i * stride;
            float x = ((float*)v)[0], y = ((float*)v)[1];
            if (x < cx - 30.0f || x > cx + 30.0f || y < cy - 30.0f || y > cy + 30.0f) continue;
            char one[110];
            _snprintf_s(one, sizeof(one), _TRUNCATE, "#%d(%.1f,%.1f|%08lX,%08lX) ", i, x, y,
                        *(DWORD*)(v + 0x10), *(DWORD*)(v + 0x14));
            if (strlen(raw) + strlen(one) < sizeof(raw) - 1) strcat_s(raw, one);
            if (++hits >= 30) break;
        }
        if (!hits) return;
        ++g_reticleLogged;
        Log("reticle raw: batch %08lX, %d vertices, scaled from #%d, %d near centre (%.1f,%.1f), want %06lX, "
            "scale %.3f, virtual %dx%d: %s",
            (DWORD)(DWORD_PTR)batch, count, (int)(((BYTE*)from - (BYTE*)p) / stride), hits, cx, cy, want,
            g_hudScale, ScreenW(), ScreenH(), raw);
        return;
    }
    char line[900] = "";
    int hits = 0, idx = 0;
    for (float* v = p; (BYTE*)v + stride <= (BYTE*)end; v = (float*)((BYTE*)v + stride), ++idx) {
        if ((*(DWORD*)((BYTE*)v + 0x10) & 0x00FFFFFF) != want) continue;
        char one[80];
        _snprintf_s(one, sizeof(one), _TRUNCATE, "#%d(%.1f,%.1f) ", idx, v[0], v[1]);
        if (strlen(line) + strlen(one) < sizeof(line) - 1) strcat_s(line, one);
        if (++hits > 40) break;
    }
    if (!hits) return;
    ++g_reticleLogged;
    int cut = (int)(((BYTE*)from - (BYTE*)p) / stride);
    Log("reticle: batch %08lX, %d vertices, scaled from #%d, colour %06lX, scale %.3f, virtual %dx%d: %s",
        (DWORD)(DWORD_PTR)batch, count, cut, want, g_hudScale, ScreenW(), ScreenH(), line);
}

static void __cdecl ScaleHudBatch(DWORD* batch) {
    DWORD* vb = (DWORD*)batch[0x10 / 4];
    if (!vb) return;
    float* p = (float*)vb[0x28 / 4];
    float* end = (float*)batch[0x14 / 4];
    if (!p || end <= p) return;
    float* from = p;
    for (int i = 0; i < 2; ++i)
        if (batch == kBatchObjects[i] && g_batchPassStart[i] > p && g_batchPassStart[i] <= end) from = g_batchPassStart[i];
    // The sanity limit belongs on the range actually walked, not on everything the buffer happens to hold. Checking
    // the whole buffer first made a batch that had already collected more than 0x10000 vertices before the pass fail
    // the test outright, so nothing in that flush was scaled and the HUD elements it carried stayed at their virtual
    // size - small enough at 2560x1600 to look missing.
    if ((BYTE*)end - (BYTE*)from > 0x10000 * 0x24) return;
    if (cfg.debugLog && from == p && (BYTE*)end - (BYTE*)p > 0x8000 * 0x24) {
        static int warned = 0;
        if (warned < 3) { ++warned; Log("HUD flush: %d vertices with no pass start noted", (int)(((BYTE*)end - (BYTE*)p) / 0x24)); }
    }
    TraceReticleVertices(batch, p, end, from, 0x24);
    // A line one unit thick becomes s pixels, so at a HUD size that is not a whole multiple - 70% of 1600 is 2.333 -
    // the same stroke rasterises to 2 pixels on one side of the reticle and 3 on the other, and no rounding of the
    // positions can fix that. The reticle is drawn from whole numbers about the centre of the screen, so it is given a
    // whole-number scale of its own and placed on the real centre; the rest of the HUD keeps the chosen size.
    float s = g_hudScale;
    float sr = g_reticleScale > 0 ? (float)g_reticleScale : floorf(s + 0.5f);
    if (sr < 1.0f) sr = 1.0f;
    const DWORD want = ReticleColour();
    // The reticle is drawn from whole numbers rather than authored art, and it has to stay square, so it keeps the
    // uniform scale while the rest of the pass takes the narrower one - which means it needs the centring branch
    // whenever the two differ, not only when the HUD size is fractional.
    const bool snap = g_reticleFix && sr != s && g_hudRealW > 0.0f;
    const float vcx = ScreenW() * 0.5f - 0.5f, vcy = ScreenH() * 0.5f - 0.5f;
    const float rcx = g_hudRealW * 0.5f - 0.5f, rcy = g_hudRealH * 0.5f - 0.5f;
    for (p = from; (BYTE*)p + 0x24 <= (BYTE*)end; p = (float*)((BYTE*)p + 0x24)) {
        if (snap && (*(DWORD*)((BYTE*)p + 0x10) & 0x00FFFFFF) == want &&
            fabsf(p[0] - vcx) < 40.0f && fabsf(p[1] - vcy) < 40.0f) {
            p[0] = (p[0] - vcx) * sr + rcx;
            p[1] = (p[1] - vcy) * sr + rcy;
        } else {
            p[0] = (p[0] + 0.5f) * s - 0.5f;
            p[1] = (p[1] + 0.5f) * s - 0.5f;
        }
    }
}

// ---- HudFontAspect: the port's horizontal stretch, applied to HUD glyphs only
// The menus get it from the X scale global, which this pass sets to 1, so HUD text came out a quarter wider than
// the same text in a menu. The fonts carry their own 2x2 glyph transform at font+0x10, used by the text draw at
// 0x60B9C0 when bit 0 of font+0x22 is set - the same one the subtitle fix uses - so narrowing x there reaches the
// glyphs without touching a single quad. Which fonts the HUD draws with is not fixed, so they are collected as
// they are used, the draw being hooked to see them.
//
// Setting each font once a pass is not enough. The engine writes these fields itself while the pass runs - the
// objective counter's font is the known case, 0x4DBECE clearing the transform's gate bit on it every frame, which
// is why that counter needed the bit turned on in the first place - so anything it touches after we do goes out at
// full width. The counter and the objective line that fades in at the start of the firing range both did.
// So the narrowing is applied per draw instead, and is idempotent: the value written last time is remembered, and
// only when the font no longer holds it has the engine been there, in which case whatever it wrote becomes the new
// starting point. The gate bit is set every time for the same reason.
// One pass draws with more fonts than is obvious - the street name banner turned out to be the ninth, and a table of
// eight silently dropped it, which is why that one label stayed wide while the rest of the HUD narrowed.
// HUDTextAspect: narrow the HUD's glyphs by UIPixelAspect, as the scale globals already do for the menus.
//
// Not every font in the pass wants it. Logging each draw with the global that holds its font sorts them out, and
// the answer is a single exception rather than a list: the street name banner is drawn with the font in 0x6D9594
// and is right as the game draws it, while the hint heading and the location line (0x6D957C), the hint body
// (0x6D9550 / 0x6D9584) and the rest (0x6D9580) are a quarter too wide without help. Narrowing all of them made the
// banner squashed; narrowing none left the others stretched. Both reports fit this one exception.
//
// The button symbols are still narrowed with the letters around them. They live in the hint body's font, in the
// same string, drawn by one call with one glyph matrix, so telling them apart needs the draw split per glyph -
// the symbols are codes 0x80 to 0xA7 and the glyph index is in ecx at 0x60BEAC. Not done here.
static int g_hudTextAspect = 1;
static const DWORD kBannerFontSlot = 0x6D9594;   // street name banner: already the right shape
static const int kMaxHudFonts = 32;
static DWORD g_hudFonts[kMaxHudFonts] = {};
static float g_hudFontSaved[kMaxHudFonts] = {};             // the engine's last x, to put back at the end
static float g_hudFontApplied[kMaxHudFonts] = {};           // what we last wrote, to tell our value from theirs
static bool g_hudFontHadBit[kMaxHudFonts] = {};             // whether the gate bit was already set when we arrived
static int g_hudFontCount = 0;

// Debug: which fonts the text draw is handed, and whether the HUD pass is running at the time. Elements drawn on
// another layer never see the pass, so the narrowing never reaches them.
static void __cdecl TraceTextFont(DWORD font, float x, float y) {
    if (!cfg.debugLog) return;
    if (!g_inHud && y > 250.0f) return;                     // the menus would fill the table before play starts
    static DWORD seen[160]; static int n = 0;
    DWORD key = font ^ ((DWORD)g_inHud << 28) ^ ((DWORD)(int)(y / 40.0f) << 20) ^ ((DWORD)(int)(x / 120.0f) << 12);
    for (int i = 0; i < n; ++i) if (seen[i] == key) return;
    if (n >= 160) return;
    seen[n++] = key;
    float* m = font ? (float*)(font + 0x10) : nullptr;
    Log("text at (%6.0f,%6.0f) font %08lX inHud %d, globals %.2f/%.2f, transform %.3f %.3f flags %04X",
        x, y, font, g_inHud, *(float*)0x6AEA00, *(float*)0x6AEA04,
        m ? m[0] : 0.0f, m ? m[3] : 0.0f, font ? (unsigned)*(WORD*)(font + 0x22) : 0u);
}

static void __fastcall NoteHudFont(DWORD font) {
    if (!g_hudTextAspect || !g_inHud || !font || g_uiPixelAspect == 100) return;
    if (font == *(DWORD*)kBannerFontSlot) return;           // the banner is not stretched; leave it alone
    int i = -1;
    for (int k = 0; k < g_hudFontCount; ++k) if (g_hudFonts[k] == font) { i = k; break; }
    if (i < 0) {
        if (g_hudFontCount >= kMaxHudFonts) return;
        i = g_hudFontCount++;
        g_hudFonts[i] = font;
        g_hudFontHadBit[i] = (*(WORD*)(font + 0x22) & 1) != 0;
        g_hudFontApplied[i] = 0.0f;
        g_hudFontSaved[i] = *(float*)(font + 0x10);
    }
    float* m = (float*)(font + 0x10);
    if (m[0] != g_hudFontApplied[i]) {                      // the engine has written it since we last did
        g_hudFontSaved[i] = m[0];
        float x = m[0] != 0.0f ? m[0] : 1.0f;               // an untouched font leaves the transform at zero
        m[0] = x * (g_uiPixelAspect / 100.0f);              // narrow x, leave y and the shear alone
        g_hudFontApplied[i] = m[0];
    }
    *(WORD*)(font + 0x22) |= 1;                             // 0x60B9C0 ignores the transform unless this is set
}

// Put back only what was changed - the x scale and, if it was clear, the gate bit. Writing the whole flags word and
// the whole transform back is what made the main menu come up in small caps after a game had been loaded: the fonts
// are shared with the menus, the engine keeps other bits of that word for itself (the trace shows 000F, 0007, 0005,
// 001D on one font), and restoring the value captured at the start of the pass reverted its own later changes.
static void RestoreHudFonts() {
    for (int i = 0; i < g_hudFontCount; ++i) {
        *(float*)(g_hudFonts[i] + 0x10) = g_hudFontSaved[i];
        if (!g_hudFontHadBit[i]) *(WORD*)(g_hudFonts[i] + 0x22) &= ~1;
    }
    g_hudFontCount = 0;
}

// ---- the button symbols: where the quad is actually built (findings, no code)
// The symbols are glyphs of the same font as the letters around them, codes 0x80 to 0xA7, and the text draw builds
// one glyph matrix per call, so nothing above the glyph level can separate them. Two attempts failed before the
// right seam was found, and both are worth recording so they are not tried again:
//   - [esp+0x1C0] holds the font's x scale and is re-read per glyph, so writing it per glyph looked correct in a
//     trace. It changes nothing on screen: measured, the symbol stayed at w/h 0.812, exactly the narrowing.
//   - writing every frame slot that held the letter scale garbles the text and drops glyphs; those slots are the
//     loop's intermediates, rewritten before they are read.
// What actually places the pixels is 0x60A2C0, a per-vertex writer rather than a quad emitter: ecx is the batch,
// [ecx+0x14] the vertex about to be filled, the pushed argument a vec4 whose first lane is that vertex's x, and the
// two floats its texture coordinates. The text draw calls it once per corner from 0x60C134, 0x60C2DE, 0x60C482 and
// 0x60C5F1, and esi still holds the glyph's record at each - so the glyph is identifiable that far down, its index
// being (esi - [font+0x28] - 0x18) / 8 with 0x20 as the first character.
// Logged corners for one glyph: (1206,718) (1222,718) (1206,793) (1222,793), so a glyph is a 16x75 quad here, but
// the order is not the same for every glyph - a rule built on "the first corner is the left edge" never fired for
// the symbols on the controls screen. Widening one means collecting its vertices and adjusting them together once
// the glyph is complete, from its own leftmost corner.
// That was written and builds, but the menus could not be reached often enough to measure it, and unverified code
// that writes into vertex memory is not worth leaving in. It is in the history if it is wanted.

__declspec(naked) static void HudTextStub() {               // replaces the prologue of 0x60B9C0
    __asm {
        pushad
        push dword ptr [esp + 0x2C]                         // y
        push dword ptr [esp + 0x2C]                         // x
        push ecx                                            // font
        call TraceTextFont
        add esp, 12
        popad
        pushad
        call NoteHudFont                                    // __fastcall: ecx is the font
        popad
        push ebp                                            // the original prologue, then back into the body
        mov ebp, esp
        and esp, 0xFFFFFFF0
        sub esp, 0x354
        mov eax, 0x60B9CC
        jmp eax
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

// s = real pixels per virtual pixel. hudLayout: rebuild the safe rect and centring offsets from the game's own
// margins in the virtual screen (HUD has its own size); otherwise divide the current menu layout by s so the pass
// matches the surrounding menu exactly.
// UIPixelAspect is deliberately not applied to the pass geometry. Squashing the whole flush was tried and is wrong:
// the HUD's art is not stretched by the port, and narrowing it turned the round cop-points icon into an ellipse of
// exactly the factor applied - 44x55 where it had been 55x55. Only the glyphs are too wide, so the correction
// belongs on the font, not on the vertices (HudFontAspect below).
static void BeginVirtualUi(VirtualUiState& st, const char* name, float s, bool hudLayout) {
    int w = ScreenW(), h = ScreenH();
    if (!g_hudFix || g_inHud || s <= 1.0f) return;         // nested passes keep the outer virtual space
    st.active = true;
    st.w = w; st.h = h; st.offX = g_uiOffX; st.offY = g_uiOffY;
    st.scaleX = *(float*)0x6AEA00; st.scaleY = *(float*)0x6AEA04;
    memcpy(st.rect, (void*)0x7280F0, sizeof(st.rect));

    int vw = (int)(w / s + 0.5f), vh = (int)(h / s + 0.5f);
    *(float*)0x6AEA00 = 1.0f;
    *(float*)0x6AEA04 = 1.0f;
    *(int*)0x6B9B98 = vw;
    *(int*)0x6B9B9C = vh;
    if (hudLayout) {
        int m[4] = { 32, 24, 32, 24 };
        if (g_rectKnown) memcpy(m, g_rectMargins, sizeof(m));
        *(int*)0x7280F0 = m[0];
        *(int*)0x7280F4 = vw - m[2];
        *(int*)0x7280F8 = m[1];
        *(int*)0x7280FC = vh - m[3];
        g_uiOffX = vw > 640 ? (vw - 640) / 2 : 0;
        g_uiOffY = vh > 480 ? (vh - 480) / 2 : 0;
    } else {
        for (int i = 0; i < 4; ++i) *(int*)(0x7280F0 + i * 4) = (int)(st.rect[i] / s + 0.5f);
        g_uiOffX = (int)(st.offX / s + 0.5f);
        g_uiOffY = (int)(st.offY / s + 0.5f);
    }
    g_hudScale = s;
    g_hudRealW = (float)w;
    g_hudRealH = (float)h;
    NoteBatchWritePointers();
    g_inHud = 1;

    static unsigned loggedMask = 0;
    unsigned bit = hudLayout ? 1u : 2u;
    if (!(loggedMask & bit)) {
        loggedMask |= bit;
        Log("%s pass: virtual %dx%d, safe rect %d,%d-%d,%d, offset %d,%d, scale %.3f", name, vw, vh,
            *(int*)0x7280F0, *(int*)0x7280F8, *(int*)0x7280F4, *(int*)0x7280FC, g_uiOffX, g_uiOffY, s);
    }
}

static void EndVirtualUi(const VirtualUiState& st) {
    if (!st.active) return;
    RestoreHudFonts();
    g_inHud = 0;
    *(int*)0x6B9B98 = st.w;
    *(int*)0x6B9B9C = st.h;
    memcpy((void*)0x7280F0, st.rect, sizeof(st.rect));
    *(float*)0x6AEA00 = st.scaleX;
    *(float*)0x6AEA04 = st.scaleY;
    g_uiOffX = st.offX;
    g_uiOffY = st.offY;
}

static void __cdecl HudRenderHook(void* self, int a, int b, int c, int d) {   // replaces cdecl thunk 0x4DD720
    auto render = (void(__fastcall*)(void*, void*, int, int, int, int))0x4DA780;   // thiscall, ret 0x10
    VirtualUiState st;
    if (a == 4) BeginVirtualUi(st, "HUD", FitScale(g_hudScalePct), true);
    render(self, nullptr, a, b, c, d);
    EndVirtualUi(st);
}

static void __fastcall EpisodeScreenRenderHook(void* self, void*, void* batch) {  // vtable slot 0x68207C (thiscall, ret 4)
    auto render = (void(__fastcall*)(void*, void*, void*))0x568390;
    VirtualUiState st;
    BeginVirtualUi(st, "episode map", UiScale(), false);
    render(self, nullptr, batch);
    EndVirtualUi(st);
}

// Aim reticle alignment (ReticleFix).
//
// Style 3 (the default) draws four primitives, all from GetScreenW/H halved: the box outline 0x60A830 and three
// 0x60A6B0 rects - a vertical bar, a horizontal bar and a single pixel off the box's top-left corner. Traced at
// 1280x800 virtual (ReticleTrace=1), their quads come out as:
//     box        631.5..647.5 x 391.5..407.5   centre 639.5, 399.5
//     vert bar   638.5..639.5 x 394.5..403.5   centre 639.0, 399.0
//     horiz bar  634.5..643.5 x 398.5..399.5   centre 639.0, 399.0
//     corner dot 630.5..631.5 x 390.5..391.5   one pixel diagonally outside the box corner
// The box is an even 16 units across and the bars are an odd 1, so the game's integer maths cannot put them on the
// same centre: the cross sits half a unit up and left of the box, and the corner dot sits just outside the corner.
// At 640x480 that is half a pixel and one stray pixel, which nobody sees. The HUD pass magnifies the whole reticle,
// so at 2560x1600 the half unit becomes a whole pixel of visible offset and the stray pixel becomes a 2x2 blob.
//
// Quad edges land on k + 0.5, so a span of an even number of units is centred on a half unit and an odd span on a
// whole one. The box is 16 and the bars are 1, so they can never agree; one of the two has to change parity. The
// bars are what the player looks at - and the reticle preview in Options > Controls shows them thin - so the box
// gives way instead: 16 units -> 15, which puts its centre on 639.0 with the bars, and leaves their thickness and
// length exactly as the game drew them. The whole reticle then sits half a unit (one pixel at 2x) up and left of the
// screen centre, which is unavoidable for an odd-sized reticle on an even-sized screen, and is where the game's own
// cross always was.
//
// The corner dot is collapsed instead of removed: 0x60A6B0 writes its far edge at x2 - 1, so the empty rect is
// x2 = x1 + 1, not x2 = x1 - asking for zero gives a backwards quad that still covers a pixel (traced: it moved to
// 629.5..630.5 instead of disappearing).

// ReticleBox=0: draw the cross on its own, without the box around it. The box is one call, 0x60A830 at 0x4DD416, and
// its arguments (push 0, push colour, sub esp, 0x10 - 0x18 bytes in all) are popped by the callee, so the call can be
// replaced outright by the add esp that the callee would have done. The style's other three primitives are untouched.
static int g_reticleBox = 1;

static void ReticleBoxInstall() {
    if (g_reticleBox) return;
    const BYTE orig[] = { 0xE8, 0x15, 0xD4, 0x12, 0x00 };   // call 0x60A830
    if (memcmp((BYTE*)0x4DD416, orig, sizeof(orig))) {
        Log("reticle box draw differs, box not removed");
        return;
    }
    const BYTE skip[] = { 0x83, 0xC4, 0x18, 0x90, 0x90 };   // add esp, 0x18 ; nop ; nop
    PatchBytes(0x4DD416, skip, sizeof(skip));
    Log("reticle box removed (0x4DD416), cross only");
}

static void ReticleFixInstall() {
    if (!g_reticleFix) return;
    struct Site { DWORD addr; BYTE orig[3]; BYTE want; const char* what; };
    const Site sites[] = {
        { 0x4DD42C, { 0x8D, 0x50, 0x02 }, 0x01, "corner dot height" },
        { 0x4DD43C, { 0x8D, 0x51, 0x02 }, 0x01, "corner dot width"  },
        { 0x4DD3E2, { 0x8D, 0x50, 0x11 }, 0x10, "box height" },
        { 0x4DD3F2, { 0x8D, 0x51, 0x11 }, 0x10, "box width"  },
    };
    for (const Site& s : sites)
        if (memcmp((BYTE*)s.addr, s.orig, sizeof(s.orig))) {
            Log("reticle: %s site 0x%06lX differs, alignment fix not installed", s.what, s.addr);
            return;
        }
    for (const Site& s : sites) PatchBytes(s.addr + 2, &s.want, 1);
    Log("reticle alignment fix installed (box 16 -> 15 units so the cross centres in it, corner dot removed)");
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
    const BYTE textOrig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x54, 0x03, 0x00, 0x00 };
    if (memcmp((BYTE*)0x60B9C0, textOrig, sizeof(textOrig)) == 0) {
        WriteJmp(0x60B9C0, &HudTextStub, sizeof(textOrig));
        Log("HUD font aspect installed (0x60B9C0)");
    } else Log("text draw bytes differ, HUD font aspect not installed");

    DWORD* slot = (DWORD*)0x68207C;
    if (*slot == 0x568390) {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = (DWORD)&EpisodeScreenRenderHook;
        VirtualProtect(slot, 4, old, &old);
        Log("episode map scale fix installed (vtable 0x68207C)");
    } else {
        Log("episode screen vtable slot differs (0x%08lX), episode map fix not installed", *slot);
    }
}
