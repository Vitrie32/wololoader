// Level 2 mod hot reload at the Main Menu (World == NULL).
//
// AoE2 HD 5.8 does not keep empires2_x2_p1.dat loaded at the Main Menu:
// Game::create_and_init_world (0x62e5d0) loads the dat fresh for every match,
// save, editor, campaign, or Tech Tree, and Game::destroy_world (0x62e9f0)
// frees it on return. The TShape SLP cache (0xada0f8) is a weak intrusive
// RB-tree that evicts unit/icon SLPs when GameScreen and World are destroyed.
//
// Hot reload therefore only needs to run while at the Main Menu with
// World == NULL ([*(void**)0xaf2c58 + 0xc8] == 0):
//   1. Rescan mods, rebuild file/asset index, merged icon sheets, dat cache,
//      civ/command tables, and invalidate MP content checksum.
//   2. Zero the cached dat CRC32 at GameDataSet+0x10 on all GameDataSet objects
//      (captured via a small hook on GameDataSet::ctor 0x5d5890).
//   3. Reconcile the game's civ registry (DlcPackage+0x98, 0xaf25ec, 0xaf25e0)
//      and reset any persistent Profile/CommHandler GameSettings player slots
//      pointing at a removed civ to Random (0x20 via 0x607490).
//   4. Reload DRS packages ("graphics", "sounds", "terrain", and "interface"
//      only when a brand-new filename was added/removed in drs\interface\) via
//      ResourceManager (0xada190) unload (0x462610) + load (0x4636f0).
//   5. Restart all Lua mod scripts (wl_script_restart).
//   6. Detect non-reloadable startup switches (Vanilla Mode, WololoaderBase
//      binary patches: popcap_1000, selection_250, borderless, formations_tab)
//      and report "Restart required for: ...".
//
// All game addresses verified in AoK HD.exe 5.8 disassembly (re/FINDINGS.md,
// "Hot reload" section):
// - 0x5d5890: GameDataSet::ctor, __thiscall(this, 13 DWORDs) -> returns this
//   in eax (0x5d5a6b: mov %esi,%eax), ret 0x34 (0x5d5a7c: c2 34 00).
//   Prologue / stolen bytes (5 B): 55 8b ec 6a ff.
//   Cached dat CRC32 lives at [this + 0x10] (verified in GameDataSet::getCrc
//   0x5d5f20 at 0x5d5f4d: mov 0x10(%ebx),%eax; test %eax,%eax; jne 0x5d6069).
// - 0xaf2c58: global Game*; [game + 0xc8] = World*; [game + 0x98] = CommHandler*
//   ([comm + 0xd0] == 5 lobby, 1/2 active MP session).
// - 0xaf3d90: global GameScreen* (NULL outside a match).
// - 0xada4b4: global ScreenManager*; top screen = *(void**)(*(void**)0xada4b4)
//   (via 0x43c0d0: mov (%ecx),%eax; ret). MainMenuScreen vtable = 0x9d66a8.
// - 0xada190: ResourceManager inline static object (vector of 8-byte entries
//   { pkg_name_str_obj, pkg_ptr } at [0xada190]..[0xada194]).
// - 0x432ab0: StrObj::equals, __thiscall(str_obj, const char *s) -> BYTE, ret 4
//   (0x432ad8). Prologue: 55 8b ec 8b 55 08 56 57.
// - 0x462610: ResourceManager::unload_package, __thiscall(0xada190, const char *name),
//   ret 4 (0x462681 / 0x462770). Prologue: 55 8b ec 6a ff 68 18 be 90 00.
// - 0x4636f0: ResourceManager::load_package, __thiscall(0xada190, const char *name, int flag),
//   ret 8 (0x463c6d). Prologue: 55 8b ec 6a ff 68 ee c1 90 00.
#include "wololoader.h"
#include "hotreload_core.h"
#include <stdio.h>
#include <string.h>

#define ADDR_GAME_PTR         0xaf2c58
#define ADDR_GAMESCREEN_PTR   0xaf3d90
#define ADDR_SCREEN_MGR       0xada4b4
#define MAINMENU_VTBL         0x9d66a8
#define GAMESETUP_VTBL        0x9d0d10

