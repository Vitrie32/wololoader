// Mod settings: options a mod declares in mod.json "settings", changed by the
// player on the Mods screen, read by the mod's Lua as MOD.settings.<id>.
//
// mod.json:
//   "settings": [
//     { "id": "extra_pop", "name": "Extra population", "type": "number",
//       "min": 0, "max": 100, "step": 5, "default": 0 },
//     { "id": "fast_vills", "name": "Fast villagers", "type": "bool", "default": true },
//     { "id": "mode", "name": "Mode", "type": "choice",
//       "choices": ["Easy", "Normal", "Hard"], "default": "Normal" }
//   ]
// The player's values live in <wololoader>\settings\<mod id>.json ({"id": value}),
// outside the mod folder so Mod Hub updates keep them.
#ifndef WL_MODSETTINGS_H
#define WL_MODSETTINGS_H

#include "wololoader.h"

#define WL_MAX_SETTINGS 32
#define WL_MAX_CHOICES  12

enum { WL_SET_BOOL, WL_SET_NUMBER, WL_SET_CHOICE };

typedef struct {
    char id[48];
    char name[64];
    int type;
    double min, max, step;
    double num, def_num;            // WL_SET_NUMBER
    int on, def_on;                 // WL_SET_BOOL
    int choice, def_choice;         // WL_SET_CHOICE: index into choices
    int nchoices;
    char choices[WL_MAX_CHOICES][40];
} WlSetting;

// Declared settings with the player's values applied. Returns the count (0..max).
int wl_settings_load(const WlMod *m, WlSetting *out, int max);
// Writes the values of all settings to the player's file. 1 = ok.
int wl_settings_save(const WlMod *m, const WlSetting *s, int n);
// <wololoader>\settings\<mod id>.json
void wl_settings_path(const WlMod *m, char *out, size_t cap);
// Value as text for the UI ("On", "15", "Hard").
void wl_setting_text(const WlSetting *s, char *out, size_t cap);
// Moves a value one step: dir +1 / -1 (bool toggles, choice cycles, number clamps).
void wl_setting_step(WlSetting *s, int dir);

#endif
