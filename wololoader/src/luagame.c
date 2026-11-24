// Game side of the Lua scripts (src/script.c): fires events and shows
// script messages in the game. Addresses from re/research/lua_hooks.md,
// re-checked in the disassembly (2026-11-15):
//
// - 0x62e790 TRIBE_Game "start game screen", __thiscall, ret 0; runs once when
//   a game has loaded and the game screen exists. Prologue
//   55 / 8b ec / 6a ff = push ebp; mov ebp,esp; push -1 (5 bytes, no rel jumps).
// - 0x6cbed0 GameScreen::display_message(type, text, color, flag), __thiscall,
//   ret 0x10. The game itself calls it (0x631e30..0x631e48) as:
//     ecx = [0xaf3d90] (current game screen), type 1, text,
//     color = byte [0xab4cc3], flag = byte [0xde042a]
//   which shows the yellow notification line. We call it the same way.
// - 0x6c3310 GameScreen per-frame update, __thiscall, plain ret; it is slot 17
//   of the GameScreen vtable (dword at 0x9d5344 = 0x6c3310, checked).
//   Prologue 55 / 8b ec / 6a ff (5 bytes).
//
// - 0x490a60 OK dialog helper, __thiscall(parent, text, name, std::function cb
//   by value: 40 bytes), ret 0x30. The game calls it at 0x51c532..0x51c566 with
//   an empty std::function (dword +0x24 = 0); we pass 40 zero bytes.
// - GameScreen vtable 0x9d5300, slot 49 (event: self, panel, code, a, b;
//   ret 0x10) = 0x6b8f60 (checked). A dialog reports its button there; like
//   mpguard.c we catch our own dialog and close it by name with 0x485e00
//   (__thiscall(*(void**)0xada3f0, name), ret 4).
// - wl.chat: 0x8037b0 chat history add, __thiscall(history, sender, text,
//   player, type, a5, a6), ret 0x18 (checked); history = [0xaeefb4]. The game
//   adds system lines like '--Sheep Found--' exactly so at 0x689a44..0x689a6d:
//   sender NULL, player = [0xaad300] (-1, nobody), 0, 0, 0. The chat panel
//   redraws itself from the history. (Research: re/research/chat_list.md.)
// - Not used any more: 0x6bf910 GameScreen chat line, __thiscall(text, p2, seconds,
//   mode 0..2), ret 0x10 (checked: cmp ebx,2; manager [screen+0xd34+mode*4];
//   adds a type-3 line with seconds*1000 ms to 0x664840). Its only game caller
//   is 0x86844c. p2 is passed through to the manager; we use 0.
//   (display_message type 0 showed nothing in game.)
//
// - wl.ask: YesNo dialog 0x490da0 (see mpguard.c: __thiscall(parent, text, name),
//   ret 8; the answer arrives in GameScreen slot 49 with panel == dialog, code 0
//   = Yes; closed by name with 0x485e00). It shares the one-modal-at-a-time queue
//   with wl.dialog. In multiplayer no dialog is opened (it would pause nobody but
//   block the player): the text is shown as a notification and the Lua callback
//   gets nil on the next frame.
//
// - wl.sound: 0x62bce0 sound manager play, __thiscall(id, 0, 0), ret 0xc,
//   manager = [0xaf2c50]; ids 0..0x32 (51 entries). The game plays the chat
//   sound so at 0x6342c1..0x6342cd: ecx=[0xaf2c50], push 0, push 0, push 2.
//
// - wl.counter: src/counter.c (countdown-clock panel 9, evidence in its header).
//
// A message shown during game_start was lost (game_start runs before the
// message area works, seen in game 2026-11-15), so messages are queued and
// shown from the per-frame update, starting 2 s after the game started.
#include "wololoader.h"
#include <string.h>

typedef void (__thiscall *StartGame_t)(void *game);
typedef void (__thiscall *DisplayMessage_t)(void *screen, int type, const char *text, int color, int flag);

static StartGame_t g_orig_start_game;
static const DisplayMessage_t DisplayMessage = (DisplayMessage_t)0x6cbed0;

typedef void (__thiscall *ScreenTick_t)(void *screen);
typedef void (__thiscall *ChatAdd_t)(void *history, const char *sender, const char *text, int player, int type, int a5, int a6);
static ScreenTick_t g_orig_tick;

static void *game_screen(void) { return *(void **)0xaf3d90; }

typedef void (__thiscall *PlaySound_t)(void *mgr, int id, int a, int b);
static void lua_sound(int id) {
    void *mgr = *(void **)0xaf2c50;
    if (!mgr || id < 0 || id > 0x32) return;
    ((PlaySound_t)0x62bce0)(mgr, id, 0, 0);
}