#define ADDR_GDS_CTOR         0x5d5890  // GameDataSet::ctor, __thiscall(this, 13 args), ret 0x34
#define ADDR_RESMGR           0xada190  // ResourceManager inline static object
#define ADDR_RESMGR_STREQ     0x432ab0  // __thiscall(str_obj, const char*), ret 4
#define ADDR_RESMGR_UNLOAD    0x462610  // __thiscall(0xada190, const char*), ret 4
#define ADDR_RESMGR_LOAD      0x4636f0  // __thiscall(0xada190, const char*, int), ret 8

typedef void *(__thiscall *GdsCtor_t)(void *self,
    DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
    DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12, DWORD a13);
typedef BYTE (__thiscall *StrEq_t)(void *str_obj, const char *name);
typedef void (__thiscall *ResUnload_t)(void *mgr, const char *name);
typedef BYTE (__thiscall *ResLoad_t)(void *mgr, const char *name, int flag);

static GdsCtor_t g_orig_gds_ctor;

#define MAX_DATASETS 16
static void *g_datasets[MAX_DATASETS];
static int g_dataset_count;

static WlBaseFeatures g_startup_features;
static int g_startup_captured;

static unsigned long long g_base_modtree_fp;
static unsigned long long g_base_autobuild_fp;
static WlDrsState g_base_drs;
static int g_baseline_set;

static char g_last_msg[512];

// ---- GameDataSet ctor hook (records GameDataSet* pointers for CRC reset) ----

static void *__fastcall hook_gds_ctor(void *self, void *edx,
    DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
    DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12, DWORD a13) {
    (void)edx;
    void *r = g_orig_gds_ctor(self, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    if (r) {
        int seen = 0;
        for (int i = 0; i < g_dataset_count; i++)
            if (g_datasets[i] == r) { seen = 1; break; }
        if (!seen && g_dataset_count < MAX_DATASETS) {
            g_datasets[g_dataset_count++] = r;
            wl_log("hotreload: recorded GameDataSet %p (#%d)", r, g_dataset_count);
        }
    }
    return r;
}

static int reset_dataset_crcs(void) {
    int n = 0;
    for (int i = 0; i < g_dataset_count; i++) {
        void *ds = g_datasets[i];
        if (!ds) continue;
        DWORD prev = *(DWORD *)((char *)ds + 0x10);
        *(DWORD *)((char *)ds + 0x10) = 0;
        wl_log("hotreload: zeroed GameDataSet %p cached CRC (was 0x%08lx)", ds, (unsigned long)prev);
        n++;
    }
    return n;
}

// ---- Recursive mod-tree fingerprint (detects any change on disk) ------------

typedef unsigned long long u64;
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static u64 fnv_bytes(u64 h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}

static u64 fnv_str(u64 h, const char *s) {
    return fnv_bytes(h, s, strlen(s) + 1);
}

// Order-independent hash of every file under `dir` (rel path + size + mtime).
static u64 hash_tree_xor(const char *dir, const char *rel, int depth) {
    if (depth > 8) return 0;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    u64 acc = 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH], sub[MAX_PATH];
        snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "\\" : "", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            acc ^= hash_tree_xor(full, sub, depth + 1);
        } else {
            u64 fh = fnv_str(FNV_OFFSET, sub);
            fh = fnv_bytes(fh, &fd.nFileSizeLow, sizeof fd.nFileSizeLow);
            fh = fnv_bytes(fh, &fd.nFileSizeHigh, sizeof fd.nFileSizeHigh);
            fh = fnv_bytes(fh, &fd.ftLastWriteTime, sizeof fd.ftLastWriteTime);
            acc ^= fh;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return acc;
}

static u64 compute_mods_tree_fp(void) {
    char mods_dir[MAX_PATH];
    snprintf(mods_dir, sizeof mods_dir, "%s\\mods", g_wl.root);
    return hash_tree_xor(mods_dir, "", 0);
}

static void read_current_features(WlBaseFeatures *out) {
    memset(out, 0, sizeof *out);
    out->vanilla = wl_vanilla_flag_exists() ? 1 : 0;
    wl_basemod_read_flags(&out->popcap_1000, &out->selection_250,
                          &out->borderless, &out->formations_tab);
}

// ---- Safety check: strictly Main Menu with World == NULL --------------------

