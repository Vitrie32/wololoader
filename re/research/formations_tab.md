# Formations Tab / Page in Bottom-Left Command Panel

Game: Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit, base `0x400000`).
All disassembly quoted from `~/.cache/wl-test/exe.s` (objdump AT&T syntax, `AoK HD.exe` 5.8 base `0x400000`).
Section VA ranges:
- `.text`: `0x401000..0x9abfac` (File Offset = VA - `0x400c00`)
- `.rdata`: `0x9ac000..0xaaa8e6` (File Offset = VA - `0x400c00`)
- `.data`: `0xaab000..0xde3044` (File Offset = VA - `0x401200`)

Marks: **[V]** = verified in disassembly / binary, **[U]** = unverified / inferred.

---

## Executive Summary

1. **Command Panel Architecture**:
   The bottom-left command panel is managed by `GameScreen` (`[0xaf3d90]`, vtable `0x9d5300`). It holds an array of 29 button pointers at `screen + 0xb14 + slot*4`.
   Slots `0..14` (15 buttons) form a **3-row × 5-column grid** (width 40px, height 40px, column pitch 41px, row pitch 41px, origin x=37).
   Each button is a `TRIBE_Panel_Button` (size `0xb68`, vtable `0x9d9278`, ctor `0x711e50`).
   The function that populates command buttons for the current selection is `GameScreen::update_command_buttons` (`0x006c9dc0`, `__thiscall`, `ret 0`).

2. **Page Mechanism**:
   The active page is stored in `*(int *)(GameScreen + 0x1288)`.
   - `0`: Inactive / no selection
   - `1`: Default main page / military commands (`0x6c9dc0`)
   - `3`: Villager Economic Buildings page (`0x6ba900`)
   - `4`: Villager Military Buildings page (`0x6bac30`)
   - `5`: **Formations page** (`0x6bcd60`) — **already built into the game engine from legacy AoK!**
   - `10` (`0x0a`): Secondary unit commands page (`0x6bb100`)
   - `17` (`0x11`): Gate rotation page (`0x6be230`)
   Page-switch buttons:
   - Villager Page 1 -> Page 3: Button ID `0x74` (116).
   - Villager Page 1 -> Page 4: Button ID `0x73` (115).
   - Between Page 3 & 4: Button ID `0x0c` (12) calling `0x6bda60`.
   - Back button: Button ID `0x89` (137) calling `0x6bc610`. The game's native Back handler already contains:
     `if (page == 3 || page == 4 || page == 5 || page == 0x11) { page = 1; FUN_007199f0(1); FUN_006c9dc0(); return; }`
     Notice that `page == 5` is natively wired to return to Page 1!

3. **Click Dispatcher & Button IDs**:
   Button clicks on `screen->buttons[0..28]` dispatch through `GameScreen::action` (vtable slot 49 / +0xc4 at `0x006b8f60`) -> `0x006bfc40` via jump table `0x6c0aa8` using `0x6c0c24[button_id - 1]`.
   - **Formations**: Line (`0x55` -> param 2), Box (`0x54` -> param 4), Staggered (`0x56` -> param 7), Flank (`0x57` -> param 8).
     Handler: `0x006bcc60(screen, formation_param)` sends network packet `0x17` (`COMMAND_FORMATION`) via `0x00809340` and updates checked visual state on slots 10..13 (`screen + 0xb3c..0xb48`).
   - **Stances**: Aggressive (`0x21` -> 0), Defensive (`0x22` -> 1), Stand Ground (`0x16` -> 2), No Attack (`0x23` -> 3).
     Handler: `0x006bf010(screen, stance_idx)` sends network packet `0x12` (`COMMAND_STANCE`) via `0x00809d50` and updates checked visual state on slots 5..8 (`screen + 0xb28..0xb34`).
   - **Formations Page Button**: Button ID `0x0a` (10 decimal) is **already mapped** in `0x6bfc40` at `0x6c01f0` to call `0x006bcd60`!

4. **Smallest Safe Hook Points**:
   Only **two small 5-byte hooks** are required:
   - **(a) Military Page Button**: Hook call-site `0x006cb940` (5 bytes: `call 0x6bf840`) in `0x006c9dc0` via `wl_patch_call`, or detour `0x006c9dc0` (prologue `55 8b ec 6a ff`). If `screen->page == 1` and military units are selected, add Button ID `0x0a` (frame `0x2c`, help string `0x132e`) at Slot 10 (or Slot 5), hide redundant slots (`button->0x720 = 0`), and finalize.
   - **(b) Formations & Stances Page**: Detour `0x006bcd60` (prologue `55 8b ec 6a ff`, `__thiscall`, `ret 0`). In our detour: set `page = 5`, clear 15 slots with `0x6c6040`, populate Stances at slots 5..8 (IDs `0x21, 0x22, 0x16, 0x23`), Formations at slots 10..13 (IDs `0x55, 0x54, 0x56, 0x57`), and Back button at slot 14 (ID `0x89`).
   - **Zero patches** needed for click handling or packet sending: the game handles all IDs natively!

