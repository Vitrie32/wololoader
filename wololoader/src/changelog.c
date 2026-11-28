// "What's new" window and Wololoader self-update.
//
// 1. Changelog window: opened by a click on the main menu label (menu.c). A ScreenPanel built
//    like the Mod Hub screen (ui_hub.c) with one scrolling TextPanel made the way the History
//    screen makes its body text (HistoryScreen ctor 0x6cc830, 0x6ccf86..0x6cd11b):
//      0x48fe60 text panel (wrap on) -> 0x495560 four fonts (normal, bold, italic, bold italic:
//      0x1a, 0x21, 0x22, 0x23 from the font table [0xada2a8], 0x472020) -> 0x48d840 scrollbar.
//    The text is <game>\wololoader\changelog.txt (CHANGELOG.md, copied by `make install` and
//    the release), turned into lines by mdtext.c and set with the game's markup path
//    (0x773f70 parse_markup + 0x4955c0, as history.c): headings are "<b>...<b>" (toggle tag).
//    Wheel and scrollbar scroll the panel natively; the Top button uses 0x496d00.
//
// 2. Update check: on the main menu (wl_update_check, called by menu.c) a thread asks GitHub
//    for the latest release (api.github.com/repos/Vitrie32/wololoader/releases/latest; WinHTTP
//    in hubcore.c, generic User-Agent, nothing about the player is sent). A newer tag_name than
//    WOLOLOADER_VERSION makes the label say so, and the window shows the release notes on top
//    with an "Update now" button. Failures (offline, rate limit) are only logged.
//
// 3. Update now (only on the player's click): downloads wololoader-windows-setup.exe into
//    <game>\wololoader\update\, checks it (PE header, i386, the setup's "WLPAYLD1" trailer,
//    size as GitHub listed it), and starts it with
//        --update --wait-pid <this process> --game "<game dir>"
//    The setup (installer/wlsetup.c) waits for the game to exit, installs into that folder
//    without questions and says "Wololoader updated to vY". Under Proton the setup runs in the
//    same Wine prefix as the game. "Quit and update" then leaves through the game's own Exit
//    path (main menu Exit handler 0x6e90c0, see FINDINGS "Changelog window and self-update").
#include "wololoader.h"
#include "hubcore.h"
#include "json.h"
#include "mdtext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDR_GAME_NEW             0x8d47df  // game's operator new (cdecl)
#define ADDR_SCREEN_CTOR          0x489d50  // ScreenPanel(this, const char *name), ret 4
#define SCREENPANEL_VTBL          0x9b24ac  // 59 slots
#define SCREENPANEL_SIZE          0x27c
#define VT_SLOTS                  59
#define VT_TICK                   17        // +0x44, __thiscall(self), plain ret
#define VT_EVENT                  49        // +0xc4, (panel, code, a, b), ret 0x10
#define ADDR_SCREEN_MGR           0xada4b4  // [ptr] screen list
#define ADDR_SET_CURRENT          0x48a8e0  // thiscall(mgr, screen), ret 4
#define ADDR_SET_BACKGROUND       0x48a1c0  // thiscall(scr, name, id, 0), ret 0xc
#define ADDR_SET_REF_SIZE         0x491250  // thiscall(scr, int wh[2]), ret 4
#define ADDR_ADD_BUTTON           0x48df30  // thiscall, ret 0x28
#define ADDR_ADD_LABEL            0x48fe60  // thiscall, ret 0x2c
#define ADDR_SET_TEXT_STYLE       0x48d390
#define ADDR_SET_TEXT_FLAG        0x48d3c0
#define ADDR_APPLY_STYLE          0x48d550
#define ADDR_STYLE_PAIR           0xada500
#define ADDR_BUTTON_SET_TEXT      0x479ce0  // thiscall(btn, int slot, const char *text), ret 8
#define ADDR_DEFER                0x4382e0  // cdecl(std::function *), deferred call queue
#define ADDR_DELETE_FUNCTOR_VTBL  0x9d7e18  // functor: screen->vtbl[0](1)