int wl_hotreload_can_run(char *reason, size_t cap) {
    if (reason && cap) reason[0] = '\0';
    void *game = *(void **)ADDR_GAME_PTR;
    if (!game) {
        if (reason && cap) snprintf(reason, cap, "Game object not initialized");
        return 0;
    }
    void *world = *(void **)((char *)game + 0xc8);
    if (world != NULL) {
        if (reason && cap) snprintf(reason, cap, "Cannot reload mods while a match or world is active");
        return 0;
    }
    if (*(void **)ADDR_GAMESCREEN_PTR != NULL) {
        if (reason && cap) snprintf(reason, cap, "Cannot reload mods while in a match");
        return 0;
    }
    if (wl_mods_is_readonly_open()) {
        if (reason && cap) snprintf(reason, cap, "Cannot reload mods from a lobby");
        return 0;
    }
    void *comm = *(void **)((char *)game + 0x98);
    if (comm) {
        int cstate = *(int *)((char *)comm + 0xd0);
        // 5 = lobby (mpcheck.c / 0x584c4f), 1/2 = active MP game/launch (0x583f10).
        if (cstate == 1 || cstate == 2 || cstate == 5) {
            if (reason && cap) snprintf(reason, cap, "Cannot reload mods in a lobby or multiplayer session");
            return 0;
        }
    }
    void *mgr = *(void **)ADDR_SCREEN_MGR;
    void *top = mgr ? *(void **)mgr : NULL;
    if (top) {
        DWORD vtbl = *(DWORD *)top;
        if (vtbl == GAMESETUP_VTBL) {
            if (reason && cap) snprintf(reason, cap, "Cannot reload mods from the lobby screen");
            return 0;
        }
        // Only allow from MainMenuScreen (vtable 0x9d66a8) or Wololoader's own
        // Mods / Mod Hub screens (ScreenPanel name at +0x58).
        const char *sname = *(const char **)((char *)top + 0x58);
        int is_menu = (vtbl == MAINMENU_VTBL);
        int is_wl = (sname && !IsBadReadPtr(sname, 1) &&
                     (strcmp(sname, "Wololoader Mods") == 0 ||
                      strcmp(sname, "Wololoader Mod Hub") == 0));
        if (!is_menu && !is_wl) {
            if (reason && cap) snprintf(reason, cap, "Mods can only be reloaded from the Main Menu or Mods screen");
            return 0;
        }
    }
    return 1;
}

// ---- DRS package reload (ResourceManager 0xada190) --------------------------

static int is_drs_package_loaded(const char *name) {
    DWORD *begin = *(DWORD **)ADDR_RESMGR;
    DWORD *end   = *(DWORD **)(ADDR_RESMGR + 4);
    if (!begin || !end || end < begin) return 0;
    for (DWORD *p = begin; p + 2 <= end; p += 2) {
        void *str_obj = (void *)p[0];
        if (str_obj && ((StrEq_t)ADDR_RESMGR_STREQ)(str_obj, name)) return 1;
    }
    return 0;
}

static int reload_changed_drs(const WlDrsState *old_s, const WlDrsState *new_s) {
    static const char *const k_pkgs[4] = { "graphics", "sounds", "terrain", "interface" };
    int want[4] = { 0, 0, 0, 0 };
    if (!wl_hotreload_diff_drs(old_s, new_s, want)) return 0;

    static const BYTE b_streq[]  = { 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08, 0x56, 0x57 };
    static const BYTE b_unload[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x18, 0xbe, 0x90, 0x00 };
    static const BYTE b_load[]   = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xee, 0xc1, 0x90, 0x00 };
    if (memcmp((void *)ADDR_RESMGR_STREQ, b_streq, sizeof b_streq) != 0 ||
        memcmp((void *)ADDR_RESMGR_UNLOAD, b_unload, sizeof b_unload) != 0 ||
        memcmp((void *)ADDR_RESMGR_LOAD, b_load, sizeof b_load) != 0) {
        wl_log("hotreload: DRS reload skipped (unexpected exe bytes)");
        return 0;
    }

    int reloaded = 0;
    for (int i = 0; i < 4; i++) {
        if (!want[i]) continue;
        if (!is_drs_package_loaded(k_pkgs[i])) {
            wl_log("hotreload: DRS '%s' changed but not currently loaded, skipping", k_pkgs[i]);
            continue;
        }
        wl_log("hotreload: reloading DRS package '%s'", k_pkgs[i]);
        ((ResUnload_t)ADDR_RESMGR_UNLOAD)((void *)ADDR_RESMGR, k_pkgs[i]);
        ((ResLoad_t)ADDR_RESMGR_LOAD)((void *)ADDR_RESMGR, k_pkgs[i], 0);
        reloaded++;
    }
    return reloaded;
}

// ---- Public API -------------------------------------------------------------

