# Selection Limit Analysis: 60 → 100 Units
**Age of Empires II HD 5.8** (AoK HD.exe, 32-bit, base 0x400000)  
**Date:** 2026-11-14  
**Status:** ❌ **NOT SAFE TO IMPLEMENT** with current methodology

---

## Executive Summary

After analyzing the existing research in `re/selection/`, I conclude that **no static-analysis-based strategy can guarantee completeness** for raising the selection limit from 60 to 100 units. The fundamental problem is that the selection array is an inline struct member, and static analysis cannot prove it has found every access site.

**Evidence of incompleteness:**
- The automated scan (`re/scripts/selscan.py`) found 34 functions that touch both the count (`+0x254`) AND the array range (`0x15c-0x250`)
- However, the UI panel draw function `0x716360` accesses `player+0x160` and `player+0x24c` WITHOUT touching `+0x254`, so the scan **missed it**
- A naive `objdump` search found **811 instructions** accessing offsets `0x160-0x24f` in the entire `.text` section
- The research explicitly states: "10,274 instructions in .text use displacements 0x160..0x24f, so a complete static list could not be guaranteed"

**Risk:** One missed access = memory corruption, wrong unit orders, or random crashes.

---

## Problem Structure

### Memory Layout
```
WorldPlayer object (this):
  +0x15c: RGE_Static_Object* selected_unit_primary
  +0x160: RGE_Static_Object* selected[60]     // 240 bytes (0x160-0x24f)
  +0x250: DWORD selection_capability_flags
  +0x254: int selected_unit_count
  +0x258: ... other player fields ...
```

The selection array is **inline** (embedded directly in the struct), not a pointer to external memory. To expand from 60 to 100 units (240 → 400 bytes), we would need to:
1. Relocate the array to external memory
2. Redirect **every** access from `player+0x160+i*4` to `g_selection[player_id][i]`
3. Miss **zero** access sites (even one missed access corrupts memory)

### Access Patterns Found

The research identified multiple access patterns:

1. **Core selection functions (4 functions):**
   - `0x754a20` - `select_unit()`: adds unit, increments count
   - `0x755450` - `unselect_all()`: clears array and count
   - `0x7554a0` - `unselect_at_index()`: shifts array down
   - `0x755540` - `unselect_object()`: finds unit, calls unselect_at_index

2. **Direct array access (examples):**
   - `0x716360` UI panel draw: loops `0x160-0x24c` to draw unit icons
   - `0x6663f0` gameplay logic: `mov eax,[eax+esi*1]` where `esi=0x160+offset`
   - `0x73dc60` capability calculator: `lea ebx,[edx+0x160]`, then `mov ecx,[ebx]; add ebx,4`

3. **Array constants:**
   - Loop bounds: `cmp ecx,0x3c` (60 in decimal)
   - Array bounds: `cmp edi,0x250` (end of array = 0x160+0xf0)
   - Shift loop: `cmp ecx,0x3b` (59, for moving elements down)

### Known Limit Sites (from plan_agy.md)

The research documented these 60-limit sites:

**Selection core (5 sites):**
- `0x754b23`: `cmp eax,0x3c` - select_unit count check
- `0x754bb0`: `cmp [esi+0x254],0x3c` - secondary check
- `0x755722`: `cmp edx,0x3c` - unselect_all loop
- `0x755797`: `cmp ecx,0x3b` - array shift loop (59)
- `0x71645f`: `cmp ecx,0x3c` - UI panel draw limit

**Drag selection (3 sites):**
- `0x87d07d`: `cmp edi,0xde0704` - global buffer end (0xde0430+720)
- `0x87d4fb`: `cmp edx,0xde0704` - same buffer check
- `0x87d57c`: `mov eax,0x3c` - max units to add

**Command system (13+ sites):**
- `0x74f085`: `cmp eax,0x3c` - copy selected to command buffer
- `0x80e6c8`: `mov ecx,0x3c` - clear last_commanded_units
- `0x80a49c`: `cmp [0xafb50c],0x3c` - move command
- `0x80a99c`: `cmp [0xafb50c],0x3c` - attack command
- ... (10 more command handlers)

**Total documented:** ~25 addresses  
**Total instructions touching 0x160-0x24f:** 811  
**Provable coverage:** ~3%

---

## Why Static Analysis Fails

### 1. Indirect Access Patterns

The selection array is accessed through:
- Direct offsets: `mov eax,[esi+0x160]`
- Scaled indices: `mov eax,[esi+edi*4+0x160]` where `edi` is the index
- Base pointer: `lea ebx,[edx+0x160]` then `mov ecx,[ebx]` with `add ebx,4`
- Computed addresses: offset calculated elsewhere, stored in register

