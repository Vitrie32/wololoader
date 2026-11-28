// Wololoader - mod loader for Age of Empires II HD (5.8).
#pragma once
#include <windows.h>

#define WOLOLOADER_VERSION "0.7.1"
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
int wl_autobuild_run(void);       // 1 = rebuilt, 0 = up to date, -1 = error
unsigned long long wl_autobuild_fingerprint(void);
void wl_content_checksum(char out[9]);
void wl_content_checksum_invalidate(void);
extern char g_wl_build_error[512];

// ui_mods.c
void wl_install_mods_screen(void);
void wl_mods_return_from_hub(int mods_changed);
void wl_mods_check_updates(void);   // background Mod Hub check for the update badges
int wl_mods_is_readonly_open(void);

// ui_hub.c
void wl_hub_open(void *return_screen, int readonly);

// zlib_game.c
int wl_game_inflate_raw_bounded(const unsigned char *in, size_t in_len, size_t max_out,
                                unsigned char **out, size_t *out_len);

// iconcache.c: scaled PNG caching for mod icons (WIC)
const char *wl_icon_scaled(const char *src_png, const char *mod_id, int size);

// shell.c
void wl_open_folder(const char *path);
BOOL wl_launch_scriptorium_args(const char *args);   // same, with extra arguments ("--hub")
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

// autostart.c: quick-launch single-player match from <root>\autostart.json ("Test it")
void wl_install_autostart(void);

// overflow.c: units selected beyond the game's 60 (0..190, pruned); total selection = 60 + this
int wl_overflow_count(void);
int wl_overflow_owns(void *player);   // is this the list's player object?
// selpanel.c: "Selected: N (+M)" label on the multi-selection panel
void wl_install_selpanel(void);

// mpguard.c: warns when entering multiplayer with mods active
void wl_install_mp_guard(void);

// mpcheck.c: lobby mod compatibility check via lobby chat
void wl_install_mp_check(void);
void wl_mpcheck_on_lobby_enter(void *screen);

// civs.c
void wl_install_civ_hooks(void);
void wl_civs_ensure(void);  // lazy: autobuild + civs.txt, never from DllMain
int wl_civs_count(void);
int wl_civs_reload_tables(int *old_ids, int *old_n, int *new_ids, int *new_n);
int wl_civs_reconcile_registry(void);
const char *wl_civ_get_sound(int civ_id);
const char *wl_civ_get_victory_sound(int civ_id);
void wl_civs_set_mode_string(int slot, const char *text);

// gamemodes.c: custom lobby game modes (relocated GameMode table, ids 12..63)
#define WL_MODE_VANILLA_COUNT 12
#define WL_MODE_CAP           64   // 0x40; <= 127 (signed imm8 in 6a XX) and <= 255 (uint8 in saves)
#define WL_MODE_MAX_CUSTOM    (WL_MODE_CAP - WL_MODE_VANILLA_COUNT)
#define WL_MODE_SID_BASE      9200000
void wl_install_gamemodes(void);
const char *wl_gamemode_key_by_id(int mode_id);
const char *wl_gamemode_current_key(void);
int wl_gamemode_map_script(const char *file_name, char *out, size_t out_size); // current mode's .rms for a game map script
int wl_gamemode_id_by_key(const char *key);
int wl_gamemode_enemy_player(void);
int wl_mp_rehearsal(void);
void wl_bp_panel_refresh(void *orig, void *screen);   // buildpage.c; orig = __thiscall(screen) panel refresh
void wl_script_refresh_settings(void);   // script.c: refill MOD.settings tables (lobby host values)   // luagame.c: <root>\mp_rehearsal.txt -> single player takes our MP paths
int wl_gamemode_map_tiles(void);   // map side in tiles from the lobby map size, 0 = unknown
int wl_gamemode_set_enemy_player(int pid);  // a loaded game: the script restores the enemy player
void wl_ui_set_map_capture(int on);
void wl_ui_set_unit_control_disabled(int off);
int wl_ui_request_restart(void);
void wl_script_event_map_click(float tx, float ty, int button);
void wl_script_clear_session(void);
int wl_gamecmd_in_defeat(void);
int wl_play_sfx_file(const char *path);

// hotreload.c: Level 2 mod hot reload at the main menu (World == NULL)
void wl_install_hotreload(void);
void wl_hotreload_mark_baseline(void);
int wl_hotreload_mods_changed(void);
int wl_hotreload_can_run(char *reason, size_t cap);
int wl_hotreload_run(int force, char *out_msg, size_t out_cap);
int wl_hotreload_from_lua(char *out_msg, size_t out_cap);
const char *wl_hotreload_last_message(void);
int wl_hotreload_restart_reason(char *out, size_t cap);

