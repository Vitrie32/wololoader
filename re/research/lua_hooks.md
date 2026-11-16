# Wololoader — Reverse-Engineering Research: Lua Event Hooks

> **Target:** `AoK HD.exe` (AoE2 HD 5.8, ImageBase `0x400000`, 32-bit PE).  
> **Status:** All core hook points and addresses listed below have been verified via static disassembly (`objdump -d`), Ghidra pseudo-C (`re/out/decompiled.c`), MSVC RTTI vtable analysis (`re/scripts/rtti.py`), and string cross-references.  
> **Classification:** Clearly divided into **[VERIFIED]** (verified bytes, prologues, ret N, and calling conventions) and **[UNVERIFIED]** (heuristic or pending runtime testing).

---

## Quick Reference Table

| Target | Address | Calling Conv | Ret N | Stolen Bytes (5+) | Purpose / Status |
|---|---|---|---|---|---|
| **GAME START** | `0x0062e790` | `__thiscall(TRIBE_Game*)` | `ret 0` | `55 8b ec 6a ff` | Runs once when loading completes & `GameScreen` is ready **[VERIFIED]** |
| **GAME START (Alt: World)** | `0x0072f7d0` | `__thiscall(BaseWorld*)` | `ret 0x14` | `55 8b ec 6a ff` | World simulation state set to 3 (playing) **[VERIFIED]** |
| **GAME END (Player Status)** | `0x00748a40` | `__thiscall(WorldPlayer*, int)` | `ret 4` | `55 8b ec 6a ff` | Victory (`status=2`) / Defeat (`status=1`) handler **[VERIFIED]** |
| **GAME END (Engine Check)** | `0x0072be70` | `__thiscall(BaseWorld*)` | `ret 0` | `55 8b ec 83 ec` | Victory condition check, sets `world+0x78 = 1` **[VERIFIED]** |
| **GAME END (Screen Dtor)** | `0x006b7fc0` | `__thiscall(GameScreen*)` | `ret 0` | `55 8b ec 6a ff` | `GameScreen` destroyed / returning to menu **[VERIFIED]** |
| **IN-GAME MESSAGE (Helper)** | `0x006cbed0` | `__thiscall(GameScreen*, ...)` | `ret 0x10` | `55 8b ec 8b 89` | Yellow notification / chat line display **[VERIFIED]** |
| **IN-GAME MESSAGE (Core)** | `0x00664840` | `__thiscall(ChatManager*, ...)`| `ret 0x20` | `55 8b ec 6a ff` | Low-level message dispatch with duration & sound **[VERIFIED]** |
| **IN-GAME DIALOG (YesNo)** | `0x00490da0` | `__thiscall(ScreenPanel*, ...)` | `ret 8` | `55 8b ec 6a ff` | Modal Yes/No dialog on `GameScreen` **[VERIFIED]** |
| **IN-GAME DIALOG (OK)** | `0x00490a60` | `__thiscall(ScreenPanel*, ...)` | `ret 0x30` | `55 8b ec 6a ff` | Modal OK-only dialog on `GameScreen` **[VERIFIED]** |
| **CLOSE DIALOG** | `0x00485e00` | `__thiscall(ScreenRegistry*, ...)`| `ret 4` | `55 8b ec ...` | Destroys dialog by name in registry **[VERIFIED]** |
| **PER-FRAME (In-Game UI)** | `0x006c3310` | `__thiscall(GameScreen*)` | `ret 0` | `55 8b ec 6a ff` | `GameScreen::tick` (vtable slot 17) **[VERIFIED]** |
| **PER-TICK (World Sim)** | `0x00738720` | `__thiscall(BaseWorld*, int)` | `ret 4` | `55 8b ec 6a ff` | `BaseWorld::update` (vtable slot 10) **[VERIFIED]** |
| **PER-FRAME (Engine Loop)** | `0x0063a040` | `__thiscall(TRIBE_Game*)` | `ret 0` | `55 8b ec 6a ff` | `TRIBE_Game::update` (vtable slot 65) **[VERIFIED]** |

---

## 1. GAME START & GAME END

### 1.1 Game Start Hook: `TRIBE_Game::start_game_screen` (`0x0062e790`) **[VERIFIED]**

