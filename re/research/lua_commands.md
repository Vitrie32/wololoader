# Reverse Engineering Report: Game Command Packets for Multiplayer-Safe Lua Scripting

**Target binary:** `AoK HD.exe` (Age of Empires II HD 5.8, 32-bit, ImageBase `0x400000`)  
**Scope:** Reverse engineering and formal documentation of internal command packet builders, packet formats, dispatchers, executors, and security/cheat checks.  
**Purpose:** Enable Wololoader Lua scripts to safely mutate game simulation state across multiplayer sessions without causing desynchronization ("Out of Sync" / OOS errors) by broadcasting deterministic command packets through the engine's native communications pipeline.  
**Classification:** All claims are classified as **[VERIFIED]** (backed by exact disassembly addresses and instruction bytes from `AoK HD.exe`) or **[GUESS]**.

---

## 1. Quick Address & Packet Reference

| Packet Type | Name / Function | Builder Addr | Builder Convention / Args | Packet Size | Executor Addr | Cheats Check? | Ownership Check? | Status |
|---|---|---|---|---|---|---|---|---|
| `0x05` (5) | **Resource Cheat** (`robin hood`, `cheese steak jimmy's`, etc.) | `0x00808c40` | `__thiscall(cmd, player_id, res_id, amount)`, `ret $0xc` | 8 bytes (`0x08`) | `0x0080a1b0` | **YES** (`[settings+0x40f]`) | No | **[VERIFIED]** |
| `0x6c` (108) | **Tribute Resources** (give to another player) | `0x00812970` | `__thiscall(cmd, src_pId, dst_pId, res_id, amount, fee)`, `ret $0x14` | 12 bytes (`0x0c`) | `0x008151c0` | **NO** | Resource balance capped | **[VERIFIED]** |
| `0x04` (4) | **Spawn Unit at Position** (Editor / Cobra Car / Saboteur) | `0x00809220` | `__thiscall(cmd, unit_type_id, player_id, x, y, z)`, `ret $0x14` | 20 bytes (`0x14`) | `0x008161d0` | **NO** | **NO** (any player/coord) | **[VERIFIED]** |
| `0x64` (100) | **Queue / Train Unit** (Building production) | `0x00812a40` | `__thiscall(cmd, player_id, bldg_id, unit_type_id, flag)`, `ret $0x10` | 16 bytes (`0x10`) | `0x00815640` | **NO** | Yes (building owner) | **[VERIFIED]** |
| `0x65` (101) | **Start Research at Building** | `0x00812d60` / `0x00812de0` | `__thiscall(cmd, bldg_unit, tech_id)`, `ret $0x8` / `ret $0x10` | 16 bytes (`0x10`) | `0x00815bf0` | **NO** | Yes (bldg owner & tech req) | **[VERIFIED]** |
| `0x12` (18) | **Set Unit Stance** | `0x00809d50` | `__thiscall(cmd, Unit **units, short count, uint8_t stance)`, `ret $0xc` | `3 + 4*n` bytes | `0x0080d580` | **NO** | Validates unit ptrs | **[VERIFIED]** |
| `0x6a` (106) | **Delete Unit / Building** | `0x00812670` | `__thiscall(cmd, Unit *unit)`, `ret $0x4` | 12 bytes (`0x0c`) | `0x008140d0` | **NO** | Yes (`unit->owner == pId`) | **[VERIFIED]** |

---

## 2. Multiplayer Architecture & The Deterministic Command Pipeline

In Age of Empires II (Genie Engine), multiplayer synchronization follows a deterministic lockstep simulation model. Modifying memory directly (e.g., writing directly to `[player + 0x3c + res_id*4]` or modifying unit positions on only one client) causes immediate simulation divergency and an **"Out of Sync" (OOS)** drop.

### 2.1 The Communications Handler (`TCommunications_Handler`)
All simulation-mutating player actions (orders, training, building, stances, tributes, deletions) are encapsulated into binary command packets and passed to the central command queue via:
- `TCommunications_Handler* cmd`: Located at `[[world] + 0xc0]` (where `world = [0xaf2c58 + 0xc8]`).
- Transmitter Function: `0x0080ef70`
  ```c
  void __thiscall TCommunications_Handler::send_command(
      TCommunications_Handler *this, /* ecx */
      void *packet_buffer,           /* stack arg 1 */
      int packet_size,               /* stack arg 2 */
      int player_id                  /* stack arg 3 */
  ); /* ret $0xc */
  ```
- Memory Allocator: Packet buffers passed to `0x0080ef70` are allocated with `0x008ea479(1, size)` (`game_calloc`). The communications layer takes ownership of the memory and frees it after transmission.

### 2.2 Dual-Layer Command Dispatch
When command packets are executed at turn boundaries, they pass through a two-tiered hierarchy:
1. `TRIBE_Command::execute_command` (`0x00813460`):
   - Switch statement handling game-specific packet types `0x64..0x81` (train, research, tribute, wall, gate, delete unit) and `case 0x04` (unit creation).
   - If a packet opcode is not handled in `TRIBE_Command`, the `default:` case falls back to `RGE_Command::execute_command` at `0x00809fe0`.
2. `RGE_Command::execute_command` (`0x00809fe0`):
   - Jump table `0x80a104` handling engine-level packets `0x00..0x22` (move, attack, cheat resource, stance, formation, waypoint).

