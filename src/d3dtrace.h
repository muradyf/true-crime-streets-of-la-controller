// D3DTrace=1 (debug): per-second counts and failures of the Direct3D 8 calls that decide whether a frame is drawn.
// IDirect3DDevice8 vtable: TestCooperativeLevel 3, GetDisplayMode 8, Present 15, GetBackBuffer 16,
// SetRenderTarget 31, BeginScene 34, EndScene 35, Clear 36, SetViewport 40, DrawPrimitive 70,
// DrawIndexedPrimitive 71, DrawPrimitiveUP 72, DrawIndexedPrimitiveUP 73.
#pragma once

static int g_d3dTrace = 0;
static void** g_d3dTraceVtable = nullptr;

struct D3DCounter { int calls = 0, fails = 0; HRESULT lastFail = 0; };
static D3DCounter g_cPresent, g_cBegin, g_cEnd, g_cClear, g_cDraw, g_cDrawIdx, g_cDrawUP, g_cDrawIdxUP, g_cViewport, g_cRT;
static struct { DWORD x, y, w, h; float minz, maxz; } g_lastViewport;
static void* g_origD3D[96];     // needs room for CreatePixelShader (87)
static ULONGLONG g_nextD3DReport = 0;

static inline HRESULT Count(D3DCounter& c, HRESULT hr) { ++c.calls; if (FAILED(hr)) { ++c.fails; c.lastFail = hr; } return hr; }

static HRESULT __stdcall HkPresent(void* d, const RECT* a, const RECT* b, HWND w, void* r) {
    return Count(g_cPresent, ((HRESULT(__stdcall*)(void*, const RECT*, const RECT*, HWND, void*))g_origD3D[15])(d, a, b, w, r));
}
static HRESULT __stdcall HkBegin(void* d) { return Count(g_cBegin, ((HRESULT(__stdcall*)(void*))g_origD3D[34])(d)); }
static HRESULT __stdcall HkEnd(void* d) { return Count(g_cEnd, ((HRESULT(__stdcall*)(void*))g_origD3D[35])(d)); }
static HRESULT __stdcall HkClear(void* d, DWORD n, void* rects, DWORD f, DWORD col, float z, DWORD s) {
    return Count(g_cClear, ((HRESULT(__stdcall*)(void*, DWORD, void*, DWORD, DWORD, float, DWORD))g_origD3D[36])(d, n, rects, f, col, z, s));
}
static HRESULT __stdcall HkSetViewport(void* d, void* vp) {
    memcpy(&g_lastViewport, vp, sizeof(g_lastViewport));
    return Count(g_cViewport, ((HRESULT(__stdcall*)(void*, void*))g_origD3D[40])(d, vp));
}
static HRESULT __stdcall HkSetRT(void* d, void* rt, void* z) { return Count(g_cRT, ((HRESULT(__stdcall*)(void*, void*, void*))g_origD3D[31])(d, rt, z)); }
static HRESULT __stdcall HkDraw(void* d, DWORD t, UINT s, UINT n) { return Count(g_cDraw, ((HRESULT(__stdcall*)(void*, DWORD, UINT, UINT))g_origD3D[70])(d, t, s, n)); }
static HRESULT __stdcall HkDrawIdx(void* d, DWORD t, UINT mi, UINT nv, UINT si, UINT pc) {
    return Count(g_cDrawIdx, ((HRESULT(__stdcall*)(void*, DWORD, UINT, UINT, UINT, UINT))g_origD3D[71])(d, t, mi, nv, si, pc));
}
static HRESULT __stdcall HkDrawUP(void* d, DWORD t, UINT pc, void* data, UINT stride) {
    return Count(g_cDrawUP, ((HRESULT(__stdcall*)(void*, DWORD, UINT, void*, UINT))g_origD3D[72])(d, t, pc, data, stride));
}
static HRESULT __stdcall HkDrawIdxUP(void* d, DWORD t, UINT mi, UINT nv, UINT pc, void* idx, DWORD fmt, void* data, UINT stride) {
    return Count(g_cDrawIdxUP, ((HRESULT(__stdcall*)(void*, DWORD, UINT, UINT, UINT, void*, DWORD, void*, UINT))g_origD3D[73])(d, t, mi, nv, pc, idx, fmt, data, stride));
}

// ShaderTrace=1 (debug): the game creates its vertex/pixel shaders once at start (0x5EC9E0 / 0x5ECAE0 call
// CreateVertexShader / CreatePixelShader, vtable 75 / 87) and does not check the result, so a driver or wrapper that
// rejects one leaves handle 0 and the geometry draws with the fixed-function pipeline (white/untextured).
static int g_shaderTrace = 0;
static int g_shaderSeq = 0;

static HRESULT __stdcall HkCreateVS(void* d, const DWORD* decl, const DWORD* fn, DWORD* handle, DWORD usage) {
    HRESULT hr = ((HRESULT(__stdcall*)(void*, const DWORD*, const DWORD*, DWORD*, DWORD))g_origD3D[75])(d, decl, fn, handle, usage);
    int i = ++g_shaderSeq;
    if (FAILED(hr) || !handle || !*handle)
        Log("shader trace: vertex shader #%d FAILED hr 0x%08lX handle %lu (version 0x%08lX)", i, hr,
            handle ? *handle : 0, fn ? fn[0] : 0);
    else if (g_shaderTrace > 1)
        Log("shader trace: vertex shader #%d ok handle %lu (version 0x%08lX)", i, *handle, fn ? fn[0] : 0);
    return hr;
}