void wl_install_hotreload(void) {
    // Capture startup feature switches before Vanilla Mode returns so we can
    // always detect changes to Vanilla Mode or WololoaderBase binary patches.
    if (!g_startup_captured) {
        read_current_features(&g_startup_features);
        g_startup_features.vanilla = g_wl_vanilla ? 1 : 0;
        g_startup_captured = 1;
    }
    if (g_wl_vanilla) return;

    // Hook GameDataSet::ctor (0x5d5890) to record the 2 GameDataSet objects
    // constructed at startup (Original & Expansions) so we can zero +0x10 CRC.
    // 55 8b ec 6a ff = push ebp; mov ebp,esp; push -1 (5 bytes, no relocs).
    static const BYTE b_ctor[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x67, 0x6e, 0x93, 0x00 };
    if (memcmp((void *)ADDR_GDS_CTOR, b_ctor, sizeof b_ctor) != 0) {
        wl_log("hotreload: unexpected bytes at 0x5d5890, GameDataSet CRC reset disabled");
        return;
    }
    g_orig_gds_ctor = (GdsCtor_t)wl_hook(ADDR_GDS_CTOR, hook_gds_ctor, 5);
    wl_log("hotreload: GameDataSet::ctor hook %s", g_orig_gds_ctor ? "ok" : "FAILED");
}

void wl_hotreload_mark_baseline(void) {
    g_base_modtree_fp = compute_mods_tree_fp();
    g_base_autobuild_fp = wl_autobuild_fingerprint();
    wl_files_drs_fingerprints(g_base_drs.pkg_fp, &g_base_drs.iface_new_fp);
    g_baseline_set = 1;
}

int wl_hotreload_restart_reason(char *out, size_t cap) {
    WlBaseFeatures cur;
    read_current_features(&cur);
    return wl_hotreload_diff_features(&g_startup_features, &cur, out, cap);
}

int wl_hotreload_mods_changed(void) {
    if (!g_baseline_set) return 0;
    if (wl_vanilla_flag_exists() != g_wl_vanilla) return 1;
    if (compute_mods_tree_fp() != g_base_modtree_fp) return 1;
    if (wl_autobuild_fingerprint() != g_base_autobuild_fp) return 1;
    return 0;
}

const char *wl_hotreload_last_message(void) {
    return g_last_msg;
}

