// formtab.c - Formations tab button in bottom-left command panel (Wololoader Base feature 'formations_tab')
//
// In AoE2 HD 5.8, military units normally place 4 formation buttons (Line, Box,
// Staggered, Flank) directly in slots 10..13 of the main command page (Page 1).
// However, the game engine contains a legacy AoK built-in Formations page (Page 5)
// at 0x006bcd60, natively wired to button ID 0x0a ('Select Formation', help string 4910,
// icon frame 0x2c) via the click dispatcher at 0x006bfc40 -> 0x006c01f0.
// Furthermore, the native Back button (ID 0x89 at 0x006bc610) already has:
//   if (page == 3 || page == 4 || page == 5 || page == 0x11) { page = 1; update_command_buttons(); }
// which automatically returns from Page 5 to Page 1!
//
// When this feature is active:
// 1. We hook GameScreen::update_command_buttons (0x006c9dc0, __thiscall, ret 0).
// 2. AFTER the original function builds Page 1, if formation buttons (IDs 0x53..0x57)
//    are present in slot 10, we replace slot 10 with a single tab button (ID 0x0a,
//    icon frame 0x2c, help string 4910) using setup_command_button (0x006c9200).
// 3. We hide and uncheck slots 11..13 (visible flag at offset +0x720 = 0, uncheck via vtable[52]).
// 4. We call GameScreen::finalize_button_visibility (0x006bf840) to apply the visibility changes.
//
// Clicking the Formations button opens the built-in Page 5 (no extra code needed),
// and clicking Back returns to Page 1 cleanly.
#include "wololoader.h"
#include <string.h>

// GameScreen::update_command_buttons at 0x006c9dc0
// __thiscall(void *screen), ret 0
// Prologue bytes: 55 8b ec 6a ff (push ebp; mov ebp,esp; push -1)
#define ADDR_UPDATE_COMMAND_BUTTONS  0x006c9dc0

// GameScreen::setup_command_button at 0x006c9200
// __thiscall(void *screen, ...), ret 0x40 (pops 64 bytes)
#define ADDR_SETUP_COMMAND_BUTTON    0x006c9200

// GameScreen::finalize_button_visibility at 0x006bf840
// __thiscall(void *screen), ret 0
#define ADDR_FINALIZE_VISIBILITY     0x006bf840

// Empty string descriptor in .data passed to setup_command_button
#define ADDR_EMPTY_STRING            0x00ad0380

// Command button IDs and resources
#define BTN_ID_FORMATIONS_TAB        0x0a
#define ICON_FRAME_FORMATIONS_TAB    0x2c
#define HELP_STR_FORMATIONS_TAB      4910   // 0x132e "Select Formation"
#define SLOT_FORMATIONS_TAB          10

typedef void (__thiscall *UpdateCommandButtons_t)(void *screen);
typedef void (__thiscall *FinalizeVisibility_t)(void *screen);
typedef void (__thiscall *ButtonSetChecked_t)(void *btn, int checked);

static UpdateCommandButtons_t g_orig_update_command_buttons;

// Exact 64-byte (16 dwords) stack layout popped by GameScreen::setup_command_button (ret $0x40)
typedef struct {
    void *shape;            // +0x00 (arg 1, ebp+0x08): [screen+0xab4] SLP shape pointer
    int slot;               // +0x04 (arg 2, ebp+0x0c): slot index (0..14)
    int icon_frame;         // +0x08 (arg 3, ebp+0x10): SLP icon frame index
    int button_id;          // +0x0c (arg 4, ebp+0x14): action ID (e.g. 0x0a)
    int action_param2;      // +0x10 (arg 5, ebp+0x18): secondary param (0)
    int tip_flag;           // +0x14 (arg 6, ebp+0x1c): byte 0 is 1 (has string descriptor)
    int tip_str_id;         // +0x18 (arg 7, ebp+0x20): tooltip string ID (4910)
    const char *tip_str_ptr;// +0x1c (arg 8, ebp+0x24): pointer to empty string (0x00ad0380)
    int tip_unk1;           // +0x20 (arg 9, ebp+0x28): 0
    int tip_unk2;           // +0x24 (arg 10, ebp+0x2c): 0
    int arg11;              // +0x28 (arg 11, ebp+0x30): -1 (0xffffffff)
    int arg12;              // +0x2c (arg 12, ebp+0x34): 0
    int arg13;              // +0x30 (arg 13, ebp+0x38): 0
    int arg14;              // +0x34 (arg 14, ebp+0x3c): 0
    int arg15;              // +0x38 (arg 15, ebp+0x40): 0
    int arg16;              // +0x3c (arg 16, ebp+0x44): 0
} CommandButtonArgs;

