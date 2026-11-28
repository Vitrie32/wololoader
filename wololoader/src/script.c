// Lua 5.4 scripting runtime for mods (<mod>\scripts\main.lua).
//
// THREADING: everything here assumes ONE thread, the game's main thread.
// wl_script_init() is run lazily from wl_civs_ensure() (never from DllMain:
// the CRT may not be ready, AGENTS.md rule 5) and the event functions must be
// called from the same thread. There is no locking.
//
// SANDBOX: only base, string, table, math, utf8 and coroutine are opened.
// io, os, package and debug do not exist. dofile, loadfile, load, loadstring,
// warn, string.dump and print (replaced) are removed. The real require is removed
// too: each mod gets its own require(name), which loads only <mod>\scripts\<name>.lua
// (see api_require). Every mod gets its
// own _ENV table holding private copies of the library tables, so a mod that
// assigns globals or edits string/table/math functions does not affect other
// mods. The string metatable (shared by Lua itself) is locked: getmetatable("")
// returns a string, so ("x"):upper() cannot be redirected by another mod.
// collectgarbage only allows "collect", "count" and "step".
//
// SAFETY: every call into Lua goes through lua_pcall. The instruction budget
// (WL_SCRIPT_INSN_LIMIT) is shared by every thread a call runs, counted by a hook
// every WL_HOOK_STEP instructions; coroutines created by a script inherit the
// hook. Once it is used up the hook fires on every instruction of every thread
// until the call is over, so pcall / xpcall / coroutine.resume cannot catch the
// abort and keep running. Lua patterns run in C without the hook, so the string
// matching functions refuse a pattern whose worst case on the given text is too
// large (pattern_cost). A custom allocator caps the state at WL_SCRIPT_MEM_LIMIT
// bytes, log output is capped per mod, and UI images must be inside a mod folder.
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "wololoader.h"
#include "modsettings.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define WL_SCRIPT_INSN_LIMIT 10000000L          // VM instructions per call
#define WL_HOOK_STEP 1000                       // instructions between budget checks
#define WL_LOG_LINES_PER_MOD 2000               // log lines a mod may write per game start
#define WL_SCRIPT_MEM_LIMIT  (64u * 1024 * 1024)
#define WL_HANDLERS_KEY "wl.handlers"           // registry: event -> array of {fn, mod}
#ifdef _WIN32
#define WL_SEP "\\"
#else
#define WL_SEP "/"                              // native test build only
#endif

static lua_State *g_L;
static int g_init_done;
static void (*g_ui_message)(const char *);
static void (*g_ui_dialog)(const char *);
static void (*g_ui_sound)(int id);
static void (*g_ui_counter)(const char *);
static void (*g_ui_chat)(const char *, const char *, int);
static void (*g_ui_ask)(const char *, int);
static DWORD g_now;                             // clock of the last frame / reset (ms)
static size_t g_mem_used;
// Log lines per mod (by id), so one mod cannot fill the disk; reset by wl_script_game_reset.
static struct { char mod[64]; int lines; } g_logcount[64];

// --- allocator with a cap ---------------------------------------------------
static void *wl_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    if (!ptr) osize = 0;  // osize is a type tag for new blocks
    if (nsize == 0) {
        free(ptr);
        g_mem_used -= osize;
        return NULL;
    }
    if (g_mem_used - osize + nsize > WL_SCRIPT_MEM_LIMIT) return NULL;
    void *p = realloc(ptr, nsize);
    if (p) g_mem_used = g_mem_used - osize + nsize;
    return p;
}

static int wl_lua_panic(lua_State *L) {
    wl_log("[lua] PANIC: %s", lua_tostring(L, -1));
    return 0;  // returning makes Lua abort(); only reached outside a pcall (a bug here)
}

// --- instruction limit ------------------------------------------------------
// One budget per top-level call (budget_start), shared by all threads it runs.
static long g_insn_used;
static int g_aborting;
static void insn_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    if (!g_aborting) {
        g_insn_used += WL_HOOK_STEP;
        if (g_insn_used < WL_SCRIPT_INSN_LIMIT) return;
        g_aborting = 1;
    }
    // from now on every instruction of this thread fails, so a pcall / xpcall in the
    // script cannot catch the abort and go on; other threads stop at their next check
    lua_sethook(L, insn_hook, LUA_MASKCOUNT, 1);
    luaL_error(L, "script aborted: instruction limit (%d) exceeded", (int)WL_SCRIPT_INSN_LIMIT);
}
static void budget_start(lua_State *L) {
    g_insn_used = 0;
    g_aborting = 0;
    lua_sethook(L, insn_hook, LUA_MASKCOUNT, WL_HOOK_STEP);
}

// pcall with a fresh instruction budget. Stack: function and nargs args.
static int api_raw_local(lua_State *L);
static int api_raw_count(lua_State *L);
static int api_raw_resource(lua_State *L);
static int api_raw_resolve_resource(lua_State *L);
static int api_raw_resources(lua_State *L);
static int api_raw_gather_info(lua_State *L);
static int api_raw_is_enemy(lua_State *L);
static int api_raw_units(lua_State *L);
static int api_raw_cmd(lua_State *L);
static int api_raw_selected(lua_State *L);
static int api_raw_researched(lua_State *L);
static int api_raw_time(lua_State *L);
static int api_raw_mode(lua_State *L);
static int api_raw_mode_enemy(lua_State *L);
static int api_raw_map_size(lua_State *L);
static int api_raw_map_capture(lua_State *L);
static int api_raw_unit_control_disabled(lua_State *L);
static int api_raw_last_map_click(lua_State *L);
static int api_raw_restart(lua_State *L);
static int api_raw_session_get(lua_State *L);
static int api_raw_session_set(lua_State *L);
static int api_raw_session_clear(lua_State *L);
static int api_sound_file(lua_State *L);
static int api_raw_ui_create(lua_State *L);
static int api_raw_ui_set(lua_State *L);
static int api_raw_ui_destroy(lua_State *L);
static int api_raw_ui_text(lua_State *L);
static int api_raw_set_cond(lua_State *L);
static int api_raw_add_command(lua_State *L);
static int api_raw_remove_command(lua_State *L);
static int api_raw_unit_by_id(lua_State *L);
static int api_raw_player_civ(lua_State *L);
static int api_raw_is_multiplayer(lua_State *L);
static int api_raw_cheats_enabled(lua_State *L);
static int api_feature(lua_State *L);
static int api_dev_eval(lua_State *L);
static int api_reload(lua_State *L);
static int api_reload_mods(lua_State *L);

static int g_in_game;
static uint32_t g_mod_gen;
static int g_dispatch_depth;
static int g_handlers_dirty;

static int (*g_is_mp)(void);
static int (*g_cheats_on)(void);
static BOOL (*g_feature_fn)(const char *);
static void (*g_set_dev_ctx)(int);
static int (*g_reload_mods_fn)(char *msg, size_t cap);
static void (*g_rt_cmd_str_fn)(int slot, const char *text, const char *help);
static void (*g_buildpage_refresh_fn)(void);
static void (*g_set_local_ctx_fn)(int on);
static int (*g_cmd_icon_fn)(const char *mod, const char *name);

void wl_script_set_mp_info(int (*is_mp)(void), int (*cheats_on)(void)) {
    g_is_mp = is_mp;
    g_cheats_on = cheats_on;
}
void wl_script_set_feature(BOOL (*feature_fn)(const char *)) { g_feature_fn = feature_fn; }
void wl_script_set_dev_ctx_fn(void (*set_dev_ctx)(int)) { g_set_dev_ctx = set_dev_ctx; }
void wl_script_set_reload_mods(int (*fn)(char *msg, size_t cap)) { g_reload_mods_fn = fn; }
void wl_script_set_rt_cmd_string_fn(void (*fn)(int slot, const char *text, const char *help)) { g_rt_cmd_str_fn = fn; }
void wl_script_set_buildpage_refresh(void (*fn)(void)) { g_buildpage_refresh_fn = fn; }
void wl_script_set_local_ctx_fn(void (*fn)(int on)) { g_set_local_ctx_fn = fn; }
void wl_script_set_cmd_icon_fn(int (*fn)(const char *mod, const char *name)) { g_cmd_icon_fn = fn; }

static int is_multiplayer(void) { return g_is_mp ? g_is_mp() : 0; }
static int cheats_enabled(void) { return g_cheats_on ? g_cheats_on() : 0; }
static int dev_allowed(void) { return !(is_multiplayer() && !cheats_enabled()); }

#define WL_CONDS_KEY "wl_conditions"
#define WL_CONSOLE_ENV_KEY "wl.console_env"

// g_depth: only the outermost call starts a budget (a nested call shares it).
static int g_depth;
static int guarded_pcall(lua_State *L, int nargs) {
    lua_Hook oh = lua_gethook(L);
    int om = lua_gethookmask(L), oc = lua_gethookcount(L);
    if (g_depth++ == 0) budget_start(L);
    else lua_sethook(L, insn_hook, LUA_MASKCOUNT, g_aborting ? 1 : WL_HOOK_STEP);
    int rc = lua_pcall(L, nargs, 0, 0);
    lua_sethook(L, oh, om, oc);          // a nested call gives the outer one its hook back
    if (--g_depth == 0) g_aborting = 0;
    return rc;
}


// --- timers, waiting coroutines, asks ---------------------------------------
// Every handler / timer callback / ask callback runs as a coroutine so that
// wl.wait can yield. A coroutine that yields is anchored in the registry
// (luaL_ref) and kept in g_tasks until wl_script_frame resumes it.
#define WL_MAX_TIMERS 64
#define WL_MAX_TASKS  64
#define WL_MAX_ASKS   16
typedef struct { int used; int id; DWORD next, interval; int fn; char mod[64]; } WlTimer;
typedef struct { int used; int ref; uint32_t seq; lua_State *co; DWORD wake; int local_ctx; char mod[64]; } WlTask;
typedef struct { int used; int token; int fn; int nil_pending; char mod[64]; } WlAsk;
static WlTimer g_timers[WL_MAX_TIMERS];
static WlTask g_tasks[WL_MAX_TASKS];
static WlAsk g_asks[WL_MAX_ASKS];
static int g_next_timer_id = 1, g_next_ask_token = 1;
static uint32_t g_next_task_seq = 1;
static int g_in_local_cmd;
static char g_err[512];                         // message of the last failed step

// Runtime building command buttons registered via Game.AddCommand (main thread only;
// tooltip strings are stored thread-safely in civs.c via g_rt_cmd_str_fn).
static WlRuntimeCmd g_rt_cmds[WL_MAX_RT_CMDS];
// Monotonic across hot reloads and restarts so 0x6c9200 never skips updating tooltips
// when a reused slot keeps the same (cmd, icon) with new text/help.
static DWORD g_rt_cmd_gen;

const WlRuntimeCmd *wl_rt_commands(int *n) {
    if (n) *n = WL_MAX_RT_CMDS;
    return g_rt_cmds;
}

static int unload_mod_rt_cmds(const char *mod_id) {
    int removed = 0;
    for (int i = 0; i < WL_MAX_RT_CMDS; i++) {
        if (g_rt_cmds[i].used && strcmp(g_rt_cmds[i].mod, mod_id) == 0) {
            g_rt_cmds[i].used = 0;
            if (g_rt_cmd_str_fn) g_rt_cmd_str_fn(i, NULL, NULL);
            removed++;
        }
    }
    return removed;
}

// Deduplication list for unknown Game.AddCommand icon names (each mod+name warns once).
#define WL_MAX_WARNED_CMD_ICONS 64
static struct { char mod[64]; char name[64]; } g_warned_cmd_icons[WL_MAX_WARNED_CMD_ICONS];
static int g_warned_cmd_icons_count;

static int str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

