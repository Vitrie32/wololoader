// Runtime trace for selection field accesses (0x15c-0x258 in player objects).
// Only compiled when WL_SELTRACE is defined (make TRACE=1).
#ifdef WL_SELTRACE

#include "wololoader.h"
#include <stdio.h>
#include <string.h>

#define MAX_TRACE_RECORDS 8192
#define MAX_PLAYERS 9

typedef struct {
    DWORD eip;
    DWORD hits;
} TraceRecord;

typedef struct {
    void *player_ptr;
    DWORD start_addr;  // player + 0x15c
    DWORD end_addr;    // player + 0x258
} TrackedPlayer;

static TraceRecord g_trace_records[MAX_TRACE_RECORDS];
static int g_trace_count = 0;
static TrackedPlayer g_players[MAX_PLAYERS];
static int g_player_count = 0;
static CRITICAL_SECTION g_trace_lock;
static BOOL g_trace_active = FALSE;
static HANDLE g_dump_timer = NULL;
static DWORD g_last_dump_tick = 0;

// Function pointers for hooked functions
typedef void *(__fastcall *SelectUnitFn)(void *self, void *edx, void *unit, int flag);
static SelectUnitFn orig_select_unit = NULL;

// Forward declarations
static void track_player(void *player);
static void record_access(DWORD eip);
static void dump_trace(void);
static LONG WINAPI trace_exception_handler(EXCEPTION_POINTERS *ex);
static void *__fastcall hook_select_unit(void *self, void *edx, void *unit, int flag);

// Timer thread for periodic dumps
static DWORD WINAPI dump_thread(LPVOID param) {
    (void)param;
    while (g_trace_active) {
        Sleep(10000);  // 10 seconds
        if (g_trace_active) {
            dump_trace();
        }
    }
    return 0;
}

// Initialize the trace system
void wl_seltrace_init(void) {
    InitializeCriticalSection(&g_trace_lock);
    memset(g_trace_records, 0, sizeof(g_trace_records));
    memset(g_players, 0, sizeof(g_players));

    // Install vectored exception handler (first in chain)
    AddVectoredExceptionHandler(1, trace_exception_handler);

    g_trace_active = TRUE;
    g_last_dump_tick = GetTickCount();
    wl_log("[SELTRACE] Runtime trace initialized");

    // Hook select_unit to capture player pointers
    // 0x754a20: push ebp; mov ebp,esp; push esi; push edi (5 bytes: 55 8b ec 56 57)
    orig_select_unit = wl_hook(0x754a20, hook_select_unit, 5);
    if (!orig_select_unit) {
        wl_log("[SELTRACE] Failed to hook select_unit");
    }

    // Start dump thread
    g_dump_timer = CreateThread(NULL, 0, dump_thread, NULL, 0, NULL);
}

// Shutdown and dump final trace
void wl_seltrace_shutdown(void) {
    if (!g_trace_active) return;
    g_trace_active = FALSE;

    // Wait for dump thread to finish
    if (g_dump_timer) {
        WaitForSingleObject(g_dump_timer, 2000);
        CloseHandle(g_dump_timer);
    }

    wl_log("[SELTRACE] Shutting down, dumping final trace");
    dump_trace();
    DeleteCriticalSection(&g_trace_lock);
}

// No longer needed - dump thread handles periodic dumps
void wl_seltrace_tick(void) {
    // Deprecated: dump_thread handles periodic dumps
}

// Hooked select_unit to capture player pointers
void *__fastcall hook_select_unit(void *self, void *edx, void *unit, int flag) {
    if (g_trace_active) {
        track_player(self);
    }
    return orig_select_unit(self, edx, unit, flag);
}

// Track a player object (install PAGE_GUARD protection)
static void track_player(void *player) {
    if (!player) return;

    EnterCriticalSection(&g_trace_lock);

    // Check if already tracked
    for (int i = 0; i < g_player_count; i++) {
        if (g_players[i].player_ptr == player) {
            LeaveCriticalSection(&g_trace_lock);
            return;
        }
    }

    if (g_player_count >= MAX_PLAYERS) {
        LeaveCriticalSection(&g_trace_lock);
        return;
    }

    // Add new player
    TrackedPlayer *tp = &g_players[g_player_count++];
    tp->player_ptr = player;
    tp->start_addr = (DWORD)player + 0x15c;
    tp->end_addr = (DWORD)player + 0x258;

    wl_log("[SELTRACE] Tracking player %d at %p, range [%08lx, %08lx)",
           g_player_count, player, tp->start_addr, tp->end_addr);

    // Protect pages containing the selection fields. Heap pages are
    // read-write: keep PAGE_READWRITE under the guard, otherwise the page
    // becomes read-only after the first hit and the next write crashes.
    DWORD page_start = tp->start_addr & ~0xfff;
    DWORD page_end = (tp->end_addr + 0xfff) & ~0xfff;
    for (DWORD page = page_start; page < page_end; page += 0x1000) {
        DWORD old_protect;
        if (VirtualProtect((void*)page, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old_protect)) {
            wl_log("[SELTRACE] Protected page %08lx", page);
        } else {
            wl_log("[SELTRACE] Failed to protect page %08lx (error %lu)", page, GetLastError());
        }
    }

    LeaveCriticalSection(&g_trace_lock);
}

