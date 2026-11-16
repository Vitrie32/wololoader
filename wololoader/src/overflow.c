// Overflow selection, stage 1b + 2 + 3: catch the units the game refuses because
// selected[60] is full, remember them in our own list (max 190 extra, 250 total)
// and, for the verified order types, send extra packets with the same order for
// them, 60 ids per packet (send_clone below; types not verified stay dry run). Design: re/selection/overflow_design.md,
// hooks and evidence: re/selection/overflow_stage1.md.
//
// Verified in the disassembly of AoK HD.exe 5.8 (objdump, 2026-11-15):
//  - 0x754a20 WorldPlayer::select_unit, __thiscall(player, unit, flag), ret 8.
//    Returns 1 = selected / already selected, 0 = refused. The 60 limit is
//    `cmp $0x3c,%eax; jge 0x754c15` at 0x754b23 (0x754c15 = return 0). Other
//    return-0 paths: unit null (0x754a2c) and visibility/class checks
//    (0x754a8a..0x754aa8, meant for foreign units). We therefore only record a
//    unit when count == 60 after the call AND it is our own, alive, unselected.
//    Owner = [unit+0x10] (compared with the player at 0x754ae6), selected flag
//    = bit 0 of [unit+0x3a] (0x754ab1), unit id = [unit+8] (0x812f37).
//  - 0x755450 unselect_all, __thiscall(player), plain ret.
//  - 0x755540 unselect by pointer, __thiscall(player, unit), ret 4. Searches
//    selected[] only, so an overflow unit would be ignored by it.
//  - 0x7554a0 unselect by index: NOT hooked (it addresses selected[] slots,
//    overflow units are never in selected[]).
//  - 0x80ef70 command send, __thiscall(cmd_obj, packet, size, player), ret 0xc,
//    returns al (0x80ef8f sete). packet[0] = type, packet[1] = unit count,
//    ids (int32 = [unit+8]) at packet+K (K per type, see g_types).
//  - 0x733bf0 object by id, __thiscall(world, id), ret 4, returns 0 if none;
//    world = [[0xaf2c58]+0xc8] (as in 0x80e610). Used to re-resolve every
//    stored unit before it is touched, so freed units are never dereferenced.
//  - Game's own "unit still valid" test (0x755879..0x755883): [unit+0x4c] < 7
//    and [unit+0x24] == 0.
#include "wololoader.h"
#include <string.h>
#include <stdio.h>

// Stage 1b: log EVERY packet through 0x80ef70 while the overflow list is not
// empty (max 20 lines per second) with type, size, first 16 bytes and how many
// of the selected[] ids occur in it. Set to 0 to turn off.
#define OVERFLOW_DIAG 0

#define OVF_MAX     190     // extra units beyond the game's 60 (250 total)
#define SEL_MAX     60
#define CHUNK_MAX   60      // ids per clone packet: see send_clone
#define PLAYER_SEL  0x160   // player+0x160: selected[60] (unit pointers)
#define PLAYER_CNT  0x254   // player+0x254: selection count

typedef int   (__thiscall *SelectUnit_t)(void *player, void *unit, int flag);
typedef void  (__thiscall *UnselectAll_t)(void *player);
typedef void  (__thiscall *UnselectPtr_t)(void *player, void *unit);
typedef int   (__thiscall *Send_t)(void *cmd, BYTE *packet, int size, int player);
typedef void *(__thiscall *GetObj_t)(void *world, int id);
typedef int   (__thiscall *SetGroup_t)(void *player, int group);
typedef int   (__thiscall *RecallGroup_t)(void *player, int group);

static SelectUnit_t  o_select;
static UnselectAll_t o_unselect_all;
static UnselectPtr_t o_unselect_ptr;
static Send_t        o_send;
static SetGroup_t    o_set_group;
static RecallGroup_t o_recall_group;
static const GetObj_t get_obj = (GetObj_t)0x733bf0;

typedef struct { void *unit; int id; } OvfUnit;
static OvfUnit g_ovf[OVF_MAX];
static int     g_n;
static void   *g_player;     // player object the list belongs to

#define MAX_GROUPS  10
#define MAX_PGROUPS 8

typedef struct {
    void *player;
    struct {
        int count;
        int ids[OVF_MAX];
    } g[MAX_GROUPS];
} PlayerGroups;

static PlayerGroups g_pgroups[MAX_PGROUPS];

static PlayerGroups *get_player_groups(void *player, BOOL create) {
    if (!player) return NULL;
    for (int i = 0; i < MAX_PGROUPS; i++) {
        if (g_pgroups[i].player == player) return &g_pgroups[i];
    }
    if (!create) return NULL;
    for (int i = 0; i < MAX_PGROUPS; i++) {
        if (!g_pgroups[i].player) {
            memset(&g_pgroups[i], 0, sizeof(g_pgroups[i]));
            g_pgroups[i].player = player;
            return &g_pgroups[i];
        }
    }
    memset(&g_pgroups[0], 0, sizeof(g_pgroups[0]));
    g_pgroups[0].player = player;
    return &g_pgroups[0];
}

static int group_to_idx(int group) {
    if (group >= 1 && group <= 10) return group - 1;
    if (group == 0) return 9; // key '0'
    return -1;
}

#define RD8(p, off)  (*(const BYTE  *)((const BYTE *)(p) + (off)))
#define RD32(p, off) (*(const DWORD *)((const BYTE *)(p) + (off)))

static void *world(void) {
    DWORD base = *(const DWORD *)0xaf2c58;      // `mov 0xaf2c58,%eax` in 0x80e626
    return base ? (void *)RD32((void *)base, 0xc8) : NULL;
}

// Stage 5: every unit in our list carries the game's "selected" flag (bit 0 of [unit+0x3a]) so the
// game draws the selection circle and health bar for it (readers: re/selection/overflow_stage1.md,
// Stage 5). The flag is ours to clear again whenever the unit leaves the list.
static void flag_set(void *unit)   { *((BYTE *)unit + 0x3a) |= 1; }
static void flag_clear(void *unit) { *((BYTE *)unit + 0x3a) &= (BYTE)~1; }   // keep the other bits (2/4/8)

// The stored pointer is still the live object with that id (never touch memory otherwise).
static BOOL entry_live(const OvfUnit *e) {
    void *w = world();
    return w && e->id != -1 && get_obj(w, e->id) == e->unit;
}

static BOOL in_sel_ptr(void *player, void *unit) {       // pointer scan of selected[60]
    for (int i = 0; i < SEL_MAX; i++)
        if (RD32(player, PLAYER_SEL + 4 * i) == (DWORD)unit) return TRUE;
    return FALSE;
}

// Still the same live unit, ours, alive and not (again) in the real selection? The old test
// "flag clear" cannot be used any more: the flag is now set on purpose.
static BOOL entry_ok(void *player, const OvfUnit *e) {
    if (!entry_live(e)) return FALSE;                     // freed / id reused
    return RD32(e->unit, 0x10) == (DWORD)player           // still ours
        && RD8(e->unit, 0x4c) < 7 && RD32(e->unit, 0x24) == 0   // game's alive test
        && !in_sel_ptr(player, e->unit);                  // not in selected[] now
}

