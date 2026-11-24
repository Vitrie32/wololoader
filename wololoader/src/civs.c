// Custom civilizations.
// tools/build.py writes wololoader\cache\empires2_x2_p1.dat (with civs 35+)
// and cache\civs.txt. Here we register those civs with the exe, give them
// names, and serve their strings. Addresses: AoK HD.exe 5.8 (re/FINDINGS.md).
#include "wololoader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

#define ADDR_REGISTER_RAJAS  0x5c9c60  // cdecl(pkg): registers civs 28..31, runs last
#define ADDR_REGISTER_CIV    0x5c9640  // cdecl(pkg, id, mp3, nameStr, bonusBase, t1, t2, playable)
#define ADDR_CIV_MAP         0xaf25ec  // map<int, Civilization*>
#define ADDR_CIV_MAP_FIND    0x59c990  // thiscall(map, int *id) -> Civilization* or 0
#define ADDR_CIV_NAME_ID     0x62f080  // (civ id) -> name string id, ret 4
#define ADDR_STRING_LOOKUP   0x448370  // thiscall(table, const char *key) -> const char*, ret 4
#define ADDR_PKG_CIV_REMOVE  0x5ca1a0  // thiscall(pkg+0x98, DlcPackageCivilization*), ret 4
#define ADDR_CIV_PKG_REMOVE  0x5ca140  // thiscall(civ+0x24, DlcPackageCivilization*), ret 4
#define ADDR_UT_VEC          0xaf25e0  // std::vector<int> {begin, end, cap} of unique tech ids
#define ADDR_UT_VEC_INSERT   0x479f30  // thiscall(vec, int *pos, const int *val), ret 8
#define ADDR_CIVCHOICE_SET   0x607490  // thiscall(CivChoice*, int civ_id), ret 4
#define ADDR_PROFILE_MGR     0xaf2b24  // ProfileManager** (pm = **(void***)0xaf2b24)
#define ADDR_GAME_PTR        0xaf2c58  // Game* global

#define FIRST_CUSTOM_CIV     35        // 32..34 are RANDOM/SPECTATOR/MIRROR in the exe
// Our string ids: STR_BASE + civ*100 + slot. Slot 0 = civ name, slot 10 = AI
// name count (decimal string, like Britons' 4400 = "16"), slots 11.. = AI names.
#define STR_BASE             9000000
#define STR_NAME             0
#define STR_AI_COUNT         10
#define MAX_AI_NAMES         40
// Civ description (selection rollover / tech tree): DlcPackageCivilization+0x10
// is set by the registrar to 0x4eb5+id (base game) or 0x1d555+id (DLC package),
// see 0x5c9744. Both ranges are unused for ids 35..50 in the strings file.
#define DESC_BASE_GAME       0x4eb5
#define DESC_BASE_DLC        0x1d555
#define MAX_CIVS             64

typedef struct {
    int id, base;
    char name[128];
    char *description;              // from civ json "description" (real '\n' line breaks)
    char *ai_names[MAX_AI_NAMES];   // civ json "ai_names"
    int ai_count;
    char ai_count_str[8];
    char mp3[MAX_PATH];             // relative to resources\_common\sound\civ\ (see below)
    char victory_sound[MAX_PATH];   // absolute path to mod's victory audio (mp3/wav), or empty
    char slp_path[MAX_PATH];        // absolute path of the mod's interface panel art
    int unique_techs[2];            // castle_tech, imp_tech (or -1, -1 if default from base)
    char *uu_strings[20];           // SIDs 28500 + (civ-35)*20 + slot: 0..9 unique unit, 10..12 unique building
} WlCiv;

// Double-buffered civ and command string tables for thread-safe hot reload:
// hook_string_lookup can run on background threads and returns const char*
// pointers into the active buffer. Reload writes into the inactive buffer
// (freeing only strings from TWO reloads ago) and flips the active index under
// g_civs_cs, so pointers already returned to another thread remain valid.
static WlCiv g_civ_bufs[2][MAX_CIVS];
static int g_civ_buf_idx;
static int g_civ_counts[2];
#define ACTIVE_CIVS      (g_civ_bufs[g_civ_buf_idx])
#define ACTIVE_CIV_COUNT (g_civ_counts[g_civ_buf_idx])

// Stable per-civ-id mp3 filename storage so Civilization+0x0c (which stores the
// raw const char* passed to 0x5c9640) never points into a shifted WlCiv slot.
static char g_civ_mp3_stable[MAX_CIVS][64];

// Captured at startup in hook_register_rajas for hot-reload reconciliation.
static void *g_rajas_pkg;
static int g_vanilla_ut_count = -1;
static int g_ever_reg_ids[MAX_CIVS];
static int g_ever_reg_count;

static CRITICAL_SECTION g_civs_cs;
static int g_civs_cs_init;

static void civs_cs_init(void) {
    if (!g_civs_cs_init) {
        InitializeCriticalSection(&g_civs_cs);
        g_civs_cs_init = 1;
    }
}

typedef void (__cdecl *RegisterPkg_t)(void *pkg);
typedef void (__cdecl *RegisterCiv_t)(void *pkg, int id, const char *mp3, int name_str,
                                      int bonus_base, int t1, int t2, char playable);
typedef BYTE *(__thiscall *CivFind_t)(void *map, int *id);
typedef int (__thiscall *CivNameId_t)(void *self, int id);
typedef const char *(__thiscall *StringLookup_t)(void *table, const char *key);
typedef void (__thiscall *ListRemove_t)(void *list_head, void *node);
typedef void (__thiscall *UtVecInsert_t)(void *vec, int *pos, const int *val);
typedef void (__thiscall *CivChoiceSetInt_t)(void *choice, int civ_id);

static RegisterPkg_t g_orig_register_rajas;
static CivNameId_t g_orig_name_id;
static StringLookup_t g_orig_lookup;

static void wl_install_civ_patches(void);

// Caller must hold g_civs_cs (or run before any reload is possible).
static const WlCiv *find_civ_locked(int id) {
    const WlCiv *arr = ACTIVE_CIVS;
    int n = ACTIVE_CIV_COUNT;
    for (int i = 0; i < n; i++)
        if (arr[i].id == id) return &arr[i];
    return NULL;
}

