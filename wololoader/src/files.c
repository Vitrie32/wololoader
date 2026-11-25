// File redirection: patches the exe's file imports (CreateFileA/W,
// FindFirstFileA, GetFileAttributesA) so that
//  1. resources\_common\dat\empires2_x2_p1.dat opens wololoader\cache\ (built dat),
//  2. names registered with wl_add_redirect (civ sound / interface art) open the mod file,
//  3. asset override: any game file <game>\resources\X opens
//     <mod>\resources\X when an enabled mod ships that file.
//     The mod folder mirrors the game's resources\ tree. If several mods ship
//     the same file, the mod that comes later in the mods list wins.
#include "wololoader.h"
#include "datbuild.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_t g_real_a;
static CreateFileW_t g_real_w;

static const char *k_redirect[] = { "empires2_x2_p1.dat" };

// Extra file-name -> absolute path redirects registered by other modules
// (e.g. civs.c: game_b35.slp -> the mod's interface art).
#define MAX_EXTRA 64
static struct { char name[64]; char path[MAX_PATH]; } g_extra[MAX_EXTRA];
static int g_extra_count;

// Thread safety: CreateFile / FindFirstFile / GetFileAttributes / D3DX hooks
// can run on audio or worker threads while hot reload swaps g_assets / g_extra.
static CRITICAL_SECTION g_files_cs;
static int g_files_cs_init;

static void files_cs_init(void) {
    if (!g_files_cs_init) {
        InitializeCriticalSection(&g_files_cs);
        g_files_cs_init = 1;
    }
}

void wl_add_redirect(const char *file_name, const char *path) {
    files_cs_init();
    EnterCriticalSection(&g_files_cs);
    // Update existing entry in place if the same filename is re-registered.
    for (int i = 0; i < g_extra_count; i++) {
        if (_stricmp(g_extra[i].name, file_name) == 0) {
            lstrcpynA(g_extra[i].path, path, sizeof g_extra[0].path);
            LeaveCriticalSection(&g_files_cs);
            wl_log("redirect rule (updated): %s -> %s", file_name, path);
            return;
        }
    }
    if (g_extra_count >= MAX_EXTRA) {
        LeaveCriticalSection(&g_files_cs);
        return;
    }
    lstrcpynA(g_extra[g_extra_count].name, file_name, sizeof g_extra[0].name);
    lstrcpynA(g_extra[g_extra_count].path, path, sizeof g_extra[0].path);
    g_extra_count++;
    LeaveCriticalSection(&g_files_cs);
    wl_log("redirect rule: %s -> %s", file_name, path);
}

void wl_files_clear_redirects(void) {
    files_cs_init();
    EnterCriticalSection(&g_files_cs);
    g_extra_count = 0;
    LeaveCriticalSection(&g_files_cs);
}

// ---- Asset override index -------------------------------------------------
// Built in wl_install_file_hooks (DllMain; only kernel32 + our own CRT are
// used, which is safe there) and rebuilt on hot reload into a fresh table
// swapped under g_files_cs. Key = lower-case path relative to the resources
// folder, backslash separators, e.g. "_common\\drs\\graphics\\123.slp".
// Open-addressing hash table, FNV-1a, size is a power of two.
typedef struct { char *key; char *path; } WlAsset;
typedef struct { WlAsset *items; unsigned cap, count; } WlAssetTable;
static WlAssetTable g_tbl;

