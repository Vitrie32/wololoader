// Main menu "Wololoader vX - N mods / What's new? Click here" label (on the empty sign top right).
// Detours MainMenuScreen::MainMenuScreen (0x6e0c30) and, after the original
// constructor ran, adds a TextPanel through the screen's own text factory.
// Addresses verified against AoK HD.exe 5.8 (see re/FINDINGS.md).
//
// The label is clickable (opens the changelog / update window, changelog.c): it gets a copy of
// the TextPanel vtable (0x9b28a4, 60 slots + RTTI slot) whose slot 36 (+0x90, left button down,
// called by Panel::mouse_down 0x4841a0 with (x, y, a, b), ret 0x10) only remembers the click.
// The window is opened from MainMenuScreen::tick (vtable 0x9d66a8 slot 17), outside the input
// dispatch. The same tick changes the label when the update check (changelog.c) finds a newer
// release. Verified 2026-11-28, see FINDINGS "Changelog window and self-update".
#include "modsettings.h"
#include "wololoader.h"
#include <stdio.h>
#include <string.h>

#define ADDR_MAINMENU_CTOR   0x6e0c30  // __thiscall(this, a, b), ret 8
#define ADDR_ADD_TEXT_PANEL  0x48ff40  // ScreenPanel text factory, ret 0x30
#define ADDR_MAKE_FONTS      0x472500  // fills ref_ptr<Font>[4], ret 4
#define FONT_MENU_SMALL      0xa       // font the menu uses for 20px text
#define ADDR_SET_TEXT_STYLE  0x48d390  // screen: (pair*, pair*) -> +0x194..0x1a0, ret 8
#define ADDR_SET_TEXT_FLAG   0x48d3c0  // screen: (int) -> +0x1a4, ret 4
#define ADDR_APPLY_STYLE     0x48d550  // screen: (panel), sets panel state, ret 4
#define ADDR_STYLE_PAIR      0xada500  // global {dword, dword} the ctor passes twice
#define TEXTPANEL_VTBL       0x9b28a4  // TextPanel vtable, 60 slots, RTTI at [-1]
#define TEXTPANEL_SLOTS      60
#define VT_MOUSE_LEFT_DOWN   36        // +0x90: 0x496bc0 (scrollbar notify), thiscall(x, y, a, b) ret 0x10
#define ADDR_MAINMENU_VTBL   0x9d66a8
#define VT_TICK              17        // MainMenuScreen::tick 0x6e8bb0 (or autostart.c's hook), plain ret

typedef void *(__thiscall *MainMenuCtor_t)(void *self, void *a, void *b);
typedef void *(__thiscall *AddTextPanel_t)(void *screen, void *parent, const char *text,
                                            int x, int y, int w, int h, void *fonts,
                                            int a24, int a28, int a2c, int str_id, int a34);
typedef void *(__thiscall *MakeFonts_t)(void *fonts, int font_id);
typedef void (__thiscall *SetTextStyle_t)(void *screen, DWORD *a, DWORD *b);
typedef void (__thiscall *SetTextFlag_t)(void *screen, int flag);
typedef void (__thiscall *ApplyStyle_t)(void *screen, void *panel);

typedef void (__thiscall *MenuTick_t)(void *self);

static MainMenuCtor_t g_orig_ctor;
static MenuTick_t g_orig_tick;
static char g_label[256];
static char g_default_label[256];  // the label we made; other code (mpguard) may replace it
static void *g_label_panel[2]; // last main menu label lines (for wl_menu_set_label)
static void *g_label_vtbl_block[TEXTPANEL_SLOTS + 1];
static volatile LONG g_label_clicked;
static int g_update_shown;    // the label already names the update

// Left button pressed on the label: open the window from the next main menu tick.
static int __fastcall label_mouse_down(void *self, void *edx, int x, int y, int a, int b) {
    (void)self; (void)edx; (void)x; (void)y; (void)a; (void)b;
    InterlockedExchange(&g_label_clicked, 1);
    return 1;   // handled
}

