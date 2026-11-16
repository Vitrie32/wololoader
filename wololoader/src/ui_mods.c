// In-game "Mods" screen, opened from the main menu's Steam Workshop button.
// Minecraft-Forge-style layout: clickable mod list on the left with icons,
// and detail pane on the right with full mod metadata and big icon.
// Addresses: AoK HD.exe 5.8, see re/FINDINGS.md ("Mods screen v2").
#include "wololoader.h"
#include <stdio.h>
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

#define ADDR_BUTTON_SET_CHECKED   0x477450  // thiscall(btn, char checked), ret 4
#define ADDR_BUTTON_SET_TEXT      0x479ce0  // thiscall(btn, int slot, const char *text), ret 8
#define ADDR_LOAD_PNG             0x49f1f0  // thiscall(chain, &out, path), ret 8
#define ADDR_LOADER_CHAIN         0xada9f4  // [ptr] ui::ImageFileDrawableLoader
#define ADDR_SCALE_RECT           0x491190  // thiscall(scr, out[4], in[4]), ret 8
#define ADDR_DRAWABLE_PANEL_CTOR  0x47a5d0  // thiscall(mem, parent, rect[4], drawable), ret 0xc
#define DRAWABLE_PANEL_SIZE       0x134

#define EV_LEFT_CLICK             3
#define REF_W                     1366
#define REF_H                     768
#define MAX_ROWS                  9
// Layout inside the parchment of the scr2p background (reference units).
#define LIST_X 225
#define LIST_W 340
#define LIST_Y 85
#define DET_X 610
#define DET_W 530
#define BOTTOM_Y 588
#define DESC_CHARS 96

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

typedef void (__thiscall *ButtonSetChecked_t)(void *btn, char checked);
typedef void (__thiscall *ButtonSetText_t)(void *btn, int slot, const char *text);
typedef void (__thiscall *TextPanelSetText_t)(void *panel, const char *text, int z0, int z1, int z2);
typedef void (__thiscall *LoadPng_t)(void *chain, void **out, const char *path);
typedef void (__thiscall *DrawableSetFit_t)(void *drawable, const int *mode);
typedef void *(__thiscall *DrawablePanelCtor_t)(void *mem, void *parent, const int rect[4], void *drawable);
typedef void *(__thiscall *ScaleRect_t)(void *screen, int out[4], const int in[4]);

static void *g_screen;
// [0] = RTTI CompleteObjectLocator (dynamic_cast reads vtable[-1]), then slots.
static void *g_vtable_block[VT_SLOTS + 1];
#define g_vtable (g_vtable_block + 1)
static Event_t g_orig_event;

static void *g_btn_toggle;
static void *g_btn_vanilla;   // Vanilla Mode on/off (takes effect on restart)

// Button text shows the state the NEXT start will use (the flag file).
static const char *vanilla_text(void) {
    return wl_vanilla_flag_exists() ? "Vanilla Mode: On" : "Vanilla Mode: Off";
}
static void *g_btn_open;
static void *g_btn_creator;  // starts Scriptorium.exe (mod creator)
static void *g_btn_back;

static void *g_lbl_bottom_status;
static void *g_lbl_bottom_note;
static char g_bottom_note[600];

static int g_displayed_mods;
static int g_selected_mod = -1;

static void *g_row_btns[MAX_ROWS];
static void *g_row_icons[MAX_ROWS];
static void *g_detail_icons[MAX_ROWS];

// Detail label panels
static void *g_lbl_name;
static void *g_lbl_ver_author;
static void *g_lbl_status;
static void *g_lbl_civs;
static void *g_lbl_folder;
static void *g_lbl_desc;

// Static string buffers (must outlive the call)
static char g_btn_texts[MAX_ROWS][192];
static char g_more_text[64];
static char g_bottom_status[128];
static char g_default_icon[MAX_PATH];

static char g_detail_name[256];
static char g_detail_ver_author[256];
static char g_detail_status[64];
static char g_detail_civs[64];
static char g_detail_folder[256];
static char g_detail_desc[1024];

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
    TextPanelSetText_t fn = (TextPanelSetText_t)vtbl[54];
    fn(panel, text ? text : "", 0, 0, 0);
}