// Returns: 1 = reloaded (or restart-only notice), 0 = no changes needed, -1 = error/refused.
int wl_hotreload_run(int force, char *out_msg, size_t out_cap) {
    char reason[256] = "";
    if (!wl_hotreload_can_run(reason, sizeof reason)) {
        wl_log("hotreload: refused: %s", reason);
        if (out_msg && out_cap) snprintf(out_msg, out_cap, "%s", reason);
        return -1;
    }

    // Vanilla Mode started without mod/file/civ/Lua hooks, or user toggled into
    // Vanilla Mode: always requires a restart.
    if (g_wl_vanilla || wl_vanilla_flag_exists()) {
        char items[256] = "";
        wl_scan_mods();
        wl_basemod_reload();
        if (!wl_hotreload_restart_reason(items, sizeof items))
            snprintf(items, sizeof items, "Vanilla Mode");
        snprintf(g_last_msg, sizeof g_last_msg, "Restart required for: %s", items);
        wl_log("hotreload: %s", g_last_msg);
        char mlbl[256];
        snprintf(mlbl, sizeof mlbl, "Wololoader v%s - %s", WOLOLOADER_VERSION, g_last_msg);
        wl_menu_set_label(mlbl);
        if (out_msg && out_cap) snprintf(out_msg, out_cap, "%s", g_last_msg);
        return 1;
    }

    if (!force && !wl_hotreload_mods_changed()) {
        if (out_msg && out_cap) snprintf(out_msg, out_cap, "No mod changes to reload.");
        return 0;
    }

    DWORD t0 = GetTickCount();
    wl_log("hotreload: starting full main-menu reload (force=%d)...", force);

    // Step 1: Rescan mods & check WololoaderBase binary-patch features.
    wl_scan_mods();
    wl_basemod_reload();
    char restart_items[256] = "";
    int restart_n = wl_hotreload_restart_reason(restart_items, sizeof restart_items);

    // Rebuild asset index, clear old redirects, rebuild merged icons.
    wl_files_clear_redirects();
    wl_files_rebuild_assets();
    wl_icons_build();
    wl_content_checksum_invalidate();

    // Rebuild empires2_x2_p1.dat, civs.txt, strings.txt, commands.txt.
    int build_rc = wl_autobuild_run();

    // Reload Wololoader civ/string/command tables (even if build failed: cache
    // files were deleted so tables reset cleanly to 0 custom civs, keeping the
    // game playable with the original dat).
    int old_ids[WL_HR_MAX_CIVS], new_ids[WL_HR_MAX_CIVS];
    int old_n = 0, new_n = 0;
    int civ_count = wl_civs_reload_tables(old_ids, &old_n, new_ids, &new_n);
    wl_resources_reload();

    WlCivDiff cdiff;
    wl_hotreload_diff_civs(old_ids, old_n, new_ids, new_n, &cdiff);
    wl_log("hotreload: civ diff: +%d added, -%d removed, %d kept",
           cdiff.added_count, cdiff.removed_count, cdiff.kept_count);

    // Step 2: Zero cached dat CRC32 on all GameDataSet objects.
    reset_dataset_crcs();

    // Step 3: Reconcile game civ registry & sanitize Profile/Comm GameSettings.
    int rec_rc = wl_civs_reconcile_registry();

    // Step 4: Reload DRS packages whose mod files changed.
    WlDrsState new_drs;
    wl_files_drs_fingerprints(new_drs.pkg_fp, &new_drs.iface_new_fp);
    int drs_reloaded = reload_changed_drs(&g_base_drs, &new_drs);

    // Step 5: Restart Lua mod scripts.
    wl_script_restart();

    // Update baseline snapshots.
    g_base_modtree_fp = compute_mods_tree_fp();
    g_base_autobuild_fp = wl_autobuild_fingerprint();
    g_base_drs = new_drs;
    g_baseline_set = 1;

    // Step 6: Format status message and update main menu label.
    if (build_rc < 0) {
        wl_hotreload_format_status(0, 0, g_wl.mod_count, NULL, g_wl_build_error,
                                   g_last_msg, sizeof g_last_msg);
        char mlbl[256];
        snprintf(mlbl, sizeof mlbl, "Wololoader v%s - mod build error (see Mods)", WOLOLOADER_VERSION);
        wl_menu_set_label(mlbl);
        wl_log("hotreload: build failed (%lu ms): %s", GetTickCount() - t0, g_wl_build_error);
        if (out_msg && out_cap) snprintf(out_msg, out_cap, "%s", g_last_msg);
        return -1;
    }
    if (rec_rc < 0) {
        wl_hotreload_format_status(0, civ_count, g_wl.mod_count, NULL,
                                   "civ registry reconcile failed",
                                   g_last_msg, sizeof g_last_msg);
        char mlbl[256];
        snprintf(mlbl, sizeof mlbl, "Wololoader v%s - reload error (restart game)", WOLOLOADER_VERSION);
        wl_menu_set_label(mlbl);
        wl_log("hotreload: civ reconcile failed (%lu ms)", GetTickCount() - t0);
        if (out_msg && out_cap) snprintf(out_msg, out_cap, "%s", g_last_msg);
        return -1;
    }

    wl_hotreload_format_status(1, civ_count, g_wl.mod_count,
                               restart_n > 0 ? restart_items : NULL, NULL,
                               g_last_msg, sizeof g_last_msg);

    char mlbl[256];
    if (restart_n > 0) {
        snprintf(mlbl, sizeof mlbl, "Wololoader v%s - %d mod%s loaded (Restart required for: %s)",
                 WOLOLOADER_VERSION, g_wl.mod_count, g_wl.mod_count == 1 ? "" : "s", restart_items);
    } else {
        snprintf(mlbl, sizeof mlbl, "Wololoader v%s - %d mod%s loaded (%d civ%s)",
                 WOLOLOADER_VERSION, g_wl.mod_count, g_wl.mod_count == 1 ? "" : "s",
                 civ_count, civ_count == 1 ? "" : "s");
    }
    wl_menu_set_label(mlbl);

    char chk[9];
    wl_content_checksum(chk);
    wl_log("hotreload: done in %lu ms (%s, drs=%d, checksum=%s)",
           GetTickCount() - t0, g_last_msg, drs_reloaded, chk);
    if (out_msg && out_cap) snprintf(out_msg, out_cap, "%s", g_last_msg);
    return 1;
}

int wl_hotreload_from_lua(char *out_msg, size_t out_cap) {
    return wl_hotreload_run(1, out_msg, out_cap);
}
