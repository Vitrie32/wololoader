// Pure C11 logic for Level 2 mod hot reload (no Windows/game dependencies).
// Shared between src/hotreload.c and the native test tools/test_hotreload.c.
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WL_HR_FIRST_CUSTOM_CIV 35
#define WL_HR_MAX_CIVS         64

// Non-reloadable startup switches (binary patches in main.c + Vanilla Mode).
typedef struct {
    int vanilla;
    int popcap_1000;
    int selection_250;
    int borderless;
    int formations_tab;
} WlBaseFeatures;

// Civ registry diff between previous and new active custom civ ID lists (>= 35).
typedef struct {
    int added[WL_HR_MAX_CIVS];
    int added_count;
    int removed[WL_HR_MAX_CIVS];
    int removed_count;
    int kept[WL_HR_MAX_CIVS];
    int kept_count;
} WlCivDiff;

// Per-package DRS mod file fingerprints (0=graphics, 1=sounds, 2=terrain, 3=interface).
// iface_new_fp hashes only mod files in drs/interface/ that do NOT exist in vanilla
// (overriding an existing vanilla interface SLP works via files.c without unloading).
typedef struct {
    unsigned long long pkg_fp[4];
    unsigned long long iface_new_fp;
} WlDrsState;

static inline int wl_hr_contains(const int *arr, int n, int val) {
    for (int i = 0; i < n; i++) {
        if (arr[i] == val) return 1;
    }
    return 0;
}

static inline void wl_hr_sort_ints(int *arr, int n) {
    for (int i = 1; i < n; i++) {
        int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

// Compute added, removed, and kept custom civ IDs (ignoring IDs < 35 and duplicates).
static inline void wl_hotreload_diff_civs(const int *old_ids, int old_n,
                                          const int *new_ids, int new_n,
                                          WlCivDiff *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < old_n; i++) {
        int id = old_ids[i];
        if (id < WL_HR_FIRST_CUSTOM_CIV) continue;
        if (wl_hr_contains(new_ids, new_n, id)) {
            if (!wl_hr_contains(out->kept, out->kept_count, id) && out->kept_count < WL_HR_MAX_CIVS)
                out->kept[out->kept_count++] = id;
        } else {
            if (!wl_hr_contains(out->removed, out->removed_count, id) && out->removed_count < WL_HR_MAX_CIVS)
                out->removed[out->removed_count++] = id;
        }
    }
    for (int i = 0; i < new_n; i++) {
        int id = new_ids[i];
        if (id < WL_HR_FIRST_CUSTOM_CIV) continue;
        if (!wl_hr_contains(old_ids, old_n, id)) {
            if (!wl_hr_contains(out->added, out->added_count, id) && out->added_count < WL_HR_MAX_CIVS)
                out->added[out->added_count++] = id;
        }
    }
    wl_hr_sort_ints(out->added, out->added_count);
    wl_hr_sort_ints(out->removed, out->removed_count);
    wl_hr_sort_ints(out->kept, out->kept_count);
}

// Compare startup binary-patch features against current on-disk state.
// Writes comma-separated list of changed non-reloadable features to `out_list`
// and returns the number of changed items.
static inline int wl_hotreload_diff_features(const WlBaseFeatures *startup,
                                             const WlBaseFeatures *current,
                                             char *out_list, size_t cap) {
    static const char *const names[5] = {
        "Vanilla Mode", "popcap_1000", "selection_250", "borderless", "formations_tab"
    };
    const int s[5] = {
        startup->vanilla != 0, startup->popcap_1000 != 0, startup->selection_250 != 0,
        startup->borderless != 0, startup->formations_tab != 0
    };
    const int c[5] = {
        current->vanilla != 0, current->popcap_1000 != 0, current->selection_250 != 0,
        current->borderless != 0, current->formations_tab != 0
    };
    if (out_list && cap > 0) out_list[0] = '\0';
    int changed = 0;
    size_t pos = 0;
    for (int i = 0; i < 5; i++) {
        if (s[i] == c[i]) continue;
        if (out_list && cap > 0) {
            int n = snprintf(out_list + pos, cap - pos, "%s%s", changed ? ", " : "", names[i]);
            if (n > 0) {
                pos += (size_t)n;
                if (pos >= cap) pos = cap - 1;
            }
        }
        changed++;
    }
    return changed;
}

// Decide which DRS packages need unload + load:
// - graphics (0), sounds (1), terrain (2): whenever their mod DRS files changed.
// - interface (3): only when a brand-new (non-vanilla) filename was added or removed.
static inline int wl_hotreload_diff_drs(const WlDrsState *old_s, const WlDrsState *new_s,
                                        int out_reload[4]) {
    int any = 0;
    for (int i = 0; i < 3; i++) {
        out_reload[i] = (old_s->pkg_fp[i] != new_s->pkg_fp[i]) ? 1 : 0;
        if (out_reload[i]) any = 1;
    }
    out_reload[3] = (old_s->iface_new_fp != new_s->iface_new_fp) ? 1 : 0;
    if (out_reload[3]) any = 1;
    return any;
}

// Format user-facing status text after a reload attempt.
static inline void wl_hotreload_format_status(int ok, int civ_count, int mod_count,
                                              const char *restart_items,
                                              const char *err_msg,
                                              char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (!ok) {
        if (err_msg && err_msg[0])
            snprintf(out, cap, "Reload failed: %s (restart the game)", err_msg);
        else
            snprintf(out, cap, "Reload failed (restart the game)");
        return;
    }
    if (restart_items && restart_items[0]) {
        snprintf(out, cap, "Mods reloaded (%d civ%s, %d mod%s). Restart required for: %s",
                 civ_count, civ_count == 1 ? "" : "s",
                 mod_count, mod_count == 1 ? "" : "s",
                 restart_items);
    } else {
        snprintf(out, cap, "Mods reloaded (%d civ%s, %d mod%s)",
                 civ_count, civ_count == 1 ? "" : "s",
                 mod_count, mod_count == 1 ? "" : "s");
    }
}
