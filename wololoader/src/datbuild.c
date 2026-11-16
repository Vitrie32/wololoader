// ==========================================================================
// Wololoader - Genie Dat Builder (C port of tools/build.py & tools/datfile.py)
//
// Purpose:
//   Rebuilds and patches the Age of Empires II HD 5.8 data file
//   (`empires2_x2_p1.dat`) at runtime or via standalone tools by merging
//   custom civilizations from enabled mods into the original base dat.
//
// Mirroring tools/build.py and tools/datfile.py:
//   - Byte-Identical Guarantee: The output dat produced by this C implementation
//     must remain 100% byte-identical to the Python reference builder
//     (`tools/build.py` using `tools/datfile.py`).
//   - Dat Structure: AoE2 dat files are raw deflate compressed archives containing
//     11 major sections. Since Wololoader only modifies Section 4 (Sounds) and
//     Section 10 (Civilizations), the remaining sections are parsed only as far as
//     necessary to determine their exact byte boundaries without drift, then
//     copied verbatim.
//   - Custom Civ Rules:
//     * In AoK HD.exe, civ IDs 32 (Random), 33 (Spectator), and 34 (Mirror Random)
//       are hard-coded in the game executable. Custom civilizations must start at
//       ID 35. When custom civs are present, 3 placeholder civs (WL_RESERVED_32..34)
//       cloned from civ 1 (Britons) are inserted to bridge the gap.
//     * Unit sounds filter by SoundItem.civilization. Because new civs have no sound
//       records, they would be completely silent in-game. Sound entries of the base
//       civ are cloned and tagged with the new civ ID (copy_civ_sounds).
//     * Discovery and civ assignment order strictly follows strcmp sorting of mod
//       folders and civ JSON filenames, matching Python's sorted(os.listdir()).
//     * Error reporting strings match Python's exact BuildError messages.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "datbuild.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
// Minimal Win32 declarations without including windows.h (avoids namespace pollution)
#define WL_FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define WL_MOVEFILE_REPLACE_EXISTING 0x00000001
#define WL_INVALID_HANDLE_VALUE ((void*)(intptr_t)-1)

typedef struct {
    unsigned long dwFileAttributes;
    unsigned long ftCreationTime[2];
    unsigned long ftLastAccessTime[2];
    unsigned long ftLastWriteTime[2];
    unsigned long nFileSizeHigh;
    unsigned long nFileSizeLow;
    unsigned long dwReserved0;
    unsigned long dwReserved1;
    char cFileName[260];
    char cAlternateFileName[14];
} WL_WIN32_FIND_DATAA;

__declspec(dllimport) void * __stdcall FindFirstFileA(const char *lpFileName, WL_WIN32_FIND_DATAA *lpFindFileData);
__declspec(dllimport) int    __stdcall FindNextFileA(void *hFindFile, WL_WIN32_FIND_DATAA *lpFindFileData);
__declspec(dllimport) int    __stdcall FindClose(void *hFindFile);
__declspec(dllimport) unsigned long __stdcall GetFileAttributesA(const char *lpFileName);
__declspec(dllimport) int    __stdcall MoveFileExA(const char *lpExistingFileName, const char *lpNewFileName, unsigned long dwFlags);
__declspec(dllimport) int    __stdcall CreateDirectoryA(const char *lpPathName, void *lpSecurityAttributes);

#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

// ==========================================================================
// Portability helpers (Filesystem & Atomic IO)
// ==========================================================================

// Tests if a path exists and is a directory.
static int wl_is_dir(const char *path) {
#ifdef _WIN32
    unsigned long attr = GetFileAttributesA(path);
    return (attr != 0xFFFFFFFF && (attr & WL_FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return 0;
#endif
}

// Tests if a path exists and is a regular file.
static int wl_is_file(const char *path) {
#ifdef _WIN32
    unsigned long attr = GetFileAttributesA(path);
    return (attr != 0xFFFFFFFF && !(attr & WL_FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) return 1;
    return 0;
#endif
}

// Creates a single directory.
static int wl_mkdir(const char *path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) ? 0 : -1;
#else
    return mkdir(path, 0755);
#endif
}

// Recursively creates parent directories if they do not already exist.
static void wl_ensure_parent_dirs(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char sep = tmp[i];
            tmp[i] = '\0';
            if (!wl_is_dir(tmp)) {
                wl_mkdir(tmp);
            }
            tmp[i] = sep;
        }
    }
}

// String comparator for qsort to sort directory entries.
// RULE (Sorting): Mod directories and civ JSON files must be sorted with strcmp
// to strictly mirror Python's `sorted(os.listdir())`. This guarantees that civ IDs
// are allocated deterministically regardless of OS filesystem traversal order.
static int wl_cmp_str(const void *a, const void *b) {
    return strcmp(*(const char * const *)a, *(const char * const *)b);
}

// Lists filenames in a directory (excluding '.' and '..') and sorts them with strcmp.
static int wl_list_dir(const char *path, char ***out_names, size_t *out_count) {
    *out_names = NULL;
    *out_count = 0;
    size_t cap = 16, count = 0;
    char **names = malloc(cap * sizeof(char *));
    if (!names) return -1;

#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WL_WIN32_FIND_DATAA ffd;
    void *h = FindFirstFileA(pattern, &ffd);
    if (h != WL_INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0) {
                if (count >= cap) {
                    cap *= 2;
                    char **new_n = realloc(names, cap * sizeof(char *));
                    if (!new_n) { FindClose(h); goto fail; }
                    names = new_n;
                }
                size_t nlen = strlen(ffd.cFileName);
                char *s = malloc(nlen + 1);
                if (!s) { FindClose(h); goto fail; }
                memcpy(s, ffd.cFileName, nlen + 1);
                names[count++] = s;
            }
        } while (FindNextFileA(h, &ffd));
        FindClose(h);
    }
#else
    DIR *d = opendir(path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                if (count >= cap) {
                    cap *= 2;
                    char **new_n = realloc(names, cap * sizeof(char *));
                    if (!new_n) { closedir(d); goto fail; }
                    names = new_n;
                }
                size_t nlen = strlen(ent->d_name);
                char *s = malloc(nlen + 1);
                if (!s) { closedir(d); goto fail; }
                memcpy(s, ent->d_name, nlen + 1);
                names[count++] = s;
            }
        }
        closedir(d);
    }
#endif

    if (count > 1) {
        qsort(names, count, sizeof(char *), wl_cmp_str);
    }
    *out_names = names;
    *out_count = count;
    return 0;

fail:
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    return -1;
}

// Frees a sorted directory listing allocated by wl_list_dir.
static void wl_free_dir_list(char **names, size_t count) {
    if (!names) return;
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
}

// Atomically replaces `dst` with `src`.
// Under Windows, uses MoveFileExA with MOVEFILE_REPLACE_EXISTING.
static int wl_rename_replace(const char *src, const char *dst) {
#ifdef _WIN32
    return MoveFileExA(src, dst, WL_MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(src, dst);
#endif
}

// Writes data to `<path>.tmp` and atomically renames it to `path`.
// RULE: The game engine reads empires2_x2_p1.dat during early startup; atomic
// writes prevent partial or corrupted file reads in the event of an abort or crash.
static int wl_atomic_write_file(const char *path, const void *data, size_t len, char *err, size_t errcap) {
    wl_ensure_parent_dirs(path);
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%.1500s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        snprintf(err, errcap, "cannot open temp file '%s' for writing: %s", tmp, strerror(errno));
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        snprintf(err, errcap, "failed to write %zu bytes to '%s': %s", len, tmp, strerror(errno));
        fclose(f);
        remove(tmp);
        return -1;
    }
    fclose(f);
    if (wl_rename_replace(tmp, path) != 0) {
        snprintf(err, errcap, "failed to rename '%s' to '%s': %s", tmp, path, strerror(errno));
        remove(tmp);
        return -1;
    }
    return 0;
}

// ==========================================================================
// Unit Field Descriptors & Overrides
//
// Mirroring tools/datfile.py and tools/build.py:
//   In AoE2 HD's Genie engine, units form an inheritance hierarchy:
//     Type 10: Unit (Base unit)
//     Type 20, 25: UnitFlag
//     Type 30: UnitDeadFish
//     Type 40: UnitBird
//     Type 50: UnitType50
//     Type 60: UnitProjectile
//     Type 70: UnitCreatable
//     Type 80: UnitBuilding
//
// Slicing model:
//   Because units contain dynamic-length fields (unit name string, damage graphics,
//   attacks list, armours list), fixed offsets from the start of a unit record
//   cannot be hard-coded. Instead, unit records are partitioned into 6 "slices"
//   (s0..s5), where each slice begins immediately after a variable-length list.
//   desc->slice indexes the slice, and desc->offset is the offset within that slice.
// ==========================================================================

typedef enum {
    WL_FIELD_U8,
    WL_FIELD_I8,
    WL_FIELD_U16,
    WL_FIELD_I16,
    WL_FIELD_U32,
    WL_FIELD_I32,
    WL_FIELD_F32,
    WL_FIELD_RAW
} WlFieldType;

typedef struct {
    const char *name;
    uint8_t mask;
    WlFieldType type;
    uint8_t count;
    uint8_t is_raw;
    uint8_t slice;
    uint16_t offset;
} WlFieldDesc;

// Bitmasks defining unit class membership in the Genie hierarchy
#define WL_CLS_10 (1 << 0) // Unit
#define WL_CLS_20 (1 << 1) // UnitFlag (20, 25)
#define WL_CLS_30 (1 << 2) // UnitDeadFish
#define WL_CLS_40 (1 << 3) // UnitBird
#define WL_CLS_50 (1 << 4) // UnitType50
#define WL_CLS_60 (1 << 5) // UnitProjectile
#define WL_CLS_70 (1 << 6) // UnitCreatable
#define WL_CLS_80 (1 << 7) // UnitBuilding

