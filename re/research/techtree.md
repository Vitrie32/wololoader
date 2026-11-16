# Tech tree records for castle unique units (2026-11-14, verified with datfile.py on the game dat)

The dat `tech_tree` section (after researches) is shared by all civs: counts ages 4, buildings 30,
units 133, researches 140. Visibility per civ comes from the `enabling_research` / `required_research`
techs (their `civ` field); the screen hides units whose enabling tech belongs to another civ.

Britons example (castle = building 82):
- TechTreeUnit 8 (Longbowman): upper_building 82, units [530], line_mode 2, enabling_research 263
  ("Longbow (make avail)", civ 1), location_in_age 1, vertical_line = slot of this UU line.
- TechTreeUnit 530 (Elite Longbowman): units [], required_research 360 ("Elite Longbow"), line_mode 3,
  enabling_research -1, vertical_line 63, connected_slots_used 2, other_connected_ids [4, 8, ...],
  other_connections [0, 2, ...].
- TechTreeBuilding 82 `units` lists every castle unit incl. 8 and 530 (order = layout order);
  its `researches` list holds unique techs, not the elite upgrades.
- TechTreeAge 3 `units` contains 8 (Castle Age), age 4 holds the elite.
- Every UU pair has its own vertical_line (87..95 for the newer civs).

Wololoader custom civ unique unit (U, E = 1224+2k, techs avail/elite appended at the end) has NO
records here -> the tech tree screen misplaces lines (seen in-game).

Fix plan (both builders, byte-identical):
1. Parse the tech tree section (datfile.py TechTreeAge/Building/Unit/Research layouts).
2. For each civ with unique_unit: find the base civ's UU TechTreeUnit pair (enabling_research ==
   base avail tech; its units[0] is the elite). Clone both: base clone id=U, units=[E],
   enabling_research=new avail id; elite clone id=E, required_research=new elite id, replace U's
   base id with U in other_connected_ids. Keep vertical_line (the base UU is hidden for the custom
   civ, so the slot is free).
3. Insert U and E into TechTreeBuilding 82 `units` right after the base pair, and into the same
   TechTreeAge `units` lists where the base pair appears.
4. Append the new TechTreeUnit records after the original units; update counts.
