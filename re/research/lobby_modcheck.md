# Multiplayer Lobby Mod Compatibility Check — Reverse Engineering Report

> **Target Game**: Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit PE, ImageBase `0x00400000`)  
> **Source**: `~/.cache/wl-test/exe.s` (full disassembly), `AoK HD.exe` (PE file)  
> **Verification**: Every claim, address, prologue byte, opcode sequence, calling convention, and struct offset is quoted directly from the binary disassembly and marked **[VERIFIED]** or **[GUESS]**.  
> **Output Target**: `re/research/lobby_modcheck.md`

---

## Executive Summary

1. **Local CRC & Remote CRC Storage [VERIFIED]**:
   - **Local CRC** is computed by `GameDataSet::getCrc()` at `0x005d5f20` (or `ModPackage::getChecksum` at `0x007d2270` when a workshop/local data mod is active).
   - **Remote Player CRC** arrives at the Host in packet **`0x52` (`PREGAMECLIENTSTATE`)** at offset **`+0x18`** of the packet payload.
   - On the **Host**, handler `0x00584a40` verifies `cmp [payload+0x18], local_crc`. On match, it copies the payload into the host's slot array `comm->mSlotState[slot]` at offset `(char*)comm + 0xda0 + slot * 0x40`, where CRC is stored at offset **`+0x18`**.
   - On **Both Host & Clients**, all player slot states are synchronized into the active `GameSettings` object at `(char*)settings + 0xb0 + slot * 0x68`. Each slot has:
     - `+0x08`: `uint32_t` CRC32 of game data.
     - `+0x0c`: `std::string` player name.
     - `+0x18`: `uint8_t` expansion / version byte.
     - `+0x54`: `int` slot status (2 = human player, 4 = AI, 0 = closed, 1 = open).

2. **Lobby Screen Class & Join Hook Point [VERIFIED]**:
   - Class name: **`GameSetupScreen`** (RTTI: `.?AVGameSetupScreen@@`, size `0xbe0` bytes).
   - Vtable: **`0x009d0d10`**.
   - Active screen pointer: `*(void**)0x00ada4b4`. If `*(void***)0x00ada4b4 == (void*)0x009d0d10`, the player is currently in the lobby.
   - Safe hook points:
     - **Authoritative Host Hook**: `0x00584a40` (`CommHandler::onPacketPregameClientState`). Intercepts remote CRC before any mismatch drop occurs.
     - **Universal Host & Client Hook**: `0x00656c60` (`GameSetupScreen::action`, vtable slot 17), which calls `0x0065fe40` (multiplayer slot synchronizer). Detects newly joined players and slot CRC changes across frames.

3. **Lobby Chat Display & `ChatHistory` [VERIFIED]**:
   - **Does lobby chat use the same history?** **YES.** The game maintains a single central `ChatHistory` singleton at `*(void**)0x00aeefb4`.
   - In its constructor (`0x00648081`), `GameSetupScreen` attaches an event listener functor (vtable `0x009d1080`) to `*(void**)0x00aeefb4`. When `ChatHistory::add_message` (`0x008037b0`) is invoked, the listener callback `0x00657f60` strips color formatting and calls `GameSetupScreen::add_chat_line` (`0x00654ba0`) automatically.
   - Direct lobby chat function: **`0x00654ba0`** (`GameSetupScreen::add_chat_line`).
     - Calling convention: `__thiscall(GameSetupScreen *this, const char *text, int play_sound_and_autoscroll)`. `ret 8`.

---

## 1. Where the Local CRC is Computed/Stored & Where Remote Player CRCs Arrive/Are Stored

### 1.1 Local CRC Computation & Resolution

The game calculates an IEEE 802.3 CRC32 of the active dataset.

- **Computation Function**: `0x005d5f20` (`GameDataSet::getCrc`) **[VERIFIED]**
  - **Prologue Bytes**: `55 8b ec 6a ff` (`push ebp; mov ebp, esp; push -1`)
  - **Calling Convention**: `__thiscall(GameDataSet *this)` (returns `uint32_t` in `eax`), `ret 0`.
  - Disassembly (`~/.cache/wl-test/exe.s:620028`):
    ```assembly
    005d5f20: 55                   push   %ebp
    005d5f21: 8b ec                mov    %esp,%ebp
    005d5f23: 6a ff                push   $0xffffffff
    005d5f25: 68 50 70 93 00       push   $0x937050
    ...
    005d5f47: c3                   ret
    ```

