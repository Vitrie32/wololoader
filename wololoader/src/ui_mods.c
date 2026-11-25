// In-game "Mods" screen, opened from the main menu's Steam Workshop button.
// Layout (v0.5): filter tabs + paged mod list on the left, a detail pane on the
// right (header with icon, name, Enable/Disable; civ names, contents, source,
// description; per-mod Open Folder / Edit in Scriptorium), one row of
// screen-wide buttons at the bottom.
//
// The screen is drawn from static state (filter, page, selected mod). Changing
// the filter or page rebuilds the whole screen (rebuild_screen), the same path
// Reload Mods and the Mod Hub return already use, so only the current page's
// icons are ever loaded.
// Addresses: AoK HD.exe 5.8, see re/FINDINGS.md ("Mods screen v2").
#include "wololoader.h"
#include "hubcore.h"
#include "modsettings.h"
#include "datbuild.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_MODMGR_HANDLER       0x6e9380  // __fastcall(MainMenuScreen*): opens ModManagerScreen
#define ADDR_GAME_NEW             0x8d47df  // game's operator new (cdecl)
#define ADDR_SCREEN_CTOR          0x489d50  // ScreenPanel(this, const char *name), ret 4
#define SCREENPANEL_VTBL          0x9b24ac  // 59 slots
#define SCREENPANEL_SIZE          0x27c
#define VT_SLOTS                  59
#define VT_EVENT                  49        // +0xc4: (panel, code, a, b), ret 0x10
#define ADDR_SCREEN_MGR           0xada4b4  // [ptr] screen list
#define ADDR_SET_CURRENT          0x48a8e0  // thiscall(mgr, screen), ret 4
#define ADDR_SET_CURRENT_BY_NAME  0x48a930  // thiscall(mgr, const char*), ret 4
#define ADDR_SET_BACKGROUND       0x48a1c0  // thiscall(scr, name, id, 0), ret 0xc
#define ADDR_SET_REF_SIZE         0x491250  // thiscall(scr, int wh[2]), ret 4
#define ADDR_ADD_BUTTON           0x48df30  // thiscall, ret 0x28
#define ADDR_ADD_LABEL            0x48fe60  // thiscall, ret 0x2c
#define ADDR_SET_TEXT_STYLE       0x48d390
#define ADDR_SET_TEXT_FLAG        0x48d3c0
#define ADDR_APPLY_STYLE          0x48d550
#define ADDR_STYLE_PAIR           0xada500

#define ADDR_BUTTON_SET_TEXT      0x479ce0  // thiscall(btn, int slot, const char *text), ret 8
#define ADDR_LOAD_PNG             0x49f1f0  // thiscall(chain, &out, path), ret 8
#define ADDR_LOADER_CHAIN         0xada9f4  // [ptr] ui::ImageFileDrawableLoader
#define ADDR_SCALE_RECT           0x491190  // thiscall(scr, out[4], in[4]), ret 8
#define ADDR_DRAWABLE_PANEL_CTOR  0x47a5d0  // thiscall(mem, parent, rect[4], drawable), ret 0xc
#define DRAWABLE_PANEL_SIZE       0x134

#define EV_LEFT_CLICK             3
#define REF_W                     1366
#define REF_H                     768

// Layout inside the parchment of the scr2p background (reference units).
// The parchment's usable area is about x 225..1140, y 15..600.
#define LIST_X      225
#define LIST_W      340
#define TABS_Y      72
#define TAB_W       108
#define TAB_H       32
#define ROWS_Y      114
#define ROW_STEP    50
#define ROW_H       44
#define PAGE_SIZE   8
#define PAGER_Y     (ROWS_Y + PAGE_SIZE * ROW_STEP + 4)   // 518
#define STATUS_Y    560

#define DET_X       610
#define DET_W       530
#define HEAD_Y      72      // detail icon top
#define TEXT_X      (DET_X + 112)
#define TEXT_W      (DET_W - 112)
#define INFO_Y      196
#define INFO_STEP   24
#define INFO_LINES  6
#define DESC_Y      (INFO_Y + INFO_LINES * INFO_STEP + 12)  // 352
#define SET_ROWS    7       // settings view: rows per page
#define SET_STEP    42
#define ACTIONS_Y   518
#define DESC_CHARS  96      // font 0xa is about 5 reference units per character
#define INFO_CHARS  100

#define BAR_Y       654     // bottom button row (below the parchment)
#define BAR_H       40

enum { FILTER_ALL, FILTER_ON, FILTER_OFF, FILTER_COUNT };

typedef void *(__cdecl *New_t)(unsigned size);
typedef void *(__thiscall *ScreenCtor_t)(void *self, const char *name);
typedef void (__thiscall *SetCurrent_t)(void *mgr, void *screen);
typedef void (__thiscall *SetCurrentByName_t)(void *mgr, const char *name);
typedef void (__thiscall *SetBg_t)(void *scr, const char *name, int id, int z);
typedef void (__thiscall *SetRef_t)(void *scr, int *wh);
typedef void *(__thiscall *AddButton_t)(void *scr, void *parent, const char *text, const char *text2,
                                         int x, int y, int w, int h, int a, int b, int c);
typedef char (__thiscall *AddLabel_t)(void *scr, void *parent, void **out, const char *text,
                                       int x, int y, int w, int h, int font, int f24, int f28, int f2c);
typedef void (__thiscall *SetTextStyle_t)(void *scr, DWORD *a, DWORD *b);
typedef void (__thiscall *SetTextFlag_t)(void *scr, int flag);
typedef void (__thiscall *ApplyStyle_t)(void *scr, void *panel);
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);

typedef void (__thiscall *ButtonSetText_t)(void *btn, int slot, const char *text);
typedef void (__thiscall *TextPanelSetText_t)(void *panel, const char *text, int z0, int z1, int z2);
typedef void (__thiscall *LoadPng_t)(void *chain, void **out, const char *path);
typedef void (__thiscall *DrawableSetFit_t)(void *drawable, const int *mode);
typedef void *(__thiscall *DrawablePanelCtor_t)(void *mem, void *parent, const int rect[4], void *drawable);
typedef void *(__thiscall *ScaleRect_t)(void *screen, int out[4], const int in[4]);

static void *g_screen;
static int g_screen_ro;              // g_screen was built as the read-only lobby view
// [0] = RTTI CompleteObjectLocator (dynamic_cast reads vtable[-1]), then slots.
static void *g_vtable_block[VT_SLOTS + 1];
#define g_vtable (g_vtable_block + 1)
static Event_t g_orig_event;

// ---- state that survives rebuilds ----
static int g_filter = FILTER_ALL;
static int g_page;
static int g_selected_mod = -1;      // index into g_wl.mods
static int g_mods_dirty;             // 1 when mods changed on this screen or in Mod Hub
static void *g_return_screen;        // set while the read-only lobby view is open
static char g_note[600];             // message line under the detail pane
static int g_settings_mode;          // detail pane shows the selected mod's settings
static int g_set_page;

// Settings of the selected mod (modsettings.c), loaded on every rebuild.
static WlSetting g_set[WL_MAX_SETTINGS];
static int g_set_count;

// Conflicts between enabled mods, computed on every rebuild (compute_conflicts).
static int g_conf_files[WL_MAX_MODS];    // resources\ files another enabled mod also ships
static int g_conf_partner[WL_MAX_MODS];  // one of those other mods (mod index), -1 = none
static int g_conf_extend[WL_MAX_MODS];   // base civ (1..31) that another mod also extends, 0 = none
static int g_conf_extend_partner[WL_MAX_MODS];

