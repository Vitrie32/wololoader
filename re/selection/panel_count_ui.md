# Selection panel: drawing our own "+N" / "Selected: N" text

Game: AoK HD.exe 5.8, base 0x400000. All disassembly quoted from `~/.cache/wl-test/exe.s` (objdump AT&T, no raw bytes), raw bytes checked with python on the exe.
Marks: **[V]** = read in the disassembly, **[U]** = unverified / inferred.

## 0. Short answer

* The panel is a `tpnl_obj` (vtable `0x9d9488`, RTTI `0x9d9484 -> 0xa23750`, ctor `0x713910`). Its **draw slot (vtable index 15) is `0x713f00`**,
  which for `player+0x254 > 1` calls `0x716360` (draw_group) at the single call site `0x713fef`.
* Best hook: **redirect that one CALL with `wl_patch_call(0x713fef, our_fastcall, 5)`**. Our function calls `0x716360(panel)` first,
  then draws the text with exactly the same call sequence the countdown panel (`0x71b410`) uses. It only runs when more than 1 unit is selected.
* Draw context = a 0x34-byte "text context" object built on the stack with `0x49e3c0(ctx, [panel+0xc])`, font = `panel+0x130`, rect = `{panel+0xb8, +0xbc, +0xc0, +0xc4}` = `{x, y, w, h}`.
  Text call is `0x4a16f0` (`__thiscall` ecx = style block, 5 stack args, `ret 0x14`), a wrapper that ends in GDI `DrawTextW`.
* No dirty flag needed: the panel is redrawn from scratch every time `render` runs (see 5).

## 1. The panel and draw_group `0x716360`

### 1.1 Entry and calling convention [V]

```
716360: push %ebp
716361: mov  %esp,%ebp
716363: push $0xffffffff
716365: push $0x966c70          ; SEH
...
71638e: mov  %ecx,%edi          ; edi = panel (this)
...
7167b0: ret                     ; plain ret, NO stack args
```
Raw bytes at 0x716360: `55 8b ec 6a ff 68 70 6c 96 00`. `__thiscall(panel)`, `ret` (0 stack bytes).

Only caller (grep `call 0x716360`: one hit) **[V]**:
```
713fe4: cmpl $0x1,0x254(%edx)   ; edx = panel+0xbf0 (player)
713feb: jle  0x713ff9           ; single unit -> other path
713fed: mov  %edi,%ecx          ; 8b cf
713fef: call 0x716360           ; e8 6c 23 00 00   (713ff4 + 0x236c = 716360, checked)
713ff4: jmp  0x715a5f           ; -> epilogue: mov %edi,%ecx / call 0x713ed0 / ... ret
```
Nothing else references `0x716360` and `0x713f00` appears only once as data (vtable slot at `0x9d94c4`).

### 1.2 Panel fields (this = panel) [V unless marked]

| offset | meaning | evidence |
|---|---|---|
| `+0x0c` | the panel's draw area / surface object ("DrawArea") | `716390..716438`: `mov 0xc(%edi),%ecx / call 0x450820`; used as the argument of `0x49e3c0` in every panel text draw |
| `+0x10` | owner screen/parent (fed to `0x6c14b0`/`0x6c1470`, which return two colour/palette values for the unit icons) | `716435: mov 0x10(%edi),%ecx / call 0x6c14b0` [U for the exact type] |
| `+0x1c` | visible flag | render `0x4845d0`: `484607: cmpl $0x0,0x1c(%esi) / je` skip |
| `+0xb8` x, `+0xbc` y, `+0xc0` width, `+0xc4` height | panel rect, absolute screen coordinates | `716751..716769`: `esi=[0xb8] edx=[0xbc] ecx=[0xc0] eax=[0xc4]+edx` then `SetRect(&r, x, y, x+w, y+h)` (`call *0x9ac400`, args pushed bottom,right,top,left,out) |
| `+0x130` | **font object** (refcounted, HFONT at `font+8`) | ctor `7139df: mov %eax,0x130(%edi)` with `eax = 0x10(%ebp)`; caller `6b453d..6b4630`: that argument is `[0xada2a8]->get(7)` via `0x472020` (`mov (%ecx,%eax,4),%eax`, `ret 4`) = entry 7 of the global font table `[0xada2a8]`. `0x49ea90` uses `mov 0x8(%eax)` as the HFONT for `SelectObject` (`49ead1: call *0x9ac07c`). |
| `+0x134 .. +0x143` | **zero for tpnl_obj** (ctor `7139f0..713a36` sets `0x134,0x138,0x13c,0x144,0x148,0x14c` to 0 or to other objects) | so unlike the countdown panel there is no colour block to copy; we build our own |
| `+0x150 + slot*0x2c` | per slot hit-test records written by icon draw `0x7197c0` (`imul $0x2c`, `+0x150/+0x154/+0x158/+0x15c`) | not needed |
| `+0xbf0` | player (WorldPlayer*) | `7163d2: mov 0xbf0(%edi),%eax / mov 0x254(%eax),%esi` |

