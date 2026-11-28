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
#define WL_MARK 0x57        /* 'W': add vanilla resource (8-byte packet, res 0..3) */
#define WL_MARK_TECH 0x58   /* 'X': complete tech */
#define WL_MARK_AGE 0x59    /* 'Y': set age */
#define WL_MARK_SPAWN_NC 0x5a  /* 'Z': create a unit that is never captured (see wl_cmd_spawn_neutral) */
#define WL_MARK_RES16 0x5b     /* '[': add int16 resource (12-byte packet: i16 res @+4, f32 amount @+8) */
#define WL_MARK_SETRES16 0x5c  /* '\': set int16 resource (12-byte packet: i16 res @+4, f32 val @+8) */
#define WL_MARK_DEFEAT 0x5d    /* ']': defeat player via RGE_Player::loss_if_game_on (0x750580) */
#define WL_MARK_WIN 0x5e       /* '^': win player via 0x7558a0 (solo) or 0x7558b0 (team) */
#define WL_MARK_REMOVE 0x5f    /* '_': remove an object the game's delete (0x6a) skips (master type < 70) */

typedef void *(__cdecl *Calloc_t)(size_t n, size_t size);
typedef int (__thiscall *Send_t)(void *cmd, BYTE *pkt, int size, int player);
typedef void (__thiscall *Exec_t)(void *proc, BYTE *pkt);
typedef void (__thiscall *AddRes_t)(void *player, int res, float amount, int flag);
// WorldPlayer::new_attribute_num (vtable +0x60, 0x744400, ret 0x8; verified via objdump)
typedef void (__thiscall *SetRes_t)(void *player, int res, float val);
typedef void (__thiscall *CompleteTech_t)(void *tech_tree, short tech);
typedef void *(__thiscall *GetObj_t)(void *world, int id);
// Verified in AoK HD.exe 5.8 disassembly (2026-11-25, re/FINDINGS.md):
// - 0x750580 RGE_Player::loss_if_game_on: __thiscall(player), plain ret
// - 0x7558a0 RGE_Player::win_game_now: __thiscall(player), plain ret
// - 0x7558b0 TRIBE_Player::win_game_now_team: __thiscall(player), plain ret
typedef void (__thiscall *VoidPlayer_t)(void *player);

static Exec_t o_exec;
static int g_win_defeat_ok;
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
static int g_dev_ctx;     // set while DevPanel / console runs a command
extern int wl_is_multiplayer(void);
extern int wl_cheats_enabled(void);
void wl_cmd_set_local_ctx(int on) { g_local_ctx = on; }
void wl_cmd_set_dev_ctx(int on) { g_dev_ctx = on; }
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
    if (g_dev_ctx && wl_is_multiplayer() && !wl_cheats_enabled()) return NULL;
    if (!w || !RDP(w, 0xc0) || !player_obj(w, pid, NULL)) return NULL;
    if (!i_send_for(w, pid)) { *rc = 0; return NULL; }
    BYTE *pkt = ((Calloc_t)0x8ea479)(1, (size_t)size);
    if (!pkt) return NULL;
    *w_out = w;
    return pkt;
}
// At most 100 Lua commands a second (real time) go to the game. More are queued and sent by
// wl_gamecmd_frame on the next frames, so a script can ask for 1000 units at once; the queue
// belongs to one world and is dropped when the game ends (the packets are not freed then: the
// game's free for its calloc 0x8ea479 is not verified, and this only happens with a full queue).
#define CMD_RATE 100
#define CMD_QUEUE 4096
static struct { BYTE *pkt; int size; int sender; } g_q[CMD_QUEUE];
static int g_qhead, g_qlen, g_qdropped;
static void *g_qworld;

