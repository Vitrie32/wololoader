// DllMain, version.dll forwarding and loader start-up.
#include "wololoader.h"
#include <stdio.h>
#include <string.h>

WlState g_wl;

// Filled here, used by the jump stubs in proxy_version.S (order must match).
void *wl_real_procs[17];
static const char *k_proc_names[17] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW", "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW", "GetFileVersionInfoW",
    "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW",
};

static void load_real_version_dll(void) {
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strncat(path, "\\version.dll", MAX_PATH - strlen(path) - 1);
    HMODULE real = LoadLibraryA(path);
    for (int i = 0; i < 17; i++)
        wl_real_procs[i] = real ? (void *)GetProcAddress(real, k_proc_names[i]) : NULL;
}

// Only patch the exact build we reverse engineered.
static BOOL is_supported_exe(void) {
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + dos->e_lfanew);
    // AoK HD.exe 5.8: SizeOfCode 0x5ab000, ImageBase 0x400000.
    return (DWORD)exe == 0x400000 && nt->OptionalHeader.SizeOfCode == 0x5ab000;
}

// stamp == 0: the right exe, but Windows loaded it at a random address (ASLR, see installer/wlsetup.c).
static DWORD WINAPI unsupported_notice(LPVOID stamp) {
    char msg[600];
    if (!stamp)
        snprintf(msg, sizeof msg,
                 "Wololoader %s is off: Windows started the game at a random memory address.\n\n"
                 "Run the Wololoader setup again and press Install: it prepares the game exe for Wololoader. "
                 "(This happens after Steam updates or verifies the game files.)\n\nThe game starts normally, without mods.",
                 WOLOLOADER_VERSION);
    else
    snprintf(msg, sizeof msg,
             "Wololoader %s does not support this build of Age of Empires II HD (build stamp %08lx).\n\n"
             "The game starts normally, without mods.\n\n"
             "Please report it at github.com/Vitrie32/wololoader/issues with this number, "
             "or remove version.dll from the game folder to stop this message.",
             WOLOLOADER_VERSION, (unsigned long)(DWORD_PTR)stamp);
    MessageBoxA(NULL, msg, "Wololoader", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    return 0;
}

static void init_paths(void) {
    GetModuleFileNameA(NULL, g_wl.game_dir, MAX_PATH);
    char *slash = strrchr(g_wl.game_dir, '\\');
    if (slash) *slash = 0;
    snprintf(g_wl.root, MAX_PATH, "%s\\wololoader", g_wl.game_dir);
    CreateDirectoryA(g_wl.root, NULL);
    char mods[MAX_PATH];
    snprintf(mods, MAX_PATH, "%s\\mods", g_wl.root);
    CreateDirectoryA(mods, NULL);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    load_real_version_dll();

    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!strstr(exe, "AoK HD.exe")) return TRUE;  // not the game (e.g. launcher)

    init_paths();
    wl_log_open();
    wl_log("Wololoader v%s starting in %s", WOLOLOADER_VERSION, g_wl.game_dir);
    wl_scan_mods();
    if (!is_supported_exe()) {
        HMODULE m = GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        int relocated = (DWORD)m != 0x400000 && nt->OptionalHeader.SizeOfCode == 0x5ab000;
        wl_log("Unsupported AoK HD.exe %s (base %p, SizeOfCode 0x%lx, TimeDateStamp 0x%lx), hooks disabled.",
               relocated ? "load address" : "build", (void *)m, nt->OptionalHeader.SizeOfCode, nt->FileHeader.TimeDateStamp);
        // Tell the player once (the game itself runs normally, without mods). A thread: no
        // user32 calls in DllMain; it runs after the loader lock is released.
        CreateThread(NULL, 0, unsupported_notice, relocated ? NULL : (LPVOID)(DWORD_PTR)nt->FileHeader.TimeDateStamp, 0, NULL);
        return TRUE;
    }
    wl_install_crash_logger();
    wl_install_hang_watchdog();
    // Start the loading window (own thread, never blocks DllMain)
    wl_install_loadscreen();
    // Also in Vanilla Mode: it only avoids a startup hang, game data is unchanged.
    wl_install_no_workshop();
    wl_install_lobby();   // lobby: say why a player with different mods cannot join
    if (wl_feature("borderless")) wl_install_borderless();
    g_wl_vanilla = wl_vanilla_flag_exists();
    if (g_wl_vanilla) {
        // Vanilla Mode: the game must stay identical to an unmodded one (online
        // lobbies with players without Wololoader). Only UI that does not touch
        // game data: the menu label and the Mods screen to switch it off again.
        wl_log("Vanilla Mode: mods and patches disabled.");
        wl_install_menu_hooks();
        wl_install_mods_screen();
        wl_install_string_hook_only();   // button name "Mods" (text only)
        wl_install_ui_textures_only();   // Mods button image (UI only)
        return TRUE;
    }
    // No wl_autobuild() here: see wl_civs_ensure() in civs.c (R6030 in DllMain).
    wl_install_file_hooks();
    wl_icons_build();      // mod icons appended to interface SLPs (redirects)
    wl_install_civ_hooks();
    wl_install_menu_hooks();
    wl_install_mods_screen();
    // Gameplay/UI features of the "Wololoader Base" mod (basemod.c), each can be switched off.
    wl_install_gamecmd();   // command executor hook: Lua game commands + overflow clones
    if (wl_feature("popcap_1000")) wl_install_popcap();
    if (wl_feature("selection_250")) { wl_install_overflow(); wl_install_selpanel(); }
    if (wl_feature("formations_tab")) wl_install_formtab();
    wl_install_lua_game();
    wl_install_uikit();
    wl_install_buildpage();
    wl_install_mp_guard();
#ifdef WL_SELTRACE
    wl_log("TRACE BUILD: Selection field access tracing enabled");
    wl_seltrace_init();
    atexit(wl_seltrace_shutdown);
#endif
    return TRUE;
}

int __stdcall Wololoader_GetModCount(void) { return g_wl.mod_count; }
const char *__stdcall Wololoader_GetVersion(void) { return WOLOLOADER_VERSION; }