// Verified 2026-11-28 in the disassembly (see re/FINDINGS.md "Changelog window and self-update").
#define ADDR_FONT_TABLE           0xada2a8  // [ptr] font table; 0x472020(table, id) -> Font*
#define ADDR_FONT_GET             0x472020  // thiscall(table, int id) -> Font*, ret 4 (no ref taken)
#define ADDR_TEXT_SET_FONTS       0x495560  // thiscall(textpanel, Font *f[4]), ret 4 (takes refs)
#define ADDR_ADD_SCROLLBAR        0x48d840  // thiscall(screen, textpanel) -> scrollbar, ret 4
#define ADDR_TEXT_SCROLL          0x496d00  // thiscall(textpanel, int mode, int n, int flag), ret 0xc
#define ADDR_PARSE_MARKUP         0x773f70  // cdecl(out24, const char *text, int, int)
#define ADDR_SET_FORMATTED_TEXT   0x4955c0  // thiscall(textpanel, out24), ret 4
#define ADDR_VECTOR_DTOR          0x474a80  // thiscall(out24 + 12)
#define ADDR_STRING_DTOR          0x4339a0  // thiscall(out24 + 0)
#define ADDR_MAINMENU_EXIT        0x6e90c0  // __fastcall(MainMenuScreen * or NULL), plain ret

#define EV_LEFT_CLICK             3
#define REF_W                     1366
#define REF_H                     768
#define TEXT_X                    233
#define TEXT_Y                    70
#define TEXT_W                    880
#define TEXT_H                    560
#define WRAP_CHARS                100       // mdtext wrap width; the panel wraps longer lines itself

#define UPDATE_API_URL  "https://api.github.com/repos/Vitrie32/wololoader/releases/latest"
#define UPDATE_ASSET    "wololoader-windows-setup.exe"
#define UPDATE_URL_OK   "https://github.com/Vitrie32/wololoader/releases/download/"
#define SETUP_MIN_BYTES (200u * 1024u)
#define SETUP_MAX_BYTES (64u * 1024u * 1024u)

typedef void *(__cdecl *New_t)(unsigned size);
typedef void *(__thiscall *ScreenCtor_t)(void *self, const char *name);
typedef void (__thiscall *SetCurrent_t)(void *mgr, void *screen);
typedef void (__thiscall *SetBg_t)(void *scr, const char *name, int id, int z);
typedef void (__thiscall *SetRef_t)(void *scr, int *wh);
typedef void *(__thiscall *AddButton_t)(void *scr, void *parent, const char *text, const char *text2,
                                         int x, int y, int w, int h, int a, int b, int c);
typedef char (__thiscall *AddLabel_t)(void *scr, void *parent, void **out, const char *text,
                                      int x, int y, int w, int h, int font, int f24, int f28, int f2c);
typedef void (__thiscall *SetTextStyle_t)(void *scr, DWORD *a, DWORD *b);
typedef void (__thiscall *SetTextFlag_t)(void *scr, int flag);
typedef void (__thiscall *ApplyStyle_t)(void *scr, void *panel);
typedef void (__thiscall *Tick_t)(void *self);
typedef int (__thiscall *Event_t)(void *self, void *panel, int code, int a, int b);
typedef void (__thiscall *ButtonSetText_t)(void *btn, int slot, const char *text);
typedef void (__thiscall *PanelSetVisible_t)(void *panel, int visible);
typedef void (__thiscall *TextPanelSetText_t)(void *panel, const char *text, int z0, int z1, int z2);
typedef void (__cdecl *Defer_t)(void *fn);
typedef void (__thiscall *FnDestroy_t)(void *fn, int free_it);
typedef void *(__thiscall *FontGet_t)(void *table, int id);
typedef void (__thiscall *SetFonts_t)(void *panel, void **fonts);
typedef void *(__thiscall *AddScrollbar_t)(void *screen, void *panel);
typedef void (__thiscall *TextScroll_t)(void *panel, int mode, int n, int flag);
typedef void *(__cdecl *ParseMarkup_t)(void *out, const char *text, int a, int b);
typedef void (__thiscall *SetFormattedText_t)(void *panel, void *out);
typedef void (__thiscall *Dtor_t)(void *obj);
typedef void (__fastcall *MainMenuExit_t)(void *main_menu);

// ---------------------------------------------------------------------------
// Update state, shared between the worker threads and the UI (main) thread.

enum { UPD_UNKNOWN = 0, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_FAILED };
enum { DL_IDLE = 0, DL_RUNNING, DL_READY, DL_FAILED };

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_once;          // 0 not yet, 1 initialising, 2 ready
static volatile LONG g_upd_state;        // UPD_*
static char  g_upd_version[32];          // "0.7.3" (written before UPD_AVAILABLE is published)
static char  g_upd_url[1024];
static unsigned g_upd_size;              // asset size from GitHub (0 = unknown)
static char *g_upd_notes;                // release notes (Markdown), malloc'd

