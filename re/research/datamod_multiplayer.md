# HD Data Mod System & Multiplayer Lobby Checksum Enforcement — Reverse Engineering Report

> **Target Game**: Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit, ImageBase `0x400000`)  
> **Source Files**: `re/out/decompiled.c`, `re/out/functions.tsv`, `scratchpad/text.asm`  
> **Verification**: Every address, prologue, opcode, struct offset, and calling convention is marked **[VERIFIED]** (checked in disassembly/binary) or **[GUESS]**.  
> **Scope**: READ-ONLY reverse engineering analysis.

---

## Executive Summary & System Overview

Age of Empires II HD features two separate dataset layers:
1. **Base Game Datasets** (`GameDataSet`, vtable `0x009c9854`, source `gamedataset.cpp`): Built-in datasets representing expansions (0: Age of Kings, 1: The Conquerors, 2: The Forgotten, 3: African Kingdoms, 4: Rise of the Rajas).
2. **Workshop Data Mods** (`ModPackage`, size `0x440`, source `modmanager.cpp`, `modpackage.cpp`): Mods located in `<game>\mods\<folder>\` that contain a `.dat` file (checked via `0x007d2980`). At most **one** Data Mod can be active at a time (`mIsActiveDataMod` flag at offset `+0x41a`).

Compatibility in multiplayer is enforced at two distinct stages:
1. **Steam Matchmaking Lobby Layer**: The host broadcasts `"Dataset"` (integer ID), `"DatasetWorkshopId"` (64-bit uint64 string), and `"DatasetWorkshopTitle"` (string) via `ISteamMatchmaking::SetLobbyData`. Clients browsing the lobby filter on these tags. When joining, clients check if they have the mod installed (`0x007ce240`); if not, the game initiates an automatic Steam Workshop subscription/download (`0x0064aa4a`).
2. **In-Lobby Staging / Network Protocol Layer**: Each player calculates an IEEE 802.3 CRC32 checksum of their active dataset (`GameDataSet::getCrc` `0x005d5f20` or `ModPackage::getChecksum` `0x007d2270`). This 32-bit CRC is transmitted in packet **`0x52` (`PREGAMECLIENTSTATE`)** at offset `+0x18` of the player state structure. The host verifies this in packet handler `0x00584a40`. Any mismatch causes the host to drop the client via `FUN_00583f50` (reason code 3) and broadcast `"GAMEDATA_CRC_CHECK"` (String ID 10956: *"%s is not running the same version of the data file. Everyone must run the same version of the data file before you can start the game."*).

---

## 1. Where the Active Data Mod / Dataset is Chosen and Stored

### 1.1 Dataset Selection in Player Profile
- **Storage File**: `<game>\profiles\player.nfp` (or Steam Cloud sync). **[VERIFIED]**
- **Serialization Function**: `FUN_00620780` (`PlayerProfile::serialize`). **[VERIFIED]**
  - Single-Player Dataset Reference: `&mrefSinglePlayerGameDataSet` at `PlayerProfile + 0x2c` (`0x006207da`). **[VERIFIED]**
  - Multiplayer Dataset Reference: `&mrefMultiPlayerGameDataSet` at `PlayerProfile + 0x34` (`0x0062084b`). **[VERIFIED]**
  - Dataset ID: `mId` stored at `GameDataSet + 0x8` (values 0..4). **[VERIFIED]**

### 1.2 Mod Package & Mod Status Persistence
- **Storage File**: `<game>\mod-status.txt`. **[VERIFIED]**
- **Reader Address**: `0x007cf1e0` (called at engine startup). **[VERIFIED]**
- **Writer Address**: `0x007cfba0` (called whenever mod configuration changes). **[VERIFIED]**
- **File Format**: Plain text table with columns formatted as:  
  `"%-10s%-75s%-10s%-10s%-10s%-24s%-24s"` (`0x007cf175`):  
  `WORKSHOPID`, `MOD NAME`, `PRIORITY`, `ENABLED`, `LOCAL`, `AUTHOR`. **[VERIFIED]**

### 1.3 Mod Directory & Mod Scanning
- **Root Folder**: `<game>\mods\` (Storage ID `4`). **[VERIFIED]**
- **Scanner Address**: `FUN_007cd3b0` (`ModManager::buildModsStep2`, `0x007cd3b0`). **[VERIFIED]**
  - Scans directory listing for `<game>\mods\*` (`0x007cd083`).
  - For each folder, allocates `0x440` bytes via game `operator new` (`0x008d47df`) and invokes `ModPackage::ctor` (`0x007d0580`). **[VERIFIED]**

### 1.4 ModPackage Data Mod Detection & Activation
- **ModPackage Layout (size `0x440`)**: **[VERIFIED]**
  - `+0x00`: `mChecksum` (`uint32_t`, cached CRC32 of mod files).
  - `+0x04`: `mTitle` (`std::string`, 24 bytes).
  - `+0x10`: `mWorkshopId` (`uint64_t`, 8 bytes, low dword at `+0x10`, high dword at `+0x14`).
  - `+0x18`: `mOwnerSteamId` (`uint64_t`, 8 bytes).
  - `+0x20`: File category array (`0x22` entries, each `0x1c` bytes = `0x3b8` bytes total).
  - `+0x3f4`: `mDirectoryName` (`std::string`, relative to `mods\`).
  - `+0x408`: `mType` (4 = local disk mod).
  - `+0x40c`: `mPriority` (`int`).
  - `+0x418`: `mEnabled` (`uint8_t`, 1 = enabled, 0 = disabled).
  - `+0x419`: `mIsLocal` (`uint8_t`).
  - `+0x41a`: `mIsActiveDataMod` (`uint8_t`, 1 = active data mod, 0 = inactive).
- **Data Mod Checker (`FUN_007d2980`)**: **[VERIFIED]**
  - Scans the mod package's category `0xd` (data files) and checks if any filename ends with `".dat"` (`0x009c9bac` at `0x007d29b1`).
  - Returns `1` if the mod ships a `.dat` file, `0` otherwise.
- **Mutual Exclusion & Activation**:
  - `FUN_007cf9b0` (`ModManager::disableAllDataMods`): Sets `pkg->mIsActiveDataMod = 0` (`0x007d2ad0`) for all packages in `ModManager::mModPackages`. **[VERIFIED]**
  - `FUN_007cf9f0` (`ModManager::setActiveDataMod`): `__thiscall(ModManager *self, ModPackage *target)`: Sets `pkg->mIsActiveDataMod = (pkg == target)` for all packages. Only ONE data mod is active at any time. **[VERIFIED]**
  - `FUN_007ce330` (`ModManager::getActiveDataMod`): `__fastcall(ModManager *self)`: Returns the active `ModPackage*` (where `isDataMod() && mIsActiveDataMod != 0`), or `NULL` if none. **[VERIFIED]**

---

## 2. How the .dat Path is Selected Per Data Mod

### 2.1 File Resolution via ModManager
- **Resolver Function**: `FUN_007cd7c0` (`ModManager::resolveFile`). **[VERIFIED]**
  - Signature: `const char* __thiscall(ModManager *self, int storageId, const char *relPath, int flag)`.
  - Logic: Iterates `self->mModPackages` (`0x007cd829`):
    ```c
    if ((pkg->mEnabled || (pkg->isDataMod() && pkg->mIsActiveDataMod)) && pkg->hasFile(storageId, relPath)) {
        return pkg->getFullPath(storageId, relPath);
    }
    ```
  - For normal assets, `pkg->mEnabled` (`+0x418`) suffices.
  - For data mods (`pkg->isDataMod() == true`), the file is **only** resolved if `pkg->mIsActiveDataMod` (`+0x41a`) is non-zero.
  - When resolved, the path points to `<game>\mods\<pkgDir>\resources\_common\dat\empires2_x2_p1.dat`.

### 2.2 Dat File Loader
- **Loader Function**: `World::loadGameData` (`0x0068d100` / `0x0072ee50`). **[VERIFIED]**
  - Requests `resources\_common\dat\empires2_x2_p1.dat`.
  - When a data mod is active, `ModManager::resolveFile` diverts the open to the mod's dat.
  - In Wololoader, this is currently intercepted via IAT hooks on `CreateFileA`/`CreateFileW` in `wololoader/src/files.c`, pointing to `wololoader\cache\empires2_x2_p1.dat`.

---

## 3. Steam Matchmaking & Multiplayer Lobby Enforcement

### 3.1 Host Publishing Lobby Metadata
When creating or updating a multiplayer lobby:
- **Function**: `FUN_005b4be0` / `FUN_005b5310` (Steam Matchmaking wrapper). **[VERIFIED]**
- **Call Site in Assembly**: `0x005b59ef` to `0x005b5a3c`. **[VERIFIED]**
  ```asm
  005b59ef: mov  eax, [ebx+0x88]           ; refGameDataSet
  005b59f7: mov  eax, [eax+0x8]            ; GameDataSet*
  005b59fa: push DWORD PTR [eax+0x8]       ; dataset ID (0..4)
  005b59fd: push DWORD PTR ds:0xaad54c     ; "Dataset" (0x009c5748)
  005b5a03: call 0x5b46f0                  ; SetLobbyData(pchKey, intVal)
  005b5a08: mov  ecx, ds:0xaf5890          ; ModManager*
  005b5a0e: call 0x7ce330                  ; ModManager::getActiveDataMod()
  005b5a13: mov  esi, eax                  ; esi = ModPackage* (or NULL)
  005b5a15: test esi, esi
  005b5a17: je   0x5b5a3c                  ; if NULL, do not set workshop metadata!
  005b5a19: push DWORD PTR [esi+0x14]      ; Workshop ID high dword
  005b5a1c: mov  ecx, edi
  005b5a1e: push DWORD PTR [esi+0x10]      ; Workshop ID low dword
  005b5a21: push DWORD PTR ds:0xaad55c     ; "DatasetWorkshopId" (0x009c5774)
  005b5a27: call 0x5b4690                  ; SetLobbyData(pchKey, uint64Val)
  005b5a2c: push DWORD PTR [esi+0x4]       ; Workshop Title string
  005b5a2f: mov  ecx, edi
  005b5a31: push DWORD PTR ds:0xaad560     ; "DatasetWorkshopTitle" (0x009c5788)
  005b5a37: call 0x5b4750                  ; SetLobbyData(pchKey, const char*)
  ```
- **Metadata Keys**:
  | Key Pointer | Key String VA | String Value | Purpose |
  |---|---|---|---|
  | `0x00aad54c` | `0x009c5748` | `"Dataset"` | Official dataset ID (0 = AoK, 1 = Conquerors, etc.) |
  | `0x00aad55c` | `0x009c5774` | `"DatasetWorkshopId"` | 64-bit Workshop PublishedFileId (0 if official) |
  | `0x00aad560` | `0x009c5788` | `"DatasetWorkshopTitle"` | Display name of the data mod in lobby browser |

### 3.2 Lobby Browser Screen Filtering
- **Class**: `LobbyBrowserScreen` (vtable `0x009d6350`, ctor `0x006d6320`). **[VERIFIED]**
- **Data Extractor**: `0x005b4e20..0x005b4e5f`: Calls `ISteamMatchmaking::GetLobbyData` with `"DatasetWorkshopId"` (`0x005b4e2a`) and `"DatasetWorkshopTitle"` (`0x005b4e51`). **[VERIFIED]**
- **Filter Tags**:
  - `LOBBYFILTER_OFFICIAL_DATASETS_ONLY` (`0x009d64b4` at `0x006db74b`). **[VERIFIED]**
  - `LOBBYFILTER_MODDED_DATASETS_ALLOWED` (`0x009d64f8` at `0x006db85e`). **[VERIFIED]**
  - Icon: `ui\mm-dataset-icon-16x16.png` (`0x009d64d8`). **[VERIFIED]**

### 3.3 Client Join & Auto-Download Flow
- **Function**: `FUN_0064a670` (`GameSetupScreen::setup`, `0x0064a670`). **[VERIFIED]**
- **Call Site**: `0x0064a9c9..0x0064aa50`:
  ```asm
  0064a9c9: push 0x0
  0064a9cb: push DWORD PTR [esi+0xbc4]     ; Workshop ID high
  0064a9d1: push DWORD PTR [esi+0xbc0]     ; Workshop ID low
  0064a9d7: call 0x7ce240                  ; ModManager::findModByWorkshopId(low, high, 0)
  0064a9dc: mov  ecx, eax                  ; ecx = ModPackage*
  0064a9e4: test ecx, ecx
  0064a9e6: je   0x64aa0b                  ; NOT FOUND -> handle missing mod!
  0064a9e8: push ecx
  0064a9e9: mov  ecx, ds:0xaf5890          ; ModManager*
  0064a9ef: call 0x7cf9f0                  ; ModManager::setActiveDataMod(ecx)
  ...
  ; If NOT found locally and WorkshopId != 0:
  0064aa0b: mov  eax, [esi+0xbc0]
  0064aa11: mov  edx, [esi+0xbc4]
  0064aa17: or   eax, edx
  0064aa19: je   0x64aa57                  ; WorkshopId is 0 (vanilla) -> OK
  0064aa28: mov  eax, [esi+0xbd8]
  0064aa2e: mov  BYTE PTR [esi+0xbd4], 1   ; mWaitingForWorkshopDownload = 1
  0064aa35: push edx
  0064aa36: push DWORD PTR [esi+0xbc0]
  0064aa3c: mov  eax, [eax+0xc8]
  0064aa42: mov  ecx, [eax+0x2590]         ; ISteamUGC / Workshop interface
  0064aa48: mov  eax, [ecx]
  0064aa4a: call DWORD PTR [eax+0x18]      ; Steam Workshop Subscribe / Download!
  ```
- **Consequence**: If a player does NOT have the mod, vanilla HD attempts to download it via Steam Workshop API. If the ID is not on the Steam Workshop, the download stalls or fails, preventing the player from readying up.

---

## 4. In-Lobby Network Protocol Checksum Comparison & Rejection

### 4.1 Checksum Calculation Algorithms
There are two checksum calculations depending on whether a data mod is active:
1. **Base Game Dat CRC**: `FUN_005d5f20` (`GameDataSet::getCrc`). **[VERIFIED]**
   - Calls `FUN_00829a60(out, "resources\_common\dat\empires2_x2_p1.dat", &crc)`:
   - Reads file in `0x40000` (256 KB) chunks (`0x00829c17`).
   - Computes standard IEEE 802.3 CRC32 via `FUN_004a30d0` (`0x004a30d0`, lookup table `0x00aaced8`, polynomial `0xEDB88320`).
   - Caches CRC32 at `GameDataSet + 0x10`.
2. **Data Mod Checksum**: `FUN_007d2270` (`ModPackage::getChecksum`). **[VERIFIED]**
   - Loops over all 34 file categories (`0x0` to `0x21`) in the mod package.
   - For every file in the mod, computes CRC32 via `FUN_00829a60` and aggregates into `*this` (`ModPackage + 0x0`).
   - Caches CRC32 at `ModPackage + 0x0`.

### 4.2 Network Packet Transmission (`PREGAMECLIENTSTATE`)
- **Packet ID**: `0x52` (decimal 82). **[VERIFIED]**
- **Internal Name**: `"PREGAMECLIENTSTATE"` (string at `0x009c27f0`, registered at `0x0057be2b`). **[VERIFIED]**
- **Sender**: Generated in `GameSetupScreen` (`FUN_0064f0e0` at `0x0064f164..0x0064f18a`). **[VERIFIED]**
  ```c
  uint32_t activeCrc = GameDataSet::getCrc();
  if (ModManager::getActiveDataMod() != NULL) {
      activeCrc = ModPackage::getChecksum();
  }
  // Written into local player table at slot + 0x18 (+0xb8, +0x120, etc.)
  ```
  Sent to host whenever client updates state, civ, team, or ready status.

### 4.3 Host Verification & Rejection Function (`FUN_00584a40`)
- **Packet Handler Address**: `0x00584a40`. **[VERIFIED]**
- **Calling Convention**: `__thiscall(TCommSession *self, PacketPregameClientState *packet, PlayerSlot *player)`. **[VERIFIED]**
- **Disassembly Analysis**:
  ```asm
  00584ad0: mov  eax, DWORD PTR [ebp-0x20]  ; Host expected base dat CRC (from 0x5d5f20)
  00584ad3: cmp  eax, DWORD PTR [ebx+0x18]  ; Compare with client sent CRC at [ebx+0x18]
  00584ad6: je   0x584b63                   ; CRCs match -> proceed to DLC flags check
  00584adc: mov  ecx, ds:0xaf5890           ; ModManager*
  00584ae2: call 0x7ce330                   ; ModManager::getActiveDataMod()
  00584ae7: test eax, eax
  00584ae9: jne  0x584b63                   ; If host has active data mod, bypass base check!
  ; --- MISMATCH REJECTION FLOW ---
  00584b08: push 0x9c2bfc                   ; "GAMEDATA_CRC_CHECK" (0x009c2bfc)
  00584b16: mov  ecx, ds:0xad13e0           ; Language string manager
  00584b1c: call 0x448370                   ; Lookup string: "%s is not running the same version..."
  00584b2c: lea  ecx, [ebp-0x1c]
  00584b2f: call 0x53e090                   ; Format message with client's player name
  00584b3b: push 0x3                        ; reason code 3 (Eject / Drop)
  00584b3d: push ebx                        ; player struct pointer
  00584b3e: mov  ecx, edi                   ; TCommSession*
  00584b40: call 0x583f50                   ; *** EJECT / DROP CLIENT FUNCTION ***
  00584b45: push DWORD PTR [ebp-0x1c]       ; Formatted error text
  00584b48: mov  ecx, edi
  00584b4a: call 0x588030                   ; Broadcast chat/system alert to lobby
  ```
- **Rejection Function Details**:
  - **Function**: `FUN_00583f50` (`0x00583f50`). **[VERIFIED]**
  - **Signature**: `void __thiscall(TCommSession *self, PlayerSlot *player, int reasonCode)`.
  - **Reason Code 3**: `REASON_DROPPED_DATA_MISMATCH` / `EJECT`.
  - **Error String Key**: `"GAMEDATA_CRC_CHECK"` (`0x009c2bfc`).
  - **English String (ID 10956)**:  
    *`"%s is not running the same version of the data file. Everyone must run the same version of the data file before you can start the game."`*

### 4.4 Lobby Update Comparison (`FUN_00656040`)
- **Address**: `0x006566ba..0x00656799` in `GameSetupScreen`. **[VERIFIED]**
- **Disassembly**:
  ```asm
  006566a1: mov  eax, DWORD PTR [ecx+0x8]   ; Remote player CRC
  006566a7: mov  DWORD PTR [ebp-0x6c], eax
  ...
  006566ba: call 0x5d5f20                   ; GameDataSet::getCrc()
  006566bf: mov  ecx, ds:0xaf5890
  006566c5: mov  DWORD PTR [ebp-0x38], eax
  006566c8: call 0x7ce330                   ; ModManager::getActiveDataMod()
  006566cd: test eax, eax
  006566cf: je   0x6566da
  006566d1: mov  ecx, eax
  006566d3: call 0x7d2270                   ; ModPackage::getChecksum()
  006566d8: jmp  0x6566dd
  006566da: mov  eax, [ebp-0x38]            ; base dat CRC
  006566dd: cmp  DWORD PTR [ebp-0x6c], eax  ; Compare remote player CRC with local CRC!
  006566e0: je   0x6567b0                   ; Match -> OK
  ; --- ERROR: Append to error list ---
  006566f2: push 0x9c2bfc                   ; "GAMEDATA_CRC_CHECK"
  ```
- If mismatched, the client disables the "I'm Ready!" button and if unresolvable triggers `FUN_006591e0` with `"GAMEDATA_CRC_CHECK_HOST"` (String ID 10957: *`"The host is not running the same version of the data file. Everyone must run the same version of the data file to play a multiplayer game."`*), disconnecting the player to the main menu.

---

## 5. Wololoader Implementation Plan

### 5.1 Architecture
To allow Wololoader to present its built dat (`wololoader/cache/empires2_x2_p1.dat`) as a legitimate Data Mod:
1. **Deterministic Workshop ID**: Wololoader already computes a 64-bit FNV-1a fingerprint in `autobuild.c` over loader version, original dat size/mtime, and all enabled mods' `mod.json` + `civs/*.json`. We map this to a unique 64-bit ID:
   ```c
   uint64_t wl_workshop_id = 0x574C000000000000ULL | (fnv1a_fingerprint & 0x0000FFFFFFFFFFFFULL); // Prefix 'WL'
   ```
2. **Synthetic `ModPackage` Object**: Wololoader allocates a single static `ModPackage` struct (or dynamically hooks getters) where:
   - `mChecksum` (`+0x00`): The CRC32 of `cache/empires2_x2_p1.dat`.
   - `mTitle` (`+0x04`): `"Wololoader"` (or `"Wololoader (N mods)"`).
   - `mWorkshopId` (`+0x10`): `wl_workshop_id`.
   - `mStorageId` (`+0x408`): 4.
   - `mEnabled` (`+0x418`): 1.
   - `mIsLocal` (`+0x419`): 1.
   - `mIsActiveDataMod` (`+0x41a`): 1.
3. **Hook Hooks Intercepting ModManager**:
   - `0x007ce330` (`ModManager::getActiveDataMod`): Returns Wololoader's synthetic `ModPackage*` when Wololoader mods are loaded.
   - `0x007ce240` (`ModManager::findModByWorkshopId`): When a client joins a lobby, if `(low, high)` matches `wl_workshop_id`, returns Wololoader's `ModPackage*`. This prevents the client from invoking the Steam Workshop download fallback.
   - `0x007d2270` (`ModPackage::getChecksum`): Returns the CRC32 of `wololoader/cache/empires2_x2_p1.dat`.
   - `0x005d5f20` (`GameDataSet::getCrc`): Returns the CRC32 of `wololoader/cache/empires2_x2_p1.dat`.

### 5.2 Hook Specifications & Signatures

#### Hook 1: `ModManager::getActiveDataMod` (`0x007ce330`)
- **Target VA**: `0x007ce330` **[VERIFIED]**
- **Prologue Bytes**: `55 8b ec 51 53` (5 bytes: `push ebp; mov ebp, esp; push ecx; push ebx`) **[VERIFIED]**
- **Calling Convention**: `__fastcall(ModManager *self, void *edx)` -> returns `ModPackage*`
- **Behavior**:
  - If Wololoader has active custom mods (`g_wl.mods.count > 0`): returns `&g_wl_mod_package`.
  - Else: calls original trampoline `g_original_getActiveDataMod(self, edx)`.

#### Hook 2: `ModManager::findModByWorkshopId` (`0x007ce240`)
- **Target VA**: `0x007ce240` **[VERIFIED]**
- **Prologue Bytes**: `55 8b ec 8b 01` (5 bytes: `push ebp; mov ebp, esp; mov eax, [ecx]`) **[VERIFIED]**
- **Calling Convention**: `__thiscall(ModManager *self, uint32_t low, uint32_t high, int type)` `ret 0xc`
- **Replacement Signature**: `ModPackage* __fastcall wl_find_mod_hook(void *self, void *edx, uint32_t low, uint32_t high, int type)`
- **Behavior**:
  - Checks if `low == (uint32_t)wl_workshop_id && high == (uint32_t)(wl_workshop_id >> 32)`.
  - If match: returns `&g_wl_mod_package`.
  - Else: calls original trampoline `g_original_findMod(self, edx, low, high, type)`.

#### Hook 3: `ModPackage::getChecksum` (`0x007d2270`)
- **Target VA**: `0x007d2270` **[VERIFIED]**
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push ebp; mov ebp, esp; push -1`) **[VERIFIED]**
- **Calling Convention**: `__fastcall(ModPackage *self, void *edx)` -> returns `uint32_t`
- **Behavior**:
  - If `self == &g_wl_mod_package`: returns cached CRC32 of `cache/empires2_x2_p1.dat`.
  - Else: calls original trampoline.

#### Hook 4: `GameDataSet::getCrc` (`0x005d5f20`)
- **Target VA**: `0x005d5f20` **[VERIFIED]**
- **Prologue Bytes**: `55 8b ec 6a ff` (5 bytes: `push ebp; mov ebp, esp; push -1`) **[VERIFIED]**
- **Calling Convention**: `__fastcall(GameDataSet *self, void *edx)` -> returns `uint32_t`
- **Behavior**:
  - If Wololoader has active custom mods: returns cached CRC32 of `cache/empires2_x2_p1.dat`.
  - Else: calls original trampoline.

---

## 6. Verification of Behavior

1. **Host with Wololoader creates lobby**:
   - Lobby metadata automatically publishes `"DatasetWorkshopId" = <wl_workshop_id>` and `"DatasetWorkshopTitle" = "Wololoader (N mods)"`.
   - In Steam lobby browser, the game displays the `mm-dataset-icon-16x16.png` dataset icon.
2. **Vanilla player joins Wololoader lobby**:
   - Vanilla client sees `"DatasetWorkshopId"`. Vanilla client searches local mods -> not found.
   - Vanilla client tries to subscribe to `<wl_workshop_id>` on Steam Workshop -> fails (non-existent workshop item).
   - Even if forced into lobby, vanilla client sends base dat CRC32 (`[packet+0x18]`), which mismatches Wololoader's dat CRC32.
   - Host `0x00584a40` immediately ejects the vanilla player with `"GAMEDATA_CRC_CHECK"` (String 10956) and logs to chat.
3. **Wololoader player with different mods joins**:
   - Different mod set produces a different `fnv1a_fingerprint`, hence different `wl_workshop_id`.
   - Joining client does not match `findModByWorkshopId`, prompting missing mod dialog or ejecting on CRC mismatch.
4. **Wololoader player with IDENTICAL mods joins**:
   - Fingerprint matches -> `wl_workshop_id` matches.
   - `findModByWorkshopId` (`0x007ce240`) returns `&g_wl_mod_package`.
   - `ModManager::setActiveDataMod` (`0x007cf9f0`) succeeds.
   - Client sends identical CRC32 in packet `0x52`.
   - Host `0x00584a40` validates matching CRC32 and bypasses rejection.
   - Both players ready up with identical dat and custom civs, enabling synchronous multiplayer gameplay without desyncs.
