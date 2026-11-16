# Dev Features Research: Mouse-to-Tile & Map Visibility

**Status:** INCOMPLETE - Unable to provide complete recipes  
**Date:** 2026-11-15  
**Target:** AoE2 HD 5.8 (AoK HD.exe, 32-bit, ImageBase 0x400000)

## Summary

**Mouse-to-Tile Conversion:** Found panel input handling code but could not trace to actual tile coordinate conversion function. The conversion likely occurs in map view methods, but the specific vtable slot and function signature remain unknown.

**Map Reveal / Fog System:** Could not locate. Traditional cheat codes ("marco", "polo") are not present in the executable. The visibility system structure was not found through static analysis.

**Recommendation:** Use runtime debugging (attach debugger to running game, set breakpoints on input handling) to trace execution flow and locate these features.

---

## Known Facts (from FINDINGS.md)

- World pointer: `[[0xaf2c58]+0xc8]`
- GameScreen pointer: `[0xaf3d90]`
- GameScreen vtable: `0x9d5300` (size 0x1390)
- Map view: `[gamescreen+0xb04]` (main map view panel)
- Panels have rect x,y,w,h at `+0xb8/+0xbc/+0xc0/+0xc4`
- Local player id: `[world+0x174]`
- Player table: `[world+0x184]` (8-byte entries)
- Game time ms: `[world+0x178]`

---

## 1. Mouse-to-Tile Coordinate Conversion

### Mouse Input Handling (VERIFIED)

**Function:** `0x488000` (panel input dispatcher)  
**Calling convention:** Unknown (appears to be part of window message processing)

Relevant code at `aok.dis:186272-186303` (`0x488102`-`0x488168`):

```asm
  488102:  cmp    $0x201,%esi        # WM_LBUTTONDOWN
  488108:  je     0x488122
  48810a:  cmp    $0x202,%esi        # WM_LBUTTONUP
  488110:  je     0x488122
  488112:  cmp    $0x204,%esi        # WM_RBUTTONDOWN
  488118:  je     0x488122
  48811a:  cmp    $0x205,%esi        # WM_RBUTTONUP
  488120:  jne    0x48816a
  488122:  mov    0xada4b4,%ecx      # Screen manager global
  488128:  mov    0xb8(%ebx),%edi    # Panel x offset
  48812e:  mov    0xbc(%ebx),%esi    # Panel y offset
  488134:  call   0x48a8b0            # Get current screen
  488139:  mov    0xc(%ebx),%ecx
  48813c:  mov    %eax,%edx
  48813e:  mov    %edx,-0x34(%ebp)
  488141:  mov    0x8(%ecx),%eax
  488144:  mov    (%edx),%ebx        # Panel vtable
  488146:  mov    0x14(%ebp),%ecx    # lParam (packed x,y)
  488149:  mov    %ecx,%edx
  48814b:  shr    $0x10,%edx         # Extract y
  48814e:  add    %edi,%ecx          # Add panel x offset
  488150:  add    %esi,%edx          # Add panel y offset
  488152:  movzwl %cx,%ecx           # Mask to 16-bit x
  488155:  shl    $0x10,%edx         # Shift y back
  488158:  or     %ecx,%edx          # Repack coordinates
  48815a:  mov    -0x34(%ebp),%ecx
  48815d:  push   %edx               # Panel-relative coords
  48815e:  push   -0x3c(%ebp)
  488161:  push   0xc(%ebp)
  488164:  push   %eax
  488165:  call   *0x48(%ebx)        # Panel input handler (vtable+0x48)
```

**What this shows:**
1. The game checks for mouse button messages (down/up, left/right)
2. Extracts x,y from lParam (standard Windows message format)
3. Adjusts coordinates from window-relative to panel-relative by adding panel offsets (`panel+0xb8`, `panel+0xbc`)
4. Calls panel's input handler at vtable slot 0x48 (18 in decimal, divided by 4)

**What's missing:**
- The actual conversion from screen pixels to world tile coordinates likely happens inside the map view's input handler
- Map view vtable unknown (need to find map view object initialization in GameScreen constructor)
- No direct evidence of coordinate conversion function signature

### GameScreen Constructor (VERIFIED)

**Address:** `0x6b35b0`  
**Calling convention:** `__thiscall(this ecx)`, `ret 4`, returns this in eax  
**Size:** 0x1390 bytes (from allocation at `aok.dis:731813`)

Code excerpt from `aok.dis:731813-731828` (`0x62e7e8`-`0x62e81e`):

```asm
  62e7e8:  push   $0x1390           # GameScreen size
  62e7ed:  movb   $0x0,0xaf3a98
  62e7f4:  call   0x8d47df          # operator new
  62e7f9:  mov    %eax,%esi
  62e7fb:  add    $0x4,%esp
  62e7fe:  mov    %esi,-0x18(%ebp)
  62e808:  test   %esi,%esi
  62e80a:  je     0x62e825
  62e80c:  push   $0x1390
  62e811:  push   $0x0
  62e813:  push   %esi
  62e814:  call   0x8d62f0          # memset
  62e819:  add    $0xc,%esp
  62e81c:  mov    %esi,%ecx
  62e81e:  call   0x6b35b0          # GameScreen constructor
```

Vtable assignment at `aok.dis:888894` (`0x6b3618`):
```asm
  6b3618:  movl   $0x9d5300,(%edi)  # Write vtable to GameScreen
```

**Map view initialization:** NOT FOUND in preliminary scan of constructor. The offset `+0xb04` is deep into the GameScreen structure. Further analysis needed to find where the map view object is constructed and what its vtable is.

### Float Constants (VERIFIED in exe data sections)