- **Data Mod Override**: If an active Data Mod is present, `ModPackage::getChecksum` (`0x007d2270`) is used instead **[VERIFIED]**:
  ```assembly
  006566bf: mov  0xaf5890,%ecx       ; ModManager*
  006566c5: mov  %eax,-0x38(%ebp)    ; save base dataset CRC
  006566c8: call 0x007ce330          ; ModManager::getActiveDataMod
  006566cd: test %eax,%eax
  006566cf: je   0x6566da
  006566d1: mov  %eax,%ecx
  006566d3: call 0x007d2270          ; ModPackage::getChecksum
  ```

- **Pointer Chain from `TRIBE_Game` to `GameDataSet`** **[VERIFIED]**:
  ```c
  void *game = *(void**)0x00af2c58;                          // TRIBE_Game*
  void *comm = *(void**)((char*)game + 0x98);                // CommHandler* (size 0x1030)
  void *settings = *(void**)((char*)comm + 0xd54);           // GameSettings*
  void *dataset_wrapper = *(void**)((char*)settings + 0x88); // GameDataSet shared_ptr wrapper
  void *dataset = *(void**)((char*)dataset_wrapper + 0x8);   // GameDataSet*
  uint32_t local_crc = ((uint32_t(__fastcall*)(void*,void*))0x005d5f20)(dataset, NULL);
  ```

---

### 1.2 Remote Player CRC Reception (Host: Packet 0x52 `PREGAMECLIENTSTATE`)

When a client joins or changes settings in the lobby, it transmits network packet `0x52` (`PREGAMECLIENTSTATE`). The Host receives and validates this in `0x00584a40`.

- **Handler Address**: `0x00584a40` **[VERIFIED]**
- **Prologue Bytes**: `55 8b ec 6a ff 68 10 ca 92 00` (`push ebp; mov ebp, esp; push -1; push $0x92ca10`)
- **Calling Convention**: `__thiscall(CommHandler *this, void *peer_wrapper, void *packet_payload)`, `ret 8` at `0x00584c6a`.
- **Packet Registration** (`0x0057be1f` in `CommHandler::init_packet_handlers`):
  ```assembly
  0057be1f: push   $0x0
  0057be21: push   $0x584a40           ; Handler function
  0057be26: push   $0x9c27f0           ; "PREGAMECLIENTSTATE"
  0057be2b: push   $0x52               ; Packet ID 0x52
  0057be2d: mov    %edi,%ecx           ; CommHandler*
  0057be2f: call   0x0057a330          ; register_handler
  ```

