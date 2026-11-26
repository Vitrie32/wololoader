# Custom Random Map Script Selection in AoE2 HD 5.8

**Goal**: Force custom RMS selection from DLL before match launch (GameSetupScreen::start_clicked hook).

## GameSettings Structure Offsets

From serialization function at **0x617c30** (FUN_00617c30, line 423541):

```c
struct GameSettings {
    // ... (earlier fields)
    +0x18  int mRegion;
    +0x1c  int mGameMode;
    +0x20  ? mMapStyle;              // serialized with "&mMapStyle" key
    +0x24  int mMapType;              // serialized with "&mMapType" key
    +0x28  int mMapSize;
    +0x2c  int mResourceLevel;
    +0x30  ? mPopCap;
    +0x34  int mStartingAge;
    +0x38  int mEndingAge;
    +0x3c  int mGameSpeed;
    +0x40  int mVictoryCondition;
    +0x44  int mRankedType;
    +0x48  bool mAllowCheats;
    // ...
    +0x94  int mResolvedMapType;      // see FUN_0060e400
    +0x98  int mUserChosenMapType;    // see FUN_0060e400
};
```

**Note**: The type of mMapStyle (+0x20) is unclear from serialization alone. FUN_0060a950 is used to serialize it, same as for int fields, but code at 0x60e730 (line 415348) calls FUN_00432970 on param_1 + 0x24, suggesting std::string operations nearby. **UNVERIFIED**.

### Existing Setters (from autostart.c)

- **0x613650** (ret 8): `void __thiscall(void *settings, int map_type)` - sets +0x98 mUserChosenMapType
- **0x6135b0** (ret 8): `void __thiscall(void *settings, int resolved)` - sets +0x94 mResolvedMapType  
  **(Both UNVERIFIED in disassembly)**

## Map Type Constants

From map type enum initialization at **0x41c6e0** (line 17918):

```c
#define MAPTYPE_CUSTOM 0x3b  // 59 decimal
```

**String**: "MAPTYPE_CUSTOM" at VA 0x9c5f38 (file offset 0x5c5f38)

### Map Type Resolution Function

**0x60e400** FUN_0060e400 (lines 415138-415149):
```c
int __fastcall FUN_0060e400(int settings) {
    int map_type = *(int *)(settings + 0x94);  // mResolvedMapType
    if (map_type == -3) {
        map_type = *(int *)(settings + 0x98);  // mUserChosenMapType
    }
    return map_type;
}
```
**UNVERIFIED**: prologue/epilogue not checked.

## Custom RMS Detection

Function at **0x60e730** (line 415344-415353):
```c
// When map type is CUSTOM (0x3b):
if (FUN_0060e400(settings) == 0x3b) {
    *param_2 = *(settings + 0x20);        // copy something at +0x20
    FUN_00432970(settings + 0x24);        // std::string op at +0x24
    *param_3 = *(settings + 0x34);        // copy something at +0x34
    FUN_00432970(settings + 0x38);        // std::string op at +0x38
}
```

**FUN_00432970**: std::string copy/assignment function (seen at lines 423891, 423898).  
**UNVERIFIED**: This function and its calling convention not checked in disassembly.

## RMS File Resolution

### Search Paths

The game searches for .rms files in:
```
resources\_common\random-map-scripts\
```
String VA: 0x9d9fb8 (file offset 0x5d9fb8), referenced at:
- Line 608641 (0x724000 region): file open/resource loading
- Line 752944 (0x825000 region): resource enumeration

### Script Name Validation

Function at **0x5e21e0** (line 379934, FUN_005e21e0):
- Parameter `param_3` is the script name (char*)
- Line 379981-379982: checks for empty/null script name:
  ```c
  if ((param_3 == NULL) || (*param_3 == '\0')) {
      FUN_00430a20(&DAT_009ae6b0, 0, "Empty rms script name");
  }
  ```
- Line 380118: Error message for missing script:
  ```c
  FUN_00465be0(param_1, 0, &DAT_009ae6b0, 0, "No such map script: '%s'\n", param_3);
  ```

**UNVERIFIED**: Function boundaries and calling convention.

## Custom RMS Serialization Keys

Found in strings but **NOT FOUND in decompiled code**:

- VA 0x9cbf20 (file 0x5cbf20): `"&customRandomMapFileName"`
- VA 0x9cc2e0 (file 0x5cc2e0): `"&customRandomMapFileCrc"`
- VA 0x9cc318 (file 0x5cc318): `"&mCustomRandomMapFile"`
- VA 0x9cc330 (file 0x5cc330): `"&mCustomRandomMapScenarioFile"`

These strings exist in the executable but their usage is not found in the decompiled sections searched. They may be:
1. In code sections not yet decompiled
2. In version-gated serialization not visible in this build
3. Dead code from earlier versions

## Unknowns / TODO

1. **Custom script filename field**: Where is the actual custom RMS filename stored in GameSettings?
   - Likely a std::string field somewhere after +0x48
   - Serialization key would be one of: "&customRandomMapFileName", "&mCustomRandomMapFile", etc.
   - **NOT LOCATED YET**

2. **mMapStyle field type**: Is +0x20 an int or std::string?
   - Serialized with "&mMapStyle" key
   - Code suggests both int and string operations nearby
   - **UNVERIFIED**

3. **Setter functions**: Need to find functions that set:
   - mMapStyle (+0x20)
   - Custom script name field (unknown offset)
   - **NOT LOCATED YET**

4. **Multiplayer behavior**:
   - Does the custom script content get sent to clients?
   - Is there a CRC check (suggested by "&customRandomMapFileCrc")?
   - **NOT INVESTIGATED**

5. **File extension**: Does the game expect ".rms" extension or is it added automatically?
   - Based on error message format ("No such map script: '%s'"), likely expects the name without extension
   - **NOT VERIFIED**

## Disassembly Verification Status

**None of the addresses have been verified in disassembly yet.** All findings are from:
- Decompiled pseudo-C (re/out/decompiled.c)
- String searches in the executable
- Cross-referencing with existing verified code (autostart.c, gamemodes.c)

Before implementation, verify:
- Function prologues (push ebp; mov ebp, esp or equivalent)
- Return instructions (ret N values)
- Calling conventions (__thiscall vs __cdecl)
- Field offsets by checking actual memory accesses

## Recommended Next Steps

1. Use objdump to verify:
   - 0x60e400 (map type resolution)
   - 0x60e730 (custom map check)
   - 0x617c30 (serialization function)

2. Search for xrefs to the custom RMS string addresses (0x9cbf20, 0x9cc318, etc.) in objdump output

3. Reverse engineer from the error messages:
   - Find "Empty rms script name" xrefs
   - Find "No such map script" xrefs
   - Trace backwards to find where script name is passed

4. Test hypothesis: Set mMapType to 0x3b and see what happens without setting script name

---

**Status**: Partial findings. Can set map type to CUSTOM (0x3b), but custom script filename field location unknown.
