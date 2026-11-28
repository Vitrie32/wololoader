// Lua on-screen UI (UI:CreateFrame / CreateLabel / CreateButton): panels, text and
// clickable buttons drawn over the game screen. Nothing here pauses the game: the
// elements are drawn every frame on top of the game, and clicks on them are taken
// from the window messages before the game sees them.
//
// Drawing: GameScreen vtable 0x9d5300 slot 20 (+0x50) is the paint of the whole game
// screen, 0x6c2e40, __thiscall(screen), plain `ret` (checked: 0x6c2fdb ret). It paints
// itself (slot 15) and the map view [screen+0xb04] every frame and the other children on
// full redraws (0x6c2f6b..0x6c2fd3). We call it, then draw on the screen's draw area
// [screen+0xc] with the same context helpers as selpanel.c (ctx init 0x49e3c0 ret 4, HDC
// 0x49e7b0, ctx destroy 0x49e4b0; TPanel::paint 0x4845d0 uses them the same way). Screen
// coordinates: panel rect [screen+0xb8/0xbc/0xc0/0xc4] (x, y, w, h).
//
// Input: the game window (the visible top-level window of the thread that paints) is
// subclassed. Left/right button presses over a visible element with a background are
// swallowed; a left press + release on the same button queues a click, which luagame.c
// delivers to Lua from the frame tick (wl_ui_frame), never from inside the window procedure.
// Mouse positions are scaled from the window client size to the game screen size
// (borderless fullscreen may stretch a smaller game resolution).
//
// UI is local: every player sees only the elements their own scripts made, and the click
// handlers run only on the clicking player's machine. Game commands sent from a click
// handler are sent from this machine (wl_cmd_set_local_ctx, gamecmd.c).
#define COBJMACROS
#include "wololoader.h"
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <string.h>

#define MAX_EL 256
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))

enum { T_FRAME = 1, T_LABEL = 2, T_BUTTON = 3, T_TEXTBOX = 4 };

typedef struct {
    int used, type, parent, seq;
    int x, y, w, h, anchor;          // anchor 0..8: row-major TopLeft..BottomRight
    int visible;
    int has_bg, has_border;
    COLORREF bg, fg, border;
    int alpha;                       // background opacity 0..255
    int text_size, bold, align;      // align 0 left, 1 center, 2 right
    char text[256];
    int hotkey;                      // VK code | 0x100 ctrl | 0x200 shift | 0x400 alt, 0 = none
    char image_path[MAX_PATH];       // PNG relative to mod folder
    HBITMAP image_bmp;               // cached bitmap, loaded on first draw
    int image_w, image_h;            // cached dimensions
    int maxlen, numeric;             // text boxes: max characters (1..255), digits only
    int caret;                       // text boxes: cursor offset 0..strlen(text)
    char placeholder[64];            // text boxes: grey hint while empty
    int mono;                        // 1 = monospace font (Consolas)
    RECT abs; int shown, z;          // from the last paint (z = draw order)
    // Translucent background (see blend_bg): the final pixels of this element's rect after the
    // last paint, and the rect / colour / alpha they belong to.
    DWORD *bg_last; RECT bg_rect; COLORREF bg_col; int bg_alpha;
} El;

static El g_el[MAX_EL];
static int g_seq;
static DWORD g_last_paint;
static int g_scr_w = 1, g_scr_h = 1;
static int g_last_scr_w, g_last_scr_h;
static HWND g_hwnd;
extern int wl_is_multiplayer(void);
static WNDPROC g_old_proc;
static HWND g_game_hwnd;
#define WM_WL_RESTART (WM_APP + 0x57)

// Map-tile click capture (RGE_View::pick at 0x87cbd0, __thiscall ret 0x18, verified in exe.s).
// GameScreen+0xb08 = TRIBE_Main_View*; +0xb8/+0xbc/+0xc0/+0xc4 = panel rect (x, y, w, h).
typedef struct {
    float x, y;
    short scr_x, scr_y;
    void *obj;
    void *tile;
} RgePickInfo;
typedef unsigned char (__thiscall *RgeViewPick_t)(
    void *view, int pick_type, int sub_type, int sx, int sy, RgePickInfo *out, int last_obj);
typedef struct { float x, y; int btn; } MapClickEv;
static int g_map_capture;
static int g_no_unit_control;
static int g_map_dragging;
static DWORD g_last_drag_tick;
static MapClickEv g_mclicks[32];
static int g_nmclicks;

void wl_ui_set_map_capture(int on) {
    g_map_capture = on ? 1 : 0;
    if (!g_map_capture) { g_nmclicks = 0; g_map_dragging = 0; }
}

void wl_ui_set_unit_control_disabled(int off) {
    g_no_unit_control = off ? 1 : 0;
}

static int in_main_view(int sx, int sy) {
    void *gs = *(void **)0xaf3d90;
    char *mv = gs ? *(char **)((char *)gs + 0xb04) : NULL;
    if (!mv) return 0;
    int px = *(int *)(mv + 0xb8), py = *(int *)(mv + 0xbc);
    int pw = *(int *)(mv + 0xc0), ph = *(int *)(mv + 0xc4);
    return sx >= px && sy >= py && sx < px + pw && sy < py + ph;
}

int wl_ui_request_restart(void) {
    if (!g_game_hwnd || wl_is_multiplayer()) return 0;
    PostMessageA(g_game_hwnd, WM_WL_RESTART, 0, 0);
    return 1;
}

static int pick_map_tile(int sx, int sy, float *ox, float *oy) {
    void *gs = *(void **)0xaf3d90;
    if (!gs) return 0;
    char *mv = *(char **)((char *)gs + 0xb04);  // TRIBE_Main_View* (verified 0x6b4450)
    if (!mv) return 0;
    int px = *(int *)(mv + 0xb8), py = *(int *)(mv + 0xbc);
    int pw = *(int *)(mv + 0xc0), ph = *(int *)(mv + 0xc4);
    if (sx < px || sy < py || sx >= px + pw || sy >= py + ph) return 0;
    RgePickInfo info;
    memset(&info, 0, sizeof info);
    unsigned char rc = ((RgeViewPick_t)0x87cbd0)(mv, 0x28, 0, sx, sy, &info, 0);
    if (rc != 0x33) return 0;
    *ox = info.x;
    *oy = info.y;
    return 1;
}
static int g_hover = -1, g_pressed = -1, g_z;
static int g_clicks[32], g_nclicks;
static int g_hovers[64], g_nhovers;  // hover enter/leave event queue
static int g_focus = -1;             // text box that gets the keyboard, or -1
static int g_tev[64], g_ntev;        // text box events: +id changed, -id submitted (Enter)
static int g_keys[64], g_nkeys;      // key events for focused text box: (id << 16) | vk
static int g_scrolls[64], g_nscrolls;// wheel events: (id << 16) | (1=up, 2=down)

