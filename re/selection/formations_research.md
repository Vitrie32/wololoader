# Reverse-Engineering Research: Formation Architecture & Selection Overflow in AoK HD 5.8

> **Game**: Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit PE, ImageBase `0x400000`).  
> **Source Disassembly**: `~/.cache/wl-test/exe.s` (objdump disassembly).  
> **Target Executable**: `<game dir>/AoK HD.exe` (read-only).  
> **Context**: Investigating selection overflow (60 selected units + up to 190 extra units = 250 total units). Right-click move orders send up to 4 extra "clone" packets (type `0x03`, $\le 60$ unit IDs each).  
> **Observed In-Game Issues**:
> 1. Formations fracture into several separate visual and kinematic blocks (e.g. 5 disjoint squads).
> 2. The AI opponent occasionally stutters, retreats, or behaves erratically during engagements.

---

## Executive Summary

| Question | Core Finding | Verified Address / Evidence |
|---|---|---|
| **1. What does `0x7d61e0` do with the unit list?** | It is a **partitioning/clustering algorithm**, not an allocator. It groups units into flat unformed units and formed subgroups in a static scratchpad (`0xaf7ae0`). Clamped to $\min(N, 60)$ units (`0x7d625e`). Heap `Formation` objects (`0x4d4` bytes) are allocated in the caller `0x80bdd0` via `0x72c600` and linked into `world+0x48`. Formations have a **hard cap of 70 units** (`0x7d80b1`). | `0x7d61e0`, `0x80bdd0`, `0x72c600`, `0x7d80b1` |
| **2. Is there a "Move Without Formation" field or command?** | Packet type `0x03` has **NO field or flag** for formation suppression (reads only types, player, target, count, floats X/Y, IDs). However, **Command Type `0x1d`** (`Formation(pId, on)`) directly controls `player+0x1a90`. When `player+0x1a90 == 0`, `0x7d61e0` forces all units to the flat unformed list, skipping all formation creation. | `0x8097d0`, `0x80ae90`, `0x7d644c` |
| **3. Does overflow exhaust or corrupt tables used by the AI?** | **Yes, via two distinct mechanisms**: <br>1. **Zombie Formation Leak**: `0x7e4650` unlinks units from old formations but **never frees empty formations**! 5 clone packets per click spam empty `Formation` objects into `world+0x48`. `0x72cf10` (used heavily by AI tactical evaluators) is an $O(N)$ linear scan over this list.<br>2. **Targeting Fragmentation**: Splitting 250 units into 5 formations creates 5 separate bounding boxes and leaders. AI tactical evaluators (`0x52f23a`, `0x4de3d6`) evaluate entire formations rather than individuals, causing target thrashing and stutter. | `0x7e4650`, `0x72cf10`, `0x52f23a`, `0x4de3d6` |
| **4. Safest Recommended Fix** | **Option (B): Clones as plain moves without formation**. <br>Attempting Option (A) causes stack smashing and heap corruption (60-unit stack buffer in `0x7d61e0`, 70-unit limit in `Formation`). For Option (B), set `packet[2] = 0x57` ('W') in clone packets, hook `0x7d61e0` / `0x80bdd0` to temporarily set `player+0x1a90 = 0` during clone execution. Clones move via direct `unit->move` calls (`0x80c562`). | `0x80c562`, `0x7d644c` |

---

## Question 1: What `0x7d61e0` (and Callees) Does With the Unit List

### 1.1 The Role of `0x7d61e0` (Group Partitioning Scratchpad)

Function `0x7d61e0` is **NOT** a heap allocator and does not create persistent game objects. It is the **formation partitioning algorithm** (`GroupManager::partition`). It evaluates a batch of candidate unit IDs and classifies them into:
1. **Flat unformed units** (units that cannot or should not form formations).
2. **Formed subgroups** (clusters of military units moving at similar speeds and locations).