// Forget the whole list and take the flag back from every unit that still exists.
static void drop_all(void) {
    for (int i = 0; i < g_n; i++)
        if (entry_live(&g_ovf[i])) flag_clear(g_ovf[i].unit);
    g_n = 0;
}

static void prune(void) {
    // O(n) get_obj lookups. Callers: once per select burst, once per order, and
    // wl_overflow_count (throttled), never once per catch (that would be O(n^2)).
    int k = 0;
    for (int i = 0; i < g_n; i++) {
        if (entry_ok(g_player, &g_ovf[i])) { g_ovf[k++] = g_ovf[i]; continue; }
        // Dropped. A live unit that is not in selected[] (dead, converted) must lose our flag;
        // one that got into selected[] for real keeps it (the game owns it now).
        if (entry_live(&g_ovf[i]) && !in_sel_ptr(g_player, g_ovf[i].unit)) flag_clear(g_ovf[i].unit);
    }
    g_n = k;
}

static int find(void *unit) {
    for (int i = 0; i < g_n; i++) if (g_ovf[i].unit == unit) return i;
    return -1;
}

static void remove_at(int i) {
    memmove(&g_ovf[i], &g_ovf[i + 1], (size_t)(g_n - i - 1) * sizeof g_ovf[0]);
    g_n--;
}

// Burst statistics for select_unit calls made while the selection is full.
// The old log line only printed the 1st and every 10th catch, so "1 caught"
// could not be told from "1..9 caught". A summary line is written when the
// burst is over (next select call after 300 ms, next order, next clear).
static struct { int refused, caught, dup, other, full; DWORD last; } g_st;

static void flush_stats(void) {
    if (!g_st.refused) return;
    wl_log("overflow: select burst: refused@60=%d caught=%d already_listed=%d not_own_or_dead=%d list_full=%d (list now %d)",
           g_st.refused, g_st.caught, g_st.dup, g_st.other, g_st.full, g_n);
    memset(&g_st, 0, sizeof g_st);
}

// ---- select_unit 0x754a20 (stolen: 55 8b ec 56 57 = push ebp; mov ebp,esp; push esi; push edi)
// Returns the game's result r. *caught = 1 when the unit is (now or already) in the overflow list.
static int select_core(void *player, void *unit, int flag, int *caught) {
    *caught = 0;
    // Stage 5: a listed unit has the selected flag, and the game answers "already selected" (1)
    // for a flagged unit without adding it (0x754ab1 test $1; 0x754ab7 je 0x754c0a). So when there
    // is room again, take it out of our list and clear the flag first: the game then adds it to
    // selected[] normally (and sets the flag itself, 0x754b43).
    if (g_n > 0 && unit && player == g_player && (int)RD32(player, PLAYER_CNT) < SEL_MAX) {
        int i = find(unit);
        if (i >= 0) { remove_at(i); flag_clear(unit); }
    }
    int r = o_select(player, unit, flag);
    if (r != 0 || !unit || (int)RD32(player, PLAYER_CNT) != SEL_MAX) return r;
    DWORD now = GetTickCount();
    if (g_st.refused && now - g_st.last > 300) flush_stats();
    g_st.last = now;
    const BOOL burst_start = (g_st.refused == 0);
    g_st.refused++;
    // Refused while the selection is full. Own, alive, not selected -> overflow.
    if (RD32(unit, 0x10) != (DWORD)player || (RD8(unit, 0x3a) & 1)
        || RD8(unit, 0x4c) >= 7 || RD32(unit, 0x24) != 0) { g_st.other++; return r; }
    if (g_player != player) {
        if (g_player) wl_log("overflow: player object changed %p -> %p, list reset", g_player, player);
        drop_all();              // flags of the old list's units that still exist
        memset(g_pgroups, 0, sizeof g_pgroups);
        g_player = player;
    }
    // Validate the whole list once per burst (a double-click refuses ~140 units in one frame);
    // the per-insert dedup below is a plain O(n) pointer scan, n <= 190. Orders prune again
    // before cloning, so a stale entry can never be sent.
    if (burst_start) prune();
    if (find(unit) >= 0) { g_st.dup++; flag_set(unit); *caught = 1; return r; }
    if (g_n >= OVF_MAX) {
        static int full_logged;
        g_st.full++;
        if (!full_logged++) wl_log("overflow: list full (%d), further units ignored", OVF_MAX);
        return r;
    }
    g_ovf[g_n].unit = unit;
    g_ovf[g_n].id = (int)RD32(unit, 8);
    g_n++;
    flag_set(unit);          // Stage 5: circle + health bar (the game's renderers test this bit)
    g_st.caught++;
    *caught = 1;
    // First 5 catches, then every 10th (the burst summary has the totals).
    if (g_n <= 5 || g_n % 10 == 0)
        wl_log("overflow: caught unit id=%d type=%u (overflow list %d, would show %d selected)",
               g_ovf[g_n - 1].id,
               (unsigned)*(const WORD *)((const BYTE *)RD32(unit, 0xc) + 0x10), g_n, SEL_MAX + g_n);
    return r;
}

static int __fastcall select_hook(void *player, void *edx, void *unit, int flag) {
    (void)edx;
    int c;
    return select_core(player, unit, flag, &c);
}

// Box select 0x87cc60: `87cd2d call 0x754a20; 87cd32 test eax,eax; 87cd34 je 0x87cd4f` leaves the
// loop at the first refusal. This wrapper replaces that CALL (bytes e8 ee 7c ed ff, target verified
// = 0x754a20): when the refused unit went into our overflow list it reports "selected" (1) so the
// loop goes on with the next candidate. Same convention as the original (thiscall, ret 8).
// Stage 4 relocates the candidate buffer 0xde0430 (60 records) into our own array of BOX_MAX (250)
// records (install_box_buffer below), so a box now yields up to 250 candidates; the first 60 are
// selected by the game, the rest land here and go into the overflow list.
static int __fastcall box_select_call(void *player, void *edx, void *unit, int flag) {
    (void)edx;
    int c;
    int r = select_core(player, unit, flag, &c);
    return (r == 0 && c) ? 1 : r;
}

// ---- unselect_all 0x755450 (stolen: 57 8b f9 83 bf 54 02 00 00 00 = push edi; mov edi,ecx; cmpl $0,0x254(%edi))
static void __fastcall unselect_all_hook(void *player, void *edx) {
    (void)edx;
    flush_stats();
    if (player == g_player && g_n > 0) {
        wl_log("overflow: selection cleared (caller %p), dropping %d overflow unit(s)", __builtin_return_address(0), g_n);
        drop_all();
    }
    o_unselect_all(player);
}

// ---- unselect by pointer 0x755540 (stolen: 55 8b ec 53 8b d9 57 = push ebp; mov ebp,esp; push ebx; mov ebx,ecx; push edi)
static void __fastcall unselect_ptr_hook(void *player, void *edx, void *unit) {
    (void)edx;
    // No player check: the unit's destructor (0x8b14cb, flag set -> call 0x755540 with the owner),
    // garrison (0x8b9480) and the other removal paths call this with the unit that is going away,
    // possibly with another owner than our list's player (conversion). Pointer identity is enough
    // and nothing is dereferenced except the unit the game just handed us. This is what keeps a
    // flagged overflow unit from outliving its object.
    if (g_n > 0) {
        int i = find(unit);
        if (i >= 0) {
            remove_at(i);
            flag_clear(unit);
            wl_log("overflow: unit unselected, overflow list %d", g_n);
        }
    }
    o_unselect_ptr(player, unit);
}

