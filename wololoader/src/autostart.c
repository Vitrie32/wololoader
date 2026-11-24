// Autostart single-player match from <game>\wololoader\autostart.json ("Test it").
//
// How it works (all addresses verified against AoK HD.exe 5.8 disassembly,
// see re/FINDINGS.md "Autostart / Single-player quick launch"):
//
// 1. MainMenuScreen vtable (0x9d66a8) slot 17 (+0x44, at 0x9d66ec) holds
//    MainMenuScreen::tick (0x6e8bb0, __thiscall(screen), plain ret via tail jmp
//    0x490890). We replace this vtable slot (after verifying it equals 0x6e8bb0)
//    so every main-menu frame can cheaply poll for autostart.json (~500 ms).
//    This catches both a freshly started game reaching the main menu AND an
//    already-running game returning to the main menu.
//
// 2. When autostart.json is found while MainMenuScreen is the active top screen
//    (ScreenManager::get_current 0x43c0d0 == self), no GameScreen is active
//    (*(void**)0xaf3d90 == NULL), and CommHandler is not in an MP lobby
//    ([comm+0xd0] != 5):
//    - Read and immediately delete autostart.json (never loop on failure).
//    - Parse the JSON config (civ / mod+civ_file+civ_name, map, size, ai,
//      ai_civ, difficulty, resources, reveal_map, cheats, auto_launch).
//    - Drive the game's exact Single Player -> Standard Game path (0x6e36b0):
//        a) Fetch the profile's SP GameSettings* via [0xaf2b24] -> 0x61f3c0.
//        b) Reset to clean single-player Random Map defaults:
//             0x610c70 (reset_to_defaults), 0x6133c0(0) (set_game_mode = RM,
//             clears scenario ptr +0x430), 0x613810 (init_sp_setup).
//        c) Enter SP setup via CommHandler::enter_setup 0x57ec60(comm, &settings, &null, 0).
//        d) Apply all autostart.json fields to GameSettings:
//             0x6134b0(1 + ai) player count & AI slots,
//             0x607490(slot+0x58, civ) CivChoice::setFromInt,
//             0x613650(map) / 0x6135b0(-3) map type,
//             +0x90 map size, +0x8c refcounted Difficulty* from [0xaf28d0],
//             +0xa0 resources, +0x9c visibility, +0x40f cheats, +0x408 = 0 (SP).
//        e) Construct & activate GameSetupScreen:
//             operator new 0x8d47df(0xbe0), memset, 0x647430(scr, comm),
//             ScreenManager::set_current 0x48a8e0(mgr, scr).
//        f) Trigger the Start Game button handler 0x655180(scr): validates
//           settings via 0x6554c0, plays the civ jingle, and arms scr+0x8a4 so
//           GameSetupScreen::action (0x656c60 -> 0x6600d0 -> 0x6571f0) launches
//           the match cleanly on the next top-level frame. If validation fails
//           (e.g. Migration + AI), the user lands on the pre-filled setup screen.
//
// Never installed in Vanilla Mode. Never starts anything in multiplayer.
#include "wololoader.h"
#include "json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Verified AoK HD.exe 5.8 addresses (see re/FINDINGS.md)
#define ADDR_MAINMENU_VTBL        0x9d66a8  // MainMenuScreen vtable (slot 17 = tick)
#define ADDR_MAINMENU_TICK        0x6e8bb0  // __thiscall(screen), plain ret (tail jmp 0x490890)
#define ADDR_SCREEN_MGR           0xada4b4  // ScreenManager* global
#define ADDR_SCREEN_GET_CURRENT   0x43c0d0  // __thiscall(mgr) -> void*, plain ret
#define ADDR_SCREEN_SET_CURRENT   0x48a8e0  // __thiscall(mgr, screen), ret 4
#define ADDR_GAME_SCREEN_GLOBAL   0xaf3d90  // GameScreen* (non-NULL while in a match)
#define ADDR_COMM_HANDLER         0xaeefb8  // CommHandler* global
#define ADDR_PROFILE_MGR          0xaf2b24  // player profile manager used by 0x6e36d9
#define ADDR_GET_SP_SETTINGS      0x61f3c0  // __thiscall(profile) -> GameSettings*, plain ret
#define ADDR_SETTINGS_RESET       0x610c70  // __thiscall(settings), plain ret
#define ADDR_SETTINGS_SET_MODE    0x6133c0  // __thiscall(settings, int mode), ret 4
#define ADDR_SETTINGS_SET_PLAYERS 0x6134b0  // __thiscall(settings, int count), ret 4
#define ADDR_SETTINGS_SET_RMAP    0x6135b0  // __thiscall(settings, int map_id), ret 4
#define ADDR_SETTINGS_SET_CMAP    0x613650  // __thiscall(settings, int map_id), ret 4
#define ADDR_SETTINGS_INIT_SP     0x613810  // __thiscall(settings), tail jmp vtbl[3]
#define ADDR_DIFFICULTY_TABLE     0xaf28d0  // Difficulty** array [0..4]: Hardest..Easiest
#define ADDR_CIV_SET_FROM_INT     0x607490  // __thiscall(CivChoice*, int civ_id), ret 4
#define ADDR_COMM_ENTER_SETUP     0x57ec60  // __thiscall(comm, void **settings, void **lobby, char flag), ret 0xc
#define ADDR_GAME_NEW             0x8d47df  // __cdecl(size_t) -> void*
#define ADDR_GAMESETUP_CTOR       0x647430  // __thiscall(screen, comm) -> void*, ret 4
#define ADDR_GAMESETUP_START      0x655180  // __thiscall(screen) -> BYTE (1=armed, 0=invalid), plain ret