// Record an instruction access (called from exception handler)
static void record_access(DWORD eip) {
    // Linear search (optimize to hash if needed)
    for (int i = 0; i < g_trace_count; i++) {
        if (g_trace_records[i].eip == eip) {
            g_trace_records[i].hits++;
            return;
        }
    }

    // New record
    if (g_trace_count < MAX_TRACE_RECORDS) {
        g_trace_records[g_trace_count].eip = eip;
        g_trace_records[g_trace_count].hits = 1;
        g_trace_count++;
    } else {
        static BOOL warned = FALSE;
        if (!warned) {
            wl_log("[SELTRACE] WARNING: trace buffer full, some accesses not recorded");
            warned = TRUE;
        }
    }
}

// Dump trace to file
static void dump_trace(void) {
    EnterCriticalSection(&g_trace_lock);

    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\seltrace.txt", g_wl.root);
    FILE *f = fopen(path, "w");
    if (!f) {
        wl_log("[SELTRACE] Failed to open %s", path);
        LeaveCriticalSection(&g_trace_lock);
        return;
    }

    fprintf(f, "# Selection field access trace\n");
    fprintf(f, "# Format: eip hits\n");
    fprintf(f, "# Total unique instructions: %d\n", g_trace_count);
    fprintf(f, "# Tracked players: %d\n", g_player_count);
    for (int i = 0; i < g_player_count; i++) {
        fprintf(f, "#   Player %d: %p [%08lx, %08lx)\n", i,
                g_players[i].player_ptr, g_players[i].start_addr, g_players[i].end_addr);
    }
    fprintf(f, "\n");

    // Sort by address for easier comparison
    for (int i = 0; i < g_trace_count - 1; i++) {
        for (int j = i + 1; j < g_trace_count; j++) {
            if (g_trace_records[j].eip < g_trace_records[i].eip) {
                TraceRecord tmp = g_trace_records[i];
                g_trace_records[i] = g_trace_records[j];
                g_trace_records[j] = tmp;
            }
        }
    }

    DWORD total_hits = 0;
    for (int i = 0; i < g_trace_count; i++) {
        fprintf(f, "%08lx %lu\n", g_trace_records[i].eip, g_trace_records[i].hits);
        total_hits += g_trace_records[i].hits;
    }

    fclose(f);
    wl_log("[SELTRACE] Dumped %d unique instructions (%lu total hits) to %s",
           g_trace_count, total_hits, path);

    LeaveCriticalSection(&g_trace_lock);
}

// Vectored exception handler
static LONG WINAPI trace_exception_handler(EXCEPTION_POINTERS *ex) {
    if (!g_trace_active) return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = ex->ExceptionRecord->ExceptionCode;

    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        // Get faulting address (ExceptionInformation[1] for memory access)
        DWORD fault_addr = (DWORD)ex->ExceptionRecord->ExceptionInformation[1];
        DWORD eip = ex->ContextRecord->Eip;

        // Only record if EIP is in exe range (0x400000 to 0x400000+0x5ab000)
        if (eip >= 0x400000 && eip < 0x9ab000) {
            EnterCriticalSection(&g_trace_lock);

            // Check if fault is in a tracked range
            BOOL in_range = FALSE;
            for (int i = 0; i < g_player_count; i++) {
                if (fault_addr >= g_players[i].start_addr && fault_addr < g_players[i].end_addr) {
                    in_range = TRUE;
                    break;
                }
            }

            if (in_range) {
                record_access(eip);
            }

            LeaveCriticalSection(&g_trace_lock);
        }

        // Set trap flag for single-step (to re-arm PAGE_GUARD)
        ex->ContextRecord->EFlags |= 0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (code == STATUS_SINGLE_STEP) {
        // Re-arm PAGE_GUARD on all tracked pages
        EnterCriticalSection(&g_trace_lock);

        for (int i = 0; i < g_player_count; i++) {
            DWORD page_start = g_players[i].start_addr & ~0xfff;
            DWORD page_end = (g_players[i].end_addr + 0xfff) & ~0xfff;
            for (DWORD page = page_start; page < page_end; page += 0x1000) {
                DWORD old_protect;
                VirtualProtect((void*)page, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old_protect);
            }
        }

        LeaveCriticalSection(&g_trace_lock);

        // Clear trap flag
        ex->ContextRecord->EFlags &= ~0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

#endif // WL_SELTRACE
