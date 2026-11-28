// Multiplayer lobby mod compatibility check.
//
// Players in a multiplayer match must run the same Wololoader version and
// enabled mods or the simulation goes out of sync (non-dat changes such as Lua
// scripts and Base features are not covered by the game's dat CRC check).
// In the multiplayer lobby (GameSetupScreen), we broadcast a short tagged chat
// line (`[WL] <version> <8-hex-checksum>`) via the game's lobby chat send path
// and locally report each player's status in the lobby chat history.
//
// Verified in AoK HD.exe 5.8 disassembly (2026-11-24, re/FINDINGS.md):
// - 0x588030 CommHandler::send_lobby_chat(const char *text): __thiscall(comm),
//   ret 4 (0x588089), prologue 55 8b ec 83 ec 10. Called by GameSetupScreen
//   chat submit at 0x657f35 with comm = [screen+0xbd8]. Sets all 9 recipient
//   flags to 1, reads local slot via 0x5983c0 (which calls 0x5836e0), and in
//   lobby state (role 5, 0x583f10 == 0) broadcasts packet 0x43 via 0x574920.
//   IMPORTANT: 0x5983c0 indexes 0xaf1af8 + (local_pid - 1)*0x28 without a
//   bounds check, so never call 0x588030 before 0x5836e0 returns 1..8.
// - 0x5844a0 CommHandler::on_chat_packet(peer_wrapper, pkt): __thiscall(comm),
//   ret 8 (0x584643), prologue 55 8b ec 6a ff (5 stolen bytes). Registered for
//   packet 0x43 at 0x579830. pkt+0x08 = int sender_pid (1..8), pkt+0x0c+pid =
//   recipient byte, pkt+0x15 = host_only flag, pkt+0x18 = const char *text.
// - 0x5836e0 CommHandler::get_local_player_id(int *out_pid): __thiscall(comm),
//   ret 4 (0x583700/0x583739/0x583745), prologue 55 8b ec 53 56 57. Writes 1..8
//   or *(int*)0xaad300 (-1) if local UID is not yet in mPlayerSlots.
// - 0x8037b0 ChatHistory::add_message(sender, text, player, type, a5, a6):
//   __thiscall(*(void**)0xaeefb4), ret 0x18 (0x804033), prologue 55 8b ec 6a ff.
//   GameSetupScreen registers a listener on [0xaeefb4] (0x648081..0x6480c4)
//   that forwards lines to GameSetupScreen::add_chat_line (0x654ba0).
// - 0x656c60 GameSetupScreen::action: __thiscall(screen), ret (0x656da7),
//   prologue 55 8b ec 6a ff (5 stolen bytes). Vtable 0x9d0d10 slot 17 (+0x44);
//   calls 0x65fe40 every frame to sync player slots.
// - 0x64a540 GameSetupScreen::~GameSetupScreen: __thiscall(screen, BYTE flags),
//   ret 4 (0x64a563), prologue 55 8b ec 56 8b f1 (6 stolen bytes), returns this.
// - 0x657da0 GameSetupScreen::submit_chat: __thiscall(screen), ret (0x657f5a),
//   prologue 55 8b ec 83 ec 14 (6 stolen bytes). Reads edit box [screen+0x650]
//   via 0x4874f0 (__thiscall, ret, const char*) and clears via 0x4879d0
//   (__thiscall, ret 4) with empty string at 0x9ae6b0.
// - GameSettings slots: settings = [comm+0xd54], isMultiPlayer = [settings+0x408]
//   (1 = MP). Slots 0..7 at settings + 0xb0 + slot*0x68:
//     +0x0c: 12-byte string { char *ptr; size_t len; size_t cap; } (verified 0x433210, 0x597dab)
//     +0x20: uint64_t player UID (verified 0x58370b..0x583720)
//     +0x54: int status (2 or 3 = human player per 0x598910; 6 = spectator per 0x597bb5)
#include "wololoader.h"
#include "modsettings.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_SLOTS          8
#define SEND_DELAY_MS      1000
#define SEND_COOLDOWN_MS   2000
#define MISSING_TIMEOUT_MS 6000
#define MISSING_AFTER_SEND 4000

