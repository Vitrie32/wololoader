# Selection Function Classification (Part 03)

This report classifies 18 candidate functions in `AoK HD.exe` (Age of Empires II HD 5.8) to determine whether they access the `WorldPlayer` unit selection structure:
- `+0x15c`: Primary selected unit pointer
- `+0x160..+0x24c`: Pointer array `selected[60]` (240 bytes)
- `+0x250`: Selection capability bitmask / dword
- `+0x254`: Selection count (`int`)

---

## Summary of Classifications

| Function | Classification | Description / Notes |
| :--- | :--- | :--- |
| `00721090` | **FALSE** | UI Mod Manager download dialog initialization (`dlg_objx`, `MODMGR_DOWNLOAD_OKAY`). Offsets are UI child controls. |
| `0073dc60` | **TRUE** | Computes / updates selection capability flags (`+0x250`) by iterating `selected[60]`. |
| `007403b0` | **TRUE** | `WorldPlayer` selection status & capability tester (checks `+0x15c`, `+0x250`, `+0x254`). |
| `0074eec0` | **TRUE** | Computes minimum unit formation/speed byte (`[unit+0xc]+0x95`) across `selected[60]`. |
| `0074ef20` | **TRUE** | Filters units in `selected[60]` matching formation/type criteria into `DAT_00af4e40`. |
| `0074f0c0` | **TRUE** | Assigns control group (`1..9`) bitmask (`[unit+0x158]`) to units in `selected[60]`. |
| `0074fb20` | **TRUE** | Preserves selection array across group operations; calls `0x751480` and `0x754a20`. |
| `00751480` | **TRUE** | Clears selection (`unselect_all` helper / zeroer: `+0x15c = 0`, `+0x254 = 0`, zeros `+0x160..+0x24c`). |
| `00754a20` | **TRUE** | `WorldPlayer::select_unit` (adds unit to `selected[60]`, updates `+0x15c`, increments `+0x254`, sets `unit+0x3a`). |
| `00755370` | **TRUE** | Calls virtual method `+0x304` on every valid unit in `selected[60]`. |
| `007553c0` | **TRUE** | Checks if all selected units share the same formation/stance (`[unit+0xc]+0x97`). |
| `00755450` | **TRUE** | `WorldPlayer::unselect_all` (clears `unit+0x3a`, zeros array `+0x160`, clears `+0x15c` and `+0x254`). |
| `007554a0` | **TRUE** | `WorldPlayer::unselect_unit_by_index` (shifts `selected[60]` down, decrements `+0x254`, updates `+0x15c`). |
| `00755540` | **TRUE** | `WorldPlayer::unselect_unit_by_pointer` (finds unit pointer in `selected[60]`, calls `0x7554a0`). |
| `00755850` | **TRUE** | Validates unit status in `selected[60]`, unselecting dead/invalid units via `0x7554a0`. |
| `007e48a0` | **FALSE** | Debug path / waypoint overlay rendering. Calls unit vtable functions at `+0x250` and `+0x254`. |
| `008138a0` | **FALSE** | Unit placement / build command executor. Writes byte to `[UnitType+0x254]`. |
| `008178f0` | **FALSE** | UI message box / confirmation dialog layout. Stores button widget pointers at `[dlg+0x250..0x258]`. |

---

## Detailed Analysis of TRUE Functions