// Unit-list order packets. Ids are int32 at packet+K, size == K + 4*count.
// Types, K and builders from overflow_design.md section 2.3 (all `lea K(,%esi,4)` verified).
// Stage 1b: the right-click orders are types 0x00 / 0x03 (builders 0x809900 / 0x8097d0) and 0x10
// (0x8095b0), which were missing. They differ from the others:
//  * count byte is NOT packet[1]: type 0x00/0x03 `mov %bl,0x8(%esi)` (0x8099e0 / 0x8098ae),
//    0x02 `mov %bl,0x8(%edx)` (0x809ee0), 0x10 `mov %bl,0x2(%esi)` (0x80967c). Others: packet[1].
//  * 0x00/0x03/0x10 call 0x80ebc0 first: if the unit list equals the one of the previous order
//    (cmd+0x28 short count, cmd+0x2c flag, cmd+0x30.. ids) it returns 0 and sets the count byte to
//    0xff: the packet then has NO ids and size == K (0x14 / 8); the executor reuses the last list.
//    Otherwise it stores the list in cmd+0x28/0x30.. and returns 4*count (size = K + 4*count).
//    (0x80ebc0 at 0x80ebd2..0x80ec95.)
//
// Stage 2: `live` = the clone is really sent. Only the four types whose EXECUTOR was read and
// agrees with (count offset, K): dispatcher 0x809fe0 (jump table 0x80a104) maps
//   type 0x00 -> 0x80c650, 0x02 -> 0x80d6d0, 0x03 -> 0x80bdd0, 0x10 -> 0x80a064 -> 0x80ba00
// and each calls 0x80dad0(player=pkt[1], &count, pkt, K) with K = 0x14, 0x14, 0x14, 0x08 and the
// count pointer pkt+8, local copy of pkt[8], pkt+8, pkt+2 (0x80c65c.., 0x80d6de.., 0x80bddc.., 0x80ba16..),
// which reads ids at pkt+K (0x80dbe8 `add %ebx,%edi`). All other types stay DRY RUN (layout and
// executor semantics of the type were not read).
static const struct { BYTE type; BYTE cnt_off; BYTE k; BYTE cached; BYTE live; } g_types[] = {
    {0x00, 8, 0x14, 1, 1}, {0x03, 8, 0x14, 1, 1}, {0x10, 2, 0x08, 1, 1}, {0x02, 8, 0x14, 0, 1},
    {0x01, 1, 0x02, 0, 0}, {0x12, 1, 0x03, 0, 0}, {0x14, 1, 0x08, 0, 0}, {0x17, 1, 0x08, 0, 0},
    {0x13, 1, 0x08, 0, 0}, {0x16, 1, 0x0c, 0, 0}, {0x21, 1, 0x54, 0, 0}, {0x22, 1, 0x5c, 0, 0},
    {0x15, 1, 0x54, 0, 0}, {0x6b, 1, 0x0c, 0, 0}, {0x66, 1, 0x18, 0, 0}, {0x69, 1, 0x14, 0, 0},
    {0x6e, 1, 0x08, 0, 0}, {0x78, 1, 0x14, 0, 0}, {0x74, 1, 0x0c, 0, 0}, {0x6d, 1, 0x08, 0, 0},
    {0x75, 1, 0x18, 0, 0}, {0x6f, 1, 0x14, 0, 0},
};

static BOOL in_selection(int id) {
    const BYTE *p = (const BYTE *)g_player;
    for (int i = 0; i < SEL_MAX; i++) {
        DWORD u = RD32(p, PLAYER_SEL + 4 * i);
        if (u && (int)RD32((void *)u, 8) == id) return TRUE;
    }
    return FALSE;
}

// ---- Stage 7: control groups (Ctrl+1..9 store, 1..9 recall) --------------------------------
// Store: 0x74f0c0 WorldPlayer::set_control_group, __thiscall(player, group), ret 4.
// Stolen: 55 8b ec 56 8b f1 (6 bytes: push ebp; mov ebp,esp; push esi; mov esi,ecx).
// Recall: 0x7547f0 WorldPlayer::recall_control_group, __thiscall(player, group), ret 4.
// Stolen: 55 8b ec 83 ec 0c (6 bytes: push ebp; mov ebp,esp; sub esp,0xc).

static int __fastcall set_group_hook(void *player, void *edx, int group) {
    (void)edx;
    int r = o_set_group(player, group);
    if (!r) return r;

    int idx = group_to_idx(group);
    if (idx < 0) return r;

    if (g_player != player) {
        if (g_player) {
            wl_log("overflow: player object changed %p -> %p, list reset", g_player, player);
            drop_all();
            memset(g_pgroups, 0, sizeof(g_pgroups));
        }
        g_player = player;
    }

    PlayerGroups *pg = get_player_groups(player, TRUE);
    if (!pg) return r;

    if (g_n > 0) prune();

    if (g_n > 0) {
        pg->g[idx].count = g_n;
        // The game keeps a unit's group as a bitmask at [unit+0x158] (0x74f150: mov %ebx,0x158(%eax),
        // ebx = 1 << group) and draws the group number over units from it. Give overflow units the
        // same mask the game just wrote on the selected units, so they show the number too. On recall
        // (0x7547f0) the game calls select_unit for every unit with the bit: units past 60 are caught
        // by our select hook into the overflow list (the table below then only fills gaps).
        void *first = RD32(player, PLAYER_CNT) > 0 ? (void *)RD32(player, PLAYER_SEL) : NULL;
        DWORD mask = first ? RD32(first, 0x158) : 0;
        for (int i = 0; i < g_n; i++) {
            pg->g[idx].ids[i] = g_ovf[i].id;
            if (g_ovf[i].unit) *(DWORD *)((BYTE *)g_ovf[i].unit + 0x158) = mask;
        }

        // A unit belongs to at most one group: remove these IDs from other groups
        for (int other = 0; other < MAX_GROUPS; other++) {
            if (other == idx || pg->g[other].count == 0) continue;
            int out = 0;
            for (int j = 0; j < pg->g[other].count; j++) {
                int id = pg->g[other].ids[j];
                BOOL in_new = FALSE;
                for (int k = 0; k < g_n; k++) {
                    if (g_ovf[k].id == id) { in_new = TRUE; break; }
                }
                if (!in_new && in_selection(id)) in_new = TRUE;
                if (!in_new) {
                    pg->g[other].ids[out++] = id;
                }
            }
            pg->g[other].count = out;
        }

        wl_log("overflow: stored control group %d: %d selected (game) + %d overflow units",
               group, (int)RD32(player, PLAYER_CNT), g_n);
    } else {
        if (pg->g[idx].count > 0) {
            wl_log("overflow: stored control group %d: %d selected (game), cleared %d previous overflow units",
                   group, (int)RD32(player, PLAYER_CNT), pg->g[idx].count);
            pg->g[idx].count = 0;
        } else {
            wl_log("overflow: stored control group %d: %d selected (game), 0 overflow units",
                   group, (int)RD32(player, PLAYER_CNT));
        }

        for (int other = 0; other < MAX_GROUPS; other++) {
            if (other == idx || pg->g[other].count == 0) continue;
            int out = 0;
            for (int j = 0; j < pg->g[other].count; j++) {
                if (!in_selection(pg->g[other].ids[j])) {
                    pg->g[other].ids[out++] = pg->g[other].ids[j];
                }
            }
            pg->g[other].count = out;
        }
    }
    return r;
}

