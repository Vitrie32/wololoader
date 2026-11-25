// In-game Mod Hub screen, opened from the Mods screen's "Mod Hub" button.
// Built on the same ScreenPanel / ButtonDrawableControlPanel / TextPanel /
// DrawablePanel pattern as ui_mods.c, with a worker thread for HTTPS/HTTP
// index & archive fetching so the game UI thread never blocks.
// Addresses: AoK HD.exe 5.8, see re/FINDINGS.md ("Mods screen v2") & ui_mods.c.
#include "wololoader.h"
#include "hubcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_GAME_NEW             0x8d47df  // game's operator new (cdecl)
#define ADDR_SCREEN_CTOR          0x489d50  // ScreenPanel(this, const char *name), ret 4
#define SCREENPANEL_VTBL          0x9b24ac  // 59 slots
#define SCREENPANEL_SIZE          0x27c
#define VT_SLOTS                  59
#define VT_TICK                   17        // +0x44: 0x490890, __thiscall(self), plain ret
#define VT_EVENT                  49        // +0xc4: 0x48d430, (panel, code, a, b), ret 0x10
#define ADDR_SCREEN_MGR           0xada4b4  // [ptr] screen list
#define ADDR_SET_CURRENT          0x48a8e0  // thiscall(mgr, screen), ret 4
#define ADDR_SET_BACKGROUND       0x48a1c0  // thiscall(scr, name, id, 0), ret 0xc
#define ADDR_SET_REF_SIZE         0x491250  // thiscall(scr, int wh[2]), ret 4
#define ADDR_ADD_BUTTON           0x48df30  // thiscall, ret 0x28
#define ADDR_ADD_LABEL            0x48fe60  // thiscall, ret 0x2c
#define ADDR_SET_TEXT_STYLE       0x48d390
#define ADDR_SET_TEXT_FLAG        0x48d3c0
#define ADDR_APPLY_STYLE          0x48d550
#define ADDR_STYLE_PAIR           0xada500

#define ADDR_BUTTON_SET_CHECKED   0x477450  // thiscall(btn, char checked), ret 4
#define ADDR_BUTTON_SET_TEXT      0x479ce0  // thiscall(btn, int slot, const char *text), ret 8
#define ADDR_BUTTON_SET_ENABLED   0x479840  // vtable[7]: thiscall(btn, char enabled), ret 4
#define ADDR_LOAD_PNG             0x49f1f0  // thiscall(chain, &out, path), ret 8
#define ADDR_LOADER_CHAIN         0xada9f4  // [ptr] ui::ImageFileDrawableLoader
#define ADDR_SCALE_RECT           0x491190  // thiscall(scr, out[4], in[4]), ret 8
#define ADDR_DRAWABLE_PANEL_CTOR  0x47a5d0  // thiscall(mem, parent, rect[4], drawable), ret 0xc
#define DRAWABLE_PANEL_SIZE       0x134

#define EV_LEFT_CLICK             3
#define REF_W                     1366
#define REF_H                     768
#define HUB_PAGE_SIZE             8
#define LIST_X                    225
#define LIST_W                    340
#define LIST_Y                    85
#define DET_X                     610
#define DET_W                     530
#define BOTTOM_Y                  588
#define DESC_CHARS                96
#define STATUS_CHARS              96

typedef void *(__cdecl *New_t)(unsigned size);
typedef void *(__thiscall *ScreenCtor_t)(void *self, const char *name);
typedef void (__thiscall *SetCurrent_t)(void *mgr, void *screen);
typedef void (__thiscall *SetBg_t)(void *scr, const char *name, int id, int z);
typedef void (__thiscall *SetRef_t)(void *scr, int *wh);
typedef void *(__thiscall *AddButton_t)(void *scr, void *parent, const char *text, const char *text2,
                                         int x, int y, int w, int h, int a, int b, int c);
typedef char (__thiscall *AddLabel_t)(void *scr, void *parent, void **out, const char *text,
                                      int x, int y, int w, int h, int font, int f24, int f28, int f2c);
typedef void (__thiscall *SetTextStyle_t)(void *scr, DWORD *a, DWORD *b);
typedef void (__thiscall *SetTextFlag_t)(void *scr, int flag);
typedef void (__thiscall *ApplyStyle_t)(void *scr, void *panel);
typedef void (__thiscall *Tick_t)(void *self);
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);

typedef void (__thiscall *ButtonSetChecked_t)(void *btn, char checked);
typedef void (__thiscall *ButtonSetText_t)(void *btn, int slot, const char *text);
typedef void (__thiscall *ButtonSetEnabled_t)(void *btn, char enabled);
typedef void (__thiscall *PanelSetVisible_t)(void *panel, int visible);
typedef void (__thiscall *TextPanelSetText_t)(void *panel, const char *text, int z0, int z1, int z2);
typedef void (__thiscall *LoadPng_t)(void *chain, void **out, const char *path);
typedef void (__thiscall *DrawableSetFit_t)(void *drawable, const int *mode);
typedef void *(__thiscall *DrawablePanelCtor_t)(void *mem, void *parent, const int rect[4], void *drawable);
typedef void *(__thiscall *ScaleRect_t)(void *screen, int out[4], const int in[4]);
typedef void (__cdecl *Defer_t)(void *fn);
typedef void (__thiscall *FnDestroy_t)(void *fn, int free_it);
typedef void (__thiscall *DrawableDtor_t)(void *drawable);

typedef struct {
    char    id[128];
    char    name[128];
    char    version[32];
    char    author[64];
    char    description[1024];
    char    tags[256];
    char    depends[256];
    char    icon_raw[HUB_PATH_CAP];
    char    req_wl[32];
    int64_t size_bytes;

    int     installed;
    int     hub_managed;
    int     installed_enabled;
    int     update_available;
    int     compatible;
    char    installed_version[64];
    int     icon_loaded; // 1 once local hub_icons/<id>.png has been checked/applied
} HubUiMod;

enum {
    HUB_IDLE = 0,
    HUB_BUSY_INDEX = 1,
    HUB_BUSY_INSTALL = 2,
    HUB_BUSY_UNINSTALL = 3
};

static void *g_hub_screen;
static void *g_return_mods_screen;
static int   g_readonly;
static int   g_mods_dirty;

static void *g_vtable_block[VT_SLOTS + 1];
#define g_vtable (g_vtable_block + 1)
static Tick_t  g_orig_tick;
static Event_t g_orig_event;

