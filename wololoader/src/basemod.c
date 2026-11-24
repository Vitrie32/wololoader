// "Wololoader Base": Wololoader's own gameplay/UI features shown as a normal mod
// (mods\WololoaderBase\mod.json), so players can switch them off in the Mods
// screen like any mod, or one by one in its "features" object:
//
//   { "name": "Wololoader Base", ..., "features": {
//       "popcap_1000": true, "selection_250": true, "borderless": true,
//       "formations_tab": true } }
//
// Rules: mod folder missing -> every feature on (older installs behave as
// before). Mod disabled ("enabled": false or turned off on the Mods screen) ->
// every feature off. Feature key missing -> on. `make install` creates the
// folder once and never overwrites it.
//
// Read from DllMain after wl_scan_mods (mods.c does the same kind of file and
// JSON reading there; no other DLLs are called).
#include "wololoader.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BASE_ID "WololoaderBase"

static int g_loaded;
static BOOL g_base_enabled = TRUE;
static WlJsonVal *g_features;   // the "features" object's parent document, or NULL

static void load(void) {
    if (g_loaded) return;
    g_loaded = 1;
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (_stricmp(m->id, BASE_ID) != 0) continue;
        g_base_enabled = m->enabled;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\mod.json", m->dir);
        char *text = wl_read_file(path, NULL);
        if (text) {
            char err[128];
            g_features = wl_json_parse(text, err, sizeof err);
            if (!g_features) wl_log("base: mod.json error: %s (all features on)", err);
            free(text);
        }
        wl_log("base: Wololoader Base mod %s", g_base_enabled ? "enabled" : "DISABLED (features off)");
        return;
    }
    wl_log("base: no %s mod folder, all features on", BASE_ID);
}

BOOL wl_feature(const char *name) {
    load();
    if (!g_base_enabled) return FALSE;
    if (!g_features) return TRUE;
    const WlJsonVal *f = wl_json_get(wl_json_get(g_features, "features"), name);
    BOOL on = f ? wl_json_is_true(f) : TRUE;
    if (!on) wl_log("base: feature '%s' off", name);
    return on;
}

// Features that are OFF unless the Base mod sets them to true (new gameplay
// changes that players should opt into, e.g. monk_autoconvert).
BOOL wl_feature_opt(const char *name) {
    load();
    if (!g_base_enabled || !g_features) return FALSE;
    const WlJsonVal *f = wl_json_get(wl_json_get(g_features, "features"), name);
    return f && wl_json_is_true(f);
}

// Re-read WololoaderBase\mod.json from disk after wl_scan_mods() during hot reload.
// Runtime-checked features (e.g. monk_autoconvert, Lua wl.feature()) see the new
// values immediately; binary-patched features are diffed by hotreload.c to report
// whether a restart is needed.
void wl_basemod_reload(void) {
    if (g_features) {
        wl_json_free(g_features);
        g_features = NULL;
    }
    g_base_enabled = TRUE;
    g_loaded = 0;
    load();
}

void wl_basemod_read_flags(int *popcap, int *sel250, int *borderless, int *formtab) {
    load();
    if (popcap)     *popcap     = wl_feature("popcap_1000") ? 1 : 0;
    if (sel250)     *sel250     = wl_feature("selection_250") ? 1 : 0;
    if (borderless) *borderless = wl_feature("borderless") ? 1 : 0;
    if (formtab)    *formtab    = wl_feature("formations_tab") ? 1 : 0;
}
