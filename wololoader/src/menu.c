// Main menu "Wololoader vX - N mods loaded" label (bottom-left).
// Detours MainMenuScreen::MainMenuScreen (0x6e0c30) and, after the original
// constructor ran, adds a TextPanel through the screen's own text factory.
// Addresses verified against AoK HD.exe 5.8 (see re/FINDINGS.md).
#include "wololoader.h"
#include <stdio.h>

#define ADDR_MAINMENU_CTOR   0x6e0c30  // __thiscall(this, a, b), ret 8
#define ADDR_ADD_TEXT_PANEL  0x48ff40  // ScreenPanel text factory, ret 0x30
#define ADDR_MAKE_FONTS      0x472500  // fills ref_ptr<Font>[4], ret 4
#define FONT_MENU_SMALL      0xa       // font the menu uses for 20px text
#define ADDR_SET_TEXT_STYLE  0x48d390  // screen: (pair*, pair*) -> +0x194..0x1a0, ret 8
#define ADDR_SET_TEXT_FLAG   0x48d3c0  // screen: (int) -> +0x1a4, ret 4
#define ADDR_APPLY_STYLE     0x48d550  // screen: (panel), sets panel state, ret 4
#define ADDR_STYLE_PAIR      0xada500  // global {dword, dword} the ctor passes twice

typedef void *(__thiscall *MainMenuCtor_t)(void *self, void *a, void *b);
typedef void *(__thiscall *AddTextPanel_t)(void *screen, void *parent, const char *text,
                                            int x, int y, int w, int h, void *fonts,
                                            int a24, int a28, int a2c, int str_id, int a34);
typedef void *(__thiscall *MakeFonts_t)(void *fonts, int font_id);
typedef void (__thiscall *SetTextStyle_t)(void *screen, DWORD *a, DWORD *b);
typedef void (__thiscall *SetTextFlag_t)(void *screen, int flag);
typedef void (__thiscall *ApplyStyle_t)(void *screen, void *panel);

static MainMenuCtor_t g_orig_ctor;
static char g_label[256];
static void *g_label_panel;   // last main menu label (for wl_menu_set_label)

static void add_label(void *screen) {
    BYTE *s = screen;
    int cur_w = *(int *)(s + 0xc0), cur_h = *(int *)(s + 0xc4);
    int ref_w = *(int *)(s + 0x130), ref_h = *(int *)(s + 0x134);
    wl_log("MainMenuScreen %p cur=%dx%d ref=%dx%d", screen, cur_w, cur_h, ref_w, ref_h);

    // Main menu appeared: close the loading window (fade out / delay if build error)
    wl_loadscreen_close();

    // Coordinates are in reference units; fall back to 768 if unset.
    int height = (ref_h > 100 && ref_h < 10000) ? ref_h : 768;

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

    void *panel = ((AddTextPanel_t)ADDR_ADD_TEXT_PANEL)(
        screen, screen, g_label, 10, height - 28, 700, 20, fonts, 1, 0, 0, -1, 0);
    g_label_panel = panel;
    if (panel) {
        ((ApplyStyle_t)ADDR_APPLY_STYLE)(screen, panel);
        BYTE *pp = panel;
        wl_log("panel state +0x144=%d +0x15c=%d", *(int *)(pp + 0x144), *(int *)(pp + 0x15c));
    }
    wl_log("Menu label panel %p: \"%s\"", panel, g_label);
}

static void *__fastcall hook_mainmenu_ctor(void *self, void *edx, void *a, void *b) {
    (void)edx;
    void *r = g_orig_ctor(self, a, b);
    // The dat (and so the mod build) is loaded before the main menu exists,
    // so a failed build is known here: say so instead of "N mods loaded".
    if (!g_wl_vanilla && g_wl_build_error[0])
        snprintf(g_label, sizeof g_label, "Wololoader v%s - mod build error (see Mods)", WOLOLOADER_VERSION);
    add_label(self);
    wl_mods_check_updates();   // background thread: Mod Hub versions for the Mods screen badges
    return r;
}

void wl_install_menu_hooks(void) {
    if (g_wl_vanilla)
        snprintf(g_label, sizeof g_label, "Wololoader v%s - Vanilla Mode (mods off)", WOLOLOADER_VERSION);
    else
        snprintf(g_label, sizeof g_label, "Wololoader v%s - %d mod%s loaded",
                 WOLOLOADER_VERSION, g_wl.mod_count, g_wl.mod_count == 1 ? "" : "s");
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1  (5 bytes, no relocs)
    g_orig_ctor = (MainMenuCtor_t)wl_hook(ADDR_MAINMENU_CTOR, hook_mainmenu_ctor, 5);
    if (!g_orig_ctor) wl_log("Main menu hook FAILED");
}

// Change the main menu label text (e.g. after Vanilla Mode is switched on).
// Text panels: vtable slot 54 = SetText(this, text, 0, 0, 0) (see ui_mods.c).
typedef void (__thiscall *LabelSetText_t)(void *panel, const char *text, int a, int b, int c);
void wl_menu_set_label(const char *text) {
    lstrcpynA(g_label, text, sizeof g_label);
    if (!g_label_panel) return;
    void **vtbl = *(void ***)g_label_panel;
    ((LabelSetText_t)vtbl[54])(g_label_panel, g_label, 0, 0, 0);
}
