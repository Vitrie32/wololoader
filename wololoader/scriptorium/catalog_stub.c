// ==========================================================================
// Scriptorium - Catalog Stub
//
// Purpose:
//   A lightweight fallback implementation of catalog.h for Job B testing.
//   Used when catalog.c is not yet available, providing:
//     - 3 civs: Britons (1), Franks (2), Goths (3)
//     - Core units: Archer (4), Villager (83), Monk (125)
//     - Core techs: Yeomen (3), Cavalier (209), Redemption (316),
//                   Bloodlines (435), Warwolf (461)
//     - Unit stat lookup and UI label definitions
// ==========================================================================

#include "catalog.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

// Static catalog definition tables
static const ScCiv s_stub_civs[] = {
    { 1, "Britons" },
    { 2, "Franks" },
    { 3, "Goths" }
};

static const ScUnit s_stub_units[] = {
    { 4,   70,  0, "ARCHR", "Archer" },
    { 83,  70,  4, "VILLM", "Villager" },
    { 125, 70, 18, "MONKX", "Monk" }
};

static const ScTech s_stub_techs[] = {
    { 3,   "TECH_YEOMEN",     "Yeomen" },
    { 209, "TECH_CAVALIER",   "Cavalier" },
    { 316, "TECH_REDEMPTION", "Redemption" },
    { 435, "TECH_BLOODLINES", "Bloodlines" },
    { 461, "TECH_WARWOLF",    "Warwolf" }
};

int sc_catalog_load(const WlZlib *z, const char *game_dir, ScCatalog *out, char *err, size_t errcap) {
    (void)z;
    (void)game_dir;
    (void)err;
    (void)errcap;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    out->civ_count = sizeof(s_stub_civs) / sizeof(s_stub_civs[0]);
    out->civs = malloc(sizeof(s_stub_civs));
    if (!out->civs) return -1;
    memcpy(out->civs, s_stub_civs, sizeof(s_stub_civs));

    out->unit_count = sizeof(s_stub_units) / sizeof(s_stub_units[0]);
    out->units = malloc(sizeof(s_stub_units));
    if (!out->units) {
        free(out->civs);
        return -1;
    }
    memcpy(out->units, s_stub_units, sizeof(s_stub_units));

    out->tech_count = sizeof(s_stub_techs) / sizeof(s_stub_techs[0]);
    out->techs = malloc(sizeof(s_stub_techs));
    if (!out->techs) {
        free(out->civs);
        free(out->units);
        return -1;
    }
    memcpy(out->techs, s_stub_techs, sizeof(s_stub_techs));

    out->priv = NULL;
    return 0;
}