#define MAX_IMAGES 64
typedef struct { char path[MAX_PATH]; HBITMAP bmp; int w, h, has_alpha; } ImgCache;
static ImgCache g_images[MAX_IMAGES];
static int g_nimages;

typedef void (__thiscall *Paint_t)(void *screen);
typedef void (__thiscall *CtxInit_t)(void *ctx, void *drawarea);
typedef void (__thiscall *CtxDone_t)(void *ctx);
typedef HDC  (__thiscall *CtxDC_t)(void *ctx);
static Paint_t o_paint;

// ---- element store (called from script.c) ---------------------------------------------
int wl_ui_create(int type) {
    if (type < T_FRAME || type > T_TEXTBOX) return -1;
    for (int i = 0; i < MAX_EL; i++) {
        if (g_el[i].used) continue;
        El *e = &g_el[i];
        memset(e, 0, sizeof *e);
        e->used = 1; e->type = type; e->parent = -1; e->seq = ++g_seq; e->visible = 1;
        e->w = 120; e->h = 30; e->text_size = 16; e->align = 1; e->alpha = 255;
        e->fg = RGB(255, 255, 255);
        e->border = RGB(196, 160, 80);
        if (type == T_FRAME)  { e->has_bg = 1; e->has_border = 1; e->bg = RGB(30, 24, 18); e->alpha = 210; e->w = 200; e->h = 120; }
        if (type == T_BUTTON) { e->has_bg = 1; e->has_border = 1; e->bg = RGB(78, 58, 36); e->bold = 1; }
        if (type == T_LABEL)  { e->align = 0; e->w = 200; e->h = 24; }
        if (type == T_TEXTBOX) { e->has_bg = 1; e->has_border = 1; e->bg = RGB(18, 14, 10); e->align = 0; e->maxlen = 32; e->h = 24; }
        return i + 1;
    }
    return -1;
}

static El *get(int id) { return (id >= 1 && id <= MAX_EL && g_el[id - 1].used) ? &g_el[id - 1] : NULL; }

static void scrub_queue_id(int *q, int *nq, int id, int mode) {
    int w = 0;
    for (int i = 0; i < *nq; i++) {
        int v = q[i];
        int match = (mode == 0) ? (v == id)
                  : (mode == 1) ? (v == id || v == -id)
                  : ((v >> 16) == id);
        if (!match) q[w++] = v;
    }
    *nq = w;
}

void wl_ui_destroy(int id) {
    El *e = get(id);
    if (!e) return;
    for (int i = 0; i < MAX_EL; i++)                     // children go with their parent
        if (g_el[i].used && g_el[i].parent == id - 1) wl_ui_destroy(i + 1);
    free(e->bg_last);
    e->bg_last = NULL;
    e->used = 0;
    if (g_hover == id - 1) g_hover = -1;
    if (g_pressed == id - 1) g_pressed = -1;
    if (g_focus == id - 1) g_focus = -1;
    scrub_queue_id(g_clicks, &g_nclicks, id, 0);
    scrub_queue_id(g_hovers, &g_nhovers, id, 1);
    scrub_queue_id(g_tev, &g_ntev, id, 1);
    scrub_queue_id(g_keys, &g_nkeys, id, 2);
    scrub_queue_id(g_scrolls, &g_nscrolls, id, 2);
}

// key: Text, X, Y, W, H, Anchor, Visible, Color, TextColor, BorderColor, Alpha, TextSize, Bold,
// Align, Parent, NoColor (no background), NoBorder, Hotkey, Image, Mono. Numbers in `v`, text in `s`.
const char *wl_ui_get_text(int id) {
    if (id == -1) {
        static char scr_buf[32];
        snprintf(scr_buf, sizeof scr_buf, "%dx%d", g_scr_w, g_scr_h);
        return scr_buf;
    }
    El *e = get(id);
    return e ? e->text : NULL;
}

int wl_ui_set(int id, const char *key, int v, const char *s) {
    El *e = get(id);
    if (!e || !key) return 0;
    if (!strcmp(key, "Text")) {
        lstrcpynA(e->text, s ? s : "", sizeof e->text);
        e->caret = (int)strlen(e->text);
    }
    else if (!strcmp(key, "X")) e->x = v;
    else if (!strcmp(key, "Y")) e->y = v;
    else if (!strcmp(key, "W")) e->w = v < 0 ? 0 : v;
    else if (!strcmp(key, "H")) e->h = v < 0 ? 0 : v;
    else if (!strcmp(key, "Anchor")) e->anchor = (v >= 0 && v <= 8) ? v : 0;
    else if (!strcmp(key, "Visible")) e->visible = v != 0;
    else if (!strcmp(key, "Color")) { e->bg = (COLORREF)v; e->has_bg = 1; }
    else if (!strcmp(key, "NoColor")) e->has_bg = 0;
    else if (!strcmp(key, "TextColor")) e->fg = (COLORREF)v;
    else if (!strcmp(key, "BorderColor")) { e->border = (COLORREF)v; e->has_border = 1; }
    else if (!strcmp(key, "NoBorder")) e->has_border = 0;
    else if (!strcmp(key, "Alpha")) e->alpha = v < 0 ? 0 : v > 255 ? 255 : v;
    else if (!strcmp(key, "TextSize")) e->text_size = v < 6 ? 6 : v > 72 ? 72 : v;
    else if (!strcmp(key, "Bold")) e->bold = v != 0;
    else if (!strcmp(key, "Mono")) e->mono = v != 0;
    else if (!strcmp(key, "Align")) e->align = (v >= 0 && v <= 2) ? v : 0;
    else if (!strcmp(key, "Hotkey")) e->hotkey = v;
    else if (!strcmp(key, "MaxLength")) {
        e->maxlen = v < 1 ? 1 : v > 255 ? 255 : v;
        e->text[e->maxlen] = 0;
        int len = (int)strlen(e->text);
        if (e->caret > len) e->caret = len;
    }
    else if (!strcmp(key, "Numeric")) e->numeric = v != 0;
    else if (!strcmp(key, "Placeholder")) lstrcpynA(e->placeholder, s ? s : "", sizeof e->placeholder);
    else if (!strcmp(key, "Focus")) { if (e->type == T_TEXTBOX) g_focus = v ? id - 1 : (g_focus == id - 1 ? -1 : g_focus); }
    else if (!strcmp(key, "Image")) {
        if (s) lstrcpynA(e->image_path, s, sizeof e->image_path);
        else e->image_path[0] = 0;
        if (e->image_bmp) { DeleteObject(e->image_bmp); e->image_bmp = NULL; }
    }
    else if (!strcmp(key, "Parent")) {
        int p = v - 1;
        for (int q = p; q >= 0; q = g_el[q].parent) if (q == id - 1) return 0;   // no cycles
        e->parent = (p >= 0 && p < MAX_EL && g_el[p].used) ? p : -1;
    }
    else return 0;
    return 1;
}

