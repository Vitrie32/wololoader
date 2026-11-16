// Wololoader Setup for Windows: one exe with the files appended (no compression, ~2 MB).
//
// Payload at the end of this exe (written by tools/package.py):
//   [file data ...][index][u32 index_size]["WLPAYLD1"]
//   index entry: u8 flags, u16 path_len, path (relative, '/' separators), u32 offset, u32 size
//   flags: 1 = keep the existing file (mod.json: holds the user's enabled switch),
//          2 = example mod (only with "Install example mods" checked)
//
// Finds the game (Age of Empires II HD, "AoK HD.exe") in: this exe's folder, Steam libraries
// (registry SteamPath -> steamapps/libraryfolders.vdf -> appmanifest_221380.acf installdir), else the
// user picks the folder. Install writes version.dll and the wololoader folder; Uninstall moves them
// to the Recycle Bin (mods included, so they can be restored).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef WL_VERSION
#define WL_VERSION "0.3"
#endif
#define APP_TITLE L"Wololoader " TEXT(WL_VERSION) L" Setup"
#define GAME_EXE L"AoK HD.exe"

enum { ID_PATH = 100, ID_BROWSE, ID_EXAMPLES, ID_INSTALL, ID_UNINSTALL, ID_CLOSE, ID_STATUS };

typedef struct { unsigned char flags; char path[260]; unsigned int off, size; } Entry;

static unsigned char *g_payload;   // whole exe in memory
static Entry *g_entries;
static int g_nentries;
static HWND g_wnd, g_path, g_examples, g_status;
static HFONT g_font;

// ---------------------------------------------------------------- payload

static int load_payload(void) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    HANDLE f = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(f, NULL), got = 0;
    g_payload = malloc(size);
    int ok = g_payload && ReadFile(f, g_payload, size, &got, NULL) && got == size;
    CloseHandle(f);
    if (!ok || size < 12 || memcmp(g_payload + size - 8, "WLPAYLD1", 8) != 0) return 0;
    unsigned int isz;
    memcpy(&isz, g_payload + size - 12, 4);
    if (isz > size - 12) return 0;
    const unsigned char *p = g_payload + size - 12 - isz, *end = g_payload + size - 12;
    g_entries = calloc(4096, sizeof(Entry));
    while (p < end && g_nentries < 4096) {
        Entry *e = &g_entries[g_nentries];
        if (end - p < 3) return 0;
        e->flags = p[0];
        unsigned short n;
        memcpy(&n, p + 1, 2);
        p += 3;
        if (n >= sizeof e->path || end - p < n + 8) return 0;
        memcpy(e->path, p, n);
        e->path[n] = 0;
        p += n;
        memcpy(&e->off, p, 4);
        memcpy(&e->size, p + 4, 4);
        p += 8;
        if ((unsigned long long)e->off + e->size > size - 12 - isz) return 0;
        g_nentries++;
    }
    return g_nentries > 0;
}

// ---------------------------------------------------------------- finding the game

static int is_game_dir(const wchar_t *dir) {
    wchar_t p[MAX_PATH];
    if (!dir[0] || _snwprintf(p, MAX_PATH, L"%s\\%s", dir, GAME_EXE) >= MAX_PATH) return 0;
    p[MAX_PATH - 1] = 0;
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static char *read_text(const wchar_t *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *b = size < (8u << 20) ? malloc(size + 1) : NULL;
    if (b && (!ReadFile(f, b, size, &got, NULL) || got != size)) { free(b); b = NULL; }
    CloseHandle(f);
    if (b) b[size] = 0;
    return b;
}

// Value of the first `"key"  "value"` pair after `from` (VDF text, backslashes escaped as \\).
static int vdf_value(const char *from, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(from, pat);
    if (!k) return 0;
    const char *q = strchr(k + strlen(pat), '"');
    if (!q) return 0;
    size_t n = 0;
    for (q++; *q && *q != '"' && n + 1 < cap; q++) {
        if (*q == '\\' && q[1]) q++;
        out[n++] = *q;
    }
    out[n] = 0;
    return n > 0;
}

static int try_library(const wchar_t *lib, wchar_t *out) {
    wchar_t acf[MAX_PATH];
    _snwprintf(acf, MAX_PATH, L"%s\\steamapps\\appmanifest_221380.acf", lib);
    acf[MAX_PATH - 1] = 0;
    char *t = read_text(acf);
    char dir[260] = "Age2HD";
    if (t) { vdf_value(t, "installdir", dir, sizeof dir); free(t); }
    wchar_t wdir[260];
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 260);
    _snwprintf(out, MAX_PATH, L"%s\\steamapps\\common\\%s", lib, wdir);
    out[MAX_PATH - 1] = 0;
    return is_game_dir(out);
}