5. **Multiplayer Safety**:
   Switching command pages is **100% local UI**. No network packets (`0x80ef70`) are sent on page switches; network packets are only sent when clicking an actual formation or stance action.

---

## 1. Command Panel Architecture & Button Storage

### 1.1 Panel Class Hierarchy & Button Array [V]

The command panel buttons are not a separate child panel container; they are individual button instances owned directly by `GameScreen` (singleton pointer at `0xaf3d90`).
- **Button Class**: `TRIBE_Panel_Button` (RTTI descriptor `0xa23334`, vtable `0x9d9278`, ctor `0x711e50`, size `0xb68` bytes).
- **Inheritance**: `TRIBE_Panel_Button` -> `ButtonPanel` -> `Panel` (vtable `0x9d5440`) -> `HandleObject`.

During screen initialization in `GameScreen::setup_layout` (`0x006c4470`), 29 buttons are allocated with `operator new(0xb68)` (`0x8d47df`) and stored into an array at `GameScreen + 0xb14 + slot*4` (`0x006b4688..0x006b4794`):

```assembly
; exe.s: 0x006b4688 - allocating 29 buttons (0x1d)
  6b4688: lea    0xb14(%edi),%esi          ; esi = &screen->buttons[0]
  6b468e: movl   $0x0,-0x6c(%ebp)          ; slot_index = 0
  6b4695: push   $0xb68                    ; sizeof(TRIBE_Panel_Button)
  6b469a: call   0x8d47df                  ; operator new
  ...
  6b46ea: call   0x711e50                  ; TRIBE_Panel_Button::ctor
  6b46fa: mov    %eax,(%esi)               ; screen->buttons[slot] = button
  ...
  6b478a: add    $0x4,%esi                 ; next slot
  6b478d: inc    %eax
  6b478e: mov    %eax,-0x6c(%ebp)
  6b4791: cmp    $0x1d,%eax                ; 29 buttons total
  6b4794: jl     0x6b4695
```

### 1.2 Grid Geometry: 3 Rows × 5 Columns (15 Slots) [V]

At `0x006c4a2a..0x006c4a82`, the first 15 buttons (`slots 0..14`) are positioned in a 3×5 grid at the bottom-left of the screen:

```assembly
; exe.s: 0x006c4a2a - layout 3 rows x 5 columns
  6c4a2a: lea    0xb14(%esi),%ebx          ; ebx = &screen->buttons[0]
  6c4a30: movl   $0x3,-0x40(%ebp)          ; row_counter = 3
  6c4a37: lea    0x8(%edi),%esi            ; esi = start_y
  6c4a40: mov    $0x25,%edi                ; edi = 0x25 (37 px) = start_x

; Column inner loop (5 columns)
  6c4a45: mov    (%ebx),%ecx               ; ecx = button pointer
  6c4a47: lea    -0x38(%ebp),%edx          ; edx = &rect {x, y, w, h}
  6c4a4a: mov    %edi,-0x38(%ebp)          ; rect.x = current_x
  6c4a4d: mov    %esi,-0x34(%ebp)          ; rect.y = current_y
  6c4a50: movl   $0x28,-0x30(%ebp)         ; rect.w = 0x28 (40 px)
  6c4a57: movl   $0x28,-0x2c(%ebp)         ; rect.h = 0x28 (40 px)
  6c4a5e: mov    (%ecx),%eax
  6c4a60: push   %edx
  6c4a61: call   *0x8(%eax)                ; Panel::set_rect(&rect)
  6c4a64: mov    (%ebx),%ecx
  6c4a66: push   $0x24
  6c4a68: call   0x712a10
  6c4a6d: add    $0x29,%edi                ; x += 0x29 (41 px col pitch)
  6c4a70: add    $0x4,%ebx                 ; next button slot pointer
  6c4a73: cmp    $0xf2,%edi                ; 37 + 5*41 = 242 (0xf2) -> 5 columns!
  6c4a79: jl     0x6c4a45
  6c4a7b: add    $0x29,%esi                ; y += 0x29 (41 px row pitch)
  6c4a7e: subl   $0x1,-0x40(%ebp)          ; row_counter--
  6c4a82: jne    0x6c4a40                  ; loop 3 rows
```

