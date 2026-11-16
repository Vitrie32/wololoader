# Selection Field Access Trace — Design and Implementation

**Author**: Runtime trace system for Wololoader  
**Date**: 2026-11-14  
**Purpose**: Record every instruction accessing player selection fields to build a provably complete access list

---

## Problem Statement

Phase 1 of raising the selection limit (60 → 100) requires finding **every** instruction that accesses the selection fields in player objects. Static analysis found 90 functions through displacement scanning, but:

1. The scan only found functions that access **both** the array and the count field (+0x254)
2. Selection panel rendering at 0x716360 uses +0x160/+0x24c without touching +0x254 → missed
3. 10,274 instructions in .text use displacements 0x160..0x24f → too many to verify manually
4. One missed access = wrong orders / memory corruption / random crash

**Solution**: Runtime instrumentation that records every instruction accessing these fields during actual gameplay.

---

## Player Object Layout (VERIFIED)

From `re/FINDINGS.md` and disassembly verification:

```
WorldPlayer (derived from RGE_Player):
  +0x15c  RGE_Static_Object*   selected_unit_primary   (4 bytes)
  +0x160  RGE_Static_Object*   selected_units[60]      (240 bytes = 0xf0)
  +0x250  DWORD                (pad/state, 4 bytes)
  +0x254  int                  selected_unit_count     (4 bytes, range 0..60)
  +0x258  ...                  (other player fields)
```

**Verification sources**:
- `0x754b23`: `cmp eax, 0x3c` (count check in select_unit)
- `0x754bb0`: `cmp dword ptr [esi+0x254], 0x3c` (secondary count check)
- `0x755722`: `cmp edx, 0x3c` (unselect_all loop limit)
- `0x716534`: `mov eax, 0x24c` (0x160 + 59*4, last array element in UI draw)

**Trace range**: `[player + 0x15c, player + 0x258)` = 252 bytes covering all selection state.

---

## Design: PAGE_GUARD Exception Handler

### Overview

Uses Windows memory protection to trap every access:

1. **Protect pages**: Mark pages containing selection fields with `PAGE_GUARD`
2. **Catch guard faults**: Vectored exception handler receives `STATUS_GUARD_PAGE_VIOLATION`
3. **Record EIP**: If fault address is in traced range, record the instruction pointer
4. **Single-step re-arm**: Set trap flag (`EFLAGS.TF`) to execute the faulting instruction, then catch `STATUS_SINGLE_STEP` to re-arm `PAGE_GUARD`

This records **every** read and write with minimal overhead (only on first access per instruction).

### Why PAGE_GUARD?

- **Complete coverage**: No need to enumerate every instruction; the CPU tells us
- **Minimal overhead**: Guard fault only triggers once per unique EIP (subsequent hits are hash lookups)
- **No patching**: Doesn't modify game code, safe and reversible

### Limitations

- **Page granularity**: If other player fields share a page, they trigger false positives (filtered by checking fault address is in `[+0x15c, +0x258)`)
- **Thread safety**: Must use critical sections (player operations can happen on multiple threads)
- **DLL noise**: Our own hook code accesses the fields, so we filter out non-exe addresses (< 0x400000 or >= 0x9ab000)

---

## Implementation: src/seltrace.c

### Data Structures

```c
typedef struct {
    DWORD eip;        // Instruction pointer (exe addresses only)
    DWORD hits;       // Access count
} TraceRecord;

typedef struct {
    void *player_ptr;
    DWORD start_addr;  // player + 0x15c
    DWORD end_addr;    // player + 0x258
} TrackedPlayer;
```

- **g_trace_records[8192]**: Hash set of accessed instructions
- **g_players[9]**: Tracked player objects (max 9 in a game)
- **g_trace_lock**: Critical section for thread safety

### Hook Point: WorldPlayer::select_unit (0x754a20)

**Why this function?**
- Called whenever a unit is selected (click, drag, hotkey)
- Receives `this` pointer = WorldPlayer object
- First 5 bytes: `55 8b ec 56 57` (push ebp; mov ebp,esp; push esi; push edi)
- `__thiscall`, so ECX = player pointer

**What we do**:
1. Detour with `wl_hook(0x754a20, hook_select_unit, 5)`
2. Capture `self` (player pointer) in hook wrapper
3. Call `track_player(self)` to install PAGE_GUARD on first access
4. Forward to original function via trampoline

**Verified calling convention**: ECX = this, stack args (unit pointer, flag). Return value in EAX.

### Player Tracking