static int find_game(wchar_t *out) {
    // 1. next to this exe (Setup copied into the game folder)
    GetModuleFileNameW(NULL, out, MAX_PATH);
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) { *slash = 0; if (is_game_dir(out)) return 1; }
    // 2. Steam libraries
    wchar_t steam[MAX_PATH];
    DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, steam, &sz) != ERROR_SUCCESS) {
        sz = sizeof steam;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", RRF_RT_REG_SZ, NULL, steam, &sz) != ERROR_SUCCESS)
            return 0;
    }
    for (wchar_t *c = steam; *c; c++) if (*c == L'/') *c = L'\\';
    if (try_library(steam, out)) return 1;
    wchar_t vdf[MAX_PATH];
    _snwprintf(vdf, MAX_PATH, L"%s\\steamapps\\libraryfolders.vdf", steam);
    vdf[MAX_PATH - 1] = 0;
    char *t = read_text(vdf);
    int found = 0;
    for (const char *p = t; p && !found && (p = strstr(p, "\"path\"")) != NULL; p += 6) {
        char lib[260];
        if (!vdf_value(p, "path", lib, sizeof lib)) continue;
        wchar_t wlib[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, lib, -1, wlib, MAX_PATH);
        found = try_library(wlib, out);
    }
    free(t);
    return found;
}

// ---------------------------------------------------------------- install / uninstall

