// Extra icons: mods ship single-frame SLPs in <mod>\icons\<sheet>\<name>.slp (sheet = the game's
// interface SLP id, e.g. 50721 = command buttons; "buildings" = all four building icon sheets
// 50705..50708). At start the frames are appended to a copy of the game's sheet in
// <root>\cache\icons\<sheet>.slp and the game is redirected there (wl_add_redirect).
//
// Base sheet lookup checks resources\_common\drs\gamedata_x2\<sheet>.slp first (HD 5.8 loads
// gamedata_x2\50706.slp with 55 frames 0..54, including Aqueduct/Feitoria/Barricade) and falls
// back to resources\_common\drs\interface\<sheet>.slp (e.g. 50721.slp, 70 frames).
//
// New frames get deterministic indices after the base sheet's frames, in sorted mod-id order
// (strcmp) and sorted file-name order (strcmp) inside each mod — matching build.py and datbuild.c
// so custom building icon indices agree between the builders and the merged SLP sheet.
// wl_icon_index(sheet, name) returns the index (-1 = not found).
//
// The original frames are not decoded (interface sheets use extended commands): only their
// offsets move. SLP 2.0N: 32-byte header (version, frame count at +4, comment), 32-byte frame
// infos (+0 command table offset, +4 outline table offset, +8 palette offset, +12 properties,
// +16 width, +20 height, +24/+28 hotspot), then data; the command table holds one absolute u32
// per row. Appending m frames moves everything after the header by 32*m.
// Runs from DllMain (plain file IO of our own CRT only).
#include "wololoader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ICONS 256
#define MAX_COLLECT 64
static struct { char sheet[12]; char name[64]; int index; } g_icons[MAX_ICONS];
static int g_nicons;

int wl_icon_index(const char *sheet, const char *name) {
    for (int i = 0; i < g_nicons; i++)
        if (!_stricmp(g_icons[i].sheet, sheet) && !_stricmp(g_icons[i].name, name)) return g_icons[i].index;
    return -1;
}

static unsigned char *read_all(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (n > 64 && n < 16 * 1024 * 1024) ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = n;
    return b;
}

#define U32(p) (*(unsigned int *)(p))

// Old frame whose data (and command table) already moved by `delta` in the output buffer:
// add `delta` to its two table offsets and to every row entry of its command table.
static int shift_frame(unsigned char *file, long len, unsigned char *info, long delta) {
    unsigned int cmd = U32(info) + (unsigned int)delta, outl = U32(info + 4) + (unsigned int)delta;
    int h = *(int *)(info + 20);
    if (h < 0 || h > 4096 || cmd + 4u * (unsigned)h > (unsigned long)len) return 0;
    for (int y = 0; y < h; y++) U32(file + cmd + 4 * y) += (unsigned int)delta;
    U32(info) = cmd;
    U32(info + 4) = outl;
    return 1;
}

typedef struct { char name[64]; unsigned char *data; long len; } Frame;

static void merge_sheet(const char *sheet, Frame *fr, int n) {
    char src[MAX_PATH], dst[MAX_PATH];
    long olen = 0;
    // Prefer gamedata_x2 (HD 5.8 building icons 50705..50708 have 55 frames there),
    // falling back to interface (50721.slp).
    snprintf(src, sizeof src, "%s\\resources\\_common\\drs\\gamedata_x2\\%s.slp", g_wl.game_dir, sheet);
    unsigned char *orig = read_all(src, &olen);
    if (!orig) {
        snprintf(src, sizeof src, "%s\\resources\\_common\\drs\\interface\\%s.slp", g_wl.game_dir, sheet);
        orig = read_all(src, &olen);
    }
    if (!orig || memcmp(orig, "2.0N", 4) != 0) { wl_log("icons: cannot read %s", src); free(orig); return; }
    int count = *(int *)(orig + 4);
    long add = 32L * n, data_len = olen - 32 - 32L * count;
    long total = olen + add;
    for (int i = 0; i < n; i++) total += fr[i].len - 64;
    unsigned char *out = calloc(1, (size_t)total);
    if (!out) { free(orig); return; }
    memcpy(out, orig, 32);
    *(int *)(out + 4) = count + n;
    memcpy(out + 32, orig + 32, 32L * count);
    memcpy(out + 32 + 32L * (count + n), orig + 32 + 32L * count, (size_t)data_len);
    int ok = 1;
    for (int i = 0; i < count && ok; i++)              // old frames: everything moved by `add`
        ok = shift_frame(out, total, out + 32 + 32L * i, add);
    long pos = 32 + 32L * (count + n) + data_len;
    for (int i = 0; i < n && ok; i++) {
        unsigned char *info = out + 32 + 32L * (count + i);
        memcpy(info, fr[i].data + 32, 32);                // the frame's own info (1-frame file)
        memcpy(out + pos, fr[i].data + 64, (size_t)(fr[i].len - 64));
        long delta = pos - 64;                            // its data started at 64 in its file
        unsigned int cmd = U32(info) + (unsigned int)delta;
        int h = *(int *)(info + 20);
        if (h < 0 || h > 4096 || cmd + 4u * (unsigned)h > (unsigned long)total) { ok = 0; break; }
        U32(info) = cmd; U32(info + 4) += (unsigned int)delta;
        for (int y = 0; y < h; y++) U32(out + cmd + 4 * y) += (unsigned int)delta;
        pos += fr[i].len - 64;
        if (g_nicons < MAX_ICONS) {
            lstrcpynA(g_icons[g_nicons].sheet, sheet, sizeof g_icons[0].sheet);
            lstrcpynA(g_icons[g_nicons].name, fr[i].name, sizeof g_icons[0].name);
            g_icons[g_nicons].index = count + i;
            g_nicons++;
        }
    }
    free(orig);
    if (!ok) { wl_log("icons: %s has a bad frame table, not merged", sheet); free(out); return; }
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\cache\\icons", g_wl.root);
    CreateDirectoryA(dir, NULL);
    snprintf(dst, sizeof dst, "%s\\%s.slp", dir, sheet);
    FILE *f = fopen(dst, "wb");
    if (f) { fwrite(out, 1, (size_t)total, f); fclose(f); }
    free(out);
    char fname[32];
    snprintf(fname, sizeof fname, "%s.slp", sheet);
    wl_add_redirect(fname, dst);
    wl_log("icons: %s.slp (%d base) + %d icon(s), first new index %d", sheet, count, n, count);
}