// Sorted table of all known unit fields matching datfile.py's FIELDS definitions.
// Kept in alphabetical order so bsearch() can locate fields in O(log N) time.
// NOTE: Slices, offsets, and types in this table must strictly mirror datfile.py's
// FIELDS layout and Genie class hierarchy. The exhaustive differential test suite
// in tools/test_fields.py validates every field and offset against build.py.
static const WlFieldDesc g_wl_fields[] = {
    { "accuracy_error_radius", 0xf0, WL_FIELD_F32, 1, 0, 4, 38 },
    { "accuracy_percent", 0xf0, WL_FIELD_I16, 1, 0, 4, 16 },
    { "adjacent_mode", 0x80, WL_FIELD_I8, 1, 0, 4, 125 },
    { "air_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 46 },
    { "animal_mode", 0xf8, WL_FIELD_I8, 1, 0, 2, 64 },
    { "annexes", 0x80, WL_FIELD_RAW, 4, 1, 4, 138 },
    { "armour_count", 0xf0, WL_FIELD_U16, 1, 0, 3, 0 },
    { "armours", 0xf0, WL_FIELD_RAW, 1, 1, 3, 2 },
    { "attack_count", 0xf0, WL_FIELD_U16, 1, 0, 2, 67 },
    { "attack_graphic", 0xf0, WL_FIELD_I16, 1, 0, 4, 42 },
    { "attack_mode", 0xff, WL_FIELD_I8, 1, 0, 1, 4 },
    { "attack_sound", 0xf8, WL_FIELD_I16, 1, 0, 2, 60 },
    { "attacks", 0xf0, WL_FIELD_RAW, 1, 1, 2, 69 },
    { "attribute", 0xff, WL_FIELD_I8, 1, 0, 0, 110 },
    { "blast_armor_level", 0xff, WL_FIELD_I8, 1, 0, 0, 81 },
    { "blast_attack_level", 0xf0, WL_FIELD_I8, 1, 0, 4, 33 },
    { "blast_width", 0xf0, WL_FIELD_F32, 1, 0, 4, 6 },
    { "button_id", 0xc0, WL_FIELD_I8, 1, 0, 4, 78 },
    { "civilization", 0xff, WL_FIELD_I8, 1, 0, 0, 111 },
    { "class_", 0xff, WL_FIELD_I16, 1, 0, 0, 9 },
    { "command_attribute", 0xff, WL_FIELD_I8, 1, 0, 0, 85 },
    { "compensation_mode", 0x20, WL_FIELD_I8, 1, 0, 4, 57 },
    { "construction_graphic_id", 0x80, WL_FIELD_I16, 1, 0, 4, 121 },
    { "construction_sound", 0x80, WL_FIELD_I16, 1, 0, 5, 6 },
    { "convert_terrain", 0xff, WL_FIELD_I8, 1, 0, 1, 5 },
    { "damage_graphic_count", 0xff, WL_FIELD_U8, 1, 0, 0, 149 },
    { "damage_graphics", 0xff, WL_FIELD_RAW, 1, 1, 0, 150 },
    { "dead_unit_id", 0xff, WL_FIELD_I16, 1, 0, 0, 43 },
    { "death_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 19 },
    { "default_armor", 0xf0, WL_FIELD_I16, 1, 0, 2, 65 },
    { "disappears_when_built", 0x80, WL_FIELD_I8, 1, 0, 4, 128 },
    { "displayed_attack", 0xf0, WL_FIELD_I16, 1, 0, 4, 46 },
    { "displayed_melee_armour", 0xf0, WL_FIELD_I16, 1, 0, 4, 44 },
    { "displayed_pierce_armour", 0xc0, WL_FIELD_I16, 1, 0, 4, 119 },
    { "displayed_range", 0xf0, WL_FIELD_F32, 1, 0, 4, 48 },
    { "displayed_reload_time", 0xf0, WL_FIELD_F32, 1, 0, 4, 52 },
    { "drop_animation_mode", 0x20, WL_FIELD_I8, 1, 0, 4, 58 },
    { "drop_site", 0xf8, WL_FIELD_I16, 2, 0, 2, 55 },
    { "dying_graphic", 0xff, WL_FIELD_I16, 2, 0, 0, 15 },
    { "dying_sound", 0xff, WL_FIELD_I16, 1, 0, 1, 2 },
    { "editor_radius", 0xff, WL_FIELD_F32, 2, 0, 0, 62 },
    { "editor_selection_colour", 0xff, WL_FIELD_I8, 1, 0, 0, 115 },
    { "enabled", 0xff, WL_FIELD_I16, 1, 0, 0, 52 },
    { "fly_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 74 },
    { "foundation_terrain_id", 0x80, WL_FIELD_I16, 1, 0, 4, 131 },
    { "frame_delay", 0xf0, WL_FIELD_I16, 1, 0, 4, 19 },
    { "garrison_capacity", 0xff, WL_FIELD_I8, 1, 0, 0, 26 },
    { "garrison_graphic", 0xc0, WL_FIELD_I32, 1, 0, 4, 89 },
    { "garrison_heal_rate", 0x80, WL_FIELD_F32, 1, 0, 5, 9 },
    { "garrison_type", 0x80, WL_FIELD_I8, 1, 0, 5, 8 },
    { "graphic_displacement", 0xf0, WL_FIELD_F32, 3, 0, 4, 21 },
    { "graphics_angle", 0x80, WL_FIELD_I16, 1, 0, 4, 126 },
    { "head_unit", 0x80, WL_FIELD_I16, 1, 0, 5, 0 },
    { "hero_mode", 0xc0, WL_FIELD_I8, 1, 0, 4, 88 },
    { "hide_in_editor", 0xff, WL_FIELD_I8, 1, 0, 0, 49 },
    { "hill_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 70 },
    { "hit_points", 0xff, WL_FIELD_I16, 1, 0, 0, 20 },
    { "hotkey", 0xff, WL_FIELD_I32, 1, 0, 0, 99 },
    { "hp_bar_height1", 0xff, WL_FIELD_F32, 1, 0, 0, 35 },
    { "hp_bar_height2", 0xff, WL_FIELD_F32, 1, 0, 0, 124 },
    { "icon_id", 0xff, WL_FIELD_I16, 1, 0, 0, 47 },
    { "id", 0xff, WL_FIELD_I16, 1, 0, 0, 3 },
    { "id2", 0xff, WL_FIELD_I16, 1, 0, 2, 0 },
    { "id3", 0xff, WL_FIELD_I16, 1, 0, 2, 2 },
    { "interaction_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 83 },
    { "language_dll_creation", 0xff, WL_FIELD_U16, 1, 0, 0, 7 },
    { "language_dll_help", 0xff, WL_FIELD_I32, 1, 0, 0, 91 },
    { "language_dll_hotkey_text", 0xff, WL_FIELD_I32, 1, 0, 0, 95 },
    { "language_dll_name", 0xff, WL_FIELD_U16, 1, 0, 0, 5 },
    { "line_of_sight", 0xff, WL_FIELD_F32, 1, 0, 0, 22 },
    { "looting_table", 0x80, WL_FIELD_I8, 6, 0, 5, 19 },
    { "max_range", 0xf0, WL_FIELD_F32, 1, 0, 4, 2 },
    { "max_total_projectiles", 0xc0, WL_FIELD_I8, 1, 0, 4, 97 },
    { "min_range", 0xf0, WL_FIELD_F32, 1, 0, 4, 34 },
    { "minimap_color", 0xff, WL_FIELD_U8, 1, 0, 0, 90 },
    { "minimap_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 84 },
    { "move_sound", 0xf8, WL_FIELD_I16, 1, 0, 2, 62 },
    { "name", 0xff, WL_FIELD_RAW, 1, 1, 1, 6 },
    { "name_length", 0xff, WL_FIELD_I16, 1, 0, 0, 1 },
    { "nothing", 0xff, WL_FIELD_I16, 1, 0, 0, 112 },
    { "old_terrain_like_id", 0x80, WL_FIELD_I16, 1, 0, 4, 133 },
    { "penetration_mode", 0x20, WL_FIELD_I8, 1, 0, 4, 59 },
    { "pile_unit", 0x80, WL_FIELD_I16, 1, 0, 5, 17 },
    { "placement_bypass_terrain", 0xff, WL_FIELD_I16, 2, 0, 0, 54 },
    { "placement_mode", 0xff, WL_FIELD_I8, 1, 0, 0, 45 },
    { "placement_terrain", 0xff, WL_FIELD_I16, 2, 0, 0, 58 },
    { "projectile_arc", 0x20, WL_FIELD_F32, 1, 0, 4, 61 },
    { "projectile_spawning_area", 0xc0, WL_FIELD_F32, 3, 0, 4, 98 },
    { "projectile_unit_id", 0xf0, WL_FIELD_I16, 1, 0, 4, 14 },
    { "reload_time", 0xf0, WL_FIELD_F32, 1, 0, 4, 10 },
    { "research_id", 0x80, WL_FIELD_I16, 1, 0, 4, 135 },
    { "resource_capacity", 0xff, WL_FIELD_I16, 1, 0, 0, 75 },
    { "resource_costs", 0xc0, WL_FIELD_RAW, 3, 1, 4, 56 },
    { "resource_decay", 0xff, WL_FIELD_F32, 1, 0, 0, 77 },
    { "resource_storages", 0xff, WL_FIELD_RAW, 3, 1, 0, 128 },
    { "rotation_speed", 0xfc, WL_FIELD_F32, 1, 0, 2, 12 },
    { "search_radius", 0xf8, WL_FIELD_F32, 1, 0, 2, 47 },
    { "secondary_projectile_unit", 0xc0, WL_FIELD_I32, 1, 0, 4, 110 },
    { "selection_effect", 0xff, WL_FIELD_I8, 1, 0, 0, 114 },
    { "selection_mask", 0xff, WL_FIELD_I8, 1, 0, 0, 107 },
    { "selection_radius", 0xff, WL_FIELD_F32, 2, 0, 0, 116 },
    { "selection_shape", 0xff, WL_FIELD_I8, 1, 0, 0, 109 },
    { "selection_shape_type", 0xff, WL_FIELD_I8, 1, 0, 0, 108 },
    { "selection_sound", 0xff, WL_FIELD_I16, 1, 0, 1, 0 },
    { "sheep_conversion", 0xf8, WL_FIELD_I16, 1, 0, 2, 45 },
    { "size_radius", 0xff, WL_FIELD_F32, 2, 0, 0, 27 },
    { "snow_graphic_id", 0x80, WL_FIELD_I16, 1, 0, 4, 123 },
    { "special_ability", 0xc0, WL_FIELD_I8, 1, 0, 4, 118 },
    { "special_graphic", 0xc0, WL_FIELD_I32, 1, 0, 4, 114 },
    { "speed", 0xfe, WL_FIELD_F32, 1, 0, 2, 4 },
    { "stack_unit_id", 0x80, WL_FIELD_I16, 1, 0, 4, 129 },
    { "standing_graphic", 0xff, WL_FIELD_I16, 2, 0, 0, 11 },
    { "stretch_mode", 0x20, WL_FIELD_I8, 1, 0, 4, 56 },
    { "ter_restriction_for_dmg_multiplier", 0xf0, WL_FIELD_I16, 1, 0, 4, 0 },
    { "terrain_restriction", 0xff, WL_FIELD_I16, 1, 0, 0, 72 },
    { "total_projectiles", 0xc0, WL_FIELD_F32, 1, 0, 4, 93 },
    { "tower_mode", 0xf0, WL_FIELD_I8, 1, 0, 4, 18 },
    { "tracking_unit", 0xfc, WL_FIELD_I16, 1, 0, 2, 17 },
    { "tracking_unit_density", 0xfc, WL_FIELD_F32, 1, 0, 2, 20 },
    { "tracking_unit_used", 0xfc, WL_FIELD_I8, 1, 0, 2, 19 },
    { "train_location_id", 0xc0, WL_FIELD_I16, 1, 0, 4, 76 },
    { "train_sound", 0xff, WL_FIELD_I16, 2, 0, 0, 39 },
    { "train_time", 0xc0, WL_FIELD_I16, 1, 0, 4, 74 },
    { "transform_unit", 0x80, WL_FIELD_I16, 1, 0, 5, 2 },
    { "trigger_type", 0xff, WL_FIELD_I8, 1, 0, 0, 82 },
    { "type", 0xff, WL_FIELD_U8, 1, 0, 0, 0 },
    { "unknown1", 0xff, WL_FIELD_I16, 1, 0, 0, 50 },
    { "unknown11", 0xfc, WL_FIELD_I8, 1, 0, 2, 16 },
    { "unknown16", 0xfc, WL_FIELD_I8, 1, 0, 2, 24 },
    { "unknown16b", 0xfc, WL_FIELD_F32, 5, 0, 2, 25 },
    { "unknown24", 0x20, WL_FIELD_I8, 1, 0, 4, 60 },
    { "unknown26", 0xc0, WL_FIELD_F32, 1, 0, 4, 79 },
    { "unknown27", 0xc0, WL_FIELD_F32, 1, 0, 4, 83 },
    { "unknown28", 0xc0, WL_FIELD_I8, 1, 0, 4, 87 },
    { "unknown33", 0x80, WL_FIELD_I8, 1, 0, 4, 137 },
    { "unknown35", 0x80, WL_FIELD_F32, 1, 0, 5, 13 },
    { "unknown3a", 0xff, WL_FIELD_F32, 1, 0, 0, 86 },
    { "unknown6", 0xff, WL_FIELD_I8, 1, 0, 0, 104 },
    { "unknown7", 0xff, WL_FIELD_I8, 1, 0, 0, 105 },
    { "unknown8", 0xff, WL_FIELD_I8, 1, 0, 0, 106 },
    { "unknown_sound", 0x80, WL_FIELD_I16, 1, 0, 5, 4 },
    { "unselectable", 0xff, WL_FIELD_I8, 1, 0, 0, 103 },
    { "villager_mode", 0xf8, WL_FIELD_I8, 1, 0, 2, 59 },
    { "visible_in_fog", 0xff, WL_FIELD_I8, 1, 0, 0, 71 },
    { "walking_graphic", 0xfc, WL_FIELD_I16, 2, 0, 2, 8 },
    { "work_rate", 0xf8, WL_FIELD_F32, 1, 0, 2, 51 },
};

#define WL_NUM_FIELDS (sizeof(g_wl_fields) / sizeof(g_wl_fields[0]))

// Binary search comparator for g_wl_fields
static int wl_cmp_field_key(const void *k, const void *m) {
    return strcmp((const char *)k, ((const WlFieldDesc *)m)->name);
}

// Maps unit type code to bitmask representing its inheritance tree
static uint8_t wl_unit_class_mask(uint8_t t) {
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

// Returns the class name string for unit type t, used in error messages
static const char *wl_unit_class_name(uint8_t t) {
    switch (t) {
        case 10: return "Unit";
        case 20: case 25: return "UnitFlag";
        case 30: return "UnitDeadFish";
        case 40: return "UnitBird";
        case 50: return "UnitType50";
        case 60: return "UnitProjectile";
        case 70: return "UnitCreatable";
        case 80: return "UnitBuilding";
        default: return "Unknown";
    }
}

// Computes the total byte length of a single unit record at data[pos] by parsing
// its dynamic-length name, damage graphics, and attack/armour list lengths.
static size_t wl_calc_unit_len(const unsigned char *data, size_t pos) {
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

// Formats a JSON value as Python repr() for build.py-identical error messages.
// RULE (Error formatting): Python's build.py prints `%r` in error messages:
//   - bools become 'True' or 'False'
//   - numbers use their exact raw literal representation
//   - strings are quoted with single quotes: 'text'
//   - null becomes 'None', arrays '[]', objects '{}'
static void wl_json_format_repr(const WlJsonVal *val, char *buf, size_t bufcap) {
    if (!val) {
        snprintf(buf, bufcap, "None");
        return;
    }
    switch (val->type) {
        case WL_JSON_BOOL:
            snprintf(buf, bufcap, "%s", val->u.bool_val ? "True" : "False");
            break;
        case WL_JSON_NUMBER:
            snprintf(buf, bufcap, "%s", val->u.num.raw);
            break;
        case WL_JSON_STRING:
            snprintf(buf, bufcap, "'%s'", val->u.str);
            break;
        case WL_JSON_NULL:
            snprintf(buf, bufcap, "None");
            break;
        case WL_JSON_ARRAY:
            snprintf(buf, bufcap, "[]");
            break;
        case WL_JSON_OBJECT:
            snprintf(buf, bufcap, "{}");
            break;
    }
}

// Writes a JSON numeric value into unit binary data according to field type.
// Validates integer vs float matching Python's _as_int() / _set_field().
static int wl_write_field_number(uint8_t *dest, WlFieldType type, const WlJsonVal *val,
                                 const char *where, const char *field, char *err, size_t errcap) {
    if (val->type != WL_JSON_NUMBER) {
        char rep[128];
        wl_json_format_repr(val, rep, sizeof(rep));
        snprintf(err, errcap, "%s: field '%s' needs a number, got %s", where, field, rep);
        return -1;
    }

    if (type == WL_FIELD_F32) {
        float f = (float)val->u.num.dval;
        uint32_t u;
        memcpy(&u, &f, sizeof(float));
        dest[0] = (uint8_t)(u & 0xFF);
        dest[1] = (uint8_t)((u >> 8) & 0xFF);
        dest[2] = (uint8_t)((u >> 16) & 0xFF);
        dest[3] = (uint8_t)((u >> 24) & 0xFF);
        return 0;
    }

    // Integer field: reject non-integral floats (e.g., 1.5 into an int field)
    if (val->u.num.is_float && val->u.num.dval != (double)(int64_t)val->u.num.dval) {
        snprintf(err, errcap, "%s: field '%s' is an integer, got %s", where, field, val->u.num.raw);
        return -1;
    }
    int64_t iv = (int64_t)val->u.num.dval;

    switch (type) {
        case WL_FIELD_U8:
            *dest = (uint8_t)iv;
            break;
        case WL_FIELD_I8:
            *(int8_t*)dest = (int8_t)iv;
            break;
        case WL_FIELD_U16: {
            uint16_t v = (uint16_t)iv;
            dest[0] = (uint8_t)(v & 0xFF);
            dest[1] = (uint8_t)((v >> 8) & 0xFF);
            break;
        }
        case WL_FIELD_I16: {
            int16_t v = (int16_t)iv;
            dest[0] = (uint8_t)(v & 0xFF);
            dest[1] = (uint8_t)((v >> 8) & 0xFF);
            break;
        }
        case WL_FIELD_U32: {
            uint32_t v = (uint32_t)iv;
            dest[0] = (uint8_t)(v & 0xFF);
            dest[1] = (uint8_t)((v >> 8) & 0xFF);
            dest[2] = (uint8_t)((v >> 16) & 0xFF);
            dest[3] = (uint8_t)((v >> 24) & 0xFF);
            break;
        }
        case WL_FIELD_I32: {
            int32_t v = (int32_t)iv;
            dest[0] = (uint8_t)(v & 0xFF);
            dest[1] = (uint8_t)((v >> 8) & 0xFF);
            dest[2] = (uint8_t)((v >> 16) & 0xFF);
            dest[3] = (uint8_t)((v >> 24) & 0xFF);
            break;
        }
        default:
            break;
    }
    return 0;
}

// Applies a field override to a unit record.
// Validates field existence, class inheritance mask, raw structured field restrictions,
// and array length for multi-value fields (e.g., radius_size, size).
// Address of a field in a unit record. Slices skip past variable-length data (name string,
// damage graphics, attack list, armour list).
static uint8_t *wl_unit_field_addr(uint8_t *unit_data, const WlFieldDesc *desc) {
    uint8_t t = unit_data[0];
    int16_t nl = (int16_t)(unit_data[1] | (unit_data[2] << 8));
    uint8_t dmg = unit_data[149];
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
        uint16_t atk_cnt = (uint16_t)(unit_data[p_atk] | (unit_data[p_atk + 1] << 8));
        s3 = p_atk + 2 + (size_t)atk_cnt * 4;
        uint16_t arm_cnt = (uint16_t)(unit_data[s3] | (unit_data[s3 + 1] << 8));
        s4 = s3 + 2 + (size_t)arm_cnt * 4;
        if (t == 80) {
            s5 = s4 + 138 + 40;
        }
    }
    size_t slices[6] = { s0, s1, s2, s3, s4, s5 };
    return unit_data + slices[desc->slice] + desc->offset;
}

static int wl_set_unit_field(uint8_t *unit_data, const char *where, const char *field,
                             const WlJsonVal *val, char *err, size_t errcap) {
    uint8_t t = unit_data[0];
    uint8_t umask = wl_unit_class_mask(t);
    const char *cname = wl_unit_class_name(t);

    const WlFieldDesc *desc = bsearch(field, g_wl_fields, WL_NUM_FIELDS, sizeof(WlFieldDesc), wl_cmp_field_key);
    if (!desc || !(desc->mask & umask)) {
        snprintf(err, errcap, "%s: unknown field '%s' for unit type %s", where, field, cname);
        return -1;
    }

    // RULE: Structured / complex fields (damage_graphics, attacks, armours, annexes)
    // cannot be overridden with simple numbers or arrays; matching build.py _set_field.
    if (desc->is_raw) {
        snprintf(err, errcap, "%s: field '%s' is a structured/raw field and cannot be overridden", where, field);
        return -1;
    }

    uint8_t *dest = wl_unit_field_addr(unit_data, desc);

    if (desc->count > 1) {
        // Multi-value list field (e.g. radius_size, size)
        if (val->type != WL_JSON_ARRAY || val->u.arr.count != desc->count) {
            snprintf(err, errcap, "%s: field '%s' needs a list of %d numbers", where, field, (int)desc->count);
            return -1;
        }
        size_t elem_sz = (desc->type == WL_FIELD_F32) ? 4 : (desc->type == WL_FIELD_I8 ? 1 : 2);
        for (size_t k = 0; k < desc->count; k++) {
            if (wl_write_field_number(dest + k * elem_sz, desc->type, val->u.arr.items[k],
                                      where, field, err, errcap) != 0) {
                return -1;
            }
        }
    } else {
        // Scalar field
        if (wl_write_field_number(dest, desc->type, val, where, field, err, errcap) != 0) {
            return -1;
        }
    }
    return 0;
}

// ==========================================================================
// Civ & Dat In-Memory Structures
// ==========================================================================

// RULE (Reserved IDs):
// In AoK HD.exe, civilization IDs 32..34 are reserved:
//   32 = RANDOM
//   33 = SPECTATOR
//   34 = MIRROR_RANDOM
// Custom mod civilizations must start at ID 35.
#define FIRST_RESERVED_ID 32
#define NUM_RESERVED 3
#define CIV_NAME_BYTES 20 // Fixed 20-byte ASCII NUL-padded civilization name field

typedef struct {
    uint8_t *data;
    size_t size;
    uint16_t unit_count;
    uint16_t resource_count;
    uint32_t *unit_offsets;
} WlDatCiv;

typedef struct {
    int16_t id;
    int16_t play_delay;
    int16_t file_count;
    int32_t cache_time;
    uint8_t *items;
    size_t cap;
} WlDatSound;

#pragma pack(push, 1)
typedef struct {
    int8_t   type;
    int16_t  a;
    int16_t  b;
    int16_t  c;
    float    d;
} WlDatEffectCmd;
#pragma pack(pop)

typedef struct {
    char name[31];
    uint16_t cmd_count;
    uint16_t cmd_cap;
    WlDatEffectCmd *commands;
} WlDatEffect;

typedef struct {
    size_t offset;
    uint16_t cmd_count;
    size_t cmds_offset;
} WlOrigEffect;

typedef struct {
    size_t offset;
    int16_t civ;
    int16_t effect_id;
    int16_t research_location;
    uint16_t name_length;
} WlOrigResearch;

typedef struct {
    uint8_t data[78];       // Fixed 78-byte graphic header
    int32_t source_ptr;     // Pointer value to copy from source graphic
    uint8_t *extra;         // deltas (16 bytes each) + attack sounds (12 bytes per angle), or NULL
    size_t extra_len;
} WlCustomGraphic;

// Graphics being added (unit "graphics", unique_building "slp"): the original section and the
// custom records appended so far. A new graphic's id = orig_count + its index in *p_custom.
typedef struct {
    const uint8_t *orig;
    size_t section;           // offset of the graphics section (u16 count, i32 pointers, records)
    uint16_t orig_count;
    WlCustomGraphic **p_custom;
    size_t *p_cnt, *p_cap;
} WlGfxCtx;

typedef struct {
    uint8_t header[62];
    uint16_t name_length;
    char *name;
} WlDatResearch;

// Constants for unique_building (mirroring build.py)
#define WL_AGE_TECH_DARK 104
#define WL_AGE_TECH_FEUDAL 101
#define WL_AGE_TECH_CASTLE 102
#define WL_AGE_TECH_IMPERIAL 103
#define WL_PAGE_MILITARY 10
#define WL_PAGE_MODS 2       // "mods": economy kind; buildpage.c moves slots 1300..1400 to the mod page
#define WL_UB_TEMPLATE_TECH 570
#define WL_CMD_SID_BASE 31210  // strings of building commands: triples (name, create/research, rich help)

// Builder extension state: tracks slots/strings/lua across all civs (mirroring ext dict in build.py)
typedef struct {
    size_t slots;   // unit slots used (buildings + train commands)
    size_t sids;    // string ID triples allocated
    struct { int id; char *text; } *strings;
    size_t strings_count;
    size_t strings_cap;
    char **lua;     // lua command lines for commands.txt
    size_t lua_count;
    size_t lua_cap;
} WlBuildExt;

static void wl_ext_init(WlBuildExt *ext) {
    ext->slots = 0;
    ext->sids = 0;
    ext->strings = NULL;
    ext->strings_count = 0;
    ext->strings_cap = 0;
    ext->lua = NULL;
    ext->lua_count = 0;
    ext->lua_cap = 0;
}

static void wl_ext_free(WlBuildExt *ext) {
    for (size_t i = 0; i < ext->strings_count; i++) {
        free(ext->strings[i].text);
    }
    free(ext->strings);
    for (size_t i = 0; i < ext->lua_count; i++) {
        free(ext->lua[i]);
    }
    free(ext->lua);
}

static int wl_ext_add_string(WlBuildExt *ext, int id, const char *text, char *err, size_t errcap) {
    if (ext->strings_count >= ext->strings_cap) {
        size_t ncap = (ext->strings_cap == 0) ? 16 : (ext->strings_cap * 2);
        void *tmp = realloc(ext->strings, ncap * sizeof(*ext->strings));
        if (!tmp) {
            snprintf(err, errcap, "out of memory");
            return -1;
        }
        ext->strings = tmp;
        ext->strings_cap = ncap;
    }
    char *copy = malloc(strlen(text) + 1);
    if (!copy) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    strcpy(copy, text);
    ext->strings[ext->strings_count].id = id;
    ext->strings[ext->strings_count].text = copy;
    ext->strings_count++;
    return 0;
}

static int wl_ext_add_lua(WlBuildExt *ext, const char *line, char *err, size_t errcap) {
    if (ext->lua_count >= ext->lua_cap) {
        size_t ncap = (ext->lua_cap == 0) ? 16 : (ext->lua_cap * 2);
        char **tmp = realloc(ext->lua, ncap * sizeof(char *));
        if (!tmp) {
            snprintf(err, errcap, "out of memory");
            return -1;
        }
        ext->lua = tmp;
        ext->lua_cap = ncap;
    }
    char *copy = malloc(strlen(line) + 1);
    if (!copy) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    strcpy(copy, line);
    ext->lua[ext->lua_count++] = copy;
    return 0;
}

// Allocates a triple of string IDs for building commands (mirroring _new_sids in build.py)
static int wl_new_sids(WlBuildExt *ext, char *err, size_t errcap) {
    int sid = WL_CMD_SID_BASE + 3 * (int)ext->sids;
    ext->sids++;
    if (sid + 2 > 32767) {
        snprintf(err, errcap, "too many named building commands (string ids above 32767)");
        return -1;
    }
    return sid;
}

static int wl_replace_civ_units(WlDatCiv *civ, int u1, const uint8_t *data1, size_t len1,
                                int u2, const uint8_t *data2, size_t len2, char *err, size_t errcap) {
    size_t hdr_len = 30 + (size_t)civ->resource_count * 4 + (size_t)civ->unit_count * 4;
    size_t new_total_size = hdr_len;
    for (size_t u = 0; u < civ->unit_count; u++) {
        if (u == (size_t)u1) {
            new_total_size += len1;
        } else if (u == (size_t)u2) {
            new_total_size += len2;
        } else if (civ->unit_offsets[u] != 0) {
            new_total_size += wl_calc_unit_len(civ->data, civ->unit_offsets[u]);
        }
    }

    uint8_t *new_data = malloc(new_total_size);
    if (!new_data) {
        snprintf(err, errcap, "out of memory allocating new civ units data");
        return -1;
    }
    uint32_t *new_offsets = malloc(civ->unit_count * sizeof(uint32_t));
    if (!new_offsets) {
        free(new_data);
        snprintf(err, errcap, "out of memory allocating new civ unit offsets");
        return -1;
    }

    memcpy(new_data, civ->data, hdr_len);

    size_t uptrs_off = 30 + (size_t)civ->resource_count * 4;
    int32_t one = 1;
    // u2 = -1 means "only one unit" (unique building): never write its pointer slot, or the
    // 4 bytes before the pointer table (end of the civ header) get overwritten.
    if (u1 >= 0) memcpy(new_data + uptrs_off + (size_t)u1 * 4, &one, 4);
    if (u2 >= 0) memcpy(new_data + uptrs_off + (size_t)u2 * 4, &one, 4);

    size_t cur_pos = hdr_len;
    for (size_t u = 0; u < civ->unit_count; u++) {
        if (u == (size_t)u1) {
            new_offsets[u] = (uint32_t)cur_pos;
            memcpy(new_data + cur_pos, data1, len1);
            cur_pos += len1;
        } else if (u == (size_t)u2) {
            new_offsets[u] = (uint32_t)cur_pos;
            memcpy(new_data + cur_pos, data2, len2);
            cur_pos += len2;
        } else if (civ->unit_offsets[u] != 0) {
            new_offsets[u] = (uint32_t)cur_pos;
            size_t u_len = wl_calc_unit_len(civ->data, civ->unit_offsets[u]);
            memcpy(new_data + cur_pos, civ->data + civ->unit_offsets[u], u_len);
            cur_pos += u_len;
        } else {
            new_offsets[u] = 0;
        }
    }

    free(civ->data);
    free(civ->unit_offsets);
    civ->data = new_data;
    civ->unit_offsets = new_offsets;
    civ->size = new_total_size;
    return 0;
}

// Validates and encodes a dat_name string into fixed 20-byte ASCII NUL-padded field.
static int wl_make_name(const char *text, const char *where, uint8_t *out_name20, char *err, size_t errcap) {
    size_t len = strlen(text);
    if (len == 0) {
        snprintf(err, errcap, "%s: dat_name is empty", where);
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)text[i] > 127) {
            snprintf(err, errcap, "%.256s: dat_name '%.256s' must be ASCII", where, text);
            return -1;
        }
    }
    if (len > CIV_NAME_BYTES - 1) {
        len = CIV_NAME_BYTES - 1;
    }
    memset(out_name20, 0, CIV_NAME_BYTES);
    memcpy(out_name20, text, len);
    return 0;
}

// Deep-copies a civilization record from civs[base] into out_civ.
static int wl_clone_civ(WlDatCiv *civs, size_t num_civs, int64_t base, const char *where,
                        WlDatCiv *out_civ, char *err, size_t errcap) {
    if (base < 0 || (size_t)base >= num_civs) {
        snprintf(err, errcap, "%s: base %lld is not an existing civ id (0..%zu)",
                 where, (long long)base, num_civs - 1);
        return -1;
    }
    const WlDatCiv *src = &civs[base];
    out_civ->size = src->size;
    out_civ->unit_count = src->unit_count;
    out_civ->resource_count = src->resource_count;
    out_civ->data = malloc(src->size);
    if (!out_civ->data) {
        snprintf(err, errcap, "out of memory cloning civ");
        return -1;
    }
    memcpy(out_civ->data, src->data, src->size);
    out_civ->unit_offsets = malloc(src->unit_count * sizeof(uint32_t));
    if (!out_civ->unit_offsets) {
        free(out_civ->data);
        snprintf(err, errcap, "out of memory cloning civ");
        return -1;
    }
    memcpy(out_civ->unit_offsets, src->unit_offsets, src->unit_count * sizeof(uint32_t));
    return 0;
}

// Frees dynamically allocated buffers in a WlDatCiv structure.
static void wl_free_civ(WlDatCiv *civ) {
    if (!civ) return;
    free(civ->data);
    free(civ->unit_offsets);
    civ->data = NULL;
    civ->unit_offsets = NULL;
}

// RULE (Sound item copying):
// In AoE2, SoundItem records have a civilization filter at offset 19-20 (int16_t).
// Unit sound events look up items matching the player's civilization; a new civ with
// no items in the list would be completely silent.
// This function duplicates all SoundItem records belonging to `base` and tags the
// duplicates with `cid`.
static int wl_copy_civ_sounds(WlDatSound *sounds, size_t num_sounds, int base, int cid) {
    int added = 0;
    for (size_t s_idx = 0; s_idx < num_sounds; s_idx++) {
        WlDatSound *s = &sounds[s_idx];
        size_t extra_cnt = 0;
        for (int i = 0; i < s->file_count; i++) {
            int16_t c = (int16_t)(s->items[i * 23 + 19] | (s->items[i * 23 + 20] << 8));
            if (c == base) extra_cnt++;
        }
        if (extra_cnt > 0) {
            if ((size_t)s->file_count + extra_cnt > s->cap) {
                s->cap = (s->file_count + extra_cnt) * 2;
                uint8_t *new_items = realloc(s->items, s->cap * 23);
                if (!new_items) return -1;
                s->items = new_items;
            }
            int orig_fc = s->file_count;
            for (int i = 0; i < orig_fc; i++) {
                int16_t c = (int16_t)(s->items[i * 23 + 19] | (s->items[i * 23 + 20] << 8));
                if (c == base) {
                    uint8_t *dst = s->items + s->file_count * 23;
                    memcpy(dst, s->items + i * 23, 23);
                    dst[19] = (uint8_t)(cid & 0xFF);
                    dst[20] = (uint8_t)((cid >> 8) & 0xFF);
                    s->file_count++;
                    added++;
                }
            }
        }
    }
    return added;
}

// ==========================================================================
// Civ File Discovery
// ==========================================================================

typedef struct {
    char mod[128];
    char fn[128];
    char path[2048];
} WlCivFile;

// Scans mods_dir for enabled mods, and collects all civ definitions (<mod>/civs/*.json).
// RULE (Mod enabling & sorting):
//   - Mod folders are processed in strcmp sorted order.
//   - If mod.json has `"enabled": false`, the mod is skipped entirely.
//   - Civ JSON filenames are processed in strcmp sorted order within each mod.
static int wl_find_civ_files(const char *mods_dir, WlCivFile **out_files, size_t *out_count, char *err, size_t errcap) {
    *out_files = NULL;
    *out_count = 0;
    if (!wl_is_dir(mods_dir)) return 0;

    char **mod_names = NULL;
    size_t mod_count = 0;
    if (wl_list_dir(mods_dir, &mod_names, &mod_count) != 0) {
        snprintf(err, errcap, "failed to list mods directory '%.500s'", mods_dir);
        return -1;
    }

    size_t cap = 16, count = 0;
    WlCivFile *files = malloc(cap * sizeof(WlCivFile));
    if (!files) {
        wl_free_dir_list(mod_names, mod_count);
        snprintf(err, errcap, "out of memory");
        return -1;
    }

    for (size_t m = 0; m < mod_count; m++) {
        const char *mod = mod_names[m];
        char mdir[2048];
        snprintf(mdir, sizeof(mdir), "%.1000s/%.256s", mods_dir, mod);
        if (!wl_is_dir(mdir)) continue;

        char meta_path[2048];
        snprintf(meta_path, sizeof(meta_path), "%.1500s/mod.json", mdir);
        if (wl_is_file(meta_path)) {
            size_t meta_len = 0;
            char *meta_bytes = wl_read_file(meta_path, &meta_len);
            if (!meta_bytes) {
                snprintf(err, errcap, "%s: %s", meta_path, strerror(errno));
                wl_free_dir_list(mod_names, mod_count);
                free(files);
                return -1;
            }
            WlJsonVal *meta_json = wl_json_parse(meta_bytes, err, errcap);
            free(meta_bytes);
            if (!meta_json) {
                char tmp_err[2048];
                snprintf(tmp_err, sizeof(tmp_err), "%.1000s: %.1000s", meta_path, err);
                strncpy(err, tmp_err, errcap - 1);
                err[errcap - 1] = '\0';
                wl_free_dir_list(mod_names, mod_count);
                free(files);
                return -1;
            }
            const WlJsonVal *en = wl_json_get(meta_json, "enabled");
            if (en && en->type == WL_JSON_BOOL && en->u.bool_val == 0) {
                wl_json_free(meta_json);
                continue; // Skip disabled mod
            }
            wl_json_free(meta_json);
        }

        char cdir[2048];
        snprintf(cdir, sizeof(cdir), "%.1500s/civs", mdir);
        if (wl_is_dir(cdir)) {
            char **civ_names = NULL;
            size_t civ_count = 0;
            if (wl_list_dir(cdir, &civ_names, &civ_count) == 0) {
                for (size_t c = 0; c < civ_count; c++) {
                    const char *fn = civ_names[c];
                    size_t fn_len = strlen(fn);
                    if (fn_len >= 5 &&
                        fn[fn_len - 5] == '.' &&
                        tolower((unsigned char)fn[fn_len - 4]) == 'j' &&
                        tolower((unsigned char)fn[fn_len - 3]) == 's' &&
                        tolower((unsigned char)fn[fn_len - 2]) == 'o' &&
                        tolower((unsigned char)fn[fn_len - 1]) == 'n') {

                        if (count >= cap) {
                            cap *= 2;
                            WlCivFile *new_f = realloc(files, cap * sizeof(WlCivFile));
                            if (!new_f) {
                                wl_free_dir_list(civ_names, civ_count);
                                wl_free_dir_list(mod_names, mod_count);
                                free(files);
                                snprintf(err, errcap, "out of memory");
                                return -1;
                            }
                            files = new_f;
                        }
                        strncpy(files[count].mod, mod, sizeof(files[count].mod) - 1);
                        files[count].mod[sizeof(files[count].mod) - 1] = '\0';
                        strncpy(files[count].fn, fn, sizeof(files[count].fn) - 1);
                        files[count].fn[sizeof(files[count].fn) - 1] = '\0';
                        snprintf(files[count].path, sizeof(files[count].path), "%.1500s/%.256s", cdir, fn);
                        count++;
                    }
                }
                wl_free_dir_list(civ_names, civ_count);
            }
        }
    }
    wl_free_dir_list(mod_names, mod_count);

    *out_files = files;
    *out_count = count;
    return 0;
}

// ==========================================================================
// Effect parsing and creation helpers
// ==========================================================================

static int wl_make_effect_name(const char *text, const char *where, char *out31, char *err, size_t errcap) {
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)text[i] > 127) {
            snprintf(err, errcap, "%s: effect name '%s' must be ASCII", where, text);
            return -1;
        }
    }
    size_t copy_len = len > 30 ? 30 : len;
    memcpy(out31, text, copy_len);
    memset(out31 + copy_len, 0, 31 - copy_len);
    return 0;
}

// Creates a unit internal name: ASCII only (drop non-ASCII), max 30 chars + NUL.
// Returns the total length (including NUL) in *out_len, writes to out_buf (caller must provide 31+ bytes).
static int wl_make_unit_name(const char *text, const char *where, char *out_buf, size_t *out_len,
                              char *err, size_t errcap) {
    if (!text || text[0] == '\0') {
        snprintf(err, errcap, "%s: unit name is empty", where);
        return -1;
    }
    // Drop non-ASCII characters
    size_t out_pos = 0;
    for (size_t i = 0; text[i] != '\0' && out_pos < 30; i++) {
        if ((unsigned char)text[i] < 128) {
            out_buf[out_pos++] = text[i];
        }
    }
    if (out_pos == 0) {
        snprintf(err, errcap, "%s: unit name '%s' has no ASCII characters", where, text);
        return -1;
    }
    out_buf[out_pos] = '\0';
    *out_len = out_pos + 1;  // including NUL
    return 0;
}

static int wl_get_json_int(const WlJsonVal *val, int def, int *out, const char *where, const char *fld, char *err, size_t errcap) {
    if (!val) { *out = def; return 0; }
    if (val->type == WL_JSON_NUMBER) {
        *out = (int)val->u.num.ival;
        return 0;
    }
    if (val->type == WL_JSON_STRING && val->u.str) {
        char *end = NULL;
        long v = strtol(val->u.str, &end, 10);
        if (end && *end == '\0') { *out = (int)v; return 0; }
    }
    snprintf(err, errcap, "%s: invalid effect command field: %s must be an integer", where, fld);
    return -1;
}

static int wl_get_json_float(const WlJsonVal *val, float def, float *out, const char *where, const char *fld, char *err, size_t errcap) {
    if (!val) { *out = def; return 0; }
    if (val->type == WL_JSON_NUMBER) {
        *out = val->u.num.is_float ? (float)val->u.num.dval : (float)val->u.num.ival;
        return 0;
    }
    if (val->type == WL_JSON_STRING && val->u.str) {
        char *end = NULL;
        float v = strtof(val->u.str, &end);
        if (end && *end == '\0') { *out = v; return 0; }
    }
    snprintf(err, errcap, "%s: invalid effect command field: %s must be a number", where, fld);
    return -1;
}

static int wl_parse_effect_command(const WlJsonVal *cmd_spec, const char *where,
                                  WlDatEffectCmd *out_cmd, char *err, size_t errcap) {
    if (cmd_spec->type != WL_JSON_OBJECT) {
        snprintf(err, errcap, "%s: effect command must be a JSON object", where);
        return -1;
    }
    const WlJsonVal *t_val = wl_json_get(cmd_spec, "type");
    if (!t_val) {
        snprintf(err, errcap, "%s: effect command missing 'type'", where);
        return -1;
    }
    int cmd_type = 0;
    if (wl_get_json_int(t_val, 0, &cmd_type, where, "type", err, errcap) != 0) return -1;

    const WlJsonVal *a_val = wl_json_get(cmd_spec, "a");
    if (!a_val) a_val = wl_json_get(cmd_spec, "unit");
    int a = -1;
    if (wl_get_json_int(a_val, -1, &a, where, "a/unit", err, errcap) != 0) return -1;

    const WlJsonVal *b_val = wl_json_get(cmd_spec, "b");
    if (!b_val) b_val = wl_json_get(cmd_spec, "class");
    if (!b_val) b_val = wl_json_get(cmd_spec, "mode");
    int b = -1;
    if (wl_get_json_int(b_val, -1, &b, where, "b/class/mode", err, errcap) != 0) return -1;

    const WlJsonVal *c_val = wl_json_get(cmd_spec, "c");
    if (!c_val) c_val = wl_json_get(cmd_spec, "attr");
    int c = -1;
    if (wl_get_json_int(c_val, -1, &c, where, "c/attr", err, errcap) != 0) return -1;

    const WlJsonVal *d_val = wl_json_get(cmd_spec, "d");
    if (!d_val) d_val = wl_json_get(cmd_spec, "value");
    if (!d_val) d_val = wl_json_get(cmd_spec, "amount");
    float d = 0.0f;
    if (wl_get_json_float(d_val, 0.0f, &d, where, "d/value/amount", err, errcap) != 0) return -1;

    out_cmd->type = (int8_t)cmd_type;
    out_cmd->a = (int16_t)a;
    out_cmd->b = (int16_t)b;
    out_cmd->c = (int16_t)c;
    out_cmd->d = d;
    return 0;
}

static int wl_effect_add_command(WlDatEffect *eff, const WlDatEffectCmd *cmd, char *err, size_t errcap) {
    if (eff->cmd_count >= eff->cmd_cap) {
        uint16_t new_cap = (eff->cmd_cap == 0) ? 16 : (eff->cmd_cap < 32768 ? eff->cmd_cap * 2 : 65535);
        if (new_cap <= eff->cmd_cap) {
            snprintf(err, errcap, "too many effect commands");
            return -1;
        }
        WlDatEffectCmd *new_cmds = realloc(eff->commands, (size_t)new_cap * sizeof(WlDatEffectCmd));
        if (!new_cmds) {
            snprintf(err, errcap, "out of memory allocating effect commands");
            return -1;
        }
        eff->commands = new_cmds;
        eff->cmd_cap = new_cap;
    }
    eff->commands[eff->cmd_count++] = *cmd;
    return 0;
}

// ==========================================================================
// Tech tree section: castle unique unit records
// ==========================================================================
// The tech tree is the dat section after the researches: a 36 byte header, then
// four record lists in this order: ages, buildings, units, researches (layout:
// tools/datfile.py TechTreeAge / TechTreeBuilding / TechTreeUnit / TechTreeResearch).
// A custom castle unique unit needs its own TechTreeUnit pair, or the tech tree
// screen misplaces its lines (re/research/techtree.md).  The section is parsed only
// when a unique unit is added (wl_tt_ensure_parsed), so when no unique unit is used
// the original bytes are copied untouched, exactly as before.
// All integers are little endian i32 unless noted (u8 counts).
//
// Mirrors add_uu_tech_tree() in tools/build.py: keep both in sync.

enum { WL_TT_AGE = 0, WL_TT_BUILDING = 1, WL_TT_UNIT = 2, WL_TT_RESEARCH = 3 };

#define WL_TT_HEADER_LEN 36   // 7 x i32 game rules, u8 age/building/unit/research counts, i32 total groups
#define WL_TT_UNIT_CNT_OFF 97 // TechTreeUnit: unit_count (u8) follows the 97 bytes of id..vertical_line
#define WL_TT_UNIT_LIST_OFF 98 // ...and its unit list (i32 each) starts right after it

typedef struct {
    uint8_t *data;   // complete record bytes (owned)
    size_t len;
} WlTtRec;

typedef struct {
    WlTtRec *recs;
    size_t cnt, cap;
} WlTtList;

typedef struct {
    const uint8_t *src;   // original tech tree section (borrowed from orig)
    size_t src_len;
    int parsed;           // 1 once lists[] hold the records
    uint8_t header[WL_TT_HEADER_LEN];
    WlTtList lists[4];    // indexed by WL_TT_*
    uint8_t *out;         // re-serialized section, filled by wl_tt_serialize (owned)
    size_t out_len;
} WlTtState;

static int32_t wl_tt_get_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void wl_tt_put_i32(uint8_t *p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xFF);
    p[1] = (uint8_t)((u >> 8) & 0xFF);
    p[2] = (uint8_t)((u >> 16) & 0xFF);
    p[3] = (uint8_t)((u >> 24) & 0xFF);
}

// Walks one record of `kind` starting at d (avail bytes left).
//   Age / Building / Research: id (4) + status (1) [+ upper_building (4) for research],
//     then three u8-counted i32 lists in order: buildings, units, researches,
//     then a fixed tail (Age 110, Building 103, Research 96 bytes).
//   Unit: id, status, upper_building, connected_slots_used, other_connected_ids (40),
//     other_connections (40), vertical_line: 97 bytes, then u8 unit_count, the unit
//     list, and 16 fixed bytes (location_in_age, required_research, line_mode,
//     enabling_research).
// Sets *cnt_off to the offset of the unit-list count byte and *len to the record length.
// Returns 0 on success, -1 if the record does not fit in avail bytes.
static int wl_tt_walk(int kind, const uint8_t *d, size_t avail, size_t *cnt_off, size_t *len) {
    size_t p;
    if (kind == WL_TT_UNIT) {
        if (avail < WL_TT_UNIT_LIST_OFF) return -1;
        *cnt_off = WL_TT_UNIT_CNT_OFF;
        p = WL_TT_UNIT_LIST_OFF + 4 * (size_t)d[WL_TT_UNIT_CNT_OFF] + 16;
    } else {
        size_t prefix = (kind == WL_TT_RESEARCH) ? 9 : 5;
        size_t tail = (kind == WL_TT_AGE) ? 110 : (kind == WL_TT_BUILDING) ? 103 : 96;
        p = prefix;
        for (int l = 0; l < 3; l++) {        // buildings, units, researches
            if (p >= avail) return -1;
            size_t n = d[p];
            if (l == 1) *cnt_off = p;
            p += 1 + 4 * n;
        }
        p += tail;
    }
    if (p > avail) return -1;
    *len = p;
    return 0;
}

static int wl_tt_push_rec(WlTtList *L, const uint8_t *data, size_t len, char *err, size_t errcap) {
    if (L->cnt >= L->cap) {
        size_t ncap = L->cap ? L->cap * 2 : 64;
        WlTtRec *nr = realloc(L->recs, ncap * sizeof(WlTtRec));
        if (!nr) { snprintf(err, errcap, "out of memory"); return -1; }
        L->recs = nr;
        L->cap = ncap;
    }
    uint8_t *copy = malloc(len);
    if (!copy) { snprintf(err, errcap, "out of memory"); return -1; }
    memcpy(copy, data, len);
    L->recs[L->cnt].data = copy;
    L->recs[L->cnt].len = len;
    L->cnt++;
    return 0;
}

// Splits the whole section into the four record lists.  Returns 0 on success.
static int wl_tt_parse(WlTtState *tt, char *err, size_t errcap) {
    if (tt->src_len < WL_TT_HEADER_LEN) { snprintf(err, errcap, "tech tree section truncated"); return -1; }
    memcpy(tt->header, tt->src, WL_TT_HEADER_LEN);
    // Header u8 counts: age 28, building 29, unit 30, research 31 (same order as WL_TT_*)
    size_t counts[4] = { tt->header[28], tt->header[29], tt->header[30], tt->header[31] };
    size_t pos = WL_TT_HEADER_LEN;
    for (int k = 0; k < 4; k++) {
        for (size_t i = 0; i < counts[k]; i++) {
            size_t cnt_off = 0, len = 0;
            if (wl_tt_walk(k, tt->src + pos, tt->src_len - pos, &cnt_off, &len) != 0) {
                snprintf(err, errcap, "tech tree record truncated");
                return -1;
            }
            if (wl_tt_push_rec(&tt->lists[k], tt->src + pos, len, err, errcap) != 0) return -1;
            pos += len;
        }
    }
    if (pos != tt->src_len) {
        snprintf(err, errcap, "tech tree parse ended at %lu of %lu bytes",
                 (unsigned long)pos, (unsigned long)tt->src_len);
        return -1;
    }
    tt->parsed = 1;
    return 0;
}

static int wl_tt_ensure_parsed(WlTtState *tt, char *err, size_t errcap) {
    return tt->parsed ? 0 : wl_tt_parse(tt, err, errcap);
}

static void wl_tt_free(WlTtState *tt) {
    for (int k = 0; k < 4; k++) {
        for (size_t i = 0; i < tt->lists[k].cnt; i++) free(tt->lists[k].recs[i].data);
        free(tt->lists[k].recs);
        tt->lists[k].recs = NULL;
        tt->lists[k].cnt = 0;
        tt->lists[k].cap = 0;
    }
    free(tt->out);
    tt->out = NULL;
    tt->out_len = 0;
    tt->parsed = 0;
}

// Inserts n ids into the unit list of rec right after the first entry equal to anchor
// (Python: units.insert(units.index(anchor) + 1, ...)).
// Returns 1 when inserted, 0 when anchor is not in the list, -2 when the u8 count
// would exceed 255, -1 on out of memory.
static int wl_tt_insert_after(WlTtRec *rec, int kind, int32_t anchor, const int32_t *ins, size_t n) {
    size_t cnt_off = 0, len = 0;
    if (wl_tt_walk(kind, rec->data, rec->len, &cnt_off, &len) != 0) return -1;
    size_t ucount = rec->data[cnt_off];
    const uint8_t *list = rec->data + cnt_off + 1;
    size_t at = ucount;
    for (size_t i = 0; i < ucount; i++) {
        if (wl_tt_get_i32(list + 4 * i) == anchor) { at = i; break; }
    }
    if (at == ucount) return 0;
    if (ucount + n > 255) return -2;

    size_t head = cnt_off + 1 + 4 * (at + 1);   // bytes up to and including the anchor
    size_t new_len = rec->len + 4 * n;
    uint8_t *nd = malloc(new_len);
    if (!nd) return -1;
    memcpy(nd, rec->data, cnt_off);
    nd[cnt_off] = (uint8_t)(ucount + n);
    memcpy(nd + cnt_off + 1, list, 4 * (at + 1));
    for (size_t j = 0; j < n; j++) wl_tt_put_i32(nd + head + 4 * j, ins[j]);
    memcpy(nd + head + 4 * n, rec->data + head, rec->len - head);
    free(rec->data);
    rec->data = nd;
    rec->len = new_len;
    return 1;
}

// Copy of unit record src with its unit list replaced by units[0..n-1]; all other
// bytes (id, connections, vertical_line, location, research links) are kept.
static int wl_tt_unit_with_list(const WlTtRec *src, const int32_t *units, size_t n, WlTtRec *dst) {
    size_t old_n = src->data[WL_TT_UNIT_CNT_OFF];
    size_t tail_off = WL_TT_UNIT_LIST_OFF + 4 * old_n;
    size_t tail_len = src->len - tail_off;
    dst->len = WL_TT_UNIT_LIST_OFF + 4 * n + tail_len;
    dst->data = malloc(dst->len);
    if (!dst->data) return -1;
    memcpy(dst->data, src->data, WL_TT_UNIT_CNT_OFF);
    dst->data[WL_TT_UNIT_CNT_OFF] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) wl_tt_put_i32(dst->data + WL_TT_UNIT_LIST_OFF + 4 * i, units[i]);
    memcpy(dst->data + WL_TT_UNIT_LIST_OFF + 4 * n, src->data + tail_off, tail_len);
    return 0;
}