#define GAMESETUP_SIZE            0xbe0
#define POLL_INTERVAL_MS          500
#define FIRST_DELAY_MS            400       // wait briefly after MainMenuScreen appears

#define CIV_RANDOM                0x20      // 32
#define CIV_MIRROR                0x22      // 34

typedef void (__thiscall *MenuTick_t)(void *self);
typedef void *(__thiscall *GetCurrentScreen_t)(void *mgr);
typedef void (__thiscall *SetCurrentScreen_t)(void *mgr, void *scr);
typedef void *(__thiscall *GetSpSettings_t)(void *profile);
typedef void (__thiscall *SettingsVoid_t)(void *settings);
typedef void (__thiscall *SettingsInt_t)(void *settings, int val);
typedef void (__thiscall *CivSetFromInt_t)(void *civ_choice, int civ_id);
typedef void (__thiscall *CommEnterSetup_t)(void *comm, void **settings, void **lobby, char flag);
typedef void *(__cdecl *GameNew_t)(size_t size);
typedef void *(__thiscall *GameSetupCtor_t)(void *screen, void *comm);
typedef BYTE (__thiscall *GameSetupStart_t)(void *screen);
typedef void (__thiscall *RefRelease_t)(void *obj);

static MenuTick_t g_orig_menu_tick;
static DWORD g_last_poll_ms;
static DWORD g_menu_first_seen_ms;
static int g_running;

typedef struct {
    int civ;           // 1..31, 32=random, 34=mirror, 35+ custom
    int map_type;      // MAPTYPE_* id (9 = Arabia)
    int map_size;      // 0=Tiny .. 6=LudiKRIS
    int ai_count;      // 0..7
    int ai_civ;        // 1..31, 32=random, 34=mirror, 35+ custom
    int difficulty;    // 0=Hardest, 1=Hard, 2=Moderate, 3=Standard, 4=Easiest
    int resources;     // 0=Standard, 1=Low, 2=Medium, 3=High
    int visibility;    // 0=Normal, 1=Explored, 2=All Visible
    int cheats;        // 0 or 1
    int auto_launch;   // 1 = click Start automatically, 0 = stop on GameSetupScreen
} AutostartCfg;