int sc_catalog_unit_stats(const ScCatalog *c, int civ_id, int unit_id, ScUnitStats *out) {
    (void)c;
    (void)civ_id;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    if (unit_id == 4) {
        // Archer
        out->has[SC_HIT_POINTS]    = 1; out->value[SC_HIT_POINTS]    = 30.0f;
        out->has[SC_LINE_OF_SIGHT] = 1; out->value[SC_LINE_OF_SIGHT] = 6.0f;
        out->has[SC_SPEED]         = 1; out->value[SC_SPEED]         = 0.96f;
        out->has[SC_SEARCH_RADIUS] = 1; out->value[SC_SEARCH_RADIUS] = 6.0f;
        out->has[SC_WORK_RATE]     = 1; out->value[SC_WORK_RATE]     = 1.0f;
        out->has[SC_MAX_RANGE]     = 1; out->value[SC_MAX_RANGE]     = 4.0f;
        out->has[SC_RELOAD_TIME]   = 1; out->value[SC_RELOAD_TIME]   = 2.0f;
        out->has[SC_TRAIN_TIME]    = 1; out->value[SC_TRAIN_TIME]    = 35.0f;
        return 0;
    } else if (unit_id == 83) {
        // Villager
        out->has[SC_HIT_POINTS]    = 1; out->value[SC_HIT_POINTS]    = 25.0f;
        out->has[SC_LINE_OF_SIGHT] = 1; out->value[SC_LINE_OF_SIGHT] = 4.0f;
        out->has[SC_SPEED]         = 1; out->value[SC_SPEED]         = 0.8f;
        out->has[SC_SEARCH_RADIUS] = 1; out->value[SC_SEARCH_RADIUS] = 4.0f;
        out->has[SC_WORK_RATE]     = 1; out->value[SC_WORK_RATE]     = 1.0f;
        out->has[SC_MAX_RANGE]     = 0; out->value[SC_MAX_RANGE]     = 0.0f;
        out->has[SC_RELOAD_TIME]   = 0; out->value[SC_RELOAD_TIME]   = 0.0f;
        out->has[SC_TRAIN_TIME]    = 1; out->value[SC_TRAIN_TIME]    = 25.0f;
        return 0;
    } else if (unit_id == 125) {
        // Monk
        out->has[SC_HIT_POINTS]    = 1; out->value[SC_HIT_POINTS]    = 30.0f;
        out->has[SC_LINE_OF_SIGHT] = 1; out->value[SC_LINE_OF_SIGHT] = 11.0f;
        out->has[SC_SPEED]         = 1; out->value[SC_SPEED]         = 0.7f;
        out->has[SC_SEARCH_RADIUS] = 1; out->value[SC_SEARCH_RADIUS] = 11.0f;
        out->has[SC_WORK_RATE]     = 1; out->value[SC_WORK_RATE]     = 1.0f;
        out->has[SC_MAX_RANGE]     = 1; out->value[SC_MAX_RANGE]     = 9.0f;
        out->has[SC_RELOAD_TIME]   = 1; out->value[SC_RELOAD_TIME]   = 5.5f;
        out->has[SC_TRAIN_TIME]    = 1; out->value[SC_TRAIN_TIME]    = 51.0f;
        return 0;
    }

    // Default fallback stats for any other unit
    out->has[SC_HIT_POINTS]    = 1; out->value[SC_HIT_POINTS]    = 50.0f;
    out->has[SC_LINE_OF_SIGHT] = 1; out->value[SC_LINE_OF_SIGHT] = 5.0f;
    out->has[SC_SPEED]         = 1; out->value[SC_SPEED]         = 1.0f;
    out->has[SC_SEARCH_RADIUS] = 1; out->value[SC_SEARCH_RADIUS] = 5.0f;
    out->has[SC_WORK_RATE]     = 1; out->value[SC_WORK_RATE]     = 1.0f;
    out->has[SC_MAX_RANGE]     = 0; out->value[SC_MAX_RANGE]     = 0.0f;
    out->has[SC_RELOAD_TIME]   = 0; out->value[SC_RELOAD_TIME]   = 0.0f;
    out->has[SC_TRAIN_TIME]    = 1; out->value[SC_TRAIN_TIME]    = 30.0f;
    return 0;
}

const ScUnit *sc_catalog_unit(const ScCatalog *c, int unit_id) {
    if (!c) return NULL;
    for (size_t i = 0; i < c->unit_count; i++) {
        if (c->units[i].id == unit_id) {
            return &c->units[i];
        }
    }
    return NULL;
}

const ScTech *sc_catalog_tech(const ScCatalog *c, int tech_id) {
    if (!c) return NULL;
    for (size_t i = 0; i < c->tech_count; i++) {
        if (c->techs[i].id == tech_id) {
            return &c->techs[i];
        }
    }
    return NULL;
}

const char *sc_catalog_civ_name(const ScCatalog *c, int civ_id) {
    if (!c) return NULL;
    for (size_t i = 0; i < c->civ_count; i++) {
        if (c->civs[i].id == civ_id) {
            return c->civs[i].name;
        }
    }
    return NULL;
}

void sc_catalog_free(ScCatalog *c) {
    if (!c) return;
    free(c->civs);
    free(c->units);
    free(c->techs);
    memset(c, 0, sizeof(*c));
}