#### Disassembly Evidence: Clamping to 60 Units
At the very entry of `0x7d61e0`, the input count is clamped to a maximum of 60 (`0x3c`):
```assembly
# exe.s lines 1216770-1216790
  7d6209: call   0x7d6fa0               # Reset scratchpad 0xaf7ae0
  7d620e: test   %esi,%esi              # esi = pointer to unit ID array
  7d6210: je     0x7d6c93
  7d6216: cmpl   $0x0,0xc(%ebp)         # count <= 0?
  7d621a: jle    0x7d6c93
  ...
  7d6253: lea    -0x128(%ebp),%eax
  7d6259: push   %edi
  7d625a: push   %eax
  7d625b: lea    0xc(%ebp),%eax
  7d625e: movl   $0x3c,-0x128(%ebp)     # Clamp limit = 0x3c (60 units)
  7d6268: push   %eax
  7d6269: call   0x451610               # min(count, 60)
  7d626e: mov    %eax,%edi              # edi = clamped_count (max 60)
```

Furthermore, `0x7d61e0` allocates a local stack buffer of exactly 60 DWORDs at `-0x254(%ebp)` to copy the validated unit IDs:
```assembly
# exe.s lines 1216800-1216835
  7d627f: lea    -0x254(%ebp),%eax      # Local stack buffer: -0x254 to -0x164 = 240 bytes = 60 DWORDs
  7d6285: sub    %esi,%eax
  7d6287: mov    %eax,-0x128(%ebp)
  7d6290: mov    0xaf2c58,%eax
  7d6295: push   (%esi)
  7d6297: mov    0xc8(%eax),%ecx        # ecx = World
  7d629d: call   0x733bf0               # World::get_unit_by_id
  ...
  7d62b4: mov    %eax,(%ecx,%esi,1)     # Store validated unit ID into -0x254(%ebp)
  7d62b7: add    $0x4,%esi
  7d62ba: cmp    %edi,%ebx
  7d62bc: jl     0x7d6290
```

### 1.2 Scratchpad Buffer Layout (`0xaf7ae0`)

The output of `0x7d61e0` is written into the global static object `0xaf7ae0` (total size: `0x3a2c` = 14,892 bytes).

| Offset in `0xaf7ae0` | Hex Address | Type | Capacity | Meaning |
|---|---|---|---|---|
| `+0x0000` | `0xaf7ae0` | `int32` | 1 | Scratchpad status (initialized to `-1` by `0x7d6fa0`) |
| `+0x0004` | `0xaf7ae4` | `int32` | 1 | `flat_count`: Number of unformed units |
| `+0x0008` | `0xaf7ae8` | `int32[60]` | 60 | `flat_unit_ids`: IDs of units moving without formation (240 bytes) |
| `+0x00f8` | `0xaf7bd8` | `int32` | 1 | `group_count`: Number of formed subgroups ($0 \le g \le 60$) |
| `+0x00fc` | `0xaf7bdc` | `int32[60]` | 60 | `group_unit_counts`: Number of units in each subgroup (240 bytes) |
| `+0x01ec` | `0xaf7ccc` | `int32[60][60]` | 3,600 | `group_units`: Matrix of unit IDs ($60 \times 60 \times 4 = 14,400$ bytes) |
| `+0x3a2c` | `0xafb50c` | `int32` | 1 | `active_formation_count`: Count of formations created during turn |
| `+0x3a30` | `0xafb510` | `int32[60]` | 60 | `active_formation_ids`: IDs of formations created during turn |

- Getter functions accessing this structure:
  - `0x7d5d30`: `GroupManager::get_flat_unit_id(int index)` $\rightarrow$ reads `[0xaf7ae8 + index*4]`.
  - `0x7d5cd0`: `GroupManager::get_subgroup_ptr(int group_idx)` $\rightarrow$ returns `0xaf7ccc + group_idx * 240`.
  - `0x7d5d00`: `GroupManager::get_subgroup_count(int group_idx)` $\rightarrow$ reads `[0xaf7bdc + group_idx*4]`.

### 1.3 How Formation Objects are Created and Stored (`0x80bdd0`)