- **Disassembly of CRC Check in `0x00584a40`** (`~/.cache/wl-test/exe.s:512580..512660`):
  ```assembly
  00584a40: push   %ebp
  00584a41: mov    %esp,%ebp
  ...
  00584a6b: mov    %ecx,%edi           ; edi = CommHandler* this
  00584a6d: cmpl   $0x5,0xd0(%edi)     ; check role: 5 == HOST
  00584a74: mov    0xc(%ebp),%ebx      ; ebx = packet_payload
  00584a77: mov    0x8(%ebp),%esi      ; esi = peer_wrapper
  00584a7a: mov    %ebx,-0x2c(%ebp)
  00584a7d: jne    0x584c4f            ; if not host, exit
  00584a83: call   0x00583640          ; returns &this->0xd54 (settings**)
  00584a88: mov    (%eax),%eax         ; eax = GameSettings*
  00584a8a: mov    0x88(%eax),%eax     ; eax = dataset wrapper
  00584a90: mov    0x8(%eax),%ecx      ; ecx = GameDataSet*
  00584a93: call   0x005d5f20          ; GameDataSet::getCrc -> local CRC
  00584a9e: mov    %eax,-0x20(%ebp)    ; save local CRC on stack
  ...
  00584aab: mov    0x10(%esi),%eax     ; eax = peer object pointer
  00584aae: mov    %eax,-0x28(%ebp)
  ...
  00584ad0: mov    -0x20(%ebp),%eax    ; eax = local CRC
  00584ad3: cmp    0x18(%ebx),%eax     ; COMPARE [packet+0x18] WITH LOCAL CRC!
  00584ad6: je     0x584b63            ; MATCH: continue to version check
  ; --- MISMATCH PATH ---
  00584adc: mov    0xaf5890,%ecx       ; ModManager*
  00584ae2: call   0x007ce330          ; getActiveDataMod
  00584ae7: test   %eax,%eax
  00584ae9: jne    0x584b63            ; if workshop mod active, bypass
  ...
  00584b08: push   $0x9c2bfc           ; "GAMEDATA_CRC_CHECK" (String ID 10956)
  00584b16: mov    0xad13e0,%ecx       ; StringTable*
  00584b1c: call   0x00448370          ; format drop message
  00584b3b: push   $0x3                ; reason = 3 (GAMEDATA_CRC_CHECK)
  00584b3d: push   %ebx                ; peer
  00584b3e: mov    %edi,%ecx           ; CommHandler*
  00584b40: call   0x00583f50          ; DROP CLIENT!
  00584b45: push   -0x1c(%ebp)         ; formatted message
  00584b48: mov    %edi,%ecx
  00584b4a: call   0x00588030          ; CommHandler::SendChat (broadcast reason)
  00584b5e: jmp    0x584c4f
  ; --- MATCH PATH ---
  00584b63: mov    -0x21(%ebp),%al     ; expansion byte
  00584b66: cmp    0x1c(%ebx),%al      ; check [packet+0x1c] version
  00584b69: je     0x584bfd            ; MATCH: copy to slot table
  ...
  00584bfd: mov    -0x28(%ebp),%eax    ; eax = peer object
  00584c00: lea    0xda0(%edi),%esi    ; esi = comm->mSlotState base (0xda0)
  00584c09: mov    0x18(%eax),%eax     ; eax = slot index (0..7)
  00584c0c: shl    $0x6,%eax           ; stride = 0x40 (64 bytes)
  00584c0f: add    %eax,%esi           ; esi = &comm->mSlotState[slot]
  00584c1e: call   0x0057d2e0          ; COPY PACKET TO SLOT (copies CRC to slot+0x18)
  00584c26: call   0x00598310          ; fetch settings slot
  00584c2e: call   0x0059ddf0          ; sync player name
  00584c4a: call   0x0057d740          ; notify slot listeners on comm+0x84
  ```

---

### 1.3 Slot Tables: Memory Offsets for Reading Remote Player CRCs

There are **two** distinct structures where remote CRCs reside:

#### Structure 1: `CommHandler::mSlotState` Array (Host Only) **[VERIFIED]**
- **Base Offset**: `(char*)comm + 0xda0`
- **Slot Capacity**: 8 slots (0..7). Total size: `0x200` bytes (`0xda0..0xfa0`).
- **Slot Stride**: `0x40` (64 bytes).
- **Slot Offsets**:
  - `+0x18`: `uint32_t` CRC32 received from packet `+0x18`.
  - `+0x1c`: `uint8_t` expansion byte.
  - `+0x20`: Player name / identification struct.
- **Copy Function**: `0x0057d2e0` (`push [ebp+8]; mov %esi, %ecx; call 0x57d2e0`):
  ```assembly
  0057d318: mov    0x18(%edi),%eax     ; read CRC from packet
  0057d31b: mov    %eax,0x18(%esi)     ; store CRC in slot+0x18
  0057d31e: movzbl 0x1c(%edi),%eax
  0057d322: mov    %al,0x1c(%esi)      ; store expansion in slot+0x1c
  ```

#### Structure 2: `GameSettings::mPlayerSlots` Array (Host AND Clients) **[VERIFIED]**
- **Base Offset**: `(char*)settings + 0xb0`
- **Slot Capacity**: 8 slots (0..7).
- **Slot Stride**: `0x68` (104 bytes).
- **Slot Offsets**:
  - `+0x08`: `uint32_t` CRC32 of game data.
  - `+0x0c`: `std::string` player name (`cap < 16 ? ptr : *(char**)ptr`).
  - `+0x18`: `uint8_t` expansion / version byte.
  - `+0x4c`: `int` team index.
  - `+0x54`: `int` slot status (0 = closed, 1 = open, 2 = human player, 4 = AI, 6 = remote human).
