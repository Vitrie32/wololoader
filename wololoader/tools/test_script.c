// Native test of src/script.c (Lua runtime) with the system gcc.
// Build and run from wololoader/:
//   mkdir -p $HOME/.cache/wl-test/lua_obj && for f in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib; do gcc -O1 -w -c -o $HOME/.cache/wl-test/lua_obj/$f.o third_party/lua/src/$f.c; done
//   gcc -O1 -Wall -Wextra -Wno-format-truncation -std=c11 -Itools/fake_windows -Isrc -Ithird_party/lua/src -o $HOME/.cache/wl-test/test_script tools/test_script.c src/script.c src/modsettings.c src/json.c $HOME/.cache/wl-test/lua_obj/*.o -lm && $HOME/.cache/wl-test/test_script
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
int wl_gamemode_enemy_player(void) { return 0; }
int wl_gamemode_set_enemy_player(int pid) { (void)pid; return 0; }
int wl_gamemode_map_tiles(void) { return 120; }
void wl_ui_set_map_capture(int on) { (void)on; }
void wl_ui_set_unit_control_disabled(int off) { (void)off; }
int wl_ui_request_restart(void) { return 1; }
int wl_play_sfx_file(const char *path) { (void)path; return 1; }

// Captured log
static char g_lines[256][512];
static int g_nlines, g_nlines_total;
void wl_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_nlines_total++;
    if (g_nlines >= 256) {   // keep the newest lines: drop the oldest half
        memmove(g_lines, g_lines[128], 128 * sizeof g_lines[0]);
        g_nlines = 128;
    }
    vsnprintf(g_lines[g_nlines++], sizeof g_lines[0], fmt, ap);
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

// Extra files of a mod (path relative to the mod folder, e.g. "scripts/ai/economy.lua").
static void write_file_n(int idx, const char *rel, const char *data, size_t n) {
    const WlMod *m = &g_wl.mods[idx];
    char p[MAX_PATH + 128];
    snprintf(p, sizeof p, "%s/%s", m->dir, rel);
    for (char *c = p + strlen(m->dir) + 1; *c; c++) {     // make the parent folders
        if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
    }
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(data, 1, n, f); fclose(f); }
}
static void write_file(int idx, const char *rel, const char *text) { write_file_n(idx, rel, text, strlen(text)); }

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

// fake game for the polled events: player 1 with units and researched techs the test changes
static WlUnitInfo g_fu[8];
static int g_nfu;
static short g_ftech[8];
static int g_nftech;
static int fg_local(void) { return 1; }
static int fg_count(void) { return 2; }
static WlResourceDef g_fres[2];
static float g_fres_val[256];
static int g_fres_add_id, g_fres_set_id;
static float g_fres_add_amt, g_fres_set_val;
static int fg_resource(int pid, int res, float *out) {
    (void)pid;
    if (res == 6) { *out = 0.0f; return 1; }
    if (res >= 0 && res < 256) { *out = g_fres_val[res]; return 1; }
    return 0;
}
static int fg_add_resource(int pid, int res, float amt) {
    (void)pid;
    if (res < 0 || res >= 256) return -1;
    g_fres_add_id = res; g_fres_add_amt = amt;
    g_fres_val[res] += amt;
    return 1;
}
static int fg_set_resource(int pid, int res, float val) {
    (void)pid;
    if (res < 0 || res >= 256) return -1;
    g_fres_set_id = res; g_fres_set_val = val;
    g_fres_val[res] = val;
    return 1;
}
static const WlResourceDef *fg_resources_list(int *out_count) {
    *out_count = 2;
    return g_fres;
}
static int fg_resource_resolve(const char *key, const char *prefer_mod) {
    (void)prefer_mod;
    for (int i = 0; i < 2; i++) {
        if (strcmp(g_fres[i].key, key) == 0) return g_fres[i].id;
    }
    return -1;
}
static WlGatherDef g_fgather[1];
static const WlGatherDef *fg_gather_by_key(const char *key, const char *prefer_mod) {
    (void)prefer_mod;
    if (key && strcmp(g_fgather[0].key, key) == 0) return &g_fgather[0];
    return NULL;
}
static int fg_is_enemy(int a, int b) { (void)a; (void)b; return 0; }
static int fg_units(int pid, WlUnitInfo *out, int max) {
    int n = 0;
    for (int i = 0; i < g_nfu && n < max; i++) if (pid == 1) out[n++] = g_fu[i];
    return n;
}
static int fg_researched(int pid, short *out, int max) {
    int n = 0;
    for (int i = 0; i < g_nftech && n < max; i++) if (pid == 1) out[n++] = g_ftech[i];
    return n;
}
static const char *g_fmode;
static int g_fwin_pid, g_fwin_team, g_fwin_calls;
static int g_fdefeat_pid, g_fdefeat_calls;
static const char *fg_game_mode(void) { return g_fmode; }
static int fg_win_player(int pid, int team) {
    if (pid <= 0) return -1;
    g_fwin_pid = pid; g_fwin_team = team; g_fwin_calls++;
    return 1;
}
static int fg_defeat_player(int pid) {
    if (pid <= 0) return -1;
    g_fdefeat_pid = pid; g_fdefeat_calls++;
    return 1;
}

static int g_reload_mods_calls;
static int fake_reload_mods(char *msg, size_t cap) {
    g_reload_mods_calls++;
    snprintf(msg, cap, "Mods reloaded (2 civs, 3 mods)");
    return 1;
}

