# Player Selection Function Classification (Part 02)

Analysis of 18 candidate functions for accesses to `WorldPlayer` selection members:
- `+0x15c`: `selected_unit_primary` (`RGE_Static_Object*`)
- `+0x160..+0x24c`: `selected_units[60]` (`RGE_Static_Object*[60]`, 240 / 0xf0 bytes)
- `+0x250`: DWORD (padding / internal state)
- `+0x254`: `selected_unit_count` (`int`, 0..60)

---

## 1. `006bb100` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Receives/fetches `WorldPlayer` pointer via `call 0x540c90` into `edi`.
  - Checks if `player->selected_unit_count` (`[edi+0x254]`) > 0.
  - Takes `lea ebx, [edi+0x160]` and iterates through `selected_units` pointers via `[ebx]`, incrementing `ebx` by 4 and comparing loop index against `player->selected_unit_count` (`[edi+0x254]`).
  - Calls unit virtual function `+0x228` and inspects unit types (`+0xc`, `+0x10`).
- **Instructions touching `0x15c..0x254`**:
  - `0x006bbdc2` | `39 87 54 02 00 00` | `cmp DWORD PTR [edi+0x254],eax` | **count** (check if count > 0 before loop)
  - `0x006bbdce` | `8d 9f 60 01 00 00` | `lea ebx,[edi+0x160]` | **base pointer lea** (start of selected_units array)
  - `0x006bbe66` | `3b b7 54 02 00 00` | `cmp esi,DWORD PTR [edi+0x254]` | **count** (loop termination bound check)
- **Bound Constants**: None (loop is dynamically bounded by `[edi+0x254]`).

---

## 2. `006bc6d0` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Calls known player getter `0x540c90` into `edx`.
  - Takes `lea esi, [edx+0x160]` and loops up to 60 (`0x3c`) times.
  - Checks `player->selected_unit_count` (`[edx+0x254]`).
  - Accesses unit objects at `[esi]`, advances `esi` by 4 (`add esi, 0x4`), and calls unit group check `0x74f290`.
- **Instructions touching `0x15c..0x254`**:
  - `0x006bc6ee` | `8d b2 60 01 00 00` | `lea esi,[edx+0x160]` | **base pointer lea** (start of selected_units array)
  - `0x006bc700` | `83 ba 54 02 00 00 00` | `cmp DWORD PTR [edx+0x254],0x0` | **count** (check if count <= 0)
- **Bound Constants**:
  - `0x3c` at `0x006bc77e` (`83 ff 3c` | `cmp edi,0x3c` — loop upper bound of 60 entries).

---

## 3. `006bf260` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Function centers view on selection (calculates bounding box / centroid of selected units).
  - Repeatedly queries `WorldPlayer` via `0x540c90`.
  - Checks `player->selected_unit_count` (`[eax+0x254]`).
  - Iterates over `player + esi` starting with `esi = 0x160`, incrementing `esi` by 4 (`add esi, 0x4`) and indexing unit pointer with `mov ecx, DWORD PTR [eax+esi*1]`.
  - Bounded by loop condition `cmp edi, DWORD PTR [eax+0x254]`.
  - If multi-selection, calculates average coordinates and calls `0x7551c0` / `0x755100` on player.
  - If single unit, falls back to `player->selected_unit_primary` at `+0x15c` (`mov esi, DWORD PTR [eax+0x15c]`).
- **Instructions touching `0x15c..0x254`**:
  - `0x006bf286` | `83 b8 54 02 00 00 00` | `cmp DWORD PTR [eax+0x254],0x0` | **count** (check if count > 0)
  - `0x006bf2c9` | `39 b8 54 02 00 00` | `cmp DWORD PTR [eax+0x254],edi` | **count** (check count > 0 before loop)
  - `0x006bf2eb` | `8b 0c 30` | `mov ecx,DWORD PTR [eax+esi*1]` | **scaled index** (eax = player, esi starts at 0x160)
  - `0x006bf347` | `3b b8 54 02 00 00` | `cmp edi,DWORD PTR [eax+0x254]` | **count** (loop termination bound check)
  - `0x006bf429` | `8b b0 5c 01 00 00` | `mov esi,DWORD PTR [eax+0x15c]` | **fixed index** (read primary selected unit)
- **Bound Constants**:
  - `0x160` base offset loaded into `esi` at `0x006bf2d5` (`be 60 01 00 00` | `mov esi,0x160`). Loop is bounded by count `[eax+0x254]`.

---

## 4. `006c1cf0` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Main gameplay UI update tick loop.
  - Obtains `WorldPlayer` via `call 0x540c90`.
  - Compares cached primary selection pointer `[edi+0xd5c]` against `player->selected_unit_primary` (`[eax+0x15c]`).
  - Compares cached selection count `[edi+0xd60]` against `player->selected_unit_count` (`[eax+0x254]`).
  - Pushes `player->selected_unit_primary` (`[eax+0x15c]`) and calls `0x719a10` (update selection UI panel for primary unit).
  - Also calls `0x6bf260` (selection center) and `0x6c36e0` (selection refresh).
