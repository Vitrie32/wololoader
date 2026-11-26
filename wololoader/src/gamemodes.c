// Custom lobby game modes: relocate the game's fixed 12-slot GameMode table
// (0xaf28dc..0xaf290b) to our 64-slot g_mode_table[], append mod-declared
// Random-Map-like GameMode objects at IDs 12..63 in GameMode::init (0x608bf0),
// clamp invalid/removed mode IDs back to 0 (Random Map) in GameSettings::validate
// (0x60f2f0) and GameSettings::set_game_mode (0x6133c0), and append custom modes
// to the lobby dropdown via a 5-byte CALL patch at 0x64b659 right before the
// game's own tooltip loop (0x64b661..0x64b810).
//
// All 48 relocated instruction sites, hook prologues, ret N sizes, and callees
// were verified directly against AoK HD.exe 5.8 (re/FINDINGS.md, "Game mode table").
// Every site's original bytes are checked before writing a single byte; if any
// differs, all mode patches stay off.
//
// Startup order (AGENTS.md rule 5):
// - wl_install_gamemodes() runs in DllMain BEFORE AoK HD.exe's .CRT$XCU static
//   ctor 0x41e400. It only reads mod.json files (C stdio + json.c) and patches
//   code bytes; it never calls into game code or other DLLs.
// - .CRT$XCU (0x41e400) then initializes g_mode_table[0..WL_MODE_CAP-1].
// - WinMain -> 0x81b060 -> 0x608bf0 (GameMode::init) populates slots 0..11 and
//   our post-hook constructs custom GameMode objects into slots 12..12+N-1.
#include "wololoader.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_GAME_NEW          0x8d47df  // __cdecl(size_t) -> void*
#define ADDR_GAME_MEMSET       0x8d62f0  // __cdecl(void*, int, size_t) -> void*
#define ADDR_EMPTY_STR_DESC    0xad0380  // static empty WlGameStr backing buffer
#define ADDR_GAME_PTR          0xaf2c58  // global Game*
#define ADDR_GAMEMODE_INIT     0x608bf0  // __cdecl(void *out) -> void*, plain ret
#define ADDR_GAMEMODE_CTOR     0x608a30  // __thiscall(this, 9 args) -> this, ret 0x24
#define ADDR_SETTINGS_VALIDATE 0x60f2f0  // __thiscall(settings), plain ret
#define ADDR_SET_GAME_MODE     0x6133c0  // __thiscall(settings, int mode), ret 4
#define ADDR_DROPDOWN_UNWRAP   0x426d40  // __cdecl(void *ref_ptr) -> Dropdown*, plain ret
#define ADDR_DROPDOWN_ADD_ITEM 0x47b690  // __thiscall(dd, const char *text, int id), ret 8
#define ADDR_DROPDOWN_CALLSITE 0x64b659  // call 0x426d40 right before tooltip loop in 0x64a670

typedef struct {
    char *data;
    int   len;
    int   cap;
} WlGameStr;

typedef struct {
    BYTE      is_localized;
    BYTE      pad[3];
    int       string_id;
    WlGameStr str;
} WlLocalizedStr;

typedef struct {
    int *begin;
    int *end;
    int *cap;
} WlVecInt;

typedef struct {
    int  id;                // assigned engine mode ID (12..63)
    char mod[64];           // declaring mod folder ID
    char key[64];           // mod.json "id" (returned by Lua Game.Mode())
    char name[128];         // display name (UTF-8 sanitized)
    char description[512];  // lobby dropdown tooltip (UTF-8 sanitized)
    int  script_event;      // mod.json "script_event" flag
    int  one_team;          // mod.json "teams": "all" -> every player on one team (single player)
    int  enemy_player;      // mod.json "enemy_player": true -> an extra computer player on its own team
    int  map_type;          // mod.json "map": {"base": ...}: built-in map forced at start (0 = none)
    char map_file[32];      // that map's script file name ("Arena.rms"), served from map_script
    char map_script[MAX_PATH]; // mod.json "map": {"script": ...}: the mod's .rms (full path)
    int  map_min_size;      // mod.json "map": {"min_size": ...}: smallest map size index (0 = Tiny .. 6), -1 = any
    char map_custom[64];    // that script's file name without ".rms": picked as a Custom map in the lobby
} WlCustomMode;

typedef char (__thiscall *SetupStart_t)(void *screen);
#define ADDR_SETUP_START 0x655180   // GameSetupScreen::start_clicked, see hook_setup_start
static SetupStart_t g_orig_setup_start;
static char __fastcall hook_setup_start(void *screen, void *edx);
typedef void (__thiscall *ScreenChanged_t)(void *screen);
static ScreenChanged_t g_orig_screen_changed;
static void __fastcall hook_screen_changed(void *screen, void *edx);
typedef void (__thiscall *SetStatus_t)(void *player, int status);
static SetStatus_t g_orig_set_status;
static void __fastcall hook_set_status(void *player, void *edx, int status);

// Relocated backing table replacing AoK HD.exe's static RefPtr<GameMode>[12] at 0xaf28dc.
// Zero-initialized in DLL .bss; .CRT$XCU (0x41e400) constructs all 64 slots and
// 0x60a200 / 0x9a8450 tear down all 64 slots cleanly at shutdown.
static void *g_mode_table[WL_MODE_CAP];

static WlCustomMode g_modes[WL_MODE_MAX_CUSTOM];
static int g_mode_count;
static int g_modes_inited;
static int g_modes_installed;

typedef void *(__cdecl *GameNew_t)(size_t size);
typedef void *(__cdecl *GameMemset_t)(void *dst, int val, size_t size);
typedef void *(__cdecl *GameModeInit_t)(void *out);
typedef void *(__thiscall *GameModeCtor_t)(
    void *self, int id, int is_scenario, int field_40, int is_rm_like, int elo_type,
    const WlVecInt *sp_victories, const WlVecInt *mp_victories,
    const WlLocalizedStr *name, const WlLocalizedStr *tooltip);