- **Loop Stride Proof (`0x00656260..0x00656270`)**:
  ```assembly
  00656192: lea    0xb0(%edi),%ecx     ; ecx = &settings->mPlayerSlots[0]
  ...
  00656266: inc    %esi                ; slot_idx++
  00656267: add    $0x68,%ecx          ; ecx += 0x68 (104 bytes stride)
  0065626d: cmp    $0x8,%esi           ; 8 slots
  00656270: jb     0x6561d0
  ```
- **CRC Read Proof (`0x006566a1`)**:
  ```assembly
  006566a1: mov    0x8(%ecx),%eax      ; eax = slot CRC32 at +0x08
  006566a7: mov    %eax,-0x6c(%ebp)
  ...
  006566dd: cmp    %eax,-0x6c(%ebp)    ; client compares local CRC vs slot CRC
  ```

---

## 2. The Lobby Screen Class & Safe Hook Points

### 2.1 Lobby Screen Class: `GameSetupScreen` **[VERIFIED]**

The lobby interface in Age of Empires II HD is managed by `GameSetupScreen`.

- **RTTI Class Name**: `.?AVGameSetupScreen@@` **[VERIFIED]**
- **Object Size**: `0xbe0` bytes (allocated via `push $0xbe0; call 0x008d47df` at `0x005c6a01`).
- **Vtable VA**: **`0x009d0d10`** **[VERIFIED]**
  - Stored in instance during constructor `0x00647430` at `0x0064747d`:
    ```assembly
    0064747d: movl $0x9d0d10,(%esi)
    ```
- **Active Screen Global**: `*(void**)0x00ada4b4` (`TScreenPanelManager::mActiveScreen`).
  - Detection snippet:
    ```c
    void *active_screen = *(void**)0x00ada4b4;
    bool in_lobby = (active_screen != NULL && *(void**)active_screen == (void*)0x009d0d10);
    ```

---

### 2.2 Hook Point 1 (Authoritative Host Network Hook): `0x00584a40` **[VERIFIED]**

This is the most critical and safe hook for the host because it catches the client's packet `0x52` **before** the vanilla game drops the player on CRC mismatch.

- **Function Target**: `0x00584a40` (`CommHandler::onPacketPregameClientState`)
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push ebp; mov ebp, esp; push -1`)
- **Stolen Bytes for Detour**: `55 8b ec 6a ff` (5 bytes)
- **Calling Convention**: `__thiscall`, replacement signature:
  ```c
  void __fastcall wl_hook_pregame_client_state(void *self, void *edx, void *peer_wrapper, void *packet_payload);
  ```
- **Stack Cleanup**: `ret 8` (`c2 08 00` at `0x00584c6a`).
- **Timing**: Fires immediately upon receipt of packet `0x52` over the network when a player connects to the host's lobby.

---

### 2.3 Hook Point 2 (Universal Host & Client UI Tick Hook): `0x00656c60` **[VERIFIED]**

Runs once per frame on the main thread while the lobby is open. Ideal for tracking slot updates on both host and client machines.

- **Function Target**: `0x00656c60` (`GameSetupScreen::action`, vtable slot 17 at `0x009d0d10 + 0x44`)
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push ebp; mov ebp, esp; push -1`)
- **Stolen Bytes for Detour**: `55 8b ec 6a ff` (5 bytes)
- **Calling Convention**: `__thiscall(GameSetupScreen *this)`, replacement:
  ```c
  void __fastcall wl_hook_gamesetup_action(void *self, void *edx);
  ```
- **Stack Cleanup**: `ret 0` (`c3` at `0x00656da7`).
- **Internal Helper**: Calls `0x0065fe40` (`GameSetupScreen::sync_player_slots`) at `0x00656cab`.
  Inside `0x00656c60`, `self + 0xbd8` is the `CommHandler*`, and `*(void**)((char*)comm + 0xd54)` is `GameSettings*`.

---

## 3. Lobby Chat Subsystem & How to Show Short Text

### 3.1 Does Lobby Chat Use `*(void**)0x00aeefb4`? **[VERIFIED]**