static char g_rt_str_text[WL_MAX_RT_CMDS][128];
static char g_rt_str_help[WL_MAX_RT_CMDS][256];
static int g_rt_str_set[WL_MAX_RT_CMDS];
static int g_bp_refresh_calls;
static int g_local_ctx_state;
static int g_local_ctx_on_calls;

static void fake_rt_str(int slot, const char *text, const char *help) {
    if (slot < 0 || slot >= WL_MAX_RT_CMDS) return;
    if (text) {
        g_rt_str_set[slot] = 1;
        snprintf(g_rt_str_text[slot], sizeof g_rt_str_text[slot], "%s", text);
        snprintf(g_rt_str_help[slot], sizeof g_rt_str_help[slot], "%s", help ? help : "");
    } else {
        g_rt_str_set[slot] = 0;
        g_rt_str_text[slot][0] = 0;
        g_rt_str_help[slot][0] = 0;
    }
}
static void fake_bp_refresh(void) { g_bp_refresh_calls++; }
static void fake_local_ctx(int on) {
    g_local_ctx_state = on;
    if (on) g_local_ctx_on_calls++;
}

static int find_rt_cmd(const char *mod, const char *name) {
    int n = 0;
    const WlRuntimeCmd *cmds = wl_rt_commands(&n);
    for (int i = 0; i < n; i++) {
        if (cmds[i].used && (!mod || strcmp(cmds[i].mod, mod) == 0) && strcmp(cmds[i].name, name) == 0)
            return i;
    }
    return -1;
}
static int count_rt_cmds(void) {
    int n = 0, c = 0;
    const WlRuntimeCmd *cmds = wl_rt_commands(&n);
    for (int i = 0; i < n; i++) if (cmds[i].used) c++;
    return c;
}

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

    write_mod(6, "events",
        "Game.OnResearch(function(p, id) wl.log('RESEARCH', p.Id, id) end)\n"
        "Game.OnBuildingDone(function(u) wl.log('BUILT', u.Id, u.Type, tostring(u.Built)) end)\n"
        "wl.on('t_units', function() for _, u in ipairs(Game.LocalPlayer:Units()) do\n"
        "  wl.log('UNIT', u.Id, tostring(u.IsBuilding), tostring(u.Built)) end end)\n");

    // sandbox attacks (each must be stopped; see the "sandbox" checks at the end)
    write_mod(7, "evil",
        "wl.on('a_pcall', function() local n = 0\n"
        "  while true do pcall(function() while true do end end) n = n + 1\n"
        "    if n > 30 then wl.log('E1 ESCAPED') return end end end)\n"
        "wl.on('a_co', function()\n"
        "  for i = 1, 1000000 do coroutine.resume(coroutine.create(function() while true do end end)) end\n"
        "  wl.log('E2 ESCAPED') end)\n"
        "wl.on('a_xpcall', function() local n = 0\n"
        "  while true do xpcall(function() while true do end end, function() while true do end end) n = n + 1\n"
        "    if n > 30 then wl.log('E1b ESCAPED') return end end end)\n"
        "wl.on('a_pattern', function() local ok, e = pcall(string.find, string.rep('a', 60), '.-.-.-.-.-.-.-.-b')\n"
        "  wl.log('E3 pattern', tostring(ok), tostring(e)) end)\n"
        "wl.on('a_pattern2', function() local ok, e = pcall(function() return (string.rep('a', 200000)):find('.-b') end)\n"
        "  wl.log('E3b pattern', tostring(ok), tostring(e)) end)\n"
        "wl.on('a_meta', function() local ok, e = pcall(function() getmetatable('').__index.upper = function() return 'HACKED' end end)\n"
        "  wl.log('E4 meta', tostring(ok), tostring(getmetatable(''))) end)\n"
        "wl.on('a_gc', function() wl.log('E6 gc', tostring(pcall(collectgarbage, 'stop')), tostring(pcall(collectgarbage, 'count'))) end)\n"
        "wl.on('a_dump', function() wl.log('E8 dump', tostring(string.dump), tostring(warn)) end)\n"
        "wl.on('a_log', function() for i = 1, 3000 do print('spam', i) end end)\n"
        "wl.on('a_image', function() local ok, e = pcall(wl.raw_ui_set, 1, 'Image', '/etc/passwd') wl.log('E5 image', tostring(ok), tostring(e)) end)\n");
    write_mod(8, "victim",
        "wl.on('a_victim', function() wl.log('VICTIM upper', ('x'):upper()) end)\n");

    // require: scripts/ai/economy.lua etc. (see the "require" checks after init)
    write_mod(9, "req",
        "wl.log('R0 types', type(require), type(package), type(dofile), type(loadfile), type(io), type(os), type(debug))\n"
        "local econ = require('ai.economy')\n"
        "local econ2 = require('ai.economy')\n"
        "wl.log('R1 economy', econ.name, econ.calls, econ.util, tostring(econ == econ2))\n"
        "wl.log('R2 util', require('util'), tostring(require('noret')), tostring(require('noret')), noret_ran)\n"
        "wl.log('R3 false', tostring(require('retfalse')), tostring(require('retfalse')), calls_rf)\n"
        "wl.log('R4 deep', require('ai.deep.leaf').depth)\n"
        "wl.on('req_attack', function()\n"
        "  local names = { '../outside', '../../outside', '..', '/abs', '\\\\abs', 'C:outside', 'C:/outside', 'a/b', 'a\\\\b',\n"
        "    'a..b', '', '.a', 'a.', 'a b', 'a-b', 'a\\0b', string.rep('a', 65) }\n"
        "  for _, name in ipairs(names) do\n"
        "    local ok, err = pcall(require, name)\n"
        "    wl.log('ATTACK', tostring(ok), tostring(err):find('bad module name', 1, true) and 'bad name' or tostring(err))\n"
        "  end\n"
        "  local ok, err = pcall(require, string.rep('a', 64))\n"
        "  wl.log('LEN64', tostring(ok), tostring(err):find('not found', 1, true) and 'not found' or tostring(err))\n"
        "  ok, err = pcall(require, 42)\n"
        "  wl.log('NUMBER', tostring(ok), tostring(err):find('bad module name', 1, true) and 'bad name' or tostring(err))\n"
        "  ok, err = pcall(require, 'missing.module')\n"
        "  wl.log('MISSING', tostring(ok), tostring(err))\n"
        "end)\n"
        "wl.on('req_bin', function() local ok, err = pcall(require, 'bin') wl.log('BIN', tostring(ok), err) end)\n"
        "wl.on('req_err', function()\n"
        "  local ok, err = pcall(require, 'err') wl.log('ERR1', tostring(ok), err)\n"
        "  ok, err = pcall(require, 'err') wl.log('ERR2', tostring(ok), err)\n"
        "  ok, err = pcall(require, 'syn') wl.log('SYN', tostring(ok), err)\n"
        "end)\n"
        "wl.on('req_cycle', function()\n"
        "  wl.log('CYC self', select(2, pcall(require, 'self')))\n"
        "  wl.log('CYC ping', select(2, pcall(require, 'ping')))\n"
        "  local ok, e = pcall(require, 'ping') wl.log('CYC again', tostring(ok), e)\n"
        "end)\n"
        "wl.on('req_budget', function() require('spin') end)\n"
        "wl.on('req_again', function() wl.log('AGAIN calls', calls, tostring(require('ai.economy') == econ)) wl.log('BOM', require('nested')) end)\n");
    write_file(9, "scripts/ai/economy.lua",
        "calls = (calls or 0) + 1\n"
        "local u = require('util')\n"          // nested require, same env
        "return { name = 'economy', calls = calls, util = u }\n");
    write_file(9, "scripts/ai/deep/leaf.lua", "return { depth = 3 }\n");
    write_file(9, "scripts/util.lua", "return 'U'\n");
    write_file(9, "scripts/noret.lua", "noret_ran = (noret_ran or 0) + 1\n");
    write_file(9, "scripts/retfalse.lua", "calls_rf = (calls_rf or 0) + 1\nreturn false\n");
    write_file(9, "scripts/err.lua", "error('module broke')\n");
    write_file(9, "scripts/syn.lua", "this is not lua (\n");
    write_file(9, "scripts/self.lua", "return require('self')\n");
    write_file(9, "scripts/ping.lua", "return require('pong')\n");
    write_file(9, "scripts/pong.lua", "return require('ping')\n");
    write_file(9, "scripts/spin.lua", "while true do end\n");
    write_file(9, "scripts/bin.lua", "\x1bLua\x54\x00\x19\x93\r\n\x1a\n");
    write_file(9, "outside.lua", "wl.log('OUTSIDE RAN')\n");   // "../outside" would reach this
    write_file(9, "scripts/outside.lua", "wl.log('INSIDE scripts')\n");  // reached only by a plain name
    write_file_n(9, "scripts/hidden.lua", "\xef\xbb\xbfreturn 'bom ok'\n", 19);
    write_file(9, "scripts/nested.lua", "return require('hidden')\n");
    char outside[MAX_PATH];
    snprintf(outside, sizeof outside, "%s/outside.lua", g_tmp);   // "../../outside" target
    FILE *of = fopen(outside, "wb");
    if (of) { fputs("wl.log('OUTSIDE RAN')\n", of); fclose(of); }

    // Mod settings (modsettings.c): defaults from mod.json, the player's values from
    // <root>\\settings\\<id>.json. The DLL's paths use '\\', which on Linux are plain
    // characters of one file name, so the files are written with the same names.
    write_mod(10, "setmod",
        "local s = MOD.settings\n"
        "wl.log('S', tostring(s.fast), s.pop, s.mode, tostring(s.missing), s.bad)\n");
    snprintf(g_wl.root, sizeof g_wl.root, "%s", g_tmp);
    {
        char p[MAX_PATH + 64];
        snprintf(p, sizeof p, "%s\\mod.json", g_wl.mods[10].dir);
        FILE *f = fopen(p, "wb");
        fputs("{\"name\":\"setmod\",\"settings\":["
              "{\"id\":\"fast\",\"type\":\"bool\",\"default\":true},"
              "{\"id\":\"pop\",\"type\":\"number\",\"min\":0,\"max\":50,\"step\":5,\"default\":10},"
              "{\"id\":\"mode\",\"type\":\"choice\",\"choices\":[\"Easy\",\"Hard\"],\"default\":\"Easy\"},"
              "{\"id\":\"bad\",\"type\":\"number\",\"min\":0,\"max\":5,\"default\":3}]}", f);
        fclose(f);
        snprintf(p, sizeof p, "%s\\settings\\setmod.json", g_tmp);
        f = fopen(p, "wb");   // player: pop 999 is clamped to 50, mode Hard, bad gets a string (ignored)
        fputs("{\"fast\": false, \"pop\": 999, \"mode\": \"Hard\", \"bad\": \"x\"}", f);
        fclose(f);
    }

    wl_script_event("game_start");  // before init: must be a harmless no-op
    static const WlUiApi fui = { fui_create, fui_destroy, fui_set, fui_text };
    wl_script_set_uikit(&fui);
    wl_script_init();
    wl_script_init();               // idempotent
    // ---- require: checks made while main.lua runs (log is still short here) -------
    printf("-- require (main.lua at init)\n");
    CHECK(has_line("[lua:req] R0 types\tfunction\tnil\tnil\tnil\tnil\tnil\tnil"));
    CHECK(has_line("[lua:req] R1 economy\teconomy\t1\tU\ttrue"));  // loaded once, nested util too
    CHECK(has_line("[lua:req] R2 util\tU\ttrue\ttrue\t1"));        // no return value = true, ran once
    CHECK(has_line("[lua:req] R3 false\tfalse\tfalse\t1"));        // false is cached, not reloaded
    CHECK(has_line("[lua:req] R4 deep\t3"));                        // ai.deep.leaf -> scripts/ai/deep/leaf.lua
    CHECK(!has_line("OUTSIDE RAN") && !has_line("INSIDE scripts"));

    printf("-- first game_start (no UI backend: message only logged)\n");
    wl_script_event("game_start");
    printf("-- UI backend installed, second game_start\n");
    wl_script_set_ui(ui_message, NULL);
    wl_script_event("game_start");
    printf("-- tick with int arg, unknown event\n");
    wl_script_event_int("tick", 42);
    wl_script_event("no_such_event");

    CHECK(has_line("[lua:good] hello\tgood\tName of good\t1.0\t" WOLOLOADER_VERSION));
    CHECK(has_line("[lua:setmod] S\tfalse\t50\tHard\tnil\t3"));
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
    // ---- polled events: OnResearch, OnBuildingDone ----------------------------------
    printf("-- research / building done\n");
    static WlGameApi fg;
    memset(&fg, 0, sizeof fg);
    fg.local_player = fg_local; fg.player_count = fg_count; fg.resource = fg_resource;
    fg.is_enemy = fg_is_enemy; fg.units = fg_units; fg.researched = fg_researched;
    wl_script_set_game(&fg);
    g_fu[0] = (WlUnitInfo){ 10, 1, 1, 80, 1, 109, 1, 2 };   // town center, ready
    g_fu[1] = (WlUnitInfo){ 11, 2, 2, 80, 1, 12, 1, 0 };    // barracks, foundation
    g_fu[2] = (WlUnitInfo){ 12, 3, 3, 4, 0, 83, 1, 2 };     // villager
    g_nfu = 3;
    g_ftech[0] = 104; g_nftech = 1;                          // dark age
    wl_script_event("game_start");
    wl_script_event("every_second");                          // first look: nothing fires
    CHECK(!has_line("[lua:events] RESEARCH") && !has_line("[lua:events] BUILT"));
    wl_script_event("t_units");
    CHECK(has_line("[lua:events] UNIT\t10\ttrue\ttrue") && has_line("[lua:events] UNIT\t11\ttrue\tfalse"));
    CHECK(has_line("[lua:events] UNIT\t12\tfalse\tfalse"));
    g_fu[1].state = 2;                                        // barracks finished
    g_fu[3] = (WlUnitInfo){ 13, 4, 4, 80, 1, 70, 1, 2 };     // a house placed and finished within a second
    g_nfu = 4;
    g_ftech[1] = 22; g_nftech = 2;                            // loom
    wl_script_event("every_second");
    CHECK(has_line("[lua:events] RESEARCH\t1\t22") && !has_line("[lua:events] RESEARCH\t1\t104"));
    CHECK(has_line("[lua:events] BUILT\t11\t12\ttrue") && has_line("[lua:events] BUILT\t13\t70\ttrue"));
    CHECK(!has_line("[lua:events] BUILT\t10"));
    int built = count_line("[lua:events] BUILT"), researched = count_line("[lua:events] RESEARCH");
    wl_script_event("every_second");                          // nothing new: no repeats
    CHECK(count_line("[lua:events] BUILT") == built && count_line("[lua:events] RESEARCH") == researched);
    wl_script_event("game_start");                            // new game: first look again
    wl_script_event("every_second");
    CHECK(count_line("[lua:events] BUILT") == built && count_line("[lua:events] RESEARCH") == researched);
    wl_script_set_game(NULL);
    // ---- require attacks and errors ---------------------------------------------------
    printf("-- require attacks\n");
    wl_script_event("req_attack");
    CHECK(count_line("[lua:req] ATTACK\tfalse\tbad name") == 17);   // every bad name refused
    CHECK(!has_line("OUTSIDE RAN") && !has_line("INSIDE scripts"));
    CHECK(has_line("[lua:req] LEN64\tfalse\tnot found"));           // 64 characters is allowed
    CHECK(has_line("[lua:req] NUMBER\tfalse\tmodule '42' not found"));  // a number is a legal name "42"
    CHECK(has_line("[lua:req] MISSING\tfalse\tmodule 'missing.module' not found"));
    wl_script_event("req_bin");
    CHECK(has_line("[lua:req] BIN\tfalse\t") && has_line("binary chunks are not allowed") && has_line("bin.lua"));
    wl_script_event("req_err");
    CHECK(has_line("ERR1\tfalse\t") && has_line("err.lua:1: module broke"));
    CHECK(count_line("module broke") == 2);                            // failures are not cached: runs again
    CHECK(has_line("SYN\tfalse\t") && has_line("syn.lua:1:"));
    wl_script_event("req_cycle");
    CHECK(has_line("[lua:req] CYC self\t") && has_line("self.lua:1: circular require of 'self'"));
    CHECK(has_line("[lua:req] CYC ping\t") && has_line("pong.lua:1: circular require of 'ping'"));
    CHECK(has_line("[lua:req] CYC again\tfalse\t") && count_line("circular require of 'ping'") == 2);  // marks cleared after failure
    wl_script_event("req_budget");
    CHECK(has_line("[lua:req] handler for 'req_budget' failed and was disabled: ") && has_line("instruction limit"));
    wl_script_event("req_again");
    CHECK(has_line("[lua:req] AGAIN calls\t1\ttrue"));                  // still cached after all the errors
    CHECK(has_line("[lua:req] BOM\tbom ok"));                             // UTF-8 BOM skipped, nested require works
    // ---- sandbox ---------------------------------------------------------------------
    printf("-- sandbox attacks\n");
    wl_script_event("a_pcall");
    CHECK(!has_line("E1 ESCAPED") && has_line("[lua:evil] handler for 'a_pcall' failed"));
    wl_script_event("a_xpcall");
    CHECK(!has_line("E1b ESCAPED"));
    wl_script_event("a_co");
    CHECK(!has_line("E2 ESCAPED"));
    wl_script_event("a_pattern");
    CHECK(has_line("E3 pattern\tfalse\t") && has_line("pattern too complex"));
    wl_script_event("a_pattern2");
    CHECK(has_line("E3b pattern\tfalse\t"));
    wl_script_event("a_meta");
    wl_script_event("a_victim");
    CHECK(has_line("[lua:victim] VICTIM upper\tX"));
    wl_script_event("a_gc");
    CHECK(has_line("E6 gc\tfalse\ttrue"));
    wl_script_event("a_dump");
    CHECK(has_line("E8 dump\tnil\tnil"));
    wl_script_event("a_image");
    CHECK(has_line("E5 image\tfalse\t"));
    g_nlines_total = 0;
    wl_script_event("a_log");
    CHECK(g_nlines_total < 2100 && has_line("further output of this mod is not logged"));
    // ---- full restart & wl.reload_mods() ----------------------------------------------
    printf("-- wl_script_restart & wl.reload_mods\n");
    wl_script_set_reload_mods(fake_reload_mods);
    write_mod(0, "remod",
        "wl.log('REMOD_BOOT v1')\n"
        "wl.on('ping_rm', function()\n"
        "  local ok, msg = wl.reload_mods()\n"
        "  wl.log('REMOD_CALL', tostring(ok), tostring(msg))\n"
        "end)\n");
    g_wl.listed_count = 1;
    wl_script_restart();
    CHECK(has_line("[lua:remod] REMOD_BOOT v1"));
    wl_script_event("ping_rm");
    CHECK(g_reload_mods_calls == 1);
    CHECK(has_line("[lua:remod] REMOD_CALL\ttrue\tMods reloaded (2 civs, 3 mods)"));
    write_mod(0, "remod", "wl.log('REMOD_BOOT v2')\n");
    wl_script_restart();
    CHECK(has_line("[lua:remod] REMOD_BOOT v2"));

    // ---- Game.AddCommand / Game.RemoveCommand / unload & restart cleanup --------------
    printf("-- Game.AddCommand / RemoveCommand / unload cleanup\n");
    wl_script_set_rt_cmd_string_fn(fake_rt_str);
    wl_script_set_buildpage_refresh(fake_bp_refresh);
    wl_script_set_local_ctx_fn(fake_local_ctx);
    g_bp_refresh_calls = 0;
    g_local_ctx_state = 0;
    g_local_ctx_on_calls = 0;
    write_mod(0, "rtcmdA",
        "local ok = Game.AddCommand({ building = 84, button = 10, icon = 49, text = 'Trade', help = 'Swap goods', name = 'Trade' })\n"
        "wl.log('RT_ADD_A', tostring(ok))\n"
        "local cond_allow = false\n"
        "Game.CommandCondition('Trade', function(b) wl.log('RT_COND', b.Id) return cond_allow end)\n"
        "Game.OnCommand('Trade', function(b)\n"
        "  wl.log('RT_CLICK_START', b.Id)\n"
        "  Time.Wait(1)\n"
        "  wl.log('RT_CLICK_RESUME', b.Id)\n"
        "end)\n"
        "wl.on('rt_allow', function() cond_allow = true end)\n"
        "wl.on('rt_update', function()\n"
        "  local r = Game.AddCommand({ building = 84, button = 10, icon = 50, text = 'Trade+', help = 'Updated', name = 'Trade', civ = 1, condition_action = 'hide' })\n"
        "  wl.log('RT_UPD', tostring(r))\n"
        "end)\n"
        "wl.on('rt_bad', function()\n"
        "  local ok1 = pcall(Game.AddCommand, { building = 84, button = 0, name = 'X' })\n"
        "  local ok2 = pcall(Game.AddCommand, { building = 84, button = 16, name = 'X' })\n"
        "  local ok3 = pcall(Game.AddCommand, { building = 84, button = 10, name = 'X', condition_action = 'bogus' })\n"
        "  local ok4 = pcall(Game.AddCommand, { building = 84, button = 10, name = '' })\n"
        "  wl.log('RT_BAD', tostring(ok1), tostring(ok2), tostring(ok3), tostring(ok4))\n"
        "end)\n"
        "wl.on('rt_rm_a', function()\n"
        "  wl.log('RT_RM_A', tostring(Game.RemoveCommand('Trade')), tostring(Game.RemoveCommand('Trade')))\n"
        "end)\n");
    write_mod(1, "rtcmdB",
        "Game.AddCommand({ building = 109, button = 4, icon = 32, text = 'Tax', help = 'Levy tax', name = 'Tax', condition = false })\n"
        "wl.on('rt_steal', function() wl.log('RT_STEAL', tostring(Game.RemoveCommand('Trade'))) end)\n");
    g_wl.listed_count = 2;
    wl_script_restart();
    CHECK(has_line("[lua:rtcmdA] RT_ADD_A\ttrue"));
    CHECK(count_rt_cmds() == 2);
    CHECK(g_bp_refresh_calls == 0);  // not in game yet
    int idxA = find_rt_cmd("rtcmdA", "Trade");
    int idxB = find_rt_cmd("rtcmdB", "Tax");
    CHECK(idxA >= 0 && idxB >= 0 && idxA != idxB);
    int nrt = 0;
    const WlRuntimeCmd *rt = wl_rt_commands(&nrt);
    CHECK(nrt == WL_MAX_RT_CMDS);
    CHECK(rt[idxA].building == 84 && rt[idxA].button == 10 && rt[idxA].icon == 49 && rt[idxA].civ == -1 && rt[idxA].page == 1);
    CHECK(strcmp(rt[idxA].cond_name, "Trade") == 0 && strcmp(rt[idxA].cond_action, "disable") == 0);
    CHECK(rt[idxA].help_sid == WL_RT_CMD_SID_BASE + idxA && rt[idxA].gen >= 1);
    CHECK(g_rt_str_set[idxA] && strcmp(g_rt_str_text[idxA], "Trade") == 0 && strcmp(g_rt_str_help[idxA], "Swap goods") == 0);
    CHECK(rt[idxB].building == 109 && rt[idxB].button == 4 && rt[idxB].cond_name[0] == '\0');
    DWORD gen1 = rt[idxA].gen;

    // Enter game, verify condition, click + coroutine local_ctx, update in place, validation, isolation
    wl_script_game_reset(1000);
    wl_script_event("game_start");
    CHECK(wl_script_eval_condition("Trade", 77) == 0);
    wl_script_event("rt_allow");
    CHECK(wl_script_eval_condition("Trade", 77) == 1);
    wl_script_command("Trade", 77);
    CHECK(has_line("[lua:rtcmdA] RT_CLICK_START\t77") && !has_line("[lua:rtcmdA] RT_CLICK_RESUME\t77"));
    wl_script_frame(2500);
    CHECK(has_line("[lua:rtcmdA] RT_CLICK_RESUME\t77") && g_local_ctx_on_calls == 1 && g_local_ctx_state == 0);

    wl_script_event("rt_bad");
    CHECK(has_line("[lua:rtcmdA] RT_BAD\tfalse\tfalse\tfalse\tfalse"));

    // Cross-mod removal must be rejected
    wl_script_event("rt_steal");
    CHECK(has_line("[lua:rtcmdB] RT_STEAL\tfalse"));
    CHECK(find_rt_cmd("rtcmdA", "Trade") == idxA);

    // Update in place: same slot, higher gen, refreshes open panel while in game
    int ref_before = g_bp_refresh_calls;
    wl_script_event("rt_update");
    CHECK(has_line("[lua:rtcmdA] RT_UPD\ttrue"));
    CHECK(find_rt_cmd("rtcmdA", "Trade") == idxA && count_rt_cmds() == 2);
    CHECK(rt[idxA].gen > gen1 && rt[idxA].icon == 50 && rt[idxA].civ == 1 && strcmp(rt[idxA].cond_action, "hide") == 0);
    CHECK(strcmp(g_rt_str_text[idxA], "Trade+") == 0 && strcmp(g_rt_str_help[idxA], "Updated") == 0);
    CHECK(g_bp_refresh_calls == ref_before + 1);
    DWORD gen2 = rt[idxA].gen;

    // Mod A removes its own command
    wl_script_event("rt_rm_a");
    CHECK(has_line("[lua:rtcmdA] RT_RM_A\ttrue\tfalse"));
    CHECK(find_rt_cmd("rtcmdA", "Trade") < 0 && !g_rt_str_set[idxA] && count_rt_cmds() == 1);
    CHECK(g_bp_refresh_calls == ref_before + 2);

    // Re-register on hot reload, then unload via reload without command -> only mod A cleared, mod B stays
    write_mod(0, "rtcmdA",
        "Game.AddCommand({ building = 84, button = 10, icon = 49, text = 'Reloaded', help = 'V2', name = 'Trade' })\n");
    CHECK(wl_script_reload_mod("rtcmdA", NULL, 0) == 1);
    int idxA2 = find_rt_cmd("rtcmdA", "Trade");
    CHECK(idxA2 >= 0 && rt[idxA2].gen > gen2 && count_rt_cmds() == 2);
    CHECK(find_rt_cmd("rtcmdB", "Tax") == idxB);

    // Hot reload mod A with no command -> mod A's command removed, mod B's preserved
    write_mod(0, "rtcmdA", "wl.log('RT_A_EMPTY')\n");
    CHECK(wl_script_reload_mod("rtcmdA", NULL, 0) == 1);
    CHECK(find_rt_cmd("rtcmdA", "Trade") < 0 && !g_rt_str_set[idxA2]);
    CHECK(find_rt_cmd("rtcmdB", "Tax") == idxB && count_rt_cmds() == 1);

    // Full restart clears all runtime commands (including mod B when disabled/empty)
    write_mod(1, "rtcmdB", "wl.log('RT_B_EMPTY')\n");
    wl_script_restart();
    CHECK(count_rt_cmds() == 0 && !g_rt_str_set[idxB]);

    // ---- Custom PNG command icons (cache/cmdicons.txt & resolver callback) ------------
    printf("-- custom PNG command icons\n");
    {
        char cdir[MAX_PATH + 32], cpath[MAX_PATH + 64];
        snprintf(cdir, sizeof cdir, "%s/cache", g_wl.root);
        mkdir(cdir, 0755);
        snprintf(cpath, sizeof cpath, "%s/cmdicons.txt", cdir);
        FILE *cf = fopen(cpath, "wb");
        fputs("iconModA\ttrade\t71\n", cf);
        fputs("iconModA\tcart\t72\n", cf);
        fputs("iconModB\ttrade\t73\n", cf);
        fclose(cf);
    }
    wl_script_set_cmd_icon_fn(NULL);   // exercise cache/cmdicons.txt fallback path
    write_mod(0, "iconModA",
        "Game.AddCommand({ building = 109, button = 4, icon = 'trade', name = 'CmdTrade' })\n"
        "Game.AddCommand({ building = 109, button = 2, icon = 'CART.PNG', name = 'CmdCart' })\n"
        "Game.AddCommand({ building = 109, button = 3, icon = 49, name = 'CmdNum' })\n"
        "Game.AddCommand({ building = 109, button = 12, icon = 'missing_icon', name = 'CmdMiss1' })\n"
        "Game.AddCommand({ building = 109, button = 13, icon = 'missing_icon', name = 'CmdMiss2' })\n"
        "local bad_ok = pcall(Game.AddCommand, { building = 109, button = 14, icon = true, name = 'CmdBad' })\n"
        "wl.log('ICON_BAD_TYPE', tostring(bad_ok))\n");
    write_mod(1, "iconModB",
        "Game.AddCommand({ building = 109, button = 4, icon = 'trade', name = 'CmdBTrade' })\n"
        "Game.AddCommand({ building = 109, button = 2, icon = 'cart', name = 'CmdBCart' })\n"); // cart belongs to iconModA only
    g_wl.listed_count = 2;
    wl_script_restart();
    {
        int iTradeA = find_rt_cmd("iconModA", "CmdTrade");
        int iCartA  = find_rt_cmd("iconModA", "CmdCart");
        int iNumA   = find_rt_cmd("iconModA", "CmdNum");
        int iMiss1  = find_rt_cmd("iconModA", "CmdMiss1");
        int iMiss2  = find_rt_cmd("iconModA", "CmdMiss2");
        int iTradeB = find_rt_cmd("iconModB", "CmdBTrade");
        int iCartB  = find_rt_cmd("iconModB", "CmdBCart");
        CHECK(iTradeA >= 0 && rt[iTradeA].icon == 71);
        CHECK(iCartA >= 0 && rt[iCartA].icon == 72);
        CHECK(iNumA >= 0 && rt[iNumA].icon == 49);
        CHECK(iMiss1 >= 0 && rt[iMiss1].icon == 32);
        CHECK(iMiss2 >= 0 && rt[iMiss2].icon == 32);
        // Unknown icon name 'missing_icon' for iconModA must log ONCE even though used twice
        CHECK(count_line("[lua:iconModA] Game.AddCommand: unknown command icon 'missing_icon'") == 1);
        CHECK(has_line("[lua:iconModA] ICON_BAD_TYPE\tfalse"));
        // Per-mod scoping: iconModB gets its own 'trade' (73), cannot resolve iconModA's 'cart' -> fallback 32
        CHECK(iTradeB >= 0 && rt[iTradeB].icon == 73);
        CHECK(iCartB >= 0 && rt[iCartB].icon == 32);
        CHECK(count_line("[lua:iconModB] Game.AddCommand: unknown command icon 'cart'") == 1);
    }

    // ---- Custom resources: Game.Resources, Player:GetResource/AddResource/SetResource --
    printf("-- custom resources\n");
    memset(g_fres, 0, sizeof g_fres);
    for (int i = 0; i < 256; i++) g_fres_val[i] = 100.0f;
    g_fres[0].id = 211; snprintf(g_fres[0].mod, sizeof g_fres[0].mod, "FaithAndRelics");
    snprintf(g_fres[0].key, sizeof g_fres[0].key, "faith");
    snprintf(g_fres[0].name, sizeof g_fres[0].name, "Faith");
    snprintf(g_fres[0].icon_rel, sizeof g_fres[0].icon_rel, "icons/faith.png");
    g_fres[0].start = 10.0f; g_fres[0].max = 500.0f; g_fres[0].show = 1;
    g_fres[1].id = 212; snprintf(g_fres[1].mod, sizeof g_fres[1].mod, "FaithAndRelics");
    snprintf(g_fres[1].key, sizeof g_fres[1].key, "piety");
    snprintf(g_fres[1].name, sizeof g_fres[1].name, "Piety");
    g_fres[1].start = 0.0f; g_fres[1].max = 0.0f; g_fres[1].show = 0;
    g_fres_val[211] = 10.0f;
    g_fres_val[212] = 0.0f;

    memset(&g_fgather, 0, sizeof g_fgather);
    snprintf(g_fgather[0].key, sizeof g_fgather[0].key, "crystal");
    g_fgather[0].resource_id = 213;
    g_fgather[0].deposit_id = 1226;
    g_fgather[0].male_id = 1227;
    g_fgather[0].female_id = 1228;

    static WlGameApi rfg;
    memset(&rfg, 0, sizeof rfg);
    rfg.local_player = fg_local; rfg.player_count = fg_count; rfg.resource = fg_resource;
    rfg.add_resource = fg_add_resource; rfg.set_resource = fg_set_resource;
    rfg.resources_list = fg_resources_list; rfg.resource_resolve = fg_resource_resolve;
    rfg.gather_by_key = fg_gather_by_key;
    wl_script_set_game(&rfg);

    write_mod(0, "restest",
        "wl.on('t_res', function()\n"
        "  local list = Game.Resources()\n"
        "  wl.log('RES_LIST', #list, list[1].id, list[1].key, list[1].name, list[1].icon, list[1].max, tostring(list[1].show), tostring(list[2].show))\n"
        "  local p = Game.LocalPlayer\n"
        "  wl.log('RES_GET', p:GetResource('faith'), p:GetResource(211), p:GetResource('food'), p:GetResource('Gold'))\n"
        "  local ok1 = p:AddResource('faith', 15)\n"
        "  wl.log('RES_AFTER_ADD', tostring(ok1), p:GetResource('faith'))\n"
        "  local ok2 = p:SetResource('faith', 42)\n"
        "  wl.log('RES_AFTER_SET', tostring(ok2), p:GetResource('faith'))\n"
        "  local ok_bad, err_bad = pcall(function() return p:GetResource('nope') end)\n"
        "  wl.log('RES_BAD', tostring(ok_bad), tostring(err_bad))\n"
        "  local dep = Game.ResourceDeposit('crystal')\n"
        "  local gi = Game.GatherInfo('crystal')\n"
        "  wl.log('GATHER_OK', tostring(dep), gi and gi.resource, gi and gi.deposit, gi and gi.male, gi and gi.female)\n"
        "  wl.log('GATHER_NIL', tostring(Game.ResourceDeposit('faith')), tostring(Game.GatherInfo('nope')))\n"
        "end)\n");
    g_wl.listed_count = 1;
    wl_script_restart();
    wl_script_event("t_res");
    CHECK(has_line("[lua:restest] RES_LIST\t2\t211\tfaith\tFaith\ticons/faith.png\t500.0\ttrue\tfalse") ||
          has_line("[lua:restest] RES_LIST\t2\t211\tfaith\tFaith\ticons/faith.png\t500\ttrue\tfalse"));
    CHECK(has_line("[lua:restest] RES_GET\t10.0\t10.0\t100.0\t100.0") ||
          has_line("[lua:restest] RES_GET\t10\t10\t100\t100"));
    CHECK(g_fres_add_id == 211 && g_fres_add_amt == 15.0f);
    CHECK(has_line("[lua:restest] RES_AFTER_ADD\ttrue\t25.0") ||
          has_line("[lua:restest] RES_AFTER_ADD\ttrue\t25"));
    CHECK(g_fres_set_id == 211 && g_fres_set_val == 42.0f);
    CHECK(has_line("[lua:restest] RES_AFTER_SET\ttrue\t42.0") ||
          has_line("[lua:restest] RES_AFTER_SET\ttrue\t42"));
    CHECK(has_line("[lua:restest] RES_BAD\tfalse\t") && has_line("unknown resource: nope"));
    CHECK(has_line("[lua:restest] GATHER_OK\t1226\t213\t1226\t1227\t1228"));
    CHECK(has_line("[lua:restest] GATHER_NIL\tnil\tnil"));
    wl_script_set_game(NULL);

    // ---- Custom game modes: Game.Mode(), Player:Win([team]), Player:Defeat() ---------
    printf("-- custom game modes & win/defeat\n");
    static WlGameApi mfg;
    memset(&mfg, 0, sizeof mfg);
    mfg.local_player = fg_local; mfg.player_count = fg_count;
    mfg.game_mode = fg_game_mode; mfg.win_player = fg_win_player; mfg.defeat_player = fg_defeat_player;
    wl_script_set_game(&mfg);

    g_fmode = NULL;
    g_fwin_pid = g_fwin_team = g_fwin_calls = 0;
    g_fdefeat_pid = g_fdefeat_calls = 0;
    write_mod(0, "modetest",
        "Game.OnStart(function()\n"
        "  wl.log('GM_START', tostring(Game.Mode()))\n"
        "end)\n"
        "wl.on('gm_end', function()\n"
        "  local ok1 = Game.Player(1):Defeat()\n"
        "  local ok2 = Game.Player(1):Win()\n"
        "  local ok3 = Game.Player(1):Win(true)\n"
        "  local ok_gaia, err_gaia = pcall(function() return Game.Player(0):Defeat() end)\n"
        "  wl.log('GM_CMD', tostring(ok1), tostring(ok2), tostring(ok3), tostring(ok_gaia), tostring(err_gaia))\n"
        "end)\n");
    g_wl.listed_count = 1;
    wl_script_restart();
    wl_script_event("game_start");
    CHECK(has_line("[lua:modetest] GM_START\tnil"));
    g_fmode = "last_town_standing";
    wl_script_event("game_start");
    CHECK(has_line("[lua:modetest] GM_START\tlast_town_standing"));
    wl_script_event("gm_end");
    CHECK(g_fdefeat_calls == 1 && g_fdefeat_pid == 1);
    CHECK(g_fwin_calls == 2 && g_fwin_pid == 1 && g_fwin_team == 1);
    CHECK(has_line("[lua:modetest] GM_CMD\ttrue\ttrue\ttrue\tfalse\t"));
    wl_script_set_game(NULL);

    printf(g_fail ? "TEST FAILED\n" : "TEST PASSED\n");
    return g_fail;
}
