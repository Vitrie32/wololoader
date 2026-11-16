// Lua game commands: change the game through the game's own command packets, so
// every machine in a multiplayer game executes them on the same turn (no desync).
//
// Who sends: every machine runs the same Lua scripts, so each command must be sent
// by exactly ONE machine, or it would happen once per player. A command for player
// P is sent by P's own machine; a command for a computer player or Gaia is sent by
// the machine of the lowest-numbered human player (the "script host"). Every other
// machine skips it (the Lua call returns false there). In single player the local
// machine sends everything.
//
// Verified in the disassembly (2026-11-15; research re/research/lua_commands.md, whose
// executor quotes were partly wrong and are corrected here):
// - command object = [world+0xc0] (WorldPlayer::create_unit 0x74dd90: mov 0x4(%edi),%ecx = world;
//   mov 0xc0(%ecx),%ecx; call 0x809220).
// - send 0x80ef70 __thiscall(cmd, packet, size, player id), ret 0xc; the packet is allocated
//   with the game calloc 0x8ea479 and owned by the command object afterwards (freed by the
//   next send, 0x80efd1..0x80efe1).
// - executor 0x813460 __thiscall(proc, packet), ret 4: hooked here (8 stolen bytes
//   `55 8b ec a1 b8 ef ae 00` = push ebp; mov ebp,esp; mov 0xaeefb8,%eax). overflow.c uses the
//   same hook through wl_overflow_exec.
// - 0x04 create unit: packet {u8 4, u8 0, i16 type, u8 player, 3 pad, f32 x, f32 y, f32 z},
//   20 bytes (builder 0x809220); executor 0x8161d0 calls player vtable +0x98
//   create_unit(type, x, y, z, 1) WITHOUT checking the type: so we check it before sending
//   (masters array [player+0x14], count int [player+0x10]: 0x740301 cmp 0x10(%ecx),%edx;
//   mov 0x14(%ecx),%eax; mov (%eax,%edx,4)).
// - 0x6a delete: packet {u8 0x6a, 3 pad, i32 unit id, i32 owner player id}, 12 bytes
//   (builder 0x812670); executor 0x8140d0 deletes when [owner+0x1adc] == packet player id and
//   master type >= 70. Player id of a player object: [player+0x1adc].
// - 0x05 resource cheat: packet {u8 5, u8 player, u8 res, u8 pad, f32 amount}, 8 bytes
//   (builder 0x808c40). The game executor 0x80a1b0 ignores it unless cheats are on, so our
//   packets carry WL_MARK in the pad byte and the executor hook runs them itself:
//   add_resource = player vtable +0x64, __thiscall(player, int res, float amount, int 0)
//   (0x80a288..0x80a295). Unmarked 0x05 packets (real cheats) go to the game unchanged.
// - instant research: also a marked 0x05 packet (pad byte WL_MARK_TECH, tech id i16 at +4);
//   the hook calls complete_tech 0x84d300 __thiscall(tech_tree [player+0x1ae8], short tech),
//   ret 4 (call site 0x8bfd8b..0x8bfd95). It checks the id range (0x84d33b) and skips techs
//   already researched (status >= 3, 0x84d3d1).
// - 0x74e020 player is_computer: `cmpl $0x3,0x8(%ecx); sete %al` (the cheat executor lets
//   these players cheat, 0x80a202). Gaia is player 0.
// All players need this Wololoader (marked packets mean nothing to a vanilla game); the lobby
// CRC check already keeps players with different mods/data apart.
#include "wololoader.h"

#define RDP(p, o) (*(void **)((char *)(p) + (o)))
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))
#define WL_MARK 0x57        /* 'W': add resource */
#define WL_MARK_TECH 0x58   /* 'X': complete tech */
#define WL_MARK_AGE 0x59    /* 'Y': set age */

typedef void *(__cdecl *Calloc_t)(size_t n, size_t size);
typedef int (__thiscall *Send_t)(void *cmd, BYTE *pkt, int size, int player);
typedef void (__thiscall *Exec_t)(void *proc, BYTE *pkt);
typedef void (__thiscall *AddRes_t)(void *player, int res, float amount, int flag);
typedef void (__thiscall *CompleteTech_t)(void *tech_tree, short tech);
typedef void *(__thiscall *GetObj_t)(void *world, int id);