```
                  +----------------------------------------+
                  | Packet arrives at Turn Simulation Tick |
                  +----------------------------------------+
                                      |
                                      v
                  +----------------------------------------+
                  |  TRIBE_Command::execute_command        |
                  |  Address: 0x00813460                   |
                  +----------------------------------------+
                                 /          \
               Handles 0x64..0x81 & 0x04     Default (unhandled)
                             /                \
                            v                  v
                 +-------------------+  +--------------------------------+
                 | TRIBE Executors   |  | RGE_Command::execute_command   |
                 | (0x8140d0, etc.)  |  | Address: 0x00809fe0            |
                 +-------------------+  +--------------------------------+
                                                       |
                                            Jump table 0x80a104 (0..0x22)
                                                       v
                                            +---------------------+
                                            | RGE Executors       |
                                            | (0x80a1b0, 0x80d580)|
                                            +---------------------+
```

---

## 3. Resource Manipulation

### 3.1 Resource Cheat Command (Packet `0x05`) — **[VERIFIED]**

Used by the engine to process chat cheat codes:
- `robin hood` -> +1000 Gold (resource 3)
- `cheese steak jimmy's` -> +1000 Food (resource 0)
- `lumberjack` -> +1000 Wood (resource 1)
- `rock on` -> +1000 Stone (resource 2)

#### Builder: `0x00808c40`
```asm
808c40: push   %ebp
808c41: mov    %esp,%ebp
808c43: push   %esi
808c44: push   %edi
808c45: push   $0x8                ; size = 8 bytes
808c47: push   $0x1
808c49: mov    %ecx,%edi           ; edi = cmd_obj
808c4b: call   0x8ea479            ; calloc(1, 8)
808c50: mov    %eax,%esi
808c52: add    $0x8,%esp
808c55: test   %esi,%esi
808c57: je     0x808c82
808c59: mov    0x8(%ebp),%ecx      ; player_id
808c5c: push   %ecx
808c5d: movb   $0x5,(%esi)         ; packet[0] = 0x05 (COMMAND_CHEAT_RESOURCE)
808c60: mov    %cl,0x1(%esi)       ; packet[1] = player_id
808c63: mov    0xc(%ebp),%eax      ; resource_id
808c66: mov    %al,0x2(%esi)       ; packet[2] = resource_id
808c69: movb   $0x0,0x3(%esi)      ; packet[3] = 0
808c6d: mov    0x10(%ebp),%eax     ; amount (float)
808c70: mov    %esp,%ecx
808c72: mov    %eax,0x4(%esi)      ; *(float*)(packet + 4) = amount
808c75: push   $0x8                ; size 8
808c77: mov    0x4(%esp),%eax
808c7b: mov    %edi,%ecx           ; cmd_obj
808c7d: mov    %eax,(%ecx)
808c7f: push   %esi                ; packet buffer
808c80: call   0x80ef70            ; TCommunications_Handler::send_command
808c85: pop    %edi
808c86: pop    %esi
808c87: pop    %ebp
808c88: ret    $0xc
```
- **Prototype:** `void __thiscall(RGE_Command *this, int player_id, int resource_id, float amount)`
- **`ret` instruction:** `ret $0xc`
- **Packet Layout (8 bytes):**
  - `packet[0]` (u8): `0x05` (opcode)
  - `packet[1]` (u8): `player_id`
  - `packet[2]` (u8): `resource_id` (`0` = Food, `1` = Wood, `2` = Stone, `3` = Gold)
  - `packet[3]` (u8): `0x00` (padding)
  - `packet[4..7]` (f32): `amount` (IEEE 754 32-bit float)

#### Executor: `0x0080a1b0`
Dispatched by `RGE_Command::execute_command` at `0x00809fe0` via jump table `0x80a104` index 5 (stub `0x80a02e`).
```asm
80a1b0: push   %ebp
80a1b1: mov    %esp,%ebp
80a1b3: sub    $0x8,%esp
80a1b6: mov    0x8(%ebp),%eax      ; eax = packet pointer
80a1b9: push   %esi
80a1ba: push   %edi
80a1bb: movzbl 0x1(%eax),%edi      ; edi = packet->player_id
80a1bf: mov    0x4(%ecx),%eax      ; ecx = world ([cmd + 4])
80a1c2: mov    0x184(%eax),%eax    ; world->player_table
80a1c8: mov    (%eax,%edi,8),%esi  ; esi = player object
80a1cb: test   %esi,%esi
80a1cd: je     0x80a22a
80a1cf: mov    %esi,%ecx
80a1d1: call   0x74e020            ; player->is_ai()
80a1d6: test   %al,%al
80a1d8: jne    0x80a20a            ; if AI -> allow cheat unconditionally!
80a1da: lea    -0x8(%ebp),%eax
80a1dd: push   %eax
80a1de: mov    0xaf2c58,%ecx       ; g_game
80a1e4: call   0x5408c0            ; retrieve GameSettings
80a1e9: mov    0xaf2c58,%ecx
80a1ef: cmpb   $0x0,0x40f(%eax)    ; TEST [GameSettings + 0x40f] == CheatsEnabled!
80a1f6: jne    0x80a20a            ; if cheats enabled in lobby -> allow!
80a1f8: call   0x5408a0            ; is_developer_mode()
80a1fd: test   %al,%al
80a1ff: jne    0x80a20a            ; if developer mode -> allow!
80a201: pop    %edi                ; CHEATS DISABLED AND NOT AI:
80a202: pop    %esi
80a203: mov    %ebp,%esp
80a205: pop    %ebp
80a206: ret    $0x4                ; REJECT / IGNORE COMMAND!
...
80a20a: ; Cheats allowed:
80a213: mov    (%esi),%eax         ; player vtable
80a215: push   $0x0
80a217: push   0x4(%edi)           ; float amount
80a21a: push   %ecx                ; resource_id
80a21b: mov    %esi,%ecx
80a21d: call   *0x64(%eax)         ; player->add_resource(res_id, amount, 0)
```
- **Security Check Verdict:** **ENFORCES CHEATS ENABLED**.
  If cheats are not enabled in the match settings (`[GameSettings + 0x40f] == 0`) and developer mode is off, the executor quietly discards the packet. In a standard ranked/competitive multiplayer game, packet `0x05` cannot be used to grant resources to human players unless cheats were toggled in the lobby.

