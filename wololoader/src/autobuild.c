// Automatic rebuild of the merged dat at game start.
// A fingerprint of everything that affects the build (loader version, the
// original dat's size/time, every mod's mod.json and civs/*.json) is stored in
// cache\build.stamp. When it differs, datbuild.c regenerates
// cache\empires2_x2_p1.dat and cache\civs.txt before the game reads them.
// Runs lazily from civs.c (not DllMain): file IO and our own code, no LoadLibrary of new DLLs
// (zlibwapi.dll is already loaded as an import of the exe).
#include "wololoader.h"
#include "modsettings.h"
#include "datbuild.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const WlZlib *wl_game_zlib(void);  // zlib_game.c

char g_wl_build_error[512];        // shown on the Mods screen when non-empty

typedef unsigned long long u64;
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static u64 fnv(u64 h, const void *data, size_t len) {
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}

static u64 fnv_str(u64 h, const char *s) { return fnv(h, s, strlen(s) + 1); }

static u64 fnv_file(u64 h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return fnv_str(h, "<missing>");
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) h = fnv(h, buf, n);
    fclose(f);
    return h;
}

// Hash every file matching `ext` in `dir` (name + content). When `sorted` is
// non-zero, filenames are sorted case-insensitively so two machines produce the
// same checksum regardless of filesystem directory order.
static int cmp_str_ci(const void *a, const void *b) {
    const char *sa = *(const char *const *)a, *sb = *(const char *const *)b;
    int r = _stricmp(sa, sb);
    return r ? r : strcmp(sa, sb);
}

static u64 fnv_dir(u64 h, const char *dir, const char *ext, int sorted) {
    char pattern[MAX_PATH], path[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*.%s", dir, ext);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) return h;
    if (!sorted) {
        do {
            snprintf(path, MAX_PATH, "%s\\%s", dir, fd.cFileName);
            h = fnv_str(h, fd.cFileName);
            h = fnv_file(h, path);
        } while (FindNextFileA(f, &fd));
        FindClose(f);
        return h;
    }
    int cap = 32, n = 0;
    char **names = malloc((size_t)cap * sizeof *names);
    if (!names) { FindClose(f); return h; }
    do {
        if (n == cap) {
            cap *= 2;
            char **tmp = realloc(names, (size_t)cap * sizeof *names);
            if (!tmp) break;
            names = tmp;
        }
        names[n++] = _strdup(fd.cFileName);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    qsort(names, (size_t)n, sizeof *names, cmp_str_ci);
    for (int i = 0; i < n; i++) {
        if (!names[i]) continue;
        snprintf(path, MAX_PATH, "%s\\%s", dir, names[i]);
        h = fnv_str(h, names[i]);
        h = fnv_file(h, path);
        free(names[i]);
    }
    free(names);
    return h;
}

// Hash of the file names in dir (not their contents), sorted when `sorted`.
static int cmp_name(const void *a, const void *b) { return _stricmp(*(char *const *)a, *(char *const *)b); }
static u64 fnv_names(u64 h, const char *dir, int sorted) {
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) return h;
    char **names = NULL;
    size_t n = 0, cap = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            char **nn = realloc(names, cap * sizeof *nn);
            if (!nn) break;
            names = nn;
        }
        names[n++] = _strdup(fd.cFileName);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    if (sorted && n > 1) qsort(names, n, sizeof *names, cmp_name);
    for (size_t i = 0; i < n; i++) { if (names[i]) h = fnv_str(h, names[i]); free(names[i]); }
    free(names);
    return h;
}