When any match begins (Single Player Random Map, Campaign, Multiplayer, or Scenario), the game finishes generation and loading, instantiates `GameScreen` (size `0x1390`), registers it into the screen manager, initializes game timers, and starts play.

- **Address:** `0x0062e790`
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  - Full first instructions:
    ```assembly
    0062e790: 55                push   %ebp
    0062e791: 8b ec             mov    %esp,%ebp
    0062e793: 6a ff             push   $0xffffffff
    0062e795: 68 8b 7a 94 00    push   $0x947a8b
    ```
- **Calling Convention:** `__thiscall(TRIBE_Game* this)` (or `__fastcall(void* self, void* edx)` in C)
- **Stack Cleanup:** `ret` (`ret 0`, 0 stack parameters, callee cleanup)
  ```assembly
  0062e9ec: 8b e5             mov    %esp,%ebp
  0062e9ee: 5d                pop    %ebp
  0062e9ef: c3                ret
  ```
- **Callers in Executable:**
  - `0x00631060`: Single player / RM game setup launch.
  - `0x00635520`: Saved game / scenario launch.
  - `0x00637920`: Multiplayer match launch.
- **Why this hook point is optimal for Lua:**
  Inside `0x0062e790`:
  1. `operator_new(0x1390)` allocates the `GameScreen` object.
  2. `GameScreen::GameScreen(0x006b35b0)` constructs HUD, minimap, control panels, and assigns `param_1[0x351] = world`.
  3. `DAT_00af3d90 = GameScreen*` is populated.
  4. `0x0048a830` activates `GameScreen` as the current foreground screen.
  5. `0x0072cda0` initializes match audio / music streams.
  
  **Hooking Pattern:**
  Detour `0x0062e790` with `wl_hook(0x0062e790, hook_start_game, 5)`. In the hook handler, call the original trampoline first. Once the original returns, **every world pointer, player object, and UI HUD panel is guaranteed to be fully initialized and ready**. The Lua `on_game_start()` event can immediately query players, inspect resources, print in-game messages, and show welcome dialogs without null pointer dereferences.

```c
// Trampoline typedef
typedef void (__fastcall *start_game_fn)(void* self, void* edx);
static start_game_fn g_orig_start_game = NULL;

void __fastcall hook_start_game(void* self, void* edx) {
    g_orig_start_game(self, edx); // Let game finish loading
    
    // GAME START EVENT FIRES HERE
    // *(void**)0xaf3d90 -> Active GameScreen*
    // *(void**)0xaf2c58 -> Active TRIBE_Game*
    // local player = ((void* (__cdecl*)())0x540c90)();
}
```

---

### 1.2 Game Start (Engine Simulation Level): `BaseWorld::start` (`0x0072f7d0`) **[VERIFIED]**