static void call_setup_command_button(void *screen, int slot, int icon_frame, int button_id, int help_string_id) {
    void *shape = *(void **)((char *)screen + 0xab4);
    if (!shape) return;

    CommandButtonArgs args;
    args.shape = shape;
    args.slot = slot;
    args.icon_frame = icon_frame;
    args.button_id = button_id;
    args.action_param2 = 0;
    args.tip_flag = 1;
    args.tip_str_id = help_string_id;
    args.tip_str_ptr = (const char *)ADDR_EMPTY_STRING;
    args.tip_unk1 = 0;
    args.tip_unk2 = 0;
    args.arg11 = -1;
    args.arg12 = 0;
    args.arg13 = 0;
    args.arg14 = 0;
    args.arg15 = 0;
    args.arg16 = 0;

    void *pargs = &args;
    __asm__ __volatile__(
        "subl $64, %%esp\n\t"
        "movl %%esp, %%edi\n\t"
        "movl %0, %%esi\n\t"
        "movl $16, %%ecx\n\t"
        "rep movsl\n\t"
        "movl %1, %%ecx\n\t"
        "movl $0x6c9200, %%eax\n\t"
        "call *%%eax\n\t"
        :
        : "b"(pargs), "r"(screen)
        : "eax", "ecx", "edx", "esi", "edi", "memory", "cc"
    );
}

static void button_set_checked(void *btn, int checked) {
    if (!btn) return;
    void **vtable = *(void ***)btn;
    ButtonSetChecked_t fn = (ButtonSetChecked_t)vtable[52]; // offset 0xd0 = vtable[52]
    fn(btn, checked);
}

static BOOL is_formation_button(void *btn) {
    if (!btn) return FALSE;
    int visible = *(int *)((char *)btn + 0x720);
    if (!visible) return FALSE;
    int id = *(int *)((char *)btn + 0x1a8);
    // Formation action IDs: 0x53 (Horde), 0x54 (Box), 0x55 (Line), 0x56 (Staggered), 0x57 (Flank)
    return (id >= 0x53 && id <= 0x57);
}

// Replacement for GameScreen::update_command_buttons (0x006c9dc0)
static void __fastcall hook_update_command_buttons(void *self, void *edx) {
    (void)edx;

    // 1. Let the original game function build Page 1 (through buildpage.c: shared commands of allies)
    wl_bp_panel_refresh((void *)g_orig_update_command_buttons, self);

    // 2. Only modify when active page is Page 1
    int page = *(int *)((char *)self + 0x1288);
    if (page != 1) return;

    void **buttons = (void **)((char *)self + 0xb14);
    if (!buttons) return;

    // 3. Check if slot 10 has a formation button (IDs 0x53..0x57)
    void *btn10 = buttons[10];
    if (!is_formation_button(btn10)) return;

    // 4. Replace slot 10 with Formations tab button:
    //    Button ID 0x0a ('Select Formation'), icon frame 0x2c, help string 4910
    call_setup_command_button(self, SLOT_FORMATIONS_TAB, ICON_FRAME_FORMATIONS_TAB,
                              BTN_ID_FORMATIONS_TAB, HELP_STR_FORMATIONS_TAB);

    // 5. Uncheck slot 10 (it is a tab button, not a toggled action)
    button_set_checked(btn10, 0);

    // 6. Clear / hide slots 11..13 (Box, Staggered, Flank)
    for (int s = 11; s <= 13; s++) {
        void *btn = buttons[s];
        if (!btn) continue;
        *(int *)((char *)btn + 0x720) = 0; // mark hidden
        button_set_checked(btn, 0);        // clear checked state
    }

    // 7. Finalize button visibility across all 15 slots via native 0x006bf840
    ((FinalizeVisibility_t)ADDR_FINALIZE_VISIBILITY)(self);
}

void wl_install_formtab(void) {
    static const BYTE EXPECT_PROLOGUE[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    if (memcmp((const void *)ADDR_UPDATE_COMMAND_BUTTONS, EXPECT_PROLOGUE, sizeof(EXPECT_PROLOGUE)) != 0) {
        wl_log("formtab: prologue mismatch at %08x, hook aborted", ADDR_UPDATE_COMMAND_BUTTONS);
        return;
    }

    // 5 stolen bytes: 55 (push ebp), 8b ec (mov ebp,esp), 6a ff (push -1)
    g_orig_update_command_buttons = (UpdateCommandButtons_t)wl_hook(
        ADDR_UPDATE_COMMAND_BUTTONS, hook_update_command_buttons, 5
    );

    if (g_orig_update_command_buttons) {
        wl_log("formtab: hooked 0x%08x (stolen 5 bytes), formations tab enabled", ADDR_UPDATE_COMMAND_BUTTONS);
    } else {
        wl_log("formtab: hook failed at 0x%08x", ADDR_UPDATE_COMMAND_BUTTONS);
    }
}