int wl_civs_count(void) {
    civs_cs_init();
    EnterCriticalSection(&g_civs_cs);
    int n = ACTIVE_CIV_COUNT;
    LeaveCriticalSection(&g_civs_cs);
    return n;
}

const char *wl_civ_get_sound(int civ_id) {
    civs_cs_init();
    EnterCriticalSection(&g_civs_cs);
    const WlCiv *c = find_civ_locked(civ_id);
    const char *r = (c && c->mp3[0]) ? c->mp3 : NULL;
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

const char *wl_civ_get_victory_sound(int civ_id) {
    civs_cs_init();
    EnterCriticalSection(&g_civs_cs);
    const WlCiv *c = find_civ_locked(civ_id);
    const char *r = (c && c->victory_sound[0]) ? c->victory_sound : NULL;
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

// The game's text system is UTF-8: its decoder 0x444e40 returns 0 WITHOUT
// advancing on an invalid byte, and the caller loop 0x4936e0 then spins
// forever (tech tree froze on a lone 0xB7 "·", 2026-11-14). An earlier
// "Â·" bug was json.c double-encoding, not an ANSI renderer. So: keep UTF-8,
// but replace every byte that is not part of a valid UTF-8 sequence by '?'.
static void sanitize_utf8(char *s, size_t cap) {
    (void)cap;
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

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) { memcpy(d, s, n); sanitize_utf8(d, n); }
    return d;
}

// Optional identity fields of a civ json:
//   "description": text shown on civ selection / tech tree ("\n" for new lines)
//   "ai_names": ["King A", "Queen B", ...]  AI player names in the lobby
//   "sound": "select.mp3"                    file in the mod folder, played on selection
//   "victory_sound": "victory.mp3"           file in the mod folder, played when local player wins
//   "interface_slp": "game_b.slp"            in-game bottom panel art, file in the mod folder
//   "unique_unit": Castle unique unit definition (name, elite_name, help, stats, costs)
static void load_identity(WlCiv *c, const char *mod, const char *json_file) {
    char path[MAX_PATH], err[256] = "";
    snprintf(path, MAX_PATH, "%s\\mods\\%s\\civs\\%s", g_wl.root, mod, json_file);
    char *text = wl_read_file(path, NULL);
    if (!text) { wl_log("civs: cannot read %s", path); return; }
    WlJsonVal *root = wl_json_parse(text, err, sizeof err);
    free(text);
    if (!root) { wl_log("civs: %s: %s", path, err); return; }

    const char *desc = wl_json_str(wl_json_get(root, "description"), NULL);
    if (desc) c->description = dup_str(desc);

    const WlJsonVal *names = wl_json_get(root, "ai_names");
    for (size_t i = 0; i < wl_json_len(names) && c->ai_count < MAX_AI_NAMES; i++) {
        const char *n = wl_json_str(wl_json_at(names, i), NULL);
        if (n) c->ai_names[c->ai_count++] = dup_str(n);
    }
    snprintf(c->ai_count_str, sizeof c->ai_count_str, "%d", c->ai_count);

    // The selection sound is "resources\\_common\\sound\\civ\\" + mp3 (0x828670).
    // A relative "..\\..\\" path did not play in-game, so use a unique file
    // name and redirect it (CreateFileA + FindFirstFileA hooks in files.c).
    const char *snd = wl_json_str(wl_json_get(root, "sound"), NULL);
    if (snd) {
        char full[MAX_PATH];
        snprintf(full, sizeof full, "%s\\mods\\%s\\%s", g_wl.root, mod, snd);
        // a missing file would be handed to the game: keep the base civ's sound instead
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
            wl_log("civs: %d sound '%s' not found in mod %s, using the base civ's", c->id, snd, mod);
        } else {
            snprintf(c->mp3, sizeof c->mp3, "wololoader_civ%d.mp3", c->id);
            wl_add_redirect(c->mp3, full);
        }
    }

    const char *vsnd = wl_json_str(wl_json_get(root, "victory_sound"), NULL);
    if (vsnd && vsnd[0]) {
        char full[MAX_PATH];
        snprintf(full, sizeof full, "%s\\mods\\%s\\%s", g_wl.root, mod, vsnd);
        for (char *p = full; *p; p++) if (*p == '/') *p = '\\';
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
            wl_log("civs: %d victory_sound '%s' not found in mod %s, using vanilla", c->id, vsnd, mod);
        } else {
            lstrcpynA(c->victory_sound, full, sizeof c->victory_sound);
        }
    }

    const char *slp = wl_json_str(wl_json_get(root, "interface_slp"), NULL);
    if (slp) {
        char full[MAX_PATH];
        snprintf(full, sizeof full, "%s\\mods\\%s\\%s", g_wl.root, mod, slp);
        // The game cannot load a missing game_b<id>.slp and crashes at game start (null
        // panel art read at 0x45a496, called from 0x6c5b54): use the base civ's art then.
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
            wl_log("civs: %d interface_slp '%s' not found in mod %s, using the base civ's art", c->id, slp, mod);
        } else {
            lstrcpynA(c->slp_path, full, sizeof c->slp_path);
            char name[32];
            snprintf(name, sizeof name, "game_b%d.slp", c->id);
            wl_add_redirect(name, c->slp_path);
        }
    }

    const WlJsonVal *ut = wl_json_get(root, "unique_techs");
    if (ut && ut->type == WL_JSON_ARRAY && ut->u.arr.count == 2) {
        c->unique_techs[0] = (int)ut->u.arr.items[0]->u.num.ival;
        c->unique_techs[1] = (int)ut->u.arr.items[1]->u.num.ival;
    }

    // Castle unique unit strings (SIDs 28500 + (civ_id - 35) * 20 + slot)
    // 0: name, 1: 'Create <name>', 2: help (or name), 3: elite name,
    // 4: 'Create <elite name>', 5: help (or elite name), 6: '<name> (make available)',
    // 7: elite name (tech), 8: 'Upgrade to <elite name>',
    // 9: 'Research <elite name> to upgrade your <name>s.'
    const WlJsonVal *uu = wl_json_get(root, "unique_unit");
    if (uu && uu->type == WL_JSON_OBJECT) {
        const char *u_name = wl_json_str(wl_json_get(uu, "name"), NULL);
        if (u_name && u_name[0]) {
            const char *el_name = wl_json_str(wl_json_get(uu, "elite_name"), NULL);
            char el_buf[256];
            if (!el_name || !el_name[0]) {
                snprintf(el_buf, sizeof el_buf, "Elite %s", u_name);
                el_name = el_buf;
            }
            const char *help = wl_json_str(wl_json_get(uu, "help"), NULL);
            if (!help || !help[0]) help = NULL;

            char buf[512];
            // 0: name
            c->uu_strings[0] = dup_str(u_name);
            // 1: 'Create <name>'
            snprintf(buf, sizeof buf, "Create %s", u_name);
            c->uu_strings[1] = dup_str(buf);
            // 2: rich tooltip, same template as the game's unit help strings
            //    (e.g. 26083): <cost>, <hp>... are filled in by the game.
            snprintf(buf, sizeof buf, "Create <b> %s<b> (<cost>) \n%s \n<hp> <attack> <armor> <piercearmor> <range>",
                     u_name, help ? help : u_name);
            c->uu_strings[2] = dup_str(buf);
            // 3: elite name
            c->uu_strings[3] = dup_str(el_name);
            // 4: 'Create <elite name>'
            snprintf(buf, sizeof buf, "Create %s", el_name);
            c->uu_strings[4] = dup_str(buf);
            // 5: rich tooltip of the elite
            snprintf(buf, sizeof buf, "Create <b> %s<b> (<cost>) \n%s \n<hp> <attack> <armor> <piercearmor> <range>",
                     el_name, help ? help : el_name);
            c->uu_strings[5] = dup_str(buf);
            // 6: '<name> (make available)'
            snprintf(buf, sizeof buf, "%s (make available)", u_name);
            c->uu_strings[6] = dup_str(buf);
            // 7: elite name (tech)
            c->uu_strings[7] = dup_str(el_name);
            // 8: 'Upgrade to <elite name>'
            snprintf(buf, sizeof buf, "Upgrade to %s", el_name);
            c->uu_strings[8] = dup_str(buf);
            // 9: 'Research <elite name> to upgrade your <name>s.'
            // template of the game's elite upgrade help (e.g. 28381)
            snprintf(buf, sizeof buf, "Upgrade to <b>%s<b> (<cost>) \nUpgrades your %ss and lets you create %ss, which are stronger.",
                     el_name, u_name, el_name);
            c->uu_strings[9] = dup_str(buf);
        }
    }
    // Unique building strings (same SID block): 10 name, 11 'Build <name>', 12 rich tooltip
    // (template of the game's building help, e.g. Monastery 26104).
    const WlJsonVal *ub = wl_json_get(root, "unique_building");
    if (ub && ub->type == WL_JSON_OBJECT) {
        const char *b_name = wl_json_str(wl_json_get(ub, "name"), NULL);
        if (b_name && b_name[0]) {
            const char *help = wl_json_str(wl_json_get(ub, "help"), NULL);
            char buf[512];
            c->uu_strings[10] = dup_str(b_name);
            snprintf(buf, sizeof buf, "Build %s", b_name);
            c->uu_strings[11] = dup_str(buf);
            snprintf(buf, sizeof buf, "Build <b>%s<b> (<cost>) \n%s \n<hp> <armor> <piercearmor>",
                     b_name, help && help[0] ? help : b_name);
            c->uu_strings[12] = dup_str(buf);
        }
    }
    wl_json_free(root);
}

