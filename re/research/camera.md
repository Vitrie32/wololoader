# Camera Movement Function Research

## Goal
Find a function to move the local player camera (main map view) to center on a given tile (x, y float).

## Starting Points
- Hotkey 'H' (Go to Town Center, 0x48) check at **0x6d7acc**
- Calls **0x47b1e1** (FUN_0047b1e0) for camera centering
- Known globals: `Game*` at `0xaf2c58`, `Game::get_local_player()` at `0x540c90`
- Player fields: `+0x10` master count, `+0x14` masters**, `+0x1ae4` Civilization*

## Call Chain (Hotkey H)

### 1. Hotkey Handler (0x6d7acc area)
```
0x6d7acc:  cmp eax, 0x48          ; Check for 'H' key
           ...
0x6d7aea:  call 0x47b31f          ; (call target decoded: 0x47b31f)
0x6d7af7:  call 0x47b1e1          ; Camera center call
```

### 2. FUN_0047b1e0 - Camera Center Wrapper
**Address:** `0x47b1e0`  
**Convention:** `__thiscall` (ecx = this pointer)  
**Return:** `ret` (no args popped, void return)  
**Prologue:** `56 8b f1` (push esi, mov esi,ecx)

```asm
0047b1e0: 56                 push   esi
0047b1e1: 8b f1              mov    esi,ecx           ; save this pointer
0047b1e3: 8b 8e 2c 02 00 00  mov    ecx,[esi+0x22c]  ; load object pointer from +0x22c
0047b1e9: e8 22 93 01 00     call   0x594510         ; UNVERIFIED: call decoded target
0047b1ee: 8b 8e 24 02 00 00  mov    ecx,[esi+0x224]  ; load object pointer from +0x224  
0047b1f4: c7 86 38 02 00 00  mov    DWORD [esi+0x238],0xffffffff
           ff ff ff ff
0047b1fe: 5e                 pop    esi
0047b1ff: e9 0c 93 01 00     jmp    0x59450f         ; UNVERIFIED: tail call
```

**Decoded call targets:**
- Call at 0x47b1ea → **0x494511** (UNVERIFIED)
- Tail jmp at 0x47b1fe → **0x49450f** (UNVERIFIED)

**Object structure (this = GameScreen or similar):**
- `+0x224`: Pointer to object (view/camera?)
- `+0x22c`: Pointer to object (view/camera?)
- `+0x238`: Some state field (set to -1)

### 3. FUN_0047b210 - Set View with Coordinates
**Address:** `0x47b210`  
**Convention:** `__thiscall` with 2 stack arguments (floats)  
**Return:** `ret 8` (pops 8 bytes = 2 float arguments)  
**Prologue:** `55 8b ec 53` (push ebp, mov ebp,esp, push ebx)

```asm
0047b210: 55                 push   ebp
0047b211: 8b ec              mov    ebp,esp
0047b213: 53                 push   ebx
0047b214: 68 b0 e6 9a 00     push   0x9ae6b0          ; constant (string table?)
0047b219: ff 75 0c           push   DWORD [ebp+0xc]   ; arg2 (y coordinate?)
0047b21c: 8b d9              mov    ebx,ecx           ; save this
0047b21e: ff 75 08           push   DWORD [ebp+0x8]   ; arg1 (x coordinate?)
0047b221: 8b 8b 24 02 00 00  mov    ecx,[ebx+0x224]  ; load view object pointer
0047b227: e8 24 94 01 00     call   0x59464f         ; UNVERIFIED: set_view call
0047b22c: 8b 8b 2c 02 00 00  mov    ecx,[ebx+0x22c]  ; load another view object
0047b232: 68 b0 e6 9a 00     push   0x9ae6b0
0047b237: ff 75 0c           push   DWORD [ebp+0xc]   ; arg2 again
0047b23a: ff 75 08           push   DWORD [ebp+0x8]   ; arg1 again
0047b23d: e8 0e 94 01 00     call   0x594649         ; UNVERIFIED: set_view call
0047b242: 5b                 pop    ebx
0047b243: 5d                 pop    ebp
0047b244: c2 08 00           ret    0x8               ; pop 8 bytes (2 floats)
```

