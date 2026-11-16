# The real chat list: ChatHistory::Add at 0x8037b0

Status: read from the disassembly only (exe.s). NOT run in game. Confidence: high (about 90%) that
`0x8037b0` is the function that appends a real chat line; medium on the exact meaning of args a1, a4, a5, a6.

## 1. Answer

`0x8037b0` appends a line to the chat history object `*(void**)0xaeefb4`.
The GameScreen chat panel is only a view of that history (51-entry ring buffer). `0x6bf910` is NOT the chat list:
it only draws a separate line in the top areas, and it also calls `0x8037b0` itself (see 4), which is
why mode 2 text overlaps real chat lines.

```
/* 0x8037b0  __thiscall(ChatHistory *this, const char *sender, const char *text,
 *                       int player, int type, int a5, int a6), ret 0x18, returns 1 ok / 0 alloc failed
 * this = *(void**)0xaeefb4  (ChatHistory, NOT the GameScreen)               */
typedef int (__fastcall *ChatAdd_t)(void *hist, void *edx, const char *sender, const char *text,
                                    int player, int type, int a5, int a6);
void *hist = *(void **)0xaeefb4;                      /* check for NULL */
((ChatAdd_t)0x8037b0)(hist, 0, NULL, "text", -1, 0, 0, 0);   /* plain system line, like '--Sheep Found--' */
```

Prologue verified: `push ebp; mov ebp,esp; push -1; push 0x9902f1; ... mov ecx -> edi`, args read at
`[ebp+8]` (sender), `+0xc` (text), `+0x10` (player), `+0x14` (type), `+0x18` (a5), `+0x1c` (a6); epilogue
`ret 0x18` at `0x804033`.

## 2. Arguments (verified from the code, the semantics partly inferred)

| Arg | Slot | Meaning |
|---|---|---|
| sender | `[ebp+8]` | `char*` or NULL. Stored in local `-0x1460`. Non-NULL: the line becomes "`@#<player><prefix>Name: text`" (format `0x9e2a78` `"@%c%d%s%s: %s"`), i.e. a "Name: text" line. NULL: no name part. |
| text | `[ebp+0xc]` | `char*`. First char `;` `#` `*` `%` is parsed specially (string-table ids `0x2f4c`, `0x2f5a`, `0x2f5b` ... and the `0x9ae6b0` path), so avoid those as the first character (or pass a5 != 0, see below). Copied to a 0x800 buffer; length >= 0x800 in the prefix paths jumps to `0x80403b` (CRT error), so keep text below about 500 chars (the panel copies into a 0x200 buffer in `0x6c15e0`). |
| player | `[ebp+0x10]` | Player number. Compared with `[0xaad300]`, which is **-1** (verified: `.data` initial value 0xffffffff), meaning "system / nobody". `-1` = uncoloured plain line. A player number below 9 = line coloured with that player's colour (`"@#%d..."` colour tag, parsed later by `0x6bf720`). For this the player must exist (`0x803bf9` reads the player object `[0xaf2c58]+0xc8`), otherwise it falls back. |
| type | `[ebp+0x14]` | Stored in the slot at `+0x30+idx*8`. `0` = normal chat (plays the chat sound via `0x62bce0`, see 3). `1` = quiet system line (used by `0x4b2504`, e.g. string ids 0x912f..0x9130). `2` = HIDDEN from the chat panel: the getter `0x8040b0` returns 0 for type 2 (`cmpl $2,0x30(...)`). This is what `0x6bf910` uses. |
| a5 | `[ebp+0x18]` | If non-zero, jumps straight to `0x803a3f` (skips the `;#*%` prefix parsing). All real call sites pass 0 except `0x6bf910` path. Not tested. |
| a6 | `[ebp+0x1c]` | Used at `0x803b28`/`0x803c9f` (a filter/ignore check: only when 0, type 0 and `[this+0x1cc]` is set). All call sites pass 0. |

Return value in `eax`: 1 on success, 0 if the string allocation (`0x8ea479`) fails.

## 3. How a line gets on screen (so the call alone is enough)

1. `0x8037b0` advances `this+0x1c8` (index, ring of 0x33 = 51 entries at `this+0x2c`, 8 bytes each: char*, type), allocates
   the final text, stores it (`0x803b10`). Index > 0x32 wraps to 0.
2. At `0x803f73` it calls `0x57f600` (for-each over listeners) with a functor `{idx, type==1, ...}` (vtable `0x9e2aa4`).
3. The GameScreen listener thunk `0x62db60` -> `0x634270` reads `(%esi)` = idx and `0x4(%esi)` = (type==1):
   - if type != 1 it calls `0x62bce0(0,0,2)` (sound), then
   - `mov ecx,[0xaf3d90]; push idx; call 0x6c15e0` (GameScreen refresh, `ret 4`).
