// Lua 5.4 scripting runtime for mods (<mod>\scripts\main.lua).
//
// THREADING: everything here assumes ONE thread, the game's main thread.
// wl_script_init() is run lazily from wl_civs_ensure() (never from DllMain:
// the CRT may not be ready, AGENTS.md rule 5) and the event functions must be
// called from the same thread. There is no locking.
//
// SANDBOX: only base, string, table, math, utf8 and coroutine are opened.
// io, os, package and debug do not exist. dofile, loadfile, load, loadstring,
// require, warn, string.dump and print (replaced) are removed. Every mod gets its
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
#include "wololoader.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
static int api_raw_is_enemy(lua_State *L);
static int api_raw_units(lua_State *L);
static int api_raw_cmd(lua_State *L);
static int api_raw_selected(lua_State *L);
static int api_raw_researched(lua_State *L);
static int api_raw_time(lua_State *L);
static int api_raw_ui_create(lua_State *L);
static int api_raw_ui_set(lua_State *L);
static int api_raw_ui_destroy(lua_State *L);
static int api_raw_ui_text(lua_State *L);

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
typedef struct { int used; int ref; lua_State *co; DWORD wake; char mod[64]; } WlTask;
typedef struct { int used; int token; int fn; int nil_pending; char mod[64]; } WlAsk;
static WlTimer g_timers[WL_MAX_TIMERS];
static WlTask g_tasks[WL_MAX_TASKS];
static WlAsk g_asks[WL_MAX_ASKS];
static int g_next_timer_id = 1, g_next_ask_token = 1;
static char g_err[512];                         // message of the last failed step

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
        t->co = co;
        t->wake = g_now + wait;
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
    memset(g_logcount, 0, sizeof g_logcount);
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
        char mod[64];
        snprintf(mod, sizeof mod, "%s", t->mod);
        DWORD wait;
        int st = task_step(co, 0, &wait);
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
static int api_counter(lua_State *L) {
    const char *t = lua_isnoneornil(L, 1) ? NULL : luaL_checkstring(L, 1);
    if (t && !*t) t = NULL;
    if (g_ui_counter) g_ui_counter(t);
    else wl_log("[lua] counter: %s", t ? t : "(hidden)");
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

// Pushes a fresh _ENV for one mod. Real globals of the state are only read here.
static void push_mod_env(lua_State *L, const WlMod *m, int index) {
    (void)index;
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
    lua_setfield(L, env, "MOD");

    // wl API (per mod so wl.log / wl.on know who is calling)
    lua_createtable(L, 0, 12);
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
    lua_pushcfunction(L, api_counter);     lua_setfield(L, -2, "counter");
    lua_pushcfunction(L, api_chat);        lua_setfield(L, -2, "chat");
    lua_pushstring(L, WOLOLOADER_VERSION); lua_setfield(L, -2, "version");
    lua_pushcfunction(L, api_raw_local);    lua_setfield(L, -2, "raw_local");
    lua_pushcfunction(L, api_raw_count);    lua_setfield(L, -2, "raw_player_count");
    lua_pushcfunction(L, api_raw_resource); lua_setfield(L, -2, "raw_resource");
    lua_pushcfunction(L, api_raw_is_enemy); lua_setfield(L, -2, "raw_is_enemy");
    lua_pushcfunction(L, api_raw_units);    lua_setfield(L, -2, "raw_units");
    lua_pushcfunction(L, api_raw_cmd);      lua_setfield(L, -2, "raw_cmd");
    lua_pushcfunction(L, api_raw_selected); lua_setfield(L, -2, "raw_selected");
    lua_pushcfunction(L, api_raw_researched); lua_setfield(L, -2, "raw_researched");
    lua_pushcfunction(L, api_raw_time);     lua_setfield(L, -2, "raw_time");
    lua_pushcfunction(L, api_raw_ui_create);  lua_setfield(L, -2, "raw_ui_create");
    lua_pushcfunction(L, api_raw_ui_set);     lua_setfield(L, -2, "raw_ui_set");
    lua_pushcfunction(L, api_raw_ui_destroy); lua_setfield(L, -2, "raw_ui_destroy");
    lua_pushcfunction(L, api_raw_ui_text);    lua_setfield(L, -2, "raw_ui_text");
    lua_setfield(L, env, "wl");
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
static int api_raw_units(lua_State *L) {
    int n = g_game ? g_game->units((int)luaL_checkinteger(L, 1), g_ubuf, 2048) : 0;
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
static int api_raw_time(lua_State *L) {
    double t = (g_game && g_game->game_time) ? g_game->game_time() : -1.0;
    if (t < 0) lua_pushnil(L); else lua_pushnumber(L, t);
    return 1;
}

// wl.raw_cmd(name, ...) -> true (sent from this machine) / false (another machine sends it);
// raises an error for invalid arguments or when no game is running. See gamecmd.c.
static int api_raw_cmd(lua_State *L) {
    const char *what = luaL_checkstring(L, 1);
    int rc = -1;
    if (!g_game || !g_game->add_resource) return luaL_error(L, "game commands are not available here");
    if (strcmp(what, "resource") == 0)
        rc = g_game->add_resource((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (float)luaL_checknumber(L, 4));
    else if (strcmp(what, "research") == 0)
        rc = g_game->research((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    else if (strcmp(what, "spawn") == 0)
        rc = g_game->spawn((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                           (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    else if (strcmp(what, "age") == 0 && g_game->set_age)
        rc = g_game->set_age((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    else if (strcmp(what, "delete") == 0)
        rc = g_game->delete_unit((int)luaL_checkinteger(L, 2));
    else return luaL_error(L, "unknown command %s", what);
    if (rc < 0) return luaL_error(L, "%s: invalid arguments or no game running", what);
    lua_pushboolean(L, rc > 0);
    return 1;
}

// On-screen UI backend (uikit.c). Without it (native test) elements still exist in Lua.
static const WlUiApi *g_ui;
void wl_script_set_uikit(const WlUiApi *api) { g_ui = api; }
static int g_fake_ui_id;
static int api_raw_ui_create(lua_State *L) {
    int type = (int)luaL_checkinteger(L, 1);
    int id = g_ui ? g_ui->create(type) : ++g_fake_ui_id;
    if (id < 0) lua_pushnil(L); else lua_pushinteger(L, id);
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
    int v = 0;
    const char *str = NULL;
    if (lua_type(L, 3) == LUA_TSTRING) str = lua_tostring(L, 3);
    else if (lua_isboolean(L, 3)) v = lua_toboolean(L, 3);
    else if (!lua_isnoneornil(L, 3)) v = (int)luaL_checknumber(L, 3);
    if (str && !strcmp(key, "Image") && !image_path_ok(str))
        return luaL_error(L, "Image must be a file inside the mod folder: %s", str);
    lua_pushboolean(L, g_ui ? g_ui->set(id, key, v, str) : 1);
    return 1;
}
static int api_raw_ui_text(lua_State *L) {
    const char *t = (g_ui && g_ui->get_text) ? g_ui->get_text((int)luaL_checkinteger(L, 1)) : NULL;
    if (t) lua_pushstring(L, t); else lua_pushnil(L);
    return 1;
}
static int api_raw_ui_destroy(lua_State *L) {
    if (g_ui) g_ui->destroy((int)luaL_checkinteger(L, 1));
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
"  if k == 'Population' then local v = wl.raw_resource(p.Id, 11); return v and math.floor(v) end\n"
"  if k == 'IsLocal' then return p.Id == wl.raw_local() end\n"
"  return Player[k]\n"
"end\n"
"Player.__tostring = function(p) return 'Player ' .. p.Id end\n"
"function Player:IsEnemy(other) return wl.raw_is_enemy(self.Id, type(other) == 'table' and other.Id or other) end\n"
"-- p:Units() all, p:Units('Monk') by class name, p:Units(function(u) ... end) custom filter\n"
"-- Game commands (sent as game commands, so every player sees the same result; true when this\n"
"-- machine sent it, false when another player's machine does):\n"
"function Player:AddResource(name, amount)\n"
"  local r = RES[name]; if not r then error('unknown resource: ' .. tostring(name), 2) end\n"
"  return wl.raw_cmd('resource', self.Id, r, amount) end\n"
"function Player:AddFood(n) return wl.raw_cmd('resource', self.Id, 0, n) end\n"
"function Player:AddWood(n) return wl.raw_cmd('resource', self.Id, 1, n) end\n"
"function Player:AddStone(n) return wl.raw_cmd('resource', self.Id, 2, n) end\n"
"function Player:AddGold(n) return wl.raw_cmd('resource', self.Id, 3, n) end\n"
"function Player:Research(techId) return wl.raw_cmd('research', self.Id, techId) end\n"
"function Player:CreateUnit(unitId, x, y) return wl.raw_cmd('spawn', self.Id, unitId, x, y) end\n"
"-- p:SetAge(2) or p:SetAge('Castle'): up = age techs researched; down = age value lowered and the\n"
"-- age-up techs researchable again (what earlier techs unlocked stays)\n"
"function Player:SetAge(age)\n"
"  if type(age) == 'string' then\n"
"    for k, v in pairs(AGES) do if v == age then age = k end end\n"
"  end\n"
"  if math.type(age) ~= 'integer' or age < 0 or age > 3 then error('SetAge: 0..3 or Dark/Feudal/Castle/Imperial', 2) end\n"
"  return wl.raw_cmd('age', self.Id, age)\n"
"end\n"
"local Unit = {}\n"
"Unit.__index = Unit\n"
"Unit.__tostring = function(u) return 'Unit ' .. u.Id end\n"
"function Unit:Delete() return wl.raw_cmd('delete', self.Id) end\n"
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
"Game.IsMultiplayer = nil\n"
"local SOUNDS = { Chat = 'chat', CantDo = 'cantdo', Tribute = 'tribute', Attack = 'attack',\n"
"  TownBell = 'townbell', TownRecall = 'townrecall', Objective = 'objective', Relic = 'relic' }\n"
"Sound = { Play = function(name) wl.sound(SOUNDS[name] or name) end }\n"
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
"  local e = setmetatable({ _id = id, _kind = kind, _p = {}, _click = {}, _hover = {}, _leave = {}, _changed = {}, _submit = {} }, Element)\n"
"  elements[id] = e\n"
"  props = props or {}\n"
"  if props.Parent then e.Parent = props.Parent end\n"
"  for k, v in pairs(props) do\n"
"    if k == 'OnClick' then e:OnClick(v)\n"
"    elseif k == 'OnHover' then e:OnHover(v)\n"
"    elseif k == 'OnLeave' then e:OnLeave(v)\n"
"    elseif k == 'OnChanged' then e:OnChanged(v)\n"
"    elseif k == 'OnSubmit' then e:OnSubmit(v)\n"
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

static void load_mod_script(lua_State *L, const WlMod *m, int index) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s" WL_SEP "scripts" WL_SEP "main.lua", m->dir);
    size_t len = 0;
    char *src = read_whole(path, &len);
    if (!src) return;  // mod has no script (normal)
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
        wl_log("[lua:%s] load error: %s", m->id, lua_tostring(L, -1));
        lua_settop(L, top);
        return;
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
    if (rc != LUA_OK) wl_log("[lua:%s] error: %s", m->id, lua_tostring(L, -1));
    lua_settop(L, top);
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
    static const char *const removed[] = {"dofile", "loadfile", "load", "loadstring", "require", NULL};
    for (int i = 0; removed[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, removed[i]);
    }
    harden_libs(L);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);

    g_L = L;
    int ran = 0;
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        load_mod_script(L, m, i);
        ran++;
    }
    wl_log("[lua] runtime ready (%d enabled mod(s) checked)", ran);
}

// Calls all handlers of `name` in registration (= mod) order. A handler that
// errors is removed; the others keep running.
static void dispatch(const char *name, int has_arg, int arg) {
    lua_State *L = g_L;
    if (!L || !name) return;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, WL_HANDLERS_KEY);
    lua_getfield(L, -1, name);
    if (!lua_istable(L, -1)) { lua_settop(L, top); return; }
    int list = lua_gettop(L);
    lua_Integer n = (lua_Integer)lua_rawlen(L, list);  // handlers added during dispatch run next time
    int dead = 0;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, list, i);
        int entry = lua_gettop(L);
        lua_getfield(L, entry, "mod");
        char mod[64];
        snprintf(mod, sizeof mod, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
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
    if (dead) {  // compact the list, dropping disabled handlers
        lua_Integer total = (lua_Integer)lua_rawlen(L, list), w = 1;
        for (lua_Integer i = 1; i <= total; i++) {
            lua_rawgeti(L, list, i);
            lua_getfield(L, -1, "dead");
            int is_dead = lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (!is_dead) { lua_rawseti(L, list, w++); }
            else lua_pop(L, 1);
        }
        for (lua_Integer i = w; i <= total; i++) { lua_pushnil(L); lua_rawseti(L, list, i); }
    }
    lua_settop(L, top);
}

void wl_script_event(const char *name) { dispatch(name, 0, 0); }
void wl_script_event_int(const char *name, int arg) { dispatch(name, 1, arg); }
// Mod building Lua button pressed (buildpage.c): handlers of Game.OnCommand(name, fn).
void wl_script_command(const char *name, int unit_id) {
    char ev[96];
    snprintf(ev, sizeof ev, "cmd:%s", name ? name : "");
    dispatch(ev, 1, unit_id);
}
