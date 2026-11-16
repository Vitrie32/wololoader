// Custom loading window shown at game start (replaces the autobuild splash).
// Own thread with message loop, AoE2-themed UI, live log display.
#include "wololoader.h"
#include <stdio.h>
#include <string.h>

#define WM_LS_STATUS (WM_USER + 1)   // wParam unused, lParam = const char* status text
#define WM_LS_LOGLINE (WM_USER + 2)  // wParam unused, lParam unused (reads from ring buffer)
#define WM_LS_CLOSE (WM_USER + 3)    // wParam unused, lParam unused

#define LOG_RING_SIZE 12

static HWND g_ls_hwnd;
static HANDLE g_ls_thread;
static HANDLE g_ls_ready;
static CRITICAL_SECTION g_ls_log_lock;
static char g_ls_log_ring[LOG_RING_SIZE][256];
static int g_ls_log_head, g_ls_log_tail, g_ls_log_count;
static char g_ls_status[128] = "Loading game data...";
static BOOL g_ls_closing;
static DWORD g_ls_start_tick;
static BOOL g_ls_menu_shown;
static DWORD g_ls_menu_tick;
static HICON g_ls_icon;

// Ring buffer: add a line (called from wl_log via log.c hook, any thread).
void wl_loadscreen_add_log(const char *line) {
    if (!g_ls_hwnd) return;
    EnterCriticalSection(&g_ls_log_lock);
    if (g_ls_log_count < LOG_RING_SIZE) {
        lstrcpynA(g_ls_log_ring[g_ls_log_tail], line, 256);
        g_ls_log_tail = (g_ls_log_tail + 1) % LOG_RING_SIZE;
        g_ls_log_count++;
    } else {
        lstrcpynA(g_ls_log_ring[g_ls_log_tail], line, 256);
        g_ls_log_tail = (g_ls_log_tail + 1) % LOG_RING_SIZE;
        g_ls_log_head = (g_ls_log_head + 1) % LOG_RING_SIZE;
    }
    LeaveCriticalSection(&g_ls_log_lock);
    PostMessageA(g_ls_hwnd, WM_LS_LOGLINE, 0, 0);
}

void wl_loadscreen_status(const char *text) {
    if (!g_ls_hwnd) return;
    lstrcpynA(g_ls_status, text, sizeof g_ls_status);
    PostMessageA(g_ls_hwnd, WM_LS_STATUS, 0, 0);
}

static void draw_gradient_rect(HDC dc, RECT *r, COLORREF top, COLORREF bottom) {
    for (int y = r->top; y < r->bottom; y++) {
        int ratio = ((y - r->top) * 255) / (r->bottom - r->top);
        int red = GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * ratio / 255;
        int green = GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * ratio / 255;
        int blue = GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * ratio / 255;
        HBRUSH br = CreateSolidBrush(RGB(red, green, blue));
        RECT line = {r->left, y, r->right, y + 1};
        FillRect(dc, &line, br);
        DeleteObject(br);
    }
}