// Case-insensitive match ignoring spaces, underscores, and hyphens.
static int eq_norm(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a || *b) {
        while (*a == ' ' || *a == '_' || *a == '-') a++;
        while (*b == ' ' || *b == '_' || *b == '-') b++;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        if (!*a) return 1;
        a++;
        b++;
    }
    return 1;
}

static int is_valid_civ_id(int id) {
    return (id >= 1 && id <= 31) || id == CIV_RANDOM || id == CIV_MIRROR || (id >= 35 && id < 200);
}

// Vanilla civ names (1..31) for name-based lookup.
static const char *const k_vanilla_civs[32] = {
    NULL,
    "Britons", "Franks", "Goths", "Teutons", "Japanese", "Chinese",
    "Byzantines", "Persians", "Saracens", "Turks", "Vikings", "Mongols",
    "Celts", "Spanish", "Aztecs", "Mayans", "Huns", "Koreans",
    "Italians", "Indians", "Incas", "Magyars", "Slavs", "Portuguese",
    "Ethiopians", "Malians", "Berbers", "Khmer", "Malay", "Burmese",
    "Vietnamese"
};

// Look up a custom civ id from the freshly rebuilt <game>\wololoader\cache\civs.txt.
// Format: <id>\t<base>\t<name>\t<mod>\t<json>[\t<t1>\t<t2>]
static int lookup_civs_txt(const char *want_mod, const char *want_file, const char *want_name) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\cache\\civs.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    int by_mod_file = -1, by_name = -1;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *f_id = strtok(line, "\t");
        char *f_base = strtok(NULL, "\t");
        char *f_name = strtok(NULL, "\t");
        char *f_mod = strtok(NULL, "\t");
        char *f_json = strtok(NULL, "\t");
        if (!f_id || !f_base || !f_name) continue;
        int id = atoi(f_id);
        if (id < 35) continue;
        if (want_mod && want_mod[0] && want_file && want_file[0] && f_mod && f_json &&
            _stricmp(f_mod, want_mod) == 0 && _stricmp(f_json, want_file) == 0) {
            by_mod_file = id;
            break;
        }
        if (want_name && want_name[0] && by_name < 0 && _stricmp(f_name, want_name) == 0) {
            by_name = id;
        }
    }
    fclose(f);
    return (by_mod_file >= 35) ? by_mod_file : by_name;
}

static int resolve_civ_name(const char *name) {
    if (!name || !name[0]) return -1;
    if (eq_norm(name, "random")) return CIV_RANDOM;
    if (eq_norm(name, "mirror") || eq_norm(name, "mirrorrandom")) return CIV_MIRROR;
    for (int i = 1; i <= 31; i++) {
        if (eq_norm(name, k_vanilla_civs[i])) return i;
    }
    int custom = lookup_civs_txt(NULL, NULL, name);
    if (custom >= 35) return custom;
    char *end = NULL;
    long n = strtol(name, &end, 10);
    if (end && *end == '\0' && is_valid_civ_id((int)n)) return (int)n;
    return -1;
}

// Map type IDs verified from MAPTYPE_* static init at 0x418de0..0x41a600.
static int parse_map_type(const WlJsonVal *v) {
    if (!v) return 9; // Arabia
    if (v->type == WL_JSON_NUMBER) {
        int id = (int)v->u.num.ival;
        return (id >= 9 && id <= 99) ? id : 9;
    }
    if (v->type != WL_JSON_STRING) return 9;
    static const struct { const char *name; int id; } k_maps[] = {
        { "arabia", 9 }, { "archipelago", 10 }, { "baltic", 11 },
        { "blackforest", 12 }, { "bf", 12 }, { "coastal", 13 },
        { "continental", 14 }, { "craterlake", 15 }, { "fortress", 16 },
        { "goldrush", 17 }, { "highland", 18 }, { "islands", 19 },
        { "mediterranean", 20 }, { "migration", 21 }, { "rivers", 22 },
        { "teamislands", 23 }, { "scandinavia", 25 }, { "mongolia", 26 },
        { "yucatan", 27 }, { "saltmarsh", 28 }, { "arena", 29 },
        { "oasis", 31 }, { "ghostlake", 32 }, { "nomad", 33 },
    };
    for (size_t i = 0; i < sizeof(k_maps) / sizeof(k_maps[0]); i++) {
        if (eq_norm(v->u.str, k_maps[i].name)) return k_maps[i].id;
    }
    char *end = NULL;
    long n = strtol(v->u.str, &end, 10);
    if (end && *end == '\0' && n >= 9 && n <= 99) return (int)n;
    return 9;
}

