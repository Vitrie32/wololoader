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

#endif // SC_ZLIB_H
