# Wololoader Reverse Engineering Research: In-Game Chat & Multiplayer Detection

> **Target Game**: Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit PE, ImageBase `0x00400000`).  
> **Environment**: Linux / Steam / Proton / Wine.  
> **Documentation Purpose**: Verified addresses, calling conventions, prologues, quoted disassemblies, call sites, and C integration code for:  
> 1. In-game CHAT display (local display, player names, colors, and system messages).  
> 2. Runtime MULTIPLAYER detection (exact expressions, game options, and human player counting).

---

## 1. IN-GAME CHAT DISPLAY

### 1.1 Root Cause: Why `GameScreen::display_message` (`0x006cbed0`) with `type = 0` Shows Nothing

In `re/research/lua_hooks.md`, section 2.1 documented:
```c
GameScreen::display_message(0x006cbed0) __thiscall(GameScreen* this, int type, const char* text, int color, int flag)
```
Disassembly of `0x006cbed0`:
```assembly
006cbed0: 55                   push   %ebp
006cbed1: 8b ec                mov    %esp,%ebp
006cbed3: 8b 89 28 0c 00 00    mov    0xc28(%ecx),%ecx  ; ecx = GameScreen->mNotificationPanel
006cbed9: 6a ff                push   $0xffffffff
006cbedb: 6a 00                push   $0x0
006cbedd: 6a ff                push   $0xffffffff
006cbedf: 6a 00                push   $0x0
006cbee1: ff 75 14             push   0x14(%ebp)        ; flag
006cbee4: ff 75 10             push   0x10(%ebp)        ; color
006cbee7: ff 75 0c             push   0xc(%ebp)         ; text
006cbeea: ff 75 08             push   0x8(%ebp)         ; type
006cbeed: e8 4e 89 f9 ff       call   0x00664840        ; TMessagePanel::add_message
006cbef2: 5d                   pop    %ebp
006cbef3: c2 10 00             ret    $0x10
```
- Offset `+0xc28` of `GameScreen` (`DAT_00af3d90`) is **NOT** the chat manager. It is a single dedicated `TMessagePanel` object assigned solely to the **in-game notification banner** (tribute announcements, age advancements, wonder timers) that only renders `type = 1`.
- When called with `type = 0`, `TMessagePanel::add_message` (`0x00664840`) evaluates the active type flags on that specific panel, finds `type = 0` unhandled, and silently drops it.
- In-game chat lines are managed through a completely different subsystem:
  1. The central chat history manager `ChatHistory` (singleton pointer at `0x00aeefb4`).
  2. The chat panel array on `GameScreen`: an array of pointers located at `GameScreen + 0xc3c` (capacity at `GameScreen + 0xc64`, typically 5 lines).
  3. When a chat message is posted to `ChatHistory`, it dispatches an event via `0x0057f600` to listener `0x00634270`, which calls `GameScreen::update_chat_display` (`0x006c15e0`). This function reads the formatted chat line, strips the color tag via `0x006bf720`, and calls `0x00664840` on the chat panels at `GameScreen + 0xc3c` with **`type = 3`**.

---

### 1.2 Function 1A: `ChatHistory::add_message` (`0x008037b0`) **[VERIFIED]**

This is the primary chat function used by the game engine when network chat packets are received from other players, when AI players send chat, or when player names and colors are specified.

- **Address:** `0x008037b0`
- **Prologue Bytes:** `55 8b ec 6a ff 68 f1 02 99 00 64 a1 00 00 00 00` (First 5: `55 8b ec 6a ff`)
- **Calling Convention:** `__thiscall`
- **Stack Cleanup:** `ret $0x18` (6 stack arguments = 24 bytes, callee cleanup)
- **`this` Pointer (`ecx`):** `*(void**)0x00aeefb4` (Global `ChatHistory` object `DAT_00aeefb4`). Must be checked for non-NULL.
- **Parameters:**
  1. `sender_name` (`[ebp+0x08]`): `const char*` pointer to sender's name (e.g. `"Player 1"`, or `NULL` for no sender prefix).
  2. `message_text` (`[ebp+0x0c]`): `const char*` pointer to the message string.
  3. `player_id` (`[ebp+0x10]`): `int` (1..8). Determines the player's chat color index. Alternatively, pass local player ID from `*(int*)0x00aad300`.
  4. `flag` (`[ebp+0x14]`): `int` (default `0`).
  5. `p5` (`[ebp+0x18]`): `int` (default `0`).
  6. `notify_ui` (`[ebp+0x1c]`): `int` (`1` = immediately render on screen and notify `GameScreen`; `0` = add to history log only).