static LRESULT CALLBACK loadscreen_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LS_STATUS || m == WM_LS_LOGLINE) {
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    if (m == WM_LS_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_TIMER) {
        // Safety timeout (60s) or delayed close after menu+error (5s from menu appearance)
        DWORD elapsed = GetTickCount() - g_ls_start_tick;
        DWORD menu_elapsed = g_ls_menu_shown ? (GetTickCount() - g_ls_menu_tick) : 0;
        if (elapsed > 60000 || (g_ls_menu_shown && menu_elapsed > 5000)) {
            PostMessageA(h, WM_LS_CLOSE, 0, 0);
        }
        return 0;
    }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        RECT client;
        HDC dc = BeginPaint(h, &ps);
        GetClientRect(h, &client);

        // Background: dark wood gradient (dark brown to slightly lighter)
        RECT bg = client;
        draw_gradient_rect(dc, &bg, RGB(30, 20, 12), RGB(50, 35, 20));

        // Gold border (3px)
        HPEN gold_pen = CreatePen(PS_SOLID, 3, RGB(232, 200, 120));
        HPEN old_pen = SelectObject(dc, gold_pen);
        HBRUSH null_brush = GetStockObject(NULL_BRUSH);
        HBRUSH old_brush = SelectObject(dc, null_brush);
        Rectangle(dc, 2, 2, client.right - 2, client.bottom - 2);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(gold_pen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(232, 200, 120));

        // Title font: Georgia or Times New Roman, 20pt, bold
        HFONT title_font = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_ROMAN, "Georgia");
        HFONT old_font = SelectObject(dc, title_font);

        // Icon (left of title)
        int icon_x = 20, icon_y = 18;
        if (g_ls_icon) {
            DrawIconEx(dc, icon_x, icon_y, g_ls_icon, 32, 32, 0, NULL, DI_NORMAL);
        }

        // Title "Wololoader vX"
        char title[64];
        snprintf(title, sizeof title, "Wololoader v%s", WOLOLOADER_VERSION);
        RECT title_rect = {icon_x + 40, 20, client.right - 20, 50};
        DrawTextA(dc, title, -1, &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(title_font);

        // Normal font for the rest (10pt)
        HFONT normal_font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(dc, normal_font);

        int y = 65;

        // Mod list section
        if (g_wl_vanilla) {
            RECT r = {20, y, client.right - 20, y + 20};
            DrawTextA(dc, "Vanilla Mode (mods off)", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 25;
        } else if (g_wl.mod_count > 0) {
            char hdr[64];
            snprintf(hdr, sizeof hdr, "Loaded mods (%d):", g_wl.mod_count);
            RECT r = {20, y, client.right - 20, y + 18};
            DrawTextA(dc, hdr, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 22;
            for (int i = 0; i < g_wl.mod_count && i < 5 && y < client.bottom - 120; i++) {
                char mod_line[160];
                snprintf(mod_line, sizeof mod_line, "  %s v%s",
                         g_wl.mods[i].name, g_wl.mods[i].version[0] ? g_wl.mods[i].version : "?");
                RECT mr = {20, y, client.right - 20, y + 18};
                DrawTextA(dc, mod_line, -1, &mr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += 18;
            }
            if (g_wl.mod_count > 5) {
                char more[32];
                snprintf(more, sizeof more, "  ... and %d more", g_wl.mod_count - 5);
                RECT mr = {20, y, client.right - 20, y + 18};
                DrawTextA(dc, more, -1, &mr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += 18;
            }
            y += 10;
        }

        // Log area (last ~10 lines)
        if (g_ls_log_count > 0) {
            HFONT log_font = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            SelectObject(dc, log_font);
            SetTextColor(dc, RGB(180, 160, 120));

            EnterCriticalSection(&g_ls_log_lock);
            int lines_to_show = g_ls_log_count < 10 ? g_ls_log_count : 10;
            int start = (g_ls_log_head + g_ls_log_count - lines_to_show) % LOG_RING_SIZE;
            for (int i = 0; i < lines_to_show && y < client.bottom - 50; i++) {
                int idx = (start + i) % LOG_RING_SIZE;
                RECT lr = {20, y, client.right - 20, y + 15};
                DrawTextA(dc, g_ls_log_ring[idx], -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += 15;
            }
            LeaveCriticalSection(&g_ls_log_lock);
            DeleteObject(log_font);
            SelectObject(dc, normal_font);
        }

        // Status bar at bottom
        RECT status_bg = {10, client.bottom - 35, client.right - 10, client.bottom - 10};
        HBRUSH status_br = CreateSolidBrush(RGB(40, 28, 16));
        FillRect(dc, &status_bg, status_br);
        DeleteObject(status_br);

        // Error in red if build failed
        if (g_wl_build_error[0]) {
            SetTextColor(dc, RGB(255, 80, 80));
            RECT err_r = {15, client.bottom - 32, client.right - 15, client.bottom - 13};
            DrawTextA(dc, g_wl_build_error, -1, &err_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(dc, RGB(200, 180, 140));
            RECT st_r = {15, client.bottom - 32, client.right - 15, client.bottom - 13};
            DrawTextA(dc, g_ls_status, -1, &st_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(dc, old_font);
        DeleteObject(normal_font);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_CLOSE || m == WM_DESTROY) {
        if (m == WM_DESTROY) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI loadscreen_thread(LPVOID arg) {
    (void)arg;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = loadscreen_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "WololoaderLoadScreen";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    int w = 560, h = 340;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g_ls_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, "Wololoader", WS_POPUP | WS_VISIBLE,
        x, y, w, h, NULL, NULL, wc.hInstance, NULL);

    if (g_ls_hwnd) {
        SetTimer(g_ls_hwnd, 1, 1000, NULL);  // check timeout every second
    }
    SetEvent(g_ls_ready);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    g_ls_hwnd = NULL;
    if (g_ls_icon) DestroyIcon(g_ls_icon);
    DeleteCriticalSection(&g_ls_log_lock);
    return 0;
}

void wl_install_loadscreen(void) {
    InitializeCriticalSection(&g_ls_log_lock);
    g_ls_start_tick = GetTickCount();
    g_ls_icon = LoadIconA(GetModuleHandleA("version.dll"), MAKEINTRESOURCEA(1));
    g_ls_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_ls_thread = CreateThread(NULL, 0, loadscreen_thread, NULL, 0, NULL);
    if (g_ls_ready) {
        WaitForSingleObject(g_ls_ready, 2000);
        CloseHandle(g_ls_ready);
        g_ls_ready = NULL;
    }
}

void wl_loadscreen_close(void) {
    if (g_ls_closing || !g_ls_hwnd) return;
    g_ls_closing = TRUE;
    g_ls_menu_shown = TRUE;
    g_ls_menu_tick = GetTickCount();

    // If build error, keep window 5s more (timer will close it); else close now
    if (!g_wl_build_error[0]) {
        PostMessageA(g_ls_hwnd, WM_LS_CLOSE, 0, 0);
        if (g_ls_thread) {
            WaitForSingleObject(g_ls_thread, 1000);
            CloseHandle(g_ls_thread);
            g_ls_thread = NULL;
        }
    }
}