typedef void (__thiscall *SettingsValidate_t)(void *settings);
typedef void (__thiscall *SetGameMode_t)(void *settings, int mode);
typedef void *(__cdecl *Unwrap_t)(void *ref_ptr);
typedef void (__thiscall *DropdownAddItem_t)(void *dd, const char *text, int id);
typedef void (__thiscall *DeleteThis_t)(void *obj);

static GameModeInit_t     g_orig_gamemode_init;
static SettingsValidate_t g_orig_settings_validate;
static SetGameMode_t      g_orig_set_game_mode;

// Replace any byte not part of a valid UTF-8 sequence with '?' so the game's
// UTF-8 decoder (0x444e40) never hangs on malformed text.
static void sanitize_utf8(char *s) {
    unsigned char *p = (unsigned char *)s;
    while (*p) {
        int n = (*p < 0x80) ? 1 : ((*p & 0xe0) == 0xc0 && *p >= 0xc2) ? 2
              : ((*p & 0xf0) == 0xe0) ? 3 : ((*p & 0xf8) == 0xf0 && *p <= 0xf4) ? 4 : 0;
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) ok = (p[i] & 0xc0) == 0x80;
        if (!ok) { *p++ = '?'; continue; }
        p += n;
    }
}

static int cmp_mod_ptr(const void *a, const void *b) {
    const WlMod *ma = *(const WlMod *const *)a;
    const WlMod *mb = *(const WlMod *const *)b;
    int r = _stricmp(ma->id, mb->id);
    return r ? r : strcmp(ma->id, mb->id);
}

// Scan enabled mods in deterministic sorted mod-folder order, then declaration
// order within each mod's mod.json "game_modes" array. Pure file/JSON I/O (safe
// inside DllMain, AGENTS.md rule 5).
static void scan_mod_game_modes(void) {
    g_mode_count = 0;
    const WlMod *enabled[WL_MAX_MODS];
    int n_en = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        if (g_wl.mods[i].enabled) enabled[n_en++] = &g_wl.mods[i];
    }
    if (n_en > 1) qsort(enabled, (size_t)n_en, sizeof *enabled, cmp_mod_ptr);

    for (int mi = 0; mi < n_en; mi++) {
        const WlMod *m = enabled[mi];
        char path[MAX_PATH], err[128] = "";
        snprintf(path, sizeof path, "%s\\mod.json", m->dir);
        char *text = wl_read_file(path, NULL);
        if (!text) continue;
        WlJsonVal *root = wl_json_parse(text, err, sizeof err);
        free(text);
        if (!root) {
            wl_log("gamemodes: %s: JSON error: %s", path, err);
            continue;
        }
        const WlJsonVal *arr = wl_json_get(root, "game_modes");
        size_t len = wl_json_len(arr);
        for (size_t j = 0; j < len; j++) {
            const WlJsonVal *item = wl_json_at(arr, j);
            if (!item || item->type != WL_JSON_OBJECT) continue;
            const char *key = wl_json_str(wl_json_get(item, "id"), NULL);
            if (!key || !key[0]) {
                wl_log("gamemodes: %s: game_modes[%u] missing non-empty 'id', skipped", m->id, (unsigned)j);
                continue;
            }
            int dup = 0;
            for (int k = 0; k < g_mode_count; k++) {
                if (strcmp(g_modes[k].key, key) == 0) { dup = 1; break; }
            }
            if (dup) {
                wl_log("gamemodes: %s: duplicate game mode id '%s', skipped", m->id, key);
                continue;
            }
            if (g_mode_count >= WL_MODE_MAX_CUSTOM) {
                wl_log("gamemodes: limit %d reached, skipping '%s' from %s", WL_MODE_MAX_CUSTOM, key, m->id);
                break;
            }
            const char *name = wl_json_str(wl_json_get(item, "name"), NULL);
            if (!name || !name[0]) name = key;
            const char *desc = wl_json_str(wl_json_get(item, "description"), "");
            const WlJsonVal *se = wl_json_get(item, "script_event");
            int script_ev = se ? wl_json_is_true(se) : 1;

            WlCustomMode *cm = &g_modes[g_mode_count];
            memset(cm, 0, sizeof *cm);
            cm->id = WL_MODE_VANILLA_COUNT + g_mode_count;
            snprintf(cm->mod, sizeof cm->mod, "%s", m->id);
            snprintf(cm->key, sizeof cm->key, "%s", key);
            snprintf(cm->name, sizeof cm->name, "%s", name);
            snprintf(cm->description, sizeof cm->description, "%s", desc ? desc : "");
            sanitize_utf8(cm->name);
            sanitize_utf8(cm->description);
            cm->script_event = script_ev;
            const char *teams = wl_json_str(wl_json_get(item, "teams"), "");
            cm->one_team = teams && _stricmp(teams, "all") == 0;
            cm->enemy_player = wl_json_is_true(wl_json_get(item, "enemy_player"));

            // "map": {"base": "arena", "script": "maps/X.rms"}: the mode plays on its own random map.
            // A custom .rms cannot be picked by name yet (the settings field for it is not mapped,
            // re/research/custom_rms.md), so the mode forces a built-in map type and, while this
            // mode is the current one, the game's script file of that map is served from the mod.
            const WlJsonVal *mp = wl_json_get(item, "map");
            if (mp && mp->type == WL_JSON_OBJECT) {
                const char *base = wl_json_str(wl_json_get(mp, "base"), "");
                const char *script = wl_json_str(wl_json_get(mp, "script"), "");
                // Map size indices as in autostart.c (string id 0x2973 + size, 0x7d4540)
                static const char *k_sizes[] = { "tiny", "small", "medium", "normal", "large", "giant", "ludikris" };
                const char *ms = wl_json_str(wl_json_get(mp, "min_size"), "");
                cm->map_min_size = -1;
                for (int z = 0; z < 7; z++) if (ms && _stricmp(ms, k_sizes[z]) == 0) cm->map_min_size = z;
                // Map type ids: MAPTYPE_* static init 0x418de0..0x41a600 (same table as autostart.c);
                // file names: resources\_common\drs\gamedata_x2\*.rms of the game.
                static const struct { const char *name; int id; const char *file; } k_base[] = {
                    { "arabia", 9, "Arabia.rms" }, { "fortress", 16, "Fortress.rms" },
                    { "arena", 29, "Arena.rms" }, { "nomad", 33, "nomad.rms" },
                };
                for (size_t b = 0; b < sizeof k_base / sizeof *k_base; b++) {
                    if (base && _stricmp(base, k_base[b].name) == 0) {
                        cm->map_type = k_base[b].id;
                        snprintf(cm->map_file, sizeof cm->map_file, "%s", k_base[b].file);
                    }
                }
                if (script && script[0]) {
                    const char *b = strrchr(script, '/'), *b2 = strrchr(script, '\\');
                    if (b2 > b) b = b2;
                    snprintf(cm->map_custom, sizeof cm->map_custom, "%s", b ? b + 1 : script);
                    char *dot = strrchr(cm->map_custom, '.');
                    if (dot && _stricmp(dot, ".rms") == 0) *dot = 0;
                }
                if (script && script[0] && cm->map_type) {
                    snprintf(cm->map_script, sizeof cm->map_script, "%s\\%s", m->dir, script);
                    for (char *c = cm->map_script; *c; c++) if (*c == '/') *c = '\\';
                    if (GetFileAttributesA(cm->map_script) == INVALID_FILE_ATTRIBUTES) {
                        wl_log("gamemodes: '%s': map script %s not found", key, cm->map_script);
                        cm->map_script[0] = 0;
                    }
                } else if (!cm->map_type) {
                    wl_log("gamemodes: '%s': unknown map base '%s' (arabia, fortress, arena, nomad)", key, base ? base : "");
                }
            }

            // Register display name & tooltip in civs.c's 0x448370 string hook.
            wl_civs_set_mode_string(g_mode_count * 2, cm->name);
            wl_civs_set_mode_string(g_mode_count * 2 + 1, cm->description);
            wl_log("gamemodes: id %d = '%s' (\"%s\", mod %s)", cm->id, cm->key, cm->name, cm->mod);
            g_mode_count++;
        }
        wl_json_free(root);
    }
}