static Exec_t o_exec;
static int g_sent_this_second;
static DWORD g_second;

static void *world(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    return game ? RDP((void *)game, 0xc8) : NULL;
}
static void *player_obj(void *w, int id, int *n_out) {
    char *tab = w ? (char *)RDP(w, 0x184) : NULL, *end = w ? (char *)RDP(w, 0x188) : NULL;
    int n = tab && end > tab ? (int)((end - tab) / 8) : 0;
    if (n_out) *n_out = n;
    return (id >= 0 && id < n) ? *(void **)(tab + id * 8) : NULL;
}
static int is_computer(void *p) { return p && RD32(p, 0x8) == 3; }

// 1: this machine sends commands for player `pid`, 0: another machine does.
static int g_local_ctx;   // set while UI click handlers run: only this machine runs them
void wl_cmd_set_local_ctx(int on) { g_local_ctx = on; }
static int i_send_for(void *w, int pid) {
    int n, me = (int)RD32(w, 0x174);
    if (g_local_ctx) return player_obj(w, pid, NULL) != NULL;
    void *p = player_obj(w, pid, &n);
    if (!p) return 0;
    if (pid == me) return 1;
    if (pid != 0 && !is_computer(p)) return 0;          // another human: their machine sends
    for (int i = 1; i < n; i++) {                       // computer/Gaia: lowest human sends
        void *q = player_obj(w, i, NULL);
        if (q && !is_computer(q)) return i == me;
    }
    return 0;
}

// Common checks; returns the packet buffer (game calloc) or NULL with *why set.
static BYTE *begin(int pid, int size, void **w_out, int *rc) {
    void *w = world();
    *rc = -1;
    if (!w || !RDP(w, 0xc0) || !player_obj(w, pid, NULL)) return NULL;
    if (!i_send_for(w, pid)) { *rc = 0; return NULL; }
    DWORD sec = GetTickCount() / 1000;
    if (sec != g_second) { g_second = sec; g_sent_this_second = 0; }
    if (++g_sent_this_second > 100) { wl_log("gamecmd: more than 100 commands in a second, dropped"); return NULL; }
    BYTE *pkt = ((Calloc_t)0x8ea479)(1, (size_t)size);
    if (!pkt) return NULL;
    *w_out = w;
    return pkt;
}
static int send_cmd(void *w, BYTE *pkt, int size) {
    ((Send_t)0x80ef70)(RDP(w, 0xc0), pkt, size, (int)RD32(w, 0x174));
    return 1;
}