- **Address:** `0x0072f7d0` (`BaseWorld` vtable slot 5 at `0x009db024 + 0x14`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  0072f7d0: 55                push   %ebp
  0072f7d1: 8b ec             mov    %esp,%ebp
  0072f7d3: 6a ff             push   $0xffffffff
  0072f7d5: 68 f5 97 96 00    push   $0x9697f5
  ```
- **Calling Convention:** `__thiscall(BaseWorld* this, int p1, int p2, int p3, int p4)`
- **Stack Cleanup:** `ret $0x14` (20 bytes callee cleanup)
- **Role:** Transitions `world->game_state` (`world + 0x78`) from 2 (loading) to 3 (playing).
  *Note:* Runs before `GameScreen` is instantiated. If Lua needs to display UI elements at start, use `0x0062e790` instead.

---

### 1.3 Game End: Match Victory & Defeat (`0x00748a40`) **[VERIFIED]**

When any player resigns, is defeated, or achieves victory:

- **Address:** `0x00748a40` (`WorldPlayer::set_status`, slot 1 in `WorldPlayer` vtable `0x009dbc7c`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  00748a40: 55                push   %ebp
  00748a41: 8b ec             mov    %esp,%ebp
  00748a43: 6a ff             push   $0xffffffff
  00748a45: 68 e1 bf 96 00    push   $0x96bfe1
  ```
- **Calling Convention:** `__thiscall(WorldPlayer* this, int status)`
- **Stack Cleanup:** `ret $0x4` (4 bytes callee cleanup)
  ```assembly
  00748d10: 5d                pop    %ebp
  00748d11: c2 04 00          ret    $0x4
  ```
- **Arguments:**
  - `ecx`: `WorldPlayer*` pointer (the player whose status has changed).
  - `status` (`0x8(%ebp)`):
    - `0`: Active / Alive
    - `1`: Resigned / Defeated
    - `2`: Victorious / Won
- **Internal Disassembly Evidence:**
  ```assembly
  00748a71: 8a 5f 35          mov    0x35(%edi),%bl       ; previous status byte
  00748a74: ff 75 08          push   0x8(%ebp)            ; new status
  00748a7a: e8 f1 c4 00 00    call   0x754f70             ; sets player+0x35
  ...
  00748aba: 8a 47 35          mov    0x35(%edi),%al
  00748abd: 3a c3             cmp    %bl,%al              ; status changed?
  00748abf: 74 4f             je     0x748b10
  00748ac1: 3c 02             cmp    $0x2,%al             ; Victory check
  00748ac3: 74 04             je     0x748ac9
  00748ac5: 3c 01             cmp    $0x1,%al             ; Defeat check
  ```
- **Distinguishing Local vs Remote Player:**
  To detect if the local human player won or lost:
  `void* local_player = ((void* (__cdecl*)())0x540c90)();`  
  If `ecx == local_player`:
  - `status == 2`: Local player won! -> Lua event `on_victory()`
  - `status == 1`: Local player lost / resigned! -> Lua event `on_defeat()`

---

### 1.4 Game End: Engine Victory Condition Evaluator (`0x0072be70`) **[VERIFIED]**

- **Address:** `0x0072be70` (`BaseWorld::check_victory`, slot 13 in `BaseWorld` vtable `0x009db024`)
- **Prologue Bytes:** `55 8b ec 83 ec 10` (6 bytes)
- **Calling Convention:** `__thiscall(BaseWorld* this)`
- **Stack Cleanup:** `ret` (`ret 0`)
- **Disassembly Evidence:**
  Iterates all players in `world->player_array` (`world + 0x184`), queries player status via `0x0074d350`. If victory criteria are met:
  ```assembly
  0072bf8e: c7 43 78 01 00 00 00  movl   $0x1,0x78(%ebx)    ; world->game_over = 1
  0072bfb9: e8 c2 45 02 00        call   0x750580           ; marks remaining player victorious
  00739870: 68 38 b7 9d 00        push   $0x9db738          ; "GAME OVER!\n"
  00739876: e8 d5 dd d9 ff        call   0x4d7650           ; fprintf to AI log
  ```

---

### 1.5 Game End: Exit to Menu / `GameScreen` Destruction (`0x006b7fc0`) **[VERIFIED]**

When leaving a match to return to the Main Menu or transitioning to the post-game `AftermathScreen`:

- **Address:** `0x006b7fc0` (`GameScreen::~GameScreen`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  006b7fc0: 55                push   %ebp
  006b7fc1: 8b ec             mov    %esp,%ebp
  006b7fc3: 6a ff             push   $0xffffffff
  006b7fc5: 68 27 ac 95 00    push   $0x95ac27
  ```
- **Calling Convention:** `__thiscall(GameScreen* this)`
- **Stack Cleanup:** `ret` (`ret 0`)
  ```assembly
  006b83c0: 8b e5             mov    %esp,%ebp
  006b83c2: 5d                pop    %ebp
  006b83c3: c3                ret
  ```
- **Disassembly Evidence:**
  Destroys HUD child controls and resets global screen pointer:
  ```assembly
  006b801a: c7 05 90 3d af 00 00 00 00 00  movl $0x0, 0xaf3d90 ; DAT_00af3d90 = NULL
  ```
- **Virtual Slot:** Slot 0 of `GameScreen` vtable (`0x009d5300`) points to the scalar deleting destructor `0x006b88c0`, which calls `0x006b7fc0` and then frees `0x1390` bytes.

---

## 2. IN-GAME MESSAGE (Chat & Yellow Notification Text)

### 2.1 Convenient In-Game Notification: `GameScreen::display_message` (`0x006cbed0`) **[VERIFIED]**

This is the standard game notification function used by AoE2 HD to display yellow event text ("X has advanced to the Castle Age", tribute messages, wonder warnings, etc.).

- **Address:** `0x006cbed0`
- **Prologue Bytes:** `55 8b ec 8b 89 28 0c 00 00` (First 5 bytes: `55 8b ec 8b 89`)
- **Calling Convention:** `__thiscall(GameScreen* this, int type, const char* text, int color, int flag)`
- **Stack Cleanup:** `ret $0x10` (16 bytes = 4 parameters, callee cleanup)
- **Complete Disassembly:**
  ```assembly
  006cbed0 <.text+0x2caed0>:
    6cbed0: 55                   push   %ebp
    6cbed1: 8b ec                mov    %esp,%ebp
    6cbed3: 8b 89 28 0c 00 00    mov    0xc28(%ecx),%ecx  ; ecx = GameScreen->0xc28 (ChatManager*)
    6cbed9: 6a ff                push   $0xffffffff       ; sound_id = -1 (no sound)
    6cbedb: 6a 00                push   $0x0              ; p8 = NULL
    6cbedd: 6a ff                push   $0xffffffff       ; duration_ms = -1 (use default ~5s)
    6cbedf: 6a 00                push   $0x0              ; callback = NULL
    6cbee1: ff 75 14             push   0x14(%ebp)        ; flag (player ID or 0)
    6cbee4: ff 75 10             push   0x10(%ebp)        ; color style (0x24 = default yellow)
    6cbee7: ff 75 0c             push   0xc(%ebp)         ; text (const char* UTF-8)
    6cbeea: ff 75 08             push   0x8(%ebp)         ; type (1 = notification)
    6cbeed: e8 4e 89 f9 ff       call   0x664840          ; MessageManager::add_message
    6cbef2: 5d                   pop    %ebp
    6cbef3: c2 10 00             ret    $0x10
  ```
- **Arguments:**
  - `ecx`: `GameScreen*` pointer (read from `*(void**)0x00af3d90`).
  - `type` (`0x8(%ebp)`):
    - `1`: In-game notification banner (yellow text displayed at top of screen).
    - `0`: In-game chat message.
    - `3`: System / objective broadcast.
  - `text` (`0xc(%ebp)`): `const char*` pointer to null-terminated UTF-8 string.
  - `color` (`0x10(%ebp)`):
    - `0x24` (`36` decimal): Standard yellow notification text (stored at `DAT_00ab4cc3`).
    - `0xf3`: White text.
    - `0xff`: System red / alert text.
  - `flag` (`0x14(%ebp)`): Player ID index or secondary attribute (default `0`).

**C Invocation Example:**
```c
void wl_send_game_message(const char* utf8_text) {
    void* game_screen = *(void**)0x00af3d90;
    if (!game_screen) return; // Not currently in a match
    
    typedef void (__thiscall *display_msg_fn)(void* screen, int type, const char* text, int color, int flag);
    display_msg_fn display_msg = (display_msg_fn)0x006cbed0;
    display_msg(game_screen, 1, utf8_text, 0x24, 0);
}
```

---

### 2.2 Low-Level Message Dispatch: `MessageManager::add_message` (`0x00664840`) **[VERIFIED]**

For scripts that require custom durations (e.g. 10 seconds) or triggering an audio sound effect with the message:

- **Address:** `0x00664840`
- **Prologue Bytes:** `55 8b ec 6a ff 68 e8 09 95 00` (First 5: `55 8b ec 6a ff`)
- **Calling Convention:** `__thiscall(void* message_mgr, int type, const char* text, char color, char flag, void* callback, int duration_ms, void* p8, int sound_id)`
- **Stack Cleanup:** `ret $0x20` (32 bytes = 8 stack arguments, callee cleanup)
  ```assembly
  00664c82: 8b e5             mov    %esp,%ebp
  00664c84: 5d                pop    %ebp
  00664c85: c2 20 00          ret    $0x20
  ```
- **Arguments:**
  - `ecx`: `ChatManager*` (`*(void**)(game_screen + 0xc28)`).
  - `type`: `1` (notification), `0` (chat).
  - `text`: `const char*`.
  - `color`: `0x24` (yellow), `0xff` (red).
  - `flag`: `0`.
  - `callback`: `NULL` (`0`).
  - `duration_ms`: Duration in milliseconds (e.g. `10000` for 10s, `-1` for default).
  - `p8`: `NULL` (`0`).
  - `sound_id`: Sound effect index (e.g. `0x15` for alert horn, `-1` for no sound).

---

## 3. IN-GAME DIALOG (Yes/No & OK Message Dialogs)

### 3.1 Can Dialogs Be Shown on `GameScreen`? **[VERIFIED: YES]**

- `GameScreen` derives directly from `ScreenPanel` (size `0x1390`, vtable `0x009d5300`, 59 slots).
- Like all screens in the game, `GameScreen` registers in `ScreenRegistry` (`[0x00ada3f0]`) under the name `"Game Screen"`.
- It maintains the standard parent reference pointers (`screen + 0x1c4`, `screen + 0x1d0`, `screen + 0x200`) required by the dialog creation helpers.
- **Obtaining the Game Screen Pointer:**
  - Fast global: `DAT_00af3d90` (`*(void**)0x00af3d90`) — non-null if and only if in an active game match.
  - Registry lookup: `0x00485dd0(*(void**)0x00ada3f0, "Game Screen")`.

---

### 3.2 Yes/No Dialog Helper: `0x00490da0` **[VERIFIED]**

- **Address:** `0x00490da0`
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  00490da0: 55                push   %ebp
  00490da1: 8b ec             mov    %esp,%ebp
  00490da3: 6a ff             push   $0xffffffff
  00490da5: 68 12 31 91 00    push   $0x913112
  ```
- **Calling Convention:** `__thiscall(ScreenPanel* parent, const char* text, const char* name)`
- **Stack Cleanup:** `ret $0x8` (8 bytes callee cleanup)
  ```assembly
  00490e5a: 8b e5             mov    %esp,%ebp
  00490e5c: 5d                pop    %ebp
  00490e5d: c2 08 00          ret    $0x8
  ```
- **Return Value:** `eax` = pointer to the created `MessageDialog` (`0x368` bytes, vtable `0x009e403c`).
- **Arguments:**
  - `ecx`: Parent screen (`GameScreen*` at `*(void**)0x00af3d90`).
  - `text` (`0x8(%ebp)`): Null-terminated message string.
  - `name` (`0xc(%ebp)`): Screen registry name. If `NULL` or `""`, defaults to `"YesNoDialog"` (`0x009b2854`).
- **Internal Behavior:** Allocates `0x368` bytes with `operator new(0x8d47df)`, calls `MessageDialog::MessageDialog(0x008175b0)`, and invokes dialog vtable slot 60 with `mode = 2` (Yes/No buttons).

---

### 3.3 OK-Only Dialog Helper: `0x00490a60` **[VERIFIED]**

- **Address:** `0x00490a60`
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  00490a60: 55                push   %ebp
  00490a61: 8b ec             mov    %esp,%ebp
  00490a63: 6a ff             push   $0xffffffff
  00490a65: 68 72 30 91 00    push   $0x913072
  ```
- **Calling Convention:** `__thiscall(ScreenPanel* parent, const char* text, const char* name, ...)`
- **Stack Cleanup:** `ret $0x30`
- **Return Value:** `eax` = pointer to created `MessageDialog`.
- **Default Name:** `"OkDialog"` (`0x009b2848`).
- **Internal Behavior:** Invokes dialog vtable slot 60 with `mode = 0` (single OK button layout).

---

### 3.4 Handling Response & Closing Dialogs **[VERIFIED]**

1. **Button Clicks:**
   When a user clicks a dialog button, the dialog does NOT automatically close itself. Instead, it dispatches an action event to the parent screen's slot 49 (`+0xc4`):
   - `GameScreen` slot 49 is `0x006b8f60` (`__thiscall(GameScreen* this, Panel* sender, int code, int p3, int p4)`).
   - Event codes:
     - `code == 0`: Yes / OK clicked.
     - `code == 1`: No / Cancel clicked.
2. **Closing the Dialog:**
   The parent screen (or mod hook) must close the dialog by deleting it from `ScreenRegistry` (`0x00ada3f0`):
   ```c
   // Close dialog by name:
   typedef void (__thiscall *delete_screen_fn)(void* registry, const char* name);
   delete_screen_fn delete_screen = (delete_screen_fn)0x00485e00;
   delete_screen(*(void**)0x00ada3f0, "YesNoDialog");
   ```

---

## 4. PER-FRAME & PER-SECOND GAME LOOP HOOKS

To drive periodic Lua events (`on_frame`, `every_second`), several verified hook points are available:

### 4.1 In-Game Render/UI Frame: `GameScreen::tick` (`0x006c3310`) **[VERIFIED]**

- **Address:** `0x006c3310` (`GameScreen` vtable slot 17 at `0x009d5300 + 0x44`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  006c3310: 55                push   %ebp
  006c3311: 8b ec             mov    %esp,%ebp
  006c3313: 6a ff             push   $0xffffffff
  006c3315: 68 b6 b9 95 00    push   $0x95b9b6
  ```
- **Calling Convention:** `__thiscall(GameScreen* this)`
- **Stack Cleanup:** `ret` (`ret 0`, 0 stack arguments)
  ```assembly
  006c3564: 8b e5             mov    %esp,%ebp
  006c3566: 5d                pop    %ebp
  006c3567: c3                ret
  ```
- **Execution Rate:** Runs every frame while in a match (`GameScreen` is the active screen). Does NOT run in main menus.
- **Suitability for `every_second`:** Excellent for **wall-clock seconds**. Uses `timeGetTime()` or `GetTickCount()` to measure elapsed real-world time.

---

### 4.2 Game Simulation Tick: `BaseWorld::update` (`0x00738720`) **[VERIFIED]**

- **Address:** `0x00738720` (`BaseWorld` vtable slot 10 at `0x009db024 + 0x28`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  00738720: 55                push   %ebp
  00738721: 8b ec             mov    %esp,%ebp
  00738723: 6a ff             push   $0xffffffff
  00738725: 68 b7 ad 96 00    push   $0x96adb7
  ```
- **Calling Convention:** `__thiscall(BaseWorld* this, int delta_time)`
- **Stack Cleanup:** `ret $0x4` (4 bytes callee cleanup)
  ```assembly
  0073889e: 8b e5             mov    %esp,%ebp
  007388a0: 5d                pop    %ebp
  007388a1: c2 04 00          ret    $0x4
  ```
- **Execution Rate:** Ticks with the engine simulation step.
- **Evidence string:** `0x73964c` logs `"baseworld update"`.
- **Suitability for `every_second`:** Excellent for **game simulation seconds**.
  - Simulation time is directly available: `*(uint32_t*)(this + 0x74)` (world time in ms).
  - Automatically pauses when the player pauses the game.
  - Automatically speeds up at 1.5x or 2.0x game speed.

---

### 4.3 Engine Main Loop Logic Tick: `TRIBE_Game::update` (`0x0063a040`) **[VERIFIED]**

- **Address:** `0x0063a040` (`TRIBE_Game` vtable slot 65 at `0x009cee9c + 0x104`)
- **Prologue Bytes:** `55 8b ec 6a ff` (5 bytes)
  ```assembly
  0063a040: 55                push   %ebp
  0063a041: 8b ec             mov    %esp,%ebp
  0063a043: 6a ff             push   $0xffffffff
  0063a045: 68 80 8e 94 00    push   $0x948e80
  ```
- **Calling Convention:** `__thiscall(TRIBE_Game* this)`
- **Stack Cleanup:** `ret` (`ret 0`)
  ```assembly
  0063a461: 5d                pop    %ebp
  0063a462: c3                ret
  ```
- **Execution Rate:** Called directly from the WinMain message loop (`0x006359b0`) every frame across the entire game (both menus and in-game).
- **In-game Check:** Tests `if (*(void**)0x00af3d90 != NULL)` to know if currently in-game.

---

## 5. Implementation Recipe for Wololoader

### 5.1 Game Lifecycle State Machine
```c
// wololoader/src/lua_events.c skeleton

static void* g_game_screen = NULL;
static uint32_t g_last_sim_second = 0;

// 1. GAME START DETOUR
static void (__fastcall *orig_start_game)(void* self, void* edx) = NULL;
void __fastcall hook_start_game(void* self, void* edx) {
    orig_start_game(self, edx);
    g_game_screen = *(void**)0x00af3d90;
    g_last_sim_second = 0;
    
    // Call Lua: on_game_start()
}

// 2. SIMULATION UPDATE / EVERY SECOND DETOUR
static void (__fastcall *orig_world_update)(void* self, void* edx, int dt) = NULL;
void __fastcall hook_world_update(void* self, void* edx, int dt) {
    orig_world_update(self, edx, dt);
    
    uint32_t sim_ms = *(uint32_t*)((char*)self + 0x74);
    uint32_t sim_sec = sim_ms / 1000;
    if (sim_sec > g_last_sim_second) {
        g_last_sim_second = sim_sec;
        // Call Lua: on_every_second(sim_sec)
    }
}

// 3. PLAYER VICTORY / DEFEAT DETOUR
static void (__fastcall *orig_player_set_status)(void* self, void* edx, int status) = NULL;
void __fastcall hook_player_set_status(void* self, void* edx, int status) {
    orig_player_set_status(self, edx, status);
    
    void* local_player = ((void* (__cdecl*)())0x00540c90)();
    if (self == local_player) {
        if (status == 2) {
            // Call Lua: on_victory()
        } else if (status == 1) {
            // Call Lua: on_defeat()
        }
    }
}

// 4. GAME END / RETURN TO MENU DETOUR
static void (__fastcall *orig_gamescreen_dtor)(void* self, void* edx) = NULL;
void __fastcall hook_gamescreen_dtor(void* self, void* edx) {
    // Call Lua: on_game_end()
    g_game_screen = NULL;
    orig_gamescreen_dtor(self, edx);
}

void wl_install_lua_hooks(void) {
    wl_hook(0x0062e790, hook_start_game, 5);         // Game Start
    wl_hook(0x00738720, hook_world_update, 5);       // World tick / every second
    wl_hook(0x00748a40, hook_player_set_status, 5);  // Victory / Defeat
    wl_hook(0x006b7fc0, hook_gamescreen_dtor, 5);    // Game Screen Exit
}
```

---

## 6. Verification Status Summary

- `0x0062e790` (Game Start): **VERIFIED**. Full disassembly, prologue `55 8b ec 6a ff`, callers verified.
- `0x00748a40` (Player Victory/Defeat): **VERIFIED**. `WorldPlayer` slot 1, prologue `55 8b ec 6a ff`, status comparison logic verified.
- `0x006b7fc0` (Game Screen Destruction): **VERIFIED**. `GameScreen::~GameScreen`, prologue `55 8b ec 6a ff`, resets `DAT_00af3d90`.
- `0x006cbed0` (In-Game Message): **VERIFIED**. `GameScreen::display_message`, prologue `55 8b ec 8b 89`, calls `0x664840`, ret 0x10.
- `0x00490da0` & `0x00490a60` (In-Game Dialogs): **VERIFIED**. YesNo and OK-only helpers, `MessageDialog` construction verified.
- `0x00485e00` (Close Dialog): **VERIFIED**. Screen registry destructor by name.
- `0x006c3310` (In-Game Frame Tick): **VERIFIED**. `GameScreen` slot 17, prologue `55 8b ec 6a ff`.
- `0x00738720` (World Sim Tick): **VERIFIED**. `BaseWorld` slot 10, prologue `55 8b ec 6a ff`.
- `0x0063a040` (Engine Frame Tick): **VERIFIED**. `TRIBE_Game` slot 65, prologue `55 8b ec 6a ff`.
- Global Pointers: `DAT_00af3d90` (`GameScreen*`), `DAT_00af2c58` (`TRIBE_Game*`), `0x00ada3f0` (`ScreenRegistry`), `0x00540c90` (`get_current_player`): **VERIFIED**.
