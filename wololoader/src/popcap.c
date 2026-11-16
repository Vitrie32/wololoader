// Population cap 1000: adds a "1000" entry to the population dropdowns and
// lifts the two places where the game clamps the population cap to 500.
// Only takes effect when a player picks 1000; every other value behaves exactly
// as before. All players of a multiplayer game need the same Wololoader.
//
// Verified in the disassembly (2026-11-14, see re/FINDINGS.md "Population limit"):
//  - 0x47b690  dropdown AddItem: __thiscall(list, const char *text, int value), ret 8.
//  - 0x64d337  game setup screen: `call 0x47b690` that adds "500" (last entry;
//              the list is a 25..200 loop followed by 250, 300, 400, 500).
//  - 0x6da131  lobby browser filter: same `call 0x47b690` for "500".
//  - 0x5961fe  `mov ecx,0x1f4; cmp eax,ecx; cmova` clamps the pop cap to 500
//              in FUN_005960a0 (setting -> display/apply).
//  - 0x748079  `comiss xmm1,[0x9b50e0]` + 0x748082 `mov [eax+0x80],500.0f`:
//              player resource 32 (bonus population cap) clamped to 25..500.
//              0x9b50e0 (500.0f) is shared with the frame-time clamp at
//              0x6c1f31, so we do NOT change it; the comiss is pointed at our
//              own 1000.0f instead.
//  - 0x750fd1  `mov [eax+0x10],500.0f` in the resource setter FUN_00750f70:
//              whenever resource 4 (population headroom) is set for a player
//              with type field [this+8] == 1, it is forced to 500.0. The Huns
//              bonus ("no houses") sets resource 4 = 1000 and got cut to 500
//              here (seen in-game: 504/504). The real limit is resource 32,
//              so this constant is raised to 1000.0 as well.
// Left alone on purpose: the AI unit-gathering arrays in 0x52c640 hold 500
// entries (cmp esi,0x1f4 at 0x52c820 / 0x52ca92); raising those would smash
// the stack. The AI then just groups at most 500 units at a time.
#include "wololoader.h"
#include <string.h>

#define POP_MAX 1000

typedef void (__thiscall *AddItem_t)(void *list, const char *text, int value);
static const AddItem_t AddItem = (AddItem_t)0x47b690;

// Our own 1000.0f for the resource-32 clamp (address goes into the comiss).
static const float g_pop_max_f = (float)POP_MAX;

// Replaces the call that adds "500": add it as before, then add "1000".
static void __fastcall add_500_then_1000(void *list, void *edx, const char *text, int value) {
    (void)edx;
    AddItem(list, text, value);
    AddItem(list, "1000", POP_MAX);
}

static BOOL write_code(DWORD at, const void *bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy((void *)at, bytes, n);
    VirtualProtect((void *)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
    return TRUE;
}

// Check the original bytes first, so a different exe build is never patched.
static BOOL expect(DWORD at, const void *bytes, size_t n) {
    if (memcmp((const void *)at, bytes, n) == 0) return TRUE;
    wl_log("popcap: unexpected bytes at 0x%08lx, skipped", (unsigned long)at);
    return FALSE;
}

void wl_install_popcap(void) {
    static const BYTE call_add[] = { 0xe8 };                       // call rel32
    static const BYTE clamp_ecx[] = { 0xb9, 0xf4, 0x01, 0x00, 0x00 }; // mov ecx,500
    static const BYTE comiss[] = { 0x0f, 0x2f, 0x0d, 0xe0, 0x50, 0x9b, 0x00 };
    static const BYTE store500[] = { 0xc7, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x43 };
    int ok = 0;

    if (expect(0x64d337, call_add, 1) && wl_patch_call(0x64d337, add_500_then_1000, 5)) ok++;
    if (expect(0x6da131, call_add, 1) && wl_patch_call(0x6da131, add_500_then_1000, 5)) ok++;

    if (expect(0x5961fe, clamp_ecx, sizeof clamp_ecx)) {
        DWORD v = POP_MAX;
        if (write_code(0x5961ff, &v, 4)) ok++;                        // imm32 of mov ecx
    }
    if (expect(0x748079, comiss, sizeof comiss) && expect(0x748082, store500, sizeof store500)) {
        DWORD addr = (DWORD)&g_pop_max_f;
        float f = (float)POP_MAX;
        if (write_code(0x74807c, &addr, 4) && write_code(0x748088, &f, 4)) ok++;  // operand, imm32
    }
    static const BYTE headroom500[] = { 0xc7, 0x40, 0x10, 0x00, 0x00, 0xfa, 0x43 };
    if (expect(0x750fd1, headroom500, sizeof headroom500)) {
        float f = (float)POP_MAX;
        if (write_code(0x750fd4, &f, 4)) ok++;                        // imm32 of the mov
    }
    wl_log("popcap: %d/5 patches applied (max %d)", ok, POP_MAX);
}