static int __fastcall recall_group_hook(void *player, void *edx, int group) {
    (void)edx;
    int r = o_recall_group(player, group);

    int idx = group_to_idx(group);
    if (idx < 0) return r;

    PlayerGroups *pg = get_player_groups(player, FALSE);
    if (!pg || pg->g[idx].count == 0) return r;

    void *w = world();
    if (!w) return r;

    if (g_player != player) {
        if (g_player) {
            wl_log("overflow: player object changed %p -> %p, list reset", g_player, player);
            drop_all();
            memset(g_pgroups, 0, sizeof(g_pgroups));
        }
        g_player = player;
    }

    int recalled_ovf = 0;
    int valid_cnt = 0;
    int old_count = pg->g[idx].count;

    for (int i = 0; i < old_count; i++) {
        int id = pg->g[idx].ids[i];
        void *u = get_obj(w, id);
        if (!u || RD8(u, 0x4c) >= 7 || RD32(u, 0x24) != 0 || RD32(u, 0x10) != (DWORD)player) {
            continue; // pruned: dead, deleted or converted
        }
        pg->g[idx].ids[valid_cnt++] = id;

        int flag = ((int)RD32(player, PLAYER_CNT) == 0) ? 1 : 0;
        int caught = 0;
        int sr = select_core(player, u, flag, &caught);
        if (sr != 0 || caught) {
            recalled_ovf++;
        }
    }
    pg->g[idx].count = valid_cnt;

    if (recalled_ovf > 0) {
        flush_stats();
        wl_log("overflow: recalled control group %d: %d selected (game) + %d overflow units (list now %d)",
               group, (int)RD32(player, PLAYER_CNT), recalled_ovf, g_n);
        r = 1;
    }
    return r;
}

#if OVERFLOW_DIAG
// Every packet while the list is not empty, 20 lines/s max. Shows which packet carries an order.
static void diag_packet(const BYTE *cmd, const BYTE *pkt, int size) {
    static DWORD win_start; static int win_n;
    DWORD now = GetTickCount();
    if (now - win_start >= 1000) { win_start = now; win_n = 0; }
    if (win_n++ >= 20) return;
    char hex[16 * 3 + 1];
    int nb = size < 16 ? size : 16, o = 0;
    for (int i = 0; i < nb; i++) o += snprintf(hex + o, sizeof hex - (size_t)o, "%02x ", pkt[i]);
    hex[o] = 0;
    int match = 0;                                       // selected ids found at ANY byte offset
    for (int off = 0; off + 4 <= size; off++)
        if (in_selection((int)*(const DWORD *)(pkt + off))) match++;
    int cn = (int)*(const short *)(cmd + 0x28), cmatch = 0;
    if (cn > 0 && cn <= SEL_MAX)
        for (int i = 0; i < cn; i++)
            if (in_selection((int)*(const DWORD *)(cmd + 0x30 + 4 * i))) cmatch++;
    wl_log("overflow: diag packet type=0x%02x size=%d bytes[%s] selected_ids_in_packet=%d | cmd cache count=%d flag=%d in_selection=%d | overflow=%d",
           pkt[0], size, hex, match, cn, (int)*(const DWORD *)(cmd + 0x2c), cmatch, g_n);
}
#endif

// ---- Stage 2: clone for the overflow units -------------------------------------------------
//
// STATE THAT MUST STAY CONSISTENT (evidence, objdump of AoK HD.exe 5.8):
//  * Sender cache = the command object (`this` of 0x80ef70): cmd+0x28 short count, cmd+0x2c flag,
//    cmd+0x30.. ids. Written by 0x80ebc0 (0x80ec4d..0x80ec95) while the builder runs, BEFORE the
//    send; the builder sets cmd+0x2c = 1 only AFTER 0x80ef70 returned non-zero (0x8096ad, 0x8098e4,
//    0x809a13). 0x80ebc0 answers "same list" (count byte 0xff) only if cmd+0x28 > 0 && cmd+0x2c != 0
//    && count and ids equal (0x80ebfc..0x80ec39).
//  * Executor cache = PER PLAYER, on EVERY machine: 0x80dad0 (called by the executors of types
//    0x00/0x02/0x03/0x10) stores an explicit list in `this+0x120+player` (count) /
//    `this+0x12c+player*0xf0` (60 ids) and, when the count byte is 0xff, reads it back
//    (0x80db51..0x80dbe3 / 0x80dbe8..0x80dc23). So an explicit clone REPLACES what "0xff" means for
//    this player on all machines (it is part of the lockstep packet stream).
//  * Therefore after a clone the executors' cache holds the overflow list while our sender cache
//    still describes the real selection -> the next short-form (0xff) order would move the
//    overflow units on every machine. The executor side cannot be repaired from here (a 0xff
//    cannot name a list), so we make the sender never send 0xff next: after the clone we set
//    cmd+0x28 = 0. 0x80ec00 `movzwl 0x28(%esi); test %ax,%ax; jle 0x80ec4d` then treats the next
//    list as "different", the builder sends the explicit selection list and re-caches it on the
//    executors (0x80ec4d writes cmd+0x28/0x2c/0x30.. again). Clearing cmd+0x28 and not the flag is
//    required because the builder sets the flag to 1 after we return (0x8098e4 etc.).
//    Result: sender and executors agree on every machine; unmodded peers see only vanilla
//    explicit packets with <= 60 ids.
//  * ORDER: original first, then clone. A 0xff original relies on the executor cache that the
//    previous original left (= the selection), so a clone sent before it would corrupt it.
//
// DUPLICATE FILTER (0x808b60): compares with the previous packet kept at cmd+8 (size at cmd+0xc,
// time at cmd+0x10): dup only if same size AND same type byte AND < 0x1f4 ms AND, for types 0..3,
// 0x80da00 (types 0/2/3: pkt[8] count, x,y as int16, target pkt[4], and all ids) says equal. The
// clone has other ids, so 0x80da00 returns 0 at the first id; for a different size it is not even
// compared. Types >3 are never dup. => our clone is never dropped by it. Side effect: while a clone
// is the "previous packet" a repeated identical player order within 500 ms is no longer dropped as
// a duplicate (it is executed twice, idempotent). The clone send result is logged anyway.
// If the ORIGINAL was dropped as a duplicate (o_send returns 0) no clone is sent.
//
// PACKET OWNERSHIP: 0x80ef70 stores the packet at cmd+8 and frees the previous one with
// 0x8ea484 (free) on the next send (0x80efd1..0x80efe1), so a packet must come from 0x8ea479
// (calloc(1,size), as in the builders) and the original `pkt` is freed by our own clone send:
// nothing may touch `pkt` after o_send(clone).
typedef void *(__cdecl *Calloc_t)(size_t n, size_t size);
static const Calloc_t game_calloc = (Calloc_t)0x8ea479;

static WORD unit_type(void *unit) {              // [[unit+0xc]+0x10], as logged in stage 1
    return *(const WORD *)((const BYTE *)RD32(unit, 0xc) + 0x10);
}