**Decoded call targets:**
- Call at 0x47b226 → **0x49464f** (UNVERIFIED)
- Call at 0x47b236 → **0x594649** (UNVERIFIED, typo in my earlier output: should be 0x494649)

**Arguments:**
- `[ebp+0x8]` = float X coordinate
- `[ebp+0xc]` = float Y coordinate
- Constant `0x9ae6b0` pushed before each call (purpose unknown)

**This object structure:**
- `+0x224`: View object pointer (receives set_view call)
- `+0x22c`: Another view object pointer (also receives set_view call)

## Camera Movement Functions (UNVERIFIED)

### FUN_00494510 (141 bytes)
**Address:** `0x494510`  
**Convention:** UNVERIFIED  
**Return:** UNVERIFIED

This function is called by the H-hotkey wrapper to center the camera. Full disassembly needed.

### Set View Function (around 0x494649)
**Address:** `0x494649` or `0x49464f`  
**Convention:** Likely `__thiscall(view*, unknown, float x, float y)` or similar  
**Return:** UNVERIFIED

Called twice by FUN_0047b210 with float coordinates and a constant 0x9ae6b0.

## How to Obtain `this` Pointer

The H-hotkey handler operates on an object (likely GameScreen) at `ecx`. To call from our DLL:

1. **UNVERIFIED:** Need to find the global pointer to GameScreen or the active screen
2. Known globals that might lead to it:
   - `Game*` at `0xaf2c58`
   - Screen manager at `0xada4b4` (from FINDINGS.md MainMenuScreen section)
   - Current screen via `ScreenManager::get_current` at `0x43c0d0`

3. Once we have the GameScreen pointer, we can either:
   - Call `FUN_0047b210(screen, float x, float y)` with `__thiscall` + 2 float args, `ret 8`
   - Or directly call the view object's set_view method if we can verify the calling convention

## Player View Coordinates (NOT FOUND YET)

The original Genie engine had `RGE_Player` with float `view_x`, `view_y` fields. These have not been located yet in the Player object structure.

**Known Player structure (from FINDINGS.md):**
- Base from `Game::get_local_player()` at `0x540c90`
- `+0x10`: master count
- `+0x14`: masters**
- `+0x35`: victory flag byte
- `+0xf8`: alliance related
- `+0x1ae4`: Civilization*

**Next steps to find view coordinates:**
1. Disassemble the actual set_view functions (0x494649, etc.)
2. Look for `movss [ecx+offset], xmm` instructions storing float coordinates
3. Trace back from the minimap click handler to find coordinate storage

## Evidence Summary

### Verified in Disassembly:
- FUN_0047b1e0 prologue and structure: `56 8b f1`
- FUN_0047b210 prologue and ret: `55 8b ec 53`, `ret 8`
- Object offsets `+0x224`, `+0x22c`, `+0x238` read from disassembly
- Call displacement calculations verified with Python

### UNVERIFIED (need objdump or actual testing):
- Exact addresses of camera functions (0x494510, 0x494649, etc.) - calculated from call displacements but not seen in objdump yet
- Calling conventions of the actual camera movement functions
- Purpose of constant 0x9ae6b0
- Return types
- How to obtain the GameScreen `this` pointer from globals
- Player view_x/view_y float offsets

## Recommended Function to Call

**FUN_0047b210** at `0x47b210`:
```c
typedef void (__thiscall *SetCameraView_t)(void *screen, float x, float y);
// Convention: __thiscall, ret 8
// Arguments: ecx = screen pointer, [ebp+8] = x, [ebp+0xc] = y
```

**To use:**
1. Get GameScreen pointer (method TBD - check `0xada4b4` screen manager + `0x43c0d0` get_current)
2. Cast and call: `((SetCameraView_t)0x47b210)(screen, tile_x, tile_y);`

## Next Steps

1. Use objdump to verify the actual bytes at 0x494510, 0x494649
2. Disassemble those functions to find float store instructions
3. Find the global pointer to GameScreen (likely in screen manager at 0xada4b4)
4. Test in-game by calling FUN_0047b210 from DllMain or a hotkey hook
5. Search for Player view_x/view_y by looking for float pairs that change when camera moves