static HRESULT __stdcall HkCreatePS(void* d, const DWORD* fn, DWORD* handle) {
    HRESULT hr = ((HRESULT(__stdcall*)(void*, const DWORD*, DWORD*))g_origD3D[87])(d, fn, handle);
    int i = ++g_shaderSeq;
    if (FAILED(hr) || !handle || !*handle)
        Log("shader trace: pixel shader #%d FAILED hr 0x%08lX handle %lu (version 0x%08lX)", i, hr,
            handle ? *handle : 0, fn ? fn[0] : 0);
    else if (g_shaderTrace > 1)
        Log("shader trace: pixel shader #%d ok handle %lu (version 0x%08lX)", i, *handle, fn ? fn[0] : 0);
    return hr;
}

// The names come from the game's own loaders: 0x5EC9E0(path, decl) creates a vertex shader, 0x5ECAE0(path) a pixel
// shader, both returning 0 on failure. Logging the path here and the result in the vtable hooks above pairs them up.
static void __cdecl LogShaderLoad(const char* path) {
    if (path) Log("shader trace: loading %s", path);
}

__declspec(naked) static void ShaderLoadVSStub() {          // entry of 0x5EC9E0: sub esp,8 ; mov al,[ecx+18h]
    __asm {
        pushad
        push dword ptr [esp + 0x24]                         // path (pushad 32 + return address)
        call LogShaderLoad
        add esp, 4
        popad
        sub esp, 8
        mov al, byte ptr [ecx + 0x18]
        push 0x5EC9E6
        ret
    }
}

__declspec(naked) static void ShaderLoadPSStub() {          // entry of 0x5ECAE0: sub esp,8 ; mov eax,[esp+0Ch]
    __asm {
        pushad
        push dword ptr [esp + 0x24]
        call LogShaderLoad
        add esp, 4
        popad
        sub esp, 8
        mov eax, dword ptr [esp + 0x0C]
        push 0x5ECAE7
        ret
    }
}

static void ShaderTraceInstall() {
    if (!g_shaderTrace) return;
    const BYTE vsOrig[] = { 0x83, 0xEC, 0x08, 0x8A, 0x41, 0x18 };
    const BYTE psOrig[] = { 0x83, 0xEC, 0x08, 0x8B, 0x44, 0x24, 0x0C };
    if (memcmp((BYTE*)0x5EC9E0, vsOrig, sizeof(vsOrig)) || memcmp((BYTE*)0x5ECAE0, psOrig, sizeof(psOrig))) {
        Log("shader loader bytes differ, shader trace names not installed");
        return;
    }
    WriteJmp(0x5EC9E0, &ShaderLoadVSStub, sizeof(vsOrig));
    WriteJmp(0x5ECAE0, &ShaderLoadPSStub, sizeof(psOrig));
    Log("shader trace installed (0x5EC9E0, 0x5ECAE0)");
}

static void D3DTraceHookSlot(void** vt, int i, void* fn) {
    if (vt[i] == fn) return;
    DWORD old;
    VirtualProtect(&vt[i], sizeof(void*), PAGE_READWRITE, &old);
    g_origD3D[i] = vt[i];
    vt[i] = fn;
    VirtualProtect(&vt[i], sizeof(void*), old, &old);
}

// Window messages that can cost an exclusive-mode device (logged with the window state).
static WNDPROC g_origGameWndProc = nullptr;
static HWND g_tracedWnd = nullptr;
static HRESULT g_lastTcl = 1;

static void LogWindowState(const char* why) {
    HWND h = g_tracedWnd;
    RECT wr = {}, cr = {};
    if (h) { GetWindowRect(h, &wr); GetClientRect(h, &cr); }
    HWND fg = GetForegroundWindow();
    char cls[64] = "";
    if (fg) GetClassNameA(fg, cls, sizeof(cls));
    DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
    Log("win %s: rect %ld,%ld-%ld,%ld client %ldx%ld style 0x%08lX ex 0x%08lX iconic %d | foreground %s (pid %lu%s) | screen %dx%d",
        why, wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom,
        h ? (DWORD)GetWindowLongA(h, GWL_STYLE) : 0, h ? (DWORD)GetWindowLongA(h, GWL_EXSTYLE) : 0, h ? IsIconic(h) : 0,
        cls, pid, pid == GetCurrentProcessId() ? ", game" : "", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

static LRESULT CALLBACK TraceWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATEAPP: case WM_ACTIVATE: case WM_SIZE: case WM_MOVE: case WM_WINDOWPOSCHANGED: case WM_DISPLAYCHANGE:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_STYLECHANGED: case WM_SYSCOMMAND: case WM_NCACTIVATE: case WM_SHOWWINDOW: {
        char what[64]; sprintf_s(what, "msg 0x%04X w 0x%lX l 0x%lX", m, (unsigned long)w, (unsigned long)l);
        LogWindowState(what);
        break;
    }
    }
    return CallWindowProcA(g_origGameWndProc, h, m, w, l);
}