// Controls
static void *g_row_btns[HUB_PAGE_SIZE];
static void *g_row_icons[HUB_PAGE_SIZE];
static void *g_row_badge_on[HUB_PAGE_SIZE];
static void *g_row_badge_off[HUB_PAGE_SIZE];
static void *g_detail_icon;

static void *g_btn_prev;
static void *g_btn_next;
static void *g_lbl_page;

static void *g_lbl_name;
static void *g_lbl_ver_author;
static void *g_lbl_mod_status;
static void *g_lbl_size;
static void *g_lbl_tags;
static void *g_lbl_deps;
static void *g_lbl_desc;

static void *g_lbl_bottom_count;
static void *g_lbl_status;

static void *g_btn_action;      // Install / Update / Installed
static void *g_btn_uninstall;
static void *g_btn_scriptorium; // Optional trivial shortcut if Scriptorium.exe exists
static void *g_btn_refresh;
static void *g_btn_back;

static void *g_icon_action;
static void *g_icon_uninstall;
static void *g_icon_scriptorium;
static void *g_icon_back;

// UI state (main thread only)
static WlJsonVal *g_index_root;
static char       g_index_url[HUB_PATH_CAP];
static HubUiMod  *g_mods;
static int        g_mod_count;
static int        g_page;
static int        g_selected = -1;
static char       g_default_icon[MAX_PATH];

// Static text buffers outliving calls
static char g_row_texts[HUB_PAGE_SIZE][192];
static char g_page_text[32];
static char g_detail_name[256];
static char g_detail_ver_author[256];
static char g_detail_mod_status[128];
static char g_detail_size[128];
static char g_detail_tags[320];
static char g_detail_deps[320];
static char g_detail_desc[1152];
static char g_bottom_count[128];
static char g_status_wrapped[600];

// Shared worker -> UI state
static CRITICAL_SECTION g_cs;
static int              g_cs_init;
static unsigned int     g_index_gen;
static struct {
    int        busy;
    unsigned int op_gen;
    int        status_dirty;
    char       status_text[512];

    int        index_ready;
    int        index_ok;
    WlJsonVal *index_root;
    char       index_url[HUB_PATH_CAP];
    char       index_err[512];

    int        op_done;
    int        op_ok;
    char       op_msg[512];

    int        icons_dirty;
} g_async;

static void ensure_cs(void) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cs);
        g_cs_init = 1;
    }
}

// Game's UTF-8 decoder (0x444e40) loops infinitely on malformed bytes, and '&'
// is consumed as a mnemonic prefix. Clean untrusted remote strings before display.
static void sanitize_text(char *out, size_t cap, const char *in) {
    if (!out || cap == 0) return;
    if (!in) { out[0] = '\0'; return; }
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o + 4 < cap) {
        unsigned char c = *p;
        if (c == '&') {
            out[o++] = '+';
            p++;
        } else if (c < 0x80) {
            if (c == '\r') { p++; continue; }
            if (c < 0x20 && c != '\n' && c != '\t') out[o++] = ' ';
            else out[o++] = (char)c;
            p++;
        } else if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80 && c >= 0xc2) {
            out[o++] = (char)p[0]; out[o++] = (char)p[1];
            p += 2;
        } else if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            out[o++] = (char)p[0]; out[o++] = (char)p[1]; out[o++] = (char)p[2];
            p += 3;
        } else if ((c & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
            out[o++] = (char)p[0]; out[o++] = (char)p[1]; out[o++] = (char)p[2]; out[o++] = (char)p[3];
            p += 4;
        } else {
            out[o++] = '?';
            p++;
        }
    }
    out[o] = '\0';
}

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
    out[o] = '\0';
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

static void panel_set_text(void *panel, const char *text) {
    if (!panel) return;
    void **vtbl = *(void ***)panel;
    ((TextPanelSetText_t)vtbl[54])(panel, text ? text : "", 0, 0, 0);
}

static void panel_set_visible(void *panel, int visible) {
    if (!panel) return;
    void **vtbl = *(void ***)panel;
    ((PanelSetVisible_t)vtbl[4])(panel, visible ? 1 : 0);
}

static void button_set_checked(void *btn, char checked) {
    if (btn) ((ButtonSetChecked_t)ADDR_BUTTON_SET_CHECKED)(btn, checked);
}

static void button_set_enabled(void *btn, int enabled) {
    if (!btn) return;
    void *fn = (*(void ***)btn)[7];
    if (fn == (void *)ADDR_BUTTON_SET_ENABLED) {
        ((ButtonSetEnabled_t)fn)(btn, enabled ? 1 : 0);
    }
}

static void button_set_text(void *btn, const char *text) {
    if (btn) ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(btn, 0, text ? text : "");
}

static void set_status_text(const char *msg) {
    char clean[512];
    sanitize_text(clean, sizeof(clean), msg);
    wrap_text(g_status_wrapped, sizeof(g_status_wrapped), clean, STATUS_CHARS);
    panel_set_text(g_lbl_status, g_status_wrapped);
}

static void *load_png(const char *path) {
    void *chain = *(void **)ADDR_LOADER_CHAIN;
    if (!chain || !path || !path[0]) return NULL;
    void *drawable = NULL;
    ((LoadPng_t)ADDR_LOAD_PNG)(chain, &drawable, path);
    return drawable;
}

static void drawable_set_fit(void *drawable) {
    if (!drawable) return;
    int mode3[3] = { 1, 1, 1 };
    ((DrawableSetFit_t)(*(void ***)drawable)[3])(drawable, mode3);
}

