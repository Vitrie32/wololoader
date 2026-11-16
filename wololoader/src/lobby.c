// Multiplayer lobby: tell the host WHY a player could not join when their game
// data differs (different mods / Wololoader settings / Vanilla Mode).
//
// Verified in the disassembly (2026-11-15, research re/research/lobby_modcheck.md):
// - 0x584a40 (host handler of the client state packet) compares the client's data
//   CRC: `cmp 0x18(%ebx),%eax` at 0x584ad3 (ebx = packet payload), and on a mismatch
//   calls the reject function with reason 3: 0x584b3b push $0x3; push %ebx; call 0x583f50.
// - 0x583f50 reject(comm, payload, reason), __thiscall, ret 8 (0x583fbc). Other callers
//   pass other reasons (0x57f540: 0). Prologue 55 / 8b ec / 83 ec 10 = push ebp;
//   mov ebp,esp; sub esp,0x10 (6 bytes, whole instructions, no relative jumps).
// - The lobby shows the same chat history the game uses in game: the lobby screen
//   registers a listener on [0xaeefb4] (0x648081..0x6480c4), so a system line added
//   with 0x8037b0 (see luagame.c) appears in the lobby chat.
#include "wololoader.h"
#include <string.h>

typedef void (__thiscall *Reject_t)(void *comm, void *payload, int reason);
typedef void (__thiscall *ChatAdd_t)(void *history, const char *sender, const char *text,
                                     int player, int type, int a5, int a6);
static Reject_t o_reject;

static void __fastcall reject_hook(void *comm, void *edx, void *payload, int reason) {
    (void)edx;
    if (reason == 3) {
        wl_log("lobby: a player was rejected: different game data (mods)");
        void *hist = *(void **)0xaeefb4;
        if (hist)
            ((ChatAdd_t)0x8037b0)(hist, NULL,
                "Wololoader: a player could not join because their mods or game data differ. "
                "Everyone needs the same mods and Wololoader settings (or all in Vanilla Mode).",
                *(int *)0xaad300, 0, 0, 0);
    }
    o_reject(comm, payload, reason);
}

// ---- "Active Mods" button in the lobby (GameSetupScreen) -------------------------
// - 0x647430 GameSetupScreen ctor, __thiscall(this, parent), ret 4 (0x6483ca); prologue
//   55 / 8b ec / 6a ff (5 bytes). Hooked: after it returns we add our button.
// - GameSetupScreen vtable 0x9d0d10, slot 49 (event: self, panel, code, a, b; ret 0x10) =
//   0x48d430 (base handler, checked in the exe); we replace the entry in THIS vtable only,
//   catch our button and pass everything else on (same pattern as mpguard.c / ui_mods.c).
// - Buttons: 0x48df30 add_button (ui_mods.c), coordinates in the screen's reference size
//   [screen+0x130]/[+0x134] (menu.c), placed right of "Change Lobby Name".
typedef void *(__thiscall *LobbyCtor_t)(void *self, void *parent);   // ctor returns this in eax
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);
typedef void *(__thiscall *AddButton_t)(void *scr, void *parent, const char *text, const char *text2,
                                         int x, int y, int w, int h, int a, int b, int c);
void wl_mods_open_readonly(void *return_screen);   // ui_mods.c
static LobbyCtor_t o_lobby_ctor;
static Event_t o_lobby_event;
static void *g_lobby_btn, *g_lobby_screen;

// Must return what the ctor returns (this): the caller uses eax as the new screen
// (returning nothing crashed in set_current 0x48a900 with a NULL screen, 2026-11-15).
// Multiplayer setup? The same GameSetupScreen is used for single player games, where
// there is no "Change Lobby Name" and the button would land on the wrong spot.
// settings = [[[0xaf2c58]+0x98]+0xd54], +0x408 == 1 'isMultiPlayer' (see luagame.c).
static int lobby_is_mp(void) {
    DWORD game = *(DWORD *)0xaf2c58, a = game ? *(DWORD *)(game + 0x98) : 0;
    DWORD settings = a ? *(DWORD *)(a + 0xd54) : 0;
    return settings && *(int *)(settings + 0x408) == 1;
}