- **Instructions touching `0x15c..0x254`**:
  - `0x006c21c5` | `3b 88 5c 01 00 00` | `cmp ecx,DWORD PTR [eax+0x15c]` | **fixed index** (compare cached primary unit with +0x15c)
  - `0x006c21df` | `3b 88 54 02 00 00` | `cmp ecx,DWORD PTR [eax+0x254]` | **count** (compare cached selection count with +0x254)
  - `0x006c21f8` | `ff b0 5c 01 00 00` | `push DWORD PTR [eax+0x15c]` | **fixed index** (push primary selected unit to update UI)
- **Bound Constants**: None.

---

## 5. `006c36e0` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Selection synchronization function (called when selection state changes).
  - Obtains `WorldPlayer` via `call 0x540c90`.
  - Reads `player->selected_unit_primary` (`+0x15c`) into temporary register and caches it into UI state (`param_1 + 0xd5c`).
  - Reads `player->selected_unit_count` (`+0x254`) and caches lower 16 bits into UI state (`param_1 + 0xd60`).
  - Calls `0x6c9dc0` (action button / command panel updater for selected units).
- **Instructions touching `0x15c..0x254`**:
  - `0x006c36fc` | `8b b0 5c 01 00 00` | `mov esi,DWORD PTR [eax+0x15c]` | **fixed index** (read primary selected unit)
  - `0x006c3707` | `8b 80 5c 01 00 00` | `mov eax,DWORD PTR [eax+0x15c]` | **fixed index** (read primary selected unit to cache at [ebx+0xd5c])
  - `0x006c3720` | `66 8b 80 54 02 00 00` | `mov ax,WORD PTR [eax+0x254]` | **count** (read selection count to cache at [ebx+0xd60])
- **Bound Constants**: None.

---

## 6. `006c9dc0` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Primary UI command button bar refresher for the current selection.
  - Obtains `WorldPlayer` via `call 0x540c90` into `edi`.
  - Contains multiple loops iterating over `player->selected_units` starting at `lea reg, [edi+0x160]`, advancing by 4 bytes, bounded by `player->selected_unit_count` (`[edi+0x254]`).
  - Checks if `player->selected_unit_count > 1` (`cmp DWORD PTR [edi+0x254], 0x1`) to show formation buttons.
  - Passes `&player->selected_units[0]` (`lea eax, [edi+0x160]`) and `player->selected_unit_count` (`push [edi+0x254]`) to helper `0x7d8f60`.
- **Instructions touching `0x15c..0x254`**:
  - `0x006ca34e` | `39 8f 54 02 00 00` | `cmp DWORD PTR [edi+0x254],ecx` | **count** (loop 1 pre-check: count > 0)
  - `0x006ca35a` | `8d 97 60 01 00 00` | `lea edx,[edi+0x160]` | **base pointer lea** (loop 1 selected_units array base)
  - `0x006ca38f` | `3b 8f 54 02 00 00` | `cmp ecx,DWORD PTR [edi+0x254]` | **count** (loop 1 termination bound)
  - `0x006ca728` | `39 b7 54 02 00 00` | `cmp DWORD PTR [edi+0x254],esi` | **count** (loop 2 pre-check: count > 0)
  - `0x006ca734` | `8d 9f 60 01 00 00` | `lea ebx,[edi+0x160]` | **base pointer lea** (loop 2 selected_units array base)
  - `0x006ca8de` | `3b b7 54 02 00 00` | `cmp esi,DWORD PTR [edi+0x254]` | **count** (loop 2 termination bound)
  - `0x006ca9cc` | `39 8f 54 02 00 00` | `cmp DWORD PTR [edi+0x254],ecx` | **count** (loop 3 pre-check: count > 0)
  - `0x006ca9d8` | `8d b7 60 01 00 00` | `lea esi,[edi+0x160]` | **base pointer lea** (loop 3 selected_units array base)
  - `0x006caab2` | `3b 8f 54 02 00 00` | `cmp ecx,DWORD PTR [edi+0x254]` | **count** (loop 3 termination bound)
  - `0x006cb570` | `83 bf 54 02 00 00 01` | `cmp DWORD PTR [edi+0x254],0x1` | **count** (check if multi-selection count > 1 for formations)
  - `0x006cb8b6` | `ff b7 54 02 00 00` | `push DWORD PTR [edi+0x254]` | **count** (push count argument to 0x7d8f60)
  - `0x006cb8bc` | `8d 87 60 01 00 00` | `lea eax,[edi+0x160]` | **base pointer lea** (push array pointer argument to 0x7d8f60)