// ---- drawing --------------------------------------------------------------------------
static HFONT font(int size, int bold, int mono) {
    static struct { int size, bold, mono; HFONT f; } cache[32];
    static int n;
    for (int i = 0; i < n; i++)
        if (cache[i].size == size && cache[i].bold == bold && cache[i].mono == mono) return cache[i].f;
    HFONT f = CreateFontA(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          mono ? (FIXED_PITCH | FF_MODERN) : DEFAULT_PITCH,
                          mono ? "Consolas" : "Arial");
    if (n < 32) { cache[n].size = size; cache[n].bold = bold; cache[n].mono = mono; cache[n].f = f; n++; }
    return f;
}

typedef BOOL (WINAPI *AlphaBlend_t)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
static AlphaBlend_t get_alphablend(void) {
    static AlphaBlend_t ab; static int tried;
    if (!tried) {
        tried = 1;
        HMODULE m = LoadLibraryA("msimg32.dll");
        ab = m ? (AlphaBlend_t)(void *)GetProcAddress(m, "AlphaBlend") : NULL;
    }
    return ab;
}

static void fill(HDC dc, const RECT *r, COLORREF c, int alpha) {
    AlphaBlend_t ab = get_alphablend();
    if (alpha < 255 && ab) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = mem ? CreateCompatibleBitmap(dc, 1, 1) : NULL;
        if (bmp) {
            HGDIOBJ old = SelectObject(mem, bmp);
            SetPixel(mem, 0, 0, c);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
            BOOL ok = ab(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, mem, 0, 0, 1, 1, bf);
            SelectObject(mem, old);
            DeleteObject(bmp); DeleteDC(mem);
            if (ok) return;
        } else if (mem) DeleteDC(mem);
    }
    if (alpha == 0) return;
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

// Translucent backgrounds. The game does not repaint the map under the UI on every frame
// (only what changed), so blending onto the screen each frame put the colour on top of last
// frame's blend: a panel grew darker until the game repainted it, then lighter again (seen as
// flicker, 2026-11-25). Here a pixel is blended only when the game drew it anew: one that still
// holds what the UI left there after the last paint (bg_last) is kept as it is.
static HDC g_bg_dc;
static HBITMAP g_bg_bmp;
static DWORD *g_bg_px;
static int g_bg_w, g_bg_h;

static DWORD *bg_scratch(HDC dc, int w, int h) {
    if (w > g_bg_w || h > g_bg_h || !g_bg_dc) {
        if (g_bg_dc) { DeleteDC(g_bg_dc); g_bg_dc = NULL; }
        if (g_bg_bmp) { DeleteObject(g_bg_bmp); g_bg_bmp = NULL; }
        int nw = w > g_bg_w ? w : g_bg_w, nh = h > g_bg_h ? h : g_bg_h;
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = nw;
        bi.bmiHeader.biHeight = -nh;          // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        g_bg_dc = CreateCompatibleDC(dc);
        g_bg_bmp = g_bg_dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
        if (!g_bg_bmp) { if (g_bg_dc) DeleteDC(g_bg_dc); g_bg_dc = NULL; g_bg_w = g_bg_h = 0; return NULL; }
        SelectObject(g_bg_dc, g_bg_bmp);
        g_bg_px = bits;
        g_bg_w = nw;
        g_bg_h = nh;
    }
    return g_bg_px;
}

static int blend_bg(HDC dc, El *e, const RECT *r, COLORREF c, int alpha) {
    int w = r->right - r->left, h = r->bottom - r->top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
    DWORD *px = bg_scratch(dc, w, h);
    if (!px) return 0;
    GdiFlush();
    if (!BitBlt(g_bg_dc, 0, 0, w, h, dc, r->left, r->top, SRCCOPY)) return 0;
    GdiFlush();
    int same = e->bg_last && EqualRect(&e->bg_rect, r) && e->bg_col == c && e->bg_alpha == alpha;
    int cr = GetRValue(c) * alpha, cg = GetGValue(c) * alpha, cb = GetBValue(c) * alpha, ia = 255 - alpha;
    for (int y = 0; y < h; y++) {
        DWORD *row = px + (size_t)y * g_bg_w;
        const DWORD *old = same ? e->bg_last + (size_t)y * w : NULL;
        for (int x = 0; x < w; x++) {
            DWORD p = row[x] & 0xffffff;
            if (old && p == old[x]) continue;          // still the UI's own pixel from last paint
            int pr = (int)(p >> 16) & 255, pg = (int)(p >> 8) & 255, pb = (int)p & 255;
            row[x] = (DWORD)(((pr * ia + cr) / 255) << 16 | ((pg * ia + cg) / 255) << 8 | ((pb * ia + cb) / 255));
        }
    }
    BitBlt(dc, r->left, r->top, w, h, g_bg_dc, 0, 0, SRCCOPY);
    if (!same) {
        free(e->bg_last);
        e->bg_last = NULL;
        e->bg_rect = *r;
        e->bg_col = c;
        e->bg_alpha = alpha;
    }
    return 1;
}