static void *create_drawable_slot(void *s, const char *path, int x, int y, int w, int h) {
    void *d = (path && path[0]) ? load_png(path) : NULL;
    if (d) drawable_set_fit(d);

    int in_rect[4] = { x, y, w, h };
    int out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(s, out_rect, in_rect);

    void *panel = ((New_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return NULL;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, s, out_rect, d);
    return panel;
}

// Swap [panel+0x130] (DrawablePanel::m_drawable) in-place so z-order under
// row badges stays intact. Refcount semantics verified in 0x47a6e0.
static void update_drawable_slot(void *panel, const char *path) {
    if (!panel) return;
    void *new_d = (path && path[0]) ? load_png(path) : NULL;
    if (!new_d && g_default_icon[0] && (!path || strcmp(path, g_default_icon) != 0)) {
        new_d = load_png(g_default_icon);
    }
    if (new_d) drawable_set_fit(new_d);

    void *old_d = *(void **)((char *)panel + 0x130);
    if (old_d) {
        LONG *rc = (LONG *)((char *)old_d + 4);
        if (--(*rc) == 0) {
            ((DrawableDtor_t)(*(void ***)old_d)[0])(old_d);
        }
    }
    *(void **)((char *)panel + 0x130) = new_d;
    *(int *)((char *)panel + 0x28) = 1;
}

static void *ui_icon(void *s, const char *name, int x, int y, int w, int h) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\ui\\%s_%d.png", g_wl.root, name, w);
    (void)h;
    void *d = load_png(path);
    if (!d) return NULL;
    drawable_set_fit(d);

    int in_rect[4] = { x, y, w, h };
    int out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(s, out_rect, in_rect);

    void *panel = ((New_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return NULL;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, s, out_rect, d);
    return panel;
}

// Same deferred deletion helper used by ui_mods.c (verified 0x4382e0 / 0x9d7e18).
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

static void hub_icon_cache_path(const char *id, char *out, size_t cap) {
    snprintf(out, cap, "%s\\cache\\hub_icons\\%s.png", g_wl.root, id);
}

static void refresh_local_states(void) {
    char mods_dir[MAX_PATH];
    snprintf(mods_dir, sizeof(mods_dir), "%s\\mods", g_wl.root);
    for (int i = 0; i < g_mod_count; i++) {
        HubUiMod *m = &g_mods[i];
        HubLocalState st;
        hub_inspect_local_mod(mods_dir, m->id, &st);
        m->installed = st.installed;
        m->hub_managed = st.hub_managed;
        m->installed_enabled = st.enabled;
        snprintf(m->installed_version, sizeof(m->installed_version), "%s", st.version);
        m->update_available = st.installed && (hub_compare_versions(m->version, st.version) > 0);
        m->compatible = (!m->req_wl[0] || hub_compare_versions(WOLOLOADER_VERSION, m->req_wl) >= 0);
    }
}

static void format_size(int64_t bytes, char *out, size_t cap) {
    if (bytes >= 1024 * 1024) {
        snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, cap, "%lld B", (long long)bytes);
    }
}

static void format_row_label(int idx, char *out, size_t cap) {
    const HubUiMod *m = &g_mods[idx];
    const char *prefix = (idx == g_selected) ? "> " : "  ";
    const char *badge = m->update_available ? " [Update]"
                      : (m->installed ? " [Installed]" : "");
    char name_part[48];
    snprintf(name_part, sizeof(name_part), "%s", m->name);
    if (strlen(name_part) > 22) {
        name_part[19] = '.'; name_part[20] = '.'; name_part[21] = '.'; name_part[22] = '\0';
    }
    if (m->author[0]) {
        char auth_part[24];
        snprintf(auth_part, sizeof(auth_part), "%s", m->author);
        if (strlen(auth_part) > 12) {
            auth_part[9] = '.'; auth_part[10] = '.'; auth_part[11] = '.'; auth_part[12] = '\0';
        }
        snprintf(out, cap, "%s%s v%s - %s%s", prefix, name_part, m->version, auth_part, badge);
    } else {
        snprintf(out, cap, "%s%s v%s%s", prefix, name_part, m->version, badge);
    }
}

static void update_buttons_state(void) {
    EnterCriticalSection(&g_cs);
    int busy = g_async.busy;
    LeaveCriticalSection(&g_cs);

    int total_pages = g_mod_count > 0 ? (g_mod_count + HUB_PAGE_SIZE - 1) / HUB_PAGE_SIZE : 1;
    button_set_enabled(g_btn_prev, !busy && g_page > 0);
    button_set_enabled(g_btn_next, !busy && g_page + 1 < total_pages);
    button_set_enabled(g_btn_refresh, !busy);
    button_set_enabled(g_btn_back, busy != HUB_BUSY_INSTALL && busy != HUB_BUSY_UNINSTALL);

    if (g_readonly) {
        panel_set_visible(g_btn_action, 0);
        panel_set_visible(g_btn_uninstall, 0);
        if (g_btn_scriptorium) panel_set_visible(g_btn_scriptorium, 0);
        panel_set_visible(g_icon_action, 0);
        panel_set_visible(g_icon_uninstall, 0);
        if (g_icon_scriptorium) panel_set_visible(g_icon_scriptorium, 0);
        return;
    }

    panel_set_visible(g_btn_action, 1);
    panel_set_visible(g_btn_uninstall, 1);
    if (g_btn_scriptorium) panel_set_visible(g_btn_scriptorium, 1);
    panel_set_visible(g_icon_action, 1);
    panel_set_visible(g_icon_uninstall, 1);
    if (g_icon_scriptorium) panel_set_visible(g_icon_scriptorium, 1);

    if (g_selected < 0 || g_selected >= g_mod_count || busy) {
        button_set_text(g_btn_action, "Install");
        button_set_enabled(g_btn_action, 0);
        button_set_enabled(g_btn_uninstall, 0);
        return;
    }

    const HubUiMod *m = &g_mods[g_selected];
    // A newer version of a mod that came with the setup (no .hub.json) can be updated too:
    // op_worker_proc moves the local copy to wololoader\trash first.
    if (m->installed && !m->hub_managed && !m->update_available) {
        button_set_text(g_btn_action, "Local Mod");
        button_set_enabled(g_btn_action, 0);
        button_set_enabled(g_btn_uninstall, 0);
    } else if (m->update_available) {
        button_set_text(g_btn_action, m->compatible ? "Update" : "Incompatible");
        button_set_enabled(g_btn_action, m->compatible);
        button_set_enabled(g_btn_uninstall, m->hub_managed);
    } else if (m->installed) {
        button_set_text(g_btn_action, "Installed");
        button_set_enabled(g_btn_action, 0);
        button_set_enabled(g_btn_uninstall, m->hub_managed);
    } else {
        button_set_text(g_btn_action, m->compatible ? "Install" : "Incompatible");
        button_set_enabled(g_btn_action, m->compatible);
        button_set_enabled(g_btn_uninstall, 0);
    }
}