Layout of the icon grid (all in draw_group) **[V]**: columns `ebx = (w-8)/40` (`mulhi 0x66666667, sar 4`), up to 3 rows (`lea (%ebx,%ebx,2)`),
each icon 0x28 x 0x28 (`push $0x28 / push $0x28` before `call 0x7197c0`), row pitch 0x2a (`imul $0x2a,-0x80(%ebp)` then `+4`),
horizontal stride is scaled (`mulss -0x94(%ebp)`) so the grid fills the width. With 60 units the grid is full (cap 60, `71645f: cmp $0x3c,%ecx` / `lea -0x3c(%ecx),%eax` = number of units skipped).
So the free area is **not** a clean empty strip: the last row ends at about `y + 4 + 3*42 = y+130`. Whether the panel is taller than 130 is **[U]** (layout code not traced; log `+0xb8..+0xc4` once at runtime and decide).

### 1.3 What draw_group does with the surface [V]

```
716385..7163ca: ecx=[edi+0xc]; push $0x9d96d0; call 0x450820 ; DrawArea::begin(rect*) ; returns 0 -> skip everything (je 0x716795)
...  icons via 0x7197c0 ...
716751..716776: SetRect(&-0x40(%ebp), x, y, x+w, y+h)         ; call *0x9ac400
71677c: mov 0xc(%edi),%ecx / lea -0x40(%ebp),%eax / push %eax
716783: call 0x451040      ; DrawArea::add_dirty_rect(&rect), ret 4 (calls 0x4a3390 then 0x450ed0)
716788: mov 0xc(%edi),%ecx / push $0x9d96d0
716790: call 0x451530      ; DrawArea::end(rect*), ret 4
```
So draw_group registers the panel's own rect as dirty and ends its begin/end bracket before it returns. After `0x716360` returns we are outside that bracket.
This is fine: the countdown panel text (`0x71b410`) is drawn with **no** begin/end of its own, only inside the parent's render (below), so text through `0x49e3c0 + 0x4a16f0` does not need `0x450820`. Our text lies inside the panel rect that was already marked dirty.

### 1.4 Where draw slot 15 runs (parent render) [V]

`TPanel::render` = vtable slot 20 = `0x4845d0` (stack frame, SEH):
```
484607: cmpl $0x0,0x1c(%esi) ; je skip           ; visible?
4846a5..4846bc: rect {+0xb8,+0xbc,+0xc0,+0xc4} ; call 0x44d010   ; push CLIP for the panel rect (popped by 0x44d050 at 484779)
4846c5: mov %esi,%ecx / mov (%esi),%eax
4846c9: call *0x3c(%eax)                          ; <-- draw slot 15 (0x713f00 for tpnl_obj)
4846cc: call 0x4833b0 ... (debug overlay test: jmp 0x485f10)
...                                               ; then children via slot 0x50
```
So everything we draw after draw_group (still inside slot 15) is clipped to the panel rect and drawn before the children. Text outside the panel rect is clipped away (see 6).

## 2. The text drawing function `0x4a16f0` [V]

```
4a16f0: push %ebp / mov %esp,%ebp / push $-1 / push $0x9160a8 ...   ; raw: 55 8b ec 6a ff 68
4a171b: mov %ecx,%esi            ; esi = style block (this)
4a171d: mov 0x8(%ebp),%ebx       ; arg1 = text context (ctx)
4a1722: mov 0xc(%ebp),%eax       ; arg2 = text (char*)
4a1725: mov 0x14(%ebp),%edi      ; arg4 = rect*
4a174f..4a1753: push 0x10(%ebp) ; push eax ; call 0x4459a0   ; 4459a0(char* text, int len(arg3), std::wstring* out)  -> narrow-to-wide
4a175b: cmpl $0x3,0x10(%esi) / jne ...                        ; style mode 3 + a global player check may clear style+0x14
4a1783..4a179f: builds {ctx, &wstr, rect[4 dwords copied], flags = arg5}
4a17aa: call 0x4a0b20            ; draws the text 9 times (outline: 49eb60 + 4a1560 x8 offsets) when mode 3, plain 4a15b0 otherwise
4a17f0: ret $0x14
```
Final GDI call is `DrawTextW` (`4a07ea: call *0x9ac32c`, import checked: `005ac32c DrawTextW`), rect built by `SetRect` (`4a07d1: call *0x9ac400`) from `{x, y, w, h}`: right = x + w, bottom = y + h (`4a07c1 add %edx,%eax` bottom, `4a07c7 add (%ecx),%eax` right).

