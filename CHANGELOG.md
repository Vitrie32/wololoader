# Wololoader changelog

## 0.7.3

- **What's new**: click the Wololoader sign at the top right of the main menu to read this changelog in the game.
- **Updates from the game**: when a newer Wololoader is out, the main menu says so. The window shows its release notes and an "Update now" button: the setup is downloaded and installs the update after you quit the game. Coming from 0.7.2 or older, run the setup once by hand; from 0.7.3 on the game updates itself.
- **Last Stand 2.5**:
  - The Horde grows up with the rounds: Militia become Men-at-Arms, Long Swordsmen, Two-Handed Swordsmen and Champions; Spearmen, Archers, Scouts, Knights and siege upgrade too.
  - The Horde's smiths learn attack and armour upgrades in rounds 8, 14, 20 and 26, and kills are worth more Points in later rounds.
  - Enemies no longer appear inside forests.
  - The enemy computer can no longer resign.
- **Mods screen**: deleting a mod's folder while the game runs no longer crashes the game.
- **For modders**: `Game.FreeSpot(x, y)` finds the nearest open spot on the map.

## 0.7.2

- **Multiplayer fixes for Last Stand**:
  - The match no longer ends in a sudden victory: every player's game keeps track of the enemy computer.
  - Players with the same mods are no longer reported as having "different mods" when a mod file is laid out differently, for example a Mod Hub copy and a hand-installed copy.

## 0.7.1

- **Buttons for allies**: a mod can make a building's buttons usable by the owner's allies (`shared = true`). Last Stand's Castle shop works this way.
- **Same mod settings for everyone**: in a multiplayer lobby, the host's mod settings are used by every player, so different settings no longer say "different mods".
- **Last Stand**:
  - The shop follows your Age: Men-at-Arms become Long Swordsmen and Two-Handed Swordsmen, towers become Guard Towers and Keeps, and perks give the upgrades of your Age.
  - Trade goods for Points on your Market's second page. Scarce resources are worth more, and every trade costs more than the last.
  - Buy map vision: 3 rounds for 1000 Points, or the whole game for 5000.
  - A counter at the top right shows the enemies left in the round.
  - Enemies come from the edge of the map. The map is bigger, with hills, cliffs and ponds.
  - Fog of war is on by default. Saved games continue from the round they were saved in.
- **Multiplayer**:
  - Game modes work in multiplayer lobbies: one team, the enemy computer, the mode's own map.
  - Commands sent at the start of a match are no longer lost.
- **Multiplayer rehearsal**: put an empty `mp_rehearsal.txt` in the `wololoader` folder to test a mode's multiplayer code alone.
- **Scriptorium icon library**: more than 120 ready-made icons for units, buildings and general use, with a "Pick from library" button.
- **Clash of Empires** works in multiplayer lobbies: the computer defender is added and teams are set.

## 0.7.0

- **Clash of Empires**: a new lobby game mode. Spend points to send troops against a computer-defended village.
- **Gatherable resources**: villagers can mine a mod's own resource from new deposits. Crystal Mines is an example.
- **Scriptorium**:
  - New Files and Game Modes screens.
  - Editors for resources and settings.
  - A bigger, simpler look.
  - A visual script editor with templates.
- **Base features**: population up to 1000 and selection of up to 250 units.

## 0.6.1

- **Game modes**:
  - A mode can bring its own map, locked in the lobby.
  - A mode can bring its own enemy computer player.
- **Last Stand 2.1**: its own arena map, the Horde as a real enemy, revealed spawn points.

## 0.6.0

- **Mods can add their own content**:
  - resources, with boxes in the top bar;
  - lobby game modes;
  - buttons on the game's own buildings.
- **Mod settings** on the Mods screen, conflict warnings, and update badges for Mod Hub mods.
- **Last Stand**, **Age of Miracles**, **Merchant Caravans** and **Last Town Standing** in the Mod Hub.