// Map size IDs verified from 0x7d4540 (string id = 0x2973 + size).
static int parse_map_size(const WlJsonVal *v) {
    if (!v) return 0; // Tiny
    if (v->type == WL_JSON_NUMBER) {
        int s = (int)v->u.num.ival;
        return (s >= 0 && s <= 6) ? s : 0;
    }
    if (v->type != WL_JSON_STRING) return 0;
    if (eq_norm(v->u.str, "tiny"))     return 0;
    if (eq_norm(v->u.str, "small"))    return 1;
    if (eq_norm(v->u.str, "medium"))   return 2;
    if (eq_norm(v->u.str, "normal"))   return 3;
    if (eq_norm(v->u.str, "large"))    return 4;
    if (eq_norm(v->u.str, "giant"))    return 5;
    if (eq_norm(v->u.str, "ludikris")) return 6;
    return 0;
}

// Difficulty indices into DAT_00af28d0[0..4], verified from 0x608270:
// 0=Hardest (0x2bd0), 1=Hard (0x2bd1), 2=Moderate (0x2bd2),
// 3=Standard (0x2bd3), 4=Easiest (0x2bd4).
static int parse_difficulty(const WlJsonVal *v) {
    if (!v) return 4; // Easiest
    if (v->type == WL_JSON_NUMBER) {
        int d = (int)v->u.num.ival;
        return (d >= 0 && d <= 4) ? d : 4;
    }
    if (v->type != WL_JSON_STRING) return 4;
    if (eq_norm(v->u.str, "hardest"))  return 0;
    if (eq_norm(v->u.str, "hard"))     return 1;
    if (eq_norm(v->u.str, "moderate")) return 2;
    if (eq_norm(v->u.str, "standard")) return 3;
    if (eq_norm(v->u.str, "easiest"))  return 4;
    return 4;
}

// Resource level IDs verified from 0x64cf40..0x64d020:
// 0=Standard, 1=Low, 2=Medium, 3=High.
static int parse_resources(const WlJsonVal *v) {
    if (!v) return 0; // Standard
    if (v->type == WL_JSON_NUMBER) {
        int r = (int)v->u.num.ival;
        return (r >= 0 && r <= 3) ? r : 0;
    }
    if (v->type != WL_JSON_STRING) return 0;
    if (eq_norm(v->u.str, "standard")) return 0;
    if (eq_norm(v->u.str, "low"))      return 1;
    if (eq_norm(v->u.str, "medium"))   return 2;
    if (eq_norm(v->u.str, "high"))     return 3;
    return 0;
}

// Visibility / reveal map IDs verified from 0x64d890..0x64d930:
// 0=Normal, 1=Explored, 2=All Visible.
static int parse_visibility(const WlJsonVal *v) {
    if (!v) return 0; // Normal
    if (v->type == WL_JSON_BOOL) return v->u.bool_val ? 2 : 0;
    if (v->type == WL_JSON_NUMBER) {
        int r = (int)v->u.num.ival;
        return (r >= 0 && r <= 2) ? r : 0;
    }
    if (v->type != WL_JSON_STRING) return 0;
    if (eq_norm(v->u.str, "normal") || eq_norm(v->u.str, "false")) return 0;
    if (eq_norm(v->u.str, "explored")) return 1;
    if (eq_norm(v->u.str, "allvisible") || eq_norm(v->u.str, "all") || eq_norm(v->u.str, "true")) return 2;
    return 0;
}

