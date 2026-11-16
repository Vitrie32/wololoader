// ==========================================================================
// Native Linux Test for Scriptorium Catalog (test_catalog.c)
//
// Reads the real game data directory (READ-ONLY) using system zlib:
//   - Civilizations count and list
//   - Units count and first 30 units
//   - Techs count and first 30 techs
//   - Stats of units 4 (Archer), 125 (Monk), 83 (Villager) for civs 1 and 2
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../scriptorium/catalog.h"
#include "../src/datbuild.h"

// Raw deflate (-15 wbits) using system zlib
static int test_deflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return -1;
    }

    strm.next_in = (Bytef *)in;
    strm.avail_in = (uInt)in_len;

    uLong bound = deflateBound(&strm, (uLong)in_len);
    unsigned char *buf = malloc((size_t)bound);
    if (!buf) {
        deflateEnd(&strm);
        return -1;
    }

    strm.next_out = buf;
    strm.avail_out = (uInt)bound;

    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        free(buf);
        deflateEnd(&strm);
        return -1;
    }

    *out = buf;
    *out_len = strm.total_out;
    deflateEnd(&strm);
    return 0;
}

// Raw inflate (-15 wbits) using system zlib
static int test_inflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -15) != Z_OK) {
        return -1;
    }

    strm.next_in = (Bytef *)in;
    strm.avail_in = (uInt)in_len;

    size_t cap = in_len * 4 > (1024 * 1024) ? in_len * 4 : (1024 * 1024);
    unsigned char *buf = malloc(cap);
    if (!buf) {
        inflateEnd(&strm);
        return -1;
    }

    strm.next_out = buf;
    strm.avail_out = (uInt)cap;

    int ret;
    while ((ret = inflate(&strm, Z_NO_FLUSH)) != Z_STREAM_END) {
        if (ret != Z_OK) {
            free(buf);
            inflateEnd(&strm);
            return -1;
        }
        size_t used = strm.total_out;
        cap *= 2;
        unsigned char *new_buf = realloc(buf, cap);
        if (!new_buf) {
            free(buf);
            inflateEnd(&strm);
            return -1;
        }
        buf = new_buf;
        strm.next_out = buf + used;
        strm.avail_out = (uInt)(cap - used);
    }

    *out = buf;
    *out_len = strm.total_out;
    inflateEnd(&strm);
    return 0;
}

static void print_unit_stats(const ScCatalog *cat, int civ_id, int unit_id) {
    const char *civ_name = sc_catalog_civ_name(cat, civ_id);
    const ScUnit *u = sc_catalog_unit(cat, unit_id);
    const char *uname = (u && u->name[0]) ? u->name : (u ? u->internal : "Unknown");
    const char *uintname = u ? u->internal : "?";

    ScUnitStats stats;
    int res = sc_catalog_unit_stats(cat, civ_id, unit_id, &stats);
    if (res != 0) {
        printf("  Unit %d (%s / \"%s\"): <failed to read stats for civ %d>\n",
               unit_id, uintname, uname, civ_id);
        return;
    }

    printf("  Unit %d (%s / \"%s\") [Civ %d %s]:\n",
           unit_id, uintname, uname, civ_id, civ_name ? civ_name : "?");
    for (int i = 0; i < SC_STAT_COUNT; i++) {
        if (stats.has[i]) {
            printf("    %-14s (%s): %g\n", SC_STAT_LABEL[i], SC_STAT_FIELD[i], stats.value[i]);
        } else {
            printf("    %-14s (%s): <none>\n", SC_STAT_LABEL[i], SC_STAT_FIELD[i]);
        }
    }
}

int main(int argc, char **argv) {
    const char *game_dir = getenv("WL_GAME") ? getenv("WL_GAME") : "Age2HD";
    if (argc > 1) {
        game_dir = argv[1];
    }

    printf("Loading Scriptorium catalog from: %s\n", game_dir);

    WlZlib z;
    z.inflate_raw = test_inflate_raw;
    z.deflate_raw = test_deflate_raw;

    ScCatalog cat;
    char err[512] = {0};
    if (sc_catalog_load(&z, game_dir, &cat, err, sizeof(err)) != 0) {
        fprintf(stderr, "Error loading catalog: %s\n", err);
        return 1;
    }

    // 1. Civ count + all civs
    printf("\n=== Civilizations (%zu) ===\n", cat.civ_count);
    for (size_t i = 0; i < cat.civ_count; i++) {
        printf("  Civ %2d: %s\n", cat.civs[i].id, cat.civs[i].name);
    }

    // 2. Unit count + first 30 units
    size_t units_to_print = cat.unit_count < 30 ? cat.unit_count : 30;
    printf("\n=== Units (%zu) [first %zu] ===\n", cat.unit_count, units_to_print);
    for (size_t i = 0; i < units_to_print; i++) {
        const ScUnit *u = &cat.units[i];
        printf("  Unit %4d: %-15s (type %2d, class %2d) -> \"%s\"\n",
               u->id, u->internal, u->type, u->class_, u->name);
    }

    // 3. Tech count + first 30 techs
    size_t techs_to_print = cat.tech_count < 30 ? cat.tech_count : 30;
    printf("\n=== Techs (%zu) [first %zu] ===\n", cat.tech_count, techs_to_print);
    for (size_t i = 0; i < techs_to_print; i++) {
        const ScTech *t = &cat.techs[i];
        printf("  Tech %4d: %-25s -> \"%s\"\n", t->id, t->internal, t->name);
    }

    // 4. Stats of units 4 (archer), 125 (monk), 83 (villager) for civs 1 and 2
    int civ_ids[2] = { 1, 2 };
    int unit_ids[3] = { 4, 125, 83 };

    for (int ci = 0; ci < 2; ci++) {
        int cid = civ_ids[ci];
        const char *cname = sc_catalog_civ_name(&cat, cid);
        printf("\n=== Unit Stats for Civ %d (%s) ===\n", cid, cname ? cname : "?");
        for (int ui = 0; ui < 3; ui++) {
            print_unit_stats(&cat, cid, unit_ids[ui]);
        }
    }

    // 5. Verification of Lookup Functions
    printf("\n=== API Lookup Verification ===\n");
    const char *civ1_name = sc_catalog_civ_name(&cat, 1);
    printf("  sc_catalog_civ_name(1): %s\n", civ1_name ? civ1_name : "NULL");

    const ScUnit *u4 = sc_catalog_unit(&cat, 4);
    printf("  sc_catalog_unit(4): %s -> \"%s\"\n",
           u4 ? u4->internal : "NULL", u4 ? u4->name : "");

    const ScTech *t3 = sc_catalog_tech(&cat, 3);
    printf("  sc_catalog_tech(3): %s -> \"%s\"\n",
           t3 ? t3->internal : "NULL", t3 ? t3->name : "");

    // Invalid lookups test
    printf("  sc_catalog_civ_name(999): %s\n",
           sc_catalog_civ_name(&cat, 999) ? "FOUND (error)" : "NULL (OK)");
    printf("  sc_catalog_unit(99999): %s\n",
           sc_catalog_unit(&cat, 99999) ? "FOUND (error)" : "NULL (OK)");
    printf("  sc_catalog_tech(99999): %s\n",
           sc_catalog_tech(&cat, 99999) ? "FOUND (error)" : "NULL (OK)");

    sc_catalog_free(&cat);
    printf("\nCatalog freed successfully.\n");
    return 0;
}