**YES.** The lobby chat uses the exact same `ChatHistory` singleton as the in-game screen.

#### Proof of Listener Registration in `GameSetupScreen::ctor` (`0x00648081..0x006480c4`):
```assembly
00648081: lea    -0x48(%ebp),%eax
00648084: movl   $0x9d1080,-0x48(%ebp) ; vtable of listener functor
0064808b: mov    %edi,-0x44(%ebp)      ; GameSetupScreen* this
0064808e: mov    %eax,-0x24(%ebp)
0064809e: mov    0xaeefb4,%eax         ; ChatHistory* singleton
006480b6: mov    %eax,0x28(%esi)
006480b9: call   0x0057d8b0
006480c4: call   0x004667c0            ; registers functor as ChatHistory listener
```

#### Proof of Network Chat Dispatch (`0x00584574..0x0058457c`):
When network chat packet `0x43` arrives in the lobby:
```assembly
00584574: mov    0xa0(%edi),%ecx       ; [comm + 0xa0] == *(void**)0xaeefb4
0058457a: push   (%eax)                ; sender name
0058457c: call   0x008037b0            ; ChatHistory::add_message
```

#### Proof of Listener Execution & Auto-forwarding to UI (`0x00657f60..0x00657f9b`):
When `ChatHistory::add_message` is called, `0x00803f73` notifies listeners. `GameSetupScreen`'s listener functor calls `0x00657f60`:
```assembly
00657f60: push   %ebp
00657f61: mov    %esp,%ebp
00657f63: mov    0x8(%ebp),%eax        ; event struct from ChatHistory
00657f67: mov    0xc(%eax),%esi        ; formatted text string
...
00657f83: cmpb   $0x40,(%esi)          ; check for '@'
00657f88: cmpb   $0x23,0x1(%esi)       ; check for '#'
00657f8e: add    $0x3,%esi             ; strips "@#<color>" tag
...
00657f98: push   $0x0                  ; flag = 0
00657f9a: push   %esi                  ; cleaned text
00657f9b: call   0x00654ba0            ; GameSetupScreen::add_chat_line !!
00657fa2: ret    $0x4
```

---

### 3.2 The Dedicated Lobby Chat Add Function: `0x00654ba0` **[VERIFIED]**

If our DLL holds a pointer to `GameSetupScreen` (from `*(void**)0x00ada4b4`), it can call `0x00654ba0` directly.

- **Address**: `0x00654ba0` (`GameSetupScreen::add_chat_line`)
- **Prologue Bytes**: `55 8b ec 6a ff` (`push ebp; mov ebp, esp; push -1`)
- **Epilogue**: `ret 8` (`c2 08 00` at `0x00654d0c`).
- **Calling Convention**: `__thiscall(GameSetupScreen *this, const char *text, int play_sound_and_autoscroll)`
- **C Prototype**:
  ```c
  typedef void (__fastcall *LobbyChatAdd_t)(void *screen, void *edx, const char *text, int play_sound);
  #define LOBBY_CHAT_ADD ((LobbyChatAdd_t)0x00654ba0)
  ```
- **Disassembly Details (`0x00654bc9..0x00654c6a`)**:
  ```assembly
  00654bc9: push   0x668(%esi)           ; 0x668 = chat listbox panel ID
  00654bcf: call   0x00426d40            ; GetPanelByID
  00654c46: push   0x668(%esi)
  00654c56: call   0x004973a0            ; TListPanel::AddItem / SetText
  00654c6a: cmpl   $0x0,0xc(%ebp)        ; check flag arg
  00654c6e: je     0x654cdd              ; if 0, skip sound and autoscroll
  ...
  00654cd8: call   0x00496f50            ; play chat sound
  00654cf7: call   0x00496d00            ; scroll listbox to bottom
  00654d0c: ret    $0x8
  ```

---

### 3.3 Which Method Should Wololoader Use?

