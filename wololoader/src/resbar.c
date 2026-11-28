// Top resource bar HUD overlay and "Show all" panel for custom resources.
// Driven by uikit.c's GameScreen::paint hook (0x6c2e40) and WNDPROC subclass so
// it shares the screen HDC, popup visibility checks, and coordinate scaling without
// using the 256-slot Lua UI element pool.
//
// Verified addresses / offsets (AoK HD.exe 5.8, re-verified via objdump):
// - [screen + 0xb0c] = TRIBE_Panel_Inven* (panel rect: +0xb8 x, +0xbc y, +0xc0 w, +0xc4 h;
//   font wrapper ptr at +0x15c, HFONT at [wrapper + 8], verified 0x712ee4).
// - [screen + 0xb10] = TRIBE_Panel_Object* (verified in GameScreen::ctor at 0x6b4651:
//   89 87 10 0b 00 00, right after pushing "Panel_Object" 0x9d54f0):
//   +0x1c visible, +0xb8/+0xbc/+0xc0/+0xc4 rect, +0xbd4 drawn item count (reset to 0 at
//   0x713f90, incremented per draw_item at 0x716bf1), +0xbf0 WorldPlayer* ([plr+0x254]
//   selection count, skip if >1 at 0x713fe4), +0xbf4 selected RGE_Static_Object* (0x713fd0),
//   +0xc04 mode byte (skip if ==2 at 0x713fbb).
//   Object: +0x0c master*, +0x10 owner*, +0x24 removed, +0x4c state (<7 alive), +0x48 float
//   attribute_amount_held (0x715154 / 0x715a2b, rounded via +0.5f from 0x9afd68), +0x50 int16
//   attribute_type_held (0x7150bc / 0x71592e). Master: +0x10 id, +0x16 class_, +0x97
//   command_attribute (1=deposit, 3/4/5/6/7/9/12=unit), +0xa4 garrison flags.
//   draw_item (0x7167c0): row r (0..3) icon at (x+15, y+72+r*20), text at (x+57, y+74+r*20),
//   font = (*(void***)0xada2a8)[6]->+8 (0x716b03..0x716b4b, 0x472020), black text (0xada500).
// - [screen + 0xc6c] = center Age badge panel* (+0xb8 x).
// - [screen + 0xd58] = GameScreen full-redraw flag (checked at 0x6c2ee9: cmp [edi+0xd58], 0;
//   setting to 1 forces GameScreen::draw on the next paint to clear closed panels/tooltips).
// - Local player resources: world = [[0xaf2c58]+0xc8], local pid = [world+0x174],
//   player = *([world+0x184] + pid*8), count = [player+0x38], float* = [player+0x3c].
#define COBJMACROS
#include "wololoader.h"
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDP(p, o) (*(void **)((char *)(p) + (o)))
#define RD32(p, o) (*(DWORD *)((char *)(p) + (o)))

// One custom resource box = one vanilla box: TRIBE_Panel_Inven (frame 6 of game_b*.slp, 392x25)
// holds 5 boxes at a 77 px pitch (numbers right-aligned at x = 75 + 77*k, 0x712e80 / 0x713150).
// Measured on game_b1.slp frame 6: box k spans x = 77*k - 3 .. +77, grey bevel frame, black
// interior y = 4..19, resource picture at the left, number at the right.
#define SLOT_W       77
#define BOX_SRC_K    3      // stone box: copied as the frame for our boxes
#define BOX_IN_TOP   4
#define BOX_IN_BOT   20
#define BTN_W        54
#define PANEL_W      286
#define PANEL_HDR_H  28
#define PANEL_ROW_H  24
#define PANEL_MAX_ROWS 12

typedef BOOL (WINAPI *AlphaBlend_t)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);

typedef struct {
    char path[MAX_PATH];
    HBITMAP bmp;
    int w, h;
    int failed;
} ResIcon;

static ResIcon *g_icons;
static int g_nicons, g_icon_cap;

static HDC g_strip_dc;
static HBITMAP g_strip_bmp;
static RECT g_strip_rect;
static void *g_strip_screen;

// Clean-background backing store for the bottom-left TRIBE_Panel_Object custom gather row
// so changing amounts or villager drop-offs never ghost on frames where the panel is not redrawn.
static HDC g_up_bg_dc;
static HBITMAP g_up_bg_bmp;
static int g_up_bg_w, g_up_bg_h;
static RECT g_up_rect;
static void *g_up_screen;
static void *g_up_obj;
static int g_up_row;
static int g_up_drawn;

static void *g_last_screen;
static int g_panel_open;
static int g_scroll;
static int g_hover_slot = -1;       // index into displayed inline slots, or -1
static int g_prev_hover_slot = -1;
static int g_hover_btn;
static int g_hover_close;

// Layout captured on paint for hit-testing in WNDPROC
typedef struct {
    RECT r;
    int res_idx;
} InlineSlot;

