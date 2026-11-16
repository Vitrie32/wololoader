# Unit selection limit (60) — research, ON HOLD (2026-11-14)

Goal was 60 -> 100. Selected units live inside the player object
(`+0x15c` primary, `+0x160..+0x24c` `selected[60]`, `+0x254` count, fields follow at `+0x258`),
so the array must be relocated and every access patched.

- `plan_agy.md`: first agy plan (limits, global buffers 0xde0430 / 0xaf4e40 / 0xafb510, command cache, packet byte count). Partially verified.
- `selpart_00..04.md`: agy classification of the 90 functions found by `re/scripts/selscan.py`
  (disp 0x254 + something in 0x15c..0x250): 34 TRUE.
- Known gap: the scan only finds functions that also touch the count; e.g. the selection panel
  draw `0x716360` (uses 0x160 / 0x24c without 0x254) is missing. 10274 instructions in .text use
  displacements 0x160..0x24f, so a complete static list could not be guaranteed.

Why on hold: one missed access = wrong orders / memory corruption / random crash. Needs either a
proven-complete access list or a runtime method (e.g. instrumenting accesses) before patching.