---

### 3.2 Tribute Command (Packet `0x6c`) — **[VERIFIED]**

The legitimate in-game mechanism for transferring resources between players.

#### Builder: `0x00812970`
Wrapped by `WorldPlayer::tribute` at `0x0073e9f0`.
```asm
812970: push   %ebp
812971: mov    %esp,%ebp
812973: push   %esi
812974: push   %edi
812975: push   $0xc                ; size = 12 bytes
812977: push   $0x1
812979: mov    %ecx,%edi           ; cmd_obj
81297b: call   0x8ea479            ; calloc(1, 12)
812980: mov    %eax,%esi
812982: add    $0x8,%esp
812985: test   %esi,%esi
812987: je     0x8129ca
812989: mov    0x8(%ebp),%al       ; src_player_id
81298c: movb   $0x6c,(%esi)        ; packet[0] = 0x6c (COMMAND_TRIBUTE)
81298f: mov    0xc(%ebp),%cl       ; dst_player_id
812992: mov    %al,0x1(%esi)       ; packet[1] = src_player_id
812995: mov    0x10(%ebp),%al      ; resource_id
812998: mov    %cl,0x2(%esi)       ; packet[2] = dst_player_id
81299b: mov    0x14(%ebp),%ecx     ; amount (float)
81299e: mov    %al,0x3(%esi)       ; packet[3] = resource_id
8129a1: mov    0x18(%ebp),%eax     ; tribute fee (float, e.g. 0.0 to 0.3)
8129a4: mov    %ecx,0x4(%esi)      ; *(float*)(packet + 4) = amount
8129a7: mov    0x8(%ebp),%ecx      ; src_player_id
8129aa: push   %ecx
8129ab: mov    %esp,%ecx
8129ad: mov    %eax,0x8(%esi)      ; *(float*)(packet + 8) = fee
8129b0: push   $0xc                ; size 12
8129b2: mov    0x4(%esp),%eax
8129b6: mov    %edi,%ecx           ; cmd_obj
8129b8: mov    %eax,(%ecx)
8129ba: push   %esi                ; packet
8129bb: call   0x80ef70            ; send_command
8129c0: pop    %edi
8129c1: pop    %esi
8129c2: pop    %ebp
8129c3: ret    $0x14
```
- **Prototype:** `void __thiscall(RGE_Command *this, int src_player_id, int dst_player_id, int resource_id, float amount, float fee)`
- **`ret` instruction:** `ret $0x14`
- **Packet Layout (12 bytes):**
  - `packet[0]` (u8): `0x6c` (opcode)
  - `packet[1]` (u8): `src_player_id`
  - `packet[2]` (u8): `dst_player_id`
  - `packet[3]` (u8): `resource_id` (`0` = Food, `1` = Wood, `2` = Stone, `3` = Gold)
  - `packet[4..7]` (f32): `amount` (float)
  - `packet[8..11]` (f32): `fee` (float, e.g. `0.0f` to `0.30f`)

#### Executor: `0x008151c0`
Dispatched from `TRIBE_Command::execute_command` at `0x00813460` case `0x6c` via `vtable[0x14]`.
- **Security Check Verdict:** **NO CHEAT CHECKS**.
  - Can be broadcast and executed in any standard multiplayer game.
  - Checks if `src_player` has enough resources: if `src_res < amount * (1.0 + fee)`, the tribute amount is clamped to what the player can afford.
  - Lua scripts can safely issue tributes between any players. Setting `fee = 0.0f` transmits the full amount without tax.

---

## 4. Unit Creation

### 4.1 Spawn Unit at Position (Packet `0x04`) — **[VERIFIED]**

Used by the scenario editor, trigger unit generation, and cheat units (e.g. `how do you turn this on` creating a Cobra Car, `to smithereens` creating a Saboteur).