static int parse_autostart_json(const char *text, AutostartCfg *cfg) {
    char err[256] = "";
    WlJsonVal *root = wl_json_parse(text, err, sizeof err);
    if (!root || root->type != WL_JSON_OBJECT) {
        wl_log("autostart: JSON parse failed: %s", err[0] ? err : "not an object");
        wl_json_free(root);
        return 0;
    }

    // Defaults: Briton (1) if unspecified, Arabia, Tiny, 1 easiest AI, random AI civ, standard res, normal visibility, cheats on.
    cfg->civ         = 1;
    cfg->map_type    = parse_map_type(wl_json_get(root, "map"));
    cfg->map_size    = parse_map_size(wl_json_get(root, "size"));
    cfg->ai_count    = 1;
    cfg->ai_civ      = CIV_RANDOM;
    cfg->difficulty  = parse_difficulty(wl_json_get(root, "difficulty"));
    cfg->resources   = parse_resources(wl_json_get(root, "resources"));
    cfg->visibility  = parse_visibility(wl_json_get(root, "reveal_map"));
    cfg->cheats      = 1;
    cfg->auto_launch = 1;

    const WlJsonVal *v_ai = wl_json_get(root, "ai");
    if (v_ai && v_ai->type == WL_JSON_NUMBER) {
        int n = (int)v_ai->u.num.ival;
        if (n < 0) n = 0;
        if (n > 7) n = 7;
        cfg->ai_count = n;
    }

    const WlJsonVal *v_cheats = wl_json_get(root, "cheats");
    if (v_cheats && v_cheats->type == WL_JSON_BOOL) cfg->cheats = v_cheats->u.bool_val ? 1 : 0;

    const WlJsonVal *v_launch = wl_json_get(root, "auto_launch");
    if (v_launch && v_launch->type == WL_JSON_BOOL) cfg->auto_launch = v_launch->u.bool_val ? 1 : 0;

    // Resolve player civ:
    // 1) If mod + civ_file is given, prefer freshly built cache\civs.txt so ID shifts
    //    after a startup rebuild always land on the exact custom civ.
    // 2) Otherwise check numeric "civ", then string "civ" / "civ_name".
    const char *mod_name = wl_json_str(wl_json_get(root, "mod"), NULL);
    const char *civ_file = wl_json_str(wl_json_get(root, "civ_file"), NULL);
    const char *civ_name = wl_json_str(wl_json_get(root, "civ_name"), NULL);
    const WlJsonVal *v_civ = wl_json_get(root, "civ");

    int resolved = -1;
    if (mod_name && civ_file) {
        resolved = lookup_civs_txt(mod_name, civ_file, civ_name);
    }
    if (resolved < 0 && v_civ) {
        if (v_civ->type == WL_JSON_NUMBER && is_valid_civ_id((int)v_civ->u.num.ival)) {
            resolved = (int)v_civ->u.num.ival;
        } else if (v_civ->type == WL_JSON_STRING) {
            resolved = resolve_civ_name(v_civ->u.str);
        }
    }
    if (resolved < 0 && civ_name) {
        resolved = resolve_civ_name(civ_name);
    }
    if (is_valid_civ_id(resolved)) {
        cfg->civ = resolved;
    } else {
        wl_log("autostart: could not resolve civ (mod='%s', file='%s', name='%s'), defaulting to 1 (Britons)",
               mod_name ? mod_name : "", civ_file ? civ_file : "", civ_name ? civ_name : "");
        cfg->civ = 1;
    }

    // Resolve AI civ
    const WlJsonVal *v_aiciv = wl_json_get(root, "ai_civ");
    if (v_aiciv) {
        int ac = -1;
        if (v_aiciv->type == WL_JSON_NUMBER && is_valid_civ_id((int)v_aiciv->u.num.ival)) {
            ac = (int)v_aiciv->u.num.ival;
        } else if (v_aiciv->type == WL_JSON_STRING) {
            ac = resolve_civ_name(v_aiciv->u.str);
        }
        if (is_valid_civ_id(ac)) cfg->ai_civ = ac;
    }

    wl_json_free(root);
    return 1;
}

