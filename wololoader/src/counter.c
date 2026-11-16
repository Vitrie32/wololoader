// wl.counter(text): one line of our own text in the game's countdown-clock
// area (where 'Wonder: 300 Years' is shown). Plan A of
// re/research/sound_counter.md section 2; every address below was re-read in
// the disassembly of AoK HD.exe 5.8 (objdump, ~/.cache/wl-test/exe.s) on
// 2026-11-15.
//
// What the game has (verified):
// - GameScreen ([0xaf3d90]) owns 10 countdown panels as an array of POINTERS
//   (stride 4) at GameScreen+0xbfc. Evidence: the creator loop 0x6b60c5..0x6b617d
//   news a 0x16c object (0x8d47df), runs the ctor 0x71b1b0 and stores the
//   pointer with 'mov %eax,(%esi)' (0x6b6122), loop count 10 (cmp $0xa); and
//   GameScreen::update_clocks 0x6c60f0 walks the array with 'mov (%eax),%ecx;
//   add $0x4,%eax' (0x6c6180..0x6c618f) and 'mov 0xbfc(%ebx),%ecx' (0x6c6116).
//   Panel 9 = *(void **)(screen + 0xbfc + 9 * 4).
// - The class vtable is 0x9d99e4 (ctor 0x71b1b0 sets it: 'movl $0x9d99e4,(%esi)'
//   at 0x71b26e; file bytes: slot 15 = 0x71b410 draw, slot 17 = 0x71b950 update).
//   We only touch a panel whose vtable pointer is exactly that.
// - set_mode 0x71bac0: __thiscall(panel, mode, playerIdx), 'ret $0x8'
//   (0x71bbb0 / 0x71bb9d). Args: 0x8(%ebp) = mode (edx), 0xc(%ebp) = player
//   index (ecx). It stores the mode at panel+0x144 (0x71baf8), the player
//   object (0 for idx <= 0) at panel+0x15c (0x71bb00..0x71bb15), colour white
//   0xffffff when there is no player (0x71bb53), then calls vtable slot 4 =
//   set_visible(0 if mode == 0 else 1) (0x71bb83..0x71bba0) and relayouts the
//   parent (call *0x44(%eax) on [panel+0x10]). It returns early without
//   changes when the mode and the player are the same (0x71bacf..0x71bae4).
// - Where the mode lives: panel+0x144 (read by 0x71bacf, 0x71b8ac, 0x71b968).
//   So 'hidden' == (*(int*)(panel+0x144) == 0). A fresh panel is hidden: the
//   creator calls vtable[4](0) right after the ctor (0x6b616d) and the ctor
//   memsets the 0x16c object to 0 (0x6b60df..0x6b60e7).
// - The per-frame update (vtable slot 17, 0x71b950) for mode 4 jumps through
//   the table at 0x71baa4 (read from the exe: [0]=0x71ba90 [4]=0x71b9dd) to
//   0x71b9dd, which with a NULL player (panel+0x15c) goes to 0x71ba72 and just
//   stores value 0: harmless.
// - Text builder 0x71b580: __thiscall(panel), no stack args, plain 'ret'
//   (0x71b940), returns char* in EAX = static buffer 0xaf4178 (4096 bytes;
//   'mov $0xaf4178,%eax' at 0x71b920). Prologue (file bytes) 55 8b ec 6a ff:
//   push ebp / mov ebp,esp / push -1 (the next instruction 'push $0x9672c8'
//   is NOT stolen). Its ONLY caller is the draw 0x71b410 (call at 0x71b43d; a
//   grep of every 'call 0x71b580' in the disassembly shows just that one).
//   The draw takes EAX as the text pointer (mov %eax,%edi), measures it with
//   a strlen loop (0x71b4b9..0x71b4cd) and passes pointer + length to the text
//   draw 0x4a16f0. It does not care where the buffer lives and does not free
//   it, so we can return OUR OWN static buffer instead of writing 0xaf4178.
// - Who hides panels: update_clocks 0x6c60f0 runs on events only (callers
//   0x63243c, 0x632b70, 0x633027, 0x633223, 0x633563, 0x633626, 0x6b7e06,
//   0x6ba3bb, 0x6c2195). For every panel it did not give a timer to it calls
//   set_mode(panel, 0, 0) (0x6c6341..0x6c635a), so it hides our panel 9 whenever
//   the game has fewer than 10 timers. Hence we re-show it from the per-frame
//   hook (cheap: set_mode is only called when the mode is 0).
//
// Design: the text hook replaces the string only for panel 9 while it is in
// OUR state: counter active, mode == 4 and player object == NULL (the game
// always gives its timers a player index >= 1: 'esi' starts at 1 in
// 0x6c613e, so a game panel never has a NULL player in mode 4). If the game
// ever needs all 10 panels (mode != 0 and not our state), we leave the panel
// alone and show nothing. We never write into any other panel.
#include "wololoader.h"
#include <string.h>