typedef void (__thiscall *SendLobbyChat_t)(void *comm, const char *text);
typedef void (__thiscall *ChatRecv_t)(void *comm, void *peer_wrapper, void *pkt);
typedef int *(__thiscall *GetLocalPid_t)(void *comm, int *out_pid);
typedef int (__thiscall *ChatAdd_t)(void *hist, const char *sender, const char *text,
                                    int player, int type, int a5, int a6);
typedef void (__thiscall *LobbyAction_t)(void *screen);
typedef void *(__thiscall *LobbyDtor_t)(void *screen, BYTE flags);
typedef void (__thiscall *ChatSubmit_t)(void *screen);
typedef const char *(__thiscall *EditGetText_t)(void *edit);
typedef void (__thiscall *EditSetText_t)(void *edit, const char *text);

typedef struct {
    int occupied;
    unsigned long long uid;
    DWORD first_seen_ms;
    int got_wl;
    int reported;        // 0 = none, 1 = same, 2 = different
    char theirs_hex[16];
    char theirs_ver[32];
    int warned_missing;
    char name[128];
} SlotTrack;

static ChatRecv_t o_chat_recv;
static LobbyAction_t o_lobby_action;
static LobbyDtor_t o_lobby_dtor;
static ChatSubmit_t o_chat_submit;

static BOOL g_installed;
static char g_our_hex[9];
static void *g_lobby_screen;
static int g_sent_once;
static DWORD g_last_send_ms;
static DWORD g_pending_send_at;
static SlotTrack g_slots[MAX_SLOTS];

static void reset_lobby_state(void *screen) {
    g_lobby_screen = screen;
    g_sent_once = 0;
    g_last_send_ms = 0;
    g_pending_send_at = 0;
    memset(g_slots, 0, sizeof g_slots);
}

static void *get_mp_comm(void *screen, void **out_settings) {
    if (!screen || *(void **)0xaf3d90 != NULL) return NULL;
    void *comm = *(void **)((char *)screen + 0xbd8);
    if (!comm) return NULL;
    void *settings = *(void **)((char *)comm + 0xd54);
    if (!settings || *(int *)((char *)settings + 0x408) != 1) return NULL;
    if (*(int *)((char *)comm + 0xd0) != 5) return NULL;
    if (out_settings) *out_settings = settings;
    return comm;
}

static int get_local_pid(void *comm) {
    int pid = -1;
    ((GetLocalPid_t)0x5836e0)(comm, &pid);
    return (pid >= 1 && pid <= MAX_SLOTS) ? pid : 0;
}

static int read_slot_name(void *settings, int slot, char *out, size_t cap) {
    if (settings && slot >= 0 && slot < MAX_SLOTS) {
        char *sp = (char *)settings + 0xb0 + slot * 0x68;
        const char *ptr = *(const char **)(sp + 0x0c);
        size_t len = *(size_t *)(sp + 0x10);
        if (ptr && len > 0 && !IsBadReadPtr(ptr, 1) && ptr[0] != '\0') {
            lstrcpynA(out, ptr, (int)cap);
            return 1;
        }
    }
    snprintf(out, cap, "Player %d", slot + 1);
    return 0;
}

static void post_local_chat(const char *msg) {
    void *hist = *(void **)0xaeefb4;
    if (!hist) return;
    ((ChatAdd_t)0x8037b0)(hist, NULL, msg, *(int *)0xaad300, 0, 0, 0);
}

static void report_slot_match(int slot) {
    const SlotTrack *s = &g_slots[slot];
    char msg[256];
    if (s->reported == 1) {
        snprintf(msg, sizeof msg, "Wololoader: %s has the same mods", s->name);
    } else {
        snprintf(msg, sizeof msg,
                 "Wololoader: %s has DIFFERENT mods (theirs %s, yours %s)",
                 s->name, s->theirs_hex, g_our_hex);
    }
    post_local_chat(msg);
}