static void update_details_ui(void) {
    if (g_selected < 0 || g_selected >= g_mod_count) {
        snprintf(g_detail_name, sizeof(g_detail_name), g_mod_count == 0 ? "No hub mods loaded" : "Select a mod");
        g_detail_ver_author[0] = '\0';
        g_detail_mod_status[0] = '\0';
        g_detail_size[0] = '\0';
        g_detail_tags[0] = '\0';
        g_detail_deps[0] = '\0';
        snprintf(g_detail_desc, sizeof(g_detail_desc),
                 "Use Refresh to fetch the latest Mod Hub index.");
        update_drawable_slot(g_detail_icon, g_default_icon);
    } else {
        const HubUiMod *m = &g_mods[g_selected];
        snprintf(g_detail_name, sizeof(g_detail_name), "%s", m->name);
        if (m->author[0]) {
            snprintf(g_detail_ver_author, sizeof(g_detail_ver_author),
                     "Version %s  -  by %s", m->version, m->author);
        } else {
            snprintf(g_detail_ver_author, sizeof(g_detail_ver_author), "Version %s", m->version);
        }

        if (m->installed && !m->hub_managed && m->update_available) {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status),
                     "Status: Update available (local v%s; Update keeps a copy in wololoader\\trash)", m->installed_version);
        } else if (m->installed && !m->hub_managed) {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status),
                     "Status: Local mod (not managed by Hub)");
        } else if (m->update_available) {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status),
                     "Status: Update available (installed: v%s)", m->installed_version);
        } else if (m->installed) {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status),
                     "Status: Installed (%s)", m->installed_enabled ? "enabled" : "disabled");
        } else if (!m->compatible) {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status),
                     "Status: Requires Wololoader %s", m->req_wl);
        } else {
            snprintf(g_detail_mod_status, sizeof(g_detail_mod_status), "Status: Available to install");
        }

        char szbuf[32];
        format_size(m->size_bytes, szbuf, sizeof(szbuf));
        snprintf(g_detail_size, sizeof(g_detail_size), "Size: %s", szbuf);
        snprintf(g_detail_tags, sizeof(g_detail_tags), "Tags: %s", m->tags[0] ? m->tags : "none");
        snprintf(g_detail_deps, sizeof(g_detail_deps), "Dependencies: %s", m->depends[0] ? m->depends : "none");

        if (m->description[0]) {
            wrap_text(g_detail_desc, sizeof(g_detail_desc), m->description, DESC_CHARS);
        } else {
            snprintf(g_detail_desc, sizeof(g_detail_desc), "No description.");
        }

        char icon_file[MAX_PATH];
        hub_icon_cache_path(m->id, icon_file, sizeof(icon_file));
        if (hub_file_exists(icon_file)) {
            char key[160];
            snprintf(key, sizeof(key), "hub_%s", m->id);
            update_drawable_slot(g_detail_icon, wl_icon_scaled(icon_file, key, 96));
        } else {
            update_drawable_slot(g_detail_icon, wl_icon_scaled(g_default_icon, "default", 96));
        }
    }

    panel_set_text(g_lbl_name, g_detail_name);
    panel_set_text(g_lbl_ver_author, g_detail_ver_author);
    panel_set_text(g_lbl_mod_status, g_detail_mod_status);
    panel_set_text(g_lbl_size, g_detail_size);
    panel_set_text(g_lbl_tags, g_detail_tags);
    panel_set_text(g_lbl_deps, g_detail_deps);
    panel_set_text(g_lbl_desc, g_detail_desc);
    update_buttons_state();
}

static void refresh_list_ui(void) {
    int total_pages = g_mod_count > 0 ? (g_mod_count + HUB_PAGE_SIZE - 1) / HUB_PAGE_SIZE : 1;
    if (g_page >= total_pages) g_page = total_pages - 1;
    if (g_page < 0) g_page = 0;

    snprintf(g_page_text, sizeof(g_page_text), "Page %d / %d", g_page + 1, total_pages);
    panel_set_text(g_lbl_page, g_page_text);

    snprintf(g_bottom_count, sizeof(g_bottom_count),
             "Wololoader v%s  -  %d hub mod%s",
             WOLOLOADER_VERSION, g_mod_count, g_mod_count == 1 ? "" : "s");
    panel_set_text(g_lbl_bottom_count, g_bottom_count);

    int start = g_page * HUB_PAGE_SIZE;
    for (int r = 0; r < HUB_PAGE_SIZE; r++) {
        int idx = start + r;
        if (idx < g_mod_count) {
            const HubUiMod *m = &g_mods[idx];
            format_row_label(idx, g_row_texts[r], sizeof(g_row_texts[r]));
            button_set_text(g_row_btns[r], g_row_texts[r]);
            button_set_checked(g_row_btns[r], idx == g_selected ? 1 : 0);
            panel_set_visible(g_row_btns[r], 1);
            panel_set_visible(g_row_icons[r], 1);

            char icon_file[MAX_PATH];
            hub_icon_cache_path(m->id, icon_file, sizeof(icon_file));
            if (hub_file_exists(icon_file)) {
                char key[160];
                snprintf(key, sizeof(key), "hub_%s", m->id);
                update_drawable_slot(g_row_icons[r], wl_icon_scaled(icon_file, key, 40));
            } else {
                update_drawable_slot(g_row_icons[r], wl_icon_scaled(g_default_icon, "default", 40));
            }

            if (m->update_available) {
                panel_set_visible(g_row_badge_on[r], 0);
                panel_set_visible(g_row_badge_off[r], 1);
            } else if (m->installed) {
                panel_set_visible(g_row_badge_on[r], 1);
                panel_set_visible(g_row_badge_off[r], 0);
            } else {
                panel_set_visible(g_row_badge_on[r], 0);
                panel_set_visible(g_row_badge_off[r], 0);
            }
        } else {
            panel_set_visible(g_row_btns[r], 0);
            panel_set_visible(g_row_icons[r], 0);
            panel_set_visible(g_row_badge_on[r], 0);
            panel_set_visible(g_row_badge_off[r], 0);
        }
    }

    update_details_ui();
}

// ---- Worker threads -------------------------------------------------------

typedef struct {
    unsigned int gen;
    int          force_icons;
    int          count;
    struct {
        char id[128];
        char url[HUB_PATH_CAP];
    } items[];
} IconJob;

