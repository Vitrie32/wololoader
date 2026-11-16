# Selection Functions Classification — Part 01

Functions to classify:
- `006663f0`: TRUE
- `00683910`: FALSE
- `00684fd0`: FALSE
- `006889b0`: FALSE
- `0068d100`: FALSE
- `0068ffd0`: FALSE
- `0069cfb0`: FALSE
- `006a8630`: FALSE
- `006adb60`: FALSE
- `006adc10`: FALSE
- `006adce0`: FALSE
- `006addf0`: FALSE
- `006adfe0`: FALSE
- `006ae3e0`: FALSE
- `006aee80`: FALSE
- `006b0980`: FALSE
- `006b96f0`: TRUE
- `006ba7c0`: TRUE

---

## Detailed Classification

### 1. `006663f0` — TRUE
- **Decision:** TRUE
- **Evidence:** Calls `0x66ae40` (`get_current_player`, wrapper around `0x540c90`) to retrieve the `WorldPlayer` object. Calls known selection functions `0x754a20` (`select_unit`), `0x755450` (`unselect_all`), and `0x755540` (`unselect_unit`) on the player pointer. Directly queries `player+0x254` (count), iterates over `player+0x160` (selection array) via scaled index `[eax + esi*1]`, and reads `player+0x15c` (primary selected unit pointer).
- **Instructions touching 0x15c..0x254:**
  - `0x00666908`: `39 b8 54 02 00 00` — `cmp DWORD PTR [eax+0x254],edi` (access form: count)
  - `0x00666910`: `be 60 01 00 00` — `mov esi,0x160` (base offset constant for selection array indexing)
  - `0x00666922`: `8b 04 30` — `mov eax,DWORD PTR [eax+esi*1]` (access form: scaled index / array element access `selected[i]`)
  - `0x00666945`: `3b b8 54 02 00 00` — `cmp edi,DWORD PTR [eax+0x254]` (access form: count)
  - `0x00666954`: `83 b8 5c 01 00 00 00` — `cmp DWORD PTR [eax+0x15c],0x0` (access form: fixed index / primary selected unit pointer)
  - `0x0066696a`: `8b 80 5c 01 00 00` — `mov eax,DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x0066697b`: `8b 80 5c 01 00 00` — `mov eax,DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
- **Bound constants for that array:** None (dynamically bounded by count in `[eax+0x254]`).

---

### 2. `00683910` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog/panel constructor setting vtable `0x9d2de4`. Offsets `0x24c..0x253` are single-byte fields of a character buffer zero-initialized on the dialog object (`param_1[0x24c] = '\0'`). No `WorldPlayer` or selection logic.

---

### 3. `00684fd0` — FALSE
- **Decision:** FALSE
- **Evidence:** "Send Quick Message Dialog" constructor (calls `0x47a3e0("Send Quick Message Dialog", ...)` and sets vtable `0x9d2ed8`). Offsets `0x248`, `0x24c`, `0x250`, `0x254` store dialog child controls (edit control, buttons).

---

### 4. `006889b0` — FALSE
- **Decision:** FALSE
- **Evidence:** Audio / sound effect emitter constructor (calls `0x729360`, sets vtable `0x9d329c`). Offsets in range `0x200..0x244` are sound properties (volume, 3D coordinates, floats).

---

### 5. `0068d100` — FALSE
- **Decision:** FALSE
- **Evidence:** Audio playback / sound effect configuration method for object with vtable `0x9d329c`. Offsets `0x204`, `0x210..0x244` store audio floats/constants (`+0x228 = 1.0f`, `+0x230 = 99.99f`).

---

### 6. `0068ffd0` — FALSE
- **Decision:** FALSE
- **Evidence:** Sound emitter update function for the same audio system class. Offsets `0x214..0x244` are float playback parameters.

---

### 7. `0069cfb0` — FALSE
- **Decision:** FALSE
- **Evidence:** "Main Menu" UI dialog constructor (calls `0x47a3e0`, sets vtable `0x9d3964`). Offsets `0x248` etc. are dialog button/control pointers.

---

### 8. `006a8630` — FALSE
- **Decision:** FALSE
- **Evidence:** In-game HUD / resources dialog constructor (sets vtable `0x9d434c`). Queries player via `0x540c90` only for economic resource quantities (`player+0x3c` food/wood/stone/gold/pop). Does not read or write player selection offsets `0x15c..0x254`. Offsets in `0x15c..0x254` belong to the dialog's member variables or local stack frame.

---

### 9. `006adb60` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog constructor for class with vtable `0x9d48e0` (calls `0x47a3e0`). Zeroes child control pointers at `+0x248`, `+0x24c`, `+0x250`, `+0x254`, `+0x258`, `+0x260`.

---

### 10. `006adc10` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog destructor for class `0x9d48e0`. Deletes child control instances at `+0x248`, `+0x24c`, `+0x254`, `+0x258`.

---

### 11. `006adce0` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog notification/action handler for class `0x9d48e0`. Compares sender with child control pointers `+0x24c` (list box), `+0x254` (OK button), and `+0x258` (Cancel button).

---

### 12. `006addf0` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog initialization/control builder for class `0x9d48e0`. Instantiates dialog controls and stores pointers into `+0x248` (label), `+0x24c` (dropdown), `+0x250`, `+0x254` (OK button, string `0xfa1`), and `+0x258` (Cancel button, string `0xfa2`).

---

### 13. `006adfe0` — FALSE
- **Decision:** FALSE
- **Evidence:** "Scenario Menu Dialog" constructor (calls `0x47a3e0("Scenario Menu Dialog", ...)` and sets vtable `0x9d49dc`). Offsets `0x248..0x268` are dialog button pointers.

---

### 14. `006ae3e0` — FALSE
- **Decision:** FALSE
- **Evidence:** "Scenario Menu Dialog" event handler. Compares clicked control with `[this+0x250]`, `[this+0x254]`, `[this+0x25c]`, `[this+0x260]`, `[this+0x264]`, `[this+0x268]`.

---

### 15. `006aee80` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog constructor (calls `0x47a3e0`, sets vtable `0x9d4b20`). Zeroes member child controls in range `+0x248..+0x2cc`.

---

### 16. `006b0980` — FALSE
- **Decision:** FALSE
- **Evidence:** UI dialog destructor for class `0x9d4b20`. Releases dialog resources.

---

### 17. `006b96f0` — TRUE
- **Decision:** TRUE
- **Evidence:** Calls `0x540c90` (`get_current_player`) to obtain `WorldPlayer`. Checks count at `player+0x254`, takes `lea ebx, [eax+0x160]` (base of selection array), and iterates unit pointers up to `0x3c` (60) entries, testing unit flags (`unit->vtable->0x168`).
- **Instructions touching 0x15c..0x254:**
  - `0x006b9709`: `8d 98 60 01 00 00` — `lea ebx,[eax+0x160]` (access form: base pointer lea (`&player->selected[0]`, offset +0x160))
  - `0x006b9710`: `3b b0 54 02 00 00` — `cmp esi,DWORD PTR [eax+0x254]` (access form: count (`player->count`, offset +0x254))
  - `0x006b9718`: `8b 0b` — `mov ecx,DWORD PTR [ebx]` (access form: array pointer dereference (`selected[i]`), traversing array via `ebx` initialized to `+0x160` and incremented by 4 at `0x6b973d`)
- **Bound constants for that array:**
  - `0x006b9740`: `83 ff 3c` — `cmp edi,0x3c` (bound constant: `0x3c` = 60 decimal, maximum selection capacity / end bound loop limit).

---

### 18. `006ba7c0` — TRUE
- **Decision:** TRUE
- **Evidence:** Calls `0x540c90` (`get_current_player`) to obtain `WorldPlayer`. Reads primary selected unit pointer at `player+0x15c` (calls `unit->vtable->0xbc` and `player->vtable->0xf4(unit+0x84)`). Checks count at `player+0x254`. Takes `lea ebx, [eax+0x160]` (base of selection array) and loops over each selected unit, calling `unit->vtable->0x300` and `0x2fc`. Finally calls `0x74f0c0` on the player pointer.
- **Instructions touching 0x15c..0x254:**
  - `0x006ba7e6`: `83 b8 5c 01 00 00 00` — `cmp DWORD PTR [eax+0x15c],0x0` (access form: fixed index / primary selected unit pointer)
  - `0x006ba7fa`: `8b 88 5c 01 00 00` — `mov ecx,DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x006ba813`: `8b 80 5c 01 00 00` — `mov eax,DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x006ba83c`: `8b 88 5c 01 00 00` — `mov ecx,DWORD PTR [eax+0x15c]` (access form: fixed index / primary selected unit pointer)
  - `0x006ba867`: `39 b8 54 02 00 00` — `cmp DWORD PTR [eax+0x254],edi` (access form: count)
  - `0x006ba870`: `8d 98 60 01 00 00` — `lea ebx,[eax+0x160]` (access form: base pointer lea (`&player->selected[0]`, offset +0x160))
  - `0x006ba876`: `8b 33` — `mov esi,DWORD PTR [ebx]` (access form: array pointer dereference (`selected[i]`), traversing array via `ebx` initialized to `+0x160` and incremented by 4 at `0x6ba88e`)
  - `0x006ba891`: `3b b8 54 02 00 00` — `cmp edi,DWORD PTR [eax+0x254]` (access form: count)
- **Bound constants for that array:** None (dynamically bounded by count in `[eax+0x254]`).