// ---- Mod settings sync ---------------------------------------------------------------------
// Different mod settings make different simulations, so the lobby needs the same values on every
// machine. The host (0x656fa0 GameSetupScreen::is_mp_host, see gamemodes.c) sends each enabled
// mod's effective settings as hidden chat lines "[WLS] <mod id> <part>/<parts> <json chunk>";
// the others use them for this session (modsettings.c wl_settings_session_set: their own files
// are not changed), refill MOD.settings, reapply the Base features and send their [WL] tag again,
// so the checksum (which uses the effective values, autobuild.c) matches. Back at the main menu
// (menu.c) the player's own values return.
#define WLS_CHUNK 96
static void schedule_send(DWORD now, DWORD delay_ms);
static int is_lobby_host(void) {
    return g_lobby_screen && ((BYTE (__thiscall *)(void *))0x656fa0)(g_lobby_screen) != 0;
}

static void send_settings(void *comm) {
    char json[2048], line[256];
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        int len = wl_settings_compact(m, json, sizeof json);
        if (len <= 0) continue;
        int parts = (len + WLS_CHUNK - 1) / WLS_CHUNK;
        for (int p = 0; p < parts; p++) {
            // "|" before the chunk: the chunk may start with a space (a string value)
            snprintf(line, sizeof line, "[WLS] %s %d/%d|%.*s", m->id, p + 1, parts, WLS_CHUNK, json + p * WLS_CHUNK);
            ((SendLobbyChat_t)0x588030)(comm, line);
        }
        wl_log("mpcheck: sent settings of %s (%d part(s))", m->id, parts);
    }
}

// Receiving: chunks are collected per mod; a complete set is applied.
static struct { char id[64]; int parts; int got; char json[2048]; } g_rx[16];
static int g_settings_applied;
static int g_resends;          // host: settings sent again for a different checksum (at most 3 per lobby)

static void apply_host_settings(const char *id, const char *json) {
    const WlMod *mod = NULL;
    for (int i = 0; i < g_wl.listed_count; i++)
        if (strcmp(g_wl.mods[i].id, id) == 0) mod = &g_wl.mods[i];
    if (!mod) return;
    char mine[2048];
    wl_settings_compact(mod, mine, sizeof mine);
    if (strcmp(mine, json) == 0) return;           // already the same
    wl_settings_session_set(id, json);
    wl_script_refresh_settings();
    if (_stricmp(id, "WololoaderBase") == 0) wl_basemod_reload();
    wl_content_checksum_invalidate();
    wl_content_checksum(g_our_hex);
    g_settings_applied = 1;
    char msg[200];
    snprintf(msg, sizeof msg, "Wololoader: using the host's settings for %s", mod->name);
    post_local_chat(msg);
    wl_log("mpcheck: host settings for %s: %s (checksum now %s)", id, json, g_our_hex);
    schedule_send(GetTickCount(), 300);              // tell the others our new checksum
}

static int handle_settings_line(const char *text) {
    if (!text || strncmp(text, "[WLS] ", 6) != 0) return 0;
    if (is_lobby_host()) return 1;                    // the host keeps its own values
    char id[64]; int part = 0, parts = 0;
    const char *bar = strchr(text + 6, '|');
    if (!bar || sscanf(text + 6, "%63s %d/%d", id, &part, &parts) < 3) return 1;
    if (parts < 1 || parts > 2048 / WLS_CHUNK + 1 || part < 1 || part > parts) return 1;
    int pos = (int)(bar + 1 - (text + 6));
    int k, freek = -1;
    for (k = 0; k < 16; k++) {
        if (g_rx[k].id[0] && strcmp(g_rx[k].id, id) == 0) break;
        if (!g_rx[k].id[0] && freek < 0) freek = k;
    }
    if (k == 16) { if (freek < 0) return 1; k = freek; memset(&g_rx[k], 0, sizeof g_rx[k]); lstrcpynA(g_rx[k].id, id, sizeof g_rx[k].id); }
    if (part == 1 || g_rx[k].parts != parts) { g_rx[k].parts = parts; g_rx[k].got = 0; g_rx[k].json[0] = 0; }
    if (part != g_rx[k].got + 1) return 1;            // out of order: wait for the next round
    const char *chunk = text + 6 + pos;
    size_t cur = strlen(g_rx[k].json), add = strlen(chunk);
    if (cur + add + 1 > sizeof g_rx[k].json) return 1;
    memcpy(g_rx[k].json + cur, chunk, add + 1);
    g_rx[k].got = part;
    if (part == parts) { apply_host_settings(id, g_rx[k].json); g_rx[k].id[0] = 0; }
    return 1;
}