static void make_label_clickable(void *panel) {
    if (!panel || *(DWORD *)panel != TEXTPANEL_VTBL) {
        wl_log("menu: label vtable %p unexpected, label not clickable", panel ? *(void **)panel : NULL);
        return;
    }
    if (!g_label_vtbl_block[1]) {
        memcpy(g_label_vtbl_block, (void *)(TEXTPANEL_VTBL - 4), sizeof g_label_vtbl_block);
        g_label_vtbl_block[1 + VT_MOUSE_LEFT_DOWN] = (void *)label_mouse_down;
    }
    *(void ***)panel = g_label_vtbl_block + 1;
}

static void compose_label(void) {
    // Two short lines: the sign is ~200 px wide (about 34 characters of this font).
    const char *upd = wl_update_version();
    char line2[96];
    if (upd) snprintf(line2, sizeof line2, "Update v%s available - click", upd);
    else if (wl_mp_rehearsal()) snprintf(line2, sizeof line2, "MP rehearsal - what's new?");
    else snprintf(line2, sizeof line2, "What's new? Click here");
    if (g_wl_vanilla)
        snprintf(g_label, sizeof g_label, "Wololoader v%s - Vanilla\n%s", WOLOLOADER_VERSION, line2);
    else
        snprintf(g_label, sizeof g_label, "Wololoader v%s - %d mod%s\n%s",
                 WOLOLOADER_VERSION, g_wl.mod_count, g_wl.mod_count == 1 ? "" : "s", line2);
    lstrcpynA(g_default_label, g_label, sizeof g_default_label);
}

// Texts from other code ("Wololoader v0.7.3 - reload error (restart game)") are one line:
// break after the version so they fit the sign.
static void two_lines(char *t) {
    if (strchr(t, '\n')) return;
    char *sep = strstr(t, " - ");
    if (sep) memmove(sep + 1, sep + 3, strlen(sep + 3) + 1), *sep = '\n';
}

static void split_label(char *l1, char *l2) {
    lstrcpynA(l1, g_label, 256);
    char *nl = strchr(l1, '\n');
    if (nl) { *nl = 0; lstrcpynA(l2, nl + 1, 256); } else l2[0] = 0;
}

// Hand cursor while the mouse is over a panel. Panel::mouse_move (0x4842d0, slot 28) hands the
// ref-counted cursor at [panel+0xec] to the cursor manager [0xad20c0] (0x456520) when the
// point is inside the panel; NULL there means the parent's cursor. The game's cursors are
// CursorSet [[0xada4b8]] +8: array of 19 ref-counted Cursor objects (+4 refs, +0xc index),
// frames of mcursors.slp (interface 51000.slp); 3 is the gauntlet hand (checked by decoding
// the SLP and dumping the array in game, 2026-11-28). The panel releases its ref when freed.
#define ADDR_CURSOR_SET   0xada4b8
#define CURSOR_HAND       3
#define CURSOR_COUNT      19
static void set_hand_cursor(void *panel) {
    void **holder = *(void ***)ADDR_CURSOR_SET;
    if (!panel || !holder || !*holder) return;
    DWORD *set = *holder;
    void **arr = (void **)set[2], **end = (void **)set[3];
    if (!arr || end - arr != CURSOR_COUNT || !arr[CURSOR_HAND]) return;
    BYTE *p = panel;
    if (*(void **)(p + 0xec)) return;   // a cursor of its own already
    ((LONG *)arr[CURSOR_HAND])[1]++;
    *(void **)(p + 0xec) = arr[CURSOR_HAND];
}

