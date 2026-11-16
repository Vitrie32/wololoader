// Automatic rebuild of the merged dat at game start.
// A fingerprint of everything that affects the build (loader version, the
// original dat's size/time, every mod's mod.json and civs/*.json) is stored in
// cache\build.stamp. When it differs, datbuild.c regenerates
// cache\empires2_x2_p1.dat and cache\civs.txt before the game reads them.
// Runs lazily from civs.c (not DllMain): file IO and our own code, no LoadLibrary of new DLLs
// (zlibwapi.dll is already loaded as an import of the exe).
#include "wololoader.h"
#include "datbuild.h"
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

// Hash every *.json in `dir` (name + content). FindFirstFile order is stable
// for an unchanged folder, which is all a fingerprint needs.
static u64 fnv_json_dir(u64 h, const char *dir) {
    char pattern[MAX_PATH], path[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*.json", dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) return h;
    do {
        snprintf(path, MAX_PATH, "%s\\%s", dir, fd.cFileName);
        h = fnv_str(h, fd.cFileName);
        h = fnv_file(h, path);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
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
    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        char path[MAX_PATH];
        h = fnv_str(h, m->id);
        snprintf(path, MAX_PATH, "%s\\mod.json", m->dir);
        h = fnv_file(h, path);
        snprintf(path, MAX_PATH, "%s\\civs", m->dir);
        h = fnv_json_dir(h, path);
    }
    return h;
}

void wl_autobuild(void) {
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
        return;
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
        DeleteFileA(stamp_path);
        wl_log("build: FAILED: %s (using the original dat)", err);
        return;
    }
    f = fopen(stamp_path, "wb");
    if (f) { fputs(want, f); fclose(f); }
    wl_log("build: ok, %d custom civ(s), %lu ms", civs, GetTickCount() - t0);
    wl_loadscreen_status("Loading game data...");
}