// Adds the tech tree records of one custom castle unique unit (see the file comment).
//   base record  id=base_uu, units=[base_elite], enabling_research=base_avail_id
//   elite record id=base_elite, required_research=base elite tech
// Clones: U (units=[E], enabling_research=new_avail_id) and E (id=E,
// required_research=new_elite_id, other_connected_ids: base_uu -> U). Both are
// appended to the unit list.  U then E are inserted after base_elite in building 82,
// and U / E after base_uu / base_elite in every age list that holds them.
static int wl_tt_add_unique_unit(WlTtState *tt, int base_avail_id, int new_avail_id, int new_elite_id,
                                 int U, int E, const char *where, char *err, size_t errcap) {
    WlTtList *units = &tt->lists[WL_TT_UNIT];

    // Base record: the first unit record whose enabling_research (last i32) is the base avail tech.
    WlTtRec *base_rec = NULL;
    for (size_t i = 0; i < units->cnt; i++) {
        WlTtRec *r = &units->recs[i];
        if (wl_tt_get_i32(r->data + r->len - 4) == base_avail_id) { base_rec = r; break; }
    }
    if (!base_rec || base_rec->data[WL_TT_UNIT_CNT_OFF] < 1) {
        snprintf(err, errcap, "%s: no tech tree unit record for base avail tech %d", where, base_avail_id);
        return -1;
    }
    int32_t base_uu = wl_tt_get_i32(base_rec->data);
    int32_t base_elite = wl_tt_get_i32(base_rec->data + WL_TT_UNIT_LIST_OFF);   // units[0]

    // Elite record: the unit record whose id is base_elite.
    WlTtRec *elite_rec = NULL;
    for (size_t i = 0; i < units->cnt; i++) {
        if (wl_tt_get_i32(units->recs[i].data) == base_elite) { elite_rec = &units->recs[i]; break; }
    }
    if (!elite_rec) {
        snprintf(err, errcap, "%s: no tech tree unit record for base elite unit %d", where, (int)base_elite);
        return -1;
    }

    // Clone the base record: id=U, units=[E], enabling_research=new avail tech.
    WlTtRec new_base;
    int32_t e_only = E;
    if (wl_tt_unit_with_list(base_rec, &e_only, 1, &new_base) != 0) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    wl_tt_put_i32(new_base.data, U);
    wl_tt_put_i32(new_base.data + new_base.len - 4, new_avail_id);

    // Clone the elite record: id=E, required_research=new elite tech,
    // other_connected_ids (10 x i32 at offset 13): base_uu -> U.
    WlTtRec new_elite;
    new_elite.len = elite_rec->len;
    new_elite.data = malloc(new_elite.len);
    if (!new_elite.data) {
        free(new_base.data);
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    memcpy(new_elite.data, elite_rec->data, new_elite.len);
    wl_tt_put_i32(new_elite.data, E);
    wl_tt_put_i32(new_elite.data + new_elite.len - 12, new_elite_id);
    for (size_t i = 0; i < 10; i++) {
        uint8_t *slot = new_elite.data + 13 + 4 * i;
        if (wl_tt_get_i32(slot) == base_uu) wl_tt_put_i32(slot, U);
    }

    // Castle building 82: U then E right after base_elite.
    WlTtRec *bld = NULL;
    for (size_t i = 0; i < tt->lists[WL_TT_BUILDING].cnt; i++) {
        WlTtRec *r = &tt->lists[WL_TT_BUILDING].recs[i];
        if (wl_tt_get_i32(r->data) == 82) { bld = r; break; }
    }
    int32_t ue[2] = { U, E };
    int rc = bld ? wl_tt_insert_after(bld, WL_TT_BUILDING, base_elite, ue, 2) : 0;
    if (rc != 1) {
        free(new_base.data);
        free(new_elite.data);
        if (rc == -2) snprintf(err, errcap, "%s: castle building unit list exceeds 255 entries", where);
        else if (rc == 0) snprintf(err, errcap, "%s: castle building 82 does not list unit %d", where, (int)base_elite);
        else snprintf(err, errcap, "out of memory");
        return -1;
    }

    // Age lists: U after base_uu, E after base_elite (only where they appear).
    for (size_t i = 0; i < tt->lists[WL_TT_AGE].cnt; i++) {
        WlTtRec *age = &tt->lists[WL_TT_AGE].recs[i];
        int32_t age_id = wl_tt_get_i32(age->data);
        int32_t pu = U, pe = E;
        int r1 = wl_tt_insert_after(age, WL_TT_AGE, base_uu, &pu, 1);
        int r2 = (r1 >= 0) ? wl_tt_insert_after(age, WL_TT_AGE, base_elite, &pe, 1) : r1;
        if (r1 < 0 || r2 < 0) {
            free(new_base.data);
            free(new_elite.data);
            if (r1 == -2 || r2 == -2) snprintf(err, errcap, "%s: age %d unit list exceeds 255 entries", where, (int)age_id);
            else snprintf(err, errcap, "out of memory");
            return -1;
        }
    }

    // Append the clones (U, then E) to the unit list.
    if (units->cnt + 2 > 255) {
        free(new_base.data);
        free(new_elite.data);
        snprintf(err, errcap, "%s: tech tree unit count exceeds 255 (u8 in the dat)", where);
        return -1;
    }
    if (wl_tt_push_rec(units, new_base.data, new_base.len, err, errcap) != 0) {
        free(new_base.data);
        free(new_elite.data);
        return -1;
    }
    if (wl_tt_push_rec(units, new_elite.data, new_elite.len, err, errcap) != 0) {
        free(new_base.data);
        free(new_elite.data);
        return -1;
    }
    free(new_base.data);
    free(new_elite.data);
    return 0;
}

// Rebuilds the section from the lists: header (counts follow the list sizes),
// then every record in list order.  Result in tt->out / tt->out_len.
static int wl_tt_serialize(WlTtState *tt, char *err, size_t errcap) {
    size_t total = WL_TT_HEADER_LEN;
    for (int k = 0; k < 4; k++) {
        if (tt->lists[k].cnt > 255) { snprintf(err, errcap, "tech tree list exceeds 255 entries"); return -1; }
        for (size_t i = 0; i < tt->lists[k].cnt; i++) total += tt->lists[k].recs[i].len;
    }
    uint8_t *out = malloc(total);
    if (!out) { snprintf(err, errcap, "out of memory"); return -1; }
    memcpy(out, tt->header, WL_TT_HEADER_LEN);
    for (int k = 0; k < 4; k++) out[28 + k] = (uint8_t)tt->lists[k].cnt;
    size_t pos = WL_TT_HEADER_LEN;
    for (int k = 0; k < 4; k++) {
        for (size_t i = 0; i < tt->lists[k].cnt; i++) {
            memcpy(out + pos, tt->lists[k].recs[i].data, tt->lists[k].recs[i].len);
            pos += tt->lists[k].recs[i].len;
        }
    }
    free(tt->out);
    tt->out = out;
    tt->out_len = total;
    return 0;
}

// "unknown <what>key(s) ['a', 'b']" for keys of obj not in known (sorted like Python's sorted()).
static int wl_check_known_keys(const WlJsonVal *obj, const char *const *known, size_t nknown,
                               const char *w, const char *what, char *err, size_t errcap) {
    char *extra[16];
    size_t n = 0;
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        const char *k = obj->u.obj.members[i].key;
        int ok = 0;
        for (size_t j = 0; j < nknown; j++) if (strcmp(k, known[j]) == 0) { ok = 1; break; }
        if (!ok && n < 16) extra[n++] = (char *)k;
    }
    if (!n) return 0;
    qsort(extra, n, sizeof(char *), wl_cmp_str);
    char buf[512];
    size_t pos = snprintf(buf, sizeof(buf), "[");
    for (size_t i = 0; i < n && pos < sizeof(buf); i++)
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s'%s'", i ? ", " : "", extra[i]);
    if (pos < sizeof(buf)) snprintf(buf + pos, sizeof(buf) - pos, "]");
    snprintf(err, errcap, "%s: unknown %skey(s) %s", w, what, buf);
    return -1;
}

static int wl_json_is_int(const WlJsonVal *v) {
    return v && v->type == WL_JSON_NUMBER && !v->u.num.is_float;
}

static void wl_put16(uint8_t *p, int v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }
static void wl_put32(uint8_t *p, int32_t v) { wl_put16(p, v & 0xFFFF); wl_put16(p + 2, (v >> 16) & 0xFFFF); }

static int wl_push_effect(WlDatEffect **p_effs, size_t *p_cnt, size_t *p_cap, const WlDatEffect *e,
                          char *err, size_t errcap) {
    if (*p_cnt >= *p_cap) {
        size_t ncap = *p_cap ? *p_cap * 2 : 16;
        WlDatEffect *n = realloc(*p_effs, ncap * sizeof(WlDatEffect));
        if (!n) { snprintf(err, errcap, "out of memory"); return -1; }
        *p_effs = n;
        *p_cap = ncap;
    }
    (*p_effs)[(*p_cnt)++] = *e;
    return 0;
}

static int wl_push_research(WlDatResearch **p_res, size_t *p_cnt, size_t *p_cap, const WlDatResearch *r,
                            char *err, size_t errcap) {
    if (*p_cnt >= *p_cap) {
        size_t ncap = *p_cap ? *p_cap * 2 : 16;
        WlDatResearch *n = realloc(*p_res, ncap * sizeof(WlDatResearch));
        if (!n) { snprintf(err, errcap, "out of memory"); return -1; }
        *p_res = n;
        *p_cap = ncap;
    }
    (*p_res)[(*p_cnt)++] = *r;
    return 0;
}

static int wl_effect_add_cmd(WlDatEffect *e, const WlDatEffectCmd *c, char *err, size_t errcap) {
    if (e->cmd_count >= e->cmd_cap) {
        size_t ncap = e->cmd_cap ? (size_t)e->cmd_cap * 2 : 4;
        if (ncap > 65535) { snprintf(err, errcap, "too many effect commands"); return -1; }
        WlDatEffectCmd *n = realloc(e->commands, ncap * sizeof(WlDatEffectCmd));
        if (!n) { snprintf(err, errcap, "out of memory"); return -1; }
        e->commands = n;
        e->cmd_cap = (uint16_t)ncap;
    }
    e->commands[e->cmd_count++] = *c;
    return 0;
}

// Civ 1 must hold the dat's "RESERVED" placeholder at unit slot `slot` (same check as build.py).
static int wl_check_reserved(const WlDatCiv *civs, int slot, const char *w, char *err, size_t errcap) {
    if ((size_t)slot < civs[1].unit_count && civs[1].unit_offsets[slot] != 0) {
        const uint8_t *u = civs[1].data + civs[1].unit_offsets[slot];
        int16_t nl = (int16_t)(u[1] | (u[2] << 8));
        size_t name_off = 150 + (size_t)u[149] * 5 + 6;
        if (nl >= 9 && memcmp(u + name_off, "RESERVED", 8) == 0 && u[name_off + 8] == '\0') return 0;
    }
    snprintf(err, errcap, "%s: civ 1 unit at %d is not named 'RESERVED'", w, slot);
    return -1;
}

static int wl_set_unit_int(uint8_t *u, const char *w, const char *field, long long v, char *err, size_t errcap) {
    WlJsonVal jv;
    memset(&jv, 0, sizeof(jv));
    jv.type = WL_JSON_NUMBER;
    jv.u.num.ival = v;
    jv.u.num.dval = (double)v;
    return wl_set_unit_field(u, w, field, &jv, err, errcap);
}

// ---- unit graphics ("graphics" in unit specs; mirrors add_graphic / apply_graphics in build.py) ----
// Graphic record (datfile.py Graphic): name 21 @0, filename 13 @21, slp i32 @34, delta_count u16 @52,
// attack_sound_used i8 @56, frame_count u16 @57, angle_count u16 @59, id i16 @74 (78-byte header),
// then delta_count deltas of 16 bytes and, if attack_sound_used, angle_count attack sounds of 12 bytes.
static int wl_gfx_find(const WlGfxCtx *g, int gid, const uint8_t **hdr, const uint8_t **extra,
                       size_t *extra_len, int32_t *ptr) {
    if (gid < 0) return -1;
    if (gid < g->orig_count) {
        const uint8_t *ptrs = g->orig + g->section + 2;
        int32_t p;
        memcpy(&p, ptrs + (size_t)gid * 4, 4);
        if (!p) return -1;
        size_t pos = g->section + 2 + (size_t)g->orig_count * 4;
        for (int i = 0; i <= gid; i++) {
            int32_t pi;
            memcpy(&pi, ptrs + (size_t)i * 4, 4);
            if (!pi) continue;
            const uint8_t *r = g->orig + pos;
            size_t len = 78 + (size_t)(r[52] | (r[53] << 8)) * 16;
            if (r[56]) len += (size_t)(r[59] | (r[60] << 8)) * 12;
            if (i == gid) {
                *hdr = r; *extra = r + 78; *extra_len = len - 78; *ptr = p;
                return 0;
            }
            pos += len;
        }
        return -1;
    }
    size_t k = (size_t)(gid - g->orig_count);
    if (k >= *g->p_cnt) return -1;
    const WlCustomGraphic *c = &(*g->p_custom)[k];
    *hdr = c->data; *extra = c->extra; *extra_len = c->extra_len; *ptr = c->source_ptr;
    return 0;
}

// Appends a copy of graphic `src` drawing SLP `slp` (frames / angles < 0: keep); *out = new id.
static int wl_gfx_add(const WlGfxCtx *g, int src, int32_t slp, int frames, int angles, int *out,
                      const char *where, char *err, size_t errcap) {
    const uint8_t *hdr, *extra;
    size_t extra_len;
    int32_t ptr;
    if (wl_gfx_find(g, src, &hdr, &extra, &extra_len, &ptr) != 0) {
        snprintf(err, errcap, "%s: graphic %d does not exist", where, src);
        return -1;
    }
    if (*g->p_cnt >= *g->p_cap) {
        size_t ncap = *g->p_cap ? *g->p_cap * 2 : 16;
        WlCustomGraphic *n = realloc(*g->p_custom, ncap * sizeof(WlCustomGraphic));
        if (!n) { snprintf(err, errcap, "out of memory"); return -1; }
        *g->p_custom = n;
        *g->p_cap = ncap;
        // hdr / extra may point into the old array (src was a custom graphic): look it up again
        if (wl_gfx_find(g, src, &hdr, &extra, &extra_len, &ptr) != 0) return -1;
    }
    WlCustomGraphic c;
    memcpy(c.data, hdr, 78);
    c.source_ptr = ptr;
    size_t keep = extra_len;
    int gid = g->orig_count + (int)*g->p_cnt;
    if (gid > 32767) { snprintf(err, errcap, "%s: too many graphics", where); return -1; }
    char nm[16];
    snprintf(nm, sizeof nm, "WLG%d", gid);
    memset(c.data, 0, 21);
    memcpy(c.data, nm, strlen(nm));
    memset(c.data + 21, 0, 13);
    memcpy(c.data + 21, nm, strlen(nm));
    memcpy(c.data + 34, &slp, 4);
    c.data[74] = (uint8_t)(gid & 0xFF);
    c.data[75] = (uint8_t)((gid >> 8) & 0xFF);
    if (frames >= 0) { c.data[57] = (uint8_t)(frames & 0xFF); c.data[58] = (uint8_t)(frames >> 8); }
    int cur_angles = c.data[59] | (c.data[60] << 8);
    if (angles >= 0 && angles != cur_angles) {
        c.data[59] = (uint8_t)(angles & 0xFF);
        c.data[60] = (uint8_t)(angles >> 8);
        if (c.data[56]) keep = (size_t)(c.data[52] | (c.data[53] << 8)) * 16;   // drop the attack sounds
        c.data[56] = 0;
    }
    c.extra = NULL;
    c.extra_len = keep;
    if (keep) {
        c.extra = malloc(keep);
        if (!c.extra) { snprintf(err, errcap, "out of memory"); return -1; }
        memcpy(c.extra, extra, keep);
    }
    (*g->p_custom)[(*g->p_cnt)++] = c;
    *out = gid;
    return 0;
}

static const struct { const char *anim, *field; int idx; } k_anims[] = {
    { "standing", "standing_graphic", 0 }, { "standing2", "standing_graphic", 1 },
    { "dying", "dying_graphic", 0 }, { "decay", "dying_graphic", 1 },
    { "walking", "walking_graphic", 0 }, { "running", "walking_graphic", 1 },
    { "attack", "attack_graphic", 0 },
};

