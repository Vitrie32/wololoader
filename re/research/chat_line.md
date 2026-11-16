# In-game chat line: candidate function and how to call it

Status: partly verified. The function that builds a chat-style line is found and read.
Its only caller is found. The network chat receive path is NOT found. Nothing was run in game.

## 1. Best candidate: `0x6bf910` (chat line builder)

- Prologue verified: `push ebp; mov ebp,esp`, `ret 0x10` at `0x6bf9e3` (4 stack args, __thiscall, ECX = GameScreen `this`).
- Only caller in the whole exe: `0x86844c` (see section 2).

Arguments (verified from the code):

| Arg | Offset | Meaning (from code) |
|---|---|---|
| p1 | `0x8(%ebp)` | C string, the text. If `text[0] == '<'` (0x3c), the code loops over an 8-entry name table at `0xada81c` (stride 0x40), compares the name with `0x8d70f0`, and on a match: advances the text past the `<Name>` tag (`edi += count` from `0xada820`) and sets the colour byte from `0xada818[i]`. No match: colour stays `0xff`. |
| p2 | `0xc(%ebp)` | Pushed unchanged as the 6th arg of `0x664840`. Meaning not known (likely sender / owner id). |
| p3 | `0x10(%ebp)` | Seconds. If > 0: `0x472020(0xb, sec*1000)` makes a timer object, and the message type is **3**. If <= 0: `0x472020(0xb, -1)`, type **0** (no timeout). |
| p4 | `0x14(%ebp)` | Mode 0..2. Values > 2 go to a branch at `0x6bfa19` that I did not read. The mode picks the target manager: `ecx = [this + 0xd34 + mode*4]`. The byte at `0x14(%ebp)` is also overwritten with the colour, which is pushed as the 3rd arg of `0x664840`. |

The call it makes, verified at `0x6bf9d8` (type 3, timed) and `0x6bf9ff` (type 0, untimed):

```
push 2                 ; 7th arg of 0x664840 (constant)
push p2                ; 6th arg
push eax               ; 5th arg = timer object from 0x472020 (refcounted), or 0 in the zero-timeout path
push 0                 ; 4th arg = flag
push colour            ; 3rd arg
push text              ; 2nd arg (edi, after the name tag was stripped)
push 3                 ; 1st arg = type (0 on the untimed path)
mov ecx, [this+0xd34+mode*4]
call 0x664840          ; ret 0x20
```

Caveat: this site pushes only 7 values. `0x664840` reads its 8th argument at `0x24(%ebp)` (at `0x66499e`). The caller's 8th slot is not visibly pushed, so the value is whatever is on the stack. Not checked whether it matters.

What `0x664840` does with its args (from the prologue, verified): `[ebp+8]` = type is stored and compared with the last type (dedup). `[ebp+0x18]` = refcounted object, `incl 0x4(%edi)`; null in the display_message path. `[ebp+0x20]` is stored at `-0x24`. So its signature is `__thiscall(this=manager, p1..p8)`, `ret 0x20`, which matches the verified display_message call.

## 2. Only caller: `0x86844c` (inside the function that starts at `0x867b60`)

```
868437: mov  eax,[edi+0x64]          ; mode
86843a: xor  edx,edx
86843c: cmp  eax,2 ; cmova eax,edx  ; mode = (x > 2) ? 0 : x
868442: push eax                     ; p4 = mode
868443: push [edi+0x3c]              ; p3 = seconds
868446: push [edi+0x74]              ; p2
868449: push ecx                     ; p1 = text
86844a: mov  ecx,esi                 ; esi = GameScreen this
86844c: call 0x6bf910
```

The text (p1) is chosen just before:
- if `eax` (string id) is negative (`js 868434`): `ecx = [edi+0x70]`, a raw string;
- otherwise: `ecx = 0x540d50(eax)`, a localized string looked up by id.

So the record at `edi` holds: string id or raw text (`0x70`), mode (`0x64`), duration (`0x3c`), p2 (`0x74`). The record is probably a queued event or chat message. The function at `0x867b60` has a 0x9c84-byte frame and many calls. I did not identify it.

