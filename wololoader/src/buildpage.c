// "Mod buildings" page and multi-page building command panels.
//
// 1) Villager "Mod Buildings" page:
//    The villager's next-page button cycles economy -> military -> mod buildings -> economy
//    when the player can build a Wololoader unique building. Without such buildings it works
//    as before.
//    A mod building is a building (master type byte +4 == 80) in the unique-building unit slots
//    1300..1400 (builders: tools/build.py add_unique_building, slots count down from 1400) whose
//    command_attribute (master byte +0x97, the game's "interface kind") is 2 = economy. Keeping 2
//    matters: the selection panel picks icon sheets and panels from that byte in many places
//    (0x7116a4, 0x7155dc, 0x717a0a ...); an unknown value (30, first try) showed a unit icon.
//
// 2) Multi-page building command panels + button conditions:
//    Filtering happens at the source inside GameScreen's building panel builder 0x6bb100
//    (__thiscall(screen), plain ret at 0x6bc601; reached from 0x6c9dc0, 0x6c6490 mode 10, and
//    0x6bfc40 case 0x79/0xaa). Hooking 0x6bb100 covers every caller, and inline jumps inside its
//    train loop (0x6bb507) and research loop (0x6bb703) skip any command not belonging to the
//    active page (or hidden by a Lua condition) BEFORE 0x6c9200 populates a slot. Commands on
//    different pages can therefore share a button number without overwriting each other, and we
//    never force visibility onto slots the game did not populate (which previously resurrected
//    stale icons from hidden buttons because 0x6c6040/0x6bf840 do not clear icon sheet/frame).
//
// Verified in the disassembly (AoK HD.exe 5.8, see re/FINDINGS.md "Building command pages"):
// - 0x6ba900 = economy build page (mode [screen+0x1288] = 3), 0x6bac30 = military page (mode 4);
//   both __thiscall(screen), plain ret (0x6bac24, 0x6baf2f).
//   0x741a30 __thiscall(player, list*, count*, 118) ret 0xc lists buildable units into
//   [screen+0xd6c] (entries 0x38 bytes, unit id int16 at +4) / [screen+0xd74] (count int16).
//     economy  0x6ba9d5 `80 bb 97 00 00 00 02 74 1a` cmpb $2,0x97(%ebx); je 0x6ba9f8 (add)
//              else 0x6ba9de, skip 0x6bab10
//     military 0x6bad07 `80 bb 97 00 00 00 0a 0f 85 17 01 00 00` cmpb $10,0x97(%ebx); jne 0x6bae2b
//              fallthrough 0x6bad14 (add), skip 0x6bae2b
// - 0x6bda60 = villager next-page toggle: 9 stolen bytes `8b 81 88 12 00 00 83 f8 03`
//   (`mov 0x1288(%ecx),%eax; cmp $3,%eax`), no relative jumps.
// - 0x6c020a `e8 21 aa ff ff` = call 0x6bac30 ("build military" command): patched to military_page.
// - 0x6ca51c `e8 df ec ff ff` = call 0x6c9200 (villager Repair button): patched to add_repair_and_mods.
// - 0x6c9200 = GameScreen::setup_command_button __thiscall(screen, 16 dwords) ret 0x40 (0x6c945f).
//   Sets [btn+0x720] = 1, calls btn->vtable[7](btn, !disabled) (0x479840 ret 4), stores cmd at
//   [btn + *(short*)(btn+0x54a)*0x70 + 0x1a8] and sub_id at +0x1ac.
// - 0x6bfc40 = command click dispatcher __thiscall(screen, cmd, a, b) ret 0xc (0x6c0aa4);
//   5 stolen bytes `55 8b ec 6a ff` (`push ebp; mov ebp,esp; push -1`).
// - 0x6bb100 = building command panel builder __thiscall(screen) ret (0x6bc601);
//   5 stolen bytes `55 8b ec 6a ff` (`push ebp; mov ebp,esp; push -1`).
//   - Train loop filter site 0x6bb507 (7 bytes): `0f b6 b0 a2 01 00 00` (`movzx esi, BYTE PTR [eax+0x1a2]`).
//     Registers: ebx=screen, edi=player, eax=unit master, ecx=train unit_id, edx=loop index (must preserve!).
//     Continue 0x6bb50e (`cmp esi, 0xe6`), skip 0x6bb638 (`mov ecx,[ebp-0x414]; inc edx`).
//   - Research loop filter site 0x6bb703 (9 bytes): `0f b6 c0 89 85 8c fb ff ff`
//     (`movzx eax, al; mov [ebp-0x474], eax`).
//     Registers: ebx=screen, edi=player, esi=&screen+0xd64 (research list ptr), [ebp-0x414]=byte offset
//     (tech_id is WORD at [*(esi) + [ebp-0x414] + 4]), al=button_id.
//     Continue 0x6bb70c (`cmp eax, 0xe6`), skip 0x6bb87e (`mov edx,[ebp-0x428]; ...`).
//   - Gather Point added at 0x6bbd99..0x6bbda9: slot 4, action 0x33.
//   - Ungarrison ("Unload") added at 0x6bbecc..0x6bbedc: slot 9, action 0x4e.
//   - Calls 0x6bf840 (FinalizeVisibility __thiscall(screen) ret 0x6bf900) at 0x6bc5e1.
// - Local player: 0x540c90 __thiscall(game [0xaf2c58]); masters [player+0x14], count [player+0x10],
//   civ object [player+0x1ae4] with civ id int at +8 (verified at 0x6bbe09 and 0x6c5a99..0x6c5aa9).
// - Button vtable[4] (+0x10 = 0x484f20, ret 4): Panel::set_visible(btn, int visible).
// - Button vtable[7] (+0x1c = 0x479840, ret 4): Button::set_enabled(btn, int enabled) -> [btn+0xc9], [btn+0x24].
#include "wololoader.h"
#include <string.h>

