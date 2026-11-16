// ==========================================================================
// Scriptorium Game Data Catalog (catalog.c)
//
// Implements the catalog.h contract for Wololoader's mod creator.
// Loads game metadata directly from the player's installed game:
//   - Civilizations: internal names from dat, display names from key-value strings
//   - Units & base stats: parsed from civ 0 (Gaia) and per-civ unit tables
//   - Researches (techs): parsed from the dat researches section
//
// Sources:
//   <game>/resources/_common/dat/empires2_x2_p1.dat  (raw deflate, see AGENTS.md)
//   <game>/resources/en/strings/key-value/key-value-strings-utf8.txt
//   <game>/resources/en/strings/key-value/key-value-modded-strings-utf8.txt
// ==========================================================================

#include "catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

// --------------------------------------------------------------------------
// Stat Field Identifiers and UI Labels
// --------------------------------------------------------------------------

// Exact json field names accepted in civ json "units" overrides
const char *const SC_STAT_FIELD[SC_STAT_COUNT] = {
    "hit_points",
    "line_of_sight",
    "speed",
    "search_radius",
    "work_rate",
    "max_range",
    "reload_time",
    "train_time"
};

// Friendly labels displayed in the Scriptorium GUI
const char *const SC_STAT_LABEL[SC_STAT_COUNT] = {
    "Hit points",
    "Line of sight",
    "Speed",
    "Search radius",
    "Work rate",
    "Max range",
    "Reload time",
    "Train time"
};

// --------------------------------------------------------------------------
// Genie Unit Class Hierarchy & Inheritance Masks
// Mirroring datbuild.c and tools/datfile.py
// --------------------------------------------------------------------------

#define WL_CLS_10 (1 << 0) // Unit (base)
#define WL_CLS_20 (1 << 1) // UnitFlag (types 20, 25)
#define WL_CLS_30 (1 << 2) // UnitDeadFish (type 30)
#define WL_CLS_40 (1 << 3) // UnitBird (type 40)
#define WL_CLS_50 (1 << 4) // UnitType50 (type 50)
#define WL_CLS_60 (1 << 5) // UnitProjectile (type 60)
#define WL_CLS_70 (1 << 6) // UnitCreatable (type 70)
#define WL_CLS_80 (1 << 7) // UnitBuilding (type 80)

static uint8_t sc_unit_class_mask(uint8_t t) {
    uint8_t m = WL_CLS_10;
    if (t == 20 || t == 25) m |= WL_CLS_20;
    else if (t == 30) m |= WL_CLS_20 | WL_CLS_30;
    else if (t == 40) m |= WL_CLS_20 | WL_CLS_30 | WL_CLS_40;
    else if (t == 50) m |= WL_CLS_20 | WL_CLS_30 | WL_CLS_40 | WL_CLS_50;
    else if (t == 60) m |= WL_CLS_20 | WL_CLS_30 | WL_CLS_40 | WL_CLS_50 | WL_CLS_60;
    else if (t == 70) m |= WL_CLS_20 | WL_CLS_30 | WL_CLS_40 | WL_CLS_50 | WL_CLS_70;
    else if (t == 80) m |= WL_CLS_20 | WL_CLS_30 | WL_CLS_40 | WL_CLS_50 | WL_CLS_70 | WL_CLS_80;
    return m;
}

// Computes the total byte length of a single polymorphic unit record in raw dat
static size_t sc_calc_unit_len(const unsigned char *data, size_t pos) {
    uint8_t t = data[pos];
    int16_t nl = (int16_t)(data[pos + 1] | (data[pos + 2] << 8));
    uint8_t dmg = data[pos + 149];
    size_t u_len = 150 + (size_t)dmg * 5 + 6 + (size_t)nl + 4;
    if (t >= 20) u_len += 4;
    if (t >= 30) u_len += 37;
    if (t >= 40) u_len += 20;
    if (t >= 50) {
        u_len += 2; // default_armor
        uint16_t at_cnt = (uint16_t)(data[pos + u_len] | (data[pos + u_len + 1] << 8));
        u_len += 2 + (size_t)at_cnt * 4;
        uint16_t ar_cnt = (uint16_t)(data[pos + u_len] | (data[pos + u_len + 1] << 8));
        u_len += 2 + (size_t)ar_cnt * 4 + 56;
    }
    if (t == 60) {
        u_len += 9;
    } else if (t >= 70) {
        u_len += 65;
        if (t == 80) {
            u_len += 82;
        }
    }
    return u_len;
}

