// Wololoader - mod loader for Age of Empires II HD (5.8).
#pragma once
#include <windows.h>

#define WOLOLOADER_VERSION "0.4.0"
#define WL_MAX_MODS 256

typedef struct {
    char dir[MAX_PATH];   // absolute folder of the mod
    char id[64];          // folder name
    char name[128];       // "name" from mod.json, falls back to id
    char version[32];     // "version" from mod.json
    char author[64];      // "author" from mod.json
    char description[256];// "description" from mod.json
    int civ_count;        // files in civs\*.json
    BOOL enabled;
    char icon[MAX_PATH];  // "<mod dir>\icon.png" if exists, else empty
} WlMod;

typedef struct {
    char game_dir[MAX_PATH];  // folder holding AoK HD.exe
    char root[MAX_PATH];      // <game_dir>\wololoader
    WlMod mods[WL_MAX_MODS];
    int mod_count;            // enabled mods actually loaded
    int listed_count;         // every folder with a mod.json (enabled or not)
} WlState;

extern WlState g_wl;

// log.c
void wl_log_open(void);
void wl_log(const char *fmt, ...);

// mods.c
void wl_scan_mods(void);
// Vanilla Mode (for online games with players without Wololoader):
// <root>\vanilla.flag present at start -> no mods, no dat redirect, no patches;
// only the menu label and the Mods screen (to switch it off again) are installed.
extern BOOL g_wl_vanilla;
BOOL wl_vanilla_flag_exists(void);
BOOL wl_vanilla_set(BOOL on);   // creates / deletes the flag; takes effect on restart
BOOL wl_set_mod_enabled(const char *mod_dir, BOOL enabled);

// autobuild.c: rebuilds wololoader\cache when mods changed (before civ hooks)
void wl_autobuild(void);
extern char g_wl_build_error[512];

// ui_mods.c
void wl_install_mods_screen(void);

// iconcache.c: scaled PNG caching for mod icons (WIC)
const char *wl_icon_scaled(const char *src_png, const char *mod_id, int size);

// shell.c
void wl_open_folder(const char *path);
BOOL wl_launch_scriptorium(void);   // starts <root>\\Scriptorium.exe

// hook.c
// Detour `target` with a 5-byte JMP to `detour`. `stolen` is the byte count
// of whole instructions overwritten (>=5). Returns a trampoline that runs the
// original instructions and jumps back, or NULL on failure.
void *wl_hook(DWORD target, void *detour, int stolen);
BOOL wl_patch_call(DWORD at, void *fn, int len);

// menu.c
void wl_install_menu_hooks(void);
void wl_menu_set_label(const char *text);

// overflow.c: units selected beyond the game's 60 (0..190, pruned); total selection = 60 + this
int wl_overflow_count(void);
int wl_overflow_owns(void *player);   // is this the list's player object?
// selpanel.c: "Selected: N (+M)" label on the multi-selection panel
void wl_install_selpanel(void);

// mpguard.c: warns when entering multiplayer with mods active
void wl_install_mp_guard(void);

// civs.c
void wl_install_civ_hooks(void);
void wl_civs_ensure(void);  // lazy: autobuild + civs.txt, never from DllMain

// taunts.c
void wl_install_taunts(void);