// Mirrors 0x608bf0's RefPtr release before writing a slot.
static void release_mode_slot(int id) {
    if (id < 0 || id >= WL_MODE_CAP) return;
    void *old = g_mode_table[id];
    if (!old) return;
    g_mode_table[id] = NULL;
    int *rc = (int *)((char *)old + 4);
    if (--(*rc) == 0) {
        DeleteThis_t del = (DeleteThis_t)(*(void ***)old)[0];
        del(old);
    }
}

// Allocate and construct one Random-Map-like GameMode (0x5c bytes) on the game heap.
static void *create_custom_gamemode(int id, int name_sid, int tip_sid) {
    // Same victory types as Random Map (0): Standard(9), Conquest(1), Time(7), Score(8), LMS(11).
    static int rm_victories[5] = { 9, 1, 7, 8, 11 };
    WlVecInt vic = { rm_victories, rm_victories + 5, rm_victories + 5 };
    WlLocalizedStr name = { 1, {0, 0, 0}, name_sid, { (char *)ADDR_EMPTY_STR_DESC, 0, 0 } };
    WlLocalizedStr tip  = { 1, {0, 0, 0}, tip_sid,  { (char *)ADDR_EMPTY_STR_DESC, 0, 0 } };

    void *mem = ((GameNew_t)ADDR_GAME_NEW)(0x5c);
    if (!mem) return NULL;
    ((GameMemset_t)ADDR_GAME_MEMSET)(mem, 0, 0x5c);

    // Same flags as Random Map (0): is_scenario=0, field_40=1, is_rm_like=1, elo_type=1.
    // elo_type=1 ensures 0x7d34f0 returns "STAT_ELO_RM" (non-NULL) for Steam helpers;
    // modded games are already marked unranked by 0x5df660 before reading elo_type.
    void *obj = ((GameModeCtor_t)ADDR_GAMEMODE_CTOR)(mem, id, 0, 1, 1, 1, &vic, &vic, &name, &tip);
    if (obj) *(int *)((char *)obj + 4) = 1;  // initial RefPtr count for g_mode_table[id]
    return obj;
}

// Hook on GameMode::init (0x608bf0): __cdecl(void *out), returns out in eax.
static void *__cdecl hook_gamemode_init(void *out) {
    void *ret = g_orig_gamemode_init(out);
    g_modes_inited = 1;
    int built = 0;
    for (int i = 0; i < g_mode_count; i++) {
        int id = WL_MODE_VANILLA_COUNT + i;
        int name_sid = WL_MODE_SID_BASE + i * 2;
        int tip_sid  = WL_MODE_SID_BASE + i * 2 + 1;
        release_mode_slot(id);
        void *obj = create_custom_gamemode(id, name_sid, tip_sid);
        if (obj) {
            g_mode_table[id] = obj;
            built++;
        } else {
            wl_log("gamemodes: failed to allocate GameMode for id %d ('%s')", id, g_modes[i].key);
        }
    }
    wl_log("gamemodes: constructed %d/%d custom GameMode object(s)", built, g_mode_count);
    return ret;
}