typedef void (__thiscall *Page_t)(void *screen);
typedef void *(__thiscall *LocalPlayer_t)(void *game);
typedef void (__thiscall *Buildable_t)(void *player, void *list, void *count, int train_loc);

static const Page_t econ_page = (Page_t)0x6ba900;
static const Page_t mil_page = (Page_t)0x6bac30;
static int g_mods_page;

// Jump targets for the asm stubs (globals so the asm can reach them).
DWORD wl_bp_econ_add = 0x6ba9f8, wl_bp_econ_skip = 0x6bab10, wl_bp_econ_else = 0x6ba9de;
DWORD wl_bp_mil_add = 0x6bad14, wl_bp_mil_skip = 0x6bae2b;
DWORD wl_bp_train_cont = 0x6bb50e, wl_bp_train_skip = 0x6bb638;
DWORD wl_bp_res_cont = 0x6bb70c, wl_bp_res_skip = 0x6bb87e;

static int is_mod_building(const BYTE *master) {
    if (!master || master[4] != 80 || master[0x97] != 2) return 0;
    int id = *(const short *)(master + 0x10);
    return id >= 1300 && id <= 1400;
}

// page 0 = economy, 1 = military function. Returns 1 add, 0 skip, 2 economy's own else branch.
int __cdecl wl_bp_accept(const BYTE *master, int page) {
    if (page == 0) {
        if (is_mod_building(master)) return 0;
        return master[0x97] == 2 ? 1 : 2;
    }
    if (g_mods_page) return is_mod_building(master);
    return master[0x97] == 10;
}

static int get_player_civ(void *player) {
    if (!player) return -1;
    BYTE *civ_obj = *(BYTE **)((char *)player + 0x1ae4);
    return civ_obj ? *(int *)(civ_obj + 8) : -1;
}

static int civ_matches(int want, int actual) {
    return want < 0 || want == actual;
}

// Active building command-page filter context (set during bldg_panel_hook -> o_bldg_panel).
static int g_bldg_active_page = 1;
static void *g_bldg_last_unit = NULL;
static int g_bldg_last_unit_id = -1;
static int g_filter_active = 0;
static int g_filter_civ = -1;
static int g_filter_bldg = -1;
static int g_filter_unit_id = -1;

// Returns 1 to let 0x6bb100 populate this train/research item, 0 to skip it at the source.
static int filter_bldg_item(const char *kind, int target_id) {
    if (!g_filter_active) return 1;
    int nb = 0;
    const WlBuildingCmd *bcmds = wl_building_commands(&nb);
    const WlBuildingCmd *matched = NULL;
    for (int i = 0; i < nb; i++) {
        if (bcmds[i].building == g_filter_bldg && civ_matches(bcmds[i].civ, g_filter_civ) &&
            strcmp(bcmds[i].type, kind) == 0 && bcmds[i].target_id == target_id) {
            matched = &bcmds[i];
            break;
        }
    }
    if (!matched) {
        // Untracked vanilla item on an extended building belongs on page 1 only.
        return g_bldg_active_page == 1 ? 1 : 0;
    }
    if (matched->page != g_bldg_active_page) return 0;
    if (matched->cond_name[0] && strcmp(matched->cond_action, "hide") == 0) {
        if (!wl_script_eval_condition(matched->cond_name, g_filter_unit_id)) return 0;
    }
    return 1;
}

int __cdecl wl_bp_filter_train(void *screen, int train_unit_id) {
    (void)screen;
    return filter_bldg_item("train", train_unit_id);
}

int __cdecl wl_bp_filter_research(void *screen, int tech_id) {
    (void)screen;
    return filter_bldg_item("research", tech_id);
}

