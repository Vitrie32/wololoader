// Multiplayer guard: players without Wololoader cannot join a game that runs
// with our modded dat / patches (seen 2026-11-14). When the player clicks the
// main menu "Multiplayer" button while mods are active, show the game's own
// Yes/No dialog:  Yes = switch Vanilla Mode on (applies after restart),
//                 No  = continue into multiplayer anyway (e.g. everyone has the
//                       same Wololoader mods). Asked once per game session.
//
// Verified addresses (2026-11-14, research by agy, checked with objdump):
//  0x6e95f0  main menu Multiplayer button handler, __fastcall(MainMenuScreen*),
//            ret, prologue 55 8b ec 6a ff (5 bytes stolen, no relative ops).
//  0x490da0  YesNo dialog helper: __thiscall(parent screen, const char *text,
//            const char *dialog_name) ret 8, returns the dialog panel. The
//            dialog reports the click to parent vtable slot 49 (event):
//            (parent, dialog_panel, code, 0, 0), code 0 = Yes, else No. The
//            dialog does NOT close itself: the parent closes it by name with
//  0x485e00  __thiscall(*(void**)0xada3f0, const char *dialog_name), ret 4
//            (verified: the main menu does this for "WorkshopTOS" at 0x6e4cde).
//            Not closing it left the dialog stuck on screen (2026-11-14).
//  0x9d66a8  MainMenuScreen vtable; slot 49 (+0xc4, at 0x9d676c) = 0x6e4c90,
//            __thiscall(self, panel, code, a, b) ret 0x10. We replace the
//            vtable entry (no detour needed) and call the original for
//            everything that is not our dialog.
#include "wololoader.h"
#include <stdio.h>

#define ADDR_MP_BUTTON      0x6e95f0
#define ADDR_YESNO_DIALOG   0x490da0
#define ADDR_MAINMENU_VTBL  0x9d66a8
#define VT_EVENT            49
#define ADDR_MAINMENU_EVENT 0x6e4c90
#define ADDR_CLOSE_DIALOG   0x485e00
#define ADDR_DIALOG_OWNER   0xada3f0
#define DIALOG_NAME         "WololoaderMPWarning"

typedef void (__fastcall *MpButton_t)(void *main_menu);
typedef void *(__thiscall *YesNo_t)(void *parent, const char *text, const char *name);
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);
typedef void (__thiscall *CloseDialog_t)(void *owner, const char *name);

static MpButton_t g_orig_mp;      // trampoline of 0x6e95f0
static Event_t g_orig_event;      // original slot 49 (0x6e4c90)
static void *g_dialog;            // our open dialog, NULL when none
static BOOL g_asked;              // asked once per session

static void __fastcall hook_mp_button(void *main_menu) {
    if (g_asked || g_wl_vanilla || g_wl.mod_count == 0) { g_orig_mp(main_menu); return; }
    g_asked = TRUE;
    // Plain ANSI text, no "&" (the game eats it).
    g_dialog = ((YesNo_t)ADDR_YESNO_DIALOG)(main_menu,
        "Mods are active: players without the same mods cannot join. "
        "Turn on Vanilla Mode? (applies after a restart)",
        DIALOG_NAME);
    wl_log("mpguard: warning dialog %p", g_dialog);
    if (!g_dialog) g_orig_mp(main_menu);   // dialog failed: do not block the player
}

static int __fastcall hook_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (g_dialog && panel == g_dialog) {
        g_dialog = NULL;
        wl_log("mpguard: answer %d", code);
        ((CloseDialog_t)ADDR_CLOSE_DIALOG)(*(void **)ADDR_DIALOG_OWNER, DIALOG_NAME);  // remove it first
        if (code == 0) {
            if (wl_vanilla_set(TRUE)) {
                char text[96];
                snprintf(text, sizeof text, "Wololoader v%s - Vanilla Mode: restart the game", WOLOLOADER_VERSION);
                wl_menu_set_label(text);
            }
        } else {
            g_orig_mp(self);                // continue into multiplayer with mods
        }
        return 1;
    }
    return g_orig_event(self, panel, code, a, b);
}

void wl_install_mp_guard(void) {
    void **slot = (void **)ADDR_MAINMENU_VTBL + VT_EVENT;
    if ((DWORD)*slot != ADDR_MAINMENU_EVENT) { wl_log("mpguard: unexpected vtable slot, skipped"); return; }
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1
    g_orig_mp = (MpButton_t)wl_hook(ADDR_MP_BUTTON, hook_mp_button, 5);
    if (!g_orig_mp) { wl_log("mpguard: hook failed"); return; }
    g_orig_event = (Event_t)*slot;
    DWORD old;
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    *slot = (void *)hook_event;
    VirtualProtect(slot, 4, old, &old);
    wl_log("mpguard: installed");
}
