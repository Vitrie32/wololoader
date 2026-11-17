// Custom taunts in the in-game taunt list (F11).
// files.c add_mod_taunts() maps <mod>\taunts\"NN name.mp3" into the game's taunt
// folders; the list builder 0x686210 (prologue 55 8b ec 6a ff 68 14 35 95 00,
// ret 4 at 0x686695) already takes the name from the file name for taunts > 42
// (0x6863fc). It only shows taunts 3-6, 31-38 and 42: both filters end with
// `cmp esi,0x2a; jne reject` (0x686361/0x686364 and 0x68651e/0x686521).
// jne -> jl makes every taunt >= 42 pass. Verified in disassembly 2026-11-17.
//
// "Mute all taunts" is NOT implemented yet: 0x4f94a0 (called at 0x584609 and
// 0x588221) only sets a bit in a per-player taunt table at 0xadcf47, it does not
// play the sound, so the taunt sound call site is still unknown.
#include "wololoader.h"

static void patch_jne_to_jl(BYTE *at) {
    DWORD old;
    if (!VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    if (at[0] == 0x75) {                 // jne
        at[0] = 0x7C;                    // jl: accept taunt >= 42
        wl_log("taunts: list filter at %p patched", (void *)at);
    } else {
        wl_log("taunts: unexpected byte 0x%02x at %p, list filter not patched", at[0], (void *)at);
    }
    VirtualProtect(at, 1, old, &old);
}

void wl_install_taunts(void) {
    patch_jne_to_jl((BYTE *)0x686364);
    patch_jne_to_jl((BYTE *)0x686521);
}
