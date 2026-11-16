// ==========================================================================
// Scriptorium - Mod and Civ IO & AST Model
//
// Purpose:
//   Loads and saves Wololoader mods and civilizations. Preserves unknown and
//   advanced JSON keys (such as "tech_tree", "team_bonus", "resources") and
//   original key ordering. Implements pretty-printed JSON writing, atomic
//   file saving (temp file + rename), and mod validation using datbuild.c.
// ==========================================================================

#ifndef SC_MODIO_H
#define SC_MODIO_H

#include <stddef.h>
#include <windows.h>
#include "../src/json.h"
#include "../src/datbuild.h"
#include "catalog.h"

// Effect structure matching { "type": ..., "a": ..., "b": ..., "c": ..., "d": ... }
typedef struct {
    int    type;
    int    a;
    int    b;
    int    c;
    double d;
} ScEffect;

// Unit overrides matching civs.json "units": { "<id>": { "stat": val, ... } }
typedef struct {
    int   unit_id;
    float value[SC_STAT_COUNT];
    int   has[SC_STAT_COUNT]; // 1 if overridden for this unit
} ScUnitOverride;

// In-memory civilization definition
typedef struct {
    char        file_name[MAX_PATH];
    char        file_path[MAX_PATH];
    WlJsonVal  *root_json;
    int         dirty;

    char        name[128];
    char        dat_name[32];
    int         base;
    char       *description;
    char      **ai_names;
    size_t      ai_name_count;
    char        sound[MAX_PATH];
    char        interface_slp[MAX_PATH];

    int         unique_techs[2]; // [0] = castle, [1] = imperial (-1 if none)
    int        *enabled_techs;
    size_t      enabled_tech_count;
    int        *disabled_techs;
    size_t      disabled_tech_count;
    int        *enabled_units;
    size_t      enabled_unit_count;
    int        *disabled_units;
    size_t      disabled_unit_count;

    ScEffect   *effects;
    size_t      effect_count;
    ScEffect   *team_bonus_effects;
    size_t      team_bonus_effect_count;

    ScUnitOverride *unit_overrides;
    size_t          unit_override_count;
} ScCivData;

// In-memory mod definition
typedef struct {
    char        folder_name[MAX_PATH];
    char        dir_path[MAX_PATH];
    WlJsonVal  *root_json;
    int         dirty;

    char        name[128];
    char        version[64];
    char        author[128];
    char       *description;
    int         has_enabled;
    int         enabled;

    ScCivData  *civs;
    size_t      civ_count;
} ScModData;

// Scans <game_dir>/wololoader/mods/ and loads all detected mods
int modio_scan_mods(const char *game_dir, ScModData **out_mods, size_t *out_count, char *err, size_t errcap);

// Loads a single mod from <mod_dir>
int modio_load_mod(const char *mod_dir, ScModData *out, char *err, size_t errcap);

// Saves a mod and all its civs to dest_dir (dest_dir can be mod->dir_path or a temp folder)
int modio_save_mod(ScModData *mod, const char *dest_dir, char *err, size_t errcap);

// Creates a new mod folder and minimal mod.json + default civ
int modio_create_mod(const char *game_dir, const char *name, char *out_folder, size_t folder_cap, char *err, size_t errcap);

// Adds a new civ with defaults to the specified mod
int modio_create_civ(ScModData *mod, const char *civ_name, char *err, size_t errcap);

// Deletes a civ from the specified mod (deletes file and removes from memory)
int modio_delete_civ(ScModData *mod, size_t civ_idx, char *err, size_t errcap);

// Syncs in-memory C fields to the underlying root_json AST
void modio_sync_civ_to_json(ScCivData *civ);
void modio_sync_mod_to_json(ScModData *mod);

// Pretty-prints a WlJsonVal AST into a UTF-8 string (2-space indent)
char *modio_json_write_pretty(const WlJsonVal *val, size_t *out_len);

// Compares two JSON ASTs for semantic equality
int modio_json_equal(const WlJsonVal *a, const WlJsonVal *b);

// Writes data to path atomically (via temp file and replace)
int modio_atomic_write_file(const char *path, const char *data, size_t len, char *err, size_t errcap);

// Validates a mod using wl_dat_build against the game's base dat
int modio_validate_mod(const ScModData *mod, const char *game_dir, const WlZlib *z, char *err, size_t errcap);

// Free resources
void modio_free_civ(ScCivData *civ);
void modio_free_mod(ScModData *mod);
void modio_free_mods(ScModData *mods, size_t count);

#endif // SC_MODIO_H