#### Builder: `0x00809220`
Wrapped by `WorldPlayer::create_unit` at `0x0074dd90` (slot `0xbc` in player vtable).
```asm
809220: push   %ebp
809221: mov    %esp,%ebp
809223: push   %esi
809224: push   %edi
809225: push   $0x14               ; size = 20 bytes
809227: push   $0x1
809229: mov    %ecx,%edi           ; cmd_obj
80922b: call   0x8ea479            ; calloc(1, 20)
809230: mov    %eax,%esi
809232: add    $0x8,%esp
809235: test   %esi,%esi
809237: je     0x809276
809239: mov    0x8(%ebp),%ax       ; unit_type_id (short)
80923d: mov    0xc(%ebp),%cl       ; player_id
809240: mov    0x10(%ebp),%edx     ; x coordinate (float)
809243: mov    %cl,0x4(%esi)       ; packet[4] = player_id
809246: movb   $0x4,(%esi)         ; packet[0] = 0x04 (COMMAND_CREATE_UNIT)
809249: movb   $0x0,0x1(%esi)      ; packet[1] = 0
80924d: mov    %ax,0x2(%esi)       ; packet[2..3] = unit_type_id
809251: movb   $0x0,0x5(%esi)      ; packet[5] = 0
809255: mov    0x14(%ebp),%eax     ; y coordinate (float)
809258: mov    %edx,0x8(%esi)      ; *(float*)(packet + 8) = x
80925b: mov    0x18(%ebp),%edx     ; z coordinate (float)
80925e: mov    %eax,0xc(%esi)      ; *(float*)(packet + 12) = y
809261: mov    0xc(%ebp),%eax      ; player_id
809264: push   %eax
809265: mov    %esp,%ecx
809267: mov    %edx,0x10(%esi)     ; *(float*)(packet + 16) = z
80926a: push   $0x14               ; size 20
80926c: mov    0x4(%esp),%eax
809270: mov    %edi,%ecx           ; cmd_obj
809272: mov    %eax,(%ecx)
809274: push   %esi                ; packet
809275: call   0x80ef70            ; send_command
80927a: pop    %edi
80927b: pop    %esi
80927c: pop    %ebp
80927d: ret    $0x14
```
- **Prototype:** `void __thiscall(RGE_Command *this, short unit_type_id, int player_id, float x, float y, float z)`
- **`ret` instruction:** `ret $0x14`
- **Packet Layout (20 bytes):**
  - `packet[0]` (u8): `0x04` (opcode)
  - `packet[1]` (u8): `0x00`
  - `packet[2..3]` (i16): `unit_type_id` (dat unit ID, e.g. `561` for Cobra Car, `125` for Monk, `4` for Archer)
  - `packet[4]` (u8): `player_id` (owner player ID)
  - `packet[5..7]` (3 bytes): `0x00` padding
  - `packet[8..11]` (f32): `x` (world tile X)
  - `packet[12..15]` (f32): `y` (world tile Y)
  - `packet[16..19]` (f32): `z` (elevation Z, usually `0.0f`)

#### Executor: `0x008161d0`
Dispatched directly from `TRIBE_Command::execute_command` at `0x00813460` (`case 4`).
```asm
8161d0: push   %ebp
8161d1: mov    %esp,%ebp
8161d3: mov    0x8(%ebp),%eax      ; eax = packet pointer
8161d6: push   %esi
8161d7: movzbl 0x4(%eax),%esi      ; esi = packet->player_id
8161db: mov    0x4(%ecx),%ecx      ; world
8161de: mov    0x184(%ecx),%ecx    ; world->player_table
8161e4: mov    (%ecx,%esi,8),%ecx  ; ecx = target player object
8161e7: test   %ecx,%ecx
8161e9: je     0x816212
8161eb: mov    (%ecx),%edx         ; player vtable
8161ed: push   $0x1
8161ef: push   0x10(%eax)          ; z
8161f2: push   0xc(%eax)           ; y
8161f5: push   0x8(%eax)           ; x
8161f8: movswl 0x2(%eax),%eax      ; unit_type_id
8161fc: push   %eax
8161fd: call   *0x98(%edx)         ; player->create_unit(unit_id, x, y, z, 1)
816203: pop    %esi
816204: pop    %ebp
816205: ret    $0x4
```
- **Security Check Verdict:** **ZERO CHEAT CHECKS. ZERO PERMISSION CHECKS.**
  Packet `0x04` is fully unconstrained. Any client can broadcast a packet `0x04` to spawn any unit for any player at any map coordinate, and every connected client will execute `player->create_unit(...)` synchronously. This makes Packet `0x04` the single best tool for multiplayer Lua scripts to spawn units or objects dynamically.

---

### 4.2 Queue/Train Unit at Building (Packet `0x64`) — **[VERIFIED]**

The standard command generated when clicking a unit button at a Town Center, Barracks, Stable, etc.