// ---- the current screen's panels ----
static int g_view[WL_MAX_MODS];      // mod indices that pass the filter
static int g_view_count;
static int g_row_mod[PAGE_SIZE];     // mod index of each visible row, -1 = none
static void *g_row_btns[PAGE_SIZE];
static void *g_row_badge_on[PAGE_SIZE];
static void *g_row_badge_off[PAGE_SIZE];
static void *g_tab_btns[FILTER_COUNT];
static void *g_btn_prev, *g_btn_next;

static void *g_btn_toggle;           // detail header: Enable / Disable
static void *g_icon_toggle_on, *g_icon_toggle_off;
static void *g_btn_mod_folder;       // detail: this mod's folder
static void *g_btn_mod_edit;         // detail: Scriptorium on this mod
static void *g_icon_mod_folder, *g_icon_mod_edit;
static void *g_btn_settings;         // detail: opens the settings view
static void *g_btn_update;           // detail: newer version in the Mod Hub
static void *g_btn_set_done, *g_btn_set_defaults, *g_btn_set_prev, *g_btn_set_next;
static void *g_set_btn[SET_ROWS];    // bool / choice: the value button; number: "+"
static void *g_set_minus[SET_ROWS];  // number only
static void *g_set_lbl[SET_ROWS];    // number only: the value
static int g_set_row[SET_ROWS];      // setting index of each row, -1 = none

static void *g_btn_vanilla, *g_btn_creator, *g_btn_open, *g_btn_back, *g_btn_hub, *g_btn_reload;
static void *g_icon_vanilla, *g_icon_creator, *g_icon_folder, *g_icon_back;

static void *g_lbl_ver_author, *g_lbl_state, *g_lbl_status, *g_lbl_note;
static void *g_lbl_info[INFO_LINES];

// Static string buffers (the game keeps the pointers, they must outlive the call)
static char g_row_texts[PAGE_SIZE][192];
static char g_tab_texts[FILTER_COUNT][32];
static char g_page_text[32];
static char g_status_text[128];
static char g_default_icon[MAX_PATH];
static char g_d_name[256], g_d_ver_author[256], g_d_state[96];
static char g_d_info[INFO_LINES][512];
static char g_set_names[SET_ROWS][96];
static char g_set_vals[SET_ROWS][64];
static char g_set_title[160];
static char g_d_desc[1024];

static void *create_screen(void);

static const char *vanilla_text(void) {
    // Shows the state the NEXT start will use (the flag file).
    return wl_vanilla_flag_exists() ? "Vanilla Mode: On" : "Vanilla Mode: Off";
}

static void *add_label(void *s, const char *text, int x, int y, int w, int h,
                       int font, int halign, int valign, int wrap) {
    DWORD pair[2] = { ((DWORD *)ADDR_STYLE_PAIR)[0], ((DWORD *)ADDR_STYLE_PAIR)[1] };
    DWORD pair2[2] = { pair[0], pair[1] };
    ((SetTextStyle_t)ADDR_SET_TEXT_STYLE)(s, pair, pair2);
    ((SetTextFlag_t)ADDR_SET_TEXT_FLAG)(s, 0);
    void *label = NULL;
    ((AddLabel_t)ADDR_ADD_LABEL)(s, s, &label, text ? text : "", x, y, w, h, font, halign, valign, wrap);
    if (label) ((ApplyStyle_t)ADDR_APPLY_STYLE)(s, label);
    return label;
}

static void *add_button(void *s, const char *text, int x, int y, int w, int h) {
    return ((AddButton_t)ADDR_ADD_BUTTON)(s, s, text, NULL, x, y, w, h, 0, 0, 0);
}

static void panel_set_text(void *panel, const char *text) {
    if (!panel) return;
    TextPanelSetText_t fn = (TextPanelSetText_t)(*(void ***)panel)[54];
    fn(panel, text ? text : "", 0, 0, 0);
}

static void button_set_text(void *btn, const char *text) {
    if (btn) ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(btn, 0, text ? text : "");
}

static void panel_set_visible(void *panel, int visible) {
    if (panel) *(int *)((char *)panel + 0x1c) = visible ? 1 : 0;
}

static void *load_png(const char *path) {
    void *chain = *(void **)ADDR_LOADER_CHAIN;
    if (!chain || !path || !path[0]) return NULL;
    void *drawable = NULL;
    ((LoadPng_t)ADDR_LOAD_PNG)(chain, &drawable, path);
    return drawable;
}

// Panel showing a loaded drawable in (x,y,w,h) reference units.
static void *panel_from_drawable(void *s, void *d, int x, int y, int w, int h) {
    int mode3[3] = { 1, 1, 1 };
    ((DrawableSetFit_t)(*(void ***)d)[3])(d, mode3);
    int in_rect[4] = { x, y, w, h };
    int out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(s, out_rect, in_rect);
    void *panel = ((New_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return NULL;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, s, out_rect, d);
    return panel;
}

// Mod icon, pre-scaled by wl_icon_scaled to exactly size x size (iconcache.c).
static void *mod_icon(void *s, const WlMod *m, int x, int y, int size) {
    const char *src = m->icon[0] ? m->icon : g_default_icon;
    const char *path = wl_icon_scaled(src, m->icon[0] ? m->id : "default", size);
    void *d = load_png(path);
    if (!d && m->icon[0]) d = load_png(wl_icon_scaled(g_default_icon, "default", size));
    return d ? panel_from_drawable(s, d, x, y, size, size) : NULL;
}

// Decoration PNG <game>\wololoader\ui\<name>_<w>.png, rendered by make install at
// exactly w x w pixels (sizes 28, 22, 20, 18). Stretching a bigger PNG did not
// work: it was drawn at natural size and clipped (seen in game 2026-11-17).
static void *ui_icon(void *s, const char *name, int x, int y, int w) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\ui\\%s_%d.png", g_wl.root, name, w);
    void *d = load_png(path);
    if (!d) { wl_log("mods: icon %s not loaded", path); return NULL; }
    return panel_from_drawable(s, d, x, y, w, w);
}

// The label's wrap flag has no visible effect, so break lines ourselves.
static void wrap_text(char *out, size_t cap, const char *in, int width) {
    size_t o = 0;
    int col = 0;
    while (*in && o + 2 < cap) {
        const char *w = in;
        while (*w == ' ') w++;
        const char *e = w;
        while (*e && *e != ' ' && *e != '\n') e++;
        int len = (int)(e - w);
        if (col > 0 && col + 1 + len > width) { out[o++] = '\n'; col = 0; }
        else if (col > 0) { out[o++] = ' '; col++; }
        for (const char *c = w; c < e && o + 2 < cap; c++) { out[o++] = *c; col++; }
        in = e;
        if (*in == '\n') { out[o++] = '\n'; col = 0; in++; }
    }
    out[o] = 0;
}

// Cuts s to max chars, ending in "..." when cut.
static void clip(char *s, size_t max) {
    if (strlen(s) > max && max > 3) strcpy(s + max - 3, "...");
}

static int has_scriptorium(void) {
    char exe[MAX_PATH];
    snprintf(exe, MAX_PATH, "%s\\Scriptorium.exe", g_wl.root);
    return GetFileAttributesA(exe) != INVALID_FILE_ATTRIBUTES;
}