static void status(const wchar_t *fmt, ...) {
    wchar_t b[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(b, 1024, fmt, ap);
    va_end(ap);
    b[1023] = 0;
    SetWindowTextW(g_status, b);
    UpdateWindow(g_status);
}

static int make_dirs_for(wchar_t *path) {   // creates every parent folder of `path`
    for (wchar_t *c = path + 3; *c; c++) {
        if (*c != L'\\') continue;
        *c = 0;
        CreateDirectoryW(path, NULL);
        *c = L'\\';
    }
    return 1;
}

// The game holds version.dll open while it runs: if we cannot open it for writing, it is running.
static int game_running(const wchar_t *game) {
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s\\version.dll", game);
    p[MAX_PATH - 1] = 0;
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { CloseHandle(f); return 0; }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

static int get_game(wchar_t *game) {
    GetWindowTextW(g_path, game, MAX_PATH);
    size_t n = wcslen(game);
    while (n && (game[n - 1] == L'\\' || game[n - 1] == L'/' || game[n - 1] == L' ')) game[--n] = 0;
    if (!is_game_dir(game)) {
        MessageBoxW(g_wnd, L"That folder does not contain \"AoK HD.exe\".\n\nPick the Age of Empires II HD folder "
                           L"(Steam: right-click the game > Manage > Browse local files).", APP_TITLE, MB_ICONWARNING);
        return 0;
    }
    if (game_running(game)) {
        MessageBoxW(g_wnd, L"Age of Empires II HD is running. Close it first.", APP_TITLE, MB_ICONWARNING);
        return 0;
    }
    return 1;
}

// ASLR: AoK HD.exe has DYNAMIC_BASE (DllCharacteristics 0x40) and relocations, so real Windows loads
// it at a random address, while Wololoader patches fixed addresses of the 0x400000 image (Wine/Proton
// always uses 0x400000). Install clears that one flag so Windows loads it at 0x400000 too; the first
// time, the exe is copied to wololoader\backup\. Uninstall sets the flag again.
// Returns 1 changed, 0 already as wanted, -1 error (err filled).
static int exe_aslr(const wchar_t *game, int enable, wchar_t *err, size_t errcap) {
    wchar_t exe[MAX_PATH];
    _snwprintf(exe, MAX_PATH, L"%s\\%s", game, GAME_EXE);
    exe[MAX_PATH - 1] = 0;
    // 1. read the header (read-only: CopyFile below must be able to open the exe too)
    HANDLE f = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        _snwprintf(err, errcap, L"Cannot open %s (error %lu).", exe, GetLastError());
        return -1;
    }
    unsigned char h[1024];
    DWORD got = 0;
    BOOL rd = ReadFile(f, h, sizeof h, &got, NULL);
    CloseHandle(f);
    if (!rd || got < 0x200) { _snwprintf(err, errcap, L"Cannot read %s.", exe); return -1; }
    DWORD pe = *(DWORD *)(h + 0x3c);
    if (pe + 24 + 72 > got || memcmp(h + pe, "PE\0\0", 4) != 0 || *(WORD *)(h + pe + 24) != 0x10b) {
        _snwprintf(err, errcap, L"%s is not the expected 32-bit game exe.", exe);
        return -1;
    }
    DWORD at = pe + 24 + 70;                       // IMAGE_OPTIONAL_HEADER32.DllCharacteristics
    WORD dc = *(WORD *)(h + at), want = enable ? (WORD)(dc | 0x40) : (WORD)(dc & ~0x40);
    if (dc == want) return 0;
    // 2. first change: keep a copy of the untouched exe
    if (!enable) {
        wchar_t bak[MAX_PATH];
        _snwprintf(bak, MAX_PATH, L"%s\\wololoader\\backup\\%s", game, GAME_EXE);
        bak[MAX_PATH - 1] = 0;
        make_dirs_for(bak);
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES && !CopyFileW(exe, bak, TRUE)) {
            _snwprintf(err, errcap, L"Cannot back up %s (error %lu).", exe, GetLastError());
            return -1;
        }
    }
    // 3. write the two bytes
    f = CreateFileW(exe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        _snwprintf(err, errcap, L"Cannot open %s for writing (error %lu).", exe, GetLastError());
        return -1;
    }
    DWORD put = 0;
    BOOL ok = SetFilePointer(f, (LONG)at, NULL, FILE_BEGIN) == at && WriteFile(f, &want, 2, &put, NULL) && put == 2;
    DWORD e = GetLastError();
    CloseHandle(f);
    if (!ok) { _snwprintf(err, errcap, L"Cannot write %s (error %lu).", exe, e); return -1; }
    return 1;
}

static void do_install(void) {
    wchar_t game[MAX_PATH];
    if (!get_game(game)) return;
    int examples = SendMessageW(g_examples, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int written = 0, kept = 0;
    for (int i = 0; i < g_nentries; i++) {
        const Entry *e = &g_entries[i];
        if ((e->flags & 2) && !examples) continue;
        wchar_t rel[260], dst[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, e->path, -1, rel, 260);
        for (wchar_t *c = rel; *c; c++) if (*c == L'/') *c = L'\\';
        if (_snwprintf(dst, MAX_PATH, L"%s\\%s", game, rel) >= MAX_PATH) continue;
        dst[MAX_PATH - 1] = 0;
        if ((e->flags & 1) && GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) { kept++; continue; }
        make_dirs_for(dst);
        status(L"Writing %s", rel);
        HANDLE f = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD put = 0;
        int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, g_payload + e->off, e->size, &put, NULL) && put == e->size;
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        if (!ok) {
            wchar_t msg[600];
            _snwprintf(msg, 600, L"Could not write\n%s\n\n(error %lu). Is the folder write-protected? Try running Setup as administrator.",
                       dst, GetLastError());
            msg[599] = 0;
            MessageBoxW(g_wnd, msg, APP_TITLE, MB_ICONERROR);
            status(L"Install failed.");
            return;
        }
        written++;
    }
    wchar_t err[600];
    if (exe_aslr(game, 0, err, 600) < 0) {
        wchar_t msg[800];
        _snwprintf(msg, 800, L"The files are installed, but the game exe could not be prepared:\n%s\n\n"
                             L"Without it Windows loads the game at a random address and Wololoader stays off. "
                             L"Try running Setup as administrator.", err);
        msg[799] = 0;
        MessageBoxW(g_wnd, msg, APP_TITLE, MB_ICONERROR);
        status(L"Installed, but the exe was not prepared.");
        return;
    }
    status(L"Installed: %d files (%d settings kept).", written, kept);
    MessageBoxW(g_wnd, L"Wololoader is installed.\n\nStart Age of Empires II HD: the main menu shows "
                       L"\"Wololoader " TEXT(WL_VERSION) L"\" and the Mods button opens the mod list.",
                APP_TITLE, MB_ICONINFORMATION);
}