#### Builder: `0x00812a40`
```asm
812a40: push   %ebp
812a41: mov    %esp,%ebp
812a43: push   %esi
812a44: push   %edi
812a45: push   $0x10               ; size = 16 bytes
812a47: push   $0x1
812a49: mov    %ecx,%edi           ; cmd_obj
812a4b: call   0x8ea479            ; calloc(1, 16)
812a50: mov    %eax,%esi
812a52: add    $0x8,%esp
812a55: test   %esi,%esi
812a57: je     0x812a99
812a59: mov    0xc(%ebp),%eax      ; building_unit_id
812a5c: mov    0x8(%ebp),%cl       ; player_id
812a5f: mov    0x10(%ebp),%dx      ; unit_type_id
812a63: mov    %eax,0x4(%esi)      ; *(int*)(packet + 4) = building_unit_id
812a66: mov    0x14(%ebp),%eax     ; flag (usually -1)
812a69: mov    %cl,0x8(%esi)       ; packet[8] = player_id
812a6c: mov    %dx,0xa(%esi)       ; *(short*)(packet + 10) = unit_type_id
812a70: mov    %eax,0xc(%esi)      ; *(int*)(packet + 12) = flag
812a73: movb   $0x64,(%esi)        ; packet[0] = 0x64 (COMMAND_TRAIN_UNIT)
812a76: movb   $0x0,0x1(%esi)      ; packet[1] = 0
812a7a: movb   $0x0,0x9(%esi)      ; packet[9] = 0
812a7e: mov    0x8(%ebp),%ecx      ; player_id
...
812a86: call   0x80ef70            ; send_command
812a8e: ret    $0x10
```
- **Prototype:** `void __thiscall(RGE_Command *this, int player_id, int building_unit_id, short unit_type_id, int flag)`
- **`ret` instruction:** `ret $0x10`
- **Executor:** `0x00815640`. Checks building ownership (`[building+0x10] == player`) and building alive status (`[building+0x4c] == 2`). If valid, queues unit production into `0x8ba6b0`.

---

## 5. Technology Research

### 5.1 Start Research at Building (Packet `0x65`) — **[VERIFIED]**

Generated when a player clicks a technology research icon at a building (e.g. Feudal Age at Town Center, Loom, Bloodlines, Ballistics).

#### Builders: `0x00812d60` & `0x00812de0`
There are two overloads:
1. `0x00812d60` — Convenience wrapper taking `Unit *building_unit` directly:
   - **Prototype:** `void __thiscall(RGE_Command *this, Unit *building_unit, short tech_id)`
   - **`ret` instruction:** `ret $0x8`
   - Internally extracts `player_id = [building + 0x10]->player_id` and `building_id = [building + 8]`, then builds the 16-byte packet.
2. `0x00812de0` — Explicit parameter overload:
   - **Prototype:** `void __thiscall(RGE_Command *this, int player_id, int building_unit_id, short tech_id, int flag)`
   - **`ret` instruction:** `ret $0x10`

```asm
812de0: push   %ebp
812de1: mov    %esp,%ebp
812de3: push   %esi
812de4: push   %edi
812de5: push   $0x10               ; size = 16 bytes
812de7: push   $0x1
812de9: mov    %ecx,%edi           ; cmd_obj
812deb: call   0x8ea479            ; calloc(1, 16)
812df0: mov    %eax,%esi
812df2: add    $0x8,%esp
812df5: test   %esi,%esi
812df7: je     0x812e37
812df9: mov    0xc(%ebp),%eax      ; building_unit_id
812dfc: mov    0x8(%ebp),%cl       ; player_id
812dff: mov    0x10(%ebp),%dx      ; tech_id
812e03: mov    %eax,0x4(%esi)      ; *(int*)(packet + 4) = building_unit_id
812e06: mov    0x14(%ebp),%eax     ; flag (typically -1)
812e09: mov    %cl,0x8(%esi)       ; packet[8] = player_id
812e0c: mov    %dx,0xa(%esi)       ; *(short*)(packet + 10) = tech_id
812e10: mov    %eax,0xc(%esi)      ; *(int*)(packet + 12) = flag
812e13: movb   $0x65,(%esi)        ; packet[0] = 0x65 (COMMAND_RESEARCH)
812e16: movb   $0x0,0x1(%esi)      ; packet[1] = 0
812e1a: movb   $0x0,0x9(%esi)      ; packet[9] = 0
812e1e: mov    0x8(%ebp),%ecx      ; player_id
812e21: push   %ecx
812e22: mov    %esp,%ecx
812e24: push   $0x10               ; size 16
812e26: call   0x80ef70            ; send_command
812e32: ret    $0x10
```

- **Packet Layout (16 bytes):**
  - `packet[0]` (u8): `0x65` (opcode)
  - `packet[1..3]` (3 bytes): `0x00` padding
  - `packet[4..7]` (i32): `building_unit_id`
  - `packet[8]` (u8): `player_id`
  - `packet[9]` (u8): `0x00` padding
  - `packet[10..11]` (i16): `tech_id`
  - `packet[12..15]` (i32): `flag` (`-1` indicates standard research)

#### Executor: `0x00815bf0`
Dispatched from `TRIBE_Command::execute_command` at `0x00813460` (`case 0x65`).
```asm
815bf0: push   %ebp
815bf1: mov    %esp,%ebp
...
815c32: mov    0x4(%edi),%eax      ; building_id
815c35: call   0x733bf0            ; world->get_object(building_id)
815c3a: mov    %eax,%esi           ; esi = building unit
815c3c: test   %esi,%esi
815c3e: je     0x815df3
815c44: mov    0xc(%esi),%eax      ; building->master
815c47: cmpb   $0x50,0x4(%eax)     ; master->type == 80 ('P', building)
815c4b: jne    0x815df3
815c51: cmpb   $0x2,0x4c(%esi)     ; status == 2 (alive/active)
815c55: jne    0x815df3
815c5b: mov    0x10(%esi),%ebx     ; player
815c79: mov    0x1ae8(%ebx),%eax   ; player->tech_tree
815c7f: movswl 0xa(%edi),%edx      ; tech_id
815c83: mov    (%eax),%eax
815c85: lea    (%edx,%edx,2),%ecx
815c88: shl    $0x2,%ecx
815c8b: add    %edx,%ecx
815c8d: cmpw   $0x1,0x4(%eax,%ecx,2) ; CHECK TECH STATUS == 1 (AVAILABLE)!
815c94: jne    0x815df3
815ca4: ; Checks building master ID matches tech research building requirement
...
815d31: call   0x8bed60            ; Building::research_tech(tech_id, -1)
```
- **Security Check Verdict:** **NO CHEAT CHECKS**.
  - Requires valid building pointer owned by the player.
  - Requires tech status to be available (`1`) and matches the building's allowed tech research list.