// Descriptors for the 8 editable unit stats (slice index and byte offset within slice)
typedef struct {
    uint8_t  mask;
    uint8_t  slice;
    uint16_t offset;
    int      is_i16; // 1 for int16_t, 0 for float
} ScStatDesc;

static const ScStatDesc g_stat_descs[SC_STAT_COUNT] = {
    [SC_HIT_POINTS]    = { 0xff, 0, 20, 1 }, // slice 0, offset 20, int16
    [SC_LINE_OF_SIGHT] = { 0xff, 0, 22, 0 }, // slice 0, offset 22, float
    [SC_SPEED]         = { 0xfe, 2,  4, 0 }, // slice 2, offset 4, float
    [SC_SEARCH_RADIUS] = { 0xf8, 2, 47, 0 }, // slice 2, offset 47, float
    [SC_WORK_RATE]     = { 0xf8, 2, 51, 0 }, // slice 2, offset 51, float
    [SC_MAX_RANGE]     = { 0xf0, 4,  2, 0 }, // slice 4, offset 2, float
    [SC_RELOAD_TIME]   = { 0xf0, 4, 10, 0 }, // slice 4, offset 10, float
    [SC_TRAIN_TIME]    = { 0xc0, 4, 74, 1 }  // slice 4, offset 74, int16
};

// --------------------------------------------------------------------------
// In-Memory Representation of Per-Civ Unit Records (ScCatalog.priv)
// --------------------------------------------------------------------------

typedef struct {
    uint8_t  *data;          // contiguous raw binary data of the civ record
    size_t    size;          // byte size of data
    uint32_t *unit_offsets;  // byte offset into data for each unit slot (0 if non-existent)
    uint16_t  unit_count;    // number of unit slots
} ScPrivCiv;

typedef struct {
    size_t     civ_count;    // total base civs in dat (including Gaia at index 0)
    ScPrivCiv *civs;
} ScCatalogPriv;

// --------------------------------------------------------------------------
// String Table (Parses key-value-strings-utf8.txt)
// --------------------------------------------------------------------------

typedef struct {
    int   id;
    char *text;
} ScStrEntry;

typedef struct {
    ScStrEntry *entries;
    size_t      count;
    size_t      cap;
} ScStrTable;

static void sc_str_table_init(ScStrTable *t) {
    t->entries = NULL;
    t->count = 0;
    t->cap = 0;
}

static void sc_str_table_free(ScStrTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->count; i++) {
        free(t->entries[i].text);
    }
    free(t->entries);
    t->entries = NULL;
    t->count = 0;
    t->cap = 0;
}

static int sc_cmp_str_entry(const void *a, const void *b) {
    int id_a = ((const ScStrEntry *)a)->id;
    int id_b = ((const ScStrEntry *)b)->id;
    if (id_a < id_b) return -1;
    if (id_a > id_b) return 1;
    return 0;
}

static int sc_cmp_str_key(const void *key, const void *elem) {
    int k = *(const int *)key;
    int id = ((const ScStrEntry *)elem)->id;
    if (k < id) return -1;
    if (k > id) return 1;
    return 0;
}

static const char *sc_str_table_lookup(const ScStrTable *t, int id) {
    if (!t || !t->entries || t->count == 0) return NULL;
    const ScStrEntry *e = (const ScStrEntry *)bsearch(&id, t->entries, t->count, sizeof(ScStrEntry), sc_cmp_str_key);
    return e ? e->text : NULL;
}

// Appends an entry into the table (before final sorting and deduplication)
static int sc_str_table_append(ScStrTable *t, int id, const char *str, size_t len) {
    if (t->count >= t->cap) {
        size_t ncap = t->cap == 0 ? 1024 : t->cap * 2;
        ScStrEntry *ne = realloc(t->entries, ncap * sizeof(ScStrEntry));
        if (!ne) return -1;
        t->entries = ne;
        t->cap = ncap;
    }
    char *copy = malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, str, len);
    copy[len] = '\0';

    t->entries[t->count].id = id;
    t->entries[t->count].text = copy;
    t->count++;
    return 0;
}

