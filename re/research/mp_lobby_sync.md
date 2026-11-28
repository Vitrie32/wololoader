# Multiplayer Lobby Synchronization (AoE2 HD 5.8)

**Research goal:** How the host propagates lobby settings (team assignments, map selection, player slots) to clients in a multiplayer lobby, for implementing custom game mode features in MP.

## 1. Settings Broadcast: Host → Clients

### 0x588790 — Host broadcasts full GameSettings (packet 0x5a)
**Signature:** `void __fastcall(CommHandler* comm)` (fastcall: ecx=comm), plain `ret`  
**Prologue:** UNVERIFIED (need objdump)  
**What it does:**
- Line 307847: Calls function pointer on `GameSettings` at `comm+0xd54`
- Line 307848-307849: Increments sequence counter at `DAT_00af1ef0`
- Line 307850: **Sets `local_280[0] = 0x5a`** — packet type byte
- Line 307851-307856: Locks/unlocks `comm+0xd50`, calls send function
- Line 307853: `FUN_005757d0(1, 0, local_280, 1)` — likely the actual packet send (UNVERIFIED which function this is, could be 0x574920 or wrapper)

**Result:** Serializes the full `GameSettings` object and broadcasts it to all clients as packet 0x5a.

### 0x588840 — Throttle/check before broadcast
**Signature:** `void __fastcall(CommHandler* comm)`, plain `ret`  
**Prologue:** UNVERIFIED  
**What it does:**
- Line 307869: Calls `FUN_005a1410(comm + 0xae8)` — returns char/bool
- If false (line 307871): calls `FUN_00588790()` to broadcast
- If true: returns without broadcasting (throttle/cooldown active?)

**Purpose:** Prevents flooding the network with settings updates. Only broadcasts when throttle allows.

### 0x657af0 — Settings-changed handler (called by all UI setters)
**Signature:** `void __thiscall(GameSetupScreen* screen)`, plain `ret`  
**Prologue:** `55 8b ec 6a ff 68 ae eb 94 00` (10 bytes, VERIFIED in FINDINGS.md)  
**What it does:**
- Line 468479-468482: **If SP (`settings+0x408 == 0`)**: calls `0x65a640()` (SP UI update)
- Line 468485-468492: **If MP (`settings+0x408 == 1`)**: 
  - Calls `0x656fa0()` check (line 468485)
  - If true: **calls `0x588840()`** to broadcast settings (line 468487)
  - Then calls `0x65efb0()` to update local MP lobby UI (line 468491)

**Called by:**
- `0x658ab0` set_map_style (line 469471)
- `0x658a80` set_map_size (need to verify, mentioned in FINDINGS but not found in grep)
- Team dropdowns, slot type dropdowns, etc. (need to find these)
- **Hook point for custom mode:** Call this after modifying `GameSettings` memory in MP

### Individual UI setters (PARTIAL — need more research)

**0x658ab0 — set_map_style**  
**Signature:** `void __thiscall(GameSetupScreen* screen, int style)`, `ret 4`  
**Prologue:** UNVERIFIED  
**What it does (from FINDINGS.md):**
- Style 2 = Custom map: reads first entry from Custom map list at `screen+0x88c/+0x890`
- Calls `0x613650` to set map type 0x3b
- Copies map file name and metadata to `settings` fields
- **Line 469471: calls `0x657af0()`** — triggers broadcast in MP

**0x658a80 — set_map_size (MENTIONED in FINDINGS.md but not found in decompiled code)**  
**Signature:** `void __thiscall(GameSetupScreen* screen, int size)`, `ret 4` (from FINDINGS.md)  
**Prologue:** UNVERIFIED  
**Need:** Objdump to verify bytes and flow

**Team / slot setters (NOT FOUND YET)**  
Need to find functions that:
- Change a slot's team assignment (`settings+0xb0+slot*0x68 + 0x0` team field)
- Change a slot's status (`settings+0xb0+slot*0x68 + 0x54` status: 1=Closed, 2=Human, 4=AI)
- Add/remove players (`0x6134b0` set_num_players from FINDINGS, but need MP UI caller)

## 2. Client-side: Receiving and Applying Settings

### Packet 0x5a handler (NOT FOUND YET)
**Expected signature:** `void __thiscall(CommHandler* comm, void* peer_wrapper, void* packet)`, `ret 8`  
**Expected registration:** Similar to packet 0x43 (chat) registered at `0x579830`  
**What it should do:**
- Deserialize packet payload into `GameSettings` at `comm+0xd54`
- Call `0x65efb0()` to refresh MP lobby UI

**Need:** Search packet handler registration table for 0x5a, or search for functions that:
- Read from a packet buffer and write to `settings+0xac` (game mode), `settings+0x94/0x98` (map), `settings+0xb0+slot*0x68` (slot data)
- Call `0x65efb0` after updating settings

### 0x65efb0 — MP lobby UI update (equivalent to 0x65a640 for SP)
**Signature:** `void __fastcall(GameSetupScreen* screen)`, plain `ret`  
**Prologue:** UNVERIFIED  
**What it does:**
- Line 473911: Checks `if (settings+0x408 != 0)` — confirms it's MP-only
- Line 473913: Calls `0x59c970(screen + 0x8c8)` — some validation check
- Lines 473914-473930: **Copies data from `screen+0x8c8` area to `screen+0x908` area**
  - Looks like staging buffer → active UI state