void wl_bp_stub_econ(void);
void wl_bp_stub_mil(void);
void wl_bp_stub_train(void);
void wl_bp_stub_research(void);
__asm__(
    ".text\n"
    ".globl _wl_bp_stub_econ\n"
    "_wl_bp_stub_econ:\n"
    "  pushl %eax\n  pushl %ecx\n  pushl %edx\n"
    "  pushl $0\n  pushl %ebx\n  call _wl_bp_accept\n  addl $8, %esp\n"
    "  cmpl $1, %eax\n  je 1f\n  cmpl $2, %eax\n  je 2f\n"
    "  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_econ_skip\n"
    "1:\n  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_econ_add\n"
    "2:\n  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_econ_else\n"
    ".globl _wl_bp_stub_mil\n"
    "_wl_bp_stub_mil:\n"
    "  pushl %eax\n  pushl %ecx\n  pushl %edx\n"
    "  pushl $1\n  pushl %ebx\n  call _wl_bp_accept\n  addl $8, %esp\n"
    "  testl %eax, %eax\n  jnz 3f\n"
    "  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_mil_skip\n"
    "3:\n  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_mil_add\n"
    // Replaces 0x6bb507 (7 bytes): 0f b6 b0 a2 01 00 00 (movzx esi, byte ptr [eax+0x1a2])
    ".globl _wl_bp_stub_train\n"
    "_wl_bp_stub_train:\n"
    "  movzbl 0x1a2(%eax), %esi\n"
    "  pushl %eax\n  pushl %ecx\n  pushl %edx\n"
    "  pushl %ecx\n  pushl %ebx\n  call _wl_bp_filter_train\n  addl $8, %esp\n"
    "  testl %eax, %eax\n"
    "  popl %edx\n  popl %ecx\n  popl %eax\n"
    "  jz 4f\n"
    "  jmp *_wl_bp_train_cont\n"
    "4:\n  jmp *_wl_bp_train_skip\n"
    // Replaces 0x6bb703 (9 bytes): 0f b6 c0 89 85 8c fb ff ff (movzx eax, al; mov [ebp-0x474], eax)
    ".globl _wl_bp_stub_research\n"
    "_wl_bp_stub_research:\n"
    "  movzbl %al, %eax\n"
    "  movl %eax, -0x474(%ebp)\n"
    "  pushl %eax\n  pushl %ecx\n  pushl %edx\n"
    "  movl (%esi), %edx\n"
    "  movl -0x414(%ebp), %ecx\n"
    "  movzwl 4(%edx,%ecx,1), %edx\n"
    "  pushl %edx\n  pushl %ebx\n  call _wl_bp_filter_research\n  addl $8, %esp\n"
    "  testl %eax, %eax\n"
    "  popl %edx\n  popl %ecx\n  popl %eax\n"
    "  jz 5f\n"
    "  jmp *_wl_bp_res_cont\n"
    "5:\n  jmp *_wl_bp_res_skip\n");

static int has_mod_buildings(void *screen) {
    void *game = *(void **)0xaf2c58;
    void *player = game ? ((LocalPlayer_t)0x540c90)(game) : NULL;
    if (!player) return 0;
    char *s = (char *)screen;
    ((Buildable_t)0x741a30)(player, s + 0xd6c, s + 0xd74, 118);
    char *list = *(char **)(s + 0xd6c);
    int count = *(short *)(s + 0xd74);
    BYTE **masters = *(BYTE ***)((char *)player + 0x14);
    int nmasters = *(int *)((char *)player + 0x10);
    if (!list || !masters) return 0;
    for (int i = 0; i < count; i++) {
        int id = *(short *)(list + i * 0x38 + 4);
        if (id >= 0 && id < nmasters && is_mod_building(masters[id])) return 1;
    }
    return 0;
}

static void __fastcall page_toggle(void *screen, void *edx) {
    (void)edx;
    int mode = *(int *)((char *)screen + 0x1288);
    if (mode == 3) {                                   // economy -> military
        g_mods_page = 0;
        mil_page(screen);
    } else if (mode == 4) {
        if (!g_mods_page && has_mod_buildings(screen)) { // military -> mod buildings
            g_mods_page = 1;
            mil_page(screen);
        } else {                                       // mod page (or no mod buildings) -> economy
            g_mods_page = 0;
            econ_page(screen);
        }
    }
}

static void __fastcall military_page(void *screen, void *edx) {
    (void)edx;
    g_mods_page = 0;
    mil_page(screen);
}

// Opens the mod buildings page directly (wl_buildpage_open is used by the command button).
void wl_buildpage_open(void *screen) {
    g_mods_page = 1;
    mil_page(screen);
}

#define WL_MODS_SLOT 7
#define WL_MODS_CMD 200
#define WL_BLDG_NEXTPAGE_CMD 201
// Disjoint from WL_MODS_CMD (200), WL_BLDG_NEXTPAGE_CMD (201), and game commands (1..0xb7).
#define WL_LUA_CMD 210
// Runtime Lua commands (Game.AddCommand): 350..413, disjoint from WL_LUA_CMD (210..337).
#define WL_RT_LUA_CMD 350
#define WL_MODS_HELP 28490
#define WL_NEXTPAGE_HELP 28491

