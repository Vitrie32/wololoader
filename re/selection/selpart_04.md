# WorldPlayer Selection Function Classification - Part 04

This document classifies the candidate functions in Part 04 to determine if they read or write the `WorldPlayer` unit selection structure:
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
| `00818390` | **FALSE** | UI Dialog/Panel constructor (vtable `0x9e4244`, calls `0x47a3e0`). Offsets `+0x24c`, `+0x250`, `+0x254` are separate child control pointers. |
| `00840830` | **FALSE** | Spatial query/filter method. `+0x24c` is bool flag, `+0x250` is count of excluded unit IDs, `+0x254` is pointer to array of unit IDs to ignore. |
| `00840a50` | **FALSE** | Spatial query/filter method (variant of `00840830`). Uses same exclude unit ID list at `+0x24c`, `+0x250`, `+0x254`. |
| `00841020` | **FALSE** | Spatial query/filter method. Same query object fields (`+0x24c` flag, `+0x250` count, `+0x254` array pointer). |
| `00841220` | **FALSE** | Spatial query/filter method (variant). Same query object fields (`+0x24c`, `+0x250`, `+0x254`). |
| `00843460` | **FALSE** | Setter method on spatial query object. Directly assigns `+0x24c` (byte flag), `+0x250` (count), `+0x254` (int* array of ignored IDs). |
| `00869c40` | **FALSE** | Object constructor (vtable `0x9e6900`). Initializes an array of 6 structs of 5 DWORDs (`0x14` bytes each) starting at `+0x1f8` (`0x1f8..0x26c`). |
| `0086a6f0` | **TRUE** | View / minimap selection highlight rendering. Loads player from `[ebx+0x134]`, checks count at `+0x254`, iterates array at `+0x160` up to `0x250` bound, accesses `[esi+edi*1]` (`Unit*`), calls `0x86b800` and `0x86a420`. |
| `0086e330` | **TRUE** | View / input command handler. Loads player from `[esi+0x15c]`, checks count `+0x254`, loops over `+0x160` array via `[eax+ebx*1]`, reads and computes address of primary selected unit at `+0x15c`. |
| `0087e9b0` | **TRUE** | Target selection / click handler. Loads player from `[edi+0x15c]`, checks selection count `[ecx+0x254] < 1` to decide whether to target or execute context action. |
| `008843c0` | **TRUE** | Command target validation / action dispatch. Loads player from `[ebx+0x15c]`, checks count `+0x254`, loops through selection array `+0x160` via `[edi+eax*1]`, bounds loop against `+0x254`. |
| `00884d30` | **FALSE** | Unit action handler. Target object `edi` is a `Unit*`, writes `BYTE PTR [edi+0x254]`. |
| `00886970` | **TRUE** | Unit selection / click interaction handler. Loads player from `[edi+0x15c]`, repeatedly calls `0x755450` (`unselect_all`) and `0x754a20` (`select_unit`), tests primary unit `+0x15c` and count `+0x254`. |
| `008875a0` | **TRUE** | Shortcut / UI key action handler. Loads player from `[esi+0x15c]`, tests selection count `cmp DWORD PTR [eax+0x254], ecx` (checks if `count == 0`). |
| `008b9a60` | **FALSE** | Unit constructor (vtable `0x9eb090`). Initializes unit fields (`WORD PTR [esi+0x254]`, `BYTE PTR [esi+0x255]`, `BYTE PTR [esi+0x256]`, etc.). |
| `008b9d80` | **FALSE** | Unit constructor variant (vtable `0x9eb090`). Initializes unit fields (`WORD PTR [esi+0x254]`, `BYTE PTR [esi+0x256]`, etc.). |
| `008ba040` | **FALSE** | Unit constructor variant (vtable `0x9eb090`). Initializes unit fields (`WORD PTR [esi+0x254]`, `BYTE PTR [esi+0x256]`, etc.). |
| `008bfa40` | **FALSE** | Unit state / action update handler. `edi` is `Unit*`; accesses unit float field `movss [edi+0x250]` and byte field `movzx [edi+0x254]`. |

---

## Detailed Analysis of TRUE Functions

### `0086a6f0` - TRUE

