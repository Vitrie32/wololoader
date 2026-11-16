# Is the current game multiplayer? (AoK HD 5.8)

Static analysis only (objdump of exe.s). Not verified at runtime.

## Answer (primary)

The game settings object has a serialized field named `isMultiPlayer` at offset `0x408`. Its value is 1 for a multiplayer game.

```c
// game = active TRIBE_Game* (global 0xaf2c58, see lua_hooks.md:76)
int game = *(int*)0xaf2c58;
int settings = *(int*)(*(int*)(game + 0x98) + 0xd54);   // game settings object (pointer)
int is_mp = settings && *(int*)(settings + 0x408) == 1;
```

Single player, campaign and scenario give 0 (the setter at 0x611d10 stores `(bool)!=0`, and the reader tests `== 1`).

## Evidence

1. String `&isMultiPlayer` at file 0x5cc06c = VA 0x9ccc6c. Only reference: `push $0x9ccc6c` at 0x611cc3, inside the settings (de)serializer (function starting 0x610f30, ecx = settings object saved at -0xcc(%ebp)). Neighbouring field names: mTeamsLocked, mSpeedLocked, isMultiPlayer, mCheatsEnabled, mRecordGameEnabled.
   ```
   611cab: cmpl $0x1,0x408(%eax)      ; eax = settings
   611cbf: sete -0x4e(%ebp)           ; value written under name "isMultiPlayer"
   611d08: setne %al
   611d10: mov  %eax,0x408(%ecx)      ; load side
   ```
2. How the settings object is fetched (same sequence at 0x630e0b, 0x5a70c0 and about 2100 `mov 0xaf2c58` sites):
   ```
   5a70c0: mov  0xaf2c58,%ecx         ; TRIBE_Game*
   5a70ca: call 0x5408c0              ; ecx=game, [esp]=out shared_ptr, ret 4
   5a70d8: mov  (%eax),%eax           ; eax = settings*
   5a70dd: cmpl $0x1,0x408(%eax)
   5a70e4: sete %cl                   ; stored to this+0x2c
   ```
   `0x5408c0` does `ecx=[game+0x98]; call 0x583640` where `0x583640` is `lea 0xd54(%ecx),%eax; ret`. It copies the shared_ptr at `[game+0x98]+0xd54` into the out param (object pointer at +0, refcount at +4, `incl 4(%ecx)`). So the object pointer is `*(int*)(*(int*)(game+0x98)+0xd54)`.
3. Callers use the field to separate the two modes:
   - 0x6379a6 `cmpl $0,0x408(%esi)` / 0x637b55 `cmpl $1,0x408(%esi)` in the multiplayer launch (0x637920) take different branches (lobby/comm setup vs SP setup).
   - 0x630e23 `cmpl $1,0x408(%eax)` then `call 0x57d900` on `[this+0x98]` only in MP.
   - 0x62261d passes `sete (0x408==1)` to `0x61dad0`.

## Confidence

- Field meaning (isMultiPlayer, 0x408): high, named by the game's own serializer string.
- Pointer chain: high for the code path (same pattern copied from the game's own callers), but the `+0x98` / `+0xd54` offsets were read from disassembly and not tested in game.
- Possible caveats: a recorded-game replay of a multiplayer game or a lobby game with 1 human may still have the flag 1 (it means "created as multiplayer", not "more than one human"). Guard against `game == 0` and `*(int*)(game+0x98) == 0` before reading (before a game exists they may be null).

## Backup

Not fully traced: the comm handler (`commhandler.cpp`, string 0x5c1cf8) object, and the `0x57d900` call on `[game+0x98]` guarded by the MP flag at 0x630e32. A cheap runtime cross check is to log the flag in both SP and MP once; 0 / 1 respectively is the expectation.

## Summary
- Flag: `isMultiPlayer`, int at settings+0x408, 1 = multiplayer.
- settings = `*(int*)(*(int*)(*(int*)0xaf2c58 + 0x98) + 0xd54)`.
- Expression: `s && *(int*)(s+0x408) == 1` (null-check each step).
- Proof: serializer 0x611cab/0x611d10 and getter pattern 0x5a70c0..0x5a70dd, 0x5408c0, 0x583640.
- Confidence: high on the field, medium-high on offsets; untested in game, flag means "MP game type" not human count.