Grid Slot Mapping:
- **Row 0**: Slot 0 (`0xb14`), Slot 1 (`0xb18`), Slot 2 (`0xb1c`), Slot 3 (`0xb20`), Slot 4 (`0xb24`)
- **Row 1**: Slot 5 (`0xb28`), Slot 6 (`0xb2c`), Slot 7 (`0xb30`), Slot 8 (`0xb34`), Slot 9 (`0xb38`)
- **Row 2**: Slot 10 (`0xb3c`), Slot 11 (`0xb40`), Slot 12 (`0xb44`), Slot 13 (`0xb48`), Slot 14 (`0xb4c`)
- **Slots 15..28**: Top-bar menu, diplomacy, chat, flare buttons (`0xb50..0xb84`).

### 1.3 Button Internal Storage & Fields (`TRIBE_Panel_Button`) [V]

Each `TRIBE_Panel_Button` instance contains:

| Offset | Type | Meaning | Verified Code Reference |
|---|---|---|---|
| `+0x000` | `void**` | Vtable (`0x9d9278`) | Ctor `0x711e50` |
| `+0x130` | `int` | Primary mode / command category (0 for normal) | `0x6c925e` |
| `+0x134` | `int` | Secondary mode / sub-parameter | `0x6c9268` |
| `+0x138` | `int` | Active flag (set to 1 when configured) | `0x6c92b6` |
| `+0x140` | `void*` | SLP shape file pointer (`screen->0xab4` `btncmd.shp` or civ building SLP) | `0x6c9470`, `0x6c9200` |
| `+0x144` | `short` | SLP frame index (icon graphic) | `0x6c92d7` |
| `+0x1a8` | `int` | **Button ID** (action ID, e.g. `0x21`, `0x55`, `0x89`, `0x0a`) in state 0 | `0x6c92c0` (`state * 0x70 + 0x1a8`) |
| `+0x1ac` | `int` | **Action Param 2** (secondary parameter, e.g. tech/building ID) | `0x6c92c9` |
| `+0x54a` | `short` | Current button state index (multiplied by `0x70` for state structs) | `0x6c92a6` |
| `+0x55c` | `int` | Hotkey virtual keycode (e.g. `VK_A`) | `0x6c9470` / `0x6c14c0` |
| `+0x560` | `int` | Hotkey modifiers (Shift, Ctrl, Alt) | `0x6c9470` / `0x6c14c0` |
| `+0x720` | `int` | **Visibility / Active flag** (1 = visible & enabled, 0 = hidden) | `0x6c9254`, `0x6bf875` |
| `+0x724` | `int` | Disabled flag (1 = disabled/grayed out, 0 = enabled) | `0x6c9272` |
| `+0xb40` | `int` | **Hotkey action ID** | `0x6c9498` |

Vtable Methods on `TRIBE_Panel_Button`:
- `vtable[4]` (`+0x10`): `SetVisible(int visible)` (`0x484df0`)
- `vtable[7]` (`+0x1c`): `SetEnabled(int enabled)` (`0x479890`)
- `vtable[52]` (`+0xd0`): `SetChecked(int checked)` (`0x712a10`) — sets active highlight border.

### 1.4 Slot Configuration Helper: `GameScreen::setup_command_button` (`0x006c9200`) [V]

Entry and calling convention:
```
Address: 0x006c9200
Convention: __thiscall (ecx = GameScreen* this)
Pops: ret $0x40 (64 bytes stack cleanup)
Raw prologue: 55 8b ec 6a ff 68 18 be 95 00 (push %ebp; mov %esp,%ebp; push $0xffffffff; push $0x95be18)
```

Stack arguments pushed before call:
1. `void *shape_file`: `screen->0xab4` (`btncmd.shp` SLP 0xc621) or building SLP.
2. `short slot_index`: `0..14`.
3. `short icon_frame`: icon frame index in SLP.
4. `int button_id`: action ID (e.g. `0x21` Aggressive, `0x55` Line, `0x0a` Formation page).
5. `int action_param2`: secondary parameter (0 for stances/formations).
6. Tooltip descriptor (0x2c bytes): built on stack or via helper `0x005d7f80(str_id)`.

Helper `FUN_005d7f80` (`0x005d7f80`, `__thiscall`, `ecx = &tooltip_struct`, stack arg: `string_id`, `ret 4`) initializes the tooltip struct with string ID and standard empty string pointer `0xad0380`.

---

## 2. How Pages Work (Villager Build Pages & Legacy Page 5)

### 2.1 The Current Page Field [V]