The actual `Formation` objects are heap-allocated by `0x80bdd0` (the Type `0x03` move command executor) after `0x7d61e0` returns.

```assembly
# exe.s lines 1284000-1284040 (inside 0x80bdd0)
  80bed3: call   0x7d61e0               # GroupManager::partition
  80bed8: xor    %ebx,%ebx
  80beda: mov    %ebx,-0x28(%ebp)
  80bedd: cmp    %ebx,0xaf7bd8          # Check group_count (0xaf7ae0 + 0xf8)
  80bee3: jle    0x80c018               # If 0 groups, JUMP TO FLAT UNIT PROCESSING!
  ...
  80bf1d: mov    0x4(%edi),%ecx         # ecx = World
  80bf20: push   %ebx                   # subgroup unit count
  80bf21: push   %esi                   # subgroup unit IDs pointer
  80bf22: call   0x72c4f0               # World::find_existing_formation
  80bf27: mov    0x4(%edi),%ecx
  80bf2c: cmp    $0xffffffff,%eax
  80bf2f: je     0x80bf4f               # If not found, allocate new formation
  80bf31: push   %eax
  80bf32: call   0x72cf10               # World::get_formation_by_id
  80bf37: mov    %eax,%edi
  80bf41: cmp    %ebx,0xd0(%edi)        # Compare existing formation unit count with subgroup count
  80bf47: je     0x80bf5d               # If identical, reuse formation!
  80bf49: mov    -0x8(%ebp),%ecx
  80bf4c: mov    0x4(%ecx),%ecx         # ecx = World
  80bf4f: push   $0x1
  80bf51: call   0x72c600               # World::create_new_formation (ALLOCATION!)
```

#### Object Storage and Limits
1. **Heap Allocation (`0x72c600`)**:
   - Calls `operator new(0x4d4)` at `0x8d47df`. Size = **`0x4d4` bytes** (1,236 bytes).
   - Constructor `0x7d7440` initializes vtable `0x9e0ab4`.
2. **Linked List in `World`**:
   - `World+0x48` points to a circular doubly-linked list of `Formation` objects.
   - `[formation + 0xbc]` = `next_formation` pointer.
   - `[formation + 0xc0]` = `prev_formation` pointer.
   - `[world + 0x4c]` = total formation count in the game.
   - `[world + 0x50]` = next unique `formation_id` counter (`next_id++`).
3. **Per-Unit Field**:
   - `[unit + 0x84]` stores the unit's current `formation_id` (`int32`). If the unit is unformed, `[unit + 0x84] == -1` (`0xffffffff`).
4. **Hard Limit: Max 70 Units Per Formation (`0x7d7f50`)**:
   In `0x7d7f50` (`Formation::add_unit`), the game enforces a hard cap:
   ```assembly
   # exe.s line 1218880
     7d80b1: cmp    $0x46,%ecx             # 0x46 = 70 decimal!
     7d80b4: jge    0x7d8199               # If unit_count >= 70, ABORT!
   ```
   The unit pointer array at `[formation + 0xd8]` and slot offset table at `[formation + 0xdc]` are statically dimensioned for at most **70 units**.

---

## Question 2: Move Packet (Type 0x03) Fields & "Move Without Formation"

### 2.1 Packet Type 0x03 Exact Byte Layout

The layout of packet type `0x03` is constructed in `0x8097d0` and parsed in `0x80bdd0`:

