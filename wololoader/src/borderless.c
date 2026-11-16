// Borderless fullscreen mode: hooks Direct3D9 to prevent exclusive fullscreen,
// fixing Alt+Tab breakage and screen freezes under Linux/Proton (KDE Wayland).
// The game requests fullscreen -> we force windowed mode at the same resolution,
// then stretch the window to borderless fullscreen (WS_POPUP covering the monitor).
#include "wololoader.h"
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

// Original function pointers
typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT);
static Direct3DCreate9_t g_real_d3dcreate9;
static HRESULT (__stdcall *g_real_createdevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static HRESULT (__stdcall *g_real_reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

// State: are we in borderless mode for a given device?
static HWND g_focus_window;
static WNDPROC g_original_wndproc;
static BOOL g_borderless_active;
static UINT g_requested_width, g_requested_height;

// ---- IAT patching (adapted from files.c) -----------------------------------
static BOOL patch_iat(const char *dll_name, const char *func_name, void *hook, void **original) {
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->Size) return FALSE;
    IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)exe + dir->VirtualAddress);
    for (; imports->Name; imports++) {
        const char *name = (const char *)((BYTE *)exe + imports->Name);
        if (lstrcmpiA(name, dll_name) != 0) continue;
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)((BYTE *)exe + imports->FirstThunk);
        IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)((BYTE *)exe + imports->OriginalFirstThunk);
        for (; thunk->u1.Function; thunk++, orig++) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *import = (IMAGE_IMPORT_BY_NAME *)((BYTE *)exe + orig->u1.AddressOfData);
            if (strcmp((char *)import->Name, func_name) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
            if (original) *original = (void *)thunk->u1.Function;
            thunk->u1.Function = (DWORD_PTR)hook;
            VirtualProtect(&thunk->u1.Function, sizeof(void *), old, &old);
            return TRUE;
        }
    }
    return FALSE;
}

// ---- Helper: make a window borderless fullscreen ----------------------------
static void make_borderless(HWND hwnd) {
    if (!hwnd) return;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, &mi)) return;
    LONG style = WS_POPUP | WS_VISIBLE;
    LONG exstyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
    SetWindowLongPtrA(hwnd, GWL_STYLE, style);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, exstyle);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED);
}

// ---- Window procedure subclass: prevent style changes, swallow Alt+Enter ----
static LRESULT CALLBACK borderless_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_borderless_active) return CallWindowProcA(g_original_wndproc, hwnd, msg, wp, lp);

    // Swallow Alt+Enter to prevent the game from trying to toggle fullscreen
    if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) return 0;

    // Keep the window borderless and covering the monitor when the game tries
    // to restyle or move it. Edit the requested values in place: calling
    // SetWindowPos/SetWindowLong from here would send these same messages
    // again (endless recursion).
    if (msg == WM_STYLECHANGING && wp == (WPARAM)GWL_STYLE) {
        STYLESTRUCT *ss = (STYLESTRUCT *)lp;
        ss->styleNew = (ss->styleNew & WS_VISIBLE) | WS_POPUP;
    } else if (msg == WM_STYLECHANGING && wp == (WPARAM)GWL_EXSTYLE) {
        STYLESTRUCT *ss = (STYLESTRUCT *)lp;
        ss->styleNew &= ~(DWORD)(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
    } else if (msg == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        // Leave minimizing alone (minimized windows go to -32000,-32000).
        if ((!(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOMOVE)) && !IsIconic(hwnd) && pos->x > -30000) {
            if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
                pos->x = mi.rcMonitor.left;
                pos->y = mi.rcMonitor.top;
                pos->cx = mi.rcMonitor.right - mi.rcMonitor.left;
                pos->cy = mi.rcMonitor.bottom - mi.rcMonitor.top;
                pos->flags &= ~(UINT)(SWP_NOSIZE | SWP_NOMOVE);
            }
        }
    }

    return CallWindowProcA(g_original_wndproc, hwnd, msg, wp, lp);
}