// SetChecked alone shows nothing on these buttons; also disable the selected
// row (button vtable slot 61, thiscall(btn, char)) so it looks pressed/dim.
typedef void (__thiscall *ButtonSetDisabled_t)(void *btn, char disabled);
static void button_set_checked(void *btn, char checked) {
    if (!btn) return;
    ((ButtonSetChecked_t)ADDR_BUTTON_SET_CHECKED)(btn, checked);
    void *set_disabled = (*(void ***)btn)[61];
    if (set_disabled == (void *)0x479840) ((ButtonSetDisabled_t)set_disabled)(btn, checked);
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

static void drawable_set_fit(void *drawable) {
    if (!drawable) return;
    int mode3[3] = { 1, 1, 1 };
    DrawableSetFit_t fn = (DrawableSetFit_t)(*(void ***)drawable)[3];
    fn(drawable, mode3);
}

static void *create_image_panel(void *s, const char *path, int x, int y, int w, int h) {
    if (!path || !path[0]) return NULL;
    void *d = load_png(path);
    if (!d && g_default_icon[0] && strcmp(path, g_default_icon) != 0) {
        d = load_png(g_default_icon);
        path = g_default_icon;
    }
    if (!d) return NULL;
    drawable_set_fit(d);

    int in_rect[4] = { x, y, w, h };
    int out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(s, out_rect, in_rect);

    void *panel = ((New_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return NULL;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, s, out_rect, d);
    wl_log("mods: icon %s -> %p", path, panel);
    return panel;
}

// The label's wrap flag has no visible effect, so break lines ourselves.
// Font 0xa is about 5 reference units per character.
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

static void format_details(int idx) {
    if (idx < 0 || idx >= g_wl.listed_count) {
        snprintf(g_detail_name, sizeof g_detail_name, "No mods installed");
        g_detail_ver_author[0] = 0;
        g_detail_status[0] = 0;
        g_detail_civs[0] = 0;
        g_detail_folder[0] = 0;
        snprintf(g_detail_desc, sizeof g_detail_desc, "Put mod folders into wololoader\\mods and restart the game.");
    } else {
        const WlMod *m = &g_wl.mods[idx];
        snprintf(g_detail_name, sizeof g_detail_name, "%s", m->name);

        const char *ver = m->version;
        if ((ver[0] == 'v' || ver[0] == 'V') && ver[1] >= '0' && ver[1] <= '9') ver++;

        if (ver[0] && m->author[0]) {
            snprintf(g_detail_ver_author, sizeof g_detail_ver_author, "Version %s  -  by %s", ver, m->author);
        } else if (ver[0]) {
            snprintf(g_detail_ver_author, sizeof g_detail_ver_author, "Version %s", ver);
        } else if (m->author[0]) {
            snprintf(g_detail_ver_author, sizeof g_detail_ver_author, "by %s", m->author);
        } else {
            g_detail_ver_author[0] = 0;
        }

        snprintf(g_detail_status, sizeof g_detail_status, "Status: %s", m->enabled ? "Enabled" : "Disabled");
        snprintf(g_detail_civs, sizeof g_detail_civs, "Civilizations: %d", m->civ_count);
        snprintf(g_detail_folder, sizeof g_detail_folder, "Folder: wololoader\\mods\\%s", m->id);

        if (m->description[0]) {
            wrap_text(g_detail_desc, sizeof g_detail_desc, m->description, DESC_CHARS);
        } else {
            snprintf(g_detail_desc, sizeof g_detail_desc, "No description.");
        }
    }
}

static void format_row_text(int idx, char *out, size_t cap) {
    if (idx < 0 || idx >= g_wl.listed_count) {
        out[0] = 0;
        return;
    }
    const WlMod *m = &g_wl.mods[idx];
    const char *prefix = (idx == g_selected_mod) ? "> " : "  ";
    if (m->enabled) {
        snprintf(out, cap, "%s%s", prefix, m->name);
    } else {
        snprintf(out, cap, "%s%s (disabled)", prefix, m->name);
    }
}

static void update_row_button(int idx) {
    if (idx < 0 || idx >= g_displayed_mods || !g_row_btns[idx]) return;
    format_row_text(idx, g_btn_texts[idx], sizeof g_btn_texts[idx]);
    ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(g_row_btns[idx], 0, g_btn_texts[idx]);
}

static void update_toggle_button(void) {
    if (!g_btn_toggle) return;
    const char *text = "Disable Mod";
    if (g_selected_mod >= 0 && g_selected_mod < g_wl.listed_count) {
        text = g_wl.mods[g_selected_mod].enabled ? "Disable Mod" : "Enable Mod";
    }
    ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(g_btn_toggle, 0, text);
}

static void update_details(int idx) {
    format_details(idx);
    panel_set_text(g_lbl_name, g_detail_name);
    panel_set_text(g_lbl_ver_author, g_detail_ver_author);
    panel_set_text(g_lbl_status, g_detail_status);
    panel_set_text(g_lbl_civs, g_detail_civs);
    panel_set_text(g_lbl_folder, g_detail_folder);
    panel_set_text(g_lbl_desc, g_detail_desc);
}

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

// Read-only view for the multiplayer lobby ("Active Mods" button, lobby.c): the
// buttons that change mods are hidden, Back returns to the lobby screen.
static void *g_return_screen;
static void set_readonly(int on) {
    void *btns[] = { g_btn_toggle, g_btn_vanilla, g_btn_creator, g_btn_open };
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++)
        if (btns[i]) panel_set_visible(btns[i], !on);
    if (!on) g_return_screen = NULL;
}

static int __fastcall on_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (code == EV_LEFT_CLICK && panel) {
        if (panel == g_btn_vanilla) {
            BOOL on = !wl_vanilla_flag_exists();
            if (wl_vanilla_set(on)) {
                ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(g_btn_vanilla, 0, vanilla_text());
                snprintf(g_bottom_note, sizeof g_bottom_note, "%s",
                         on ? "Vanilla Mode on: restart the game. Mods and patches stay off (online play)."
                            : "Vanilla Mode off: restart the game to load your mods again.");
                panel_set_text(g_lbl_bottom_note, g_bottom_note);
                wl_log("Vanilla Mode set to %s (next start)", on ? "on" : "off");
            }
            return 1;
        }
        if (panel == g_btn_toggle) {
            if (g_selected_mod >= 0 && g_selected_mod < g_wl.listed_count) {
                WlMod *m = &g_wl.mods[g_selected_mod];
                BOOL new_state = !m->enabled;
                if (wl_set_mod_enabled(m->dir, new_state)) {
                    m->enabled = new_state;
                    int count = 0;
                    for (int j = 0; j < g_wl.listed_count; j++) {
                        if (g_wl.mods[j].enabled) count++;
                    }
                    g_wl.mod_count = count;

                    update_toggle_button();
                    update_row_button(g_selected_mod);
                    update_details(g_selected_mod);

                    snprintf(g_bottom_status, sizeof g_bottom_status,
                             "Wololoader v%s  -  %d of %d mods enabled",
                             WOLOLOADER_VERSION, g_wl.mod_count, g_wl.listed_count);
                    panel_set_text(g_lbl_bottom_status, g_bottom_status);

                    snprintf(g_bottom_note, sizeof g_bottom_note,
                             "Mod status saved. Restart the game to apply changes.");
                    panel_set_text(g_lbl_bottom_note, g_bottom_note);

                    wl_log("mods: mod '%s' %s", m->id, new_state ? "enabled" : "disabled");
                } else {
                    wl_log("mods: failed to update mod.json for '%s'", m->id);
                }
            }
            return 1;
        }
        if (panel == g_btn_creator) {
            snprintf(g_bottom_note, sizeof g_bottom_note, "%s",
                     wl_launch_scriptorium() ? "Scriptorium opened. Restart the game after saving mods."
                                             : "Scriptorium.exe not found in the wololoader folder.");
            panel_set_text(g_lbl_bottom_note, g_bottom_note);
            return 1;
        }
        if (panel == g_btn_open) {
            char dir[MAX_PATH];
            snprintf(dir, MAX_PATH, "%s\\mods", g_wl.root);
            wl_open_folder(dir);
            return 1;
        }
        if (panel == g_btn_back) {
            if (g_return_screen) {                      // opened read-only from the lobby
                void *back = g_return_screen;
                set_readonly(0);
                ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, back);
                // Remove the Mods screen from the screen list: left below the lobby, it was
                // shown again when the lobby closed (Cancel/Back, seen in game 2026-11-15).
                // It is rebuilt on the next open.
                defer_delete_screen(g_screen);
                g_screen = NULL;
            } else {
                ((SetCurrentByName_t)ADDR_SET_CURRENT_BY_NAME)(*(void **)ADDR_SCREEN_MGR, "Main Menu");
            }
            return 1;
        }
        // Read-only (lobby): the change buttons are hidden; ignore them anyway.
        if (g_return_screen && (panel == g_btn_toggle || panel == g_btn_vanilla ||
                                panel == g_btn_creator || panel == g_btn_open))
            return 1;
        for (int i = 0; i < g_displayed_mods; i++) {
            if (panel == g_row_btns[i]) {
                if (g_selected_mod != i) {
                    int prev = g_selected_mod;
                    g_selected_mod = i;
                    if (prev >= 0 && prev < g_displayed_mods) {
                        button_set_checked(g_row_btns[prev], 0);
                        update_row_button(prev);
                        if (g_detail_icons[prev]) {
                            panel_set_visible(g_detail_icons[prev], 0);
                        }
                    }
                    button_set_checked(g_row_btns[i], 1);
                    update_row_button(i);
                    if (g_detail_icons[i]) {
                        panel_set_visible(g_detail_icons[i], 1);
                    }
                    update_details(i);
                    update_toggle_button();
                } else {
                    button_set_checked(g_row_btns[i], 1);
                }
                return 1;
            }
        }
    }
    return g_orig_event(self, panel, code, a, b);
}

