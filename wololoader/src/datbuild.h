#ifndef WL_DATBUILD_H
#define WL_DATBUILD_H

#include <stddef.h>

// Builder output revision: part of the autobuild fingerprint (autobuild.c).
// Bump it whenever wl_dat_build produces different bytes for the same mods,
// so players' caches are rebuilt automatically.
#define WL_BUILDER_REV "gather1"  // bump: gatherable custom resources + cache/gather.txt

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

// Automatic renumbering of new mod sprites (see datbuild.c). One entry per renumbered
// sprite: <mods_dir>/<mod>/resources/_common/drs/graphics/<from>.slp is used as id <to>.
#define WL_SLP_REMAP_FIRST 90000   // above every sprite id of the game (gamedata_x2 ends at 88713)
typedef struct { char mod[256]; int from, to; } WlSlpRemap;
// drs_dir: <game>/resources/_common/drs. *out is malloc'd (free it). 0 = ok, -1 = error.
int wl_slp_remap_compute(const char *drs_dir, const char *mods_dir, WlSlpRemap **out, size_t *out_count);

#endif // WL_DATBUILD_H