// Resolves a PNG command icon name for `mod` through g_cmd_icon_fn (iconmerge.c in-memory table)
// or by reading <root>/cache/cmdicons.txt ("<mod>\t<name>\t<frame>") when no callback is set.
static int resolve_cmd_icon(const char *mod, const char *name) {
    if (!mod || !mod[0] || !name || !name[0]) return -1;
    size_t len = strlen(name);
    if (len >= 4 && str_ieq(name + len - 4, ".png")) len -= 4;
    if (len == 0 || len >= 64) return -1;
    char stem[64];
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == '\t' || c == '\r' || c == '\n') return -1;
        stem[i] = c;
    }
    stem[len] = '\0';
    if (g_cmd_icon_fn) return g_cmd_icon_fn(mod, stem);
    if (!g_wl.root[0]) return -1;
    char path[MAX_PATH + 32];
    snprintf(path, sizeof path, "%s" WL_SEP "cache" WL_SEP "cmdicons.txt", g_wl.root);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[256];
    int found = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1++ = '\0';
        char *t2 = strchr(t1, '\t');
        if (!t2) continue;
        *t2++ = '\0';
        if (strcmp(line, mod) == 0 && str_ieq(t1, stem)) {
            char *end = NULL;
            long frame = strtol(t2, &end, 10);
            if (end && end != t2 && *end == '\0' && frame >= 0) {
                found = (int)frame;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

static void warn_unknown_cmd_icon_once(const char *mod, const char *name) {
    for (int i = 0; i < g_warned_cmd_icons_count; i++) {
        if (strcmp(g_warned_cmd_icons[i].mod, mod) == 0 &&
            str_ieq(g_warned_cmd_icons[i].name, name))
            return;
    }
    if (g_warned_cmd_icons_count < WL_MAX_WARNED_CMD_ICONS) {
        snprintf(g_warned_cmd_icons[g_warned_cmd_icons_count].mod,
                 sizeof g_warned_cmd_icons[0].mod, "%s", mod);
        snprintf(g_warned_cmd_icons[g_warned_cmd_icons_count].name,
                 sizeof g_warned_cmd_icons[0].name, "%s", name);
        g_warned_cmd_icons_count++;
    }
    wl_log("[lua:%s] Game.AddCommand: unknown command icon '%s', falling back to frame 32", mod, name);
}

static void clear_all_rt_cmds(void) {
    for (int i = 0; i < WL_MAX_RT_CMDS; i++) {
        if (g_rt_cmds[i].used) {
            g_rt_cmds[i].used = 0;
            if (g_rt_cmd_str_fn) g_rt_cmd_str_fn(i, NULL, NULL);
        }
    }
    g_warned_cmd_icons_count = 0;
}

typedef struct { int used; char mod[64]; char text[128]; } WlModCounter;
static WlModCounter g_mod_counters[WL_MAX_MODS];

enum { STEP_DONE, STEP_YIELD, STEP_ERROR };

static DWORD seconds_to_ms(lua_Number s) {
    if (!(s > 0)) return 0;
    if (s > 4000000.0) s = 4000000.0;           // ~46 days, keeps DWORD math safe
    return (DWORD)(s * 1000.0);
}

// Runs / resumes `co` (nargs values on its stack). Fresh instruction budget
// per resume (hooks are per thread, so it is set on `co`).
static int task_step(lua_State *co, int nargs, DWORD *wait_ms) {
    int nres = 0;
    *wait_ms = 0;
    lua_Hook oh = lua_gethook(co);
    int om = lua_gethookmask(co), oc = lua_gethookcount(co);
    if (g_depth++ == 0) budget_start(co);
    else lua_sethook(co, insn_hook, LUA_MASKCOUNT, g_aborting ? 1 : WL_HOOK_STEP);
    int rc = lua_resume(co, g_L, nargs, &nres);
    lua_sethook(co, oh, om, oc);
    if (--g_depth == 0) g_aborting = 0;
    if (rc == LUA_YIELD) {
        // wl.wait yields the number of seconds; a bare coroutine.yield() = next frame
        if (nres >= 1 && lua_isnumber(co, -1)) *wait_ms = seconds_to_ms(lua_tonumber(co, -1));
        lua_settop(co, 0);
        return STEP_YIELD;
    }
    if (rc == LUA_OK) { lua_settop(co, 0); return STEP_DONE; }
    const char *m = lua_tostring(co, -1);
    snprintf(g_err, sizeof g_err, "%s", m ? m : "(error object is not a string)");
    lua_settop(co, 0);
    return STEP_ERROR;
}

// Stack of L: function, then nargs arguments (all consumed). Runs the function
// as a new coroutine up to its first yield. If it yields it becomes a task.
// Returns STEP_*; on STEP_ERROR the message is in g_err (the caller logs it).
static int start_coroutine(lua_State *L, int nargs, const char *mod) {
    lua_State *co = lua_newthread(L);           // L: fn args thread
    lua_insert(L, -(nargs + 2));                // L: thread fn args
    lua_xmove(L, co, nargs + 1);                // co: fn args; L: thread
    DWORD wait;
    int st = task_step(co, nargs, &wait);
    int slot = -1;
    if (st == STEP_YIELD) {
        for (int i = 0; i < WL_MAX_TASKS; i++) if (!g_tasks[i].used) { slot = i; break; }
        if (slot < 0) wl_log("[lua:%s] too many waiting coroutines, one was dropped", mod);
    }
    if (slot >= 0) {
        WlTask *t = &g_tasks[slot];
        t->used = 1;
        t->seq = g_next_task_seq++;
        t->co = co;
        t->wake = g_now + wait;
        t->local_ctx = (g_in_local_cmd > 0);
        snprintf(t->mod, sizeof t->mod, "%s", mod);
        t->ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pops the thread
    } else {
        lua_pop(L, 1);                          // thread
    }
    return st;
}

// Calls registry function `fnref` (arg: none, or a boolean / nil for asks) as a coroutine.
static void run_callback(int fnref, const char *mod, const char *what, int arg_kind, int arg) {
    lua_State *L = g_L;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, fnref);
    int nargs = 0;
    if (arg_kind) {
        if (arg < 0) lua_pushnil(L); else lua_pushboolean(L, arg);
        nargs = 1;
    }
    if (start_coroutine(L, nargs, mod) == STEP_ERROR)
        wl_log("[lua:%s] error in %s: %s", mod, what, g_err);
    lua_settop(L, top);
}

static void free_timer(WlTimer *t) {
    if (!t->used) return;
    luaL_unref(g_L, LUA_REGISTRYINDEX, t->fn);
    t->used = 0;
}

static void free_ask(WlAsk *a) {
    if (!a->used) return;
    luaL_unref(g_L, LUA_REGISTRYINDEX, a->fn);
    a->used = 0;
}

void wl_script_game_reset(DWORD now_ms) {
    g_now = now_ms;
    g_in_game = 0;
    memset(g_logcount, 0, sizeof g_logcount);
    memset(g_mod_counters, 0, sizeof g_mod_counters);
    if (!g_L) return;
    for (int i = 0; i < WL_MAX_TIMERS; i++) free_timer(&g_timers[i]);
    for (int i = 0; i < WL_MAX_ASKS; i++) free_ask(&g_asks[i]);
    for (int i = 0; i < WL_MAX_TASKS; i++)
        if (g_tasks[i].used) { luaL_unref(g_L, LUA_REGISTRYINDEX, g_tasks[i].ref); g_tasks[i].used = 0; }
}

void wl_script_ask_answer(int token, int answer) {
    if (!g_L) return;
    for (int i = 0; i < WL_MAX_ASKS; i++) {
        WlAsk *a = &g_asks[i];
        if (!a->used || a->token != token) continue;
        int fn = a->fn;
        char mod[64];
        snprintf(mod, sizeof mod, "%s", a->mod);
        a->used = 0;                            // release before the callback may ask again
        run_callback(fn, mod, "wl.ask callback", 1, answer);
        luaL_unref(g_L, LUA_REGISTRYINDEX, fn);
        return;
    }
}

void wl_script_frame(DWORD now_ms) {
    if (!g_L) return;
    g_now = now_ms;
    // 1. asks that could not be shown: callback(nil) one frame later
    for (int i = 0; i < WL_MAX_ASKS; i++)
        if (g_asks[i].used && g_asks[i].nil_pending) wl_script_ask_answer(g_asks[i].token, -1);
    // 2. timers (callbacks may add / cancel timers while we iterate)
    for (int i = 0; i < WL_MAX_TIMERS; i++) {
        WlTimer *t = &g_timers[i];
        if (!t->used || (int32_t)(now_ms - t->next) < 0) continue;
        int fn = t->fn;
        char mod[64];
        snprintf(mod, sizeof mod, "%s", t->mod);
        if (t->interval) {                      // repeating: keep fn alive for the next round
            t->next += t->interval;
            if ((int32_t)(now_ms - t->next) >= 0) t->next = now_ms + t->interval;  // no catch-up bursts
            run_callback(fn, mod, "wl.every callback", 0, 0);
        } else {
            t->used = 0;                        // one shot: release the slot, unref after the call
            run_callback(fn, mod, "wl.after callback", 0, 0);
            luaL_unref(g_L, LUA_REGISTRYINDEX, fn);
        }
    }
    // 3. coroutines waiting in wl.wait
    for (int i = 0; i < WL_MAX_TASKS; i++) {
        WlTask *t = &g_tasks[i];
        if (!t->used || (int32_t)(now_ms - t->wake) < 0) continue;
        lua_State *co = t->co;
        int ref = t->ref;
        uint32_t seq = t->seq;
        int lctx = t->local_ctx;
        char mod[64];
        snprintf(mod, sizeof mod, "%s", t->mod);
        DWORD wait;
        if (lctx) {
            g_in_local_cmd++;
            if (g_set_local_ctx_fn) g_set_local_ctx_fn(1);
        }
        int st = task_step(co, 0, &wait);
        if (lctx) {
            if (--g_in_local_cmd == 0 && g_set_local_ctx_fn) g_set_local_ctx_fn(0);
        }
        // task_step may have triggered wl_script_game_reset or mod reload (freeing t->ref).
        if (!t->used || t->co != co || t->ref != ref || t->seq != seq) continue;
        if (st == STEP_YIELD) { t->wake = now_ms + wait; continue; }
        if (st == STEP_ERROR) wl_log("[lua:%s] error in coroutine (stopped): %s", mod, g_err);
        luaL_unref(g_L, LUA_REGISTRYINDEX, t->ref);   // coroutine is finished, let the GC have it
        t->used = 0;
    }
}

// --- API functions (upvalue 1 = mod id string, upvalue 2 = mod index) -------
// Joins all arguments like print() would, separated by tabs.
static void join_args(lua_State *L, luaL_Buffer *b) {
    int n = lua_gettop(L);
    luaL_buffinit(L, b);
    for (int i = 1; i <= n; i++) {
        luaL_tolstring(L, i, NULL);  // pushes the string
        if (i > 1) luaL_addchar(b, '\t');
        luaL_addvalue(b);
    }
}

static int log_allowed(const char *mod) {
    int i;
    for (i = 0; i < 64 && g_logcount[i].mod[0]; i++) if (!strcmp(g_logcount[i].mod, mod)) break;
    if (i == 64) return 1;
    if (!g_logcount[i].mod[0]) snprintf(g_logcount[i].mod, sizeof g_logcount[i].mod, "%s", mod);
    int n = ++g_logcount[i].lines;
    if (n == WL_LOG_LINES_PER_MOD + 1)
        wl_log("[lua:%s] more than %d lines: further output of this mod is not logged until the next game", mod, WL_LOG_LINES_PER_MOD);
    return n <= WL_LOG_LINES_PER_MOD;
}
static int api_log(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    if (!log_allowed(mod)) return 0;
    luaL_Buffer b;
    join_args(L, &b);
    luaL_pushresult(&b);
    wl_log("[lua:%s] %s", mod, lua_tostring(L, -1));
    return 0;
}

// Color of the next message (game palette index, -1 = the game's default 0x24);
// read by the message backend (luagame.c) while g_ui_message runs.
int wl_script_msg_color = -1;
static int api_message(lua_State *L) {
    const char *t = luaL_checkstring(L, 1);
    wl_script_msg_color = lua_isinteger(L, 2) ? (int)(lua_tointeger(L, 2) & 0xff) : -1;
    if (g_ui_message) g_ui_message(t);
    else wl_log("[lua] message: %s", t);
    wl_script_msg_color = -1;
    return 0;
}

static void show_ui_message(const char *t) {
    wl_script_msg_color = -1;
    if (g_ui_message) g_ui_message(t);
    else wl_log("[lua] message: %s", t);
}

// wl.chat(text [, sender [, player]]): player 1..8 = colour, anything else = default
static int api_chat(lua_State *L) {
    const char *t = luaL_checkstring(L, 1);
    const char *sender = luaL_optstring(L, 2, NULL);
    lua_Integer player = luaL_optinteger(L, 3, 0);
    if (player < 1 || player > 8) player = 0;
    if (g_ui_chat) g_ui_chat(t, sender, (int)player);
    else wl_log("[lua] chat: %s%s%s", sender ? sender : "", sender ? ": " : "", t);
    return 0;
}

// wl.ask(text, callback): callback(true | false | nil)
static int api_ask(lua_State *L) {
    const char *t = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    WlAsk *a = NULL;
    for (int i = 0; i < WL_MAX_ASKS; i++) if (!g_asks[i].used) { a = &g_asks[i]; break; }
    if (!a) return luaL_error(L, "too many open wl.ask questions");
    lua_pushvalue(L, 2);
    a->fn = luaL_ref(L, LUA_REGISTRYINDEX);
    a->used = 1;
    a->token = g_next_ask_token++;
    a->nil_pending = 0;
    snprintf(a->mod, sizeof a->mod, "%s", lua_tostring(L, lua_upvalueindex(1)));
    if (g_ui_ask) g_ui_ask(t, a->token);
    else { wl_log("[lua] ask: %s", t); a->nil_pending = 1; }
    return 0;
}

static int new_timer(lua_State *L, int repeating) {
    lua_Number sec = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (sec != sec || sec < 0) return luaL_argerror(L, 1, "seconds must be >= 0");
    WlTimer *t = NULL;
    for (int i = 0; i < WL_MAX_TIMERS; i++) if (!g_timers[i].used) { t = &g_timers[i]; break; }
    if (!t) return luaL_error(L, "too many timers");
    DWORD ms = seconds_to_ms(sec);
    if (repeating && ms < 50) ms = 50;          // an every() must not run more than once per ~frame
    lua_pushvalue(L, 2);
    t->fn = luaL_ref(L, LUA_REGISTRYINDEX);
    t->used = 1;
    t->id = g_next_timer_id++;
    t->next = g_now + ms;
    t->interval = repeating ? ms : 0;
    snprintf(t->mod, sizeof t->mod, "%s", lua_tostring(L, lua_upvalueindex(1)));
    lua_pushinteger(L, t->id);
    return 1;
}
static int api_after(lua_State *L) { return new_timer(L, 0); }
static int api_every(lua_State *L) { return new_timer(L, 1); }

// wl.cancel(handle): true if a timer was cancelled
static int api_cancel(lua_State *L) {
    lua_Integer id = luaL_checkinteger(L, 1);
    for (int i = 0; i < WL_MAX_TIMERS; i++)
        if (g_timers[i].used && g_timers[i].id == id) {
            free_timer(&g_timers[i]);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

// wl.wait(seconds): yields the running handler / timer coroutine
static int api_wait(lua_State *L) {
    lua_Number sec = luaL_checknumber(L, 1);
    if (sec != sec || sec < 0) return luaL_argerror(L, 1, "seconds must be >= 0");
    if (!lua_isyieldable(L))
        return luaL_error(L, "wl.wait can only be used inside an event handler or timer callback, not at the top level of main.lua");
    lua_pushnumber(L, sec);
    return lua_yield(L, 1);
}

// wl.sound(id or name): game UI sounds 0..50 (table in re/research/sound_counter.md).
static const struct { const char *name; int id; } k_sounds[] = {
    {"chat", 0x02}, {"cantdo", 0x03}, {"tribute", 0x04}, {"attack", 0x0f},
    {"townbell", 0x27}, {"townrecall", 0x28}, {"objective", 0x2d}, {"relic", 0x2e},
};
static int api_sound(lua_State *L) {
    int id = -1;
    if (lua_type(L, 1) == LUA_TNUMBER) id = (int)lua_tointeger(L, 1);
    else {
        const char *n = luaL_checkstring(L, 1);
        for (size_t i = 0; i < sizeof k_sounds / sizeof k_sounds[0]; i++)
            if (strcmp(n, k_sounds[i].name) == 0) id = k_sounds[i].id;
        if (id < 0) return luaL_error(L, "wl.sound: unknown sound name '%s'", n);
    }
    if (id < 0 || id > 50) return luaL_error(L, "wl.sound: id must be 0..50");
    if (g_ui_sound) g_ui_sound(id);
    else wl_log("[lua] sound: %d", id);
    return 0;
}

// wl.counter(text): one line in the countdown-clock area; nil or "" hides it.
// Tracked per mod so multiple mods share the line ("  |  ") and unloading a mod clears only its own.
static void sync_counter_display(int log_when_no_backend) {
    char combined[512];
    size_t pos = 0;
    combined[0] = 0;
    for (int i = 0; i < WL_MAX_MODS; i++) {
        if (!g_mod_counters[i].used || !g_mod_counters[i].text[0]) continue;
        if (pos > 0 && pos + 5 < sizeof combined) {
            memcpy(combined + pos, "  |  ", 5);
            pos += 5;
        }
        size_t rem = sizeof combined - 1 - pos;
        size_t len = strlen(g_mod_counters[i].text);
        if (len > rem) len = rem;
        memcpy(combined + pos, g_mod_counters[i].text, len);
        pos += len;
        combined[pos] = 0;
    }
    const char *arg = combined[0] ? combined : NULL;
    if (g_ui_counter) g_ui_counter(arg);
    else if (log_when_no_backend) wl_log("[lua] counter: %s", arg ? arg : "(hidden)");
}

static void unload_mod_counter(const char *mod) {
    int changed = 0;
    for (int i = 0; i < WL_MAX_MODS; i++) {
        if (g_mod_counters[i].used && strcmp(g_mod_counters[i].mod, mod) == 0) {
            if (g_mod_counters[i].text[0]) changed = 1;
            g_mod_counters[i].used = 0;
            g_mod_counters[i].text[0] = 0;
        }
    }
    if (changed) sync_counter_display(0);
}

static int api_counter(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    const char *t = lua_isnoneornil(L, 1) ? NULL : luaL_checkstring(L, 1);
    if (t && !*t) t = NULL;
    int slot = -1;
    for (int i = 0; i < WL_MAX_MODS; i++) {
        if (g_mod_counters[i].used && strcmp(g_mod_counters[i].mod, mod) == 0) { slot = i; break; }
        if (!g_mod_counters[i].used && slot < 0) slot = i;
    }
    if (slot >= 0) {
        WlModCounter *c = &g_mod_counters[slot];
        if (t) {
            c->used = 1;
            snprintf(c->mod, sizeof c->mod, "%s", mod);
            snprintf(c->text, sizeof c->text, "%s", t);
        } else {
            c->used = 0;
            c->text[0] = 0;
        }
    }
    sync_counter_display(1);
    return 0;
}

static int api_dialog(lua_State *L) {
    const char *t = luaL_checkstring(L, 1);
    if (g_ui_dialog) g_ui_dialog(t);
    else wl_log("[lua] dialog: %s", t);
    return 0;
}

// wl.on(event, fn): appends {fn=, mod=} to handlers[event]
static int api_on(lua_State *L) {
    const char *ev = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    lua_getfield(L, -1, ev);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, ev);
    }
    lua_createtable(L, 0, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "fn");
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setfield(L, -2, "mod");
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    return 0;
}

// print replacement: same as wl.log, needs the mod id upvalue too
#define PUSH_MOD_CFUNC(L, f, id) do { lua_pushstring((L), (id)); lua_pushcclosure((L), (f), 1); } while (0)

// --- require ----------------------------------------------------------------
// require("ai.economy") runs <mod>\scripts\ai\economy.lua in the SAME environment as
// main.lua (text chunks only, mode "t"). The result is cached per mod (upvalue 2);
// a module that is already loading (directly or through others) is a circular
// require error. The name is checked before any path is built: only ASCII letters,
// digits, '_' and single '.' separators, 1..64 characters. So "..", "/", "\\", ":"
// and absolute paths cannot be expressed, and nothing outside scripts\ is read.
// Upvalues: 1 = mod env, 2 = cache (name -> result), 3 = loading (name -> true),
// 4 = mod folder, 5 = mod id. Scripts cannot reach these tables (no debug library).
#define WL_MODULE_NAME_MAX 64
static char *read_whole(const char *path, size_t *len);

static int valid_module_name(const char *s, size_t n) {
    if (n == 0 || n > WL_MODULE_NAME_MAX || s[0] == '.' || s[n - 1] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.') {
            if (s[i - 1] == '.') return 0;          // "a..b" (i > 0 here: s[0] is not '.')
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return 0;                               // also rejects NUL, '/', '\\', ':', space
        }
    }
    return 1;
}

static int api_require(lua_State *L) {
    size_t n;
    const char *name = luaL_checklstring(L, 1, &n);
    if (!valid_module_name(name, n))
        return luaL_error(L, "require: bad module name (letters, digits, '_' and '.' between parts, 1..%d characters, like \"ai.economy\")", WL_MODULE_NAME_MAX);
    // cached? (a module that returned false is cached too, so test for nil)
    lua_pushvalue(L, 1);
    lua_rawget(L, lua_upvalueindex(2));
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    lua_pushvalue(L, 1);
    lua_rawget(L, lua_upvalueindex(3));
    if (!lua_isnil(L, -1)) return luaL_error(L, "circular require of '%s'", name);
    lua_pop(L, 1);

    // name is clean now (only [A-Za-z0-9_] and '.'), so it can be a path part
    char rel[WL_MODULE_NAME_MAX + 8];
    for (size_t i = 0; i < n; i++) rel[i] = name[i] == '.' ? WL_SEP[0] : name[i];
    rel[n] = 0;
    char path[MAX_PATH + 96];
    snprintf(path, sizeof path, "%s" WL_SEP "scripts" WL_SEP "%s.lua", lua_tostring(L, lua_upvalueindex(4)), rel);
    size_t len = 0;
    char *src = read_whole(path, &len);
    if (!src) return luaL_error(L, "module '%s' not found (this mod has no scripts file for it)", name);
    const char *code = src;
    if (len >= 3 && (unsigned char)code[0] == 0xEF && (unsigned char)code[1] == 0xBB && (unsigned char)code[2] == 0xBF) {
        code += 3; len -= 3;                        // UTF-8 BOM (Notepad)
    }
    if (len > 0 && code[0] == 0x1b) {               // precompiled bytecode: never run
        free(src);
        return luaL_error(L, "%s: binary chunks are not allowed in mod scripts", path);
    }
    char chunk[MAX_PATH + 96];
    snprintf(chunk, sizeof chunk, "@%s", path);     // error messages name the file
    int rc = luaL_loadbufferx(L, code, len, chunk, "t");
    free(src);
    if (rc != LUA_OK) return lua_error(L);          // "<file>:<line>: ..." message

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setupvalue(L, -2, 1);                       // the module's _ENV = this mod's env
    lua_pushvalue(L, 1);
    lua_pushboolean(L, 1);
    lua_rawset(L, lua_upvalueindex(3));
    // lua_pcall, not a plain call: a failing module must clear its "loading" mark.
    // The instruction budget is shared: this runs inside the caller's budget (g_depth > 0).
    rc = lua_pcall(L, 0, 1, 0);
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    lua_rawset(L, lua_upvalueindex(3));
    if (rc != LUA_OK) return lua_error(L);          // error message already names the file
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }   // no return value = true
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -2);
    lua_rawset(L, lua_upvalueindex(2));
    return 1;
}

// Adds require to a mod env (the tables are closure upvalues, not reachable from Lua).
static void push_require(lua_State *L, int env, const WlMod *m) {
    lua_pushvalue(L, env);                          // upvalue 1: the env
    lua_newtable(L);                                // upvalue 2: cache
    lua_newtable(L);                                // upvalue 3: loading
    lua_pushstring(L, m->dir);                      // upvalue 4
    lua_pushstring(L, m->id);                       // upvalue 5
    lua_pushcclosure(L, api_require, 5);
    lua_setfield(L, env, "require");
}

// --- environment construction -----------------------------------------------
static const char *const k_base_keep[] = {
    "assert", "collectgarbage", "error", "getmetatable", "ipairs", "next", "pairs",
    "pcall", "rawequal", "rawget", "rawlen", "rawset", "select", "setmetatable",
    "tonumber", "tostring", "type", "xpcall", NULL};