// The plaque behind the label (DrawablePanel, same recipe as ui_mods.c panel_from_drawable;
// addresses in FINDINGS "DrawablePanel"). Made before the labels, so they draw on top.
#define ADDR_GAME_NEW             0x8d47df  // game's operator new (cdecl)
#define ADDR_LOADER_CHAIN         0xada9f4  // [ptr] ui::ImageFileDrawableLoader
#define ADDR_LOAD_PNG             0x49f1f0  // thiscall(chain, &out, path), ret 8
#define ADDR_SCALE_RECT           0x491190  // thiscall(scr, out[4], in[4]), ret 8
#define ADDR_DRAWABLE_PANEL_CTOR  0x47a5d0  // thiscall(mem, parent, rect[4], drawable), ret 0xc
#define DRAWABLE_PANEL_SIZE       0x134
typedef void *(__cdecl *GameNew_t)(unsigned size);
typedef void (__thiscall *LoadPng_t)(void *chain, void **out, const char *path);
typedef void (__thiscall *ScaleRect_t)(void *screen, int out[4], const int in[4]);
typedef void *(__thiscall *DrawablePanelCtor_t)(void *mem, void *parent, const int rect[4], void *drawable);
typedef void (__thiscall *DrawableSetFit_t)(void *drawable, const int *mode);
static void add_plaque(void *screen, int x, int y) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\ui\\menu_plaque.png", g_wl.root);
    void *chain = *(void **)ADDR_LOADER_CHAIN;
    if (!chain || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        wl_log("menu: %s missing, label without plaque", path);
        return;
    }
    void *d = NULL;
    ((LoadPng_t)ADDR_LOAD_PNG)(chain, &d, path);
    if (!d) return;
    int mode[3] = { 1, 1, 1 };
    ((DrawableSetFit_t)(*(void ***)d)[3])(d, mode);
    int in_rect[4] = { x, y, 200, 50 }, out_rect[4] = { 0, 0, 0, 0 };
    ((ScaleRect_t)ADDR_SCALE_RECT)(screen, out_rect, in_rect);
    void *panel = ((GameNew_t)ADDR_GAME_NEW)(DRAWABLE_PANEL_SIZE);
    if (!panel) return;
    memset(panel, 0, DRAWABLE_PANEL_SIZE);
    ((DrawablePanelCtor_t)ADDR_DRAWABLE_PANEL_CTOR)(panel, screen, out_rect, d);
    set_hand_cursor(panel);
}

static void add_label(void *screen) {
    BYTE *s = screen;
    int cur_w = *(int *)(s + 0xc0), cur_h = *(int *)(s + 0xc4);
    int ref_w = *(int *)(s + 0x130), ref_h = *(int *)(s + 0x134);
    wl_log("MainMenuScreen %p cur=%dx%d ref=%dx%d", screen, cur_w, cur_h, ref_w, ref_h);

    // Main menu appeared: close the loading window (fade out / delay if build error)
    wl_loadscreen_close();

    // Coordinates are in reference units (= client size when the screen was made); fall back
    // to 1366x768 if unset. Where it goes (1366x768 background.jpg):
    //  - 0.7.2 bottom left (10, h-28): ran into the Exit button (main-menu.json 174,631 230x137).
    //  - 0.7.3 on the small empty sign top right (843..1052, 77..114): the game writes its own
    //    text there in some setups (seen with Steam online, 2026-11-28; the ctor's sign panel
    //    0x6e1ddf at 782,20 gets text from 0x6eb650), so the two texts overlapped.
    //  - now: our own plaque (ui\menu_plaque.png, 200x50) on the free cobblestones between the
    //    Exit sign and the copyright parchment, above the game's "Version" text (0x6e19b7 at
    //    375,720 250x20). No button, sign or text panel of the game is there.
    // The background is stretched to the window, so the position is scaled; the PNG is drawn
    // at its own size (see ui_mods.c ui_icon), so the size is not.
    int rw = (ref_w > 100 && ref_w < 10000) ? ref_w : 1366;
    int rh = (ref_h > 100 && ref_h < 10000) ? ref_h : 768;
    int px = 392 * rw / 1366, py = 664 * rh / 768;
    add_plaque(screen, px, py);
    int lx = px + 6, ly = py + 7, lw = 188, lh = 36;

    // ref_ptr<Font>[4]; the factory copies it, so leaking our refs is harmless.
    static BYTE fonts[16];
    ((MakeFonts_t)ADDR_MAKE_FONTS)(fonts, FONT_MENU_SMALL);

    // Same sequence the constructor uses around its own text panels
    // (0x6e190a..0x6e1a3a): style setup, create, then apply style.
    DWORD style[2] = { ((DWORD *)ADDR_STYLE_PAIR)[0], ((DWORD *)ADDR_STYLE_PAIR)[1] };
    DWORD style2[2] = { style[0], style[1] };
    wl_log("text style pair = %08lx %08lx", style[0], style[1]);
    ((SetTextStyle_t)ADDR_SET_TEXT_STYLE)(screen, style, style2);
    ((SetTextFlag_t)ADDR_SET_TEXT_FLAG)(screen, 0);

    // Two labels, one per line (a 0x48ff40 label shows a single line: '\n' is not a break).
    char l1[256], l2[256];
    split_label(l1, l2);
    for (int i = 0; i < 2; i++) {
        void *panel = ((AddTextPanel_t)ADDR_ADD_TEXT_PANEL)(
            screen, screen, i ? l2 : l1, lx, ly + i * lh / 2, lw, lh / 2, fonts, 1, 0, 0, -1, 0);
        g_label_panel[i] = panel;
        if (panel) {
            ((ApplyStyle_t)ADDR_APPLY_STYLE)(screen, panel);
            make_label_clickable(panel);
            set_hand_cursor(panel);
        }
    }
    wl_log("Menu label panels %p %p: \"%s\" / \"%s\"", g_label_panel[0], g_label_panel[1], l1, l2);
}

