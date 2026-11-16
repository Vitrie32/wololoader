# WorldPlayer Selection Function Classification - Part 00

This document classifies the candidate functions in Part 00 to determine if they read or write the `WorldPlayer` unit selection structure:
- `+0x15c`: Primary selected unit pointer (`Unit*`)
- `+0x160..+0x24c`: Selection array (`Unit* selected[60]`)
- `+0x250`: DWORD (selection flags / state)
- `+0x254`: INT (selection count)

Reference selection functions:
- `0x00754a20`: `select_unit(Unit* unit, int add_to_selection)`
- `0x00755450`: `unselect_all()` (uses `+0x15c`, loops `0x3c` entries over `+0x160`, reads/clears `+0x254`, clears `unit+0x3a` selected flag)
- `0x007554a0`
- `0x00755540`

---

## Summary of Classifications

| Address | Classification | Description / Evidence |
|---|---|---|
| `0047a930` | **FALSE** | UI Panel constructor (vtable `0x9b1ad0`). Fields `+0x24c`, `+0x250`, `+0x254` are UI control properties. |
| `00482ee0` | **FALSE** | UI Panel destructor (vtable `0x9b20a4`). Compares `[esi+0x250]`/`[esi+0x254]` against `this` for focus/hover cleanup. |
| `00486040` | **FALSE** | UI element constructor. Zero-initializes control properties at `+0x23c..+0x254`. |
| `004866f0` | **FALSE** | UI control animation/timer setter (`+0x24c`, `+0x250`, `+0x254`, `+0x258`, `+0x25c`, `+0x260` with `timeGetTime`). |
| `00489b90` | **FALSE** | UI element constructor (vtable `0x9b24ac`). Initializes UI properties including `+0x250`, `+0x254`. |
| `0048b120` | **FALSE** | Scrollbar rendering function. `+0x250..+0x25c` are bounding box coordinates passed to draw routine `0x48b330`. |
| `0048b790` | **FALSE** | Scrollbar hit test handler. Compares mouse coordinates against button/thumb rects at `+0x250..+0x25c`. |
| `0048b990` | **FALSE** | Scrollbar thumb drag handler. Uses `+0x250` and `+0x254` as scrollbar rect bounds. |
| `0048be00` | **FALSE** | Scrollbar layout update. Computes geometry and stores button/track rects at `+0x250`, `+0x254`, `+0x25c`, etc. |
| `004a9180` | **TRUE** | Unit action complete handler (e.g. building completed). Reads `unit+0x3a` selected flag; gets `WorldPlayer` via `0x540c90`, checks selection count `+0x254 == 0`, and calls `0x754a20` (`select_unit`). |
| `00518bc0` | **FALSE** | AI settings table constructor. Initializes 300 DWORDs of strategic defaults (`+0x15c`, `+0x160`, `+0x250`, `+0x254`, etc.). |
| `00519760` | **FALSE** | AI settings serializer. Sends AI strategic attributes (`+0x250`, `+0x254`, etc.) via `0x5328c0`. |
| `005225c0` | **FALSE** | AI strategic calculation logic. Compares threshold against `+0x254` on AI personality struct; calls `0x5408c0` to get player. |
| `005a1e50` | **FALSE** | Cleans struct containing 9 player slots of size `0x40` (offset `0x10..0x250`), resets `+0x250` and `+0x260`. |
| `005a35d0` | **FALSE** | Copies data into player slot struct: DWORD to `+0x250`, string to `+0x254` via `0x432970`. |
| `005b1d00` | **FALSE** | Composite object constructor (vtable `0x9c59cc`). Sub-object vtable `0x9c592c` placed at `+0x250`, byte at `+0x254` set to 0. |
| `00610f30` | **FALSE** | Multi-player array handler with stride `0x68` (entries at `+0xb4`, `+0x11c`, `+0x184`, `+0x1ec`, `+0x254`, `+0x2bc`, `+0x324`, `+0x38c`). |
| `00613850` | **FALSE** | Clear method for the same 8-entry multi-player struct with stride `0x68`. Clears `+0x254` as entry index 4. |

---

## Detailed Analysis of TRUE Functions

### `004a9180` - TRUE

- **Function Role**: Unit action/task completion handler (building completed by builder).
- **Evidence**:
  - Checks if the builder unit is currently selected:
    ```asm
    4a92fe: 8a 59 3a        mov bl, BYTE PTR [ecx+0x3a]  ; Unit+0x3a selected flag
    ```
  - Later tests `bl` (flag bit 0):
    ```asm
    4a93a9: f6 c3 01        test bl, 0x1
    4a93ac: 74 20           je 0x4a93ce
    ```
  - Retrieves the `WorldPlayer` pointer via player getter `0x00540c90`:
    ```asm
    4a93ae: 8b 0d 58 2c af 00 mov ecx, DWORD PTR ds:0xaf2c58
    4a93b4: e8 d7 78 09 00    call 0x540c90
    ```
  - Checks if the player's unit selection count is 0:
    ```asm
    4a93b9: 83 b8 54 02 00 00 00 cmp DWORD PTR [eax+0x254], 0x0
    4a93c0: 75 0c                jne 0x4a93ce
    ```
  - If count is 0, calls `0x00754a20` (`select_unit`) on `ecx = eax` (`WorldPlayer`) to select the newly finished structure (`[edi+0x10]`):
    ```asm
    4a93c2: 6a 01                push 0x1
    4a93c4: ff 77 10             push DWORD PTR [edi+0x10]
    4a93c7: 8b c8                mov ecx, eax
    4a93c9: e8 52 b6 2a 00       call 0x754a20
    ```
- **Instructions touching offsets `0x15c..0x254`**:
  - `004a93b9`: `83 b8 54 02 00 00 00` — `cmp DWORD PTR [eax+0x254], 0x0` (access form: count)
- **Bound constants**:
  - None used in this function (compares count against `0x0`).