// Queue of script messages (main thread only, like the scripts).
#define MSG_MAX 16
static char g_msgs[MSG_MAX][256];
static int g_msg_color[MSG_MAX];
extern int wl_script_msg_color;   // script.c
static int g_msg_count;
static DWORD g_game_start_tick, g_last_second;
// Script clock (Time.Wait / After / Every, OnSecond): GAME time, so it stops while the game is
// paused (F3, the F10 menu, a dialog in single player) and follows the game speed. Game time =
// [world+0x178] in ms (verified: 0x69e373..0x69e391 reads [[0xaf2c58]+0xc8]+0x178, divides by
// 1000 (0x10624dd3, shr 6) and splits it into h:mm:ss for display). Only forward steps of up to
// 5 s count, so loading a saved game or a time reset never makes timers jump.
static DWORD g_clock, g_last_wt;
static int g_have_wt;
static DWORD game_clock(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    void *world = game ? *(void **)((char *)game + 0xc8) : NULL;
    if (!world) return g_clock;
    DWORD wt = *(DWORD *)((char *)world + 0x178);
    if (g_have_wt && wt - g_last_wt <= 5000) g_clock += wt - g_last_wt;
    g_last_wt = wt; g_have_wt = 1;
    return g_clock;
}
typedef struct { char text[256]; char sender[64]; int has_sender; int player; } ChatItem;
static ChatItem g_chats[MSG_MAX];
static int g_chat_count;
static void show_next_dialog(void *gs);
static void reset_dialog_state(void);
static void deliver_nil_answers(void);

static int g_msg_dropped;   // logged once per frame (a script may send hundreds at once)
static void lua_message(const char *text) {
    if (g_msg_count < MSG_MAX) {
        g_msg_color[g_msg_count] = wl_script_msg_color;
        lstrcpynA(g_msgs[g_msg_count++], text, sizeof g_msgs[0]);
    } else if (g_msg_dropped++ == 0) wl_log("[lua] message queue full, dropped: %s (and more this frame)", text);
}

extern void wl_script_hotreload_frame(DWORD now_ms);

static void __fastcall hook_tick(void *screen, void *edx) {
    (void)edx;
    g_orig_tick(screen);
    if (!g_game_start_tick) return;                       // not in a game yet
    DWORD now = GetTickCount();
    if (now - g_game_start_tick < 2000) return;
    void *gs = game_screen();
    if (!gs) {                                            // game screen went away: drop timers etc.
        g_game_start_tick = 0;
        reset_dialog_state();
        wl_counter_reset();
        wl_script_game_reset(g_clock);
        return;
    }
    deliver_nil_answers();                                // MP asks from the previous frame
    DWORD gnow = game_clock();
    wl_script_frame(gnow);                                 // wl.after / wl.every / wl.wait
    wl_script_hotreload_frame(now);                        // hot-reload changed mod scripts (~1/s, debounced)
    wl_gamecmd_frame();                                    // Lua game commands over 100/s
    wl_counter_frame(gs);                                 // wl.counter line (src/counter.c)
    wl_ui_frame();                                        // UI button clicks (src/uikit.c)
    if (gs && g_msg_count) {
        for (int i = 0; i < g_msg_count; i++)
            DisplayMessage(gs, 1, g_msgs[i], g_msg_color[i] >= 0 ? g_msg_color[i] : *(unsigned char *)0xab4cc3, *(unsigned char *)0xde042a);
        wl_log("lua: showed %d message(s)", g_msg_count);
        g_msg_count = 0;
        g_msg_dropped = 0;
    }
    if (gs && g_chat_count) {
        for (int i = 0; i < g_chat_count; i++)
            if (*(void **)0xaeefb4)
                ((ChatAdd_t)0x8037b0)(*(void **)0xaeefb4, g_chats[i].has_sender ? g_chats[i].sender : NULL,
                                      g_chats[i].text, g_chats[i].player > 0 ? g_chats[i].player : *(int *)0xaad300,
                                      0, 0, 0);
        g_chat_count = 0;
    }
    if (gs) show_next_dialog(gs);
    // Game time (game_clock above): stops while paused; still per machine, not for changing game
    // state in multiplayer (documented in MODDING.md).
    if (gnow - g_last_second >= 1000) {
        g_last_second = gnow;
        wl_script_event("every_second");
        wl_monk_tick();
    }
}

