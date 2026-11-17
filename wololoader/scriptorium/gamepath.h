// ==========================================================================
// Scriptorium - validation of paths into the game's own resources\ tree
//
// Used by /api/game-file and /api/game-list (api.c). Kept free of Windows
// headers so tools/test_gamepath.c can test it natively.
// ==========================================================================
#ifndef SC_GAMEPATH_H
#define SC_GAMEPATH_H

#include <stddef.h>

// 1 when `rel` is a safe path relative to <game>\resources\ (forward slashes):
// 1..8 segments of [A-Za-z0-9_.-], no empty/"."/".." segment, no drive letter,
// no backslash, no Windows device name (CON, NUL, COM1...), at most 200 chars.
// With is_dir = 0 the last segment must be a file with an allowed extension
// (.slp .bina .mp3 .wav .png, case-insensitive).
int sc_game_rel_path_ok(const char *rel, int is_dir);

// 1 when `name` (a bare file name) has an allowed game-file extension.
int sc_game_file_name_ok(const char *name);

#endif // SC_GAMEPATH_H