static InlineSlot g_slots[64];
static int g_nslots;
static int g_has_btn;
static RECT g_btn_rect;
static RECT g_panel_rect;
static RECT g_close_rect;
static int g_total_rows;
static int g_vis_rows;

static AlphaBlend_t get_ab(void) {
    static AlphaBlend_t fn;
    static int tried;
    if (!tried) {
        tried = 1;
        HMODULE m = LoadLibraryA("msimg32.dll");
        fn = m ? (AlphaBlend_t)(void *)GetProcAddress(m, "AlphaBlend") : NULL;
    }
    return fn;
}

static HFONT fallback_font(int size, int bold) {
    static struct { int size, bold; HFONT f; } cache[8];
    static int n;
    for (int i = 0; i < n; i++)
        if (cache[i].size == size && cache[i].bold == bold) return cache[i].f;
    HFONT f = CreateFontA(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH, "Arial");
    if (n < 8) { cache[n].size = size; cache[n].bold = bold; cache[n].f = f; n++; }
    return f;
}

static void trigger_screen_redraw(void) {
    if (g_last_screen) {
        *(int *)((char *)g_last_screen + 0xd58) = 1;
    }
}

static ResIcon *load_res_icon(const char *path) {
    if (!path || !path[0]) return NULL;
    for (int i = 0; i < g_nicons; i++) {
        if (strcmp(g_icons[i].path, path) == 0) {
            return g_icons[i].failed ? NULL : &g_icons[i];
        }
    }
    if (g_nicons >= g_icon_cap) {
        int ncap = g_icon_cap ? g_icon_cap * 2 : 32;
        ResIcon *ni = (ResIcon *)realloc(g_icons, (size_t)ncap * sizeof(*ni));
        if (!ni) return NULL;
        g_icons = ni;
        g_icon_cap = ncap;
    }
    ResIcon *ic = &g_icons[g_nicons++];
    memset(ic, 0, sizeof(*ic));
    lstrcpynA(ic->path, path, sizeof ic->path);
    ic->failed = 1;

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
    if (FAILED(hr)) goto done;

    wchar_t wpath[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH)) goto done;

    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, wpath, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) goto done;
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) goto done;
    hr = IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 512 || h > 512) goto done;
    hr = IWICImagingFactory_CreateFormatConverter(factory, &conv);
    if (FAILED(hr)) goto done;
    hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame,
                                        &GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, NULL, 0.0,
                                        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto done;

    UINT stride = w * 4, bytes = stride * h;
    BYTE *px = (BYTE *)malloc(bytes);
    if (!px) goto done;
    hr = IWICFormatConverter_CopyPixels(conv, NULL, stride, bytes, px);
    if (FAILED(hr)) { free(px); goto done; }

    for (UINT i = 0; i < bytes; i += 4) {
        BYTE a = px[i + 3];
        if (a != 255) {
            px[i + 0] = (BYTE)((px[i + 0] * a) / 255);
            px[i + 1] = (BYTE)((px[i + 1] * a) / 255);
            px[i + 2] = (BYTE)((px[i + 2] * a) / 255);
        }
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL);
    bmp = CreateDIBitmap(sdc, &bi.bmiHeader, CBM_INIT, px, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, sdc);
    free(px);

done:
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (need_uninit) CoUninitialize();

    if (!bmp) return NULL;
    ic->bmp = bmp;
    ic->w = (int)w;
    ic->h = (int)h;
    ic->failed = 0;
    return ic;
}

static void draw_icon(HDC dc, const char *path, int x, int y, int sz) {
    ResIcon *ic = load_res_icon(path);
    if (ic && ic->bmp) {
        HDC mem = CreateCompatibleDC(dc);
        if (mem) {
            HGDIOBJ old = SelectObject(mem, ic->bmp);
            AlphaBlend_t ab = get_ab();
            if (ab) {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                ab(dc, x, y, sz, sz, mem, 0, 0, ic->w, ic->h, bf);
            } else {
                StretchBlt(dc, x, y, sz, sz, mem, 0, 0, ic->w, ic->h, SRCCOPY);
            }
            SelectObject(mem, old);
            DeleteDC(mem);
            return;
        }
    }
    // Fallback icon when PNG is missing: small golden diamond emblem
    POINT pts[4] = {
        { x + sz / 2, y + 1 },
        { x + sz - 1, y + sz / 2 },
        { x + sz / 2, y + sz - 1 },
        { x + 1,      y + sz / 2 }
    };
    HBRUSH br = CreateSolidBrush(RGB(224, 184, 72));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 64, 18));
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    Polygon(dc, pts, 4);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_shadow_text(HDC dc, const char *txt, const RECT *r, UINT fmt, COLORREF fg) {
    // Matches TRIBE_Panel_Inven::draw_item (0x713150): 1px black shadow at (-1, +1)
    RECT sh = *r;
    OffsetRect(&sh, -1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextA(dc, txt, -1, &sh, fmt);
    SetTextColor(dc, fg);
    RECT fg_r = *r;
    DrawTextA(dc, txt, -1, &fg_r, fmt);
}

static void capture_or_restore_strip(HDC dc, void *screen, const RECT *r) {
    int w = r->right - r->left;
    int h = r->bottom - r->top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 64) return;

    if (g_strip_screen != screen || !g_strip_dc || !g_strip_bmp || !EqualRect(&g_strip_rect, r)) {
        if (g_strip_dc)  { DeleteDC(g_strip_dc); g_strip_dc = NULL; }
        if (g_strip_bmp) { DeleteObject(g_strip_bmp); g_strip_bmp = NULL; }
        g_strip_dc = CreateCompatibleDC(dc);
        g_strip_bmp = g_strip_dc ? CreateCompatibleBitmap(dc, w, h) : NULL;
        if (g_strip_bmp) {
            SelectObject(g_strip_dc, g_strip_bmp);
            GdiFlush();
            BitBlt(g_strip_dc, 0, 0, w, h, dc, r->left, r->top, SRCCOPY);
            g_strip_rect = *r;
            g_strip_screen = screen;
        } else {
            if (g_strip_dc) { DeleteDC(g_strip_dc); g_strip_dc = NULL; }
            g_strip_screen = NULL;
        }
        return;
    }
    BitBlt(dc, r->left, r->top, w, h, g_strip_dc, 0, 0, SRCCOPY);
}