### 1. `0073dc60` (TRUE)
Iterates through `selected[60]` of `WorldPlayer` (`edx`) to compute and update the selection capabilities mask at `edx+0x250`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x73dc72`: `8b 82 54 02 00 00` — `mov eax, DWORD PTR [edx+0x254]` (access form: count)
  - `0x73dc78`: `c7 82 50 02 00 00 00 00 00 00` — `mov DWORD PTR [edx+0x250], 0x0` (access form: fixed index / capabilities mask)
  - `0x73dc8e`: `8d 9a 60 01 00 00` — `lea ebx, [edx+0x160]` (access form: base pointer lea)
  - `0x73dd03`: `0b b2 50 02 00 00` — `or esi, DWORD PTR [edx+0x250]` (access form: fixed index / capabilities mask)
  - `0x73dd09`: `89 b2 50 02 00 00` — `mov DWORD PTR [edx+0x250], esi` (access form: fixed index / capabilities mask)
  - `0x73dd2d`: `89 b2 50 02 00 00` — `mov DWORD PTR [edx+0x250], esi` (access form: fixed index / capabilities mask)
  - `0x73dd3c`: `0b 8a 50 02 00 00` — `or ecx, DWORD PTR [edx+0x250]` (access form: fixed index / capabilities mask)
  - `0x73dd42`: `89 8a 50 02 00 00` — `mov DWORD PTR [edx+0x250], ecx` (access form: fixed index / capabilities mask)
  - `0x73dd7e`: `89 8a 50 02 00 00` — `mov DWORD PTR [edx+0x250], ecx` (access form: fixed index / capabilities mask)
  - `0x73ddb4`: `83 8a 50 02 00 00 01` — `or DWORD PTR [edx+0x250], 0x1` (access form: fixed index / capabilities mask)
  - `0x73ddbe`: `8b 82 54 02 00 00` — `mov eax, DWORD PTR [edx+0x254]` (access form: count)
  - `0x73de28`: `89 8a 50 02 00 00` — `mov DWORD PTR [edx+0x250], ecx` (access form: fixed index / capabilities mask)
  - *(Dereference: `0x73dca8`: `8b 0b` `mov ecx, DWORD PTR [ebx]` with increment `0x73ddc9`: `83 c3 04` `add ebx, 0x4`)*
- **Bound constants:**
  - `0x73ddcf`: `83 fe 3c` — `cmp esi, 0x3c` (bound constant `0x3c`)

---

### 2. `007403b0` (TRUE)
Checks capabilities, unit properties, and selection state on `WorldPlayer` (`ebx`/`[ebp-0xc]`).

- **Instructions touching offsets 0x15c..0x254:**
  - `0x7406a1`: `83 bb 54 02 00 00 00` — `cmp DWORD PTR [ebx+0x254], 0x0` (access form: count)
  - `0x7406aa`: `83 bb 50 02 00 00 00` — `cmp DWORD PTR [ebx+0x250], 0x0` (access form: fixed index / capabilities mask)
  - `0x740771`: `8b 9b 50 02 00 00` — `mov ebx, DWORD PTR [ebx+0x250]` (access form: fixed index / capabilities mask)
  - `0x7407f3`: `8b 90 5c 01 00 00` — `mov edx, DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit)
  - `0x740837`: `f6 80 50 02 00 00 01` — `test BYTE PTR [eax+0x250], 0x1` (access form: fixed index / capabilities mask)
  - `0x740863`: `8b 80 50 02 00 00` — `mov eax, DWORD PTR [eax+0x250]` (access form: fixed index / capabilities mask)
  - `0x7408c8`: `c7 83 50 02 00 00 00 00 00 00` — `mov DWORD PTR [ebx+0x250], 0x0` (access form: fixed index / capabilities mask)
  - `0x740c8a`: `8b 83 50 02 00 00` — `mov eax, DWORD PTR [ebx+0x250]` (access form: fixed index / capabilities mask)
  - `0x740d07`: `f6 83 50 02 00 00 20` — `test BYTE PTR [ebx+0x250], 0x20` (access form: fixed index / capabilities mask)
  - `0x740d8d`: `f7 83 50 02 00 00 00 05 00 00` — `test DWORD PTR [ebx+0x250], 0x500` (access form: fixed index / capabilities mask)
  - `0x740dcf`: `f7 83 50 02 00 00 00 01 00 00` — `test DWORD PTR [ebx+0x250], 0x100` (access form: fixed index / capabilities mask)
- **Bound constants:** None.

---