typedef void (__thiscall *AddButton_t)(void *screen, DWORD sheet, DWORD slot, DWORD icon, DWORD cmd, DWORD z,
                                        DWORD h0, DWORD h1, DWORD h2, DWORD h3, DWORD h4,
                                        DWORD m1, DWORD z1, DWORD z2, DWORD z3, DWORD z4, DWORD z5);
typedef void (__thiscall *Command_t)(void *screen, int cmd, int a, int b);
static Command_t o_command;

static void __fastcall add_repair_and_mods(void *screen, void *edx, DWORD sheet, DWORD slot, DWORD icon, DWORD cmd,
                                           DWORD z, DWORD h0, DWORD h1, DWORD h2, DWORD h3, DWORD h4,
                                           DWORD m1, DWORD z1, DWORD z2, DWORD z3, DWORD z4, DWORD z5) {
    (void)edx;
    AddButton_t add = (AddButton_t)0x6c9200;
    add(screen, sheet, slot, icon, cmd, z, h0, h1, h2, h3, h4, m1, z1, z2, z3, z4, z5);   // repair
    if (!has_mod_buildings(screen)) return;
    int ic = wl_icon_index("50721", "modbuildings");
    add(screen, sheet, WL_MODS_SLOT, ic >= 0 ? (DWORD)ic : 32, WL_MODS_CMD, 0,
        1, WL_MODS_HELP, 0xad0380, 0, 0, (DWORD)-1, 0, 0, 0, 0, 0);
}

typedef void (__thiscall *BldgPanel_t)(void *screen);
typedef void (__thiscall *FinalizeVisibility_t)(void *screen);
static BldgPanel_t o_bldg_panel;
static const DWORD ADDR_FINALIZE_VISIBILITY = 0x006bf840;
static const DWORD ADDR_REFRESH_PANEL = 0x006c6490;
static int g_refresh_ok;
static int g_in_bldg_panel;

// Refresh the active GameScreen command panel after runtime Game.AddCommand /
// Game.RemoveCommand or mid-game hot reload. 0x6c6490 (__thiscall(screen), plain ret)
// dereferences 0x540c90([0xaf2c58]) + 0x15c unchecked, so we guard all three pointers.
void wl_buildpage_refresh(void) {
    if (!g_refresh_ok || g_in_bldg_panel) return;
    void *screen = *(void **)0xaf3d90;
    void *game = *(void **)0xaf2c58;
    if (!screen || !game) return;
    if (!((LocalPlayer_t)0x540c90)(game)) return;
    ((Page_t)ADDR_REFRESH_PANEL)(screen);
}

static void set_button_visible(void *btn, int visible) {
    if (!btn) return;
    *(int *)((char *)btn + 0x720) = visible ? 1 : 0;
    void **vtable = *(void ***)btn;
    if (vtable && vtable[4]) {
        ((void (__thiscall *)(void *, int))vtable[4])(btn, visible ? 1 : 0);
    }
}

// [btn+0x724] greys the button in draw (0x7120b0); button vtable[7] (+0x1c = 0x479840, ret 4)
// sets [btn+0xc9] and [btn+0x24] so click/hotkey handlers ignore disabled buttons.
static void set_button_disabled(void *btn, int disabled) {
    if (!btn) return;
    *(int *)((char *)btn + 0x724) = disabled ? 1 : 0;
    void **vtable = *(void ***)btn;
    if (vtable && vtable[7]) {
        ((void (__thiscall *)(void *, int))vtable[7])(btn, disabled ? 0 : 1);
    }
}

// Action id stored on a command button (0x6c9200 at 0x6c92a6..0x6c92c0):
// indexed by state *(short*)(btn + 0x54a) * 0x70 + 0x1a8.
static int button_action(void *btn) {
    if (!btn) return 0;
    int st = *(short *)((char *)btn + 0x54a);
    if (st < 0 || st > 8) st = 0;
    return *(int *)((char *)btn + st * 0x70 + 0x1a8);
}

// Guard built-in building buttons added by 0x6bb100:
// - Market (84) slots 5 and 10 carry the "Sell" ([screen+0xbd0]) and "Buy" ([screen+0xbd4])
//   TextPanels even though [btn+0x720] == 0 (0x6bbfaa, 0x6bc1bd).
// - Gather Point = slot 4, action 0x33 (0x6bbd99).
// - TC Back to Work = slot 8, action 0xab (0x6bbf35).
// - Ungarrison ("Unload") = slot 9, action 0x4e (0x6bbecc) / 0x4d.
// - TC Ring Town Bell = slot 14, action 0xa3 (0x6bc43d).
static int is_protected_builtin(int bldg_id, int slot, void *btn) {
    if (bldg_id == 84 && (slot == 5 || slot == 10)) return 1;
    if (!btn || *(int *)((char *)btn + 0x720) == 0) return 0;
    int act = button_action(btn);
    if (slot == 4 && act == 0x33) return 1;
    if (slot == 8 && act == 0xab) return 1;
    if (slot == 9 && (act == 0x4e || act == 0x4d)) return 1;
    if (slot == 14 && act == 0xa3) return 1;
    return 0;
}