Stored at `*(int *)(GameScreen + 0x1288)`.

Page ID Table:
- `0`: Inactive / selection cleared (`0x6bdc80`)
- `1`: Selection Page 1 (Military unit commands, villager root page) (`0x6c9dc0`)
- `3`: Villager Economic Buildings (`0x6ba900`)
- `4`: Villager Military Buildings (`0x6bac30`)
- `5`: **Formations Page** (`0x6bcd60`) — legacy AoK/AoC formations page!
- `10` (`0x0a`): Unit Secondary Commands (`0x6bb100`)
- `17` (`0x11`): Gate Rotation (`0x6be230`)

### 2.2 Page Switch Buttons [V]

1. **Villager Page 1 -> Building Pages**:
   - Button ID `0x74` (116) at Slot 0: Economic Buildings -> sets page 3, calls `0x6ba900` (`0x6ca47e`).
   - Button ID `0x73` (115) at Slot 1: Military Buildings -> sets page 4, calls `0x6bac30` (`0x6ca4d7`).
2. **Toggle Between Economic and Military (Pages 3 & 4)**:
   - Button ID `0x0c` (12) at Slot 13 (icon frame `0x20` = 32, help string `0x1330` = 4912 "Military/Economic Buildings").
   - Click handler in `0x6bfc40` jumps to `0x6bfd16` -> calls `GameScreen::toggle_build_page` (`0x006bda60`):
     ```c
     // decompiled.c: 0x006bda60
     if (screen->current_page == 3) {
         screen->current_page = 4;
         // clear buttons, populate military buildings...
         return;
     }
     if (screen->current_page == 4) {
         screen->current_page = 3;
         // clear buttons, populate economic buildings...
         return;
     }
     ```
3. **The Back Button (ID `0x89` = 137)**:
   - Placed at Slot 14 (icon frame 0, help string `0x132f` = 4911 "Cancel").
   - Click handler in `0x6bfc40` jumps to `0x6c027c` -> calls `GameScreen::handle_back_button` (`0x006bc610`).
   - Disassembly of `0x006bc610` [V]:
     ```assembly
     ; exe.s: 0x006bc63a
       6bc63a: mov    0x1288(%esi),%eax         ; eax = screen->current_page
       6bc640: cmp    $0x3,%eax                 ; Page 3 (Econ)?
       6bc643: je     0x6bc65b
       6bc645: cmp    $0x4,%eax                 ; Page 4 (Mil)?
       6bc648: je     0x6bc65b
       6bc64a: cmp    $0x5,%eax                 ; Page 5 (Formations)?
       6bc64d: je     0x6bc65b
       6bc64f: cmp    $0x11,%eax                ; Page 17 (Gate)?
       6bc652: jne    0x6bc673
       6bc654: movl   $0x1,0x1288(%esi)         ; screen->current_page = 1
       6bc65e: mov    %esi,%ecx
       6bc660: call   0x6c9dc0                  ; update_command_buttons()!
       6bc665: ret
     ```
   **Key Finding**: The engine ALREADY checks `cmp $0x5, %eax`! When `current_page == 5`, clicking Back automatically resets `current_page = 1` and rebuilds the main military page via `0x006c9dc0`.

### 2.3 Page Dispatcher: `GameScreen::update_command_page` (`0x006c6490`) [V]

When the game needs to refresh the active page (e.g. after selection update or building completion), it calls `0x006c6490`:

```assembly
; exe.s: 0x006c6490
  6c64b5: mov    0x1288(%param_1),%eax     ; page
  6c64bb: cmp    $0x11,%eax                ; page <= 17
  6c64be: ja     0x6c64ef                  ; default -> update_command_buttons (page 1)
  6c64c0: movzbl 0x6c6538(%eax),%eax       ; lookup switch index
  6c64c8: jmp    *0x6c651c(,%eax,4)        ; jump table
```

Switch mapping table `0x6c6538` [V]:
- `page == 0` -> case 0 (`0x6c64cb`): `0x6c6040` (clear) + `0x6bf840` (finalize)
- `page == 3` -> case 1 (`0x6c64db`): `0x6ba900` (Econ buildings page)
- `page == 4` -> case 2 (`0x6c64e0`): `0x6bac30` (Mil buildings page)
- `page == 5` -> case 3 (`0x6c64e5`): `0x6bcd60` (**Formations page!**)
- `page == 10` -> case 4 (`0x6c64ea`): `0x6bb100` (Secondary unit commands)
- `page == 17` -> case 5 (`0x6c64ef`): `0x6be230` (Gate rotation)
- all other (including `page == 1`): calls `0x6c9dc0` (Main selection / military page)