### 3. `0074eec0` (TRUE)
Iterates `selected[60]` to find the minimum speed/formation byte of selected units.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x74eec4`: `8b 91 54 02 00 00` — `mov edx, DWORD PTR [ecx+0x254]` (access form: count)
  - `0x74eed7`: `8d b1 60 01 00 00` — `lea esi, [ecx+0x160]` (access form: base pointer lea)
  - *(Dereference: `0x74eee7`: `8b 0e` `mov ecx, DWORD PTR [esi]` with increment `0x74ef03`: `83 c6 04` `add esi, 0x4`)*
- **Bound constants:**
  - `0x74ef06`: `83 f8 3c` — `cmp eax, 0x3c` (bound constant `0x3c`)

---

### 4. `0074ef20` (TRUE)
Filters units from `selected[60]` that match target formation/type into a global buffer (`DAT_00af4e40`).

- **Instructions touching offsets 0x15c..0x254:**
  - `0x74ef44`: `39 83 54 02 00 00` — `cmp DWORD PTR [ebx+0x254], eax` (access form: count)
  - `0x74ef86`: `8d bb 60 01 00 00` — `lea edi, [ebx+0x160]` (access form: base pointer lea)
  - `0x74ef90`: `3b 93 54 02 00 00` — `cmp edx, DWORD PTR [ebx+0x254]` (access form: count)
  - *(Dereference: `0x74ef9c`: `8b 37` `mov esi, DWORD PTR [edi]`, `0x74f064`: `8b 07` `mov eax, DWORD PTR [edi]` with increment `0x74f07f`: `83 c7 04` `add edi, 0x4`)*
- **Bound constants:**
  - `0x74f085`: `83 f8 3c` — `cmp eax, 0x3c` (bound constant `0x3c`)

---

### 5. `0074f0c0` (TRUE)
Assigns a control group hotkey mask (`1..9`) to all selected units in `selected[60]`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x74f0d5`: `83 be 54 02 00 00 00` — `cmp DWORD PTR [esi+0x254], 0x0` (access form: count)
  - `0x74f12e`: `8d 96 60 01 00 00` — `lea edx, [esi+0x160]` (access form: base pointer lea)
  - `0x74f136`: `3b be 54 02 00 00` — `cmp edi, DWORD PTR [esi+0x254]` (access form: count)
  - *(Dereference: `0x74f13e`: `8b 02` `mov eax, DWORD PTR [edx]` with increment `0x74f157`: `83 c2 04` `add edx, 0x4`)*
- **Bound constants:**
  - `0x74f15a`: `83 f9 3c` — `cmp ecx, 0x3c` (bound constant `0x3c`)

---

### 6. `0074fb20` (TRUE)
Temporarily saves `selected[60]` to stack buffer, resets selection with `0x751480`, re-selects units via `0x754a20`, and restores primary unit at `+0x15c`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x74fd10`: `8b 87 54 02 00 00` — `mov eax, DWORD PTR [edi+0x254]` (access form: count)
  - `0x74fd21`: `8b 8f 5c 01 00 00` — `mov ecx, DWORD PTR [edi+0x15c]` (access form: fixed index / primary selected unit)
  - `0x74fd27`: `8d b7 60 01 00 00` — `lea esi, [edi+0x160]` (access form: base pointer lea)
  - `0x74fd88`: `89 86 5c 01 00 00` — `mov DWORD PTR [esi+0x15c], eax` (access form: fixed index / primary selected unit)
  - *(Array copy: `0x74fd38`: `f3 a5` `rep movs DWORD PTR es:[edi], DWORD PTR ds:[esi]` using count from `eax`)*
- **Bound constants:** None (relies directly on count at `+0x254`).

---

### 7. `00751480` (TRUE)
Selection clearing routine. Resets `+0x15c = 0`, `+0x254 = 0`, and zeros the 60 entries at `+0x160..+0x24c`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x751492`: `8d ba 60 01 00 00` — `lea edi, [edx+0x160]` (access form: base pointer lea)
  - `0x7514c4`: `c7 82 54 02 00 00 00 00 00 00` — `mov DWORD PTR [edx+0x254], 0x0` (access form: count)
  - `0x7514ce`: `c7 82 5c 01 00 00 00 00 00 00` — `mov DWORD PTR [edx+0x15c], 0x0` (access form: fixed index / primary selected unit)
  - *(Zeroing: `0x7514d8`: `f3 ab` `rep stos DWORD PTR es:[edi], eax` with `ecx = 0x3c`)*
- **Bound constants:**
  - `0x751486`: `b9 3c 00 00 00` — `mov ecx, 0x3c` (bound constant `0x3c`)

---