// ---- building command strings and Lua buttons (written by the builder, see tools/build.py
// add_building_commands): cache\strings.txt "id<TAB>text" (\\n = new line), ids 31210.. in
// triples (name, create/research, rich help); cache\commands.txt
// "civ<TAB>building<TAB>button<TAB>name<TAB>icon<TAB>help id".
#define CMD_SID_FIRST 31210
#define CMD_SID_LAST 32767
#define CMD_SID_COUNT (CMD_SID_LAST - CMD_SID_FIRST + 1)
static char *g_cmd_str_bufs[2][CMD_SID_COUNT];
static WlLuaButton g_lbtn_bufs[2][64];
static int g_nlbtn_bufs[2];
static WlBuildingCmd g_bcmds_bufs[2][WL_MAX_BLDG_CMDS];
static int g_nbcmds_bufs[2];
static int g_cmd_buf_idx;

const WlLuaButton *wl_lua_buttons(int *n) {
    civs_cs_init();
    EnterCriticalSection(&g_civs_cs);
    int idx = g_cmd_buf_idx;
    *n = g_nlbtn_bufs[idx];
    const WlLuaButton *r = g_lbtn_bufs[idx];
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

const WlBuildingCmd *wl_building_commands(int *n) {
    civs_cs_init();
    EnterCriticalSection(&g_civs_cs);
    int idx = g_cmd_buf_idx;
    *n = g_nbcmds_bufs[idx];
    const WlBuildingCmd *r = g_bcmds_bufs[idx];
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

// Split a line on literal '\t' so empty fields ("\t\t") are preserved cleanly.
static int split_tabs(char *line, char **fields, int max_fields) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return 0;
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = 0;
        p = tab + 1;
    }
    return n;
}

static void free_cmd_buf(int slot) {
    for (int i = 0; i < CMD_SID_COUNT; i++) {
        free(g_cmd_str_bufs[slot][i]);
        g_cmd_str_bufs[slot][i] = NULL;
    }
    g_nlbtn_bufs[slot] = 0;
    g_nbcmds_bufs[slot] = 0;
}

