#ifndef WL_DATBUILD_H
#define WL_DATBUILD_H

#include <stddef.h>

// Builder output revision: part of the autobuild fingerprint (autobuild.c).
// Bump it whenever wl_dat_build produces different bytes for the same mods,
// so players' caches are rebuilt automatically.
#define WL_BUILDER_REV "unit-gfx1"   // unit "graphics" (copied graphic records with mod SLPs)

typedef struct {
    int (*inflate_raw)(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);
    int (*deflate_raw)(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);
} WlZlib; // return 0 on success; out buffers malloc'd

int wl_dat_build(const WlZlib *z,
                 const char *src_dat,
                 const char *mods_dir,
                 const char *out_dat,
                 const char *out_civs_txt,
                 const char *out_strings_txt,
                 const char *out_commands_txt,
                 char *err,
                 size_t errcap);

#endif // WL_DATBUILD_H