### 8. `00754a20` (TRUE)
`WorldPlayer::select_unit`: Appends unit to `selected[60]`, updates count, sets primary unit, and marks `unit+0x3a`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x754b1d`: `8b 86 54 02 00 00` — `mov eax, DWORD PTR [esi+0x254]` (access form: count)
  - `0x754b2c`: `89 bc 86 60 01 00 00` — `mov DWORD PTR [esi+eax*4+0x160], edi` (access form: scaled index)
  - `0x754b33`: `ff 86 54 02 00 00` — `inc DWORD PTR [esi+0x254]` (access form: count)
  - `0x754b3d`: `89 be 5c 01 00 00` — `mov DWORD PTR [esi+0x15c], edi` (access form: fixed index / primary selected unit)
  - `0x754bb0`: `83 be 54 02 00 00 3c` — `cmp DWORD PTR [esi+0x254], 0x3c` (access form: count)
  - `0x754be3`: `8b 86 54 02 00 00` — `mov eax, DWORD PTR [esi+0x254]` (access form: count)
  - `0x754be9`: `89 9c 86 60 01 00 00` — `mov DWORD PTR [esi+eax*4+0x160], ebx` (access form: scaled index)
  - `0x754bf0`: `ff 86 54 02 00 00` — `inc DWORD PTR [esi+0x254]` (access form: count)
- **Bound constants:**
  - `0x754b23`: `83 f8 3c` — `cmp eax, 0x3c` (bound constant `0x3c`)
  - `0x754bb0`: `83 be 54 02 00 00 3c` — `cmp DWORD PTR [esi+0x254], 0x3c` (bound constant `0x3c`)

---

### 9. `00755370` (TRUE)
Iterates `selected[60]` calling virtual method `+0x304` on each unit.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x755380`: `8d b8 60 01 00 00` — `lea edi, [eax+0x160]` (access form: base pointer lea)
  - `0x755386`: `3b 98 54 02 00 00` — `cmp ebx, DWORD PTR [eax+0x254]` (access form: count)
  - *(Dereference: `0x75538e`: `8b 0f` `mov ecx, DWORD PTR [edi]` with increment `0x7553a3`: `83 c7 04` `add edi, 0x4`)*
- **Bound constants:**
  - `0x7553a6`: `83 fe 3c` — `cmp esi, 0x3c` (bound constant `0x3c`)

---

### 10. `007553c0` (TRUE)
Verifies if all units in `selected[60]` have a uniform formation/stance (`[unit+0xc]+0x97`).

- **Instructions touching offsets 0x15c..0x254:**
  - `0x7553c4`: `8b 81 54 02 00 00` — `mov eax, DWORD PTR [ecx+0x254]` (access form: count)
  - `0x7553d4`: `8d b9 60 01 00 00` — `lea edi, [ecx+0x160]` (access form: base pointer lea)
  - *(Dereference: `0x7553e4`: `8b 0f` `mov ecx, DWORD PTR [edi]` with increment `0x755407`: `83 c7 04` `add edi, 0x4`)*
- **Bound constants:**
  - `0x75540a`: `83 fe 3c` — `cmp esi, 0x3c` (bound constant `0x3c`)

---

### 11. `00755450` (TRUE)
`WorldPlayer::unselect_all`: Clears all units, resets `unit+0x3a`, zeros array and counters.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x755453`: `83 bf 54 02 00 00 00` — `cmp DWORD PTR [edi+0x254], 0x0` (access form: count)
  - `0x75545f`: `c7 87 5c 01 00 00 00 00 00 00` — `mov DWORD PTR [edi+0x15c], 0x0` (access form: fixed index / primary selected unit)
  - `0x75546b`: `8d 87 60 01 00 00` — `lea eax, [edi+0x160]` (access form: base pointer lea)
  - `0x755471`: `3b b7 54 02 00 00` — `cmp esi, DWORD PTR [edi+0x254]` (access form: count)
  - `0x755484`: `c7 00 00 00 00 00` — `mov DWORD PTR [eax], 0x0` (access form: base pointer indirect write / clear slot)
  - `0x755493`: `c7 87 54 02 00 00 00 00 00 00` — `mov DWORD PTR [edi+0x254], 0x0` (access form: count)
  - *(Dereference: `0x755479`: `8b 08` `mov ecx, DWORD PTR [eax]` with increment `0x75548b`: `83 c0 04` `add eax, 0x4`)*
- **Bound constants:**
  - `0x75548e`: `83 fa 3c` — `cmp edx, 0x3c` (bound constant `0x3c`)

---

### 12. `007554a0` (TRUE)
`WorldPlayer::unselect_unit_by_index`: Removes unit at given index, shifts remaining elements down, updates count and primary unit. Note: `0x58 * 4 = 0x160`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x7554ae`: `8b 04 b7` — `mov eax, DWORD PTR [edi+esi*4]` (access form: scaled index, where `esi = edx + 0x58`, accessing `0x160 + edx*4`)
  - `0x7554b1`: `8d 34 b7` — `lea esi, [edi+esi*4]` (access form: scaled index / base pointer lea, `0x160 + edx*4`)
  - `0x7554d4`: `ff 8f 54 02 00 00` — `dec DWORD PTR [edi+0x254]` (access form: count)
  - `0x7554da`: `3b 9f 54 02 00 00` — `cmp ebx, DWORD PTR [edi+0x254]` (access form: count)
  - `0x7554ee`: `3b 9f 54 02 00 00` — `cmp ebx, DWORD PTR [edi+0x254]` (access form: count)
  - `0x7554f9`: `c7 84 9f 60 01 00 00 00 00 00 00` — `mov DWORD PTR [edi+ebx*4+0x160], 0x0` (access form: scaled index / clear vacated end slot)
  - `0x755505`: `39 87 5c 01 00 00` — `cmp DWORD PTR [edi+0x15c], eax` (access form: fixed index / primary selected unit)
  - `0x75550d`: `c7 87 5c 01 00 00 00 00 00 00` — `mov DWORD PTR [edi+0x15c], 0x0` (access form: fixed index / primary selected unit)
  - `0x755534`: `89 87 5c 01 00 00` — `mov DWORD PTR [edi+0x15c], eax` (access form: fixed index / primary selected unit)
