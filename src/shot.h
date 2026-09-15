// Debug screenshots straight from Direct3D (works in exclusive fullscreen, where desktop capture sees black).
// When DebugLog=1 and scripts\TrueCrimeDualSense.shot exists, its first line is taken as an output .bmp path.
// The device's Present (vtable 15) is hooked while a shot is pending: the back buffer (GetBackBuffer 16) is copied
// with CopyRects (28) into a system-memory image surface (CreateImageSurface 27) of the same format, locked and
// written as a top-down 32-bit BMP, then the frame is presented. GetFrontBuffer returned black through the system
// d3d8 layer in fullscreen, so it is not used.
// IDirect3DSurface8 vtable: Release 2, GetDesc 8, LockRect 9, UnlockRect 10.
#pragma once

static char g_shotTrigger[MAX_PATH] = "";
static char g_shotPath[MAX_PATH] = "";
static ULONGLONG g_nextShotCheck = 0;
static void** g_shotVtable = nullptr;
using PresentFn = HRESULT(__stdcall*)(void*, const RECT*, const RECT*, HWND, void*);
static PresentFn g_origPresent = nullptr;

// ForceWindowed (debug): [Renderer] Windowed is read by 0x61FA90 with the "registry" flag set; no registry key is
// ever opened, so it always returns the default pushed at 0x6216C4 (push 0) and writes 0 back to the ini.
// Changing that default to 1 makes the game run in a window at the configured resolution.
static int g_forceWindowed = 0;

static void ForceWindowedInstall() {
    if (!g_forceWindowed) return;
    BYTE* p = (BYTE*)0x6216C2;
    const BYTE expected[] = { 0x6A, 0x01, 0x6A, 0x00, 0x68, 0x88, 0xEF, 0x68, 0x00 };   // push 1; push 0; push "Windowed"
    if (memcmp(p, expected, sizeof(expected))) { Log("windowed default bytes differ, ForceWindowed not applied"); return; }
    DWORD old;
    VirtualProtect(p + 3, 1, PAGE_EXECUTE_READWRITE, &old);
    p[3] = 0x01;
    VirtualProtect(p + 3, 1, old, &old);
    Log("ForceWindowed: windowed mode default set to 1 (0x6216C5)");
}

static void ShotInit(const char* iniPath) {
    strcpy_s(g_shotTrigger, iniPath);
    char* dot = strrchr(g_shotTrigger, '.');
    if (dot) strcpy_s(dot, g_shotTrigger + sizeof(g_shotTrigger) - dot, ".shot");
}

static void CaptureBackBuffer(void* dev, const char* outPath) {
    void** vt = *(void***)dev;
    void* bb = nullptr;
    HRESULT hr = ((HRESULT(__stdcall*)(void*, UINT, DWORD, void**))vt[16])(dev, 0, 0 /*MONO*/, &bb);
    if (FAILED(hr) || !bb) { Log("shot: GetBackBuffer failed 0x%08lX", hr); return; }
    void** bvt = *(void***)bb;
    struct { DWORD format, type, usage, pool; UINT size; DWORD ms; UINT w, h; } desc = {};
    ((HRESULT(__stdcall*)(void*, void*))bvt[8])(bb, &desc);
    if (desc.format != 21 /*A8R8G8B8*/ && desc.format != 22 /*X8R8G8B8*/) {
        Log("shot: unsupported back buffer format %lu", desc.format);
        ((ULONG(__stdcall*)(void*))bvt[2])(bb);
        return;
    }
    void* img = nullptr;
    hr = ((HRESULT(__stdcall*)(void*, UINT, UINT, DWORD, void**))vt[27])(dev, desc.w, desc.h, desc.format, &img);
    if (SUCCEEDED(hr) && img) {
        void** ivt = *(void***)img;
        hr = ((HRESULT(__stdcall*)(void*, void*, const RECT*, UINT, void*, const POINT*))vt[28])(dev, bb, nullptr, 0, img, nullptr);
        if (SUCCEEDED(hr)) {
            struct { INT pitch; void* bits; } lr = {};
            if (SUCCEEDED(((HRESULT(__stdcall*)(void*, void*, const RECT*, DWORD))ivt[9])(img, &lr, nullptr, 0x10 /*READONLY*/))) {
                FILE* f = nullptr;
                if (!fopen_s(&f, outPath, "wb") && f) {
                    BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
                    ih.biSize = sizeof(ih); ih.biWidth = (LONG)desc.w; ih.biHeight = -(LONG)desc.h;
                    ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
                    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
                    fh.bfSize = fh.bfOffBits + desc.w * desc.h * 4;
                    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
                    for (UINT y = 0; y < desc.h; ++y) fwrite((BYTE*)lr.bits + y * lr.pitch, 4, desc.w, f);
                    fclose(f);
                    Log("shot: %ux%u -> %s", desc.w, desc.h, outPath);
                }
                ((HRESULT(__stdcall*)(void*))ivt[10])(img);
            }
        } else {
            Log("shot: CopyRects failed 0x%08lX", hr);
        }
        ((ULONG(__stdcall*)(void*))ivt[2])(img);
    } else {
        Log("shot: CreateImageSurface failed 0x%08lX", hr);
    }
    ((ULONG(__stdcall*)(void*))bvt[2])(bb);
}

static HRESULT __stdcall PresentHook(void* dev, const RECT* src, const RECT* dst, HWND wnd, void* dirty) {
    if (g_shotPath[0]) {
        char path[MAX_PATH];
        strcpy_s(path, g_shotPath);
        g_shotPath[0] = 0;
        CaptureBackBuffer(dev, path);
    }
    return g_origPresent(dev, src, dst, wnd, dirty);
}

static void EnsurePresentHook() {
    void* dev = *(void**)0x72C014;
    if (!dev) return;
    void** vt = *(void***)dev;
    if (vt == g_shotVtable && vt[15] == (void*)&PresentHook) return;
    if (vt[15] == (void*)&PresentHook) { g_shotVtable = vt; return; }
    DWORD old;
    if (!VirtualProtect(&vt[15], sizeof(void*), PAGE_READWRITE, &old)) return;
    g_origPresent = (PresentFn)vt[15];
    vt[15] = (void*)&PresentHook;
    VirtualProtect(&vt[15], sizeof(void*), old, &old);
    g_shotVtable = vt;
    Log("shot: Present hook installed (vtable %p)", vt);
}

static void ShotUpdate() {
    if (!cfg.debugLog || !g_shotTrigger[0] || GetTickCount64() < g_nextShotCheck) return;
    g_nextShotCheck = GetTickCount64() + 250;
    FILE* f = nullptr;
    if (fopen_s(&f, g_shotTrigger, "r") || !f) return;
    char path[MAX_PATH] = "";
    fgets(path, sizeof(path), f);
    fclose(f);
    DeleteFileA(g_shotTrigger);
    for (char* c = path; *c; ++c) if (*c == '\r' || *c == '\n') { *c = 0; break; }
    if (!path[0]) return;
    EnsurePresentHook();
    strcpy_s(g_shotPath, path);
}