---

## 3. Button Click Dispatcher, Formations, and Stances

### 3.1 Click Dispatcher Architecture [V]

When a user clicks on the command grid:
1. `GameScreen::action` (`vtable[49]` / `+0xc4` at `0x006b8f60`) handles mouse events.
2. If the clicked control is one of `screen->buttons[0..28]`, it jumps to `0x006b94a6` and invokes `GameScreen::handle_command_button` (`0x006bfc40`):
   ```
   Convention: __thiscall (ecx = GameScreen* this)
   Arguments: (int button_id, int action_param2, int is_right_click)
   ```
3. Inside `0x006bfc40`, `button_id` is dispatched using:
   - Byte index table: `0x006c0c24[button_id - 1]`
   - Jump table: `0x006c0aa8[index * 4]`

### 3.2 Stance Buttons: IDs, Slots, and Packets [V]

Default placement on Page 1 (configured at `0x006cadcb..0x006caf3c`):

| Stance | Button ID | Slot | Frame | Active Frame | Help String ID | Text |
|---|---|---|---|---|---|---|
| **Aggressive** | `0x21` (33) | Slot 5 (`0xb28`) | 9 (`0x09`) | `0x35` (53) | `0x1345` (4933) | "Aggressive Stance" |
| **Defensive** | `0x22` (34) | Slot 6 (`0xb2c`) | 10 (`0x0a`) | `0x34` (52) | `0x1346` (4934) | "Defensive Stance" |
| **Stand Ground** | `0x16` (22) | Slot 7 (`0xb30`) | 11 (`0x0b`) | `0x33` (51) | `0x133a` (4922) | "Stand Ground" |
| **No Attack** | `0x23` (35) | Slot 8 (`0xb34`) | 50 (`0x32`) | `0x36` (54) | `0x1347` (4935) | "No Attack Stance" |

Dispatcher code in `0x006bfc40` [V]:
```assembly
; exe.s: 0x006bff2e - stances dispatch
  6bff2e: push   $0x0                      ; stance_idx = 0 (Aggressive)
  6bff30: mov    %edi,%ecx                 ; ecx = GameScreen*
  6bff32: call   0x6bf010                  ; GameScreen::set_stance
  6bff37: jmp    0x6c0a8a

  6bff3c: push   $0x1                      ; stance_idx = 1 (Defensive)
  6bff3e: mov    %edi,%ecx
  6bff40: call   0x6bf010
  6bff45: jmp    0x6c0a8a

  6bff4a: push   $0x2                      ; stance_idx = 2 (Stand Ground)
  6bff4c: mov    %edi,%ecx
  6bff4e: call   0x6bf010
  6bff53: jmp    0x6c0a8a

  6bff58: push   $0x3                      ; stance_idx = 3 (No Attack)
  6bff5a: mov    %edi,%ecx
  6bff5c: call   0x6bf010
  6bff61: jmp    0x6c0a8a
```

Stance Handler `GameScreen::set_stance` (`0x006bf010`, `__thiscall`, `ret 4`) [V]:
1. Calls `Player->vtable[0xd4]` (`0x0074df60`) with `stance_idx`.
   - `0x0074df60` calls `FUN_00809d50(unit_ids, unit_count, stance_idx)` which constructs **Packet `0x12` (`COMMAND_STANCE`)** and transmits it over comm via `FUN_0080ef70`.
2. Updates UI button checked borders:
   - Calls `button->vtable[0xd0](0)` (uncheck) on all 4 stance buttons at `0xb28`, `0xb2c`, `0xb30`, `0xb34` (slots 5..8).
   - Calls `button->vtable[0xd0](1)` (check) on the matching button `screen + 0xb28 + stance_idx*4`.

### 3.3 Formation Buttons: IDs, Slots, and Packets [V]

Default placement on Page 1 (configured at `0x006cb5e9..0x006cb709`):

| Formation | Button ID | Slot | Frame | Param | Help String ID | Text |
|---|---|---|---|---|---|---|
| **Line** | `0x55` (85) | Slot 10 (`0xb3c`) | 40 (`0x28`) | 2 | `0xa03d` (41021) | "Line Formation" |
| **Box** | `0x54` (84) | Slot 11 (`0xb40`) | 43 (`0x2b`) | 4 | `0xa03c` (41020) | "Box Formation" |
| **Staggered** | `0x56` (86) | Slot 12 (`0xb44`) | 63 (`0x3f`) | 7 | `0xa03e` (41022) | "Staggered Formation" |
| **Flank** | `0x57` (87) | Slot 13 (`0xb48`) | 62 (`0x3e`) | 8 | `0xa03f` (41023) | "Flank Formation" |