static int local_res_value(int res_id, float *out) {
    DWORD game = *(DWORD *)0xaf2c58;
    void *world = game ? RDP((void *)game, 0xc8) : NULL;
    if (!world) return 0;
    int pid = (int)RD32(world, 0x174);
    char *tab = (char *)RDP(world, 0x184), *end = (char *)RDP(world, 0x188);
    int n = (tab && end > tab) ? (int)((end - tab) / 8) : 0;
    if (pid < 0 || pid >= n) return 0;
    void *p = *(void **)(tab + pid * 8);
    if (!p) return 0;
    int cnt = (int)RD32(p, 0x38);
    float *arr = (float *)RDP(p, 0x3c);
    if (!arr || res_id < 0 || res_id >= cnt) return 0;
    *out = arr[res_id];
    return 1;
}

void wl_resbar_reset(void) {
    g_panel_open = 0;
    g_scroll = 0;
    g_hover_slot = -1;
    g_prev_hover_slot = -1;
    g_hover_btn = 0;
    g_hover_close = 0;
    g_strip_screen = NULL;
    g_up_drawn = 0;
    g_up_screen = NULL;
    g_up_obj = NULL;
    for (int i = 0; i < g_nicons; i++) {
        if (g_icons[i].bmp) DeleteObject(g_icons[i].bmp);
    }
    g_nicons = 0;
}

int wl_resbar_active(void) {
    int n = 0, gn = 0;
    wl_resources_list(&n);
    wl_gather_list(&gn);
    return n > 0 || gn > 0;
}