// After the whole UI is drawn: remember the final pixels of every translucent background.
static void remember_bgs(HDC dc) {
    for (int i = 0; i < MAX_EL; i++) {
        El *e = &g_el[i];
        if (!e->used || !e->shown || !e->has_bg || e->alpha <= 0 || e->alpha >= 255) continue;
        RECT r = e->bg_rect;
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0 || !EqualRect(&r, &e->abs)) continue;
        DWORD *px = bg_scratch(dc, w, h);
        if (!px) continue;
        GdiFlush();
        if (!BitBlt(g_bg_dc, 0, 0, w, h, dc, r.left, r.top, SRCCOPY)) continue;
        GdiFlush();
        if (!e->bg_last) e->bg_last = malloc((size_t)w * h * sizeof(DWORD));
        if (!e->bg_last) continue;
        for (int y = 0; y < h; y++) {
            const DWORD *row = px + (size_t)y * g_bg_w;
            DWORD *dst = e->bg_last + (size_t)y * w;
            for (int x = 0; x < w; x++) dst[x] = row[x] & 0xffffff;
        }
    }
}

static COLORREF shade(COLORREF c, int pct) {   // pct > 100 lighter, < 100 darker
    int r = GetRValue(c) * pct / 100, g = GetGValue(c) * pct / 100, b = GetBValue(c) * pct / 100;
    if (pct > 100) { r += 12; g += 12; b += 12; }
    return RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

// Load image from cache or file. Path is relative to the element's mod folder. Returns cached entry or NULL.
static ImgCache *load_image(const char *path) {
    if (!path || !path[0]) return NULL;
    for (int i = 0; i < g_nimages; i++)
        if (!strcmp(g_images[i].path, path)) return &g_images[i];
    if (g_nimages >= MAX_IMAGES) {
        wl_log("ui: image cache full (%d), '%s' not loaded", MAX_IMAGES, path);
        return NULL;
    }
    // Load with WIC (similar to iconcache.c but no scaling, load to 32bpp BGRA HBITMAP).
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL need_uninit = (hr_co == S_OK || hr_co == S_FALSE);
    if (FAILED(hr_co) && hr_co != RPC_E_CHANGED_MODE) return NULL;

    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    HBITMAP bmp = NULL;
    UINT w = 0, h = 0;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr)) { wl_log("ui: WIC CoCreateInstance failed (0x%08lx) for %s", (unsigned long)hr, path); goto img_cleanup; }

    wchar_t wpath[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH)) goto img_cleanup;

    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, wpath, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) { wl_log("ui: image '%s' load failed (0x%08lx)", path, (unsigned long)hr); goto img_cleanup; }

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) goto img_cleanup;

    hr = IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 2048 || h > 2048) goto img_cleanup;

    hr = IWICImagingFactory_CreateFormatConverter(factory, &conv);
    if (FAILED(hr)) goto img_cleanup;

    hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame,
                                        &GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, NULL, 0.0,
                                        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto img_cleanup;

    UINT stride = w * 4, bytes = stride * h;
    BYTE *pixels = (BYTE *)malloc(bytes);
    if (!pixels) goto img_cleanup;

    hr = IWICFormatConverter_CopyPixels(conv, NULL, stride, bytes, pixels);
    if (FAILED(hr)) { free(pixels); goto img_cleanup; }

    // Premultiply alpha for AlphaBlend (BGRA, alpha in byte 3 of each pixel).
    int has_alpha = 0;
    for (UINT i = 0; i < bytes; i += 4) {
        BYTE a = pixels[i + 3];
        if (a != 255) {
            has_alpha = 1;
            pixels[i + 0] = (BYTE)((pixels[i + 0] * a) / 255);
            pixels[i + 1] = (BYTE)((pixels[i + 1] * a) / 255);
            pixels[i + 2] = (BYTE)((pixels[i + 2] * a) / 255);
        }
    }

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    // AoE2 HD's GDI overlay surface uses 0x00 in byte 3 for opaque GDI pixels (FillRect/DrawTextA)
    // and treats 0xFF as transparent. Clear byte 3 on opaque images so BitBlt/StretchBlt stays opaque.
    if (!has_alpha) {
        for (UINT i = 0; i < bytes; i += 4) pixels[i + 3] = 0;
    }
    void *dib_bits = NULL;
    HDC screen_dc = GetDC(NULL);
    bmp = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
    if (bmp && dib_bits) memcpy(dib_bits, pixels, bytes);
    ReleaseDC(NULL, screen_dc);
    free(pixels);

img_cleanup:
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (need_uninit) CoUninitialize();

    if (!bmp) { wl_log("ui: image '%s' failed to create bitmap", path); return NULL; }
    ImgCache *ic = &g_images[g_nimages++];
    lstrcpynA(ic->path, path, sizeof ic->path);
    ic->bmp = bmp;
    ic->w = (int)w;
    ic->h = (int)h;
    ic->has_alpha = has_alpha;
    wl_log("ui: loaded image '%s' (%ux%u, alpha=%d)", path, w, h, has_alpha);
    return ic;
}

static void layout(int i, const RECT *pr, int parent_shown) {
    El *e = &g_el[i];
    int pw = pr->right - pr->left, ph = pr->bottom - pr->top;
    int col = e->anchor % 3, row = e->anchor / 3;
    int x = col == 0 ? e->x : col == 1 ? (pw - e->w) / 2 + e->x : pw - e->w - e->x;
    int y = row == 0 ? e->y : row == 1 ? (ph - e->h) / 2 + e->y : ph - e->h - e->y;
    e->abs.left = pr->left + x; e->abs.top = pr->top + y;
    e->abs.right = e->abs.left + e->w; e->abs.bottom = e->abs.top + e->h;
    e->shown = parent_shown && e->visible;
}