static const char *const k_libs[] = {"string", "table", "math", "utf8", "coroutine", NULL};

// Shallow copy of the table at `idx` (library tables hold only functions/numbers).
static void push_table_copy(lua_State *L, int idx) {
    idx = lua_absindex(L, idx);
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_settable(L, -4);
    }
}

// Case-insensitive exact match using _strnicmp (tools/fake_windows/windows.h lacks _stricmp).
static int is_base_mod_id(const char *id) {
    return id && strlen(id) == 14 && _strnicmp(id, "WololoaderBase", 14) == 0;
}

// Fills the table at index t with the mod's effective settings (modsettings.c).
static void fill_settings_table(lua_State *L, int t, const WlMod *m) {
    static WlSetting set[WL_MAX_SETTINGS];
    int n = wl_settings_load(m, set, WL_MAX_SETTINGS);
    for (int i = 0; i < n; i++) {
        if (set[i].type == WL_SET_BOOL) lua_pushboolean(L, set[i].on);
        else if (set[i].type == WL_SET_NUMBER) {
            double v = set[i].num;   // whole numbers as Lua integers (50, not 50.0)
            if (v == (double)(lua_Integer)v) lua_pushinteger(L, (lua_Integer)v);
            else lua_pushnumber(L, v);
        }
        else lua_pushstring(L, set[i].choices[set[i].choice]);
        lua_setfield(L, t, set[i].id);
    }
}

// Multiplayer lobby (mpcheck.c): refill every loaded mod's MOD.settings in place.
void wl_script_refresh_settings(void) {
    lua_State *L = g_L;
    if (!L) return;
    int top = lua_gettop(L);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "wl_mod_settings") == LUA_TTABLE) {
        int reg = lua_gettop(L);
        for (int i = 0; i < g_wl.listed_count; i++) {
            const WlMod *m = &g_wl.mods[i];
            if (lua_getfield(L, reg, m->id) != LUA_TTABLE) { lua_pop(L, 1); continue; }
            int t = lua_gettop(L);
            lua_pushnil(L);                       // clear: collect keys first, then remove
            while (lua_next(L, t)) { lua_pop(L, 1); lua_pushvalue(L, -1); lua_pushnil(L); lua_settable(L, t); }
            fill_settings_table(L, t, m);
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
}

// Pushes a fresh _ENV for one mod. Real globals of the state are only read here.
static void push_mod_env(lua_State *L, const WlMod *m, int index) {
    int is_console = (index < 0);
    int is_base = !is_console && is_base_mod_id(m->id);
    int is_dev = is_base || is_console;
    uint32_t gen = ++g_mod_gen;
    lua_newtable(L);
    int env = lua_gettop(L);
    lua_pushglobaltable(L);
    int g = lua_gettop(L);
    for (int i = 0; k_base_keep[i]; i++) {
        lua_getfield(L, g, k_base_keep[i]);
        lua_setfield(L, env, k_base_keep[i]);
    }
    for (int i = 0; k_libs[i]; i++) {
        lua_getfield(L, g, k_libs[i]);
        if (lua_istable(L, -1)) push_table_copy(L, -1);
        else lua_pushnil(L);
        lua_remove(L, -2);
        lua_setfield(L, env, k_libs[i]);
    }
    lua_pop(L, 1);  // global table

    PUSH_MOD_CFUNC(L, api_log, m->id);
    lua_setfield(L, env, "print");
    lua_pushvalue(L, env);
    lua_setfield(L, env, "_G");  // _G inside the mod = the mod's own env

    // MOD = { id, name, version, dir }
    lua_createtable(L, 0, 4);
    lua_pushstring(L, m->id);      lua_setfield(L, -2, "id");
    lua_pushstring(L, m->name);    lua_setfield(L, -2, "name");
    lua_pushstring(L, m->version); lua_setfield(L, -2, "version");
    lua_pushstring(L, m->dir);     lua_setfield(L, -2, "dir");
    // MOD.settings = { <id> = value }: mod.json "settings" with the player's
    // values from the Mods screen (modsettings.c). Changing one restarts the
    // scripts through the hot reload. The table is also kept in the registry
    // (wl_mod_settings[mod id]) so a multiplayer lobby can refill it in place with
    // the host's values (wl_script_refresh_settings); scripts that saved a
    // reference to it (local S = MOD.settings) see the new values.
    {
        lua_newtable(L);
        if (!is_console) {
            fill_settings_table(L, lua_gettop(L), m);
            luaL_getsubtable(L, LUA_REGISTRYINDEX, "wl_mod_settings");
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, m->id);
            lua_pop(L, 1);
        }
        lua_setfield(L, -2, "settings");
    }
    // MOD.civs = { civ ids of this mod's civilizations } from cache\civs.txt ("cid\tbase\tname\tmod\tfile...",
    // written by the dat builder), so scripts can act only for players of their own civ.
    {
        lua_createtable(L, 4, 0);
        int n = 0;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\cache\\civs.txt", g_wl.root);
        FILE *cf = is_console ? NULL : fopen(path, "rb");
        if (cf) {
            char line[512];
            while (fgets(line, sizeof line, cf)) {
                int cid = atoi(line);
                char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
                char *t3 = t2 ? strchr(t2 + 1, '\t') : NULL, *t4 = t3 ? strchr(t3 + 1, '\t') : NULL;
                if (!t3 || !t4 || cid <= 0) continue;
                *t4 = 0;
                if (strcmp(t3 + 1, m->id) == 0) { lua_pushinteger(L, cid); lua_rawseti(L, -2, ++n); }
            }
            fclose(cf);
        }
        lua_setfield(L, -2, "civs");
    }
    lua_setfield(L, env, "MOD");

    // wl API (per mod so wl.log / wl.on know who is calling)
    lua_createtable(L, 0, 16);
    PUSH_MOD_CFUNC(L, api_after, m->id);   lua_setfield(L, -2, "after");
    PUSH_MOD_CFUNC(L, api_every, m->id);   lua_setfield(L, -2, "every");
    PUSH_MOD_CFUNC(L, api_ask, m->id);     lua_setfield(L, -2, "ask");
    lua_pushcfunction(L, api_cancel);      lua_setfield(L, -2, "cancel");
    lua_pushcfunction(L, api_wait);        lua_setfield(L, -2, "wait");
    PUSH_MOD_CFUNC(L, api_log, m->id);     lua_setfield(L, -2, "log");
    PUSH_MOD_CFUNC(L, api_on, m->id);      lua_setfield(L, -2, "on");
    lua_pushcfunction(L, api_message);     lua_setfield(L, -2, "message");
    lua_pushcfunction(L, api_dialog);      lua_setfield(L, -2, "dialog");
    lua_pushcfunction(L, api_sound);       lua_setfield(L, -2, "sound");
    PUSH_MOD_CFUNC(L, api_counter, m->id); lua_setfield(L, -2, "counter");
    lua_pushcfunction(L, api_chat);        lua_setfield(L, -2, "chat");
    lua_pushstring(L, WOLOLOADER_VERSION); lua_setfield(L, -2, "version");
    lua_pushcfunction(L, api_raw_local);    lua_setfield(L, -2, "raw_local");
    lua_pushcfunction(L, api_raw_count);    lua_setfield(L, -2, "raw_player_count");
    lua_pushcfunction(L, api_raw_resource); lua_setfield(L, -2, "raw_resource");
    PUSH_MOD_CFUNC(L, api_raw_resolve_resource, m->id); lua_setfield(L, -2, "raw_resolve_resource");
    lua_pushcfunction(L, api_raw_resources); lua_setfield(L, -2, "raw_resources");
    PUSH_MOD_CFUNC(L, api_raw_gather_info, m->id); lua_setfield(L, -2, "raw_gather_info");
    lua_pushcfunction(L, api_raw_is_enemy); lua_setfield(L, -2, "raw_is_enemy");
    lua_pushcfunction(L, api_raw_units);    lua_setfield(L, -2, "raw_units");
    lua_pushboolean(L, is_dev);
    lua_pushcclosure(L, api_raw_cmd, 1);    lua_setfield(L, -2, "raw_cmd");
    lua_pushcfunction(L, api_raw_selected); lua_setfield(L, -2, "raw_selected");
    lua_pushcfunction(L, api_raw_researched); lua_setfield(L, -2, "raw_researched");
    lua_pushcfunction(L, api_raw_time);     lua_setfield(L, -2, "raw_time");
    lua_pushcfunction(L, api_raw_mode);     lua_setfield(L, -2, "raw_mode");
    lua_pushcfunction(L, api_raw_mode_enemy); lua_setfield(L, -2, "raw_mode_enemy");
    lua_pushcfunction(L, api_raw_map_size); lua_setfield(L, -2, "raw_map_size");
    lua_pushcfunction(L, api_raw_map_capture); lua_setfield(L, -2, "raw_map_capture");
    lua_pushcfunction(L, api_raw_unit_control_disabled); lua_setfield(L, -2, "raw_unit_control_disabled");
    lua_pushcfunction(L, api_raw_last_map_click); lua_setfield(L, -2, "raw_last_map_click");
    lua_pushcfunction(L, api_raw_restart); lua_setfield(L, -2, "raw_restart");
    lua_pushcfunction(L, api_raw_session_get); lua_setfield(L, -2, "raw_session_get");
    lua_pushcfunction(L, api_raw_session_set); lua_setfield(L, -2, "raw_session_set");
    lua_pushcfunction(L, api_raw_session_clear); lua_setfield(L, -2, "raw_session_clear");
    lua_pushcfunction(L, api_sound_file); lua_setfield(L, -2, "sound_file");
    lua_pushcfunction(L, api_raw_is_multiplayer); lua_setfield(L, -2, "raw_is_multiplayer");
    lua_pushcfunction(L, api_raw_cheats_enabled); lua_setfield(L, -2, "raw_cheats_enabled");
    lua_pushstring(L, m->id);
    lua_pushinteger(L, (lua_Integer)gen);
    lua_pushcclosure(L, api_raw_ui_create, 2);  lua_setfield(L, -2, "raw_ui_create");
    lua_pushinteger(L, (lua_Integer)gen);
    lua_pushcclosure(L, api_raw_ui_set, 1);     lua_setfield(L, -2, "raw_ui_set");
    lua_pushinteger(L, (lua_Integer)gen);
    lua_pushcclosure(L, api_raw_ui_destroy, 1); lua_setfield(L, -2, "raw_ui_destroy");
    lua_pushinteger(L, (lua_Integer)gen);
    lua_pushcclosure(L, api_raw_ui_text, 1);    lua_setfield(L, -2, "raw_ui_text");
    PUSH_MOD_CFUNC(L, api_raw_set_cond, m->id); lua_setfield(L, -2, "raw_set_cond");
    PUSH_MOD_CFUNC(L, api_raw_add_command, m->id); lua_setfield(L, -2, "raw_add_command");
    PUSH_MOD_CFUNC(L, api_raw_remove_command, m->id); lua_setfield(L, -2, "raw_remove_command");
    lua_pushcfunction(L, api_raw_unit_by_id); lua_setfield(L, -2, "raw_unit_by_id");
    lua_pushcfunction(L, api_raw_player_civ); lua_setfield(L, -2, "raw_player_civ");
    lua_pushcfunction(L, api_reload);       lua_setfield(L, -2, "reload");
    lua_pushcfunction(L, api_reload_mods);  lua_setfield(L, -2, "reload_mods");
    if (is_base) {
        lua_pushcfunction(L, api_feature);  lua_setfield(L, -2, "feature");
        lua_pushcfunction(L, api_dev_eval); lua_setfield(L, -2, "dev_eval");
    }
    lua_setfield(L, env, "wl");

    push_require(L, env, m);
}


// ---- read-only game state (gameapi.c through WlGameApi; NULL in the native test) ----
static const WlGameApi *g_game;
void wl_script_set_game(const WlGameApi *api) { g_game = api; }

static int api_raw_local(lua_State *L) {
    int id = g_game ? g_game->local_player() : -1;
    if (id < 0) lua_pushnil(L); else lua_pushinteger(L, id);
    return 1;
}
static int api_raw_count(lua_State *L) { lua_pushinteger(L, g_game ? g_game->player_count() : 0); return 1; }
static int api_raw_resource(lua_State *L) {
    float v;
    if (g_game && g_game->resource((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), &v))
        lua_pushnumber(L, v);
    else lua_pushnil(L);
    return 1;
}
static int api_raw_resolve_resource(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    const char *key = luaL_checkstring(L, 1);
    int id = (g_game && g_game->resource_resolve) ? g_game->resource_resolve(key, mod) : -1;
    if (id >= 0) lua_pushinteger(L, id);
    else lua_pushnil(L);
    return 1;
}
static int api_raw_resources(lua_State *L) {
    int n = 0;
    const WlResourceDef *list = (g_game && g_game->resources_list) ? g_game->resources_list(&n) : NULL;
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, list[i].id);          lua_setfield(L, -2, "id");
        lua_pushstring(L, list[i].mod);          lua_setfield(L, -2, "mod");
        lua_pushstring(L, list[i].key);          lua_setfield(L, -2, "key");
        lua_pushstring(L, list[i].name);         lua_setfield(L, -2, "name");
        lua_pushstring(L, list[i].icon_rel);     lua_setfield(L, -2, "icon");
        lua_pushnumber(L, list[i].start);        lua_setfield(L, -2, "start");
        lua_pushnumber(L, list[i].max);          lua_setfield(L, -2, "max");
        lua_pushboolean(L, list[i].show != 0);   lua_setfield(L, -2, "show");
        lua_pushstring(L, list[i].description);  lua_setfield(L, -2, "description");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}
static int api_raw_gather_info(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    const char *key = luaL_checkstring(L, 1);
    const WlGatherDef *g = (g_game && g_game->gather_by_key) ? g_game->gather_by_key(key, mod) : NULL;
    if (!g) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, g->resource_id); lua_setfield(L, -2, "resource");
    lua_pushinteger(L, g->deposit_id);  lua_setfield(L, -2, "deposit");
    lua_pushinteger(L, g->male_id);     lua_setfield(L, -2, "male");
    lua_pushinteger(L, g->female_id);   lua_setfield(L, -2, "female");
    return 1;
}
static int api_raw_is_enemy(lua_State *L) {
    lua_pushboolean(L, g_game && g_game->is_enemy((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2)));
    return 1;
}
static void push_units(lua_State *L, const WlUnitInfo *buf, int n) {
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, buf[i].id);    lua_setfield(L, -2, "id");
        lua_pushnumber(L, buf[i].x);      lua_setfield(L, -2, "x");
        lua_pushnumber(L, buf[i].y);      lua_setfield(L, -2, "y");
        lua_pushinteger(L, buf[i].cls);   lua_setfield(L, -2, "class");
        lua_pushboolean(L, buf[i].building); lua_setfield(L, -2, "building");
        lua_pushinteger(L, buf[i].type);  lua_setfield(L, -2, "type");
        lua_pushinteger(L, buf[i].owner); lua_setfield(L, -2, "owner");
        lua_pushboolean(L, buf[i].state == 2); lua_setfield(L, -2, "ready");
        lua_rawseti(L, -2, i + 1);
    }
}
static WlUnitInfo g_ubuf[2048];
// wl.raw_units(pid [, type]): the player's units (first 2048), or every unit of one type.
static int api_raw_units(lua_State *L) {
    int pid = (int)luaL_checkinteger(L, 1), n = 0;
    if (!lua_isnoneornil(L, 2)) n = (g_game && g_game->units_of_type) ? g_game->units_of_type(pid, (int)luaL_checkinteger(L, 2), g_ubuf, 2048) : 0;
    else n = g_game ? g_game->units(pid, g_ubuf, 2048) : 0;
    push_units(L, g_ubuf, n);
    return 1;
}
// wl.raw_researched(pid) -> array of researched tech ids
static int api_raw_researched(lua_State *L) {
    static short ids[4096];
    int n = (g_game && g_game->researched) ? g_game->researched((int)luaL_checkinteger(L, 1), ids, 4096) : 0;
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) { lua_pushinteger(L, ids[i]); lua_rawseti(L, -2, i + 1); }
    return 1;
}
static int api_raw_selected(lua_State *L) {
    int n = (g_game && g_game->selected) ? g_game->selected(g_ubuf, 60) : 0;
    push_units(L, g_ubuf, n);
    return 1;
}

// Per-refresh cache for building command condition evaluation (avoids scanning all
// player units once per conditioned button on every panel refresh).
typedef struct { char name[64]; int result; } WlCondCacheEntry;
static int g_cond_refresh_active;
static int g_cond_refresh_unit_id;
static int g_cond_refresh_unit_loaded; // 0 = not fetched, 1 = found, -1 = not found
static WlUnitInfo g_cond_refresh_unit;
static WlCondCacheEntry g_cond_cache[32];
static int g_cond_cache_cnt;

void wl_script_cond_begin_refresh(int unit_id) {
    g_cond_refresh_active = 1;
    g_cond_refresh_unit_id = unit_id;
    g_cond_refresh_unit_loaded = 0;
    g_cond_cache_cnt = 0;
}

void wl_script_cond_end_refresh(void) {
    g_cond_refresh_active = 0;
    g_cond_refresh_unit_loaded = 0;
    g_cond_cache_cnt = 0;
}

static int lookup_unit_by_id(int id, WlUnitInfo *out) {
    if (g_cond_refresh_active && id == g_cond_refresh_unit_id && g_cond_refresh_unit_loaded != 0) {
        if (g_cond_refresh_unit_loaded == 1) { *out = g_cond_refresh_unit; return 1; }
        return 0;
    }
    int found = 0;
    if (g_game) {
        // Check selected units first (O(1) for the selected building whose panel is open).
        if (g_game->selected) {
            int n = g_game->selected(g_ubuf, 60);
            for (int i = 0; i < n; i++) {
                if (g_ubuf[i].id == id) { *out = g_ubuf[i]; found = 1; break; }
            }
        }
        if (!found && g_game->units && g_game->local_player) {
            int pid = g_game->local_player();
            if (pid >= 0) {
                int n = g_game->units(pid, g_ubuf, 2048);
                for (int i = 0; i < n; i++) {
                    if (g_ubuf[i].id == id) { *out = g_ubuf[i]; found = 1; break; }
                }
            }
        }
    }
    if (g_cond_refresh_active && id == g_cond_refresh_unit_id) {
        if (found) g_cond_refresh_unit = *out;
        g_cond_refresh_unit_loaded = found ? 1 : -1;
    }
    return found;
}

