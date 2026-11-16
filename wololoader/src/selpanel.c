// Selection count label: "+M" (gold) inside the next empty icon slot of the multi-selection panel when
// the overflow list (overflow.c) holds units beyond the game's 60. Research: re/selection/panel_count_ui.md.
//
// Hook: the only call of draw_group 0x716360 (tpnl_obj draw slot 0x713f00):
//   713fed: mov %edi,%ecx ; 713fef: call 0x716360 (e8 6c 23 00 00) ; 713ff4: jmp 0x715a5f
// 0x716360 is __thiscall(panel), plain `ret`, so a __fastcall(self, edx) wrapper keeps the stack
// balanced. We call it first and draw on top afterwards (the panel render pushed a clip of the
// panel rect, 0x4846a5..0x4846bc, so anything outside the rect would be clipped anyway).
//
// Context from the countdown panel 0x71b410 (verified in the disassembly):
//   71b45f: call 0x8d62f0            ; memset(ctx, 0, 0x34), cdecl
//   71b467: lea -0x5c(%ebp),%ecx ; push 0xc(%esi) ; call 0x49e3c0     ; ctx init(DrawArea=[panel+0xc]), ret 4
//   71b51f: lea -0x5c(%ebp),%ecx ; call 0x49e4b0                        ; ctx destroy, plain ret (0x49e52d)
// Instead of the game's text call (0x4a16f0) we fetch the ctx's HDC with 0x49e7b0 and draw box + text with GDI.
// Helpers 0x49e3c0/0x49e4b0 start with `push %ebp; mov %esp,%ebp`, 0x49e7b0 with `push %esi; mov %ecx,%esi`.
#include "wololoader.h"
#include <string.h>
#include <stdio.h>

typedef void (__thiscall *DrawGroup_t)(void *panel);
typedef void (__thiscall *CtxInit_t)(void *ctx, void *drawarea);
typedef void (__thiscall *CtxDone_t)(void *ctx);
typedef HDC  (__thiscall *CtxDC_t)(void *ctx);

#define CALL_SITE 0x713fef
#define RD32(p, off) (*(const DWORD *)((const BYTE *)(p) + (off)))

static const DrawGroup_t draw_group = (DrawGroup_t)0x716360;
static const CtxInit_t   ctx_init   = (CtxInit_t)0x49e3c0;
static const CtxDone_t   ctx_done   = (CtxDone_t)0x49e4b0;
static const CtxDC_t     ctx_dc     = (CtxDC_t)0x49e7b0;