static void release_refcounted(void *obj) {
    if (!obj) return;
    int *rc = (int *)((char *)obj + 4);
    if (--(*rc) == 0) {
        void **vtbl = *(void ***)obj;
        if (vtbl && vtbl[0]) ((RefRelease_t)vtbl[0])(obj);
    }
}

// Mirrors 0x6e36d9..0x6e36ee to obtain the active player profile's SP GameSettings*.
static void *get_sp_settings(void) {
    void *p1 = *(void **)ADDR_PROFILE_MGR;
    if (!p1) return NULL;
    void *p2 = *(void **)p1;
    if (!p2) return NULL;
    int idx = *(int *)((char *)p2 + 0x10);
    void **arr = *(void ***)((char *)p2 + 0x14);
    if (!arr || idx < 0 || idx > 1024) return NULL;
    void *profile = arr[idx];
    if (!profile) return NULL;
    return ((GetSpSettings_t)ADDR_GET_SP_SETTINGS)(profile);
}

static void launch_sp_game(const AutostartCfg *cfg) {
    void *comm = *(void **)ADDR_COMM_HANDLER;
    void *mgr  = *(void **)ADDR_SCREEN_MGR;
    if (!comm || !mgr) {
        wl_log("autostart: comm=%p mgr=%p missing, aborting", comm, mgr);
        return;
    }
    // Never run in multiplayer.
    if (*(int *)((char *)comm + 0xd0) == 5) {
        wl_log("autostart: multiplayer lobby active (comm+0xd0==5), refusing to start");
        return;
    }

    void *settings = get_sp_settings();
    if (!settings) {
        wl_log("autostart: get_sp_settings() returned NULL, aborting");
        return;
    }
    // Hold our own reference across setup (same as 0x6e36f5).
    *(int *)((char *)settings + 4) += 1;

    wl_log("autostart: configuring SP GameSettings %p (civ=%d map=%d size=%d ai=%d ai_civ=%d diff=%d res=%d vis=%d cheats=%d)",
           settings, cfg->civ, cfg->map_type, cfg->map_size, cfg->ai_count,
           cfg->ai_civ, cfg->difficulty, cfg->resources, cfg->visibility, cfg->cheats);

    // 1. Guarantee single-player mode and reset to clean Random Map defaults.
    *(int *)((char *)settings + 0x408) = 0;                 // isMultiPlayer = 0
    ((SettingsVoid_t)ADDR_SETTINGS_RESET)(settings);        // 0x610c70: defaults
    ((SettingsInt_t)ADDR_SETTINGS_SET_MODE)(settings, 0);   // 0x6133c0: Random Map + clear scenario ptr
    ((SettingsVoid_t)ADDR_SETTINGS_INIT_SP)(settings);      // 0x613810: exact call from 0x6e3702
    *(int *)((char *)settings + 0x408) = 0;                 // re-assert SP

    // 2. Enter single-player setup on CommHandler (exact call from 0x6e3715..0x6e3722).
    //    Sets comm+0xd0 = 2, comm+0xd54 = settings, and binds slot 0 to the local human player.
    void *null_lobby = NULL;
    ((CommEnterSetup_t)ADDR_COMM_ENTER_SETUP)(comm, &settings, &null_lobby, 0);

    // 3. Apply player count and AI slots (0x6134b0 populates slots 1..ai as Computer
    //    with the default AI script and closes remaining slots).
    int total_players = 1 + cfg->ai_count;
    if (total_players < 1) total_players = 1;
    if (total_players > 8) total_players = 8;
    ((SettingsInt_t)ADDR_SETTINGS_SET_PLAYERS)(settings, total_players);

    // 4. Configure local player (slot 0) and AI slots (1..ai_count):
    //    PlayerSlot stride is 0x68 starting at settings + 0xb0:
    //      +0x00 team (0 = none), +0x4c color (0..7), +0x58 CivChoice.
    char *slot0 = (char *)settings + 0xb0;
    *(int *)(slot0 + 0x00) = 0;
    *(int *)(slot0 + 0x4c) = 0;
    ((CivSetFromInt_t)ADDR_CIV_SET_FROM_INT)(slot0 + 0x58, cfg->civ);

    for (int i = 1; i < total_players; i++) {
        char *sl = (char *)settings + 0xb0 + i * 0x68;
        *(int *)(sl + 0x00) = 0;
        *(int *)(sl + 0x4c) = i;
        ((CivSetFromInt_t)ADDR_CIV_SET_FROM_INT)(sl + 0x58, cfg->ai_civ);
    }

    // 5. Apply map, size, difficulty, resources, visibility, and cheats.
    ((SettingsInt_t)ADDR_SETTINGS_SET_CMAP)(settings, cfg->map_type); // +0x98 mUserChosenMapType
    ((SettingsInt_t)ADDR_SETTINGS_SET_RMAP)(settings, -3);            // +0x94 mResolvedMapType (-3 = resolve at launch)
    *(int *)((char *)settings + 0x90) = cfg->map_size;                // mMapSize
    *(int *)((char *)settings + 0xa0) = cfg->resources;               // mResourceLevel
    *(int *)((char *)settings + 0x9c) = cfg->visibility;              // mVisibility
    *(BYTE *)((char *)settings + 0x40f) = (BYTE)(cfg->cheats ? 1 : 0);// mCheatsEnabled
    *(int *)((char *)settings + 0x408) = 0;                           // isMultiPlayer = 0

    // Difficulty (+0x8c) is a ref-counted Difficulty* from DAT_00af28d0[0..4] (verified in 0x6584f0).
    void **diff_table = *(void ***)ADDR_DIFFICULTY_TABLE;
    if (diff_table && cfg->difficulty >= 0 && cfg->difficulty < 5) {
        void *new_diff = diff_table[cfg->difficulty];
        if (new_diff) {
            *(int *)((char *)new_diff + 4) += 1;
            void *old_diff = *(void **)((char *)settings + 0x8c);
            release_refcounted(old_diff);
            *(void **)((char *)settings + 0x8c) = new_diff;
        }
    }

    // 6. Create and show GameSetupScreen (exact sequence from 0x6e373c..0x6e3782).
    void *scr = ((GameNew_t)ADDR_GAME_NEW)(GAMESETUP_SIZE);
    if (!scr) {
        wl_log("autostart: operator new(0x%x) failed", GAMESETUP_SIZE);
        release_refcounted(settings);
        return;
    }
    memset(scr, 0, GAMESETUP_SIZE);
    scr = ((GameSetupCtor_t)ADDR_GAMESETUP_CTOR)(scr, comm);
    if (!scr) {
        wl_log("autostart: GameSetupScreen ctor returned NULL");
        release_refcounted(settings);
        return;
    }
    ((SetCurrentScreen_t)ADDR_SCREEN_SET_CURRENT)(mgr, scr);
    release_refcounted(settings);
    wl_log("autostart: GameSetupScreen %p active", scr);

    // 7. Arm single-player game launch via the Start Game button handler (0x655180).
    //    This runs full validation (0x6554c0) and sets scr+0x8a4 so GameSetupScreen::action
    //    (0x656c60 -> 0x6600d0 -> 0x6571f0) launches the game on the next frame cleanly.
    if (cfg->auto_launch) {
        BYTE ok = ((GameSetupStart_t)ADDR_GAMESETUP_START)(scr);
        wl_log("autostart: GameSetupScreen::start_clicked(0x655180) returned %u (%s)",
               (unsigned)ok, ok ? "match launch armed" : "validation rejected; staying on setup screen");
    } else {
        wl_log("autostart: auto_launch=false, staying on pre-filled GameSetupScreen");
    }
}