// Bottom-left unit panel (TRIBE_Panel_Object at [screen+0xb10]): vanilla TRIBE_Panel_Object::draw
// (0x713f00) bails out on attribute_type_held (+0x50) > 0x38 (deposit, 0x7150c5) or > 0x39
// (villager, 0x715932). We draw the missing resource icon and amount at the exact row
// r = [panel+0xbd4] (0..3) and coordinates of draw_item (0x7167c0): icon (x+15, y+72+r*20),
// text (x+57, y+74+r*20) in font 6 ([*(void***)0xada2a8][6]->+8), black on parchment.
static void paint_unit_panel(void *screen, HDC dc, const RECT *scr) {
    int gcount = 0;
    wl_gather_list(&gcount);
    if (gcount <= 0) return;

    void *panel = RDP(screen, 0xb10);
    if (!panel || RD32(panel, 0x1c) == 0) {
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }

    unsigned char mode = *(unsigned char *)((char *)panel + 0xc04);
    void *obj = RDP(panel, 0xbf4);
    void *plr = RDP(panel, 0xbf0);
    int sel_cnt = plr ? (int)RD32(plr, 0x254) : 0;
    if (mode == 2 || !obj || !plr || sel_cnt > 1) {
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }

    // Alive/valid check matching gameapi.c: state < 7 and removed flag == 0.
    if (*(unsigned char *)((char *)obj + 0x4c) >= 7 || RD32(obj, 0x24) != 0) {
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }
    void *master = RDP(obj, 0x0c);
    if (!master) {
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }

    unsigned char cmd_attr = *(unsigned char *)((char *)master + 0x97);
    short cls = *(short *)((char *)master + 0x16);
    short master_id = *(short *)((char *)master + 0x10);
    short rid = *(short *)((char *)obj + 0x50);
    float amt = *(float *)((char *)obj + 0x48);
    int shown = (int)(amt + 0.5f);  // matches 0x71515b / 0x715909 / 0x715a32 (+0.5f @ 0x9afd68)

    const WlGatherDef *gd = NULL;
    int should_draw = 0;
    if (cmd_attr == 1 && cls != 0x28) {
        // Gaia resource deposit (0x714ca0..0x7150bc)
        if (rid >= WL_CUSTOM_RES_FIRST) gd = wl_gather_by_resource(rid);
        if (!gd) gd = wl_gather_by_deposit(master_id);
        if (gd) {
            if (shown < 0) shown = 0;
            should_draw = 1;
        }
    } else if (cmd_attr == 3 || cmd_attr == 4 || cmd_attr == 5 || cmd_attr == 6 ||
               cmd_attr == 7 || cmd_attr == 9 || cmd_attr == 12) {
        // Owned unit/villager carrying a custom gather resource (0x714c61..0x715935).
        // Vanilla only shows carried amount for local player's units without garrison display
        // ([obj+0x10] == [panel+0xbf0] @ 0x715786, ([master+0xa4] & 1) == 0 @ 0x715795, shown > 0 @ 0x715911).
        if (RDP(obj, 0x10) == plr && ((*(unsigned char *)((char *)master + 0xa4)) & 1) == 0 &&
            rid >= WL_CUSTOM_RES_FIRST && shown > 0) {
            gd = wl_gather_by_resource(rid);
            if (gd) should_draw = 1;
        }
    }

    int r = (int)RD32(panel, 0xbd4);
    if (r < 0 || r > 3) should_draw = 0;

    if (!should_draw) {
        // If the same object was showing our row last frame (e.g. villager just deposited its
        // load so shown dropped to 0), restore the clean parchment behind our row so nothing ghosts.
        if (g_up_drawn && g_up_screen == screen && g_up_obj == obj && g_up_bg_dc && g_up_bg_bmp) {
            int rw = g_up_rect.right - g_up_rect.left;
            int rh = g_up_rect.bottom - g_up_rect.top;
            if (rw > 0 && rh > 0) {
                BitBlt(dc, g_up_rect.left, g_up_rect.top, rw, rh, g_up_bg_dc, 0, 0, SRCCOPY);
            }
        }
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }

    const WlResourceDef *res = wl_resource_by_id(gd->resource_id);
    if (!res) {
        g_up_drawn = 0;
        g_up_obj = NULL;
        return;
    }

    int px = (int)RD32(panel, 0xb8);
    int py = (int)RD32(panel, 0xbc);
    int pw = (int)RD32(panel, 0xc0);
    int icon_x = px + 15;             // verified 0x716848: lea eax, [esi+0xf]
    int icon_y = py + 72 + r * 20;    // verified 0x71683b..0x716844: y + (r*5 + 18)*4
    int text_x = px + 57;             // verified 0x716b89: add ecx, 0x39
    int text_y = py + 74 + r * 20;    // verified 0x716b92..0x716b9c: y + r*20 + 0x4a

    int row_right = px + (pw > 160 ? 160 : (pw > 70 ? pw - 2 : 160));
    RECT row_r = { px + 14, icon_y - 1, row_right, icon_y + 20 };
    if (row_r.left < scr->left) row_r.left = scr->left;
    if (row_r.top < scr->top) row_r.top = scr->top;
    if (row_r.right > scr->right) row_r.right = scr->right;
    if (row_r.bottom > scr->bottom) row_r.bottom = scr->bottom;
    int rw = row_r.right - row_r.left;
    int rh = row_r.bottom - row_r.top;
    if (rw <= 0 || rh <= 0 || rw > 512 || rh > 64) return;

    if (g_up_drawn && g_up_screen == screen && g_up_obj == obj && g_up_row == r &&
        EqualRect(&g_up_rect, &row_r) && g_up_bg_dc && g_up_bg_bmp) {
        BitBlt(dc, row_r.left, row_r.top, rw, rh, g_up_bg_dc, 0, 0, SRCCOPY);
    } else {
        if (!g_up_bg_dc || !g_up_bg_bmp || rw > g_up_bg_w || rh > g_up_bg_h) {
            if (g_up_bg_dc)  { DeleteDC(g_up_bg_dc); g_up_bg_dc = NULL; }
            if (g_up_bg_bmp) { DeleteObject(g_up_bg_bmp); g_up_bg_bmp = NULL; }
            g_up_bg_dc = CreateCompatibleDC(dc);
            g_up_bg_bmp = g_up_bg_dc ? CreateCompatibleBitmap(dc, rw, rh) : NULL;
            if (g_up_bg_bmp) {
                SelectObject(g_up_bg_dc, g_up_bg_bmp);
                g_up_bg_w = rw;
                g_up_bg_h = rh;
            } else {
                if (g_up_bg_dc) { DeleteDC(g_up_bg_dc); g_up_bg_dc = NULL; }
                g_up_bg_w = g_up_bg_h = 0;
            }
        }
        if (g_up_bg_dc && g_up_bg_bmp) {
            GdiFlush();
            BitBlt(g_up_bg_dc, 0, 0, rw, rh, dc, row_r.left, row_r.top, SRCCOPY);
        }
        g_up_screen = screen;
        g_up_obj = obj;
        g_up_row = r;
        g_up_rect = row_r;
        g_up_drawn = 1;
    }

    draw_icon(dc, res->icon, icon_x, icon_y, 20);

    // Borrow TRIBE_Panel_Object::draw_item's exact font 6: (*(void***)0xada2a8)[6]->+8
    // (verified 0x716b03..0x716b4b, 0x472020) and black ink (0xada500, 0x716b20..0x716b42).
    HFONT panel_font = NULL;
    void **fm = *(void ***)0xada2a8;
    if (fm && fm[6]) panel_font = (HFONT)RDP(fm[6], 8);
    if (!panel_font) panel_font = fallback_font(13, 0);

    HGDIOBJ old_font = SelectObject(dc, panel_font);
    int old_bk = SetBkMode(dc, TRANSPARENT);
    COLORREF old_col = GetTextColor(dc);
    SetTextColor(dc, RGB(0, 0, 0));

    char num[32];
    snprintf(num, sizeof num, "%d", shown);
    RECT tr = { text_x, text_y, row_r.right, row_r.bottom };
    DrawTextA(dc, num, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, old_col);
    SetBkMode(dc, old_bk);
    SelectObject(dc, old_font);
}