// Looks at an order the game just sent. Fills the ids it carries; returns the table index or -1
// when this is not a selection order we judge. Dry-run lines are logged here.
static int classify(const BYTE *cmd, const BYTE *pkt, int size, const BYTE **ids_out, int *n_out,
                    BOOL *cachedform) {
    int idx = -1;
    for (size_t i = 0; i < sizeof g_types / sizeof g_types[0]; i++)
        if (g_types[i].type == pkt[0]) { idx = (int)i; break; }
    if (idx < 0) return -1;                  // not a known unit-list order
    int k = g_types[idx].k;
    if (size < k) return -1;
    int n = pkt[g_types[idx].cnt_off];
    const BYTE *ids = pkt + k;
    *cachedform = FALSE;
    if (g_types[idx].cached && n == 0xff && size == k) {
        // "same units as the previous order": the list is in the command object's cache.
        n = (int)*(const short *)(cmd + 0x28);
        ids = cmd + 0x30;
        *cachedform = TRUE;
        if (n < 1 || n > SEL_MAX) return -1;
    } else if (n == 0 || size != k + 4 * n) return -1;   // layout does not match: not ours to judge
    int match = 0;
    for (int i = 0; i < n; i++) {
        int id = (int)*(const DWORD *)(ids + 4 * i);   // unaligned read is fine on x86
        if (in_selection(id)) match++;
    }
    if (match != n) {
        wl_log("overflow: order type=0x%02x ids=%d%s, only %d are in selected[]: not a selection order, ignored",
               pkt[0], n, *cachedform ? " (cached)" : "", match);
        return -1;
    }
    *ids_out = ids; *n_out = n;
    return idx;
}

// ---- Stage 3b diagnostics: did the clones reach the executor, and did the units move? ----------
//
// Static reading of the whole pipeline (see overflow_stage1.md, "Stage 3b") found no reason for the
// clones to be dropped or ignored, so this prints the facts at run time:
//  * send side: net manager (`this+0x28e0` of the command object) turn 0xac0 and counters 0xac4 (sequence
//    number of the next command in this turn) / 0xac8 (commands this turn), before and after the clones.
//    Every packet that passes 0x57e300 increments both by one (0x57e448/0x57e44e), so after an order with
//    N clones they must have grown by N + 1.
//  * executor side: hook of the command executor 0x813460 (`__thiscall(proc, packet)`, `ret 4`; it is the
//    virtual slot 3 that the turn loop 0x80d9d0 calls for every queued command). For the selection order
//    types it logs, while an order is being watched, how many ids the packet carries, how many resolve to
//    units, how many of them are overflow units, and how many units got a different AI target
//    (`[[unit+0x78]+0x3c/0x40]` = target x/y, written by 0x4e1b20 which the executor calls per unit) by
//    running the executor.
//  * result: 2.5 s after the order the positions (`[unit+0x3c]`, `[unit+0x40]` floats) of every selected and
//    every overflow unit are compared with the ones taken at send time: "moved N/M".
#define OVERFLOW_EXEC_DIAG 0   // 1: log EXEC / MOVE CHECK lines (clones confirmed executed in game 2026-11-15)

// ---- Clones move WITHOUT formation (stage 6, re/selection/formations_research.md) -----------------------
// Every order packet makes the executor (type 0x03 -> 0x80bdd0 -> 0x7d61e0) cluster its units and create a
// heap Formation object (0x4d4 bytes) that is never freed when it empties (0x7e4650 only unlinks units):
// with clones that was several extra formations per right-click, split blocks on screen and odd AI.
// 0x7d61e0 skips formation clustering when the player's formation switch [player+0x1a90] is 0
// (0x7d644c: cmpl $0,0x1a90(%eax); jne 0x7d6490; set by the command at 0x80aeb9). So clones carry a
// marker in header byte 2 (unused padding for types 0x00/0x02/0x03: count at +8, ids at +0x14; the
// executors never read it; type 0x10 keeps its count at byte 2 and is not marked) and the executor hook
// runs them with the owner's switch at 0, then restores it. The marker travels in the packet, so every
// machine running this Wololoader executes clones the same way (multiplayer: all players need it).
#define CLONE_MARK 0x57   /* 'W' */
typedef void (__thiscall *Exec_t)(void *proc, BYTE *pkt);
static Exec_t o_exec;
static void exec_with_mark(void *proc, BYTE *pkt) {
    if (pkt && pkt[2] == CLONE_MARK && (pkt[0] == 0x00 || pkt[0] == 0x02 || pkt[0] == 0x03)) {
        void *w = world();
        int n = pkt[8];
        void *u = (w && n >= 1 && n <= SEL_MAX) ? get_obj(w, (int)*(const DWORD *)(pkt + 0x14)) : NULL;
        BYTE *owner = u ? (BYTE *)RD32(u, 0x10) : NULL;           // unit owner (player object)
        if (owner) {
            int saved = *(int *)(owner + 0x1a90);
            *(int *)(owner + 0x1a90) = 0;
            o_exec(proc, pkt);
            *(int *)(owner + 0x1a90) = saved;
            return;
        }
    }
    o_exec(proc, pkt);
}
static BOOL g_exec_on;
// Called by the executor hook in gamecmd.c for every packet that is not a Wololoader command.
// Returns FALSE when the overflow feature is off (the caller then runs the original itself).
static void exec_body(void *proc, BYTE *pkt);
BOOL wl_overflow_exec(void *orig, void *proc, BYTE *pkt) {
    if (!g_exec_on) return FALSE;
    o_exec = (Exec_t)orig;
    exec_body(proc, pkt);
    return TRUE;
}
#if !OVERFLOW_EXEC_DIAG
static void exec_body(void *proc, BYTE *pkt) { exec_with_mark(proc, pkt); }
#endif
#if OVERFLOW_EXEC_DIAG
static DWORD g_watch_until;                 // GetTickCount() deadline of the executor logging
static struct {
    DWORD due; int armed;
    int nsel, novf;
    struct { void *unit; int id; float x, y; } sel[SEL_MAX], ovf[OVF_MAX];
} g_chk;

static float unit_x(void *u) { return *(const float *)((const BYTE *)u + 0x3c); }
static float unit_y(void *u) { return *(const float *)((const BYTE *)u + 0x40); }

// Called at send time (before the clones) with the units that will be ordered.
static void chk_arm(void *player) {
    g_chk.nsel = g_chk.novf = 0;
    void *w = world();
    if (!w) return;
    for (int i = 0; i < SEL_MAX; i++) {
        void *u = (void *)RD32(player, PLAYER_SEL + 4 * i);
        if (!u) continue;
        int k = g_chk.nsel++;
        g_chk.sel[k].unit = u; g_chk.sel[k].id = (int)RD32(u, 8);
        g_chk.sel[k].x = unit_x(u); g_chk.sel[k].y = unit_y(u);
    }
    for (int i = 0; i < g_n; i++) {
        void *u = g_ovf[i].unit;
        g_chk.ovf[i].unit = u; g_chk.ovf[i].id = g_ovf[i].id;
        g_chk.ovf[i].x = unit_x(u); g_chk.ovf[i].y = unit_y(u);
    }
    g_chk.novf = g_n;
    g_chk.due = GetTickCount() + 2500;
    g_chk.armed = 1;
    g_watch_until = GetTickCount() + 5000;
}