- **Bound Constants**: None (all iterations dynamically bounded by `[edi+0x254]`).

---

## 7. `006f43b0` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Constructor for `Match Visibility Dialog` (`FUN_0047a3e0("Match Visibility Dialog", ...)`). `this` pointer (`esi`) is a dialog object. Offsets `0x248..0x27c` initialize dialog child control pointers and parameters. Does not reference `WorldPlayer` or unit selection.

---

## 8. `006f4500` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Destructor for `Match Visibility Dialog`. `param_1` is the dialog object. Decrements reference counts and frees child controls located at dialog offsets `0x248..0x27c`. Not `WorldPlayer`.

---

## 9. `006f46a0` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Member function of `Match Visibility Dialog`. Reads UI control values stored in the dialog (`param_1 + 0x248..0x25c`) and applies them to match visibility settings. Not `WorldPlayer`.

---

## 10. `006f4860` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Member function of `Match Visibility Dialog`. Populates dialog controls at `param_1 + 0x248..0x254` with match visibility options. Not `WorldPlayer`.

---

## 11. `006f4c40` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Dialog setup/layout function for `Match Visibility Dialog`. Sets up child buttons and dropdowns at dialog offsets `0x24c`, `0x250`, `0x254`. Not `WorldPlayer`.

---

## 12. `007072f0` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Copy constructor / assignment operator for a game stats / profile record structure (size > 0x2a0 bytes, containing std::string at `+0x280`). Copies blocks of DWORDs across `+0x200..+0x254` between two record instances (`param_1` and `param_2`). Not `WorldPlayer`.

---

## 13. `00707c40` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Field copy function for the same player stats / profile record structure as `007072f0` (copies 4 groups of 0x78 bytes). Not `WorldPlayer`.

---

## 14. `00709e40` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Profile / statistics rendering and calculation function. Operates on local stack arrays and instances of the 0x2f8-byte stats record, calling `007072f0` and GDI functions (`HDC`, `COLORREF`). Accesses to offsets `+0x200..+0x254` are on records within that table. Not `WorldPlayer`.

---

## 15. `00713f00` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Object status panel drawing function (`tpnl_obj`, `param_1` in `edi`).
  - Reads active player pointer from `[edi+0xbf0]` into `edx`.
  - Checks if `player->selected_unit_count` (`[edx+0x254]`) > 1.
  - If count > 1, branches immediately to `call 0x716360` (the multi-unit selection icon grid layout function). If count == 1, continues to single unit status drawing.
- **Instructions touching `0x15c..0x254`**:
  - `0x00713fe4` | `83 ba 54 02 00 00 01` | `cmp DWORD PTR [edx+0x254],0x1` | **count** (branch to multi-unit selection HUD 0x716360 if count > 1)
- **Bound Constants**: None.

---

## 16. `007187b0` — **TRUE**
- **Decision**: TRUE
- **Evidence**:
  - Object status panel update / refresh tick (`tpnl_obj`, `ebx`).
  - Reads player pointer from `[ebx+0xbf0]`.
  - Checks `player->selected_unit_count` (`[eax+0x254] > 1`).
  - Reads `player->selected_unit_primary` (`[edx+0x15c]`) and calls `0x719a10` to switch primary display unit.
  - Calls known player selection member function `0x755540` (`WorldPlayer::unselect_unit`) on the player object (`[ebx+0xbf0]`) at `0x007189e8`.
- **Instructions touching `0x15c..0x254`**:
  - `0x00718814` | `83 b8 54 02 00 00 01` | `cmp DWORD PTR [eax+0x254],0x1` | **count** (check if multi-selection count > 1)
  - `0x00718959` | `8b 82 5c 01 00 00` | `mov eax,DWORD PTR [edx+0x15c]` | **fixed index** (read primary selected unit)
- **Bound Constants**: None.

---

## 17. `0071bcb0` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Constructor for UI component (`vtable 0x009d9b10`). `param_1` is a UI element instance. Offsets `0x15c..0x254` initialize UI properties, text formatting, and child control pointers. Not `WorldPlayer`.

---

## 18. `0071f6c0` — **FALSE**
- **Decision**: FALSE
- **Evidence**: Member function of a UI dialog / panel (`__thiscall` taking 0x3c parameter bytes). Manages child controls stored in the dialog object at offsets `+0x218..+0x258`. Not `WorldPlayer`.

---

## Summary of Classifications
- **TRUE** (8 functions): `006bb100`, `006bc6d0`, `006bf260`, `006c1cf0`, `006c36e0`, `006c9dc0`, `00713f00`, `007187b0`
- **FALSE** (10 functions): `006f43b0`, `006f4500`, `006f46a0`, `006f4860`, `006f4c40`, `007072f0`, `00707c40`, `00709e40`, `0071bcb0`, `0071f6c0`
