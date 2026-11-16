// Native test of src/script.c (Lua runtime) with the system gcc.
// Build and run from wololoader/:
//   mkdir -p $HOME/.cache/wl-test/lua_obj && for f in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib; do gcc -O1 -w -c -o $HOME/.cache/wl-test/lua_obj/$f.o third_party/lua/src/$f.c; done
//   gcc -O1 -Wall -Wextra -Wno-format-truncation -std=c11 -Itools/fake_windows -Isrc -Ithird_party/lua/src -o $HOME/.cache/wl-test/test_script tools/test_script.c src/script.c $HOME/.cache/wl-test/lua_obj/*.o -lm && $HOME/.cache/wl-test/test_script
// Exit code 0 = all checks passed.
#include "wololoader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

WlState g_wl;
BOOL g_wl_vanilla;

// Captured log
static char g_lines[256][512];
static int g_nlines;
void wl_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_nlines < 256) vsnprintf(g_lines[g_nlines++], sizeof g_lines[0], fmt, ap);
    va_end(ap);
    printf("  log| %s\n", g_lines[g_nlines - 1]);
}

static int has_line(const char *needle) {
    for (int i = 0; i < g_nlines; i++) if (strstr(g_lines[i], needle)) return 1;
    return 0;
}
static int count_line(const char *needle) {
    int n = 0;
    for (int i = 0; i < g_nlines; i++) if (strstr(g_lines[i], needle)) n++;
    return n;
}
static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail = 1; } } while (0)

static char g_tmp[MAX_PATH];
static void write_mod(int idx, const char *id, const char *lua) {
    WlMod *m = &g_wl.mods[idx];
    memset(m, 0, sizeof *m);
    snprintf(m->dir, sizeof m->dir, "%s/%s", g_tmp, id);
    snprintf(m->id, sizeof m->id, "%s", id);
    snprintf(m->name, sizeof m->name, "Name of %s", id);
    snprintf(m->version, sizeof m->version, "1.0");
    m->enabled = 1;
    char p[MAX_PATH + 32];
    snprintf(p, sizeof p, "%s/scripts", m->dir);
    mkdir(m->dir, 0755);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/scripts/main.lua", m->dir);
    FILE *f = fopen(p, "wb");
    fputs(lua, f);
    fclose(f);
    g_wl.listed_count = idx + 1;
}

static char g_last_msg[256];
// fake UI backend
static int g_ui_next, g_ui_destroyed[16], g_ui_ndestroyed;
static char g_ui_log[4096];
static int fui_create(int t) { (void)t; return ++g_ui_next; }
static char g_fui_text[64] = "42";
static const char *fui_text(int id) { (void)id; return g_fui_text; }
static void fui_destroy(int id) { if (g_ui_ndestroyed < 16) g_ui_destroyed[g_ui_ndestroyed++] = id; }
static int fui_set(int id, const char *k, int v, const char *str) {
    char b[160]; snprintf(b, sizeof b, "%d.%s=%d%s%s;", id, k, v, str ? ":" : "", str ? str : "");
    strncat(g_ui_log, b, sizeof g_ui_log - strlen(g_ui_log) - 1); return 1;
}
static char g_last_chat[256], g_last_sender[64];
static int g_last_player, g_asked_token, g_ask_count;
static void ui_chat(const char *t, const char *sender, int player) {
    snprintf(g_last_chat, sizeof g_last_chat, "%s", t);
    snprintf(g_last_sender, sizeof g_last_sender, "%s", sender ? sender : "(null)");
    g_last_player = player;
}
static void ui_ask(const char *t, int token) { (void)t; g_asked_token = token; g_ask_count++; }
static char g_last_counter[256];
static int g_counter_calls;
static void ui_counter(const char *t) { snprintf(g_last_counter, sizeof g_last_counter, "%s", t ? t : "(null)"); g_counter_calls++; }
static void ui_message(const char *t) { snprintf(g_last_msg, sizeof g_last_msg, "%s", t); }