static void do_uninstall(void) {
    wchar_t game[MAX_PATH];
    if (!get_game(game)) return;
    if (MessageBoxW(g_wnd, L"Move version.dll and the wololoader folder (with all mods in it) to the Recycle Bin?\n\n"
                       L"The game exe gets its original header flag back.",
                    APP_TITLE, MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    wchar_t err[600];
    if (exe_aslr(game, 1, err, 600) < 0) {   // give the exe its ASLR flag back
        MessageBoxW(g_wnd, err, APP_TITLE, MB_ICONERROR);
        return;
    }
    // SHFileOperation wants a double-NUL-terminated list
    wchar_t list[2 * MAX_PATH + 4] = { 0 };
    int n = _snwprintf(list, MAX_PATH, L"%s\\version.dll", game) + 1;
    n += _snwprintf(list + n, MAX_PATH, L"%s\\wololoader", game) + 1;
    list[n] = 0;
    SHFILEOPSTRUCTW op = { 0 };
    op.hwnd = g_wnd;
    op.wFunc = FO_DELETE;
    op.pFrom = list;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    int r = SHFileOperationW(&op);
    status(r == 0 && !op.fAnyOperationsAborted ? L"Uninstalled (moved to the Recycle Bin)." : L"Uninstall did not finish.");
}

static void browse(void) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = L"Pick the Age of Empires II HD folder (the one with AoK HD.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t p[MAX_PATH];
    if (SHGetPathFromIDListW(id, p)) SetWindowTextW(g_path, p);
    CoTaskMemFree(id);
}

// ---------------------------------------------------------------- window

static HWND add(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = h;
        NONCLIENTMETRICSW ncm;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = sizeof ncm;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        add(L"STATIC", L"Mod loader for Age of Empires II HD (Steam, version 5.8).\n"
                       L"Game folder:", 0, 16, 14, 470, 34, 0);
        g_path = add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 16, 52, 380, 24, ID_PATH);
        add(L"BUTTON", L"Browse...", WS_TABSTOP, 404, 51, 82, 26, ID_BROWSE);
        g_examples = add(L"BUTTON", L"Install example mods (Monklanians civ, Dev Panel)", BS_AUTOCHECKBOX | WS_TABSTOP,
                         16, 86, 470, 22, ID_EXAMPLES);
        SendMessageW(g_examples, BM_SETCHECK, BST_CHECKED, 0);
        g_status = add(L"STATIC", L"", 0, 16, 116, 470, 20, ID_STATUS);
        add(L"BUTTON", L"Install", BS_DEFPUSHBUTTON | WS_TABSTOP, 220, 146, 86, 28, ID_INSTALL);
        add(L"BUTTON", L"Uninstall", WS_TABSTOP, 312, 146, 86, 28, ID_UNINSTALL);
        add(L"BUTTON", L"Close", WS_TABSTOP, 404, 146, 82, 28, ID_CLOSE);
        wchar_t game[MAX_PATH];
        if (find_game(game)) { SetWindowTextW(g_path, game); status(L"Found the game."); }
        else status(L"Game not found automatically: pick its folder with Browse.");
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_BROWSE: browse(); break;
        case ID_INSTALL: do_install(); break;
        case ID_UNINSTALL: do_uninstall(); break;
        case ID_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd;
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!load_payload()) {
        MessageBoxW(NULL, L"This setup file is damaged (no files inside). Download it again.", APP_TITLE, MB_ICONERROR);
        return 1;
    }
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"WololoaderSetup";
    RegisterClassW(&wc);
    RECT r = { 0, 0, 502, 190 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    HWND h = CreateWindowW(wc.lpszClassName, APP_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(h, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