typedef struct { DWORD d[10]; } GameFunction;   // MSVC std::function, empty = zeros
typedef void *(__thiscall *OkDialog_t)(void *parent, const char *text, const char *name, GameFunction cb);
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);
typedef void (__thiscall *CloseDialog_t)(void *owner, const char *name);
typedef void *(__thiscall *YesNo_t)(void *parent, const char *text, const char *name);
#define LUA_DIALOG_NAME "WololoaderLuaDialog"
#define LUA_ASK_NAME    "WololoaderLuaAsk"
static Event_t g_orig_gs_event;
static void *g_dialog;                       // our open modal (OK dialog or ask), or NULL
static int g_dialog_is_ask, g_dialog_token;  // what g_dialog is
// One queue for wl.dialog (token 0) and wl.ask (token > 0): one modal at a time.
typedef struct { char text[512]; int token; } DialogItem;
static DialogItem g_dialogs[MSG_MAX];
static int g_dialog_count;
static int g_nil_tokens[MSG_MAX];            // asks to answer with nil on the next frame
static int g_nil_count;

static void lua_dialog(const char *text) {
    if (g_dialog_count < MSG_MAX) {
        lstrcpynA(g_dialogs[g_dialog_count].text, text, sizeof g_dialogs[0].text);
        g_dialogs[g_dialog_count++].token = 0;
    } else wl_log("[lua] dialog queue full, dropped: %s", text);
}

static void lua_ask(const char *text, int token) {
    if (g_dialog_count < MSG_MAX) {
        lstrcpynA(g_dialogs[g_dialog_count].text, text, sizeof g_dialogs[0].text);
        g_dialogs[g_dialog_count++].token = token;
    } else {
        wl_log("[lua] dialog queue full, ask answered with nil: %s", text);
        if (g_nil_count < MSG_MAX) g_nil_tokens[g_nil_count++] = token;
    }
}

static void deliver_nil_answers(void) {
    int tokens[MSG_MAX], n = g_nil_count;
    memcpy(tokens, g_nil_tokens, sizeof tokens);
    g_nil_count = 0;
    for (int i = 0; i < n; i++) wl_script_ask_answer(tokens[i], -1);
}

static void reset_dialog_state(void) {
    g_dialog = NULL;
    g_dialog_count = 0;
    g_nil_count = 0;
}

static void lua_chat(const char *text, const char *sender, int player) {
    if (g_chat_count >= MSG_MAX) return;
    ChatItem *c = &g_chats[g_chat_count++];
    lstrcpynA(c->text, text, sizeof c->text);
    c->has_sender = sender != NULL;
    if (sender) lstrcpynA(c->sender, sender, sizeof c->sender);
    c->player = player;
}

// Multiplayer game? The game's own check (0x5a70c0..0x5a70e4, via 0x5408c0 and
// 0x583640 = lea eax,[ecx+0xd54]): settings = [[[0xaf2c58]+0x98]+0xd54], and
// settings+0x408 == 1 is 'isMultiPlayer' (serializer string at 0x9ccc6c,
// read at 0x611cab). Verified in the disassembly 2026-11-15.
int wl_is_multiplayer(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    if (!game) return 0;
    DWORD a = *(DWORD *)(game + 0x98);
    if (!a) return 0;
    DWORD settings = *(DWORD *)(a + 0xd54);
    return settings && *(int *)(settings + 0x408) == 1;
}
static int is_multiplayer(void) { return wl_is_multiplayer(); }

// Cheats enabled in game settings? Verified in AoK HD.exe 5.8 disassembly:
// cheat executor 0x80a1b0 checks settings* via 0x5408c0 ([[game+0x98]+0xd54])
// at 0x80a20b..0x80a22f and reads BYTE [settings+0x40f] != 0; matches
// GameSettings serializer 0x611d16..0x611d26 ("&mCheatsEnabled" at 0x9ccc7c).
int wl_cheats_enabled(void) {
    DWORD game = *(DWORD *)0xaf2c58;
    if (!game) return 0;
    DWORD a = *(DWORD *)(game + 0x98);
    if (!a) return 0;
    DWORD settings = *(DWORD *)(a + 0xd54);
    return settings && *(unsigned char *)(settings + 0x40f) != 0;
}

static void show_next_dialog(void *gs) {
    if (g_dialog || !g_dialog_count || !g_orig_gs_event) return;
    // A modal dialog pauses the game: never in multiplayer, show the text as a
    // notification line instead (all players' games keep running).
    if (is_multiplayer()) {
        for (int i = 0; i < g_dialog_count; i++) {
            DisplayMessage(gs, 1, g_dialogs[i].text, *(unsigned char *)0xab4cc3, *(unsigned char *)0xde042a);
            if (g_dialogs[i].token && g_nil_count < MSG_MAX) g_nil_tokens[g_nil_count++] = g_dialogs[i].token;
        }
        wl_log("lua: multiplayer, %d dialog(s) shown as messages", g_dialog_count);
        g_dialog_count = 0;
        return;
    }
    DialogItem item = g_dialogs[0];
    memmove(&g_dialogs[0], &g_dialogs[1], (size_t)(g_dialog_count - 1) * sizeof g_dialogs[0]);
    g_dialog_count--;
    if (item.token) {
        g_dialog = ((YesNo_t)0x490da0)(gs, item.text, LUA_ASK_NAME);
        g_dialog_is_ask = 1;
        g_dialog_token = item.token;
        wl_log("lua: ask dialog %p", g_dialog);
        if (!g_dialog && g_nil_count < MSG_MAX) g_nil_tokens[g_nil_count++] = item.token;  // could not show it
    } else {
        GameFunction none;
        memset(&none, 0, sizeof none);
        g_dialog = ((OkDialog_t)0x490a60)(gs, item.text, LUA_DIALOG_NAME, none);
        g_dialog_is_ask = 0;
        wl_log("lua: dialog %p", g_dialog);
    }
}

