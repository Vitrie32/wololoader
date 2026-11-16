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
    char slp_path[MAX_PATH];        // absolute path of the mod's interface panel art
    int unique_techs[2];            // castle_tech, imp_tech (or -1, -1 if default from base)
    char *uu_strings[20];           // SIDs 28500 + (civ-35)*20 + slot: 0..9 unique unit, 10..12 unique building
} WlCiv;

static WlCiv g_civs[MAX_CIVS];
static int g_civ_count;

typedef void (__cdecl *RegisterPkg_t)(void *pkg);
typedef void (__cdecl *RegisterCiv_t)(void *pkg, int id, const char *mp3, int name_str,
                                      int bonus_base, int t1, int t2, char playable);
typedef BYTE *(__thiscall *CivFind_t)(void *map, int *id);
typedef int (__thiscall *CivNameId_t)(void *self, int id);
typedef const char *(__thiscall *StringLookup_t)(void *table, const char *key);

static RegisterPkg_t g_orig_register_rajas;
static CivNameId_t g_orig_name_id;
static StringLookup_t g_orig_lookup;

static void wl_install_civ_patches(void);

static const WlCiv *find_civ(int id) {
    for (int i = 0; i < g_civ_count; i++)
        if (g_civs[i].id == id) return &g_civs[i];
    return NULL;
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
static char *g_cmd_str[CMD_SID_LAST - CMD_SID_FIRST + 1];
static WlLuaButton g_lbtn[64];
static int g_nlbtn;

const WlLuaButton *wl_lua_buttons(int *n) { *n = g_nlbtn; return g_lbtn; }

static void load_command_files(void) {
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
            free(g_cmd_str[id - CMD_SID_FIRST]);
            g_cmd_str[id - CMD_SID_FIRST] = dup_str(t);
            ns++;
        }
        fclose(f);
    }
    snprintf(path, sizeof path, "%s\\cache\\commands.txt", g_wl.root);
    f = fopen(path, "rb");
    g_nlbtn = 0;
    if (f) {
        while (fgets(line, sizeof line, f) && g_nlbtn < 64) {
            WlLuaButton *b = &g_lbtn[g_nlbtn];
            char name[64];
            if (sscanf(line, "%d\t%d\t%d\t%63[^\t]\t%d\t%d", &b->civ, &b->building, &b->button, name, &b->icon, &b->help) == 6) {
                lstrcpynA(b->name, name, sizeof b->name);
                g_nlbtn++;
            }
        }
        fclose(f);
    }
    if (ns || g_nlbtn) wl_log("civs: %d command string(s), %d Lua button(s)", ns, g_nlbtn);
}

static const char *command_string(int n) {
    // rich texts like the unique units: name + 21000 (tech tree) and create/research + 20000
    // (button rollover) show the triple's third string; help fields are 79000 + id
    if (n >= CMD_SID_FIRST + 21000 && n <= CMD_SID_LAST + 21000 && (n - 21000 - CMD_SID_FIRST) % 3 == 0) n = n - 21000 + 2;
    else if (n >= CMD_SID_FIRST + 20000 && n <= CMD_SID_LAST + 20000 && (n - 20000 - CMD_SID_FIRST) % 3 == 1) n = n - 20000 + 1;
    else if (n >= CMD_SID_FIRST + 79000 && n <= CMD_SID_LAST + 79000) n -= 79000;
    else if (n >= CMD_SID_FIRST + 150000 && n <= CMD_SID_LAST + 150000) n -= 150000;
    if (n < CMD_SID_FIRST || n > CMD_SID_LAST) return NULL;
    return g_cmd_str[n - CMD_SID_FIRST];
}

static void load_civs_txt(void) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\cache\\civs.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) { wl_log("civs: no cache\\civs.txt (run tools/build.py)"); return; }
    char line[512];
    while (fgets(line, sizeof line, f) && g_civ_count < MAX_CIVS) {
        line[strcspn(line, "\r\n")] = 0;
        char *id = strtok(line, "\t"), *base = strtok(NULL, "\t"), *name = strtok(NULL, "\t");
        if (!id || !base || !name) continue;
        WlCiv *c = &g_civs[g_civ_count];
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
        wl_log("civs: %d '%s' (base %d)%s%s%s%s", c->id, c->name, c->base,
               c->description ? " +description" : "", c->mp3[0] ? " +sound" : "",
               c->slp_path[0] ? " +interface" : "",
               (c->unique_techs[0] >= 0) ? " +unique_techs" : "");
        g_civ_count++;
    }
    fclose(f);
}

// Runs after the game registered Rajas civs; every base civ exists by now.
static void __cdecl hook_register_rajas(void *pkg) {
    wl_civs_ensure();
    g_orig_register_rajas(pkg);
    for (int i = 0; i < g_civ_count; i++) {
        WlCiv *c = &g_civs[i];
        int base_id = c->base;
        BYTE *base = ((CivFind_t)ADDR_CIV_MAP_FIND)((void *)ADDR_CIV_MAP, &base_id);
        if (!base) { wl_log("civs: base civ %d not registered, skip %d", c->base, c->id); continue; }
        // Civilization: +0xc mp3 (char*), +0x14 bonus string base, +0x18/+0x1c tech ids.
        // +0x14 is the AI player name list (count string at the id, names after it).
        const char *mp3 = c->mp3[0] ? c->mp3 : *(const char **)(base + 0xc);
        int ai = c->ai_count ? STR_BASE + c->id * 100 + STR_AI_COUNT : *(int *)(base + 0x14);
        int t1 = (c->unique_techs[0] >= 0) ? c->unique_techs[0] : *(int *)(base + 0x18);
        int t2 = (c->unique_techs[1] >= 0) ? c->unique_techs[1] : *(int *)(base + 0x1c);
        ((RegisterCiv_t)ADDR_REGISTER_CIV)(pkg, c->id, mp3, STR_BASE + c->id * 100 + STR_NAME, ai, t1, t2, 1);
        wl_log("civs: registered %d (mp3 %s, techs %d,%d, pkg %p)", c->id, mp3 ? mp3 : "?", t1, t2, pkg);
    }
}

