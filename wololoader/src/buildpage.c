// "Mod buildings" page: the villager's next-page button cycles economy -> military -> mod
// buildings -> economy when the player can build a Wololoader unique building. Without such
// buildings it works as before.
//
// A mod building is a building (master type byte +4 == 80) in the unique-building unit slots
// 1300..1400 (builders: tools/build.py add_unique_building, slots count down from 1400) whose
// command_attribute (master byte +0x97, the game's "interface kind") is 2 = economy. Keeping 2
// matters: the selection panel picks icon sheets and panels from that byte in many places
// (0x7116a4, 0x7155dc, 0x717a0a ...); an unknown value (30, first try) showed a unit icon.
//
// Verified in the disassembly (2026-11-15):
// - GameScreen 0x6ba900 = economy build page (mode [screen+0x1288] = 3), 0x6bac30 = military page
//   (0x6bac66 movl $4,0x1288(%edi)); both __thiscall(screen), plain ret (0x6bac24, 0x6baf2f).
//   They list what the villager can build with 0x741a30 __thiscall(player, list*, count*, 118)
//   ret 0xc into [screen+0xd6c] (entries 0x38 bytes, unit id int16 at +4) / [screen+0xd74]
//   (count int16), with the master in %ebx, then filter on +0x97:
//     economy  0x6ba9d5 `80 bb 97 00 00 00 02  74 1a` cmpb $2 ; je 0x6ba9f8 (add button)
//              else 0x6ba9de: adds it anyway when player resource 82 > 0, otherwise 0x6bab10 (skip)
//     military 0x6bad07 `80 bb 97 00 00 00 0a  0f 85 17 01 00 00` cmpb $10 ; jne 0x6bae2b (skip),
//              falls through to 0x6bad14 (add button)
//   Both are replaced by a jump to an asm stub that asks wl_bp_accept and jumps to one of those
//   targets. %eax/%ecx/%edx are saved; %ebx (master) is not changed.
// - The mod page is the military page with g_mods_page set (the refresh, jump table 0x6c64b6, calls
//   the page function by mode, so it keeps showing whatever g_mods_page says).
// - 0x6bda60 = next-page button: `mov 0x1288(%ecx),%eax; cmp $3; je 0x6bac30; cmp $4; je 0x6ba900;
//   ret` (25 bytes). Replaced by page_toggle (9 stolen bytes, no relative jumps in them).
// - 0x6c020a `e8 21 aa ff ff` = the "build military" command calling 0x6bac30: patched to
//   military_page, which leaves the mod page first.
// - "Mod Buildings" command button on the villager panel: the villager branch of the unit
//   command panel 0x6c96b0 adds Build (slot 0, cmd 0x74, icon 30, help 41061), Military (slot 1,
//   cmd 0x73, icon 31, help 41062) and Repair (slot 2, `e8 df ec ff ff` call 0x6c9200 at
//   0x6ca51c, then jmp 0x6ca6b9). That call is patched to add_repair_and_mods, which adds the
//   repair button and then ours (slot WL_MODS_SLOT, cmd WL_MODS_CMD) when the player can build a
//   mod building. 0x6c9200 = add command button, __thiscall(screen, sheet [screen+0xab4], slot,
//   icon, cmd, 0, help {u8 1, int string id, 0xad0380, 0, 0} by value, -1, 0, 0, 0, 0, 0),
//   ret 0x40 (0x6c945f); the economy page builds the help struct the same way (0x6bab5c..).
//   Icon: frame "modbuildings" appended to 50721.slp by iconmerge.c (fallback 32, the game's
//   More Buildings arrow). Help text: string id WL_MODS_HELP, served by civs.c.
// - command handler 0x6bfc40 __thiscall(screen, cmd, a, b) ret 0xc (0x6c0aa4); cmd 1..0xb7 go
//   through its jump table, others do nothing, so WL_MODS_CMD = 200 is free. Hooked (stolen
//   `55 8b ec 6a ff` = push ebp; mov ebp,esp; push -1).
// - Lua buttons on a mod building's own panel: 0x6c9dc0 builds the command panel of the selected
//   object [screen+0xd5c] (__thiscall(screen), plain ret 0x6cb960; reached from the panel refresh
//   0x6c6514 for the unit/building modes). Hooked (stolen `55 8b ec 6a ff`): after the game's
//   buttons, a finished ([unit+0x4c] == 2, as the research executor checks at 0x815c51) building
//   of the local player whose unit id has Lua buttons (cache\commands.txt) gets them, slot =
//   button - 1, cmd WL_LUA_CMD + index; pressing one calls Game.OnCommand handlers (script.c).
// - local player: 0x540c90 __thiscall(game [0xaf2c58]); masters [player+0x14], count [player+0x10].
#include "wololoader.h"

typedef void (__thiscall *Page_t)(void *screen);
typedef void *(__thiscall *LocalPlayer_t)(void *game);
typedef void (__thiscall *Buildable_t)(void *player, void *list, void *count, int train_loc);

static const Page_t econ_page = (Page_t)0x6ba900;
static const Page_t mil_page = (Page_t)0x6bac30;
static int g_mods_page;

// Jump targets for the stubs (globals so the asm can reach them).
DWORD wl_bp_econ_add = 0x6ba9f8, wl_bp_econ_skip = 0x6bab10, wl_bp_econ_else = 0x6ba9de;
DWORD wl_bp_mil_add = 0x6bad14, wl_bp_mil_skip = 0x6bae2b;

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