Found via string/data search:
- `48.0` at VAs `0xa37c26`, `0xe12d7b` — possibly tile size in pixels (UNVERIFIED)
- `0.5` at multiple VAs — common for coordinate centering
- Map dimensions: `120.0` at `0x4adb02`, `0x4adbf3`, `0x9b0248`, `0xa530e2`
- `150.0` at `0x9d3924`, `0xa87a1e`
- `200.0` at `0x68d948`, `0x68d95b`, `0x9d3928`

These float constants suggest coordinate calculations exist, but specific usage contexts not determined.

### Incomplete Recipe

**To convert mouse position to tile coords (UNVERIFIED - critical gaps remain):**

1. Get GameScreen: `screen = *(void**)0xaf3d90`
2. Verify screen is non-null
3. Get map view: `mapview = screen + 0xb04` (or follow pointer if it's indirect)
4. **[GAP]** Determine map view vtable and locate coordinate conversion method
5. **[GAP]** Determine if input is pixel coords or if mouse position must be obtained first
6. **[GAP]** Call conversion method with correct signature
7. **[GAP]** Interpret output (likely two floats for tile x, y)

**Critical unknowns:**
- Map view vtable address
- Coordinate conversion method vtable slot
- Function signature (arguments, return type, calling convention)
- Whether there's bounds checking (is point over map view?)
- Whether camera/scroll position affects conversion

---

## 2. Map Reveal / Fog of War

### Cheat Codes (VERIFIED ABSENT)

Searched for traditional AoE2 cheat strings in executable:
- "marco" (reveal map): **NOT FOUND**
- "polo" (remove fog): **NOT FOUND**  
- "aegis", "rock on", "lumberjack", "robin hood", "cheese steak jimmy", "woof woof", "black death": **NOT FOUND**

**Conclusion:** AoE2 HD 5.8 either:
1. Removed cheat codes entirely
2. Uses a different cheat system (console commands, not hardcoded strings)
3. Cheats are debug-only and stripped from release build
4. Strings are obfuscated/encrypted

### Visibility System (NOT FOUND)

Searched for visibility-related terms:
- "fog", "visibility", "explored", "reveal", "sight", "vision": **NOT FOUND**

Suggests either:
- Debug strings were stripped
- Internal names use abbreviations or non-English terms
- System is entirely code-based without string constants

### Player Structure (PARTIALLY KNOWN)

From FINDINGS.md:
- Player table: `[world+0x184]` (8-byte entries, first entry is player pointer)
- Local player id: `[world+0x174]`

**UNVERIFIED hypothesis:** Each player has a visibility structure containing:
- Explored tiles bitmap (which tiles have ever been seen)
- Visible tiles bitmap (which tiles are currently in sight)
- Unit line-of-sight data

**No evidence found for:**
- Offset of visibility data within player structure
- Format of visibility bitmaps
- Functions that update visibility
- Functions that toggle fog/reveal map

### Map Tile Iteration Patterns (NOT SEARCHED)

Did not search for loops iterating over map dimensions, as this would require extensive code analysis to distinguish map update loops from other iteration patterns.

### Incomplete Recipe

**Cannot provide recipe for toggling fog/visibility** due to lack of findings. Would need:
1. Player visibility structure offset
2. Format of visibility data (bitmap? byte array? bit per tile?)
3. Function to set/clear visibility flags
4. OR command packet type for fog toggle (if implemented as multiplayer-safe command)

---

## 3. Alternative Investigation Approaches

### Runtime Debugging (RECOMMENDED)

Since static analysis reached a dead end, runtime debugging would be more effective:

**For mouse-to-tile conversion:**
1. Attach debugger to running game
2. Set breakpoint on `0x488122` (panel input handler)
3. Right-click on map
4. Step through calls to find coordinate conversion
5. Inspect function arguments and return values

**For visibility system:**
1. Search game memory for patterns matching map tile arrays
2. Use Cheat Engine or similar to find "explored" or "visible" flags
3. Locate functions that read/write visibility data
4. Test memory modifications to toggle fog

### Command System Investigation

**NOT DONE:** Search for command packet processing code. Commands in AoE2 are synchronized across multiplayer via numbered command packets. Fog toggle might be command type rather than direct state modification.

**Search strategy:**
- Find command packet parsing (large switch statement or function table)
- Look for command enumeration
- Check if any commands relate to visibility/fog

### Memory Structure Analysis

**NOT DONE:** Use runtime tools to:
- Dump player structure
- Identify all fields and their types
- Locate visibility data by experimentation (reveal map in scenario editor, compare memory)

---

## Conclusion

**Deliverables:**
1. ✗ Mouse-to-tile conversion recipe: **INCOMPLETE** - found input handling but not conversion function
2. ✗ Map reveal/fog toggle recipe: **NOT FOUND** - no evidence of traditional cheats or visibility system

**Key Findings:**
- Panel input handling at `0x488000`, dispatches to panel vtable+0x48 at `0x488165`
- GameScreen constructor at `0x6b35b0`, vtable `0x9d5300`
- Traditional cheat codes confirmed absent from executable
- No visibility-related strings found

**Remaining Work:**
- Trace from panel input handler to find map view's coordinate conversion method
- Find map view vtable and method signatures
- Locate player visibility structures
- Search for command packet system
- Consider runtime debugging as primary investigation method

**Why Static Analysis Failed:**
- Stripped binary (no symbols or function names)
- Complex vtable-based architecture makes tracing difficult
- Cheat/debug features may be compile-time removed
- Visibility system may use terse internal names

**Next Steps:**
1. Use runtime debugger to trace right-click → coordinate conversion
2. Memory scan for visibility bitmaps during gameplay
3. Analyze command packet format if multiplayer-safe fog toggle exists
4. Check if HD version has built-in debug console