static volatile LONG g_dl_state;         // DL_*
static volatile LONG g_dl_done, g_dl_total;
static char  g_dl_error[256];            // under g_cs

static void ensure_cs(void) {
    if (InterlockedCompareExchange(&g_cs_once, 1, 0) == 0) {
        InitializeCriticalSection(&g_cs);
        InterlockedExchange(&g_cs_once, 2);
    }
    while (g_cs_once != 2) Sleep(0);
}

// Only digits, letters, '.', '-': the tag goes into a label and a file name.
static int plausible_version(const char *v) {
    if (!v || !*v || strlen(v) >= sizeof g_upd_version) return 0;
    for (const char *p = v; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-'))
            return 0;
    return *v >= '0' && *v <= '9';
}

static DWORD WINAPI check_worker(LPVOID arg) {
    (void)arg;
    // A setup downloaded in an earlier session has done its work by now (it waited for that
    // game to exit): remove it. A still running one cannot be deleted, which is harmless.
    char old[MAX_PATH];
    snprintf(old, sizeof old, "%s\\update\\%s", g_wl.root, UPDATE_ASSET);
    DeleteFileA(old);
    snprintf(old, sizeof old, "%s\\update\\%s.part", g_wl.root, UPDATE_ASSET);
    DeleteFileA(old);
    char err[256] = "", *buf = NULL;
    size_t len = 0;
    if (hub_fetch_stream(UPDATE_API_URL, 1024 * 1024, NULL, &buf, &len, NULL, NULL, NULL, err, sizeof err) != 0) {
        wl_log("update: check failed: %s", err);
        InterlockedExchange(&g_upd_state, UPD_FAILED);
        return 0;
    }
    char perr[128] = "";
    WlJsonVal *root = wl_json_parse(buf, perr, sizeof perr);
    free(buf);
    if (!root) {
        wl_log("update: check failed: bad JSON (%s)", perr);
        InterlockedExchange(&g_upd_state, UPD_FAILED);
        return 0;
    }
    const char *tag = wl_json_str(wl_json_get(root, "tag_name"), "");
    if (*tag == 'v' || *tag == 'V') tag++;
    const char *url = NULL;
    double size = 0;
    const WlJsonVal *assets = wl_json_get(root, "assets");
    for (size_t i = 0; assets && i < wl_json_len(assets); i++) {
        const WlJsonVal *a = wl_json_at(assets, i);
        if (strcmp(wl_json_str(wl_json_get(a, "name"), ""), UPDATE_ASSET) != 0) continue;
        url = wl_json_str(wl_json_get(a, "browser_download_url"), NULL);
        size = wl_json_num(wl_json_get(a, "size"), 0);
    }
    int newer = plausible_version(tag) && hub_compare_versions(tag, WOLOLOADER_VERSION) > 0;
    if (!plausible_version(tag)) {
        wl_log("update: check failed: unexpected tag_name");
        InterlockedExchange(&g_upd_state, UPD_FAILED);
    } else if (!newer) {
        wl_log("update: v%s is the latest release (this is v%s)", tag, WOLOLOADER_VERSION);
        InterlockedExchange(&g_upd_state, UPD_CURRENT);
    } else if (!url || strncmp(url, UPDATE_URL_OK, strlen(UPDATE_URL_OK)) != 0 || strlen(url) >= sizeof g_upd_url) {
        // Only the project's own release downloads are accepted.
        wl_log("update: v%s is out, but it has no usable %s", tag, UPDATE_ASSET);
        InterlockedExchange(&g_upd_state, UPD_FAILED);
    } else {
        const char *notes = wl_json_str(wl_json_get(root, "body"), "");
        size_t nl = strlen(notes);
        if (nl > 64 * 1024) nl = 64 * 1024;
        char *copy = malloc(nl + 1);
        if (copy) { memcpy(copy, notes, nl); copy[nl] = 0; }
        lstrcpynA(g_upd_version, tag, sizeof g_upd_version);
        lstrcpynA(g_upd_url, url, sizeof g_upd_url);
        g_upd_size = (size > 0 && size < 4e9) ? (unsigned)size : 0;
        g_upd_notes = copy;
        wl_log("update: v%s available (%u bytes): %s", g_upd_version, g_upd_size, g_upd_url);
        InterlockedExchange(&g_upd_state, UPD_AVAILABLE);   // publishes the fields above
    }
    wl_json_free(root);
    return 0;
}

void wl_update_check(void) {
    if (InterlockedCompareExchange(&g_upd_state, UPD_CHECKING, UPD_UNKNOWN) != UPD_UNKNOWN) return;
    HANDLE h = CreateThread(NULL, 0, check_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_upd_state, UPD_FAILED);
}

const char *wl_update_version(void) {
    return g_upd_state == UPD_AVAILABLE ? g_upd_version : NULL;
}

// ---- download + start the setup -------------------------------------------

static void dl_fail(const char *fmt, const char *detail) {
    ensure_cs();
    EnterCriticalSection(&g_cs);
    snprintf(g_dl_error, sizeof g_dl_error, fmt, detail ? detail : "");
    LeaveCriticalSection(&g_cs);
    wl_log("update: %s", g_dl_error);
    InterlockedExchange(&g_dl_state, DL_FAILED);
}

static void dl_progress(const char *stage, const char *id, size_t done, size_t total, void *user) {
    (void)stage; (void)id; (void)user;
    InterlockedExchange(&g_dl_done, (LONG)done);
    InterlockedExchange(&g_dl_total, (LONG)total);
}

// A Wololoader setup: 32-bit PE whose last 8 bytes are the payload magic (installer/wlsetup.c).
static const char *check_setup(const char *path, unsigned expect_size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return "cannot read the download";
    DWORD size = GetFileSize(f, NULL), got = 0;
    unsigned char head[1024], tail[8];
    BOOL ok = ReadFile(f, head, sizeof head, &got, NULL) && got == sizeof head;
    DWORD got2 = 0;
    if (ok) ok = SetFilePointer(f, -8, NULL, FILE_END) != INVALID_SET_FILE_POINTER &&
                 ReadFile(f, tail, 8, &got2, NULL) && got2 == 8;
    CloseHandle(f);
    if (!ok || size < SETUP_MIN_BYTES || size > SETUP_MAX_BYTES) return "the download has an unexpected size";
    if (expect_size && size != expect_size) return "the download is incomplete";
    if (head[0] != 'M' || head[1] != 'Z') return "the download is not a Windows program";
    DWORD pe = *(DWORD *)(head + 0x3c);
    if (pe > sizeof head - 6 || memcmp(head + pe, "PE\0\0", 4) != 0 || *(WORD *)(head + pe + 4) != 0x14c)
        return "the download is not a Windows program";
    if (memcmp(tail, "WLPAYLD1", 8) != 0) return "the download is not a Wololoader setup";
    return NULL;
}

static DWORD WINAPI download_worker(LPVOID arg) {
    (void)arg;
    char dir[MAX_PATH], part[MAX_PATH], exe[MAX_PATH], err[256] = "";
    snprintf(dir, sizeof dir, "%s\\update", g_wl.root);
    snprintf(part, sizeof part, "%s\\%s.part", dir, UPDATE_ASSET);
    snprintf(exe, sizeof exe, "%s\\%s", dir, UPDATE_ASSET);
    CreateDirectoryA(dir, NULL);
    DeleteFileA(part);
    DeleteFileA(exe);
    FILE *fp = fopen(part, "wb");
    if (!fp) { dl_fail("cannot write %s", part); return 0; }
    wl_log("update: downloading %s", g_upd_url);
    int r = hub_fetch_stream(g_upd_url, SETUP_MAX_BYTES, fp, NULL, NULL, dl_progress, NULL, NULL, err, sizeof err);
    if (fclose(fp) != 0 && r == 0) { r = -1; lstrcpynA(err, "disk write error", sizeof err); }
    if (r != 0) { DeleteFileA(part); dl_fail("download failed: %s", err); return 0; }
    const char *bad = check_setup(part, g_upd_size);
    if (bad) { DeleteFileA(part); dl_fail("%s", bad); return 0; }
    if (!MoveFileExA(part, exe, MOVEFILE_REPLACE_EXISTING)) { dl_fail("cannot rename %s", part); return 0; }

    // The setup waits for this process to end, then installs into the game folder.
    char game[MAX_PATH];
    lstrcpynA(game, g_wl.game_dir, sizeof game);
    size_t n = strlen(game);
    while (n > 3 && (game[n - 1] == '\\' || game[n - 1] == '/')) game[--n] = 0;   // `\"` would end the quote
    char cmd[3 * MAX_PATH];
    snprintf(cmd, sizeof cmd, "\"%s\" --update --wait-pid %lu --game \"%s\"", exe, GetCurrentProcessId(), game);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        char e[32];
        snprintf(e, sizeof e, "%lu", GetLastError());
        dl_fail("cannot start the setup (error %s)", e);
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    wl_log("update: setup started (pid %lu): %s", pi.dwProcessId, cmd);
    InterlockedExchange(&g_dl_state, DL_READY);
    return 0;
}

static void start_download(void) {
    if (g_upd_state != UPD_AVAILABLE) return;
    LONG s = g_dl_state;
    if (s == DL_RUNNING || s == DL_READY) return;
    if (InterlockedCompareExchange(&g_dl_state, DL_RUNNING, s) != s) return;
    InterlockedExchange(&g_dl_done, 0);
    InterlockedExchange(&g_dl_total, 0);
    HANDLE h = CreateThread(NULL, 0, download_worker, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else dl_fail("cannot start the download%s", "");
}

// ---------------------------------------------------------------------------
// The window

static void *g_screen;
static void *g_vtable_block[VT_SLOTS + 1];   // [0] = RTTI slot (dynamic_cast reads vtable[-1])
#define g_vtable (g_vtable_block + 1)
static Tick_t  g_orig_tick;
static Event_t g_orig_event;

static void *g_text, *g_status, *g_btn_close, *g_btn_update, *g_btn_top;
static int   g_shown_dl = -1;       // last DL_* state shown
static LONG  g_shown_done = -1;
static int   g_text_has_update;     // the window was built with the release notes
static char  g_status_text[256];

static void *add_label(void *s, const char *text, int x, int y, int w, int h,
                       int font, int halign, int valign, int wrap) {
    DWORD pair[2] = { ((DWORD *)ADDR_STYLE_PAIR)[0], ((DWORD *)ADDR_STYLE_PAIR)[1] };
    DWORD pair2[2] = { pair[0], pair[1] };
    ((SetTextStyle_t)ADDR_SET_TEXT_STYLE)(s, pair, pair2);
    ((SetTextFlag_t)ADDR_SET_TEXT_FLAG)(s, 0);
    void *label = NULL;
    ((AddLabel_t)ADDR_ADD_LABEL)(s, s, &label, text ? text : "", x, y, w, h, font, halign, valign, wrap);
    if (label) ((ApplyStyle_t)ADDR_APPLY_STYLE)(s, label);
    return label;
}

static void *add_button(void *s, const char *text, int x, int y, int w, int h) {
    return ((AddButton_t)ADDR_ADD_BUTTON)(s, s, text, NULL, x, y, w, h, 0, 0, 0);
}

static void panel_set_text(void *panel, const char *text) {
    if (panel) ((TextPanelSetText_t)(*(void ***)panel)[54])(panel, text ? text : "", 0, 0, 0);
}

static void panel_set_visible(void *panel, int visible) {
    if (panel) ((PanelSetVisible_t)(*(void ***)panel)[4])(panel, visible ? 1 : 0);
}

static void button_set_text(void *btn, const char *text) {
    if (btn) ((ButtonSetText_t)ADDR_BUTTON_SET_TEXT)(btn, 0, text ? text : "");
}

// Same as history.c set_body_formatted_text: parse "<b>" etc. and give the panel the styled text.
static void set_formatted_text(void *panel, const char *text) {
    if (!panel) return;
    char out[24];   // { 12-byte std::string, 12-byte std::vector of style spans } (0x48ff40 does the same)
    memset(out, 0, sizeof out);
    ((ParseMarkup_t)ADDR_PARSE_MARKUP)(out, text ? text : "", 0, 0);
    ((SetFormattedText_t)ADDR_SET_FORMATTED_TEXT)(panel, out);
    ((Dtor_t)ADDR_VECTOR_DTOR)(out + 12);
    ((Dtor_t)ADDR_STRING_DTOR)(out + 0);
}

// Same as ui_hub.c: delete the screen from the game's deferred queue (0x4382e0 / 0x9d7e18).
static void defer_delete_screen(void *screen) {
    DWORD f[10];
    memset(f, 0, sizeof f);
    f[0] = ADDR_DELETE_FUNCTOR_VTBL;
    f[1] = (DWORD)screen;
    f[9] = (DWORD)f;
    ((Defer_t)ADDR_DEFER)(f);
    void *impl = (void *)f[9];
    if (impl) ((FnDestroy_t)(*(void ***)impl)[4])(impl, impl != (void *)f);
}

static void lines_append(MdLines *l, const char *text) {
    md_add_line(l, text);
}

// The whole text of the window: release notes (when an update is out), then the changelog.
static char *build_text(int with_update) {
    MdLines l = { 0 };
    if (with_update) {
        char h[96];
        snprintf(h, sizeof h, "<b>Update available: Wololoader v%s (you have v%s)<b>", g_upd_version, WOLOLOADER_VERSION);
        lines_append(&l, h);
        lines_append(&l, "");
        if (g_upd_notes && *g_upd_notes) md_to_lines(&l, g_upd_notes, WRAP_CHARS, 1);
        else lines_append(&l, "(This release has no notes.)");
        lines_append(&l, "");
        lines_append(&l, "____________________");
        lines_append(&l, "");
    }
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\changelog.txt", g_wl.root);
    char *md = wl_read_file(path, NULL);
    if (md) {
        md_to_lines(&l, md, WRAP_CHARS, 1);
        free(md);
    } else {
        lines_append(&l, "changelog.txt was not found in the wololoader folder.");
    }
    size_t total = 1;
    for (int i = 0; i < l.count; i++) total += strlen(l.line[i]) + 1;
    char *out = malloc(total), *o = out;
    if (out) {
        for (int i = 0; i < l.count; i++) {
            size_t n = strlen(l.line[i]);
            memcpy(o, l.line[i], n);
            o += n;
            if (i + 1 < l.count) *o++ = '\n';
        }
        *o = 0;
    }
    md_free(&l);
    return out;
}

static void set_status(const char *text) {
    if (strcmp(text, g_status_text) == 0) return;
    lstrcpynA(g_status_text, text, sizeof g_status_text);
    panel_set_text(g_status, g_status_text);
}

// Status line and Update button from the download state (UI thread, every frame).
static void refresh_update_ui(void) {
    if (!g_btn_update) return;
    LONG s = g_dl_state, done = g_dl_done;
    if (s == g_shown_dl && (s != DL_RUNNING || done == g_shown_done)) return;
    g_shown_dl = (int)s;
    g_shown_done = done;
    char msg[320];
    switch (s) {
    case DL_RUNNING: {
        LONG total = g_dl_total;
        if (total > 0) snprintf(msg, sizeof msg, "Downloading v%s: %.1f of %.1f MB...", g_upd_version, done / 1048576.0, total / 1048576.0);
        else snprintf(msg, sizeof msg, "Downloading v%s: %.1f MB...", g_upd_version, done / 1048576.0);
        set_status(msg);
        panel_set_visible(g_btn_update, 0);
        break;
    }
    case DL_READY:
        snprintf(msg, sizeof msg, "v%s is downloaded. Quit the game: the update installs when the game has closed.", g_upd_version);
        set_status(msg);
        button_set_text(g_btn_update, "Quit and update");
        panel_set_visible(g_btn_update, 1);
        break;
    case DL_FAILED:
        EnterCriticalSection(&g_cs);
        snprintf(msg, sizeof msg, "Update failed: %s. Try again later.", g_dl_error);
        LeaveCriticalSection(&g_cs);
        set_status(msg);
        button_set_text(g_btn_update, "Update now");
        panel_set_visible(g_btn_update, 1);
        break;
    default:
        snprintf(msg, sizeof msg, "Wololoader v%s is out. Update now downloads its setup; it installs after you quit the game.", g_upd_version);
        set_status(msg);
        break;
    }
}

static void __fastcall on_tick(void *self, void *edx) {
    (void)edx;
    g_orig_tick(self);
    refresh_update_ui();
}

// The game's own Exit (main menu handler 0x6e90c0) with no screen to delete: it sends WM_CLOSE
// to the game window ([0xaf2c58]+0x10, 0x5404e0) now and once more from the deferred queue.
static void quit_game(void) {
    static const BYTE want[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x68, 0x80, 0x94, 0x00 };
    if (memcmp((void *)ADDR_MAINMENU_EXIT, want, sizeof want) != 0) {
        set_status("Please quit the game now: the update installs when it has closed.");
        return;
    }
    wl_log("update: quitting through the game's Exit (0x6e90c0)");
    ((MainMenuExit_t)ADDR_MAINMENU_EXIT)(NULL);
}

static int __fastcall on_event(void *self, void *edx, void *panel, int code, int a, int b) {
    (void)edx;
    if (code == EV_LEFT_CLICK && panel) {
        if (panel == g_btn_close) {
            wl_log("changelog: closed");
            void *old = g_screen;
            g_screen = NULL;
            if (old) defer_delete_screen(old);
            return 1;
        }
        if (panel == g_btn_top) {
            ((TextScroll_t)ADDR_TEXT_SCROLL)(g_text, 6, 0, 1);   // mode 6: first line on top
            return 1;
        }
        if (panel == g_btn_update) {
            if (g_dl_state == DL_READY) quit_game();
            else start_download();
            return 1;
        }
    }
    return g_orig_event(self, panel, code, a, b);
}

static void *create_screen(void) {
    void *s = ((New_t)ADDR_GAME_NEW)(SCREENPANEL_SIZE);
    if (!s) return NULL;
    memset(s, 0, SCREENPANEL_SIZE);
    ((ScreenCtor_t)ADDR_SCREEN_CTOR)(s, "Wololoader Changelog");

    memcpy(g_vtable_block, (void *)(SCREENPANEL_VTBL - 4), sizeof g_vtable_block);
    g_orig_tick  = (Tick_t)g_vtable[VT_TICK];
    g_orig_event = (Event_t)g_vtable[VT_EVENT];
    g_vtable[VT_TICK]  = (void *)on_tick;
    g_vtable[VT_EVENT] = (void *)on_event;
    *(void **)s = g_vtable;

    ((SetBg_t)ADDR_SET_BACKGROUND)(s, "scr2p", 0xc3a4, 0);
    int ref[2] = { REF_W, REF_H };
    ((SetRef_t)ADDR_SET_REF_SIZE)(s, ref);

    g_text_has_update = g_upd_state == UPD_AVAILABLE;
    add_label(s, g_text_has_update ? "Wololoader update" : "What's new in Wololoader", 0, 15, REF_W, 40, 1, 1, 1, 0);

    // Body: left/top aligned, word wrap on (f24=0, f28=0, f2c=1), like the History body text.
    g_text = add_label(s, "", TEXT_X, TEXT_Y, TEXT_W, TEXT_H, 0x1a, 0, 0, 1);
    if (g_text) {
        void *table = *(void **)ADDR_FONT_TABLE;
        if (table) {
            void *fonts[4];
            static const int ids[4] = { 0x1a, 0x21, 0x22, 0x23 };   // normal, bold, italic, bold italic
            for (int i = 0; i < 4; i++) fonts[i] = ((FontGet_t)ADDR_FONT_GET)(table, ids[i]);
            if (fonts[0] && fonts[1]) ((SetFonts_t)ADDR_TEXT_SET_FONTS)(g_text, fonts);
        }
        ((AddScrollbar_t)ADDR_ADD_SCROLLBAR)(s, g_text);
        char *text = build_text(g_text_has_update);
        set_formatted_text(g_text, text ? text : "");
        free(text);
        ((TextScroll_t)ADDR_TEXT_SCROLL)(g_text, 6, 0, 1);
    }

    g_status_text[0] = 0;
    g_status = add_label(s, "", TEXT_X, 646, 560, 40, 0xa, 0, 1, 1);
    g_btn_top = add_button(s, "Top", 830, 650, 100, 34);
    g_btn_close = add_button(s, "Close", 1013, 650, 100, 34);
    g_btn_update = NULL;
    if (g_text_has_update) {
        g_btn_update = add_button(s, "Update now", TEXT_X, 694, 230, 40);
        g_shown_dl = -1;
        refresh_update_ui();
    } else {
        char msg[96];
        snprintf(msg, sizeof msg, "You have Wololoader v%s.", WOLOLOADER_VERSION);
        set_status(msg);
    }
    return s;
}

void wl_changelog_open(void) {
    ensure_cs();
    if (g_screen) return;   // already open
    g_screen = create_screen();
    if (!g_screen) return;
    ((SetCurrent_t)ADDR_SET_CURRENT)(*(void **)ADDR_SCREEN_MGR, g_screen);
    wl_log("changelog: opened (update %s)", g_text_has_update ? g_upd_version : "none");
}