static void load_command_files_into(int slot) {
    free_cmd_buf(slot);
    char **cmd_str = g_cmd_str_bufs[slot];
    WlLuaButton *lbtn = g_lbtn_bufs[slot];
    WlBuildingCmd *bcmds = g_bcmds_bufs[slot];
    int nlbtn = 0, nbcmds = 0;
    char path[MAX_PATH], line[1200];
    snprintf(path, sizeof path, "%s\\cache\\strings.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    int ns = 0;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            int id = atoi(line);
            if (id < CMD_SID_FIRST || id > CMD_SID_LAST) continue;
            char *t = tab + 1, *o = t;
            for (char *p = t; *p && *p != '\r' && *p != '\n'; p++) {
                if (p[0] == '\\' && p[1] == 'n') { *o++ = '\n'; p++; }
                else *o++ = *p;
            }
            *o = 0;
            free(cmd_str[id - CMD_SID_FIRST]);
            cmd_str[id - CMD_SID_FIRST] = dup_str(t);
            ns++;
        }
        fclose(f);
    }
    snprintf(path, sizeof path, "%s\\cache\\commands.txt", g_wl.root);
    f = fopen(path, "rb");
    if (f) {
        while (fgets(line, sizeof line, f) && nbcmds < WL_MAX_BLDG_CMDS) {
            char *fields[16];
            int nf = split_tabs(line, fields, 16);
            if (nf == 0) continue;

            WlBuildingCmd *bc = &bcmds[nbcmds];
            memset(bc, 0, sizeof(*bc));

            // 11-field format: civ, bldg, page, btn, type, target, icon, help, name, cond_act, cond_name
            if (nf >= 9 && (strcmp(fields[4], "train") == 0 ||
                            strcmp(fields[4], "research") == 0 ||
                            strcmp(fields[4], "lua") == 0)) {
                int c_page = atoi(fields[2]);
                bc->civ = atoi(fields[0]);
                bc->building = atoi(fields[1]);
                bc->page = c_page >= 1 ? c_page : 1;
                bc->button = atoi(fields[3]);
                lstrcpynA(bc->type, fields[4], sizeof bc->type);
                bc->target_id = atoi(fields[5]);
                bc->icon = atoi(fields[6]);
                bc->help = atoi(fields[7]);
                lstrcpynA(bc->name, fields[8], sizeof bc->name);
                lstrcpynA(bc->cond_action, (nf >= 10 && fields[9][0]) ? fields[9] : "disable", sizeof bc->cond_action);
                lstrcpynA(bc->cond_name, (nf >= 11) ? fields[10] : "", sizeof bc->cond_name);

                if (strcmp(bc->type, "lua") == 0 && nlbtn < 64) {
                    WlLuaButton *b = &lbtn[nlbtn];
                    b->civ = bc->civ;
                    b->building = bc->building;
                    b->button = bc->button;
                    b->icon = bc->icon;
                    b->help = bc->help;
                    lstrcpynA(b->name, bc->name, sizeof b->name);
                    nlbtn++;
                }
                nbcmds++;
            } else if (nf == 6) {
                // Legacy 6-field format: civ, bldg, btn, name, icon, help
                bc->civ = atoi(fields[0]);
                bc->building = atoi(fields[1]);
                bc->page = 1;
                bc->button = atoi(fields[2]);
                lstrcpynA(bc->type, "lua", sizeof bc->type);
                bc->target_id = 0;
                bc->icon = atoi(fields[4]);
                bc->help = atoi(fields[5]);
                lstrcpynA(bc->name, fields[3], sizeof bc->name);
                lstrcpynA(bc->cond_action, "disable", sizeof bc->cond_action);
                bc->cond_name[0] = '\0';
                nbcmds++;

                if (nlbtn < 64) {
                    WlLuaButton *b = &lbtn[nlbtn];
                    b->civ = bc->civ;
                    b->building = bc->building;
                    b->button = bc->button;
                    b->icon = bc->icon;
                    b->help = bc->help;
                    lstrcpynA(b->name, bc->name, sizeof b->name);
                    nlbtn++;
                }
            }
        }
        fclose(f);
    }
    g_nlbtn_bufs[slot] = nlbtn;
    g_nbcmds_bufs[slot] = nbcmds;
    if (ns || nbcmds) wl_log("civs: %d command string(s), %d building command(s)", ns, nbcmds);
}

static const char *command_string_locked(int n) {
    // rich texts like the unique units: name + 21000 (tech tree) and create/research + 20000
    // (button rollover) show the triple's third string; help fields are 79000 + id
    if (n >= CMD_SID_FIRST + 21000 && n <= CMD_SID_LAST + 21000 && (n - 21000 - CMD_SID_FIRST) % 3 == 0) n = n - 21000 + 2;
    else if (n >= CMD_SID_FIRST + 20000 && n <= CMD_SID_LAST + 20000 && (n - 20000 - CMD_SID_FIRST) % 3 == 1) n = n - 20000 + 1;
    else if (n >= CMD_SID_FIRST + 79000 && n <= CMD_SID_LAST + 79000) n -= 79000;
    else if (n >= CMD_SID_FIRST + 150000 && n <= CMD_SID_LAST + 150000) n -= 150000;
    if (n < CMD_SID_FIRST || n > CMD_SID_LAST) return NULL;
    return g_cmd_str_bufs[g_cmd_buf_idx][n - CMD_SID_FIRST];
}

static void free_civ_buf(int slot) {
    int n = g_civ_counts[slot];
    for (int i = 0; i < n; i++) {
        WlCiv *c = &g_civ_bufs[slot][i];
        free(c->description);
        for (int j = 0; j < c->ai_count; j++) free(c->ai_names[j]);
        for (int j = 0; j < 20; j++) free(c->uu_strings[j]);
    }
    memset(g_civ_bufs[slot], 0, sizeof g_civ_bufs[slot]);
    g_civ_counts[slot] = 0;
}

static void load_civs_txt_into(int slot) {
    free_civ_buf(slot);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\cache\\civs.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) { wl_log("civs: no cache\\civs.txt"); return; }
    char line[512];
    int count = 0;
    while (fgets(line, sizeof line, f) && count < MAX_CIVS) {
        line[strcspn(line, "\r\n")] = 0;
        char *id = strtok(line, "\t"), *base = strtok(NULL, "\t"), *name = strtok(NULL, "\t");
        if (!id || !base || !name) continue;
        WlCiv *c = &g_civ_bufs[slot][count];
        memset(c, 0, sizeof *c);
        c->unique_techs[0] = -1;
        c->unique_techs[1] = -1;
        c->id = atoi(id);
        c->base = atoi(base);
        lstrcpynA(c->name, name, sizeof c->name);
        sanitize_utf8(c->name, sizeof c->name);
        if (c->id < FIRST_CUSTOM_CIV) { wl_log("civs: bad id %d, skipped", c->id); continue; }
        char *mod = strtok(NULL, "\t"), *json = strtok(NULL, "\t");
        if (mod && json) load_identity(c, mod, json);
        char *t1_s = strtok(NULL, "\t");
        char *t2_s = strtok(NULL, "\t");
        if (t1_s && t2_s) {
            c->unique_techs[0] = atoi(t1_s);
            c->unique_techs[1] = atoi(t2_s);
        }
        wl_log("civs: %d '%s' (base %d)%s%s%s%s%s", c->id, c->name, c->base,
               c->description ? " +description" : "", c->mp3[0] ? " +sound" : "",
               c->victory_sound[0] ? " +victory_sound" : "",
               c->slp_path[0] ? " +interface" : "",
               (c->unique_techs[0] >= 0) ? " +unique_techs" : "");
        count++;
    }
    fclose(f);
    g_civ_counts[slot] = count;
}