// Sorts entries and compacts duplicates (keeping the later occurrence)
static void sc_str_table_sort_and_dedup(ScStrTable *t) {
    if (t->count <= 1) return;
    qsort(t->entries, t->count, sizeof(ScStrEntry), sc_cmp_str_entry);

    size_t w = 0;
    for (size_t r = 0; r < t->count; r++) {
        if (w > 0 && t->entries[w - 1].id == t->entries[r].id) {
            // Duplicate id: replace earlier text with later text
            free(t->entries[w - 1].text);
            t->entries[w - 1].text = t->entries[r].text;
        } else {
            t->entries[w++] = t->entries[r];
        }
    }
    t->count = w;
}

// Loads a key-value strings file. Format: <id> "<text>" (lines starting with // are comments).
static int sc_str_table_load_file(ScStrTable *t, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return -1; }
    rewind(f);

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return -1; }
    size_t read_bytes = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[read_bytes] = '\0';

    char *line = buf;
    while (*line) {
        // Find line end
        char *next = line;
        while (*next && *next != '\r' && *next != '\n') next++;
        char saved = *next;
        *next = '\0';

        // Skip leading whitespace
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        // Only parse lines with numeric IDs; comments and non-numeric keys are ignored
        if (*p != '\0' && memcmp(p, "//", 2) != 0 && isdigit((unsigned char)*p)) {
            char *endp = NULL;
            long id = strtol(p, &endp, 10);
            if (endp && endp != p) {
                char *q1 = strchr(endp, '"');
                if (q1) {
                    char *q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        sc_str_table_append(t, (int)id, q1 + 1, (size_t)(q2 - (q1 + 1)));
                    }
                }
            }
        }

        if (saved == '\0') break;
        line = next + 1;
        if (saved == '\r' && *line == '\n') line++;
    }

    free(buf);
    return 0;
}

// --------------------------------------------------------------------------
// Path Helper
// --------------------------------------------------------------------------