static int enabled_count(void) {
    int n = 0;
    for (int i = 0; i < g_wl.listed_count; i++) if (g_wl.mods[i].enabled) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Mod contents (read from disk for the selected mod only)

// Files under dir, recursively; stops counting at 9999.
static int count_files(const char *dir, int depth) {
    if (depth > 12) return 0;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char sub[MAX_PATH];
            snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
            n += count_files(sub, depth + 1);
        } else {
            n++;
        }
    } while (n < 9999 && FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

static int count_glob(const char *dir, const char *pattern) {
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\%s", dir, pattern);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) n++; } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

// "Woodlanders, Monklanians" from civs\*.json "name"; units = custom_units entries.
static void read_civs(const WlMod *m, char *names, size_t cap, int *units) {
    names[0] = 0;
    *units = 0;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\civs\\*.json", m->dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\civs\\%s", m->dir, fd.cFileName);
        size_t len = 0;
        char *text = wl_read_file(path, &len);
        if (!text) continue;
        char err[128];
        WlJsonVal *root = wl_json_parse(text, err, sizeof err);
        free(text);
        char base[MAX_PATH];
        snprintf(base, sizeof base, "%s", fd.cFileName);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        const char *name = root ? wl_json_str(wl_json_get(root, "name"), base) : base;
        size_t used = strlen(names);
        snprintf(names + used, cap - used, "%s%s", used ? ", " : "", name);
        if (root) {
            *units += (int)wl_json_len(wl_json_get(root, "custom_units"));
            wl_json_free(root);
        }
    } while (strlen(names) + 2 < cap && FindNextFileA(h, &fd));
    FindClose(h);
}

static void append_part(char *out, size_t cap, const char *part) {
    size_t used = strlen(out);
    snprintf(out + used, cap - used, "%s%s", used ? ", " : "", part);
}

// "Last Town Standing, Battle Royale" from mod.json "game_modes" array.
static void read_game_modes(const WlMod *m, char *names, size_t cap) {
    names[0] = 0;
    char path[MAX_PATH], err[64] = "";
    snprintf(path, sizeof path, "%s\\mod.json", m->dir);
    char *text = wl_read_file(path, NULL);
    if (!text) return;
    WlJsonVal *root = wl_json_parse(text, err, sizeof err);
    free(text);
    if (!root) return;
    const WlJsonVal *arr = wl_json_get(root, "game_modes");
    size_t n = wl_json_len(arr);
    for (size_t i = 0; i < n; i++) {
        const WlJsonVal *it = wl_json_at(arr, i);
        const char *name = wl_json_str(wl_json_get(it, "name"), NULL);
        if (!name || !name[0]) name = wl_json_str(wl_json_get(it, "id"), NULL);
        if (name && name[0]) append_part(names, cap, name);
    }
    wl_json_free(root);
}

// ---------------------------------------------------------------------------
// Update badges: the Mod Hub index is fetched once in the background (started from the
// main menu, wl_mods_check_updates) and compared with the installed versions.

typedef struct { char id[64]; char version[32]; char requires[32]; } HubVer;
static HubVer g_hubver[256];
static volatile LONG g_hubver_n = -1;      // -1 not fetched yet, >= 0 entries available
static volatile LONG g_hubver_busy;

static DWORD WINAPI hubver_worker(LPVOID arg) {
    (void)arg;
    char url[HUB_PATH_CAP] = "", cfg[HUB_PATH_CAP] = "", err[256] = "";
    int env = 0;
    hub_resolve_index_url(g_wl.game_dir, url, sizeof url, cfg, sizeof cfg, &env);
    WlJsonVal *root = hub_fetch_and_parse_index(url, err, sizeof err);
    if (root) {
        const WlJsonVal *mods = wl_json_get(root, "mods");
        LONG n = 0;
        for (size_t i = 0; mods && i < wl_json_len(mods) && n < 256; i++) {
            const WlJsonVal *m = wl_json_at(mods, i);
            const char *id = wl_json_str(wl_json_get(m, "id"), NULL);
            if (!id) continue;
            snprintf(g_hubver[n].id, sizeof g_hubver[n].id, "%s", id);
            snprintf(g_hubver[n].version, sizeof g_hubver[n].version, "%s", wl_json_str(wl_json_get(m, "version"), ""));
            snprintf(g_hubver[n].requires, sizeof g_hubver[n].requires, "%s", wl_json_str(wl_json_get(m, "requires_wololoader"), ""));
            n++;
        }
        wl_json_free(root);
        InterlockedExchange(&g_hubver_n, n);
        wl_log("mods: update check: %ld mods in the Mod Hub", (long)n);
    } else {
        wl_log("mods: update check failed: %s", err);
    }
    InterlockedExchange(&g_hubver_busy, 0);
    return 0;
}

void wl_mods_check_updates(void) {
    if (g_wl_vanilla || g_hubver_n >= 0 || InterlockedExchange(&g_hubver_busy, 1)) return;
    HANDLE h = CreateThread(NULL, 0, hubver_worker, NULL, 0, NULL);
    if (h) CloseHandle(h); else InterlockedExchange(&g_hubver_busy, 0);
}

