// DeviceProfile=1 (debug): samples the game thread's call stack every 10 ms while it is inside CreateDevice (0x61DBC0)
// and the post-create restore calls (0x6125F0 steps), then logs the most frequent stacks as module!export+offset.
// Exports only (no PDB downloads); system DLLs keep frame pointers, so an EBP chain walk is enough. Nothing is
// allocated while the game thread is suspended (it may hold the heap lock); symbols are resolved afterwards.
#pragma once
#include <dbghelp.h>
#include <map>
#include <string>
#pragma comment(lib, "dbghelp.lib")

static int g_deviceProfile = 0;

namespace prof {
    enum { MaxSamples = 2048, MaxFrames = 24 };
    struct Sample { BYTE phase; BYTE n; DWORD pc[MaxFrames]; };
    static Sample g_samples[MaxSamples];
    static volatile LONG g_count = 0;
    static volatile LONG g_phase = 0;          // 0 idle, 1 CreateDevice, 2 restore
    static HANDLE g_game = nullptr, g_thread = nullptr, g_wake = nullptr;
    static bool g_symInit = false;

    static bool Readable(DWORD a) {
        MEMORY_BASIC_INFORMATION mbi;
        return a && VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
               !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    }

    static void TakeSample(int phase) {
        if (g_count >= MaxSamples) return;
        if (SuspendThread(g_game) == (DWORD)-1) return;
        CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(g_game, &c)) {
            Sample& s = g_samples[g_count];
            s.phase = (BYTE)phase; s.n = 0;
            s.pc[s.n++] = c.Eip;
            DWORD ebp = c.Ebp, lo = c.Esp;
            while (s.n < MaxFrames && ebp >= lo && (ebp & 3) == 0 && Readable(ebp) && Readable(ebp + 4)) {
                DWORD next = *(DWORD*)ebp, ret = *(DWORD*)(ebp + 4);
                if (!ret) break;
                s.pc[s.n++] = ret;
                if (next <= ebp) break;
                ebp = next;
            }
            ++g_count;
        }
        ResumeThread(g_game);
    }

    static std::string Name(DWORD a) {
        char buf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* si = (SYMBOL_INFO*)buf; si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
        char mod[MAX_PATH] = "?";
        HMODULE hm = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &hm)) {
            GetModuleFileNameA(hm, mod, MAX_PATH);
            char* s = strrchr(mod, '\\'); if (s) memmove(mod, s + 1, strlen(s));
        }
        char out[512];
        DWORD64 disp = 0;
        if (hm == GetModuleHandleA(nullptr)) sprintf_s(out, "exe+0x%lX", a);
        else if (g_symInit && SymFromAddr(GetCurrentProcess(), a, &disp, si) && disp < 0x10000) sprintf_s(out, "%s!%s+0x%llX", mod, si->Name, disp);
        else sprintf_s(out, "%s+0x%lX", mod, hm ? a - (DWORD)hm : a);
        return out;
    }

    static void Report(int from, int to) {
        if (!g_symInit) { SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS); g_symInit = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE; }
        else SymRefreshModuleList(GetCurrentProcess());
        static const char* const phases[] = { "", "CreateDevice", "restore" };
        std::map<std::string, int> leaf[3], stacks[3];
        int total[3] = {};
        for (int i = from; i < to; ++i) {
            const Sample& s = g_samples[i];
            ++total[s.phase];
            ++leaf[s.phase][Name(s.pc[0])];
            std::string st;
            for (int f = 0; f < s.n && f < 14; ++f) { if (f) st += " < "; st += Name(s.pc[f]); }
            ++stacks[s.phase][st];
        }
        for (int p = 1; p <= 2; ++p) {
            if (!total[p]) continue;
            Log("profile %s: %d samples", phases[p], total[p]);
            std::multimap<int, std::string, std::greater<int>> byCount;
            for (auto& kv : leaf[p]) byCount.insert({ kv.second, kv.first });
            int k = 0;
            for (auto& kv : byCount) { if (++k > 10) break; Log("  leaf %4d  %s", kv.first, kv.second.c_str()); }
            byCount.clear();
            for (auto& kv : stacks[p]) byCount.insert({ kv.second, kv.first });
            k = 0;
            for (auto& kv : byCount) { if (++k > 6) break; Log("  stack %4d  %s", kv.first, kv.second.c_str()); }
        }
    }

    // Reports after 2 s without a phase, so CreateDevice and the restore right after it are both sampled before
    // the (slow, first-time) symbol load.
    static DWORD WINAPI Thread(LPVOID) {
        LONG reported = 0;
        for (;;) {
            if (WaitForSingleObject(g_wake, 2000) == WAIT_OBJECT_0) {
                int phase;
                while ((phase = g_phase) != 0) { TakeSample(phase); Sleep(10); }
            } else if (g_count > reported) {
                LONG to = g_count;
                Report(reported, to);
                reported = to;
            }
        }
    }
}

static void DeviceProfilePhase(int phase) {        // called on the game thread; 0 ends a phase
    if (!g_deviceProfile) return;
    if (!prof::g_thread) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &prof::g_game,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        prof::g_wake = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        prof::g_thread = CreateThread(nullptr, 0, &prof::Thread, nullptr, 0, nullptr);
        SetThreadPriority(prof::g_thread, THREAD_PRIORITY_HIGHEST);
    }
    prof::g_phase = phase;
    if (phase) SetEvent(prof::g_wake);
}