static void check_autostart_file(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\autostart.json", g_wl.root);
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return;

    wl_log("autostart: found %s", path);
    // A request is only valid for a few minutes. When the game could not be
    // started (e.g. Steam refused the launch) the file stayed behind and the
    // next normal start jumped straight into a match (seen 2026-11-24).
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) {
        FILETIME now_ft; GetSystemTimeAsFileTime(&now_ft);
        ULONGLONG now = ((ULONGLONG)now_ft.dwHighDateTime << 32) | now_ft.dwLowDateTime;
        ULONGLONG wr  = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        ULONGLONG age_s = now > wr ? (now - wr) / 10000000ULL : 0;
        if (age_s > 300) {
            wl_log("autostart: request is %llu s old (limit 300 s), ignored and deleted", age_s);
            DeleteFileA(path);
            return;
        }
    }
    char *text = wl_read_file(path, NULL);
    // Delete immediately before parsing or launching so an error never causes a restart loop.
    if (!DeleteFileA(path)) {
        wl_log("autostart: warning: could not delete %s (err %lu)", path, GetLastError());
    }
    if (!text) {
        wl_log("autostart: could not read %s", path);
        return;
    }

    AutostartCfg cfg;
    int parsed = parse_autostart_json(text, &cfg);
    free(text);
    if (!parsed) return;

    launch_sp_game(&cfg);
}