static void draw_el(HDC dc, int i) {
    El *e = &g_el[i];
    RECT r = e->abs;

    // Draw background
    if (e->has_bg) {
        COLORREF c = e->bg;
        if (e->type == T_BUTTON && g_pressed == i) c = shade(c, 70);
        else if (e->type == T_BUTTON && g_hover == i) c = shade(c, 135);
        if (!(e->alpha > 0 && e->alpha < 255 && blend_bg(dc, e, &r, c, e->alpha))) fill(dc, &r, c, e->alpha);
    }

    // Draw image (frames and buttons only)
    if ((e->type == T_FRAME || e->type == T_BUTTON) && e->image_path[0]) {
        ImgCache *ic = load_image(e->image_path);
        if (ic && ic->bmp) {
            HDC mem = CreateCompatibleDC(dc);
            if (mem) {
                HGDIOBJ old = SelectObject(mem, ic->bmp);
                AlphaBlend_t ab = get_alphablend();
                RECT img_rect = r;

                if (e->type == T_BUTTON && e->text[0]) {
                    // Button with text: image left of text, square of button height - 4px
                    int sz = e->h - 4;
                    if (sz < 1) sz = 1;
                    img_rect.left = r.left + 2;
                    img_rect.top = r.top + 2;
                    img_rect.right = img_rect.left + sz;
                    img_rect.bottom = img_rect.top + sz;
                } else {
                    // Frame or button without text: fill the rect
                    img_rect = r;
                }

                int dw = img_rect.right - img_rect.left;
                int dh = img_rect.bottom - img_rect.top;
                if (dw > 0 && dh > 0) {
                    BOOL drawn = FALSE;
                    if (ic->has_alpha && ab) {
                        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                        drawn = ab(dc, img_rect.left, img_rect.top, dw, dh, mem, 0, 0, ic->w, ic->h, bf);
                    }
                    if (!drawn) {
                        if (dw == ic->w && dh == ic->h) {
                            BitBlt(dc, img_rect.left, img_rect.top, dw, dh, mem, 0, 0, SRCCOPY);
                        } else {
                            SetStretchBltMode(dc, COLORONCOLOR);
                            StretchBlt(dc, img_rect.left, img_rect.top, dw, dh, mem, 0, 0, ic->w, ic->h, SRCCOPY);
                        }
                    }
                }
                SelectObject(mem, old);
                DeleteDC(mem);
            }
        }
    }

    // Draw border
    if (e->has_border) {
        HBRUSH b = CreateSolidBrush(e->border);
        FrameRect(dc, &r, b);
        if (e->type == T_BUTTON) { RECT in = { r.left + 1, r.top + 1, r.right - 1, r.bottom - 1 }; FrameRect(dc, &in, b); }
        DeleteObject(b);
    }

    // Text box: text (or placeholder), scrolled so the caret stays visible, and a blinking caret
    if (e->type == T_TEXTBOX) {
        HGDIOBJ old = SelectObject(dc, font(e->text_size, e->bold, e->mono));
        SetBkMode(dc, TRANSPARENT);
        RECT t = { r.left + 5, r.top, r.right - 5, r.bottom };
        int len = (int)strlen(e->text);
        int caret = e->caret;
        if (caret < 0) caret = 0;
        if (caret > len) caret = len;
        int avail = t.right - t.left - 4;
        if (avail < 1) avail = 1;
        int start = 0;
        SIZE csz = { 0, 0 };
        GetTextExtentPoint32A(dc, e->text + start, caret - start, &csz);
        while (start < caret && csz.cx > avail) {
            start++;
            GetTextExtentPoint32A(dc, e->text + start, caret - start, &csz);
        }
        const char *txt = e->text + start;
        int vis_len = len - start;
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32A(dc, txt, vis_len, &sz);
        while (vis_len > caret - start && sz.cx > avail) {
            vis_len--;
            GetTextExtentPoint32A(dc, txt, vis_len, &sz);
        }
        if (!e->text[0] && g_focus != i && e->placeholder[0]) {
            SetTextColor(dc, RGB(130, 120, 100));
            DrawTextA(dc, e->placeholder, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        } else {
            SetTextColor(dc, e->fg);
            DrawTextA(dc, txt, vis_len, &t, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        }
        if (g_focus == i && (GetTickCount() / 500) % 2 == 0) {
            int cx = t.left + csz.cx + 1, h = e->text_size + 2;
            RECT c = { cx, (r.top + r.bottom - h) / 2, cx + 2, (r.top + r.bottom + h) / 2 };
            HBRUSH b = CreateSolidBrush(e->fg);
            FillRect(dc, &c, b);
            DeleteObject(b);
        }
        SelectObject(dc, old);
        return;
    }
    // Draw text
    if (e->text[0]) {
        HGDIOBJ old = SelectObject(dc, font(e->text_size, e->bold, e->mono));
        SetBkMode(dc, TRANSPARENT);
        UINT fmt = (e->align == 1 ? DT_CENTER : e->align == 2 ? DT_RIGHT : DT_LEFT) | DT_NOPREFIX;
        fmt |= (e->type == T_LABEL) ? DT_WORDBREAK : (DT_SINGLELINE | DT_VCENTER);

        RECT t = { r.left + 6, r.top + (e->type == T_LABEL ? 2 : 0), r.right - 6, r.bottom };

        // Button with image: shift text right
        if (e->type == T_BUTTON && e->image_path[0]) {
            int sz = e->h - 4;
            if (sz < 1) sz = 1;
            t.left += sz + 4;
        }

        if (e->type == T_BUTTON && g_pressed == i) OffsetRect(&t, 1, 1);
        RECT sh = t; OffsetRect(&sh, 1, 1);
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextA(dc, e->text, -1, &sh, fmt);
        SetTextColor(dc, e->fg);
        DrawTextA(dc, e->text, -1, &t, fmt);
        SelectObject(dc, old);
    }
}

// Draw `parent`'s children (parent -1 = roots) in creation order, each followed by its own children.
static void draw_tree(HDC dc, int parent, const RECT *pr, int parent_shown) {
    int last = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < MAX_EL; i++)
            if (g_el[i].used && g_el[i].parent == parent && g_el[i].seq > last &&
                (best < 0 || g_el[i].seq < g_el[best].seq)) best = i;
        if (best < 0) return;
        last = g_el[best].seq;
        layout(best, pr, parent_shown);
        g_el[best].z = ++g_z;
        if (g_el[best].shown && dc) draw_el(dc, best);
        draw_tree(dc, best, &g_el[best].abs, g_el[best].shown);
    }
}

