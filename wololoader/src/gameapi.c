// Read-only game state for the Lua API (script.c reaches it through the
// WlGameApi table, so the native test can run without the game). Reading only:
// nothing here changes the game, so it is multiplayer safe.
//
// Verified addresses (2026-11-15, see monk.c and re/research/monk_autoconvert.md):
// - world = [[0xaf2c58]+0xc8]; players: [world+0x184]..[world+0x188], 8 bytes each,
//   player pointer first; local player id [world+0x174] (0x540c90).
// - player units: list [player+0x18], units [list+4], count [list+8].
// - player resources: float array at [player+0x3c] (0x748055: mov 0x3c(%edi),%eax;
//   0x748060: movss 0x80(%eax) = resource 32). Ids: 0 food, 1 wood, 2 stone, 3 gold,
//   4 population headroom, 6 current age, 11 current population, 32 bonus pop cap.
// - is_enemy 0x74f320 __thiscall(player, id) ret 4.
// - unit: id [unit+8], owner [unit+0x10], master [unit+0xc] (class int16 +0x16, type byte
//   +4: 80 = building), position floats [unit+0x3c]/[unit+0x40], alive [unit+0x4c] < 7
//   and [unit+0x24] == 0.
#include "wololoader.h"

#define RDP(p, o) (*(void **)((char *)(p) + (o)))
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))
typedef int (__thiscall *IsEnemy_t)(void *player, int id);

static char *ptab(int *n) {
    DWORD game = *(DWORD *)0xaf2c58;
    void *world = game ? RDP((void *)game, 0xc8) : NULL;
    char *tab = world ? (char *)RDP(world, 0x184) : NULL, *end = world ? (char *)RDP(world, 0x188) : NULL;
    *n = tab && end > tab ? (int)((end - tab) / 8) : 0;
    return tab;
}

static void *player(int id) {
    int n;
    char *tab = ptab(&n);
    return (tab && id >= 0 && id < n) ? *(void **)(tab + id * 8) : NULL;
}

static int ga_local(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    void *world = game ? RDP((void *)game, 0xc8) : NULL;
    return world ? (int)RD32(world, 0x174) : -1;
}

static int ga_count(void) { int n; ptab(&n); return n; }

static int ga_resource(int pid, int res, float *out) {
    void *p = player(pid);
    float *arr = p ? (float *)RDP(p, 0x3c) : NULL;
    // [player+0x38] = live num_attributes (WorldPlayerBase::ctor 0x74c420,
    // verified in re/out/decompiled.c). Supports all custom resources 211..32766.
    int count = p ? (int)RD32(p, 0x38) : 0;
    if (!arr || res < 0 || res >= count) return 0;
    *out = arr[res];
    return 1;
}

static int ga_is_enemy(int a, int b) {
    void *p = player(a);
    return (p && b > 0 && b < ga_count()) ? ((IsEnemy_t)0x74f320)(p, b) != 0 : 0;
}

// One unit's info; FALSE for dead/removed units. Unit type id = master int16 at +0x10 (the
// index into the player's masters array [player+0x14], 0x8c48af..0x8c48b9); owner id =
// [[unit+0x10]+0x1adc] (player id, 0x812695..0x8126a1).
static int unit_info(void *x, WlUnitInfo *o) {
    if (!x || *(unsigned char *)((char *)x + 0x4c) >= 7 || RD32(x, 0x24) != 0) return 0;
    void *m = RDP(x, 0xc), *own = RDP(x, 0x10);
    o->id = (int)RD32(x, 8);
    o->x = *(float *)((char *)x + 0x3c);
    o->y = *(float *)((char *)x + 0x40);
    o->cls = m ? *(short *)((char *)m + 0x16) : -1;
    o->building = m ? *(unsigned char *)((char *)m + 4) == 80 : 0;
    o->type = m ? *(short *)((char *)m + 0x10) : -1;
    o->owner = own ? (int)RD32(own, 0x1adc) : -1;
    o->state = *(unsigned char *)((char *)x + 0x4c);   // 2 = ready; a building below 2 is still a foundation
    return 1;
}