static int wl_apply_graphics(const WlGfxCtx *g, uint8_t *unit, const WlJsonVal *spec,
                             const char *where, char *err, size_t errcap) {
    if (!spec || spec->type != WL_JSON_OBJECT) {
        snprintf(err, errcap, "%s: graphics must be an object", where);
        return -1;
    }
    for (size_t i = 0; i < spec->u.obj.count; i++) {
        const char *anim = spec->u.obj.members[i].key;
        const WlJsonVal *v = spec->u.obj.members[i].val;
        char w[1100];
        snprintf(w, sizeof w, "%.1000s graphics.%s", where, anim);
        int a = -1;
        for (size_t k = 0; k < sizeof k_anims / sizeof *k_anims; k++) if (!strcmp(k_anims[k].anim, anim)) a = (int)k;
        if (a < 0) {
            snprintf(err, errcap, "%s: unknown animation '%s' (use standing, standing2, dying, decay, walking, running, attack)", w, anim);
            return -1;
        }
        const WlFieldDesc *desc = bsearch(k_anims[a].field, g_wl_fields, WL_NUM_FIELDS, sizeof(WlFieldDesc), wl_cmp_field_key);
        if (!desc || !(desc->mask & wl_unit_class_mask(unit[0]))) {
            snprintf(err, errcap, "%s: this unit type has no %s", w, k_anims[a].field);
            return -1;
        }
        long long slp, frames = -1, angles = -1;
        if (wl_json_is_int(v)) {
            slp = v->u.num.ival;
        } else {
            const WlJsonVal *sv = v->type == WL_JSON_OBJECT ? wl_json_get(v, "slp") : NULL;
            if (!wl_json_is_int(sv)) {
                snprintf(err, errcap, "%s: needs an SLP id or {\"slp\": id, \"frames\": n, \"angles\": n}", w);
                return -1;
            }
            static const char *const known[] = { "slp", "frames", "angles" };
            if (wl_check_known_keys(v, known, 3, w, "", err, errcap) != 0) return -1;
            slp = sv->u.num.ival;
            const WlJsonVal *fv = wl_json_get(v, "frames"), *av = wl_json_get(v, "angles");
            if (fv) frames = wl_json_is_int(fv) ? fv->u.num.ival : 0;
            if (av) angles = wl_json_is_int(av) ? av->u.num.ival : 0;
        }
        if (slp <= 0 || slp >= (1LL << 31)) { snprintf(err, errcap, "%s: slp must be a positive integer", w); return -1; }
        if (frames != -1 && (frames < 1 || frames > 1000)) { snprintf(err, errcap, "%s: frames must be 1..1000", w); return -1; }
        if (angles != -1 && (angles < 1 || angles > 64)) { snprintf(err, errcap, "%s: angles must be 1..64", w); return -1; }
        uint8_t *f = wl_unit_field_addr(unit, desc) + 2 * k_anims[a].idx;
        int src = (int16_t)(f[0] | (f[1] << 8));
        if (src < 0) { snprintf(err, errcap, "%s: the unit has no %s animation to copy", w, anim); return -1; }
        int gid;
        if (wl_gfx_add(g, src, (int32_t)slp, (int)frames, (int)angles, &gid, w, err, errcap) != 0) return -1;
        f[0] = (uint8_t)(gid & 0xFF);
        f[1] = (uint8_t)((gid >> 8) & 0xFF);
    }
    return 0;
}

// One "train" command: a copy of a type-70 unit in the next free slot 1400-ext->slots, trained at B.
// Unit layout (datfile.py): type@0, name_length@1, id@3, ..., name@150+5*damage_graphics+6, then id2, id3.
static int wl_cmd_train(const WlGfxCtx *gctx, WlDatCiv *civ, const WlDatCiv *civs, int B, int button, const WlJsonVal *t,
                        WlDatEffect *avail_eff, size_t uu_total, WlBuildExt *ext,
                        const char *w, char *err, size_t errcap) {
    const WlJsonVal *obj = NULL;
    long long src;
    if (wl_json_is_int(t)) {
        src = t->u.num.ival;
    } else if (t->type == WL_JSON_OBJECT && wl_json_get(t, "copy_from")) {
        obj = t;
        static const char *const known[] = { "copy_from", "name", "help", "units", "graphics" };
        if (wl_check_known_keys(t, known, 5, w, "train ", err, errcap) != 0) return -1;
        const WlJsonVal *cf = wl_json_get(t, "copy_from");
        if (!wl_json_is_int(cf)) {
            snprintf(err, errcap, "%s: train copy_from unit does not exist", w);
            return -1;
        }
        src = cf->u.num.ival;
    } else {
        snprintf(err, errcap, "%s: train must be a unit id or an object with copy_from", w);
        return -1;
    }
    if (src < 0 || (size_t)src >= civ->unit_count || civ->unit_offsets[src] == 0) {
        snprintf(err, errcap, "%s: train copy_from unit %lld does not exist", w, src);
        return -1;
    }
    if (civ->data[civ->unit_offsets[src]] != 70) {
        snprintf(err, errcap, "%s: train copy_from unit %lld must be a trainable unit (type 70)", w, src);
        return -1;
    }
    int slot = 1400 - (int)ext->slots;
    ext->slots++;
    if (slot < 1300 || slot <= 1225 + 2 * (int)uu_total) {
        snprintf(err, errcap, "%s: no free unit slot left (1300..1400)", w);
        return -1;
    }
    if (wl_check_reserved(civs, slot, w, err, errcap) != 0) return -1;

    const WlJsonVal *name_v = obj ? wl_json_get(obj, "name") : NULL;
    const char *nm = NULL, *hp = NULL;
    char un[32];
    size_t un_len = 0;
    if (name_v) {
        const WlJsonVal *help_v = wl_json_get(obj, "help");
        if (name_v->type != WL_JSON_STRING || (help_v && help_v->type != WL_JSON_STRING)) {
            snprintf(err, errcap, "%s: train name and help must be strings", w);
            return -1;
        }
        nm = name_v->u.str;
        hp = help_v ? help_v->u.str : nm;
    }

    // Copy the source unit, renamed when a name is given (the name block changes length).
    size_t len = wl_calc_unit_len(civ->data, civ->unit_offsets[src]);
    const uint8_t *s = civ->data + civ->unit_offsets[src];
    size_t s1 = 150 + (size_t)s[149] * 5 + 6;            // start of the internal name
    size_t old_nl = (size_t)(uint16_t)(s[1] | (s[2] << 8));
    int sid = 0;
    if (name_v) {
        sid = wl_new_sids(ext, err, errcap);
        if (sid < 0) return -1;
        size_t cl = strlen(nm), hl = strlen(hp);
        char *t1 = malloc(cl + 16), *t2 = malloc(cl + hl + 96);
        if (!t1 || !t2) { free(t1); free(t2); snprintf(err, errcap, "out of memory"); return -1; }
        sprintf(t1, "Create %s", nm);
        sprintf(t2, "Create <b>%s<b> (<cost>) \n%s \n<hp> <attack> <armor> <piercearmor> <range>", nm, hp);
        int rc = wl_ext_add_string(ext, sid, nm, err, errcap) || wl_ext_add_string(ext, sid + 1, t1, err, errcap) ||
                 wl_ext_add_string(ext, sid + 2, t2, err, errcap);
        free(t1);
        free(t2);
        if (rc) return -1;
        if (wl_make_unit_name(nm, w, un, &un_len, err, errcap) != 0) return -1;
    }
    size_t nl = name_v ? un_len : old_nl;
    size_t nlen = len - old_nl + nl;
    uint8_t *u = malloc(nlen);
    if (!u) { snprintf(err, errcap, "out of memory"); return -1; }
    memcpy(u, s, s1);
    memcpy(u + s1, name_v ? (const uint8_t *)un : s + s1, nl);
    memcpy(u + s1 + nl, s + s1 + old_nl, len - s1 - old_nl);
    wl_put16(u + 1, (int)nl);
    wl_put16(u + 3, slot);                                 // id
    wl_put16(u + s1 + nl, slot);                           // id2
    wl_put16(u + s1 + nl + 2, slot);                       // id3

    char fw[1100];
    snprintf(fw, sizeof(fw), "%.1000s unit %d", w, slot);
    int rc = wl_set_unit_int(u, w, "train_location_id", B, err, errcap) ||
             wl_set_unit_int(u, w, "button_id", button, err, errcap) ||
             wl_set_unit_int(u, w, "enabled", 0, err, errcap);
    if (!rc && name_v)
        rc = wl_set_unit_int(u, w, "language_dll_name", sid, err, errcap) ||
             wl_set_unit_int(u, w, "language_dll_creation", sid + 1, err, errcap) ||
             wl_set_unit_int(u, w, "language_dll_help", 79000 + sid + 2, err, errcap) ||
             wl_set_unit_int(u, w, "language_dll_hotkey_text", 150000 + sid, err, errcap);
    const WlJsonVal *units_v = obj ? wl_json_get(obj, "units") : NULL;
    if (!rc && units_v) {
        if (units_v->type != WL_JSON_OBJECT) {
            snprintf(err, errcap, "%s: train units must be an object", w);
            rc = -1;
        }
        for (size_t i = 0; !rc && i < units_v->u.obj.count; i++)
            rc = wl_set_unit_field(u, fw, units_v->u.obj.members[i].key, units_v->u.obj.members[i].val, err, errcap);
    }
    const WlJsonVal *gfx_v = obj ? wl_json_get(obj, "graphics") : NULL;
    if (!rc && gfx_v) rc = wl_apply_graphics(gctx, u, gfx_v, fw, err, errcap);
    if (!rc) rc = wl_replace_civ_units(civ, slot, u, nlen, -1, NULL, 0, err, errcap);
    free(u);
    if (rc) return -1;
    WlDatEffectCmd ec = { .type = 2, .a = (int16_t)slot, .b = 1, .c = -1, .d = 0.0f };
    return wl_effect_add_cmd(avail_eff, &ec, err, errcap);
}

// One "research" command: a new tech (copy of research copy_from, default 22) at building B with its
// own effect. Research header (datfile.py Research): required_techs i16[6]@0, resource_costs
// {type i16, amount i16, flag i8}[3]@12, required_tech_count@27, civ@29, full_tech_mode@31,
// research_location@33, language_dll_name@35, language_dll_description@37, research_time@39,
// effect_id@41, type@43, icon_id@45, button_id i8@47, language_dll_help i32@48, tech_tree i32@52,
// hotkey i32@56, name_length@60.
static int wl_cmd_research(int cid, int B, int button, int age, size_t k, const WlJsonVal *r,
                           const uint8_t *orig, const WlOrigResearch *orig_researches, size_t orig_res_cnt,
                           WlDatResearch **p_res, size_t *p_res_cnt, size_t *p_res_cap,
                           size_t ef_cnt, WlDatEffect **p_effs, size_t *p_effs_cnt, size_t *p_effs_cap,
                           WlBuildExt *ext, const char *w, char *err, size_t errcap) {
    static const int age_techs[4] = { WL_AGE_TECH_DARK, WL_AGE_TECH_FEUDAL, WL_AGE_TECH_CASTLE, WL_AGE_TECH_IMPERIAL };
    const WlJsonVal *rname = r->type == WL_JSON_OBJECT ? wl_json_get(r, "name") : NULL;
    const WlJsonVal *rhelp = rname ? wl_json_get(r, "help") : NULL;
    if (!rname || rname->type != WL_JSON_STRING || (rhelp && rhelp->type != WL_JSON_STRING)) {
        snprintf(err, errcap, "%s: research must be an object with a name (string)", w);
        return -1;
    }
    static const char *const known[] = { "copy_from", "name", "help", "cost", "time", "effects", "age", "icon" };
    if (wl_check_known_keys(r, known, 8, w, "research ", err, errcap) != 0) return -1;

    const WlJsonVal *v = wl_json_get(r, "copy_from");
    long long src = 22;
    if (v) src = wl_json_is_int(v) ? v->u.num.ival : -1;
    if (src < 0 || (size_t)src >= orig_res_cnt) {
        if (wl_json_is_int(v)) snprintf(err, errcap, "%s: research copy_from %lld does not exist", w, src);
        else snprintf(err, errcap, "%s: research copy_from does not exist", w);
        return -1;
    }
    v = wl_json_get(r, "age");
    long long r_age = age;
    if (v) r_age = wl_json_is_int(v) ? v->u.num.ival : -1;
    if (r_age < 0 || r_age > 3) {
        snprintf(err, errcap, "%s: research age must be 0..3", w);
        return -1;
    }
    const WlJsonVal *effs = wl_json_get(r, "effects");
    if (effs && effs->type != WL_JSON_ARRAY) {
        snprintf(err, errcap, "%s: research effects must be a list", w);
        return -1;
    }

    WlDatEffect eff;
    memset(&eff, 0, sizeof(eff));
    char ef_name[64];
    snprintf(ef_name, sizeof(ef_name), "WL UB tech %d.%zu", cid, k);
    if (wl_make_effect_name(ef_name, w, eff.name, err, errcap) != 0) return -1;
    for (size_t i = 0; effs && i < effs->u.arr.count; i++) {
        WlDatEffectCmd ec;
        if (wl_parse_effect_command(effs->u.arr.items[i], w, &ec, err, errcap) != 0 ||
            wl_effect_add_cmd(&eff, &ec, err, errcap) != 0) {
            free(eff.commands);
            return -1;
        }
    }
    size_t eff_id = ef_cnt + *p_effs_cnt;
    if (wl_push_effect(p_effs, p_effs_cnt, p_effs_cap, &eff, err, errcap) != 0) {
        free(eff.commands);
        return -1;
    }

    WlDatResearch tech;
    memset(&tech, 0, sizeof(tech));
    memcpy(tech.header, orig + orig_researches[src].offset, 62);
    uint8_t *h = tech.header;
    wl_put16(h + 29, cid);
    wl_put16(h + 41, (int)eff_id);
    wl_put16(h + 33, B);
    h[47] = (uint8_t)button;
    wl_put16(h + 0, age_techs[r_age]);
    for (int i = 1; i < 6; i++) wl_put16(h + 2 * i, -1);
    wl_put16(h + 27, 1);

    const WlJsonVal *cost = wl_json_get(r, "cost");
    static const char *const res_names[4] = { "food", "wood", "stone", "gold" };
    if (cost && (cost->type != WL_JSON_OBJECT || wl_check_known_keys(cost, res_names, 4, w, "", err, errcap) != 0)) {
        snprintf(err, errcap, "%s: research cost must be an object with food/wood/stone/gold", w);
        return -1;
    }
    int items[4][2], n = 0;
    for (int ri = 0; cost && ri < 4; ri++) {
        const WlJsonVal *cv = wl_json_get(cost, res_names[ri]);
        if (!cv) continue;
        if (!wl_json_is_int(cv) || cv->u.num.ival < 0 || cv->u.num.ival > 32767) {
            snprintf(err, errcap, "%s: research cost values must be whole numbers >= 0", w);
            return -1;
        }
        if (cv->u.num.ival > 0) { items[n][0] = ri; items[n][1] = (int)cv->u.num.ival; n++; }
    }
    if (n > 3) {
        snprintf(err, errcap, "%s: research cost has more than 3 resources", w);
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        uint8_t *rc = h + 12 + 5 * i;
        wl_put16(rc, i < n ? items[i][0] : -1);
        wl_put16(rc + 2, i < n ? items[i][1] : 0);
        rc[4] = i < n ? 1 : 0;
    }
    v = wl_json_get(r, "time");
    long long rt = 30;
    if (v) rt = wl_json_is_int(v) ? v->u.num.ival : -1;
    if (rt < 0 || rt > 32767) {
        snprintf(err, errcap, "%s: research time must be a whole number >= 0", w);
        return -1;
    }
    wl_put16(h + 39, (int)rt);
    v = wl_json_get(r, "icon");
    if (v) {
        if (!wl_json_is_int(v)) {
            snprintf(err, errcap, "%s: research icon must be a number", w);
            return -1;
        }
        wl_put16(h + 45, (int)v->u.num.ival);
    }

    int sid = wl_new_sids(ext, err, errcap);
    if (sid < 0) return -1;
    const char *nm = rname->u.str, *hp = rhelp ? rhelp->u.str : nm;
    size_t cl = strlen(nm), hl = strlen(hp);
    char *t1 = malloc(cl + 16), *t2 = malloc(cl + hl + 48);
    if (!t1 || !t2) { free(t1); free(t2); snprintf(err, errcap, "out of memory"); return -1; }
    sprintf(t1, "Research %s", nm);
    sprintf(t2, "Research <b>%s<b> (<cost>) \n%s", nm, hp);
    int rc = wl_ext_add_string(ext, sid, nm, err, errcap) || wl_ext_add_string(ext, sid + 1, t1, err, errcap) ||
             wl_ext_add_string(ext, sid + 2, t2, err, errcap);
    free(t1);
    free(t2);
    if (rc) return -1;
    wl_put16(h + 35, sid);
    wl_put16(h + 37, sid + 1);
    wl_put32(h + 48, 79000 + sid + 2);

    char tn[48];
    snprintf(tn, sizeof(tn), "WL UBT %d.%zu", cid, k);
    tech.name_length = (uint16_t)(strlen(tn) + 1);
    tech.name = malloc(tech.name_length);
    if (!tech.name) { snprintf(err, errcap, "out of memory"); return -1; }
    memcpy(tech.name, tn, tech.name_length);
    if (wl_push_research(p_res, p_res_cnt, p_res_cap, &tech, err, errcap) != 0) {
        free(tech.name);
        return -1;
    }
    return 0;
}

// unique_building "commands" (mirrors add_building_commands in build.py):
//   [{"button": n, "train": id | {...}} | {"button": n, "research": {...}} | {"button": n, "lua": "Name", ...}]
// avail_idx: index of the building's "avail" effect in *p_effs (train commands enable their units there;
// an index, not a pointer, because research commands grow *p_effs).
static int wl_add_building_commands(const WlGfxCtx *gctx, WlDatCiv *civ, const WlDatCiv *civs, int cid, int B, const WlJsonVal *ub,
                                    int age, size_t avail_idx,
                                    const uint8_t *orig, const WlOrigResearch *orig_researches, size_t orig_res_cnt,
                                    WlDatResearch **p_res, size_t *p_res_cnt, size_t *p_res_cap,
                                    size_t ef_cnt, WlDatEffect **p_effs, size_t *p_effs_cnt, size_t *p_effs_cap,
                                    size_t uu_total, WlBuildExt *ext,
                                    const char *where, char *err, size_t errcap) {
    const WlJsonVal *cmds = wl_json_get(ub, "commands");
    if (!cmds) return 0;
    if (cmds->type != WL_JSON_ARRAY) {
        snprintf(err, errcap, "%s: unique_building commands must be a list", where);
        return -1;
    }
    for (size_t k = 0; k < cmds->u.arr.count; k++) {
        const WlJsonVal *c = cmds->u.arr.items[k];
        char w[1024];
        snprintf(w, sizeof(w), "%.900s commands[%zu]", where, k);
        if (c->type != WL_JSON_OBJECT) {
            snprintf(err, errcap, "%s: must be an object", w);
            return -1;
        }
        const WlJsonVal *tv = wl_json_get(c, "train"), *rv = wl_json_get(c, "research"), *lv = wl_json_get(c, "lua");
        if ((tv != NULL) + (rv != NULL) + (lv != NULL) != 1) {
            snprintf(err, errcap, "%s: needs exactly one of 'train', 'research', 'lua'", w);
            return -1;
        }
        const char *known[5] = { "button", tv ? "train" : rv ? "research" : "lua", "text", "help", "icon" };
        if (wl_check_known_keys(c, known, lv ? 5 : 2, w, "", err, errcap) != 0) return -1;
        const WlJsonVal *bv = wl_json_get(c, "button");
        if (!wl_json_is_int(bv) || bv->u.num.ival < 1 || bv->u.num.ival > 15) {
            snprintf(err, errcap, "%s: button must be 1..15", w);
            return -1;
        }
        int button = (int)bv->u.num.ival;

        if (tv) {
            if (wl_cmd_train(gctx, civ, civs, B, button, tv, &(*p_effs)[avail_idx], uu_total, ext, w, err, errcap) != 0)
                return -1;
        } else if (rv) {
            if (wl_cmd_research(cid, B, button, age, k, rv, orig, orig_researches, orig_res_cnt,
                                p_res, p_res_cnt, p_res_cap, ef_cnt, p_effs, p_effs_cnt, p_effs_cap,
                                ext, w, err, errcap) != 0)
                return -1;
        } else {  // lua button: nothing in the dat; the DLL adds it to the building's panel
            if (lv->type != WL_JSON_STRING || !lv->u.str[0] || strchr(lv->u.str, '\t') || strchr(lv->u.str, '\n')) {
                snprintf(err, errcap, "%s: lua must be a handler name", w);
                return -1;
            }
            const char *name = lv->u.str;
            const WlJsonVal *iv = wl_json_get(c, "icon");
            if (iv && !wl_json_is_int(iv)) {
                snprintf(err, errcap, "%s: lua icon must be a number (command icon frame)", w);
                return -1;
            }
            int icon = iv ? (int)iv->u.num.ival : 32;
            const WlJsonVal *xv = wl_json_get(c, "text"), *hv = wl_json_get(c, "help");
            if ((xv && xv->type != WL_JSON_STRING) || (hv && hv->type != WL_JSON_STRING)) {
                snprintf(err, errcap, "%s: lua text and help must be strings", w);
                return -1;
            }
            const char *text = xv ? xv->u.str : name, *hp = hv ? hv->u.str : "";
            int sid = wl_new_sids(ext, err, errcap);
            if (sid < 0) return -1;
            size_t tl = strlen(text), hl = strlen(hp), nl = strlen(name);
            char *t2 = malloc(tl + hl + 16), *line = malloc(nl + 80);
            if (!t2 || !line) { free(t2); free(line); snprintf(err, errcap, "out of memory"); return -1; }
            sprintf(t2, "<b>%s<b> \n%s", text, hp);
            sprintf(line, "%d\t%d\t%d\t%s\t%d\t%d", cid, B, button, name, icon, sid + 2);
            int rc = wl_ext_add_string(ext, sid, text, err, errcap) || wl_ext_add_string(ext, sid + 1, text, err, errcap) ||
                     wl_ext_add_string(ext, sid + 2, t2, err, errcap) || wl_ext_add_lua(ext, line, err, errcap);
            free(t2);
            free(line);
            if (rc) return -1;
        }
    }
    return 0;
}

// ==========================================================================
// Unique Building Support
// ==========================================================================