static void sc_join_path(char *dst, size_t dstcap, const char *dir, const char *sub) {
    if (!dst || dstcap == 0) return;
    dst[0] = '\0';
    if (!dir || !sub) return;

    size_t dlen = strlen(dir);
    int need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    char sep = '/';
    if (strchr(dir, '\\') && !strchr(dir, '/')) {
        sep = '\\';
    }

    if (need_sep) {
        snprintf(dst, dstcap, "%s%c%s", dir, sep, sub);
    } else {
        snprintf(dst, dstcap, "%s%s", dir, sub);
    }

#if !defined(_WIN32)
    for (char *p = dst; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#else
    for (char *p = dst; *p; p++) {
        if (*p == '/') *p = '\\';
    }
#endif
}

static void sc_copy_clean_string(char *dst, size_t dstcap, const char *src, size_t srclen) {
    if (!dst || dstcap == 0) return;
    dst[0] = '\0';
    if (!src || srclen == 0) return;

    size_t copy_len = srclen < dstcap - 1 ? srclen : dstcap - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    // Truncate at first embedded null or strip trailing whitespace
    size_t len = strlen(dst);
    while (len > 0 && ((unsigned char)dst[len - 1] <= ' ' || dst[len - 1] == '\0')) {
        dst[--len] = '\0';
    }
}

// --------------------------------------------------------------------------
// Stats Extraction for a Raw Unit Record
// --------------------------------------------------------------------------

static void sc_extract_unit_stats_from_raw(const uint8_t *udata, ScUnitStats *out) {
    memset(out, 0, sizeof(*out));
    uint8_t t = udata[0];
    uint8_t umask = sc_unit_class_mask(t);

    int16_t nl = (int16_t)(udata[1] | (udata[2] << 8));
    uint8_t dmg = udata[149];
    size_t s0 = 0;
    size_t s1 = 150 + (size_t)dmg * 5;
    size_t s2 = s1 + 6 + (size_t)nl;
    size_t s3 = 0, s4 = 0, s5 = 0;
    if (t >= 50) {
        size_t p_atk = s2 + 4;
        if (t >= 20) p_atk += 4;
        if (t >= 30) p_atk += 37;
        if (t >= 40) p_atk += 20;
        p_atk += 2; // default_armor
        uint16_t atk_cnt = (uint16_t)(udata[p_atk] | (udata[p_atk + 1] << 8));
        s3 = p_atk + 2 + (size_t)atk_cnt * 4;
        uint16_t arm_cnt = (uint16_t)(udata[s3] | (udata[s3 + 1] << 8));
        s4 = s3 + 2 + (size_t)arm_cnt * 4;
        if (t == 80) {
            s5 = s4 + 138 + 40;
        }
    }
    size_t slices[6] = { s0, s1, s2, s3, s4, s5 };

    for (int i = 0; i < SC_STAT_COUNT; i++) {
        const ScStatDesc *desc = &g_stat_descs[i];
        if (desc->mask & umask) {
            out->has[i] = 1;
            const uint8_t *src = udata + slices[desc->slice] + desc->offset;
            if (desc->is_i16) {
                int16_t val = (int16_t)(src[0] | (src[1] << 8));
                out->value[i] = (float)val;
            } else {
                float val;
                memcpy(&val, src, sizeof(float));
                out->value[i] = val;
            }
        } else {
            out->has[i] = 0;
            out->value[i] = 0.0f;
        }
    }
}

// --------------------------------------------------------------------------
// Public API: sc_catalog_load
// --------------------------------------------------------------------------

int sc_catalog_load(const WlZlib *z, const char *game_dir, ScCatalog *out, char *err, size_t errcap) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (err && errcap > 0) err[0] = '\0';

    if (!z || !z->inflate_raw) {
        if (err && errcap > 0) snprintf(err, errcap, "invalid WlZlib deflate/inflate function pointers");
        return -1;
    }
    if (!game_dir || game_dir[0] == '\0') {
        if (err && errcap > 0) snprintf(err, errcap, "invalid game directory path");
        return -1;
    }

    // 1. Load string tables (base strings and modded overrides)
    ScStrTable str_table;
    sc_str_table_init(&str_table);

    char str_path[1024];
    sc_join_path(str_path, sizeof(str_path), game_dir, "resources/en/strings/key-value/key-value-strings-utf8.txt");
    if (sc_str_table_load_file(&str_table, str_path) != 0) {
        if (err && errcap > 0) snprintf(err, errcap, "failed to read strings file: %s", str_path);
        return -1;
    }

    char mod_str_path[1024];
    sc_join_path(mod_str_path, sizeof(mod_str_path), game_dir, "resources/en/strings/key-value/key-value-modded-strings-utf8.txt");
    // Modded strings file is optional; if present, its entries override base strings
    sc_str_table_load_file(&str_table, mod_str_path);

    sc_str_table_sort_and_dedup(&str_table);

    // 2. Read and decompress empires2_x2_p1.dat
    char dat_path[1024];
    sc_join_path(dat_path, sizeof(dat_path), game_dir, "resources/_common/dat/empires2_x2_p1.dat");

    FILE *fdat = fopen(dat_path, "rb");
    if (!fdat) {
        if (err && errcap > 0) snprintf(err, errcap, "failed to open %s: %s", dat_path, strerror(errno));
        sc_str_table_free(&str_table);
        return -1;
    }

    if (fseek(fdat, 0, SEEK_END) != 0) {
        fclose(fdat);
        sc_str_table_free(&str_table);
        return -1;
    }
    long dat_size = ftell(fdat);
    if (dat_size < 0) {
        fclose(fdat);
        sc_str_table_free(&str_table);
        return -1;
    }
    rewind(fdat);

    unsigned char *comp_data = malloc((size_t)dat_size);
    if (!comp_data) {
        fclose(fdat);
        sc_str_table_free(&str_table);
        if (err && errcap > 0) snprintf(err, errcap, "out of memory reading %s", dat_path);
        return -1;
    }
    size_t comp_len = fread(comp_data, 1, (size_t)dat_size, fdat);
    fclose(fdat);

    unsigned char *orig = NULL;
    size_t orig_len = 0;
    if (z->inflate_raw(comp_data, comp_len, &orig, &orig_len) != 0 || !orig) {
        free(comp_data);
        sc_str_table_free(&str_table);
        if (err && errcap > 0) snprintf(err, errcap, "failed to decompress %s", dat_path);
        return -1;
    }
    free(comp_data);

    // Section 1: Version Header
    if (orig_len < 8 || memcmp(orig, "VER ", 4) != 0) {
        free(orig);
        sc_str_table_free(&str_table);
        if (err && errcap > 0) snprintf(err, errcap, "invalid dat version header in %s", dat_path);
        return -1;
    }

    size_t pos = 8;

    // Section 2: Terrain Restrictions
    if (pos + 4 > orig_len) goto truncated;
    uint16_t restr_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    uint16_t terr_cnt = (uint16_t)(orig[pos + 2] | (orig[pos + 3] << 8));
    size_t restr_sz = 4 + (size_t)restr_cnt * 8 + (size_t)restr_cnt * (size_t)terr_cnt * 20;
    if (pos + restr_sz > orig_len) goto truncated;
    pos += restr_sz;

    // Section 3: Player Colours
    if (pos + 2 > orig_len) goto truncated;
    uint16_t color_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    size_t color_sz = 2 + (size_t)color_cnt * 36;
    if (pos + color_sz > orig_len) goto truncated;
    pos += color_sz;

    // Section 4: Sounds
    if (pos + 2 > orig_len) goto truncated;
    uint16_t snd_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;
    for (size_t s = 0; s < snd_cnt; s++) {
        if (pos + 10 > orig_len) goto truncated;
        int16_t file_count = (int16_t)(orig[pos + 4] | (orig[pos + 5] << 8));
        pos += 10;
        size_t items_sz = (size_t)file_count * 23;
        if (pos + items_sz > orig_len) goto truncated;
        pos += items_sz;
    }

    // Section 5: Graphics
    if (pos + 2 > orig_len) goto truncated;
    uint16_t g_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;
    if (pos + (size_t)g_cnt * 4 > orig_len) goto truncated;
    const uint8_t *g_ptrs = orig + pos;
    pos += (size_t)g_cnt * 4;
    for (size_t g = 0; g < g_cnt; g++) {
        int32_t p = (int32_t)(g_ptrs[g * 4] | (g_ptrs[g * 4 + 1] << 8) | (g_ptrs[g * 4 + 2] << 16) | (g_ptrs[g * 4 + 3] << 24));
        if (p) {
            if (pos + 78 > orig_len) goto truncated;
            uint16_t delta_cnt = (uint16_t)(orig[pos + 52] | (orig[pos + 53] << 8));
            uint8_t atk_snd_used = orig[pos + 56];
            uint16_t angle_cnt = (uint16_t)(orig[pos + 59] | (orig[pos + 60] << 8));
            pos += 78 + (size_t)delta_cnt * 16;
            if (atk_snd_used) pos += (size_t)angle_cnt * 12;
            if (pos > orig_len) goto truncated;
        }
    }

    // Section 6: Terrain Block
    if (pos + 140 > orig_len) goto truncated;
    size_t tb_base = pos + 24 + 114 + 2;
    int slots_found = -1;
    int candidates[4] = { (int)terr_cnt + 1, (int)terr_cnt, 42, 100 };
    for (int ci = 0; ci < 4; ci++) {
        int s = candidates[ci];
        if (s < 42) continue;
        size_t stride = 436 + 2 * (size_t)(s - 42);
        size_t b = tb_base + (size_t)s * stride + 36;
        if (b + 1444 <= orig_len &&
            memcmp(orig + b, "\xff\xff\xff\xff", 4) == 0 &&
            memcmp(orig + b + 1440, "\xff\xff\xff\xff", 4) == 0) {
            slots_found = s;
            break;
        }
    }
    if (slots_found < 0) {
        free(orig);
        sc_str_table_free(&str_table);
        if (err && errcap > 0) snprintf(err, errcap, "cannot determine terrain slot count");
        return -1;
    }
    size_t tb_stride = 436 + 2 * (size_t)(slots_found - 42);
    pos = tb_base + (size_t)slots_found * tb_stride + 16 * 1440 + 716;
    if (pos > orig_len) goto truncated;

    // Section 7: Random Maps
    if (pos + 8 > orig_len) goto truncated;
    uint32_t rm_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 8 + (size_t)rm_cnt * 72;
    if (pos > orig_len) goto truncated;
    for (size_t r = 0; r < rm_cnt; r++) {
        if (pos + 36 > orig_len) goto truncated;
        pos += 36;
        if (pos + 4 > orig_len) goto truncated;
        uint32_t lc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)lc * 44;
        if (pos + 4 > orig_len) goto truncated;
        uint32_t tc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)tc * 24;
        if (pos + 4 > orig_len) goto truncated;
        uint32_t uc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)uc * 44;
        if (pos + 4 > orig_len) goto truncated;
        uint32_t ec = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)ec * 24;
        if (pos > orig_len) goto truncated;
    }

    // Section 8: Effects
    if (pos + 4 > orig_len) goto truncated;
    uint32_t ef_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 4;
    for (size_t e = 0; e < ef_cnt; e++) {
        if (pos + 33 > orig_len) goto truncated;
        pos += 31;
        uint16_t ec = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2 + (size_t)ec * 11;
        if (pos > orig_len) goto truncated;
    }

    // Section 9: Unit Headers
    if (pos + 4 > orig_len) goto truncated;
    uint32_t uh_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 4;
    for (size_t u = 0; u < uh_cnt; u++) {
        if (pos + 1 > orig_len) goto truncated;
        uint8_t ex = orig[pos++];
        if (ex) {
            if (pos + 2 > orig_len) goto truncated;
            uint16_t cc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
            pos += 2 + (size_t)cc * 59;
            if (pos > orig_len) goto truncated;
        }
    }

    // Section 10: Civilizations
    if (pos + 2 > orig_len) goto truncated;
    uint16_t base_civ_count = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;

    ScCatalogPriv *priv = calloc(1, sizeof(ScCatalogPriv));
    if (!priv) goto oom;
    priv->civ_count = base_civ_count;
    priv->civs = calloc(base_civ_count, sizeof(ScPrivCiv));
    if (!priv->civs) { free(priv); goto oom; }

    // Array to store internal dat names for fallback civ names
    char (*civ_internal_names)[21] = calloc(base_civ_count, sizeof(*civ_internal_names));
    if (!civ_internal_names) {
        free(priv->civs); free(priv);
        goto oom;
    }

    for (size_t c = 0; c < base_civ_count; c++) {
        size_t c_start = pos;
        if (pos + 21 > orig_len) { free(civ_internal_names); goto truncated_priv; }
        // pos[0] is enabled flag; pos[1..20] is 20-byte ASCII internal civ name
        memcpy(civ_internal_names[c], orig + pos + 1, 20);
        civ_internal_names[c][20] = '\0';
        pos += 21;

        if (pos + 2 > orig_len) { free(civ_internal_names); goto truncated_priv; }
        uint16_t rc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2 + 2 + 2 + (size_t)rc * 4 + 1; // rc, tech_tree, team_bonus, resources, icon_set

        if (pos + 2 > orig_len) { free(civ_internal_names); goto truncated_priv; }
        uint16_t uc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2;

        if (pos + (size_t)uc * 4 > orig_len) { free(civ_internal_names); goto truncated_priv; }
        const uint8_t *uptrs = orig + pos;
        pos += (size_t)uc * 4;

        priv->civs[c].unit_count = uc;
        priv->civs[c].unit_offsets = calloc(uc, sizeof(uint32_t));
        if (!priv->civs[c].unit_offsets) { free(civ_internal_names); goto oom_priv; }

        for (size_t u = 0; u < uc; u++) {
            int32_t p = (int32_t)(uptrs[u * 4] | (uptrs[u * 4 + 1] << 8) | (uptrs[u * 4 + 2] << 16) | (uptrs[u * 4 + 3] << 24));
            if (p) {
                priv->civs[c].unit_offsets[u] = (uint32_t)(pos - c_start);
                pos += sc_calc_unit_len(orig, pos);
                if (pos > orig_len) { free(civ_internal_names); goto truncated_priv; }
            }
        }

        size_t c_len = pos - c_start;
        priv->civs[c].size = c_len;
        priv->civs[c].data = malloc(c_len);
        if (!priv->civs[c].data) { free(civ_internal_names); goto oom_priv; }
        memcpy(priv->civs[c].data, orig + c_start, c_len);
    }

    // Populate out->civs (civs 1..31; civ 0 Gaia is excluded from out->civs)
    if (base_civ_count > 1) {
        out->civ_count = base_civ_count - 1;
        out->civs = calloc(out->civ_count, sizeof(ScCiv));
        if (!out->civs) { free(civ_internal_names); goto oom_priv; }

        for (size_t c = 1; c < base_civ_count; c++) {
            ScCiv *sc = &out->civs[c - 1];
            sc->id = (int)c;
            // AoE2 standard string id: 10270 + civ_id (10271 = Britons, 10272 = Franks, etc.)
            int str_id = 10270 + (int)c;
            const char *name_str = sc_str_table_lookup(&str_table, str_id);
            if (name_str && name_str[0] != '\0') {
                snprintf(sc->name, sizeof(sc->name), "%s", name_str);
            } else {
                // Fall back to dat internal name
                sc_copy_clean_string(sc->name, sizeof(sc->name), civ_internal_names[c], 20);
            }
        }
    }
    free(civ_internal_names);

    // Populate out->units from civ 0 (Gaia)
    if (base_civ_count > 0 && priv->civs[0].data) {
        size_t gaia_uc = priv->civs[0].unit_count;
        size_t valid_cnt = 0;
        for (size_t u = 0; u < gaia_uc; u++) {
            if (priv->civs[0].unit_offsets[u] != 0) valid_cnt++;
        }

        out->unit_count = valid_cnt;
        out->units = calloc(valid_cnt, sizeof(ScUnit));
        if (!out->units) goto oom_priv;

        size_t uidx = 0;
        for (size_t u = 0; u < gaia_uc; u++) {
            uint32_t off = priv->civs[0].unit_offsets[u];
            if (off == 0) continue;

            const uint8_t *udata = priv->civs[0].data + off;
            ScUnit *su = &out->units[uidx++];
            su->id = (int)u;
            su->type = (int)udata[0];
            int16_t cl = (int16_t)(udata[9] | (udata[10] << 8));
            su->class_ = (cl >= 0 ? (int)cl : -1);

            int16_t nl = (int16_t)(udata[1] | (udata[2] << 8));
            uint8_t dmg = udata[149];
            size_t s1 = 150 + (size_t)dmg * 5;
            const char *int_name = (const char *)(udata + s1 + 6);
            sc_copy_clean_string(su->internal, sizeof(su->internal), int_name, nl > 0 ? (size_t)nl : 0);

            uint16_t lang_name_id = (uint16_t)(udata[5] | (udata[6] << 8));
            su->name[0] = '\0';
            if (lang_name_id != 0) {
                const char *disp_name = sc_str_table_lookup(&str_table, (int)lang_name_id);
                if (disp_name) {
                    snprintf(su->name, sizeof(su->name), "%s", disp_name);
                }
            }
        }
    }

    // Section 11: Researches (Techs)
    if (pos + 2 > orig_len) goto truncated_priv;
    uint16_t tech_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;

    out->tech_count = tech_cnt;
    out->techs = calloc(tech_cnt, sizeof(ScTech));
    if (!out->techs) goto oom_priv;

    for (size_t t = 0; t < tech_cnt; t++) {
        if (pos + 62 > orig_len) goto truncated_priv;
        ScTech *st = &out->techs[t];
        st->id = (int)t;

        uint16_t lang_name_id = (uint16_t)(orig[pos + 35] | (orig[pos + 36] << 8));
        uint16_t nl = (uint16_t)(orig[pos + 60] | (orig[pos + 61] << 8));
        pos += 62;

        if (pos + nl > orig_len) goto truncated_priv;
        const char *int_name = (const char *)(orig + pos);
        sc_copy_clean_string(st->internal, sizeof(st->internal), int_name, nl);
        pos += nl;

        st->name[0] = '\0';
        if (lang_name_id != 0) {
            const char *disp_name = sc_str_table_lookup(&str_table, (int)lang_name_id);
            if (disp_name) {
                snprintf(st->name, sizeof(st->name), "%s", disp_name);
            }
        }
    }

    out->priv = priv;
    free(orig);
    sc_str_table_free(&str_table);
    return 0;