**Problem:** Recognizing all these patterns requires understanding the semantics of every function, which is impossible without complete decompilation and data flow analysis.

### 2. The UI Panel Gap

The scan script (`selscan.py`) searches for functions that access:
- `0x254` (count) AND
- Something in `0x15c-0x250` (primary or array)

**But** `0x716360` (UI panel draw) accesses:
- `0x7163d8`: `mov esi,[eax+0x254]` ✓ (reads count)
- `0x716534`: `mov eax,0x24c` ❌ (uses immediate, not `[player+0x24c]`)
- `0x716585`: `mov edi,[ecx+eax*1]` (indirect access via `eax=0x24c`)

The scan looks for `[reg+0x24c]`, but the code uses `mov eax,0x24c` then `[reg+eax]`, so it was **missed**.

**Implication:** The scan methodology has a false negative rate > 0. We don't know how many other functions use similar patterns.

### 3. Compiler Optimizations

The game was compiled with `i686-w64-mingw32-g++` with optimizations. The compiler may:
- Hoist offset calculations out of loops
- Strength-reduce `[player+i*4+0x160]` to `[ptr+i*4]` where `ptr` was pre-computed
- Inline functions that access the array
- Use SSE instructions that load multiple array entries at once

**Problem:** Each optimization creates a new access pattern that the scan might miss.

---

## Evaluated Strategies

### Strategy A: Patch All Known Sites (REJECTED)
**Approach:** Patch the 25 documented addresses + any others we can find manually.

**Why rejected:**
- Only 3% coverage proven
- The UI panel miss proves the scan is incomplete
- One missed site = memory corruption
- Risk level: **UNACCEPTABLE**

### Strategy B: Hook Core Functions Only (REJECTED)
**Approach:** Hook the 4 core functions (`select_unit`, `unselect_all`, etc.) to redirect array access.

**Why rejected:**
- Doesn't help with direct array reads (e.g., UI panel, command copy)
- The research shows at least 30+ functions read the array directly
- Still leaves 97% of access sites unpatched
- Risk level: **UNACCEPTABLE**

### Strategy C: Runtime Instrumentation (FEASIBLE but complex)
**Approach:**
1. Use Windows Vectored Exception Handler (VEH)
2. Allocate player structs in memory pages with `PAGE_GUARD`
3. When ANY code touches `player+0x160-0x24f`, the VEH fires
4. Log the instruction address
5. After comprehensive testing, compile the complete list
6. Remove instrumentation, apply patches

**Pros:**
- Guaranteed to find all access sites actually executed
- Can be tested exhaustively before going live

**Cons:**
- Complex implementation (VEH, page protection, instruction decode)
- Requires extensive in-game testing of every scenario:
  - Single/multi unit selection
  - Drag box, shift-click, double-click, ctrl+A
  - All unit types, all commands (move, attack, patrol, garrison, etc.)
  - Control groups, idle villager hotkey, find all military
  - UI panel interactions, minimap, replays, saved games
- Performance impact during discovery (every array access traps)
- Still might miss rarely-executed code paths

**Feasibility:** Possible but requires weeks of work and testing.

### Strategy D: Player Struct Size Expansion (REJECTED)
**Approach:**
1. Find player class constructor and allocation site
2. Hook `operator new` to allocate extra space
3. Move array to the end of the struct
4. Patch member functions only

**Why rejected:**
- Still requires finding all access sites (member functions + non-member code)
- Player allocation might be `malloc(sizeof(WorldPlayer))` with a compile-time size
- Other code might assume struct size (e.g., array of players, serialization)
- Doesn't solve the completeness problem
- Risk level: **UNACCEPTABLE**

### Strategy E: Accept Current Limit (RECOMMENDED)
**Approach:** Leave the selection limit at 60.

**Rationale:**
- 60 units is already generous (original AoC had 40)
- The game's pathfinding, formations, and UI were designed for ≤60
- Raising the limit provides marginal gameplay benefit vs. high crash risk
- Other mods (pop cap 1000, custom civs) are safer and more impactful

---

## The Only Safe Path: Runtime Instrumentation

If raising the selection limit is a hard requirement, the **only safe strategy** is:

### Phase 1: Discovery (2-4 weeks)
1. Implement VEH-based page guard on player objects
2. Log every instruction that touches `player+0x15c-0x254`
3. Comprehensive in-game testing:
   - All unit types (land, water, air, siege)
   - All commands (26+ command types)
   - All selection methods (click, drag, shift, ctrl, hotkeys)
   - Edge cases (200 pop game, all unit types selected, replays)
4. Compile the complete instruction address list