// Adds a unique building to a civilization (mirrors add_unique_building in build.py).
// Building slot B = 1400 - ub_k (counts down; unique units count up from 1224).
// Creates the building unit, optionally a custom graphic when "slp" is given,
// an effect to enable it, and a tech cloned from research 570 (Feitoria make-avail).
static int wl_add_unique_building(
    WlDatCiv *civ,
    const WlDatCiv *civs,
    int cid,
    const WlJsonVal *ub,
    const char *where,
    size_t ub_k,
    size_t uu_total,
    const unsigned char *orig,
    size_t graphics_section_offset,
    uint16_t orig_graphics_count,
    const WlOrigEffect *orig_effects __attribute__((unused)),
    size_t ef_cnt,
    WlDatEffect **p_custom_effects,
    size_t *p_custom_effects_cnt,
    size_t *p_custom_effects_cap,
    const WlOrigResearch *orig_researches,
    size_t orig_res_cnt,
    WlDatResearch **p_custom_researches,
    size_t *p_custom_researches_cnt,
    size_t *p_custom_researches_cap,
    WlCustomGraphic **p_custom_graphics,
    size_t *p_custom_graphics_cnt,
    size_t *p_custom_graphics_cap,
    WlBuildExt *ext,
    char *err,
    size_t errcap
) {
    if (ub->type != WL_JSON_OBJECT) {
        snprintf(err, errcap, "%s: unique_building must be an object", where);
        return -1;
    }

    // Required keys
    const WlJsonVal *cf_val = wl_json_get(ub, "copy_from");
    const WlJsonVal *name_val = wl_json_get(ub, "name");
    if (!cf_val) {
        snprintf(err, errcap, "%s: unique_building missing required key 'copy_from'", where);
        return -1;
    }
    if (!name_val) {
        snprintf(err, errcap, "%s: unique_building missing required key 'name'", where);
        return -1;
    }

    // Known keys validation
    const char *known_ub[] = { "age", "button", "commands", "copy_from", "help", "name", "page", "slp", "units" };
    char *extra_ub[16];
    size_t extra_ub_cnt = 0;
    for (size_t i = 0; i < ub->u.obj.count; i++) {
        const char *k = ub->u.obj.members[i].key;
        int ok = 0;
        for (size_t j = 0; j < sizeof(known_ub)/sizeof(*known_ub); j++) {
            if (strcmp(k, known_ub[j]) == 0) { ok = 1; break; }
        }
        if (!ok && extra_ub_cnt < 16) extra_ub[extra_ub_cnt++] = (char *)k;
    }
    if (extra_ub_cnt > 0) {
        if (extra_ub_cnt > 1) qsort(extra_ub, extra_ub_cnt, sizeof(char *), wl_cmp_str);
        char extra_buf[256];
        size_t epos = snprintf(extra_buf, sizeof(extra_buf), "[");
        for (size_t i = 0; i < extra_ub_cnt; i++) {
            epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "%s'%s'", (i > 0 ? ", " : ""), extra_ub[i]);
        }
        snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "]");
        snprintf(err, errcap, "%s: unknown unique_building key(s) %s", where, extra_buf);
        return -1;
    }

    if (cid > 114) {
        snprintf(err, errcap, "%s: civ_id %d exceeds 114 for unique_building", where, cid);
        return -1;
    }

    int B = 1400 - (int)ub_k;
    if (B < 1300 || B <= 1225 + 2 * (int)uu_total) {   // buildpage.c treats 1300..1400 as mod buildings
        snprintf(err, errcap, "%s: unique building slot %d collides with unique unit slots", where, B);
        return -1;
    }

    // Check civ 1 unit at B is RESERVED
    if ((size_t)B >= civs[1].unit_count || civs[1].unit_offsets[B] == 0) {
        snprintf(err, errcap, "%s: civ 1 unit at %d is not named 'RESERVED'", where, B);
        return -1;
    }
    const uint8_t *u_check = civs[1].data + civs[1].unit_offsets[B];
    int16_t nl_check = (int16_t)(u_check[1] | (u_check[2] << 8));
    uint8_t dmg_check = u_check[149];
    size_t name_off_check = 150 + (size_t)dmg_check * 5 + 6;
    if (nl_check < 9 || memcmp(u_check + name_off_check, "RESERVED", 8) != 0 || u_check[name_off_check + 8] != '\0') {
        snprintf(err, errcap, "%s: civ 1 unit at %d is not named 'RESERVED'", where, B);
        return -1;
    }

    // Validate copy_from
    if (cf_val->type != WL_JSON_NUMBER || cf_val->u.num.is_float) {
        snprintf(err, errcap, "%s: unique_building copy_from must be an integer", where);
        return -1;
    }
    long copy_from = cf_val->u.num.ival;
    if (copy_from < 0 || (size_t)copy_from >= civ->unit_count || civ->unit_offsets[copy_from] == 0) {
        snprintf(err, errcap, "%s: unique_building copy_from unit %ld does not exist", where, copy_from);
        return -1;
    }
    uint8_t cf_type = civ->data[civ->unit_offsets[copy_from]];
    if (cf_type != 80) {
        snprintf(err, errcap, "%s: unique_building copy_from unit %ld must be a building (type 80, got %d)",
                 where, copy_from, (int)cf_type);
        return -1;
    }

    // Parse optional parameters
    const WlJsonVal *page_val = wl_json_get(ub, "page");
    int command_attr = WL_PAGE_MODS;
    if (page_val) {
        if (page_val->type != WL_JSON_STRING) {
            snprintf(err, errcap, "%s: unique_building page must be 'mods' or 'military'", where);
            return -1;
        }
        if (strcmp(page_val->u.str, "military") == 0) {
            command_attr = WL_PAGE_MILITARY;
        } else if (strcmp(page_val->u.str, "mods") != 0) {
            snprintf(err, errcap, "%s: unique_building page must be 'mods' or 'military'", where);
            return -1;
        }
    }

    const WlJsonVal *button_val = wl_json_get(ub, "button");
    int button = 13;
    if (button_val) {
        if (button_val->type != WL_JSON_NUMBER || button_val->u.num.is_float || button_val->u.num.ival < 1 || button_val->u.num.ival > 15) {
            snprintf(err, errcap, "%s: unique_building button must be 1..15", where);
            return -1;
        }
        button = (int)button_val->u.num.ival;
    }

    const WlJsonVal *age_val = wl_json_get(ub, "age");
    int age = 2;
    int age_techs[] = { WL_AGE_TECH_DARK, WL_AGE_TECH_FEUDAL, WL_AGE_TECH_CASTLE, WL_AGE_TECH_IMPERIAL };
    if (age_val) {
        if (age_val->type != WL_JSON_NUMBER || age_val->u.num.is_float || age_val->u.num.ival < 0 || age_val->u.num.ival > 3) {
            snprintf(err, errcap, "%s: unique_building age must be 0..3", where);
            return -1;
        }
        age = (int)age_val->u.num.ival;
    }

    int sid_base = 28500 + (cid - 35) * 20;

    // Clone building unit
    const uint8_t *src_b = civ->data + civ->unit_offsets[copy_from];
    size_t len_B = wl_calc_unit_len(civ->data, civ->unit_offsets[copy_from]);
    uint8_t *rep_B = malloc(len_B);
    if (!rep_B) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    memcpy(rep_B, src_b, len_B);

    int16_t u16_B = (int16_t)B;
    rep_B[3] = (uint8_t)(u16_B & 0xFF); rep_B[4] = (uint8_t)((u16_B >> 8) & 0xFF);

    int sid10 = sid_base + 10;
    rep_B[5] = (uint8_t)(sid10 & 0xFF); rep_B[6] = (uint8_t)((sid10 >> 8) & 0xFF);
    int sid11 = sid_base + 11;
    rep_B[7] = (uint8_t)(sid11 & 0xFF); rep_B[8] = (uint8_t)((sid11 >> 8) & 0xFF);
    rep_B[52] = 0; rep_B[53] = 0;  // enabled = 0
    int sid12 = 79000 + sid_base + 12;
    rep_B[91] = (uint8_t)(sid12 & 0xFF); rep_B[92] = (uint8_t)((sid12 >> 8) & 0xFF);
    rep_B[93] = (uint8_t)((sid12 >> 16) & 0xFF); rep_B[94] = (uint8_t)((sid12 >> 24) & 0xFF);
    int hk10 = 150000 + sid10;
    rep_B[95] = (uint8_t)(hk10 & 0xFF); rep_B[96] = (uint8_t)((hk10 >> 8) & 0xFF);
    rep_B[97] = (uint8_t)((hk10 >> 16) & 0xFF); rep_B[98] = (uint8_t)((hk10 >> 24) & 0xFF);

    // Replace name
    if (name_val->type != WL_JSON_STRING || !name_val->u.str) {
        snprintf(err, errcap, "%s: unique_building name must be a string", where);
        free(rep_B);
        return -1;
    }
    char new_name_B[32];
    size_t new_nl_B;
    char where_ub[1024];
    snprintf(where_ub, sizeof(where_ub), "%.500s unique_building", where);
    if (wl_make_unit_name(name_val->u.str, where_ub, new_name_B, &new_nl_B, err, errcap) != 0) {
        free(rep_B);
        return -1;
    }

    int16_t nl_B = (int16_t)(rep_B[1] | (rep_B[2] << 8));
    uint8_t dmg_B = rep_B[149];
    size_t s1_B = 150 + (size_t)dmg_B * 5;

    if ((int16_t)new_nl_B != nl_B) {
        size_t new_len_B = len_B - (size_t)nl_B + new_nl_B;
        uint8_t *new_rep_B = malloc(new_len_B);
        if (!new_rep_B) {
            snprintf(err, errcap, "out of memory");
            free(rep_B);
            return -1;
        }
        memcpy(new_rep_B, rep_B, s1_B + 6);
        memcpy(new_rep_B + s1_B + 6, new_name_B, new_nl_B);
        size_t old_s2_B = s1_B + 6 + (size_t)nl_B;
        size_t new_s2_B = s1_B + 6 + new_nl_B;
        memcpy(new_rep_B + new_s2_B, rep_B + old_s2_B, len_B - old_s2_B);
        new_rep_B[1] = (uint8_t)(new_nl_B & 0xFF);
        new_rep_B[2] = (uint8_t)((new_nl_B >> 8) & 0xFF);
        free(rep_B);
        rep_B = new_rep_B;
        len_B = new_len_B;
        nl_B = (int16_t)new_nl_B;
    } else {
        memcpy(rep_B + s1_B + 6, new_name_B, new_nl_B);
    }

    size_t s2_B = s1_B + 6 + (size_t)nl_B;
    rep_B[s2_B + 0] = (uint8_t)(u16_B & 0xFF); rep_B[s2_B + 1] = (uint8_t)((u16_B >> 8) & 0xFF);
    rep_B[s2_B + 2] = (uint8_t)(u16_B & 0xFF); rep_B[s2_B + 3] = (uint8_t)((u16_B >> 8) & 0xFF);

    // Compute slice 4 offset for train_location_id, button_id, command_attribute
    size_t p_atk_B = s2_B + 4 + 4 + 37 + 20 + 2;
    uint16_t at_cnt_B = (uint16_t)(rep_B[p_atk_B] | (rep_B[p_atk_B + 1] << 8));
    size_t s3_B = p_atk_B + 2 + (size_t)at_cnt_B * 4;
    uint16_t ar_cnt_B = (uint16_t)(rep_B[s3_B] | (rep_B[s3_B + 1] << 8));
    size_t s4_B = s3_B + 2 + (size_t)ar_cnt_B * 4;
    rep_B[s4_B + 76] = 118; rep_B[s4_B + 77] = 0;  // train_location_id = 118
    rep_B[s4_B + 78] = (uint8_t)button;            // button_id
    rep_B[85] = (uint8_t)command_attr;             // command_attribute: slice 0, offset 85 (field table)

    // Handle "slp" for custom graphic
    const WlJsonVal *slp_val = wl_json_get(ub, "slp");
    if (slp_val) {
        if (slp_val->type != WL_JSON_NUMBER || slp_val->u.num.is_float || slp_val->u.num.ival <= 0 || slp_val->u.num.ival >= (1LL << 31)) {
            snprintf(err, errcap, "%s: unique_building slp must be a positive integer", where);
            free(rep_B);
            return -1;
        }
        int32_t slp = (int32_t)slp_val->u.num.ival;

        // Read source graphic ID from standing_graphic[0]
        int16_t gsrc = (int16_t)(rep_B[11] | (rep_B[12] << 8));
        if (gsrc < 0 || gsrc >= orig_graphics_count) {
            snprintf(err, errcap, "%s: building %ld has no valid standing graphic", where, copy_from);
            free(rep_B);
            return -1;
        }

        // Read source graphic record from Section 5
        size_t gfx_pos = graphics_section_offset + 2 + (size_t)orig_graphics_count * 4;
        const uint8_t *g_ptrs = orig + graphics_section_offset + 2;
        int32_t src_ptr = (int32_t)(g_ptrs[gsrc * 4] | (g_ptrs[gsrc * 4 + 1] << 8) | (g_ptrs[gsrc * 4 + 2] << 16) | (g_ptrs[gsrc * 4 + 3] << 24));
        if (src_ptr == 0) {
            snprintf(err, errcap, "%s: building %ld standing graphic %d is null", where, copy_from, (int)gsrc);
            free(rep_B);
            return -1;
        }

        // Find source graphic record
        for (size_t g = 0; g < orig_graphics_count; g++) {
            int32_t p = (int32_t)(g_ptrs[g * 4] | (g_ptrs[g * 4 + 1] << 8) | (g_ptrs[g * 4 + 2] << 16) | (g_ptrs[g * 4 + 3] << 24));
            if (p && g < (size_t)gsrc) {
                uint16_t delta_cnt = (uint16_t)(orig[gfx_pos + 52] | (orig[gfx_pos + 53] << 8));
                uint8_t atk_snd_used = orig[gfx_pos + 56];
                uint16_t angle_cnt = (uint16_t)(orig[gfx_pos + 59] | (orig[gfx_pos + 60] << 8));
                gfx_pos += 78 + (size_t)delta_cnt * 16;
                if (atk_snd_used) gfx_pos += (size_t)angle_cnt * 12;
            }
        }

        // Copy source graphic header (78 bytes)
        if (*p_custom_graphics_cnt >= *p_custom_graphics_cap) {
            size_t ncap = (*p_custom_graphics_cap == 0) ? 16 : (*p_custom_graphics_cap * 2);
            WlCustomGraphic *ng = realloc(*p_custom_graphics, ncap * sizeof(WlCustomGraphic));
            if (!ng) {
                snprintf(err, errcap, "out of memory");
                free(rep_B);
                return -1;
            }
            *p_custom_graphics = ng;
            *p_custom_graphics_cap = ncap;
        }

        WlCustomGraphic *cg = &(*p_custom_graphics)[*p_custom_graphics_cnt];
        memcpy(cg->data, orig + gfx_pos, 78);
        cg->source_ptr = src_ptr;
        cg->extra = NULL;      // a building picture: no deltas, no attack sounds
        cg->extra_len = 0;

        // Modify graphic record: name, filename, slp, id, frame_count=1, angle_count=1, delta_count=0, attack_sound_used=0
        char gname[32];
        snprintf(gname, sizeof(gname), "WLB%d", cid);
        memset(cg->data, 0, 21);  // name (21 bytes)
        memcpy(cg->data, gname, strlen(gname) < 20 ? strlen(gname) : 20);
        memset(cg->data + 21, 0, 13);  // filename (13 bytes)
        memcpy(cg->data + 21, gname, strlen(gname) < 12 ? strlen(gname) : 12);

        // slp (i32 at offset 34)
        cg->data[34] = (uint8_t)(slp & 0xFF);
        cg->data[35] = (uint8_t)((slp >> 8) & 0xFF);
        cg->data[36] = (uint8_t)((slp >> 16) & 0xFF);
        cg->data[37] = (uint8_t)((slp >> 24) & 0xFF);

        // id (i16 at offset 74; Graphic FIELDS in tools/datfile.py: ... new_speed 61, frame_rate 65,
        // replay_delay 69, sequence_type 73, id 74, mirroring_mode 76, unknown2 77 = 78 bytes)
        int16_t new_gid = (int16_t)(orig_graphics_count + *p_custom_graphics_cnt);
        cg->data[74] = (uint8_t)(new_gid & 0xFF);
        cg->data[75] = (uint8_t)((new_gid >> 8) & 0xFF);

        // frame_count (u16 at offset 57) = 1
        cg->data[57] = 1;
        cg->data[58] = 0;

        // angle_count (u16 at offset 59) = 1
        cg->data[59] = 1;
        cg->data[60] = 0;

        // delta_count (u16 at offset 52) = 0
        cg->data[52] = 0;
        cg->data[53] = 0;

        // attack_sound_used (i8 at offset 56) = 0
        cg->data[56] = 0;

        (*p_custom_graphics_cnt)++;

        // Update building's standing_graphic[0] and snow_graphic_id
        rep_B[11] = (uint8_t)(new_gid & 0xFF);
        rep_B[12] = (uint8_t)((new_gid >> 8) & 0xFF);
        // snow_graphic_id at slice 4, offset 123
        rep_B[s4_B + 123] = (uint8_t)(new_gid & 0xFF);
        rep_B[s4_B + 124] = (uint8_t)((new_gid >> 8) & 0xFF);
    }

    // Apply "units" overrides
    const WlJsonVal *units_val = wl_json_get(ub, "units");
    if (units_val) {
        if (units_val->type != WL_JSON_OBJECT) {
            snprintf(err, errcap, "%s: unique_building units must be an object", where);
            free(rep_B);
            return -1;
        }
        char where_unit[1024];
        snprintf(where_unit, sizeof(where_unit), "%.500s unit %d", where, B);
        for (size_t f = 0; f < units_val->u.obj.count; f++) {
            const char *fname = units_val->u.obj.members[f].key;
            const WlJsonVal *fval = units_val->u.obj.members[f].val;
            if (wl_set_unit_field(rep_B, where_unit, fname, fval, err, errcap) != 0) {
                free(rep_B);
                return -1;
            }
        }
    }

    // Create effect
    WlDatEffect eff;
    memset(&eff, 0, sizeof(eff));
    char ef_name[64];
    snprintf(ef_name, sizeof(ef_name), "WL UB avail %d", cid);
    if (wl_make_effect_name(ef_name, where, eff.name, err, errcap) != 0) {
        free(rep_B);
        return -1;
    }
    eff.cmd_count = 1;
    eff.cmd_cap = 1;
    eff.commands = malloc(sizeof(WlDatEffectCmd));
    if (!eff.commands) {
        free(rep_B);
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    eff.commands[0].type = 2;
    eff.commands[0].a = (int16_t)B;
    eff.commands[0].b = 1;
    eff.commands[0].c = -1;
    eff.commands[0].d = 0.0f;

    if (*p_custom_effects_cnt >= *p_custom_effects_cap) {
        size_t ncap = (*p_custom_effects_cap == 0) ? 16 : (*p_custom_effects_cap * 2);
        WlDatEffect *neffs = realloc(*p_custom_effects, ncap * sizeof(WlDatEffect));
        if (!neffs) {
            free(eff.commands);
            free(rep_B);
            snprintf(err, errcap, "out of memory");
            return -1;
        }
        *p_custom_effects = neffs;
        *p_custom_effects_cap = ncap;
    }
    size_t avail_idx = *p_custom_effects_cnt;
    size_t eff_id = ef_cnt + avail_idx;
    (*p_custom_effects)[(*p_custom_effects_cnt)++] = eff;

    // Building unit into the civ, then its commands (same order as build.py: the commands' effects
    // and techs come before the building's own tech).
    int rc = wl_replace_civ_units(civ, B, rep_B, len_B, -1, NULL, 0, err, errcap);
    free(rep_B);
    if (rc != 0) return -1;
    ext->slots = ub_k + 1;
    WlGfxCtx gctx = { orig, graphics_section_offset, orig_graphics_count,
                      p_custom_graphics, p_custom_graphics_cnt, p_custom_graphics_cap };
    if (wl_add_building_commands(&gctx, civ, civs, cid, B, ub, age, avail_idx,
                                 orig, orig_researches, orig_res_cnt,
                                 p_custom_researches, p_custom_researches_cnt, p_custom_researches_cap,
                                 ef_cnt, p_custom_effects, p_custom_effects_cnt, p_custom_effects_cap,
                                 uu_total, ext, where, err, errcap) != 0)
        return -1;

    // Create tech (clone of research 570)
    WlDatResearch tech;
    if (WL_UB_TEMPLATE_TECH >= orig_res_cnt) {
        snprintf(err, errcap, "%s: template tech %d not found", where, WL_UB_TEMPLATE_TECH);
        return -1;
    }
    size_t tmpl_off = orig_researches[WL_UB_TEMPLATE_TECH].offset;
    memcpy(tech.header, orig + tmpl_off, 62);
    tech.header[29] = (uint8_t)(cid & 0xFF);
    tech.header[30] = (uint8_t)((cid >> 8) & 0xFF);
    tech.header[41] = (uint8_t)(eff_id & 0xFF);
    tech.header[42] = (uint8_t)((eff_id >> 8) & 0xFF);
    tech.header[43] = (uint8_t)((eff_id >> 16) & 0xFF);
    tech.header[44] = (uint8_t)((eff_id >> 24) & 0xFF);

    // required_techs[0] = age tech, rest = -1
    int16_t age_tech = (int16_t)age_techs[age];
    tech.header[0] = (uint8_t)(age_tech & 0xFF);
    tech.header[1] = (uint8_t)((age_tech >> 8) & 0xFF);
    for (int i = 1; i < 6; i++) {
        tech.header[i * 2] = 0xFF;
        tech.header[i * 2 + 1] = 0xFF;
    }
    tech.header[27] = 1;  // required_tech_count

    // language_dll_name and description = sid10
    tech.header[35] = (uint8_t)(sid10 & 0xFF);
    tech.header[36] = (uint8_t)((sid10 >> 8) & 0xFF);
    tech.header[37] = (uint8_t)(sid10 & 0xFF);
    tech.header[38] = (uint8_t)((sid10 >> 8) & 0xFF);

    // name
    char tech_name[32];
    snprintf(tech_name, sizeof(tech_name), "WL UB %d", cid);
    tech.name_length = (uint16_t)(strlen(tech_name) + 1);
    tech.name = malloc(tech.name_length);
    if (!tech.name) {
        snprintf(err, errcap, "out of memory");
        return -1;
    }
    memcpy(tech.name, tech_name, tech.name_length);

    if (*p_custom_researches_cnt >= *p_custom_researches_cap) {
        size_t ncap = (*p_custom_researches_cap == 0) ? 16 : (*p_custom_researches_cap * 2);
        WlDatResearch *nres = realloc(*p_custom_researches, ncap * sizeof(WlDatResearch));
        if (!nres) {
            free(tech.name);
            snprintf(err, errcap, "out of memory");
            return -1;
        }
        *p_custom_researches = nres;
        *p_custom_researches_cap = ncap;
    }
    (*p_custom_researches)[(*p_custom_researches_cnt)++] = tech;

    return 0;
}

// ==========================================================================
// Apply Civ JSON Definition
// ==========================================================================