static void record_ever_registered(int id) {
    for (int i = 0; i < g_ever_reg_count; i++)
        if (g_ever_reg_ids[i] == id) return;
    if (g_ever_reg_count < MAX_CIVS) g_ever_reg_ids[g_ever_reg_count++] = id;
}

static const char *stable_civ_mp3(const WlCiv *c, const BYTE *base) {
    if (c->mp3[0]) {
        int idx = c->id - FIRST_CUSTOM_CIV;
        if (idx >= 0 && idx < MAX_CIVS) {
            lstrcpynA(g_civ_mp3_stable[idx], c->mp3, sizeof g_civ_mp3_stable[0]);
            return g_civ_mp3_stable[idx];
        }
    }
    return *(const char **)(base + 0xc);
}

// Push one unique-tech id onto the game's global std::vector<int> at 0xaf25e0..0xaf25e8
// (mirrors Civilization::ctor 0x5c93f1..0x5c944b, using 0x479f30 when capacity is full).
static void ut_vec_push_back(int val) {
    int **vec = (int **)ADDR_UT_VEC;
    if (vec[1] != vec[2]) {
        *vec[1] = val;
        vec[1]++;
    } else {
        ((UtVecInsert_t)ADDR_UT_VEC_INSERT)((void *)ADDR_UT_VEC, vec[1], &val);
    }
}

// Runs after the game registered Rajas civs; every base civ exists by now.
static void __cdecl hook_register_rajas(void *pkg) {
    wl_civs_ensure();
    g_rajas_pkg = pkg;
    g_orig_register_rajas(pkg);
    // Record vanilla unique-tech vector size before custom civs append theirs
    // so hot reload can truncate back to vanilla and re-append active custom techs.
    int **ut_vec = (int **)ADDR_UT_VEC;
    if (ut_vec[0] && ut_vec[1] >= ut_vec[0])
        g_vanilla_ut_count = (int)(ut_vec[1] - ut_vec[0]);
    wl_log("civs: rajas pkg %p, vanilla unique techs %d", pkg, g_vanilla_ut_count);

    int n = ACTIVE_CIV_COUNT;
    for (int i = 0; i < n; i++) {
        WlCiv *c = &ACTIVE_CIVS[i];
        int base_id = c->base;
        BYTE *base = ((CivFind_t)ADDR_CIV_MAP_FIND)((void *)ADDR_CIV_MAP, &base_id);
        if (!base) { wl_log("civs: base civ %d not registered, skip %d", c->base, c->id); continue; }
        // Civilization: +0xc mp3 (char*), +0x14 bonus string base, +0x18/+0x1c tech ids.
        // +0x14 is the AI player name list (count string at the id, names after it).
        const char *mp3 = stable_civ_mp3(c, base);
        int ai = c->ai_count ? STR_BASE + c->id * 100 + STR_AI_COUNT : *(int *)(base + 0x14);
        int t1 = (c->unique_techs[0] >= 0) ? c->unique_techs[0] : *(int *)(base + 0x18);
        int t2 = (c->unique_techs[1] >= 0) ? c->unique_techs[1] : *(int *)(base + 0x1c);
        ((RegisterCiv_t)ADDR_REGISTER_CIV)(pkg, c->id, mp3, STR_BASE + c->id * 100 + STR_NAME, ai, t1, t2, 1);
        record_ever_registered(c->id);
        wl_log("civs: registered %d (mp3 %s, techs %d,%d, pkg %p)", c->id, mp3 ? mp3 : "?", t1, t2, pkg);
    }
}

// Reload Wololoader's civ & command string tables from cache\ into the inactive
// double-buffer and swap atomically under g_civs_cs.
int wl_civs_reload_tables(int *old_ids, int *old_n, int *new_ids, int *new_n) {
    civs_cs_init();
    int cur_c = g_civ_buf_idx, next_c = 1 - cur_c;
    int cur_s = g_cmd_buf_idx, next_s = 1 - cur_s;

    if (old_ids && old_n) {
        int n = g_civ_counts[cur_c];
        for (int i = 0; i < n; i++) old_ids[i] = g_civ_bufs[cur_c][i].id;
        *old_n = n;
    }

    load_civs_txt_into(next_c);
    load_command_files_into(next_s);

    EnterCriticalSection(&g_civs_cs);
    g_civ_buf_idx = next_c;
    g_cmd_buf_idx = next_s;
    int count = g_civ_counts[next_c];
    LeaveCriticalSection(&g_civs_cs);

    if (new_ids && new_n) {
        for (int i = 0; i < count; i++) new_ids[i] = g_civ_bufs[next_c][i].id;
        *new_n = count;
    }
    return count;
}

// Reset any PlayerSlot[0..7] inside a GameSettings object that points at a
// disabled custom Civilization (id >= 35, playable == 0) to Random (0x20).
// Verified offsets (AoK HD.exe 5.8):
// - PlayerSlot[i]: settings + 0xb0 + i*0x68
// - CivChoice: slot + 0x58 (0x6159b3: lea 0x58(%esi),%ecx; call 0x607490)
// - CivChoice+0 = CivilizationMode* (+8 == 0 means specific civ, 0x607820)
// - CivChoice+4 = Civilization* (+8 = id, +0x20 = playable byte)
// - CivChoice::setFromInt(0x607490): __thiscall(choice, 0x20), ret 4
static int sanitize_game_settings(void *settings) {
    if (!settings) return 0;
    int reset_count = 0;
    for (int i = 0; i < 8; i++) {
        void *choice = (char *)settings + 0xb0 + i * 0x68 + 0x58;
        void *mode_obj = *(void **)choice;
        BYTE *civ = *(BYTE **)((char *)choice + 4);
        if (!mode_obj || !civ) continue;
        if (*(int *)((char *)mode_obj + 8) != 0) continue;  // Random/Spectator/Mirror
        int cid = *(int *)(civ + 8);
        BYTE playable = *(BYTE *)(civ + 0x20);
        if (cid >= FIRST_CUSTOM_CIV && playable == 0) {
            ((CivChoiceSetInt_t)ADDR_CIVCHOICE_SET)(choice, 0x20);
            wl_log("civs: reset GameSettings %p slot %d (removed civ %d) -> Random", settings, i, cid);
            reset_count++;
        }
    }
    return reset_count;
}