static void chk_poll(void) {
    if (!g_chk.armed || (int)(GetTickCount() - g_chk.due) < 0) return;
    g_chk.armed = 0;
    void *w = world();
    int ms = 0, gs = 0, mo = 0, go = 0;
    if (w) {
        for (int i = 0; i < g_chk.nsel; i++) {
            void *u = get_obj(w, g_chk.sel[i].id);
            if (u != g_chk.sel[i].unit) { gs++; continue; }
            float dx = unit_x(u) - g_chk.sel[i].x, dy = unit_y(u) - g_chk.sel[i].y;
            if (dx * dx + dy * dy > 0.25f) ms++;
        }
        for (int i = 0; i < g_chk.novf; i++) {
            void *u = get_obj(w, g_chk.ovf[i].id);
            if (u != g_chk.ovf[i].unit) { go++; continue; }
            float dx = unit_x(u) - g_chk.ovf[i].x, dy = unit_y(u) - g_chk.ovf[i].y;
            if (dx * dx + dy * dy > 0.25f) mo++;
        }
    }
    wl_log("overflow: MOVE CHECK 2.5 s after the order: selected moved %d/%d (gone %d), overflow moved %d/%d (gone %d)",
           ms, g_chk.nsel, gs, mo, g_chk.novf, go);
}

static BOOL id_in_chk_ovf(int id) {
    for (int i = 0; i < g_chk.novf; i++) if (g_chk.ovf[i].id == id) return TRUE;
    return FALSE;
}

static void exec_body(void *proc, BYTE *pkt) {
    chk_poll();
    int idx = -1;
    if (pkt && (int)(GetTickCount() - g_watch_until) < 0)
        for (size_t i = 0; i < sizeof g_types / sizeof g_types[0]; i++)
            if (g_types[i].type == pkt[0] && g_types[i].live) { idx = (int)i; break; }
    if (idx < 0) { exec_with_mark(proc, pkt); return; }
    const int k = g_types[idx].k, n = pkt[g_types[idx].cnt_off];
    void *w = world();
    float bx[SEL_MAX], by[SEL_MAX];
    int cnt = (n >= 1 && n <= SEL_MAX) ? n : 0;
    const BYTE *ids = pkt + k;
    const DWORD game_obj = *(const DWORD *)0xaeefb8;      // `mov 0xaeefb8,%eax; push 0xac0(%eax)` at 0x813463
    const DWORD turn = game_obj ? RD32((void *)game_obj, 0xac0) : 0;
    if (w)
        for (int i = 0; i < cnt; i++) {
            void *u = get_obj(w, (int)*(const DWORD *)(ids + 4 * i));
            void *ai = u ? (void *)RD32(u, 0x78) : NULL;
            bx[i] = by[i] = 0.0f;
            if (ai && !IsBadReadPtr(ai, 0x48)) { bx[i] = *(const float *)((const BYTE *)ai + 0x3c); by[i] = *(const float *)((const BYTE *)ai + 0x40); }
        }
    exec_with_mark(proc, pkt);                          // the game executes the order here
    int res = 0, ovf = 0, changed = 0;
    if (w)
        for (int i = 0; i < cnt; i++) {
            int id = (int)*(const DWORD *)(ids + 4 * i);
            void *u = get_obj(w, id);
            if (!u) continue;
            res++;
            if (id_in_chk_ovf(id)) ovf++;
            void *ai = (void *)RD32(u, 0x78);
            if (ai && !IsBadReadPtr(ai, 0x48) &&
                (*(const float *)((const BYTE *)ai + 0x3c) != bx[i] || *(const float *)((const BYTE *)ai + 0x40) != by[i]))
                changed++;
        }
    wl_log("overflow: EXEC type=0x%02x turn=%lu player=%u count=%d%s ids_resolved=%d overflow_ids=%d ai_target_changed=%d first_id=%d",
           pkt[0], (unsigned long)turn, (unsigned)pkt[1], n, cnt ? "" : " (short form / other)", res, ovf, changed,
           cnt ? (int)*(const DWORD *)ids : -1);
}
#else
static void chk_arm(void *player) { (void)player; }
#endif

// Builds and sends the clone for the overflow units. `pkt` is the packet the game just sent.
static void send_clone(void *cmd, const BYTE *pkt, int size, int player, int idx,
                       const BYTE *ids, int n, BOOL cachedform) {
    const int k = g_types[idx].k;
    const int sel = (int)RD32(g_player, PLAYER_CNT);
    // Same filter as the game applied: if the order covers the whole selection nothing was
    // filtered out, every overflow unit gets it. Otherwise the game's getter (0x74ef20) filtered by
    // unit type properties, so only overflow units of a type present in the order qualify.
    WORD types[SEL_MAX]; int nt = 0;
    if (n != sel)
        for (int i = 0; i < n; i++) {
            void *u = get_obj(world(), (int)*(const DWORD *)(ids + 4 * i));
            if (!u) continue;
            WORD t = unit_type(u); int j = 0;
            while (j < nt && types[j] != t) j++;
            if (j == nt) types[nt++] = t;
        }
    // Header of the original: `pkt` is freed by the first clone send (see PACKET OWNERSHIP), so
    // copy everything we need from it NOW. K <= 0x14 for all live types.
    BYTE hdr[0x14];
    memcpy(hdr, pkt, (size_t)k);
    int pick[OVF_MAX], m = 0;
    for (int i = 0; i < g_n && m < OVF_MAX; i++) {
        if (n != sel) {
            WORD t = unit_type(g_ovf[i].unit); int j = 0;
            while (j < nt && types[j] != t) j++;
            if (j == nt) continue;
        }
        pick[m++] = i;
    }
    if (m == 0) {
        wl_log("overflow: order type=0x%02x: no overflow unit passes the order filter, nothing sent", pkt[0]);
        return;
    }
    // Stage 3: chunks of at most CHUNK_MAX = 60 ids, ceil(m/60) packets, each built from the
    // original header. 60 is the hard limit for all four live types: they all go through
    // 0x80dad0, whose per-player cache slot is 0xf0 bytes = 60 ids (`imul $0xf0` 0x80dbd4/0x80dbfc,
    // base this+0x12c) and which copies `count` ids without clamping (loop 0x80dc10..0x80dc21);
    // the count is a byte (cmp %eax,%ecx at 0x80dc1f), so 60 fits everywhere. 61+ would write
    // into the next player's slot, hence never more than 60 per packet.
    // Dup filter between consecutive chunks (0x808b60 -> 0x80da00, read in full): it compares
    // count, x, y, target and then EVERY id (loop 0x80da51..0x80da5e, returns 0 at the first
    // differing id). Chunks hold disjoint ids, so consecutive same-type same-size clones are
    // never "equal"; chunk 1 also differs from the original (its ids are selected units, ours are
    // not). Type 0x10 is never compared (types > 3 are never dup).
    // Executor cache: every chunk is explicit, so each overwrites this player's per-player cache
    // (0x80dad0) on all machines and the cache ends up holding the LAST chunk. That is harmless
    // because the sender cache reset below guarantees the next order is explicit (never 0xff),
    // so nobody ever resolves a 0xff against the last chunk; the next order re-caches the list.
    const BYTE type = hdr[0];
    int sent = 0, dropped = 0;
    // Stage 3b diagnostics: counters of the net manager (`this+0x28e0` of the command object, the same
    // object 0x57e300 runs on): 0xac0 turn, 0xac4 sequence number of the next command, 0xac8 commands
    // this turn. Each packet that passes 0x57e300 adds one to 0xac4 and 0xac8.
    const DWORD net = RD32(cmd, 0x28e0);
    const DWORD seq0 = net ? RD32((void *)net, 0xac4) : 0, cnt0 = net ? RD32((void *)net, 0xac8) : 0;
    chk_arm(g_player);
    for (int off = 0; off < m; off += CHUNK_MAX) {
        const int cnt = (m - off < CHUNK_MAX) ? m - off : CHUNK_MAX;
        const int csize = k + 4 * cnt;
        BYTE *clone = (BYTE *)game_calloc(1, (size_t)csize);
        if (!clone) break;
        memcpy(clone, hdr, (size_t)k);                     // header: type, player, target, x, y ...
        clone[g_types[idx].cnt_off] = (BYTE)cnt;           // explicit form, replaces 0xff if any
        if (g_types[idx].cnt_off != 2 && clone[2] == 0) clone[2] = CLONE_MARK;   // no formation (stage 6)
        for (int i = 0; i < cnt; i++) *(DWORD *)(clone + k + 4 * i) = (DWORD)g_ovf[pick[off + i]].id;
        (void)size;
        // trampoline: no recursion. The previous packet (the original, or the previous chunk) is
        // freed by the game inside this call, we do not touch it afterwards.
        if (o_send(cmd, clone, csize, player)) sent++; else dropped++;
    }
    // Executors cache the last chunk as "last list" of this player (0x80dad0); make the sender's
    // next order explicit so no machine ever resolves a 0xff against it (see above).
    *(short *)((BYTE *)cmd + 0x28) = 0;
    wl_log("overflow: sent %d clone(s) type=0x%02x units=%d (order had %d ids%s, selected=%d, overflow=%d)%s",
           sent, type, m, n, cachedform ? ", short form" : "", sel, g_n,
           dropped ? " [clone dropped by the duplicate filter!]" : "");
    if (net)
        wl_log("overflow: net manager turn=%lu seq %lu -> %lu, commands this turn %lu -> %lu (expected +%d for %d clone(s); the original was counted before)",
               (unsigned long)RD32((void *)net, 0xac0), (unsigned long)seq0, (unsigned long)RD32((void *)net, 0xac4),
               (unsigned long)cnt0, (unsigned long)RD32((void *)net, 0xac8), sent, sent);
}

