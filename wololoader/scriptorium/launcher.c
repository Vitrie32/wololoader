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
#include <tlhelp32.h>
#include <ctype.h>
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
            // Find unix home directory and sanitize \??\Z:\... into clean Unix path
            const char *raw_home = getenv("WINEHOMEDIR");
            if (!raw_home || !raw_home[0]) raw_home = getenv("HOME");
            char unix_home[MAX_PATH] = "/tmp";
            if (raw_home && raw_home[0]) {
                const char *p = raw_home;
                if (!strncmp(p, "\\??\\", 4)) p += 4;
                if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;
                snprintf(unix_home, sizeof(unix_home), "%s", p);
                for (char *c = unix_home; *c; c++) {
                    if (*c == '\\') *c = '/';
                }
            }

            char params[2048];
            snprintf(params, sizeof(params),
                     "/c start /unix %s --app=\"%s\" --user-data-dir=\"%s/.cache/scriptorium-browser\" --window-size=1280,820",
                     chosen, url, unix_home);

            printf("[launcher] Wine app-mode browser: %s (home: %s)\n", chosen, unix_home);
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

int launcher_is_game_running(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "AoK HD.exe") == 0) {
                    CloseHandle(snap);
                    return 1;
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (FindWindowA(NULL, "Age of Empires II: HD Edition") != NULL) {
        return 1;
    }
    return 0;
}

// Many installs run AoK HD.exe as a "non-Steam game" shortcut (Steam + Proton
// on Linux), not as Steam app 221380: launching 221380 then fails with
// "No licenses" (seen 2026-11-24). Steam stores shortcuts in the binary file
// userdata/<user>/config/shortcuts.vdf; each entry has "\x02appid\0" + u32 and
// "\x01Exe\0" + string. The launch id is (appid << 32) | 0x02000000.
// Returns 1 and writes the URI when a shortcut whose Exe names AoK HD.exe exists.
static int find_shortcut_uri(char *uri, size_t cap) {
    char bases[4][MAX_PATH];
    int nb = 0;
    if (launcher_is_wine()) {
        const char *raw_home = getenv("WINEHOMEDIR");
        if (!raw_home || !raw_home[0]) raw_home = getenv("HOME");
        if (!raw_home || !raw_home[0]) return 0;
        const char *p = raw_home;
        if (!strncmp(p, "\\??\\", 4)) p += 4;
        if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;
        // Read the Linux files through Wine's Z: drive.
        snprintf(bases[nb++], MAX_PATH, "Z:%s\\.local\\share\\Steam\\userdata", p);
        snprintf(bases[nb++], MAX_PATH, "Z:%s\\.steam\\steam\\userdata", p);
        for (int i = 0; i < nb; i++)
            for (char *c = bases[i]; *c; c++) if (*c == '/') *c = '\\';
    } else {
        char steam[MAX_PATH]; DWORD sz = sizeof steam;
        if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath",
                         RRF_RT_REG_SZ, NULL, steam, &sz) == ERROR_SUCCESS) {
            snprintf(bases[nb++], MAX_PATH, "%s\\userdata", steam);
        }
    }
    for (int b = 0; b < nb; b++) {
        char pat[MAX_PATH + 4];
        snprintf(pat, sizeof pat, "%s\\*", bases[b]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            char path[MAX_PATH * 2];
            snprintf(path, sizeof path, "%s\\%s\\config\\shortcuts.vdf", bases[b], fd.cFileName);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            static unsigned char buf[1 << 20];
            size_t n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            unsigned appid = 0; int have_id = 0;
            for (size_t i = 0; i + 11 <= n; i++) {
                // Key names are case-insensitive in practice ("appid"/"AppId", "Exe"/"exe").
                if (buf[i] == 0x02 && !_strnicmp((const char *)buf + i + 1, "appid", 5) && buf[i + 6] == 0) {
                    memcpy(&appid, buf + i + 7, 4);
                    have_id = 1;
                } else if (have_id && buf[i] == 0x01 && !_strnicmp((const char *)buf + i + 1, "exe", 3) && buf[i + 4] == 0) {
                    const char *exe = (const char *)buf + i + 5;
                    size_t len = strnlen(exe, n - (i + 5));
                    char low[MAX_PATH] = "";
                    for (size_t k = 0; k < len && k + 1 < sizeof low; k++) { low[k] = (char)tolower((unsigned char)exe[k]); low[k + 1] = 0; }
                    if (strstr(low, "aok hd.exe")) {
                        unsigned long long gid = ((unsigned long long)appid << 32) | 0x02000000ULL;
                        snprintf(uri, cap, "steam://rungameid/%llu", gid);
                        FindClose(h);
                        return 1;
                    }
                    have_id = 0;   // the next entry starts with its own appid
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return 0;
}

void launcher_launch_steam_game(void) {
    char shortcut[64];
    const char *uri = "steam://rungameid/221380";   // the game as a regular Steam app
    if (find_shortcut_uri(shortcut, sizeof shortcut)) {
        uri = shortcut;
        printf("[launcher] Found AoK HD.exe as a non-Steam shortcut.\n");
    }
    if (launcher_is_wine()) {
        printf("[launcher] Launching Steam game via winebrowser: %s\n", uri);
        ShellExecuteA(NULL, "open", "winebrowser", uri, NULL, SW_SHOWNORMAL);
    } else {
        printf("[launcher] Launching Steam game via ShellExecute: %s\n", uri);
        ShellExecuteA(NULL, "open", uri, NULL, NULL, SW_SHOWNORMAL);
    }
}
