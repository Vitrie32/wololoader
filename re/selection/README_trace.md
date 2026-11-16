# Selection Field Trace — Quick Start

## Overview

Runtime instrumentation system that records every instruction accessing player selection fields (+0x15c to +0x258). Used to validate static analysis and find missed accesses before patching for 60→100 selection limit increase.

## Build

```bash
# Normal build (production, no overhead)
cd wololoader
make

# Trace build (debug, records all accesses)
make clean
make TRACE=1
```

Both builds complete with **zero warnings**.

## Install and Run

```bash
# Install trace build (DON'T run 'make install', user will test manually)
# The user must:
# 1. Copy build/version.dll to game folder
# 2. Ensure Steam launch options have: WINEDLLOVERRIDES="version=n,b"
# 3. Start the game
# 4. Play normally for 5-10 minutes (select units, give orders, use hotkeys)
# 5. Exit the game

# Output will be in:
# <game>/wololoader/seltrace.txt
# <game>/wololoader/wololoader.log (contains trace status messages)
```

## Analyze Results

```bash
# Compare with static analysis
python3 tools/seltrace_compare.py \
    "<game dir>/wololoader/seltrace.txt" \
    re/selection/

# Output:
# - Addresses found at runtime but not in static analysis (CRITICAL)
# - Addresses in static analysis never hit (informational)
# - Summary statistics
```

## Expected Output

**seltrace.txt format**:
```
# Selection field access trace
# Format: eip hits
# Total unique instructions: 120
# Tracked players: 2
#   Player 0: 0x12345678 [0x123457d4, 0x123458b0)
#   Player 1: 0x12346000 [0x1234615c, 0x12346338)

00754b23 1234
00754bb0 567
00755722 89
...
```

**Log output** (wololoader.log):
```
[SELTRACE] Runtime trace initialized
hook 00754a20 -> 0x12340000 (tramp 0x12341000)
[SELTRACE] Tracking player 1 at 0x12345678, range [123457d4, 123458b0)
[SELTRACE] Protected page 12345000
[SELTRACE] Protected page 12346000
[SELTRACE] Dumped 45 unique instructions (2345 total hits) to ...
[SELTRACE] Dumped 89 unique instructions (5678 total hits) to ...
[SELTRACE] Shutting down, dumping final trace
[SELTRACE] Dumped 120 unique instructions (12345 total hits) to ...
```

## Troubleshooting

### No player objects tracked
**Symptom**: Log shows "Runtime trace initialized" but no "Tracking player" messages  
**Cause**: select_unit never called (unlikely, but possible if hook failed)  
**Fix**: Check that 0x754a20 has bytes `55 8b ec 56 57`

### Trace file empty or very few instructions
**Symptom**: seltrace.txt has < 10 instructions  
**Cause**: User didn't select units during gameplay  
**Fix**: Play longer, use drag selection, hotkeys, control groups

### Game crashes on startup
**Symptom**: Game exits immediately after trace build installed  
**Cause**: Hook address mismatch (wrong exe version)  
**Fix**: Verify `is_supported_exe()` returns true (SizeOfCode = 0x5ab000, ImageBase = 0x400000)

### Performance issues
**Symptom**: Game lags with trace build  
**Cause**: PAGE_GUARD overhead (expected)  
**Fix**: This is a debug build, acceptable for short test runs

## What to Look For

### Good result
```
=== SUMMARY ===
In both:          90 addresses
Runtime only:      0 addresses (MISSED by static analysis)
Static only:      15 addresses (never hit)
✓ Runtime trace validates static analysis (no missed accesses)
```

### Bad result (needs investigation)
```
=== SUMMARY ===
In both:          85 addresses
Runtime only:      8 addresses (MISSED by static analysis)  <-- PROBLEM
Static only:      15 addresses (never hit)
⚠️  WARNING: Runtime trace found addresses not in static analysis!
```

If "Runtime only" > 0, those addresses **must** be analyzed and added to the patch list before proceeding with the selection limit increase.

## Files

- `src/seltrace.c` - Runtime trace implementation
- `tools/seltrace_compare.py` - Analysis tool
- `re/selection/trace_design.md` - Full design documentation
- `re/FINDINGS.md` - Verified addresses (section on selection)
- `re/selection/plan_agy.md` - Static analysis results
- `re/selection/selpart_*.md` - Function classifications

## Next Steps After Trace

1. Review comparison output
2. If runtime-only addresses found: analyze in disassembly, add to static list
3. If all validated: proceed to Phase 2 (patching implementation)
4. Document any new functions found in re/selection/

---

**Note**: Do NOT run `make install` without user approval. Never write to game folder from agent code.
