// Native Linux unit test for hubcore.c (version compare, SHA-256, zip safety,
// dependency-first install, enabled preservation, uninstall to trash).
// Build:
//   gcc -O2 -Wall -Wextra -std=c11 -Isrc -o /tmp/test_hubcore tools/test_hubcore.c src/hubcore.c src/json.c -lz -lm
#define _POSIX_C_SOURCE 200809L
#include "../src/hubcore.h"
#include "../src/json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

static int linux_inflate_raw_bounded(const unsigned char *in, size_t in_len, size_t max_out,
                                     unsigned char **out, size_t *out_len, void *user) {
    (void)user;
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -15) != Z_OK) return -1;

    size_t cap = in_len * 4;
    if (cap < 4096) cap = 4096;
    if (max_out > 0 && cap > max_out) cap = max_out;
    if (max_out == 0) cap = 64;

    unsigned char *buf = malloc(cap);
    if (!buf) { inflateEnd(&s); return -1; }

    s.next_in = (Bytef *)in;
    s.avail_in = (uInt)in_len;
    size_t len = 0;
    int rc = Z_OK;

    while (buf && rc == Z_OK) {
        if (len == cap) {
            if (len >= max_out) { free(buf); buf = NULL; break; }
            size_t next_cap = cap * 2;
            if (next_cap < cap || next_cap > max_out) next_cap = max_out;
            unsigned char *bigger = realloc(buf, next_cap);
            if (!bigger) { free(buf); buf = NULL; break; }
            buf = bigger;
            cap = next_cap;
        }
        size_t space = (max_out == 0) ? cap : (cap - len);
        s.next_out = buf + len;
        s.avail_out = (uInt)space;
        rc = inflate(&s, Z_NO_FLUSH);
        len = cap - s.avail_out;
        if (len > max_out) { free(buf); buf = NULL; break; }
    }

    inflateEnd(&s);
    if (!buf || rc != Z_STREAM_END || len > max_out) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = len;
    return 0;
}

static void test_ids_and_versions(void) {
    assert(hub_is_valid_id("Monklanians") == 1);
    assert(hub_is_valid_id("Base_Dep-Mod 2") == 1);
    assert(hub_is_valid_id("") == 0);
    assert(hub_is_valid_id(".") == 0);
    assert(hub_is_valid_id("..") == 0);
    assert(hub_is_valid_id("../evil") == 0);
    assert(hub_is_valid_id("a/b") == 0);
    assert(hub_is_valid_id("a\\b") == 0);
    assert(hub_is_valid_id("C:mod") == 0);
    assert(hub_is_valid_id("WololoaderBase") == 0);
    assert(hub_is_valid_id("wololoaderbase") == 0);
    assert(hub_is_valid_id(" lead") == 0);
    assert(hub_is_valid_id("trail ") == 0);

    assert(hub_compare_versions("1.0.0", "1.0") == 0);
    assert(hub_compare_versions("v1.2.0", "1.10.0") < 0);
    assert(hub_compare_versions("2.0.0", "1.99.99") > 0);
    assert(hub_compare_versions("1.0.0-beta", "1.0.0") < 0);
    assert(hub_compare_versions("1.0.0", "1.0.0-rc1") > 0);
    assert(hub_compare_versions("0.4.1", "0.4.0") > 0);
    printf("PASS: id validation & version compare\n");
}