// ---- Hook IDirect3DDevice9::Reset -------------------------------------------
static HRESULT __stdcall hook_reset(IDirect3DDevice9 *self, D3DPRESENT_PARAMETERS *pp) {
    if (pp && !pp->Windowed) {
        wl_log("borderless: Reset with fullscreen request (%ux%u), converting to windowed",
               pp->BackBufferWidth, pp->BackBufferHeight);
        g_requested_width = pp->BackBufferWidth;
        g_requested_height = pp->BackBufferHeight;
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
        // Keep BackBufferWidth/Height as requested (game renders at this resolution)
    }
    HRESULT hr = g_real_reset(self, pp);
    if (SUCCEEDED(hr) && g_focus_window && g_borderless_active) {
        make_borderless(g_focus_window);
    }
    return hr;
}

// ---- Hook IDirect3D9::CreateDevice ------------------------------------------
static HRESULT __stdcall hook_createdevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type,
                                           HWND focus, DWORD flags,
                                           D3DPRESENT_PARAMETERS *pp,
                                           IDirect3DDevice9 **out) {
    BOOL was_fullscreen = FALSE;
    if (pp && !pp->Windowed) {
        was_fullscreen = TRUE;
        wl_log("borderless: CreateDevice with fullscreen request (%ux%u), converting to windowed",
               pp->BackBufferWidth, pp->BackBufferHeight);
        g_requested_width = pp->BackBufferWidth;
        g_requested_height = pp->BackBufferHeight;
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
        // Keep BackBufferWidth/Height as requested
    }

    HRESULT hr = g_real_createdevice(self, adapter, type, focus, flags, pp, out);
    if (FAILED(hr)) return hr;

    if (was_fullscreen && *out) {
        g_borderless_active = TRUE;
        g_focus_window = focus;

        // Hook the device's Reset method (vtable index 16)
        void **vtable = *(void ***)(*out);
        DWORD old;
        if (VirtualProtect(&vtable[16], sizeof(void *), PAGE_READWRITE, &old)) {
            g_real_reset = (void *)vtable[16];
            vtable[16] = hook_reset;
            VirtualProtect(&vtable[16], sizeof(void *), old, &old);
        }

        // Make the window borderless fullscreen
        make_borderless(focus);

        // Subclass the window to maintain borderless state and block Alt+Enter
        if (!g_original_wndproc) {
            g_original_wndproc = (WNDPROC)SetWindowLongPtrA(focus, GWLP_WNDPROC, (LONG_PTR)borderless_wndproc);
        }

        wl_log("borderless: device created, window set to borderless %ux%u",
               g_requested_width, g_requested_height);
    }

    return hr;
}

// ---- Hook IDirect3D9 from our Direct3DCreate9 wrapper -----------------------
static IDirect3D9 *WINAPI hook_d3dcreate9(UINT sdk_version) {
    IDirect3D9 *d3d = g_real_d3dcreate9(sdk_version);
    if (!d3d) return NULL;

    // Hook the CreateDevice method (vtable index 16)
    void **vtable = *(void ***)d3d;
    DWORD old;
    if (VirtualProtect(&vtable[16], sizeof(void *), PAGE_READWRITE, &old)) {
        g_real_createdevice = (void *)vtable[16];
        vtable[16] = hook_createdevice;
        VirtualProtect(&vtable[16], sizeof(void *), old, &old);
        wl_log("borderless: hooked IDirect3D9::CreateDevice");
    }

    return d3d;
}

// ---- Installation -----------------------------------------------------------
void wl_install_borderless(void) {
    if (patch_iat("d3d9.dll", "Direct3DCreate9", hook_d3dcreate9, (void **)&g_real_d3dcreate9)) {
        wl_log("borderless: hooked Direct3DCreate9, will force borderless fullscreen");
    } else {
        wl_log("borderless: failed to hook Direct3DCreate9");
    }
}
