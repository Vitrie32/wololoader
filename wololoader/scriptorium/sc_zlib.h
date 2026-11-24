// ==========================================================================
// Scriptorium - Zlib Wrapper
//
// Purpose:
//   Provides raw deflate/inflate routines for Scriptorium by dynamically
//   loading the game's zlibwapi.dll (zlib 1.2.7, __stdcall export convention).
//   Does not depend on Wololoader DLL state or headers.
// ==========================================================================

#ifndef SC_ZLIB_H
#define SC_ZLIB_H

#include "../src/datbuild.h"
#include <stddef.h>

// Initializes raw zlib compression/decompression using <game_dir>\zlibwapi.dll.
// Returns a pointer to a valid WlZlib interface, or NULL on error.
const WlZlib *sc_zlib_init(const char *game_dir, char *err, size_t errcap);

// Raw inflate with a strict upper bound on uncompressed output bytes.
// Returns 0 on success, -1 on error or if output exceeds max_out.
int sc_inflate_raw_bounded(const unsigned char *in, size_t in_len, size_t max_out,
                           unsigned char **out, size_t *out_len);

#endif // SC_ZLIB_H