static int frame_cmp(const void *a, const void *b) { return strcmp(((const Frame *)a)->name, ((const Frame *)b)->name); }
static int mod_ptr_cmp(const void *a, const void *b) {
    const WlMod *ma = *(const WlMod *const *)a, *mb = *(const WlMod *const *)b;
    return strcmp(ma->id, mb->id);
}

// Collects <mod>\icons\<folder>\*.slp of every enabled mod in deterministic sorted mod-id
// and filename order (matching tools/build.py and src/datbuild.c).
static int collect(const char *folder, Frame *fr, int max) {
    const WlMod *sorted_mods[WL_MAX_MODS];
    int nmods = 0;
    for (int m = 0; m < g_wl.listed_count && nmods < WL_MAX_MODS; m++) {
        if (g_wl.mods[m].enabled) sorted_mods[nmods++] = &g_wl.mods[m];
    }
    if (nmods > 1) qsort(sorted_mods, (size_t)nmods, sizeof *sorted_mods, mod_ptr_cmp);

    int n = 0;
    for (int m = 0; m < nmods && n < max; m++) {
        const WlMod *mod = sorted_mods[m];
        char pat[MAX_PATH];
        snprintf(pat, sizeof pat, "%s\\icons\\%s\\*.slp", mod->dir, folder);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        int first = n;
        do {
            if (n >= max) break;
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s\\icons\\%s\\%s", mod->dir, folder, fd.cFileName);
            long len;
            unsigned char *d = read_all(path, &len);
            int fh = (d && len >= 64) ? *(int *)(d + 32 + 20) : -1;
            if (!d || memcmp(d, "2.0N", 4) != 0 || *(int *)(d + 4) != 1 ||
                U32(d + 32) < 64 || U32(d + 36) < 64 || fh <= 0 || fh > 4096 ||
                U32(d + 32) + 4u * (unsigned)fh > (unsigned long)len) {
                wl_log("icons: %s is not a valid 1-frame SLP, skipped", path); free(d); continue;
            }
            lstrcpynA(fr[n].name, fd.cFileName, sizeof fr[n].name);
            char *dot = strrchr(fr[n].name, '.'); if (dot) *dot = 0;
            fr[n].data = d; fr[n].len = len; n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (n - first > 1) qsort(fr + first, (size_t)(n - first), sizeof *fr, frame_cmp);
    }
    return n;
}

void wl_icons_build(void) {
    g_nicons = 0;
    static Frame fr[MAX_COLLECT];
    static const char *const sheets[] = { "50721" };
    static const char *const bldg[] = { "50705", "50706", "50707", "50708" };
    for (size_t s = 0; s < sizeof sheets / sizeof *sheets; s++) {
        int n = collect(sheets[s], fr, MAX_COLLECT);
        if (n) merge_sheet(sheets[s], fr, n);
        for (int i = 0; i < n; i++) free(fr[i].data);
    }
    int n = collect("buildings", fr, MAX_COLLECT);
    if (n) for (size_t s = 0; s < 4; s++) merge_sheet(bldg[s], fr, n);
    for (int i = 0; i < n; i++) free(fr[i].data);
    n = collect("units", fr, MAX_COLLECT);
    if (n) merge_sheet("50730", fr, n);
    for (int i = 0; i < n; i++) free(fr[i].data);
}