// sender < 0: the local player (most commands). The move builder 0x8097d0 passes the units' owner
// instead (0x8098c6 mov 0x1adc(%eax)), so wl_cmd_move sends as the owner (Gaia = 0).
static void send_now_as(void *w, BYTE *pkt, int size, int sender) {
    ((Send_t)0x80ef70)(RDP(w, 0xc0), pkt, size, sender >= 0 ? sender : (int)RD32(w, 0x174));
}
static int rate_ok(void) {
    DWORD sec = GetTickCount() / 1000;
    if (sec != g_second) { g_second = sec; g_sent_this_second = 0; }
    return g_sent_this_second < CMD_RATE;
}
// Multiplayer: commands sent in the first moments of a match never execute (seen 2026-11-28 as
// MP host: 25 packets sent at game start were all lost, the same kinds sent 2.7 s later ran one
// turn later; the network turns have not started yet). So in multiplayer packets wait in the queue
// until the game clock [world+0x178] (ms, gameapi.c ga_time) has passed MP_HOLD_MS, and then
// leave at most one every MP_GAP_MS for the next 10 s, so a start-up burst is spread over the turns.
#define MP_HOLD_MS 3000
#define MP_GAP_MS 30
static DWORD g_mp_last_send;
static int mp_wait(void *w) {
    if (!wl_is_multiplayer()) return 0;
    int t = (int)RD32(w, 0x178);
    if (t < MP_HOLD_MS) return 1;
    if (t > MP_HOLD_MS + 10000) return 0;            // the start-up burst is out: normal rate (100/s)
    return GetTickCount() - g_mp_last_send < MP_GAP_MS;
}
static int send_cmd_as(void *w, BYTE *pkt, int size, int sender);
static int send_cmd(void *w, BYTE *pkt, int size) { return send_cmd_as(w, pkt, size, -1); }
static int send_cmd_as(void *w, BYTE *pkt, int size, int sender) {
    if (g_qworld != w) { g_qhead = g_qlen = 0; g_qworld = w; }
    if (g_qlen == 0 && rate_ok() && !mp_wait(w)) {
        g_sent_this_second++;
        g_mp_last_send = GetTickCount();
        send_now_as(w, pkt, size, sender);
        return 1;
    }
    if (g_qlen == CMD_QUEUE) {
        if (g_qdropped++ == 0) wl_log("gamecmd: %d commands waiting, more are dropped", CMD_QUEUE);
        return -1;
    }
    int k = (g_qhead + g_qlen++) % CMD_QUEUE;
    g_q[k].pkt = pkt;
    g_q[k].size = size;
    g_q[k].sender = sender;
    return 1;
}
// From the game screen's frame (luagame.c): sends queued commands within the rate.
void wl_gamecmd_frame(void) {
    if (!g_qlen) return;
    void *w = world();
    if (!w || w != g_qworld || !RDP(w, 0xc0)) { g_qhead = g_qlen = 0; g_qworld = NULL; return; }
    while (g_qlen && rate_ok() && !mp_wait(w)) {
        g_sent_this_second++;
        g_mp_last_send = GetTickCount();
        send_now_as(w, g_q[g_qhead].pkt, g_q[g_qhead].size, g_q[g_qhead].sender);
        g_qhead = (g_qhead + 1) % CMD_QUEUE;
        g_qlen--;
    }
    if (!g_qlen) g_qdropped = 0;
}