int wl_cmd_add_resource(int pid, int res, float amount) {
    void *w; int rc;
    if (res < 0 || res > 3) return -1;
    BYTE *pkt = begin(pid, 8, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = (BYTE)res; pkt[3] = WL_MARK;
    memcpy(pkt + 4, &amount, 4);
    return send_cmd(w, pkt, 8);
}

int wl_cmd_research(int pid, int tech) {
    void *w; int rc;
    if (tech < 0 || tech > 0x7fff) return -1;
    BYTE *pkt = begin(pid, 8, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = 0; pkt[3] = WL_MARK_TECH;
    *(short *)(pkt + 4) = (short)tech;
    return send_cmd(w, pkt, 8);
}

// Set the age (0 Dark .. 3 Imperial). Up: the missing age techs (101 Feudal, 102 Castle,
// 103 Imperial) are completed like Research. Down: the age value (resource 6) is set and the
// age techs above it become researchable again (next one status 1 = can research, the ones after
// it 0), so the player can age up again. Effects of techs already researched (units, buildings,
// bonuses) stay: the game has no way to undo them.
// Tech tree [player+0x1ae8]: entries = [tt], 16 bytes each, status int16 at +4 (complete_tech
// 0x84d3b1..0x84d3ea: shl $4, 0x4(%eax,%edx,1), 3 = researched); count int16 at [tt+4]
// (0x84d33b cmp 0x4(%edi),%bx); research needs status 1 (executor 0x815c81 cmpw $0x1).
int wl_cmd_set_age(int pid, int age) {
    void *w; int rc;
    if (age < 0 || age > 3) return -1;
    BYTE *pkt = begin(pid, 8, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = (BYTE)age; pkt[3] = WL_MARK_AGE;
    return send_cmd(w, pkt, 8);
}

static void exec_set_age(void *p, int age) {
    void *tt = RDP(p, 0x1ae8);
    if (!tt) return;
    BYTE *ent = (BYTE *)RDP(tt, 0);
    int count = *(short *)((BYTE *)tt + 4);
    for (int k = 1; k <= 3; k++) {
        int id = 100 + k;
        if (!ent || id >= count) continue;
        short *status = (short *)(ent + id * 16 + 4);
        if (k <= age) {
            if (*status != 3) ((CompleteTech_t)0x84d300)(tt, (short)id);
        } else {
            *status = (short)(k == age + 1 ? 1 : 0);
        }
    }
    float *res = (float *)RDP(p, 0x3c);
    if (res && res[6] != (float)age) {
        AddRes_t add = (AddRes_t)(*(void ***)p)[0x64 / 4];
        add(p, 6, (float)age - res[6], 0);
    }
}

int wl_cmd_spawn(int pid, int type, float x, float y) {
    void *w0 = world();
    void *p = player_obj(w0, pid, NULL);
    if (!p || type < 0 || type >= (int)RD32(p, 0x10)) return -1;
    void **masters = (void **)RDP(p, 0x14);
    if (!masters || !masters[type]) return -1;          // no such unit in this civ's data
    void *w; int rc;
    BYTE *pkt = begin(pid, 20, &w, &rc);
    if (!pkt) return rc;
    float z = 0.0f;
    pkt[0] = 0x04; *(short *)(pkt + 2) = (short)type; pkt[4] = (BYTE)pid;
    memcpy(pkt + 8, &x, 4); memcpy(pkt + 12, &y, 4); memcpy(pkt + 16, &z, 4);
    return send_cmd(w, pkt, 20);
}

int wl_cmd_delete(int unit_id) {
    void *w0 = world();
    void *u = w0 ? ((GetObj_t)0x733bf0)(w0, unit_id) : NULL;
    void *owner = u ? RDP(u, 0x10) : NULL;
    if (!owner) return -1;
    int pid = (int)RD32(owner, 0x1adc);
    void *w; int rc;
    BYTE *pkt = begin(pid, 12, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x6a;
    *(int *)(pkt + 4) = unit_id;
    *(int *)(pkt + 8) = pid;
    return send_cmd(w, pkt, 12);
}

// Runs on EVERY machine when the turn executes the packet.
static int exec_wl(BYTE *pkt) {
    if (pkt[0] != 0x05 || (pkt[3] != WL_MARK && pkt[3] != WL_MARK_TECH && pkt[3] != WL_MARK_AGE)) return 0;
    void *p = player_obj(world(), pkt[1], NULL);
    if (!p) return 1;
    if (pkt[3] == WL_MARK) {
        float amount;
        memcpy(&amount, pkt + 4, 4);
        AddRes_t add = (AddRes_t)(*(void ***)p)[0x64 / 4];
        add(p, pkt[2], amount, 0);
    } else if (pkt[3] == WL_MARK_AGE) {
        exec_set_age(p, pkt[2]);
    } else {
        void *tt = RDP(p, 0x1ae8);
        if (tt) ((CompleteTech_t)0x84d300)(tt, *(short *)(pkt + 4));
    }
    return 1;
}

static void __fastcall exec_hook(void *proc, void *edx, BYTE *pkt) {
    (void)edx;
    if (pkt && exec_wl(pkt)) return;
    if (wl_overflow_exec((void *)o_exec, proc, pkt)) return;
    o_exec(proc, pkt);
}

void wl_install_gamecmd(void) {
    static const BYTE b_exec[] = { 0x55, 0x8b, 0xec, 0xa1, 0xb8, 0xef, 0xae, 0x00 };
    if (memcmp((const void *)0x813460, b_exec, sizeof b_exec) != 0) {
        wl_log("gamecmd: unexpected bytes at 0x813460, game commands off");
        return;
    }
    o_exec = (Exec_t)wl_hook(0x813460, exec_hook, 8);
    wl_log("gamecmd: command executor hook %s", o_exec ? "installed" : "FAILED");
}