// Inspect pages that have at least one static (bcmds) or runtime (rcmds) command for (civ, bldg).
// Outputs static count, runtime count, lowest page, highest page, and distinct page count.
static int scan_bldg_pages(const WlBuildingCmd *bcmds, int nb,
                           const WlRuntimeCmd *rcmds, int nr,
                           int civ, int bldg,
                           int *out_bldg_cnt, int *out_rt_cnt,
                           int *out_first, int *out_max, int *out_num_pages) {
    int bldg_cnt = 0, rt_cnt = 0, first_page = 0, max_page = 0;
    for (int i = 0; i < nb; i++) {
        if (bcmds[i].building == bldg && civ_matches(bcmds[i].civ, civ)) {
            int p = bcmds[i].page >= 1 ? bcmds[i].page : 1;
            bldg_cnt++;
            if (first_page == 0 || p < first_page) first_page = p;
            if (p > max_page) max_page = p;
        }
    }
    for (int i = 0; i < nr; i++) {
        if (!rcmds[i].used) continue;
        if (rcmds[i].building == bldg && civ_matches(rcmds[i].civ, civ)) {
            int p = rcmds[i].page >= 1 ? rcmds[i].page : 1;
            rt_cnt++;
            if (first_page == 0 || p < first_page) first_page = p;
            if (p > max_page) max_page = p;
        }
    }
    // A vanilla building with runtime commands on page > 1 still has its vanilla page 1.
    if (bldg_cnt == 0 && rt_cnt > 0 && max_page > 1 && first_page > 1) first_page = 1;
    int num_pages = 0;
    for (int p = first_page; p > 0 && p <= max_page; p++) {
        int found = (p == 1 && bldg_cnt == 0 && rt_cnt > 0 && max_page > 1);
        for (int i = 0; !found && i < nb; i++) {
            int cp = bcmds[i].page >= 1 ? bcmds[i].page : 1;
            if (bcmds[i].building == bldg && civ_matches(bcmds[i].civ, civ) && cp == p) found = 1;
        }
        for (int i = 0; !found && i < nr; i++) {
            if (!rcmds[i].used) continue;
            int cp = rcmds[i].page >= 1 ? rcmds[i].page : 1;
            if (rcmds[i].building == bldg && civ_matches(rcmds[i].civ, civ) && cp == p) found = 1;
        }
        if (found) num_pages++;
    }
    if (out_bldg_cnt) *out_bldg_cnt = bldg_cnt;
    if (out_rt_cnt) *out_rt_cnt = rt_cnt;
    if (out_first) *out_first = first_page > 0 ? first_page : 1;
    if (out_max) *out_max = max_page > 0 ? max_page : 1;
    if (out_num_pages) *out_num_pages = num_pages;
    return bldg_cnt + rt_cnt;
}

static int page_has_commands(const WlBuildingCmd *bcmds, int nb,
                             const WlRuntimeCmd *rcmds, int nr,
                             int civ, int bldg, int page, int bldg_cnt, int rt_cnt) {
    if (page == 1 && bldg_cnt == 0 && rt_cnt > 0) return 1;
    for (int i = 0; i < nb; i++) {
        int cp = bcmds[i].page >= 1 ? bcmds[i].page : 1;
        if (bcmds[i].building == bldg && civ_matches(bcmds[i].civ, civ) && cp == page) return 1;
    }
    for (int i = 0; i < nr; i++) {
        if (!rcmds[i].used) continue;
        int cp = rcmds[i].page >= 1 ? rcmds[i].page : 1;
        if (rcmds[i].building == bldg && civ_matches(rcmds[i].civ, civ) && cp == page) return 1;
    }
    return 0;
}

// Re-verify on click that the selected unit is still an owned, finished building
// matching the expected master id, civ, and condition (guards against stale buttons
// or hotkeys after conversion / selection switch).
static int validate_cmd_unit(void *screen, int want_bldg, int want_civ,
                             const char *cond_name, int *out_unit_id) {
    BYTE *unit = *(BYTE **)((char *)screen + 0xd5c);
    void *game = *(void **)0xaf2c58;
    void *me = game ? ((LocalPlayer_t)0x540c90)(game) : NULL;
    if (!unit || !me || *(void **)(unit + 0x10) != me || unit[0x4c] != 2) return 0;
    BYTE *master = *(BYTE **)(unit + 0xc);
    if (!master || master[4] != 80 || *(short *)(master + 0x10) != want_bldg) return 0;
    if (!civ_matches(want_civ, get_player_civ(me))) return 0;
    int unit_id = (int)*(DWORD *)(unit + 8);
    if (cond_name && cond_name[0] && !wl_script_eval_condition(cond_name, unit_id)) return 0;
    if (out_unit_id) *out_unit_id = unit_id;
    return 1;
}

