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

static void TraceReticleVertices(DWORD* batch, float* p, float* end, float* from, DWORD stride) {
    if (!g_reticleTrace || g_reticleLogged > 40) return;
    int count = (int)(((BYTE*)end - (BYTE*)p) / stride);
    if (count <= 0 || count > 20000) return;
    // [0x6D54F4] packs the three Options sliders in the order the screen stores them, which is red and blue the other
    // way round from the D3DCOLOR the vertices carry (traced: global FF8000 against vertices 0080FF). Swap them back.
    DWORD packed = *(DWORD*)0x6D54F4;
    DWORD want = ((packed & 0xFF) << 16) | (packed & 0xFF00) | ((packed >> 16) & 0xFF);
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
    if (!p || end <= p || (BYTE*)end - (BYTE*)p > 0x10000 * 0x24) return;
    float* from = p;
    for (int i = 0; i < 2; ++i)
        if (batch == kBatchObjects[i] && g_batchPassStart[i] > p && g_batchPassStart[i] <= end) from = g_batchPassStart[i];
    TraceReticleVertices(batch, p, end, from, 0x24);
    float s = g_hudScale;
    for (p = from; (BYTE*)p + 0x24 <= (BYTE*)end; p = (float*)((BYTE*)p + 0x24)) {
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

// s = real pixels per virtual pixel. hudLayout: rebuild the safe rect and centring offsets from the game's own
// margins in the virtual screen (HUD has its own size); otherwise divide the current menu layout by s so the pass
// matches the surrounding menu exactly.
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
// Fix: make every part share the box's centre. The bars grow by one unit in each direction (thickness 1 -> 2, length
// 9 -> 10), which is the only way an even-width box and a centred bar can agree, and the corner dot is given a zero
// size so it draws nothing.
static int g_reticleFix = 1;

static void ReticleFixInstall() {
    if (!g_reticleFix) return;
    struct Site { DWORD addr; BYTE orig[3]; BYTE want; const char* what; };
    const Site sites[] = {
        { 0x4DD42C, { 0x8D, 0x50, 0x02 }, 0x00, "corner dot height" },
        { 0x4DD43C, { 0x8D, 0x51, 0x02 }, 0x00, "corner dot width"  },
        { 0x4DD498, { 0x8D, 0x50, 0x0A }, 0x0B, "vertical bar length" },
        { 0x4DD4A9, { 0x8D, 0x51, 0x02 }, 0x03, "vertical bar thickness" },
        { 0x4DD4F4, { 0x8D, 0x50, 0x02 }, 0x03, "horizontal bar thickness" },
        { 0x4DD505, { 0x8D, 0x51, 0x0A }, 0x0B, "horizontal bar length" },
    };
    for (const Site& s : sites)
        if (memcmp((BYTE*)s.addr, s.orig, sizeof(s.orig))) {
            Log("reticle: %s site 0x%06lX differs, alignment fix not installed", s.what, s.addr);
            return;
        }
    for (const Site& s : sites) PatchBytes(s.addr + 2, &s.want, 1);
    Log("reticle alignment fix installed (bars centred on the box, corner dot removed)");
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
