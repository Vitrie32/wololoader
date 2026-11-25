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
// Since 0.5 the features are mod settings (modsettings.h): mod.json "settings"
// declares them, the player switches them on the Mods screen (Settings), and the
// values live in wololoader\settings\WololoaderBase.json. A mod.json from an
// older install (only "features", which make install never overwrites) gets the
// "settings" array added once, with the "features" values as defaults.
//
// Read from DllMain after wl_scan_mods (mods.c does the same kind of file and
// JSON reading there; no other DLLs are called).
#include "wololoader.h"
#include "json.h"
#include "modsettings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BASE_ID "WololoaderBase"

static int g_loaded;
static BOOL g_base_enabled = TRUE;
static WlJsonVal *g_features;   // the "features" object's parent document, or NULL
static WlSetting g_set[WL_MAX_SETTINGS];
static int g_set_count;

// Same list and defaults as base_mod/mod.json.
static const struct { const char *id, *name; int def; } k_base_settings[] = {
    { "popcap_1000",      "Population cap 1000 (restart)",    1 },
    { "selection_250",    "Select up to 250 units (restart)", 1 },
    { "borderless",       "Borderless window (restart)",      1 },
    { "formations_tab",   "Formations tab (restart)",         1 },
    { "monk_autoconvert", "Monks convert nearby enemies",     0 },
    { "dev_panel",        "Dev Panel and Lua console",        1 },
};

// Adds "settings" to an older mod.json (text inserted before the last '}'),
// keeping every other key. Defaults come from its "features" object.
static void migrate_settings(const char *path, const char *text, const WlJsonVal *doc) {
    const char *end = strrchr(text, '}');
    if (!end) return;
    const WlJsonVal *feats = wl_json_get(doc, "features");
    size_t cap = strlen(text) + 2048, o = 0;
    char *out = malloc(cap);
    if (!out) return;
    const char *last = end;                       // drop whitespace before the closing brace
    while (last > text && (last[-1] == ' ' || last[-1] == '\n' || last[-1] == '\r' || last[-1] == '\t')) last--;
    memcpy(out, text, (size_t)(last - text));
    o = (size_t)(last - text);
    o += snprintf(out + o, cap - o, ",\n  \"settings\": [\n");
    int n = (int)(sizeof k_base_settings / sizeof k_base_settings[0]);
    for (int i = 0; i < n; i++) {
        const WlJsonVal *f = wl_json_get(feats, k_base_settings[i].id);
        int def = f ? wl_json_is_true(f) : k_base_settings[i].def;
        o += snprintf(out + o, cap - o,
                      "    { \"id\": \"%s\", \"name\": \"%s\", \"type\": \"bool\", \"default\": %s }%s\n",
                      k_base_settings[i].id, k_base_settings[i].name, def ? "true" : "false", i + 1 < n ? "," : "");
    }
    o += snprintf(out + o, cap - o, "  ]\n}\n");
    char err[64];
    WlJsonVal *check = wl_json_parse(out, err, sizeof err);   // never write a broken file
    if (check) {
        wl_json_free(check);
        char tmp[MAX_PATH + 8];
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        FILE *fp = fopen(tmp, "wb");
        if (fp) {
            int ok = fwrite(out, 1, o, fp) == o;
            ok = (fclose(fp) == 0) && ok;
            if (ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) wl_log("base: mod.json got its settings list");
            else DeleteFileA(tmp);
        }
    } else {
        wl_log("base: settings migration skipped (%s)", err);
    }
    free(out);
}

// The setting with this id: 1 / 0, or -1 when mod.json declares none.
static int setting_value(const char *name) {
    for (int i = 0; i < g_set_count; i++)
        if (strcmp(g_set[i].id, name) == 0) return g_set[i].on;
    return -1;
}

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
            else if (!wl_json_get(g_features, "settings")) migrate_settings(path, text, g_features);
            free(text);
        }
        g_set_count = wl_settings_load(m, g_set, WL_MAX_SETTINGS);
        wl_log("base: Wololoader Base mod %s", g_base_enabled ? "enabled" : "DISABLED (features off)");
        return;
    }
    wl_log("base: no %s mod folder, all features on", BASE_ID);
}

BOOL wl_feature(const char *name) {
    load();
    if (!g_base_enabled) return FALSE;
    int sv = setting_value(name);
    if (sv >= 0) {
        if (!sv) wl_log("base: feature '%s' off", name);
        return sv ? TRUE : FALSE;
    }
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
    if (!g_base_enabled) return FALSE;
    int sv = setting_value(name);
    if (sv >= 0) return sv ? TRUE : FALSE;
    if (!g_features) return FALSE;
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
    g_set_count = 0;
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