// wl.raw_player_civ(pid) -> civ id (1..31 game civs, 35+ mod civs) or nil
static int api_raw_player_civ(lua_State *L) {
    int c = (g_game && g_game->player_civ) ? g_game->player_civ((int)luaL_checkinteger(L, 1)) : -1;
    if (c < 0) lua_pushnil(L); else lua_pushinteger(L, c);
    return 1;
}

static int api_raw_unit_by_id(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    WlUnitInfo u;
    if (!lookup_unit_by_id(id, &u)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, u.id);           lua_setfield(L, -2, "id");
    lua_pushnumber(L, u.x);             lua_setfield(L, -2, "x");
    lua_pushnumber(L, u.y);             lua_setfield(L, -2, "y");
    lua_pushinteger(L, u.cls);          lua_setfield(L, -2, "class");
    lua_pushboolean(L, u.building);     lua_setfield(L, -2, "building");
    lua_pushinteger(L, u.type);         lua_setfield(L, -2, "type");
    lua_pushinteger(L, u.owner);        lua_setfield(L, -2, "owner");
    lua_pushboolean(L, u.state == 2);   lua_setfield(L, -2, "ready");
    return 1;
}

static int api_raw_set_cond(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_CONDS_KEY);
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "fn");
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setfield(L, -2, "mod");
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
    return 0;
}

