// Scriptorium game data catalog: what the editor needs to know about the
// player's own game (civ names, units with their stats, techs with names).
// Read at runtime from the player's game folder; nothing from the game is
// shipped with Scriptorium.
//
// Sources:
//   <game>\resources\_common\dat\empires2_x2_p1.dat  (raw deflate, see AGENTS.md)
//   <game>\resources\en\strings\key-value\key-value-strings-utf8.txt
//   <game>\resources\en\strings\key-value\key-value-modded-strings-utf8.txt
//     line format:  <id> "<text>"   (lines starting with // are comments)
//
// This header is the CONTRACT between catalog.c (data) and the GUI.
// Do not change it without updating both sides.
#ifndef SC_CATALOG_H
#define SC_CATALOG_H

#include <stddef.h>
#include "../src/datbuild.h"   // WlZlib

// Unit stats the editor shows / edits. Names are exactly the field names the
// builders accept in a civ json "units" object (see datbuild.c field table).
// Index into ScUnitStats.value / .has with these enums.
typedef enum {
    SC_HIT_POINTS,      // "hit_points"     (all units)
    SC_LINE_OF_SIGHT,   // "line_of_sight"  (all units)
    SC_SPEED,           // "speed"          (type >= 20)
    SC_SEARCH_RADIUS,   // "search_radius"  (type >= 40)
    SC_WORK_RATE,       // "work_rate"      (type >= 40)
    SC_MAX_RANGE,       // "max_range"      (type >= 50)
    SC_RELOAD_TIME,     // "reload_time"    (type >= 50)
    SC_TRAIN_TIME,      // "train_time"     (type >= 70)
    SC_STAT_COUNT
} ScStat;

extern const char *const SC_STAT_FIELD[SC_STAT_COUNT];  // json field names, same order
extern const char *const SC_STAT_LABEL[SC_STAT_COUNT];  // UI labels ("Hit points", ...)

typedef struct {
    float value[SC_STAT_COUNT];
    int has[SC_STAT_COUNT];     // 1 when this unit type has the field (same class masks as datbuild.c field table)
} ScUnitStats;

typedef struct {
    int id;                     // civ id 1..31 (0 = Gaia is not listed)
    char name[64];              // display name (UTF-8), from the strings file
} ScCiv;

typedef struct {
    int id;                     // unit id
    int type;                   // Genie unit type (10, 20, 30, 40, 50, 60, 70, 80)
    int class_;                 // unit class (e.g. 18 = monk class), -1 if unknown
    char internal[64];          // internal dat name (ASCII)
    char name[128];             // display name (UTF-8) or "" when it has none
} ScUnit;

typedef struct {
    int id;                     // tech (research) id
    char internal[64];          // internal dat name
    char name[128];             // display name (UTF-8) or ""
} ScTech;

typedef struct {
    ScCiv  *civs;   size_t civ_count;
    ScUnit *units;  size_t unit_count;   // units of civ 0 that exist (non-empty slots), sorted by id
    ScTech *techs;  size_t tech_count;   // sorted by id
    void   *priv;                        // implementation data (per-civ unit data for stats)
} ScCatalog;

// Load everything from `game_dir` (folder holding "AoK HD.exe").
// Returns 0 on success, -1 with a message in err.
int sc_catalog_load(const WlZlib *z, const char *game_dir, ScCatalog *out, char *err, size_t errcap);

// Stats of `unit_id` as civ `civ_id` has them in the original dat
// (civ 1..31; so "Britons archer" can differ from "Franks archer").
// Returns 0 on success, -1 if civ or unit does not exist.
int sc_catalog_unit_stats(const ScCatalog *c, int civ_id, int unit_id, ScUnitStats *out);

// Lookups (NULL when missing).
const ScUnit *sc_catalog_unit(const ScCatalog *c, int unit_id);
const ScTech *sc_catalog_tech(const ScCatalog *c, int tech_id);
const char   *sc_catalog_civ_name(const ScCatalog *c, int civ_id);

void sc_catalog_free(ScCatalog *c);

#endif
