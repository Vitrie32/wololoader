// Monk auto-convert (Wololoader Base feature "monk_autoconvert", OFF unless set
// to true): once a second, every IDLE monk of the local player that sees an
// enemy unit (not a building) is ordered to convert the nearest one, exactly as
// if the player had right-clicked it. The order goes through the game's own
// command (network packet), so it is multiplayer safe.
//
// Verified in the disassembly (2026-11-15, research re/research/monk_autoconvert.md):
// - world = [[0xaf2c58]+0xc8]; player table = [world+0x184] .. [world+0x188],
//   8 bytes per entry, player pointer first; local player id = [world+0x174]
//   (TRIBE_Game::get_local_player 0x540c90: mov 0x184(%eax),%eax; mov (%eax,%ecx,8),%eax).
// - player units: list = [player+0x18], units = [list+4], count = [list+8]
//   (recall loop 0x754820..0x7548d7).
// - unit: owner player [unit+0x10], master [unit+0xc] (class int16 at +0x16, 18 = monks;
//   type byte at +4, 80 = building; line of sight float at +0x2c),
//   position floats [unit+0x3c], [unit+0x40]; alive: [unit+0x4c] < 7 and [unit+0x24] == 0.
// - 0x4b07c0 unit is_idle, __thiscall(unit), plain ret, returns al = 1 when idle.
// - 0x74f320 WorldPlayer::is_enemy(id), __thiscall, ret 4 (diplomacy [player+0x70],
//   value 3 = enemy; Gaia is never an enemy, so sheep and animals are skipped).
// - 0x74db20 WorldPlayer order unit -> target, __thiscall(player, unit, target, float x,
//   float y), ret 0x10: builds the type 0x00 order with the game builder 0x809900 and sends it.
#include "wololoader.h"

#define RDP(p, o) (*(void **)((char *)(p) + (o)))
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))

typedef char (__thiscall *IsIdle_t)(void *unit);
typedef int (__thiscall *IsEnemy_t)(void *player, int id);
typedef void (__thiscall *OrderTarget_t)(void *player, void *unit, void *target, float x, float y);

static int g_on = -1;

static int alive(void *u) {
    return u && *(unsigned char *)((char *)u + 0x4c) < 7 && RD32(u, 0x24) == 0;
}

void wl_monk_tick(void) {
    if (g_on < 0) g_on = wl_feature_opt("monk_autoconvert");
    if (!g_on) return;
    DWORD game = *(DWORD *)0xaf2c58;
    if (!game) return;
    void *world = RDP((void *)game, 0xc8);
    if (!world) return;
    char *tab = (char *)RDP(world, 0x184), *end = (char *)RDP(world, 0x188);
    int nplayers = tab && end > tab ? (int)((end - tab) / 8) : 0;
    int me = (int)RD32(world, 0x174);
    if (me <= 0 || me >= nplayers) return;
    void *player = *(void **)(tab + me * 8);
    if (!player) return;
    char *list = (char *)RDP(player, 0x18);
    if (!list) return;
    void **units = (void **)RDP(list, 4);
    int count = (int)RD32(list, 8), ordered = 0;
    for (int i = 0; i < count && ordered < 10; i++) {
        void *m = units[i];
        if (!alive(m)) continue;
        void *master = RDP(m, 0xc);
        if (!master || *(short *)((char *)master + 0x16) != 18) continue;     // monks only
        if (!((IsIdle_t)0x4b07c0)(m)) continue;
        float mx = *(float *)((char *)m + 0x3c), my = *(float *)((char *)m + 0x40);
        float los = *(float *)((char *)master + 0x2c);
        float best_d = los * los;
        void *best = NULL;
        for (int p = 1; p < nplayers; p++) {
            if (p == me || !((IsEnemy_t)0x74f320)(player, p)) continue;
            void *ep = *(void **)(tab + p * 8);
            char *el = ep ? (char *)RDP(ep, 0x18) : NULL;
            if (!el) continue;
            void **eu = (void **)RDP(el, 4);
            int en = (int)RD32(el, 8);
            for (int k = 0; k < en; k++) {
                void *t = eu[k];
                if (!alive(t)) continue;
                void *tm = RDP(t, 0xc);
                if (!tm || *(unsigned char *)((char *)tm + 4) == 80) continue;   // no buildings
                float dx = *(float *)((char *)t + 0x3c) - mx, dy = *(float *)((char *)t + 0x40) - my;
                float d = dx * dx + dy * dy;
                if (d < best_d) { best_d = d; best = t; }
            }
        }
        if (!best) continue;
        ((OrderTarget_t)0x74db20)(player, m, best,
            *(float *)((char *)best + 0x3c), *(float *)((char *)best + 0x40));
        ordered++;
    }
    if (ordered) wl_log("monk: %d idle monk(s) sent to convert", ordered);
}