- **Function Role**: View / minimap selection highlight rendering (draws selection outlines/markers around selected units).
- **Evidence**:
  - Retrieves `WorldPlayer` from view object:
    ```asm
    86af64: 8b 83 34 01 00 00 mov eax, DWORD PTR [ebx+0x134] ; WorldPlayer*
    ```
  - Checks if unit selection count `+0x254` is positive:
    ```asm
    86af6a: 83 b8 54 02 00 00 00 cmp DWORD PTR [eax+0x254], 0x0
    86af71: 0f 8e 97 00 00 00    jle 0x86b00e
    ```
  - Initializes array offset register `edi = 0x160`:
    ```asm
    86af86: bf 60 01 00 00       mov edi, 0x160
    ```
  - Inside loop, re-verifies processed unit index against count `[esi+0x254]`:
    ```asm
    86af96: 3b 8e 54 02 00 00    cmp ecx, DWORD PTR [esi+0x254]
    86af9c: 7d 70                jge 0x86b00e
    ```
  - Fetches unit pointer from selection array `player + edi`:
    ```asm
    86af9e: 8b 34 3e             mov esi, DWORD PTR [esi+edi*1]
    ```
  - Advances offset by 4 and enforces loop bound `0x250` (60 entries):
    ```asm
    86b003: 83 c7 04             add edi, 0x4
    86b006: 81 ff 50 02 00 00    cmp edi, 0x250
    86b00c: 7c 82                jl  0x86af90
    ```
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x0086af6a`: `83 b8 54 02 00 00 00` — `cmp DWORD PTR [eax+0x254], 0x0` (access form: count)
  - `0x0086af86`: `bf 60 01 00 00` — `mov edi, 0x160` (access form: base pointer lea / offset constant `0x160`)
  - `0x0086af96`: `3b 8e 54 02 00 00` — `cmp ecx, DWORD PTR [esi+0x254]` (access form: count)
  - `0x0086af9e`: `8b 34 3e` — `mov esi, DWORD PTR [esi+edi*1]` (access form: scaled index `[esi+edi*1]`, accessing `selected[i]`)
  - `0x0086b006`: `81 ff 50 02 00 00` — `cmp edi, 0x250` (access form: end bound `0x250`)
- **Bound constants**:
  - `0x160` (array base offset)
  - `0x250` (end bound offset: `0x160 + 60 * 4 = 0x250`)

---

### `0086e330` - TRUE

- **Function Role**: View / mouse input action handler (handles right-click / commands issued to current selection).
- **Evidence**:
  - Loads `WorldPlayer` from view object:
    ```asm
    86e430: 8b 86 5c 01 00 00 mov eax, DWORD PTR [esi+0x15c] ; WorldPlayer*
    ```
  - Checks if unit selection count `+0x254` is positive:
    ```asm
    86e43d: 83 b8 54 02 00 00 00 cmp DWORD PTR [eax+0x254], 0x0
    86e444: 0f 8e 3f 01 00 00    jle 0x86e589
    ```
  - Initializes offset `ebx = 0x160`:
    ```asm
    86e44a: bb 60 01 00 00       mov ebx, 0x160
    ```
  - Reads unit pointer from array:
    ```asm
    86e450: 8b 3c 18             mov edi, DWORD PTR [eax+ebx*1]
    ```
  - Increments index and checks against selection count:
    ```asm
    86e53a: 83 c3 04             add ebx, 0x4
    86e53d: 8b 86 5c 01 00 00    mov eax, DWORD PTR [esi+0x15c]
    86e543: 47                   inc edi
    86e547: 3b b8 54 02 00 00    cmp edi, DWORD PTR [eax+0x254]
    86e54d: 0f 8c fd fe ff ff    jl  0x86e450
    ```
  - Directly tests and accesses primary selected unit `+0x15c`:
    ```asm
    86e9d2: 83 b8 5c 01 00 00 00 cmp DWORD PTR [eax+0x15c], 0x0
    86ea5b: 05 5c 01 00 00       add eax, 0x15c ; &player->primary_selected passed to 0x813390
    86edec: 8b 91 5c 01 00 00    mov edx, DWORD PTR [ecx+0x15c]
    86eebf: ff b0 5c 01 00 00    push DWORD PTR [eax+0x15c]
    86eef4: ff b0 5c 01 00 00    push DWORD PTR [eax+0x15c]
    86ef41: 8b 89 5c 01 00 00    mov ecx, DWORD PTR [ecx+0x15c]
    86efcc: 8b b8 5c 01 00 00    mov edi, DWORD PTR [eax+0x15c]
    ```
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x0086e43d`: `83 b8 54 02 00 00 00` — `cmp DWORD PTR [eax+0x254], 0x0` (access form: count)
  - `0x0086e44a`: `bb 60 01 00 00` — `mov ebx, 0x160` (access form: base pointer lea / offset constant `0x160`)
  - `0x0086e450`: `8b 3c 18` — `mov edi, DWORD PTR [eax+ebx*1]` (access form: scaled index `[eax+ebx*1]`, accessing `selected[i]`)
  - `0x0086e547`: `3b b8 54 02 00 00` — `cmp edi, DWORD PTR [eax+0x254]` (access form: count, loop termination condition)
  - `0x0086e9d2`: `83 b8 5c 01 00 00 00` — `cmp DWORD PTR [eax+0x15c], 0x0` (access form: fixed index / primary selected unit pointer)
  - `0x0086ea5b`: `05 5c 01 00 00` — `add eax, 0x15c` (access form: base pointer lea / computes `&player->primary_selected`)
  - `0x0086edec`: `8b 91 5c 01 00 00` — `mov edx, DWORD PTR [ecx+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0086eebf`: `ff b0 5c 01 00 00` — `push DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0086eef4`: `ff b0 5c 01 00 00` — `push DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0086ef41`: `8b 89 5c 01 00 00` — `mov ecx, DWORD PTR [ecx+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0086efcc`: `8b b8 5c 01 00 00` — `mov edi, DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
- **Bound constants**:
  - `0x160` (array base offset)

---

### `0087e9b0` - TRUE

- **Function Role**: Target selection / mouse click dispatcher.
- **Evidence**:
  - Loads `WorldPlayer` from view object `edi`:
    ```asm
    87f965: 8b 8f 5c 01 00 00 mov ecx, DWORD PTR [edi+0x15c] ; WorldPlayer*
    ```
  - Checks if player selection count `+0x254` is less than or equal to 0 (where `esi = 1`):
    ```asm
    87f96e: 39 b1 54 02 00 00 cmp DWORD PTR [ecx+0x254], esi
    87f974: 7e 77             jle 0x87f9ed
    ```
  - Branch decides whether to perform click selection or issue commands to current selection.
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x0087f96e`: `39 b1 54 02 00 00` — `cmp DWORD PTR [ecx+0x254], esi` (access form: count)
- **Bound constants**:
  - None used in this function (compares count against `1`).