truncated_priv:
    if (priv) {
        for (size_t i = 0; i < priv->civ_count; i++) {
            free(priv->civs[i].data);
            free(priv->civs[i].unit_offsets);
        }
        free(priv->civs);
        free(priv);
    }
truncated:
    free(orig);
    sc_str_table_free(&str_table);
    sc_catalog_free(out);
    if (err && errcap > 0) snprintf(err, errcap, "dat file truncated or corrupted: %s", dat_path);
    return -1;

oom_priv:
    if (priv) {
        for (size_t i = 0; i < priv->civ_count; i++) {
            free(priv->civs[i].data);
            free(priv->civs[i].unit_offsets);
        }
        free(priv->civs);
        free(priv);
    }
oom:
    free(orig);
    sc_str_table_free(&str_table);
    sc_catalog_free(out);
    if (err && errcap > 0) snprintf(err, errcap, "out of memory loading catalog");
    return -1;
}

// --------------------------------------------------------------------------
// Public API: sc_catalog_unit_stats
// --------------------------------------------------------------------------

int sc_catalog_unit_stats(const ScCatalog *c, int civ_id, int unit_id, ScUnitStats *out) {
    if (!c || !c->priv || !out) return -1;
    ScCatalogPriv *priv = (ScCatalogPriv *)c->priv;

    if (civ_id < 0 || (size_t)civ_id >= priv->civ_count) return -1;
    ScPrivCiv *pc = &priv->civs[civ_id];

    if (unit_id < 0 || (size_t)unit_id >= pc->unit_count) return -1;
    uint32_t off = pc->unit_offsets[unit_id];
    if (off == 0) return -1; // unit slot empty in this civ

    const uint8_t *udata = pc->data + off;
    sc_extract_unit_stats_from_raw(udata, out);
    return 0;
}