#define PANEL_ARRAY_OFF  0xbfc
#define PANEL_COUNT      10
#define PANEL_VTABLE     0x9d99e4
#define PANEL_MODE_OFF   0x144
#define PANEL_PLAYER_OFF 0x15c
#define COUNTER_MAX      100

typedef char *(__thiscall *PanelText_t)(void *panel);
typedef void (__thiscall *PanelSetMode_t)(void *panel, int mode, int player);   // ret 8
static PanelText_t g_orig_text;
static const PanelSetMode_t PanelSetMode = (PanelSetMode_t)0x71bac0;

static char g_text[COUNTER_MAX + 1];
static int g_active;     // a script wants the line
static int g_shown;      // we put panel 9 into our state

// Panel 9 of the current game screen, or NULL when anything looks wrong.
static void *our_panel(void *screen) {
    if (!screen) return NULL;
    void *p = *(void **)((char *)screen + PANEL_ARRAY_OFF + (PANEL_COUNT - 1) * 4);
    if (!p || *(DWORD *)p != PANEL_VTABLE) return NULL;
    return p;
}

static int in_our_state(void *p) {
    return *(int *)((char *)p + PANEL_MODE_OFF) == 4 && *(void **)((char *)p + PANEL_PLAYER_OFF) == NULL;
}

static char *__fastcall hook_panel_text(void *self, void *edx) {
    (void)edx;
    if (g_active) {
        void *p = our_panel(*(void **)0xaf3d90);
        if (p && p == self && in_our_state(p)) return g_text;
    }
    return g_orig_text(self);
}

void wl_counter_set(const char *text) {
    if (!text || !*text) { g_active = 0; return; }
    lstrcpynA(g_text, text, sizeof g_text);   // always NUL-terminated, copied before activating
    g_active = 1;
}

// Forget everything (new game / game screen gone). Does not touch the panels:
// the next game creates new ones (hidden), and the old ones may be freed.
void wl_counter_reset(void) {
    g_active = 0;
    g_shown = 0;
    g_text[0] = 0;
}

// Per frame, from the GameScreen update hook (main thread).
void wl_counter_frame(void *screen) {
    if (!g_orig_text) return;
    void *p = our_panel(screen);
    if (!p) return;
    int mode = *(int *)((char *)p + PANEL_MODE_OFF);
    if (g_active) {
        if (mode == 0) { PanelSetMode(p, 4, 0); g_shown = 1; }   // hidden (or hidden again by update_clocks)
        // else: already ours, or the game uses all 10 panels: leave it
    } else if (g_shown) {
        if (in_our_state(p)) PanelSetMode(p, 0, 0);
        g_shown = 0;
    }
}

void wl_install_counter(void) {
    static const BYTE orig[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    if (memcmp((void *)0x71b580, orig, sizeof orig) != 0 || *(DWORD *)0x9d99e4 != 0x71b390 ||
        *(DWORD *)(0x9d99e4 + 15 * 4) != 0x71b410 || memcmp((void *)0x71bac0, orig, 3) != 0) {
        wl_log("counter: unexpected bytes at 0x71b580 / vtable 0x9d99e4, wl.counter disabled");
        return;
    }
    g_orig_text = (PanelText_t)wl_hook(0x71b580, hook_panel_text, sizeof orig);
    wl_log("counter: text hook %s", g_orig_text ? "installed" : "FAILED");
}