void wl_resbar_paint(void *screen, HDC dc, const RECT *scr);

// Under a menu or dialog: the boxes stay, but the "Show all" panel, hover and tooltip go away
// (no input reaches them while the menu is open).
void wl_resbar_paint_passive(void *screen, HDC dc, const RECT *scr) {
    g_panel_open = 0;
    g_hover_slot = -1;
    g_hover_btn = 0;
    g_hover_close = 0;
    wl_resbar_paint(screen, dc, scr);
}

static int res_visible_in_hud(const WlResourceDef *r, const char *cur_mode) {
    if (!r || r->hidden) return 0;
    if (r->game_mode[0]) {
        if (!cur_mode || !cur_mode[0] || strcmp(cur_mode, r->game_mode) != 0) return 0;
    }
    return 1;
}

void wl_resbar_paint(void *screen, HDC dc, const RECT *scr) {
    if (!dc || !screen || !scr) return;
    int total = 0, gtotal = 0;
    const WlResourceDef *defs = wl_resources_list(&total);
    wl_gather_list(&gtotal);
    if (total <= 0 && gtotal <= 0) return;

    g_last_screen = screen;
    if (!defs || total <= 0) {
        paint_unit_panel(screen, dc, scr);
        return;
    }

    const char *cur_mode = wl_gamemode_current_key();

    // Collect resources eligible for HUD display (not hidden, matching active game_mode if set)
    int panel_idx[256];
    int panel_n = 0;
    int vis_idx[64];
    int vis_n = 0;
    for (int i = 0; i < total; i++) {
        if (!res_visible_in_hud(&defs[i], cur_mode)) continue;
        if (panel_n < 256) panel_idx[panel_n++] = i;
        if (defs[i].show && vis_n < 64) vis_idx[vis_n++] = i;
    }

    if (panel_n <= 0) {
        g_nslots = 0;
        g_has_btn = 0;
        g_panel_open = 0;
        g_total_rows = 0;
        g_vis_rows = 0;
        if (gtotal > 0) paint_unit_panel(screen, dc, scr);
        return;
    }

    // Live vanilla top-bar geometry: TRIBE_Panel_Inven at [screen+0xb0c],
    // center Age badge at [screen+0xc6c].
    void *inven = RDP(screen, 0xb0c);
    void *age   = RDP(screen, 0xc6c);
    int inv_x = inven ? (int)RD32(inven, 0xb8) : 0;
    int inv_y = inven ? (int)RD32(inven, 0xbc) : 0;
    int inv_w = inven ? (int)RD32(inven, 0xc0) : 396;
    int age_x = age   ? (int)RD32(age, 0xb8)   : (scr->right - 260 + inv_w) / 2 - 82;

    int x0 = inv_x + inv_w + 4;
    int x1 = age_x - 8;
    if (x1 < x0) x1 = x0;
    int avail_w = x1 - x0;

    int need_btn = (vis_n * SLOT_W > avail_w) || (vis_n < panel_n);
    int max_inline = need_btn ? ((avail_w > BTN_W) ? (avail_w - BTN_W) / SLOT_W : 0) : vis_n;
    if (max_inline > vis_n) max_inline = vis_n;
    if (max_inline > 64) max_inline = 64;

    // Restore clean vanilla top-bar art behind our slots so changing numbers never ghost
    int used_w = max_inline * SLOT_W + (need_btn ? BTN_W + 4 : 0);
    if (used_w > avail_w && avail_w > 0) used_w = avail_w;
    if (need_btn && used_w < BTN_W + 4) used_w = BTN_W + 4;
    RECT strip_r = { x0, inv_y, x0 + used_w, inv_y + 26 };
    if (used_w > 0) capture_or_restore_strip(dc, screen, &strip_r);

    // Borrow TRIBE_Panel_Inven's exact live HFONT ([inven+0x15c]->font at +8, verified 0x712ee4)
    HFONT vanilla_font = NULL;
    if (inven) {
        void *fwrap = RDP(inven, 0x15c);
        if (fwrap) vanilla_font = (HFONT)RDP(fwrap, 8);
    }
    if (!vanilla_font) vanilla_font = fallback_font(13, 0);

    HGDIOBJ old_font = SelectObject(dc, vanilla_font);
    int old_bk = SetBkMode(dc, TRANSPARENT);
    COLORREF old_col = GetTextColor(dc);

    int text_y = (scr->right == 1280) ? (inv_y + 10) : (inv_y + 4);
    int icon_y = (scr->right == 1280) ? (inv_y + 8)  : (inv_y + 4);

    g_nslots = 0;
    for (int i = 0; i < max_inline; i++) {
        int ridx = vis_idx[i];
        const WlResourceDef *r = &defs[ridx];
        int sx = x0 + i * SLOT_W;
        float val = 0.0f;
        local_res_value(r->id, &val);
        char num[32];
        snprintf(num, sizeof num, "%d", (int)val);

        // The game's own box art: copy the stone box drawn this frame, clear its picture and number,
        // then our icon in the picture area and the number where the game puts its numbers.
        if (inven) {
            int src_l = inv_x + 77 * BOX_SRC_K - 3;
            // 80 px: the box plus its right bevel (the next box starts 77 px on, like the game's).
            BitBlt(dc, sx, inv_y, SLOT_W + 3, 25, dc, src_l, inv_y, SRCCOPY);
            // The stone picture also covers parts of the bevel, so the left part is rebuilt from
            // one clean column of the box (bevel + black interior, x = +45), stretched.
            StretchBlt(dc, sx + 3, inv_y, 42, 25, dc, src_l + 45, inv_y, 1, 25, SRCCOPY);
            // The black interior ends before the right bevel (x = +75); clear all of it so an old,
            // longer number never shows through (seen as "4" over "9", 2026-11-26).
            RECT in = { sx + 45, inv_y + BOX_IN_TOP, sx + 75, inv_y + BOX_IN_BOT };
            HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dc, &in, black);
        }
        (void)icon_y;
        draw_icon(dc, r->icon, sx + 7, inv_y + BOX_IN_TOP, BOX_IN_BOT - BOX_IN_TOP);
        RECT tr = { sx + 26, text_y, sx + 69, inv_y + 24 };   // inside the interior, clear of the bevel like vanilla
        draw_shadow_text(dc, num, &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX, RGB(255, 255, 255));

        g_slots[g_nslots].r.left   = sx;
        g_slots[g_nslots].r.top    = inv_y;
        g_slots[g_nslots].r.right  = sx + SLOT_W;
        g_slots[g_nslots].r.bottom = inv_y + 26;
        g_slots[g_nslots].res_idx  = ridx;
        g_nslots++;
    }

    g_has_btn = need_btn;
    if (need_btn) {
        int bx = x0 + max_inline * SLOT_W + 2;
        if (bx + BTN_W > scr->right - 266) bx = x0;
        g_btn_rect.left   = bx;
        g_btn_rect.top    = inv_y + 3;
        g_btn_rect.right  = bx + BTN_W;
        g_btn_rect.bottom = inv_y + 23;

        COLORREF bg = (g_panel_open || g_hover_btn) ? RGB(74, 54, 32) : RGB(46, 34, 22);
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, &g_btn_rect, br);
        DeleteObject(br);
        HBRUSH bd = CreateSolidBrush(g_panel_open ? RGB(240, 200, 96) : RGB(180, 146, 74));
        FrameRect(dc, &g_btn_rect, bd);
        DeleteObject(bd);

        int hidden = panel_n - max_inline;
        char lbl[24];
        if (hidden > 0) snprintf(lbl, sizeof lbl, "+%d", hidden);
        else snprintf(lbl, sizeof lbl, "All");
        draw_shadow_text(dc, lbl, &g_btn_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                         g_panel_open ? RGB(255, 232, 140) : RGB(255, 255, 255));
    }

    // Hover tooltip below the top bar for inline slots (when the panel is closed)
    if (!g_panel_open && g_hover_slot >= 0 && g_hover_slot < g_nslots) {
        const WlResourceDef *r = &defs[g_slots[g_hover_slot].res_idx];
        float val = 0.0f;
        local_res_value(r->id, &val);
        char line1[192];
        if (r->max > 0.0f)
            snprintf(line1, sizeof line1, "%s: %d / %d", r->name, (int)val, (int)r->max);
        else
            snprintf(line1, sizeof line1, "%s: %d", r->name, (int)val);

        SelectObject(dc, fallback_font(13, 0));
        SIZE s1 = { 0, 0 }, s2 = { 0, 0 };
        GetTextExtentPoint32A(dc, line1, (int)strlen(line1), &s1);
        int has_desc = r->description[0] != '\0';
        if (has_desc) GetTextExtentPoint32A(dc, r->description, (int)strlen(r->description), &s2);
        int tw = (s1.cx > s2.cx ? s1.cx : s2.cx) + 16;
        if (tw < 100) tw = 100;
        if (tw > 320) tw = 320;
        int th = has_desc ? 42 : 24;
        int tx = g_slots[g_hover_slot].r.left;
        if (tx + tw > scr->right - 8) tx = scr->right - tw - 8;
        if (tx < 4) tx = 4;
        RECT tip = { tx, 34, tx + tw, 34 + th };
        HBRUSH tbg = CreateSolidBrush(RGB(26, 20, 14));
        HBRUSH tbd = CreateSolidBrush(RGB(186, 150, 76));
        FillRect(dc, &tip, tbg);
        FrameRect(dc, &tip, tbd);
        DeleteObject(tbg);
        DeleteObject(tbd);

        RECT r1 = { tip.left + 8, tip.top + 4, tip.right - 8, tip.top + 20 };
        draw_shadow_text(dc, line1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(255, 240, 180));
        if (has_desc) {
            RECT r2 = { tip.left + 8, tip.top + 21, tip.right - 8, tip.bottom - 3 };
            draw_shadow_text(dc, r->description, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                             RGB(210, 204, 190));
        }
    }
    if (g_prev_hover_slot != g_hover_slot && !g_panel_open) {
        trigger_screen_redraw();
    }
    g_prev_hover_slot = g_hover_slot;

    // "Show all" panel listing every visible custom resource (icon, name, integer amount)
    g_total_rows = panel_n;
    g_vis_rows = panel_n < PANEL_MAX_ROWS ? panel_n : PANEL_MAX_ROWS;
    if (g_panel_open) {
        int max_scroll = panel_n - g_vis_rows;
        if (max_scroll < 0) max_scroll = 0;
        if (g_scroll < 0) g_scroll = 0;
        if (g_scroll > max_scroll) g_scroll = max_scroll;

        int ph = PANEL_HDR_H + g_vis_rows * PANEL_ROW_H + 10;
        int px = g_has_btn ? (g_btn_rect.left - PANEL_W / 2 + BTN_W / 2) : x0;
        if (px + PANEL_W > scr->right - 8) px = scr->right - PANEL_W - 8;
        if (px < 8) px = 8;
        int py = 34;

        g_panel_rect.left   = px;
        g_panel_rect.top    = py;
        g_panel_rect.right  = px + PANEL_W;
        g_panel_rect.bottom = py + ph;

        HBRUSH pbg = CreateSolidBrush(RGB(28, 22, 16));
        HBRUSH pbd = CreateSolidBrush(RGB(196, 160, 80));
        FillRect(dc, &g_panel_rect, pbg);
        FrameRect(dc, &g_panel_rect, pbd);
        DeleteObject(pbg);

        // Header bar
        RECT hdr = { px + 1, py + 1, px + PANEL_W - 1, py + PANEL_HDR_H };
        HBRUSH hbg = CreateSolidBrush(RGB(48, 36, 22));
        FillRect(dc, &hdr, hbg);
        DeleteObject(hbg);
        RECT sep = { px + 1, py + PANEL_HDR_H, px + PANEL_W - 1, py + PANEL_HDR_H + 1 };
        FillRect(dc, &sep, pbd);

        SelectObject(dc, fallback_font(14, 1));
        char title[64];
        snprintf(title, sizeof title, "Resources (%d)", panel_n);
        RECT tr = { px + 10, py + 2, px + PANEL_W - 30, py + PANEL_HDR_H };
        draw_shadow_text(dc, title, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(240, 210, 128));

        // Close button [X]
        g_close_rect.left   = px + PANEL_W - 24;
        g_close_rect.top    = py + 4;
        g_close_rect.right  = px + PANEL_W - 4;
        g_close_rect.bottom = py + PANEL_HDR_H - 4;
        HBRUSH cbg = CreateSolidBrush(g_hover_close ? RGB(130, 48, 36) : RGB(70, 44, 28));
        FillRect(dc, &g_close_rect, cbg);
        FrameRect(dc, &g_close_rect, pbd);
        DeleteObject(cbg);
        draw_shadow_text(dc, "X", &g_close_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(255, 255, 255));

        // Rows
        SelectObject(dc, vanilla_font);
        int need_sb = panel_n > g_vis_rows;
        int content_r = px + PANEL_W - (need_sb ? 14 : 8);
        for (int row = 0; row < g_vis_rows; row++) {
            int idx = g_scroll + row;
            if (idx >= panel_n) break;
            const WlResourceDef *r = &defs[panel_idx[idx]];
            int ry = py + PANEL_HDR_H + 5 + row * PANEL_ROW_H;
            if (row & 1) {
                RECT alt = { px + 4, ry, content_r, ry + PANEL_ROW_H };
                HBRUSH ab = CreateSolidBrush(RGB(34, 27, 20));
                FillRect(dc, &alt, ab);
                DeleteObject(ab);
            }
            draw_icon(dc, r->icon, px + 8, ry + 3, 18);

            RECT nr = { px + 32, ry, content_r - 78, ry + PANEL_ROW_H };
            draw_shadow_text(dc, r->name, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                             RGB(238, 228, 204));

            float val = 0.0f;
            local_res_value(r->id, &val);
            char amt[32];
            if (r->max > 0.0f) snprintf(amt, sizeof amt, "%d/%d", (int)val, (int)r->max);
            else snprintf(amt, sizeof amt, "%d", (int)val);
            RECT ar = { content_r - 76, ry, content_r - 4, ry + PANEL_ROW_H };
            draw_shadow_text(dc, amt, &ar, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(255, 255, 255));
        }

        // Scrollbar when rows overflow
        if (need_sb) {
            int track_top = py + PANEL_HDR_H + 5;
            int track_h = g_vis_rows * PANEL_ROW_H;
            RECT track = { px + PANEL_W - 10, track_top, px + PANEL_W - 4, track_top + track_h };
            HBRUSH tbr = CreateSolidBrush(RGB(20, 16, 12));
            FillRect(dc, &track, tbr);
            DeleteObject(tbr);
            int thumb_h = (track_h * g_vis_rows) / panel_n;
            if (thumb_h < 16) thumb_h = 16;
            int thumb_y = track_top + (max_scroll > 0 ? ((track_h - thumb_h) * g_scroll) / max_scroll : 0);
            RECT thumb = { track.left + 1, thumb_y, track.right - 1, thumb_y + thumb_h };
            FillRect(dc, &thumb, pbd);
        }
        DeleteObject(pbd);
    }

    SetTextColor(dc, old_col);
    SetBkMode(dc, old_bk);
    SelectObject(dc, old_font);

    if (gtotal > 0) paint_unit_panel(screen, dc, scr);
}