// ---- send 0x80ef70 (stolen: 55 8b ec 51 53 = push ebp; mov ebp,esp; push ecx; push ebx)
static int __fastcall send_hook(void *cmd, void *edx, BYTE *pkt, int size, int player) {
    (void)edx;
    static int busy;
    BOOL watch = g_n > 0 && pkt && size >= 2 && cmd && !busy;
#if OVERFLOW_EXEC_DIAG
    chk_poll();
#endif
    if (watch) {
        flush_stats();
#if OVERFLOW_DIAG
        diag_packet(cmd, pkt, size);
#endif
        prune();
        watch = g_n > 0;
    }
    int r = o_send(cmd, pkt, size, player);          // original always goes first (see ORDER above)
    if (!watch || !r) return r;                      // r == 0: the game dropped it as a duplicate
    // pkt stays valid until the next send (stored at cmd+8), cmd cache is unchanged by o_send.
    const BYTE *ids; int n; BOOL cachedform;
    int idx = classify((const BYTE *)cmd, pkt, size, &ids, &n, &cachedform);
    if (idx < 0) return r;
    if (!g_types[idx].live) {
        wl_log("overflow: order type=0x%02x units_in_packet=%d%s overflow=%d -> clone NOT sent (type not verified, dry run)",
               pkt[0], n, cachedform ? " (short form)" : "", g_n);
        return r;
    }
    busy = 1;
    send_clone(cmd, pkt, size, player, idx, ids, n, cachedform);
    busy = 0;
    return r;
}

static BOOL expect(DWORD at, const void *bytes, size_t n) {
    if (memcmp((const void *)at, bytes, n) == 0) return TRUE;
    wl_log("overflow: unexpected bytes at 0x%08lx, hook skipped", (unsigned long)at);
    return FALSE;
}

// ---- Stage 4: plain box select with up to 250 candidates --------------------------------------------
// The box collector 0x87cee0 stores candidates in the static buffer 0xde0430 (12-byte records
// {int id; word a; word b; word x; word y}, 60 records, last record ends at 0xde0700). Every absolute
// reference to it (19 operands + 1 tied count, found by scanning the exe for ANY dword in 0xde0430..0xde0720, see
// overflow_stage1.md "Stage 4") is moved to g_boxbuf. Only the collector 0x87cee0 gets the larger cap:
// the other writer 0x87d110 (virtual slot 0xf4, click-select) keeps its 60-record cap (end compare shifted
// by the same delta) because its callers clamp the returned count to 60.
#define BOX_MAX   250
#define BOX_OLD   0xde0430UL
static BYTE g_boxbuf[BOX_MAX * 12 + 16] __attribute__((aligned(4)));   // +16: the movups readers over-read 4 bytes

enum { K_REL, K_END, K_RET };
static const struct { DWORD at; BYTE pre[3]; DWORD old; int kind; } g_boxsites[] = {
    // reference users (consumers/other producers): old operand - 0xde0430 + new base
    { 0x86ef50, {0x4d,0xf0,0xb9}, 0xde0430, K_REL },   // fn 0x86ef..  mov $buf,%ecx
    { 0x86efb6, {0xb7,0x04,0x85}, 0xde0434, K_REL },   //              movzwl buf+4(,%eax,4)
    { 0x86f8c2, {0x89,0x04,0x8d}, 0xde0430, K_REL },   // fn 0x86f880  mov %eax,buf(,%ecx,4)
    { 0x86f8cb, {0x89,0x04,0x8d}, 0xde0438, K_REL },   //              mov %eax,buf+8(,%ecx,4)
    { 0x86f8d6, {0xc7,0x04,0x8d}, 0xde0434, K_REL },   //              movl $0xa0004,buf+4(,%ecx,4)
    { 0x87ccae, {0x00,0x00,0xbb}, 0xde0430, K_REL },   // fn 0x87cc60  box loop: mov $buf,%ebx
    { 0x87cdbb, {0x00,0x00,0xbb}, 0xde0434, K_REL },   //              fallback loop: mov $buf+4,%ebx
    { 0x87cfba, {0x8d,0x3c,0x85}, 0xde0434, K_REL },   // fn 0x87cee0  lea buf+4(,%eax,4),%edi
    { 0x87d050, {0x33,0xc0,0xb9}, 0xde0430, K_REL },   //              dedupe scan: mov $buf,%ecx
    { 0x87d07f, {0x0c,0x81,0xff}, 0xde0704, K_END },   //              cmp $end,%edi  (cap 60 -> BOX_MAX)
    { 0x87d0bf, {0x83,0x3c,0x85}, 0xde0434, K_REL },   //              cmpw $1,buf+4(,%eax,4)
    { 0x87d0cf, {0x89,0x0c,0x85}, 0xde0434, K_REL },   //              mov %cx,buf+4(,%eax,4)
    { 0x87d0d8, {0x5f,0x5e,0xb8}, 0x3c,     K_RET },   //              buffer full: mov $60,%eax -> BOX_MAX
    { 0x87d4c5, {0x8d,0x14,0x85}, 0xde0434, K_REL },   // fn 0x87d110  lea buf+4(,%eax,4),%edx
    { 0x87d4fd, {0xdc,0x81,0xfa}, 0xde0704, K_REL },   //              cmp $end,%edx (stays a 60-record cap)
    { 0x88774e, {0x7e,0x40,0xb9}, 0xde0430, K_REL },   // fn 0x887660  mov $buf,%ecx
    { 0x8877b0, {0xff,0x8d,0x90}, 0xde0430, K_REL },   //              lea buf(%eax),%edx
    { 0x8877c1, {0x00,0x8b,0x8b}, 0xde0430, K_REL },   //              mov buf(%ebx),%ecx
    { 0x887cdc, {0xff,0x8d,0x90}, 0xde0430, K_REL },   // fn 0x887c20  lea buf(%eax),%edx
    { 0x887ced, {0x00,0x8b,0x8f}, 0xde0430, K_REL },   //              mov buf(%edi),%ecx
};