4. `0x6c15e0(this=GameScreen, idx)` reads the newest entries with `0x8040b0(index)` (skips type 2) and refills
   the 8 line managers at `screen+0xc3c..` (count `screen+0xc64`) via `0x664840` type 3, newest at the bottom.
   So lines stack and scroll like player chat. Scrolling is `0x6c6550(delta)` (called for up/down at `0x6c3da5`, `0x6c3db3`).

Inferred, not run: because every real call site (below) calls only `0x8037b0`, no extra redraw call should be needed.
Fallback if nothing shows: `((void(__thiscall*)(void*,int))0x6c15e0)(*(void**)0xaf3d90, *(int*)((char*)hist+0x1c8))`
(0x8040d0 returns `[this+0x1c8]`; do this only for debugging, it duplicates what the listener does).

## 4. Real call sites (proof of the arguments)

- `0x689a6d` (system text from the string table, plain line):
  `push eax(text from vcall 0x24 of [0xaf2c58], string id "TREATY_OVER_MSG" 0x9d3880); push 0 (sender);`
  with `mov ecx,[0xaeefb4]` at `0x689a64`, preceded by `push 0; push 0; push 0; push [0xaad300](-1)` =
  type 0, a5 0, a6 0, player -1.
- `0x867c9d` (text `0x34(%edi)` string id or raw `0x70(%edi)`): `push 0,0,0, push [0xaad300], text, push 0; mov ecx,[0xaeefb4]` = same pattern.
- `0x4b2524`: `push 0,0,1 (type=1), push [0xaad300] (-1), text(0x540d50(id)), push 0` = quiet system line.
- `0x58457c` (a received player chat line, name + colour): `mov ecx,[edi+0xa0]` (the history pointer, same object as
  `[0xaeefb4]`), `push 0,0,0, push player=[esi+8], push text=[esi+0x18], push sender name=(*eax)`. This is the network/peer chat path
  (function around `0x584500`, not read fully). Gives `Name: text` coloured by player.
- `0x85adef` (console `getfps`): `sender = "FpsControl"` (non-NULL string), text, player -1.
- AI `chat-local` handler `0x4e6640` calls `0x8037b0` at `0x4e66a7` (player/type come from the AI call; stack layout there is confusing, not decoded).
- `0x6bf910` calls it at `0x6bf944` with `type=2, a5=0, sender=0, player=-1`: hidden entry, so it never shows in the list.

## 5. Suggested use in luagame.c (not tested)

```c
typedef int (__fastcall *ChatAdd_t)(void*, void*, const char*, const char*, int, int, int, int);
void *hist = *(void**)0xaeefb4;                         /* may be NULL outside a game */
if (hist) ((ChatAdd_t)0x8037b0)(hist, NULL, NULL, text, -1, 0, 0, 0);   /* white/system line, with chat sound */
/* coloured like a player: sender = "Name", player = 1..8 (needs that player to exist):
 *   ((ChatAdd_t)0x8037b0)(hist, NULL, "Name", text, 2, 0, 0, 0);        -> "Name: text" in player 2's colour */
```
Use type 1 instead of 0 to skip the chat sound. Call it from the game thread (the same place `0x6bf910` is called now).
Multiplayer: this only changes the local history, nothing is sent (the call site list shows it is the display half only).

## 6. Not verified

- Exact order of the two `%s` in the formats `0x9e2a78/0x9e2a88/0x9e2a94` (prefix vs text); the expected result is a normal "Name: text".
- What a5 != 0 changes beyond skipping the `;#*%` parsing, and what a6 does.
- Whether `[0xaeefb4]` is always the same object as `[GameScreen-owner+0xa0]` (looks like it, only `0x58457c` uses `+0xa0`).

## Summary

1. Function: `0x8037b0`, `__thiscall`, `ret 0x18`, ChatHistory::Add(sender, text, player, type, a5, a6).
2. `this` = `*(void**)0xaeefb4` (not the GameScreen); `0x6c15e0` on GameScreen `[0xaf3d90]` redraws it via a listener automatically.
3. Plain system line: `(hist, NULL, NULL, text, -1, 0, 0, 0)`; player colour: `sender=name, player=1..8`; type 2 is hidden, type 1 is silent.
4. Proof sites: `0x689a6d`, `0x867c9d`, `0x4b2524` (system), `0x58457c` (player chat), all pass `[0xaad300] = -1` or the player number.
5. Confidence about 90% for the function, about 70% for the a1/a4/a5/a6 details; nothing run in game.