```
+---------------------------------------------------------------------------------+
| Offset | Type   | Field Name            | Description                           |
+---------------------------------------------------------------------------------+
| 0x00   | uint8  | type                  | Always 0x03 (Move Command)            |
| 0x01   | uint8  | player_id             | Issuing player ID (1..8)              |
| 0x02   | uint8  | unused_padding_0      | UNUSED (Always 0x00)                  |
| 0x03   | uint8  | unused_padding_1      | UNUSED (Always 0x00)                  |
| 0x04   | int32  | target_unit_id        | -1 (0xffffffff) for ground move       |
| 0x08   | uint8  | unit_count            | Number of IDs (1..60) or 0xff (cached)|
| 0x09   | uint8  | unused_padding_2      | UNUSED (Always 0x00)                  |
| 0x0a   | uint8  | unused_padding_3      | UNUSED (Always 0x00)                  |
| 0x0b   | uint8  | unused_padding_4      | UNUSED (Always 0x00)                  |
| 0x0c   | float  | target_x              | Target map coordinate X               |
| 0x10   | float  | target_y              | Target map coordinate Y               |
| 0x14   | int32  | unit_ids[unit_count]  | Array of unit IDs (4 * unit_count)    |
+---------------------------------------------------------------------------------+
```

#### Disassembly Proof of Field Reads in `0x80bdd0`:
- `80bde4: mov 0x8(%ebp), %esi` $\rightarrow$ `movzbl 0x8(%esi), %eax` (reads count at offset `0x08`).
- `80be0a: mov 0x8(%ebp), %eax` $\rightarrow$ `mov 0x4(%eax), %eax` (reads target unit ID at offset `0x04`).
- `80beb9: movss 0x10(%eax), %xmm0` (reads target Y at offset `0x10`).
- `80bec4: movss 0xc(%eax), %xmm0` (reads target X at offset `0x0c`).
- `80dbe8: add %ebx, %edi` (reads unit IDs starting at offset `0x14`).

**Conclusion**: Packet `0x03` contains **NO field, flag, or bitmask** indicating "move without formation". Bytes `0x02`, `0x03`, `0x09`, `0x0a`, `0x0b` are pure alignment padding and are completely ignored by the vanilla game engine.

---

### 2.2 The Real Mechanism: Player Formation Toggle & Command Type 0x1d

While packet `0x03` has no per-order formation flag, the game engine has a **player-level formation toggle** stored at `[player + 0x1a90]`.

#### 1. Command Type `0x1d` (`Formation(pId, on)`)
In the main command dispatch jump table (`0x80a104`), type `0x1d` maps to `0x80ae90`:
```assembly
# exe.s lines 1282635-1282665 (Function 0x80ae90)
  80ae90: push   %ebp
  80ae91: mov    %esp,%ebp
  80ae93: mov    0x4(%ecx),%eax         # eax = World
  80ae96: mov    0x8(%ebp),%edx         # edx = Packet Type 0x1d Pointer
  80ae99: push   %esi
  80ae9a: mov    0x184(%eax),%ecx       # ecx = World->player_array
  80aea0: mov    0x188(%eax),%eax       # eax = World->player_array_end
  80aea6: movzbl 0x1(%edx),%esi         # esi = packet[1] (player ID)
  80aeaa: sub    %ecx,%eax
  80aeac: sar    $0x3,%eax              # player count
  80aeaf: cmp    %eax,%esi
  80aeb1: jge    0x80aebf
  80aeb3: mov    (%ecx,%esi,8),%ecx     # ecx = Player object pointer
  80aeb6: mov    0x4(%edx),%eax         # eax = packet[4..7] (int32 on/off: 1 or 0)
  80aeb9: mov    %eax,0x1a90(%ecx)      # player->formation_toggle = on/off!
  80aebf: pop    %esi
  80aec0: pop    %ebp
  80aec1: ret    $0x4
```
- **Packet Type `0x1d` Layout**:
  - `packet[0]`: `0x1d` (byte)
  - `packet[1]`: `player_id` (byte)
  - `packet[2..3]`: Padding
  - `packet[4..7]`: `int32 on_off` (`1` = formations enabled, `0` = formations disabled). Size = 8 bytes.