static void install_box_buffer(void) {
    const DWORD base = (DWORD)g_boxbuf;
    const size_t n = sizeof g_boxsites / sizeof g_boxsites[0];
    // Both compares (`jge`) are signed: the array must not straddle 0x80000000.
    if ((base ^ (base + sizeof g_boxbuf)) & 0x80000000UL) {
        wl_log("overflow: box buffer %p straddles 2 GB, plain box stays at 60", g_boxbuf);
        return;
    }
    for (size_t i = 0; i < n; i++) {        // all-or-nothing: check every operand first
        const BYTE *p = (const BYTE *)g_boxsites[i].at;
        if (memcmp(p - 3, g_boxsites[i].pre, 3) != 0 || *(const DWORD *)p != g_boxsites[i].old) {
            wl_log("overflow: box buffer: unexpected bytes at 0x%08lx, plain box stays at 60",
                   (unsigned long)g_boxsites[i].at);
            return;
        }
    }
    for (size_t i = 0; i < n; i++) {
        DWORD *p = (DWORD *)g_boxsites[i].at, v, old;
        switch (g_boxsites[i].kind) {
        case K_END: v = base + 4 + BOX_MAX * 12; break;          // &buf[BOX_MAX].flags
        case K_RET: v = BOX_MAX; break;
        default:    v = g_boxsites[i].old - BOX_OLD + base; break;
        }
        if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old)) {
            // Cannot happen after the check pass in practice; a half-moved buffer would corrupt
            // selection, so say it loudly.
            wl_log("overflow: box buffer: VirtualProtect failed at 0x%08lx (PARTIAL PATCH)", (unsigned long)g_boxsites[i].at);
            return;
        }
        *p = v;
        VirtualProtect(p, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 4);
    }
    wl_log("overflow: box candidate buffer moved to %p (%d records, %u operands patched)", g_boxbuf, BOX_MAX, (unsigned)n);
}

void wl_install_overflow(void) {
    static const BYTE b_select[] = { 0x55, 0x8b, 0xec, 0x56, 0x57 };
    static const BYTE b_uall[]   = { 0x57, 0x8b, 0xf9, 0x83, 0xbf, 0x54, 0x02, 0x00, 0x00, 0x00 };
    static const BYTE b_uptr[]   = { 0x55, 0x8b, 0xec, 0x53, 0x8b, 0xd9, 0x57 };
    static const BYTE b_send[]   = { 0x55, 0x8b, 0xec, 0x51, 0x53 };
    static const BYTE b_getobj[] = { 0x55, 0x8b, 0xec, 0x8b, 0x45 };
    int ok = 0;
    if (!expect(0x733bf0, b_getobj, sizeof b_getobj)) return;   // everything validates through it
    if (expect(0x754a20, b_select, sizeof b_select) &&
        (o_select = wl_hook(0x754a20, select_hook, 5))) ok++;
    if (expect(0x755450, b_uall, sizeof b_uall) &&
        (o_unselect_all = wl_hook(0x755450, unselect_all_hook, 10))) ok++;
    if (expect(0x755540, b_uptr, sizeof b_uptr) &&
        (o_unselect_ptr = wl_hook(0x755540, unselect_ptr_hook, 7))) ok++;
    if (expect(0x80ef70, b_send, sizeof b_send) &&
        (o_send = wl_hook(0x80ef70, send_hook, 5))) ok++;
    install_box_buffer();
    // Box select loop call site 0x87cd2d: `e8 ee 7c ed ff` = call 0x754a20 (checked).
    static const BYTE b_boxcall[] = { 0xe8, 0xee, 0x7c, 0xed, 0xff };
    if (expect(0x87cd2d, b_boxcall, sizeof b_boxcall) && wl_patch_call(0x87cd2d, box_select_call, 5)) ok++;
    // Command executor hook: always (clone marker, stage 6); with OVERFLOW_EXEC_DIAG it also logs.
    // Command executor 0x813460 `__thiscall(proc, packet)`, `ret 4` (virtual slot 3 of the command object
    // vtable 0x9e3b70, called by the turn loop 0x80d9d0 as `call *0xc(%edx)`). Stolen 8 bytes = whole
    // instructions: `55 8b ec a1 b8 ef ae 00` = push ebp; mov ebp,esp; mov 0xaeefb8,%eax (absolute operand,
    // no relative jump); the next instruction (push ebx) starts at 0x813468.
    // The hook itself lives in gamecmd.c (Lua game commands share it); it calls wl_overflow_exec.
    g_exec_on = TRUE;
    ok++;
    // Stage 7: control groups store (0x74f0c0) and recall (0x7547f0).
    // Store 0x74f0c0 stolen: 55 8b ec 56 8b f1 (6 bytes: push ebp; mov ebp,esp; push esi; mov esi,ecx)
    // Recall 0x7547f0 stolen: 55 8b ec 83 ec 0c (6 bytes: push ebp; mov ebp,esp; sub esp,0xc)
    static const BYTE b_setgrp[] = { 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1 };
    static const BYTE b_recall[] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c };
    if (expect(0x74f0c0, b_setgrp, sizeof b_setgrp) &&
        (o_set_group = wl_hook(0x74f0c0, set_group_hook, 6))) ok++;
    if (expect(0x7547f0, b_recall, sizeof b_recall) &&
        (o_recall_group = wl_hook(0x7547f0, recall_group_hook, 6))) ok++;
    wl_log("overflow: stage 7 (250 units, control groups Ctrl+1..9/1..9), %d/8 hooks installed", ok);
}

// Number of valid overflow units right now (selected = 60 + this, max 190). Prunes first so freed /
// reselected units do not count; throttled to every 100 ms because a UI line may call it per frame.
int wl_overflow_count(void) {
    static DWORD last;
    DWORD now = GetTickCount();
    if (g_n > 0 && now - last >= 100) { last = now; prune(); }
    return g_n;
}

// True when `player` is the player object the overflow list belongs to (used by selpanel.c).
int wl_overflow_owns(void *player) { return player != NULL && player == g_player; }
