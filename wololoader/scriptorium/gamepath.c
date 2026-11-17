// ==========================================================================
// Scriptorium - validation of paths into the game's own resources\ tree
// (see gamepath.h for the rules and why each one exists)
// ==========================================================================
#include "gamepath.h"
#include <ctype.h>
#include <string.h>

static const char *const GAME_FILE_EXTS[] = { ".slp", ".bina", ".mp3", ".wav", ".png" };

// ASCII case-insensitive equality of the first n characters (a and b are
// NUL-terminated or at least n long). Own helper: _strnicmp is not in C11.
static int ci_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}

// Windows maps these names to devices even with an extension ("nul.slp" is the NUL device),
// so a file of that name must not be opened.
static int is_device_segment(const char *seg, size_t len) {
    size_t stem = 0;
    while (stem < len && seg[stem] != '.') stem++;
    if (stem == 3 && (ci_eq_n(seg, "CON", 3) || ci_eq_n(seg, "PRN", 3) ||
                      ci_eq_n(seg, "AUX", 3) || ci_eq_n(seg, "NUL", 3))) return 1;
    if (stem == 4 && (ci_eq_n(seg, "COM", 3) || ci_eq_n(seg, "LPT", 3)) &&
        seg[3] >= '1' && seg[3] <= '9') return 1;
    return 0;
}

int sc_game_file_name_ok(const char *name) {
    if (!name) return 0;
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (size_t i = 0; i < sizeof GAME_FILE_EXTS / sizeof *GAME_FILE_EXTS; i++) {
        size_t n = strlen(GAME_FILE_EXTS[i]);
        if (strlen(dot) == n && ci_eq_n(dot, GAME_FILE_EXTS[i], n)) return 1;
    }
    return 0;
}

int sc_game_rel_path_ok(const char *rel, int is_dir) {
    if (!rel || !rel[0] || strlen(rel) > 200) return 0;
    size_t len = strlen(rel), start = 0, segs = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i < len && rel[i] != '/') {
            // only plain name characters: no backslash, colon (drive letters, streams), spaces
            if (!(isalnum((unsigned char)rel[i]) || rel[i] == '_' || rel[i] == '-' || rel[i] == '.')) return 0;
            continue;
        }
        // end of a segment: leading, trailing and doubled slashes give an empty one
        size_t seglen = i - start;
        if (seglen == 0 || seglen > 100) return 0;
        if (seglen == 1 && rel[start] == '.') return 0;
        if (seglen == 2 && rel[start] == '.' && rel[start + 1] == '.') return 0;
        if (is_device_segment(rel + start, seglen)) return 0;
        segs++;
        start = i + 1;
    }
    if (segs > 8) return 0;
    if (is_dir) return 1;
    const char *last = strrchr(rel, '/');
    return sc_game_file_name_ok(last ? last + 1 : rel);
}
