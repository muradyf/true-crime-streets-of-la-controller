// Standalone check of the DualSense reader: prints report id/length and parsed state.
// Usage: ds_test.exe [seconds]
#include "dualsense_hid.h"
#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t** argv) {
    int seconds = argc > 1 ? _wtoi(argv[1]) : 5;
    DualSense ds;
    if (!ds.Open()) { printf("DualSense not found\n"); return 1; }
    wprintf(L"opened: %s\n  bluetooth=%d featureLen=%u modeSwitch=%d error=%lu\n", ds.Path().c_str(),
            ds.IsBluetooth(), ds.FeatureLength(), ds.FeatureResult(), ds.FeatureError());
    PadState st;
    ULONGLONG end = GetTickCount64() + seconds * 1000ULL, nextPrint = 0;
    while (GetTickCount64() < end) {
        if (!ds.Poll(st)) { printf("device lost\n"); return 2; }
        if (GetTickCount64() >= nextPrint) {
            nextPrint = GetTickCount64() + 500;
            printf("id=0x%02X len=%d bt=%d | L(%3u,%3u) R(%3u,%3u) L2=%3u R2=%3u hat=%u | sq=%d x=%d o=%d tri=%d L1=%d R1=%d L2b=%d R2b=%d cr=%d opt=%d L3=%d R3=%d ps=%d tp=%d\n",
                   st.reportId, st.reportLen, st.bluetooth, st.lx, st.ly, st.rx, st.ry, st.l2, st.r2, st.hat,
                   st.square, st.cross, st.circle, st.triangle, st.l1, st.r1, st.l2b, st.r2b,
                   st.create, st.options, st.l3, st.r3, st.ps, st.touchpad);
        }
        Sleep(5);
    }
    return 0;
}
