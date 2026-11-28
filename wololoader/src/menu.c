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

static void add_label(void *screen) {
    BYTE *s = screen;
    int cur_w = *(int *)(s + 0xc0), cur_h = *(int *)(s + 0xc4);
    int ref_w = *(int *)(s + 0x130), ref_h = *(int *)(s + 0x134);
    wl_log("MainMenuScreen %p cur=%dx%d ref=%dx%d", screen, cur_w, cur_h, ref_w, ref_h);

    // Main menu appeared: close the loading window (fade out / delay if build error)
    wl_loadscreen_close();

    // Coordinates are in reference units (= client size when the screen was made); fall back
    // to 1366x768 if unset. The label sits on the small empty sign hanging top right in
    // main-menu/default/background.jpg (1366x768: x 843..1052, y 77..114). The game's own
    // ctor text panels are elsewhere (0x6e19b7 version at 375,720; 0x6e1ddf big sign above
    // at 782,20 344x36; 0x6e1ec2 copyright at 690,660), and main-menu.json has no button
    // there, so nothing covers it. The old place (10, h-28) ran into the Exit button.
    // The background is stretched to the window, so scale the sign rect the same way.
    int rw = (ref_w > 100 && ref_w < 10000) ? ref_w : 1366;
    int rh = (ref_h > 100 && ref_h < 10000) ? ref_h : 768;
    int lx = 846 * rw / 1366, ly = 78 * rh / 768, lw = 204 * rw / 1366, lh = 35 * rh / 768;

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
