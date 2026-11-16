#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../src/datbuild.h"

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

int main(int argc, char **argv) {
    char src_dat[1024];
    char mods_dir[1024];
    char out_dat[1024];
    char out_civs_txt[1024];
    char out_strings_txt[1024];
    char out_commands_txt[1024];

    if (argc == 2) {
        snprintf(src_dat, sizeof(src_dat), "%.800s/resources/_common/dat/empires2_x2_p1.dat", argv[1]);
        snprintf(mods_dir, sizeof(mods_dir), "%.800s/wololoader/mods", argv[1]);
        snprintf(out_dat, sizeof(out_dat), "%.800s/wololoader/cache/empires2_x2_p1.dat", argv[1]);
        snprintf(out_civs_txt, sizeof(out_civs_txt), "%.800s/wololoader/cache/civs.txt", argv[1]);
        snprintf(out_strings_txt, sizeof(out_strings_txt), "%.800s/wololoader/cache/strings.txt", argv[1]);
        snprintf(out_commands_txt, sizeof(out_commands_txt), "%.800s/wololoader/cache/commands.txt", argv[1]);
    } else if (argc == 5) {
        strncpy(src_dat, argv[1], sizeof(src_dat) - 1);
        src_dat[sizeof(src_dat) - 1] = '\0';
        strncpy(mods_dir, argv[2], sizeof(mods_dir) - 1);
        mods_dir[sizeof(mods_dir) - 1] = '\0';
        strncpy(out_dat, argv[3], sizeof(out_dat) - 1);
        out_dat[sizeof(out_dat) - 1] = '\0';
        strncpy(out_civs_txt, argv[4], sizeof(out_civs_txt) - 1);
        out_civs_txt[sizeof(out_civs_txt) - 1] = '\0';
        snprintf(out_strings_txt, sizeof(out_strings_txt), "%.800s.strings.txt", argv[3]);
        snprintf(out_commands_txt, sizeof(out_commands_txt), "%.800s.commands.txt", argv[3]);
    } else if (argc == 7) {
        strncpy(src_dat, argv[1], sizeof(src_dat) - 1);
        src_dat[sizeof(src_dat) - 1] = '\0';
        strncpy(mods_dir, argv[2], sizeof(mods_dir) - 1);
        mods_dir[sizeof(mods_dir) - 1] = '\0';
        strncpy(out_dat, argv[3], sizeof(out_dat) - 1);
        out_dat[sizeof(out_dat) - 1] = '\0';
        strncpy(out_civs_txt, argv[4], sizeof(out_civs_txt) - 1);
        out_civs_txt[sizeof(out_civs_txt) - 1] = '\0';
        strncpy(out_strings_txt, argv[5], sizeof(out_strings_txt) - 1);
        out_strings_txt[sizeof(out_strings_txt) - 1] = '\0';
        strncpy(out_commands_txt, argv[6], sizeof(out_commands_txt) - 1);
        out_commands_txt[sizeof(out_commands_txt) - 1] = '\0';
    } else {
        fprintf(stderr, "usage: %s \"<game dir>\" | <src_dat> <mods_dir> <out_dat> <out_civs_txt> [<out_strings_txt> <out_commands_txt>]\n", argv[0]);
        return 2;
    }

    WlZlib z = {
        .inflate_raw = test_inflate_raw,
        .deflate_raw = test_deflate_raw,
    };

    char err[1024];
    err[0] = '\0';
    int ret = wl_dat_build(&z, src_dat, mods_dir, out_dat, out_civs_txt, out_strings_txt, out_commands_txt, err, sizeof(err));
    if (ret < 0) {
        fprintf(stderr, "build error: %s\n", err);
        return 1;
    }

    printf("wrote %s (%d custom civs)\n", out_dat, ret);
    return 0;
}