// script.c: Lua 5.4 mod scripts (<mod>\\scripts\\main.lua). Single-threaded: game main thread only.
void wl_script_init(void);   // lazy (from wl_civs_ensure), never from DllMain; idempotent
void wl_script_event(const char *name);
void wl_script_event_int(const char *name, int arg);
// Plug the game-side UI in later; until then wl.message/wl.dialog only log.
void wl_script_set_ui(void (*message)(const char *), void (*dialog)(const char *));
// chat: sender may be NULL; player 1..8 = that player's colour, 0 = default.
void wl_script_set_chat(void (*chat)(const char *text, const char *sender, int player));
// ask: show a Yes/No question; the backend later calls wl_script_ask_answer(token, ...).
void wl_script_set_ask(void (*ask)(const char *text, int token));
// answer: 1 = Yes, 0 = No, -1 = not asked (multiplayer / no dialog): callback gets nil.
void wl_script_ask_answer(int token, int answer);
// Per-frame driver for wl.after / wl.every / wl.wait (real time, ms, from GetTickCount).
void wl_script_frame(DWORD now_ms);
// New game / game screen gone: drops all timers, waiting coroutines and open asks.
void wl_script_game_reset(DWORD now_ms);
void wl_script_set_sound(void (*sound)(int id));
// Read-only game state for Lua (gameapi.c; script.c uses it through this table)
typedef struct { int id; float x, y; int cls; int building; int type; int owner; int state; } WlUnitInfo;  // state: [unit+0x4c], 2 = ready (a building: finished)
typedef struct {
    int (*local_player)(void);
    int (*player_count)(void);
    int (*resource)(int pid, int res, float *out);
    int (*is_enemy)(int a, int b);
    int (*units)(int pid, WlUnitInfo *out, int max);
    // Game commands (gamecmd.c): 1 sent, 0 another machine sends it, -1 invalid / no game.
    int (*add_resource)(int pid, int res, float amount);
    int (*research)(int pid, int tech);
    int (*spawn)(int pid, int type, float x, float y);
    int (*delete_unit)(int unit_id);
    int (*selected)(WlUnitInfo *out, int max);   // the local player's selection (first 60)
    double (*game_time)(void);                   // game time in seconds, -1 = no game
    int (*set_age)(int pid, int age);
    int (*researched)(int pid, short *out, int max);   // ids of the techs the player has researched
} WlGameApi;
void wl_script_set_game(const WlGameApi *api);
const WlGameApi *wl_game_api(void);
// counter: text of the on-screen counter line, NULL = hide (wl.counter).
void wl_script_set_counter(void (*counter)(const char *text));
// counter.c: the on-screen counter line (countdown-clock panel 9)
void wl_install_counter(void);
void wl_counter_set(const char *text);   // NULL or "" hides; max 100 chars kept
void wl_counter_frame(void *game_screen);
void wl_counter_reset(void);
void wl_install_lua_game(void);
BOOL wl_feature(const char *name);
BOOL wl_feature_opt(const char *name);  // basemod.c: off unless set true
void wl_monk_tick(void);  // monk.c, once a second in a game
void wl_install_lobby(void);  // lobby.c
void wl_mods_readonly_reset(void);  // ui_mods.c
void wl_install_string_hook_only(void);  // civs.c (Vanilla Mode)
void wl_install_ui_textures_only(void);  // files.c (Vanilla Mode)  // basemod.c: Wololoader Base mod feature switches  // luagame.c: game_start event, wl.message backend

// crash.c
void wl_install_crash_logger(void);
void wl_install_hang_watchdog(void);   // logs where the main thread loops when the game freezes

// loadscreen.c: custom AoE2-themed loading window
void wl_install_loadscreen(void);
void wl_loadscreen_status(const char *text);
void wl_loadscreen_add_log(const char *line);
void wl_loadscreen_close(void);

// files.c
void wl_install_file_hooks(void);

// popcap.c: "1000" population option + lifted 500 clamps
void wl_install_popcap(void);

// overflow.c: selection beyond 60 units, stage 1 = dry run (log only)
void wl_install_overflow(void);
BOOL wl_overflow_exec(void *orig, void *proc, unsigned char *pkt);   // overflow.c, called by gamecmd.c
void wl_install_gamecmd(void);
int wl_cmd_add_resource(int pid, int res, float amount);
int wl_cmd_research(int pid, int tech);
int wl_cmd_spawn(int pid, int type, float x, float y);
int wl_cmd_delete(int unit_id);
int wl_cmd_set_age(int pid, int age);
void wl_gamecmd_frame(void);   // sends Lua commands queued over the 100/s rate
void wl_cmd_set_local_ctx(int on);
// uikit.c: Lua on-screen UI
void wl_install_uikit(void);
void wl_install_buildpage(void);   // buildpage.c: villager mod buildings page
void wl_icons_build(void);          // iconmerge.c: append mod icons to interface SLPs
int wl_icon_index(const char *sheet, const char *name);
typedef struct { int civ, building, button, icon, help; char name[64]; } WlLuaButton;
const WlLuaButton *wl_lua_buttons(int *n);   // civs.c: Lua buttons of mod buildings (cache\\commands.txt)
void wl_script_command(const char *name, int unit_id);   // script.c: Game.OnCommand handlers
void wl_ui_frame(void);
int wl_ui_create(int type);              // 1 frame, 2 label, 3 button, 4 text box; id or -1
void wl_ui_destroy(int id);
int wl_ui_set(int id, const char *key, int v, const char *s);
// script.c backend for the UI (set by luagame.c; NULL in the native test)
typedef struct { int (*create)(int); void (*destroy)(int); int (*set)(int, const char *, int, const char *);
                 const char *(*get_text)(int); } WlUiApi;
const char *wl_ui_get_text(int id);
void wl_script_set_uikit(const WlUiApi *api);
void wl_install_formtab(void);      // formtab.c: Formations page tab button
void wl_install_no_workshop(void);  // noworkshop.c
void wl_install_borderless(void);   // borderless.c
void wl_add_redirect(const char *file_name, const char *path);

// seltrace.c (only when WL_SELTRACE is defined)
#ifdef WL_SELTRACE
void wl_seltrace_init(void);
void wl_seltrace_shutdown(void);
void wl_seltrace_tick(void);
#endif

// exported for other mods / tools
__declspec(dllexport) int __stdcall Wololoader_GetModCount(void);
__declspec(dllexport) const char *__stdcall Wololoader_GetVersion(void);