#### Color & Formatting Mechanism Inside `0x008037b0`
When `sender_name != NULL` and `1 <= player_id <= 8`, `0x008037b0` formats the line into:
```
"@%c%d%s%s: %s"
```
where `%c` is `'#'` (`0x23`), `%d` is `player_id`, followed by the sender name and message.  
Player color mapping in AoE2 HD:
| Player ID | Color | Tag Prefix |
|---|---|---|
| 1 | Blue | `@#1` |
| 2 | Red | `@#2` |
| 3 | Green | `@#3` |
| 4 | Yellow | `@#4` |
| 5 | Cyan | `@#5` |
| 6 | Purple | `@#6` |
| 7 | Grey | `@#7` |
| 8 | Orange | `@#8` |

Custom color tags like `<YELLOW>`, `<RED>`, `<GREEN>` (table at `0x00ada818`) placed at the start of the message string are also parsed by `0x006bf720`.

#### Quoted Disassembly of `0x008037b0`:
```assembly
008037b0: 55                   push   %ebp
008037b1: 8b ec                mov    %esp,%ebp
008037b3: 6a ff                push   $0xffffffff
008037b5: 68 f1 02 99 00       push   $0x9902f1
008037ba: 64 a1 00 00 00 00    mov    %fs:0x0,%eax
008037c0: 50                   push   %eax
008037c1: b8 94 14 00 00       mov    $0x1494,%eax
008037c6: e8 b5 13 0d 00       call   0x008d4b80        ; __alloca_probe
008037cb: a1 e0 81 ab 00       mov    0x00ab81e0,%eax   ; security cookie
008037d0: 33 c5                xor    %ebp,%eax
008037d2: 89 45 f0             mov    %eax,-0x10(%ebp)
008037d5: 56                   push   %esi
008037d6: 57                   push   %edi
008037d7: 50                   push   %eax
008037d8: 8d 45 f4             lea    -0xc(%ebp),%eax
008037db: 64 a3 00 00 00 00    mov    %eax,%fs:0x0
008037e1: 8b f9                mov    %ecx,%edi         ; edi = this (ChatHistory*)
...
; Format string selection:
00803ec3: 8d 85 f0 f3 ff ff    lea    -0xc10(%ebp),%eax
00803ec9: 89 85 b8 eb ff ff    mov    %eax,-0x1448(%ebp)
00803ecf: 68 88 a4 9a 00       push   $0x9aa488         ; "@%c%d%s%s: %s"
00803ed4: 68 00 10 00 00       push   $0x1000
00803ed9: ff 74 c7 2c          push   0x2c(%edi,%eax,8) ; history destination buffer
00803edd: e8 2e bd c2 ff       call   0x0042fc10        ; vsprintf helper
...
; Dispatch to UI listeners:
00803f73: 8b 8d a4 eb ff ff    mov    -0x145c(%ebp),%ecx
00803f79: 50                   push   %eax
00803f7a: e8 81 b6 d7 ff       call   0x0057f600        ; notify screen listeners
...
00804030: 8b e5                mov    %ebp,%esp
00804032: 5d                   pop    %ebp
00804033: c2 18 00             ret    $0x18
```

#### Existing Call Site: Received Chat Packet Handler `0x005844a0`
When a network `CommPacketChat` arrives from another player, `CommHandler::on_chat_packet` (`0x005844a0`) processes it and calls `0x008037b0` at `0x0058457c`:
```assembly
00584554: 8d 45 ac             lea    -0x54(%ebp),%eax
00584557: 50                   push   %eax
00584558: 8b cf                mov    %edi,%ecx         ; CommHandler*
0058455a: e8 41 f4 ff ff       call   0x005839a0        ; get_player_name_by_id
0058455f: c6 45 fc 05          movb   $0x5,-0x4(%ebp)
00584563: 6a 00                push   $0x0              ; notify_ui = 0 (in packet handler)
00584565: 6a 00                push   $0x0              ; p5 = 0
00584567: 6a 00                push   $0x0              ; flag = 0
00584569: 51                   push   %ecx              ; space for player_id
0058456a: 8b 4e 08             mov    0x8(%esi),%ecx    ; packet->sender_player_id
0058456d: 8b d4                mov    %esp,%edx
0058456f: 89 0a                mov    %ecx,(%edx)       ; player_id arg
00584571: ff 76 18             push   0x18(%esi)        ; packet->message_text
00584574: 8b 8f a0 00 00 00    mov    0xa0(%edi),%ecx   ; ecx = ChatHistory* (*(edi+0xa0) == DAT_00aeefb4)
0058457a: ff 30                push   (%eax)            ; sender_name string pointer
0058457c: e8 2f f2 27 00       call   0x008037b0        ; ChatHistory::add_message
```