// Creates one new civilization by cloning base_civ and applying overrides from spec.
// RULE (Validation & Python parity):
//   - Requires "name", "dat_name", "base" keys.
//   - Disallows unknown keys, printing sorted list of unknown keys in Python format.
//   - Validates base civ index is within 0..len(civs)-1.
//   - Overrides dat_name, tech_tree, team_bonus, units, resources, effects, unique_techs, unique_unit, unique_building.
static int wl_apply_civ_json(WlDatCiv *civs, size_t *civs_count, size_t civs_cap,
                             const unsigned char *orig,
                             size_t graphics_section_offset,
                             uint16_t orig_graphics_count,
                             const WlOrigEffect *orig_effects, size_t ef_cnt,
                             WlDatEffect **p_custom_effects, size_t *p_custom_effects_cnt, size_t *p_custom_effects_cap,
                             const WlOrigResearch *orig_researches, size_t orig_res_cnt,
                             WlDatResearch **p_custom_researches, size_t *p_custom_researches_cnt, size_t *p_custom_researches_cap,
                             WlCustomGraphic **p_custom_graphics, size_t *p_custom_graphics_cnt, size_t *p_custom_graphics_cap,
                             size_t *p_uu_k, size_t *p_ub_k, size_t uu_total, WlTtState *tts,
                             WlBuildExt *ext,
                             const WlJsonVal *spec, const char *where,
                             int *out_cid, char *err, size_t errcap) {
    WlGfxCtx gctx = { orig, graphics_section_offset, orig_graphics_count,
                      p_custom_graphics, p_custom_graphics_cnt, p_custom_graphics_cap };
    if (spec->type != WL_JSON_OBJECT) {
        snprintf(err, errcap, "%s: top level must be an object", where);
        return -1;
    }

    // Required keys check
    const char *req_keys[] = { "name", "dat_name", "base" };
    for (int i = 0; i < 3; i++) {
        if (!wl_json_get(spec, req_keys[i])) {
            snprintf(err, errcap, "%s: missing required key '%s'", where, req_keys[i]);
            return -1;
        }
    }

    // Unknown keys check: must match Python's `set(spec) - known` sorted error format
    // Identity keys are ignored here and read by the DLL (civs.c); keep in sync with build.py.
    const char *known_keys[] = {
        "ai_names", "base", "dat_name", "description",
        "disabled_techs", "disabled_units", "effects",
        "enabled_techs", "enabled_units", "interface_slp",
        "name", "resources", "sound", "team_bonus",
        "team_bonus_effects", "tech_tree", "unique_building", "unique_techs", "unique_unit", "units"
    };
    char *extra_keys[32];
    size_t extra_cnt = 0;
    for (size_t i = 0; i < spec->u.obj.count; i++) {
        const char *k = spec->u.obj.members[i].key;
        int is_known = 0;
        for (size_t j = 0; j < sizeof known_keys / sizeof *known_keys; j++) {
            if (strcmp(k, known_keys[j]) == 0) {
                is_known = 1;
                break;
            }
        }
        if (!is_known && extra_cnt < 32) {
            extra_keys[extra_cnt++] = (char *)k;
        }
    }
    if (extra_cnt > 0) {
        if (extra_cnt > 1) {
            qsort(extra_keys, extra_cnt, sizeof(char *), wl_cmp_str);
        }
        char extra_buf[256];
        size_t epos = 0;
        epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "[");
        for (size_t i = 0; i < extra_cnt; i++) {
            epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "%s'%s'", (i > 0 ? ", " : ""), extra_keys[i]);
        }
        snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "]");
        snprintf(err, errcap, "%s: unknown key(s) %s", where, extra_buf);
        return -1;
    }

    // Base civ validation
    const WlJsonVal *base_val = wl_json_get(spec, "base");
    if (base_val->type != WL_JSON_NUMBER || base_val->u.num.is_float) {
        char rep[128];
        wl_json_format_repr(base_val, rep, sizeof(rep));
        snprintf(err, errcap, "%s: base %s is not an existing civ id (0..%zu)", where, rep, *civs_count - 1);
        return -1;
    }
    int64_t base_id = base_val->u.num.ival;
    if (base_id < 0 || (size_t)base_id >= *civs_count) {
        snprintf(err, errcap, "%s: base %lld is not an existing civ id (0..%zu)", where, (long long)base_id, *civs_count - 1);
        return -1;
    }

    if (*civs_count >= civs_cap) {
        snprintf(err, errcap, "%s: maximum civ capacity reached", where);
        return -1;
    }

    // Clone base civ
    WlDatCiv new_civ;
    if (wl_clone_civ(civs, *civs_count, base_id, where, &new_civ, err, errcap) != 0) {
        return -1;
    }

    // dat_name: fixed 20-byte ASCII field inside Civilization record
    const WlJsonVal *dn_val = wl_json_get(spec, "dat_name");
    if (dn_val->type != WL_JSON_STRING) {
        snprintf(err, errcap, "%s: dat_name must be a string", where);
        wl_free_civ(&new_civ);
        return -1;
    }
    if (wl_make_name(dn_val->u.str, where, new_civ.data + 1, err, errcap) != 0) {
        wl_free_civ(&new_civ);
        return -1;
    }

    // unique_techs validation
    const WlJsonVal *ut_val = wl_json_get(spec, "unique_techs");
    if (ut_val) {
        if (ut_val->type != WL_JSON_ARRAY || ut_val->u.arr.count != 2) {
            snprintf(err, errcap, "%s: unique_techs must be a list of 2 tech ids [castle_tech, imp_tech]", where);
            wl_free_civ(&new_civ);
            return -1;
        }
    }

    int has_tt_custom = (wl_json_get(spec, "disabled_techs") != NULL ||
                         wl_json_get(spec, "enabled_techs") != NULL ||
                         wl_json_get(spec, "disabled_units") != NULL ||
                         wl_json_get(spec, "enabled_units") != NULL ||
                         wl_json_get(spec, "effects") != NULL);

    if (has_tt_custom) {
        int base_tt = -1;
        const WlJsonVal *tt_val = wl_json_get(spec, "tech_tree");
        if (tt_val) {
            base_tt = (tt_val->type == WL_JSON_NUMBER) ? (int)tt_val->u.num.ival : (int)strtol(tt_val->u.str ? tt_val->u.str : "-1", NULL, 10);
        } else {
            base_tt = (int)(uint16_t)(new_civ.data[23] | (new_civ.data[24] << 8));
        }
        if (base_tt < 0 || (size_t)base_tt >= ef_cnt) {
            snprintf(err, errcap, "%s: base tech tree effect %d out of range (0..%zu)",
                     where, base_tt, ef_cnt - 1);
            wl_free_civ(&new_civ);
            return -1;
        }

        WlDatEffect new_eff;
        memset(&new_eff, 0, sizeof(new_eff));
        char tt_name[64];
        snprintf(tt_name, sizeof(tt_name), "%s Tech Tree", dn_val->u.str);
        if (wl_make_effect_name(tt_name, where, new_eff.name, err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }

        const WlOrigEffect *base_eff = &orig_effects[base_tt];
        const uint8_t *base_cmds_raw = orig + base_eff->cmds_offset;

        const WlJsonVal *en_techs_val = wl_json_get(spec, "enabled_techs");
        int *en_techs = NULL;
        size_t en_techs_cnt = 0;
        if (en_techs_val && en_techs_val->type == WL_JSON_ARRAY) {
            en_techs_cnt = en_techs_val->u.arr.count;
            if (en_techs_cnt > 0) {
                en_techs = malloc(en_techs_cnt * sizeof(int));
                for (size_t ti = 0; ti < en_techs_cnt; ti++) {
                    en_techs[ti] = (int)en_techs_val->u.arr.items[ti]->u.num.ival;
                }
            }
        }

        const WlJsonVal *en_units_val = wl_json_get(spec, "enabled_units");
        int *en_units = NULL;
        size_t en_units_cnt = 0;
        if (en_units_val && en_units_val->type == WL_JSON_ARRAY) {
            en_units_cnt = en_units_val->u.arr.count;
            if (en_units_cnt > 0) {
                en_units = malloc(en_units_cnt * sizeof(int));
                for (size_t ui = 0; ui < en_units_cnt; ui++) {
                    en_units[ui] = (int)en_units_val->u.arr.items[ui]->u.num.ival;
                }
            }
        }

        for (uint16_t c = 0; c < base_eff->cmd_count; c++) {
            const uint8_t *p = base_cmds_raw + (size_t)c * 11;
            WlDatEffectCmd cmd;
            cmd.type = (int8_t)p[0];
            cmd.a = (int16_t)(p[1] | (p[2] << 8));
            cmd.b = (int16_t)(p[3] | (p[4] << 8));
            cmd.c = (int16_t)(p[5] | (p[6] << 8));
            memcpy(&cmd.d, p + 7, 4);

            if (cmd.type == 102 && en_techs) {
                int tid = (int)cmd.d;
                int found = 0;
                for (size_t ti = 0; ti < en_techs_cnt; ti++) {
                    if (en_techs[ti] == tid) { found = 1; break; }
                }
                if (found) continue;
            }

            if (wl_effect_add_command(&new_eff, &cmd, err, errcap) != 0) {
                free(en_techs);
                free(en_units);
                free(new_eff.commands);
                wl_free_civ(&new_civ);
                return -1;
            }
        }
        free(en_techs);

        const WlJsonVal *dis_techs_val = wl_json_get(spec, "disabled_techs");
        if (dis_techs_val && dis_techs_val->type == WL_JSON_ARRAY) {
            for (size_t ti = 0; ti < dis_techs_val->u.arr.count; ti++) {
                int tid = (int)dis_techs_val->u.arr.items[ti]->u.num.ival;
                int existing = 0;
                for (uint16_t k = 0; k < new_eff.cmd_count; k++) {
                    if (new_eff.commands[k].type == 102 && (int)new_eff.commands[k].d == tid) {
                        existing = 1;
                        break;
                    }
                }
                if (!existing) {
                    WlDatEffectCmd c102;
                    c102.type = 102;
                    c102.a = -1;
                    c102.b = -1;
                    c102.c = -1;
                    c102.d = (float)tid;
                    if (wl_effect_add_command(&new_eff, &c102, err, errcap) != 0) {
                        free(en_units);
                        free(new_eff.commands);
                        wl_free_civ(&new_civ);
                        return -1;
                    }
                }
            }
        }

        if (en_units) {
            for (size_t ui = 0; ui < en_units_cnt; ui++) {
                WlDatEffectCmd c2;
                c2.type = 2;
                c2.a = (int16_t)en_units[ui];
                c2.b = 1;
                c2.c = -1;
                c2.d = 0.0f;
                if (wl_effect_add_command(&new_eff, &c2, err, errcap) != 0) {
                    free(en_units);
                    free(new_eff.commands);
                    wl_free_civ(&new_civ);
                    return -1;
                }
            }
            free(en_units);
        }

        const WlJsonVal *dis_units_val = wl_json_get(spec, "disabled_units");
        if (dis_units_val && dis_units_val->type == WL_JSON_ARRAY) {
            for (size_t ui = 0; ui < dis_units_val->u.arr.count; ui++) {
                int uid = (int)dis_units_val->u.arr.items[ui]->u.num.ival;
                // Effect type 2 = enable/disable unit (a=unit, b=0 off). Type 103 is
                // "tech research time", not a unit flag: never emit it (mirrors build.py).
                WlDatEffectCmd c2;
                c2.type = 2;
                c2.a = (int16_t)uid;
                c2.b = 0;
                c2.c = -1;
                c2.d = 0.0f;
                if (wl_effect_add_command(&new_eff, &c2, err, errcap) != 0) {
                    free(new_eff.commands);
                    wl_free_civ(&new_civ);
                    return -1;
                }
            }
        }

        const WlJsonVal *eff_val = wl_json_get(spec, "effects");
        if (eff_val && eff_val->type == WL_JSON_ARRAY) {
            for (size_t ei = 0; ei < eff_val->u.arr.count; ei++) {
                char where_eff[1024];
                snprintf(where_eff, sizeof(where_eff), "%.800s effects[%zu]", where, ei);
                WlDatEffectCmd c;
                if (wl_parse_effect_command(eff_val->u.arr.items[ei], where_eff, &c, err, errcap) != 0) {
                    free(new_eff.commands);
                    wl_free_civ(&new_civ);
                    return -1;
                }
                if (wl_effect_add_command(&new_eff, &c, err, errcap) != 0) {
                    free(new_eff.commands);
                    wl_free_civ(&new_civ);
                    return -1;
                }
            }
        }

        if (*p_custom_effects_cnt >= *p_custom_effects_cap) {
            size_t ncap = (*p_custom_effects_cap == 0) ? 16 : (*p_custom_effects_cap * 2);
            WlDatEffect *neffs = realloc(*p_custom_effects, ncap * sizeof(WlDatEffect));
            if (!neffs) {
                snprintf(err, errcap, "out of memory growing custom effects");
                free(new_eff.commands);
                wl_free_civ(&new_civ);
                return -1;
            }
            *p_custom_effects = neffs;
            *p_custom_effects_cap = ncap;
        }
        size_t new_idx = ef_cnt + *p_custom_effects_cnt;
        (*p_custom_effects)[(*p_custom_effects_cnt)++] = new_eff;

        uint16_t u16 = (uint16_t)new_idx;
        new_civ.data[23] = (uint8_t)(u16 & 0xFF);
        new_civ.data[24] = (uint8_t)((u16 >> 8) & 0xFF);
    } else {
        const WlJsonVal *tt_val = wl_json_get(spec, "tech_tree");
        if (tt_val) {
            int64_t v = (tt_val->type == WL_JSON_NUMBER) ? tt_val->u.num.ival : (int64_t)strtoll(tt_val->u.str ? tt_val->u.str : "0", NULL, 10);
            uint16_t u16 = (uint16_t)(int16_t)v;
            new_civ.data[23] = (uint8_t)(u16 & 0xFF);
            new_civ.data[24] = (uint8_t)((u16 >> 8) & 0xFF);
        }
    }

    const WlJsonVal *tbe_val = wl_json_get(spec, "team_bonus_effects");
    if (tbe_val && tbe_val->type == WL_JSON_ARRAY) {
        WlDatEffect tb_eff;
        memset(&tb_eff, 0, sizeof(tb_eff));
        char tb_name[64];
        snprintf(tb_name, sizeof(tb_name), "%s Team Bonus", dn_val->u.str);
        if (wl_make_effect_name(tb_name, where, tb_eff.name, err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }
        for (size_t ei = 0; ei < tbe_val->u.arr.count; ei++) {
            char where_tbe[1024];
            snprintf(where_tbe, sizeof(where_tbe), "%.800s team_bonus_effects[%zu]", where, ei);
            WlDatEffectCmd c;
            if (wl_parse_effect_command(tbe_val->u.arr.items[ei], where_tbe, &c, err, errcap) != 0) {
                free(tb_eff.commands);
                wl_free_civ(&new_civ);
                return -1;
            }
            if (wl_effect_add_command(&tb_eff, &c, err, errcap) != 0) {
                free(tb_eff.commands);
                wl_free_civ(&new_civ);
                return -1;
            }
        }
        if (*p_custom_effects_cnt >= *p_custom_effects_cap) {
            size_t ncap = (*p_custom_effects_cap == 0) ? 16 : (*p_custom_effects_cap * 2);
            WlDatEffect *neffs = realloc(*p_custom_effects, ncap * sizeof(WlDatEffect));
            if (!neffs) {
                snprintf(err, errcap, "out of memory growing custom effects");
                free(tb_eff.commands);
                wl_free_civ(&new_civ);
                return -1;
            }
            *p_custom_effects = neffs;
            *p_custom_effects_cap = ncap;
        }
        size_t new_idx = ef_cnt + *p_custom_effects_cnt;
        (*p_custom_effects)[(*p_custom_effects_cnt)++] = tb_eff;

        uint16_t u16 = (uint16_t)new_idx;
        new_civ.data[25] = (uint8_t)(u16 & 0xFF);
        new_civ.data[26] = (uint8_t)((u16 >> 8) & 0xFF);
    } else {
        const WlJsonVal *tb_val = wl_json_get(spec, "team_bonus");
        if (tb_val) {
            int64_t v = (tb_val->type == WL_JSON_NUMBER) ? tb_val->u.num.ival : (int64_t)strtoll(tb_val->u.str ? tb_val->u.str : "0", NULL, 10);
            uint16_t u16 = (uint16_t)(int16_t)v;
            new_civ.data[25] = (uint8_t)(u16 & 0xFF);
            new_civ.data[26] = (uint8_t)((u16 >> 8) & 0xFF);
        }
    }

    // Unit field overrides
    const WlJsonVal *units_val = wl_json_get(spec, "units");
    if (units_val && units_val->type == WL_JSON_OBJECT) {
        for (size_t u = 0; u < units_val->u.obj.count; u++) {
            const char *uid_str = units_val->u.obj.members[u].key;
            const WlJsonVal *flds = units_val->u.obj.members[u].val;

            char *endptr = NULL;
            long uid = strtol(uid_str, &endptr, 10);
            if (!endptr || *endptr != '\0' || endptr == uid_str) {
                snprintf(err, errcap, "%s: unit id '%s' is not a number", where, uid_str);
                wl_free_civ(&new_civ);
                return -1;
            }
            if (uid < 0 || (size_t)uid >= new_civ.unit_count || new_civ.unit_offsets[uid] == 0) {
                snprintf(err, errcap, "%s: unit id %ld does not exist in civ %lld", where, uid, (long long)base_id);
                wl_free_civ(&new_civ);
                return -1;
            }

            if (!flds || flds->type != WL_JSON_OBJECT) {
                snprintf(err, errcap, "%s: units.%s must be an object", where, uid_str);
                wl_free_civ(&new_civ);
                return -1;
            }
            {
                char where_unit[1024];
                snprintf(where_unit, sizeof(where_unit), "%.500s unit %ld", where, uid);
                uint8_t *u_data = new_civ.data + new_civ.unit_offsets[uid];
                for (size_t f = 0; f < flds->u.obj.count; f++) {
                    const char *fname = flds->u.obj.members[f].key;
                    const WlJsonVal *fval = flds->u.obj.members[f].val;
                    int frc = !strcmp(fname, "graphics")
                        ? wl_apply_graphics(&gctx, u_data, fval, where_unit, err, errcap)
                        : wl_set_unit_field(u_data, where_unit, fname, fval, err, errcap);
                    if (frc != 0) {
                        wl_free_civ(&new_civ);
                        return -1;
                    }
                }
            }
        }
    }

    // Resource overrides (starting food, wood, gold, stone, etc.)
    const WlJsonVal *res_val = wl_json_get(spec, "resources");
    if (res_val && res_val->type == WL_JSON_OBJECT) {
        for (size_t r = 0; r < res_val->u.obj.count; r++) {
            const char *idx_str = res_val->u.obj.members[r].key;
            const WlJsonVal *val = res_val->u.obj.members[r].val;

            long idx = strtol(idx_str, NULL, 10);
            if (idx < 0 || (size_t)idx >= new_civ.resource_count) {
                snprintf(err, errcap, "%s: resource index %s out of range (0..%u)",
                         where, idx_str, (unsigned)(new_civ.resource_count - 1));
                wl_free_civ(&new_civ);
                return -1;
            }
            if (val->type != WL_JSON_NUMBER) {
                snprintf(err, errcap, "%s: resource %ld must be a number", where, idx);
                wl_free_civ(&new_civ);
                return -1;
            }
            float f = (float)val->u.num.dval;
            uint32_t u;
            memcpy(&u, &f, sizeof(float));
            size_t roff = 27 + (size_t)idx * 4;
            new_civ.data[roff] = (uint8_t)(u & 0xFF);
            new_civ.data[roff + 1] = (uint8_t)((u >> 8) & 0xFF);
            new_civ.data[roff + 2] = (uint8_t)((u >> 16) & 0xFF);
            new_civ.data[roff + 3] = (uint8_t)((u >> 24) & 0xFF);
        }
    }

    const WlJsonVal *uu_val = wl_json_get(spec, "unique_unit");
    if (uu_val) {
        if (uu_val->type != WL_JSON_OBJECT) {
            snprintf(err, errcap, "%s: unique_unit must be an object", where);
            wl_free_civ(&new_civ);
            return -1;
        }
        const WlJsonVal *cf_val = wl_json_get(uu_val, "copy_from");
        if (!cf_val) {
            snprintf(err, errcap, "%s: unique_unit missing required key 'copy_from'", where);
            wl_free_civ(&new_civ);
            return -1;
        }
        const WlJsonVal *name_val_uu = wl_json_get(uu_val, "name");
        if (!name_val_uu) {
            snprintf(err, errcap, "%s: unique_unit missing required key 'name'", where);
            wl_free_civ(&new_civ);
            return -1;
        }

        const char *known_uu[] = {
            "copy_from", "elite_copy_from", "elite_cost", "elite_graphics", "elite_name",
            "elite_time", "elite_units", "graphics", "help", "name", "units"
        };
        char *extra_uu[16];
        size_t extra_uu_cnt = 0;
        for (size_t i = 0; i < uu_val->u.obj.count; i++) {
            const char *k = uu_val->u.obj.members[i].key;
            int ok = 0;
            for (size_t j = 0; j < sizeof(known_uu)/sizeof(*known_uu); j++) {
                if (strcmp(k, known_uu[j]) == 0) { ok = 1; break; }
            }
            if (!ok && extra_uu_cnt < 16) {
                extra_uu[extra_uu_cnt++] = (char *)k;
            }
        }
        if (extra_uu_cnt > 0) {
            if (extra_uu_cnt > 1) qsort(extra_uu, extra_uu_cnt, sizeof(char *), wl_cmp_str);
            char extra_buf[256];
            size_t epos = 0;
            epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "[");
            for (size_t i = 0; i < extra_uu_cnt; i++) {
                epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "%s'%s'", (i > 0 ? ", " : ""), extra_uu[i]);
            }
            snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "]");
            snprintf(err, errcap, "%s: unknown unique_unit key(s) %s", where, extra_buf);
            wl_free_civ(&new_civ);
            return -1;
        }

        int cid = (int)*civs_count;
        if (cid > 114) {  // SID = 28500 + (civ-35)*20 + slot: the game reads unit/tech name ids as SIGNED int16, so ids must be < 32768 (55000 showed the internal name); 28440..30100 is unused in the string files
            snprintf(err, errcap, "%s: civ_id %d exceeds 114 for unique_unit", where, cid);
            wl_free_civ(&new_civ);
            return -1;
        }

        int U = 1224 + 2 * (int)*p_uu_k;
        int E = U + 1;
        if (E > 1400) {
            snprintf(err, errcap, "%s: unique unit slot %d exceeds 1400", where, E);
            wl_free_civ(&new_civ);
            return -1;
        }

        for (int slot = U; slot <= E; slot++) {
            if ((size_t)slot >= civs[1].unit_count || civs[1].unit_offsets[slot] == 0) {
                snprintf(err, errcap, "%s: civ 1 unit at %d is not named 'RESERVED'", where, slot);
                wl_free_civ(&new_civ);
                return -1;
            }
            const uint8_t *u_data = civs[1].data + civs[1].unit_offsets[slot];
            int16_t nl = (int16_t)(u_data[1] | (u_data[2] << 8));
            uint8_t dmg = u_data[149];
            size_t name_offset = 150 + (size_t)dmg * 5 + 6;
            if (nl < 9 || memcmp(u_data + name_offset, "RESERVED", 8) != 0 || u_data[name_offset + 8] != '\0') {
                snprintf(err, errcap, "%s: civ 1 unit at %d is not named 'RESERVED'", where, slot);
                wl_free_civ(&new_civ);
                return -1;
            }
        }

        if (cf_val->type != WL_JSON_NUMBER || cf_val->u.num.is_float) {
            snprintf(err, errcap, "%s: unique_unit copy_from must be an integer", where);
            wl_free_civ(&new_civ);
            return -1;
        }
        long copy_from = cf_val->u.num.ival;
        if (copy_from < 0 || (size_t)copy_from >= new_civ.unit_count || new_civ.unit_offsets[copy_from] == 0) {
            snprintf(err, errcap, "%s: unique_unit copy_from unit %ld does not exist", where, copy_from);
            wl_free_civ(&new_civ);
            return -1;
        }
        uint8_t cf_type = new_civ.data[new_civ.unit_offsets[copy_from]];
        if (cf_type != 70) {
            snprintf(err, errcap, "%s: unique_unit copy_from unit %ld must be type 70 (got %d)", where, copy_from, (int)cf_type);
            wl_free_civ(&new_civ);
            return -1;
        }

        long elite_copy_from = copy_from;
        const WlJsonVal *ecf_val = wl_json_get(uu_val, "elite_copy_from");
        if (ecf_val) {
            if (ecf_val->type != WL_JSON_NUMBER || ecf_val->u.num.is_float) {
                snprintf(err, errcap, "%s: unique_unit elite_copy_from must be an integer", where);
                wl_free_civ(&new_civ);
                return -1;
            }
            elite_copy_from = ecf_val->u.num.ival;
            if (elite_copy_from < 0 || (size_t)elite_copy_from >= new_civ.unit_count || new_civ.unit_offsets[elite_copy_from] == 0) {
                snprintf(err, errcap, "%s: unique_unit elite_copy_from unit %ld does not exist", where, elite_copy_from);
                wl_free_civ(&new_civ);
                return -1;
            }
            uint8_t ecf_type = new_civ.data[new_civ.unit_offsets[elite_copy_from]];
            if (ecf_type != 70) {
                snprintf(err, errcap, "%s: unique_unit elite_copy_from unit %ld must be type 70 (got %d)", where, elite_copy_from, (int)ecf_type);
                wl_free_civ(&new_civ);
                return -1;
            }
        }

        int base_avail_id = -1;
        int uu_base_id = -1;
        for (size_t r = 0; r < orig_res_cnt; r++) {
            if (orig_researches[r].civ == (int16_t)base_id && orig_researches[r].effect_id >= 0 && (size_t)orig_researches[r].effect_id < ef_cnt) {
                const WlOrigEffect *eff = &orig_effects[orig_researches[r].effect_id];
                const uint8_t *cmds_raw = orig + eff->cmds_offset;
                for (uint16_t c = 0; c < eff->cmd_count; c++) {
                    const uint8_t *cmd = cmds_raw + (size_t)c * 11;
                    int8_t cmd_type = (int8_t)cmd[0];
                    int16_t a = (int16_t)(cmd[1] | (cmd[2] << 8));
                    int16_t b = (int16_t)(cmd[3] | (cmd[4] << 8));
                    if (cmd_type == 2 && b == 1) {
                        if (a >= 0 && (size_t)a < civs[base_id].unit_count && civs[base_id].unit_offsets[a] != 0) {
                            const uint8_t *u_data = civs[base_id].data + civs[base_id].unit_offsets[a];
                            if (u_data[0] >= 70) {
                                int16_t nl = (int16_t)(u_data[1] | (u_data[2] << 8));
                                uint8_t dmg = u_data[149];
                                size_t s1 = 150 + (size_t)dmg * 5;
                                size_t s2 = s1 + 6 + (size_t)nl;
                                size_t p_atk = s2 + 4 + 4 + 37 + 20 + 2;
                                uint16_t at_cnt = (uint16_t)(u_data[p_atk] | (u_data[p_atk + 1] << 8));
                                size_t s3 = p_atk + 2 + (size_t)at_cnt * 4;
                                uint16_t ar_cnt = (uint16_t)(u_data[s3] | (u_data[s3 + 1] << 8));
                                size_t s4 = s3 + 2 + (size_t)ar_cnt * 4;
                                int16_t train_loc = (int16_t)(u_data[s4 + 76] | (u_data[s4 + 77] << 8));
                                if (train_loc == 82) {
                                    base_avail_id = (int)r;
                                    uu_base_id = a;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (base_avail_id >= 0) break;
            }
        }

        int base_elite_id = -1;
        if (uu_base_id >= 0) {
            for (size_t r = 0; r < orig_res_cnt; r++) {
                if (orig_researches[r].civ == (int16_t)base_id && orig_researches[r].research_location == 82 &&
                    orig_researches[r].effect_id >= 0 && (size_t)orig_researches[r].effect_id < ef_cnt) {
                    const WlOrigEffect *eff = &orig_effects[orig_researches[r].effect_id];
                    const uint8_t *cmds_raw = orig + eff->cmds_offset;
                    for (uint16_t c = 0; c < eff->cmd_count; c++) {
                        const uint8_t *cmd = cmds_raw + (size_t)c * 11;
                        int8_t cmd_type = (int8_t)cmd[0];
                        int16_t a = (int16_t)(cmd[1] | (cmd[2] << 8));
                        if (cmd_type == 3 && a == uu_base_id) {
                            base_elite_id = (int)r;
                            break;
                        }
                    }
                    if (base_elite_id >= 0) break;
                }
            }
        }

        if (base_avail_id < 0 || base_elite_id < 0) {
            snprintf(err, errcap, "%s: base civ %lld has no castle unique unit techs", where, (long long)base_id);
            wl_free_civ(&new_civ);
            return -1;
        }

        int sid_base = 28500 + (cid - 35) * 20;

        // 1. Create Avail effect
        WlDatEffect avail_eff;
        memset(&avail_eff, 0, sizeof(avail_eff));
        char ef_name[64];
        snprintf(ef_name, sizeof(ef_name), "WL UU avail %d", cid);
        if (wl_make_effect_name(ef_name, where, avail_eff.name, err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }
        avail_eff.cmd_count = 1;
        avail_eff.cmd_cap = 1;
        avail_eff.commands = malloc(sizeof(WlDatEffectCmd));
        avail_eff.commands[0].type = 2;
        avail_eff.commands[0].a = (int16_t)U;
        avail_eff.commands[0].b = 1;
        avail_eff.commands[0].c = -1;
        avail_eff.commands[0].d = 0.0f;

        if (*p_custom_effects_cnt >= *p_custom_effects_cap) {
            size_t ncap = (*p_custom_effects_cap == 0) ? 16 : (*p_custom_effects_cap * 2);
            WlDatEffect *neffs = realloc(*p_custom_effects, ncap * sizeof(WlDatEffect));
            if (!neffs) { free(avail_eff.commands); wl_free_civ(&new_civ); snprintf(err, errcap, "out of memory"); return -1; }
            *p_custom_effects = neffs;
            *p_custom_effects_cap = ncap;
        }
        size_t avail_eff_id = ef_cnt + *p_custom_effects_cnt;
        (*p_custom_effects)[(*p_custom_effects_cnt)++] = avail_eff;

        // 2. Create Elite effect
        WlDatEffect elite_eff;
        memset(&elite_eff, 0, sizeof(elite_eff));
        snprintf(ef_name, sizeof(ef_name), "WL UU elite %d", cid);
        if (wl_make_effect_name(ef_name, where, elite_eff.name, err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }
        elite_eff.cmd_count = 1;
        elite_eff.cmd_cap = 1;
        elite_eff.commands = malloc(sizeof(WlDatEffectCmd));
        elite_eff.commands[0].type = 3;
        elite_eff.commands[0].a = (int16_t)U;
        elite_eff.commands[0].b = (int16_t)E;
        elite_eff.commands[0].c = -1;
        elite_eff.commands[0].d = 0.0f;

        if (*p_custom_effects_cnt >= *p_custom_effects_cap) {
            size_t ncap = (*p_custom_effects_cap == 0) ? 16 : (*p_custom_effects_cap * 2);
            WlDatEffect *neffs = realloc(*p_custom_effects, ncap * sizeof(WlDatEffect));
            if (!neffs) { free(elite_eff.commands); wl_free_civ(&new_civ); snprintf(err, errcap, "out of memory"); return -1; }
            *p_custom_effects = neffs;
            *p_custom_effects_cap = ncap;
        }
        size_t elite_eff_id = ef_cnt + *p_custom_effects_cnt;
        (*p_custom_effects)[(*p_custom_effects_cnt)++] = elite_eff;

        // 3. Create Avail research clone
        WlDatResearch avail_res;
        size_t b_avail_off = orig_researches[base_avail_id].offset;
        memcpy(avail_res.header, orig + b_avail_off, 62);
        avail_res.header[29] = (uint8_t)(cid & 0xFF);
        avail_res.header[30] = (uint8_t)((cid >> 8) & 0xFF);
        avail_res.header[41] = (uint8_t)(avail_eff_id & 0xFF);
        avail_res.header[42] = (uint8_t)((avail_eff_id >> 8) & 0xFF);
        int sid6 = sid_base + 6;
        avail_res.header[35] = (uint8_t)(sid6 & 0xFF);
        avail_res.header[36] = (uint8_t)((sid6 >> 8) & 0xFF);
        avail_res.header[37] = (uint8_t)(sid6 & 0xFF);
        avail_res.header[38] = (uint8_t)((sid6 >> 8) & 0xFF);
        char res_name_buf[32];
        snprintf(res_name_buf, sizeof(res_name_buf), "WL UU %d", cid);
        uint16_t rnl = (uint16_t)(strlen(res_name_buf) + 1);
        avail_res.header[60] = (uint8_t)(rnl & 0xFF);
        avail_res.header[61] = (uint8_t)((rnl >> 8) & 0xFF);
        avail_res.name_length = rnl;
        avail_res.name = malloc(rnl);
        memcpy(avail_res.name, res_name_buf, rnl);

        size_t new_avail_id = orig_res_cnt + *p_custom_researches_cnt;
        if (*p_custom_researches_cnt >= *p_custom_researches_cap) {
            size_t ncap = (*p_custom_researches_cap == 0) ? 16 : (*p_custom_researches_cap * 2);
            WlDatResearch *nres = realloc(*p_custom_researches, ncap * sizeof(WlDatResearch));
            if (!nres) { free(avail_res.name); wl_free_civ(&new_civ); snprintf(err, errcap, "out of memory"); return -1; }
            *p_custom_researches = nres;
            *p_custom_researches_cap = ncap;
        }
        (*p_custom_researches)[(*p_custom_researches_cnt)++] = avail_res;

        // 4. Create Elite research clone
        WlDatResearch elite_res;
        size_t b_elite_off = orig_researches[base_elite_id].offset;
        memcpy(elite_res.header, orig + b_elite_off, 62);
        elite_res.header[29] = (uint8_t)(cid & 0xFF);
        elite_res.header[30] = (uint8_t)((cid >> 8) & 0xFF);
        elite_res.header[41] = (uint8_t)(elite_eff_id & 0xFF);
        elite_res.header[42] = (uint8_t)((elite_eff_id >> 8) & 0xFF);
        int sid7 = sid_base + 7;
        elite_res.header[35] = (uint8_t)(sid7 & 0xFF);
        elite_res.header[36] = (uint8_t)((sid7 >> 8) & 0xFF);
        int sid8 = sid_base + 8;
        elite_res.header[37] = (uint8_t)(sid8 & 0xFF);
        elite_res.header[38] = (uint8_t)((sid8 >> 8) & 0xFF);
        int sid9 = 79000 + sid_base + 9;  // help ids: the game shows string (help - 79000), e.g. 107381 -> 28381
        elite_res.header[48] = (uint8_t)(sid9 & 0xFF);
        elite_res.header[49] = (uint8_t)((sid9 >> 8) & 0xFF);
        elite_res.header[50] = (uint8_t)((sid9 >> 16) & 0xFF);
        elite_res.header[51] = (uint8_t)((sid9 >> 24) & 0xFF);

        int16_t *reqs = (int16_t *)(elite_res.header + 0);
        int rep_found = 0;
        for (int i = 0; i < 6; i++) {
            if (reqs[i] == (int16_t)base_avail_id) {
                reqs[i] = (int16_t)new_avail_id;
                rep_found = 1;
                break;
            }
        }
        if (!rep_found) {
            for (int i = 0; i < 6; i++) {
                if (reqs[i] != 103 && reqs[i] >= 0) {
                    reqs[i] = (int16_t)new_avail_id;
                    rep_found = 1;
                    break;
                }
            }
        }

        const WlJsonVal *ecost_val = wl_json_get(uu_val, "elite_cost");
        int cost_types[3] = { -1, -1, -1 };
        int cost_amounts[3] = { 0, 0, 0 };
        int cost_cnt = 0;
        if (ecost_val) {
            if (ecost_val->type != WL_JSON_OBJECT) {
                snprintf(err, errcap, "%s: unique_unit elite_cost must be an object", where);
                wl_free_civ(&new_civ);
                return -1;
            }
            const char *cost_names[] = { "food", "wood", "stone", "gold" };
            int cost_ids[] = { 0, 1, 2, 3 };
            char *extra_rc[8];
            size_t extra_rc_cnt = 0;
            for (size_t mi = 0; mi < ecost_val->u.obj.count; mi++) {
                const char *ck = ecost_val->u.obj.members[mi].key;
                int valid_res = 0;
                for (int ci = 0; ci < 4; ci++) {
                    if (strcmp(ck, cost_names[ci]) == 0) { valid_res = 1; break; }
                }
                if (!valid_res && extra_rc_cnt < 8) {
                    extra_rc[extra_rc_cnt++] = (char *)ck;
                }
            }
            if (extra_rc_cnt > 0) {
                if (extra_rc_cnt > 1) qsort(extra_rc, extra_rc_cnt, sizeof(char *), wl_cmp_str);
                char extra_buf[256];
                size_t epos = 0;
                epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "[");
                for (size_t i = 0; i < extra_rc_cnt; i++) {
                    epos += snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "%s'%s'", (i > 0 ? ", " : ""), extra_rc[i]);
                }
                snprintf(extra_buf + epos, sizeof(extra_buf) - epos, "]");
                snprintf(err, errcap, "%s: unknown elite_cost resource(s) %s", where, extra_buf);
                wl_free_civ(&new_civ);
                return -1;
            }
            for (int ci = 0; ci < 4; ci++) {
                const WlJsonVal *cv = wl_json_get(ecost_val, cost_names[ci]);
                if (cv) {
                    if (cv->type != WL_JSON_NUMBER || cv->u.num.dval < 0) {
                        snprintf(err, errcap, "%s: elite_cost %s must be a positive number", where, cost_names[ci]);
                        wl_free_civ(&new_civ);
                        return -1;
                    }
                    if (cv->u.num.ival > 0) {
                        if (cost_cnt < 3) {
                            cost_types[cost_cnt] = cost_ids[ci];
                            cost_amounts[cost_cnt] = (int)cv->u.num.ival;
                            cost_cnt++;
                        } else {
                            snprintf(err, errcap, "%s: unique_unit elite_cost has more than 3 non-zero costs", where);
                            wl_free_civ(&new_civ);
                            return -1;
                        }
                    }
                }
            }
        } else {
            cost_types[0] = 0; cost_amounts[0] = 1000;
            cost_types[1] = 3; cost_amounts[1] = 800;
            cost_cnt = 2;
        }
        for (int si = 0; si < 3; si++) {
            uint8_t *cp = elite_res.header + 12 + si * 5;
            int16_t t = (int16_t)cost_types[si];
            int16_t a = (int16_t)cost_amounts[si];
            int8_t fl = (cost_types[si] >= 0) ? 1 : 0;
            cp[0] = (uint8_t)(t & 0xFF); cp[1] = (uint8_t)((t >> 8) & 0xFF);
            cp[2] = (uint8_t)(a & 0xFF); cp[3] = (uint8_t)((a >> 8) & 0xFF);
            cp[4] = (uint8_t)fl;
        }

        const WlJsonVal *etime_val = wl_json_get(uu_val, "elite_time");
        int etime = 60;
        if (etime_val) {
            if (etime_val->type != WL_JSON_NUMBER || etime_val->u.num.is_float || etime_val->u.num.ival < 0) {
                snprintf(err, errcap, "%s: unique_unit elite_time must be a positive integer", where);
                wl_free_civ(&new_civ);
                return -1;
            }
            etime = (int)etime_val->u.num.ival;
        }
        elite_res.header[39] = (uint8_t)(etime & 0xFF);
        elite_res.header[40] = (uint8_t)((etime >> 8) & 0xFF);

        uint16_t enl = orig_researches[base_elite_id].name_length;
        elite_res.name_length = enl;
        elite_res.name = malloc(enl);
        memcpy(elite_res.name, orig + b_elite_off + 62, enl);

        if (*p_custom_researches_cnt >= *p_custom_researches_cap) {
            size_t ncap = (*p_custom_researches_cap == 0) ? 16 : (*p_custom_researches_cap * 2);
            WlDatResearch *nres = realloc(*p_custom_researches, ncap * sizeof(WlDatResearch));
            if (!nres) { free(elite_res.name); wl_free_civ(&new_civ); snprintf(err, errcap, "out of memory"); return -1; }
            *p_custom_researches = nres;
            *p_custom_researches_cap = ncap;
        }
        (*p_custom_researches)[(*p_custom_researches_cnt)++] = elite_res;
        size_t new_elite_id = orig_res_cnt + *p_custom_researches_cnt - 1;  // id of the elite tech just pushed

        // 4b. Tech tree records for the castle unit (re/research/techtree.md).
        // Parsed lazily here, so mods without unique units never touch the section.
        if (wl_tt_ensure_parsed(tts, err, errcap) != 0 ||
            wl_tt_add_unique_unit(tts, (int)base_avail_id, (int)new_avail_id, (int)new_elite_id,
                                  U, E, where, err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }

        // 5. Build unit U
        const uint8_t *src_u = new_civ.data + new_civ.unit_offsets[copy_from];
        size_t len_U = wl_calc_unit_len(new_civ.data, new_civ.unit_offsets[copy_from]);
        uint8_t *rep_U = malloc(len_U);
        memcpy(rep_U, src_u, len_U);

        int16_t u16_U = (int16_t)U;
        rep_U[3] = (uint8_t)(u16_U & 0xFF); rep_U[4] = (uint8_t)((u16_U >> 8) & 0xFF);
        int sid0 = sid_base + 0;
        rep_U[5] = (uint8_t)(sid0 & 0xFF); rep_U[6] = (uint8_t)((sid0 >> 8) & 0xFF);
        int sid1 = sid_base + 1;
        rep_U[7] = (uint8_t)(sid1 & 0xFF); rep_U[8] = (uint8_t)((sid1 >> 8) & 0xFF);
        rep_U[52] = 0; rep_U[53] = 0; // enabled = 0
        int sid2 = 79000 + sid_base + 2;  // help id: game shows string (help - 79000), e.g. 105107 -> 26107
        rep_U[91] = (uint8_t)(sid2 & 0xFF); rep_U[92] = (uint8_t)((sid2 >> 8) & 0xFF);
        rep_U[93] = (uint8_t)((sid2 >> 16) & 0xFF); rep_U[94] = (uint8_t)((sid2 >> 24) & 0xFF);
        int hk0 = 150000 + sid0;  // hotkey_text: real units use 150000 + name_id, needed for rich tooltip
        rep_U[95] = (uint8_t)(hk0 & 0xFF); rep_U[96] = (uint8_t)((hk0 >> 8) & 0xFF);
        rep_U[97] = (uint8_t)((hk0 >> 16) & 0xFF); rep_U[98] = (uint8_t)((hk0 >> 24) & 0xFF);

        int16_t nl_U = (int16_t)(rep_U[1] | (rep_U[2] << 8));
        uint8_t dmg_U = rep_U[149];
        size_t s1_U = 150 + (size_t)dmg_U * 5;

        // Replace unit name with the display name from JSON
        const WlJsonVal *uu_name_val = wl_json_get(uu_val, "name");
        if (!uu_name_val || uu_name_val->type != WL_JSON_STRING || !uu_name_val->u.str) {
            snprintf(err, errcap, "%s: unique_unit missing 'name' field", where);
            free(rep_U); wl_free_civ(&new_civ);
            return -1;
        }
        char new_name_U[32];
        size_t new_nl_U;
        char where_uu_name[1024];
        snprintf(where_uu_name, sizeof(where_uu_name), "%.500s unique_unit", where);
        if (wl_make_unit_name(uu_name_val->u.str, where_uu_name, new_name_U, &new_nl_U, err, errcap) != 0) {
            free(rep_U); wl_free_civ(&new_civ);
            return -1;
        }

        // Splice buffer if name length changed
        if ((int16_t)new_nl_U != nl_U) {
            size_t new_len_U = len_U - (size_t)nl_U + new_nl_U;
            uint8_t *new_rep_U = malloc(new_len_U);
            if (!new_rep_U) {
                snprintf(err, errcap, "out of memory");
                free(rep_U); wl_free_civ(&new_civ);
                return -1;
            }
            // Copy before name: [0..s1_U+5]
            memcpy(new_rep_U, rep_U, s1_U + 6);
            // Copy new name
            memcpy(new_rep_U + s1_U + 6, new_name_U, new_nl_U);
            // Copy after name: [s1_U+6+nl_U..len_U-1]
            size_t old_s2_U = s1_U + 6 + (size_t)nl_U;
            size_t new_s2_U = s1_U + 6 + new_nl_U;
            memcpy(new_rep_U + new_s2_U, rep_U + old_s2_U, len_U - old_s2_U);
            // Update name_length field
            new_rep_U[1] = (uint8_t)(new_nl_U & 0xFF);
            new_rep_U[2] = (uint8_t)((new_nl_U >> 8) & 0xFF);
            free(rep_U);
            rep_U = new_rep_U;
            len_U = new_len_U;
            nl_U = (int16_t)new_nl_U;
        } else {
            // Same length, just replace in place
            memcpy(rep_U + s1_U + 6, new_name_U, new_nl_U);
        }

        size_t s2_U = s1_U + 6 + (size_t)nl_U;
        rep_U[s2_U + 0] = (uint8_t)(u16_U & 0xFF); rep_U[s2_U + 1] = (uint8_t)((u16_U >> 8) & 0xFF);
        rep_U[s2_U + 2] = (uint8_t)(u16_U & 0xFF); rep_U[s2_U + 3] = (uint8_t)((u16_U >> 8) & 0xFF);

        size_t p_atk_U = s2_U + 4 + 4 + 37 + 20 + 2;
        uint16_t at_cnt_U = (uint16_t)(rep_U[p_atk_U] | (rep_U[p_atk_U + 1] << 8));
        size_t s3_U = p_atk_U + 2 + (size_t)at_cnt_U * 4;
        uint16_t ar_cnt_U = (uint16_t)(rep_U[s3_U] | (rep_U[s3_U + 1] << 8));
        size_t s4_U = s3_U + 2 + (size_t)ar_cnt_U * 4;
        rep_U[s4_U + 76] = 82; rep_U[s4_U + 77] = 0; // train_location_id = 82
        rep_U[s4_U + 78] = 1;                        // button_id = 1

        const WlJsonVal *uu_units_val = wl_json_get(uu_val, "units");
        if (uu_units_val) {
            if (uu_units_val->type != WL_JSON_OBJECT) {
                snprintf(err, errcap, "%s: unique_unit units must be an object", where);
                free(rep_U); wl_free_civ(&new_civ);
                return -1;
            }
            char where_unit[1024];
            snprintf(where_unit, sizeof(where_unit), "%.500s unit %d", where, U);
            for (size_t f = 0; f < uu_units_val->u.obj.count; f++) {
                const char *fname = uu_units_val->u.obj.members[f].key;
                const WlJsonVal *fval = uu_units_val->u.obj.members[f].val;
                if (wl_set_unit_field(rep_U, where_unit, fname, fval, err, errcap) != 0) {
                    free(rep_U); wl_free_civ(&new_civ);
                    return -1;
                }
            }
        }
        const WlJsonVal *uu_gfx = wl_json_get(uu_val, "graphics");
        if (uu_gfx) {
            char where_g[1024];
            snprintf(where_g, sizeof(where_g), "%.500s unique_unit", where);
            if (wl_apply_graphics(&gctx, rep_U, uu_gfx, where_g, err, errcap) != 0) {
                free(rep_U); wl_free_civ(&new_civ);
                return -1;
            }
        }

        // 6. Build unit E
        const uint8_t *src_e = new_civ.data + new_civ.unit_offsets[elite_copy_from];
        size_t len_E = wl_calc_unit_len(new_civ.data, new_civ.unit_offsets[elite_copy_from]);
        uint8_t *rep_E = malloc(len_E);
        memcpy(rep_E, src_e, len_E);

        int16_t u16_E = (int16_t)E;
        rep_E[3] = (uint8_t)(u16_E & 0xFF); rep_E[4] = (uint8_t)((u16_E >> 8) & 0xFF);
        int sid3 = sid_base + 3;
        rep_E[5] = (uint8_t)(sid3 & 0xFF); rep_E[6] = (uint8_t)((sid3 >> 8) & 0xFF);
        int sid4 = sid_base + 4;
        rep_E[7] = (uint8_t)(sid4 & 0xFF); rep_E[8] = (uint8_t)((sid4 >> 8) & 0xFF);
        rep_E[52] = 0; rep_E[53] = 0; // enabled = 0
        int sid5 = 79000 + sid_base + 5;
        rep_E[91] = (uint8_t)(sid5 & 0xFF); rep_E[92] = (uint8_t)((sid5 >> 8) & 0xFF);
        rep_E[93] = (uint8_t)((sid5 >> 16) & 0xFF); rep_E[94] = (uint8_t)((sid5 >> 24) & 0xFF);
        int hk3 = 150000 + sid3;  // hotkey_text: real units use 150000 + name_id, needed for rich tooltip
        rep_E[95] = (uint8_t)(hk3 & 0xFF); rep_E[96] = (uint8_t)((hk3 >> 8) & 0xFF);
        rep_E[97] = (uint8_t)((hk3 >> 16) & 0xFF); rep_E[98] = (uint8_t)((hk3 >> 24) & 0xFF);

        int16_t nl_E = (int16_t)(rep_E[1] | (rep_E[2] << 8));
        uint8_t dmg_E = rep_E[149];
        size_t s1_E = 150 + (size_t)dmg_E * 5;

        // Replace elite unit name with "Elite " + name or elite_name from JSON
        const WlJsonVal *uu_elite_name_val = wl_json_get(uu_val, "elite_name");
        char elite_name_text[256];
        if (uu_elite_name_val && uu_elite_name_val->type == WL_JSON_STRING && uu_elite_name_val->u.str) {
            snprintf(elite_name_text, sizeof(elite_name_text), "%s", uu_elite_name_val->u.str);
        } else {
            snprintf(elite_name_text, sizeof(elite_name_text), "Elite %s", uu_name_val->u.str);
        }
        char new_name_E[32];
        size_t new_nl_E;
        char where_elite_name[1024];
        snprintf(where_elite_name, sizeof(where_elite_name), "%.500s unique_unit elite", where);
        if (wl_make_unit_name(elite_name_text, where_elite_name, new_name_E, &new_nl_E, err, errcap) != 0) {
            free(rep_U); free(rep_E); wl_free_civ(&new_civ);
            return -1;
        }

        // Splice buffer if name length changed
        if ((int16_t)new_nl_E != nl_E) {
            size_t new_len_E = len_E - (size_t)nl_E + new_nl_E;
            uint8_t *new_rep_E = malloc(new_len_E);
            if (!new_rep_E) {
                snprintf(err, errcap, "out of memory");
                free(rep_U); free(rep_E); wl_free_civ(&new_civ);
                return -1;
            }
            // Copy before name: [0..s1_E+5]
            memcpy(new_rep_E, rep_E, s1_E + 6);
            // Copy new name
            memcpy(new_rep_E + s1_E + 6, new_name_E, new_nl_E);
            // Copy after name: [s1_E+6+nl_E..len_E-1]
            size_t old_s2_E = s1_E + 6 + (size_t)nl_E;
            size_t new_s2_E = s1_E + 6 + new_nl_E;
            memcpy(new_rep_E + new_s2_E, rep_E + old_s2_E, len_E - old_s2_E);
            // Update name_length field
            new_rep_E[1] = (uint8_t)(new_nl_E & 0xFF);
            new_rep_E[2] = (uint8_t)((new_nl_E >> 8) & 0xFF);
            free(rep_E);
            rep_E = new_rep_E;
            len_E = new_len_E;
            nl_E = (int16_t)new_nl_E;
        } else {
            // Same length, just replace in place
            memcpy(rep_E + s1_E + 6, new_name_E, new_nl_E);
        }

        size_t s2_E = s1_E + 6 + (size_t)nl_E;
        rep_E[s2_E + 0] = (uint8_t)(u16_E & 0xFF); rep_E[s2_E + 1] = (uint8_t)((u16_E >> 8) & 0xFF);
        rep_E[s2_E + 2] = (uint8_t)(u16_E & 0xFF); rep_E[s2_E + 3] = (uint8_t)((u16_E >> 8) & 0xFF);

        size_t p_atk_E = s2_E + 4 + 4 + 37 + 20 + 2;
        uint16_t at_cnt_E = (uint16_t)(rep_E[p_atk_E] | (rep_E[p_atk_E + 1] << 8));
        size_t s3_E = p_atk_E + 2 + (size_t)at_cnt_E * 4;
        uint16_t ar_cnt_E = (uint16_t)(rep_E[s3_E] | (rep_E[s3_E + 1] << 8));
        size_t s4_E = s3_E + 2 + (size_t)ar_cnt_E * 4;
        rep_E[s4_E + 76] = 82; rep_E[s4_E + 77] = 0; // train_location_id = 82
        rep_E[s4_E + 78] = 1;                        // button_id = 1

        if (uu_units_val) {
            char where_unit[1024];
            snprintf(where_unit, sizeof(where_unit), "%.500s unit %d", where, E);
            for (size_t f = 0; f < uu_units_val->u.obj.count; f++) {
                const char *fname = uu_units_val->u.obj.members[f].key;
                const WlJsonVal *fval = uu_units_val->u.obj.members[f].val;
                if (wl_set_unit_field(rep_E, where_unit, fname, fval, err, errcap) != 0) {
                    free(rep_U); free(rep_E); wl_free_civ(&new_civ);
                    return -1;
                }
            }
        }

        const WlJsonVal *uu_el_units_val = wl_json_get(uu_val, "elite_units");
        if (uu_el_units_val) {
            if (uu_el_units_val->type != WL_JSON_OBJECT) {
                snprintf(err, errcap, "%s: unique_unit elite_units must be an object", where);
                free(rep_U); free(rep_E); wl_free_civ(&new_civ);
                return -1;
            }
            char where_unit[1024];
            snprintf(where_unit, sizeof(where_unit), "%.500s unit %d", where, E);
            for (size_t f = 0; f < uu_el_units_val->u.obj.count; f++) {
                const char *fname = uu_el_units_val->u.obj.members[f].key;
                const WlJsonVal *fval = uu_el_units_val->u.obj.members[f].val;
                if (wl_set_unit_field(rep_E, where_unit, fname, fval, err, errcap) != 0) {
                    free(rep_U); free(rep_E); wl_free_civ(&new_civ);
                    return -1;
                }
            }
        }
        const WlJsonVal *el_gfx = wl_json_get(uu_val, "elite_graphics");
        if (!el_gfx) el_gfx = uu_gfx;
        if (el_gfx) {
            char where_g[1024];
            snprintf(where_g, sizeof(where_g), "%.500s unique_unit elite", where);
            if (wl_apply_graphics(&gctx, rep_E, el_gfx, where_g, err, errcap) != 0) {
                free(rep_U); free(rep_E); wl_free_civ(&new_civ);
                return -1;
            }
        }

        if (wl_replace_civ_units(&new_civ, U, rep_U, len_U, E, rep_E, len_E, err, errcap) != 0) {
            free(rep_U); free(rep_E); wl_free_civ(&new_civ);
            return -1;
        }
        free(rep_U);
        free(rep_E);
        (*p_uu_k)++;
    }

    // unique_building
    const WlJsonVal *ub_val = wl_json_get(spec, "unique_building");
    if (ub_val) {
        int cid = (int)*civs_count;
        if (wl_add_unique_building(&new_civ, civs, cid, ub_val, where, *p_ub_k, uu_total,
                                   orig, graphics_section_offset, orig_graphics_count,
                                   orig_effects, ef_cnt,
                                   p_custom_effects, p_custom_effects_cnt, p_custom_effects_cap,
                                   orig_researches, orig_res_cnt,
                                   p_custom_researches, p_custom_researches_cnt, p_custom_researches_cap,
                                   p_custom_graphics, p_custom_graphics_cnt, p_custom_graphics_cap,
                                   ext,
                                   err, errcap) != 0) {
            wl_free_civ(&new_civ);
            return -1;
        }
        *p_ub_k = ext->slots;   // building slot + one per train command (build.py: ub_k = ext["slots"])
    }

    *out_cid = (int)*civs_count;   // id of the civ just added (same as `cid` above)
    civs[*civs_count] = new_civ;
    (*civs_count)++;
    return 0;
}

// ==========================================================================
// Main API Entry Point: wl_dat_build
// ==========================================================================

int wl_dat_build(const WlZlib *z,
                 const char *src_dat,
                 const char *mods_dir,
                 const char *out_dat,
                 const char *out_civs_txt,
                 const char *out_strings_txt,
                 const char *out_commands_txt,
                 char *err,
                 size_t errcap) {
    if (!z || !z->inflate_raw || !z->deflate_raw) {
        snprintf(err, errcap, "invalid WlZlib pointers");
        return -1;
    }
    err[0] = '\0';

    WlBuildExt ext;
    wl_ext_init(&ext);

    WlOrigEffect *orig_effects = NULL;
    WlDatEffect *custom_effects = NULL;
    size_t custom_effects_cnt = 0, custom_effects_cap = 0;

    WlOrigResearch *orig_researches = NULL;
    WlDatResearch *custom_researches = NULL;
    size_t custom_researches_cnt = 0, custom_researches_cap = 0;

    WlCustomGraphic *custom_graphics = NULL;
    size_t custom_graphics_cnt = 0, custom_graphics_cap = 0;

    size_t uu_k = 0;
    size_t ub_k = 0;

    // ----------------------------------------------------------------------
    // 1. Read and decompress source dat
    // ----------------------------------------------------------------------
    size_t comp_len = 0;
    unsigned char *comp_data = (unsigned char *)wl_read_file(src_dat, &comp_len);
    if (!comp_data) {
        snprintf(err, errcap, "%s: %s", src_dat, strerror(errno));
        return -1;
    }

    unsigned char *orig = NULL;
    size_t orig_len = 0;
    if (z->inflate_raw(comp_data, comp_len, &orig, &orig_len) != 0) {
        free(comp_data);
        snprintf(err, errcap, "failed to decompress '%s'", src_dat);
        return -1;
    }
    free(comp_data);

    // Section 1: Version Header (8 bytes).
    // Format: Fixed char[8] containing the version identifier (e.g. 'VER 5.7\0').
    // Validates that the input is a valid AoE2 Genie dat file before proceeding.
    if (orig_len < 8 || memcmp(orig, "VER ", 4) != 0) {
        free(orig);
        snprintf(err, errcap, "not a dat file (invalid version header)");
        return -1;
    }

    // ----------------------------------------------------------------------
    // 2. Parse section boundaries up to civilizations
    // ----------------------------------------------------------------------
    size_t pos = 8;

    // Section 2: Terrain Restrictions.
    // Layout:
    //   - restriction_count (uint16_t, 2 bytes)
    //   - terrain_count (uint16_t, 2 bytes)
    //   - restriction pointers (restriction_count * 4 bytes)
    //   - passability tables: restriction_count * 8 + restriction_count * terrain_count * 20 bytes.
    // Why parsed: Skipped to locate the sounds section boundary without drift.
    if (pos + 4 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t restr_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    uint16_t terr_cnt = (uint16_t)(orig[pos + 2] | (orig[pos + 3] << 8));
    pos += 4 + (size_t)restr_cnt * 8 + (size_t)restr_cnt * (size_t)terr_cnt * 20;

    // Section 3: Player Colours.
    // Layout:
    //   - color_count (uint16_t, 2 bytes)
    //   - color entries: color_count * 36 bytes.
    // Why parsed: Skipped to reach sounds_start.
    if (pos + 2 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t color_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2 + (size_t)color_cnt * 36;

    size_t sounds_start = pos;

    // Section 4: Sounds (MODIFIED SECTION).
    // Layout:
    //   - sound_count (uint16_t, 2 bytes)
    //   - for each sound: id (2B), play_delay (2B), file_count (2B), cache_time (4B),
    //     followed by file_count * 23 bytes (SoundItem records).
    // SoundItem record layout (23 bytes):
    //   - filename (char[13]), resource_id (int32_t), probability (int16_t),
    //     civilization (int16_t at offset 19-20), icon_id (int16_t).
    // Why modified: Unit sound events filter by SoundItem.civilization. Custom civs
    // clone the base civ's sound items so custom civ units are not silent.
    if (pos + 2 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t snd_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;

    WlDatSound *sounds = malloc(snd_cnt * sizeof(WlDatSound));
    if (!sounds) { free(orig); snprintf(err, errcap, "out of memory"); return -1; }

    for (size_t s = 0; s < snd_cnt; s++) {
        if (pos + 10 > orig_len) { free(sounds); free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
        sounds[s].id = (int16_t)(orig[pos] | (orig[pos + 1] << 8));
        sounds[s].play_delay = (int16_t)(orig[pos + 2] | (orig[pos + 3] << 8));
        sounds[s].file_count = (int16_t)(orig[pos + 4] | (orig[pos + 5] << 8));
        sounds[s].cache_time = (int32_t)(orig[pos + 6] | (orig[pos + 7] << 8) | (orig[pos + 8] << 16) | (orig[pos + 9] << 24));
        pos += 10;
        size_t items_sz = (size_t)sounds[s].file_count * 23;
        sounds[s].cap = (sounds[s].file_count > 0 ? sounds[s].file_count : 1);
        sounds[s].items = malloc(sounds[s].cap * 23);
        if (!sounds[s].items) { free(sounds); free(orig); snprintf(err, errcap, "out of memory"); return -1; }
        memcpy(sounds[s].items, orig + pos, items_sz);
        pos += items_sz;
    }
    // Section 4 end = Section 5 start

    // Section 5: Graphics.
    // Layout:
    //   - graphic_count (uint16_t, 2 bytes)
    //   - pointers: graphic_count * 4 bytes
    //   - graphic headers: 78 bytes + delta_cnt * 16 bytes. If attack_sound_used, + angle_cnt * 12 bytes.
    // Why parsed: Dynamic size traversal to locate the terrain block.
    // MODIFIED: When unique_building with "slp" is used, custom graphics are appended.
    size_t graphics_section_offset = pos;
    if (pos + 2 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t g_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;
    const uint8_t *g_ptrs = orig + pos;
    pos += (size_t)g_cnt * 4;
    for (size_t g = 0; g < g_cnt; g++) {
        int32_t p = (int32_t)(g_ptrs[g * 4] | (g_ptrs[g * 4 + 1] << 8) | (g_ptrs[g * 4 + 2] << 16) | (g_ptrs[g * 4 + 3] << 24));
        if (p) {
            uint16_t delta_cnt = (uint16_t)(orig[pos + 52] | (orig[pos + 53] << 8));
            uint8_t atk_snd_used = orig[pos + 56];
            uint16_t angle_cnt = (uint16_t)(orig[pos + 59] | (orig[pos + 60] << 8));
            pos += 78 + (size_t)delta_cnt * 16;
            if (atk_snd_used) pos += (size_t)angle_cnt * 12;
        }
    }
    size_t graphics_end = pos;

    // Section 6: Terrain Block.
    // Layout:
    //   - header and tile sizes (24 + 114 + 2 = 140 bytes)
    //   - terrain slots: slots * (436 + 2 * (slots - 42)) bytes
    //   - border definitions: 16 * 1440 bytes
    //   - trailing metadata: 716 bytes
    // RULE (Terrain slot determination):
    //   Terrain slot counts vary between AoE2 releases (42 in original AoC, 100+ in HD).
    //   Candidate counts are probed for sentinel 0xFFFFFFFF markers before border tables
    //   (mirroring tools/datfile.py TerrainBlock.parse).
    size_t tb_base = pos + 24 + 114 + 2;
    int slots_found = -1;
    int candidates[4] = { (int)terr_cnt + 1, (int)terr_cnt, 42, 100 };
    for (int ci = 0; ci < 4; ci++) {
        int s = candidates[ci];
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
        snprintf(err, errcap, "cannot determine terrain slot count");
        return -1;
    }
    size_t tb_stride = 436 + 2 * (size_t)(slots_found - 42);
    pos = tb_base + (size_t)slots_found * tb_stride + 16 * 1440 + 716;

    // Section 7: Random Maps.
    // Layout:
    //   - map_count (uint32_t, 4 bytes)
    //   - map headers: 8 + map_count * 72 bytes
    //   - map bodies: lands (lc * 44B), terrains (tc * 24B), units (uc * 44B), elevations (ec * 24B).
    if (pos + 8 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint32_t rm_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 8 + (size_t)rm_cnt * 72;
    for (size_t r = 0; r < rm_cnt; r++) {
        pos += 36;
        uint32_t lc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)lc * 44;
        uint32_t tc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)tc * 24;
        uint32_t uc = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)uc * 44;
        uint32_t ec = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
        pos += 8 + (size_t)ec * 24;
    }

    // Section 8: Effects (Technologies / Upgrades).
    // Layout:
    //   - effect_count (uint32_t, 4 bytes)
    //   - for each effect: 31 bytes + command_count (uint16_t) + command_count * 11 bytes.
    size_t effects_start = pos;
    if (pos + 4 > orig_len) { free(sounds); free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint32_t ef_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 4;

    orig_effects = malloc(ef_cnt * sizeof(WlOrigEffect));
    if (!orig_effects) { free(sounds); free(orig); snprintf(err, errcap, "out of memory"); return -1; }

    for (size_t e = 0; e < ef_cnt; e++) {
        if (pos + 33 > orig_len) { free(orig_effects); free(sounds); free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
        orig_effects[e].offset = pos;
        pos += 31;
        uint16_t ec = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2;
        orig_effects[e].cmd_count = ec;
        orig_effects[e].cmds_offset = pos;
        size_t cmds_sz = (size_t)ec * 11;
        if (pos + cmds_sz > orig_len) { free(orig_effects); free(sounds); free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
        pos += cmds_sz;
    }
    size_t orig_effects_end = pos;

    // Section 9: Unit Headers.
    // Layout:
    //   - header_count (uint32_t, 4 bytes)
    //   - for each header: exists flag (1 byte). If non-zero, command_count (uint16_t) + command_count * 59 bytes.
    if (pos + 4 > orig_len) { free(orig_effects); free(sounds); free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint32_t uh_cnt = (uint32_t)(orig[pos] | (orig[pos + 1] << 8) | (orig[pos + 2] << 16) | (orig[pos + 3] << 24));
    pos += 4;
    for (size_t u = 0; u < uh_cnt; u++) {
        uint8_t ex = orig[pos++];
        if (ex) {
            uint16_t cc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
            pos += 2 + (size_t)cc * 59;
        }
    }

    size_t civs_start = pos;

    // Section 10: Civilizations (MODIFIED SECTION).
    // Layout:
    //   - base_civ_count (uint16_t, 2 bytes)
    //   - for each civ: enabled (1B), name (20B), resource_count (2B), tech_tree_id (2B),
    //     team_bonus_id (2B), resources (rc * 4B), icon_set (1B), unit_count (2B),
    //     unit_pointers (uc * 4B), followed by polymorphic unit records.
    if (pos + 2 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t base_civ_count = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;

    size_t civs_cap = base_civ_count + NUM_RESERVED + 256;
    WlDatCiv *civs = calloc(civs_cap, sizeof(WlDatCiv));
    if (!civs) { free(orig); snprintf(err, errcap, "out of memory"); return -1; }
    size_t cur_civ_count = 0;

    for (size_t c = 0; c < base_civ_count; c++) {
        size_t c_start = pos;
        pos += 1 + 20; // enabled, name
        uint16_t rc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2 + 2 + 2 + (size_t)rc * 4 + 1; // rc, tech_tree, team_bonus, resources, icon_set
        uint16_t uc = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
        pos += 2;
        const uint8_t *uptrs = orig + pos;
        pos += (size_t)uc * 4;

        civs[c].unit_count = uc;
        civs[c].resource_count = rc;
        civs[c].unit_offsets = calloc(uc, sizeof(uint32_t));
        if (!civs[c].unit_offsets) { free(civs); free(orig); snprintf(err, errcap, "out of memory"); return -1; }

        for (size_t u = 0; u < uc; u++) {
            int32_t p = (int32_t)(uptrs[u * 4] | (uptrs[u * 4 + 1] << 8) | (uptrs[u * 4 + 2] << 16) | (uptrs[u * 4 + 3] << 24));
            if (p) {
                civs[c].unit_offsets[u] = (uint32_t)(pos - c_start);
                pos += wl_calc_unit_len(orig, pos);
            }
        }
        size_t c_len = pos - c_start;
        civs[c].size = c_len;
        civs[c].data = malloc(c_len);
        if (!civs[c].data) { free(civs); free(orig); snprintf(err, errcap, "out of memory"); return -1; }
        memcpy(civs[c].data, orig + c_start, c_len);
        cur_civ_count++;
    }

    // (civs end here; the researches section follows)

    // Section 11: Researches.
    // Layout:
    //   - research_count (uint16_t, 2 bytes)
    //   - for each research: 62-byte fixed header + name_length bytes of ASCII name.
    // Why parsed: Mod unique units append custom availability and elite researches.
    if (pos + 2 > orig_len) { free(orig); snprintf(err, errcap, "dat truncated"); return -1; }
    uint16_t orig_res_cnt = (uint16_t)(orig[pos] | (orig[pos + 1] << 8));
    pos += 2;
    size_t orig_res_bodies_start = pos;

    orig_researches = malloc(orig_res_cnt * sizeof(WlOrigResearch));
    if (!orig_researches) {
        for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
        free(civs);
        for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
        free(sounds);
        free(orig_effects);
        free(orig);
        snprintf(err, errcap, "out of memory");
        return -1;
    }

    for (size_t r = 0; r < orig_res_cnt; r++) {
        if (pos + 62 > orig_len) {
            free(orig_researches); orig_researches = NULL;
            for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
            free(civs);
            for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
            free(sounds);
            free(orig_effects);
            free(orig);
            snprintf(err, errcap, "dat truncated");
            return -1;
        }
        orig_researches[r].offset = pos;
        orig_researches[r].civ = (int16_t)(orig[pos + 29] | (orig[pos + 30] << 8));
        orig_researches[r].research_location = (int16_t)(orig[pos + 33] | (orig[pos + 34] << 8));
        orig_researches[r].effect_id = (int16_t)(orig[pos + 41] | (orig[pos + 42] << 8));
        orig_researches[r].name_length = (uint16_t)(orig[pos + 60] | (orig[pos + 61] << 8));
        pos += 62 + orig_researches[r].name_length;
        if (pos > orig_len) {
            free(orig_researches); orig_researches = NULL;
            for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
            free(civs);
            for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
            free(sounds);
            free(orig_effects);
            free(orig);
            snprintf(err, errcap, "dat truncated");
            return -1;
        }
    }
    size_t orig_res_bodies_end = pos;
    size_t orig_res_bodies_len = orig_res_bodies_end - orig_res_bodies_start;
    size_t tt_start = pos;
    size_t tt_len = orig_len - tt_start;

    // Tech tree section (after the researches). Copied untouched unless a custom castle
    // unique unit adds its records; then it is parsed and re-serialized (wl_tt_*).
    WlTtState tts;
    memset(&tts, 0, sizeof(tts));
    tts.src = orig + tt_start;
    tts.src_len = tt_len;

    // ----------------------------------------------------------------------
    // 3. Discover and apply mod civilizations
    // ----------------------------------------------------------------------
    WlCivFile *civ_files = NULL;
    size_t num_civ_files = 0;
    if (wl_find_civ_files(mods_dir, &civ_files, &num_civ_files, err, errcap) != 0) {
        free(orig_researches);
        for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
        free(civs);
        for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
        free(sounds);
        free(orig_effects);
        free(orig);
        return -1;
    }

    // Dynamic buffer for civs.txt: formatted as "%d\t%d\t%s\t%s\t%s[\t%d\t%d]\n"
    size_t lines_cap = 4096, lines_len = 0;
    char *civs_txt = malloc(lines_cap);
    if (!civs_txt) {
        snprintf(err, errcap, "out of memory");
        goto cleanup_err;
    }
    civs_txt[0] = '\0';

    if (num_civ_files > 0) {
        // Base dat must have exactly 32 civilizations (0 = Gaia, 1..31 = playable base civs)
        if (base_civ_count != FIRST_RESERVED_ID) {
            snprintf(err, errcap, "expected %d civs in the base dat, found %d", FIRST_RESERVED_ID, (int)base_civ_count);
            free(civs_txt);
            goto cleanup_err;
        }

        // RULE: Add 3 placeholder civilizations (WL_RESERVED_32, 33, 34)
        // In AoK HD.exe, IDs 32..34 are hard-wired in the exe (Random, Spectator, Mirror).
        // Custom civs start at ID 35. The placeholders are cloned from civ 1 (Britons).
        for (int k = 0; k < NUM_RESERVED; k++) {
            int cid = (int)cur_civ_count;
            WlDatCiv ph;
            char where_ph[64];
            snprintf(where_ph, sizeof(where_ph), "placeholder");
            if (wl_clone_civ(civs, cur_civ_count, 1, where_ph, &ph, err, errcap) != 0) {
                free(civs_txt);
                goto cleanup_err;
            }
            char ph_name[32];
            snprintf(ph_name, sizeof(ph_name), "WL_RESERVED_%d", FIRST_RESERVED_ID + k);
            wl_make_name(ph_name, where_ph, ph.data + 1, err, errcap);
            civs[cid] = ph;
            cur_civ_count++;
        }

        // Count uu_total first (needed for unique_building slot allocation)
        size_t uu_total = 0;
        for (size_t f = 0; f < num_civ_files; f++) {
            size_t jlen = 0;
            char *jbytes = wl_read_file(civ_files[f].path, &jlen);
            if (jbytes) {
                char tmp_err[2048];
                WlJsonVal *spec = wl_json_parse(jbytes, tmp_err, sizeof(tmp_err));
                free(jbytes);
                if (spec && spec->type == WL_JSON_OBJECT && wl_json_get(spec, "unique_unit")) {
                    uu_total++;
                }
                wl_json_free(spec);
            }
        }

        // Apply each custom civ JSON in sorted order
        for (size_t f = 0; f < num_civ_files; f++) {
            size_t jlen = 0;
            char *jbytes = wl_read_file(civ_files[f].path, &jlen);
            if (!jbytes) {
                snprintf(err, errcap, "%s: %s", civ_files[f].path, strerror(errno));
                free(civs_txt);
                goto cleanup_err;
            }
            WlJsonVal *spec = wl_json_parse(jbytes, err, errcap);
            free(jbytes);
            char where[1024];
            snprintf(where, sizeof(where), "%.256s/civs/%.256s", civ_files[f].mod, civ_files[f].fn);
            if (!spec) {
                char tmp_err[2048];
                snprintf(tmp_err, sizeof(tmp_err), "%.1000s: %.1000s", civ_files[f].path, err);
                strncpy(err, tmp_err, errcap - 1);
                err[errcap - 1] = '\0';
                free(civs_txt);
                goto cleanup_err;
            }

            int new_cid = -1;
            if (wl_apply_civ_json(civs, &cur_civ_count, civs_cap,
                                  orig, graphics_section_offset, g_cnt,
                                  orig_effects, ef_cnt,
                                  &custom_effects, &custom_effects_cnt, &custom_effects_cap,
                                  orig_researches, orig_res_cnt,
                                  &custom_researches, &custom_researches_cnt, &custom_researches_cap,
                                  &custom_graphics, &custom_graphics_cnt, &custom_graphics_cap,
                                  &uu_k, &ub_k, uu_total, &tts,
                                  &ext,
                                  spec, where, &new_cid, err, errcap) != 0) {
                wl_json_free(spec);
                free(civs_txt);
                goto cleanup_err;
            }

            const WlJsonVal *name_val = wl_json_get(spec, "name");
            const char *display_name = (name_val && name_val->type == WL_JSON_STRING) ? name_val->u.str : "";
            const WlJsonVal *base_val = wl_json_get(spec, "base");
            int base_id = (int)base_val->u.num.ival;
            const WlJsonVal *ut_val = wl_json_get(spec, "unique_techs");

            // Append civ entry to civs.txt: "<id>\t<base>\t<display_name>\t<mod>\t<filename>[\t<t1>\t<t2>]\n"
            char line[2048];
            int lsz = 0;
            if (ut_val && ut_val->type == WL_JSON_ARRAY && ut_val->u.arr.count == 2) {
                int t1 = (int)ut_val->u.arr.items[0]->u.num.ival;
                int t2 = (int)ut_val->u.arr.items[1]->u.num.ival;
                lsz = snprintf(line, sizeof(line), "%d\t%d\t%.500s\t%.256s\t%.256s\t%d\t%d\n",
                               new_cid, base_id, display_name, civ_files[f].mod, civ_files[f].fn, t1, t2);
            } else {
                lsz = snprintf(line, sizeof(line), "%d\t%d\t%.500s\t%.256s\t%.256s\n",
                               new_cid, base_id, display_name, civ_files[f].mod, civ_files[f].fn);
            }
            if (lines_len + (size_t)lsz + 1 > lines_cap) {
                lines_cap = (lines_len + (size_t)lsz + 1) * 2;
                char *new_txt = realloc(civs_txt, lines_cap);
                if (!new_txt) {
                    wl_json_free(spec);
                    free(civs_txt);
                    snprintf(err, errcap, "out of memory");
                    goto cleanup_err;
                }
                civs_txt = new_txt;
            }
            memcpy(civs_txt + lines_len, line, (size_t)lsz + 1);
            lines_len += (size_t)lsz;

            // RULE (Sound copying): duplicate base civ's sound items for the new civ
            wl_copy_civ_sounds(sounds, snd_cnt, base_id, new_cid);
            wl_json_free(spec);
        }
    }

    // Tech tree bytes for the output: re-serialized only when it was parsed
    // (a custom castle unique unit was added); otherwise the original bytes.
    const uint8_t *tt_bytes = orig + tt_start;
    size_t tt_bytes_len = tt_len;
    if (tts.parsed) {
        if (wl_tt_serialize(&tts, err, errcap) != 0) goto cleanup_err;
        tt_bytes = tts.out;
        tt_bytes_len = tts.out_len;
    }

    // ----------------------------------------------------------------------
    // 4. Assemble new decompressed dat buffer
    // ----------------------------------------------------------------------
    size_t head_len = sounds_start;
    size_t s6_7_len = effects_start - graphics_end;
    size_t orig_ef_bodies_len = orig_effects_end - (effects_start + 4);
    size_t custom_ef_total_len = 0;
    for (size_t i = 0; i < custom_effects_cnt; i++) {
        custom_ef_total_len += 31 + 2 + (size_t)custom_effects[i].cmd_count * 11;
    }
    size_t custom_gfx_len = 0;   // pointer + 78-byte record + deltas/sounds each
    for (size_t i = 0; i < custom_graphics_cnt; i++) custom_gfx_len += 4 + 78 + custom_graphics[i].extra_len;
    size_t s9_len = civs_start - orig_effects_end;
    // Section 11: Researches = original count+bodies, then the unique unit
    // researches appended by wl_apply_civ_json (62-byte header + name each).
    // Everything after the researches is the tech tree (tt_bytes: original, or re-serialized).
    size_t custom_res_total_len = 0;
    for (size_t i = 0; i < custom_researches_cnt; i++)
        custom_res_total_len += 62 + (size_t)custom_researches[i].name_length;
    size_t tail_len = 2 + orig_res_bodies_len + custom_res_total_len + tt_bytes_len;

    size_t snd_total_len = 2;
    for (size_t s = 0; s < snd_cnt; s++) {
        snd_total_len += 10 + (size_t)sounds[s].file_count * 23;
    }

    size_t civ_total_len = 2;
    for (size_t c = 0; c < cur_civ_count; c++) {
        civ_total_len += civs[c].size;
    }

    size_t orig_s5_len = graphics_end - graphics_section_offset;
    size_t new_decomp_len = head_len + snd_total_len + orig_s5_len + custom_gfx_len + s6_7_len + (4 + orig_ef_bodies_len + custom_ef_total_len) + s9_len + civ_total_len + tail_len;
    unsigned char *new_decomp = malloc(new_decomp_len);
    if (!new_decomp) {
        snprintf(err, errcap, "out of memory allocating decompressed dat buffer");
        free(civs_txt);
        goto cleanup_err;
    }

    size_t dpos = 0;
    // 1. Head (Sections 1..3: Version, Terrain Restrictions, Player Colours)
    memcpy(new_decomp + dpos, orig, head_len);
    dpos += head_len;

    // 2. Sounds (Section 4, with duplicated SoundItem entries for custom civs)
    new_decomp[dpos++] = (uint8_t)(snd_cnt & 0xFF);
    new_decomp[dpos++] = (uint8_t)((snd_cnt >> 8) & 0xFF);
    for (size_t s = 0; s < snd_cnt; s++) {
        new_decomp[dpos++] = (uint8_t)(sounds[s].id & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].id >> 8) & 0xFF);
        new_decomp[dpos++] = (uint8_t)(sounds[s].play_delay & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].play_delay >> 8) & 0xFF);
        new_decomp[dpos++] = (uint8_t)(sounds[s].file_count & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].file_count >> 8) & 0xFF);
        new_decomp[dpos++] = (uint8_t)(sounds[s].cache_time & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].cache_time >> 8) & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].cache_time >> 16) & 0xFF);
        new_decomp[dpos++] = (uint8_t)((sounds[s].cache_time >> 24) & 0xFF);
        size_t isz = (size_t)sounds[s].file_count * 23;
        if (isz > 0) {
            memcpy(new_decomp + dpos, sounds[s].items, isz);
            dpos += isz;
        }
    }

    // 3. Section 5: Graphics (original + custom graphics appended)
    uint16_t new_g_cnt = g_cnt + (uint16_t)custom_graphics_cnt;
    new_decomp[dpos++] = (uint8_t)(new_g_cnt & 0xFF);
    new_decomp[dpos++] = (uint8_t)((new_g_cnt >> 8) & 0xFF);

    // Copy original pointers
    memcpy(new_decomp + dpos, orig + graphics_section_offset + 2, (size_t)g_cnt * 4);
    dpos += (size_t)g_cnt * 4;

    // Append custom graphic pointers (use source_ptr from each custom graphic)
    for (size_t i = 0; i < custom_graphics_cnt; i++) {
        int32_t ptr = custom_graphics[i].source_ptr;
        new_decomp[dpos++] = (uint8_t)(ptr & 0xFF);
        new_decomp[dpos++] = (uint8_t)((ptr >> 8) & 0xFF);
        new_decomp[dpos++] = (uint8_t)((ptr >> 16) & 0xFF);
        new_decomp[dpos++] = (uint8_t)((ptr >> 24) & 0xFF);
    }

    // Copy original graphic records
    size_t orig_gfx_records_start = graphics_section_offset + 2 + (size_t)g_cnt * 4;
    size_t orig_gfx_records_len = graphics_end - orig_gfx_records_start;
    memcpy(new_decomp + dpos, orig + orig_gfx_records_start, orig_gfx_records_len);
    dpos += orig_gfx_records_len;

    // Append custom graphic records (78-byte header, then its deltas and attack sounds)
    for (size_t i = 0; i < custom_graphics_cnt; i++) {
        memcpy(new_decomp + dpos, custom_graphics[i].data, 78);
        dpos += 78;
        if (custom_graphics[i].extra_len) {
            memcpy(new_decomp + dpos, custom_graphics[i].extra, custom_graphics[i].extra_len);
            dpos += custom_graphics[i].extra_len;
        }
    }

    // 4. Sections 6..7 (Terrain Block, Random Maps)
    memcpy(new_decomp + dpos, orig + graphics_end, s6_7_len);
    dpos += s6_7_len;

    // 4. Section 8: Effects (Originals + Custom Effects appended)
    uint32_t total_ef_cnt = ef_cnt + (uint32_t)custom_effects_cnt;
    new_decomp[dpos++] = (uint8_t)(total_ef_cnt & 0xFF);
    new_decomp[dpos++] = (uint8_t)((total_ef_cnt >> 8) & 0xFF);
    new_decomp[dpos++] = (uint8_t)((total_ef_cnt >> 16) & 0xFF);
    new_decomp[dpos++] = (uint8_t)((total_ef_cnt >> 24) & 0xFF);
    if (orig_ef_bodies_len > 0) {
        memcpy(new_decomp + dpos, orig + effects_start + 4, orig_ef_bodies_len);
        dpos += orig_ef_bodies_len;
    }
    for (size_t i = 0; i < custom_effects_cnt; i++) {
        memcpy(new_decomp + dpos, custom_effects[i].name, 31);
        dpos += 31;
        uint16_t cc = custom_effects[i].cmd_count;
        new_decomp[dpos++] = (uint8_t)(cc & 0xFF);
        new_decomp[dpos++] = (uint8_t)((cc >> 8) & 0xFF);
        if (cc > 0) {
            memcpy(new_decomp + dpos, custom_effects[i].commands, (size_t)cc * 11);
            dpos += (size_t)cc * 11;
        }
    }

    // 5. Section 9: Unit Headers
    memcpy(new_decomp + dpos, orig + orig_effects_end, s9_len);
    dpos += s9_len;

    // 6. Civilizations (Section 10: Base civs + Placeholders 32..34 + Custom Civs)
    new_decomp[dpos++] = (uint8_t)(cur_civ_count & 0xFF);
    new_decomp[dpos++] = (uint8_t)((cur_civ_count >> 8) & 0xFF);
    for (size_t c = 0; c < cur_civ_count; c++) {
        memcpy(new_decomp + dpos, civs[c].data, civs[c].size);
        dpos += civs[c].size;
    }

    // 7. Section 11: Researches (originals + unique unit researches)
    uint16_t total_res_cnt = (uint16_t)(orig_res_cnt + custom_researches_cnt);
    new_decomp[dpos++] = (uint8_t)(total_res_cnt & 0xFF);
    new_decomp[dpos++] = (uint8_t)((total_res_cnt >> 8) & 0xFF);
    memcpy(new_decomp + dpos, orig + orig_res_bodies_start, orig_res_bodies_len);
    dpos += orig_res_bodies_len;
    for (size_t i = 0; i < custom_researches_cnt; i++) {
        memcpy(new_decomp + dpos, custom_researches[i].header, 62);  // header[60..61] = name_length
        new_decomp[dpos + 60] = (uint8_t)(custom_researches[i].name_length & 0xFF);
        new_decomp[dpos + 61] = (uint8_t)((custom_researches[i].name_length >> 8) & 0xFF);
        dpos += 62;
        memcpy(new_decomp + dpos, custom_researches[i].name, custom_researches[i].name_length);
        dpos += custom_researches[i].name_length;
    }

    // 8. Tail: tech tree (original bytes, or re-serialized when unique unit records were added)
    memcpy(new_decomp + dpos, tt_bytes, tt_bytes_len);
    dpos += tt_bytes_len;

    // ----------------------------------------------------------------------
    // 5. Deflate new dat buffer
    // ----------------------------------------------------------------------
    unsigned char *deflated = NULL;
    size_t deflated_len = 0;
    if (z->deflate_raw(new_decomp, new_decomp_len, &deflated, &deflated_len) != 0) {
        free(new_decomp);
        free(civs_txt);
        snprintf(err, errcap, "failed to compress output dat");
        goto cleanup_err;
    }
    free(new_decomp);

    // ----------------------------------------------------------------------
    // 6. Write output files atomically
    // ----------------------------------------------------------------------
    if (wl_atomic_write_file(out_dat, deflated, deflated_len, err, errcap) != 0) {
        free(deflated);
        free(civs_txt);
        goto cleanup_err;
    }
    free(deflated);

    if (wl_atomic_write_file(out_civs_txt, civs_txt, lines_len, err, errcap) != 0) {
        free(civs_txt);
        goto cleanup_err;
    }
    free(civs_txt);

    // Write strings.txt
    size_t strings_cap = 1024;
    size_t strings_len = 0;
    char *strings_buf = malloc(strings_cap);
    if (!strings_buf) {
        snprintf(err, errcap, "out of memory");
        goto cleanup_err;
    }
    strings_buf[0] = '\0';
    for (size_t i = 0; i < ext.strings_count; i++) {
        const char *t = ext.strings[i].text;
        size_t need = strings_len + 24 + 2 * strlen(t);
        if (need > strings_cap) {
            char *nb = realloc(strings_buf, need * 2);
            if (!nb) {
                free(strings_buf);
                snprintf(err, errcap, "out of memory");
                goto cleanup_err;
            }
            strings_buf = nb;
            strings_cap = need * 2;
        }
        strings_len += sprintf(strings_buf + strings_len, "%d\t", ext.strings[i].id);
        for (; *t; t++) {
            if (*t == '\n') { strings_buf[strings_len++] = '\\'; strings_buf[strings_len++] = 'n'; }
            else strings_buf[strings_len++] = *t == '\t' ? ' ' : *t;
        }
        strings_buf[strings_len++] = '\n';
    }
    if (wl_atomic_write_file(out_strings_txt, strings_buf, strings_len, err, errcap) != 0) {
        free(strings_buf);
        goto cleanup_err;
    }
    free(strings_buf);

    // Write commands.txt
    size_t commands_cap = 1024;
    size_t commands_len = 0;
    char *commands_buf = malloc(commands_cap);
    if (!commands_buf) {
        snprintf(err, errcap, "out of memory");
        goto cleanup_err;
    }
    commands_buf[0] = '\0';
    for (size_t i = 0; i < ext.lua_count; i++) {
        size_t line_len = strlen(ext.lua[i]);
        size_t needed = commands_len + line_len + 2;
        if (needed > commands_cap) {
            size_t new_cap = needed * 2;
            char *new_buf = realloc(commands_buf, new_cap);
            if (!new_buf) {
                free(commands_buf);
                snprintf(err, errcap, "out of memory");
                goto cleanup_err;
            }
            commands_buf = new_buf;
            commands_cap = new_cap;
        }
        memcpy(commands_buf + commands_len, ext.lua[i], line_len);
        commands_len += line_len;
        commands_buf[commands_len++] = '\n';
    }
    if (wl_atomic_write_file(out_commands_txt, commands_buf, commands_len, err, errcap) != 0) {
        free(commands_buf);
        goto cleanup_err;
    }
    free(commands_buf);

    // Cleanup resources
    wl_ext_free(&ext);
    for (size_t i = 0; i < custom_researches_cnt; i++) free(custom_researches[i].name);
    free(custom_researches);
    free(orig_researches);
    for (size_t i = 0; i < custom_effects_cnt; i++) free(custom_effects[i].commands);
    free(custom_effects);
    free(orig_effects);
    for (size_t i = 0; i < custom_graphics_cnt; i++) free(custom_graphics[i].extra);
    free(custom_graphics);
    for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
    free(civs);
    for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
    free(sounds);
    free(civ_files);
    wl_tt_free(&tts);
    free(orig);

    return (int)num_civ_files;

cleanup_err:
    wl_ext_free(&ext);
    for (size_t i = 0; i < custom_researches_cnt; i++) free(custom_researches[i].name);
    free(custom_researches);
    free(orig_researches);
    for (size_t i = 0; i < custom_effects_cnt; i++) free(custom_effects[i].commands);
    free(custom_effects);
    free(orig_effects);
    for (size_t i = 0; i < custom_graphics_cnt; i++) free(custom_graphics[i].extra);
    free(custom_graphics);
    for (size_t i = 0; i < cur_civ_count; i++) wl_free_civ(&civs[i]);
    free(civs);
    for (size_t s = 0; s < snd_cnt; s++) free(sounds[s].items);
    free(sounds);
    free(civ_files);
    wl_tt_free(&tts);
    free(orig);
    return -1;
}