static void send_wl_tag(void *comm, DWORD now) {
    if (is_lobby_host()) send_settings(comm);
    char tag[64];
    snprintf(tag, sizeof tag, "[WL] %s %s", WOLOLOADER_VERSION, g_our_hex);
    ((SendLobbyChat_t)0x588030)(comm, tag);
    g_last_send_ms = now;
    g_sent_once = 1;
    g_pending_send_at = 0;
    wl_log("mpcheck: sent '%s'", tag);
}

static void schedule_send(DWORD now, DWORD delay_ms) {
    DWORD target = now + delay_ms;
    if (g_last_send_ms != 0 && (LONG)(target - (g_last_send_ms + SEND_COOLDOWN_MS)) < 0)
        target = g_last_send_ms + SEND_COOLDOWN_MS;
    if (g_pending_send_at == 0 || (LONG)(target - g_pending_send_at) < 0)
        g_pending_send_at = target;
}

static int parse_wl_tag(const char *text, char ver_out[32], char hex_out[9]) {
    if (!text || strncmp(text, "[WL] ", 5) != 0) return 0;
    const char *p = text + 5;
    while (*p == ' ') p++;
    size_t vi = 0;
    while (*p && !isspace((unsigned char)*p) && vi + 1 < 32) ver_out[vi++] = *p++;
    ver_out[vi] = '\0';
    if (vi == 0) return 0;
    while (*p == ' ') p++;
    for (int i = 0; i < 8; i++) {
        if (!isxdigit((unsigned char)p[i])) return 0;
        hex_out[i] = (char)tolower((unsigned char)p[i]);
    }
    hex_out[8] = '\0';
    return p[8] == '\0' || isspace((unsigned char)p[8]);
}

void wl_mpcheck_on_lobby_enter(void *screen) {
    if (!g_installed) return;
    reset_lobby_state(screen);
    if (!screen) return;
    // A previous lobby's host settings end here; this lobby's host sends its own on join.
    memset(g_rx, 0, sizeof g_rx);
    g_resends = 0;
    if (wl_settings_session_clear()) {
        wl_script_refresh_settings();
        wl_basemod_reload();
        wl_content_checksum_invalidate();
    }
    wl_content_checksum(g_our_hex);
    wl_log("mpcheck: entered MP lobby (checksum %s)", g_our_hex);
}

static void *__fastcall hook_lobby_dtor(void *self, void *edx, BYTE flags) {
    (void)edx;
    if (self == g_lobby_screen) reset_lobby_state(NULL);
    return o_lobby_dtor(self, flags);
}