static DWORD WINAPI icon_worker_proc(LPVOID arg) {
    IconJob *job = (IconJob *)arg;
    char dir[HUB_PATH_CAP];
    snprintf(dir, sizeof(dir), "%s\\cache", g_wl.root);
    hub_mkdir_p(dir);
    snprintf(dir, sizeof(dir), "%s\\cache\\hub_icons", g_wl.root);
    hub_mkdir_p(dir);

    for (int i = 0; i < job->count; i++) {
        EnterCriticalSection(&g_cs);
        unsigned int cur_gen = g_index_gen;
        LeaveCriticalSection(&g_cs);
        if (cur_gen != job->gen) break;

        if (!job->items[i].url[0]) continue;
        char dst[HUB_PATH_CAP];
        hub_icon_cache_path(job->items[i].id, dst, sizeof(dst));
        if (!job->force_icons && hub_file_exists(dst)) continue;

        char *data = NULL;
        size_t len = 0;
        char err[256] = "";
        if (hub_fetch_stream(job->items[i].url, HUB_MAX_ICON_BYTES, NULL, &data, &len,
                             NULL, NULL, NULL, err, sizeof(err)) == 0 && data && len > 0) {
            if (hub_atomic_write_file(dst, data, len, err, sizeof(err)) == 0) {
                wl_log("hub: icon cached for '%s' (%zu bytes)", job->items[i].id, len);
                EnterCriticalSection(&g_cs);
                if (g_index_gen == job->gen) g_async.icons_dirty = 1;
                LeaveCriticalSection(&g_cs);
            } else {
                wl_log("hub: icon save failed for '%s': %s", job->items[i].id, err);
            }
        } else {
            wl_log("hub: icon fetch failed for '%s': %s", job->items[i].id, err);
        }
        free(data);
    }
    free(job);
    return 0;
}

typedef struct {
    unsigned int gen;
    int          force_refresh;
} FetchIndexJob;

static DWORD WINAPI fetch_index_worker(LPVOID arg) {
    FetchIndexJob *job = (FetchIndexJob *)arg;
    char eff_url[HUB_PATH_CAP] = "";
    char cfg_url[HUB_PATH_CAP] = "";
    int env_override = 0;
    hub_resolve_index_url(g_wl.game_dir, eff_url, sizeof(eff_url), cfg_url, sizeof(cfg_url), &env_override);

    wl_log("hub: fetching index from %s", eff_url);
    char err[512] = "";
    WlJsonVal *root = hub_fetch_and_parse_index(eff_url, err, sizeof(err));

    int ok_flag = (root != NULL);
    EnterCriticalSection(&g_cs);
    if (job->gen == g_index_gen) {
        g_async.busy = HUB_IDLE;
        g_async.index_ready = 1;
        g_async.index_ok = ok_flag;
        if (root) {
            wl_json_free(g_async.index_root);
            g_async.index_root = root;
            snprintf(g_async.index_url, sizeof(g_async.index_url), "%s", eff_url);
            g_async.index_err[0] = '\0';
            root = NULL;
        } else {
            snprintf(g_async.index_err, sizeof(g_async.index_err), "%s", err);
        }
    }
    LeaveCriticalSection(&g_cs);

    if (root) wl_json_free(root);
    if (!ok_flag && err[0]) wl_log("hub: index fetch failed: %s", err);
    else wl_log("hub: index fetched ok");
    free(job);
    return 0;
}

static void start_index_fetch(int force_refresh) {
    EnterCriticalSection(&g_cs);
    if (g_async.busy != HUB_IDLE && g_async.busy != HUB_BUSY_INDEX) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    unsigned int gen = ++g_index_gen;
    g_async.busy = HUB_BUSY_INDEX;
    g_async.op_gen = gen;
    g_async.index_ready = 0;
    snprintf(g_async.status_text, sizeof(g_async.status_text), "Fetching Mod Hub index...");
    g_async.status_dirty = 1;
    LeaveCriticalSection(&g_cs);

    update_buttons_state();
    set_status_text("Fetching Mod Hub index...");

    FetchIndexJob *job = malloc(sizeof(FetchIndexJob));
    if (!job) return;
    job->gen = gen;
    job->force_refresh = force_refresh;
    HANDLE h = CreateThread(NULL, 0, fetch_index_worker, job, 0, NULL);
    if (h) CloseHandle(h);
    else free(job);
}

static int dll_inflate_cb(const unsigned char *in, size_t in_len, size_t max_out,
                          unsigned char **out, size_t *out_len, void *user) {
    (void)user;
    return wl_game_inflate_raw_bounded(in, in_len, max_out, out, out_len);
}

static void dll_progress_cb(const char *stage, const char *mod_id,
                            size_t done_bytes, size_t total_bytes, void *user) {
    (void)user;
    char msg[512];
    const char *label = mod_id ? mod_id : "mod";
    if (strcmp(stage, "downloading") == 0) {
        if (total_bytes > 0) {
            int pct = (int)((done_bytes * 100ULL) / total_bytes);
            if (pct > 100) pct = 100;
            snprintf(msg, sizeof(msg), "Downloading %s (%d%%)...", label, pct);
        } else {
            snprintf(msg, sizeof(msg), "Downloading %s (%zu KB)...", label, done_bytes / 1024u);
        }
    } else if (strcmp(stage, "verifying") == 0) {
        snprintf(msg, sizeof(msg), "Verifying %s...", label);
    } else if (strcmp(stage, "extracting") == 0) {
        snprintf(msg, sizeof(msg), "Extracting %s...", label);
    } else {
        snprintf(msg, sizeof(msg), "Installing %s...", label);
    }

    EnterCriticalSection(&g_cs);
    snprintf(g_async.status_text, sizeof(g_async.status_text), "%s", msg);
    g_async.status_dirty = 1;
    LeaveCriticalSection(&g_cs);
}

static void dll_log_cb(const char *msg, void *user) {
    (void)user;
    wl_log("%s", msg);
}

typedef struct {
    char       id[128];
    char       name[128];
    int        is_uninstall;
    char       index_url[HUB_PATH_CAP];
} OpJob;