int main(void) {
    const char *home = getenv("HOME");
    char base[MAX_PATH];
    snprintf(base, sizeof base, "%s/.cache/wl-test", home ? home : ".");
    mkdir(base, 0755);
    snprintf(g_tmp, sizeof g_tmp, "%s/script_test_%d", base, (int)getpid());
    mkdir(g_tmp, 0755);

    write_mod(0, "good",
        "wl.log('hello', MOD.id, MOD.name, MOD.version, wl.version)\n"
        "counter = 0\n"
        "wl.on('game_start', function() wl.message('Good: Wololo!') counter = counter + 1 end)\n"
        "wl.on('tick', function(n) wl.log('tick', n, counter) end)\n"
        "print('via print', type(io), type(os), type(debug), type(package), type(dofile), type(load))\n"
        "wl.log('str', ('abc'):upper(), math.max(1,2), #utf8.char(228), type(coroutine.create))\n");
    write_mod(1, "bad",
        "wl.log('bad: global counter seen =', tostring(counter))\n"  // must be nil: separate env
        "wl.on('game_start', function() error('boom') end)\n"
        "wl.on('game_start', function() wl.log('bad: second handler still runs') end)\n"
        "error('top-level failure')\n");   // after registering: handlers stay, error logged
    write_mod(2, "loop",
        "wl.on('game_start', function() while true do end end)\n"
        "wl.on('game_start', function() wl.log('loop: after loop handler') end)\n");
    write_mod(3, "syntax", "this is not lua (\n");
    write_mod(4, "timers",
        "local n = 0\n"
        "wl.on('t_start', function()\n"
        "  wl.after(2, function() wl.log('T after fired') end)\n"
        "  local h = wl.every(3, function() n = n + 1 wl.log('T every', n) end)\n"
        "  wl.after(10, function() wl.log('T cancel result', tostring(wl.cancel(h)), tostring(wl.cancel(h))) end)\n"
        "  local never = wl.after(1, function() wl.log('T NEVER') end)\n"
        "  wl.cancel(never)\n"
        "  wl.chat('hi', 'Abbot', 2) wl.chat('plain') wl.chat('bad player', 'X', 9)\n"
        "end)\n"
        "wl.on('t_wait', function()\n"
        "  wl.log('W start')\n"
        "  wl.wait(5)\n"
        "  wl.log('W step A')\n"
        "  wl.wait(0.5)\n"
        "  wl.log('W step B')\n"
        "end)\n"
        "wl.on('t_err', function()\n"
        "  wl.wait(1)\n"
        "  error('late boom')\n"
        "end)\n"
        "wl.on('t_loop', function()\n"
        "  wl.wait(1)\n"
        "  while true do end\n"
        "end)\n"
        "wl.on('t_ask', function()\n"
        "  wl.ask('Bless?', function(a) wl.log('ASK1 answer', tostring(a)) wl.wait(1) wl.log('ASK1 after wait') end)\n"
        "  wl.ask('Again?', function(a) wl.log('ASK2 answer', tostring(a)) end)\n"
        "  wl.ask('Broken?', function(a) error('ask boom') end)\n"
        "end)\n"
        "wl.on('t_counter', function()\n"
        "  wl.counter('Monks: 5') wl.counter('') wl.counter('Monks: 4') wl.counter(nil) wl.counter(7) wl.counter()\n"
        "  local ok, err = pcall(wl.counter, {})\n"
        "  wl.log('counter bad arg', tostring(ok), tostring(err))\n"
        "end)\n"
        "wl.on('t_after_err', function() wl.after(1, function() error('timer boom') end) end)\n"
        "local ok, err = pcall(wl.wait, 1)\n"
        "wl.log('top-level wait', tostring(ok), tostring(err))\n");
    write_mod(5, "uitest",
        "local f = UI:CreateFrame({ Position = {10, 20}, Size = {200, 100}, Anchor = 'TopRight', Color = '#102030', Transparency = 0.5 })\n"
        "local b = UI:CreateButton({ Parent = f, Text = 'Go', Position = {5, 5}, Hotkey = 'F5',\n"
        "  OnClick = function(btn) wl.log('CLICK', btn.Text) Time.Wait(1) wl.log('CLICK after wait') end,\n"
        "  OnHover = function(btn) wl.log('HOVER', btn.Text) end,\n"
        "  OnLeave = function(btn) wl.log('LEAVE', btn.Text) end })\n"
        "local l = UI.CreateLabel({ Parent = f, Text = 'Hi', TextColor = {255, 0, 0} })\n"
        "b.Text = 'Go!'\n"
        "b.Image = 'icons/test.png'\n"
        "wl.log('props', b.Text, f.Anchor, tostring(b.Parent == f), tostring(b.Destroyed))\n"
        "local ok, err = pcall(function() b.Nope = 1 end) wl.log('bad prop', tostring(ok), err)\n"
        "ok, err = pcall(function() b.Anchor = 'Middle' end) wl.log('bad anchor', tostring(ok), err)\n"
        "ok, err = pcall(function() b.Color = '#12' end) wl.log('bad color', tostring(ok), err)\n"
        "ok, err = pcall(function() b.Hotkey = 'Ctrl+Alt+F12' end) wl.log('hotkey ctrl+alt+f12', tostring(ok))\n"
        "ok, err = pcall(function() b.Hotkey = 'BadKey' end) wl.log('bad hotkey', tostring(ok), tostring(err))\n"
        "local tb = UI:CreateTextBox({ Numeric = true, MaxLength = 4, Placeholder = 'count', OnChanged = function(b, t) wl.log('CHANGED', t, b.Text) end })\n"
        "tb:OnSubmit(function(b, t) wl.log('SUBMIT', t) end)\n"
        "wl.on('ui_kill', function() f:Destroy() wl.log('killed', tostring(b.Destroyed), tostring(l.Destroyed)) end)\n");

    wl_script_event("game_start");  // before init: must be a harmless no-op
    static const WlUiApi fui = { fui_create, fui_destroy, fui_set, fui_text };
    wl_script_set_uikit(&fui);
    wl_script_init();
    wl_script_init();               // idempotent

    printf("-- first game_start (no UI backend: message only logged)\n");
    wl_script_event("game_start");
    printf("-- UI backend installed, second game_start\n");
    wl_script_set_ui(ui_message, NULL);
    wl_script_event("game_start");
    printf("-- tick with int arg, unknown event\n");
    wl_script_event_int("tick", 42);
    wl_script_event("no_such_event");

    CHECK(has_line("[lua:good] hello\tgood\tName of good\t1.0\t" WOLOLOADER_VERSION));
    CHECK(has_line("[lua:good] via print\tnil\tnil\tnil\tnil\tnil\tnil"));
    CHECK(has_line("[lua:good] str\tABC\t2\t2\tfunction"));
    CHECK(has_line("[lua:bad] bad: global counter seen =\tnil"));
    CHECK(has_line("[lua:bad] error: ") && has_line("top-level failure"));
    CHECK(has_line("[lua:syntax] load error: "));
    CHECK(has_line("[lua] message: Good: Wololo!"));
    CHECK(strcmp(g_last_msg, "Good: Wololo!") == 0);
    // error handler disabled after the first failure, second handler of bad keeps running
    CHECK(count_line("[lua:bad] handler for 'game_start' failed and was disabled") == 1);
    CHECK(count_line("bad: second handler still runs") == 2);  // both game_start events
    CHECK(has_line("[lua:loop] handler for 'game_start' failed and was disabled: ") && has_line("instruction limit"));
    CHECK(count_line("loop: after loop handler") == 2);
    CHECK(has_line("[lua:good] tick\t42\t2"));  // counter incremented by both game_starts

    // ---- timers / wait / ask with a fake clock ---------------------------------
    printf("-- timers, wait, ask\n");
    wl_script_set_chat(ui_chat);
    wl_script_set_ask(ui_ask);
    CHECK(has_line("[lua:timers] top-level wait\tfalse\t") && has_line("not at the top level of main.lua"));
    wl_script_game_reset(1000);
    wl_script_event("t_start");
    CHECK(strcmp(g_last_chat, "bad player") == 0 && strcmp(g_last_sender, "X") == 0 && g_last_player == 0);  // 9 -> default
    wl_script_event("t_wait");
    CHECK(has_line("W start") && !has_line("W step A"));
    wl_script_frame(1500);
    CHECK(!has_line("T after fired"));
    wl_script_frame(3000);                       // after(2) is due at 3000
    CHECK(count_line("T after fired") == 1);
    wl_script_frame(4000);                       // every(3): due at 4000
    CHECK(count_line("T every\t1") == 1);
    wl_script_frame(5900);
    CHECK(!has_line("W step A"));
    wl_script_frame(6000);                       // wait(5) from 1000
    CHECK(count_line("W step A") == 1 && !has_line("W step B"));
    wl_script_frame(6600);
    CHECK(count_line("W step B") == 1);
    wl_script_frame(7000);
    CHECK(count_line("T every\t2") == 1);
    wl_script_frame(7000);                       // same time again: nothing fires twice
    CHECK(count_line("T every\t2") == 1 && count_line("T every") == 2);
    wl_script_frame(11000);                      // cancel timer at 11000 (also every #3 due at 10000)
    CHECK(has_line("T cancel result\ttrue\tfalse"));
    int every_before = count_line("T every");
    wl_script_frame(30000);
    CHECK(count_line("T every") == every_before);  // cancelled: silent
    CHECK(!has_line("T NEVER"));

    printf("-- errors inside coroutines\n");
    wl_script_event("t_err");
    wl_script_event("t_loop");
    wl_script_event("t_after_err");
    wl_script_frame(31000);
    CHECK(has_line("error in coroutine (stopped): ") && has_line("late boom"));
    CHECK(has_line("instruction limit") && count_line("error in coroutine (stopped)") == 2);
    CHECK(has_line("error in wl.after callback: ") && has_line("timer boom"));
    wl_script_frame(40000);
    CHECK(count_line("error in coroutine (stopped)") == 2);   // dead coroutines are not resumed again
    CHECK(count_line("handler for 't_err' failed") == 0);     // late errors do not disable the handler

    printf("-- ask answers\n");
    g_ask_count = 0;
    wl_script_event("t_ask");
    CHECK(g_ask_count == 3);
    int t3 = g_asked_token, t2 = t3 - 1, t1 = t3 - 2;
    wl_script_ask_answer(t1, 1);
    CHECK(has_line("ASK1 answer\ttrue") && !has_line("ASK1 after wait"));
    wl_script_ask_answer(t1, 1);                 // callback released: second answer ignored
    CHECK(count_line("ASK1 answer") == 1);
    wl_script_ask_answer(t2, 0);
    CHECK(has_line("ASK2 answer\tfalse"));
    wl_script_ask_answer(t3, 1);
    CHECK(has_line("error in wl.ask callback: ") && has_line("ask boom"));
    wl_script_frame(41000);                      // ASK1 wait(1) from 40000
    wl_script_frame(41500);
    CHECK(count_line("ASK1 after wait") == 1);
    // no UI backend: nil on the next frame
    wl_script_set_ask(NULL);
    wl_script_event("t_ask");
    CHECK(!has_line("ASK1 answer\tnil"));
    wl_script_frame(42000);
    CHECK(has_line("ASK1 answer\tnil") && has_line("ASK2 answer\tnil"));
    // a reset drops open asks and waiting coroutines
    wl_script_set_ask(ui_ask);
    wl_script_event("t_ask");
    wl_script_event("t_wait");
    int waits = count_line("W step A"), answers = count_line("ASK1 answer");
    wl_script_game_reset(50000);
    wl_script_ask_answer(g_asked_token, 1);
    wl_script_frame(100000);
    CHECK(count_line("ASK1 answer") == answers);  // no new answer
    CHECK(count_line("W step A") == waits);
    // ---- wl.counter -------------------------------------------------------------
    printf("-- counter\n");
    wl_script_event("t_counter");                // no backend: only logged
    CHECK(has_line("[lua] counter: Monks: 5") && has_line("[lua] counter: (hidden)") && has_line("[lua] counter: 7"));
    CHECK(has_line("counter bad arg\tfalse\t") && has_line("string expected"));
    wl_script_set_counter(ui_counter);
    wl_script_event("t_counter");
    CHECK(g_counter_calls == 6);                 // the bad table call never reaches the backend
    CHECK(strcmp(g_last_counter, "(null)") == 0);  // last good call was wl.counter() = hide
    wl_script_set_counter(NULL);
    // ---- on-screen UI -----------------------------------------------------------
    printf("-- ui\n");
    CHECK(has_line("[lua:uitest] props\tGo!\tTopRight\ttrue\tfalse"));
    CHECK(has_line("bad prop\tfalse\t") && has_line("unknown UI property: Nope"));
    CHECK(has_line("bad anchor\tfalse\t") && has_line("unknown Anchor: Middle"));
    CHECK(has_line("bad color\tfalse\t") && has_line("bad color: #12"));
    CHECK(has_line("hotkey ctrl+alt+f12\ttrue"));
    CHECK(has_line("bad hotkey\tfalse"));
    CHECK(strstr(g_ui_log, "2.Hotkey=116"));  // F5 = VK 0x74 = 116
    CHECK(strstr(g_ui_log, "2.Hotkey=1403"));  // Ctrl+Alt+F12: 0x7B | 0x100 | 0x400 = 1403
    CHECK(strstr(g_ui_log, "2.Image="));
    wl_script_event_int("ui_click", 999);       // unknown id: nothing
    wl_script_event_int("ui_click", 2);
    CHECK(has_line("[lua:uitest] CLICK\tGo!"));
    wl_script_event_int("ui_enter", 2);
    CHECK(has_line("[lua:uitest] HOVER\tGo!"));
    wl_script_event_int("ui_leave", 2);
    CHECK(has_line("[lua:uitest] LEAVE\tGo!"));
    wl_script_frame(200000);
    wl_script_frame(202000);
    CHECK(has_line("CLICK after wait"));
    wl_script_event("ui_kill");
    CHECK(has_line("killed\ttrue\ttrue"));
    CHECK(strstr(g_ui_log, "1.Anchor=2;") && strstr(g_ui_log, "1.Color=3153936;") && strstr(g_ui_log, "1.Alpha=128;"));
    CHECK(strstr(g_ui_log, "2.Parent=1;") && strstr(g_ui_log, "2.Text=0:Go!;") && strstr(g_ui_log, "3.TextColor=255;"));
    CHECK(g_ui_ndestroyed == 1 && g_ui_destroyed[0] == 1);
    CHECK(strstr(g_ui_log, "4.Numeric=1;") && strstr(g_ui_log, "4.MaxLength=4;") && strstr(g_ui_log, "4.Placeholder=0:count;"));
    wl_script_event_int("ui_text", 4);
    CHECK(has_line("[lua:uitest] CHANGED\t42\t42"));
    wl_script_event_int("ui_submit", 4);
    CHECK(has_line("[lua:uitest] SUBMIT\t42"));
    printf(g_fail ? "TEST FAILED\n" : "TEST PASSED\n");
    return g_fail;
}