Signature:
```c
/* __thiscall: ecx = style block, ret 0x14 */
void TextDraw(Style *style, TextCtx *ctx, const char *text, int len, const int rect[4]/*x,y,w,h*/, DWORD dt_flags);
```
| arg | meaning | proof |
|---|---|---|
| ecx | style block (see 2.1) | `71b4f8: lea -0x28(%ebp),%ecx / call 0x4a16f0` |
| 1 | text ctx, `0x34` bytes built by `0x49e3c0` | `71b4f4: lea -0x5c(%ebp),%eax / push %eax` |
| 2 | `char*` (narrow, NUL-terminated is fine) | counter passes the buffer from `0x71b580` |
| 3 | length in chars (counter computes `strlen` by the `cl` loop `71b4c0..71b4cd`) | `71b4cd: sub %edx,%eax` |
| 4 | `int rect[4] = {x, y, w, h}` absolute screen coords | `71b4c7..71b4eb`: `[0xb8],[0xbc],[0xc0],[0xc4]` of the panel |
| 5 | `DrawText` format flags | counter: `lea 0x20(,%ebx,2),%ebx` -> `0x20` or `0x22`; panel at `71a7be: push $0x10`. These are Win32 `DT_SINGLELINE` (0x20) `\|` `DT_RIGHT` (2); 0x10 = `DT_WORDBREAK`. Passed straight to `DrawTextW` through `4a1595: push 0x18(%ecx)` -> `4a07d7: push 0x18(%ebp)` -> `4a07ea` [V] (interpretation as DT_* is inferred from the API **[U-ish]**, but it is DrawTextW's last parameter, so the meaning is the documented one) |

### 2.1 Style block (the thiscall `this`), 0x20 bytes, built by the callers [V]

Counter (`71b488..71b4b5`):
```
-0x28: colour  = [panel+0x134]  (= 0xffffff, set in ctor 71b2ef: movl $0xffffff,0x134(%esi))
-0x24: dword   = [panel+0x138]  (0)
-0x20: dword   = [panel+0x13c]  (0)
-0x1c: dword   = [panel+0x140]  (0)
-0x18: dword   = 3                     ; mode 3 = outline (4a175b tests cmpl $3,0x10(%esi))
-0x14: byte    = 1
```
In the other panel (`71a76d..71a79e`) the 2nd pair is `[0xada500],[0xada504]` (globals) with the same `3` and `1`. We use zeros as the counter does:
```c
DWORD style[8] = {0x00FFFFFF, 0, 0, 0, 3, 1 /*byte at +0x14*/, 0, 0};   /* [+0]=colour (white), [+0x10]=3, byte [+0x14]=1 */
```
The colour byte order (RGB vs BGR) does not matter for white; for other colours it is **[U]** (probably COLORREF 0x00BBGGRR). Outline colour at `+8` = 0 (black) is **[U]**; the outline pass means the text stays readable on top of icons.
Note `4a17b3..4a17c1`: while `+0x14 == 0` the game does a one-time `0x49e8b0`; with `+0x14 = 1` it skips it, so keep the byte 1 like the callers do.

### 2.2 The text context `0x49e3c0` / font `0x49ea90` / destroy `0x49e4b0` [V]

Exact sequence used by both `0x71b410` and `0x71a6d0` (quoted):
```
71b45f: call 0x8d62f0              ; memset(&ctx, 0, 0x34)   (push 0x34 / push 0 / push &ctx)
71b467: lea  -0x5c(%ebp),%ecx
71b46a: push 0xc(%esi)             ; DrawArea = [panel+0xc]
71b46d: call 0x49e3c0              ; ctx::init(DrawArea)      __thiscall, ret 4
71b472: movl $0x0,-0x4(%ebp)       ; (SEH state only)
71b479: lea  0x130(%esi),%eax      ; &panel->font   (pointer TO the font pointer)
71b47f: lea  -0x5c(%ebp),%ecx
71b483: call 0x49ea90              ; ctx::set_font(font**)    __thiscall, ret 4: (49ea9d: mov (%edi),%eax = *font; refcounts; SelectObject)
 ... TextDraw ...
71b51f: lea  -0x5c(%ebp),%ecx
71b522: call 0x49e4b0              ; ctx::destroy             __thiscall, plain ret (0x49e52d)
```
`0x49e3c0`: `49e3ea mov 0x8(%ebp),%edi / 49e3f0 mov %edi,(%esi)` stores the DrawArea in ctx+0; `ret $0x4` at `0x49e4a6`.
`0x49ea90`: `ret $0x4` (`49eae2`). If the DrawArea has no DC (`ctx+0xc == 0`) the font is only stored, nothing is drawn: harmless.

## 3. Recipe

```c
/* addresses: all [V] above */
typedef void (__thiscall *DrawGroup_t)(void *panel);                      /* 0x716360, plain ret */
typedef void *(__thiscall *CtxInit_t)(void *ctx, void *drawarea);         /* 0x49e3c0, ret 4 */
typedef void (__thiscall *CtxFont_t)(void *ctx, void **font);             /* 0x49ea90, ret 4 */
typedef void (__thiscall *CtxDone_t)(void *ctx);                          /* 0x49e4b0, plain ret */
typedef void (__thiscall *TextDraw_t)(void *style, void *ctx, const char *s, int len,
                                      const int *rect, DWORD flags);      /* 0x4a16f0, ret 0x14 */

extern int g_extra_selected;                       /* ours: units beyond the game's 60 */

static void __fastcall wl_draw_group_hook(void *panel, void *edx)
{
    ((DrawGroup_t)0x716360)(panel);                /* original, draws the 60 icons */
    if (g_extra_selected <= 0) return;

    char buf[32]; int n = wsprintfA(buf, "+%d", g_extra_selected);   /* or "Selected: %d" */
    BYTE *p = (BYTE *)panel;
    int px = *(int *)(p + 0xb8), py = *(int *)(p + 0xbc);
    int pw = *(int *)(p + 0xc0), ph = *(int *)(p + 0xc4);
    int rect[4] = { px, py + ph - 20, pw - 6, 18 };  /* {x, y, w, h}: bottom strip, flush right (see 4 for [U]) */

    DWORD style[8] = { 0x00FFFFFF, 0, 0, 0, 3, 1, 0, 0 };
    BYTE  ctx[0x34]; memset(ctx, 0, sizeof ctx);
    ((CtxInit_t)0x49e3c0)(ctx, *(void **)(p + 0xc));
    ((CtxFont_t)0x49ea90)(ctx, (void **)(p + 0x130));
    ((TextDraw_t)0x4a16f0)(style, ctx, buf, n, rect, 0x22 /* DT_SINGLELINE | DT_RIGHT */);
    ((CtxDone_t)0x49e4b0)(ctx);
}
/* install, once, after the exe check (not from DllMain): */
wl_patch_call(0x713fef, wl_draw_group_hook, 5);    /* original: e8 6c 23 00 00 = call 0x716360 (verified bytes) */
```
Why the wrapper is `__fastcall(void *self, void *edx)`: the original is `__thiscall` with no stack args and `ret`, so a fastcall with two register args leaves the stack unchanged, same as AGENTS rule 4. The `mov %edi,%ecx` right before the call at `713fed` supplies `ecx = panel`.
The call site patch needs no trampoline (we call `0x716360` directly; we are not detouring its prologue), and the 5 stolen bytes are exactly one whole `call rel32` instruction.

Alternative (not needed): overwrite the vtable dword `0x9d94c4` (slot 15, currently `0x713f00`) with a wrapper that calls `0x713f00` first (needs `VirtualProtect` on `.rdata`; the wrapper would also run when only 1 unit is selected, so check `player+0x254 > 1` itself).

## 4. What could go wrong / still unverified

1. **Position [U]**. The grid fills the panel width and has 3 rows of 42 px starting at y+4; if the panel height is only about 130 the bottom strip overlaps the last icon row. The text has an outline (mode 3) so it stays readable over icons; if it looks bad, put it at the top (`py`, 4 px above the grid) or right-aligned in the 3rd row. Before shipping, log `+0xb8..+0xc4` once (cheap) and pick coordinates from the real size. Pure measurement, no further RE needed.
2. **Clipping**. Render pushes a clip equal to the panel rect (`0x44d010` at `4846bc`), so anything outside `{x,y,w,h}` disappears. Keep the text inside it. The text rect's `DT_SINGLELINE` height must be at least the font height (the countdown uses h = panel h; the other panel uses h = 0x28 = 40). 18 might clip a tall font; use `h = 24..40` if needed. Font = font table entry 7 (size not checked) **[U]**.
3. **Colour / outline colour byte order [U]** (white works either way). Flags are the documented `DrawTextW` ones; I only saw 0x10, 0x20, 0x22 used by the game, so stay with 0x22 or 0x20.
4. **Draw order**: we draw after `draw_group` and before the children (slot 15 runs before the child loop of render, `4846c9` then `484760`). A child panel overlapping this rect would paint over our text [U, none seen].
5. **Thread / timing**: render and all of this run on the game's UI thread; our `g_extra_selected` should be written from the same thread (our per-frame `0x6c3310` hook). Do not call from `DllMain` (AGENTS rule 5): install the patch in the lazy init like the other `wl_install_*`.
6. **Exceptions**: the game functions are SEH-framed; our wrapper has no handler. If the DrawArea is invalid the original `0x716360` would already have bailed (`call 0x450820` returned 0 -> `je 0x716795`), but our overlay would still run: guard with `*(void **)(p + 0xc) != NULL` and `*(int *)(p + 0x1c) != 0` (visible).
7. `0x4a16f0` calls `0x4459a0` which converts `char*` with the ANSI code page; ASCII only is safe.
8. Strings: `wsprintfA` is in user32 (already imported by the game DLL set); `snprintf` from the mingw CRT is fine too, but avoid it before the CRT is up (rule 5 only matters in DllMain).

## 5. Redraw / dirty flag

No flag has to be set, **[V] statically**:
* `0x716360` has no "unchanged" early-out: after `begin` succeeds it unconditionally loops over all slots, redraws every icon, and at the end calls `0x451040(&rect)` itself (registers the panel rect as dirty). The dispatcher `0x713f00` also clears all 0x3d slot records on every call (`713fb0: movb $0x0,(%eax) ... lea 0x2c(%eax)` loop), i.e. it expects to rebuild everything per draw.
* `0x716360` is the hottest function in the selection trace (49 848 hits vs 3 748 for `0x713f00` in `re/selection/trace_vs_static.md`), consistent with a per-frame draw, not a draw-on-change.
* The per-tick update `0x7187b0` only gates the rate of its own logic (`call 0x4706d0 / sub 0xbfc(%ebx) / cmp 0xc00(%ebx) / jb` = a ms timer) and the primary-unit switching; it does not guard the draw.
So when `g_extra_selected` changes, the next frame's render shows the new text. If it ever shows stale text anyway, the fallback is to call `0x451040(DrawArea=[panel+0xc], &rect)` (`__thiscall`, `ret 4`) with the panel rect after our text, mirroring `716783`; but that is untested and probably unnecessary.
**[U]**: whether the whole game screen is composited every frame (almost surely, because the HD renderer redraws panels each frame), not proven by static analysis alone.

## 6. Summary and confidence

* Hook: `wl_patch_call(0x713fef, wrapper, 5)` (only call of `0x716360`; bytes `e8 6c 23 00 00`; `__thiscall`, plain `ret`, `ecx = panel`).
* Text: `0x49e3c0(ctx,[panel+0xc])`, `0x49ea90(ctx,&panel->+0x130)`, `0x4a16f0(style; ctx, char*, len, {x,y,w,h}, DrawText flags)`, `0x49e4b0(ctx)`; copied from the working countdown draw `0x71b410` (the same sequence is in `0x71a6d0`).
* Panel fields: rect `+0xb8/+0xbc/+0xc0/+0xc4` = x,y,w,h absolute; draw area `+0xc`; font `+0x130` (font table entry 7); player `+0xbf0`; visible `+0x1c`.

Confidence: **high** (about 90 percent) for the hook point, the call sequence, argument order, field offsets, calling conventions and that no dirty flag is needed; **medium** for exact on-screen placement (panel height unknown, log it first), the colour byte order and the font size (table entry 7). Nothing here has been run in game.