static void *create_screen(void) {
    wl_log("mods: create");
    wl_civs_ensure();  // so a build error can be shown
    void *s = ((New_t)ADDR_GAME_NEW)(SCREENPANEL_SIZE);
    wl_log("mods: new -> %p", s);
    if (!s) return NULL;
    memset(s, 0, SCREENPANEL_SIZE);
    ((ScreenCtor_t)ADDR_SCREEN_CTOR)(s, "Wololoader Mods");
    wl_log("mods: ctor ok");

    memcpy(g_vtable_block, (void *)(SCREENPANEL_VTBL - 4), sizeof g_vtable_block);
    g_orig_event = (Event_t)g_vtable[VT_EVENT];
    g_vtable[VT_EVENT] = (void *)on_event;
    *(void **)s = g_vtable;

    ((SetBg_t)ADDR_SET_BACKGROUND)(s, "scr2p", 0xc3a4, 0);
    wl_log("mods: background ok");
    int ref[2] = { REF_W, REF_H };
    ((SetRef_t)ADDR_SET_REF_SIZE)(s, ref);
    wl_log("mods: ref size ok");

    snprintf(g_default_icon, sizeof g_default_icon,
             "%s\\resources\\_common\\textures\\ui\\tech-tree-icon.png",
             g_wl.game_dir);

    // Title "Mods" centred at (0,15,1366,40), font 1.
    add_label(s, "Mods", 0, 15, REF_W, 40, 1, 1, 1, 0);
    wl_log("mods: title ok");

    int count = g_wl.listed_count < MAX_ROWS ? g_wl.listed_count : MAX_ROWS;
    g_displayed_mods = count;
    g_selected_mod = count > 0 ? 0 : -1;

    // Left list: up to 9 rows. Row i at y = LIST_Y + i*52:
    // icon 40x40 at x=LIST_X; button at x=LIST_X+48, w=LIST_W-48, h=44
    AddButton_t button = (AddButton_t)ADDR_ADD_BUTTON;
    for (int i = 0; i < count; i++) {
        const WlMod *m = &g_wl.mods[i];
        int y = LIST_Y + i * 52;
        const char *icon_path = m->icon[0] ? m->icon : g_default_icon;
        const char *icon_id = m->icon[0] ? m->id : "default";

        g_row_icons[i] = create_image_panel(s, wl_icon_scaled(icon_path, icon_id, 40), LIST_X, y + 2, 40, 40);

        format_row_text(i, g_btn_texts[i], sizeof g_btn_texts[i]);
        g_row_btns[i] = button(s, s, g_btn_texts[i], NULL, LIST_X + 48, y, LIST_W - 48, 44, 0, 0, 0);

        // Detail icon 96x96 at (DET_X,LIST_Y) - create one per listed mod
        g_detail_icons[i] = create_image_panel(s, wl_icon_scaled(icon_path, icon_id, 96), DET_X, LIST_Y, 96, 96);
        if (g_detail_icons[i]) {
            panel_set_visible(g_detail_icons[i], i == 0 ? 1 : 0);
        }
    }

    if (g_wl.listed_count > MAX_ROWS) {
        snprintf(g_more_text, sizeof g_more_text, "... and %d more", g_wl.listed_count - MAX_ROWS);
        add_label(s, g_more_text, LIST_X + 48, LIST_Y + MAX_ROWS * 52, LIST_W - 48, 24, 0xa, 0, 1, 0);
    }

    // Right details pane:
    if (count > 0) {
        format_details(0);
        button_set_checked(g_row_btns[0], 1);
    } else {
        format_details(-1);
    }

    // Name label font 1, left/top, at (DET_X+112, LIST_Y+4, DET_W-112, 40)
    g_lbl_name = add_label(s, g_detail_name, DET_X + 112, LIST_Y + 4, DET_W - 112, 40, 1, 0, 0, 0);

    // "Version X  -  by Author" font 0xa at (DET_X+112, LIST_Y+50, DET_W-112, 26)
    g_lbl_ver_author = add_label(s, g_detail_ver_author, DET_X + 112, LIST_Y + 50, DET_W - 112, 26, 0xa, 0, 0, 0);

    // font-0xa left-aligned lines at x=DET_X, w=DET_W, h=26:
    // y=197 "Status: Enabled|Disabled"
    g_lbl_status = add_label(s, g_detail_status, DET_X, LIST_Y + 112, DET_W, 26, 0xa, 0, 0, 0);
    // y=225 "Civilizations: N"
    g_lbl_civs = add_label(s, g_detail_civs, DET_X, LIST_Y + 140, DET_W, 26, 0xa, 0, 0, 0);
    // y=253 "Folder: wololoader\mods\<id>"
    g_lbl_folder = add_label(s, g_detail_folder, DET_X, LIST_Y + 168, DET_W, 26, 0xa, 0, 0, 0);

    // description label at (DET_X, LIST_Y+216, DET_W, 280) font 0xa, left, top, wrap on
    g_lbl_desc = add_label(s, g_detail_desc, DET_X, LIST_Y + 216, DET_W, 280, 0xa, 0, 0, 0);

    // Bottom:
    // "Wololoader v<ver>  -  <enabled> of <listed> mods enabled" left at (LIST_X, BOTTOM_Y, LIST_W, 24) font 0xa
    snprintf(g_bottom_status, sizeof g_bottom_status,
             "Wololoader v%s  -  %d of %d mods enabled",
             WOLOLOADER_VERSION, g_wl.mod_count, g_wl.listed_count);
    g_lbl_bottom_status = add_label(s, g_bottom_status, LIST_X, BOTTOM_Y, LIST_W, 24, 0xa, 0, 1, 0);

    // "Restart the game after adding or changing mods." at (DET_X, BOTTOM_Y, DET_W, 24) left font 0xa
    // Mod changes are rebuilt automatically at start; a failed build is shown here.
    if (g_wl_build_error[0])
        snprintf(g_bottom_note, sizeof g_bottom_note, "Build error: %s", g_wl_build_error);
    else
        snprintf(g_bottom_note, sizeof g_bottom_note, "Changes to mods apply after restarting the game.");
    g_lbl_bottom_note = add_label(s, g_bottom_note, DET_X, BOTTOM_Y, DET_W, 24, 0xa, 0, 1, 0);

    wl_log("mods: labels ok");

    // Five buttons: Toggle, Vanilla Mode, Mod Creator, Open Mods Folder, Back
    // (166 wide, 25 apart, spanning LIST_X=225 .. 1140 = DET_X+DET_W).
    const char *toggle_text = (count > 0 && !g_wl.mods[0].enabled) ? "Enable Mod" : "Disable Mod";
    // Mod Creator only when <root>\Scriptorium.exe is there (release installers leave it out for now);
    // without it the four other buttons share the row (210 wide, 25 apart).
    char sc_exe[MAX_PATH];
    snprintf(sc_exe, MAX_PATH, "%s\\Scriptorium.exe", g_wl.root);
    if (GetFileAttributesA(sc_exe) != INVALID_FILE_ATTRIBUTES) {
        g_btn_toggle  = button(s, s, toggle_text, NULL, 225, 660, 166, 40, 0, 0, 0);
        g_btn_vanilla = button(s, s, vanilla_text(), NULL, 416, 660, 166, 40, 0, 0, 0);
        g_btn_creator = button(s, s, "Mod Creator", NULL, 607, 660, 166, 40, 0, 0, 0);
        g_btn_open    = button(s, s, "Mods Folder", NULL, 798, 660, 166, 40, 0, 0, 0);
        g_btn_back    = button(s, s, "Back", NULL, 989, 660, 151, 40, 0, 0, 0);
    } else {
        g_btn_creator = NULL;
        g_btn_toggle  = button(s, s, toggle_text, NULL, 225, 660, 210, 40, 0, 0, 0);
        g_btn_vanilla = button(s, s, vanilla_text(), NULL, 460, 660, 210, 40, 0, 0, 0);
        g_btn_open    = button(s, s, "Mods Folder", NULL, 695, 660, 210, 40, 0, 0, 0);
        g_btn_back    = button(s, s, "Back", NULL, 930, 660, 210, 40, 0, 0, 0);
    }
    wl_log("Mods screen %p (toggle %p, open %p, back %p)", s, g_btn_toggle, g_btn_open, g_btn_back);

    return s;
}