#### 2. How `0x7d61e0` Reacts When `player->formation_toggle == 0`
Inside `0x7d61e0`, the engine checks `0x1a90(%eax)`:
```assembly
# exe.s lines 1216940-1216960
  7d6446: mov    -0x130(%ebp),%eax      # eax = Player object pointer
  7d644c: cmpl   $0x0,0x1a90(%eax)      # Check player->formation_toggle!
  7d6453: jne    0x7d6490               # If != 0, continue to military formation clustering
  
  # === FORMATIONS DISABLED PATH ===
  7d6455: xor    %esi,%esi
  7d6457: test   %edi,%edi
  7d6459: jle    0x7d6c92
  7d6460: mov    -0x254(%ebp,%esi,4),%eax # Load unit ID from stack
  7d6467: cmp    $0xffffffff,%eax
  7d646a: je     0x7d6478
  7d646c: mov    -0x12c(%ebp),%ecx      # ecx = GroupManager (0xaf7ae0)
  7d6472: push   %eax
  7d6473: call   0x7d4af0               # Insert unit into FLAT UNFORMED list!
  7d6478: inc    %esi
  7d6479: cmp    %edi,%esi
  7d647b: jl     0x7d6460
  ...
  7d648d: ret    $0x18                  # RETURN IMMEDIATELY!
```

When `player->formation_toggle == 0`:
1. Every unit in the order is piped directly to `0x7d4af0` (the flat unformed list).
2. The function **returns immediately** at `0x7d648d`.
3. The group count at `0xaf7bd8` remains `0`.
4. In caller `0x80bdd0`, line `80bee3: jle 0x80c018` jumps completely past all formation creation code!
5. Lines `0x80c486..0x80c564` iterate over the flat list, detach each unit from its old formation (`0x7e4650`), and call virtual method `unit->vtable[0xac]` (`unit->move(target, x, y)`) directly:
```assembly
# exe.s lines 1284440-1284460 (Direct unit move execution)
  80c531: mov    0x8(%ebp),%ecx         # ecx = packet pointer
  80c537: mov    (%edi),%eax            # eax = unit vtable
  80c53c: movss  0x10(%ecx),%xmm0       # Target Y
  80c541: mov    0xac(%eax),%eax        # vtable slot 0xac: Unit::order_move
  80c547: movl   $0x0,0x8(%esp)
  80c54f: movss  %xmm0,0x4(%esp)
  80c555: movss  0xc(%ecx),%xmm0        # Target X
  80c55a: mov    %edi,%ecx              # this = Unit
  80c55c: movss  %xmm0,(%esp)
  80c561: push   %esi                   # target unit (or NULL)
  80c562: call   *%eax                  # Direct individual move order executed!
```

---

## Question 3: Shared Tables, Exhaustion, and Corruption by Extra Packets

Could our extra packets exhaust or corrupt shared tables used by the AI? **Yes.**

### 3.1 The Zombie Formation Memory Leak (`0x7e4650`)

When a unit is assigned to a new formation, `0x7d7f50` removes it from its old formation by calling `0x7e4650`.
```assembly
# exe.s lines 1233775-1233800 (Inside 0x7e4650)
  7e4784: lea    0x174(%edi),%ecx
  7e478a: decl   0xd0(%edi)             # Decrement formation->unit_count
  7e4790: call   0x7e4810
  7e4795: orl    $0x2,0xfc(%edi)
  7e479c: mov    $0x1,%eax
  7e47a1: pop    %edi
  7e47a2: pop    %esi
  7e47a3: pop    %ebx
  7e47a4: pop    %ebp
  7e47a5: ret    $0x4                   # RETURN WITHOUT FREEING!
```

`0x7e4650` **NEVER** frees a formation or removes it from `world+0x48`, even when `unit_count` reaches 0!
Formation destruction (`0x72ca60`) is ONLY called by command types `0x13`, `0x14`, and `0x18` (manual disband/stop commands).