Dispatcher code in `0x006bfc40` [V]:
```assembly
; exe.s: 0x006c0098 - formations dispatch
  6c0098: push   $0x0                      ; button 0x53 (param 0 / default)
  6c009a: mov    %edi,%ecx
  6c009c: call   0x6bcc60                  ; GameScreen::set_formation
  6c00a1: jmp    0x6c0a8a

  6c00a6: push   $0x4                      ; button 0x54 (param 4 = Box)
  6c00a8: mov    %edi,%ecx
  6c00aa: call   0x6bcc60
  6c00af: jmp    0x6c0a8a

  6c00b4: push   $0x2                      ; button 0x55 (param 2 = Line)
  6c00b6: mov    %edi,%ecx
  6c00b8: call   0x6bcc60
  6c00bd: jmp    0x6c0a8a

  6c00c2: push   $0x7                      ; button 0x56 (param 7 = Staggered)
  6c00c4: mov    %edi,%ecx
  6c00c6: call   0x6bcc60
  6c00cb: jmp    0x6c0a8a

  6c00d0: push   $0x8                      ; button 0x57 (param 8 = Flank)
  6c00d2: mov    %edi,%ecx
  6c00d4: call   0x6bcc60
  6c00d9: jmp    0x6c0a8a
```

Formation Handler `GameScreen::set_formation` (`0x006bcc60`, `__thiscall`, `ret 4`) [V]:
1. Calls `Player->vtable[0xf0]` (`0x0074d7b0`) with `formation_param`.
   - `0x0074d7b0` calls `FUN_00809340(unit_ids, unit_count, player_idx, formation_param)` which constructs **Packet `0x17` (`COMMAND_FORMATION`)** and transmits it over comm via `FUN_0080ef70`.
2. Updates UI button checked borders:
   - Calls `button->vtable[0xd0](0)` (uncheck) on all 4 formation buttons at `0xb3c`, `0xb40`, `0xb44`, `0xb48` (slots 10..13).
   - Calls `button->vtable[0xd0](1)` (check) on the matching button at `0xb3c` (Line), `0xb40` (Box), `0xb44` (Staggered), or `0xb48` (Flank).

### 3.4 The Built-in Formations Page Button ID (`0x0a` = 10) [V]

In `0x006bfc40`, button ID `0x0a` (10) is already mapped to target `0x006c01f0`:
```assembly
; exe.s: 0x006c01f0
  6c01f0: mov    %edi,%ecx                 ; ecx = GameScreen*
  6c01f2: call   0x6bcd60                  ; calls show_formations_page!
  6c01f7: jmp    0x6c0a8a
```

Matching resources [V]:
- Help string ID: `0x132e` (4910):
  `"<b>Select Formation<b> \nChoose the type of formation for these units. Units in formation take positions based on their combat role and stay together as they move around the map."`
- Icon frames: 44 (`0x2c`, formation icon) or 40 (`0x28`, line formation icon).

---

## 4. Smallest Safe Hook Points for the Formations Tab

To achieve:
1. On the main military page (Page 1), show a single "Formations" button instead of crowding the panel.
2. Clicking it opens a dedicated sub-page showing both Formations and Stances, plus a Back button.
3. Clicking Back returns to Page 1.
4. Formation and stance clicks send the native commands and update UI checkmarks natively.

### 4.1 Hook Point A: Adding the "Formations" Button on Page 1

#### Target Function: `GameScreen::update_command_buttons` (`0x006c9dc0`) [V]
- **Address**: `0x006c9dc0`
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push %ebp; mov %esp,%ebp; push $0xffffffff`)
- **Calling Convention**: `__thiscall` (`ecx = GameScreen* this`)
- **Return Cleanup**: `ret 0` (`6cb960: ret`)

#### Recommended Smallest Hook: Call-Site Patch at `0x006cb940` [V]
Right at the end of `update_command_buttons`, right before `ret`, is a direct 5-byte CALL to `0x006bf840` (`finalize_button_visibility`):
```assembly
; exe.s: 0x006cb93e
  6cb93e: mov    %ebx,%ecx                 ; ebx = GameScreen*
  6cb940: call   0x6bf840                  ; e8 fb 3e ff ff (5 bytes)
  6cb945: ...                              ; epilogue -> 6cb960: ret