// Replaces the main menu's Steam Workshop handler. Never calls the original.
static void __fastcall hook_modmgr_button(void *main_menu) {
    (void)main_menu;
    wl_log("mods: main menu button");
    if (!g_screen) g_screen = create_screen();
    wl_log("mods: screen %p", g_screen);
    if (g_screen) {
        if (g_selected_mod >= 0 && g_selected_mod < g_displayed_mods) {
            button_set_checked(g_row_btns[g_selected_mod], 1);
            update_row_button(g_selected_mod);
            update_toggle_button();
        }
        ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, g_screen);
    }
    wl_log("mods: shown");
}

// Game start while the read-only view is open (lobby countdown): forget the
// lobby screen (it is being replaced by the game) and restore the normal buttons.
void wl_mods_readonly_reset(void) {
    if (g_return_screen) { set_readonly(0); wl_log("mods: read-only view reset (game started)"); }
}

void wl_mods_open_readonly(void *return_screen) {
    if (!g_screen) g_screen = create_screen();
    if (!g_screen) return;
    g_return_screen = return_screen;
    set_readonly(1);
    if (g_selected_mod >= 0 && g_selected_mod < g_displayed_mods) {
        button_set_checked(g_row_btns[g_selected_mod], 1);
        update_row_button(g_selected_mod);
    }
    ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, g_screen);
    wl_log("mods: shown read-only (return to %p)", return_screen);
}

void wl_install_mods_screen(void) {
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1
    if (!wl_hook(ADDR_MODMGR_HANDLER, hook_modmgr_button, 5)) wl_log("Mods screen hook FAILED");
}