// The meaning of a JSON value, not its bytes: formatting, key order and the top-level "enabled"
// flag (rewritten by the Mods screen, wl_set_mod_enabled) do not change it. Object keys are
// sorted so a Mod Hub copy and a hand-edited copy with the same content match.
static int cmp_member(const void *a, const void *b) {
    return strcmp((*(const WlJsonMember *const *)a)->key, (*(const WlJsonMember *const *)b)->key);
}
static u64 fnv_json(u64 h, const WlJsonVal *v, int top) {
    if (!v) return fnv_str(h, "~");
    switch (v->type) {
    case WL_JSON_NULL:   return fnv_str(h, "n");
    case WL_JSON_BOOL:   return fnv_str(h, v->u.bool_val ? "t" : "f");
    case WL_JSON_NUMBER: { char b[48]; snprintf(b, sizeof b, "#%.10g", v->u.num.dval); return fnv_str(h, b); }
    case WL_JSON_STRING: h = fnv_str(h, "s"); return fnv_str(h, v->u.str ? v->u.str : "");
    case WL_JSON_ARRAY: {
        size_t n = wl_json_len(v);
        h = fnv_str(h, "[");
        for (size_t i = 0; i < n; i++) h = fnv_json(h, wl_json_at(v, i), 0);
        return fnv_str(h, "]");
    }
    case WL_JSON_OBJECT: {
        size_t n = v->u.obj.count;
        const WlJsonMember **ms = malloc((n ? n : 1) * sizeof *ms);
        if (!ms) return h;
        for (size_t i = 0; i < n; i++) ms[i] = &v->u.obj.members[i];
        qsort(ms, n, sizeof *ms, cmp_member);
        h = fnv_str(h, "{");
        for (size_t i = 0; i < n; i++) {
            if (top && strcmp(ms[i]->key, "enabled") == 0) continue;
            h = fnv_str(h, ms[i]->key);
            h = fnv_json(h, ms[i]->val, 0);
        }
        free(ms);
        return fnv_str(h, "}");
    }
    }
    return h;
}
static u64 fnv_json_file(u64 h, const char *path) {
    size_t len = 0;
    char *text = wl_read_file(path, &len);
    if (!text) return fnv_str(h, "<missing>");
    char err[64];
    WlJsonVal *v = wl_json_parse(text, err, sizeof err);
    free(text);
    if (!v) return fnv_file(h, path);
    h = fnv_json(h, v, 1);
    wl_json_free(v);
    return h;
}

static u64 hash_mod_files(u64 h, const WlMod *m, int sorted) {
    char path[MAX_PATH];
    h = fnv_str(h, m->id);
    snprintf(path, MAX_PATH, "%s\\mod.json", m->dir);
    // the multiplayer checksum (sorted) compares meaning; the build fingerprint keeps the bytes
    h = sorted ? fnv_json_file(h, path) : fnv_file(h, path);
    snprintf(path, MAX_PATH, "%s\\civs", m->dir);
    h = fnv_dir(h, path, "json", sorted);
    snprintf(path, MAX_PATH, "%s\\icons\\buildings", m->dir);
    h = fnv_dir(h, path, "slp", sorted);
    // Names of the mod's sprites: which ids clash decides the renumbering (wl_slp_remap_compute).
    snprintf(path, MAX_PATH, "%s\\resources\\_common\\drs\\graphics", m->dir);
    h = fnv_names(h, path, sorted);
    return h;
}

static u64 fingerprint(const char *src_dat) {
    u64 h = fnv_str(FNV_OFFSET, WOLOLOADER_VERSION);
    h = fnv_str(h, WL_BUILDER_REV);   // bump WL_BUILDER_REV (datbuild.h) whenever the dat output changes
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExA(src_dat, GetFileExInfoStandard, &a)) {
        h = fnv(h, &a.nFileSizeLow, sizeof a.nFileSizeLow);
        h = fnv(h, &a.ftLastWriteTime, sizeof a.ftLastWriteTime);
    }
    for (int i = 0; i < g_wl.listed_count; i++)
        h = hash_mod_files(h, &g_wl.mods[i], 0);
    return h;
}

static int cmp_mod_id(const void *a, const void *b) {
    const WlMod *ma = *(const WlMod *const *)a, *mb = *(const WlMod *const *)b;
    int r = _stricmp(ma->id, mb->id);
    return r ? r : strcmp(ma->id, mb->id);
}

static char g_checksum_cached[9];

void wl_content_checksum_invalidate(void) {
    g_checksum_cached[0] = 0;
}

unsigned long long wl_autobuild_fingerprint(void) {
    char src[MAX_PATH];
    snprintf(src, MAX_PATH, "%s\\resources\\_common\\dat\\empires2_x2_p1.dat", g_wl.game_dir);
    return fingerprint(src);
}