static void __fastcall bldg_panel_hook(void *screen, void *edx) {
    (void)edx;

    BYTE *unit = *(BYTE **)((char *)screen + 0xd5c);
    void *game = *(void **)0xaf2c58;
    void *me = game ? ((LocalPlayer_t)0x540c90)(game) : NULL;
    int nb = 0, nr = 0;
    const WlBuildingCmd *bcmds = wl_building_commands(&nb);
    const WlRuntimeCmd *rcmds = wl_rt_commands(&nr);

    int id = -1, civ = -1, unit_id = -1;
    int count = 0, bldg_cnt = 0, rt_cnt = 0, first_page = 1, max_page = 1, num_pages = 0;
    if ((nb > 0 || nr > 0) && unit && me && *(void **)(unit + 0x10) == me && unit[0x4c] == 2) {
        BYTE *master = *(BYTE **)(unit + 0xc);
        if (master && master[4] == 80) {
            id = *(short *)(master + 0x10);
            civ = get_player_civ(me);
            unit_id = (int)*(DWORD *)(unit + 8);
            count = scan_bldg_pages(bcmds, nb, rcmds, nr, civ, id,
                                    &bldg_cnt, &rt_cnt, &first_page, &max_page, &num_pages);
        }
    }

    g_in_bldg_panel++;
    if (count > 0) {
        if (unit != g_bldg_last_unit || unit_id != g_bldg_last_unit_id) {
            g_bldg_last_unit = unit;
            g_bldg_last_unit_id = unit_id;
            g_bldg_active_page = first_page;
        } else if (!page_has_commands(bcmds, nb, rcmds, nr, civ, id, g_bldg_active_page, bldg_cnt, rt_cnt)) {
            g_bldg_active_page = first_page;
        }
        // If only single-page runtime commands match a vanilla building (bldg_cnt == 0),
        // leave g_filter_active = 0 so vanilla train/research loops run untouched.
        g_filter_active = (bldg_cnt > 0 || num_pages > 1) ? 1 : 0;
        g_filter_civ = civ;
        g_filter_bldg = id;
        g_filter_unit_id = unit_id;
        wl_script_cond_begin_refresh(unit_id);
    } else {
        g_filter_active = 0;
    }

    o_bldg_panel(screen);

    g_filter_active = 0;
    if (count == 0) {
        g_in_bldg_panel--;
        return;
    }

    void **buttons = (void **)((char *)screen + 0xb14);
    AddButton_t add = (AddButton_t)0x6c9200;
    DWORD sheet = *(DWORD *)((char *)screen + 0xab4);
    int need_finalize = 0;

    // 0x6bb100 already filtered train/research commands at the source (0x6bb507/0x6bb703)
    // and called 0x6bf840. Now add active-page Lua buttons, apply disable/hide conditions
    // only to slots that were actually populated, and add Next Page when >1 page exists.
    for (int i = 0; bldg_cnt > 0 && i < nb; i++) {
        const WlBuildingCmd *bc = &bcmds[i];
        if (bc->building != id || !civ_matches(bc->civ, civ) || bc->page != g_bldg_active_page) continue;
        if (bc->button < 1 || bc->button > 14) continue;
        int slot = bc->button - 1;

        int is_avail = 1;
        if (bc->cond_name[0]) {
            is_avail = wl_script_eval_condition(bc->cond_name, unit_id);
        }

        if (strcmp(bc->type, "lua") == 0) {
            if (!is_avail && strcmp(bc->cond_action, "hide") == 0) continue;
            if (is_protected_builtin(id, slot, buttons[slot])) continue;
            add(screen, sheet, (DWORD)slot, (DWORD)bc->icon, (DWORD)(WL_LUA_CMD + i), 0,
                1, (DWORD)bc->help, 0xad0380, 0, 0, (DWORD)-1, 0, 0, 0, 0, 0);
            need_finalize = 1;
            if (buttons[slot]) {
                set_button_disabled(buttons[slot], (!is_avail && strcmp(bc->cond_action, "disable") == 0) ? 1 : 0);
            }
        } else {
            // Never force visibility on a slot 0x6bb100 did not populate ([btn+0x720] == 0).
            if (!buttons[slot] || *(int *)((char *)buttons[slot] + 0x720) == 0) continue;
            if (is_protected_builtin(id, slot, buttons[slot])) continue;
            if (!is_avail) {
                if (strcmp(bc->cond_action, "hide") == 0) {
                    set_button_visible(buttons[slot], 0);
                    need_finalize = 1;
                } else if (strcmp(bc->cond_action, "disable") == 0) {
                    set_button_disabled(buttons[slot], 1);
                }
            }
        }
    }

    int max_rt_btn = num_pages > 1 ? 14 : 15;
    for (int i = 0; rt_cnt > 0 && i < nr; i++) {
        const WlRuntimeCmd *rc = &rcmds[i];
        if (!rc->used) continue;
        int rpage = rc->page >= 1 ? rc->page : 1;
        if (rc->building != id || !civ_matches(rc->civ, civ) || rpage != g_bldg_active_page) continue;
        if (rc->button < 1 || rc->button > max_rt_btn) continue;
        int slot = rc->button - 1;

        int is_avail = 1;
        if (rc->cond_name[0]) {
            is_avail = wl_script_eval_condition(rc->cond_name, unit_id);
        }
        if (!is_avail && strcmp(rc->cond_action, "hide") == 0) continue;
        if (is_protected_builtin(id, slot, buttons[slot])) continue;
        // Pass rc->gen as sub_id (z) so 0x6c9200 does not early-return at 0x6c92dd
        // when a hot-reloaded script updates text/help with the same cmd and icon.
        add(screen, sheet, (DWORD)slot, (DWORD)rc->icon, (DWORD)(WL_RT_LUA_CMD + i), rc->gen,
            1, (DWORD)rc->help_sid, 0xad0380, 0, 0, (DWORD)-1, 0, 0, 0, 0, 0);
        need_finalize = 1;
        if (buttons[slot]) {
            set_button_disabled(buttons[slot], (!is_avail && strcmp(rc->cond_action, "disable") == 0) ? 1 : 0);
        }
    }

    if (num_pages > 1) {
        add(screen, sheet, 14, 32, WL_BLDG_NEXTPAGE_CMD, 0,
            1, WL_NEXTPAGE_HELP, 0xad0380, 0, 0, (DWORD)-1, 0, 0, 0, 0, 0);
        need_finalize = 1;
    }

    if (need_finalize) {
        ((FinalizeVisibility_t)ADDR_FINALIZE_VISIBILITY)(screen);
    }
    wl_script_cond_end_refresh();
    g_in_bldg_panel--;
}