// The Hub's newer version of this mod that this Wololoader can install, or NULL.
static const char *update_for(const WlMod *m) {
    LONG n = g_hubver_n;
    for (LONG i = 0; i < n; i++) {
        if (_stricmp(g_hubver[i].id, m->id) != 0) continue;
        if (hub_compare_versions(g_hubver[i].version, m->version) <= 0) return NULL;
        if (g_hubver[i].requires[0] && hub_compare_versions(WOLOLOADER_VERSION, g_hubver[i].requires) < 0) return NULL;
        return g_hubver[i].version;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Conflicts between enabled mods: the same file under resources\ (only one mod's
// copy is used) or the same game civilization changed with "extend".

typedef struct { unsigned long long h; int mod; } ConfEntry;
static ConfEntry *g_conf;
static int g_conf_n, g_conf_cap;

static void conf_add(unsigned long long h, int mod) {
    if (g_conf_n == g_conf_cap) {
        int cap = g_conf_cap ? g_conf_cap * 2 : 1024;
        ConfEntry *n = realloc(g_conf, (size_t)cap * sizeof *n);
        if (!n) return;
        g_conf = n;
        g_conf_cap = cap;
    }
    g_conf[g_conf_n].h = h;
    g_conf[g_conf_n].mod = mod;
    g_conf_n++;
}

// FNV-1a 64 of the lower-case path relative to the mod's resources folder.
static unsigned long long path_hash(unsigned long long h, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '/') c = '\\';
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

// Sprites the loader renumbers itself (wl_slp_remap_compute): not a conflict.
static WlSlpRemap *g_conf_remap;
static size_t g_conf_remap_n;

static int conf_renumbered(int mod, const char *rel, const char *name) {
    if (_stricmp(rel, "_common\\drs\\graphics") != 0) return 0;
    char *end;
    long n = strtol(name, &end, 10);
    if (end == name || _stricmp(end, ".slp") != 0) return 0;
    for (size_t i = 0; i < g_conf_remap_n; i++)
        if (g_conf_remap[i].from == n && _stricmp(g_conf_remap[i].mod, g_wl.mods[mod].id) == 0) return 1;
    return 0;
}

static void conf_walk(const char *dir, const char *rel, unsigned long long prefix, int mod, int depth, int *budget) {
    if (depth > 12 || *budget <= 0) return;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        unsigned long long ph = path_hash(prefix, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char sub[MAX_PATH];
            snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
            char subrel[MAX_PATH];
            snprintf(subrel, sizeof subrel, "%s%s%s", rel, rel[0] ? "\\" : "", fd.cFileName);
            conf_walk(sub, subrel, path_hash(ph, "\\"), mod, depth + 1, budget);
        } else if (conf_renumbered(mod, rel, fd.cFileName)) {
            continue;
        } else if ((*budget)-- > 0) {
            conf_add(ph, mod);
        }
    } while (*budget > 0 && FindNextFileA(h, &fd));
    FindClose(h);
}

static int cmp_conf(const void *a, const void *b) {
    const ConfEntry *x = a, *y = b;
    if (x->h != y->h) return x->h < y->h ? -1 : 1;
    return x->mod - y->mod;
}

// Walking every mod's files on each row click would be slow under Wine, so the
// result is kept until mods change (toggle, Mod Hub, Reload Mods, a new open).
static int g_conf_valid;

static void compute_conflicts(void) {
    if (g_conf_valid) return;
    g_conf_valid = 1;
    g_conf_n = 0;
    {
        char drs[MAX_PATH], mdir[MAX_PATH];
        snprintf(drs, sizeof drs, "%s\\resources\\_common\\drs", g_wl.game_dir);
        snprintf(mdir, sizeof mdir, "%s\\mods", g_wl.root);
        free(g_conf_remap);
        g_conf_remap = NULL;
        g_conf_remap_n = 0;
        if (wl_slp_remap_compute(drs, mdir, &g_conf_remap, &g_conf_remap_n) != 0) { g_conf_remap = NULL; g_conf_remap_n = 0; }
    }
    int extend_by[32];                // base civ -> first enabled mod that extends it
    for (int c = 0; c < 32; c++) extend_by[c] = -1;
    for (int i = 0; i < g_wl.listed_count; i++) {
        g_conf_files[i] = 0;
        g_conf_partner[i] = -1;
        g_conf_extend[i] = 0;
        g_conf_extend_partner[i] = -1;
    }
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        char dir[MAX_PATH];
        snprintf(dir, sizeof dir, "%s\\resources", m->dir);
        int budget = 20000;           // files per mod; a bigger mod is only partly checked
        conf_walk(dir, "", 14695981039346656037ULL, i, 0, &budget);

        snprintf(dir, sizeof dir, "%s\\civs\\*.json", m->dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(dir, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s\\civs\\%s", m->dir, fd.cFileName);
            size_t len = 0;
            char *text = wl_read_file(path, &len);
            if (!text) continue;
            char err[64];
            WlJsonVal *root = wl_json_parse(text, err, sizeof err);
            free(text);
            int base = root ? (int)wl_json_num(wl_json_get(root, "extend"), 0) : 0;
            if (root) wl_json_free(root);
            if (base < 1 || base > 31) continue;
            int other = extend_by[base];
            if (other < 0) { extend_by[base] = i; continue; }
            if (other == i) continue;
            g_conf_extend[i] = base;       g_conf_extend_partner[i] = other;
            g_conf_extend[other] = base;   if (g_conf_extend_partner[other] < 0) g_conf_extend_partner[other] = i;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (g_conf_n > 1) qsort(g_conf, (size_t)g_conf_n, sizeof *g_conf, cmp_conf);
    for (int a = 0; a < g_conf_n;) {
        int b = a + 1;
        while (b < g_conf_n && g_conf[b].h == g_conf[a].h) b++;
        if (b - a > 1) {
            for (int k = a; k < b; k++) {
                int mod = g_conf[k].mod;
                if (k > a && g_conf[k - 1].mod == mod) continue;   // same mod twice: not a conflict
                int other = -1;
                for (int j = a; j < b && other < 0; j++) if (g_conf[j].mod != mod) other = g_conf[j].mod;
                if (other < 0) continue;
                g_conf_files[mod]++;
                if (g_conf_partner[mod] < 0) g_conf_partner[mod] = other;
            }
        }
        a = b;
    }
}

static void format_details(int idx) {
    for (int i = 0; i < INFO_LINES; i++) g_d_info[i][0] = 0;
    if (idx < 0 || idx >= g_wl.listed_count) {
        snprintf(g_d_name, sizeof g_d_name, "%s", g_wl.listed_count ? "No mods in this list" : "No mods installed");
        g_d_ver_author[0] = 0;
        g_d_state[0] = 0;
        snprintf(g_d_desc, sizeof g_d_desc, "%s",
                 g_wl.listed_count ? "Pick another filter above the list."
                                   : "Get mods from the Mod Hub, or put mod folders into wololoader\\mods.");
        return;
    }
    const WlMod *m = &g_wl.mods[idx];
    snprintf(g_d_name, sizeof g_d_name, "%s", m->name);
    clip(g_d_name, 34);

    const char *ver = m->version;
    if ((ver[0] == 'v' || ver[0] == 'V') && ver[1] >= '0' && ver[1] <= '9') ver++;
    if (ver[0] && m->author[0])
        snprintf(g_d_ver_author, sizeof g_d_ver_author, "Version %s  -  by %s", ver, m->author);
    else if (ver[0])
        snprintf(g_d_ver_author, sizeof g_d_ver_author, "Version %s", ver);
    else if (m->author[0])
        snprintf(g_d_ver_author, sizeof g_d_ver_author, "by %s", m->author);
    else
        g_d_ver_author[0] = 0;

    if (_stricmp(m->id, "WololoaderBase") == 0)
        snprintf(g_d_state, sizeof g_d_state, "%s", m->enabled ? "On: core features and scripts" : "Off");
    else
        snprintf(g_d_state, sizeof g_d_state, "%s", m->enabled ? "On: loads in every game" : "Off: not loaded");

    // Civilizations & Game modes
    int line = 0;
    char civs[400];
    int units = 0;
    read_civs(m, civs, sizeof civs, &units);
    if (civs[0]) snprintf(g_d_info[line], sizeof g_d_info[0], "Civilizations: %s", civs);
    else snprintf(g_d_info[line], sizeof g_d_info[0], "Civilizations: none");
    clip(g_d_info[line], INFO_CHARS);
    line++;

    char modes[400];
    read_game_modes(m, modes, sizeof modes);
    if (modes[0] && line < INFO_LINES) {
        snprintf(g_d_info[line], sizeof g_d_info[0], "Game modes: %s", modes);
        clip(g_d_info[line], INFO_CHARS);
        line++;
    }

    // Contents
    char sub[MAX_PATH], part[64], contents[256] = "";
    if (units) { snprintf(part, sizeof part, "%d custom unit%s", units, units == 1 ? "" : "s"); append_part(contents, sizeof contents, part); }
    snprintf(sub, sizeof sub, "%s\\scripts", m->dir);
    int scripts = count_glob(sub, "*.lua");
    if (scripts) { snprintf(part, sizeof part, "%d Lua script%s", scripts, scripts == 1 ? "" : "s"); append_part(contents, sizeof contents, part); }
    snprintf(sub, sizeof sub, "%s\\resources", m->dir);
    int res = count_files(sub, 0);
    if (res) { snprintf(part, sizeof part, "%d game file%s replaced", res, res == 1 ? "" : "s"); append_part(contents, sizeof contents, part); }
    snprintf(sub, sizeof sub, "%s\\history", m->dir);
    if (count_files(sub, 0)) append_part(contents, sizeof contents, "history pages");
    snprintf(g_d_info[line], sizeof g_d_info[0], "Contents: %s", contents[0] ? contents : "settings only");
    clip(g_d_info[line], INFO_CHARS);
    line++;

    // Source: Mod Hub (.hub.json in the mod folder) or a local folder
    char mods_dir[MAX_PATH];
    snprintf(mods_dir, sizeof mods_dir, "%s\\mods", g_wl.root);
    HubLocalState st;
    memset(&st, 0, sizeof st);
    hub_inspect_local_mod(mods_dir, m->id, &st);
    const char *upd = update_for(m);
    if (upd)
        snprintf(g_d_info[line], sizeof g_d_info[0], "Update: version %s is in the Mod Hub (press Update)", upd);
    else if (st.hub_managed)
        snprintf(g_d_info[line], sizeof g_d_info[0], "Source: Mod Hub (up to date)");
    else
        snprintf(g_d_info[line], sizeof g_d_info[0], "Source: local folder");
    line++;

    snprintf(g_d_info[line], sizeof g_d_info[0], "Folder: wololoader\\mods\\%s", m->id);
    clip(g_d_info[line], INFO_CHARS);
    line++;

    // Conflicts with other enabled mods (compute_conflicts). For files the mod that
    // comes later in the list wins (files.c tbl_put); the list is sorted by folder.
    char conf[400] = "";
    if (m->enabled && g_conf_files[idx] > 0 && g_conf_partner[idx] >= 0) {
        int p = g_conf_partner[idx];
        snprintf(part, sizeof part, "%d file%s", g_conf_files[idx], g_conf_files[idx] == 1 ? "" : "s");
        size_t used = strlen(conf);
        snprintf(conf + used, sizeof conf - used, "%s also in %s (%s wins)", part, g_wl.mods[p].name,
                 p > idx ? g_wl.mods[p].name : m->name);
    }
    if (m->enabled && g_conf_extend[idx] > 0 && g_conf_extend_partner[idx] >= 0) {
        size_t used = strlen(conf);
        snprintf(conf + used, sizeof conf - used, "%scivilization %d also changed by %s",
                 used ? "; " : "", g_conf_extend[idx], g_wl.mods[g_conf_extend_partner[idx]].name);
    }
    if (conf[0]) snprintf(g_d_info[line], sizeof g_d_info[0], "Conflicts: %s", conf);
    else snprintf(g_d_info[line], sizeof g_d_info[0], "Conflicts: none");
    clip(g_d_info[line], INFO_CHARS);

    if (m->description[0]) wrap_text(g_d_desc, sizeof g_d_desc, m->description, DESC_CHARS);
    else snprintf(g_d_desc, sizeof g_d_desc, "No description.");
}

// ---------------------------------------------------------------------------
// Text updates (in place, no rebuild)

static void format_row_text(int mod_idx, char *out, size_t cap) {
    if (mod_idx < 0) { out[0] = 0; return; }
    const WlMod *m = &g_wl.mods[mod_idx];
    char name[160];
    snprintf(name, sizeof name, "%s", m->name);
    clip(name, m->enabled ? 26 : 20);
    int conflict = m->enabled && (g_conf_files[mod_idx] > 0 || g_conf_extend[mod_idx] > 0);
    snprintf(out, cap, "%s%s%s%s%s", mod_idx == g_selected_mod ? "> " : "", name,
             m->enabled ? "" : " (off)", conflict ? " (!)" : "", update_for(m) ? " (new)" : "");
}

static void update_tabs(void) {
    int on = enabled_count();
    snprintf(g_tab_texts[FILTER_ALL], sizeof g_tab_texts[0], "%sAll %d", g_filter == FILTER_ALL ? "> " : "", g_wl.listed_count);
    snprintf(g_tab_texts[FILTER_ON], sizeof g_tab_texts[0], "%sOn %d", g_filter == FILTER_ON ? "> " : "", on);
    snprintf(g_tab_texts[FILTER_OFF], sizeof g_tab_texts[0], "%sOff %d", g_filter == FILTER_OFF ? "> " : "", g_wl.listed_count - on);
    for (int f = 0; f < FILTER_COUNT; f++) button_set_text(g_tab_btns[f], g_tab_texts[f]);
    snprintf(g_status_text, sizeof g_status_text, "Wololoader v%s  -  %d of %d mods on",
             WOLOLOADER_VERSION, on, g_wl.listed_count);
    panel_set_text(g_lbl_status, g_status_text);
}

static void update_rows(void) {
    for (int r = 0; r < PAGE_SIZE; r++) {
        int mi = g_row_mod[r];
        if (mi < 0) continue;
        format_row_text(mi, g_row_texts[r], sizeof g_row_texts[r]);
        button_set_text(g_row_btns[r], g_row_texts[r]);
        panel_set_visible(g_row_badge_on[r], g_wl.mods[mi].enabled);
        panel_set_visible(g_row_badge_off[r], !g_wl.mods[mi].enabled);
    }
}

// Visibility of the buttons and their icons. readonly: lobby view, nothing that
// changes mods is shown.
static void update_buttons(void) {
    int ro = g_return_screen != NULL;
    int has_sel = g_selected_mod >= 0 && g_selected_mod < g_wl.listed_count;
    int on = has_sel && g_wl.mods[g_selected_mod].enabled;
    button_set_text(g_btn_toggle, on ? "Disable" : "Enable");
    panel_set_visible(g_btn_toggle, !ro && has_sel);
    panel_set_visible(g_icon_toggle_off, !ro && has_sel && on);    // cross: the button turns it off
    panel_set_visible(g_icon_toggle_on, !ro && has_sel && !on);    // check: the button turns it on
    panel_set_visible(g_btn_mod_folder, !ro && has_sel);
    panel_set_visible(g_icon_mod_folder, !ro && has_sel);
    panel_set_visible(g_btn_mod_edit, !ro && has_sel);
    panel_set_visible(g_icon_mod_edit, !ro && has_sel);
    panel_set_visible(g_btn_settings, !ro && has_sel);
    void *change[] = { g_btn_vanilla, g_icon_vanilla, g_btn_creator, g_icon_creator,
                       g_btn_open, g_icon_folder, g_btn_hub, g_btn_reload };
    for (size_t i = 0; i < sizeof change / sizeof change[0]; i++) panel_set_visible(change[i], !ro);
    panel_set_text(g_lbl_state, has_sel ? g_d_state : "");
}

static void set_note(const char *text) {
    snprintf(g_note, sizeof g_note, "%s", text ? text : "");
    panel_set_text(g_lbl_note, g_note);
}

// ---------------------------------------------------------------------------
// Screen lifetime

// Delete a screen safely from inside its own click handler: the game's ModManager
// "Close" path (0x6f181b..0x6f1868) builds a std::function on the stack ({vtbl
// 0x9d7e18, [4] = screen, [0x24] = &itself}, 0x28 bytes; its call slot 0x6a5390 runs
// screen->vtbl[0](1)), hands it to the deferred queue 0x4382e0 (cdecl, 1 arg) and then
// destroys the local copy with vtbl slot 4 (0x428c80, __thiscall(fn, free_it), arg =
// (impl != &local)). Deleting a screen that is not current just removes it.
typedef void (__cdecl *Defer_t)(void *fn);
typedef void (__thiscall *FnDestroy_t)(void *fn, int free_it);
static void defer_delete_screen(void *screen) {
    DWORD f[10];
    memset(f, 0, sizeof f);
    f[0] = 0x9d7e18;
    f[1] = (DWORD)screen;
    f[9] = (DWORD)f;
    ((Defer_t)0x4382e0)(f);
    void *impl = (void *)f[9];
    if (impl) ((FnDestroy_t)(*(void ***)impl)[4])(impl, impl != (void *)f);
}

static void show_screen(void) {
    ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, g_screen);
}

// Builds a fresh screen from the current state and shows it; the old one is
// deleted through the game's deferred queue (we may be inside its click handler).
static void rebuild_screen(void) {
    void *old = g_screen;
    g_screen = create_screen();
    if (g_screen) show_screen();
    if (old) defer_delete_screen(old);
}

int wl_mods_is_readonly_open(void) {
    return g_return_screen != NULL;
}

// ---------------------------------------------------------------------------
// Clicks

static void toggle_selected(void) {
    if (g_selected_mod < 0 || g_selected_mod >= g_wl.listed_count) return;
    WlMod *m = &g_wl.mods[g_selected_mod];
    BOOL new_state = !m->enabled;
    if (!wl_set_mod_enabled(m->dir, new_state)) {
        wl_log("mods: failed to update mod.json for '%s'", m->id);
        set_note("Could not save the change (mod.json is not writable).");
        return;
    }
    m->enabled = new_state;
    g_mods_dirty = 1;
    g_conf_valid = 0;
    g_wl.mod_count = enabled_count();
    wl_log("mods: mod '%s' %s", m->id, new_state ? "enabled" : "disabled");

    // The row stays where it is until the filter or page changes, so the
    // player can switch it back right away.
    format_details(g_selected_mod);
    update_rows();
    update_tabs();
    update_buttons();
    if (g_wl_vanilla)
        set_note("Saved. Restart required (Vanilla Mode is on).");
    else if (_stricmp(m->id, "WololoaderBase") == 0)
        set_note("Saved. Binary patches need a restart; Back reloads the scripts.");
    else
        set_note("Saved. Press Back or Reload Mods to apply, no restart needed.");
}

// Writes the selected mod's settings; Back / Reload Mods then restarts its scripts.
static void save_settings(void) {
    if (g_selected_mod < 0) return;
    const WlMod *m = &g_wl.mods[g_selected_mod];
    if (wl_settings_save(m, g_set, g_set_count)) {
        g_mods_dirty = 1;
        wl_content_checksum_invalidate();   // settings are part of the multiplayer check
        snprintf(g_note, sizeof g_note, "Saved. Press Back or Reload Mods to apply.");
        wl_log("mods: settings of '%s' saved", m->id);
    } else {
        snprintf(g_note, sizeof g_note, "Could not save the settings (wololoader\\settings).");
    }
    panel_set_text(g_lbl_note, g_note);
}

static int __fastcall on_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (code != EV_LEFT_CLICK || !panel) return g_orig_event(self, panel, code, a, b);
    int ro = g_return_screen != NULL;

    if (panel == g_btn_back) {
        if (ro) {                                   // opened read-only from the lobby
            void *back = g_return_screen;
            g_return_screen = NULL;
            ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, back);
            // Remove the Mods screen from the screen list: left below the lobby, it was
            // shown again when the lobby closed (Cancel/Back, seen in game 2026-11-15).
            defer_delete_screen(g_screen);
            g_screen = NULL;
            return 1;
        }
        int reload = g_mods_dirty || wl_hotreload_mods_changed();
        if (reload) {
            char msg[512] = "";
            wl_hotreload_run(0, msg, sizeof msg);
            g_mods_dirty = 0;
        }
        // Rebuilt on the next open so counts and new mods show the current state.
        void *old = g_screen;
        g_screen = NULL;
        g_note[0] = 0;
        g_settings_mode = 0;
        g_conf_valid = 0;
        ((SetCurrentByName_t)ADDR_SET_CURRENT_BY_NAME)(*(void **)ADDR_SCREEN_MGR, "Main Menu");
        if (old) defer_delete_screen(old);
        return 1;
    }

    for (int f = 0; f < FILTER_COUNT; f++) {
        if (panel == g_tab_btns[f]) {
            if (g_filter != f) { g_filter = f; g_page = 0; g_selected_mod = -1; g_settings_mode = 0; rebuild_screen(); }
            return 1;
        }
    }
    if (panel == g_btn_prev || panel == g_btn_next) {
        int pages = g_view_count > 0 ? (g_view_count + PAGE_SIZE - 1) / PAGE_SIZE : 1;
        int np = g_page + (panel == g_btn_next ? 1 : -1);
        if (np >= 0 && np < pages) { g_page = np; g_selected_mod = -1; g_settings_mode = 0; rebuild_screen(); }
        return 1;
    }
    for (int r = 0; r < PAGE_SIZE; r++) {
        if (panel == g_row_btns[r] && g_row_mod[r] >= 0) {
            if (g_selected_mod != g_row_mod[r]) {
                g_selected_mod = g_row_mod[r];
                g_settings_mode = 0;
                rebuild_screen();          // swaps the big icon and every detail line
            }
            return 1;
        }
    }

    if (ro) return 1;   // the change buttons are hidden in the lobby view; ignore them anyway

    if (panel == g_btn_toggle) { toggle_selected(); return 1; }
    if (panel == g_btn_settings && g_btn_settings) { g_settings_mode = 1; g_set_page = 0; rebuild_screen(); return 1; }
    if (panel == g_btn_set_done && g_btn_set_done) { g_settings_mode = 0; rebuild_screen(); return 1; }
    if ((panel == g_btn_set_prev || panel == g_btn_set_next) && panel) {
        g_set_page += panel == g_btn_set_next ? 1 : -1;
        rebuild_screen();
        return 1;
    }
    if (panel == g_btn_set_defaults && g_btn_set_defaults) {
        for (int i = 0; i < g_set_count; i++) {
            g_set[i].on = g_set[i].def_on;
            g_set[i].num = g_set[i].def_num;
            g_set[i].choice = g_set[i].def_choice;
        }
        save_settings();
        rebuild_screen();
        return 1;
    }
    for (int r = 0; r < SET_ROWS; r++) {
        int si = g_set_row[r];
        if (si < 0 || (panel != g_set_btn[r] && panel != g_set_minus[r])) continue;
        wl_setting_step(&g_set[si], panel == g_set_minus[r] ? -1 : 1);
        wl_setting_text(&g_set[si], g_set_vals[r], sizeof g_set_vals[r]);
        if (g_set_lbl[r]) panel_set_text(g_set_lbl[r], g_set_vals[r]);
        else button_set_text(g_set_btn[r], g_set_vals[r]);
        save_settings();
        return 1;
    }
    if (panel == g_btn_mod_folder && g_selected_mod >= 0) {
        wl_open_folder(g_wl.mods[g_selected_mod].dir);
        return 1;
    }
    if (panel == g_btn_mod_edit && g_selected_mod >= 0) {
        char args[96];
        snprintf(args, sizeof args, "--mod %s", g_wl.mods[g_selected_mod].id);
        set_note(wl_launch_scriptorium_args(args)
                     ? "Scriptorium opened on this mod. Press Reload Mods after saving."
                     : "Scriptorium.exe not found in the wololoader folder.");
        return 1;
    }
    if (panel == g_btn_vanilla) {
        BOOL on = !wl_vanilla_flag_exists();
        if (wl_vanilla_set(on)) {
            g_mods_dirty = 1;
            button_set_text(g_btn_vanilla, vanilla_text());
            set_note(on ? "Vanilla Mode on next start: no mods, no patches (for online games)."
                        : "Vanilla Mode off: mods load again on next start.");
            wl_log("Vanilla Mode set to %s (next start)", on ? "on" : "off");
        }
        return 1;
    }
    if (panel == g_btn_reload) {
        char msg[512] = "";
        wl_hotreload_run(1, msg, sizeof msg);
        g_mods_dirty = 0;
        g_conf_valid = 0;
        snprintf(g_note, sizeof g_note, "%s", msg);
        rebuild_screen();
        return 1;
    }
    if (panel == g_btn_hub || (panel == g_btn_update && g_btn_update)) { wl_hub_open(g_screen, 0); return 1; }
    if (panel == g_btn_creator) {
        set_note(wl_launch_scriptorium() ? "Scriptorium opened. Press Reload Mods after saving."
                                         : "Scriptorium.exe not found in the wololoader folder.");
        return 1;
    }
    if (panel == g_btn_open) {
        char dir[MAX_PATH];
        snprintf(dir, MAX_PATH, "%s\\mods", g_wl.root);
        wl_open_folder(dir);
        return 1;
    }
    return g_orig_event(self, panel, code, a, b);
}

// ---------------------------------------------------------------------------
// Building the screen

// Fills g_view from the filter and makes page / selection valid.
static void build_view(void) {
    g_view_count = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        int on = g_wl.mods[i].enabled ? 1 : 0;
        if (g_filter == FILTER_ALL || (g_filter == FILTER_ON) == on) g_view[g_view_count++] = i;
    }
    int pages = g_view_count > 0 ? (g_view_count + PAGE_SIZE - 1) / PAGE_SIZE : 1;
    if (g_page >= pages) g_page = pages - 1;
    if (g_page < 0) g_page = 0;
    // A selection must be on the shown page; else the page's first mod.
    int start = g_page * PAGE_SIZE, end = start + PAGE_SIZE;
    if (end > g_view_count) end = g_view_count;
    int found = 0;
    for (int v = start; v < end; v++) if (g_view[v] == g_selected_mod) found = 1;
    if (!found) {
        // It may sit on another page of this filter (e.g. after Reload Mods): go there.
        for (int v = 0; v < g_view_count && !found; v++)
            if (g_view[v] == g_selected_mod) { g_page = v / PAGE_SIZE; found = 1; }
    }
    if (!found) g_selected_mod = start < end ? g_view[start] : -1;
}