int wl_cmd_add_resource(int pid, int res, float amount) {
    void *w; int rc;
    if (res < 0 || res > WL_CUSTOM_RES_MAX) return -1;
    if (res <= 3) {
        BYTE *pkt = begin(pid, 8, &w, &rc);
        if (!pkt) return rc;
        pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = (BYTE)res; pkt[3] = WL_MARK;
        memcpy(pkt + 4, &amount, 4);
        return send_cmd(w, pkt, 8);
    }
    // Custom / extended resource ids (4..32766): validate against live [player+0x38]
    // and transmit a 12-byte marked 0x05 packet with int16 resource id at +4.
    void *w0 = world();
    void *p = player_obj(w0, pid, NULL);
    if (!p || res >= (int)RD32(p, 0x38)) return -1;
    BYTE *pkt = begin(pid, 12, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = 0; pkt[3] = WL_MARK_RES16;
    *(short *)(pkt + 4) = (short)res;
    memcpy(pkt + 8, &amount, 4);
    return send_cmd(w, pkt, 12);
}

int wl_cmd_set_resource(int pid, int res, float amount) {
    void *w0 = world();
    void *p = player_obj(w0, pid, NULL);
    if (!p || res < 0 || res > WL_CUSTOM_RES_MAX || res >= (int)RD32(p, 0x38)) return -1;
    void *w; int rc;
    BYTE *pkt = begin(pid, 12, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = 0; pkt[3] = WL_MARK_SETRES16;
    *(short *)(pkt + 4) = (short)res;
    memcpy(pkt + 8, &amount, 4);
    return send_cmd(w, pkt, 12);
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

// Defeat player `pid` (1..8) deterministically on every machine via lockstep 0x05 packet.
// Calls RGE_Player::loss_if_game_on (0x750580) in exec_wl.
int wl_cmd_defeat(int pid) {
    void *w; int rc;
    if (!g_win_defeat_ok || pid <= 0) return -1;
    BYTE *pkt = begin(pid, 8, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = 0; pkt[3] = WL_MARK_DEFEAT;
    return send_cmd(w, pkt, 8);
}

// Mark player `pid` (1..8) as winner deterministically via lockstep 0x05 packet.
// team == 0: RGE_Player::win_game_now (0x7558a0); team != 0: TRIBE_Player::win_game_now_team (0x7558b0).
int wl_cmd_win(int pid, int team) {
    void *w; int rc;
    if (!g_win_defeat_ok || pid <= 0) return -1;
    BYTE *pkt = begin(pid, 8, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = (BYTE)(team ? 1 : 0); pkt[3] = WL_MARK_WIN;
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
    void *m = RDP(u, 0xc);
    if (m && *(BYTE *)((char *)m + 4) < 70) {
        // The game's delete executor 0x8140d0 ignores master types below 70 (0x8140ec cmpb $0x46),
        // e.g. 837 REVEAL (type 20) or flags (10): those go through our marked packet instead.
        BYTE *pk = begin(pid, 12, &w, &rc);
        if (!pk) return rc;
        pk[0] = 0x05; pk[1] = (BYTE)pid; pk[2] = 0; pk[3] = WL_MARK_REMOVE;
        *(int *)(pk + 4) = unit_id;
        return send_cmd(w, pk, 12);
    }
    BYTE *pkt = begin(pid, 12, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x6a;
    *(int *)(pkt + 4) = unit_id;
    *(int *)(pkt + 8) = pid;
    return send_cmd(w, pkt, 12);
}

// Move: the game's own command 3 (builder 0x8097d0, executor 0x80bdd0 via 0x809fe0 case 3):
// {u8 3, u8 player, 2 pad, i32 target id (-1 = none), u8 unit count, 3 pad, f32 x, f32 y,
//  i32 unit ids[count]}, 0x14 + 4 * count bytes. The builder writes player 0 for a computer
// player's units (0x8098a5..: is_computer -> puVar3[1] = 0); the unit list reader 0x80dad0
// then takes the ids from the packet (log "Units defined inside command"), and does the same
// for Gaia (player 0). Human players' units carry their player id (the reader also caches
// them as that player's last commanded units). Sent by the machine that sends for the owner.
int wl_cmd_move(int unit_id, float x, float y) {
    void *w0 = world();
    void *u = w0 ? ((GetObj_t)0x733bf0)(w0, unit_id) : NULL;
    void *owner = u ? RDP(u, 0x10) : NULL;
    if (!owner || x < 0 || y < 0 || x > 1000 || y > 1000) return -1;
    int pid = (int)RD32(owner, 0x1adc);
    void *w; int rc;
    BYTE *pkt = begin(pid, 0x18, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 3;
    pkt[1] = (BYTE)(pid == 0 || is_computer(owner) ? 0 : pid);
    *(int *)(pkt + 4) = -1;
    pkt[8] = 1;
    memcpy(pkt + 0xc, &x, 4);
    memcpy(pkt + 0x10, &y, 4);
    *(int *)(pkt + 0x14) = unit_id;
    return send_cmd_as(w, pkt, 0x18, pid);   // as the owner, like the builder (see send_now_as)
}

// A unit that stays its owner's (Gaia) when players see it. Gaia combat units get
// [unit+0x194] = 1 when they are set up (TRIBE_Combat_Object::setup 0x8c9c20: 0x8c9ced / 0x8c9d09,
// owner [+0x1adc] == 0 and master class [+0x16] not in an exempt list); RGE_Combat_Object::update
// 0x5aa8a0 then gives the unit to the first human player who sees it (0x5aaaa2 cmpb $0x0,0x194;
// 0x5aab57 call *0x50 changes the owner; 0x5aabbe clears the flag). Our marked packet creates the
// unit on every machine in the same turn, exactly like the game's create executor 0x8161d0
// (player vtable +0x98, __thiscall(type, x, y, z, 1), 0x816240), and clears the flag at once.
// Packet {u8 5, u8 player, u8 0, u8 'Z', i16 type, 2 pad, f32 x, f32 y}, 16 bytes.
int wl_cmd_spawn_neutral(int pid, int type, float x, float y) {
    void *w0 = world();
    void *p = player_obj(w0, pid, NULL);
    if (!p || type < 0 || type >= (int)RD32(p, 0x10)) return -1;
    void **masters = (void **)RDP(p, 0x14);
    if (!masters || !masters[type]) return -1;
    void *w; int rc;
    BYTE *pkt = begin(pid, 16, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0x05; pkt[1] = (BYTE)pid; pkt[2] = 0; pkt[3] = WL_MARK_SPAWN_NC;
    *(short *)(pkt + 4) = (short)type;
    memcpy(pkt + 8, &x, 4);
    memcpy(pkt + 12, &y, 4);
    return send_cmd(w, pkt, 16);
}

typedef void (__thiscall *CreateUnit_t)(void *player, int type, float x, float y, float z, int one);
static void exec_spawn_neutral(void *p, int type, float x, float y) {
    if (type < 0 || type >= (int)RD32(p, 0x10)) return;
    void **masters = (void **)RDP(p, 0x14);
    if (!masters || !masters[type]) return;
    ((CreateUnit_t)(*(void ***)p)[0x98 / 4])(p, type, x, y, 0.0f, 1);
    // The new unit is the newest of that type at that spot: player unit list [player+0x18],
    // array [list+4], count [list+8] (gameapi.c ga_units).
    char *list = (char *)RDP(p, 0x18);
    if (!list) return;
    void **u = (void **)RDP(list, 4);
    int count = (int)RD32(list, 8);
    for (int i = count - 1; i >= 0 && i >= count - 64; i--) {
        char *x0 = (char *)u[i];
        void *m = x0 ? RDP(x0, 0xc) : NULL;
        if (!m || *(short *)((char *)m + 0x10) != type) continue;
        float ux = *(float *)(x0 + 0x3c), uy = *(float *)(x0 + 0x40);
        if (ux - x > 2 || x - ux > 2 || uy - y > 2 || y - uy > 2) continue;
        if (x0[0x194]) { x0[0x194] = 0; wl_log("gamecmd: unit %d (type %d) will not be captured", (int)RD32(x0, 8), type); }
        break;
    }
}

// Attack ("order"): the game's command 0, same layout as move (builder 0x809900: 0x80999e movb $0x0,
// target id = [target+8] at +4 (0x80998d..0x809998), count +8, x +0xc, y +0x10, ids +0x14; executor
// 0x809fe0 case 0 -> 0x80c650). x/y = the target's position. Sent as the owner like move.
int wl_cmd_attack(int unit_id, int target_id) {
    void *w0 = world();
    void *u = w0 ? ((GetObj_t)0x733bf0)(w0, unit_id) : NULL;
    void *t = w0 ? ((GetObj_t)0x733bf0)(w0, target_id) : NULL;
    void *owner = u ? RDP(u, 0x10) : NULL;
    if (!owner || !t) return -1;
    int pid = (int)RD32(owner, 0x1adc);
    float x = *(float *)((char *)t + 0x3c), y = *(float *)((char *)t + 0x40);
    void *w; int rc;
    BYTE *pkt = begin(pid, 0x18, &w, &rc);
    if (!pkt) return rc;
    pkt[0] = 0;
    pkt[1] = (BYTE)(pid == 0 || is_computer(owner) ? 0 : pid);
    *(int *)(pkt + 4) = target_id;
    pkt[8] = 1;
    memcpy(pkt + 0xc, &x, 4);
    memcpy(pkt + 0x10, &y, 4);
    *(int *)(pkt + 0x14) = unit_id;
    return send_cmd_as(w, pkt, 0x18, pid);
}

// Runs on EVERY machine when the turn executes the packet.
static int exec_wl(BYTE *pkt) {
    if (pkt[0] != 0x05 || (pkt[3] != WL_MARK && pkt[3] != WL_MARK_TECH && pkt[3] != WL_MARK_AGE &&
                           pkt[3] != WL_MARK_SPAWN_NC && pkt[3] != WL_MARK_RES16 && pkt[3] != WL_MARK_SETRES16 &&
                           pkt[3] != WL_MARK_DEFEAT && pkt[3] != WL_MARK_WIN && pkt[3] != WL_MARK_REMOVE))
        return 0;
    void *p = player_obj(world(), pkt[1], NULL);
    if (!p) return 1;
    if (pkt[3] == WL_MARK) {
        float amount;
        memcpy(&amount, pkt + 4, 4);
        AddRes_t add = (AddRes_t)(*(void ***)p)[0x64 / 4];
        add(p, pkt[2], amount, 0);
    } else if (pkt[3] == WL_MARK_RES16) {
        short res = *(short *)(pkt + 4);
        int count = (int)RD32(p, 0x38);
        if (res >= 0 && res < count) {
            float amount;
            memcpy(&amount, pkt + 8, 4);
            AddRes_t add = (AddRes_t)(*(void ***)p)[0x64 / 4];
            add(p, (int)res, amount, 0);
        }
    } else if (pkt[3] == WL_MARK_SETRES16) {
        short res = *(short *)(pkt + 4);
        int count = (int)RD32(p, 0x38);
        if (res >= 0 && res < count) {
            float val;
            memcpy(&val, pkt + 8, 4);
            if (res >= WL_CUSTOM_RES_FIRST) {
                const WlResourceDef *def = wl_resource_by_id(res);
                if (def && def->max > 0.0f && val > def->max) val = def->max;
            }
            SetRes_t set = (SetRes_t)(*(void ***)p)[0x60 / 4];
            set(p, (int)res, val);
        }
    } else if (pkt[3] == WL_MARK_SPAWN_NC) {
        float x, y;
        memcpy(&x, pkt + 8, 4);
        memcpy(&y, pkt + 12, 4);
        exec_spawn_neutral(p, *(short *)(pkt + 4), x, y);
    } else if (pkt[3] == WL_MARK_REMOVE) {
        // vtable slot 30 (+0x78) of every object class: RGE_Static_Object (vtable 0x9eab30) and
        // RGE_Animated_Object (0x9e7328) both point to 0x8b2e10 (plain ret, __thiscall(obj)): it
        // releases the object's map/LOS state (0x755540 / 0x8b7670) and sets hp [obj+0x34] = 0, so
        // the object goes. TRIBE_Combat's delete slot 0x8c47b0 ends in the same jmp [vtable+0x78].
        void *o = ((GetObj_t)0x733bf0)(world(), *(int *)(pkt + 4));
        void *own = o ? RDP(o, 0x10) : NULL;
        void *m = o ? RDP(o, 0xc) : NULL;
        if (own && m && (int)RD32(own, 0x1adc) == pkt[1] && *(BYTE *)((char *)m + 4) < 70)
            ((void (__thiscall *)(void *))(*(void ***)o)[0x78 / 4])(o);
    } else if (pkt[3] == WL_MARK_AGE) {
        exec_set_age(p, pkt[2]);
    } else if (pkt[3] == WL_MARK_DEFEAT) {
        if (g_win_defeat_ok && pkt[1] > 0) {
            extern int g_wl_in_defeat_flag;
            g_wl_in_defeat_flag = 1;
            ((VoidPlayer_t)0x750580)(p);
            g_wl_in_defeat_flag = 0;
        }
    } else if (pkt[3] == WL_MARK_WIN) {
        if (g_win_defeat_ok && pkt[1] > 0) {
            if (pkt[2]) ((VoidPlayer_t)0x7558b0)(p);
            else ((VoidPlayer_t)0x7558a0)(p);
        }
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
    static const BYTE b_loss[] = { 0x80, 0x79, 0x35, 0x00, 0x75, 0x07, 0x8b, 0x01, 0x6a, 0x02, 0xff, 0x50, 0x04, 0xc3 };
    static const BYTE b_win[]  = { 0x80, 0x79, 0x35, 0x00, 0x75, 0x07, 0x8b, 0x01, 0x6a, 0x01, 0xff, 0x50, 0x04, 0xc3 };
    static const BYTE b_wint[] = { 0x55, 0x8b, 0xec, 0x51, 0x53, 0x56, 0x57, 0x8b, 0xd9, 0xe8, 0xe2, 0xff, 0xff, 0xff };
    if (memcmp((const void *)0x813460, b_exec, sizeof b_exec) != 0) {
        wl_log("gamecmd: unexpected bytes at 0x813460, game commands off");
        return;
    }
    g_win_defeat_ok = (memcmp((const void *)0x750580, b_loss, sizeof b_loss) == 0 &&
                       memcmp((const void *)0x7558a0, b_win,  sizeof b_win)  == 0 &&
                       memcmp((const void *)0x7558b0, b_wint, sizeof b_wint) == 0);
    if (!g_win_defeat_ok) wl_log("gamecmd: unexpected bytes at 0x750580/0x7558a0/0x7558b0, Win/Defeat off");
    o_exec = (Exec_t)wl_hook(0x813460, exec_hook, 8);
    wl_log("gamecmd: command executor hook %s (win/defeat %s)",
           o_exec ? "installed" : "FAILED", g_win_defeat_ok ? "ok" : "off");
}

int g_wl_in_defeat_flag;
int wl_gamecmd_in_defeat(void) { return g_wl_in_defeat_flag; }