static void __fastcall command_hook(void *screen, void *edx, int cmd, int a, int b) {
    (void)edx;
    if (cmd == WL_MODS_CMD) { wl_buildpage_open(screen); return; }
    if (cmd == WL_BLDG_NEXTPAGE_CMD) {
        BYTE *unit = *(BYTE **)((char *)screen + 0xd5c);
        void *game = *(void **)0xaf2c58;
        void *me = game ? ((LocalPlayer_t)0x540c90)(game) : NULL;
        if (unit && me) {
            BYTE *master = *(BYTE **)(unit + 0xc);
            if (master) {
                int id = *(short *)(master + 0x10);
                int civ = get_player_civ(me);
                int nb = 0, nr = 0;
                const WlBuildingCmd *bcmds = wl_building_commands(&nb);
                const WlRuntimeCmd *rcmds = wl_rt_commands(&nr);
                int bldg_cnt = 0, rt_cnt = 0, first_page = 1, max_page = 1, num_pages = 0;
                if (scan_bldg_pages(bcmds, nb, rcmds, nr, civ, id,
                                    &bldg_cnt, &rt_cnt, &first_page, &max_page, &num_pages) > 0) {
                    int next_page = 0;
                    for (int p = g_bldg_active_page + 1; p <= max_page; p++) {
                        if (page_has_commands(bcmds, nb, rcmds, nr, civ, id, p, bldg_cnt, rt_cnt)) {
                            next_page = p;
                            break;
                        }
                    }
                    if (next_page == 0) next_page = first_page;
                    g_bldg_active_page = next_page;
                    bldg_panel_hook(screen, NULL);
                }
            }
        }
        return;
    }
    if (cmd >= WL_LUA_CMD && cmd < WL_LUA_CMD + WL_MAX_BLDG_CMDS) {
        int nb = 0;
        const WlBuildingCmd *bcmds = wl_building_commands(&nb);
        int idx = cmd - WL_LUA_CMD;
        int unit_id = -1;
        if (idx < nb && strcmp(bcmds[idx].type, "lua") == 0 &&
            validate_cmd_unit(screen, bcmds[idx].building, bcmds[idx].civ, bcmds[idx].cond_name, &unit_id)) {
            wl_cmd_set_local_ctx(1);
            wl_script_command(bcmds[idx].name, unit_id);
            wl_cmd_set_local_ctx(0);
        }
        return;
    }
    if (cmd >= WL_RT_LUA_CMD && cmd < WL_RT_LUA_CMD + WL_MAX_RT_CMDS) {
        int nr = 0;
        const WlRuntimeCmd *rcmds = wl_rt_commands(&nr);
        int idx = cmd - WL_RT_LUA_CMD;
        int unit_id = -1;
        if (idx < nr && rcmds[idx].used &&
            validate_cmd_unit(screen, rcmds[idx].building, rcmds[idx].civ, rcmds[idx].cond_name, &unit_id)) {
            char name[64];
            lstrcpynA(name, rcmds[idx].name, sizeof name);
            wl_cmd_set_local_ctx(1);
            wl_script_command(name, unit_id);
            wl_cmd_set_local_ctx(0);
        }
        return;
    }
    o_command(screen, cmd, a, b);
}