static LRESULT CALLBACK ui_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static BOOL CALLBACK find_wnd(HWND h, LPARAM lp) {
    RECT r;
    if (IsWindowVisible(h) && GetClientRect(h, &r) && r.right * r.bottom > 0) {
        HWND *best = (HWND *)lp;
        RECT br;
        if (!*best || (GetClientRect(*best, &br) && r.right * r.bottom > br.right * br.bottom)) *best = h;
    }
    return TRUE;
}

static void __fastcall paint_hook(void *screen, void *edx) {
    (void)edx;
    o_paint(screen);
    int any = 0;
    for (int i = 0; i < MAX_EL && !any; i++) any = g_el[i].used;
    int resbar = wl_resbar_active();
    if ((!any && !resbar) || !RD32(screen, 0xc)) return;

    // Hide the UI while another screen is on top (F10 game menu, dialogs: they are screens of
    // their own, the game screen keeps painting under them). Screen manager [0xada4b4]: its
    // first dword is the current screen (0x43c0d0 `mov (%ecx),%eax; ret` is the getter that
    // set-current 0x48a8e0 compares the new screen with). Skipping also leaves g_last_paint
    // old, so mouse and hotkeys go to the game again after 300 ms.
    // The custom resource boxes (resbar.c) are part of the top bar, which the game keeps showing
    // under menus: they stay drawn (the menu paints over them) but get no input then
    // (g_last_paint is not refreshed). Seen 2026-11-25: Esc hid them.
    void *mgr = *(void **)0xada4b4;
    int covered = mgr && *(void **)mgr != screen;
    // In-game popups (F10 menu, chat dialog, ...) are panels of the game screen itself: its
    // focus list at screen+0x128 has the popup on top instead of the map view [screen+0xb04].
    // GameScreen paint 0x6c2e8f..0x6c2ec2 makes the same test (current via 0x43c0d0 = first
    // dword, compared with 0xb04) and then stops repainting the map.
    void *top = *(void **)((char *)screen + 0x128);
    if (top && top != *(void **)((char *)screen + 0xb04)) covered = 1;
    if (covered) {
        if (!resbar) return;
        RECT cs = { (LONG)RD32(screen, 0xb8), (LONG)RD32(screen, 0xbc), 0, 0 };
        cs.right = cs.left + (LONG)RD32(screen, 0xc0);
        cs.bottom = cs.top + (LONG)RD32(screen, 0xc4);
        BYTE cctx[0x34];
        memset(cctx, 0, sizeof cctx);
        ((CtxInit_t)0x49e3c0)(cctx, (void *)RD32(screen, 0xc));
        HDC cdc = ((CtxDC_t)0x49e7b0)(cctx);
        wl_resbar_paint_passive(screen, cdc, &cs);
        ((CtxDone_t)0x49e4b0)(cctx);
        return;
    }

    if (!g_hwnd) {
        HWND h = NULL;
        EnumThreadWindows(GetCurrentThreadId(), find_wnd, (LPARAM)&h);
        if (h) {
            g_hwnd = h;
            g_old_proc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)ui_proc);
            wl_log("ui: input hooked on window %p", (void *)h);
        }
    }
    RECT scr = { (LONG)RD32(screen, 0xb8), (LONG)RD32(screen, 0xbc), 0, 0 };
    scr.right = scr.left + (LONG)RD32(screen, 0xc0);
    scr.bottom = scr.top + (LONG)RD32(screen, 0xc4);
    g_scr_w = scr.right > 0 ? scr.right : 1; g_scr_h = scr.bottom > 0 ? scr.bottom : 1;
    BYTE ctx[0x34];
    memset(ctx, 0, sizeof ctx);
    ((CtxInit_t)0x49e3c0)(ctx, (void *)RD32(screen, 0xc));
    HDC dc = ((CtxDC_t)0x49e7b0)(ctx);
    if (resbar) wl_resbar_paint(screen, dc, &scr);
    if (any) {
        g_z = 0;
        draw_tree(dc, -1, &scr, 1);
        remember_bgs(dc);
    }
    ((CtxDone_t)0x49e4b0)(ctx);
    g_last_paint = GetTickCount();
}

// ---- input ---------------------------------------------------------------------------
// Topmost shown element under the point: `blocking` = has a background (frames, buttons).
// `want_hover` = element type that can receive hover events (frames, buttons; labels only if they have handlers).
static int hit(int x, int y, int *blocking) {
    int best = -1;
    for (int i = 0; i < MAX_EL; i++) {
        El *e = &g_el[i];
        if (!e->used || !e->shown || e->type == T_LABEL) continue;
        if (x < e->abs.left || x >= e->abs.right || y < e->abs.top || y >= e->abs.bottom) continue;
        if (best < 0 || e->z > g_el[best].z) best = i;   // drawn last = on top
    }
    if (blocking) *blocking = best >= 0 && (g_el[best].has_bg || g_el[best].type == T_BUTTON || g_el[best].type == T_TEXTBOX);
    return best;
}

static void to_screen(HWND h, LPARAM lp, int *x, int *y) {
    RECT c;
    int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
    if (GetClientRect(h, &c) && c.right > 0 && c.bottom > 0) {
        mx = MulDiv(mx, g_scr_w, c.right);
        my = MulDiv(my, g_scr_h, c.bottom);
    }
    *x = mx; *y = my;
}

static void update_hover(int new_hover) {
    if (new_hover == g_hover) return;
    // Queue leave for old, enter for new (only for frames and buttons, plus labels if they have hover handlers later)
    if (g_hover >= 0 && g_nhovers < 64) {
        g_hovers[g_nhovers++] = -(g_hover + 1);  // negative = leave event
    }
    if (new_hover >= 0 && g_nhovers < 64) {
        g_hovers[g_nhovers++] = new_hover + 1;   // positive = enter event
    }
    g_hover = new_hover;
}

