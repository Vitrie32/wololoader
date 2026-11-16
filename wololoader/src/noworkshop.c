// Skip the game's Steam Workshop / ModManager sync at startup.
//
// Why: right after the main menu the game ran the "Validating Subscriptions"
// sync (UGC queries through steam_api). With the Steam emulator it sometimes
// never finished and the game hung (main thread stuck presenting frames).
// Wololoader has its own mods (wololoader\mods), so the game's ModManager is
// not needed.
//
// How (verified 2026-11-14 in the disassembly, research by a Kiro worker):
// 0x7ce430 is the getter of the game's own "NOMODS" command line flag:
//     a0 18 59 af 00   mov al,[0xaf5918]
//     c3               ret
// Its two callers (0x5c34a8, 0x7cd48b) only do `test al,al; je`; with the flag
// set the game logs "Commandline NOMODS detected. ModManager will not install
// any mods" and skips the enumeration. We make it return 1 (`mov al,1; ret`),
// exactly as if the game had been started with NOMODS.
#include "wololoader.h"
#include <string.h>

void wl_install_no_workshop(void) {
    static const BYTE orig[] = { 0xa0, 0x18, 0x59, 0xaf, 0x00, 0xc3 };
    static const BYTE patch[] = { 0xb0, 0x01, 0xc3 };   // mov al,1; ret
    BYTE *at = (BYTE *)0x7ce430;
    DWORD old;
    if (memcmp(at, orig, sizeof orig) != 0) { wl_log("noworkshop: unexpected bytes, skipped"); return; }
    if (!VirtualProtect(at, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(at, patch, sizeof patch);
    VirtualProtect(at, sizeof patch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof patch);
    wl_log("noworkshop: Steam Workshop sync disabled (NOMODS)");
}