// Reconcile the game's civ registry (g_rajas_pkg+0x98, 0xaf25ec, 0xaf25e0) and
// persistent Profile/CommHandler GameSettings with the reloaded ACTIVE_CIVS.
// Must run on the main thread at the Main Menu (World == NULL).
int wl_civs_reconcile_registry(void) {
    if (!g_rajas_pkg) {
        wl_log("civs: reconcile skipped (rajas package not captured yet)");
        return 0;
    }
    // Verify prologue bytes of every game helper we call (AGENTS.md rule 2).
    static const BYTE b_reg[]    = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x06, 0x50, 0x93, 0x00 };
    static const BYTE b_rm_pkg[] = { 0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0x75, 0x08, 0x8b, 0xd9 };
    static const BYTE b_rm_civ[] = { 0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0x75, 0x08, 0x8b, 0xd9 };
    static const BYTE b_ut_ins[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x70, 0xff, 0x90, 0x00 };
    static const BYTE b_setint[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xd9, 0xe7, 0x93, 0x00 };
    if (memcmp((void *)ADDR_REGISTER_CIV, b_reg, sizeof b_reg) != 0 ||
        memcmp((void *)ADDR_PKG_CIV_REMOVE, b_rm_pkg, sizeof b_rm_pkg) != 0 ||
        memcmp((void *)ADDR_CIV_PKG_REMOVE, b_rm_civ, sizeof b_rm_civ) != 0 ||
        memcmp((void *)ADDR_UT_VEC_INSERT, b_ut_ins, sizeof b_ut_ins) != 0 ||
        memcmp((void *)ADDR_CIVCHOICE_SET, b_setint, sizeof b_setint) != 0) {
        wl_log("civs: reconcile aborted (unexpected exe bytes)");
        return -1;
    }

    // 1. Unlink every custom DlcPackageCivilization (id >= 35) from g_rajas_pkg+0x98
    //    and civ+0x24. Each remove decrements node->rc (+4); rc goes 2->1->0 and the
    //    game's own virtual dtor frees DlcPackageCivilization via 0x8d480f.
    //    Civilization objects stay in 0xaf25ec with playable (+0x20) = 0 so no
    //    external refcounted Civilization* can ever become a dangling pointer.
    void *pkg_list = (char *)g_rajas_pkg + 0x98;
    void *node = *(void **)pkg_list;   // 0x43c0d0: head ptr at list_head+0
    int unlinked = 0;
    while (node) {
        void *next = *(void **)((char *)node + 0x14);  // 0x5d2ef0 / 0x427930: pkg list next at +0x14
        BYTE *civ = *(BYTE **)((char *)node + 0x08);   // 0x5c9764: Civilization* at +0x08
        if (civ && *(int *)(civ + 8) >= FIRST_CUSTOM_CIV) {
            ((ListRemove_t)ADDR_PKG_CIV_REMOVE)(pkg_list, node);
            ((ListRemove_t)ADDR_CIV_PKG_REMOVE)(civ + 0x24, node);
            *(BYTE *)(civ + 0x20) = 0;
            unlinked++;
        }
        node = next;
    }
    for (int i = 0; i < g_ever_reg_count; i++) {
        int id = g_ever_reg_ids[i];
        BYTE *civ = ((CivFind_t)ADDR_CIV_MAP_FIND)((void *)ADDR_CIV_MAP, &id);
        if (civ) *(BYTE *)(civ + 0x20) = 0;
    }

    // 2. Truncate unique-tech std::vector<int> at 0xaf25e0 back to vanilla size.
    int **ut_vec = (int **)ADDR_UT_VEC;
    if (g_vanilla_ut_count >= 0 && ut_vec[0] && ut_vec[1] >= ut_vec[0] + g_vanilla_ut_count) {
        ut_vec[1] = ut_vec[0] + g_vanilla_ut_count;
    }

    // 3. Register / re-enable every currently active custom civ.
    int n = ACTIVE_CIV_COUNT;
    int registered = 0;
    for (int i = 0; i < n; i++) {
        const WlCiv *c = &ACTIVE_CIVS[i];
        int base_id = c->base;
        BYTE *base = ((CivFind_t)ADDR_CIV_MAP_FIND)((void *)ADDR_CIV_MAP, &base_id);
        if (!base) { wl_log("civs: reconcile: base civ %d missing, skip %d", c->base, c->id); continue; }
        const char *mp3 = stable_civ_mp3(c, base);
        int ai = c->ai_count ? STR_BASE + c->id * 100 + STR_AI_COUNT : *(int *)(base + 0x14);
        int t1 = (c->unique_techs[0] >= 0) ? c->unique_techs[0] : *(int *)(base + 0x18);
        int t2 = (c->unique_techs[1] >= 0) ? c->unique_techs[1] : *(int *)(base + 0x1c);

        int cid = c->id;
        BYTE *existing = ((CivFind_t)ADDR_CIV_MAP_FIND)((void *)ADDR_CIV_MAP, &cid);
        if (existing) {
            // 0x5c9640 skips Civilization::ctor when already in 0xaf25ec, so update
            // fields in place and re-append unique techs before creating the new
            // DlcPackageCivilization via 0x5c9640.
            *(const char **)(existing + 0x0c) = mp3;
            *(int *)(existing + 0x10) = STR_BASE + c->id * 100 + STR_NAME;
            *(int *)(existing + 0x14) = ai;
            *(int *)(existing + 0x18) = t1;
            *(int *)(existing + 0x1c) = t2;
            *(BYTE *)(existing + 0x20) = 1;
            ut_vec_push_back(t1);
            ut_vec_push_back(t2);
        }
        ((RegisterCiv_t)ADDR_REGISTER_CIV)(g_rajas_pkg, c->id, mp3,
                                           STR_BASE + c->id * 100 + STR_NAME, ai, t1, t2, 1);
        record_ever_registered(c->id);
        registered++;
    }

    // 4. Sanitize persistent Profile GameSettings (+0x4c, +0x50, +0x54, +0x58)
    //    and CommHandler's current GameSettings so no slot references a removed civ.
    int slots_reset = 0;
    void **pm_holder = *(void ***)ADDR_PROFILE_MGR;
    void *pm = pm_holder ? *pm_holder : NULL;
    if (pm) {
        void **pbegin = *(void ***)((char *)pm + 0x14);
        void **pend   = *(void ***)((char *)pm + 0x18);
        if (pbegin && pend && pend >= pbegin && (pend - pbegin) <= 64) {
            static const int k_gs_off[4] = { 0x4c, 0x50, 0x54, 0x58 };
            for (void **pp = pbegin; pp < pend; pp++) {
                void *prof = *pp;
                if (!prof) continue;
                for (int k = 0; k < 4; k++) {
                    void *gs = *(void **)((char *)prof + k_gs_off[k]);
                    slots_reset += sanitize_game_settings(gs);
                }
            }
        }
    }
    void *game = *(void **)ADDR_GAME_PTR;
    void *comm = game ? *(void **)((char *)game + 0x98) : NULL;
    void *cur_gs = comm ? *(void **)((char *)comm + 0xd54) : NULL;
    slots_reset += sanitize_game_settings(cur_gs);

    wl_log("civs: reconciled registry (unlinked %d, active %d, slots reset %d)",
           unlinked, registered, slots_reset);
    return registered;
}