// Hook on GameSettings::validate (0x60f2f0): __thiscall(settings), plain ret, void.
// Clamps out-of-range or unloaded mode IDs (e.g. saved in a profile after disabling
// a mod) back to 0 (Random Map) so 0x60f3a0 never dereferences a NULL slot.
static void __fastcall hook_settings_validate(void *settings, void *edx) {
    (void)edx;
    if (settings) {
        int *pm = (int *)((char *)settings + 0xac);
        int m = *pm;
        if (m < 0 || m >= WL_MODE_CAP || (g_modes_inited && !g_mode_table[m])) {
            wl_log("gamemodes: validate clamped mode %d -> 0", m);
            *pm = 0;
        }
    }
    g_orig_settings_validate(settings);
}

// Hook on GameSettings::set_game_mode (0x6133c0): __thiscall(settings, int mode), ret 4.
// Protects savegame/replay load (0x68df70) and network settings packets when a mod
// is no longer loaded.
static void __fastcall hook_set_game_mode(void *settings, void *edx, int mode) {
    (void)edx;
    if (mode < 0 || mode >= WL_MODE_CAP || (g_modes_inited && !g_mode_table[mode])) {
        wl_log("gamemodes: set_game_mode clamped mode %d -> 0", mode);
        mode = 0;
    }
    g_orig_set_game_mode(settings, mode);
}

// CALL patch at 0x64b659 (replaces `call 0x426d40` inside GameSetupScreen::init_controls
// 0x64a670, right after the last vanilla mode item and right before the tooltip loop
// at 0x64b661..0x64b810). Appends custom modes so the vanilla loop attaches their tooltips.
static void *__cdecl hook_append_dropdown_modes(void *ref_ptr) {
    void *dd = ((Unwrap_t)ADDR_DROPDOWN_UNWRAP)(ref_ptr);
    if (dd) {
        for (int i = 0; i < g_mode_count; i++) {
            int id = WL_MODE_VANILLA_COUNT + i;
            if (!g_mode_table[id]) continue;
            ((DropdownAddItem_t)ADDR_DROPDOWN_ADD_ITEM)(dd, g_modes[i].name, id);
        }
    }
    return dd;
}

// ---- Table relocation patch table (48 verified instructions) ---------------

enum {
    PATCH_IMM8_CAP,   // 1-byte signed imm8 count -> WL_MODE_CAP
    PATCH_IMM32_CAP,  // 4-byte imm32 count -> WL_MODE_CAP
    PATCH_ADDR_SLOT   // 4-byte address operand -> (DWORD)&g_mode_table[slot]
};

typedef struct {
    DWORD va;
    BYTE  len;
    BYTE  orig[8];
    BYTE  patch_off;
    BYTE  kind;
    BYTE  slot;       // for PATCH_ADDR_SLOT: slot index into g_mode_table
} ModePatch;