static void __fastcall hook_lobby_action(void *self, void *edx) {
    (void)edx;
    o_lobby_action(self);
    if (self != g_lobby_screen) return;

    void *settings = NULL;
    void *comm = get_mp_comm(self, &settings);
    if (!comm) return;
    int local_pid = get_local_pid(comm);
    if (!local_pid) return;
    int local_slot = local_pid - 1;

    DWORD now = GetTickCount();
    int other_humans = 0;

    for (int i = 0; i < MAX_SLOTS; i++) {
        char *sp = (char *)settings + 0xb0 + i * 0x68;
        int status = *(int *)(sp + 0x54);
        int is_human = (status == 2 || status == 3 || status == 6);
        unsigned long long uid = *(unsigned long long *)(sp + 0x20);
        char name[128];
        int has_name = (i != local_slot) && is_human && read_slot_name(settings, i, name, sizeof name);

        if (!has_name) {
            if (g_slots[i].occupied) memset(&g_slots[i], 0, sizeof g_slots[i]);
            continue;
        }
        other_humans++;
        if (!g_slots[i].occupied || g_slots[i].uid != uid) {
            memset(&g_slots[i], 0, sizeof g_slots[i]);
            g_slots[i].occupied = 1;
            g_slots[i].uid = uid;
            g_slots[i].first_seen_ms = now;
            lstrcpynA(g_slots[i].name, name, sizeof g_slots[i].name);
            schedule_send(now, SEND_DELAY_MS);
            wl_log("mpcheck: player joined slot %d (%s)", i, name);
        } else {
            lstrcpynA(g_slots[i].name, name, sizeof g_slots[i].name);
        }
    }

    if (g_pending_send_at != 0 && (LONG)(now - g_pending_send_at) >= 0) {
        if (other_humans > 0) send_wl_tag(comm, now);
        else g_pending_send_at = 0;
    }

    if (!g_sent_once || g_pending_send_at != 0) return;

    for (int i = 0; i < MAX_SLOTS; i++) {
        SlotTrack *s = &g_slots[i];
        if (!s->occupied || s->got_wl || s->warned_missing) continue;
        if ((LONG)(now - (s->first_seen_ms + MISSING_TIMEOUT_MS)) < 0) continue;
        if ((LONG)(now - (g_last_send_ms + MISSING_AFTER_SEND)) < 0) continue;
        s->warned_missing = 1;
        char msg[256];
        snprintf(msg, sizeof msg, "Wololoader: %s may not have Wololoader or mods", s->name);
        post_local_chat(msg);
        wl_log("mpcheck: %s", msg);
    }
}

// The tags are for Wololoader, not for people: ChatHistory::add_message 0x8037b0
// (__thiscall(hist, sender, text, player, type, a5, a6), ret 0x18, prologue 55 8b ec 6a ff)
// drops lines that start with "[WL] " or "[WLS] " (other players' tags, and our own if the
// game echoes what we send).
static ChatAdd_t o_chat_add;
static int __fastcall hook_chat_add(void *hist, void *edx, const char *sender, const char *text,
                                    int player, int type, int a5, int a6) {
    (void)edx;
    if (text && !IsBadReadPtr(text, 6) && (strncmp(text, "[WL] ", 5) == 0 || strncmp(text, "[WLS] ", 6) == 0))
        return 0;
    return o_chat_add(hist, sender, text, player, type, a5, a6);
}

static void __fastcall hook_chat_recv(void *comm, void *edx, void *peer_wrapper, void *pkt) {
    (void)edx;
    const char *t0 = pkt ? *(const char **)((char *)pkt + 0x18) : NULL;
    if (t0 && !IsBadReadPtr(t0, 6) && strncmp(t0, "[WLS] ", 6) == 0) {   // settings sync: not shown in chat
        if (g_lobby_screen && comm && get_mp_comm(g_lobby_screen, NULL) == comm) handle_settings_line(t0);
        return;
    }
    o_chat_recv(comm, peer_wrapper, pkt);
    if (!g_lobby_screen || !comm || !pkt) return;

    void *settings = NULL;
    if (get_mp_comm(g_lobby_screen, &settings) != comm) return;

    int local_pid = get_local_pid(comm);
    if (!local_pid) return;
    if (*((BYTE *)pkt + 0x15) != 0 && *(int *)((char *)comm + 0xd0) != 5) return;
    if (*((BYTE *)pkt + 0x0c + local_pid) == 0) return;

    int sender_pid = *(int *)((char *)pkt + 8);
    if (sender_pid < 1 || sender_pid > MAX_SLOTS || sender_pid == local_pid) return;

    const char *text = *(const char **)((char *)pkt + 0x18);
    char theirs_ver[32], theirs_hex[9];
    if (!parse_wl_tag(text, theirs_ver, theirs_hex)) return;

    int slot = sender_pid - 1;
    char *sp = (char *)settings + 0xb0 + slot * 0x68;
    unsigned long long uid = *(unsigned long long *)(sp + 0x20);
    SlotTrack *s = &g_slots[slot];
    if (!s->occupied || s->uid != uid) {
        memset(s, 0, sizeof *s);
        s->occupied = 1;
        s->uid = uid;
        s->first_seen_ms = GetTickCount();
    }
    read_slot_name(settings, slot, s->name, sizeof s->name);
    s->got_wl = 1;

    int same = (strcmp(theirs_ver, WOLOLOADER_VERSION) == 0 && strcmp(theirs_hex, g_our_hex) == 0);
    int new_kind = same ? 1 : 2;
    int changed = (s->reported != new_kind) || (strcmp(s->theirs_hex, theirs_hex) != 0) ||
                  (strcmp(s->theirs_ver, theirs_ver) != 0);
    s->reported = new_kind;
    lstrcpynA(s->theirs_hex, theirs_hex, sizeof s->theirs_hex);
    lstrcpynA(s->theirs_ver, theirs_ver, sizeof s->theirs_ver);
    // The host answers a different checksum by sending its settings again (the joiner's lobby
    // may not have been ready for the first ones).
    if (!same && is_lobby_host() && g_resends < 3) { g_resends++; schedule_send(GetTickCount(), 500); }
    if (changed) {
        report_slot_match(slot);
        wl_log("mpcheck: slot %d (%s) %s (theirs %s %s, ours %s %s)",
               slot, s->name, same ? "match" : "DIFF",
               theirs_ver, theirs_hex, WOLOLOADER_VERSION, g_our_hex);
    }
}