static int api_raw_add_command(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "building");
    if (!lua_isinteger(L, -1)) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: building must be an integer unit id"); }
    int building = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (building < 0) return luaL_error(L, "Game.AddCommand: building must be >= 0");

    lua_getfield(L, 1, "button");
    if (!lua_isinteger(L, -1)) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: button must be an integer (1..15)"); }
    int button = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (button < 1 || button > 15) return luaL_error(L, "Game.AddCommand: button must be 1..15");

    char name[64];
    lua_getfield(L, 1, "name");
    const char *ns = (lua_type(L, -1) == LUA_TSTRING) ? lua_tostring(L, -1) : NULL;
    if (!ns || !ns[0]) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: name must be a non-empty string"); }
    snprintf(name, sizeof name, "%s", ns);
    lua_pop(L, 1);

    int icon = 32;
    lua_getfield(L, 1, "icon");
    if (!lua_isnil(L, -1)) {
        if (lua_isinteger(L, -1)) {
            icon = (int)lua_tointeger(L, -1);
            if (icon < 0) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: icon must be >= 0"); }
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            const char *iname = lua_tostring(L, -1);
            int resolved = resolve_cmd_icon(mod, iname);
            if (resolved >= 0) {
                icon = resolved;
            } else {
                warn_unknown_cmd_icon_once(mod, iname);
                icon = 32;
            }
        } else {
            lua_pop(L, 1);
            return luaL_error(L, "Game.AddCommand: icon must be an integer or icon name");
        }
    }
    lua_pop(L, 1);

    char text[256];
    lua_getfield(L, 1, "text");
    if (lua_isnil(L, -1)) {
        snprintf(text, sizeof text, "%s", name);
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        snprintf(text, sizeof text, "%s", lua_tostring(L, -1));
    } else {
        lua_pop(L, 1);
        return luaL_error(L, "Game.AddCommand: text must be a string");
    }
    lua_pop(L, 1);

    char help[1024];
    lua_getfield(L, 1, "help");
    if (lua_isnil(L, -1)) {
        help[0] = '\0';
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        snprintf(help, sizeof help, "%s", lua_tostring(L, -1));
    } else {
        lua_pop(L, 1);
        return luaL_error(L, "Game.AddCommand: help must be a string");
    }
    lua_pop(L, 1);

    int civ = -1;
    lua_getfield(L, 1, "civ");
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: civ must be an integer (-1 for any)"); }
        civ = (int)lua_tointeger(L, -1);
        if (civ < -1) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: civ must be -1 or >= 0"); }
    }
    lua_pop(L, 1);

    int page = 1;
    lua_getfield(L, 1, "page");
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: page must be an integer >= 1"); }
        page = (int)lua_tointeger(L, -1);
        if (page < 1) { lua_pop(L, 1); return luaL_error(L, "Game.AddCommand: page must be >= 1"); }
    }
    lua_pop(L, 1);

    char cond_name[64];
    lua_getfield(L, 1, "condition");
    if (lua_isnil(L, -1)) {
        snprintf(cond_name, sizeof cond_name, "%s", name);
    } else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
        cond_name[0] = '\0';
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        snprintf(cond_name, sizeof cond_name, "%s", lua_tostring(L, -1));
    } else {
        lua_pop(L, 1);
        return luaL_error(L, "Game.AddCommand: condition must be a string or false");
    }
    lua_pop(L, 1);

    char cond_action[16];
    lua_getfield(L, 1, "condition_action");
    if (lua_isnil(L, -1)) {
        snprintf(cond_action, sizeof cond_action, "disable");
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        const char *ca = lua_tostring(L, -1);
        if (strcmp(ca, "disable") != 0 && strcmp(ca, "hide") != 0) {
            lua_pop(L, 1);
            return luaL_error(L, "Game.AddCommand: condition_action must be 'disable' or 'hide'");
        }
        snprintf(cond_action, sizeof cond_action, "%s", ca);
    } else {
        lua_pop(L, 1);
        return luaL_error(L, "Game.AddCommand: condition_action must be 'disable' or 'hide'");
    }
    lua_pop(L, 1);

    int slot = -1;
    for (int i = 0; i < WL_MAX_RT_CMDS; i++) {
        if (g_rt_cmds[i].used && strcmp(g_rt_cmds[i].mod, mod) == 0 && strcmp(g_rt_cmds[i].name, name) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < WL_MAX_RT_CMDS; i++) {
            if (!g_rt_cmds[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) return luaL_error(L, "Game.AddCommand: too many runtime commands (%d at most)", WL_MAX_RT_CMDS);

    WlRuntimeCmd *rc = &g_rt_cmds[slot];
    rc->used = 1;
    rc->civ = civ;
    rc->building = building;
    rc->page = page;
    rc->button = button;
    rc->icon = icon;
    rc->help_sid = WL_RT_CMD_SID_BASE + slot;
    rc->gen = ++g_rt_cmd_gen;
    lua_getfield(L, 1, "shared");
    rc->shared = lua_toboolean(L, -1);
    lua_pop(L, 1);
    snprintf(rc->name, sizeof rc->name, "%s", name);
    snprintf(rc->cond_action, sizeof rc->cond_action, "%s", cond_action);
    snprintf(rc->cond_name, sizeof rc->cond_name, "%s", cond_name);
    snprintf(rc->mod, sizeof rc->mod, "%s", mod);

    if (g_rt_cmd_str_fn) g_rt_cmd_str_fn(slot, text, help);
    if (g_in_game && g_buildpage_refresh_fn) g_buildpage_refresh_fn();

    lua_pushboolean(L, 1);
    return 1;
}

static int api_raw_remove_command(lua_State *L) {
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    const char *name = luaL_checkstring(L, 1);
    int removed = 0;
    for (int i = 0; i < WL_MAX_RT_CMDS; i++) {
        if (g_rt_cmds[i].used && strcmp(g_rt_cmds[i].mod, mod) == 0 && strcmp(g_rt_cmds[i].name, name) == 0) {
            g_rt_cmds[i].used = 0;
            if (g_rt_cmd_str_fn) g_rt_cmd_str_fn(i, NULL, NULL);
            removed = 1;
        }
    }
    if (removed && g_in_game && g_buildpage_refresh_fn) g_buildpage_refresh_fn();
    lua_pushboolean(L, removed);
    return 1;
}
static int api_raw_time(lua_State *L) {
    double t = (g_game && g_game->game_time) ? g_game->game_time() : -1.0;
    if (t < 0) lua_pushnil(L); else lua_pushnumber(L, t);
    return 1;
}
static int api_raw_mode(lua_State *L) {
    const char *m = (g_game && g_game->game_mode) ? g_game->game_mode() : NULL;
    if (m && m[0]) lua_pushstring(L, m); else lua_pushnil(L);
    return 1;
}
static int api_raw_map_size(lua_State *L) {
    int n = wl_gamemode_map_tiles();
    if (n > 0) lua_pushinteger(L, n); else lua_pushnil(L);
    return 1;
}

// Session key-value store: survives Game.Restart(), cleared on a new lobby match (hook_setup_start).
#define WL_SESSION_MAX 32
static struct { char key[64]; char val[128]; int is_num; double num; int used; } g_session[WL_SESSION_MAX];
void wl_script_clear_session(void) { memset(g_session, 0, sizeof g_session); }

static int api_raw_session_get(lua_State *L) {
    const char *k = luaL_checkstring(L, 1);
    for (int i = 0; i < WL_SESSION_MAX; i++) {
        if (g_session[i].used && strcmp(g_session[i].key, k) == 0) {
            if (g_session[i].is_num) lua_pushnumber(L, g_session[i].num);
            else lua_pushstring(L, g_session[i].val);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}
static int api_raw_session_set(lua_State *L) {
    const char *k = luaL_checkstring(L, 1);
    int slot = -1, free_slot = -1;
    for (int i = 0; i < WL_SESSION_MAX; i++) {
        if (g_session[i].used && strcmp(g_session[i].key, k) == 0) { slot = i; break; }
        if (!g_session[i].used && free_slot < 0) free_slot = i;
    }
    if (lua_isnoneornil(L, 2)) {
        if (slot >= 0) g_session[slot].used = 0;
        return 0;
    }
    if (slot < 0) slot = free_slot;
    if (slot < 0) return luaL_error(L, "session store full (max %d keys)", WL_SESSION_MAX);
    g_session[slot].used = 1;
    snprintf(g_session[slot].key, sizeof g_session[slot].key, "%s", k);
    if (lua_type(L, 2) == LUA_TNUMBER) {
        g_session[slot].is_num = 1;
        g_session[slot].num = (double)lua_tonumber(L, 2);
        g_session[slot].val[0] = 0;
    } else {
        g_session[slot].is_num = 0;
        snprintf(g_session[slot].val, sizeof g_session[slot].val, "%s", luaL_tolstring(L, 2, NULL));
        lua_pop(L, 1);
    }
    return 0;
}
static int api_raw_session_clear(lua_State *L) { (void)L; wl_script_clear_session(); return 0; }
static float g_last_mc_x, g_last_mc_y; static int g_last_mc_btn;
static int api_raw_map_capture(lua_State *L) { wl_ui_set_map_capture(lua_toboolean(L, 1)); return 0; }
static int api_raw_unit_control_disabled(lua_State *L) { wl_ui_set_unit_control_disabled(lua_toboolean(L, 1)); return 0; }
static int api_raw_last_map_click(lua_State *L) { lua_pushnumber(L, g_last_mc_x); lua_pushnumber(L, g_last_mc_y); lua_pushinteger(L, g_last_mc_btn); return 3; }
static int api_raw_restart(lua_State *L) { lua_pushboolean(L, wl_ui_request_restart()); return 1; }
static int api_sound_file(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, wl_play_sfx_file(path));
    return 1;
}
static int api_raw_mode_enemy(lua_State *L) {
    if (lua_isinteger(L, 1)) wl_gamemode_set_enemy_player((int)lua_tointeger(L, 1));
    lua_pushinteger(L, wl_gamemode_enemy_player());
    return 1;
}
static int api_raw_is_multiplayer(lua_State *L) {
    lua_pushboolean(L, is_multiplayer());
    return 1;
}
static int api_raw_cheats_enabled(lua_State *L) {
    lua_pushboolean(L, cheats_enabled());
    return 1;
}
static int api_feature(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_pushboolean(L, g_feature_fn ? g_feature_fn(name) : 1);
    return 1;
}

// wl.raw_cmd(name, ...) -> true (sent from this machine) / false (another machine sends it);
// raises an error for invalid arguments or when no game is running. See gamecmd.c.
static int api_raw_cmd(lua_State *L) {
    int is_dev = lua_toboolean(L, lua_upvalueindex(1));
    if (is_dev && !dev_allowed())
        return luaL_error(L, "DevPanel actions are disabled in multiplayer unless cheats are enabled");
    const char *what = luaL_checkstring(L, 1);
    int rc = -1;
    if (!g_game) return luaL_error(L, "game commands are not available here");
    if (is_dev && g_set_dev_ctx) g_set_dev_ctx(1);
    if (strcmp(what, "resource") == 0 && g_game->add_resource)
        rc = g_game->add_resource((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (float)luaL_checknumber(L, 4));
    else if (strcmp(what, "set_resource") == 0 && g_game->set_resource)
        rc = g_game->set_resource((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (float)luaL_checknumber(L, 4));
    else if (strcmp(what, "research") == 0 && g_game->research)
        rc = g_game->research((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    else if (strcmp(what, "spawn") == 0 && g_game->spawn)
        rc = g_game->spawn((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                           (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    else if (strcmp(what, "age") == 0 && g_game->set_age)
        rc = g_game->set_age((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    else if (strcmp(what, "delete") == 0 && g_game->delete_unit)
        rc = g_game->delete_unit((int)luaL_checkinteger(L, 2));
    else if (strcmp(what, "spawn_neutral") == 0 && g_game->spawn_neutral)
        rc = g_game->spawn_neutral((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                                   (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    else if (strcmp(what, "attack") == 0 && g_game->attack_unit)
        rc = g_game->attack_unit((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    else if (strcmp(what, "move") == 0 && g_game->move_unit)
        rc = g_game->move_unit((int)luaL_checkinteger(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
    else if (strcmp(what, "win") == 0 && g_game->win_player)
        rc = g_game->win_player((int)luaL_checkinteger(L, 2), lua_toboolean(L, 3));
    else if (strcmp(what, "defeat") == 0 && g_game->defeat_player)
        rc = g_game->defeat_player((int)luaL_checkinteger(L, 2));
    else {
        if (is_dev && g_set_dev_ctx) g_set_dev_ctx(0);
        return luaL_error(L, "unknown command %s", what);
    }
    if (is_dev && g_set_dev_ctx) g_set_dev_ctx(0);
    if (rc < 0) return luaL_error(L, "%s: invalid arguments or no game running", what);
    lua_pushboolean(L, rc > 0);
    return 1;
}

// On-screen UI backend (uikit.c). Without it (native test) elements still exist in Lua.
// Tracked per mod and per sandbox generation so reloading a mod destroys all its elements
// (and stale closures from an older generation cannot touch reused slot IDs).
static const WlUiApi *g_ui;
void wl_script_set_uikit(const WlUiApi *api) { g_ui = api; }
static int g_fake_ui_id;
typedef struct { int id; int parent; uint32_t gen; char mod[64]; } WlUiOwned;
static WlUiOwned *g_ui_owned;
static int g_ui_owned_count, g_ui_owned_cap;

static WlUiOwned *ui_owned_find(int id) {
    for (int i = 0; i < g_ui_owned_count; i++)
        if (g_ui_owned[i].id == id) return &g_ui_owned[i];
    return NULL;
}
static void ui_owned_remove_at(int idx) {
    if (idx < 0 || idx >= g_ui_owned_count) return;
    if (idx + 1 < g_ui_owned_count)
        memmove(&g_ui_owned[idx], &g_ui_owned[idx + 1], (size_t)(g_ui_owned_count - idx - 1) * sizeof g_ui_owned[0]);
    g_ui_owned_count--;
}
static void ui_owned_remove_tree(int id) {
    for (int i = 0; i < g_ui_owned_count; ) {
        if (g_ui_owned[i].parent == id) {
            ui_owned_remove_tree(g_ui_owned[i].id);
            i = 0;
        } else {
            i++;
        }
    }
    for (int i = 0; i < g_ui_owned_count; i++) {
        if (g_ui_owned[i].id == id) { ui_owned_remove_at(i); break; }
    }
}
static void ui_owned_add(int id, uint32_t gen, const char *mod) {
    ui_owned_remove_tree(id);
    if (g_ui_owned_count >= g_ui_owned_cap) {
        int ncap = g_ui_owned_cap ? g_ui_owned_cap * 2 : 64;
        WlUiOwned *nb = (WlUiOwned *)realloc(g_ui_owned, (size_t)ncap * sizeof nb[0]);
        if (!nb) return;
        g_ui_owned = nb;
        g_ui_owned_cap = ncap;
    }
    WlUiOwned *e = &g_ui_owned[g_ui_owned_count++];
    e->id = id;
    e->parent = 0;
    e->gen = gen;
    snprintf(e->mod, sizeof e->mod, "%s", mod);
}
static void unload_mod_ui(const char *mod) {
    for (;;) {
        int pick = -1;
        for (int i = 0; i < g_ui_owned_count; i++) {
            if (strcmp(g_ui_owned[i].mod, mod) == 0) { pick = g_ui_owned[i].id; break; }
        }
        if (pick < 0) break;
        for (int guard = 0; guard < 256; guard++) {
            WlUiOwned *e = ui_owned_find(pick);
            if (!e || e->parent <= 0) break;
            WlUiOwned *p = ui_owned_find(e->parent);
            if (!p || strcmp(p->mod, mod) != 0) break;
            pick = p->id;
        }
        ui_owned_remove_tree(pick);
        if (g_ui) g_ui->destroy(pick);
    }
}

static int api_raw_ui_create(lua_State *L) {
    int type = (int)luaL_checkinteger(L, 1);
    const char *mod = lua_tostring(L, lua_upvalueindex(1));
    uint32_t gen = (uint32_t)lua_tointeger(L, lua_upvalueindex(2));
    int id = g_ui ? g_ui->create(type) : ++g_fake_ui_id;
    if (id < 0) { lua_pushnil(L); return 1; }
    ui_owned_add(id, gen, mod);
    lua_pushinteger(L, id);
    return 1;
}
// UI images must be files inside an enabled mod's folder (no "..", no other absolute path).
static int image_path_ok(const char *p) {
    if (strstr(p, "..")) return 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        size_t n = strlen(m->dir);
        if (m->enabled && n && !_strnicmp(p, m->dir, n) && (p[n] == '\\' || p[n] == '/')) return 1;
    }
    return 0;
}
static int api_raw_ui_set(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    const char *key = luaL_checkstring(L, 2);
    uint32_t gen = (uint32_t)lua_tointeger(L, lua_upvalueindex(1));
    int v = 0;
    const char *str = NULL;
    if (lua_type(L, 3) == LUA_TSTRING) str = lua_tostring(L, 3);
    else if (lua_isboolean(L, 3)) v = lua_toboolean(L, 3);
    else if (!lua_isnoneornil(L, 3)) v = (int)luaL_checknumber(L, 3);
    if (str && !strcmp(key, "Image") && !image_path_ok(str))
        return luaL_error(L, "Image must be a file inside the mod folder: %s", str);
    WlUiOwned *e = ui_owned_find(id);
    if (!e || e->gen != gen) { lua_pushboolean(L, 0); return 1; }
    int ok = g_ui ? g_ui->set(id, key, v, str) : 1;
    if (ok && strcmp(key, "Parent") == 0) {
        e = ui_owned_find(id);
        if (e) e->parent = (v > 0) ? v : 0;
    }
    lua_pushboolean(L, ok);
    return 1;
}
static int api_raw_ui_text(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    uint32_t gen = (uint32_t)lua_tointeger(L, lua_upvalueindex(1));
    if (id != -1) {
        WlUiOwned *e = ui_owned_find(id);
        if (!e || e->gen != gen) { lua_pushnil(L); return 1; }
    }
    const char *t = (g_ui && g_ui->get_text) ? g_ui->get_text(id) : NULL;
    if (t) lua_pushstring(L, t); else lua_pushnil(L);
    return 1;
}
static int api_raw_ui_destroy(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    uint32_t gen = (uint32_t)lua_tointeger(L, lua_upvalueindex(1));
    WlUiOwned *e = ui_owned_find(id);
    if (!e || e->gen != gen) return 0;
    ui_owned_remove_tree(id);
    if (g_ui) g_ui->destroy(id);
    return 0;
}

// ---- Scripting API (Game, UI, Sound, Time), written in Lua on top of wl.*; run in each mod env first ----
static const char k_prelude[] =
"local wl = wl\n"
"-- Connections: Game.OnStart(fn) etc. return { Connected, Disconnect() }\n"
"local function on(event, fn)\n"
"  local c = { Connected = true }\n"
"  function c:Disconnect() c.Connected = false end\n"
"  wl.on(event, function(...) if c.Connected then fn(...) end end)\n"
"  return c\n"
"end\n"
"local AGES = { [0] = 'Dark', 'Feudal', 'Castle', 'Imperial' }\n"
"local RES = { Food = 0, Wood = 1, Stone = 2, Gold = 3 }\n"
"local RES_LO = { food = 0, wood = 1, stone = 2, gold = 3 }\n"
"local function resolve_res(name)\n"
"  if math.type(name) == 'integer' and name >= 0 then return name end\n"
"  if type(name) == 'string' then\n"
"    local r = RES[name] or RES_LO[name:lower()]\n"
"    if r ~= nil then return r end\n"
"    r = wl.raw_resolve_resource(name)\n"
"    if r ~= nil then return r end\n"
"  end\n"
"  error('unknown resource: ' .. tostring(name), 3)\n"
"end\n"
"local CLASS = { Archer = 0, Trade = 2, Building = 3, Villager = 4, Infantry = 6, Cavalry = 12,\n"
"  Siege = 13, Monk = 18, Transport = 20, FishingShip = 21, Warship = 22, Wall = 27,\n"
"  CavalryArcher = 36, MonkWithRelic = 43 }\n"
"local CLASS_NAME = {}\n"
"for k, v in pairs(CLASS) do CLASS_NAME[v] = k end\n"
"local Player = {}\n"
"Player.__index = function(p, k)\n"
"  local r = RES[k]\n"
"  if r then return wl.raw_resource(p.Id, r) end\n"
"  if k == 'Age' then local a = wl.raw_resource(p.Id, 6); return a and math.floor(a) end\n"
"  if k == 'AgeName' then local a = wl.raw_resource(p.Id, 6); return a and AGES[math.floor(a)] end\n"
"  if k == 'Civ' then return wl.raw_player_civ(p.Id) end\n"
"  if k == 'Population' then local v = wl.raw_resource(p.Id, 11); return v and math.floor(v) end\n"
"  if k == 'IsLocal' then return p.Id == wl.raw_local() end\n"
"  return Player[k]\n"
"end\n"
"Player.__tostring = function(p) return 'Player ' .. p.Id end\n"
"function Player:IsEnemy(other) return wl.raw_is_enemy(self.Id, type(other) == 'table' and other.Id or other) end\n"
"-- p:Units() all, p:Units('Monk') by class name, p:Units(function(u) ... end) custom filter\n"
"-- Game commands (sent as game commands, so every player sees the same result; true when this\n"
"-- machine sent it, false when another player's machine does):\n"
"function Player:GetResource(name) return wl.raw_resource(self.Id, resolve_res(name)) end\n"
"function Player:AddResource(name, amount) return wl.raw_cmd('resource', self.Id, resolve_res(name), amount) end\n"
"function Player:SetResource(name, amount) return wl.raw_cmd('set_resource', self.Id, resolve_res(name), amount) end\n"
"function Player:AddFood(n) return wl.raw_cmd('resource', self.Id, 0, n) end\n"
"function Player:AddWood(n) return wl.raw_cmd('resource', self.Id, 1, n) end\n"
"function Player:AddStone(n) return wl.raw_cmd('resource', self.Id, 2, n) end\n"
"function Player:AddGold(n) return wl.raw_cmd('resource', self.Id, 3, n) end\n"
"function Player:Research(techId) return wl.raw_cmd('research', self.Id, techId) end\n"
"-- p:CreateUnit(unitId, x, y [, { Capturable = false }]): Capturable = false keeps a Gaia unit Gaia's\n"
"-- when players see it (the game gives seen Gaia units to the first player; gamecmd.c)\n"
"function Player:CreateUnit(unitId, x, y, opts)\n"
"  if type(opts) == 'table' and opts.Capturable == false then return wl.raw_cmd('spawn_neutral', self.Id, unitId, x, y) end\n"
"  return wl.raw_cmd('spawn', self.Id, unitId, x, y) end\n"
"-- p:SetAge(2) or p:SetAge('Castle'): up = age techs researched; down = age value lowered and the\n"
"-- age-up techs researchable again (what earlier techs unlocked stays)\n"
"function Player:SetAge(age)\n"
"  if type(age) == 'string' then\n"
"    for k, v in pairs(AGES) do if v == age then age = k end end\n"
"  end\n"
"  if math.type(age) ~= 'integer' or age < 0 or age > 3 then error('SetAge: 0..3 or Dark/Feudal/Castle/Imperial', 2) end\n"
"  return wl.raw_cmd('age', self.Id, age)\n"
"end\n"
"function Player:Win(team) return wl.raw_cmd('win', self.Id, team and true or false) end\n"
"function Player:Defeat() return wl.raw_cmd('defeat', self.Id) end\n"
"local Unit = {}\n"
"Unit.__index = Unit\n"
"Unit.__tostring = function(u) return 'Unit ' .. u.Id end\n"
"function Unit:Delete() return wl.raw_cmd('delete', self.Id) end\n"
"-- unit:MoveTo(x, y): walk to a map tile (the game's move command, sent like the other commands)\n"
"function Unit:MoveTo(x, y) return wl.raw_cmd('move', self.Id, x, y) end\n"
"-- unit:Attack(target): attack a unit or building (a unit table or an id; the game's order command)\n"
"function Unit:Attack(t) return wl.raw_cmd('attack', self.Id, type(t) == 'table' and t.Id or t) end\n"
"-- p:UnitsOfType(typeId): every unit of one type (also Gaia's: Game.Player(0))\n"
"function Player:UnitsOfType(t)\n"
"  local out = {}\n"
"  for _, u in ipairs(wl.raw_units(self.Id, t)) do\n"
"    out[#out + 1] = setmetatable({ Id = u.id, X = u.x, Y = u.y, Class = u.class, ClassName = CLASS_NAME[u.class],\n"
"      IsBuilding = u.building, Built = u.building and u.ready, Type = u.type, Owner = self }, Unit)\n"
"  end\n"
"  return out\n"
"end\n"
"function Player:Units(filter)\n"
"  local want = type(filter) == 'string' and CLASS[filter]\n"
"  if type(filter) == 'string' and not want then error('unknown unit class: ' .. filter, 2) end\n"
"  local out = {}\n"
"  for _, u in ipairs(wl.raw_units(self.Id)) do\n"
"    local unit = { Id = u.id, X = u.x, Y = u.y, Class = u.class, ClassName = CLASS_NAME[u.class],\n"
"      IsBuilding = u.building, Built = u.building and u.ready, Type = u.type, Owner = self }\n"
"    setmetatable(unit, Unit)\n"
"    if (want and u.class == want) or (type(filter) == 'function' and filter(unit)) or filter == nil then\n"
"      out[#out + 1] = unit\n"
"    end\n"
"  end\n"
"  return out\n"
"end\n"
"local cache = {}\n"
"local function player(id) if not id then return nil end\n"
"  cache[id] = cache[id] or setmetatable({ Id = id }, Player); return cache[id] end\n"
"Game = setmetatable({}, { __index = function(_, k)\n"
"  if k == 'LocalPlayer' then return player(wl.raw_local()) end\n"
"  if k == 'Time' then return wl.raw_time() end\n"
"end })\n"
"-- Game.Selected(): units the local player has selected (first 60)\n"
"function Game.Selected()\n"
"  local out = {}\n"
"  for _, u in ipairs(wl.raw_selected()) do\n"
"    out[#out + 1] = setmetatable({ Id = u.id, X = u.x, Y = u.y, Class = u.class, ClassName = CLASS_NAME[u.class],\n"
"      IsBuilding = u.building, Built = u.building and u.ready, Type = u.type, Owner = player(u.owner) }, Unit)\n"
"  end\n"
"  return out\n"
"end\n"
"function Game.Player(id) return player(id) end\n"
"function Game.Players() local t = {}\n"
"  for i = 1, wl.raw_player_count() - 1 do t[#t + 1] = player(i) end; return t end\n"
"function Game.Resources() return wl.raw_resources() end\n"
"function Game.GatherInfo(key)\n"
"  if type(key) ~= 'string' then return nil end\n"
"  return wl.raw_gather_info(key)\n"
"end\n"
"function Game.ResourceDeposit(key)\n"
"  local info = Game.GatherInfo(key)\n"
"  return info and info.deposit or nil\n"
"end\n"
"function Game.Mode() return wl.raw_mode() end\n"
"-- Game.MapSize(): side of the (square) map in tiles from the lobby map size, nil if unknown\n"
"function Game.MapSize() return wl.raw_map_size() end\n"
"function Game.SetMapClickCapture(on) wl.raw_map_capture(not not on) end\n"
"function Game.SetUnitControlDisabled(off) wl.raw_unit_control_disabled(not not off) end\n"
"function Game.OnMapClick(fn) return on('map_click', function() local x, y, b = wl.raw_last_map_click() fn(x, y, b == 2 and 'right' or 'left') end) end\n"
"function Game.Restart() return wl.raw_restart() end\n"
"function Game.GetSession(k) return wl.raw_session_get(tostring(k)) end\n"
"function Game.SetSession(k, v) wl.raw_session_set(tostring(k), v) end\n"
"function Game.ClearSession() wl.raw_session_clear() end\n"
"-- the game mode's enemy player (mod.json \"enemy_player\": true), nil without one\n"
"function Game.ModeEnemy() local id = wl.raw_mode_enemy() if id and id > 0 then return player(id) end end\n"
"-- after loading a savegame: tell Wololoader again which player is the mode's enemy\n"
"function Game.SetModeEnemy(p) local id = wl.raw_mode_enemy(type(p) == 'table' and p.Id or p) if id and id > 0 then return player(id) end end\n"
"function Game.OnStart(fn) return on('game_start', fn) end\n"
"-- Game.OnCommand(name, fn(building)): a Lua button of a mod building (unique_building commands)\n"
"function Game.OnCommand(name, fn)\n"
"  return on('cmd:' .. tostring(name), function(id)\n"
"    local me = Game.LocalPlayer\n"
"    local b = { Id = id, Owner = me }\n"
"    if me then for _, u in ipairs(me:Units()) do if u.Id == id then b = u end end end\n"
"    fn(setmetatable(b, getmetatable(b) or Unit))\n"
"  end)\n"
"end\n"
"function Game.CommandCondition(name, fn)\n"
"  wl.raw_set_cond(tostring(name), function(id)\n"
"    local me = Game.LocalPlayer\n"
"    local u = wl.raw_unit_by_id(id)\n"
"    local b\n"
"    if u then\n"
"      b = setmetatable({ Id = u.id, X = u.x, Y = u.y, Class = u.class, ClassName = CLASS_NAME[u.class],\n"
"        IsBuilding = u.building, Built = u.building and u.ready, Type = u.type, Owner = player(u.owner) or me }, Unit)\n"
"    else\n"
"      b = setmetatable({ Id = id, Owner = me }, Unit)\n"
"    end\n"
"    return fn(b)\n"
"  end)\n"
"end\n"
"function Game.AddCommand(spec)\n"
"  return wl.raw_add_command(spec)\n"
"end\n"
"function Game.RemoveCommand(name)\n"
"  return wl.raw_remove_command(name)\n"
"end\n"
"function Game.OnSecond(fn) return on('every_second', fn) end\n"
"-- Polled events (checked once a second from the game's frame; reading only, MP safe):\n"
"-- Game.OnAgeUp(fn(player, ageName)), Game.OnUnitCreated(fn(unit)), Game.OnUnitDied(fn(id, player)),\n"
"-- Game.OnResearch(fn(player, techId)), Game.OnBuildingDone(fn(unit))\n"
"local watchers = { age = {}, born = {}, died = {}, tech = {}, built = {} }\n"
"local lastAge, lastUnits, lastTech, lastBuilt = {}, {}, {}, {}\n"
"local function add(list, fn)\n"
"  local c = { Connected = true }\n"
"  function c:Disconnect() c.Connected = false end\n"
"  list[#list + 1] = { c = c, fn = fn }; return c\n"
"end\n"
"local function fire(list, ...) for _, w in ipairs(list) do if w.c.Connected then w.fn(...) end end end\n"
"function Game.OnAgeUp(fn) return add(watchers.age, fn) end\n"
"function Game.OnUnitCreated(fn) return add(watchers.born, fn) end\n"
"function Game.OnUnitDied(fn) return add(watchers.died, fn) end\n"
"function Game.OnResearch(fn) return add(watchers.tech, fn) end\n"
"function Game.OnBuildingDone(fn) return add(watchers.built, fn) end\n"
"wl.on('game_start', function() lastAge, lastUnits, lastTech, lastBuilt = {}, {}, {}, {} end)\n"
"wl.on('every_second', function()\n"
"  local wantUnits = #watchers.born > 0 or #watchers.died > 0 or #watchers.built > 0\n"
"  if #watchers.age == 0 and #watchers.tech == 0 and not wantUnits then return end\n"
"  for _, p in ipairs(Game.Players()) do\n"
"    local a = p.Age\n"
"    if a then\n"
"      if lastAge[p.Id] and a > lastAge[p.Id] then fire(watchers.age, p, AGES[a]) end\n"
"      lastAge[p.Id] = a\n"
"    end\n"
"    if #watchers.tech > 0 then\n"
"      local now, seen = {}, lastTech[p.Id]\n"
"      for _, id in ipairs(wl.raw_researched(p.Id)) do\n"
"        now[id] = true\n"
"        if seen and not seen[id] then fire(watchers.tech, p, id) end\n"
"      end\n"
"      lastTech[p.Id] = now\n"
"    end\n"
"    if wantUnits then\n"
"      local now, seen, built, wasBuilt = {}, lastUnits[p.Id], {}, lastBuilt[p.Id]\n"
"      for _, u in ipairs(p:Units()) do\n"
"        now[u.Id] = true\n"
"        if seen and not seen[u.Id] then fire(watchers.born, u) end\n"
"        if u.Built then\n"
"          built[u.Id] = true\n"
"          if wasBuilt and not wasBuilt[u.Id] then fire(watchers.built, u) end\n"
"        end\n"
"      end\n"
"      if seen then for id in pairs(seen) do if not now[id] then fire(watchers.died, id, p) end end end\n"
"      lastUnits[p.Id], lastBuilt[p.Id] = now, built\n"
"    end\n"
"  end\n"
"end)\n"
"function Game.IsMultiplayer() return wl.raw_is_multiplayer() end\n"
"function Game.CheatsEnabled() return wl.raw_cheats_enabled() end\n"
"local SOUNDS = { Chat = 'chat', CantDo = 'cantdo', Tribute = 'tribute', Attack = 'attack',\n"
"  TownBell = 'townbell', TownRecall = 'townrecall', Objective = 'objective', Relic = 'relic' }\n"
"Sound = { Play = function(name) if name == 'Villager' or name == 'villager' then wl.sound_file('resources/_common/drs/sounds/5353.wav') else wl.sound(SOUNDS[name] or name) end end, PlayFile = function(p) return wl.sound_file(tostring(p)) end }\n"
"UI = {}\n"
"function UI.Message(text, opts) wl.message(tostring(text), opts and opts.Color) end\n"
"function UI.Chat(text, opts) opts = opts or {}; wl.chat(tostring(text), opts.From, opts.Color) end\n"
"function UI.Dialog(text) wl.dialog(tostring(text)) end\n"
"-- Counters: local c = UI:CreateCounter(\"Monks: 0\"); c:SetText(...); c:Hide(); c:Show(); c:Destroy()\n"
"-- The game has one free countdown line, so visible counters are joined into it with \"  |  \".\n"
"local counters = {}\n"
"local function redraw()\n"
"  local parts = {}\n"
"  for _, c in ipairs(counters) do if c.Visible and c.Text ~= '' then parts[#parts + 1] = c.Text end end\n"
"  wl.counter(#parts > 0 and table.concat(parts, '  |  ') or nil)\n"
"end\n"
"local Counter = {}\n"
"Counter.__index = Counter\n"
"function Counter:SetText(text) self.Text = tostring(text or ''); redraw() end\n"
"function Counter:Show() self.Visible = true; redraw() end\n"
"function Counter:Hide() self.Visible = false; redraw() end\n"
"function Counter:Destroy()\n"
"  for i, c in ipairs(counters) do if c == self then table.remove(counters, i) break end end\n"
"  redraw()\n"
"end\n"
"function UI.CreateCounter(self, text)\n"
"  if self ~= UI then text = self end            -- UI:CreateCounter(t) and UI.CreateCounter(t)\n"
"  local c = setmetatable({ Text = tostring(text or ''), Visible = true }, Counter)\n"
"  counters[#counters + 1] = c; redraw(); return c\n"
"end\n"
"function UI.Ask(text, callback)\n"
"  if callback then return wl.ask(tostring(text), callback) end\n"
"  local done, answer; wl.ask(tostring(text), function(a) done = true; answer = a end)\n"
"  while not done do wl.wait(0.1) end; return answer\n"
"end\n"
"-- On-screen UI (uikit.c): UI:CreateFrame / CreateLabel / CreateButton{ ...properties... }\n"
"local ANCHOR = { TopLeft = 0, Top = 1, TopRight = 2, Left = 3, Center = 4, Right = 5,\n"
"  BottomLeft = 6, Bottom = 7, BottomRight = 8 }\n"
"local ALIGN = { Left = 0, Center = 1, Right = 2 }\n"
"local function rgb(c)   -- \"#RRGGBB\", {r, g, b} or 0xRRGGBB -> Windows COLORREF\n"
"  if type(c) == 'string' then\n"
"    local h = c:gsub('^#', ''); local n = #h == 6 and tonumber(h, 16)\n"
"    if not n then error('bad color: ' .. c .. ' (use \"#RRGGBB\")', 4) end; c = n\n"
"  end\n"
"  if type(c) == 'table' then\n"
"    return (c[1] or c.R or 0) + (c[2] or c.G or 0) * 256 + (c[3] or c.B or 0) * 65536\n"
"  end\n"
"  if math.type(c) ~= 'integer' then error('bad color (use \"#RRGGBB\" or {r, g, b})', 4) end\n"
"  return ((c >> 16) & 255) + ((c >> 8) & 255) * 256 + (c & 255) * 65536\n"
"end\n"
"local function xy(v, a, b)\n"
"  if type(v) ~= 'table' then error(a .. '/' .. b .. ' must be a table like {10, 20}', 4) end\n"
"  return math.floor(v[1] or v[a] or 0), math.floor(v[2] or v[b] or 0)\n"
"end\n"
"local function hotkey_code(s)\n"
"  if type(s) ~= 'string' then error('Hotkey must be a string like \"F5\" or \"Ctrl+A\"', 3) end\n"
"  local vk_map = { F1=0x70, F2=0x71, F3=0x72, F4=0x73, F5=0x74, F6=0x75, F7=0x76, F8=0x77,\n"
"    F9=0x78, F10=0x79, F11=0x7A, F12=0x7B,\n"
"    ['0']=0x30, ['1']=0x31, ['2']=0x32, ['3']=0x33, ['4']=0x34, ['5']=0x35, ['6']=0x36, ['7']=0x37, ['8']=0x38, ['9']=0x39,\n"
"    A=0x41, B=0x42, C=0x43, D=0x44, E=0x45, F=0x46, G=0x47, H=0x48, I=0x49, J=0x4A, K=0x4B, L=0x4C, M=0x4D,\n"
"    N=0x4E, O=0x4F, P=0x50, Q=0x51, R=0x52, S=0x53, T=0x54, U=0x55, V=0x56, W=0x57, X=0x58, Y=0x59, Z=0x5A }\n"
"  local ctrl, shift, alt = false, false, false\n"
"  for part in s:gmatch('[^+]+') do\n"
"    local p = part:gsub('^%s*(.-)%s*$', '%1')\n"
"    if p == 'Ctrl' then ctrl = true\n"
"    elseif p == 'Shift' then shift = true\n"
"    elseif p == 'Alt' then alt = true\n"
"    else\n"
"      local vk = vk_map[p]\n"
"      if not vk then error('unknown key: ' .. p .. ' (use F1..F12, A..Z, 0..9, or Ctrl+/Shift+/Alt+)', 3) end\n"
"      local code = vk\n"
"      if ctrl then code = code | 0x100 end\n"
"      if shift then code = code | 0x200 end\n"
"      if alt then code = code | 0x400 end\n"
"      return code\n"
"    end\n"
"  end\n"
"  error('Hotkey must include a key name (e.g. \"F5\", \"Ctrl+A\")', 3)\n"
"end\n"
"local function resolve_image_path(path)\n"
"  if not path or path == '' then return nil end\n"
"  if type(path) ~= 'string' then error('Image must be a string path', 3) end\n"
"  if path:match('^[A-Za-z]:') or path:match('^[/\\\\]') or path:find('..', 1, true) then\n"
"    error('Image must be a path inside the mod folder, like \"icons/gold.png\"', 3) end\n"
"  if not MOD.dir then return path end\n"
"  return MOD.dir .. '\\\\' .. path:gsub('/', '\\\\')\n"
"end\n"
"local SET = {\n"
"  Text = function(id, v) wl.raw_ui_set(id, 'Text', tostring(v == nil and '' or v)) end,\n"
"  Position = function(id, v) local x, y = xy(v, 'X', 'Y'); wl.raw_ui_set(id, 'X', x); wl.raw_ui_set(id, 'Y', y) end,\n"
"  Size = function(id, v) local w, h = xy(v, 'W', 'H'); wl.raw_ui_set(id, 'W', w); wl.raw_ui_set(id, 'H', h) end,\n"
"  Anchor = function(id, v) local a = ANCHOR[v]\n"
"    if not a then error('unknown Anchor: ' .. tostring(v), 3) end; wl.raw_ui_set(id, 'Anchor', a) end,\n"
"  Visible = function(id, v) wl.raw_ui_set(id, 'Visible', v and true or false) end,\n"
"  Color = function(id, v) if v then wl.raw_ui_set(id, 'Color', rgb(v)) else wl.raw_ui_set(id, 'NoColor', 0) end end,\n"
"  TextColor = function(id, v) wl.raw_ui_set(id, 'TextColor', rgb(v)) end,\n"
"  BorderColor = function(id, v) if v then wl.raw_ui_set(id, 'BorderColor', rgb(v)) else wl.raw_ui_set(id, 'NoBorder', 0) end end,\n"
"  Transparency = function(id, v) wl.raw_ui_set(id, 'Alpha', math.floor((1 - math.max(0, math.min(1, v))) * 255 + 0.5)) end,\n"
"  TextSize = function(id, v) wl.raw_ui_set(id, 'TextSize', v) end,\n"
"  Bold = function(id, v) wl.raw_ui_set(id, 'Bold', v and true or false) end,\n"
"  Align = function(id, v) local a = ALIGN[v]\n"
"    if not a then error('unknown Align: ' .. tostring(v), 3) end; wl.raw_ui_set(id, 'Align', a) end,\n"
"  Parent = function(id, v) wl.raw_ui_set(id, 'Parent', v and v._id or 0) end,\n"
"  Placeholder = function(id, v) wl.raw_ui_set(id, 'Placeholder', tostring(v or '')) end,\n"
"  MaxLength = function(id, v) wl.raw_ui_set(id, 'MaxLength', v) end,\n"
"  Numeric = function(id, v) wl.raw_ui_set(id, 'Numeric', v and true or false) end,\n"
"  Hotkey = function(id, v) wl.raw_ui_set(id, 'Hotkey', v and hotkey_code(v) or 0) end,\n"
"  Image = function(id, v) wl.raw_ui_set(id, 'Image', v and resolve_image_path(v) or false) end,\n"
"}\n"
"local elements = {}\n"
"local Element = {}\n"
"Element.__index = function(e, k)\n"
"  local m = Element[k]; if m ~= nil then return m end\n"
"  if k == 'Destroyed' then return rawget(e, '_dead') or false end\n"
"  return rawget(e, '_p')[k]\n"
"end\n"
"Element.__newindex = function(e, k, v)\n"
"  local s = SET[k]\n"
"  if not s then error('unknown UI property: ' .. tostring(k), 2) end\n"
"  if rawget(e, '_dead') then return end\n"
"  if k == 'Parent' and v ~= nil and getmetatable(v) ~= Element then error('Parent must be a UI element', 2) end\n"
"  s(e._id, v); e._p[k] = v\n"
"end\n"
"Element.__tostring = function(e) return e._kind .. ' ' .. e._id end\n"
"function Element:Show() self.Visible = true end\n"
"function Element:Hide() self.Visible = false end\n"
"function Element:SetText(t) self.Text = t end\n"
"function Element:SetPosition(x, y) self.Position = { x, y } end\n"
"function Element:SetSize(w, h) self.Size = { w, h } end\n"
"function Element:Destroy()\n"
"  if rawget(self, '_dead') then return end\n"
"  wl.raw_ui_destroy(self._id)\n"
"  local function kill(e)\n"
"    rawset(e, '_dead', true); elements[e._id] = nil\n"
"    for _, c in pairs(elements) do if c._p.Parent == e then kill(c) end end\n"
"  end\n"
"  kill(self)\n"
"end\n"
"function Element:OnClick(fn)\n"
"  local c = { Connected = true }\n"
"  function c:Disconnect() c.Connected = false end\n"
"  local list = self._click; list[#list + 1] = { fn = fn, c = c }\n"
"  return c\n"
"end\n"
"function Element:OnHover(fn)\n"
"  local c = { Connected = true }\n"
"  function c:Disconnect() c.Connected = false end\n"
"  local list = self._hover; list[#list + 1] = { fn = fn, c = c }\n"
"  return c\n"
"end\n"
"function Element:OnLeave(fn)\n"
"  local c = { Connected = true }\n"
"  function c:Disconnect() c.Connected = false end\n"
"  local list = self._leave; list[#list + 1] = { fn = fn, c = c }\n"
"  return c\n"
"end\n"
"local function create(kind, num, a, b)\n"
"  local props = (a == UI) and b or a\n"
"  if props ~= nil and type(props) ~= 'table' then props = { Text = props } end\n"
"  local id = wl.raw_ui_create(num)\n"
"  if not id then error('too many UI elements (256 at most)', 3) end\n"
"  local e = setmetatable({ _id = id, _kind = kind, _p = {}, _click = {}, _hover = {}, _leave = {}, _changed = {}, _submit = {}, _key = {}, _scroll = {} }, Element)\n"
"  elements[id] = e\n"
"  props = props or {}\n"
"  if props.Parent then e.Parent = props.Parent end\n"
"  for k, v in pairs(props) do\n"
"    if k == 'OnClick' then e:OnClick(v)\n"
"    elseif k == 'OnHover' then e:OnHover(v)\n"
"    elseif k == 'OnLeave' then e:OnLeave(v)\n"
"    elseif k == 'OnChanged' then e:OnChanged(v)\n"
"    elseif k == 'OnSubmit' then e:OnSubmit(v)\n"
"    elseif k == 'OnKey' then e:OnKey(v)\n"
"    elseif k == 'OnScroll' then e:OnScroll(v)\n"
"    elseif k ~= 'Parent' then e[k] = v end\n"
"  end\n"
"  return e\n"
"end\n"
"function UI.CreateFrame(a, b) return create('Frame', 1, a, b) end\n"
"function UI.CreateLabel(a, b) return create('Label', 2, a, b) end\n"
"function UI.CreateButton(a, b) return create('Button', 3, a, b) end\n"
"function UI.CreateTextBox(a, b) return create('TextBox', 4, a, b) end\n"
"function Element:Focus() wl.raw_ui_set(self._id, 'Focus', true) end\n"
"function Element:OnChanged(fn)\n"
"  local c = { Connected = true }; function c:Disconnect() c.Connected = false end\n"
"  local list = self._changed; list[#list + 1] = { fn = fn, c = c }; return c\n"
"end\n"
"function Element:OnSubmit(fn)\n"
"  local c = { Connected = true }; function c:Disconnect() c.Connected = false end\n"
"  local list = self._submit; list[#list + 1] = { fn = fn, c = c }; return c\n"
"end\n"
"function Element:OnKey(fn)\n"
"  local c = { Connected = true }; function c:Disconnect() c.Connected = false end\n"
"  local list = self._key; list[#list + 1] = { fn = fn, c = c }; return c\n"
"end\n"
"function Element:OnScroll(fn)\n"
"  local c = { Connected = true }; function c:Disconnect() c.Connected = false end\n"
"  local list = self._scroll; list[#list + 1] = { fn = fn, c = c }; return c\n"
"end\n"
"local KEY_NAMES = { [0x09]='Tab', [0x21]='PageUp', [0x22]='PageDown', [0x26]='Up', [0x28]='Down' }\n"
"wl.on('ui_key', function(packed)\n"
"  local id = packed >> 16; local vk = packed & 0xffff\n"
"  local e = elements[id]; if not e then return end\n"
"  local name = KEY_NAMES[vk] or tostring(vk)\n"
"  for _, h in ipairs(e._key) do\n"
"    if h.c.Connected then\n"
"      local ok, err = pcall(h.fn, e, name)\n"
"      if not ok then print('OnKey error: ' .. tostring(err)) end\n"
"    end\n"
"  end\n"
"end)\n"
"wl.on('ui_scroll', function(packed)\n"
"  local id = packed >> 16; local dir = (packed & 0xffff) == 1 and 1 or -1\n"
"  local e = elements[id]; if not e then return end\n"
"  for _, h in ipairs(e._scroll) do\n"
"    if h.c.Connected then\n"
"      local ok, err = pcall(h.fn, e, dir)\n"
"      if not ok then print('OnScroll error: ' .. tostring(err)) end\n"
"    end\n"
"  end\n"
"end)\n"
"local function text_event(list_name, label)\n"
"  return function(id)\n"
"    local e = elements[id]\n"
"    if not e then return end\n"
"    local t = wl.raw_ui_text(id); if t then e._p.Text = t end\n"
"    for _, h in ipairs(e[list_name]) do\n"
"      if h.c.Connected then\n"
"        local ok, err = pcall(h.fn, e, e._p.Text or '')\n"
"        if not ok then print(label .. ' error: ' .. tostring(err)) end\n"
"      end\n"
"    end\n"
"  end\n"
"end\n"
"wl.on('ui_text', text_event('_changed', 'OnChanged'))\n"
"wl.on('ui_submit', text_event('_submit', 'OnSubmit'))\n"
"wl.on('ui_click', function(id)\n"
"  local e = elements[id]\n"
"  if not e then return end\n"
"  for _, h in ipairs(e._click) do\n"
"    if h.c.Connected then\n"
"      local ok, err = pcall(h.fn, e)\n"
"      if not ok then print('OnClick error: ' .. tostring(err)) end\n"
"    end\n"
"  end\n"
"end)\n"
"wl.on('ui_enter', function(id)\n"
"  local e = elements[id]\n"
"  if not e then return end\n"
"  for _, h in ipairs(e._hover) do\n"
"    if h.c.Connected then\n"
"      local ok, err = pcall(h.fn, e)\n"
"      if not ok then print('OnHover error: ' .. tostring(err)) end\n"
"    end\n"
"  end\n"
"end)\n"
"wl.on('ui_leave', function(id)\n"
"  local e = elements[id]\n"
"  if not e then return end\n"
"  for _, h in ipairs(e._leave) do\n"
"    if h.c.Connected then\n"
"      local ok, err = pcall(h.fn, e)\n"
"      if not ok then print('OnLeave error: ' .. tostring(err)) end\n"
"    end\n"
"  end\n"
"end)\n"
"Time = { Wait = wl.wait, After = function(s, fn) return wl.after(s, fn) end,\n"
"  Every = function(s, fn) return wl.every(s, fn) end, Cancel = wl.cancel }\n"
"\n";

// --- DevPanel Lua console (dedicated env, REPL =/expr, same sandbox) ------
#define WL_CON_MAX_LINES 128
static char g_con_lines[WL_CON_MAX_LINES][300];
static int g_con_nlines;

static void con_append_line(const char *s, size_t n) {
    if (g_con_nlines >= WL_CON_MAX_LINES) {
        memmove(g_con_lines, g_con_lines[1], (size_t)(WL_CON_MAX_LINES - 1) * sizeof g_con_lines[0]);
        g_con_nlines = WL_CON_MAX_LINES - 1;
    }
    char *dst = g_con_lines[g_con_nlines++];
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < sizeof g_con_lines[0]; i++) {
        if (s[i] == '\t') {
            if (o + 2 < sizeof g_con_lines[0]) { dst[o++] = ' '; dst[o++] = ' '; }
        } else if (s[i] != '\r') {
            dst[o++] = s[i];
        }
    }
    dst[o] = 0;
}

static void con_append(const char *s) {
    if (!s) return;
    const char *p = s;
    for (;;) {
        const char *nl = strchr(p, '\n');
        if (!nl) { con_append_line(p, strlen(p)); break; }
        con_append_line(p, (size_t)(nl - p));
        p = nl + 1;
    }
}

static int api_console_print(lua_State *L) {
    luaL_Buffer b;
    join_args(L, &b);
    luaL_pushresult(&b);
    const char *s = lua_tostring(L, -1);
    if (s) {
        if (log_allowed("console")) wl_log("[lua:console] %s", s);
        con_append(s);
    }
    return 0;
}

static int console_pcall(lua_State *L, int nargs, int nresults) {
    lua_Hook oh = lua_gethook(L);
    int om = lua_gethookmask(L), oc = lua_gethookcount(L);
    if (g_depth++ == 0) budget_start(L);
    else lua_sethook(L, insn_hook, LUA_MASKCOUNT, g_aborting ? 1 : WL_HOOK_STEP);
    int rc = lua_pcall(L, nargs, nresults, 0);
    lua_sethook(L, oh, om, oc);
    if (--g_depth == 0) g_aborting = 0;
    return rc;
}

static void get_console_env(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, WL_CONSOLE_ENV_KEY);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);

    WlMod cm;
    memset(&cm, 0, sizeof cm);
    snprintf(cm.id, sizeof cm.id, "console");
    snprintf(cm.name, sizeof cm.name, "Dev Console");
    snprintf(cm.version, sizeof cm.version, "%s", WOLOLOADER_VERSION);
    for (int i = 0; i < g_wl.listed_count; i++) {
        if (is_base_mod_id(g_wl.mods[i].id)) {
            snprintf(cm.dir, sizeof cm.dir, "%s", g_wl.mods[i].dir);
            break;
        }
    }
    push_mod_env(L, &cm, -1);
    lua_pushcfunction(L, api_console_print);
    lua_setfield(L, -2, "print");
    lua_getfield(L, -1, "wl");
    lua_pushcfunction(L, api_console_print);
    lua_setfield(L, -2, "log");
    lua_pop(L, 1);

    if (luaL_loadbufferx(L, k_prelude, sizeof k_prelude - 1, "=wololoader_api", "t") == LUA_OK) {
        lua_pushvalue(L, -2);
        lua_setupvalue(L, -2, 1);
        if (guarded_pcall(L, 0) != LUA_OK) { wl_log("[lua:console] api error: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
    } else {
        wl_log("[lua:console] api load error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, WL_CONSOLE_ENV_KEY);
}

static void push_con_lines(lua_State *L) {
    lua_createtable(L, g_con_nlines, 0);
    for (int i = 0; i < g_con_nlines; i++) {
        lua_pushstring(L, g_con_lines[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

static int format_ret_fn(lua_State *L) {
    int nret = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= nret; i++) {
        luaL_tolstring(L, i, NULL);
        if (i > 1) luaL_addstring(&b, ", ");
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    return 1;
}

// wl.dev_eval(line) -> ok, lines_table, err_or_nil
static int api_dev_eval(lua_State *L) {
    size_t raw_len = 0;
    const char *raw = luaL_checklstring(L, 1, &raw_len);
    if (!dev_allowed()) {
        wl_log("[lua:console] refused in multiplayer without cheats");
        lua_pushboolean(L, 0);
        lua_newtable(L);
        lua_pushstring(L, "Dev console is unavailable in multiplayer unless cheats are enabled.");
        return 3;
    }
    const char *start = raw, *end = raw + raw_len;
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    size_t len = (size_t)(end - start);
    if (len == 0) {
        lua_pushboolean(L, 1);
        lua_newtable(L);
        return 2;
    }
    if ((unsigned char)start[0] == 0x1b) {
        lua_pushboolean(L, 0);
        lua_newtable(L);
        lua_pushstring(L, "binary chunks are not allowed");
        return 3;
    }

    g_con_nlines = 0;
    int top = lua_gettop(L);
    int rc = LUA_ERRSYNTAX;
    if (start[0] == '=') {
        size_t rlen = len - 1;
        char *buf = (char *)malloc(rlen + 8);
        if (!buf) return luaL_error(L, "out of memory");
        memcpy(buf, "return ", 7);
        memcpy(buf + 7, start + 1, rlen);
        buf[7 + rlen] = 0;
        rc = luaL_loadbufferx(L, buf, 7 + rlen, "=console", "t");
        free(buf);
    } else {
        char *buf = (char *)malloc(len + 8);
        if (!buf) return luaL_error(L, "out of memory");
        memcpy(buf, "return ", 7);
        memcpy(buf + 7, start, len);
        buf[7 + len] = 0;
        rc = luaL_loadbufferx(L, buf, 7 + len, "=console", "t");
        free(buf);
        if (rc != LUA_OK) {
            lua_pop(L, 1);
            rc = luaL_loadbufferx(L, start, len, "=console", "t");
        }
    }
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        char msg[300];
        snprintf(msg, sizeof msg, "%s", err ? err : "syntax error");
        lua_settop(L, top);
        lua_pushboolean(L, 0);
        lua_newtable(L);
        lua_pushstring(L, msg);
        return 3;
    }

    get_console_env(L);
    lua_setupvalue(L, -2, 1);  // _ENV = dedicated console env
    int call_base = lua_gettop(L) - 1;
    rc = console_pcall(L, 0, LUA_MULTRET);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        char msg[300];
        snprintf(msg, sizeof msg, "%s", err ? err : "(error)");
        wl_log("[lua:console] error: %s", msg);
        lua_settop(L, top);
        lua_pushboolean(L, 0);
        push_con_lines(L);
        lua_pushstring(L, msg);
        return 3;
    }

    int nret = lua_gettop(L) - call_base;
    if (nret > 0) {
        lua_pushcfunction(L, format_ret_fn);
        lua_insert(L, call_base + 1);
        rc = console_pcall(L, nret, 1);
        if (rc != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            char msg[300];
            snprintf(msg, sizeof msg, "%s", err ? err : "(error)");
            wl_log("[lua:console] error: %s", msg);
            lua_settop(L, top);
            lua_pushboolean(L, 0);
            push_con_lines(L);
            lua_pushstring(L, msg);
            return 3;
        }
        const char *res = lua_tostring(L, -1);
        if (res) con_append(res);
    }
    lua_settop(L, top);
    lua_pushboolean(L, 1);
    push_con_lines(L);
    return 2;
}

// --- file loading -----------------------------------------------------------
static char *read_whole(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 4 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *len = got;
    return buf;
}

enum { LOAD_OK = 1, LOAD_MISSING = 0, LOAD_ERR = -1 };

static int run_mod_main(lua_State *L, const WlMod *m, int index, char *err_out, size_t err_cap) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s" WL_SEP "scripts" WL_SEP "main.lua", m->dir);
    size_t len = 0;
    char *src = read_whole(path, &len);
    if (!src) return LOAD_MISSING;  // mod has no script (normal)
    // Skip UTF-8 BOM so Notepad-saved files load.
    const char *code = src;
    if (len >= 3 && (unsigned char)code[0] == 0xEF && (unsigned char)code[1] == 0xBB && (unsigned char)code[2] == 0xBF) {
        code += 3; len -= 3;
    }
    char chunk[MAX_PATH + 2];
    snprintf(chunk, sizeof chunk, "@%s", path);
    int top = lua_gettop(L);
    // mode "t": text only, precompiled bytecode is a known sandbox escape
    int rc = luaL_loadbufferx(L, code, len, chunk, "t");
    free(src);
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        wl_log("[lua:%s] load error: %s", m->id, msg ? msg : "syntax error");
        if (err_out && err_cap) snprintf(err_out, err_cap, "%s", msg ? msg : "syntax error");
        lua_settop(L, top);
        return LOAD_ERR;
    }
    push_mod_env(L, m, index);
    // Roblox-style API (k_prelude) in the same env, before main.lua
    if (luaL_loadbufferx(L, k_prelude, sizeof k_prelude - 1, "=wololoader_api", "t") == LUA_OK) {
        lua_pushvalue(L, -2);
        lua_setupvalue(L, -2, 1);
        if (guarded_pcall(L, 0) != LUA_OK) { wl_log("[lua] api error: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
    } else { wl_log("[lua] api load error: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
    lua_setupvalue(L, -2, 1);  // upvalue 1 of a main chunk is _ENV; pops the env
    wl_log("[lua:%s] running scripts\\main.lua", m->id);
    rc = guarded_pcall(L, 0);
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        wl_log("[lua:%s] error: %s", m->id, msg ? msg : "runtime error");
        if (err_out && err_cap) snprintf(err_out, err_cap, "%s", msg ? msg : "runtime error");
        lua_settop(L, top);
        return LOAD_ERR;
    }
    lua_settop(L, top);
    return LOAD_OK;
}

// --- hot reload: change detection, clean per-mod unload, reload -------------
typedef struct {
    int has_main;
    int file_count;
    uint64_t total_size;
    uint64_t newest_mtime;
    uint64_t hash;
} WlScriptFp;

typedef struct {
    int loaded;
    WlScriptFp loaded_fp;
    int pending;
    WlScriptFp pend_fp;
    DWORD pend_since_ms;
} WlModWatch;

static WlModWatch g_watch[WL_MAX_MODS];
static DWORD g_last_poll_ms;
static int g_have_poll;

static int fp_equal(const WlScriptFp *a, const WlScriptFp *b) {
    return a->has_main == b->has_main && a->file_count == b->file_count &&
           a->total_size == b->total_size && a->newest_mtime == b->newest_mtime &&
           a->hash == b->hash;
}

static int is_lua_filename(const char *name) {
    size_t n = strlen(name);
    if (n < 5) return 0;
    const char *e = name + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'l' && (e[2] | 32) == 'u' && (e[3] | 32) == 'a';
}

static uint64_t hash_file_entry(const char *rel, uint64_t mtime, uint64_t size) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)rel; *p; p++) { h ^= *p; h *= 0x100000001b3ULL; }
    for (int i = 0; i < 8; i++) { h ^= (mtime >> (i * 8)) & 0xff; h *= 0x100000001b3ULL; }
    for (int i = 0; i < 8; i++) { h ^= (size  >> (i * 8)) & 0xff; h *= 0x100000001b3ULL; }
    return h;
}

static void record_lua_entry(WlScriptFp *fp, const char *rel, int depth, const char *name, uint64_t mtime, uint64_t size) {
    fp->file_count++;
    fp->total_size += size;
    if (mtime > fp->newest_mtime) fp->newest_mtime = mtime;
    fp->hash ^= hash_file_entry(rel, mtime, size);
    if (depth == 0 && strlen(name) == 8 && _strnicmp(name, "main.lua", 8) == 0)
        fp->has_main = 1;
}

static void scan_dir_lua(const char *dir, const char *rel_prefix, int depth, WlScriptFp *fp) {
    if (depth > 8) return;
#ifdef _WIN32
    char pat[MAX_PATH + 32];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char rel[MAX_PATH];
        if (rel_prefix[0]) snprintf(rel, sizeof rel, "%s/%s", rel_prefix, name);
        else snprintf(rel, sizeof rel, "%s", name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char sub[MAX_PATH + 32];
            snprintf(sub, sizeof sub, "%s\\%s", dir, name);
            scan_dir_lua(sub, rel, depth + 1, fp);
        } else if (is_lua_filename(name)) {
            uint64_t mt = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | (uint64_t)fd.ftLastWriteTime.dwLowDateTime;
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;
            record_lua_entry(fp, rel, depth, name, mt, sz);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char full[MAX_PATH + 64], rel[MAX_PATH];
        snprintf(full, sizeof full, "%s/%s", dir, name);
        if (rel_prefix[0]) snprintf(rel, sizeof rel, "%s/%s", rel_prefix, name);
        else snprintf(rel, sizeof rel, "%s", name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir_lua(full, rel, depth + 1, fp);
        } else if (S_ISREG(st.st_mode) && is_lua_filename(name)) {
            uint64_t mt = (uint64_t)st.st_mtime * 1000000000ULL + (uint64_t)st.st_mtim.tv_nsec;
            uint64_t sz = (uint64_t)st.st_size;
            record_lua_entry(fp, rel, depth, name, mt, sz);
        }
    }
    closedir(d);
#endif
}

static void scan_mod_scripts(const char *mod_dir, WlScriptFp *out) {
    memset(out, 0, sizeof *out);
    char sdir[MAX_PATH + 16];
    snprintf(sdir, sizeof sdir, "%s" WL_SEP "scripts", mod_dir);
    scan_dir_lua(sdir, "", 0, out);
}

static void compact_handler_list(lua_State *L, int list_idx) {
    lua_Integer total = (lua_Integer)lua_rawlen(L, list_idx), w = 1;
    for (lua_Integer i = 1; i <= total; i++) {
        lua_rawgeti(L, list_idx, i);
        lua_getfield(L, -1, "dead");
        int is_dead = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (!is_dead) lua_rawseti(L, list_idx, w++);
        else lua_pop(L, 1);
    }
    for (lua_Integer i = w; i <= total; i++) { lua_pushnil(L); lua_rawseti(L, list_idx, i); }
}

static void cleanup_dirty_handlers(lua_State *L) {
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return; }
    int htbl = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, htbl)) {
        if (lua_istable(L, -1)) {
            compact_handler_list(L, lua_gettop(L));
            if (lua_rawlen(L, -1) == 0) {
                lua_pushvalue(L, -2);
                lua_pushnil(L);
                lua_settable(L, htbl);
            }
        }
        lua_pop(L, 1);
    }
    g_handlers_dirty = 0;
    lua_settop(L, top);
}

static void unload_mod_handlers(lua_State *L, const char *mod_id) {
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return; }
    int htbl = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, htbl)) {
        if (lua_istable(L, -1)) {
            int list = lua_gettop(L);
            lua_Integer n = (lua_Integer)lua_rawlen(L, list);
            int marked = 0;
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, list, i);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "mod");
                    const char *m = lua_tostring(L, -1);
                    if (m && strcmp(m, mod_id) == 0) {
                        lua_pushboolean(L, 1);
                        lua_setfield(L, -3, "dead");
                        marked = 1;
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            if (marked) {
                if (g_dispatch_depth == 0) {
                    compact_handler_list(L, list);
                    if (lua_rawlen(L, list) == 0) {
                        lua_pushvalue(L, -2);
                        lua_pushnil(L);
                        lua_settable(L, htbl);
                    }
                } else {
                    g_handlers_dirty = 1;
                }
            }
        }
        lua_pop(L, 1);
    }
    lua_settop(L, top);
}

static void unload_mod_conditions(lua_State *L, const char *mod_id) {
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_CONDS_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return; }
    int ctbl = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, ctbl)) {
        int match = 0;
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "mod");
            const char *m = lua_tostring(L, -1);
            match = (m && strcmp(m, mod_id) == 0);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (match) {
            lua_pushvalue(L, -1);
            lua_pushnil(L);
            lua_settable(L, ctbl);
        }
    }
    g_cond_cache_cnt = 0;
    lua_settop(L, top);
}

static void unload_mod(lua_State *L, const char *mod_id) {
    if (!L || !mod_id || !mod_id[0]) return;
    unload_mod_handlers(L, mod_id);
    unload_mod_conditions(L, mod_id);
    unload_mod_rt_cmds(mod_id);
    for (int i = 0; i < WL_MAX_TIMERS; i++)
        if (g_timers[i].used && strcmp(g_timers[i].mod, mod_id) == 0) free_timer(&g_timers[i]);
    for (int i = 0; i < WL_MAX_ASKS; i++)
        if (g_asks[i].used && strcmp(g_asks[i].mod, mod_id) == 0) free_ask(&g_asks[i]);
    for (int i = 0; i < WL_MAX_TASKS; i++) {
        if (g_tasks[i].used && strcmp(g_tasks[i].mod, mod_id) == 0) {
            luaL_unref(L, LUA_REGISTRYINDEX, g_tasks[i].ref);
            g_tasks[i].used = 0;
        }
    }
    unload_mod_ui(mod_id);
    unload_mod_counter(mod_id);
    for (int i = 0; i < 64 && g_logcount[i].mod[0]; i++) {
        if (strcmp(g_logcount[i].mod, mod_id) == 0) { g_logcount[i].lines = 0; break; }
    }
}

static void short_lua_err(const char *err, char *out, size_t cap) {
    if (!cap) return;
    const char *p = err ? err : "error";
    const char *s = strstr(p, "scripts/");
    if (!s) s = strstr(p, "scripts\\");
    if (s) p = s;
    size_t w = 0;
    for (; *p && w + 1 < cap; p++) {
        char c = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
        out[w++] = c;
    }
    out[w] = 0;
}

static void dispatch_filtered(const char *name, const char *mod_filter, int has_arg, int arg);

enum { RELOAD_OK = 1, RELOAD_MISSING = 0, RELOAD_ERR = -1, RELOAD_NOT_FOUND = -2, RELOAD_DENIED = -3 };

static int reload_mod_at(int idx, int notify, char *err_out, size_t err_cap) {
    const WlMod *m = &g_wl.mods[idx];
    int was_loaded = g_watch[idx].loaded;
    unload_mod(g_L, m->id);
    lua_gc(g_L, LUA_GCCOLLECT, 0);

    char raw_err[512] = "";
    int rc = run_mod_main(g_L, m, idx, raw_err, sizeof raw_err);
    if (rc == LOAD_MISSING) {
        g_watch[idx].loaded = 0;
        if (g_in_game && g_buildpage_refresh_fn) g_buildpage_refresh_fn();
        if (was_loaded) {
            wl_log("[lua:%s] scripts\\main.lua missing, mod unloaded", m->id);
            if (notify) {
                char msg[160];
                snprintf(msg, sizeof msg, "Unloaded %s", m->id);
                show_ui_message(msg);
            }
        }
        if (err_out && err_cap) snprintf(err_out, err_cap, "no scripts/main.lua");
        return RELOAD_MISSING;
    }
    if (rc == LOAD_ERR) {
        // Keep the mod unloaded on syntax or runtime error (discarding partial registrations).
        unload_mod(g_L, m->id);
        lua_gc(g_L, LUA_GCCOLLECT, 0);
        g_watch[idx].loaded = 0;
        if (g_in_game && g_buildpage_refresh_fn) g_buildpage_refresh_fn();
        wl_log("[lua:%s] reload failed: %s", m->id, raw_err);
        char serr[180];
        short_lua_err(raw_err, serr, sizeof serr);
        char msg[240];
        snprintf(msg, sizeof msg, "Reload failed: %s: %s", m->id, serr);
        if (notify) show_ui_message(msg);
        if (err_out && err_cap) snprintf(err_out, err_cap, "%s", msg);
        return RELOAD_ERR;
    }

    g_watch[idx].loaded = 1;
    wl_log("[lua:%s] reloaded", m->id);
    // Mid-game reload: run this mod's Game.OnStart handlers so match-start timers and UI work,
    // and refresh any open building command panel in case buttons changed or were removed.
    if (g_in_game) {
        dispatch_filtered("game_start", m->id, 0, 0);
        if (g_buildpage_refresh_fn) g_buildpage_refresh_fn();
    }
    if (notify) {
        char msg[160];
        snprintf(msg, sizeof msg, "Reloaded %s", m->id);
        show_ui_message(msg);
    }
    return RELOAD_OK;
}

static int find_enabled_mod(const char *key) {
    if (!key || !key[0]) return -1;
    size_t klen = strlen(key);
    for (int i = 0; i < g_wl.listed_count; i++)
        if (g_wl.mods[i].enabled && strcmp(g_wl.mods[i].id, key) == 0) return i;
    for (int i = 0; i < g_wl.listed_count; i++)
        if (g_wl.mods[i].enabled && strlen(g_wl.mods[i].id) == klen && _strnicmp(g_wl.mods[i].id, key, klen) == 0) return i;
    for (int i = 0; i < g_wl.listed_count; i++)
        if (g_wl.mods[i].enabled && strlen(g_wl.mods[i].name) == klen && _strnicmp(g_wl.mods[i].name, key, klen) == 0) return i;
    return -1;
}

int wl_script_reload_mod(const char *mod_id, char *err_out, size_t err_cap) {
    if (!g_L || !g_init_done || g_wl_vanilla) return RELOAD_NOT_FOUND;
    if (!dev_allowed()) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "reload is disabled in multiplayer unless cheats are enabled");
        return RELOAD_DENIED;
    }
    int idx = find_enabled_mod(mod_id);
    if (idx < 0) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "mod '%s' not found or not enabled", mod_id ? mod_id : "");
        return RELOAD_NOT_FOUND;
    }
    scan_mod_scripts(g_wl.mods[idx].dir, &g_watch[idx].loaded_fp);
    g_watch[idx].pending = 0;
    return reload_mod_at(idx, 1, err_out, err_cap);
}

// Polls <mod>/scripts/*.lua about once per second (250 ms while debouncing) and reloads
// changed mods one at a time after a 250 ms quiet window so half-written saves are skipped.
void wl_script_hotreload_frame(DWORD now_ms) {
    if (!g_L || !g_init_done || g_wl_vanilla) return;
    if (!dev_allowed()) return;
    int any_pending = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        if (g_wl.mods[i].enabled && g_watch[i].pending) { any_pending = 1; break; }
    }
    DWORD interval = any_pending ? 250 : 1000;
    if (g_have_poll && (int32_t)(now_ms - g_last_poll_ms) < (int32_t)interval) return;
    g_last_poll_ms = now_ms;
    g_have_poll = 1;

    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        WlScriptFp fp;
        scan_mod_scripts(m->dir, &fp);
        if (fp_equal(&fp, &g_watch[i].loaded_fp)) {
            g_watch[i].pending = 0;
            continue;
        }
        if (!g_watch[i].pending || !fp_equal(&fp, &g_watch[i].pend_fp)) {
            g_watch[i].pending = 1;
            g_watch[i].pend_fp = fp;
            g_watch[i].pend_since_ms = now_ms;
            continue;
        }
        if ((int32_t)(now_ms - g_watch[i].pend_since_ms) >= 250) {
            g_watch[i].pending = 0;
            g_watch[i].loaded_fp = fp;
            reload_mod_at(i, 1, NULL, 0);
        }
    }
}

// wl.reload([mod_id]): reloads one mod (returns true | false, err) or all changed mods (returns reloaded, errors).
static int api_reload(lua_State *L) {
    if (!dev_allowed())
        return luaL_error(L, "reload is disabled in multiplayer unless cheats are enabled");
    if (!lua_isnoneornil(L, 1)) {
        const char *target = luaL_checkstring(L, 1);
        char err[256] = "";
        int rc = wl_script_reload_mod(target, err, sizeof err);
        if (rc == RELOAD_OK) { lua_pushboolean(L, 1); return 1; }
        lua_pushboolean(L, 0);
        lua_pushstring(L, err[0] ? err : "reload failed");
        return 2;
    }
    lua_newtable(L);
    lua_newtable(L);
    int n_ok = 0, n_err = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        WlScriptFp fp;
        scan_mod_scripts(m->dir, &fp);
        if (fp_equal(&fp, &g_watch[i].loaded_fp)) continue;
        g_watch[i].loaded_fp = fp;
        g_watch[i].pending = 0;
        char err[256] = "";
        int rc = reload_mod_at(i, 1, err, sizeof err);
        if (rc == RELOAD_OK) {
            lua_pushstring(L, m->id);
            lua_rawseti(L, -3, ++n_ok);
        } else if (rc == RELOAD_ERR) {
            lua_pushstring(L, err);
            lua_rawseti(L, -2, ++n_err);
        }
    }
    return 2;
}

// wl.reload_mods(): full Level 2 mod hot reload at the main menu (World == NULL).
// Returns (true, status_msg) on success or (false, reason_or_error) if refused/failed.
static int api_reload_mods(lua_State *L) {
    if (!g_reload_mods_fn) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "mod hot reload is not available here");
        return 2;
    }
    char msg[512] = "";
    int rc = g_reload_mods_fn(msg, sizeof msg);
    lua_pushboolean(L, rc > 0);
    lua_pushstring(L, msg[0] ? msg : (rc > 0 ? "Mods reloaded" : "Reload failed"));
    return 2;
}