static int textbox_insert_char(El *e, int c) {
    int len = (int)strlen(e->text);
    if (e->caret < 0) e->caret = 0;
    if (e->caret > len) e->caret = len;
    if (c < 32 || len >= e->maxlen || len >= (int)sizeof e->text - 1) return 0;
    if (e->numeric && !((c >= '0' && c <= '9') || (c == '-' && e->caret == 0 && e->text[0] != '-'))) return 0;
    memmove(e->text + e->caret + 1, e->text + e->caret, (size_t)(len - e->caret + 1));
    e->text[e->caret++] = (char)c;
    return 1;
}

static int is_el_visible(int i) {
    for (int q = i; q >= 0; q = g_el[q].parent) {
        if (!g_el[q].used || !g_el[q].visible) return 0;
    }
    return 1;
}

static int match_hotkey(int key) {
    for (int i = 0; i < MAX_EL; i++) {
        El *e = &g_el[i];
        if (!e->used || e->type != T_BUTTON || !is_el_visible(i)) continue;
        if (e->hotkey == key) {
            if (g_nclicks < 32) g_clicks[g_nclicks++] = i + 1;
            return 1;
        }
    }
    return 0;
}

static int current_mods(void) {
    int mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= 0x100;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= 0x200;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= 0x400;
    return mods;
}

static LRESULT CALLBACK ui_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    g_game_hwnd = h;
    if (msg == WM_WL_RESTART) {
        // Deferred GameScreen::restart_game (0x6c6470 -> TRIBE_Game::restart_game 0x6350c0),
        // invoked cleanly from the Windows message pump just like clicking Restart in the menu.
        void *gs = *(void **)0xaf3d90;
        if (gs && !wl_is_multiplayer()) {
            g_map_capture = 0;
            g_no_unit_control = 0;
            g_map_dragging = 0;
            g_nmclicks = 0;
            typedef void (__thiscall *RestartGame_t)(void *screen);
            ((RestartGame_t)0x6c6470)(gs);
        }
        return 0;
    }
    int active = GetTickCount() - g_last_paint < 300;     // the game screen is on and painting

    if (wl_resbar_wndproc(h, msg, wp, lp, active, g_scr_w, g_scr_h)) return 0;

    // Focused text box: it takes the keyboard (the game gets no keys, so no hotkeys fire).
    if (g_focus >= 0 && (!active || !is_el_visible(g_focus))) g_focus = -1;
    if (g_focus >= 0 && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR)) {
        El *e = &g_el[g_focus];
        int len = (int)strlen(e->text);
        if (e->caret < 0) e->caret = 0;
        if (e->caret > len) e->caret = len;
        if (msg == WM_KEYDOWN) {
            if (!(lp & (1 << 30))) {
                int mods = current_mods();
                if (wp == VK_ESCAPE && mods == 0) {
                    if (match_hotkey(VK_ESCAPE)) return 0;
                    if (g_nkeys < 64) g_keys[g_nkeys++] = ((g_focus + 1) << 16) | VK_ESCAPE;
                    return 0;
                }
                if ((mods & 0x500) && !(mods == 0x100 && wp == 'V') && match_hotkey((int)wp | mods))
                    return 0;
            }
            if (wp == VK_LEFT) { if (e->caret > 0) e->caret--; }
            else if (wp == VK_RIGHT) { if (e->caret < len) e->caret++; }
            else if (wp == VK_HOME) { e->caret = 0; }
            else if (wp == VK_END) { e->caret = len; }
            else if (wp == VK_DELETE) {
                if (e->caret < len) {
                    memmove(e->text + e->caret, e->text + e->caret + 1, (size_t)(len - e->caret));
                    if (g_ntev < 64) g_tev[g_ntev++] = g_focus + 1;
                }
            } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                if (OpenClipboard(h)) {
                    HANDLE clip = GetClipboardData(CF_TEXT);
                    const char *s = clip ? (const char *)GlobalLock(clip) : NULL;
                    if (s) {
                        int added = 0;
                        for (; *s; s++) {
                            if ((unsigned char)*s >= 32 && textbox_insert_char(e, (unsigned char)*s)) added = 1;
                        }
                        GlobalUnlock(clip);
                        if (added && g_ntev < 64) g_tev[g_ntev++] = g_focus + 1;
                    }
                    CloseClipboard();
                }
            } else if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_TAB) {
                if (g_nkeys < 64) g_keys[g_nkeys++] = ((g_focus + 1) << 16) | (int)(wp & 0xffff);
            }
        } else if (msg == WM_CHAR) {
            int c = (int)(unsigned char)wp;
            if (c == 8) {                                  // Backspace
                if (e->caret > 0) {
                    memmove(e->text + e->caret - 1, e->text + e->caret, (size_t)(len - e->caret + 1));
                    e->caret--;
                    if (g_ntev < 64) g_tev[g_ntev++] = g_focus + 1;
                }
            } else if (c == 13) {                          // Enter: submit, keep the text
                if (g_ntev < 64) g_tev[g_ntev++] = -(g_focus + 1);
                g_focus = -1;
            } else if (c == 27) {                          // Esc: just leave the box
                g_focus = -1;
            } else if (textbox_insert_char(e, c)) {
                if (g_ntev < 64) g_tev[g_ntev++] = g_focus + 1;
            }
        }
        return 0;
    }

    // Hotkey handling (WM_KEYDOWN and WM_SYSKEYDOWN for Alt combos)
    if (active && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        // Check for auto-repeat (lParam bit 30)
        if (!(lp & (1 << 30))) {
            int key = (int)wp | current_mods();
            if (match_hotkey(key)) return 0;  // swallow the key
        }
    }

    if (active && msg == WM_MOUSEWHEEL) {
        POINT pt = { (short)(WORD)lp, (short)(WORD)(lp >> 16) };
        ScreenToClient(h, &pt);
        int x, y, blocking;
        to_screen(h, MAKELPARAM(pt.x, pt.y), &x, &y);
        int i = hit(x, y, &blocking);
        if (blocking && i >= 0) {
            int delta = (short)(WORD)(wp >> 16);
            if (g_nscrolls < 64) g_scrolls[g_nscrolls++] = ((i + 1) << 16) | (delta > 0 ? 1 : 2);
            return 0;
        }
    }

    if (active && (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
                   msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP)) {
        int x, y, blocking;
        to_screen(h, lp, &x, &y);
        int i = hit(x, y, &blocking);
        if (msg == WM_MOUSEMOVE) {
            update_hover(i);                              // frames and buttons (hit skips labels)
            if (g_map_dragging && g_map_capture && !blocking && (wp & MK_LBUTTON)) {
                DWORD now_t = GetTickCount();
                if (now_t - g_last_drag_tick >= 110) {
                    float tx = 0, ty = 0;
                    if (pick_map_tile(x, y, &tx, &ty) && g_nmclicks < 32) {
                        g_mclicks[g_nmclicks].x = tx;
                        g_mclicks[g_nmclicks].y = ty;
                        g_mclicks[g_nmclicks].btn = 1;
                        g_nmclicks++;
                        g_last_drag_tick = now_t;
                    }
                }
                return 0;
            }
        } else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
            g_focus = (i >= 0 && g_el[i].type == T_TEXTBOX) ? i : -1;   // click elsewhere = leave the box
            if (blocking) {
                g_pressed = (g_el[i].type == T_BUTTON) ? i : -1;
                return 0;
            }
            if (g_map_capture) {
                float tx = 0, ty = 0;
                if (pick_map_tile(x, y, &tx, &ty)) {
                    if (g_nmclicks < 32) {
                        g_mclicks[g_nmclicks].x = tx;
                        g_mclicks[g_nmclicks].y = ty;
                        g_mclicks[g_nmclicks].btn = 1;
                        g_nmclicks++;
                    }
                    g_map_dragging = 1;
                    g_last_drag_tick = GetTickCount();
                    return 0;   // consume left click while deploying
                }
            }
        } else if (msg == WM_LBUTTONUP) {
            g_map_dragging = 0;
            if (g_pressed >= 0) {
                if (i == g_pressed && g_nclicks < 32) g_clicks[g_nclicks++] = i + 1;
                g_pressed = -1;
                return 0;
            }
            if (blocking) return 0;
        } else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
            if (blocking) return 0;
            if (msg == WM_RBUTTONDOWN && g_map_capture) {
                float tx = 0, ty = 0;
                if (pick_map_tile(x, y, &tx, &ty) && g_nmclicks < 32) {
                    g_mclicks[g_nmclicks].x = tx;
                    g_mclicks[g_nmclicks].y = ty;
                    g_mclicks[g_nmclicks].btn = 2;
                    g_nmclicks++;
                }
                return 0;   // right-click cancels placement cleanly without ordering units
            }
            if (g_no_unit_control && in_main_view(x, y)) return 0;  // block manual move/attack orders
        } else if (blocking) return 0;                    // right button over our UI
    } else if (!active) {
        update_hover(-1);
        g_pressed = -1;
        g_map_dragging = 0;
    }
    return CallWindowProcA(g_old_proc, h, msg, wp, lp);
}