---

### 1.3 Function 1B: `DisplayChatNotification` (`0x006bfc10`) **[VERIFIED]**

This is the standard 2-argument helper function used by `TRIBE_Game` across the game to display system notifications into the chat area (e.g. "Player X has resigned", "Tribute sent", etc.). It wraps `0x008037b0`.

- **Address:** `0x006bfc10`
- **Prologue Bytes:** `55 8b ec a1 00 d3 aa 00 6a 01` (First 5: `55 8b ec a1 00`)
- **Calling Convention:** `__stdcall` (callee stack cleanup)
- **Stack Cleanup:** `ret $0x8` (2 stack arguments = 8 bytes)
- **Parameters:**
  1. `text` (`[ebp+0x08]`): `const char*` null-terminated UTF-8 string to display.
  2. `sound_flag` (`[ebp+0x0c]`): `int` (`1` = play chat notification sound, `0` = silent).

#### Complete Disassembly of `0x006bfc10`:
```assembly
006bfc10: 55                   push   %ebp
006bfc11: 8b ec                mov    %esp,%ebp
006bfc13: a1 00 d3 aa 00       mov    0x00aad300,%eax   ; eax = current local player ID
006bfc18: 6a 01                push   $0x1              ; notify_ui = 1 (immediate screen update)
006bfc1a: 6a 00                push   $0x0              ; p5 = 0
006bfc1c: ff 75 0c             push   0xc(%ebp)         ; flag = sound_flag
006bfc1f: 51                   push   %ecx              ; allocate stack slot for player_id
006bfc20: 8b cc                mov    %esp,%ecx
006bfc22: ff 75 08             push   0x8(%ebp)         ; message_text = text
006bfc25: 6a 00                push   $0x0              ; sender_name = NULL (system / no name tag)
006bfc27: 89 01                mov    %eax,(%ecx)       ; player_id = *(0x00aad300)
006bfc29: 8b 0d b4 ef ae 00    mov    0x00aeefb4,%ecx   ; ecx = DAT_00aeefb4 (ChatHistory*)
006bfc2f: e8 7c 3b 14 00       call   0x008037b0        ; ChatHistory::add_message
006bfc34: 5d                   pop    %ebp
006bfc35: c2 08 00             ret    $0x8
```

#### Existing Call Sites in `TRIBE_Game` Event Handlers:
At `0x00632425` (inside `TRIBE_Game::handle_game_event`):
```assembly
00632419: 8b 0d 90 3d af 00    mov    0x00af3d90,%ecx   ; GameScreen*
0063241f: 8d 85 f0 fe ff ff    lea    -0x110(%ebp),%eax ; formatted message buffer
00632425: 6a 01                push   $0x1              ; sound_flag = 1
00632427: 50                   push   %eax              ; text
00632428: e8 e3 d7 08 00       call   0x006bfc10        ; DisplayChatNotification
```
At `0x006325e2` (silent notification):
```assembly
006325dc: 8d 85 f0 fe ff ff    lea    -0x110(%ebp),%eax
006325e2: 6a 00                push   $0x0              ; sound_flag = 0
006325e4: 50                   push   %eax              ; text
006325e5: e8 26 d6 08 00       call   0x006bfc10        ; DisplayChatNotification
```

---

### 1.4 How to Call In-Game Chat from C / Wololoader

#### Method A: Player / Colored Chat with Sender Name (`0x008037b0`)
```c
// Adds a line with sender name and player color tag (1..8) to chat and history
void wl_send_chat_ex(const char *sender_name, const char *text, int player_id) {
    void *chat_mgr = *(void **)0x00aeefb4;
    if (!chat_mgr) return; // Chat manager not initialized (not in a match)

    typedef void (__thiscall *ChatAdd_t)(
        void *this_ptr,
        const char *sender,
        const char *msg,
        int player,
        int flag,
        int p5,
        int notify_ui
    );
    ChatAdd_t chat_add = (ChatAdd_t)0x008037b0;
    // player_id 1 = Blue, 2 = Red, 3 = Green, 4 = Yellow, etc.
    // notify_ui = 1 renders immediately on screen.
    chat_add(chat_mgr, sender_name, text, player_id, 0, 0, 1);
}
```