static int ga_units(int pid, WlUnitInfo *out, int max) {
    void *p = player(pid);
    char *list = p ? (char *)RDP(p, 0x18) : NULL;
    if (!list) return 0;
    void **u = (void **)RDP(list, 4);
    int count = (int)RD32(list, 8), n = 0;
    for (int i = 0; i < count && n < max; i++)
        if (unit_info(u[i], &out[n])) n++;
    return n;
}

// Like ga_units but only units of one type, looking at the whole list (Gaia has thousands of
// trees and mines, so the first 2048 of ga_units may not reach a unit created later).
// Player object lists {?, void **objects @4, int count @8}: +0x18 = the normal objects; +0x1c holds
// objects the game keeps apart (Genie RGE_Player "sleeping" objects: flags, the 837 REVEAL map
// revealer, ...; seen 2026-11-26: revealers created by Last Stand were not in +0x18 and so never
// deleted). UnitsOfType scans both so a script can find what it created.
static int scan_list(char *list, int type, WlUnitInfo *out, int n, int max) {
    if (!list || IsBadReadPtr(list, 12)) return n;
    void **u = (void **)RDP(list, 4);
    int count = (int)RD32(list, 8);
    if (!u || count <= 0 || count > 200000 || IsBadReadPtr(u, (UINT_PTR)count * 4)) return n;
    WlUnitInfo tmp;
    for (int i = 0; i < count && n < max; i++)
        if (unit_info(u[i], &tmp) && tmp.type == type) out[n++] = tmp;
    return n;
}
static int ga_units_of_type(int pid, int type, WlUnitInfo *out, int max) {
    void *p = player(pid);
    if (!p) return 0;
    int n = scan_list((char *)RDP(p, 0x18), type, out, 0, max);
    int before = n;
    n = scan_list((char *)RDP(p, 0x1c), type, out, n, max);
    static int logged;
    if (n > before && !logged) { logged = 1; wl_log("gameapi: %d unit(s) of type %d in player %d list +0x1c", n - before, type, pid); }
    return n;
}

// Selection of the local player: player+0x160 selected[60] unit pointers, count int at +0x254
// (overflow.c PLAYER_SEL / PLAYER_CNT; the units beyond 60 of selection_250 are not included).
static int ga_selected(WlUnitInfo *out, int max) {
    void *p = player(ga_local());
    if (!p) return 0;
    int cnt = (int)RD32(p, 0x254), n = 0;
    void **sel = (void **)((char *)p + 0x160);
    for (int i = 0; i < cnt && i < 60 && n < max; i++)
        if (unit_info(sel[i], &out[n])) n++;
    return n;
}

// Game time: [world+0x178] in ms (see luagame.c game_clock).
static double ga_time(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    void *world = game ? RDP((void *)game, 0xc8) : NULL;
    return world ? RD32(world, 0x178) / 1000.0 : -1.0;
}

// Researched techs: tech tree [player+0x1ae8], entries [tt] 16 bytes each with status int16 at +4
// (3 = researched), count int16 at [tt+4] (complete_tech 0x84d300, see gamecmd.c).
static int ga_researched(int pid, short *out, int max) {
    void *p = player(pid);
    void *tt = p ? RDP(p, 0x1ae8) : NULL;
    BYTE *ent = tt ? (BYTE *)RDP(tt, 0) : NULL;
    if (!ent) return 0;
    int count = *(short *)((BYTE *)tt + 4), n = 0;
    for (int i = 0; i < count && n < max; i++)
        if (*(short *)(ent + i * 16 + 4) == 3) out[n++] = (short)i;
    return n;
}

static const char *ga_mode(void) {
    return wl_gamemode_current_key();
}

// Civ id: [[player+0x1ae4]+8] (the civ object of the player; buildpage.c get_player_civ reads it
// for the command panel, where the mod buttons per civ are verified in game).
static int ga_player_civ(int pid) {
    void *p = player(pid);
    BYTE *civ = p ? (BYTE *)RDP(p, 0x1ae4) : NULL;
    return civ ? *(int *)(civ + 8) : -1;
}

