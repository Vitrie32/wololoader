// Custom resources: runtime registry loaded from cache\resources.txt, plus
// engine hooks for:
//   1. Tooltip <cost> expansion (0x773f70 parse_markup)
//   2. "Not enough <Name>." & resource name strings (0x62f430 Game::get_string)
//   3. Optional max cap enforcement (0x74cfe0 WorldPlayerBase::add_attribute_num)
//
// All addresses verified against AoK HD.exe 5.8 via i686-w64-mingw32-objdump:
// - 0x773f70 parse_markup: __cdecl(void *out, const char *raw_text, int id, char is_tech),
//   plain ret (0x774f06), returns `out` in eax (0x774ee6: mov eax, [ebp-0x98]).
//   Prologue (5 bytes, no rel jumps): `55 8b ec 6a ff` (push ebp; mov ebp,esp; push -1).
//   String table: *(void**)0xad13e0, lookup 0x448370 __thiscall(table, const char *key), ret 4
//   (0x7743c2..0x7743d0).
// - 0x540c90 Game::get_local_player: __thiscall(game), plain ret (0x540cb0).
// - 0x84e670 TechTree::get_tech_cost: __thiscall(tt, short id, short *t0, short *a0,
//   short *t1, short *a1, short *t2, short *a2), ret 0x1c (0x84e86b).
// - 0x84e870 TechData::get_tech_cost: same signature, ret 0x1c (0x84e944).
// - 0x62f430 Game::get_string: __thiscall(game, int cat, int sub, int arg, char *buf, int sz),
//   ret 0x14 (0x62fcc3), returns `buf` in eax.
//   Prologue (5 bytes, no rel jumps): `55 8b ec 53 56` (push ebp; mov ebp,esp; push ebx; push esi).
//   case 0x67, sub==1: "Not enough <resource>." (arg = missing resource id).
//   case 100 (0x64): resource display name (sub = resource id).
// - 0x74cfe0 WorldPlayerBase::add_attribute_num: __thiscall(player, short id, float amount, int earned),
//   ret 0xc (0x74d0b0).
//   Prologue (8 bytes, no rel jumps): `55 8b ec 56 66 8b 75 08` (push ebp; mov ebp,esp; push esi; mov si,[ebp+8]).
#include "wololoader.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDP(p, o) (*(void **)((char *)(p) + (o)))
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))

#define ADDR_PARSE_MARKUP    0x773f70
#define ADDR_GAME_GET_STRING 0x62f430
#define ADDR_ADD_ATTR_BASE   0x74cfe0
#define ADDR_GET_LOCAL_PLR   0x540c90
#define ADDR_TT_GET_COST     0x84e670
#define ADDR_TD_GET_COST     0x84e870
#define ADDR_STRING_TABLE    0xad13e0
#define ADDR_STRING_LOOKUP   0x448370

typedef void *(__cdecl *ParseMarkup_t)(void *out, const char *raw_text, int id, char is_tech);
typedef char *(__thiscall *GetString_t)(void *game, int cat, int sub, int arg, char *buf, int buf_sz);
typedef void (__thiscall *AddAttrBase_t)(void *player, short id, float amount, int earned);
typedef void *(__thiscall *GetLocalPlayer_t)(void *game);
typedef void (__thiscall *GetTechCost_t)(void *obj, short tech_id,
                                         short *t0, short *a0, short *t1, short *a1, short *t2, short *a2);
typedef const char *(__thiscall *StrLookup_t)(void *table, const char *key);

static ParseMarkup_t o_parse_markup;
static GetString_t   o_get_string;
static AddAttrBase_t o_add_attr;

static WlResourceDef *g_res;
static int g_res_count;
static int g_res_cap;
static WlGatherDef *g_gather;
static int g_gather_count;
static int g_gather_cap;
static int g_res_loaded;
static int g_res_loading;
static char g_res_stamp[64];

static int ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return *a == *b;
}