// --------------------------------------------------------------------------
// Public API: Lookups
// --------------------------------------------------------------------------

static int sc_cmp_unit_id(const void *key, const void *elem) {
    int id = *(const int *)key;
    const ScUnit *u = (const ScUnit *)elem;
    if (id < u->id) return -1;
    if (id > u->id) return 1;
    return 0;
}

const ScUnit *sc_catalog_unit(const ScCatalog *c, int unit_id) {
    if (!c || !c->units || c->unit_count == 0) return NULL;
    return (const ScUnit *)bsearch(&unit_id, c->units, c->unit_count, sizeof(ScUnit), sc_cmp_unit_id);
}

static int sc_cmp_tech_id(const void *key, const void *elem) {
    int id = *(const int *)key;
    const ScTech *t = (const ScTech *)elem;
    if (id < t->id) return -1;
    if (id > t->id) return 1;
    return 0;
}

const ScTech *sc_catalog_tech(const ScCatalog *c, int tech_id) {
    if (!c || !c->techs || c->tech_count == 0) return NULL;
    return (const ScTech *)bsearch(&tech_id, c->techs, c->tech_count, sizeof(ScTech), sc_cmp_tech_id);
}

const char *sc_catalog_civ_name(const ScCatalog *c, int civ_id) {
    if (!c || !c->civs || c->civ_count == 0) return NULL;
    for (size_t i = 0; i < c->civ_count; i++) {
        if (c->civs[i].id == civ_id) {
            return c->civs[i].name;
        }
    }
    return NULL;
}

// --------------------------------------------------------------------------
// Public API: sc_catalog_free
// --------------------------------------------------------------------------

void sc_catalog_free(ScCatalog *c) {
    if (!c) return;

    if (c->priv) {
        ScCatalogPriv *priv = (ScCatalogPriv *)c->priv;
        for (size_t i = 0; i < priv->civ_count; i++) {
            free(priv->civs[i].data);
            free(priv->civs[i].unit_offsets);
        }
        free(priv->civs);
        free(priv);
        c->priv = NULL;
    }

    free(c->civs);
    c->civs = NULL;
    c->civ_count = 0;

    free(c->units);
    c->units = NULL;
    c->unit_count = 0;

    free(c->techs);
    c->techs = NULL;
    c->tech_count = 0;
}