- Line 473931: Calls `0x57df50` on the buffer

**Hook point for (d):** To hide map controls when `map.lock` is set, hook this function and:
- Check if current mode has `map.lock`
- If yes: set `panel+0x1c = 0` (visible flag) for map style/location/size controls
- Need to find the panel pointers for these controls in `GameSetupScreen` structure

### Map control pointers in GameSetupScreen (NOT FOUND YET)
Need to find offsets for:
- Map style dropdown panel pointer
- Map location dropdown panel pointer  
- Map size dropdown panel pointer

**Method:** Search for functions that initialize these controls in MP lobby (similar to how 0x64a670 builds SP lobby per FINDINGS.md dropdown callsite at 0x64b659).

## 3. Host Detection

### Method 1: Check local player slot index
From `0x5836e0` (`CommHandler::get_local_player_id`, VERIFIED in FINDINGS.md):
- Returns player ID 1-8 by matching local UID against `settings+0xd0+slot*0x68 + 0x20`
- Subtract 1 to get slot index 0-7
- **If slot index == 0: likely the host**

**Confidence:** MEDIUM — slot 0 is typically the host/creator in lobby games, but not verified in disassembly.

### Method 2: Check CommHandler or GameSettings flag (NOT FOUND YET)
Need to search for:
- A boolean flag at `CommHandler + offset` or `settings + offset` that indicates host status
- Functions that check host status before allowing UI edits (non-host players can't change settings)
- Packet send code that only runs on host

**Search strategy:**
- Look for code that enables/disables lobby controls based on a flag
- Check packet 0x5a send call sites — if there's a guard condition, that's the host check
- Objdump search for `cmp` instructions near 0x588790 callers

## 4. Start Button Synchronization

### 0x655180 — start_clicked (VERIFIED in FINDINGS.md)
**Signature:** `BYTE __fastcall(GameSetupScreen* screen)`, plain `ret`, returns al=1 if OK  
**Prologue:** `56 57 8b f9` (stolen bytes in hook)  
**What it does (from decompiled code):**
- Line 466095-466098: Loop `for (slot=1..8)` calls `FUN_00804930(slot, 1)` on each slot
- Line 466099-466102: Calls `0x6554c0()` validation, returns 0 if failed
- Line 466103: Plays civ jingle `0x6599c0()`
- Line 466104-466105: Sets `screen+0x8a4 = timeGetTime()` and `screen+0xbd6 = 1`
- Line 466107-466109: **If MP (`settings+0x408 == 1`)**: calls `FUN_00657af0()` (settings-changed handler)
- Next frame: `GameSetupScreen::action` (0x656c60) sees `screen+0xbd6 == 1` and launches match via `0x6600d0 -> 0x6571f0`

**Key insight:** In MP, the start button calls `0x657af0()` which broadcasts settings via packet 0x5a **before** launching the match. This suggests:

**Hypothesis (UNVERIFIED):** When the host clicks Start:
1. Validation runs (0x6554c0)
2. Settings are broadcast one final time (0x657af0 -> 0x588840 -> 0x588790)
3. Host waits briefly for clients to acknowledge (the `screen+0x8a4` timestamp and next-frame check suggest async coordination)
4. All machines launch the match with their local `GameSettings` (which should be in sync after the broadcast)

**Implication for custom modes:** If the host modifies `GameSettings` memory (team assignments, extra slot, map selection) in the start button hook **before** the `0x657af0()` call at line 466108, those changes will be broadcast to clients. No need to call individual UI setters.

**CRITICAL VERIFICATION NEEDED:**
- Does packet 0x5a include **all** fields: slot teams, slot status, slot count, map type, map size?
- Or does the host need to call individual UI setters (0x658ab0, etc.) which each trigger a broadcast?
- Objdump the packet 0x5a serialization to see which `GameSettings` fields are included

## 5. Implementation Recipe (PRELIMINARY — needs verification)

### For (a) One team + locked teams
**In host's `hook_setup_start` (0x655180), before line 466108 `FUN_00657af0()`:**
```c
if (isMultiPlayer && isHost && customModeRequiresOneTeam) {
    GameSettings* settings = get_settings();
    for (int i = 0; i < settings->mNumPlayers; i++) {
        *(int*)(settings + 0xb0 + i*0x68 + 0x0) = 1;  // Set all to team 1
    }
    // Lock teams: need to find the lock flag offset in GameSettings (NOT FOUND YET)
}
```
Then `0x657af0()` at line 466108 will broadcast these changes.

### For (b) Extra enemy slot
**In host's `hook_setup_start`, before the broadcast:**
```c
if (isMultiPlayer && isHost && customModeRequiresEnemy) {
    // Call 0x6134b0(settings, settings->mNumPlayers + 1) to add one slot
    // Set new slot: team = 2, status = 4 (AI), AI script = default
    // Need to find: set_slot_team, set_slot_status, set_slot_ai functions
}
```

### For (c) Map selection
**In host's `hook_setup_start`, before the broadcast:**
```c
if (isMultiPlayer && isHost && customModeHasMap) {
    // Option A: Call 0x658ab0(screen, 2) to set style=Custom
    //           Call 0x658a80(screen, minSize) to set size (each triggers a broadcast — inefficient)
    // Option B: Set settings fields directly:
    //           *(int*)(settings + 0x94) = customMode->map_type;
    //           *(int*)(settings + 0x98) = customMode->map_type;
    //           *(int*)(settings + 0x90) = customMode->map_min_size;
    //           Then one broadcast via 0x657af0() sends all changes
}
```
**Preferred:** Option B (direct memory writes + single broadcast) if packet 0x5a includes all fields.

### For (d) Hide map controls
**Hook `0x65efb0` (MP lobby UI update) on ALL machines (host and clients):**
```c
void __fastcall hook_mp_lobby_ui(GameSetupScreen* screen, void* edx) {
    orig_mp_lobby_ui(screen, edx);
    
    if (currentModeHasMapLock) {
        // Find control pointers (need offsets):
        void* mapStyleControl = *(void**)(screen + OFFSET_MAP_STYLE_PANEL);
        void* mapLocationControl = *(void**)(screen + OFFSET_MAP_LOCATION_PANEL);
        void* mapSizeControl = *(void**)(screen + OFFSET_MAP_SIZE_PANEL);
        
        // Hide via Panel+0x1c visible flag:
        if (mapStyleControl) *(int*)((char*)mapStyleControl + 0x1c) = 0;
        if (mapLocationControl) *(int*)((char*)mapLocationControl + 0x1c) = 0;
        if (mapSizeControl) *(int*)((char*)mapSizeControl + 0x1c) = 0;
    }
}
```

## 6. Remaining Research Tasks

### HIGH PRIORITY:
1. **Host detection:** Find the actual flag or confirm slot 0 == host
   - Method: Objdump near 0x588790 call sites for guard conditions
   - Method: Search for UI enable/disable code that checks host status
   
2. **Packet 0x5a handler:** Find where clients receive and deserialize settings
   - Method: Search packet handler registration table (like 0x579830 for packet 0x43)
   - Method: Grep for functions that write to `settings+0xac`, `+0x94`, `+0xb0` from a buffer
   
3. **Map control panel offsets:** Find pointers to map style/location/size controls in `GameSetupScreen`
   - Method: Search MP lobby init function (equivalent to 0x64a670 for SP)
   - Method: Objdump calls to dropdown creation function near MP lobby ctor
   
4. **Verify packet 0x5a payload:** Which `GameSettings` fields are serialized?
   - Method: Objdump 0x588790 and the function at line 307847 to see serialization loop
   - Critical: Does it include slot teams, slot status, map type, map size?

### MEDIUM PRIORITY:
5. **Find set_map_size (0x658a80):** Verify bytes and calling convention
6. **Find team setter:** Function to change slot team assignments
7. **Find slot status setter:** Function to change slot type (Closed/Human/AI)
8. **Lock teams flag:** Offset in `GameSettings` for team lock toggle

### LOW PRIORITY:
9. **Start synchronization details:** Exact flow after host clicks Start
10. **Throttle mechanism:** How `0x5a1410` works to prevent broadcast flooding

## 7. Confidence Levels

| Item | Confidence | Verification Status |
|------|-----------|---------------------|
| 0x588790 broadcasts packet 0x5a | HIGH | Decompiled code shows packet type 0x5a, send call |
| 0x588840 is throttle before broadcast | HIGH | Decompiled code shows conditional call to 0x588790 |
| 0x657af0 is settings-changed handler | CONFIRMED | FINDINGS.md verified, stolen bytes checked |
| 0x657af0 calls 0x588840 in MP | HIGH | Decompiled code line 468487 shows this |
| 0x65efb0 is MP lobby UI update | HIGH | Called from 0x657af0 MP branch, checks isMP flag |
| 0x658ab0 is set_map_style | CONFIRMED | FINDINGS.md verified, calls 0x657af0 |
| 0x655180 calls 0x657af0 on start | HIGH | Decompiled code line 466108 shows this in MP branch |
| Slot 0 == host | MEDIUM | Typical pattern, but not verified in disassembly |
| Direct memory + 0x657af0 works | MEDIUM | Depends on packet 0x5a payload (unverified) |
| Map controls hideable via +0x1c | HIGH | FINDINGS.md verified this for other panels |
| Map control offsets in screen | UNVERIFIED | Need to find |

## 8. Next Steps for Implementation

**Before writing code:**
1. Run objdump on 0x588790 to verify packet 0x5a serialization (which fields)
2. Search for packet 0x5a handler registration
3. Find host detection flag or verify slot 0 method
4. Find map control panel offsets in GameSetupScreen

**After verification:**
5. Hook 0x655180 (start_clicked) for host-side GameSettings modifications
6. Hook 0x65efb0 (MP UI update) for map control hiding on all machines
7. Test in LAN multiplayer with two machines