static int is_wl_command(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    const char *cmd = NULL;
    if (_strnicmp(s, "/wlcheck", 8) == 0) cmd = s + 8;
    else if (_strnicmp(s, "/wl", 3) == 0) cmd = s + 3;
    if (!cmd) return 0;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    return *cmd == '\0';
}

static void __fastcall hook_chat_submit(void *self, void *edx) {
    (void)edx;
    if (self == g_lobby_screen) {
        void *edit = *(void **)((char *)self + 0x650);
        const char *txt = edit ? ((EditGetText_t)0x4874f0)(edit) : NULL;
        if (is_wl_command(txt)) {
            ((EditSetText_t)0x4879d0)(edit, (const char *)0x9ae6b0);
            void *settings = NULL;
            void *comm = get_mp_comm(self, &settings);
            if (comm && get_local_pid(comm)) {
                DWORD now = GetTickCount();
                send_wl_tag(comm, now);
                for (int i = 0; i < MAX_SLOTS; i++) {
                    if (!g_slots[i].occupied) continue;
                    if (g_slots[i].got_wl) report_slot_match(i);
                    else g_slots[i].warned_missing = 0;
                }
            }
            return;
        }
    }
    o_chat_submit(self);
}

void wl_install_mp_check(void) {
    static const BYTE b_recv[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_action[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    static const BYTE b_dtor[]   = { 0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1 };
    static const BYTE b_submit[] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x14 };

    if (memcmp((const void *)0x5844a0, b_recv, sizeof b_recv) != 0 ||
        memcmp((const void *)0x656c60, b_action, sizeof b_action) != 0 ||
        memcmp((const void *)0x64a540, b_dtor, sizeof b_dtor) != 0 ||
        memcmp((const void *)0x657da0, b_submit, sizeof b_submit) != 0) {
        wl_log("mpcheck: unexpected exe bytes, lobby mod check disabled");
        return;
    }

    o_chat_recv   = (ChatRecv_t)wl_hook(0x5844a0, hook_chat_recv, sizeof b_recv);
    o_lobby_action = (LobbyAction_t)wl_hook(0x656c60, hook_lobby_action, sizeof b_action);
    o_lobby_dtor  = (LobbyDtor_t)wl_hook(0x64a540, hook_lobby_dtor, sizeof b_dtor);
    o_chat_submit = (ChatSubmit_t)wl_hook(0x657da0, hook_chat_submit, sizeof b_submit);
    static const BYTE b_add[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    if (memcmp((const void *)0x8037b0, b_add, sizeof b_add) == 0)
        o_chat_add = (ChatAdd_t)wl_hook(0x8037b0, hook_chat_add, sizeof b_add);

    if (!o_chat_recv || !o_lobby_action || !o_lobby_dtor || !o_chat_submit) {
        wl_log("mpcheck: one or more hooks failed");
        return;
    }
    g_installed = 1;
    wl_log("mpcheck: installed");
}