static void *__fastcall hook_mainmenu_ctor(void *self, void *edx, void *a, void *b) {
    (void)edx;
    void *r = g_orig_ctor(self, a, b);
    // Back at the main menu: a multiplayer lobby's host settings (mpcheck.c) end here and the
    // player's own mod settings return.
    if (wl_settings_session_clear()) {
        wl_script_refresh_settings();
        wl_basemod_reload();
        wl_content_checksum_invalidate();
        wl_log("mpcheck: back to the player's own mod settings");
    }
    // The dat (and so the mod build) is loaded before the main menu exists,
    // so a failed build is known here: say so instead of "N mods loaded".
    if (!g_wl_vanilla && g_wl_build_error[0])
        snprintf(g_label, sizeof g_label, "Wololoader v%s - mod build error (see Mods)", WOLOLOADER_VERSION);
    two_lines(g_label);
    add_label(self);
    wl_mods_check_updates();   // background thread: Mod Hub versions for the Mods screen badges
    wl_update_check();         // background thread: newer Wololoader release on GitHub (once)
    return r;
}

static void __fastcall hook_mainmenu_tick(void *self, void *edx) {
    (void)edx;
    g_orig_tick(self);
    if (!g_update_shown && wl_update_version()) {
        g_update_shown = 1;
        // Only replace our own text: a build error or a "restart" notice stays.
        if (strcmp(g_label, g_default_label) == 0) {
            compose_label();
            wl_menu_set_label(g_label);
        }
    }
    if (InterlockedExchange(&g_label_clicked, 0)) {
        wl_log("menu: label clicked");
        wl_changelog_open();
    }
}

void wl_install_menu_hooks(void) {
    compose_label();
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1  (5 bytes, no relocs)
    g_orig_ctor = (MainMenuCtor_t)wl_hook(ADDR_MAINMENU_CTOR, hook_mainmenu_ctor, 5);
    if (!g_orig_ctor) wl_log("Main menu hook FAILED");
    // Main menu tick: chained after autostart.c's hook (installed before this one in DllMain);
    // in Vanilla Mode it is still the game's 0x6e8bb0. Either way it is a plain __thiscall(self).
    void **slot = (void **)ADDR_MAINMENU_VTBL + VT_TICK;
    DWORD old;
    g_orig_tick = (MenuTick_t)*slot;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = (void *)hook_mainmenu_tick;
    VirtualProtect(slot, 4, old, &old);
    wl_log("menu: label click + update hooks installed (tick was %p)", (void *)g_orig_tick);
}

// Change the main menu label text (e.g. after Vanilla Mode is switched on).
// Text panels: vtable slot 54 = SetText(this, text, 0, 0, 0) (see ui_mods.c).
typedef void (__thiscall *LabelSetText_t)(void *panel, const char *text, int a, int b, int c);
void wl_menu_set_label(const char *text) {
    lstrcpynA(g_label, text, sizeof g_label);
    two_lines(g_label);
    char l1[256], l2[256];
    split_label(l1, l2);
    for (int i = 0; i < 2; i++) {
        if (!g_label_panel[i]) continue;
        void **vtbl = *(void ***)g_label_panel[i];
        ((LabelSetText_t)vtbl[54])(g_label_panel[i], i ? l2 : l1, 0, 0, 0);
    }
}
