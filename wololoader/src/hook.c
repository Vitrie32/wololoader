// Tiny x86 detour: overwrite whole instructions at `target` with JMP rel32.
// The caller passes `stolen` (verified from disassembly); stolen bytes must not
// contain relative jumps/calls, since they are copied verbatim. One exception: a 5-byte hook on a
// function that one of our own hooks already starts with JMP rel32 chains to that hook.
#include "wololoader.h"
#include <string.h>

static void write_jmp(BYTE *at, const void *to) {
    at[0] = 0xE9;
    *(DWORD *)(at + 1) = (DWORD)to - (DWORD)at - 5;
}

void *wl_hook(DWORD target, void *detour, int stolen) {
    if (stolen < 5) return NULL;
    BYTE *src = (BYTE *)target;
    BYTE *tramp = VirtualAlloc(NULL, stolen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    if (stolen == 5 && src[0] == 0xE9) {
        // Already hooked by another module of ours (its JMP rel32 is the whole 5 bytes): chain.
        // The trampoline jumps to that hook, which still calls the original through its own trampoline.
        write_jmp(tramp, src + 5 + *(LONG *)(src + 1));
    } else {
        memcpy(tramp, src, stolen);
        write_jmp(tramp + stolen, src + stolen);
    }

    DWORD old;
    if (!VirtualProtect(src, stolen, PAGE_EXECUTE_READWRITE, &old)) return NULL;
    write_jmp(src, detour);
    for (int i = 5; i < stolen; i++) src[i] = 0x90;
    VirtualProtect(src, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), src, stolen);
    wl_log("hook %08lx -> %p (tramp %p)", target, detour, tramp);
    return tramp;
}

// Overwrite `len` (>=5) bytes at `at` with CALL rel32 to `fn` plus NOPs.
BOOL wl_patch_call(DWORD at, void *fn, int len) {
    BYTE *p = (BYTE *)at;
    DWORD old;
    if (len < 5 || !VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    p[0] = 0xE8;
    *(DWORD *)(p + 1) = (DWORD)fn - at - 5;
    for (int i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    wl_log("patch call %08lx -> %p", at, fn);
    return TRUE;
}