static void *create_screen(void) {
    wl_log("mods: create");
    wl_civs_ensure();  // so a build error can be shown
    void *s = ((New_t)ADDR_GAME_NEW)(SCREENPANEL_SIZE);
    if (!s) return NULL;
    memset(s, 0, SCREENPANEL_SIZE);
    ((ScreenCtor_t)ADDR_SCREEN_CTOR)(s, "Wololoader Mods");

    memcpy(g_vtable_block, (void *)(SCREENPANEL_VTBL - 4), sizeof g_vtable_block);
    g_orig_event = (Event_t)g_vtable[VT_EVENT];
    g_vtable[VT_EVENT] = (void *)on_event;
    *(void **)s = g_vtable;

    ((SetBg_t)ADDR_SET_BACKGROUND)(s, "scr2p", 0xc3a4, 0);
    int ref[2] = { REF_W, REF_H };
    ((SetRef_t)ADDR_SET_REF_SIZE)(s, ref);

    snprintf(g_default_icon, sizeof g_default_icon,
             "%s\\resources\\_common\\textures\\ui\\tech-tree-icon.png", g_wl.game_dir);

    build_view();
    compute_conflicts();
    wl_mods_check_updates();
    int ro = g_return_screen != NULL;
    g_screen_ro = ro;
    int creator = has_scriptorium();

    add_label(s, ro ? "Mods (this game)" : "Mods", 0, 15, REF_W, 40, 1, 1, 1, 0);
    g_btn_reload = add_button(s, "Reload Mods", 820, 22, 160, 40);
    g_btn_hub    = add_button(s, "Mod Hub", 990, 22, 150, 40);

    // ---- left: filter tabs, rows, pager, status ----
    for (int f = 0; f < FILTER_COUNT; f++)
        g_tab_btns[f] = add_button(s, "", LIST_X + f * (TAB_W + 8), TABS_Y, TAB_W, TAB_H);

    for (int r = 0; r < PAGE_SIZE; r++) {
        int v = g_page * PAGE_SIZE + r;
        g_row_mod[r] = v < g_view_count ? g_view[v] : -1;
        g_row_btns[r] = g_row_badge_on[r] = g_row_badge_off[r] = NULL;
        if (g_row_mod[r] < 0) continue;
        int y = ROWS_Y + r * ROW_STEP;
        mod_icon(s, &g_wl.mods[g_row_mod[r]], LIST_X, y + 2, 40);
        g_row_texts[r][0] = 0;
        g_row_btns[r] = add_button(s, g_row_texts[r], LIST_X + 48, y, LIST_W - 48, ROW_H);
        // Enabled badge on the icon's lower right corner (drawn over the icon).
        g_row_badge_on[r]  = ui_icon(s, "badge_on", LIST_X + 24, y + 26, 18);
        g_row_badge_off[r] = ui_icon(s, "badge_off", LIST_X + 24, y + 26, 18);
    }
    if (g_view_count == 0) {
        add_label(s, g_filter == FILTER_OFF ? "Every mod is on." :
                     g_filter == FILTER_ON ? "No mod is on." : "No mods installed.",
                  LIST_X, ROWS_Y + 10, LIST_W, 30, 0xa, 1, 1, 0);
    }

    int pages = g_view_count > 0 ? (g_view_count + PAGE_SIZE - 1) / PAGE_SIZE : 1;
    g_btn_prev = g_btn_next = NULL;
    if (pages > 1) {
        g_btn_prev = add_button(s, "< Prev", LIST_X, PAGER_Y, 90, 34);
        snprintf(g_page_text, sizeof g_page_text, "Page %d / %d", g_page + 1, pages);
        add_label(s, g_page_text, LIST_X + 95, PAGER_Y, LIST_W - 190, 34, 0xa, 1, 1, 0);
        g_btn_next = add_button(s, "Next >", LIST_X + LIST_W - 90, PAGER_Y, 90, 34);
    }
    g_lbl_status = add_label(s, "", LIST_X, STATUS_Y, LIST_W, 24, 0xa, 0, 1, 0);

    // ---- right: detail pane ----
    format_details(g_selected_mod);
    if (g_selected_mod >= 0) mod_icon(s, &g_wl.mods[g_selected_mod], DET_X, HEAD_Y, 96);
    int head_x = g_selected_mod >= 0 ? TEXT_X : DET_X;
    int head_w = g_selected_mod >= 0 ? TEXT_W : DET_W;
    add_label(s, g_d_name, head_x, HEAD_Y, head_w, 40, 1, 0, 0, 0);
    g_lbl_ver_author = add_label(s, g_d_ver_author, TEXT_X, HEAD_Y + 42, TEXT_W, 24, 0xa, 0, 0, 0);
    g_btn_toggle = add_button(s, "Disable", TEXT_X, HEAD_Y + 72, 150, 38);
    g_icon_toggle_on  = ui_icon(s, "check", TEXT_X + 10, HEAD_Y + 80, 22);
    g_icon_toggle_off = ui_icon(s, "cross", TEXT_X + 10, HEAD_Y + 80, 22);
    g_lbl_state = add_label(s, "", TEXT_X + 162, HEAD_Y + 72, TEXT_W - 162, 38, 0xa, 0, 1, 0);

    g_set_count = g_selected_mod >= 0 ? wl_settings_load(&g_wl.mods[g_selected_mod], g_set, WL_MAX_SETTINGS) : 0;
    if (ro || g_set_count == 0) g_settings_mode = 0;
    for (int i = 0; i < INFO_LINES; i++) g_lbl_info[i] = NULL;
    for (int r = 0; r < SET_ROWS; r++) { g_set_btn[r] = g_set_minus[r] = g_set_lbl[r] = NULL; g_set_row[r] = -1; }
    g_btn_mod_folder = g_icon_mod_folder = g_btn_mod_edit = g_icon_mod_edit = g_btn_settings = NULL;
    g_btn_set_done = g_btn_set_defaults = g_btn_set_prev = g_btn_set_next = NULL;

    if (g_settings_mode) {
        // Settings view: one row per setting (name left, control right), pages of SET_ROWS.
        int pages = (g_set_count + SET_ROWS - 1) / SET_ROWS;
        if (g_set_page >= pages) g_set_page = pages - 1;
        if (g_set_page < 0) g_set_page = 0;
        if (pages > 1)
            snprintf(g_set_title, sizeof g_set_title, "Settings (page %d / %d): saved at once", g_set_page + 1, pages);
        else
            snprintf(g_set_title, sizeof g_set_title, "Settings: saved at once, applied on Back or Reload Mods");
        add_label(s, g_set_title, DET_X, INFO_Y - 8, DET_W, 24, 0xa, 0, 0, 0);
        for (int r = 0; r < SET_ROWS; r++) {
            int si = g_set_page * SET_ROWS + r;
            if (si >= g_set_count) break;
            g_set_row[r] = si;
            int y = INFO_Y + 22 + r * SET_STEP;
            snprintf(g_set_names[r], sizeof g_set_names[r], "%s", g_set[si].name);
            clip(g_set_names[r], 52);
            add_label(s, g_set_names[r], DET_X, y, 290, 34, 0xa, 0, 1, 0);
            wl_setting_text(&g_set[si], g_set_vals[r], sizeof g_set_vals[r]);
            if (g_set[si].type == WL_SET_NUMBER) {
                g_set_minus[r] = add_button(s, "-", DET_X + 300, y, 50, 34);
                g_set_lbl[r] = add_label(s, g_set_vals[r], DET_X + 355, y, 110, 34, 0xa, 1, 1, 0);
                g_set_btn[r] = add_button(s, "+", DET_X + 470, y, 50, 34);
            } else {
                g_set_btn[r] = add_button(s, g_set_vals[r], DET_X + 300, y, 220, 34);
            }
        }
        g_btn_set_done = add_button(s, "Done", DET_X, ACTIONS_Y, 150, 36);
        g_icon_mod_folder = ui_icon(s, "back", DET_X + 12, ACTIONS_Y + 8, 20);
        g_btn_set_defaults = add_button(s, "Defaults", DET_X + 160, ACTIONS_Y, 150, 36);
        if (pages > 1) {
            g_btn_set_prev = add_button(s, "<", DET_X + 410, ACTIONS_Y, 50, 36);
            g_btn_set_next = add_button(s, ">", DET_X + 470, ACTIONS_Y, 50, 36);
        }
    } else {
        for (int i = 0; i < INFO_LINES; i++)
            g_lbl_info[i] = add_label(s, g_d_info[i], DET_X, INFO_Y + i * INFO_STEP, DET_W, 24, 0xa, 0, 0, 0);
        add_label(s, g_d_desc, DET_X, DESC_Y, DET_W, ACTIONS_Y - DESC_Y - 8, 0xa, 0, 0, 0);

        // Actions row, 530 wide: Open Folder 160 | Edit in Scriptorium 220 | Settings 130.
        g_btn_mod_folder = add_button(s, "Open Folder", DET_X, ACTIONS_Y, 160, 36);
        g_icon_mod_folder = ui_icon(s, "folder", DET_X + 10, ACTIONS_Y + 7, 22);
        int ax = DET_X + 170;
        if (creator) {
            g_btn_mod_edit = add_button(s, "Edit in Scriptorium", ax, ACTIONS_Y, 220, 36);
            g_icon_mod_edit = ui_icon(s, "quill", ax + 10, ACTIONS_Y + 7, 22);
            ax += 230;
        }
        if (g_set_count > 0) { g_btn_settings = add_button(s, "Settings", ax, ACTIONS_Y, 130, 36); ax += 140; }
        // Update: opens the Mod Hub, which installs the new version (and keeps a local copy in the trash).
        g_btn_update = NULL;
        if (g_selected_mod >= 0 && update_for(&g_wl.mods[g_selected_mod]) && !ro) {
            if (ax + 110 > DET_X + DET_W) ax = DET_X + DET_W - 110;   // no room: covers the last button
            g_btn_update = add_button(s, "Update", ax, ACTIONS_Y, 110, 36);
        }
    }

    // Note line: build error, last reload result or the last action's message.
    const char *last_hr = wl_hotreload_last_message();
    if (g_wl_build_error[0])
        snprintf(g_note, sizeof g_note, "Build error: %s", g_wl_build_error);
    else if (!g_note[0] && last_hr && last_hr[0])
        snprintf(g_note, sizeof g_note, "%s", last_hr);
    clip(g_note, INFO_CHARS);
    g_lbl_note = add_label(s, g_note, DET_X, STATUS_Y, DET_W, 24, 0xa, 0, 1, 0);

    // ---- bottom bar: same height and baseline for every button ----
    int x = LIST_X;
    g_btn_vanilla = add_button(s, vanilla_text(), x, BAR_Y, 220, BAR_H);
    g_icon_vanilla = ui_icon(s, "swords", x + 10, BAR_Y + 9, 22);
    x += 230;
    g_btn_creator = g_icon_creator = NULL;
    if (creator) {
        g_btn_creator = add_button(s, "Mod Creator", x, BAR_Y, 190, BAR_H);
        g_icon_creator = ui_icon(s, "quill", x + 10, BAR_Y + 9, 22);
        x += 200;
    }
    g_btn_open = add_button(s, "Mods Folder", x, BAR_Y, 190, BAR_H);
    g_icon_folder = ui_icon(s, "folder", x + 10, BAR_Y + 9, 22);
    g_btn_back = add_button(s, "Back", 1140 - 130, BAR_Y, 130, BAR_H);
    g_icon_back = ui_icon(s, "back", 1140 - 120, BAR_Y + 10, 20);
    (void)g_icon_back;

    update_tabs();
    update_rows();
    update_buttons();
    wl_log("mods: screen %p (filter %d, page %d, %d shown, selected %d)",
           s, g_filter, g_page, g_view_count, g_selected_mod);
    return s;
}