void wl_bp_stub_econ(void);
void wl_bp_stub_mil(void);
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
    "3:\n  popl %edx\n  popl %ecx\n  popl %eax\n  jmp *_wl_bp_mil_add\n");

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
        if (!g_mods_page && has_mod_buildings(screen)) {   // military -> mod buildings
            g_mods_page = 1;
            mil_page(screen);
        } else {                                       // military or mod page -> economy
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
#define WL_MODS_HELP 28490

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

#define WL_LUA_CMD 201
typedef void (__thiscall *Panel_t)(void *screen);
static Panel_t o_panel;

static void __fastcall panel_hook(void *screen, void *edx) {
    (void)edx;
    o_panel(screen);
    int n;
    const WlLuaButton *b = wl_lua_buttons(&n);
    if (!n) return;
    BYTE *unit = *(BYTE **)((char *)screen + 0xd5c);
    void *game = *(void **)0xaf2c58;
    void *me = game ? ((LocalPlayer_t)0x540c90)(game) : NULL;
    if (!unit || !me || *(void **)(unit + 0x10) != me || unit[0x4c] != 2) return;
    BYTE *master = *(BYTE **)(unit + 0xc);
    if (!master) return;
    int id = *(short *)(master + 0x10);
    AddButton_t add = (AddButton_t)0x6c9200;
    DWORD sheet = *(DWORD *)((char *)screen + 0xab4);
    for (int i = 0; i < n && i < 50; i++) {
        if (b[i].building != id || b[i].button < 1 || b[i].button > 15) continue;
        add(screen, sheet, (DWORD)(b[i].button - 1), (DWORD)b[i].icon, (DWORD)(WL_LUA_CMD + i), 0,
            1, (DWORD)b[i].help, 0xad0380, 0, 0, (DWORD)-1, 0, 0, 0, 0, 0);
    }
}

static void __fastcall command_hook(void *screen, void *edx, int cmd, int a, int b) {
    (void)edx;
    if (cmd == WL_MODS_CMD) { wl_buildpage_open(screen); return; }
    if (cmd >= WL_LUA_CMD && cmd < WL_LUA_CMD + 50) {
        int n;
        const WlLuaButton *lb = wl_lua_buttons(&n);
        BYTE *unit = *(BYTE **)((char *)screen + 0xd5c);
        if (cmd - WL_LUA_CMD < n && unit) {
            wl_cmd_set_local_ctx(1);
            wl_script_command(lb[cmd - WL_LUA_CMD].name, (int)*(DWORD *)(unit + 8));
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
    static const BYTE b_econ[] = { 0x80, 0xbb, 0x97, 0x00, 0x00, 0x00, 0x02, 0x74, 0x1a };
    static const BYTE b_mil[] = { 0x80, 0xbb, 0x97, 0x00, 0x00, 0x00, 0x0a, 0x0f, 0x85, 0x17, 0x01, 0x00, 0x00 };
    static const BYTE b_call[] = { 0xe8, 0x21, 0xaa, 0xff, 0xff };
    static const BYTE b_repair[] = { 0xe8, 0xdf, 0xec, 0xff, 0xff };
    static const BYTE b_cmd[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_panel[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xc6, 0xc1, 0x95, 0x00 };
    if (memcmp((void *)0x6bda60, b_toggle, sizeof b_toggle) || memcmp((void *)0x6ba9d5, b_econ, sizeof b_econ) ||
        memcmp((void *)0x6bad07, b_mil, sizeof b_mil) || memcmp((void *)0x6c020a, b_call, sizeof b_call) ||
        memcmp((void *)0x6ca51c, b_repair, sizeof b_repair) || memcmp((void *)0x6bfc40, b_cmd, sizeof b_cmd) ||
        // 0x6c9dc0 may already start with formtab.c's JMP (formations tab): wl_hook chains to it
        (memcmp((void *)0x6c9dc0, b_panel, sizeof b_panel) && *(BYTE *)0x6c9dc0 != 0xE9)) {
        wl_log("buildpage: unexpected bytes, mod buildings page off (econ %d mil %d toggle %d call %d repair %d cmd %d panel %d)",
               !memcmp((void *)0x6ba9d5, b_econ, sizeof b_econ), !memcmp((void *)0x6bad07, b_mil, sizeof b_mil),
               !memcmp((void *)0x6bda60, b_toggle, sizeof b_toggle), !memcmp((void *)0x6c020a, b_call, sizeof b_call),
               !memcmp((void *)0x6ca51c, b_repair, sizeof b_repair), !memcmp((void *)0x6bfc40, b_cmd, sizeof b_cmd),
               !memcmp((void *)0x6c9dc0, b_panel, sizeof b_panel));
        return;
    }
    int ok = patch_jmp(0x6ba9d5, (void *)wl_bp_stub_econ, sizeof b_econ);
    ok = ok && patch_jmp(0x6bad07, (void *)wl_bp_stub_mil, sizeof b_mil);
    ok = ok && wl_hook(0x6bda60, page_toggle, 9) != NULL;
    ok = ok && wl_patch_call(0x6c020a, military_page, 5);
    ok = ok && wl_patch_call(0x6ca51c, add_repair_and_mods, 5);
    ok = ok && (o_command = (Command_t)wl_hook(0x6bfc40, command_hook, 5)) != NULL;
    ok = ok && (o_panel = (Panel_t)wl_hook(0x6c9dc0, panel_hook, 5)) != NULL;
    wl_log("buildpage: mod buildings page %s", ok ? "installed" : "FAILED");
}