static void test_sha256_and_zip_paths(void) {
    char hex[65];
    hub_sha256_hex((const uint8_t *)"", 0, hex);
    assert(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    hub_sha256_hex((const uint8_t *)"abc", 3, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    assert(hub_is_valid_sha256_hex(hex) == 1);
    assert(hub_is_valid_sha256_hex("xyz") == 0);

    char norm[512];
    int is_dir = 0;
    assert(hub_validate_zip_path("mod.json", 8, 0, 0, norm, &is_dir) == 0 && strcmp(norm, "mod.json") == 0 && !is_dir);
    assert(hub_validate_zip_path("MyMod\\scripts\\main.lua", 22, 0, 0, norm, &is_dir) == 0 &&
           strcmp(norm, "MyMod/scripts/main.lua") == 0 && !is_dir);
    assert(hub_validate_zip_path("/etc/passwd", 11, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("\\windows\\x", 10, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("../escape", 9, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("a/../b", 6, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("C:foo", 5, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("a//b", 4, 0, 0, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("sym", 3, 3 << 8, 0xA000u << 16, norm, &is_dir) != 0);
    assert(hub_validate_zip_path("reparse", 7, 0, 0x0400u, norm, &is_dir) != 0);
    printf("PASS: SHA-256 & ZIP path validation\n");
}

static void test_zip_fixtures_and_install(const char *www_dir) {
    char tmp_root[] = "/tmp/wl_hubcore_test_XXXXXX";
    assert(mkdtemp(tmp_root) != NULL);

    char game_dir[HUB_PATH_CAP], mods_dir[HUB_PATH_CAP], err[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game", tmp_root);
    snprintf(mods_dir, sizeof(mods_dir), "%s/game/wololoader/mods", tmp_root);
    assert(hub_mkdir_p(mods_dir) == 0);

    // 1) Reject traversal zip
    char bad_trav[HUB_PATH_CAP], stage[HUB_PATH_CAP];
    snprintf(bad_trav, sizeof(bad_trav), "%s/BadTrav.zip", www_dir);
    snprintf(stage, sizeof(stage), "%s/bad_trav_out", tmp_root);
    assert(hub_mkdir_p(stage) == 0);
    assert(hub_extract_zip_safe(bad_trav, stage, linux_inflate_raw_bounded, NULL, err, sizeof(err)) != 0);
    printf("PASS: rejected BadTrav.zip (%s)\n", err);

    // 2) Reject oversized > 32MB entry zip
    char bad_big[HUB_PATH_CAP];
    snprintf(bad_big, sizeof(bad_big), "%s/BadBig.zip", www_dir);
    snprintf(stage, sizeof(stage), "%s/bad_big_out", tmp_root);
    assert(hub_mkdir_p(stage) == 0);
    assert(hub_extract_zip_safe(bad_big, stage, linux_inflate_raw_bounded, NULL, err, sizeof(err)) != 0);
    printf("PASS: rejected BadBig.zip (%s)\n", err);

    // 3) Build a local file:// index referencing www_dir zips and install ChildMod (pulls BaseDepMod)
    char sha_base[65], sha_child1[65], sha_child2[65];
    char p_base[HUB_PATH_CAP], p_c1[HUB_PATH_CAP], p_c2[HUB_PATH_CAP];
    snprintf(p_base, sizeof(p_base), "%s/BaseDepMod.zip", www_dir);
    snprintf(p_c1, sizeof(p_c1), "%s/ChildMod_v1.zip", www_dir);
    snprintf(p_c2, sizeof(p_c2), "%s/ChildMod_v2.zip", www_dir);
    assert(hub_sha256_file(p_base, sha_base) == 0);
    assert(hub_sha256_file(p_c1, sha_child1) == 0);
    assert(hub_sha256_file(p_c2, sha_child2) == 0);

    struct stat sb_base, sb_c1, sb_c2;
    assert(stat(p_base, &sb_base) == 0);
    assert(stat(p_c1, &sb_c1) == 0);
    assert(stat(p_c2, &sb_c2) == 0);

    char idx_path[HUB_PATH_CAP];
    snprintf(idx_path, sizeof(idx_path), "%s/index.json", tmp_root);
    FILE *fp = fopen(idx_path, "wb");
    assert(fp != NULL);
    fprintf(fp,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"mods\": [\n"
        "    {\"id\":\"BaseDepMod\",\"name\":\"Base\",\"version\":\"1.0.0\",\"download\":\"%s\",\"sha256\":\"%s\",\"size\":%lld,\"depends\":[]},\n"
        "    {\"id\":\"ChildMod\",\"name\":\"Child\",\"version\":\"1.0.0\",\"download\":\"%s\",\"sha256\":\"%s\",\"size\":%lld,\"depends\":[\"BaseDepMod\"]}\n"
        "  ]\n"
        "}\n",
        p_base, sha_base, (long long)sb_base.st_size,
        p_c1, sha_child1, (long long)sb_c1.st_size);
    fclose(fp);

    WlJsonVal *idx = hub_fetch_and_parse_index(idx_path, err, sizeof(err));
    assert(idx != NULL);

    HubInstallResult ir;
    int rc = hub_install_mod(game_dir, mods_dir, idx, idx_path, "ChildMod", 0, "0.4.1",
                             linux_inflate_raw_bounded, NULL, NULL, NULL, NULL, NULL, &ir);
    assert(rc == 0);
    assert(ir.installed_dep_count == 1 && strcmp(ir.installed_deps[0], "BaseDepMod") == 0);
    hub_install_result_free(&ir);
    wl_json_free(idx);

    HubLocalState st_base, st_child;
    hub_inspect_local_mod(mods_dir, "BaseDepMod", &st_base);
    hub_inspect_local_mod(mods_dir, "ChildMod", &st_child);
    assert(st_base.installed && st_base.hub_managed);
    assert(st_child.installed && st_child.hub_managed);

    // Disable ChildMod, update to v2, ensure enabled=false is preserved
    char child_mj[HUB_PATH_CAP];
    snprintf(child_mj, sizeof(child_mj), "%s/ChildMod/mod.json", mods_dir);
    FILE *mj_out = fopen(child_mj, "wb");
    assert(mj_out != NULL);
    fprintf(mj_out, "{\"name\":\"Child\",\"version\":\"1.0.0\",\"enabled\":false}\n");
    fclose(mj_out);

    fp = fopen(idx_path, "wb");
    assert(fp != NULL);
    fprintf(fp,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"mods\": [\n"
        "    {\"id\":\"BaseDepMod\",\"name\":\"Base\",\"version\":\"1.0.0\",\"download\":\"%s\",\"sha256\":\"%s\",\"size\":%lld,\"depends\":[]},\n"
        "    {\"id\":\"ChildMod\",\"name\":\"Child\",\"version\":\"2.0.0\",\"download\":\"%s\",\"sha256\":\"%s\",\"size\":%lld,\"depends\":[\"BaseDepMod\"]}\n"
        "  ]\n"
        "}\n",
        p_base, sha_base, (long long)sb_base.st_size,
        p_c2, sha_child2, (long long)sb_c2.st_size);
    fclose(fp);

    idx = hub_fetch_and_parse_index(idx_path, err, sizeof(err));
    assert(idx != NULL);
    rc = hub_install_mod(game_dir, mods_dir, idx, idx_path, "ChildMod", 0, "0.4.1",
                         linux_inflate_raw_bounded, NULL, NULL, NULL, NULL, NULL, &ir);
    assert(rc == 0 && ir.updated == 1);
    hub_install_result_free(&ir);
    wl_json_free(idx);

    hub_inspect_local_mod(mods_dir, "ChildMod", &st_child);
    assert(st_child.installed && st_child.has_enabled && st_child.enabled == 0);
    assert(strcmp(st_child.version, "2.0.0") == 0);
    printf("PASS: dependency-first install & enabled=false preservation on update\n");

    // Uninstall to trash
    char trash[HUB_PATH_CAP];
    int status = 0;
    assert(hub_uninstall_mod(game_dir, mods_dir, "ChildMod", trash, sizeof(trash), &status, err, sizeof(err)) == 0);
    assert(hub_dir_exists(trash));
    hub_inspect_local_mod(mods_dir, "ChildMod", &st_child);
    assert(!st_child.installed);
    printf("PASS: uninstall moved ChildMod to %s\n", trash);

    hub_rmdir_recursive(tmp_root);
}

int main(int argc, char **argv) {
    test_ids_and_versions();
    test_sha256_and_zip_paths();
    if (argc > 1 && argv[1][0]) {
        test_zip_fixtures_and_install(argv[1]);
    }
    printf("ALL HUBCORE TESTS PASSED\n");
    return 0;
}