#### Method B: Simple Chat Message (`0x006bfc10`)
```c
// Adds a line using the local player color and plays the chat audio chime
void wl_send_chat(const char *text) {
    if (!*(void **)0x00aeefb4) return; // Must have active chat manager
    typedef void (__stdcall *ChatSimple_t)(const char *msg, int sound_flag);
    ChatSimple_t chat_simple = (ChatSimple_t)0x006bfc10;
    chat_simple(text, 1); // 1 = play chime, 0 = silent
}
```

---

## 2. RUNTIME MULTIPLAYER DETECTION

### 2.1 The Canonical Game Check: `GameOptions::mGameType` (`+0x408`) **[VERIFIED]**

The game engine stores the match configuration in an instance of `.?AVGameOptions@@` (size `0x438`, vtable `0x009c2748`).

```c
// Returns 1 if currently in a MULTIPLAYER match, 0 if SINGLE PLAYER (or not in game)
int wl_is_multiplayer(void) {
    void *comm = *(void **)0x00aeefb8; // Global CommHandler*
    if (!comm) return 0;
    void *options = *(void **)((char *)comm + 0xd54); // Active GameOptions*
    if (!options) return 0;
    return (*(int *)((char *)options + 0x408) == 1);
}
```
Equivalent single-expression C read:
```c
(*(void**)0x00aeefb8 && *(void**)((char*)*(void**)0x00aeefb8 + 0xd54) && \
(*(int*)((char*)*(void**)((char*)*(void**)0x00aeefb8 + 0xd54) + 0x408) == 1))
```
*(Or via `TRIBE_Game` at `0x00af2c58`: `comm = *(void**)((char*)*(void**)0x00af2c58 + 0x98)`).*

#### Field Definitions at `GameOptions + 0x408`:
| Value | Mode Description | Notes |
|---|---|---|
| `0` | Single Player (Standard, Campaign, Custom Scenario) | Cheat codes allowed; local pause without network sync |
| `1` | **Multiplayer** (Ranked, Unranked, Steam Lobby, LAN) | Cheat codes disabled; pause synchronised across network |
| `2` | Scenario Editor | Map editing mode |
| `3` | Campaign Sequence / Cutscene | Special scripted sequence |

---

### 2.2 Disassembly Verification: Where the Game Checks `0x408`

#### Verification 1: Pause Handler (`0x0062e0eb` in `TRIBE_Game::process_system_message`)
When the user pauses the game (`WM_USER` message `0x17a2`), `TRIBE_Game` checks if the game is multiplayer before sending the pause network packet and broadcasting `"Game Paused by %s"`:
```assembly
0062e0c5: 8b 0d 90 3d af 00    mov    0x00af3d90,%ecx   ; ecx = GameScreen*
0062e0cb: 85 c9                test   %ecx,%ecx
0062e0cd: 74 05                je     0x0062e0d4
0062e0cf: e8 8c 4f 09 00       call   0x006c3060        ; toggle pause overlay
0062e0d4: 8d 85 ec fe ff ff    lea    -0x114(%ebp),%eax
0062e0da: 8b ce                mov    %esi,%ecx         ; esi = TRIBE_Game*
0062e0dc: 50                   push   %eax
0062e0dd: e8 de 27 f1 ff       call   0x005408c0        ; TRIBE_Game::get_game_options
0062e0e2: c7 45 fc 0b 00 00 00 movl   $0xb,-0x4(%ebp)
0062e0e9: 8b 00                mov    (%eax),%eax       ; eax = GameOptions*
0062e0eb: 8b b8 08 04 00 00    mov    0x408(%eax),%edi  ; edi = GameOptions->mGameType
0062e0f1: c7 45 fc ff ff ff ff movl   $0xffffffff,-0x4(%ebp)
...
0062e10c: 83 ff 01             cmp    $0x1,%edi         ; IS MULTIPLAYER?
0062e10f: 0f 85 a5 00 00 00    jne    0x0062e1ba        ; If NOT 1 (Single Player), skip network broadcast!
; If == 1 (Multiplayer): Broadcast pause packet to other players and display pause notification:
0062e115: 8b 45 0c             mov    0xc(%ebp),%eax
0062e118: 6a 00                push   $0x0
0062e11a: 8d 04 80             lea    (%eax,%eax,4),%eax
0062e11d: 8d 0c c5 d0 1a af 00 lea    0xaf1ad0(,%eax,8),%ecx
0062e124: 8d 85 a8 fe ff ff    lea    -0x158(%ebp),%eax
0062e12a: 50                   push   %eax
0062e12b: e8 c0 98 f6 ff       call   0x005979f0        ; CommHandler::broadcast_pause
```