static void __fastcall draw_group_hook(void *panel, void *edx) {
    (void)edx;
    draw_group(panel);                                   // original: draws the icons
    const BYTE *p = (const BYTE *)panel;
    void *player = (void *)RD32(p, 0xbf0);
    void *area   = (void *)RD32(p, 0xc);
    if (!area || !RD32(p, 0x1c) || !wl_overflow_owns(player)) return;
    int extra = wl_overflow_count();
    if (extra <= 0) return;

    int px = (int)RD32(p, 0xb8), py = (int)RD32(p, 0xbc);
    int pw = (int)RD32(p, 0xc0), ph = (int)RD32(p, 0xc4);
    if (pw < 20 || ph < 10) return;

    // ---- icon grid geometry, replicated from draw_group 0x716360 (exe.s) ----
    // count = player+0x254 (7163d8 mov 0x254(%eax),%esi). cols0 = (w-8)/40: 7163b9 imul 0x66666667 ; sar 4 ;
    // +sign bit (7163be..7163c3) after `lea -0x8(%edx)` at 7163ae.
    // Up to 3 rows: `lea (%ebx,%ebx,2)` vs count (7163de..7163e3). If count > 3*cols0 the game re-flows:
    //   c = (int)(count / 3.0f + 0.5f)   (divss [0x9b0234]=3.0f, addss [0x9afd68]=0.5f, cvttss2si, 7163e5..7163fc)
    //   cols = (3*c >= count) ? c : c+1  (716400..71640b cmovge)
    //   spacing = (cols0-1) * 41.0f / (cols-1)  (mulss [0x9d9708]=41.0f, divss, 716408..71642d)
    // else cols = cols0 and spacing = 41.0f (7163b1 movss [0x9d9708] -> -0x94(%ebp)).
    // Icon k (0-based) sits at row k/cols, col k%cols (716475..716499 compute row/col of the LAST icon,
    // then 716706..716738 step backwards). draw_group calls 0x7197c0(slot, relx, rely, 0x28, 0x28, ...) with
    //   relx = (int)(col*spacing + 8.0f)   (716622.. mulss -0x94, addss [0x9b483c]=8.0f at 7165d0, cvttss2si)
    //   rely = row*0x2a + 4                (7165e2 imul $0x2a ; 7165e6 add $4)
    // and 0x7197c0 adds the panel origin: x = [panel+0xb8]+relx (719848 add %ebx,%ecx), y = [panel+0xbc]+rely
    // (71983d add %edi,%edx); the icon is 0x28 x 0x28 (arg 0x14/0x18 -> rect end = x+0x28-1, 719846..71984f).
    int count = (int)RD32(player, 0x254);
    if (count < 1) return;
    int cols0 = (pw - 8) / 40, cols = cols0;
    float spacing = 41.0f;
    if (count > 3 * cols0) {
        int c = (int)((float)count / 3.0f + 0.5f);
        cols = (3 * c >= count) ? c : c + 1;
        spacing = (float)(cols0 - 1) * 41.0f / (float)(cols - 1 > 0 ? cols - 1 : 1);
    }
    if (cols < 1) return;
    int slot = count;                                    // the first slot after the last drawn icon
    if (slot / cols >= 3) slot = count - 1;              // grid full: draw over the last icon
    int col = slot % cols, row = slot / cols;
    int sx = px + (int)((float)col * spacing + 8.0f);
    int sy = py + row * 0x2a + 4;

    char buf[24];
    int n = snprintf(buf, sizeof buf, "+%d", extra);
    if (n <= 0 || n >= (int)sizeof buf) return;

    // Draw straight on the panel's GDI device context. The game's own text path ends in DrawTextW on this very DC
    // (0x4a07a0: SetRect(x, y, x+w, y+h) then 0x49e7b0(ctx) -> hdc -> *0x9ac32c DrawTextW). 0x49e7b0 is
    // `push esi; mov ecx,esi; call 0x49e6c0; mov 0xc(%esi),%eax; pop esi; ret` (plain ret): 0x49e6c0 acquires the
    // DC from the DrawArea (0x44eb40), selects ctx font/colour, installs the clip region (SelectClipRgn *0x9ac028 =
    // the clip stack, so we stay inside the panel) and SetBkMode(TRANSPARENT) (*0x9ac038, 1). 0x49e4b0 (ctx_done,
    // via 0x49e8f0) releases it again, exactly as after a normal text draw.
    // The unit icon frames are SLP frames 0x24/0x25 of the shape at [panel+0x144] drawn by 0x7197c0 -> 0x45a560;
    // there is no plain fill-rect function in that path, so the dark box is a GDI FillRect on the same DC.
    BYTE ctx[0x34];
    memset(ctx, 0, sizeof ctx);
    ctx_init(ctx, area);
    // Font: the game's table [0xada2a8] has nothing bigger and readable than Georgia 11 B (entry 0x21, 15 px: the
    // rest is Lucida Blackletter/Calligraphy or Georgia <= 11), so the number uses a GDI font of the same family,
    // the largest height (<= 26 px) whose text still fits into the 40 px box.
    HDC dc = ctx_dc(ctx);
    int fh = 0;
    if (dc) {
        static HFONT fonts[27];
        HFONT oldf = NULL;
        SIZE sz;
        for (int h = 26; h >= 12; h--) {
            if (!fonts[h])
                fonts[h] = CreateFontA(-h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_ROMAN, "Georgia");
            if (!fonts[h]) continue;
            HFONT o = (HFONT)SelectObject(dc, fonts[h]);
            if (!oldf) oldf = o;
            fh = h;
            if (GetTextExtentPoint32A(dc, buf, n, &sz) && sz.cx <= 0x28 - 6) break;
        }
        // box: 1 px light frame + black fill (like the unit icon frames), exactly the 40x40 slot
        RECT r = { sx, sy, sx + 0x28, sy + 0x28 };
        HBRUSH frame = CreateSolidBrush(RGB(200, 200, 190)), black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &r, frame);
        r.left++; r.top++; r.right--; r.bottom--;
        FillRect(dc, &r, black);
        DeleteObject(frame);
        DeleteObject(black);
        // text: black outline (8 offsets) then gold, centred in the slot (DT_CENTER | DT_VCENTER | DT_SINGLELINE)
        UINT fl = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP;
        int oldbk = SetBkMode(dc, TRANSPARENT);
        COLORREF oldc = SetTextColor(dc, RGB(0, 0, 0));
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                RECT o = { sx + dx, sy + dy, sx + 0x28 + dx, sy + 0x28 + dy };
                DrawTextA(dc, buf, n, &o, fl);
            }
        SetTextColor(dc, RGB(255, 215, 0));
        r.left = sx; r.top = sy; r.right = sx + 0x28; r.bottom = sy + 0x28;
        DrawTextA(dc, buf, n, &r, fl);
        SetTextColor(dc, oldc);
        SetBkMode(dc, oldbk);
        if (oldf) SelectObject(dc, oldf);
    }
    ctx_done(ctx);

    static int logged[8];
    int cur[8] = { px, py, pw, ph, count, cols, slot, fh };
    if (memcmp(logged, cur, sizeof cur) != 0) {
        memcpy(logged, cur, sizeof cur);
        wl_log("selpanel: rect %d %d %d %d count %d cols %d spacing %.2f slot %d -> box %d,%d,40,40 font %dpx dc %s",
               px, py, pw, ph, count, cols, (double)spacing, slot, sx, sy, fh, dc ? "ok" : "none");
    }
}

void wl_install_selpanel(void) {
    static const BYTE site[]  = { 0xe8, 0x6c, 0x23, 0x00, 0x00 };
    static const BYTE pro[]   = { 0x55, 0x8b, 0xec };
    static const BYTE dcpro[] = { 0x56, 0x8b, 0xf1 };        // 49e7b0: push %esi ; mov %ecx,%esi
    static const DWORD fns[]  = { 0x49e3c0, 0x49e4b0 };
    if (memcmp((void *)CALL_SITE, site, sizeof site) != 0) {
        wl_log("selpanel: call site %08x bytes differ, label disabled", CALL_SITE);
        return;
    }
    if (memcmp((void *)0x49e7b0, dcpro, sizeof dcpro) != 0) {
        wl_log("selpanel: ctx_dc 0x49e7b0 prologue differs, label disabled");
        return;
    }
    for (int i = 0; i < 2; i++)
        if (memcmp((void *)fns[i], pro, sizeof pro) != 0) {
            wl_log("selpanel: helper %08lx prologue differs, label disabled", fns[i]);
            return;
        }
    wl_patch_call(CALL_SITE, draw_group_hook, 5);
}