// Full Lua runtime restart for Level 2 hot reload (hotreload.c).
// When called outside Lua (e.g. from Mods screen), destroys and recreates g_L.
// When called from inside a Lua frame (wl.reload_mods()), reloads every mod in place
// so the active Lua stack frame remains valid on return.
void wl_script_restart(void) {
    if (g_wl_vanilla) return;
    if (!g_L || !g_init_done) {
        wl_script_init();
        return;
    }
    if (g_depth > 0) {
        // Inside an active Lua pcall (e.g. wl.reload_mods()): keep g_L alive,
        // unload every old mod's handlers/UI/timers/conditions/runtime commands, then load all enabled mods.
        lua_State *L = g_L;
        wl_script_game_reset(g_now);
        clear_all_rt_cmds();
        while (g_ui_owned_count > 0) {
            int id = g_ui_owned[0].id;
            if (g_ui && g_ui->destroy) g_ui->destroy(id);
            ui_owned_remove_tree(id);
        }
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, WL_CONDS_KEY);
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, WL_CONSOLE_ENV_KEY);
        g_cond_cache_cnt = 0;
        g_cond_refresh_active = 0;
        memset(g_watch, 0, sizeof g_watch);
        lua_gc(L, LUA_GCCOLLECT, 0);
        int ran = 0;
        for (int i = 0; i < g_wl.listed_count; i++) {
            const WlMod *m = &g_wl.mods[i];
            if (!m->enabled) continue;
            scan_mod_scripts(m->dir, &g_watch[i].loaded_fp);
            g_watch[i].pending = 0;
            g_watch[i].loaded = (run_mod_main(L, m, i, NULL, 0) == LOAD_OK);
            ran++;
        }
        wl_log("[lua] runtime reloaded in-place (%d enabled mod(s) checked)", ran);
        return;
    }

    // Outside Lua: tear down all UI widgets, timers, runtime commands, and the Lua VM completely, then re-init.
    wl_script_game_reset(0);
    clear_all_rt_cmds();
    while (g_ui_owned_count > 0) {
        int id = g_ui_owned[0].id;
        if (g_ui && g_ui->destroy) g_ui->destroy(id);
        ui_owned_remove_tree(id);
    }
    if (g_ui_counter) g_ui_counter(NULL);
    lua_close(g_L);
    g_L = NULL;
    g_init_done = 0;
    g_mem_used = 0;
    g_in_game = 0;
    g_dispatch_depth = 0;
    g_handlers_dirty = 0;
    g_depth = 0;
    g_aborting = 0;
    g_cond_cache_cnt = 0;
    g_cond_refresh_active = 0;
    g_have_poll = 0;
    memset(g_watch, 0, sizeof g_watch);
    wl_script_init();
}