// victory.c
void wl_install_victory(void);

// taunts.c
void wl_install_taunts(void);

// script.c: Lua 5.4 mod scripts (<mod>\\scripts\\main.lua). Single-threaded: game main thread only.
void wl_script_init(void);   // lazy (from wl_civs_ensure), never from DllMain; idempotent
void wl_script_restart(void); // full reload of all mod scripts (hotreload.c)
void wl_script_set_reload_mods(int (*fn)(char *msg, size_t cap));
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
// resources.c: custom resources registry (cache\resources.txt), <cost> tooltip & error text hooks
#define WL_CUSTOM_RES_FIRST 211
#define WL_CUSTOM_RES_MAX   32766
typedef struct {
    int id;                 // numeric id >= 211
    char mod[64];           // declaring mod folder
    char key[65];           // short key (e.g. "faith")
    char name[129];         // display name (e.g. "Faith")
    char icon[MAX_PATH];    // absolute path to icon PNG, or empty
    char icon_rel[257];     // relative icon path from mod.json
    float start;
    float max;              // 0 = no cap
    int show;               // 1 = show in top bar
    int hidden;             // 1 = hide from top bar and "Show all" panel completely
    char game_mode[65];     // if non-empty, only show in HUD during this custom game mode
    char description[256];
} WlResourceDef;

typedef struct {
    char key[65];           // resource key (col 0 of cache\gather.txt)
    int resource_id;        // custom resource id >= 211 (col 1)
    int deposit_id;         // Gaia deposit unit id (col 2)
    int male_id;            // male miner unit id (col 3)
    int female_id;          // female miner unit id (col 4)
} WlGatherDef;

void wl_install_resources(void);
void wl_resources_ensure(void);          // lazy load cache\resources.txt + gather.txt (after wl_civs_ensure)
void wl_resources_reload(void);          // reload cache\resources.txt + gather.txt
const WlResourceDef *wl_resources_list(int *out_count);
const WlResourceDef *wl_resource_by_id(int id);
int wl_resource_resolve(const char *key, const char *prefer_mod);
const WlGatherDef *wl_gather_list(int *out_count);
const WlGatherDef *wl_gather_by_resource(int resource_id);
const WlGatherDef *wl_gather_by_deposit(int deposit_id);
const WlGatherDef *wl_gather_by_key(const char *key, const char *prefer_mod);

// resbar.c: custom resource top-bar HUD + "Show all" panel (driven by uikit.c)
int  wl_resbar_active(void);
#ifdef _WIN32
void wl_resbar_paint(void *screen, HDC dc, const RECT *scr);
void wl_resbar_paint_passive(void *screen, HDC dc, const RECT *scr);   // under menus: no panel/hover
int  wl_resbar_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp, int active, int scr_w, int scr_h);
#endif
void wl_resbar_reset(void);

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
    int (*move_unit)(int unit_id, float x, float y);   // walk to a map tile (game move command)
    int (*units_of_type)(int pid, int type, WlUnitInfo *out, int max);   // the whole list, one unit type
    int (*spawn_neutral)(int pid, int type, float x, float y);       // like spawn, never captured
    int (*set_resource)(int pid, int res, float amount);             // 12-byte marked 0x05 set-resource packet
    const WlResourceDef *(*resources_list)(int *out_count);          // custom resources from cache/resources.txt
    int (*resource_resolve)(const char *key, const char *prefer_mod);// resolve key/name to numeric id or -1
    const char *(*game_mode)(void);                                  // current custom mode key, or NULL for vanilla
    int (*win_player)(int pid, int team);                            // marked 0x05 win packet (team: 0 solo, 1 team)
    int (*defeat_player)(int pid);                                   // marked 0x05 defeat packet
    int (*player_civ)(int pid);                                      // civ id of a player, -1 if none
    int (*attack_unit)(int unit_id, int target_id);                  // game order (attack) command
    const WlGatherDef *(*gather_by_key)(const char *key, const char *prefer_mod); // cache/gather.txt lookup
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
void wl_basemod_reload(void);
void wl_basemod_read_flags(int *popcap, int *sel250, int *borderless, int *formtab);
void wl_monk_tick(void);  // monk.c, once a second in a game
void wl_install_lobby(void);  // lobby.c
void wl_install_history(void); // history.c
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
void wl_files_clear_redirects(void);
void wl_files_rebuild_assets(void);
void wl_files_drs_fingerprints(unsigned long long pkg_fp[4], unsigned long long *iface_new_fp);
void wl_files_sync_rms_defs(const WlGatherDef *defs, int count);

