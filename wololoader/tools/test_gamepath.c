/* Native test for scriptorium/gamepath.c: the path rules that keep /api/game-file inside
 * <game>\resources\. Build and run:
 *   gcc -std=c11 -Wall -Wextra -Iscriptorium -o /tmp/tgp tools/test_gamepath.c scriptorium/gamepath.c && /tmp/tgp
 */
#include <stdio.h>
#include "gamepath.h"

static int fails = 0;
#define EXPECT(expr, want, what) do { \
    int got_ = (expr); \
    if (got_ != (want)) { printf("FAIL %s: got %d want %d\n", what, got_, want); fails++; } \
} while (0)

int main(void) {
    // accepted game files
    EXPECT(sc_game_rel_path_ok("_common/drs/graphics/123.slp", 0), 1, "graphics slp");
    EXPECT(sc_game_rel_path_ok("_common/drs/interface/50730.slp", 0), 1, "interface slp");
    EXPECT(sc_game_rel_path_ok("_common/dat/empires2_x2_p1.dat", 0), 0, "dat is not an allowed extension");
    EXPECT(sc_game_rel_path_ok("_common/sound/a.MP3", 0), 1, "extension case-insensitive");
    EXPECT(sc_game_rel_path_ok("_common/sound/a.wav", 0), 1, "wav");
    EXPECT(sc_game_rel_path_ok("_common/drs/interface/50500.bina", 0), 1, "bina");
    EXPECT(sc_game_rel_path_ok("_common/textures/x.png", 0), 1, "png");
    // accepted directories
    EXPECT(sc_game_rel_path_ok("_common/drs/graphics", 1), 1, "dir");
    EXPECT(sc_game_rel_path_ok("_common/drs/graphics/", 1), 0, "trailing slash");
    // traversal, absolute, drive letters, backslashes
    EXPECT(sc_game_rel_path_ok("../secret.slp", 0), 0, "parent");
    EXPECT(sc_game_rel_path_ok("_common/../../x.slp", 0), 0, "parent inside");
    EXPECT(sc_game_rel_path_ok("/etc/x.slp", 0), 0, "absolute slash");
    EXPECT(sc_game_rel_path_ok("\\x.slp", 0), 0, "absolute backslash");
    EXPECT(sc_game_rel_path_ok("C:x.slp", 0), 0, "drive letter");
    EXPECT(sc_game_rel_path_ok("C:/x.slp", 0), 0, "drive letter slash");
    EXPECT(sc_game_rel_path_ok("_common\\drs\\x.slp", 0), 0, "backslash");
    EXPECT(sc_game_rel_path_ok("_common/x.slp:stream", 0), 0, "alternate data stream");
    EXPECT(sc_game_rel_path_ok("_common//x.slp", 0), 0, "double slash");
    EXPECT(sc_game_rel_path_ok("_common/./x.slp", 0), 0, "dot segment");
    EXPECT(sc_game_rel_path_ok("_common/x y.slp", 0), 0, "space");
    EXPECT(sc_game_rel_path_ok("_common/%2e%2e/x.slp", 0), 0, "encoded dots stay literal (% rejected)");
    // Windows device names
    EXPECT(sc_game_rel_path_ok("_common/nul.slp", 0), 0, "NUL device");
    EXPECT(sc_game_rel_path_ok("_common/COM1.slp", 0), 0, "COM1 device");
    EXPECT(sc_game_rel_path_ok("_common/lpt9.mp3", 0), 0, "LPT9 device");
    EXPECT(sc_game_rel_path_ok("_common/nullify.slp", 0), 1, "name that only starts with NUL is fine");
    // limits and empties
    EXPECT(sc_game_rel_path_ok("", 0), 0, "empty");
    EXPECT(sc_game_rel_path_ok(NULL, 1), 0, "null");
    EXPECT(sc_game_rel_path_ok("a/b/c/d/e/f/g/h/x.slp", 0), 0, "nine segments");
    EXPECT(sc_game_rel_path_ok("a/b/c/d/e/f/g/x.slp", 0), 1, "eight segments");
    EXPECT(sc_game_rel_path_ok("x.slp", 0), 1, "file at the root");
    EXPECT(sc_game_file_name_ok("x.slp."), 0, "trailing dot is no extension");
    EXPECT(sc_game_file_name_ok("slp"), 0, "no dot");

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("test_gamepath: all checks passed\n");
    return 0;
}