// The original only maps ids 1..31 through the registry; custom ids return -2.
static int __fastcall hook_civ_name_id(void *self, void *edx, int id) {
    (void)edx;
    if (find_civ(id)) return STR_BASE + id * 100 + STR_NAME;
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
    // Wololoader's own UI strings (buildpage.c "Mod Buildings" button help, id 28490).
    if (key && strcmp(key, "28490") == 0) return "<b>Mod Buildings<b> \nDisplay the buildings added by mods.";
    if (key && key[0] >= '1' && key[0] <= '9') {
        const char *cs = command_string(atoi(key));
        if (cs) return cs;
    }
    if (key && key[0] >= '1' && key[0] <= '9' && g_civ_count) {
        int n = atoi(key);
        // Custom unique unit strings: SID(slot) = 28500 + (civ_id - 35) * 20 + slot; SID = 28500 + (civ-35)*20 + slot: the game reads unit/tech name ids as SIGNED int16, so ids must be < 32768 (55000 showed the internal name); 28440..30100 is unused in the string files
        // Slots 0..9: unit names, creation tooltips, help, research names/tooltips.
        // Tech tree screen asks for name id + 21000 (seen in the log: 49500,
        // 49503) for the box tooltip; the box name seems to come from that text
        // too (it fell back to the internal name "MONKX" without it).
        // Map our name slots 0 / 3 to their rich texts in slots 2 / 5.
        if (n >= 28500 + 21000 && n < 30100 + 21000) {
            int s = (n - 21000 - 28500) % 20;
            if (s == 0 || s == 3 || s == 10) n = n - 21000 + 2;
        }
        // Unit button rollover asks for creation id + 20000 (Teutonic Knight
        // 6112 -> 26112). Seen in the log for our unit: 28501 then 48501.
        // Map our creation slots 1 / 4 to their rich texts in slots 2 / 5.
        if (n >= 28500 + 20000 && n < 30100 + 20000) {
            int s = (n - 20000 - 28500) % 20;
            if (s == 1 || s == 4 || s == 11) n = n - 20000 + 1;
        }
        // Help field (79000 + id) in case some screen looks it up directly.
        if (n >= 28500 + 79000 && n < 30100 + 79000) n -= 79000;
        if (n >= 28500 && n < 30100) {
            int cid = 35 + (n - 28500) / 20;
            int slot = (n - 28500) % 20;
            if (slot >= 0 && slot < 20) {
                const WlCiv *c = find_civ(cid);
                if (c && c->uu_strings[slot]) return c->uu_strings[slot];
            }
        }
        if (n >= STR_BASE && n < STR_BASE + 100000) {
            const WlCiv *c = find_civ((n - STR_BASE) / 100);
            int slot = (n - STR_BASE) % 100;
            if (c && slot == STR_NAME) return c->name;
            if (c && slot == STR_AI_COUNT) return c->ai_count_str;
            if (c && slot > STR_AI_COUNT && slot <= STR_AI_COUNT + c->ai_count)
                return c->ai_names[slot - STR_AI_COUNT - 1];
        }
        int d = n - DESC_BASE_DLC;
        if (d < FIRST_CUSTOM_CIV) d = n - DESC_BASE_GAME;
        if (d >= FIRST_CUSTOM_CIV && d < FIRST_CUSTOM_CIV + MAX_CIVS) {
            const WlCiv *c = find_civ(d);
            if (c && c->description) return c->description;
        }
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
        wl_autobuild();
        load_civs_txt();
        load_command_files();
        // Mods are scanned (DllMain) and built by now; Lua is created here, lazily,
        // because its CRT/allocator use must not run inside DllMain (AGENTS.md rule 5).
        wl_script_init();
        InterlockedExchange(&g_civs_state, 2);
        return;
    }
    if (g_civs_thread == GetCurrentThreadId()) return;  // re-entered from the build itself
    while (g_civs_state != 2) Sleep(1);
}

// Vanilla Mode: only the string lookup hook, for the main-menu button name
// ("Mods" instead of "Steam Workshop") and the startup title. No custom civs are
// registered then (g_civ_count == 0), so no game text changes.
void wl_install_string_hook_only(void) {
    if (g_orig_lookup) return;
    g_orig_lookup = (StringLookup_t)wl_hook(ADDR_STRING_LOOKUP, hook_string_lookup, 5);
}

void wl_install_civ_hooks(void) {
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
    const WlCiv *c = find_civ(id);
    if (!c) return id;
    return c->slp_path[0] ? id : c->base;
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
    for (int i = 0; i < g_civ_count; i++)
        if (g_civs[i].id > n) n = g_civs[i].id;
    return n;
}

// AI script symbols (0x4f5960): the game defines BRITON-CIV ... VIETNAMESE-CIV
// for each player through a jump table on the civ id (ids above 0x1f are
// skipped). AI scripts such as Promisory define their constants inside
// "#load-if-defined <CIV>-CIV" blocks, so a civ without a symbol fails with
// "ERR2004: Missing identifier". Custom civs therefore play as their base civ.
int __cdecl wl_ai_civ_id(int id) {
    const WlCiv *c = find_civ(id);
    return c ? c->base : id;
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