// popcap.c: "1000" population option + lifted 500 clamps
void wl_install_popcap(void);

// overflow.c: selection beyond 60 units, stage 1 = dry run (log only)
void wl_install_overflow(void);
BOOL wl_overflow_exec(void *orig, void *proc, unsigned char *pkt);   // overflow.c, called by gamecmd.c
void wl_install_gamecmd(void);
int wl_cmd_add_resource(int pid, int res, float amount);
int wl_cmd_set_resource(int pid, int res, float amount);
int wl_cmd_research(int pid, int tech);
int wl_cmd_spawn(int pid, int type, float x, float y);
int wl_cmd_delete(int unit_id);
int wl_cmd_move(int unit_id, float x, float y);
int wl_cmd_attack(int unit_id, int target_id);   // game order command 0 (gamecmd.c)
int wl_cmd_spawn_neutral(int pid, int type, float x, float y);   // never captured by players (gamecmd.c)   // game move command 3 (gamecmd.c)
int wl_cmd_set_age(int pid, int age);
int wl_cmd_win(int pid, int team);
int wl_cmd_defeat(int pid);
void wl_gamecmd_frame(void);   // sends Lua commands queued over the 100/s rate
void wl_cmd_set_local_ctx(int on);
// uikit.c: Lua on-screen UI
void wl_install_uikit(void);
void wl_install_buildpage(void);   // buildpage.c: villager mod buildings page
void wl_buildpage_refresh(void);   // buildpage.c: refresh active building command panel (0x6c6490)
void wl_icons_build(void);          // iconmerge.c: append mod icons to interface SLPs
int wl_icon_index(const char *sheet, const char *name);
int wl_cmd_icon_index(const char *mod, const char *name);  // iconmerge.c: <mod>/icons/commands/<name>.png -> 50721 frame
#define WL_MAX_BLDG_CMDS 128
#define WL_MAX_RT_CMDS 64
#define WL_RT_CMD_SID_BASE 9100000
typedef struct { int civ, building, button, icon, help; char name[64]; } WlLuaButton;
typedef struct {
    int civ;
    int building;
    int page;
    int button;
    char type[16];          // "train", "research", "lua"
    int target_id;          // unit id or tech id or 0
    int icon;
    int help;
    char name[64];          // handler or display name
    char cond_action[16];   // "disable" or "hide"
    char cond_name[64];     // condition name registered in Lua
} WlBuildingCmd;
typedef struct {
    int used;
    int civ;                // -1 = any civ, or specific civ id
    int building;           // master unit id (e.g. 84 Market, 109 Town Center)
    int page;               // 1-based command page (default 1)
    int button;             // 1..15 (slot = button - 1)
    int icon;               // frame index in 50721.slp
    int help_sid;           // WL_RT_CMD_SID_BASE + slot index
    DWORD gen;              // monotonic generation passed as sub_id to 0x6c9200
    char name[64];          // command handler name (Game.OnCommand)
    char cond_action[16];   // "disable" or "hide"
    char cond_name[64];     // condition name (defaults to name)
    char mod[64];           // owning mod id
    int shared;             // Game.AddCommand shared = true: allies of the owner see and use it too
} WlRuntimeCmd;
const WlLuaButton *wl_lua_buttons(int *n);   // civs.c: Lua buttons of mod buildings (cache\\commands.txt)
const WlBuildingCmd *wl_building_commands(int *n);
const WlRuntimeCmd *wl_rt_commands(int *n);  // script.c: runtime Game.AddCommand table (length WL_MAX_RT_CMDS)
void wl_script_set_rt_cmd_string_fn(void (*fn)(int slot, const char *text, const char *help));
void wl_script_set_buildpage_refresh(void (*fn)(void));
void wl_script_set_local_ctx_fn(void (*fn)(int on));
void wl_script_set_cmd_icon_fn(int (*fn)(const char *mod, const char *name));
int wl_script_reload_mod(const char *mod_id, char *err_out, size_t err_cap);
void wl_script_command(const char *name, int unit_id);   // script.c: Game.OnCommand handlers
void wl_script_cond_begin_refresh(int unit_id);
void wl_script_cond_end_refresh(void);
int wl_script_eval_condition(const char *name, int unit_id);
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