static DWORD WINAPI op_worker_proc(LPVOID arg) {
    OpJob *job = (OpJob *)arg;
    char mods_dir[MAX_PATH];
    snprintf(mods_dir, sizeof(mods_dir), "%s\\mods", g_wl.root);

    int ok = 0;
    char msg[512] = "";

    if (job->is_uninstall) {
        wl_log("hub: uninstalling '%s'", job->id);
        char trash_dir[HUB_PATH_CAP] = "";
        char err[256] = "";
        int status = 0;
        if (hub_uninstall_mod(g_wl.game_dir, mods_dir, job->id, trash_dir, sizeof(trash_dir),
                              &status, err, sizeof(err)) == 0) {
            ok = 1;
            snprintf(msg, sizeof(msg),
                     "Uninstalled %s (moved to trash). Return to Main Menu to apply.", job->name);
            wl_log("hub: uninstalled '%s' -> %s", job->id, trash_dir);
        } else {
            snprintf(msg, sizeof(msg), "Uninstall failed: %s", err);
            wl_log("hub: uninstall failed for '%s': %s", job->id, err);
        }
    } else {
        HubInstallResult ir;
        EnterCriticalSection(&g_cs);
        WlJsonVal *root = g_index_root;
        LeaveCriticalSection(&g_cs);

        // Updating a local copy (e.g. an example mod from the setup): keep it in the trash
        // and remember whether it was on, then install the Hub version in its place.
        HubLocalState before;
        hub_inspect_local_mod(mods_dir, job->id, &before);
        int was_local = before.installed && !before.hub_managed;
        if (was_local) {
            char src[MAX_PATH], trash[MAX_PATH], dst[MAX_PATH];
            SYSTEMTIME t;
            GetLocalTime(&t);
            snprintf(src, sizeof src, "%s\\%s", mods_dir, job->id);
            snprintf(trash, sizeof trash, "%s\\trash", g_wl.root);
            CreateDirectoryA(trash, NULL);
            snprintf(dst, sizeof dst, "%s\\%s-local-%04d%02d%02d-%02d%02d%02d", trash, job->id,
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            if (!MoveFileA(src, dst)) {
                wl_log("hub: could not move local '%s' to the trash (%lu)", job->id, GetLastError());
                was_local = -1;
            } else {
                wl_log("hub: local '%s' v%s kept in %s", job->id, before.version, dst);
            }
        }

        if (was_local == -1) {
            snprintf(msg, sizeof(msg), "Update failed: the local folder could not be moved to wololoader\\trash (in use?)");
        } else if (hub_install_mod(g_wl.game_dir, mods_dir, root, job->index_url, job->id, 0,
                            WOLOLOADER_VERSION, dll_inflate_cb, NULL,
                            dll_progress_cb, NULL, dll_log_cb, NULL, &ir) == 0) {
            ok = 1;
            if (was_local == 1 && before.has_enabled && !before.enabled) {
                char mdir[MAX_PATH];
                snprintf(mdir, sizeof mdir, "%s\\%s", mods_dir, job->id);
                wl_set_mod_enabled(mdir, FALSE);   // it was off before the update
            }
            if (ir.installed_dep_count > 0) {
                snprintf(msg, sizeof(msg),
                         "%s %s v%s (+%d dep%s). Return to Main Menu to apply.",
                         ir.updated ? "Updated" : "Installed",
                         job->name, ir.version,
                         ir.installed_dep_count, ir.installed_dep_count == 1 ? "" : "s");
            } else {
                snprintf(msg, sizeof(msg),
                         "%s %s v%s. Return to Main Menu to apply.",
                         ir.updated ? "Updated" : "Installed",
                         job->name, ir.version);
            }
            hub_install_result_free(&ir);
        } else {
            snprintf(msg, sizeof(msg), "Install failed: %s", ir.err);
            wl_log("hub: install failed for '%s': %s", job->id, ir.err);
        }
    }

    EnterCriticalSection(&g_cs);
    g_async.busy = HUB_IDLE;
    g_async.op_done = 1;
    g_async.op_ok = ok;
    snprintf(g_async.op_msg, sizeof(g_async.op_msg), "%s", msg);
    LeaveCriticalSection(&g_cs);

    free(job);
    return 0;
}

static void start_install_or_uninstall(int is_uninstall) {
    if (g_readonly || g_selected < 0 || g_selected >= g_mod_count) return;
    const HubUiMod *m = &g_mods[g_selected];

    EnterCriticalSection(&g_cs);
    if (g_async.busy != HUB_IDLE) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    g_async.busy = is_uninstall ? HUB_BUSY_UNINSTALL : HUB_BUSY_INSTALL;
    g_async.op_done = 0;
    snprintf(g_async.status_text, sizeof(g_async.status_text), "%s %s...",
             is_uninstall ? "Uninstalling" : "Starting download for", m->name);
    g_async.status_dirty = 1;
    LeaveCriticalSection(&g_cs);

    update_buttons_state();
    set_status_text(g_async.status_text);

    OpJob *job = malloc(sizeof(OpJob));
    if (!job) return;
    snprintf(job->id, sizeof(job->id), "%s", m->id);
    snprintf(job->name, sizeof(job->name), "%s", m->name);
    job->is_uninstall = is_uninstall;
    snprintf(job->index_url, sizeof(job->index_url), "%s", g_index_url);
    HANDLE h = CreateThread(NULL, 0, op_worker_proc, job, 0, NULL);
    if (h) CloseHandle(h);
    else free(job);
}

static void apply_new_index(WlJsonVal *root, const char *url) {
    wl_json_free(g_index_root);
    g_index_root = root;
    snprintf(g_index_url, sizeof(g_index_url), "%s", url);

    free(g_mods);
    g_mods = NULL;
    g_mod_count = 0;

    const WlJsonVal *arr = wl_json_get(root, "mods");
    size_t n = arr ? wl_json_len(arr) : 0;
    if (n > 0) {
        g_mods = calloc(n, sizeof(HubUiMod));
    }

    for (size_t i = 0; i < n && g_mods; i++) {
        const WlJsonVal *jm = wl_json_at(arr, i);
        if (!jm || jm->type != WL_JSON_OBJECT) continue;
        const char *id = wl_json_str(wl_json_get(jm, "id"), "");
        if (!hub_is_valid_id(id)) continue;

        HubUiMod *m = &g_mods[g_mod_count++];
        snprintf(m->id, sizeof(m->id), "%s", id);
        sanitize_text(m->name, sizeof(m->name), wl_json_str(wl_json_get(jm, "name"), id));
        sanitize_text(m->version, sizeof(m->version), wl_json_str(wl_json_get(jm, "version"), "0.1"));
        sanitize_text(m->author, sizeof(m->author), wl_json_str(wl_json_get(jm, "author"), ""));
        sanitize_text(m->description, sizeof(m->description), wl_json_str(wl_json_get(jm, "description"), ""));
        sanitize_text(m->req_wl, sizeof(m->req_wl), wl_json_str(wl_json_get(jm, "requires_wololoader"), ""));
        snprintf(m->icon_raw, sizeof(m->icon_raw), "%s", wl_json_str(wl_json_get(jm, "icon"), ""));
        const WlJsonVal *v_sz = wl_json_get(jm, "size");
        m->size_bytes = (v_sz && v_sz->type == WL_JSON_NUMBER) ? v_sz->u.num.ival : 0;

        const WlJsonVal *v_tags = wl_json_get(jm, "tags");
        for (size_t k = 0; k < wl_json_len(v_tags); k++) {
            const char *t = wl_json_str(wl_json_at(v_tags, k), NULL);
            if (!t || !t[0]) continue;
            char ct[64];
            sanitize_text(ct, sizeof(ct), t);
            if (m->tags[0]) strncat(m->tags, ", ", sizeof(m->tags) - strlen(m->tags) - 1);
            strncat(m->tags, ct, sizeof(m->tags) - strlen(m->tags) - 1);
        }

        const WlJsonVal *v_deps = wl_json_get(jm, "depends");
        for (size_t k = 0; k < wl_json_len(v_deps); k++) {
            const char *d = wl_json_str(wl_json_at(v_deps, k), NULL);
            if (!d || !d[0]) continue;
            char cd[64];
            sanitize_text(cd, sizeof(cd), d);
            if (m->depends[0]) strncat(m->depends, ", ", sizeof(m->depends) - strlen(m->depends) - 1);
            strncat(m->depends, cd, sizeof(m->depends) - strlen(m->depends) - 1);
        }
    }

    refresh_local_states();
    g_page = 0;
    g_selected = g_mod_count > 0 ? 0 : -1;
    refresh_list_ui();

    // Launch background icon fetcher for entries that have icons
    if (g_mod_count > 0) {
        IconJob *ij = malloc(sizeof(IconJob) + (size_t)g_mod_count * sizeof(ij->items[0]));
        if (ij) {
            EnterCriticalSection(&g_cs);
            ij->gen = g_index_gen;
            LeaveCriticalSection(&g_cs);
            ij->force_icons = 1;
            ij->count = g_mod_count;
            for (int i = 0; i < g_mod_count; i++) {
                snprintf(ij->items[i].id, sizeof(ij->items[i].id), "%s", g_mods[i].id);
                if (g_mods[i].icon_raw[0]) {
                    hub_resolve_rel_url(g_index_url, g_mods[i].icon_raw,
                                        ij->items[i].url, sizeof(ij->items[i].url));
                } else {
                    ij->items[i].url[0] = '\0';
                }
            }
            HANDLE h = CreateThread(NULL, 0, icon_worker_proc, ij, 0, NULL);
            if (h) CloseHandle(h);
            else free(ij);
        }
    }
}

// ---- ScreenPanel callbacks ------------------------------------------------

static void __fastcall on_tick(void *self, void *edx) {
    (void)edx;
    g_orig_tick(self);

    int status_dirty = 0;
    char status_buf[512] = "";

    int index_ready = 0, index_ok = 0;
    WlJsonVal *new_root = NULL;
    char new_url[HUB_PATH_CAP] = "";
    char index_err[512] = "";

    int op_done = 0, op_ok = 0;
    char op_msg[512] = "";

    int icons_dirty = 0;

    EnterCriticalSection(&g_cs);
    if (g_async.status_dirty) {
        status_dirty = 1;
        snprintf(status_buf, sizeof(status_buf), "%s", g_async.status_text);
        g_async.status_dirty = 0;
    }
    if (g_async.index_ready) {
        index_ready = 1;
        index_ok = g_async.index_ok;
        new_root = g_async.index_root;
        g_async.index_root = NULL;
        snprintf(new_url, sizeof(new_url), "%s", g_async.index_url);
        snprintf(index_err, sizeof(index_err), "%s", g_async.index_err);
        g_async.index_ready = 0;
    }
    if (g_async.op_done) {
        op_done = 1;
        op_ok = g_async.op_ok;
        snprintf(op_msg, sizeof(op_msg), "%s", g_async.op_msg);
        g_async.op_done = 0;
    }
    if (g_async.icons_dirty) {
        icons_dirty = 1;
        g_async.icons_dirty = 0;
    }
    LeaveCriticalSection(&g_cs);

    if (status_dirty) {
        set_status_text(status_buf);
    }
    if (index_ready) {
        if (index_ok && new_root) {
            apply_new_index(new_root, new_url);
            char msg[128];
            snprintf(msg, sizeof(msg), "Loaded %d mod%s from Mod Hub.",
                     g_mod_count, g_mod_count == 1 ? "" : "s");
            set_status_text(msg);
        } else {
            wl_json_free(new_root);
            update_buttons_state();
            char msg[600];
            snprintf(msg, sizeof(msg), "Offline / error: %s. Press Refresh to retry.",
                     index_err[0] ? index_err : "Could not reach Mod Hub");
            set_status_text(msg);
        }
    }
    if (op_done) {
        if (op_ok) g_mods_dirty = 1;
        refresh_local_states();
        refresh_list_ui();
        set_status_text(op_msg);
    }
    if (icons_dirty) {
        refresh_list_ui();
    }
}

static int __fastcall on_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (code == EV_LEFT_CLICK && panel) {
        if (panel == g_btn_back) {
            EnterCriticalSection(&g_cs);
            int busy = g_async.busy;
            if (busy == HUB_BUSY_INDEX) {
                g_index_gen++;
                g_async.busy = HUB_IDLE;
            }
            LeaveCriticalSection(&g_cs);
            if (busy == HUB_BUSY_INSTALL || busy == HUB_BUSY_UNINSTALL) {
                return 1;
            }
            wl_log("hub: closing (mods_dirty=%d)", g_mods_dirty);
            void *old = g_hub_screen;
            g_hub_screen = NULL;
            wl_mods_return_from_hub(g_mods_dirty);
            if (old) defer_delete_screen(old);
            return 1;
        }
        if (panel == g_btn_refresh) {
            start_index_fetch(1);
            return 1;
        }
        if (panel == g_btn_prev) {
            if (g_page > 0) {
                g_page--;
                g_selected = g_page * HUB_PAGE_SIZE;
                refresh_list_ui();
            }
            return 1;
        }
        if (panel == g_btn_next) {
            int total_pages = g_mod_count > 0 ? (g_mod_count + HUB_PAGE_SIZE - 1) / HUB_PAGE_SIZE : 1;
            if (g_page + 1 < total_pages) {
                g_page++;
                g_selected = g_page * HUB_PAGE_SIZE;
                refresh_list_ui();
            }
            return 1;
        }
        if (panel == g_btn_scriptorium && g_btn_scriptorium && !g_readonly) {
            set_status_text(wl_launch_scriptorium_args("--hub")
                            ? "Mod Hub opened in Scriptorium."
                            : "Scriptorium.exe not found.");
            return 1;
        }
        if (panel == g_btn_action && !g_readonly) {
            start_install_or_uninstall(0);
            return 1;
        }
        if (panel == g_btn_uninstall && !g_readonly) {
            start_install_or_uninstall(1);
            return 1;
        }
        for (int r = 0; r < HUB_PAGE_SIZE; r++) {
            if (panel == g_row_btns[r]) {
                int idx = g_page * HUB_PAGE_SIZE + r;
                if (idx >= 0 && idx < g_mod_count) {
                    g_selected = idx;
                    refresh_list_ui();
                }
                return 1;
            }
        }
    }
    return g_orig_event(self, panel, code, a, b);
}