// --- public API -------------------------------------------------------------
void wl_script_set_ui(void (*message)(const char *), void (*dialog)(const char *)) {
    g_ui_message = message;
    g_ui_dialog = dialog;
}

void wl_script_set_sound(void (*sound)(int id)) { g_ui_sound = sound; }
void wl_script_set_counter(void (*counter)(const char *)) { g_ui_counter = counter; }

void wl_script_set_chat(void (*chat)(const char *, const char *, int)) { g_ui_chat = chat; }
void wl_script_set_ask(void (*ask)(const char *, int)) { g_ui_ask = ask; }

// --- hardening of the standard library -------------------------------------
// Lua's pattern matcher backtracks in C (no instruction hook): ".-.-.-b" on a long
// text runs for hours. Worst case ~ len^(1 + unbounded quantifiers) * 2^(optional ones),
// one len factor less when anchored with '^'. Refuse above WL_PATTERN_COST.
#define WL_PATTERN_COST 2e8
static double pattern_cost(const char *p, size_t plen, size_t len) {
    int unbounded = 0, optional = 0, anchored = plen > 0 && p[0] == '^';
    for (size_t i = 0; i < plen; i++) {
        if (p[i] == '%') { i++; continue; }                    // escape or class (%a, %b.., %f[..])
        if (p[i] == '[') {                                     // set: skip to the closing ]
            i++;
            if (i < plen && p[i] == '^') i++;
            if (i < plen && p[i] == ']') i++;
            while (i < plen && p[i] != ']') { if (p[i] == '%') i++; i++; }
            continue;
        }
        if (p[i] == '*' || p[i] == '-' || p[i] == '+') unbounded++;
        else if (p[i] == '?') optional++;
    }
    double n = (double)len + 1, cost = anchored ? 1 : n;
    for (int k = 0; k < unbounded; k++) cost *= n;
    for (int k = 0; k < optional && k < 40; k++) cost *= 2;
    return cost;
}
// upvalue 1: the original function. find/match: (s, pattern, init, plain); gmatch/gsub: (s, pattern, ...)
static int guarded_match(lua_State *L) {
    size_t len, plen;
    const char *str = luaL_checklstring(L, 1, &len);
    const char *pat = luaL_checklstring(L, 2, &plen);
    (void)str;
    int plain = lua_toboolean(L, 4) && lua_tointeger(L, lua_upvalueindex(2)) == 1;   // only find has `plain`
    if (!plain && pattern_cost(pat, plen, len) > WL_PATTERN_COST)
        return luaL_error(L, "pattern too complex for a text of %d characters", (int)len);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}