// From the frame tick (luagame.c): deliver queued clicks and hover events to Lua.
void wl_ui_frame(void) {
    if (g_scr_w > 1 && g_scr_h > 1 && (g_scr_w != g_last_scr_w || g_scr_h != g_last_scr_h)) {
        g_last_scr_w = g_scr_w;
        g_last_scr_h = g_scr_h;
        wl_script_event_int("ui_resize", (g_scr_w << 16) | (g_scr_h & 0xffff));
    }
    int nm = g_nmclicks;
    if (nm) {
        MapClickEv ev[32];
        memcpy(ev, g_mclicks, sizeof(MapClickEv) * (size_t)nm);
        g_nmclicks = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < nm; i++) wl_script_event_map_click(ev[i].x, ev[i].y, ev[i].btn);
        wl_cmd_set_local_ctx(0);
    }
    int n = g_nclicks;
    if (n) {
        int ids[32];
        memcpy(ids, g_clicks, sizeof(int) * (size_t)n);
        g_nclicks = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < n; i++) if (get(ids[i])) wl_script_event_int("ui_click", ids[i]);
        wl_cmd_set_local_ctx(0);
    }

    int nt = g_ntev;
    if (nt) {
        int ev[64];
        memcpy(ev, g_tev, sizeof(int) * (size_t)nt);
        g_ntev = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < nt; i++) {
            int id = ev[i] > 0 ? ev[i] : -ev[i];
            if (get(id)) wl_script_event_int(ev[i] > 0 ? "ui_text" : "ui_submit", id);
        }
        wl_cmd_set_local_ctx(0);
    }

    int nk = g_nkeys;
    if (nk) {
        int keys[64];
        memcpy(keys, g_keys, sizeof(int) * (size_t)nk);
        g_nkeys = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < nk; i++) if (get(keys[i] >> 16)) wl_script_event_int("ui_key", keys[i]);
        wl_cmd_set_local_ctx(0);
    }

    int ns = g_nscrolls;
    if (ns) {
        int scrolls[64];
        memcpy(scrolls, g_scrolls, sizeof(int) * (size_t)ns);
        g_nscrolls = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < ns; i++) if (get(scrolls[i] >> 16)) wl_script_event_int("ui_scroll", scrolls[i]);
        wl_cmd_set_local_ctx(0);
    }

    int nh = g_nhovers;
    if (nh) {
        int events[64];
        memcpy(events, g_hovers, sizeof(int) * (size_t)nh);
        g_nhovers = 0;
        wl_cmd_set_local_ctx(1);
        for (int i = 0; i < nh; i++) {
            int id = events[i] < 0 ? -events[i] : events[i];
            if (!get(id)) continue;
            if (events[i] < 0) {
                wl_script_event_int("ui_leave", id);
            } else {
                wl_script_event_int("ui_enter", id);
            }
        }
        wl_cmd_set_local_ctx(0);
    }
}

void wl_install_uikit(void) {
    void **slot = (void **)0x9d5300 + 20;
    if ((DWORD)*slot != 0x6c2e40) { wl_log("ui: GameScreen paint slot unexpected, Lua UI off"); return; }
    DWORD old;
    o_paint = (Paint_t)*slot;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return;
    *slot = (void *)paint_hook;
    VirtualProtect(slot, 4, old, &old);
    wl_log("ui: GameScreen paint hooked (Lua UI)");
}
