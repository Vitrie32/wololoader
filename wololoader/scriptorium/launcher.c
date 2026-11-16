// ==========================================================================
// Scriptorium v2 - Application Window Launcher & Lifetime Implementation
//
// Purpose:
//   Implements app-mode browser window launching per API.md specification.
//   Handles Wine-specific unix process launching via 'start /unix',
//   Windows Edge/Chrome standalone application window spawning, and
//   heartbeat monitoring with a 15-second grace period after first ping.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "launcher.h"

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int   s_first_ping_received = 0;
static DWORD s_last_ping_tick      = 0;
static int   s_quit_requested      = 0;

int launcher_is_wine(void) {
    static int s_checked = 0;
    static int s_is_wine = 0;
    if (!s_checked) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll && GetProcAddress(ntdll, "wine_get_version")) {
            s_is_wine = 1;
        }
        s_checked = 1;
    }
    return s_is_wine;
}

void launcher_notify_ping(void) {
    s_first_ping_received = 1;
    s_last_ping_tick      = GetTickCount();
}

void launcher_request_quit(void) {
    s_quit_requested = 1;
}

int launcher_should_exit(void) {
    if (s_quit_requested) return 1;
    if (s_first_ping_received) {
        DWORD now = GetTickCount();
        if (now - s_last_ping_tick > 15000) {
            printf("[launcher] Heartbeat timeout: no ping received for >15s. Shutting down.\n");
            return 1;
        }
    }
    return 0;
}

static int file_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void launcher_open_browser(const char *url) {
    if (!url || !url[0]) return;

    if (launcher_is_wine()) {
        // Candidate Linux browser executables in order of preference
        static const char *const s_linux_browsers[] = {
            "/usr/bin/google-chrome-stable",
            "/usr/bin/google-chrome",
            "/usr/bin/chromium",
            "/usr/bin/chromium-browser",
            "/usr/bin/brave-browser",
            "/usr/bin/microsoft-edge",
            NULL
        };

        const char *chosen = NULL;
        for (int i = 0; s_linux_browsers[i]; i++) {
            char z_path[MAX_PATH];
            snprintf(z_path, sizeof(z_path), "Z:%s", s_linux_browsers[i]);
            // Convert forward slashes to backslashes for GetFileAttributesA
            for (char *p = z_path; *p; p++) {
                if (*p == '/') *p = '\\';
            }
            if (file_exists(z_path)) {
                chosen = s_linux_browsers[i];
                break;
            }
        }

        if (chosen) {
            // Find unix home directory
            const char *home = getenv("WINEHOMEDIR");
            if (!home || !home[0]) home = getenv("HOME");
            if (!home || !home[0]) home = "/tmp";

            char params[2048];
            snprintf(params, sizeof(params),
                     "/c start /unix %s --app=\"%s\" --user-data-dir=\"%s/.cache/scriptorium-browser\" --window-size=1280,820",
                     chosen, url, home);

            printf("[launcher] Wine app-mode browser: %s\n", chosen);
            ShellExecuteA(NULL, "open", "cmd.exe", params, NULL, SW_HIDE);
            return;
        }

        // Fallback under Wine: winebrowser
        printf("[launcher] No native Linux browser found; using winebrowser.\n");
        ShellExecuteA(NULL, "open", "winebrowser", url, NULL, SW_SHOWNORMAL);
        return;
    }

    // Native Windows: look for Edge or Chrome
    char program_files_x86[MAX_PATH] = "";
    char program_files[MAX_PATH]     = "";
    char local_app_data[MAX_PATH]   = "";

    GetEnvironmentVariableA("ProgramFiles(x86)", program_files_x86, sizeof(program_files_x86));
    GetEnvironmentVariableA("ProgramFiles", program_files, sizeof(program_files));
    GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, sizeof(local_app_data));

    char edge1[MAX_PATH], edge2[MAX_PATH], chrome1[MAX_PATH], chrome2[MAX_PATH];
    snprintf(edge1, sizeof(edge1), "%s\\Microsoft\\Edge\\Application\\msedge.exe", program_files_x86);
    snprintf(edge2, sizeof(edge2), "%s\\Microsoft\\Edge\\Application\\msedge.exe", program_files);
    snprintf(chrome1, sizeof(chrome1), "%s\\Google\\Chrome\\Application\\chrome.exe", program_files);
    snprintf(chrome2, sizeof(chrome2), "%s\\Google\\Chrome\\Application\\chrome.exe", local_app_data);

    const char *win_browser = NULL;
    if (file_exists(edge1))        win_browser = edge1;
    else if (file_exists(edge2))   win_browser = edge2;
    else if (file_exists(chrome1)) win_browser = chrome1;
    else if (file_exists(chrome2)) win_browser = chrome2;

    if (win_browser) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "\"%s\" --app=\"%s\" --user-data-dir=\"%s\\Scriptorium\\browser\" --window-size=1280,820",
                 win_browser, url, local_app_data[0] ? local_app_data : ".");

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            printf("[launcher] Started Windows app window: %s\n", win_browser);
            return;
        }
    }

    // Fallback: default web browser tab
    printf("[launcher] Opening default browser via ShellExecute.\n");
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

void launcher_open_folder(const char *path) {
    if (!path || !path[0]) return;

    if (launcher_is_wine()) {
        // winebrowser can open file/folder paths
        ShellExecuteA(NULL, "open", "winebrowser", path, NULL, SW_SHOWNORMAL);
    } else {
        ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    }
}
