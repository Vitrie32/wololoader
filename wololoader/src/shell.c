// Open a folder in the user's file manager.
// Under Wine/Proton, explorer.exe would show Wine's own file browser, so we
// use winebrowser, which hands the path to xdg-open on the Linux side.
#include "wololoader.h"
#include <shellapi.h>
#include <stdio.h>

static BOOL running_under_wine(void) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != NULL;
}

void wl_open_folder(const char *path) {
    CreateDirectoryA(path, NULL);
    if (running_under_wine()) {
        char cmd[MAX_PATH + 64];
        snprintf(cmd, sizeof cmd, "winebrowser.exe \"%s\"", path);
        STARTUPINFOA si;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        PROCESS_INFORMATION pi;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            wl_log("open folder (winebrowser): %s", path);
            return;
        }
        wl_log("winebrowser failed (%lu), falling back to ShellExecute", GetLastError());
    }
    HINSTANCE r = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    wl_log("open folder (ShellExecute=%d): %s", (int)(INT_PTR)r, path);
}

// Start the mod creator <root>\Scriptorium.exe (it opens its own app window).
// The game keeps running; changes apply after a game restart (autobuild).
BOOL wl_launch_scriptorium(void) {
    char exe[MAX_PATH], cmd[MAX_PATH + 16];
    snprintf(exe, MAX_PATH, "%s\\Scriptorium.exe", g_wl.root);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) {
        wl_log("Scriptorium not found: %s", exe);
        return FALSE;
    }
    snprintf(cmd, sizeof cmd, "\"%s\"", exe);
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_wl.root, &si, &pi)) {
        wl_log("Scriptorium start failed (%lu)", GetLastError());
        return FALSE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    wl_log("Scriptorium started");
    return TRUE;
}