static unsigned fnv1a(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

// Lower-case and turn '/' into '\\' (in place).
static void normalize(char *s) {
    for (; *s; s++) {
        if (*s == '/') *s = '\\';
        else if (*s >= 'A' && *s <= 'Z') *s += 32;
    }
}

static WlAsset *tbl_slot(const WlAssetTable *t, const char *key) {
    if (!t->cap) return NULL;
    unsigned i = fnv1a(key) & (t->cap - 1);
    while (t->items[i].key && strcmp(t->items[i].key, key) != 0) i = (i + 1) & (t->cap - 1);
    return &t->items[i];
}

static void tbl_put(WlAssetTable *t, const char *key, const char *path) {
    if (t->count * 2 >= t->cap) {           // keep load factor < 0.5
        unsigned old_cap = t->cap;
        WlAsset *old = t->items;
        t->cap = old_cap ? old_cap * 2 : 256;
        t->items = calloc(t->cap, sizeof *t->items);
        t->count = 0;
        for (unsigned i = 0; i < old_cap; i++)
            if (old[i].key) { *tbl_slot(t, old[i].key) = old[i]; t->count++; }
        free(old);
    }
    WlAsset *a = tbl_slot(t, key);
    if (a->key) { free(a->path); a->path = _strdup(path); return; }   // later mod wins
    a->key = _strdup(key);
    a->path = _strdup(path);
    t->count++;
}

static void tbl_free(WlAssetTable *t) {
    if (!t->items) return;
    for (unsigned i = 0; i < t->cap; i++) {
        free(t->items[i].key);
        free(t->items[i].path);
    }
    free(t->items);
    t->items = NULL;
    t->cap = 0;
    t->count = 0;
}

// Sprites renumbered because two enabled mods ship the same new id
// (wl_slp_remap_compute in datbuild.c, the same table the dat builder uses):
// while indexing mod g_remap_mod, "_common\drs\graphics\<from>.slp" is
// stored as "<to>.slp", so the first mod keeps <from> and nothing clashes.
static WlSlpRemap *g_remap;
static size_t g_remap_n;
static const char *g_remap_mod;

static void remap_key(char *key) {
    static const char pre[] = "_common\\drs\\graphics\\";
    if (!g_remap_mod || strncmp(key, pre, sizeof pre - 1) != 0) return;
    const char *name = key + sizeof pre - 1;
    char *end;
    long n = strtol(name, &end, 10);
    if (end == name || strcmp(end, ".slp") != 0) return;
    for (size_t i = 0; i < g_remap_n; i++) {
        if (g_remap[i].from == n && _stricmp(g_remap[i].mod, g_remap_mod) == 0) {
            snprintf(key, MAX_PATH, "%s%d.slp", pre, g_remap[i].to);
            return;
        }
    }
}

// Walk <dir> recursively; `rel` is the key prefix ("" at resources\ itself).
static void index_dir(WlAssetTable *t, const char *dir, const char *rel, int depth) {
    if (depth > 12) return;
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH], key[MAX_PATH];
        snprintf(full, MAX_PATH, "%s\\%s", dir, fd.cFileName);
        snprintf(key, MAX_PATH, "%s%s%s", rel, rel[0] ? "\\" : "", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) index_dir(t, full, key, depth + 1);
        else { normalize(key); remap_key(key); tbl_put(t, key, full); }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Custom taunts: <mod>\taunts\NN name.mp3 is served in every language's
// resources\<lang>\sound\taunt\ folder (game files are "01 yes.mp3".."42 ...").
// The chat code accepts taunt numbers 1..256 (range check at 0x4f94a0, found by
// Kiro, prologue 55 8b ec 8b 4d 10 8d 41 ff 3d ff 00 00 00); the game finds the
// file by listing the taunt folder, which the listing hook below merges mod files
// into. So a mod adds taunt 43+ by shipping "43 wololo again.mp3", or replaces a
// game taunt with the same number and name. Unverified in game.
static void add_mod_taunts(WlAssetTable *t, const char *mod_dir) {
    char tdir[MAX_PATH], pattern[MAX_PATH];
    snprintf(tdir, MAX_PATH, "%s\\taunts", mod_dir);
    DWORD at = GetFileAttributesA(tdir);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) return;
    snprintf(pattern, MAX_PATH, "%s\\resources\\*", g_wl.game_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.' || fd.cFileName[0] == '_' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char rel[MAX_PATH];
        snprintf(rel, MAX_PATH, "%s\\sound\\taunt", fd.cFileName);
        index_dir(t, tdir, rel, 0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void populate_asset_table(WlAssetTable *t) {
    // Wololoader's own art first (e.g. the main-menu Mods button with our logo,
    // made by tools/make_menu_icon.py at install). Mods are indexed after it,
    // and later entries win, so a mod can still replace these files.
    char own[MAX_PATH];
    snprintf(own, MAX_PATH, "%s\\resources", g_wl.root);
    index_dir(t, own, "", 0);

    char drs[MAX_PATH], mods[MAX_PATH];
    snprintf(drs, MAX_PATH, "%s\\resources\\_common\\drs", g_wl.game_dir);
    snprintf(mods, MAX_PATH, "%s\\mods", g_wl.root);
    free(g_remap);
    g_remap = NULL;
    g_remap_n = 0;
    if (wl_slp_remap_compute(drs, mods, &g_remap, &g_remap_n) != 0) { g_remap = NULL; g_remap_n = 0; }
    for (size_t i = 0; i < g_remap_n; i++)
        wl_log("sprites: %s graphics\\%d.slp clashes with another mod, used as %d.slp", g_remap[i].mod, g_remap[i].from, g_remap[i].to);

    for (int i = 0; i < g_wl.listed_count; i++) {
        const WlMod *m = &g_wl.mods[i];
        if (!m->enabled) continue;
        char res[MAX_PATH];
        snprintf(res, MAX_PATH, "%s\\resources", m->dir);
        unsigned before = t->count;
        g_remap_mod = m->id;
        index_dir(t, res, "", 0);
        g_remap_mod = NULL;
        add_mod_taunts(t, m->dir);
        if (t->count != before) wl_log("asset override: %s ships %u file(s)", m->id, t->count - before);
    }
    wl_log("asset override: %u file(s) indexed", t->count);
}

void wl_files_rebuild_assets(void) {
    files_cs_init();
    WlAssetTable fresh = { NULL, 0, 0 };
    populate_asset_table(&fresh);
    EnterCriticalSection(&g_files_cs);
    WlAssetTable old = g_tbl;
    g_tbl = fresh;
    LeaveCriticalSection(&g_files_cs);
    tbl_free(&old);
}

static unsigned long long fnv64_entry(const char *key, DWORD sz_lo, DWORD mt_lo, DWORD mt_hi) {
    unsigned long long h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    const unsigned char meta[12] = {
        (unsigned char)(sz_lo), (unsigned char)(sz_lo >> 8), (unsigned char)(sz_lo >> 16), (unsigned char)(sz_lo >> 24),
        (unsigned char)(mt_lo), (unsigned char)(mt_lo >> 8), (unsigned char)(mt_lo >> 16), (unsigned char)(mt_lo >> 24),
        (unsigned char)(mt_hi), (unsigned char)(mt_hi >> 8), (unsigned char)(mt_hi >> 16), (unsigned char)(mt_hi >> 24),
    };
    for (int i = 0; i < 12; i++) {
        h ^= meta[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Order-independent fingerprints for mod files under resources\_common\drs\<pkg>\.
// pkg indices: 0=graphics, 1=sounds, 2=terrain, 3=interface.
// iface_new_fp hashes only mod filenames in drs\interface\ that do not exist in vanilla.
void wl_files_drs_fingerprints(unsigned long long pkg_fp[4], unsigned long long *iface_new_fp) {
    static const char *const k_pfx[4] = {
        "_common\\drs\\graphics\\",
        "_common\\drs\\sounds\\",
        "_common\\drs\\terrain\\",
        "_common\\drs\\interface\\",
    };
    for (int p = 0; p < 4; p++) pkg_fp[p] = 0;
    if (iface_new_fp) *iface_new_fp = 0;
    files_cs_init();
    EnterCriticalSection(&g_files_cs);
    for (unsigned i = 0; i < g_tbl.cap; i++) {
        const WlAsset *a = &g_tbl.items[i];
        if (!a->key || !a->path) continue;
        for (int p = 0; p < 4; p++) {
            size_t plen = strlen(k_pfx[p]);
            if (strncmp(a->key, k_pfx[p], plen) != 0) continue;
            WIN32_FILE_ATTRIBUTE_DATA st;
            DWORD sz = 0, mt_lo = 0, mt_hi = 0;
            if (GetFileAttributesExA(a->path, GetFileExInfoStandard, &st)) {
                sz = st.nFileSizeLow;
                mt_lo = st.ftLastWriteTime.dwLowDateTime;
                mt_hi = st.ftLastWriteTime.dwHighDateTime;
            }
            pkg_fp[p] ^= fnv64_entry(a->key, sz, mt_lo, mt_hi);
            if (p == 3 && iface_new_fp) {
                char vanilla_path[MAX_PATH];
                snprintf(vanilla_path, sizeof vanilla_path, "%s\\resources\\%s", g_wl.game_dir, a->key);
                DWORD attr = GetFileAttributesA(vanilla_path);
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    *iface_new_fp ^= fnv64_entry(a->key, 0, 0, 0);
                }
            }
            break;
        }
    }
    LeaveCriticalSection(&g_files_cs);
}

// If `path` points into a resources\ folder of the game and a mod overrides
// that file, copy the mod file's path to `out`.
static BOOL asset_override(const char *path, char *out) {
    char low[MAX_PATH];
    lstrcpynA(low, path, MAX_PATH);
    normalize(low);
    // Never redirect files that already live in wololoader\ (mods, cache).
    if (strstr(low, "\\wololoader\\")) return FALSE;
    const char *rel = NULL;
    if (strncmp(low, "resources\\", 10) == 0) rel = low + 10;
    else if ((rel = strstr(low, "\\resources\\")) != NULL) rel += 11;
    if (!rel) return FALSE;
    while (rel[0] == '.' && rel[1] == '\\') rel += 2;   // "resources\.\x"
    BOOL found = FALSE;
    EnterCriticalSection(&g_files_cs);
    if (g_tbl.count) {
        WlAsset *a = tbl_slot(&g_tbl, rel);
        if (a && a->key) {
            lstrcpynA(out, a->path, MAX_PATH);
            found = TRUE;
        }
    }
    LeaveCriticalSection(&g_files_cs);
    return found;
}

// Real FindFirstFileA / GetFileAttributesA (the hooks below are defined later).
typedef HANDLE (WINAPI *FindFirstFileA_t_fwd)(LPCSTR, LPWIN32_FIND_DATAA);
static DWORD (WINAPI *g_real_attr_fwd)(LPCSTR);

// ---- New files in folder listings -------------------------------------------
// Overriding only replaces files the game already has. To let a mod ADD files
// (a new random map script, a new AI), folder listings must show them too:
// when the game enumerates <game>\resources\<dir>\<wildcard>, every indexed
// mod file in <dir> that matches the wildcard and does not exist in the game
// folder is appended after the real results. Opening it then goes through
// asset_override like any other file. Nothing is written to the game folder.

// Case-insensitive wildcard match with '*' and '?' (both strings lower-case).
static BOOL glob_match(const char *p, const char *s) {
    if (!*p) return !*s;
    if (*p == '*') return glob_match(p + 1, s) || (*s && glob_match(p, s + 1));
    if (*s && (*p == '?' || *p == *s)) return glob_match(p + 1, s + 1);
    return FALSE;
}

// A listing we extend: the real search handle plus the mod files to append.
// The HANDLE we give the game is a pointer to this struct; g_enums holds the
// live ones so FindNextFileA/FindClose can tell ours from real handles.
typedef struct {
    HANDLE real;            // real search handle, NULL once exhausted / none
    int count, pos;
    WIN32_FIND_DATAA items[];
} WlEnum;
#define MAX_ENUMS 32
static WlEnum *g_enums[MAX_ENUMS];
static volatile LONG g_enum_active;

static BOOL is_our_enum(HANDLE h) {
    if (!g_enum_active) return FALSE;
    EnterCriticalSection(&g_files_cs);
    BOOL found = FALSE;
    for (int i = 0; i < MAX_ENUMS; i++) {
        if (g_enums[i] && (HANDLE)g_enums[i] == h) { found = TRUE; break; }
    }
    LeaveCriticalSection(&g_files_cs);
    return found;
}

static FindFirstFileA_t_fwd g_real_find_fwd;  // set in wl_install_file_hooks

// Build the list of extra mod files for `pattern`, or NULL when there are none.
static WlEnum *collect_extras(const char *pattern) {
    if (!strpbrk(pattern, "*?")) return NULL;
    char low[MAX_PATH];
    lstrcpynA(low, pattern, MAX_PATH);
    normalize(low);
    if (strstr(low, "\\wololoader\\")) return NULL;
    char *slash = strrchr(low, '\\');
    if (!slash) return NULL;
    *slash = 0;
    const char *spec = slash + 1;
    char dir[MAX_PATH];                        // dir relative to resources, may be ""
    if (strcmp(low, "resources") == 0) dir[0] = 0;
    else if (strncmp(low, "resources\\", 10) == 0) lstrcpynA(dir, low + 10, MAX_PATH);
    else {
        const char *r = strstr(low, "\\resources");
        if (!r || (r[10] && r[10] != '\\')) return NULL;
        lstrcpynA(dir, r[10] ? r + 11 : "", MAX_PATH);
    }
    size_t dlen = strlen(dir);
    // Original folder part of the pattern (game spelling) to test existence.
    char orig_dir[MAX_PATH];
    lstrcpynA(orig_dir, pattern, MAX_PATH);
    char *os = strrchr(orig_dir, '\\'), *os2 = strrchr(orig_dir, '/');
    if (os2 > os) os = os2;
    if (os) *os = 0;

    WlEnum *e = NULL;
    EnterCriticalSection(&g_files_cs);
    for (unsigned i = 0; i < g_tbl.cap; i++) {
        const WlAsset *a = &g_tbl.items[i];
        if (!a->key) continue;
        const char *name = a->key;
        if (dlen) {
            if (strncmp(name, dir, dlen) != 0 || name[dlen] != '\\') continue;
            name += dlen + 1;
        }
        if (strchr(name, '\\') || !glob_match(spec, name)) continue;
        char game_path[MAX_PATH];
        snprintf(game_path, MAX_PATH, "%s\\%s", orig_dir, name);
        if (g_real_attr_fwd(game_path) != INVALID_FILE_ATTRIBUTES) continue;  // already listed by the game
        WIN32_FIND_DATAA fd;
        HANDLE h = g_real_find_fwd(a->path, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        FindClose(h);
        int n = e ? e->count : 0;
        WlEnum *grown = realloc(e, sizeof *e + (n + 1) * sizeof(WIN32_FIND_DATAA));
        if (!grown) break;
        if (!e) { grown->real = NULL; grown->count = 0; grown->pos = 0; }
        e = grown;
        e->items[e->count++] = fd;
    }
    LeaveCriticalSection(&g_files_cs);
    return e;
}

// Start a listing: `real` is what the game's own call returned.
// Returns the handle to give the game (ours when we add files).
static HANDLE start_enum(const char *pattern, HANDLE real, WIN32_FIND_DATAA *fd) {
    WlEnum *e = collect_extras(pattern);
    if (!e) return real;
    EnterCriticalSection(&g_files_cs);
    int slot = -1;
    for (int i = 0; i < MAX_ENUMS; i++) if (!g_enums[i]) { slot = i; break; }
    if (slot < 0) {
        LeaveCriticalSection(&g_files_cs);
        free(e);
        return real;
    }
    if (real != INVALID_HANDLE_VALUE) {
        e->real = real;                       // the game already got the first real entry
    } else {
        *fd = e->items[e->pos++];             // empty folder in the game: start with ours
    }
    g_enums[slot] = e;
    InterlockedIncrement(&g_enum_active);
    LeaveCriticalSection(&g_files_cs);
    wl_log("listing %s: +%d mod file(s)", pattern, e->count);
    return (HANDLE)e;
}

// Returns TRUE and fills `out` when `path` should be redirected.
static BOOL redirect_target(const char *path, char *out) {
    const char *slash = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    if (slash2 > slash) slash = slash2;
    const char *file = slash ? slash + 1 : path;
    for (size_t i = 0; i < sizeof k_redirect / sizeof *k_redirect; i++) {
        if (_stricmp(file, k_redirect[i]) != 0) continue;
        wl_civs_ensure();  // first open of the dat: build the cache now (not in DllMain)
        snprintf(out, MAX_PATH, "%s\\cache\\%s", g_wl.root, k_redirect[i]);
        return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
    }
    BOOL hit = FALSE;
    EnterCriticalSection(&g_files_cs);
    for (int i = 0; i < g_extra_count; i++) {
        if (_stricmp(file, g_extra[i].name) != 0) continue;
        lstrcpynA(out, g_extra[i].path, MAX_PATH);
        hit = TRUE;
        break;
    }
    LeaveCriticalSection(&g_files_cs);
    if (hit) return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
    return asset_override(path, out);
}

// The SLP loader probes a file with FindFirstFileA before opening it
// (0x824b71), so redirected files must be visible to the probe as well.
typedef HANDLE (WINAPI *FindFirstFileA_t)(LPCSTR, LPWIN32_FIND_DATAA);
static FindFirstFileA_t g_real_find;

static HANDLE WINAPI hook_find_first_file_a(LPCSTR name, LPWIN32_FIND_DATAA fd) {
    char alt[MAX_PATH];
    if (name && !strchr(name, '*') && !strchr(name, '?') && redirect_target(name, alt)) {
        HANDLE h = g_real_find(alt, fd);
        wl_log("redirect probe %s -> %s (%s)", name, alt, h != INVALID_HANDLE_VALUE ? "found" : "missing");
        if (h != INVALID_HANDLE_VALUE) {
            // Report the name the game asked for, not the mod's file name.
            const char *s = strrchr(name, '\\');
            lstrcpynA(fd->cFileName, s ? s + 1 : name, MAX_PATH);
        }
        return h;
    }
    HANDLE h = g_real_find(name, fd);
    if (name && strpbrk(name, "*?")) return start_enum(name, h, fd);
    return h;
}

static BOOL WINAPI hook_find_next_file_a(HANDLE h, LPWIN32_FIND_DATAA fd) {
    if (!is_our_enum(h)) return FindNextFileA(h, fd);
    WlEnum *e = (WlEnum *)h;
    if (e->real) {
        if (FindNextFileA(e->real, fd)) return TRUE;
        FindClose(e->real);                   // real results done, now ours
        e->real = NULL;
    }
    if (e->pos < e->count) { *fd = e->items[e->pos++]; return TRUE; }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

static BOOL WINAPI hook_find_close(HANDLE h) {
    if (g_enum_active) {
        WlEnum *victim = NULL;
        EnterCriticalSection(&g_files_cs);
        for (int i = 0; i < MAX_ENUMS; i++) {
            if (!g_enums[i] || (HANDLE)g_enums[i] != h) continue;
            victim = g_enums[i];
            g_enums[i] = NULL;
            InterlockedDecrement(&g_enum_active);
            break;
        }
        LeaveCriticalSection(&g_files_cs);
        if (victim) {
            if (victim->real) FindClose(victim->real);
            free(victim);
            return TRUE;
        }
    }
    return FindClose(h);
}

// The static CRT (_findfirst, stat) uses FindFirstFileExA.
typedef HANDLE (WINAPI *FindFirstFileExA_t)(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
static FindFirstFileExA_t g_real_find_ex;

static HANDLE WINAPI hook_find_first_file_ex_a(LPCSTR name, FINDEX_INFO_LEVELS lvl, LPVOID data,
                                               FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    char alt[MAX_PATH];
    if (name && !strpbrk(name, "*?") && redirect_target(name, alt)) {
        HANDLE h = g_real_find_ex(alt, lvl, data, op, filter, flags);
        if (h != INVALID_HANDLE_VALUE) {
            const char *s = strrchr(name, '\\');
            lstrcpynA(((WIN32_FIND_DATAA *)data)->cFileName, s ? s + 1 : name, MAX_PATH);
        }
        return h;
    }
    HANDLE h = g_real_find_ex(name, lvl, data, op, filter, flags);
    // Only plain name searches get mod files (not "directories only").
    if (name && op == FindExSearchNameMatch && strpbrk(name, "*?"))
        return start_enum(name, h, (WIN32_FIND_DATAA *)data);
    return h;
}

// Existence checks (the game asks before loading some files).
typedef DWORD (WINAPI *GetFileAttributesA_t)(LPCSTR);
static GetFileAttributesA_t g_real_attr;

static DWORD WINAPI hook_get_file_attributes_a(LPCSTR name) {
    char alt[MAX_PATH];
    if (name && redirect_target(name, alt)) return g_real_attr(alt);
    return g_real_attr(name);
}

static HANDLE WINAPI hook_create_file_a(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                        DWORD disp, DWORD flags, HANDLE tmpl) {
    char alt[MAX_PATH];
    if (name && !(acc & GENERIC_WRITE) && redirect_target(name, alt)) {
        wl_log("redirect %s -> %s", name, alt);
        name = alt;
    }
    return g_real_a(name, acc, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI hook_create_file_w(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                        DWORD disp, DWORD flags, HANDLE tmpl) {
    char narrow[MAX_PATH * 2], alt[MAX_PATH];
    WCHAR walt[MAX_PATH];
    if (name && !(acc & GENERIC_WRITE) &&
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof narrow, NULL, NULL) &&
        redirect_target(narrow, alt)) {
        wl_log("redirect %s -> %s", narrow, alt);
        MultiByteToWideChar(CP_UTF8, 0, alt, -1, walt, MAX_PATH);
        name = walt;
    }
    return g_real_w(name, acc, share, sa, disp, flags, tmpl);
}

// UI textures (main menu buttons etc.) are not opened with CreateFile by the
// exe but loaded by d3dx9_43.dll (its own file calls, not our IAT hooks):
// the exe imports D3DXCreateTextureFromFileExA (14 args, stdcall). Swap the
// path for a mod/loader override and pass everything else through.
typedef long (WINAPI *D3DXTexEx_t)(void *dev, const char *file, unsigned w, unsigned h, unsigned mips,
                                    DWORD usage, int fmt, int pool, DWORD filter, DWORD mipfilter,
                                    DWORD colorkey, void *info, void *palette, void **tex);
static D3DXTexEx_t g_real_texex;
static long WINAPI hook_d3dx_tex_ex(void *dev, const char *file, unsigned w, unsigned h, unsigned mips,
                                    DWORD usage, int fmt, int pool, DWORD filter, DWORD mipfilter,
                                    DWORD colorkey, void *info, void *palette, void **tex) {
    char over[MAX_PATH];
    if (file && asset_override(file, over)) file = over;
    return g_real_texex(dev, file, w, h, mips, usage, fmt, pool, filter, mipfilter, colorkey, info, palette, tex);
}

// Replace one import of the main exe; returns the original function.
static void *patch_iat(const char *dll, const char *func, void *hook) {
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(base + d->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *slots = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (void *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, func) != 0) continue;
            DWORD old;
            void *orig = (void *)slots->u1.Function;
            VirtualProtect(&slots->u1.Function, 4, PAGE_READWRITE, &old);
            slots->u1.Function = (DWORD)hook;
            VirtualProtect(&slots->u1.Function, 4, old, &old);
            return orig;
        }
    }
    return NULL;
}

// Vanilla Mode: only Wololoader's own UI art (the Mods button image) through the
// D3DX texture import; no mod files, no dat redirect, no file listing changes.
void wl_install_ui_textures_only(void) {
    files_cs_init();
    char own[MAX_PATH];
    snprintf(own, MAX_PATH, "%s\\resources", g_wl.root);
    index_dir(&g_tbl, own, "", 0);
    if (g_tbl.count)
        g_real_texex = patch_iat("d3dx9_43.dll", "D3DXCreateTextureFromFileExA", hook_d3dx_tex_ex);
    wl_log("vanilla: own UI art %u file(s), D3DX hook %s", g_tbl.count, g_real_texex ? "ok" : "off");
}

void wl_install_file_hooks(void) {
    files_cs_init();
    populate_asset_table(&g_tbl);   // needs g_wl.mods: wl_scan_mods runs before us in DllMain
    g_real_a = patch_iat("KERNEL32.dll", "CreateFileA", hook_create_file_a);
    g_real_w = patch_iat("KERNEL32.dll", "CreateFileW", hook_create_file_w);
    g_real_find = patch_iat("KERNEL32.dll", "FindFirstFileA", hook_find_first_file_a);
    if (!g_real_find) g_real_find = (FindFirstFileA_t)FindFirstFileA;
    g_real_attr = patch_iat("KERNEL32.dll", "GetFileAttributesA", hook_get_file_attributes_a);
    if (!g_real_attr) g_real_attr = (GetFileAttributesA_t)GetFileAttributesA;
    g_real_attr_fwd = g_real_attr;
    g_real_find_fwd = g_real_find;
    // Install D3DX and directory listing hooks unconditionally so asset, DRS,
    // RMS, AI, and taunt mods enabled later via hot reload work immediately.
    g_real_texex = patch_iat("d3dx9_43.dll", "D3DXCreateTextureFromFileExA", hook_d3dx_tex_ex);
    wl_log("asset override: D3DX texture hook %s", g_real_texex ? "ok" : "missing");
    g_real_find_ex = patch_iat("KERNEL32.dll", "FindFirstFileExA", hook_find_first_file_ex_a);
    if (!g_real_find_ex) g_real_find_ex = (FindFirstFileExA_t)FindFirstFileExA;
    patch_iat("KERNEL32.dll", "FindNextFileA", hook_find_next_file_a);
    patch_iat("KERNEL32.dll", "FindClose", hook_find_close);
    wl_log("file hooks: CreateFileA %s, CreateFileW %s", g_real_a ? "ok" : "missing",
           g_real_w ? "ok" : "missing");
    if (!g_real_a) g_real_a = (CreateFileA_t)CreateFileA;
    if (!g_real_w) g_real_w = (CreateFileW_t)CreateFileW;
}
