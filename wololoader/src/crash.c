// Crash logger: records access violations raised inside AoK HD.exe code with
// registers and the exe return addresses found on the stack.
#include "wololoader.h"
#include <stdio.h>

#define EXE_LO 0x401000
#define EXE_HI 0x9ac000

static volatile LONG g_logged;

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
        er->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION &&
        er->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_logged) > 5) return EXCEPTION_CONTINUE_SEARCH;

    wl_log("EXCEPTION %08lx at eip=%08lx (%s %08lx)", er->ExceptionCode, c->Eip,
           er->NumberParameters > 1 ? (er->ExceptionInformation[0] ? "write" : "read") : "",
           er->NumberParameters > 1 ? (DWORD)er->ExceptionInformation[1] : 0);
    wl_log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    DWORD *sp = (DWORD *)c->Esp;
    int found = 0;
    for (int i = 0; i < 2048 && found < 24; i++) {
        DWORD v;
        if (IsBadReadPtr(sp + i, 4)) break;
        v = sp[i];
        if (v >= EXE_LO && v < EXE_HI) { wl_log("  stack+%04x: %08lx", i * 4, v); found++; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void wl_install_crash_logger(void) {
    AddVectoredExceptionHandler(1, on_exception);
}

// ---- Hang watchdog ---------------------------------------------------------
// A freeze (infinite loop) raises no exception, so the logger above sees
// nothing. This thread pings the game's main window every second; when it does
// not answer for 3 s, the main thread is sampled 10 times (EIP + exe return
// addresses on its stack) and written to the log. Map the addresses with
// re/out/functions.tsv. Logged once per hang (re-armed when the window answers).
static HANDLE g_main_thread;
static DWORD g_main_tid;

static BOOL CALLBACK find_main_window(HWND w, LPARAM out) {
    DWORD pid;
    DWORD tid = GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && tid == g_main_tid && IsWindowVisible(w)) {
        *(HWND *)out = w;
        return FALSE;
    }
    return TRUE;
}

static void sample_main_thread(int n) {
    CONTEXT c;
    ZeroMemory(&c, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(g_main_thread) == (DWORD)-1) return;
    BOOL ok = GetThreadContext(g_main_thread, &c);
    DWORD stack[48];
    int found = 0;
    if (ok) {
        DWORD *sp = (DWORD *)c.Esp;
        for (int i = 0; i < 1024 && found < 48; i++) {
            if (IsBadReadPtr(sp + i, 4)) break;
            if (sp[i] >= EXE_LO && sp[i] < EXE_HI) stack[found++] = sp[i];
        }
    }
    ResumeThread(g_main_thread);   // resume before logging (log takes locks)
    if (!ok) return;
    char line[600];
    int len = snprintf(line, sizeof line, "HANG sample %d: eip=%08lx esi=%08lx edi=%08lx ebx=%08lx stack:",
                       n, c.Eip, c.Esi, c.Edi, c.Ebx);
    for (int i = 0; i < found && i < 24 && len < (int)sizeof line - 10; i++)
        len += snprintf(line + len, sizeof line - len, " %08lx", stack[i]);
    wl_log("%s", line);
}

static DWORD WINAPI watchdog_thread(LPVOID arg) {
    (void)arg;
    BOOL reported = FALSE;
    for (;;) {
        Sleep(1000);
        HWND w = NULL;
        EnumWindows(find_main_window, (LPARAM)&w);
        if (!w) continue;
        DWORD_PTR res;
        if (SendMessageTimeoutA(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &res)) {
            reported = FALSE;
            continue;
        }
        if (reported) continue;
        reported = TRUE;
        wl_log("HANG: main window did not answer for 3 s, sampling main thread");
        for (int i = 0; i < 10; i++) { sample_main_thread(i); Sleep(100); }
    }
    return 0;
}

// Called from DllMain, which runs on the game's main thread at load time.
void wl_install_hang_watchdog(void) {
    g_main_tid = GetCurrentThreadId();
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
        return;
    HANDLE t = CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