// 8-hex-char checksum of active multiplayer content: Wololoader version,
// builder revision, and enabled mods (sorted deterministically so filesystem
// order and local file timestamps never cause false mismatches).
void wl_content_checksum(char out[9]) {
    if (!g_checksum_cached[0]) {
        u64 h = fnv_str(FNV_OFFSET, WOLOLOADER_VERSION);
        h = fnv_str(h, WL_BUILDER_REV);
        const WlMod *enabled[WL_MAX_MODS];
        int n = 0;
        for (int i = 0; i < g_wl.listed_count; i++)
            if (g_wl.mods[i].enabled) enabled[n++] = &g_wl.mods[i];
        qsort(enabled, (size_t)n, sizeof *enabled, cmp_mod_id);
        for (int i = 0; i < n; i++) {
            h = hash_mod_files(h, enabled[i], 1);
            // The mod settings change what the scripts do, so they must match too. The effective
            // values count (the lobby host's in a synced multiplayer lobby, see mpcheck.c), not
            // the player's file, so players who took the host's values match.
            char sj[2048];
            wl_settings_compact(enabled[i], sj, sizeof sj);
            h = fnv_str(h, sj);
        }
        unsigned sum = (unsigned)((h ^ (h >> 32)) & 0xffffffffu);
        snprintf(g_checksum_cached, sizeof g_checksum_cached, "%08x", sum);
    }
    memcpy(out, g_checksum_cached, 9);
}

int wl_autobuild_run(void) {
    g_wl_build_error[0] = 0;
    char src[MAX_PATH], cache[MAX_PATH], out_dat[MAX_PATH], out_civs[MAX_PATH], out_strings[MAX_PATH], out_commands[MAX_PATH], stamp_path[MAX_PATH];
    snprintf(src, MAX_PATH, "%s\\resources\\_common\\dat\\empires2_x2_p1.dat", g_wl.game_dir);
    snprintf(cache, MAX_PATH, "%s\\cache", g_wl.root);
    snprintf(out_dat, MAX_PATH, "%s\\empires2_x2_p1.dat", cache);
    snprintf(out_civs, MAX_PATH, "%s\\civs.txt", cache);
    snprintf(out_strings, MAX_PATH, "%s\\strings.txt", cache);
    snprintf(out_commands, MAX_PATH, "%s\\commands.txt", cache);
    snprintf(stamp_path, MAX_PATH, "%s\\build.stamp", cache);
    CreateDirectoryA(cache, NULL);

    char want[32], have[32] = "";
    snprintf(want, sizeof want, "%016llx", fingerprint(src));
    FILE *f = fopen(stamp_path, "rb");
    if (f) { if (!fgets(have, sizeof have, f)) have[0] = 0; fclose(f); }
    have[strcspn(have, "\r\n")] = 0;
    if (strcmp(want, have) == 0 && GetFileAttributesA(out_dat) != INVALID_FILE_ATTRIBUTES) {
        wl_log("build: cache up to date (%s)", want);
        return 0;
    }

    wl_log("build: mods changed (%s -> %s), rebuilding...", have[0] ? have : "none", want);
    wl_loadscreen_status("Building mods...");
    DWORD t0 = GetTickCount();
    char mods_dir[MAX_PATH], err[sizeof g_wl_build_error] = "";
    snprintf(mods_dir, MAX_PATH, "%s\\mods", g_wl.root);
    int civs = wl_dat_build(wl_game_zlib(), src, mods_dir, out_dat, out_civs, out_strings, out_commands, err, sizeof err);
    if (civs < 0) {
        // A broken mod must not break the game: drop the cache so the original
        // dat is used, and show the error on the Mods screen.
        lstrcpynA(g_wl_build_error, err, sizeof g_wl_build_error);
        DeleteFileA(out_dat);
        DeleteFileA(out_civs);
        DeleteFileA(out_strings);
        DeleteFileA(out_commands);
        char out_extra[MAX_PATH];
        snprintf(out_extra, sizeof out_extra, "%s\\resources.txt", cache);
        DeleteFileA(out_extra);
        snprintf(out_extra, sizeof out_extra, "%s\\gather.txt", cache);
        DeleteFileA(out_extra);
        DeleteFileA(stamp_path);
        wl_log("build: FAILED: %s (using the original dat)", err);
        return -1;
    }
    f = fopen(stamp_path, "wb");
    if (f) { fputs(want, f); fclose(f); }
    wl_log("build: ok, %d custom civ(s), %lu ms", civs, GetTickCount() - t0);
    wl_loadscreen_status("Loading game data...");
    return 1;
}

void wl_autobuild(void) {
    (void)wl_autobuild_run();
}
