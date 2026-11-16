#!/usr/bin/env bash
# Start AoE2 HD on the invisible display of `hidden-screen` (Xvfb :77), muted,
# with the same Proton and prefix as the user's Steam shortcut. For agents
# testing Wololoader without the user (see the headless-app-test skill).
# Xvfb has no DRI3, so Vulkan (DXVK) cannot present: WineD3D (software GL) is used.
# The game folder, prefix and wololoader.log are shared with the user's own game:
# never run this while the user plays.
set -u
GAME="${WL_GAME:-$HOME/.steam/steam/steamapps/common/Age2HD}"
if ps -eo comm | grep -qi "^AoK HD"; then echo "the game is already running"; exit 1; fi
hidden-screen start || exit 1
# Mute the game's audio stream as soon as it appears (it shows up late).
( for _ in $(seq 90); do hidden-screen mute >/dev/null 2>&1; sleep 1; done ) &
cd "$GAME" && exec env -u WAYLAND_DISPLAY DISPLAY=:77 \
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
  STEAM_COMPAT_DATA_PATH="$HOME/.steam/steam/steamapps/compatdata/4282181384" \
  SteamAppId=0 SteamGameId=4282181384 \
  WINEDLLOVERRIDES="version=n,b" PROTON_USE_WINED3D=1 \
  "$HOME/.steam/steam/compatibilitytools.d/UMU-Proton-10.0-4/proton" run "$GAME/AoK HD.exe"