static void map_to_screen(HWND h, int mx, int my, int scr_w, int scr_h, int *ox, int *oy) {
    RECT c;
    if (GetClientRect(h, &c) && c.right > 0 && c.bottom > 0) {
        mx = MulDiv(mx, scr_w, c.right);
        my = MulDiv(my, scr_h, c.bottom);
    }
    *ox = mx;
    *oy = my;
}

static int pt_in(const RECT *r, int x, int y) {
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

int wl_resbar_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp, int active, int scr_w, int scr_h) {
    if (!active) {
        if (g_panel_open) {
            g_panel_open = 0;
            trigger_screen_redraw();
        }
        g_hover_slot = -1;
        g_hover_btn = 0;
        g_hover_close = 0;
        return 0;
    }

    if (g_panel_open && msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        g_panel_open = 0;
        trigger_screen_redraw();
        return 1;
    }

    if (msg == WM_MOUSEWHEEL && g_panel_open) {
        POINT pt = { (short)(WORD)lp, (short)(WORD)(lp >> 16) };
        ScreenToClient(h, &pt);
        int x, y;
        map_to_screen(h, pt.x, pt.y, scr_w, scr_h, &x, &y);
        if (pt_in(&g_panel_rect, x, y)) {
            int delta = (short)(WORD)(wp >> 16);
            g_scroll += (delta > 0) ? -2 : 2;
            int max_s = g_total_rows - g_vis_rows;
            if (max_s < 0) max_s = 0;
            if (g_scroll < 0) g_scroll = 0;
            if (g_scroll > max_s) g_scroll = max_s;
            return 1;
        }
    }

    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
        msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
        int x, y;
        map_to_screen(h, (short)LOWORD(lp), (short)HIWORD(lp), scr_w, scr_h, &x, &y);

        if (msg == WM_MOUSEMOVE) {
            g_hover_btn = g_has_btn && pt_in(&g_btn_rect, x, y);
            g_hover_close = g_panel_open && pt_in(&g_close_rect, x, y);
            int hs = -1;
            if (!g_panel_open) {
                for (int i = 0; i < g_nslots; i++) {
                    if (pt_in(&g_slots[i].r, x, y)) { hs = i; break; }
                }
            }
            g_hover_slot = hs;
            return 0;
        }

        if (g_has_btn && pt_in(&g_btn_rect, x, y)) {
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
                g_panel_open = !g_panel_open;
                if (!g_panel_open) trigger_screen_redraw();
            }
            return 1;
        }

        if (g_panel_open && pt_in(&g_panel_rect, x, y)) {
            if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && pt_in(&g_close_rect, x, y)) {
                g_panel_open = 0;
                trigger_screen_redraw();
            }
            return 1;
        }
    }
    return 0;
}
