// Native Linux test for Level 2 hot-reload pure logic (src/hotreload_core.h).
// Build & run from wololoader/:
//   gcc -O2 -Wall -Wextra -std=c11 -Isrc -o /tmp/test_hotreload tools/test_hotreload.c && /tmp/test_hotreload
#include "hotreload_core.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        g_fail = 1; \
    } \
} while (0)

static void test_civ_diff(void) {
    // 1. Add, remove, keep, ignore vanilla (<35), deduplicate, sort output
    int old_ids[] = { 37, 35, 1, 35, 39, 32 };
    int new_ids[] = { 38, 35, 36, 39, 39, 14 };
    WlCivDiff d;
    wl_hotreload_diff_civs(old_ids, 6, new_ids, 6, &d);

    CHECK(d.added_count == 2);
    CHECK(d.added[0] == 36 && d.added[1] == 38);
    CHECK(d.removed_count == 1);
    CHECK(d.removed[0] == 37);
    CHECK(d.kept_count == 2);
    CHECK(d.kept[0] == 35 && d.kept[1] == 39);

    // 2. Empty -> added
    int empty[1] = { 0 };
    int one[] = { 35 };
    wl_hotreload_diff_civs(empty, 0, one, 1, &d);
    CHECK(d.added_count == 1 && d.added[0] == 35);
    CHECK(d.removed_count == 0 && d.kept_count == 0);

    // 3. All removed
    wl_hotreload_diff_civs(one, 1, empty, 0, &d);
    CHECK(d.removed_count == 1 && d.removed[0] == 35);
    CHECK(d.added_count == 0 && d.kept_count == 0);

    // 4. Identical
    int same[] = { 35, 36 };
    wl_hotreload_diff_civs(same, 2, same, 2, &d);
    CHECK(d.added_count == 0 && d.removed_count == 0 && d.kept_count == 2);
}

static void test_feature_diff(void) {
    WlBaseFeatures start = { 0, 1, 1, 1, 1 };
    WlBaseFeatures cur = start;
    char buf[256];

    CHECK(wl_hotreload_diff_features(&start, &cur, buf, sizeof buf) == 0);
    CHECK(buf[0] == '\0');

    cur.popcap_1000 = 0;
    cur.borderless = 0;
    CHECK(wl_hotreload_diff_features(&start, &cur, buf, sizeof buf) == 2);
    CHECK(strcmp(buf, "popcap_1000, borderless") == 0);

    cur = start;
    cur.vanilla = 1;
    CHECK(wl_hotreload_diff_features(&start, &cur, buf, sizeof buf) == 1);
    CHECK(strcmp(buf, "Vanilla Mode") == 0);

    cur.selection_250 = 0;
    cur.formations_tab = 0;
    CHECK(wl_hotreload_diff_features(&start, &cur, buf, sizeof buf) == 3);
    CHECK(strcmp(buf, "Vanilla Mode, selection_250, formations_tab") == 0);
}

static void test_drs_diff(void) {
    WlDrsState a = { { 11, 22, 33, 44 }, 55 };
    WlDrsState b = a;
    int rel[4];

    // No change
    CHECK(wl_hotreload_diff_drs(&a, &b, rel) == 0);
    CHECK(rel[0] == 0 && rel[1] == 0 && rel[2] == 0 && rel[3] == 0);

    // Overriding an existing vanilla interface SLP changes pkg_fp[3], NOT iface_new_fp -> do NOT unload interface!
    b.pkg_fp[3] = 999;
    CHECK(wl_hotreload_diff_drs(&a, &b, rel) == 0);
    CHECK(rel[3] == 0);

    // Adding a brand-new filename in drs/interface/ changes iface_new_fp -> reload interface
    b.iface_new_fp = 777;
    CHECK(wl_hotreload_diff_drs(&a, &b, rel) == 1);
    CHECK(rel[0] == 0 && rel[1] == 0 && rel[2] == 0 && rel[3] == 1);

    // Changing graphics and terrain
    b = a;
    b.pkg_fp[0] = 100;
    b.pkg_fp[2] = 300;
    CHECK(wl_hotreload_diff_drs(&a, &b, rel) == 1);
    CHECK(rel[0] == 1 && rel[1] == 0 && rel[2] == 1 && rel[3] == 0);
}

static void test_status_format(void) {
    char buf[256];

    wl_hotreload_format_status(1, 1, 1, NULL, NULL, buf, sizeof buf);
    CHECK(strcmp(buf, "Mods reloaded (1 civ, 1 mod)") == 0);

    wl_hotreload_format_status(1, 3, 5, NULL, NULL, buf, sizeof buf);
    CHECK(strcmp(buf, "Mods reloaded (3 civs, 5 mods)") == 0);

    wl_hotreload_format_status(1, 2, 4, "popcap_1000, borderless", NULL, buf, sizeof buf);
    CHECK(strcmp(buf, "Mods reloaded (2 civs, 4 mods). Restart required for: popcap_1000, borderless") == 0);

    wl_hotreload_format_status(0, 0, 2, NULL, "bad json", buf, sizeof buf);
    CHECK(strcmp(buf, "Reload failed: bad json (restart the game)") == 0);
}

int main(void) {
    test_civ_diff();
    test_feature_diff();
    test_drs_diff();
    test_status_format();
    if (g_fail) {
        printf("HOTRELOAD CORE TESTS FAILED\n");
        return 1;
    }
    printf("All hotreload core tests passed.\n");
    return 0;
}