static void *__fastcall lobby_ctor_hook(void *self, void *edx, void *parent) {
    (void)edx;
    void *ret = o_lobby_ctor(self, parent);
    g_lobby_btn = NULL;
    if (!lobby_is_mp()) { wl_log("lobby: single player setup, no Active Mods button"); return ret; }
    int rw = *(int *)((char *)self + 0x130), rh = *(int *)((char *)self + 0x134);
    if (rw < 640 || rw > 8000) rw = 1024;
    if (rh < 480 || rh > 8000) rh = 768;
    // Place the button right of "Change Lobby Name" with the same size, found at run time:
    // 1) a hidden probe button gives the button class (vtable); 2) the top-left button of the
    //    lobby with that vtable (pointer fields of the screen object) is "Change Lobby Name";
    //    its absolute screen rect is at +0xb8..+0xc4 (logged 2026-11-15: ours read 383 29 145 29);
    // 3) 0x491190 (screen ref rect -> screen rect, __thiscall(scr, out[4], in[4]), ret 8,
    //    used by ui_mods.c) gives the affine mapping, inverted for our reference coordinates.
    typedef void *(__thiscall *ScaleRect_t)(void *screen, int out[4], const int in[4]);
    void *probe = ((AddButton_t)0x48df30)(self, self, "", NULL, 0, 0, 10, 10, 0, 0, 0);
    int x = 147 * rw / 800, y = 0, w = 123 * rw / 800, h = 25 * rh / 600;   // fallback
    if (probe) {
        *(int *)((char *)probe + 0x1c) = 0;                       // hide (visible flag, ui_mods.c)
        void *vt = *(void **)probe, *cln = NULL;
        for (int off = 0; off + 4 <= 0xbe0; off += 4) {           // GameSetupScreen size 0xbe0
            void *c = *(void **)((char *)self + off);
            if (!c || c == probe || IsBadReadPtr(c, 0xc8) || *(void **)c != vt) continue;
            // Closest to the top-left corner (x + 2y): "Change Lobby Name". "Reset" (top right)
            // sits a few pixels higher, so "topmost" picked it (seen in game 2026-11-15).
            int score = *(int *)((char *)c + 0xb8) + 2 * *(int *)((char *)c + 0xbc);
            if (!cln || score < *(int *)((char *)cln + 0xb8) + 2 * *(int *)((char *)cln + 0xbc)) cln = c;
        }
        int in0[4] = { 0, 0, 1000, 1000 }, o[4] = { 0, 0, 0, 0 };
        ((ScaleRect_t)0x491190)(self, o, in0);
        if (cln && o[2] > 0 && o[3] > 0) {
            int sx = *(int *)((char *)cln + 0xb8), sy = *(int *)((char *)cln + 0xbc);
            int sw = *(int *)((char *)cln + 0xc0), sh = *(int *)((char *)cln + 0xc4);
            x = (sx + sw + 10 - o[0]) * 1000 / o[2];
            y = (sy - o[1]) * 1000 / o[3];
            w = sw * 1000 / o[2];
            h = sh * 1000 / o[3];
            wl_log("lobby: Change Lobby Name at %d %d %d %d, map %d %d %d %d", sx, sy, sw, sh, o[0], o[1], o[2], o[3]);
        }
    }
    g_lobby_btn = ((AddButton_t)0x48df30)(self, self, "Active Mods", NULL, x, y, w, h, 0, 0, 0);
    g_lobby_screen = self;
    if (g_lobby_btn)
        wl_log("lobby: Active Mods button %p (ref %dx%d) rect %d %d %d %d", g_lobby_btn, rw, rh,
               *(int *)((char *)g_lobby_btn + 0xb8), *(int *)((char *)g_lobby_btn + 0xbc),
               *(int *)((char *)g_lobby_btn + 0xc0), *(int *)((char *)g_lobby_btn + 0xc4));
    return ret;
}

static int __fastcall lobby_event_hook(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (code == 3 /* left click */ && panel && panel == g_lobby_btn && self == g_lobby_screen) {
        wl_mods_open_readonly(self);
        return 1;
    }
    return o_lobby_event(self, panel, code, a, b);
}

static void install_active_mods_button(void) {
    static const BYTE ctor[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    void **slot = (void **)0x9d0d10 + 49;
    if (memcmp((void *)0x647430, ctor, sizeof ctor) != 0 || (DWORD)*slot != 0x48d430) {
        wl_log("lobby: unexpected bytes, Active Mods button skipped");
        return;
    }
    o_lobby_ctor = (LobbyCtor_t)wl_hook(0x647430, lobby_ctor_hook, sizeof ctor);
    if (!o_lobby_ctor) return;
    DWORD old;
    o_lobby_event = (Event_t)*slot;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = (void *)lobby_event_hook;
    VirtualProtect(slot, 4, old, &old);
}

void wl_install_lobby(void) {
    install_active_mods_button();
    static const BYTE orig[] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10 };
    if (memcmp((void *)0x583f50, orig, sizeof orig) != 0) {
        wl_log("lobby: unexpected bytes at 0x583f50, skipped");
        return;
    }
    o_reject = (Reject_t)wl_hook(0x583f50, reject_hook, sizeof orig);
    wl_log("lobby: mod mismatch message %s", o_reject ? "installed" : "FAILED");
}