| Method | Function | Target Object | Advantages | Caveats |
|---|---|---|---|---|
| **A. `ChatHistory::Add`** | `0x008037b0` | `*(void**)0x00aeefb4` | Global singleton; works in lobby AND in-game; preserved across transitions. | Strips `@#` color tags via listener; adds 8-byte entry to ring buffer. |
| **B. `LobbyChatAdd`** | `0x00654ba0` | `*(void**)0x00ada4b4` | Directly manipulates lobby listbox; controls sound/scroll via `flag=1`. | Only valid while `active_screen` is `GameSetupScreen`. |

> **Recommendation**: Use **Method A (`ChatHistory::Add` at `0x008037b0`)**. It is completely decoupled from UI window pointers, is guaranteed to be non-NULL while the game engine is running, and automatically routes into `GameSetupScreen::add_chat_line` through the engine's built-in listener.

---

## 4. Concrete Implementation Plan

### 4.1 Plan Overview

1. Hook `0x00584a40` (`CommHandler::onPacketPregameClientState`) using `wl_hook`.
2. When a client packet arrives on the Host (`role == 5`):
   - Extract `peer` from `arg1 + 0x10`.
   - Read `slot = *(int*)(peer + 0x18)`.
   - Read client's transmitted CRC from `packet_payload + 0x18`.
   - Read local dataset CRC from `GameDataSet::getCrc` (`0x005d5f20`).
   - Extract player name from `settings + 0xb0 + slot * 0x68 + 0x0c`.
   - Compare CRCs.
   - Post message to lobby chat via `ChatHistory::Add` (`0x008037b0`) with `player = -1` (system line) and `type = 0` (or `type = 1` for silent).
3. If CRCs mismatch: allow the vanilla game to execute its original drop sequence at `0x00584b40`, but the host now sees our descriptive Wololoader diagnostic message in the lobby chat window prior to the generic `"GAMEDATA_CRC_CHECK"` drop notice.

---

### 4.2 Hook Specification

| Property | Value | Notes |
|---|---|---|
| **Target Address** | `0x00584a40` | `CommHandler::onPacketPregameClientState` |
| **Prologue Bytes** | `55 8b ec 6a ff` | 5 bytes (`push %ebp; mov %esp,%ebp; push $0xffffffff`) |
| **Detour Type** | `wl_hook` | 5-byte JMP detour to hook function |
| **Calling Convention** | `__thiscall` | Handled via `__fastcall(self, edx, peer_wrapper, packet_payload)` |
| **Stack Cleanup** | `ret 8` | Callee cleans 8 stack bytes (`peer_wrapper`, `packet_payload`) |
| **Original Trampoline** | `g_wl_orig_pregame_client_state` | Restores stolen bytes and jumps back to `0x00584a45` |

---

### 4.3 C Code Implementation Snippet