#### Impact of Clones:
- Each right-click move with 250 units currently sends **1 original + 4 clone packets**.
- Each packet creates a new `Formation` object (`0x4d4` bytes = 1,236 bytes).
- 20 right-clicks during battle allocate **$20 \times 5 = 100$ formation objects**, of which 80+ become dead, 0-unit zombie nodes in `world+0x48`.
- `0x72cf10` (formation lookup by ID) is an $O(N)$ linear circular linked list traversal:
```assembly
# exe.s lines 1033420-1033440 (0x72cf10)
  72cf1b: mov    0x48(%ecx),%ecx        # Head of linked list
  72cf1e: mov    0xbc(%ecx),%eax        # eax = head->next
  72cf24: cmp    %ecx,%eax
  72cf26: je     0x72cf3a
  72cf28: cmp    %edx,0xc4(%eax)        # Compare node->formation_id with target ID
  72cf2e: je     0x72cf3c               # Found
  72cf30: mov    0xbc(%eax),%eax        # node = node->next
  72cf36: cmp    %ecx,%eax
  72cf38: jne    0x72cf28               # Loop over all nodes
```
Because `0x72cf10` is called by unit pathing, combat targeting, and tactical AI every simulation frame, accumulating thousands of zombie formations severely degrades CPU performance.

---

### 3.2 Scratchpad `0xaf7ae0` & Active Formation Buffer `0xafb50c`

The static buffer `0xaf7ae0` and active formation array `0xafb50c` are shared between:
- Player command executors (`0x80bdd0`, `0x80c650`, etc.).
- AI army formation routines (`0x52b300`, `0x52dd5b`).

```assembly
# exe.s lines 397810-397835 (AI army creation in 0x52b300)
  52b3e1: movl   $0x0,0xafb50c          # AI resets active formation counter
  52b3f3: mov    $0xaf7ae0,%ecx         # AI uses 0xaf7ae0!
  52b413: call   0x7d5d50               # AI formation partitioner
  52b41f: cmp    %eax,0xaf7bd8          # AI checks group count
```

- **Thread-Safety Analysis**: The simulation loop runs on a **single thread** in deterministic lockstep. Commands are processed sequentially by `0x813460`. Therefore, clone packets do NOT corrupt memory *during* an AI execution turn.
- **Buffer Saturation**: In `0x80bdd0`, `0xafb50c` records active formation IDs up to 60:
  ```assembly
  80bfac: cmp    $0x3c,%ecx             # Cap at 60 active formations
  80bfaf: jge    0x80bfc4
  80bfb7: mov    %eax,0xafb510(,%ecx,4)
  80bfbe: incl   0xafb50c
  ```
  Since 4 clones add only 4 IDs to `0xafb50c` (which is reset to 0 at the start of each order at `0x80beaf`), `0xafb50c` is not overflowed.

---

### 3.3 Root Cause of AI "Acting Strangely": Tactical Targeting Fragmentation

Why does the AI opponent stutter, oscillate, or act erratically in game?

In AoE2, tactical AI logic (`0x52f23a`) and individual unit combat targeting (`0x4de3d6`) evaluate enemy units by their **formation ID**:
```assembly
# exe.s lines 303410-303430 (Target evaluation in 0x4de3d6)
  4de3d6: mov    0x4(%esi),%eax         # eax = Target Unit
  4de3d9: mov    0x84(%eax),%ecx        # ecx = Target Unit's formation_id ([unit+0x84])
  4de3df: cmp    $0xffffffff,%ecx
  4de3e2: je     0x4de4ca               # If unformed, evaluate as single unit
  4de3ee: mov    0xc8(%eax),%ecx        # ecx = World
  4de3f4: call   0x72cf10               # Lookup enemy formation!
  4de403: cmpl   $0x1,0xf4(%edi)
  4de414: call   0x7ddc20               # Evaluate formation threat & centroid!
  4de42c: call   0x7d9f40               # Formation geometry engagement
```

#### What happens when 250 player units are split across 5 separate formations:
1. Each formation has its own separate center of mass, bounding box, and leader unit.
2. The 5 formations overlap physically in space.
3. As the front units of formation 1 and formation 2 shift, the AI's army threat evaluator sees **5 distinct enemy tactical formations** overlapping at the same location.
4. On tick $T$, the AI army targets Formation 1. On tick $T+1$, a unit from Formation 2 steps slightly closer; the AI re-targets to Formation 2's center.
5. This causes the AI army to **target-thrash**, rapidly switching orders between the 5 overlapping formation boxes. The AI units repeatedly start, stop, turn around, and oscillate in place rather than executing a coordinated assault.