- **Bound constants:** None (uses `+0x254` count directly).

---

### 13. `00755540` (TRUE)
`WorldPlayer::unselect_unit_by_pointer`: Scans `selected[60]` for the matching unit pointer and calls `0x7554a0` with its index.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x755547`: `8b bb 54 02 00 00` — `mov edi, DWORD PTR [ebx+0x254]` (access form: count)
  - `0x755554`: `8d b3 60 01 00 00` — `lea esi, [ebx+0x160]` (access form: base pointer lea)
  - *(Dereference: `0x755564`: `8b 0e` `mov ecx, DWORD PTR [esi]` with increment `0x755571`: `83 c6 04` `add esi, 0x4`)*
- **Bound constants:**
  - `0x755574`: `83 f8 3c` — `cmp eax, 0x3c` (bound constant `0x3c`)

---

### 14. `00755850` (TRUE)
Validates all selected units; if a unit is invalid or dead (`unit+0x4c >= 7` or `unit+0x24 != 0`), removes it via `call 0x7554a0`.

- **Instructions touching offsets 0x15c..0x254:**
  - `0x755863`: `3b 83 54 02 00 00` — `cmp eax, DWORD PTR [ebx+0x254]` (access form: count)
  - `0x75586e`: `8b 84 83 60 01 00 00` — `mov eax, DWORD PTR [ebx+eax*4+0x160]` (access form: scaled index)
- **Bound constants:**
  - `0x755891`: `66 83 fe 3c` — `cmp si, 0x3c` (bound constant `0x3c`)

---

## Detailed Analysis of FALSE Functions

### 15. `00721090` (FALSE)
- Dialog initialization for the Mod Manager download window.
- The object in `param_1` is a UI dialog class, not `WorldPlayer`.
- Offsets `+0x248`, `+0x24c`, `+0x250`, `+0x258`, `+0x260` represent child control pointers created via `FUN_0048fe60`, `FUN_0048ef90`, and `FUN_0048df30`.

### 16. `007e48a0` (FALSE)
- Debug rendering pass for pathing / waypoints (`FUN_0083fa00`).
- The appearances of `0x250` and `0x254` are vtable dispatch offsets on unit instances (`call DWORD PTR [eax+0x250]`, `call DWORD PTR [eax+0x254]`), not member variables of `WorldPlayer`.

### 17. `008138a0` (FALSE)
- Unit build / placement command execution.
- Instruction `0x813c81`: `mov BYTE PTR [ecx+0x254], al` is a single byte field on a `UnitType` or master unit structure, not the 4-byte `WorldPlayer` selection count.

### 18. `008178f0` (FALSE)
- UI dialog layout function (message box with up to 3 buttons).
- Allocates button instances via `FUN_0048df30` and stores their pointers in dialog fields at `+0x250`, `+0x254`, `+0x258`.