// The original only maps ids 1..31 through the registry; custom ids return -2.
static int __fastcall hook_civ_name_id(void *self, void *edx, int id) {
    (void)edx;
    EnterCriticalSection(&g_civs_cs);
    int found = find_civ_locked(id) != NULL;
    LeaveCriticalSection(&g_civs_cs);
    if (found) return STR_BASE + id * 100 + STR_NAME;
    return g_orig_name_id(self, id);
}

static const char *__fastcall hook_string_lookup(void *table, void *edx, const char *key) {
    (void)edx;
    if (key && key[0] == 'S') {  // main menu button now opens the Wololoader Mods screen
        if (strcmp(key, "STEAM_WORKSHOP") == 0) return "Mods";
        if (strcmp(key, "STEAM_WORKSHOP_ROLLOVER") == 0) return "View installed Wololoader mods.";
    }
    // Startup screen title (Steam Workshop sync, key from the strings file):
    // show our own name there instead of "Validating Subscriptions".
    if (key && key[0] == 'M' && strcmp(key, "MODMGR_DOWNLOAD_TITLE") == 0)
        return "Wololoader - loading mods...";
    // Wololoader's own UI strings (buildpage.c "Mod Buildings" button help, id 28490; "Next Page", id 28491).
    if (key && strcmp(key, "28490") == 0) return "<b>Mod Buildings<b> \nDisplay the buildings added by mods.";
    if (key && strcmp(key, "28491") == 0) return "<b>Next Page<b> \nCycle to the next page of commands.";
    if (key && key[0] >= '1' && key[0] <= '9') {
        int n = atoi(key);
        const char *res = NULL;
        EnterCriticalSection(&g_civs_cs);
        res = command_string_locked(n);
        if (!res && ACTIVE_CIV_COUNT) {
            int un = n;
            // Custom unique unit strings: SID(slot) = 28500 + (civ_id - 35) * 20 + slot.
            // Tech tree asks for name id + 21000 -> rich texts in slots 2 / 5 / 12.
            if (un >= 28500 + 21000 && un < 30100 + 21000) {
                int s = (un - 21000 - 28500) % 20;
                if (s == 0 || s == 3 || s == 10) un = un - 21000 + 2;
            }
            // Unit button rollover asks for creation id + 20000 -> rich texts in slots 2 / 5 / 12.
            if (un >= 28500 + 20000 && un < 30100 + 20000) {
                int s = (un - 20000 - 28500) % 20;
                if (s == 1 || s == 4 || s == 11) un = un - 20000 + 1;
            }
            // Help field (79000 + id) in case some screen looks it up directly.
            if (un >= 28500 + 79000 && un < 30100 + 79000) un -= 79000;
            if (un >= 28500 && un < 30100) {
                int cid = 35 + (un - 28500) / 20;
                int slot = (un - 28500) % 20;
                if (slot >= 0 && slot < 20) {
                    const WlCiv *c = find_civ_locked(cid);
                    if (c && c->uu_strings[slot]) res = c->uu_strings[slot];
                }
            }
            if (!res && n >= STR_BASE && n < STR_BASE + 100000) {
                const WlCiv *c = find_civ_locked((n - STR_BASE) / 100);
                int slot = (n - STR_BASE) % 100;
                if (c && slot == STR_NAME) res = c->name;
                else if (c && slot == STR_AI_COUNT) res = c->ai_count_str;
                else if (c && slot > STR_AI_COUNT && slot <= STR_AI_COUNT + c->ai_count)
                    res = c->ai_names[slot - STR_AI_COUNT - 1];
            }
            if (!res) {
                int d = n - DESC_BASE_DLC;
                if (d < FIRST_CUSTOM_CIV) d = n - DESC_BASE_GAME;
                if (d >= FIRST_CUSTOM_CIV && d < FIRST_CUSTOM_CIV + MAX_CIVS) {
                    const WlCiv *c = find_civ_locked(d);
                    if (c && c->description) res = c->description;
                }
            }
        }
        LeaveCriticalSection(&g_civs_cs);
        if (res) return res;
    }
    return g_orig_lookup(table, key);
}

// Lazy start-up of the civ system: rebuild the cache if mods changed, then read
// civs.txt. This must NOT run inside DllMain: the build calls zlibwapi.dll,
// whose C runtime is not initialised yet at that point (R6030 "CRT not
// initialized"). It runs on first need instead: when the game opens the dat
// (files.c) or registers civs (hook_register_rajas), whichever comes first.
static volatile LONG g_civs_state;   // 0 = not started, 1 = running, 2 = done
static DWORD g_civs_thread;

