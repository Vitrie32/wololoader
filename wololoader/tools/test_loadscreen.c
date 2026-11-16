// Standalone test for the loadscreen window
#include "../src/wololoader.h"
#include <stdio.h>
#include <string.h>

WlState g_wl;
char g_wl_build_error[512] = "";
BOOL g_wl_vanilla = FALSE;

// Stubs
void wl_log_open(void) {}
void wl_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

int main(void) {
    // Simulate wololoader state
    strcpy(g_wl.game_dir, "C:\\Program Files\\Age2HD");
    strcpy(g_wl.root, "C:\\Program Files\\Age2HD\\wololoader");

    // Add some fake mods
    g_wl.mod_count = 3;
    g_wl.listed_count = 3;
    strcpy(g_wl.mods[0].name, "Monklanians");
    strcpy(g_wl.mods[0].version, "1.0");
    strcpy(g_wl.mods[1].name, "Custom Civs Pack");
    strcpy(g_wl.mods[1].version, "2.1");
    strcpy(g_wl.mods[2].name, "Balance Mod");
    strcpy(g_wl.mods[2].version, "0.5");

    printf("Starting loadscreen test...\n");
    wl_install_loadscreen();
    Sleep(500);

    // Simulate log messages
    wl_loadscreen_add_log("Wololoader v0.3 starting...");
    Sleep(300);
    wl_loadscreen_add_log("Scanning mods: found 3 enabled");
    Sleep(300);
    wl_loadscreen_add_log("Checking cache...");
    Sleep(300);

    wl_loadscreen_status("Building mods...");
    wl_loadscreen_add_log("build: mods changed, rebuilding...");
    Sleep(800);
    wl_loadscreen_add_log("build: processing Monklanians civ...");
    Sleep(600);
    wl_loadscreen_add_log("build: ok, 1 custom civ(s), 1234 ms");
    Sleep(400);

    wl_loadscreen_status("Loading game data...");
    wl_loadscreen_add_log("Installing file hooks");
    Sleep(400);
    wl_loadscreen_add_log("Installing civ hooks");
    Sleep(400);
    wl_loadscreen_add_log("Installing menu hooks");
    Sleep(400);

    // Test with build error
    strcpy(g_wl_build_error, "Parse error in mod.json line 42");
    wl_loadscreen_add_log("ERROR: mod build failed");
    Sleep(2000);

    printf("Closing loadscreen...\n");
    wl_loadscreen_close();
    Sleep(1000);

    printf("Test complete.\n");
    return 0;
}