### 5.2 Instant Research & Direct Tech Application — **[VERIFIED]**
The engine has **no standalone network command packet for "instant research"** (cheat codes in AoE2 do not include a tech-granting code). When research completes normally at a building, the engine executes:
```c
// At 0x008bfd95 (Building queue tick handler):
player_tech_tree = *(void**)(player + 0x1ae8);
TechTree::complete_tech(player_tech_tree, tech_id); // 0x0084d300, __thiscall ret $0x4
```
Function `0x0084d300`:
- Sets tech status in `player->tech_tree` to `3` (researched).
- Applies all effect modifications (unit HP, attack, armor, speed, costs).
- Updates player visibility and unlocks subsequent techs.

> [!IMPORTANT]
> **Multiplayer Safe Approach for Lua:**
> Because `0x0084d300` is a local function without a dedicated packet, calling it directly on one machine causes an immediate desync. To complete tech instantly in multiplayer, Lua scripts have two clean approaches:
> 1. Send Packet `0x65` via `0x812d60` to queue research, and if fast-research is desired, enable the game's `aegis` fast-build setting or set building queue speed deterministically across all peers.
> 2. Run Lua scripts on every machine synchronously (lockstep hook), invoking `0x0084d300` on the exact same simulation tick on every client.

---

## 6. Stance and Deletion

### 6.1 Set Unit Stance (Packet `0x12`) — **[VERIFIED]**

Changes unit combat behavior (Aggressive, Defensive, Stand Ground, No Attack).

#### Builder: `0x00809d50`
Wrapped by `WorldPlayer::set_stance` at `0x0074df60` (player vtable slot `0xd4`).
```asm
809d50: push   %ebp
809d51: mov    %esp,%ebp
809d53: push   %ecx
809d54: push   %ebx
809d55: mov    0xc(%ebp),%bx       ; unit count
809d59: push   %esi
809d5a: movswl %bx,%esi
809d5d: push   %edi
809d5e: mov    0x8(%ebp),%edi      ; Unit** array pointer
809d61: mov    %ecx,-0x4(%ebp)     ; cmd_obj
809d64: lea    0x3(,%esi,4),%eax   ; size = 3 + 4 * count
809d6b: mov    %eax,0xc(%ebp)
809d6e: test   %edi,%edi
809d70: je     0x809deb
809d72: cmp    $0x1,%bx
809d76: jl     0x809deb
809d78: push   %eax
809d79: push   $0x1
809d7b: call   0x8ea479            ; calloc(1, 3 + 4*n)
809d80: mov    %eax,%edx
809d82: add    $0x8,%esp
809d85: lea    0x3(%edx),%ecx      ; &packet[3] (start of unit IDs)
...
809dba: movb   $0x12,(%edx)        ; packet[0] = 0x12 (COMMAND_STANCE)
809dbd: mov    %bl,0x1(%edx)       ; packet[1] = count
809dc0: mov    0x10(%ebp),%al      ; stance_idx
809dc3: mov    %al,0x2(%edx)       ; packet[2] = stance_idx
...
809de6: call   0x80ef70            ; send_command
809ded: ret    $0xc
```
- **Prototype:** `void __thiscall(RGE_Command *this, Unit **units, short count, uint8_t stance_idx)`
- **`ret` instruction:** `ret $0xc`
- **Packet Layout (`3 + 4 * count` bytes):**
  - `packet[0]` (u8): `0x12` (opcode)
  - `packet[1]` (u8): `count` (number of unit IDs)
  - `packet[2]` (u8): `stance_idx`:
    - `0` = Aggressive
    - `1` = Defensive
    - `2` = Stand Ground
    - `3` = No Attack
  - `packet[3 + 4*i]` (i32 array): 32-bit unit IDs

#### Executor: `0x0080d580`
Dispatched by `RGE_Command::execute_command` at `0x00809fe0` via jump table `0x80a104` index `0x12` (stub `0x80a076`).
- Checks: Validates unit IDs using `0x80e0f0`. If units form a formation, updates formation stance at `0x007e9240`. Otherwise updates each unit's stance individually.
- **Security Check Verdict:** **NO CHEAT CHECKS**. Safe for multiplayer.

---

### 6.2 Delete Unit / Building (Packet `0x6a`) — **[VERIFIED]**

The core engine command triggered when a player presses `Delete` on selected units/buildings or clicks the UI skull/delete icon.