static void *create_hub_screen(void) {
    wl_log("hub: create screen");
    void *s = ((New_t)ADDR_GAME_NEW)(SCREENPANEL_SIZE);
    if (!s) return NULL;
    memset(s, 0, SCREENPANEL_SIZE);
    ((ScreenCtor_t)ADDR_SCREEN_CTOR)(s, "Wololoader Mod Hub");

    memcpy(g_vtable_block, (void *)(SCREENPANEL_VTBL - 4), sizeof g_vtable_block);
    g_orig_tick  = (Tick_t)g_vtable[VT_TICK];
    g_orig_event = (Event_t)g_vtable[VT_EVENT];
    g_vtable[VT_TICK]  = (void *)on_tick;
    g_vtable[VT_EVENT] = (void *)on_event;
    *(void **)s = g_vtable;

    ((SetBg_t)ADDR_SET_BACKGROUND)(s, "scr2p", 0xc3a4, 0);
    int ref[2] = { REF_W, REF_H };
    ((SetRef_t)ADDR_SET_REF_SIZE)(s, ref);

    snprintf(g_default_icon, sizeof g_default_icon,
             "%s\\resources\\_common\\textures\\ui\\tech-tree-icon.png",
             g_wl.game_dir);

    add_label(s, "Mod Hub", 0, 15, REF_W, 40, 1, 1, 1, 0);

    AddButton_t button = (AddButton_t)ADDR_ADD_BUTTON;
    g_btn_refresh = button(s, s, "Refresh", NULL, 990, 22, 150, 40, 0, 0, 0);

    // Left list rows (8 per page) + paging controls
    for (int r = 0; r < HUB_PAGE_SIZE; r++) {
        int y = LIST_Y + r * 52;
        g_row_icons[r] = create_drawable_slot(s, wl_icon_scaled(g_default_icon, "default", 40),
                                              LIST_X, y + 2, 40, 40);
        g_row_btns[r] = button(s, s, "", NULL, LIST_X + 48, y, LIST_W - 48, 44, 0, 0, 0);
        g_row_badge_on[r]  = ui_icon(s, "badge_on", LIST_X + 24, y + 26, 18, 18);
        g_row_badge_off[r] = ui_icon(s, "badge_off", LIST_X + 24, y + 26, 18, 18);
        panel_set_visible(g_row_btns[r], 0);
        panel_set_visible(g_row_icons[r], 0);
        panel_set_visible(g_row_badge_on[r], 0);
        panel_set_visible(g_row_badge_off[r], 0);
    }

    g_btn_prev = button(s, s, "< Prev", NULL, LIST_X, 515, 85, 34, 0, 0, 0);
    g_lbl_page = add_label(s, "Page 1 / 1", LIST_X + 90, 515, 160, 34, 0xa, 1, 1, 0);
    g_btn_next = button(s, s, "Next >", NULL, LIST_X + 255, 515, 85, 34, 0, 0, 0);

    // Right detail area
    g_detail_icon = create_drawable_slot(s, wl_icon_scaled(g_default_icon, "default", 96),
                                         DET_X, LIST_Y, 96, 96);
    g_lbl_name       = add_label(s, "", DET_X + 112, LIST_Y + 4, DET_W - 112, 40, 1, 0, 0, 0);
    g_lbl_ver_author = add_label(s, "", DET_X + 112, LIST_Y + 50, DET_W - 112, 26, 0xa, 0, 0, 0);
    g_lbl_mod_status = add_label(s, "", DET_X + 112, LIST_Y + 78, DET_W - 112, 24, 0xa, 0, 0, 0);
    g_lbl_size       = add_label(s, "", DET_X, LIST_Y + 114, DET_W, 24, 0xa, 0, 0, 0);
    g_lbl_tags       = add_label(s, "", DET_X, LIST_Y + 140, DET_W, 24, 0xa, 0, 0, 0);
    g_lbl_deps       = add_label(s, "", DET_X, LIST_Y + 166, DET_W, 24, 0xa, 0, 0, 0);
    g_lbl_desc       = add_label(s, "", DET_X, LIST_Y + 202, DET_W, 270, 0xa, 0, 0, 0);

    // Bottom status + actions
    snprintf(g_bottom_count, sizeof(g_bottom_count), "Wololoader v%s  -  Mod Hub", WOLOLOADER_VERSION);
    g_lbl_bottom_count = add_label(s, g_bottom_count, LIST_X, BOTTOM_Y, LIST_W, 24, 0xa, 0, 1, 0);
    g_lbl_status       = add_label(s, "Fetching Mod Hub index...", DET_X, BOTTOM_Y, DET_W, 44, 0xa, 0, 0, 0);

    char sc_exe[MAX_PATH];
    snprintf(sc_exe, sizeof(sc_exe), "%s\\Scriptorium.exe", g_wl.root);
    int has_creator = GetFileAttributesA(sc_exe) != INVALID_FILE_ATTRIBUTES;

    g_btn_action    = button(s, s, "Install", NULL, 225, 650, 230, 48, 0, 0, 0);
    g_btn_uninstall = button(s, s, "Uninstall", NULL, 465, 656, 180, 40, 0, 0, 0);
    if (has_creator) {
        g_btn_scriptorium = button(s, s, "Open in Scriptorium", NULL, 655, 656, 225, 40, 0, 0, 0);
    } else {
        g_btn_scriptorium = NULL;
    }
    g_btn_back = button(s, s, "Back", NULL, 1040, 659, 100, 34, 0, 0, 0);

    g_icon_action      = ui_icon(s, "check", 237, 660, 28, 28);
    g_icon_uninstall   = ui_icon(s, "cross", 473, 665, 22, 22);
    g_icon_scriptorium = has_creator ? ui_icon(s, "quill", 663, 665, 22, 22) : NULL;
    g_icon_back        = ui_icon(s, "back", 1048, 666, 20, 20);

    return s;
}

void wl_hub_open(void *return_screen, int readonly) {
    ensure_cs();
    g_return_mods_screen = return_screen;
    g_readonly = readonly ? 1 : 0;
    g_mods_dirty = 0;

    if (!g_hub_screen) {
        g_hub_screen = create_hub_screen();
    }
    if (!g_hub_screen) return;

    ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, g_hub_screen);
    wl_log("hub: screen opened (readonly=%d)", g_readonly);
    refresh_list_ui();
    start_index_fetch(0);
}
