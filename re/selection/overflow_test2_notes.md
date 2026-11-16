# Overflow Selection Stage 2 — Test 2 Notes

**Date:** 2026-11-15 (02:38 - 03:00 UTC+3)  
**Game:** Age of Empires II HD 5.8 (Proton / Xvfb :77)  
**Binary:** Wololoader v0.3 with overflow selection Stage 2 (`wl_install_overflow()`)  
**Raw logs:** [overflow_test2.txt](../../re/selection/overflow_test2.txt)

---

## 1. Test Setup & Execution

1. **Launch:** Game started via `wololoader/tools/run_hidden_game.sh` on display `:77` (1366x768).
2. **Setup:** Single Player -> Standard Game with civilization Huns.
3. **Cheats enabled:** `aegis`, `cheese steak jimmy's`, `robin hood`.
4. **Villager batch creation:** Town Center selected, trained ~86 villagers in rapid succession with `aegis`. Total population reached 90/500 (86 spawned villagers + 3 starting villagers + 1 starting scout).
5. **Selection:**
   - Shift + box-drag selected exactly 60 units (the engine hard limit: 56 villagers + 4 sheep).
   - Shift + double-click on villager at (290, 175).
   - The game attempted to select all remaining on-screen villagers of that type. The engine refused 33 units beyond 60.
   - Wololoader caught all 33 refused units in the overflow list (`overflow list now 33`).
   - Total active group: 60 selected in-engine + 33 overflow = 93 units.

---

## 2. In-Game Movement Orders & Results

Three sequential right-click move commands were issued to empty terrain:

### Move 1 (Spot 1: x=950, y=350 — far right snow field)
- **Log:**
  ```text
  [02:56:59] overflow: sent clone type=0x00 units=33 (order had 60 ids, selected=60, overflow=33)
  ```
- **Visual Result:** After 10 seconds, all units marched in unison towards the right. The 33 overflow villagers marched side-by-side with the 60 in-engine villagers.

### Move 2 (Spot 2: x=400, y=500 — bottom-center snow field)
- **Log:**
  ```text
  [02:57:56] overflow: sent clone type=0x03 units=33 (order had 60 ids, selected=60, overflow=33)
  ```
- **Visual Result:** The massive formation smoothly pivoted and walked down/left towards the destination. Around the Town Center, **zero villagers were left behind**; every single villager participated in the movement.

### Move 3 (Spot 3: x=700, y=200 — top-right snow field)
- **Log:**
  ```text
  [02:58:53] overflow: sent clone type=0x00 units=33 (order had 60 ids, selected=60, overflow=33)
  ```
- **Visual Result:** The entire horde pivoted upwards towards the destination and advanced as a cohesive unit.

---

## 3. Verification & Stability

- **Villager counts:**
  - In-engine selected: **60**
  - Overflow tracked: **33**
  - Total marching: **93** (all 89 villagers + 4 sheep)
- **Did extra villagers move?**
  - **YES**. Confirmed in all 3 moves visually across screenshots (`move1.png`, `move2.png`, `move3.png`). No villagers remained idling at the Town Center.
- **Errors / Warnings:**
  - `clone NOT sent`: **0** (none)
  - `EXCEPTION` lines: **0** (none)
  - Crashes: **0** (none)
- **Packet Types Cloned:**
  - Cloned order type `0x00` (move with waypoint) succeeded twice.
  - Cloned order type `0x03` (move command) succeeded once.

---

## 4. Conclusion

Stage 2 overflow selection works seamlessly. Cloned command packets are received and processed by the game engine for units outside the standard 60-unit selection buffer without crashes, desyncs, or dropped orders.