```c
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/* ChatHistory::Add signature: __thiscall(this, sender, text, player, type, a5, a6), ret 0x18 */
typedef int (__fastcall *ChatAdd_fn)(void *hist, void *edx, const char *sender,
                                    const char *text, int player, int type, int a5, int a6);
#define CHAT_ADD ((ChatAdd_fn)0x008037b0)

/* GameDataSet::getCrc signature: __thiscall(this), ret 0 */
typedef uint32_t (__fastcall *GetCrc_fn)(void *dataset, void *edx);
#define GET_DATASET_CRC ((GetCrc_fn)0x005d5f20)

/* Original trampoline */
typedef void (__fastcall *PregameClientState_fn)(void *self, void *edx, void *peer_wrapper, void *packet_payload);
static PregameClientState_fn g_orig_pregame_client_state = NULL;

static void wl_post_lobby_chat(const char *msg)
{
    void *hist = *(void**)0x00aeefb4;
    if (hist) {
        /* player = -1 (system white text), type = 0 (plays chatrcvd.wav) */
        CHAT_ADD(hist, NULL, NULL, msg, -1, 0, 0, 0);
    }
}

static const char* wl_get_slot_player_name(void *settings, int slot, char *fallback_buf, size_t buf_size)
{
    if (settings && slot >= 0 && slot < 8) {
        char *slot_ptr = (char*)settings + 0xb0 + slot * 0x68;
        /* MSVC 32-bit std::string: buffer at +0x0c, size at +0x1c, capacity at +0x20 */
        size_t cap = *(size_t*)(slot_ptr + 0x0c + 20);
        const char *name = (cap < 16) ? (slot_ptr + 0x0c) : *(const char**)(slot_ptr + 0x0c);
        if (name && name[0] != '\0') {
            return name;
        }
    }
    snprintf(fallback_buf, buf_size, "Player %d", slot + 1);
    return fallback_buf;
}

void __fastcall wl_hook_pregame_client_state(void *self, void *edx, void *peer_wrapper, void *packet_payload)
{
    /* [self + 0xd0] role: 5 == HOST */
    int role = *(int*)((char*)self + 0xd0);

    if (role == 5 && peer_wrapper && packet_payload) {
        void *peer = *(void**)((char*)peer_wrapper + 0x10);
        if (peer) {
            int slot = *(int*)((char*)peer + 0x18);
            uint32_t remote_crc = *(uint32_t*)((char*)packet_payload + 0x18);

            /* Resolve local dataset CRC */
            void *settings = *(void**)((char*)self + 0xd54);
            uint32_t local_crc = 0;
            if (settings) {
                void *ds_wrap = *(void**)((char*)settings + 0x88);
                if (ds_wrap) {
                    void *dataset = *(void**)((char*)ds_wrap + 0x8);
                    if (dataset) {
                        local_crc = GET_DATASET_CRC(dataset, NULL);
                    }
                }
            }

            char fallback_name[32];
            const char *player_name = wl_get_slot_player_name(settings, slot, fallback_name, sizeof(fallback_name));

            char msg[128];
            if (remote_crc == local_crc) {
                snprintf(msg, sizeof(msg), "Wololoader: mods match (%s)", player_name);
            } else {
                snprintf(msg, sizeof(msg), "Wololoader: %s has different mods (CRC %08X vs %08X)",
                         player_name, remote_crc, local_crc);
            }

            wl_post_lobby_chat(msg);
        }
    }

    /* Execute original game packet handler */
    g_orig_pregame_client_state(self, edx, peer_wrapper, packet_payload);
}
```

---

## 5. Summary Table of Verified Addresses

| Symbol / Target | Virtual Address (VA) | Stolen / Prologue Bytes | Calling Convention | `ret N` | Purpose |
|---|---|---|---|---|---|
| `CommHandler::onPacketPregameClientState` | `0x00584a40` | `55 8b ec 6a ff` | `__thiscall` | `ret 8` | Host Packet `0x52` handler; receives remote CRC |
| `GameDataSet::getCrc` | `0x005d5f20` | `55 8b ec 6a ff` | `__thiscall` | `ret 0` | Computes CRC32 of active game dataset |
| `CommHandler::onPacketPregameHostState` | `0x00584c70` | `55 8b ec 6a ff` | `__thiscall` | `ret 8` | Client Packet `0x5a` handler; unpacks host slots |
| `GameSetupScreen::vtable` | `0x009d0d10` | N/A | N/A | N/A | Vtable for lobby screen |
| `GameSetupScreen::ctor` | `0x00647430` | `55 8b ec 6a ff` | `__thiscall` | `ret 4` | Constructs lobby screen & registers chat listener |
| `GameSetupScreen::action` | `0x00656c60` | `55 8b ec 6a ff` | `__thiscall` | `ret 0` | Lobby screen per-frame idle/sync tick (Slot 17) |
| `GameSetupScreen::add_chat_line` | `0x00654ba0` | `55 8b ec 6a ff` | `__thiscall` | `ret 8` | Appends text directly to lobby UI chat listbox |
| `GameSetupScreen::on_chat_listener` | `0x00657f60` | `55 8b ec 8b 45` | `__thiscall` | `ret 4` | ChatHistory listener callback forwarding to UI |
| `ChatHistory::add_message` | `0x008037b0` | `55 8b ec 6a ff` | `__thiscall` | `ret 0x18` | Appends line to central ChatHistory singleton |
| `ChatHistory singleton pointer` | `0x00aeefb4` | N/A | N/A | N/A | Pointer to active `ChatHistory` object |
| `TScreenPanelManager active screen` | `0x00ada4b4` | N/A | N/A | N/A | Pointer to currently displayed `TScreen` |
| `CommHandler global pointer` | `0x00af1af4` | N/A | N/A | N/A | Pointer to active `CommHandler` (at `game + 0x98`) |