static void** g_shaderTraceVtable = nullptr;

static void ShaderTraceUpdate() {                           // hook the create calls as soon as a device exists
    if (!g_shaderTrace) return;
    void* dev = *(void**)0x72C014;
    if (!dev) return;
    void** vt = *(void***)dev;
    if (vt == g_shaderTraceVtable) return;
    g_shaderTraceVtable = vt;
    D3DTraceHookSlot(vt, 75, &HkCreateVS);
    D3DTraceHookSlot(vt, 87, &HkCreatePS);
    Log("shader trace: create hooks installed (vtable %p)", (void*)vt);
}

static void D3DTraceUpdate() {
    ShaderTraceUpdate();
    if (!g_d3dTrace) return;
    void* dev = *(void**)0x72C014;
    if (!dev) return;
    HWND gameWnd = *(HWND*)0x75119C;
    if (gameWnd && gameWnd != g_tracedWnd && IsWindow(gameWnd)) {
        g_tracedWnd = gameWnd;
        g_origGameWndProc = (WNDPROC)SetWindowLongPtrA(gameWnd, GWLP_WNDPROC, (LONG_PTR)&TraceWndProc);
        LogWindowState("subclassed");
    }
    void** vt = *(void***)dev;
    if (*(int*)0x6B99E0 == 3) {
        HRESULT tcl = ((HRESULT(__stdcall*)(void*))vt[3])(dev);
        if (tcl != g_lastTcl) { Log("d3d TestCooperativeLevel 0x%08lX -> 0x%08lX", g_lastTcl, tcl); g_lastTcl = tcl; LogWindowState("tcl change"); }
    }
    if (vt != g_d3dTraceVtable) {
        D3DTraceHookSlot(vt, 15, &HkPresent);  D3DTraceHookSlot(vt, 34, &HkBegin);   D3DTraceHookSlot(vt, 35, &HkEnd);
        D3DTraceHookSlot(vt, 36, &HkClear);    D3DTraceHookSlot(vt, 40, &HkSetViewport); D3DTraceHookSlot(vt, 31, &HkSetRT);
        D3DTraceHookSlot(vt, 70, &HkDraw);     D3DTraceHookSlot(vt, 71, &HkDrawIdx); D3DTraceHookSlot(vt, 72, &HkDrawUP);
        D3DTraceHookSlot(vt, 73, &HkDrawIdxUP);
        g_d3dTraceVtable = vt;
        struct { UINT w, h, refresh; DWORD fmt; } mode = {};
        ((HRESULT(__stdcall*)(void*, void*))vt[8])(dev, &mode);
        void* bb = nullptr;
        struct { DWORD format, type, usage, pool; UINT size; DWORD ms; UINT w, h; } desc = {};
        if (SUCCEEDED(((HRESULT(__stdcall*)(void*, UINT, DWORD, void**))vt[16])(dev, 0, 0, &bb)) && bb) {
            ((HRESULT(__stdcall*)(void*, void*))(*(void***)bb)[8])(bb, &desc);
            ((ULONG(__stdcall*)(void*))(*(void***)bb)[2])(bb);
        }
        Log("d3d trace installed: device %p, display %ux%u fmt %lu, back buffer %ux%u fmt %lu, screen globals %dx%d",
            dev, mode.w, mode.h, mode.fmt, desc.w, desc.h, desc.format, *(int*)0x6B9B98, *(int*)0x6B9B9C);
    }
    if (GetTickCount64() < g_nextD3DReport) return;
    g_nextD3DReport = GetTickCount64() + 1000;
    HRESULT tcl = ((HRESULT(__stdcall*)(void*))vt[3])(dev);
    auto fmt = [](const D3DCounter& c) { static char b[4][48]; static int k = 0; char* s = b[k++ % 4];
        if (c.fails) sprintf_s(s, 48, "%d(fail %d 0x%08lX)", c.calls, c.fails, c.lastFail); else sprintf_s(s, 48, "%d", c.calls); return s; };
    Log("d3d/s: present %s begin %s end %s clear %s", fmt(g_cPresent), fmt(g_cBegin), fmt(g_cEnd), fmt(g_cClear));
    Log("d3d/s: draw %s drawIdx %s drawUP %s drawIdxUP %s setRT %s viewport %s last %lu,%lu %lux%lu | TCL 0x%08lX state %d",
        fmt(g_cDraw), fmt(g_cDrawIdx), fmt(g_cDrawUP), fmt(g_cDrawIdxUP), fmt(g_cRT), fmt(g_cViewport),
        g_lastViewport.x, g_lastViewport.y, g_lastViewport.w, g_lastViewport.h, tcl, *(int*)0x6B99E0);
    g_cPresent = g_cBegin = g_cEnd = g_cClear = g_cDraw = g_cDrawIdx = g_cDrawUP = g_cDrawIdxUP = g_cViewport = g_cRT = D3DCounter();
}