// Nearest map tile to (x, y) that no tree, mine, rock, cliff or building covers, within `radius`
// tiles. Scans every player's two object lists ([player+0x18] and +0x1c, see ga_units_of_type),
// all of them: Gaia has thousands of trees, more than a Lua unit list holds (2048). Gaia animals
// (class 58 livestock, 9 prey animal, 7 predator) do not block. Returns 1 and the tile centre.
#define FS_MAX 24
static unsigned char g_fs[2 * FS_MAX + 1][2 * FS_MAX + 1];
static void fs_mark_list(char *list, int cx, int cy, int r, int gaia) {
    if (!list || IsBadReadPtr(list, 12)) return;
    void **u = (void **)RDP(list, 4);
    int count = (int)RD32(list, 8);
    if (!u || count <= 0 || count > 400000 || IsBadReadPtr(u, (UINT_PTR)count * 4)) return;
    for (int i = 0; i < count; i++) {
        char *x = (char *)u[i];
        if (!x || *(unsigned char *)(x + 0x4c) >= 7 || RD32(x, 0x24) != 0) continue;
        char *m = (char *)RDP(x, 0xc);
        if (!m) continue;
        int type = *(unsigned char *)(m + 4), cls = *(short *)(m + 0x16);
        if (gaia) { if (cls == 58 || cls == 9 || cls == 7) continue; }
        else if (type != 80) continue;                     // players: only buildings block
        int tx = (int)*(float *)(x + 0x3c) - cx + r, ty = (int)*(float *)(x + 0x40) - cy + r;
        int pad = (type == 80) ? 2 : 0;                    // buildings cover more than one tile
        for (int dx = -pad; dx <= pad; dx++)
            for (int dy = -pad; dy <= pad; dy++) {
                int a = tx + dx, b = ty + dy;
                if (a >= 0 && b >= 0 && a <= 2 * r && b <= 2 * r) g_fs[a][b] = 1;
            }
    }
}
static int ga_free_spot(float fx, float fy, int r, float *ox, float *oy) {
    int n = ga_count();
    if (n <= 0) return 0;
    if (r < 1) r = 1;
    if (r > FS_MAX) r = FS_MAX;
    int cx = (int)fx, cy = (int)fy;
    memset(g_fs, 0, sizeof g_fs);
    for (int pid = 0; pid < n; pid++) {
        void *p = player(pid);
        if (!p) continue;
        fs_mark_list((char *)RDP(p, 0x18), cx, cy, r, pid == 0);
        fs_mark_list((char *)RDP(p, 0x1c), cx, cy, r, pid == 0);
    }
    for (int d = 0; d <= r; d++)
        for (int dx = -d; dx <= d; dx++)
            for (int dy = -d; dy <= d; dy++) {
                if (dx != -d && dx != d && dy != -d && dy != d) continue;
                int a = dx + r, b = dy + r;
                if (cx + dx < 2 || cy + dy < 2) continue;
                // a free tile with free neighbours, so a group of units fits
                int ok = !g_fs[a][b];
                for (int k = -1; ok && k <= 1; k++)
                    for (int l = -1; ok && l <= 1; l++) {
                        int aa = a + k, bb = b + l;
                        if (aa >= 0 && bb >= 0 && aa <= 2 * r && bb <= 2 * r && g_fs[aa][bb]) ok = 0;
                    }
                if (ok) { *ox = cx + dx + 0.5f; *oy = cy + dy + 0.5f; return 1; }
            }
    return 0;
}

static const WlGameApi k_api = { ga_local, ga_count, ga_resource, ga_is_enemy, ga_units,
    wl_cmd_add_resource, wl_cmd_research, wl_cmd_spawn, wl_cmd_delete, ga_selected, ga_time, wl_cmd_set_age, ga_researched, wl_cmd_move, ga_units_of_type, wl_cmd_spawn_neutral,
    wl_cmd_set_resource, wl_resources_list, wl_resource_resolve, ga_mode, wl_cmd_win, wl_cmd_defeat, ga_player_civ, wl_cmd_attack, wl_gather_by_key,
    ga_free_spot };
const WlGameApi *wl_game_api(void) { return &k_api; }