---

### `008843c0` - TRUE

- **Function Role**: Command target validation / action dispatch.
- **Evidence**:
  - Loads `WorldPlayer` from view object:
    ```asm
    884501: 8b 83 5c 01 00 00 mov eax, DWORD PTR [ebx+0x15c] ; WorldPlayer*
    ```
  - Checks if unit selection count `+0x254` is positive:
    ```asm
    88450c: 39 88 54 02 00 00 cmp DWORD PTR [eax+0x254], ecx ; ecx = 0
    884512: 0f 8e 3b 02 00 00 jle 0x884753
    ```
  - Initializes array offset `edi = 0x160`:
    ```asm
    884518: bf 60 01 00 00    mov edi, 0x160
    ```
  - Dereferences unit from `[edi+eax*1]` (`player + offset`):
    ```asm
    884520: 8b 3c 07          mov edi, DWORD PTR [edi+eax*1]
    ```
  - Checks if unit is valid and can execute order (`call 0x4cc250`, distance checks).
  - Loops over selection array until reaching `player->count`:
    ```asm
    8845b0: 83 c7 04          add edi, 0x4
    8845b3: 8b 4d f4          mov ecx, DWORD PTR [ebp-0xc]
    8845b6: 89 7d d4          mov DWORD PTR [ebp-0x2c], edi
    8845b9: 3b 88 54 02 00 00 cmp ecx, DWORD PTR [eax+0x254]
    8845bf: 0f 8c 5b ff ff ff jl  0x884520
    ```
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x0088450c`: `39 88 54 02 00 00` — `cmp DWORD PTR [eax+0x254], ecx` (access form: count)
  - `0x00884518`: `bf 60 01 00 00` — `mov edi, 0x160` (access form: base pointer lea / offset constant `0x160`)
  - `0x00884520`: `8b 3c 07` — `mov edi, DWORD PTR [edi+eax*1]` (access form: scaled index `[edi+eax*1]`, accessing `selected[i]`)
  - `0x008845b9`: `3b 88 54 02 00 00` — `cmp ecx, DWORD PTR [eax+0x254]` (access form: count, loop termination condition)
- **Bound constants**:
  - `0x160` (array base offset)

---

### `00886970` - TRUE

- **Function Role**: Unit selection / click interaction handler (processes clicking on units / buildings, single select, multi select).
- **Evidence**:
  - Loads `WorldPlayer` from view object `edi`:
    ```asm
    886c38: 8b 8f 5c 01 00 00 mov ecx, DWORD PTR [edi+0x15c] ; WorldPlayer*
    ```
  - Calls known selection functions `0x00755450` (`unselect_all`) and `0x00754a20` (`select_unit`) on `WorldPlayer`:
    ```asm
    886c3e: e8 0d e8 ec ff call 0x755450 ; unselect_all
    886c4e: e8 cd dd ec ff call 0x754a20 ; select_unit
    886c97: e8 b4 e7 ec ff call 0x755450 ; unselect_all
    886cc7: e8 54 dd ec ff call 0x754a20 ; select_unit
    886dcb: e8 80 e6 ec ff call 0x755450 ; unselect_all
    886ddb: e8 40 dc ec ff call 0x754a20 ; select_unit
    887098: e8 83 d9 ec ff call 0x754a20 ; select_unit
    8870e1: e8 6a e3 ec ff call 0x755450 ; unselect_all
    8870f1: e8 2a d9 ec ff call 0x754a20 ; select_unit
    8871a2: e8 a9 e2 ec ff call 0x755450 ; unselect_all
    8871b7: e8 64 d8 ec ff call 0x754a20 ; select_unit
    8871ff: e8 4c e2 ec ff call 0x755450 ; unselect_all
    887230: e8 1b e2 ec ff call 0x755450 ; unselect_all
    887298: e8 b3 e1 ec ff call 0x755450 ; unselect_all
    887322: e8 29 e1 ec ff call 0x755450 ; unselect_all
    887332: e8 e9 d6 ec ff call 0x754a20 ; select_unit
    8873fa: e8 51 e0 ec ff call 0x755450 ; unselect_all
    ```
  - Checks/reads primary selected unit `+0x15c`:
    ```asm
    886c6f: 8b 81 5c 01 00 00 mov eax, DWORD PTR [ecx+0x15c]
    886eb2: ff b0 5c 01 00 00 push DWORD PTR [eax+0x15c]
    886fdb: ff b0 5c 01 00 00 push DWORD PTR [eax+0x15c]
    887059: ff b0 5c 01 00 00 push DWORD PTR [eax+0x15c]
    8870d7: 39 88 5c 01 00 00 cmp DWORD PTR [eax+0x15c], ecx
    887145: 8b 80 5c 01 00 00 mov eax, DWORD PTR [eax+0x15c]
    887252: 8b 80 5c 01 00 00 mov eax, DWORD PTR [eax+0x15c]
    887290: 3b 91 5c 01 00 00 cmp edx, DWORD PTR [ecx+0x15c]
    ```
  - Reads and tests selection count `+0x254`:
    ```asm
    88719c: 8b b1 54 02 00 00 mov esi, DWORD PTR [ecx+0x254]
    8872d8: 83 b8 54 02 00 00 00 cmp DWORD PTR [eax+0x254], 0x0
    887315: 83 b9 54 02 00 00 01 cmp DWORD PTR [ecx+0x254], 0x1
    887353: 83 b9 54 02 00 00 00 cmp DWORD PTR [ecx+0x254], 0x0
    ```
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x00886c6f`: `8b 81 5c 01 00 00` — `mov eax, DWORD PTR [ecx+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x00886eb2`: `ff b0 5c 01 00 00` — `push DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x00886fdb`: `ff b0 5c 01 00 00` — `push DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x00887059`: `ff b0 5c 01 00 00` — `push DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x008870d7`: `39 88 5c 01 00 00` — `cmp DWORD PTR [eax+0x15c], ecx` (access form: fixed index / primary selected unit pointer)
  - `0x00887145`: `8b 80 5c 01 00 00` — `mov eax, DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0088719c`: `8b b1 54 02 00 00` — `mov esi, DWORD PTR [ecx+0x254]` (access form: count)
  - `0x00887252`: `8b 80 5c 01 00 00` — `mov eax, DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x00887290`: `3b 91 5c 01 00 00` — `cmp edx, DWORD PTR [ecx+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x008872d8`: `83 b8 54 02 00 00 00` — `cmp DWORD PTR [eax+0x254], 0x0` (access form: count)
  - `0x00887315`: `83 b9 54 02 00 00 01` — `cmp DWORD PTR [ecx+0x254], 0x1` (access form: count)
  - `0x00887353`: `83 b9 54 02 00 00 00` — `cmp DWORD PTR [ecx+0x254], 0x0` (access form: count)
- **Bound constants**:
  - None used in this function.

---

### `008875a0` - TRUE

- **Function Role**: Shortcut / UI key action handler.
- **Evidence**:
  - Loads `WorldPlayer` from view object:
    ```asm
    887613: 8b 86 5c 01 00 00 mov eax, DWORD PTR [esi+0x15c] ; WorldPlayer*
    ```
  - Checks if player selection count `+0x254` is equal to 0 (where `ecx = 0`):
    ```asm
    887619: 39 88 54 02 00 00 cmp DWORD PTR [eax+0x254], ecx
    88761f: 75 db             jne 0x8875fc
    ```
  - If count is non-zero, aborts shortcut action.
- **Instructions touching offsets `0x15c..0x254`**:
  - `0x00887619`: `39 88 54 02 00 00` — `cmp DWORD PTR [eax+0x254], ecx` (access form: count)
- **Bound constants**:
  - None used in this function (compares count against `0x0`).