static void unescape_desc(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    for (const char *p = src; *p && w + 1 < cap; p++) {
        if (p[0] == '\\' && p[1] == 'n') {
            dst[w++] = '\n';
            p++;
        } else {
            dst[w++] = *p;
        }
    }
    dst[w] = '\0';
}

static void read_stamp(char out[64]) {
    out[0] = '\0';
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\cache\\build.stamp", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (!fgets(out, 64, f)) out[0] = '\0';
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
}

static void load_gather_txt(void) {
    g_gather_count = 0;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\cache\\gather.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) {
        wl_files_sync_rms_defs(NULL, 0);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        // Columns: key \t resource_id \t deposit_id \t male_id \t female_id
        char *cols[5] = { NULL };
        int ncol = 0;
        char *p = line;
        while (ncol < 5) {
            cols[ncol++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        if (ncol < 5 || !cols[0][0]) continue;
        int rid = atoi(cols[1]);
        int dep = atoi(cols[2]);
        int mal = atoi(cols[3]);
        int fem = atoi(cols[4]);
        if (rid < WL_CUSTOM_RES_FIRST || rid > WL_CUSTOM_RES_MAX) continue;
        if (dep <= 0 || mal <= 0 || fem <= 0) continue;

        if (g_gather_count >= g_gather_cap) {
            int ncap = g_gather_cap ? g_gather_cap * 2 : 16;
            WlGatherDef *ng = (WlGatherDef *)realloc(g_gather, (size_t)ncap * sizeof(*ng));
            if (!ng) break;
            g_gather = ng;
            g_gather_cap = ncap;
        }
        WlGatherDef *g = &g_gather[g_gather_count++];
        memset(g, 0, sizeof(*g));
        lstrcpynA(g->key, cols[0], sizeof g->key);
        g->resource_id = rid;
        g->deposit_id  = dep;
        g->male_id     = mal;
        g->female_id   = fem;
    }
    fclose(f);
    if (g_gather_count > 0) {
        wl_log("resources: loaded %d gatherable resource(s) from cache\\gather.txt", g_gather_count);
    }
    wl_files_sync_rms_defs(g_gather, g_gather_count);
}

static void load_resources_txt(void) {
    g_res_count = 0;
    read_stamp(g_res_stamp);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\cache\\resources.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) {
        load_gather_txt();
        g_res_loaded = 1;
        return;
    }
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        // Columns: id \t mod \t key \t name \t icon [\t start \t max \t show \t desc \t hidden \t game_mode]
        char *cols[11] = { NULL };
        int ncol = 0;
        char *p = line;
        while (ncol < 11) {
            cols[ncol++] = p;
            char *tab = strchr(p, '\t');
            if (!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        if (ncol < 5) continue;
        int id = atoi(cols[0]);
        if (id < WL_CUSTOM_RES_FIRST || id > WL_CUSTOM_RES_MAX) continue;

        if (g_res_count >= g_res_cap) {
            int ncap = g_res_cap ? g_res_cap * 2 : 16;
            WlResourceDef *nr = (WlResourceDef *)realloc(g_res, (size_t)ncap * sizeof(*nr));
            if (!nr) break;
            g_res = nr;
            g_res_cap = ncap;
        }
        WlResourceDef *r = &g_res[g_res_count++];
        memset(r, 0, sizeof(*r));
        r->id = id;
        lstrcpynA(r->mod, cols[1], sizeof r->mod);
        lstrcpynA(r->key, cols[2], sizeof r->key);
        lstrcpynA(r->name, cols[3][0] ? cols[3] : cols[2], sizeof r->name);
        lstrcpynA(r->icon_rel, cols[4], sizeof r->icon_rel);
        if (r->icon_rel[0]) {
            snprintf(r->icon, sizeof r->icon, "%s\\mods\\%s\\%s", g_wl.root, r->mod, r->icon_rel);
            for (char *c = r->icon; *c; c++) if (*c == '/') *c = '\\';
        }
        r->start = (ncol >= 6 && cols[5][0]) ? (float)atof(cols[5]) : 0.0f;
        r->max   = (ncol >= 7 && cols[6][0]) ? (float)atof(cols[6]) : 0.0f;
        r->show  = (ncol >= 8 && cols[7][0]) ? (atoi(cols[7]) != 0) : 1;
        if (ncol >= 9 && cols[8][0]) unescape_desc(cols[8], r->description, sizeof r->description);
        r->hidden = (ncol >= 10 && cols[9][0]) ? (atoi(cols[9]) != 0) : 0;
        if (ncol >= 11 && cols[10][0]) lstrcpynA(r->game_mode, cols[10], sizeof r->game_mode);
    }
    fclose(f);
    wl_log("resources: loaded %d custom resource(s) from cache\\resources.txt", g_res_count);
    load_gather_txt();
    g_res_loaded = 1;
}

void wl_resources_reload(void) {
    if (g_res_loading) return;
    g_res_loading = 1;
    load_resources_txt();
    g_res_loading = 0;
    wl_resbar_reset();
}

void wl_resources_ensure(void) {
    if (g_res_loading) return;
    wl_civs_ensure();
    if (!g_res_loaded) {
        g_res_loading = 1;
        load_resources_txt();
        g_res_loading = 0;
        return;
    }
    // Detect cache rebuilds (e.g. after main-menu hot reload) via build.stamp.
    char cur_stamp[64];
    read_stamp(cur_stamp);
    if (strcmp(cur_stamp, g_res_stamp) != 0) {
        g_res_loading = 1;
        load_resources_txt();
        g_res_loading = 0;
        wl_resbar_reset();
    }
}

const WlResourceDef *wl_resources_list(int *out_count) {
    wl_resources_ensure();
    if (out_count) *out_count = g_res_count;
    return g_res;
}

const WlResourceDef *wl_resource_by_id(int id) {
    if (id < WL_CUSTOM_RES_FIRST) return NULL;
    if (!g_res_loaded) wl_resources_ensure();
    int idx = id - WL_CUSTOM_RES_FIRST;
    if (idx >= 0 && idx < g_res_count && g_res[idx].id == id) return &g_res[idx];
    for (int i = 0; i < g_res_count; i++) {
        if (g_res[i].id == id) return &g_res[i];
    }
    return NULL;
}

int wl_resource_resolve(const char *key, const char *prefer_mod) {
    if (!key || !key[0]) return -1;
    if (ci_eq(key, "food"))  return 0;
    if (ci_eq(key, "wood"))  return 1;
    if (ci_eq(key, "stone")) return 2;
    if (ci_eq(key, "gold"))  return 3;

    wl_resources_ensure();
    const char *colon = strchr(key, ':');
    if (colon) {
        size_t mlen = (size_t)(colon - key);
        const char *kpart = colon + 1;
        for (int i = 0; i < g_res_count; i++) {
            if (strlen(g_res[i].mod) == mlen && strncmp(g_res[i].mod, key, mlen) == 0 &&
                strcmp(g_res[i].key, kpart) == 0)
                return g_res[i].id;
        }
        return -1;
    }
    if (prefer_mod && prefer_mod[0]) {
        for (int i = 0; i < g_res_count; i++) {
            if (strcmp(g_res[i].mod, prefer_mod) == 0 && strcmp(g_res[i].key, key) == 0)
                return g_res[i].id;
        }
    }
    for (int i = 0; i < g_res_count; i++) {
        if (strcmp(g_res[i].key, key) == 0) return g_res[i].id;
    }
    // Case-insensitive fallback on key or display name for Lua ergonomics
    if (prefer_mod && prefer_mod[0]) {
        for (int i = 0; i < g_res_count; i++) {
            if (strcmp(g_res[i].mod, prefer_mod) == 0 && (ci_eq(g_res[i].key, key) || ci_eq(g_res[i].name, key)))
                return g_res[i].id;
        }
    }
    for (int i = 0; i < g_res_count; i++) {
        if (ci_eq(g_res[i].key, key) || ci_eq(g_res[i].name, key)) return g_res[i].id;
    }
    return -1;
}

const WlGatherDef *wl_gather_list(int *out_count) {
    wl_resources_ensure();
    if (out_count) *out_count = g_gather_count;
    return g_gather;
}

const WlGatherDef *wl_gather_by_resource(int resource_id) {
    if (resource_id < WL_CUSTOM_RES_FIRST) return NULL;
    if (!g_res_loaded) wl_resources_ensure();
    for (int i = 0; i < g_gather_count; i++) {
        if (g_gather[i].resource_id == resource_id) return &g_gather[i];
    }
    return NULL;
}

const WlGatherDef *wl_gather_by_deposit(int deposit_id) {
    if (deposit_id <= 0) return NULL;
    if (!g_res_loaded) wl_resources_ensure();
    for (int i = 0; i < g_gather_count; i++) {
        if (g_gather[i].deposit_id == deposit_id) return &g_gather[i];
    }
    return NULL;
}

const WlGatherDef *wl_gather_by_key(const char *key, const char *prefer_mod) {
    if (!key || !key[0]) return NULL;
    wl_resources_ensure();
    // Resolve through resources.txt first so "mod:key", prefer_mod, and display-name aliases work.
    int rid = wl_resource_resolve(key, prefer_mod);
    if (rid >= WL_CUSTOM_RES_FIRST) {
        const WlGatherDef *g = wl_gather_by_resource(rid);
        if (g) return g;
    }
    // Direct fallback on gather.txt key (exact, then case-insensitive).
    for (int i = 0; i < g_gather_count; i++) {
        if (strcmp(g_gather[i].key, key) == 0) return &g_gather[i];
    }
    for (int i = 0; i < g_gather_count; i++) {
        if (ci_eq(g_gather[i].key, key)) return &g_gather[i];
    }
    return NULL;
}

// ---- Hook 1: Game::get_string (0x62f430) ---------------------------------------------
// Handles "Not enough <Name>." (cat 0x67, sub 1, arg >= 211) and resource name lookup
// (cat 100, sub >= 211).
static char *__fastcall hook_get_string(void *self, void *edx, int cat, int sub, int arg, char *buf, int buf_sz) {
    (void)edx;
    if (buf && buf_sz > 0) {
        if (cat == 0x67 && sub == 1 && arg >= WL_CUSTOM_RES_FIRST) {
            const WlResourceDef *r = wl_resource_by_id(arg);
            if (r) {
                snprintf(buf, (size_t)buf_sz, "Not enough %s.", r->name);
                return buf;
            }
        } else if (cat == 100 && sub >= WL_CUSTOM_RES_FIRST) {
            const WlResourceDef *r = wl_resource_by_id(sub);
            if (r) {
                snprintf(buf, (size_t)buf_sz, "%s", r->name);
                return buf;
            }
        }
    }
    return o_get_string(self, cat, sub, arg, buf, buf_sz);
}

// ---- Hook 2: parse_markup (0x773f70) -------------------------------------------------
// Pre-expands <cost> when a unit or tech has any custom resource cost (>= 211),
// preserving vanilla localization for "Cost: " and Food/Wood/Stone/Gold, and letting
// o_parse_markup process all other markup tags (<B>, <hp>, <attack>, etc.) natively.
static const char *find_cost_tag(const char *s) {
    if (!s) return NULL;
    for (; *s; s++) {
        if (s[0] == '<' &&
            tolower((unsigned char)s[1]) == 'c' &&
            tolower((unsigned char)s[2]) == 'o' &&
            tolower((unsigned char)s[3]) == 's' &&
            tolower((unsigned char)s[4]) == 't' &&
            s[5] == '>') {
            return s;
        }
    }
    return NULL;
}

static const char *lookup_game_str(int sid, const char *fallback) {
    void *tbl = *(void **)ADDR_STRING_TABLE;
    if (!tbl) return fallback;
    char key[16];
    snprintf(key, sizeof key, "%d", sid);
    const char *s = ((StrLookup_t)ADDR_STRING_LOOKUP)(tbl, key);
    return (s && s[0]) ? s : fallback;
}

static void *__cdecl hook_parse_markup(void *out, const char *raw_text, int id, char is_tech) {
    if (!raw_text || !find_cost_tag(raw_text)) {
        return o_parse_markup(out, raw_text, id, is_tech);
    }
    if (!g_res_loaded) wl_resources_ensure();
    if (!g_res_count) {
        return o_parse_markup(out, raw_text, id, is_tech);
    }

    short t[3] = { -1, -1, -1 };
    short a[3] = { 0, 0, 0 };
    short en[3] = { 0, 0, 0 };

    void *game = *(void **)0xaf2c58;
    void *world = game ? RDP(game, 0xc8) : NULL;
    void *player = (game && world) ? ((GetLocalPlayer_t)ADDR_GET_LOCAL_PLR)(game) : NULL;

    if (is_tech == 1) {
        if (player) {
            void *tt = RDP(player, 0x1ae8);
            if (tt && id >= 0 && id < *(short *)((char *)tt + 4)) {
                ((GetTechCost_t)ADDR_TT_GET_COST)(tt, (short)id, &t[0], &a[0], &t[1], &a[1], &t[2], &a[2]);
                en[0] = en[1] = en[2] = 1;
            }
        } else if (world) {
            void *td = RDP(world, 0x204);
            if (td && id >= 0 && id < *(short *)((char *)td + 4)) {
                ((GetTechCost_t)ADDR_TD_GET_COST)(td, (short)id, &t[0], &a[0], &t[1], &a[1], &t[2], &a[2]);
                en[0] = en[1] = en[2] = 1;
            }
        }
    } else {
        void *master = NULL;
        if (player && id >= 0 && id < *(int *)((char *)player + 0x10)) {
            void **masters = (void **)RDP(player, 0x14);
            if (masters) master = masters[id];
        } else if (world && RDP(world, 0xb0)) {
            void **mps = (void **)RDP(world, 0xb0);
            void *mp0 = mps ? mps[0] : NULL;
            if (mp0 && id >= 0 && id < *(short *)((char *)mp0 + 0x18)) {
                void **masters = (void **)RDP(mp0, 0x1c);
                if (masters) master = masters[id];
            }
        }
        if (master) {
            unsigned char utype = *(unsigned char *)((char *)master + 4);
            if (utype == 70 || utype == 80) {
                const short *c = (const short *)((char *)master + 0x18c);
                for (int i = 0; i < 3; i++) {
                    t[i]  = c[i * 3 + 0];
                    a[i]  = c[i * 3 + 1];
                    en[i] = c[i * 3 + 2];
                }
            }
        }
    }

    int has_custom = 0;
    for (int i = 0; i < 3; i++) {
        if (t[i] >= WL_CUSTOM_RES_FIRST && a[i] > 0 && en[i] != 0) {
            has_custom = 1;
            break;
        }
    }
    if (!has_custom) {
        return o_parse_markup(out, raw_text, id, is_tech);
    }

    // Build localized "Cost: <vanilla>, <custom>" string matching 0x773f70's order.
    int vanilla[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        if (t[i] >= 0 && t[i] <= 3 && a[i] > 0) vanilla[t[i]] += a[i];
    }
    static const int k_vsid[4] = { 4301, 4302, 4303, 4304 };
    static const char *const k_vfb[4] = { "Food", "Wood", "Stone", "Gold" };

    char cost_str[512];
    size_t cpos = (size_t)snprintf(cost_str, sizeof cost_str, "%s", lookup_game_str(20200, "Cost: "));
    int first = 1;
    for (int v = 0; v < 4; v++) {
        if (vanilla[v] <= 0) continue;
        cpos += (size_t)snprintf(cost_str + cpos, sizeof cost_str - cpos, "%s%d %s",
                                 first ? "" : ", ", vanilla[v], lookup_game_str(k_vsid[v], k_vfb[v]));
        first = 0;
    }
    for (int i = 0; i < 3; i++) {
        if (t[i] >= WL_CUSTOM_RES_FIRST && a[i] > 0 && en[i] != 0) {
            const WlResourceDef *r = wl_resource_by_id(t[i]);
            const char *rname = r ? r->name : "Resource";
            cpos += (size_t)snprintf(cost_str + cpos, sizeof cost_str - cpos, "%s%d %s",
                                     first ? "" : ", ", (int)a[i], rname);
            first = 0;
        }
    }

    // Substitute every <cost> tag in raw_text into a stack buffer.
    char patched[4096];
    size_t w = 0;
    const char *p = raw_text;
    while (*p) {
        const char *tag = find_cost_tag(p);
        if (!tag) {
            size_t rem = strlen(p);
            if (w + rem >= sizeof patched) return o_parse_markup(out, raw_text, id, is_tech);
            memcpy(patched + w, p, rem + 1);
            w += rem;
            break;
        }
        size_t chunk = (size_t)(tag - p);
        size_t clen = strlen(cost_str);
        if (w + chunk + clen >= sizeof patched) return o_parse_markup(out, raw_text, id, is_tech);
        memcpy(patched + w, p, chunk);
        w += chunk;
        memcpy(patched + w, cost_str, clen);
        w += clen;
        p = tag + 6;
    }
    patched[w] = '\0';
    return o_parse_markup(out, patched, id, is_tech);
}

// ---- Hook 3: WorldPlayerBase::add_attribute_num (0x74cfe0) ---------------------------
// Enforces custom resource `max` cap (> 0) deterministically across all sources
// (villager gathering, passive generation, tech effects, Lua commands).
static void __fastcall hook_add_attr(void *player, void *edx, short id, float amount, int earned) {
    (void)edx;
    o_add_attr(player, id, amount, earned);
    if (id >= WL_CUSTOM_RES_FIRST && player) {
        const WlResourceDef *r = wl_resource_by_id(id);
        if (r && r->max > 0.0f) {
            int count = *(int *)((char *)player + 0x38);
            float *arr = (float *)RDP(player, 0x3c);
            if (arr && id < count && arr[id] > r->max) {
                arr[id] = r->max;
            }
        }
    }
}

void wl_install_resources(void) {
    static const BYTE b_markup[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_getstr[] = { 0x55, 0x8b, 0xec, 0x53, 0x56 };
    static const BYTE b_addatr[] = { 0x55, 0x8b, 0xec, 0x56, 0x66, 0x8b, 0x75, 0x08 };

    if (memcmp((const void *)ADDR_PARSE_MARKUP, b_markup, sizeof b_markup) == 0) {
        o_parse_markup = (ParseMarkup_t)wl_hook(ADDR_PARSE_MARKUP, hook_parse_markup, 5);
    } else {
        wl_log("resources: unexpected bytes at 0x773f70, <cost> tooltip hook disabled");
    }

    if (memcmp((const void *)ADDR_GAME_GET_STRING, b_getstr, sizeof b_getstr) == 0) {
        o_get_string = (GetString_t)wl_hook(ADDR_GAME_GET_STRING, hook_get_string, 5);
    } else {
        wl_log("resources: unexpected bytes at 0x62f430, resource message hook disabled");
    }

    if (memcmp((const void *)ADDR_ADD_ATTR_BASE, b_addatr, sizeof b_addatr) == 0) {
        o_add_attr = (AddAttrBase_t)wl_hook(ADDR_ADD_ATTR_BASE, hook_add_attr, 8);
    } else {
        wl_log("resources: unexpected bytes at 0x74cfe0, resource max-cap hook disabled");
    }

    wl_log("resources: hooks installed (markup=%s, get_string=%s, add_attr=%s)",
           o_parse_markup ? "ok" : "off",
           o_get_string ? "ok" : "off",
           o_add_attr ? "ok" : "off");
}