### Phase 2: Validation (1 week)
1. For each logged address, verify in disassembly:
   - Prologue bytes (for hooking)
   - Calling convention
   - Is it a direct access or indirect?
   - What's the access pattern?
2. Categorize into:
   - Hookable functions
   - Direct patches (constant changes)
   - Complex cases (needs custom hook)

### Phase 3: Implementation (1 week)
1. Allocate external selection arrays `g_selection[9][100]`
2. Hook or patch each discovered site
3. Add bounds checking to all hooks
4. Add telemetry (log any out-of-bounds access)

### Phase 4: Testing (2 weeks)
1. Disable instrumentation, enable telemetry
2. Repeat all Phase 1 tests
3. Monitor for any logged bounds violations (= missed site)
4. If violations found, return to Phase 1

**Total effort:** 6-8 weeks  
**Risk after completion:** Low (but not zero - rare code paths might still be missed)

---

## Residual Risks (Even with Runtime Instrumentation)

### 1. Multiplayer Synchronization
- All players need identical Wololoader version
- Packet format includes unit count (1 byte, max 255, so 100 is OK)
- Packet size: 8 bytes header + 100×4 bytes IDs = 408 bytes (fits in one UDP packet)
- **Risk:** Vanilla players or different Wololoader versions will desync

### 2. Saved Games and Replays
- Savegame format might serialize the array size
- Replays might have fixed packet expectations
- **Risk:** Loading old saves or replays might crash

### 3. Rarely-Executed Code
- Error handlers, debug code, cheat commands
- Code that only executes under specific conditions (e.g., 8-player FFA with all Hun with 200 pop)
- **Risk:** Crash in rare scenarios even after extensive testing

### 4. Performance
- 100-unit formations, pathfinding, and combat calculations
- The game's AI unit-gathering has 500-entry stack arrays (`0x52c640`) - raising selection to 100 is fine, but shows the game was not designed for large selections
- **Risk:** Frame drops, input lag, or pathfinding failures with 100-unit groups

### 5. UI Panel Space
- The UI panel at 0x716360 draws unit icons in a grid
- Current layout has space for 60 icons
- **Risk:** 100 icons might overflow the panel or require scrolling (needs UI redesign)

---

## Recommendation

**DO NOT IMPLEMENT** selection limit increase with current methodology.

**Path forward:**
1. If this feature is critical: allocate 6-8 weeks for the runtime instrumentation approach
2. If this feature is nice-to-have: deprioritize in favor of safer, higher-impact features
3. Document this analysis for future reference

**Safer alternatives with similar player value:**
- ✅ Pop cap 1000 (already implemented, safe, tested)
- ✅ Custom civilizations (already implemented, safe, tested)
- ⚠️ Unique units (partially done, 2 open issues, low risk)
- ⚠️ Faster unit production (would need rate limit patches, medium risk)
- ❌ Selection limit 100 (high risk, requires instrumentation)

---

## Conclusion

The selection limit increase from 60 to 100 is **technically possible** but **cannot be proven safe** using static analysis alone. The existing research correctly identified this as the blocker and put the feature on hold.

The only path to safety is **runtime instrumentation** with comprehensive testing, which would require 6-8 weeks of dedicated effort. Without this investment, any implementation would be shipping with a 97% unknown codebase interaction surface and accepting a high probability of memory corruption and crashes.

**Status:** ❌ **On hold** - requires runtime instrumentation for safe implementation  
**Risk level:** 🔴 **HIGH** without instrumentation  
**Effort required:** 6-8 weeks for safe implementation

---

## Appendix: What selection.c Would Look Like (NOT IMPLEMENTED)

For reference, if we had a complete access list, `selection.c` would follow this structure:

```c
// wololoader/src/selection.c - NOT IMPLEMENTED, UNSAFE
#include "wololoader.h"

#define SEL_MAX 100
#define SEL_OLD 60

// External storage: 9 players × 100 unit pointers
static void* g_selection[9][SEL_MAX];
static int g_sel_count[9];

// Hook 0x754a20 select_unit to redirect array writes
// Hook 0x755450 unselect_all to use g_selection
// Hook 0x7554a0 unselect_at_index to shift g_selection
// Hook 0x755540 unselect_object
// Hook 0x716360 UI panel draw (redirect array reads)
// ... + 806 more sites

void wl_install_selection(void) {
    // This would require 811+ patches/hooks
    // Each one must be verified for:
    // - Original bytes
    // - Calling convention
    // - Register usage
    // - Side effects
    
    // Example (DO NOT USE):
    if (!expect(0x754b23, "\x83\xf8\x3c", 3)) return;
    wl_write_byte(0x754b25, 0x64); // 0x3c -> 0x64 (100)
    
    // ... 810 more patches ...
}
```

This is why it cannot be implemented safely.