static int patch_jmp(DWORD at, void *to, int len) {
    DWORD old;
    if (!VirtualProtect((void *)at, (SIZE_T)len, PAGE_EXECUTE_READWRITE, &old)) return 0;
    BYTE *p = (BYTE *)at;
    p[0] = 0xE9;
    *(DWORD *)(p + 1) = (DWORD)to - (at + 5);
    for (int i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect((void *)at, (SIZE_T)len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, (SIZE_T)len);
    return 1;
}

void wl_install_buildpage(void) {
    static const BYTE b_toggle[] = { 0x8b, 0x81, 0x88, 0x12, 0x00, 0x00, 0x83, 0xf8, 0x03 };
    static const BYTE b_econ[]   = { 0x80, 0xbb, 0x97, 0x00, 0x00, 0x00, 0x02, 0x74, 0x1a };
    static const BYTE b_mil[]    = { 0x80, 0xbb, 0x97, 0x00, 0x00, 0x00, 0x0a, 0x0f, 0x85, 0x17, 0x01, 0x00, 0x00 };
    static const BYTE b_call[]   = { 0xe8, 0x21, 0xaa, 0xff, 0xff };
    static const BYTE b_repair[] = { 0xe8, 0xdf, 0xec, 0xff, 0xff };
    static const BYTE b_cmd[]    = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_bldg[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x1a, 0xb3, 0x95, 0x00 };
    static const BYTE b_train[]  = { 0x0f, 0xb6, 0xb0, 0xa2, 0x01, 0x00, 0x00 };
    static const BYTE b_res[]    = { 0x0f, 0xb6, 0xc0, 0x89, 0x85, 0x8c, 0xfb, 0xff, 0xff };

    if (memcmp((void *)0x6bda60, b_toggle, sizeof b_toggle) || memcmp((void *)0x6ba9d5, b_econ, sizeof b_econ) ||
        memcmp((void *)0x6bad07, b_mil, sizeof b_mil) || memcmp((void *)0x6c020a, b_call, sizeof b_call) ||
        memcmp((void *)0x6ca51c, b_repair, sizeof b_repair) || memcmp((void *)0x6bfc40, b_cmd, sizeof b_cmd) ||
        memcmp((void *)0x6bb100, b_bldg, sizeof b_bldg) || memcmp((void *)0x6bb507, b_train, sizeof b_train) ||
        memcmp((void *)0x6bb703, b_res, sizeof b_res)) {
        wl_log("buildpage: unexpected bytes, mod buildings page off (econ %d mil %d toggle %d call %d repair %d cmd %d bldg %d train %d res %d)",
               !memcmp((void *)0x6ba9d5, b_econ, sizeof b_econ), !memcmp((void *)0x6bad07, b_mil, sizeof b_mil),
               !memcmp((void *)0x6bda60, b_toggle, sizeof b_toggle), !memcmp((void *)0x6c020a, b_call, sizeof b_call),
               !memcmp((void *)0x6ca51c, b_repair, sizeof b_repair), !memcmp((void *)0x6bfc40, b_cmd, sizeof b_cmd),
               !memcmp((void *)0x6bb100, b_bldg, sizeof b_bldg), !memcmp((void *)0x6bb507, b_train, sizeof b_train),
               !memcmp((void *)0x6bb703, b_res, sizeof b_res));
        return;
    }
    int ok = patch_jmp(0x6ba9d5, (void *)wl_bp_stub_econ, sizeof b_econ);
    ok = ok && patch_jmp(0x6bad07, (void *)wl_bp_stub_mil, sizeof b_mil);
    ok = ok && patch_jmp(0x6bb507, (void *)wl_bp_stub_train, sizeof b_train);
    ok = ok && patch_jmp(0x6bb703, (void *)wl_bp_stub_research, sizeof b_res);
    ok = ok && wl_hook(0x6bda60, page_toggle, 9) != NULL;
    ok = ok && wl_patch_call(0x6c020a, military_page, 5);
    ok = ok && wl_patch_call(0x6ca51c, add_repair_and_mods, 5);
    ok = ok && (o_command = (Command_t)wl_hook(0x6bfc40, command_hook, 5)) != NULL;
    // Stolen 5 bytes at 0x6bb100: 55 8b ec 6a ff (push ebp; mov ebp,esp; push -1)
    ok = ok && (o_bldg_panel = (BldgPanel_t)wl_hook(0x6bb100, bldg_panel_hook, 5)) != NULL;
    // Verified prologue at 0x6c6490 (refresh active command panel):
    // push esi; mov esi,ecx; mov ecx,[0xaf2c58]; call 0x540c90
    static const BYTE b_refresh[] = { 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x58, 0x2c, 0xaf, 0x00, 0xe8, 0xf2, 0xa7, 0xe7, 0xff };
    g_refresh_ok = (memcmp((void *)ADDR_REFRESH_PANEL, b_refresh, sizeof b_refresh) == 0);
    if (!g_refresh_ok) wl_log("buildpage: unexpected bytes at 0x6c6490, live panel refresh disabled");
    wl_script_set_buildpage_refresh(wl_buildpage_refresh);
    wl_script_set_local_ctx_fn(wl_cmd_set_local_ctx);
    wl_log("buildpage: mod buildings page %s", ok ? "installed" : "FAILED");
}
