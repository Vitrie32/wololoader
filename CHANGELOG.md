# Wololoader changelog

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