```

Patch with `wl_patch_call(0x006cb940, our_finalize_page1, 5)`.

In `our_finalize_page1`:
```c
// Replacement calling convention: __fastcall (self in ecx, edx unused)
void __fastcall our_finalize_page1(void *self, void *edx) {
    int *screen = (int *)self;
    int page = *(int *)((char *)self + 0x1288);

    // If on Page 1 and military units are selected (e.g. slot 10 has a formation button)
    void **buttons = (void **)((char *)self + 0xb14);
    int *btn10 = (int *)buttons[10];
    if (page == 1 && btn10 && *(int *)((char *)btn10 + 0x720) != 0) {
        // 1. Configure Slot 10 as "Formations" tab button
        // Slot 10: ID 0x0a, icon frame 0x2c (44), string 0x132e (4910)
        wl_setup_button(self, 10, 0x2c, 0x0a, 0x132e);

        // 2. Hide redundant slots 11, 12, 13 (and optionally 5..8 if moving stances)
        for (int s = 11; s <= 13; s++) {
            int *btn = (int *)buttons[s];
            if (btn) *(int *)((char *)btn + 0x720) = 0; // hide
        }
    }

    // Call original finalize_button_visibility(self)
    typedef void (__thiscall *fn_finalize)(void *);
    ((fn_finalize)0x006bf840)(self);
}
```

### 4.2 Hook Point B: The Formations & Stances Page Builder

#### Target Function: `GameScreen::show_formations_page` (`0x006bcd60`) [V]
- **Address**: `0x006bcd60`
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push %ebp; mov %esp,%ebp; push $0xffffffff`)
- **Calling Convention**: `__thiscall` (`ecx = GameScreen* this`)
- **Return Cleanup**: `ret 0` (`6bcfcc: ret`)

Patch with `wl_hook(0x006bcd60, our_show_formations_page, 5)`:

```c
// Replacement calling convention: __fastcall (self in ecx, edx unused)
void __fastcall our_show_formations_page(void *self, void *edx) {
    // 1. Set current page to 5
    *(int *)((char *)self + 0x1288) = 5;

    // 2. Clear all 15 command button slots
    typedef void (__thiscall *fn_clear)(void *);
    ((fn_clear)0x006c6040)(self);

    // 3. Populate Stances in Row 1 (Slots 5..8)
    // Slot 5: Aggressive (ID 0x21, frame 9, string 0x1345)
    wl_setup_button(self, 5, 9, 0x21, 0x1345);
    // Slot 6: Defensive (ID 0x22, frame 10, string 0x1346)
    wl_setup_button(self, 6, 10, 0x22, 0x1346);
    // Slot 7: Stand Ground (ID 0x16, frame 11, string 0x133a)
    wl_setup_button(self, 7, 11, 0x16, 0x133a);
    // Slot 8: No Attack (ID 0x23, frame 50, string 0x1347)
    wl_setup_button(self, 8, 50, 0x23, 0x1347);

    // 4. Populate Formations in Row 2 (Slots 10..13)
    // Slot 10: Line (ID 0x55, frame 40 / 0x28, string 0xa03d)
    wl_setup_button(self, 10, 0x28, 0x55, 0xa03d);
    // Slot 11: Box (ID 0x54, frame 43 / 0x2b, string 0xa03c)
    wl_setup_button(self, 11, 0x2b, 0x54, 0xa03c);
    // Slot 12: Staggered (ID 0x56, frame 63 / 0x3f, string 0xa03e)
    wl_setup_button(self, 12, 0x3f, 0x56, 0xa03e);
    // Slot 13: Flank (ID 0x57, frame 62 / 0x3e, string 0xa03f)
    wl_setup_button(self, 13, 0x3e, 0x57, 0xa03f);

    // 5. Populate Back / Cancel Button at Slot 14
    // Slot 14: Back (ID 0x89, frame 0, string 0x132f)
    wl_setup_button(self, 14, 0, 0x89, 0x132f);

    // 6. Finalize button visibility
    typedef void (__thiscall *fn_finalize)(void *);
    ((fn_finalize)0x006bf840)(self);
}
```

### 4.3 Why This Architecture Requires No Other Hooks

1. **Click Handling for Formations & Stances**:
   - The button IDs placed on Page 5 are the exact native game IDs (`0x21, 0x22, 0x16, 0x23` for stances, `0x55, 0x54, 0x56, 0x57` for formations).
   - Because they are placed in slots `5..8` and `10..13`, their native handlers `0x006bf010` and `0x006bcc60` will automatically check/uncheck the exact slot pointers (`0xb28..0xb34` and `0xb3c..0xb48`).
2. **Back Button Handling**:
   - Back button uses ID `0x89`. Its native handler `0x006bc610` checks:
     `if (page == 3 || page == 4 || page == 5 || page == 0x11) { page = 1; update_command_buttons(); }`
     Since our page is `5`, the game engine already natively transitions back to Page 1!