#### Verification 2: Cheat Code Blocker (`0x006346b6` in `TRIBE_Game::process_chat_cheat`)
When a player types a cheat code (`"how do you turn this on"`, `"robin hood"`, etc.) in chat, the game verifies that the match is Single Player:
```assembly
006346b6: 8d 85 ec fd ff ff    lea    -0x214(%ebp),%eax
006346bc: 8b 0d 58 2c af 00    mov    0x00af2c58,%ecx   ; TRIBE_Game*
006346c2: 50                   push   %eax
006346c3: e8 f8 c1 f0 ff       call   0x005408c0        ; get_game_options
006346ca: 8b 00                mov    (%eax),%eax       ; GameOptions*
006346cc: 8b 80 08 04 00 00    mov    0x408(%eax),%eax  ; eax = GameOptions->mGameType
006346d2: 85 c0                test   %eax,%eax
006346d4: 75 1a                jne    0x006346f0        ; If != 0 (Multiplayer), CHEAT IS BLOCKED!
; If == 0 (Single Player): Execute cheat effect
```

#### Verification 3: Class Construction (`0x0060b2c0` & `0x00427676`)
In `FUN_0060b2c0` (Constructor for `GameOptions`, vtable `0x009c2748`):
```assembly
; param_2 is is_multiplayer (0 for SP, 1 for MP):
0060b377: 8b 45 0c             mov    0xc(%ebp),%eax    ; param_2
0060b37a: 89 86 08 04 00 00    mov    %eax,0x408(%esi)  ; GameOptions->0x408 = param_2
```
And in `FUN_00427676`:
- Single-player setup invokes: `FUN_0060b2c0(options, 0, ...)` -> sets `+0x408 = 0`.
- Multiplayer lobby setup invokes: `FUN_0060b2c0(options, 1, ...)` -> sets `+0x408 = 1`.

---

### 2.3 Alternative Detection: Human Player Counting

If an agent needs to determine whether multiple human players are in the match (versus a multiplayer room where someone plays alone with AI bots):

```c
// Counts human players in the current game session
int wl_count_human_players(void) {
    void *game = *(void **)0x00af2c58; // TRIBE_Game*
    if (!game) return 0;
    void *world = *(void **)((char *)game + 0x5c); // BaseWorld* (or *(void**)0x00addca0)
    if (!world) return 0;

    void **p_start = *(void ***)((char *)world + 0x184); // std::vector<WorldPlayer*> begin
    void **p_end   = *(void ***)((char *)world + 0x188); // std::vector<WorldPlayer*> end
    if (!p_start || !p_end || p_end <= p_start) return 0;

    int total_slots = (int)(p_end - p_start) / 2; // 8 bytes per player entry
    int human_count = 0;

    // Slot 0 is Gaia. Player slots start at index 1.
    for (int i = 1; i < total_slots && i <= 8; i++) {
        void *player = *(void **)((char *)p_start + i * 8);
        if (player) {
            // Player type at player + 0x8:
            // 3 = AI / Computer player (verified at 0x0074e020: cmpl $0x3, 0x8(%ecx))
            // Non-3 (1, 2, 4) = Human player
            int ptype = *(int *)((char *)player + 0x8);
            if (ptype != 3) {
                human_count++;
            }
        }
    }
    return human_count;
}
```

---

## 3. SUMMARY CHEAT SHEET

| Goal | Target Address / Expression | Type / Convention | Notes |
|---|---|---|---|
| **Post Chat Line with Color & Sender** | `0x008037b0` | `__thiscall(this=*(void**)0xaeefb4, sender, text, player_id, 0, 0, 1)` | Callee `ret $0x18`. Updates chat log and displays on screen with player color. |
| **Post Simple System Chat Notification** | `0x006bfc10` | `__stdcall(const char *text, int sound_flag)` | Callee `ret $0x8`. Uses local player color and plays chime if `sound_flag=1`. |
| **Is Multiplayer Match?** | `*(int*)((char*)*(void**)((char*)*(void**)0xaeefb8 + 0xd54) + 0x408) == 1` | Memory Read | Evaluates to `1` in MP, `0` in SP. Tested across pause and cheat code routines. |
| **Is Single Player Match?** | `*(int*)((char*)*(void**)((char*)*(void**)0xaeefb8 + 0xd54) + 0x408) == 0` | Memory Read | Evaluates to `1` in SP. |
| **Human Player Count** | Loop `players[1..8]` in `*(void**)0xaddca0 + 0x184`: `*(int*)(player + 8) != 3` | Iteration | Player type `3` is AI; any other non-zero type is Human. |