void wl_civs_ensure(void) {
    if (g_civs_state == 2) return;
    if (InterlockedCompareExchange(&g_civs_state, 1, 0) == 0) {
        g_civs_thread = GetCurrentThreadId();
        civs_cs_init();
        wl_autobuild();
        load_civs_txt_into(g_civ_buf_idx);
        load_command_files_into(g_cmd_buf_idx);
        // Mods are scanned (DllMain) and built by now; Lua is created here, lazily,
        // because its CRT/allocator use must not run inside DllMain (AGENTS.md rule 5).
        wl_script_init();
        wl_hotreload_mark_baseline();
        InterlockedExchange(&g_civs_state, 2);
        return;
    }
    if (g_civs_thread == GetCurrentThreadId()) return;  // re-entered from the build itself
    while (g_civs_state != 2) Sleep(1);
}

// Vanilla Mode: only the string lookup hook, for the main-menu button name
// ("Mods" instead of "Steam Workshop") and the startup title. No custom civs are
// registered then (ACTIVE_CIV_COUNT == 0), so no game text changes.
void wl_install_string_hook_only(void) {
    civs_cs_init();
    if (g_orig_lookup) return;
    g_orig_lookup = (StringLookup_t)wl_hook(ADDR_STRING_LOOKUP, hook_string_lookup, 5);
}

void wl_install_civ_hooks(void) {
    civs_cs_init();
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1
    g_orig_lookup = (StringLookup_t)wl_hook(ADDR_STRING_LOOKUP, hook_string_lookup, 5);
    // Installed unconditionally: civs.txt is read later (wl_civs_ensure) and
    // every hook passes through when there are no custom civs.
    // 55 8b ec 56 8b 75 08 = push ebp; mov ebp,esp; push esi; mov esi,[ebp+8]
    g_orig_register_rajas = (RegisterPkg_t)wl_hook(ADDR_REGISTER_RAJAS, hook_register_rajas, 7);
    // 55 8b ec 83 ec 08 = push ebp; mov ebp,esp; sub esp,8
    g_orig_name_id = (CivNameId_t)wl_hook(ADDR_CIV_NAME_ID, hook_civ_name_id, 6);
    wl_install_civ_patches();
}

// In-game UI art is chosen per civ id (game_b%d.slp, 0x6c56b0). A custom civ
// keeps its own id when its mod ships "interface_slp" (files.c redirects
// game_b<id>.slp to it), otherwise it uses the base civ's art.
int __cdecl wl_ui_civ_id(int id) {
    EnterCriticalSection(&g_civs_cs);
    const WlCiv *c = find_civ_locked(id);
    int r = !c ? id : (c->slp_path[0] ? id : c->base);
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

// Replaces at 0x6c5aa9 (6 bytes):  mov eax,[eax+8] ; mov [ebp-0x78],eax
__asm__(
    ".globl _wl_game_b_stub\n"
    "_wl_game_b_stub:\n"
    "  mov 8(%eax), %eax\n"
    "  push %ecx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  call _wl_ui_civ_id\n"
    "  add $4, %esp\n"
    "  pop %edx\n"
    "  pop %ecx\n"
    "  mov %eax, -0x78(%ebp)\n"
    "  ret\n");
void wl_game_b_stub(void);

// Civ count across DLC packages (thiscall, no args, plain ret). Its only two
// callers (0x68bcf0, 0x84f860: tech tree lookup) loop ids 1..count looking for
// the player's civ, so the count must reach our highest custom civ id.
#define ADDR_CIV_COUNT 0x5d63e0
typedef int (__thiscall *CivCount_t)(void *self);
static CivCount_t g_orig_civ_count;

static int __fastcall hook_civ_count(void *self, void *edx) {
    (void)edx;
    int n = g_orig_civ_count(self);
    EnterCriticalSection(&g_civs_cs);
    int cnt = ACTIVE_CIV_COUNT;
    const WlCiv *arr = ACTIVE_CIVS;
    for (int i = 0; i < cnt; i++)
        if (arr[i].id > n) n = arr[i].id;
    LeaveCriticalSection(&g_civs_cs);
    return n;
}

// AI script symbols (0x4f5960): the game defines BRITON-CIV ... VIETNAMESE-CIV
// for each player through a jump table on the civ id (ids above 0x1f are
// skipped). AI scripts such as Promisory define their constants inside
// "#load-if-defined <CIV>-CIV" blocks, so a civ without a symbol fails with
// "ERR2004: Missing identifier". Custom civs therefore play as their base civ.
int __cdecl wl_ai_civ_id(int id) {
    EnterCriticalSection(&g_civs_cs);
    const WlCiv *c = find_civ_locked(id);
    int r = c ? c->base : id;
    LeaveCriticalSection(&g_civs_cs);
    return r;
}

// Replaces at 0x4f660c (6 bytes):  mov eax,[eax+8] ; cmp eax,0x1f
// The next instruction is "ja", so the cmp must be the last flag-setting
// instruction before ret. ecx and edx are live in the caller.
__asm__(
    ".globl _wl_ai_civ_stub\n"
    "_wl_ai_civ_stub:\n"
    "  mov 8(%eax), %eax\n"
    "  push %ecx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  call _wl_ai_civ_id\n"
    "  add $4, %esp\n"
    "  pop %edx\n"
    "  pop %ecx\n"
    "  cmp $0x1f, %eax\n"
    "  ret\n");
void wl_ai_civ_stub(void);

// Second jump table in the same function (own player's civ: unique unit
// constants). Replaces at 0x4f5d47 (7 bytes):
//   mov eax,[eax+8] ; dec eax ; cmp eax,0x1e      (next: ja)
__asm__(
    ".globl _wl_ai_own_civ_stub\n"
    "_wl_ai_own_civ_stub:\n"
    "  mov 8(%eax), %eax\n"
    "  push %ecx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  call _wl_ai_civ_id\n"
    "  add $4, %esp\n"
    "  pop %edx\n"
    "  pop %ecx\n"
    "  dec %eax\n"
    "  cmp $0x1e, %eax\n"
    "  ret\n");
void wl_ai_own_civ_stub(void);

static void wl_install_civ_patches(void) {
    wl_patch_call(0x4f660c, wl_ai_civ_stub, 6);
    wl_patch_call(0x4f5d47, wl_ai_own_civ_stub, 7);
    // 53 56 8d 99 d4 00 00 00 = push ebx; push esi; lea ebx,[ecx+0xd4]
    g_orig_civ_count = (CivCount_t)wl_hook(ADDR_CIV_COUNT, hook_civ_count, 8);
    wl_patch_call(0x6c5aa9, wl_game_b_stub, 6);
}