## 3. Other message sites (for comparison)

- `0x664840` callers total: about 50 sites. Most push `type = 1` and use the string table (`0x483f00`) with `0xf3` as an extra arg. These are the bottom notification line through `screen+0xc28`, as already verified in memory.
- `0x6cbed0` (display_message) callers: 7 sites (`0x62eaa9`, `0x631e48`, `0x6336c5`, `0x633acc`, `0x633b98`, `0x6ba54a`, `0x6bee21`). All push `type = 1` as the first arg.
- Type 0 and type 3 are used by `0x664840` inside `0x6bf910` (manager `this+0xd34[mode]`). The earlier note "type 0 shows nothing" applies to the `0xc28` manager in display_message. It does NOT rule out type 0 on the `0xd34` managers. Not tested.
- Type 3 is also pushed in `0x6c1684`, `0x6c1734`, `0x6c180b`, `0x6c18b5`, `0x6c6635`, `0x6c6811`, `0x6c69f5` (color `0xff` or `0`). Not analysed.

## 4. Strings (file offset to VA: `VA = off - 0x5ab400 + 0x9ac000`)

- `"%s: %s"` at VA `0x9af8e0`: format used by `0x44b520`, `0x690b21`, `0x702630` (string builders, not identified as chat).
- `"%c#%s: %s"` at VA `0x9d2b0c`, `"  *message : %s"` at `0x9d2cac`: used at `0x68069f`, `0x682f36`. Not analysed.
- `"chat_panel[%d]"` at VA `0x9d5614` and `"chat_word_wrap_panel"` at `0x9d5624`: UI panel names used at `0x6b648b` and `0x6b653d` (chat panel setup, not analysed in detail).
- AI script commands registered with `0x4d3950(name, argc, handler)`: `chat-local` (`0x9b6628`, handler `0x4e6640`), `chat-to-all`, `chat-to-allies`, `chat-to-enemies`, `chat-to-player`, `up-chat-data-to-all`, etc. These are AI/script commands, not the network receive path. Handler `0x4e6640` only calls `0x4d2590` and `0x8037b0`.
- `CommPacketChat` / `CommPacketChatGroup` RTTI names at VA `0x9c392c`-area. The handler method was NOT found.

## 5. How to call it locally from the DLL (proposal, not tested)

```c
/* 0x6bf910: __thiscall(GameScreen *self, const char *text, DWORD p2, int seconds, int mode), ret 0x10
 * self = [0xaf3d90] (verified). mode 0..2. seconds > 0 gives a timed line (type 3). */
typedef void (__fastcall *chat_line_fn)(void *self, void *edx, const char *text, DWORD p2, int seconds, int mode);
#define CHAT_LINE ((chat_line_fn)0x6bf910)
/* call: CHAT_LINE(screen, NULL_EDX, text, 0, 10, 0); */
```

Notes:
- The name tag is parsed by the game. Send `"<PlayerName>text"` only if you want the name colour.
- `p2` is unknown. Use 0 and check in game.
- Not called from a network path, so no send happens.

## 6. Confidence

- Address `0x6bf910`, calling convention, `ret 0x10`, and that it calls `0x664840` with type 3 or 0: **high** (read in disassembly).
- Meaning of p1 (text), p3 (seconds), p4 (mode): **medium-high** (code-based, consistent with the caller).
- That this is THE chat line shown in the chat area: **medium**. It handles `<Name>` tags and per-mode managers, which fits chat. Not verified in game. Needs one in-game test (call it with a string and mode 0 and check where it shows).
- Network CommPacketChat handler and its link to `0x867b60`: **not found**.

## 7. Next steps

1. Run the call from `wl_hook` or an inject in the headless test and see where the line appears. Try modes 0, 1, 2.
2. Identify `0x867b60` (what record `edi` is). Its callers will show the chat source.
3. Find the `CommPacketChat` handler from its vtable or `CommHandler` registration.
4. Read the `mode > 2` branch at `0x6bfa19`.