3. **Formations Button Click Handling**:
   - The Formations button uses ID `0x0a`. In the dispatcher `0x006bfc40`, ID `0x0a` already jumps to `0x006c01f0` and calls `0x006bcd60`. Our detour on `0x006bcd60` receives the click immediately.
4. **Engine Page Refresh**:
   - If the game refreshes the command grid via `0x006c6490` while `page == 5`, jump table `0x6c651c` case 3 calls `0x006bcd60`, which executes our detour and refreshes Page 5 cleanly.

---

## 5. Local UI Confirmation (No Network / Desync Risk)

### 5.1 Verification of Page Transition Code [V]

The following functions were audited for any network packet generation:
- `GameScreen::handle_back_button` (`0x006bc610`):
  Only writes `*(int *)(screen + 0x1288) = 1`, calls `0x7199f0(1)`, and calls `0x6c9dc0(screen)`. Zero network calls.
- `GameScreen::show_formations_page` (`0x006bcd60`):
  Only writes `*(int *)(screen + 0x1288) = 5`, calls `0x6c6040`, calls `0x6c9200`, and calls `0x6bf840`. Zero network calls.
- `GameScreen::update_command_page` (`0x006c6490`):
  Only inspects `screen + 0x1288` and dispatches to local page update routines. Zero network calls.
- `GameScreen::toggle_build_page` (`0x006bda60`):
  Only toggles `screen + 0x1288` between 3 and 4 and redraws buttons. Zero network calls.

### 5.2 Verification of Action Packets [V]

Actual game commands are only generated when an action button is pressed:
- Stance selection: `0x0074df60` -> `0x00809d50` -> Packet `0x12` (`COMMAND_STANCE`) -> `0x0080ef70`
- Formation selection: `0x0074d7b0` -> `0x00809340` -> Packet `0x17` (`COMMAND_FORMATION`) -> `0x0080ef70`

**Conclusion**: Switching command pages is 100% client-side UI state. It involves zero comm packets, has zero desync risk, and is completely multiplayer-safe.

---

## 6. Summary of Addresses and Offsets Reference

| Symbol | Address / Offset | Convention / Signature | Purpose |
|---|---|---|---|
| `GameScreen` singleton | `[0x00af3d90]` | `GameScreen*` | Active in-game screen object |
| `screen->buttons` | `screen + 0xb14` | `TRIBE_Panel_Button*[29]` | Array of 29 UI buttons |
| `screen->current_page` | `screen + 0x1288` | `int` | Active command panel page |
| `screen->cmd_shape` | `screen + 0xab4` | `void*` | `btncmd.shp` SLP pointer (SLP 0xc621) |
| `button->visible` | `btn + 0x720` | `int` | 1 = visible/active, 0 = hidden |
| `button->disabled` | `btn + 0x724` | `int` | 1 = disabled, 0 = enabled |
| `button->btn_id` | `btn + 0x1a8` | `int` | Action button ID in state 0 |
| `button->SetChecked` | `vtable[52]` (`+0xd0`) | `__thiscall(void*, int)` | Toggle checked outline highlight |
| `0x006c9dc0` | `0x006c9dc0` | `__thiscall(GameScreen*)`, ret 0 | `update_command_buttons` (Page 1) |
| `0x006cb940` | `0x006cb940` | 5-byte CALL `0x6bf840` | Call-site hook for Page 1 button injection |
| `0x006bcd60` | `0x006bcd60` | `__thiscall(GameScreen*)`, ret 0 | `show_formations_page` (Page 5) |
| `0x006bc610` | `0x006bc610` | `__thiscall(GameScreen*)`, ret 0 | `handle_back_button` (Button `0x89`) |
| `0x006c6490` | `0x006c6490` | `__thiscall(GameScreen*)`, ret 0 | `update_command_page` (page dispatcher) |
| `0x006bfc40` | `0x006bfc40` | `__thiscall(GameScreen*, id, p2, rclick)`, ret 0xc | `handle_command_button` (click dispatcher) |
| `0x006c6040` | `0x006c6040` | `__thiscall(GameScreen*)`, ret 0 | Clear/reset all 15 command slots |
| `0x006bf840` | `0x006bf840` | `__thiscall(GameScreen*)`, ret 0 | Finalize 15 command slots visibility |
| `0x006c9200` | `0x006c9200` | `__thiscall(GameScreen*)`, ret $0x40 | `setup_command_button` |
| `0x005d7f80` | `0x005d7f80` | `__thiscall(struct*, str_id)`, ret 4 | Build tooltip descriptor |