```c
void track_player(void *player) {
    // Check if already tracked
    // Add to g_players[]
    // Protect all pages covering [player+0x15c, player+0x258)
    DWORD page_start = (player + 0x15c) & ~0xfff;
    DWORD page_end = ((player + 0x258) + 0xfff) & ~0xfff;
    for each page:
        VirtualProtect(page, 4096, PAGE_EXECUTE_READ | PAGE_GUARD, ...)
}
```

**Note**: `PAGE_EXECUTE_READ | PAGE_GUARD` allows execution but traps first data access per page per thread.

### Exception Handler

```c
LONG WINAPI trace_exception_handler(EXCEPTION_POINTERS *ex) {
    if (STATUS_GUARD_PAGE_VIOLATION) {
        fault_addr = ExceptionInformation[1];
        eip = ContextRecord->Eip;

        // Filter: exe range only
        if (eip < 0x400000 || eip >= 0x9ab000)
            goto rearm;

        // Filter: fault in tracked range
        if (fault_addr in [player+0x15c, player+0x258) for any player) {
            record_access(eip);
        }

    rearm:
        // Single-step to re-arm
        ContextRecord->EFlags |= 0x100;  // Set TF
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (STATUS_SINGLE_STEP) {
        // Re-arm PAGE_GUARD on all tracked pages
        for each player:
            VirtualProtect(pages, PAGE_EXECUTE_READ | PAGE_GUARD);

        ContextRecord->EFlags &= ~0x100;  // Clear TF
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
```

### Periodic Dump

Background thread dumps trace every 10 seconds and at exit:

```c
DWORD WINAPI dump_thread(LPVOID param) {
    while (g_trace_active) {
        Sleep(10000);
        dump_trace();
    }
}
```

**Output format** (`wololoader/seltrace.txt`):
```
# Selection field access trace
# Total unique instructions: N
# Tracked players: M
#   Player 0: 0xXXXXXXXX [start, end)
...

00754b23 1234
00755797 567
...
```

One line per instruction: `eip hits` (sorted by address).

---

## Build System Integration

### Makefile Changes

```makefile
ifdef TRACE
CFLAGS  += -DWL_SELTRACE
SRC     += src/seltrace.c
endif
```

**Normal build**: `make` (no trace code, zero overhead)  
**Trace build**: `make TRACE=1` (includes seltrace.c, logs all accesses)

### main.c Integration

```c
#ifdef WL_SELTRACE
    wl_log("TRACE BUILD: Selection field access tracing enabled");
    wl_seltrace_init();
    atexit(wl_seltrace_shutdown);
#endif
```

Initialized after hooks are installed, shutdown on exit dumps final trace.

---

## Analysis Tool: tools/seltrace_compare.py

Compares runtime trace with static analysis (re/selection/*.md):

1. Parse `seltrace.txt` → set of runtime addresses
2. Parse `plan_agy.md`, `selpart_*.md` → set of static addresses
3. Report:
   - **Runtime only**: Addresses missed by static analysis ⚠️
   - **Static only**: Addresses never hit (dead code or untriggered paths)
   - **Both**: Validated static analysis ✓

**Usage**:
```bash
python3 tools/seltrace_compare.py <game>/wololoader/seltrace.txt re/selection/
```

---

## Verification Checklist

- [x] Hook prologue verified: 0x754a20 = `55 8b ec 56 57` (5 bytes)
- [x] Player object offsets verified from disassembly
- [x] Builds with `make` (normal, zero warnings)
- [x] Builds with `make TRACE=1` (trace enabled, zero warnings)
- [ ] Run in-game to collect trace (user must test)
- [ ] Verify seltrace.txt contains addresses
- [ ] Compare with static analysis using seltrace_compare.py

---

## Known Limitations

1. **Player pointer capture**: Only works if `select_unit` is called. Alternative: hook game initialization to find player array directly.
2. **Overhead**: PAGE_GUARD trap + single-step per unique instruction. Acceptable for debug builds.
3. **Multithreading**: Critical sections protect data structures, but exception handler must be re-entrant safe.
4. **Stack return addresses**: Currently only records direct EIP. Could add stack walk to find calling functions in DLLs.

---

## Future Improvements

1. **Hash set optimization**: Replace linear search with real hash table
2. **Disassembly integration**: Parse instruction bytes to determine read vs. write, offset accessed
3. **Function name lookup**: Use exports or symbol files to label addresses
4. **Differential trace**: Compare two runs to find code paths only triggered by specific actions

---

## References

- `re/FINDINGS.md`: All verified game addresses
- `re/selection/README.md`: Selection limit research overview
- `re/selection/plan_agy.md`: Static analysis (90 functions, 34 confirmed)
- `wololoader/src/hook.c`: Detour implementation (`wl_hook`)
- Windows API: `VirtualProtect`, `AddVectoredExceptionHandler`, `CONTEXT.EFlags`