static void __fastcall hook_mainmenu_tick(void *self, void *edx) {
    (void)edx;
    g_orig_menu_tick(self);

    if (g_running) return;
    // Never run while an in-game GameScreen exists.
    if (*(void **)ADDR_GAME_SCREEN_GLOBAL != NULL) return;
    // Only act while MainMenuScreen itself is the active top screen.
    void *mgr = *(void **)ADDR_SCREEN_MGR;
    if (!mgr || ((GetCurrentScreen_t)ADDR_SCREEN_GET_CURRENT)(mgr) != self) {
        g_menu_first_seen_ms = 0;
        return;
    }

    DWORD now = GetTickCount();
    if (g_menu_first_seen_ms == 0) {
        g_menu_first_seen_ms = now;
        return;
    }
    if ((DWORD)(now - g_menu_first_seen_ms) < FIRST_DELAY_MS) return;
    if (g_last_poll_ms != 0 && (DWORD)(now - g_last_poll_ms) < POLL_INTERVAL_MS) return;
    g_last_poll_ms = now;

    g_running = 1;
    check_autostart_file();
    g_running = 0;
}

void wl_install_autostart(void) {
    // Verify MainMenuScreen vtable slot 17 (+0x44) points to 0x6e8bb0
    // and verify the entry bytes of unhooked game functions we call.
    // (0x647430 GameSetupScreen::ctor is already detoured by lobby.c earlier in DllMain.)
    void **slot = (void **)ADDR_MAINMENU_VTBL + 17;
    static const BYTE b_start[]   = { 0x56, 0x57, 0x8b, 0xf9 };
    static const BYTE b_enter[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_players[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };

    if ((DWORD)*slot != ADDR_MAINMENU_TICK ||
        memcmp((const void *)ADDR_GAMESETUP_START, b_start, sizeof b_start) != 0 ||
        memcmp((const void *)ADDR_COMM_ENTER_SETUP, b_enter, sizeof b_enter) != 0 ||
        memcmp((const void *)ADDR_SETTINGS_SET_PLAYERS, b_players, sizeof b_players) != 0) {
        wl_log("autostart: unexpected exe bytes (slot17=%p), disabled", *slot);
        return;
    }

    DWORD old;
    g_orig_menu_tick = (MenuTick_t)*slot;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = (void *)hook_mainmenu_tick;
    VirtualProtect(slot, 4, old, &old);
    wl_log("autostart: installed (MainMenuScreen::tick vtable slot 17)");
}