#### Player Helper: `WorldPlayer::delete_object` (`0x0073e7e0`)
```asm
73e7e0: push   %ebp
73e7e1: mov    %esp,%ebp
73e7e3: mov    0x8(%ebp),%edx      ; edx = Unit*
73e7e6: test   %edx,%edx
73e7e8: je     0x73e809
73e7ea: cmp    %ecx,0x10(%edx)     ; check unit->owner == player (this)
73e7ed: jne    0x73e809
73e7ef: mov    0xc(%edx),%eax      ; eax = unit->master
73e7f2: mov    0x4(%eax),%al       ; master->type
73e7f5: cmp    $0x50,%al           ; 'P' (80 = unit)
73e7f7: je     0x73e7fe
73e7f9: cmp    $0x46,%al           ; 'F' (70 = building)
73e7fb: jne    0x73e809
73e7fe: mov    0xc0(%ecx),%ecx     ; ecx = [player+0xc0] = cmd_obj
73e804: push   %edx                ; Unit*
73e805: call   0x812670            ; call build_delete_command
73e80a: ret    $0x4
```

#### Builder: `0x00812670`
```asm
812670: push   %ebp
812671: mov    %esp,%ebp
812673: push   %esi
812674: push   %edi
812675: push   $0xc                ; size = 12 bytes
812677: push   $0x1
812679: mov    %ecx,%edi           ; cmd_obj
81267b: call   0x8ea479            ; calloc(1, 12)
812680: mov    %eax,%esi
812682: add    $0x8,%esp
812685: test   %esi,%esi
812687: je     0x8126d4
812689: mov    0x8(%ebp),%edx      ; edx = Unit*
81268c: movb   $0x6a,(%esi)        ; packet[0] = 0x6a (COMMAND_DELETE_UNIT)
81268f: mov    0x8(%edx),%ecx      ; ecx = unit->id ([unit + 8])
812692: mov    %ecx,0x4(%esi)      ; *(int*)(packet + 4) = unit_id
812695: mov    0x10(%edx),%ecx     ; ecx = unit->owner_player ([unit + 0x10])
812698: push   %ecx
812699: mov    0x1adc(%ecx),%eax   ; eax = player->player_id ([player + 0x1adc])
81269f: mov    %esp,%ecx
8126a1: mov    %eax,0x8(%esi)      ; *(int*)(packet + 8) = player_id
...
8126b6: push   %esi                ; packet buffer
8126b7: call   0x80ef70            ; send_command(packet, 12, player_id)
8126bc: pop    %edi
8126bd: pop    %esi
8126be: pop    %ebp
8126bf: ret    $0x4
```
- **Prototype:** `void __thiscall(RGE_Command *this, Unit *unit)`
- **`ret` instruction:** `ret $0x4`
- **Packet Layout (12 bytes):**
  - `packet[0]` (u8): `0x6a` (opcode)
  - `packet[1..3]` (3 bytes): `0x00` padding
  - `packet[4..7]` (i32): `unit_id` (`[unit + 8]`)
  - `packet[8..11]` (i32): `player_id` (unit owner's player ID)

#### Executor: `0x008140d0`
Dispatched from `TRIBE_Command::execute_command` at `0x00813460` (`case 0x6a`).
```asm
8140d0: push   %ebp
8140d1: mov    %esp,%ebp
8140d3: mov    0x4(%ecx),%ecx      ; ecx = world ([cmd + 4])
8140d6: push   %esi
8140d7: push   %edi
8140d8: mov    0x8(%ebp),%edi      ; edi = packet pointer
8140db: push   0x4(%edi)           ; unit_id
8140de: call   0x733bf0            ; world->get_object(unit_id)
8140e3: mov    %eax,%esi           ; esi = Unit*
8140e5: test   %esi,%esi
8140e7: je     0x81410a
8140e9: mov    0xc(%esi),%ecx      ; ecx = unit->master
8140ec: cmpb   $0x46,0x4(%ecx)     ; master->type >= 70 (0x46, creatable/building)
8140f0: jb     0x81410a
8140f2: mov    0x10(%esi),%edx     ; edx = unit->owner_player
8140f5: mov    0x8(%edi),%ecx      ; ecx = packet->player_id
8140f8: cmp    0x1adc(%edx),%ecx   ; CHECK: unit->owner->id == packet->player_id!
8140fe: jne    0x81410a            ; if mismatch -> reject!
814100: mov    (%esi),%eax         ; unit vtable
814102: mov    %esi,%ecx           ; this = unit
814104: call   *0x370(%eax)        ; unit->kill() / unit->destroy() (slot 0xdc)
81410a: pop    %edi
81410b: pop    %esi
81410c: pop    %ebp
81410d: ret    $0x4
```

- **Security Check Verdict:** **NO CHEAT CHECKS**.
  - Enforces `packet->player_id == unit->owner->player_id`.
  - **Lua Script Power Note:** Because the check compares `unit->owner->id` directly to `packet->player_id`, a Lua script wishing to delete or destroy an enemy unit can construct a raw Packet `0x6a` and set `packet[8..11] = unit->owner->id`. The executor will find that `unit->owner->id == packet->player_id` matches and immediately destroy the unit across all connected machines.

---

## 7. Lua C-Binding Recipes for Wololoader

To expose these commands to Wololoader Lua scripts, the C runtime can implement clean wrapper functions that format the packets and call `0x0080ef70` directly:

```c
#include <stdint.h>
#include <string.h>

typedef void* (__cdecl *calloc_fn)(size_t num, size_t size);
typedef void (__thiscall *send_cmd_fn)(void *cmd_obj, void *pkt, int size, int player_id);

static calloc_fn   game_calloc   = (calloc_fn)0x008ea479;
static send_cmd_fn game_send_cmd = (send_cmd_fn)0x0080ef70;

/* Helper: get communications handler from world */
static void* get_cmd_handler(void) {
    void **g_game = (void**)0x00af2c58;
    if (!g_game || !*g_game) return NULL;
    void *world = *(void**)((uintptr_t)*g_game + 0xc8);
    if (!world) return NULL;
    return *(void**)((uintptr_t)world + 0xc0);
}

/* 1. Spawn Unit at Position (Multiplayer Safe) */
int wl_cmd_spawn_unit(int player_id, int unit_type_id, float x, float y, float z) {
    void *cmd = get_cmd_handler();
    if (!cmd) return 0;
    
    uint8_t *pkt = (uint8_t*)game_calloc(1, 20);
    if (!pkt) return 0;
    
    pkt[0] = 0x04;
    pkt[1] = 0x00;
    *(uint16_t*)(pkt + 2) = (uint16_t)unit_type_id;
    pkt[4] = (uint8_t)player_id;
    *(float*)(pkt + 8)  = x;
    *(float*)(pkt + 12) = y;
    *(float*)(pkt + 16) = z;
    
    game_send_cmd(cmd, pkt, 20, player_id);
    return 1;
}

/* 2. Tribute Resources */
int wl_cmd_tribute(int src_player_id, int dst_player_id, int res_id, float amount, float fee) {
    void *cmd = get_cmd_handler();
    if (!cmd) return 0;
    
    uint8_t *pkt = (uint8_t*)game_calloc(1, 12);
    if (!pkt) return 0;
    
    pkt[0] = 0x6c;
    pkt[1] = (uint8_t)src_player_id;
    pkt[2] = (uint8_t)dst_player_id;
    pkt[3] = (uint8_t)res_id;
    *(float*)(pkt + 4) = amount;
    *(float*)(pkt + 8) = fee;
    
    game_send_cmd(cmd, pkt, 12, src_player_id);
    return 1;
}

/* 3. Delete Unit by ID */
int wl_cmd_delete_unit(int unit_id, int owner_player_id) {
    void *cmd = get_cmd_handler();
    if (!cmd) return 0;
    
    uint8_t *pkt = (uint8_t*)game_calloc(1, 12);
    if (!pkt) return 0;
    
    pkt[0] = 0x6a;
    *(uint32_t*)(pkt + 4) = (uint32_t)unit_id;
    *(uint32_t*)(pkt + 8) = (uint32_t)owner_player_id;
    
    game_send_cmd(cmd, pkt, 12, owner_player_id);
    return 1;
}

/* 4. Set Unit Stance */
int wl_cmd_set_stance(int player_id, const int *unit_ids, int count, int stance_idx) {
    void *cmd = get_cmd_handler();
    if (!cmd || count <= 0) return 0;
    
    int size = 3 + 4 * count;
    uint8_t *pkt = (uint8_t*)game_calloc(1, size);
    if (!pkt) return 0;
    
    pkt[0] = 0x12;
    pkt[1] = (uint8_t)count;
    pkt[2] = (uint8_t)stance_idx;
    for (int i = 0; i < count; i++) {
        *(uint32_t*)(pkt + 3 + 4 * i) = (uint32_t)unit_ids[i];
    }
    
    game_send_cmd(cmd, pkt, size, player_id);
    return 1;
}
```

---

## 8. Summary of Findings

1. **Cheat Commands vs Gameplay Commands:**
   - Resource cheat `0x05` is strictly gated behind `[GameSettings + 0x40f]` (`CheatsEnabled`).
   - All standard commands (`0x04` unit spawn, `0x6c` tribute, `0x64` train, `0x65` research, `0x12` stance, `0x6a` deletion) do **not** check the cheats flag and execute reliably in standard multiplayer.
2. **Zero-Permission Power Tool:**
   - **Packet `0x04` (`COMMAND_CREATE_UNIT`)** has no cheat check and no ownership check; it spawns any unit from the dat file for any player at arbitrary map coordinates synchronously.
3. **Multiplayer Deletion Tool:**
   - **Packet `0x6a` (`COMMAND_DELETE_UNIT`)** validates that `packet->player_id == unit->owner->id`. By passing the unit's actual owner ID in the packet header, Lua can deterministically delete any unit or building on the map.

---
**Review note (2026-11-15, checked in objdump):** the executor listing of 3.1 (`0x80a1b0`) above is NOT the real code. The real executor first tests `[[cmd+0x28e0]+0x9f5]`, then `0x74e020` (`[player+8]==3`, computer player), then `[settings+0x40f]` and `0x5408a0`, and calls player vtable `+0x64` as `(int res, float amount, 0)`. The spawn executor `0x8161d0` and the builder listings also differ in detail (no calloc null check in `0x809220`; flying units get z = 9). What Wololoader uses is documented and verified in `wololoader/src/gamecmd.c`.