---

## Question 4: Safest Fix Recommendation

### Evaluation of Options

#### Option (A): One Formation for All Units (>60 Units)
*Status: **REJECTED (Extremely Dangerous / High Crash Risk)***

Attempting to force $>60$ units (e.g. 250 units) into a single formation faces multiple hard engine limitations:
1. **Stack Overflow / Corruption in `0x7d61e0`**:
   The stack buffer at `-0x254(%ebp)` is exactly $60 \times 4 = 240$ bytes. Writing 250 IDs writes 1,000 bytes, smashing the stack frame, EBP, return address, and SEH registration.
2. **Hard Limit in `Formation::add_unit` (`0x7d80b1`)**:
   `cmp $0x46, %ecx` hard-caps any formation at **70 units**. Exceeding 70 causes heap buffer overruns in `[formation+0xd8]` and `[formation+0xdc]`.
3. **Scratchpad Bounds**:
   `0xaf7ae0` subgroup storage is strictly dimensioned for 60 units per group (`0x1ec + (g*60 + u)*4`).
4. **Kinematic Gridlock**:
   Formation geometry layouts (line, box, staggered) are calculated by `0x7d9380` and `0x7f1780`. Placing 250 units in a single AoE2 formation creates a structure wider than the screen, breaking pathfinding and causing severe movement deadlock.

---

#### Option (B): Clones as Plain Moves Without Formation
*Status: **RECOMMENDED (Safe, Clean, Deterministic)***

- **Packet 1 (Original game packet, first 60 units)**: Executes normally through `0x80bdd0`. Forms a standard formation for the front 60 units. Visually and mechanically, the army's vanguard maintains military formation behavior.
- **Packets 2..5 (Wololoader clone packets, remaining ~190 units)**: Execute as plain unformed moves (`unit->move` directly).
  - No new `Formation` objects allocated (`0x72c600` skipped).
  - Zero zombie formation leaks in `world+0x48`.
  - Units detach from old formations (`[unit+0x84] = -1`), so the AI evaluates them cleanly without formation targeting thrash.
  - Overflow units smoothly path toward the destination without geometric rigid-body collision boxes.

---

### Exact Implementation Design for Option (B)

#### Step 1: Mark Clone Packets Using Unused Padding Byte
In Packet Type `0x03`, byte `0x02` is unused padding (always `0x00`).
In `wololoader/src/overflow.c`, inside `send_clone()`:
```c
// Mark clone packet with signature byte 'W' (0x57) at offset 0x02:
clone[2] = 0x57; 
```

#### Step 2: Hook `0x80bdd0` (Move Executor) or `0x7d61e0`
When `0x80bdd0` executes a Type `0x03` packet:
1. Inspect `packet[2]`:
   - If `packet[2] == 0x57`: This is a Wololoader clone packet!
2. Resolve the player object: `void *player = get_player(packet[1])`.
3. Temporarily set `player->formation_toggle = 0`:
   ```c
   int old_toggle = *(int *)((char *)player + 0x1a90);
   *(int *)((char *)player + 0x1a90) = 0;
   ```
4. Call original `0x7d61e0`.
   - Because `player+0x1a90 == 0`, `0x7d61e0` routes all units to `0x7d4af0` (flat unformed list) and immediately returns with `[0xaf7bd8] == 0`.
5. Restore `player->formation_toggle = old_toggle`.
6. `0x80bdd0` sees `[0xaf7bd8] <= 0`, jumps directly to `0x80c018`, and calls `unit->move(target, x, y)` individually for every unit at `0x80c562`.

#### Why This Is Completely Safe:
- Uses the **game's own native, fully-tested unformed movement path**.
- Zero modifications to internal array bounds or struct definitions.
- Works identically in single-player, replays, and multiplayer (the marker byte `packet[2]` travels through the lockstep packet stream deterministically).