static int __fastcall hook_gs_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (g_dialog && panel == g_dialog) {
        int is_ask = g_dialog_is_ask, token = g_dialog_token;
        g_dialog = NULL;
        ((CloseDialog_t)0x485e00)(*(void **)0xada3f0, is_ask ? LUA_ASK_NAME : LUA_DIALOG_NAME);  // close first
        wl_log("lua: dialog closed (%d)", code);
        if (is_ask) wl_script_ask_answer(token, code == 0 ? 1 : 0);   // code 0 = Yes
        return 1;
    }
    return g_orig_gs_event(self, panel, code, a, b);
}

extern void wl_script_set_mp_info(int (*is_mp)(void), int (*cheats_on)(void));
extern void wl_script_set_feature(BOOL (*feature_fn)(const char *));
extern void wl_script_set_dev_ctx_fn(void (*set_dev_ctx)(int));
extern void wl_cmd_set_dev_ctx(int on);

static void __fastcall hook_start_game(void *game, void *edx) {
    (void)edx;
    g_orig_start_game(game);
    wl_mods_readonly_reset();
    wl_log("lua: game_start (mp=%d, cheats=%d)", wl_is_multiplayer(), wl_cheats_enabled());
    g_game_start_tick = GetTickCount();
    g_have_wt = 0;
    g_last_second = g_clock;
    reset_dialog_state();
    wl_counter_reset();
    wl_script_game_reset(g_clock);   // new game: no timers / waits / asks from the last one
    wl_script_event("game_start");
}

void wl_install_lua_game(void) {
    static const BYTE start_orig[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE msg_orig[] = { 0x55, 0x8b, 0xec, 0x8b, 0x89, 0x28, 0x0c, 0x00, 0x00 };
    wl_script_set_mp_info(wl_is_multiplayer, wl_cheats_enabled);
    wl_script_set_feature(wl_feature);
    wl_script_set_dev_ctx_fn(wl_cmd_set_dev_ctx);
    wl_script_set_reload_mods(wl_hotreload_from_lua);
    if (memcmp((void *)0x6cbed0, msg_orig, sizeof msg_orig) == 0)
    { wl_script_set_ui(lua_message, lua_dialog); wl_script_set_game(wl_game_api()); wl_script_set_sound(lua_sound); wl_script_set_counter(wl_counter_set); wl_script_set_chat(lua_chat); wl_script_set_ask(lua_ask);
      static const WlUiApi ui = { wl_ui_create, wl_ui_destroy, wl_ui_set, wl_ui_get_text }; wl_script_set_uikit(&ui); }
    else
        wl_log("lua: unexpected bytes at 0x6cbed0, messages go to the log");
    if (memcmp((void *)0x62e790, start_orig, sizeof start_orig) != 0) {
        wl_log("lua: unexpected bytes at 0x62e790, no game_start event");
        return;
    }
    g_orig_start_game = (StartGame_t)wl_hook(0x62e790, hook_start_game, sizeof start_orig);
    if (memcmp((void *)0x6c3310, start_orig, sizeof start_orig) == 0)   // same 5-byte prologue
        g_orig_tick = (ScreenTick_t)wl_hook(0x6c3310, hook_tick, sizeof start_orig);
    else
        wl_log("lua: unexpected bytes at 0x6c3310, messages stay queued");
    void **slot = (void **)0x9d5300 + 49;
    if ((DWORD)*slot == 0x6b8f60) {
        DWORD old;
        g_orig_gs_event = (Event_t)*slot;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = (void *)hook_gs_event;
        VirtualProtect(slot, 4, old, &old);
    } else {
        wl_log("lua: GameScreen vtable slot 49 unexpected, wl.dialog disabled");
    }
    wl_install_counter();   // wl.counter text hook (src/counter.c)
    wl_log("lua: game hooks %s", g_orig_start_game ? "installed" : "FAILED");
}