// Replaces the main menu's Steam Workshop handler. Never calls the original.
static void __fastcall hook_modmgr_button(void *main_menu) {
    (void)main_menu;
    if (g_screen && g_screen_ro) { rebuild_screen(); return; }   // left over from the lobby view
    if (!g_screen) g_screen = create_screen();
    if (g_screen) show_screen();
}

// Game start while the read-only view is open (lobby countdown): forget the
// lobby screen (it is being replaced by the game). The screen itself may still be
// current here, so it is not deleted; the next open from the main menu rebuilds it
// (g_screen_ro).
void wl_mods_readonly_reset(void) {
    if (!g_return_screen) return;
    g_return_screen = NULL;
    wl_log("mods: read-only view reset (game started)");
}

// Read-only view for the multiplayer lobby ("Active Mods" button, lobby.c).
void wl_mods_open_readonly(void *return_screen) {
    g_return_screen = return_screen;
    g_conf_valid = 0;
    g_filter = FILTER_ON;   // the lobby cares about what is loaded
    g_page = 0;
    g_note[0] = 0;
    rebuild_screen();
    if (!g_screen) g_return_screen = NULL;
    else wl_log("mods: shown read-only (return to %p)", return_screen);
}

void wl_mods_return_from_hub(int mods_changed) {
    if (mods_changed) {
        g_mods_dirty = 1;
        g_conf_valid = 0;
        wl_scan_mods();
        snprintf(g_note, sizeof g_note, "Mod Hub changes saved. Press Back or Reload Mods to apply.");
        rebuild_screen();
        wl_log("mods: returned from Mod Hub (rescanned: %d mods)", g_wl.listed_count);
    } else if (g_screen) {
        show_screen();
    } else {
        rebuild_screen();
    }
}

void wl_install_mods_screen(void) {
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1
    if (!wl_hook(ADDR_MODMGR_HANDLER, hook_modmgr_button, 5)) wl_log("Mods screen hook FAILED");
}