static const ModePatch k_table_patches[] = {
    // A. Static ctor (0x41e400), EH unwind (0x93ee70), atexit dtor (0x9a8450), shutdown (0x60a200)
    { 0x41e42c, 2, { 0x6a, 0x0c },                         1, PATCH_IMM8_CAP,  0 },
    { 0x41e430, 5, { 0x68, 0xdc, 0x28, 0xaf, 0x00 },       1, PATCH_ADDR_SLOT, 0 },
    { 0x93ee75, 2, { 0x6a, 0x0c },                         1, PATCH_IMM8_CAP,  0 },
    { 0x93ee79, 5, { 0x68, 0xdc, 0x28, 0xaf, 0x00 },       1, PATCH_ADDR_SLOT, 0 },
    { 0x9a847e, 2, { 0x6a, 0x0c },                         1, PATCH_IMM8_CAP,  0 },
    { 0x9a8482, 5, { 0x68, 0xdc, 0x28, 0xaf, 0x00 },       1, PATCH_ADDR_SLOT, 0 },
    { 0x60a243, 5, { 0xbf, 0x0c, 0x00, 0x00, 0x00 },       1, PATCH_IMM32_CAP, 0 },
    { 0x60a248, 5, { 0xbe, 0xdc, 0x28, 0xaf, 0x00 },       1, PATCH_ADDR_SLOT, 0 },

    // B. Per-mode slot read/write pairs in GameMode::init (0x608bf0), slots 0..11
    { 0x608d5c, 6, { 0x8b, 0x0d, 0xdc, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 0 },
    { 0x608d70, 6, { 0x89, 0x35, 0xdc, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 0 },
    { 0x608f1b, 6, { 0x8b, 0x0d, 0xe0, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 1 },
    { 0x608f2f, 6, { 0x89, 0x35, 0xe0, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 1 },
    { 0x60910e, 6, { 0x8b, 0x0d, 0xe4, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 2 },
    { 0x609122, 6, { 0x89, 0x35, 0xe4, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 2 },
    { 0x6092c1, 6, { 0x8b, 0x0d, 0xe8, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 3 },
    { 0x6092d5, 6, { 0x89, 0x35, 0xe8, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 3 },
    { 0x609470, 6, { 0x8b, 0x0d, 0xec, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 4 },
    { 0x609484, 6, { 0x89, 0x35, 0xec, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 4 },
    { 0x6095f0, 6, { 0x8b, 0x0d, 0xf0, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 5 },
    { 0x609604, 6, { 0x89, 0x35, 0xf0, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 5 },
    { 0x6097c7, 6, { 0x8b, 0x0d, 0xf4, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 6 },
    { 0x6097db, 6, { 0x89, 0x35, 0xf4, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 6 },
    { 0x609977, 6, { 0x8b, 0x0d, 0xf8, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 7 },
    { 0x60998b, 6, { 0x89, 0x35, 0xf8, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 7 },
    { 0x609b22, 6, { 0x8b, 0x0d, 0xfc, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 8 },
    { 0x609b36, 6, { 0x89, 0x35, 0xfc, 0x28, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 8 },
    { 0x609d18, 6, { 0x8b, 0x0d, 0x00, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 9 },
    { 0x609d2c, 6, { 0x89, 0x35, 0x00, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 9 },
    { 0x609ee4, 6, { 0x8b, 0x0d, 0x04, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 10 },
    { 0x609ef8, 6, { 0x89, 0x35, 0x04, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 10 },
    { 0x60a0f6, 6, { 0x8b, 0x0d, 0x08, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 11 },
    { 0x60a10a, 6, { 0x89, 0x35, 0x08, 0x29, 0xaf, 0x00 }, 2, PATCH_ADDR_SLOT, 11 },

    // C. All 15 indexed table reads + 1 direct slot-5 read (0x7311b9)
    { 0x522757, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x567281, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x5b6a90, 7, { 0x8b, 0x3c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x5df7e2, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x60d00d, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x60dddd, 7, { 0x8b, 0x04, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x60f0aa, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x60f3a0, 7, { 0x8b, 0x34, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x6133fe, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x64b6ab, 7, { 0x8b, 0x04, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x65b67d, 7, { 0x8b, 0x34, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x70252e, 7, { 0x8b, 0x04, 0x8d, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x7311b9, 6, { 0x8b, 0x0d, 0xf0, 0x28, 0xaf, 0x00 },       2, PATCH_ADDR_SLOT, 5 },
    { 0x76a75b, 7, { 0x8b, 0x3c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x7d347f, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
    { 0x7d3529, 7, { 0x8b, 0x0c, 0x85, 0xdc, 0x28, 0xaf, 0x00 }, 3, PATCH_ADDR_SLOT, 0 },
};

static int check_bytes(DWORD va, const BYTE *exp, size_t len, const char *label) {
    if (memcmp((const void *)va, exp, len) == 0) return 1;
    const BYTE *got = (const BYTE *)va;
    char ebuf[48] = "", gbuf[48] = "";
    for (size_t i = 0; i < len && i < 12; i++) {
        snprintf(ebuf + strlen(ebuf), sizeof ebuf - strlen(ebuf), "%s%02x", i ? " " : "", exp[i]);
        snprintf(gbuf + strlen(gbuf), sizeof gbuf - strlen(gbuf), "%s%02x", i ? " " : "", got[i]);
    }
    wl_log("gamemodes: byte mismatch at 0x%08lx (%s): expected [%s], got [%s]",
           (unsigned long)va, label, ebuf, gbuf);
    return 0;
}

static int verify_all_sites(void) {
    for (size_t i = 0; i < sizeof k_table_patches / sizeof k_table_patches[0]; i++) {
        const ModePatch *p = &k_table_patches[i];
        if (!check_bytes(p->va, p->orig, p->len, "table ref")) return 0;
    }
    static const BYTE b_init[]     = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_ctor[]     = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xda, 0xed, 0x93, 0x00 };
    static const BYTE b_validate[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_setmode[]  = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_callsite[] = { 0xe8, 0xe2, 0xb6, 0xdd, 0xff };
    static const BYTE b_unwrap[]   = { 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x5d, 0xc3 };
    static const BYTE b_additem[]  = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x58, 0x06, 0x91, 0x00 };

    if (!check_bytes(ADDR_GAMEMODE_INIT, b_init, sizeof b_init, "GameMode::init")) return 0;
    if (!check_bytes(ADDR_GAMEMODE_CTOR, b_ctor, sizeof b_ctor, "GameMode::ctor")) return 0;
    if (!check_bytes(ADDR_SETTINGS_VALIDATE, b_validate, sizeof b_validate, "GameSettings::validate")) return 0;
    if (!check_bytes(ADDR_SET_GAME_MODE, b_setmode, sizeof b_setmode, "GameSettings::set_game_mode")) return 0;
    if (!check_bytes(ADDR_DROPDOWN_CALLSITE, b_callsite, sizeof b_callsite, "dropdown call 0x426d40")) return 0;
    if (!check_bytes(ADDR_DROPDOWN_UNWRAP, b_unwrap, sizeof b_unwrap, "RefPtr::unwrap")) return 0;
    if (!check_bytes(ADDR_DROPDOWN_ADD_ITEM, b_additem, sizeof b_additem, "Dropdown::add_item")) return 0;
    return 1;
}

static int apply_table_patches(void) {
    HANDLE proc = GetCurrentProcess();
    for (size_t i = 0; i < sizeof k_table_patches / sizeof k_table_patches[0]; i++) {
        const ModePatch *p = &k_table_patches[i];
        BYTE *dst = (BYTE *)(p->va + p->patch_off);
        size_t wlen = (p->kind == PATCH_IMM8_CAP) ? 1 : 4;
        DWORD old = 0;
        if (!VirtualProtect(dst, wlen, PAGE_EXECUTE_READWRITE, &old)) {
            wl_log("gamemodes: VirtualProtect failed at 0x%08lx", (unsigned long)p->va);
            return 0;
        }
        if (p->kind == PATCH_IMM8_CAP) {
            *dst = (BYTE)WL_MODE_CAP;
        } else if (p->kind == PATCH_IMM32_CAP) {
            DWORD v = (DWORD)WL_MODE_CAP;
            memcpy(dst, &v, 4);
        } else {
            DWORD v = (DWORD)&g_mode_table[p->slot];
            memcpy(dst, &v, 4);
        }
        VirtualProtect(dst, wlen, old, &old);
        FlushInstructionCache(proc, dst, wlen);
    }
    return 1;
}

void wl_install_gamemodes(void) {
    if (g_modes_installed) return;
    scan_mod_game_modes();

    // Verify every single patched instruction and callee before writing a byte.
    if (!verify_all_sites()) {
        wl_log("gamemodes: verification failed, all mode patches left off");
        g_mode_count = 0;
        return;
    }

    if (!apply_table_patches()) {
        wl_log("gamemodes: table relocation failed");
        g_mode_count = 0;
        return;
    }

    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1 (5 bytes, no relocs)
    g_orig_gamemode_init = (GameModeInit_t)wl_hook(ADDR_GAMEMODE_INIT, hook_gamemode_init, 5);
    // 56 57 8b f9 be 01 00 00 00 = push esi; push edi; mov edi,ecx; mov esi,1 (9 bytes, no rel)
    g_orig_setup_start = (SetupStart_t)wl_hook(ADDR_SETUP_START, hook_setup_start, 9);
    {
        static const BYTE exp2[10] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xae, 0xeb, 0x94, 0x00 };
        if (memcmp((void *)0x657af0, exp2, sizeof exp2) == 0)
            g_orig_screen_changed = (ScreenChanged_t)wl_hook(0x657af0, hook_screen_changed, 10);
        else
            wl_log("gamemodes: 0x657af0 bytes differ, mode maps are not locked in the lobby");
    }
    {
        static const BYTE exp[10] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xe1, 0xbf, 0x96, 0x00 };
        if (memcmp((void *)0x748a40, exp, sizeof exp) == 0)
            g_orig_set_status = (SetStatus_t)wl_hook(0x748a40, hook_set_status, 10);
        else
            wl_log("gamemodes: set_game_status 0x748a40 bytes differ, enemy player not protected");
    }
    if (!g_orig_setup_start) wl_log("gamemodes: Start hook failed (one-team modes keep the lobby teams)");
    g_orig_settings_validate = (SettingsValidate_t)wl_hook(ADDR_SETTINGS_VALIDATE, hook_settings_validate, 5);
    g_orig_set_game_mode = (SetGameMode_t)wl_hook(ADDR_SET_GAME_MODE, hook_set_game_mode, 5);
    // e8 e2 b6 dd ff = call 0x426d40 (5 bytes)
    BOOL dd_ok = wl_patch_call(ADDR_DROPDOWN_CALLSITE, hook_append_dropdown_modes, 5);

    if (!g_orig_gamemode_init || !g_orig_settings_validate || !g_orig_set_game_mode || !dd_ok) {
        wl_log("gamemodes: one or more hooks failed (init=%p val=%p set=%p dd=%d)",
               (void *)g_orig_gamemode_init, (void *)g_orig_settings_validate,
               (void *)g_orig_set_game_mode, (int)dd_ok);
        return;
    }
    g_modes_installed = 1;
    wl_log("gamemodes: table relocated to %p (cap %d, %d custom mode(s))",
           (void *)g_mode_table, WL_MODE_CAP, g_mode_count);
}

const char *wl_gamemode_key_by_id(int mode_id) {
    if (!g_modes_installed) return NULL;
    int idx = mode_id - WL_MODE_VANILLA_COUNT;
    if (idx < 0 || idx >= g_mode_count) return NULL;
    if (!g_mode_table[mode_id]) return NULL;
    return g_modes[idx].key;
}

// Mode id (12..) of a custom mode key such as "last_stand", or -1.
int wl_gamemode_id_by_key(const char *key);

// ---- "teams": "all" ---------------------------------------------------------------------------
// GameSetupScreen::start_clicked 0x655180 (__thiscall(screen) -> BYTE, plain ret; the Start button
// and autostart.c call it). Before it validates and launches, a one-team mode puts every open
// player slot on team 1 (value 2): PlayerSlot = settings + 0xb0 + i*0x68, +0x00 team (0 = none),
// +0x54 status (2 human, 4 computer) (re/FINDINGS.md GameSettings fields). Single player only:
// in multiplayer the lobby syncs teams over the network, so a local change here would desync.
// ---- "map": the mode's own map as the only lobby map -------------------------------------------
// GameSetupScreen keeps the Custom map list (one entry per .rms of random-map-scripts, mod files
// included through files.c's listing hook) as a vector of entry pointers at screen+0x88c/+0x890.
// 0x658ab0 GameSetupScreen::set_map_style __thiscall(screen, int style), ret 4 (0x658ca3); style 2
// (Custom) takes the FIRST entry (0x658b0d mov edi,[edi]), sets map type 0x3b (0x658b16..0x658b1c)
// and copies the entry into the settings: entry+0x10 string -> settings+0x24, entry+0x24 string ->
// settings+0x38 (0x432970 = string copy, {char *data; int len; int cap}), then refreshes the screen
// (0x657af0). So: move our entry to the front, call it with 2, restore the order.
// 0x60e3a0 GameSettings::get_map_style __thiscall(settings) returns 2 while map type is 0x3b.
// UNVERIFIED in game until the log line below names the entries (2026-11-26).
typedef void (__thiscall *SetMapStyle_t)(void *screen, int style);
typedef int (__thiscall *GetMapStyle_t)(void *settings);
static int g_in_force_map;

static const char *gstr(const char *p) {      // game string {data, len, cap} at p
    const char *d = *(const char * const *)p;
    return (d && !IsBadReadPtr(d, 1)) ? d : "";
}
static int name_is(const char *have, const char *want) {   // "X" or "X.rms" (any folder)
    const char *b = strrchr(have, '\\'), *b2 = strrchr(have, '/');
    if (b2 > b) b = b2;
    if (b) have = b + 1;
    size_t n = strlen(want);
    return _strnicmp(have, want, n) == 0 && (have[n] == 0 || _stricmp(have + n, ".rms") == 0);
}

// 0x658a80 GameSetupScreen::set_map_size __thiscall(screen, int size), ret 4 (0x658a9f): writes
// settings+0x90 (mMapSize) through 0x655d70 and calls 0x657af0, like the Map Size dropdown.
typedef void (__thiscall *SetMapSize_t)(void *screen, int size);
static void force_mode_size(char *screen, char *settings, const WlCustomMode *cm) {
    if (cm->map_min_size < 0 || g_in_force_map) return;
    int size = *(int *)(settings + 0x90);
    if (size >= cm->map_min_size && size <= 6) return;
    g_in_force_map = 1;
    ((SetMapSize_t)0x658a80)(screen, cm->map_min_size);
    g_in_force_map = 0;
    wl_log("gamemodes: '%s': map size %d -> %d", cm->key, size, cm->map_min_size);
}

static int force_mode_map(char *screen, char *settings, const WlCustomMode *cm) {
    force_mode_size(screen, settings, cm);
    if (!cm->map_custom[0] || g_in_force_map) return 0;
    void **beg = *(void ***)(screen + 0x88c), **end = *(void ***)(screen + 0x890);
    if (!beg || end <= beg || end - beg > 4096) return 0;
    static int logged;
    if (!logged) {
        logged = 1;
        for (void **e = beg; e < end && e < beg + 40; e++)
            if (*e) wl_log("gamemodes: custom map %d: '%s' / '%s'", (int)(e - beg), gstr((char *)*e + 0x10), gstr((char *)*e + 0x24));
    }
    if (((GetMapStyle_t)0x60e3a0)(settings) == 2 &&
        (name_is(gstr(settings + 0x24), cm->map_custom) || name_is(gstr(settings + 0x38), cm->map_custom)))
        return 1;                                   // already ours
    void **ours = NULL;
    for (void **e = beg; e < end; e++) {
        if (*e && (name_is(gstr((char *)*e + 0x10), cm->map_custom) || name_is(gstr((char *)*e + 0x24), cm->map_custom))) { ours = e; break; }
    }
    if (!ours) {
        static int warned;
        if (!warned++) wl_log("gamemodes: '%s': custom map '%s' not in the lobby list", cm->key, cm->map_custom);
        return 0;
    }
    void *first = *beg;
    *beg = *ours; *ours = first;                     // ours to the front (pointer swap, no refcount change)
    g_in_force_map = 1;
    ((SetMapStyle_t)0x658ab0)(screen, 2);
    g_in_force_map = 0;
    *ours = *beg; *beg = first;                      // the list order back
    wl_log("gamemodes: '%s': map = Custom '%s'", cm->key, cm->map_custom);
    return 1;
}

static const WlCustomMode *mode_of_settings(char *settings) {
    int idx = settings ? *(int *)(settings + 0xac) - WL_MODE_VANILLA_COUNT : -1;
    return (g_modes_installed && idx >= 0 && idx < g_mode_count) ? &g_modes[idx] : NULL;
}

// 0x657af0 GameSetupScreen settings-changed handler __thiscall(screen), plain ret (0x657cda/0x657d97),
// called after every lobby change (35 call sites, also at the end of 0x658ab0). After it, a mode
// with its own map takes it back: picking another map snaps back, so the mode's map is the only one.
// Stolen 10 bytes: 55 8b ec / 6a ff / 68 ae eb 94 00.
static void __fastcall hook_screen_changed(void *screen, void *edx) {
    (void)edx;
    g_orig_screen_changed(screen);
    if (g_in_force_map) return;
    DWORD game = *(DWORD *)ADDR_GAME_PTR;
    void *comm = game ? *(void **)((char *)game + 0x98) : NULL;
    char *settings = comm ? *(char **)((char *)comm + 0xd54) : NULL;
    const WlCustomMode *cm = mode_of_settings(settings);
    // single player only: in a multiplayer lobby the clients must not change the host's map
    if (cm && cm->map_custom[0] && !*(int *)(settings + 0x408)) force_mode_map(screen, settings, cm);
}

// ---- "enemy_player": true --------------------------------------------------------------------
// The mode's enemy (Last Stand's Horde) is a real computer player on its own team, so every unit
// fights it on sight (Gaia units are not enemies: nobody attacked them by themselves). It is added
// at start after the last used slot: 0x6134b0 GameSettings::set_player_count __thiscall(settings, n),
// ret 4 (0x6135a0): +0x3f0 = n; slots < n whose status (+0x54) is not 2/0/4/6 (table 0x9c4420)
// become Computer with the default AI (0x615780), slots >= n are closed (0x615800). The new slot
// gets team value 3 (team 2) while everyone else has 2. Its player id is slot + 1 (single player
// slot order). The mod script removes its starting units; set_game_status below keeps it alive.
static int g_enemy_pid;     // player id of the enemy player of the running match (0 = none)

static void add_enemy_slot(char *settings, const char *key) {
    int last = -1;
    for (int i = 0; i < 8; i++) {
        int status = *(int *)(settings + 0xb0 + i * 0x68 + 0x54);
        if (status == 2 || status == 4) last = i;
    }
    if (last < 0 || last >= 7) {
        wl_log("gamemodes: '%s': no free player slot for the enemy player (Gaia is used)", key);
        return;
    }
    typedef void (__thiscall *SetCount_t)(void *settings, int n);
    ((SetCount_t)0x6134b0)(settings, last + 2);
    char *slot = settings + 0xb0 + (last + 1) * 0x68;
    if (*(int *)(slot + 0x54) != 4) {
        wl_log("gamemodes: '%s': enemy slot %d did not become a computer (status %d)", key, last + 1, *(int *)(slot + 0x54));
        return;
    }
    *(int *)slot = 3;                  // team 2: an enemy of team 1 (value 2)
    *(int *)(slot + 0x4c) = last + 1;  // colour (+0x4c, autostart.c)
    g_enemy_pid = last + 2;
    wl_log("gamemodes: '%s': enemy player %d added (slot %d)", key, g_enemy_pid, last + 1);
}

int wl_gamemode_enemy_player(void) { return g_enemy_pid; }

// TRIBE_Player::set_game_status 0x748a40 __thiscall(player, int status), ret 4 (0x748d11); status 2 =
// lost (0x750580 loss_if_game_on calls it through vtable[1] with 2). The enemy player owns nothing
// between rounds, so the game's own defeat check would remove it and the match would end in a win.
// Its defeat is ignored until the game is over (world+0x78 = 1, set by 0x72be70 on a win).
// Stolen 10 bytes: 55 8b ec / 6a ff / 68 e1 bf 96 00 (push ebp; mov ebp,esp; push -1; push imm32).
static void __fastcall hook_set_status(void *player, void *edx, int status) {
    (void)edx;
    if (status == 2 && g_enemy_pid && player && *(int *)((char *)player + 0x1adc) == g_enemy_pid) {
        DWORD game = *(DWORD *)0xaf2c58;
        char *w = game ? *(char **)((char *)game + 0xc8) : NULL;
        if (w && !*(int *)(w + 0x78)) return;
    }
    g_orig_set_status(player, status);
}

static char __fastcall hook_setup_start(void *screen, void *edx) {
    (void)edx;
    g_enemy_pid = 0;
    DWORD game = *(DWORD *)ADDR_GAME_PTR;
    void *comm = game ? *(void **)((char *)game + 0x98) : NULL;
    char *settings = comm ? *(char **)((char *)comm + 0xd54) : NULL;
    if (settings && g_modes_installed) {
        int mode_id = *(int *)(settings + 0xac);
        int idx = mode_id - WL_MODE_VANILLA_COUNT;
        int custom_ok = (idx >= 0 && idx < g_mode_count) ? (!*(int *)(settings + 0x408) && force_mode_map((char *)screen, settings, &g_modes[idx])) : 0;
        if (!custom_ok && idx >= 0 && idx < g_mode_count && g_modes[idx].map_type) {
            if (*(int *)(settings + 0x408)) {
                wl_log("gamemodes: '%s' wants map %d; multiplayer keeps the lobby map", g_modes[idx].key, g_modes[idx].map_type);
            } else {
                // Same calls as autostart.c: 0x613650 sets +0x98 mUserChosenMapType, 0x6135b0(-3) makes
                // the game resolve the map type at launch (both __thiscall(settings, int), ret 4).
                typedef void (__thiscall *SetMap_t)(void *settings, int val);
                ((SetMap_t)0x613650)(settings, g_modes[idx].map_type);
                ((SetMap_t)0x6135b0)(settings, -3);
                wl_log("gamemodes: '%s': map type %d (%s)", g_modes[idx].key, g_modes[idx].map_type, g_modes[idx].map_file);
            }
        }
        if (idx >= 0 && idx < g_mode_count && g_modes[idx].one_team) {
            if (*(int *)(settings + 0x408)) {
                wl_log("gamemodes: '%s' wants one team; multiplayer keeps the lobby teams", g_modes[idx].key);
            } else {
                int n = 0;
                for (int i = 0; i < 8; i++) {
                    char *slot = settings + 0xb0 + i * 0x68;
                    int status = *(int *)(slot + 0x54);
                    // Team field values: 1 = no team ("-"), 2..5 = teams 1..4 (as in recorded games);
                    // 1 left every player an enemy (seen in game 2026-11-26), so team 1 = value 2.
                    if (status == 2 || status == 4) { *(int *)slot = 2; n++; }
                }
                // Lock Teams: GameSettings serializer field "&mTeamsLocked" = settings + 0x405
                // (0x611bfa add $0x405,%eax; push $0x9ccc4c). Unlocked, computer players switched
                // to Enemy during the game (seen 2026-11-26).
                *(BYTE *)(settings + 0x405) = 1;
                wl_log("gamemodes: '%s': %d player(s) put on one team, teams locked", g_modes[idx].key, n);
                if (g_modes[idx].enemy_player) add_enemy_slot(settings, g_modes[idx].key);
            }
        }
    }
    return g_orig_setup_start(screen);
}

int wl_gamemode_id_by_key(const char *key) {
    if (!g_modes_installed || !key) return -1;
    for (int i = 0; i < g_mode_count; i++)
        if (_stricmp(g_modes[i].key, key) == 0 && g_mode_table[WL_MODE_VANILLA_COUNT + i])
            return WL_MODE_VANILLA_COUNT + i;
    return -1;
}

// files.c: the current mode's own map script for a game script file name ("Arena.rms"), or 0.
int wl_gamemode_map_script(const char *file_name, char *out, size_t out_size) {
    if (!g_modes_installed || !file_name) return 0;
    DWORD game = *(DWORD *)ADDR_GAME_PTR;
    void *comm = game ? *(void **)((char *)game + 0x98) : NULL;
    char *settings = comm ? *(char **)((char *)comm + 0xd54) : NULL;
    if (!settings) return 0;
    int idx = *(int *)(settings + 0xac) - WL_MODE_VANILLA_COUNT;
    if (idx < 0 || idx >= g_mode_count || !g_modes[idx].map_script[0]) return 0;
    if (_stricmp(file_name, g_modes[idx].map_file) != 0) return 0;
    snprintf(out, out_size, "%s", g_modes[idx].map_script);
    return 1;
}

const char *wl_gamemode_current_key(void) {
    if (!g_modes_installed) return NULL;
    DWORD game = *(DWORD *)ADDR_GAME_PTR;
    if (!game) return NULL;
    void *comm = *(void **)((char *)game + 0x98);
    if (!comm) return NULL;
    void *settings = *(void **)((char *)comm + 0xd54);
    if (!settings) return NULL;
    int mode_id = *(int *)((char *)settings + 0xac);
    return wl_gamemode_key_by_id(mode_id);
}
