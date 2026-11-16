// Crash logger: records exceptions with registers, the module (exe, our version.dll or a
// system DLL) and offset of EIP, and the exe / Wololoader return addresses on the stack.
// Many exceptions are harmless (the game catches them: 5 read faults at every lobby load), so
// each EIP is logged at most twice and 60 in total; anything still unhandled is logged again by
// the unhandled-exception filter as FATAL, which is what ends the game.
#include "wololoader.h"
#include <stdio.h>
#include <string.h>

#define EXE_LO 0x401000
#define EXE_HI 0x9ac000

static volatile LONG g_logged;
static DWORD g_seen_eip[64];
static int g_seen_cnt[64];
static DWORD g_dll_lo, g_dll_hi;      // our own module

// "AoK HD.exe+1c8b50", "version.dll+2a10", "KERNELBASE.dll+11f3e"
static void where(DWORD a, char *out, size_t cap) {
    HMODULE m = NULL;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(DWORD_PTR)a, &m) && m) {
        GetModuleFileNameA(m, path, sizeof path);
        const char *b = strrchr(path, '\\');
        snprintf(out, cap, "%s+%lx", b ? b + 1 : path, a - (DWORD)(DWORD_PTR)m);
    } else snprintf(out, cap, "?");
}

static void log_exception(const char *tag, EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    char mod[MAX_PATH + 16];
    where(c->Eip, mod, sizeof mod);
    wl_log("%s %08lx at eip=%08lx %s (%s %08lx)", tag, er->ExceptionCode, c->Eip, mod,
           er->NumberParameters > 1 ? (er->ExceptionInformation[0] ? "write" : "read") : "",
           er->NumberParameters > 1 ? (DWORD)er->ExceptionInformation[1] : 0);
    wl_log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    DWORD *sp = (DWORD *)c->Esp;
    int found = 0;
    for (int i = 0; i < 2048 && found < 24; i++) {
        if (IsBadReadPtr(sp + i, 4)) break;
        DWORD v = sp[i];
        if (v >= EXE_LO && v < EXE_HI) { wl_log("  stack+%04x: %08lx", i * 4, v); found++; }
        else if (v >= g_dll_lo && v < g_dll_hi) { wl_log("  stack+%04x: %08lx version.dll+%lx", i * 4, v, v - g_dll_lo); found++; }
    }
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // only faults: C++ throws (e06d7363), debug prints, thread names etc. are normal
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != 0xc0000374 /* heap corruption */)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD eip = ep->ContextRecord->Eip;
    int i;
    for (i = 0; i < 64 && g_seen_eip[i] && g_seen_eip[i] != eip; i++) {}
    if (i < 64) { g_seen_eip[i] = eip; if (++g_seen_cnt[i] > 2) return EXCEPTION_CONTINUE_SEARCH; }
    if (InterlockedIncrement(&g_logged) > 60) return EXCEPTION_CONTINUE_SEARCH;
    log_exception("EXCEPTION", ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Reached only when nothing caught the exception: the game is about to close.
static LONG WINAPI on_unhandled(EXCEPTION_POINTERS *ep) {
    log_exception("FATAL (game closes)", ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void wl_install_crash_logger(void) {
    HMODULE self = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)wl_install_crash_logger, &self) && self) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)self + ((IMAGE_DOS_HEADER *)self)->e_lfanew);
        g_dll_lo = (DWORD)(DWORD_PTR)self;
        g_dll_hi = g_dll_lo + nt->OptionalHeader.SizeOfImage;
    }
    AddVectoredExceptionHandler(1, on_exception);
    SetUnhandledExceptionFilter(on_unhandled);
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