// collectgarbage("collect" | "count" | "step"): stopping the collector or changing its mode is not allowed
static int guarded_gc(lua_State *L) {
    const char *opt = luaL_optstring(L, 1, "collect");
    if (strcmp(opt, "collect") && strcmp(opt, "count") && strcmp(opt, "step"))
        return luaL_error(L, "collectgarbage('%s') is not allowed in mod scripts", opt);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}
// xpcall(f, handler, ...): Lua runs the handler while the error is being raised, which inside
// our instruction hook means with hooks off: an endless handler would never stop. This version
// calls f with pcall and the handler afterwards, as a normal (counted) call.
static int safe_xpcall(lua_State *L) {
    int n = lua_gettop(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 1);                          // f
    for (int i = 3; i <= n; i++) lua_pushvalue(L, i);
    if (lua_pcall(L, n - 2, LUA_MULTRET, 0) == LUA_OK) {
        lua_pushboolean(L, 1);
        lua_insert(L, n + 1);
        return lua_gettop(L) - n;
    }
    lua_pushvalue(L, 2);                          // handler(err)
    lua_insert(L, -2);
    lua_call(L, 1, 1);
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;
}

static void harden_libs(lua_State *L) {
    lua_pushcfunction(L, safe_xpcall);
    lua_setglobal(L, "xpcall");
    lua_getglobal(L, "string");
    static const char *const fns[] = { "find", "match", "gmatch", "gsub", NULL };
    for (int i = 0; fns[i]; i++) {
        lua_getfield(L, -1, fns[i]);
        lua_pushinteger(L, i == 0);                            // 1 = find (has the plain flag)
        lua_pushcclosure(L, guarded_match, 2);
        lua_setfield(L, -2, fns[i]);
    }
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");                               // bytecode: useless without load, keep it away
    lua_pop(L, 1);
    // lock the shared string metatable: getmetatable("") returns a string, not the table
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1)) {
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getglobal(L, "collectgarbage");
    lua_pushcclosure(L, guarded_gc, 1);
    lua_setglobal(L, "collectgarbage");
    lua_pushnil(L);
    lua_setglobal(L, "warn");
}

void wl_script_init(void) {
    if (g_init_done) return;
    g_init_done = 1;
    if (g_wl_vanilla) return;  // Vanilla Mode: no mods, no scripts

    lua_State *L = lua_newstate(wl_lua_alloc, NULL);
    if (!L) { wl_log("[lua] cannot create state"); return; }
    lua_atpanic(L, wl_lua_panic);
    // Open exactly these libraries. luaL_requiref(..., glb=1) sets the global.
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_TABLIBNAME, luaopen_table}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine}, {NULL, NULL}};
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    // File and string loading from scripts is not allowed.
    // (require is per mod: see push_require)
    static const char *const removed[] = {"dofile", "loadfile", "load", "loadstring", "require", NULL};
    for (int i = 0; removed[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, removed[i]);
    }
    harden_libs(L);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, WL_CONDS_KEY);

    g_L = L;
    int ran = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        scan_mod_scripts(m->dir, &g_watch[i].loaded_fp);
        g_watch[i].pending = 0;
        g_watch[i].loaded = (run_mod_main(L, m, i, NULL, 0) == LOAD_OK);
        ran++;
    }
    wl_log("[lua] runtime ready (%d enabled mod(s) checked)", ran);
}

// Calls all handlers of `name` (or only `mod_filter`'s handlers when non-NULL) in
// registration (= mod) order. A handler that errors is removed; the others keep running.
static void dispatch_filtered(const char *name, const char *mod_filter, int has_arg, int arg) {
    lua_State *L = g_L;
    if (!L || !name) return;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    lua_getfield(L, -1, name);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return; }
    int list = lua_gettop(L);
    lua_Integer n = (lua_Integer)lua_rawlen(L, list);  // handlers added during dispatch run next time
    g_dispatch_depth++;
    int dead = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, list, i);
        int entry = lua_gettop(L);
        if (!lua_istable(L, entry)) { lua_settop(L, entry - 1); continue; }
        lua_getfield(L, entry, "dead");
        int is_dead = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (is_dead) { lua_settop(L, entry - 1); continue; }
        lua_getfield(L, entry, "mod");
        char mod[64];
        snprintf(mod, sizeof mod, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        if (mod_filter && strcmp(mod, mod_filter) != 0) { lua_settop(L, entry - 1); continue; }
        lua_getfield(L, entry, "fn");
        int nargs = 0;
        if (has_arg) { lua_pushinteger(L, arg); nargs = 1; }
        // Handlers run as coroutines (wl.wait). Only an error before the first
        // yield disables the handler; later errors stop just that coroutine.
        if (start_coroutine(L, nargs, mod) == STEP_ERROR) {
            wl_log("[lua:%s] handler for '%s' failed and was disabled: %s", mod, name, g_err);
            lua_pushboolean(L, 1);
            lua_setfield(L, entry, "dead");
            dead = 1;
        }
        lua_settop(L, entry - 1);
    }
    if (dead) g_handlers_dirty = 1;
    g_dispatch_depth--;
    if (g_dispatch_depth == 0 && g_handlers_dirty) cleanup_dirty_handlers(L);
    lua_settop(L, top);
}

void wl_script_event(const char *name) {
    if (name && strcmp(name, "game_start") == 0) g_in_game = 1;
    dispatch_filtered(name, NULL, 0, 0);
}
void wl_script_event_int(const char *name, int arg) { dispatch_filtered(name, NULL, 1, arg); }

void wl_script_event_map_click(float tx, float ty, int button) {
    g_last_mc_x = tx; g_last_mc_y = ty; g_last_mc_btn = button;
    dispatch_filtered("map_click", NULL, 0, 0);
}

// Building Lua button pressed (buildpage.c): handlers of Game.OnCommand(name, fn).
void wl_script_command(const char *name, int unit_id) {
    char ev[96];
    snprintf(ev, sizeof ev, "cmd:%s", name ? name : "");
    g_in_local_cmd++;
    dispatch_filtered(ev, NULL, 1, unit_id);
    g_in_local_cmd--;
}

// Evaluates a condition registered via Game.CommandCondition(name, fn).
// Unregistered names default to 1 (available). Returning nil or false counts as 0 (not available);
// Lua errors are logged and return 0. Results are cached per panel refresh when wrapped by
// wl_script_cond_begin_refresh / wl_script_cond_end_refresh.
int wl_script_eval_condition(const char *name, int unit_id) {
    if (!g_L || !g_init_done || !name || !name[0]) return 1;
    if (g_cond_refresh_active && unit_id == g_cond_refresh_unit_id) {
        for (int i = 0; i < g_cond_cache_cnt; i++) {
            if (strcmp(g_cond_cache[i].name, name) == 0) return g_cond_cache[i].result;
        }
    }
    lua_State *L = g_L;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_CONDS_KEY);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return 1; }
    lua_getfield(L, -1, name);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return 1; }

    char mod[64] = "?";
    lua_getfield(L, -1, "mod");
    if (lua_isstring(L, -1)) snprintf(mod, sizeof mod, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, -1, "fn");
    if (!lua_isfunction(L, -1)) { lua_settop(L, top); return 1; }
    lua_pushinteger(L, unit_id);

    lua_Hook oh = lua_gethook(L);
    int om = lua_gethookmask(L), oc = lua_gethookcount(L);
    if (g_depth++ == 0) budget_start(L);
    else lua_sethook(L, insn_hook, LUA_MASKCOUNT, g_aborting ? 1 : WL_HOOK_STEP);

    int res = 0;
    if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
        // Standard Lua truthiness: nil and false both evaluate to 0 (unavailable).
        res = lua_toboolean(L, -1);
    } else {
        const char *msg = lua_tostring(L, -1);
        wl_log("[lua:%s] CommandCondition '%s' error: %s", mod, name, msg ? msg : "unknown error");
        res = 0;
    }
    lua_sethook(L, oh, om, oc);
    if (--g_depth == 0) g_aborting = 0;
    lua_settop(L, top);

    if (g_cond_refresh_active && unit_id == g_cond_refresh_unit_id && g_cond_cache_cnt < 32) {
        snprintf(g_cond_cache[g_cond_cache_cnt].name, sizeof g_cond_cache[0].name, "%s", name);
        g_cond_cache[g_cond_cache_cnt].result = res;
        g_cond_cache_cnt++;
    }
    return res;
}
