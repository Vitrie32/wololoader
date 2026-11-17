#!/usr/bin/env python3
"""
build.py - Wololoader builder: merges mods into a generated dat.

    python3 -I build.py "<game dir>"

Reads   <game>/resources/_common/dat/empires2_x2_p1.dat           (never modified)
Scans   <game>/wololoader/mods/*/            (sorted by folder name; mod.json
                                              "enabled": false skips the mod)
Writes  <game>/wololoader/cache/empires2_x2_p1.dat  (raw deflate, like the original)
        <game>/wololoader/cache/civs.txt     (id<TAB>base<TAB>name<TAB>mod<TAB>json)

Only the cache folder is ever written.  See civ json format in the docstring of
apply_civ_json().
"""

import copy
import json
import os
import sys

# `python3 -I` drops the script directory from sys.path; add it back for datfile.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile as df  # noqa: E402

DAT_REL = os.path.join("resources", "_common", "dat", "empires2_x2_p1.dat")
FIRST_RESERVED_ID = 32          # RANDOM / SPECTATOR / MIRROR are hard-coded in the exe
NUM_RESERVED = 3
CIV_NAME_BYTES = 20             # Civilization.name is a fixed char[20] (19 chars + NUL)


class BuildError(Exception):
    pass


def make_name(text, where):
    """Encode a dat civ name into the fixed 20 byte field (ASCII, NUL padded)."""
    try:
        raw = text.encode("ascii")
    except UnicodeEncodeError:
        raise BuildError("%s: dat_name %r must be ASCII" % (where, text))
    if not raw:
        raise BuildError("%s: dat_name is empty" % where)
    if len(raw) > CIV_NAME_BYTES - 1:
        print("warning: %s: dat_name %r truncated to %d chars"
              % (where, text, CIV_NAME_BYTES - 1))
        raw = raw[:CIV_NAME_BYTES - 1]
    return raw.ljust(CIV_NAME_BYTES, b"\0")


def make_effect_name(text, where):
    """Encode an effect name into the fixed 31 byte field (ASCII, NUL padded)."""
    try:
        raw = text.encode("ascii")
    except UnicodeEncodeError:
        raise BuildError("%s: effect name %r must be ASCII" % (where, text))
    if len(raw) > 30:
        raw = raw[:30]
    return raw.ljust(31, b"\0")


def parse_effect_command(cmd_spec, where):
    if not isinstance(cmd_spec, dict):
        raise BuildError("%s: effect command must be a JSON object" % where)
    if "type" not in cmd_spec:
        raise BuildError("%s: effect command missing 'type'" % where)
    try:
        cmd_type = int(cmd_spec["type"])
        a = int(cmd_spec.get("a", cmd_spec.get("unit", -1)))
        b = int(cmd_spec.get("b", cmd_spec.get("class", cmd_spec.get("mode", -1))))
        c = int(cmd_spec.get("c", cmd_spec.get("attr", -1)))
        d = float(cmd_spec.get("d", cmd_spec.get("value", cmd_spec.get("amount", 0.0))))
    except (ValueError, TypeError) as e:
        raise BuildError("%s: invalid effect command field: %s" % (where, e))
    return df.EffectCommand(type=cmd_type, a=a, b=b, c=c, d=d)


def clone_civ(civs, base, where):
    if not isinstance(base, int) or isinstance(base, bool) or not 0 <= base < len(civs):
        raise BuildError("%s: base %r is not an existing civ id (0..%d)"
                         % (where, base, len(civs) - 1))
    return copy.deepcopy(civs[base])


def _set_field(obj, field, value, where):
    cur = getattr(obj, field, None) if field in obj.__dict__ else None
    if field not in obj.__dict__:
        raise BuildError("%s: unknown field %r for unit type %s"
                         % (where, field, type(obj).__name__))
    if isinstance(cur, (bytes, df.Struct)) or (
            isinstance(cur, list) and cur and isinstance(cur[0], df.Struct)):
        raise BuildError("%s: field %r is a structured/raw field and cannot be overridden"
                         % (where, field))
    if isinstance(cur, list):
        if not isinstance(value, list) or len(value) != len(cur):
            raise BuildError("%s: field %r needs a list of %d numbers" % (where, field, len(cur)))
        obj.__dict__[field] = list(value)
    else:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise BuildError("%s: field %r needs a number, got %r" % (where, field, value))
        obj.__dict__[field] = float(value) if isinstance(cur, float) else _as_int(value, where, field)


def _as_int(value, where, field):
    if isinstance(value, float) and value != int(value):
        raise BuildError("%s: field %r is an integer, got %r" % (where, field, value))
    return int(value)


# "graphics": {"standing": slp | {"slp", "frames", "angles"}, ...} on a unit: each animation gets a
# copy of the unit's current graphic record with the mod's SLP (frames/angles optional). Deltas
# (shadows, sub-sprites), sounds and speed stay those of the copied record; when "angles" changes,
# the per-angle attack sounds are dropped. The C port (datbuild.c wl_apply_graphics) does the same.
ANIMS = {"standing": ("standing_graphic", 0), "standing2": ("standing_graphic", 1),
         "dying": ("dying_graphic", 0), "decay": ("dying_graphic", 1),
         "walking": ("walking_graphic", 0), "running": ("walking_graphic", 1),
         "attack": ("attack_graphic", None)}


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def add_graphic(dat, src, slp, frames, angles, where):
    """Appends a copy of graphic `src` drawing SLP `slp`; returns the new graphic id."""
    gfx = dat.graphics
    if not 0 <= src < len(gfx.ptrs) or gfx.graphics[src] is None:
        raise BuildError("%s: graphic %d does not exist" % (where, src))
    g = copy.deepcopy(gfx.graphics[src])
    gid = len(gfx.ptrs)
    nm = ("WLG%d" % gid).encode("ascii")
    g.name = nm + b"\0" * (21 - len(nm))
    g.filename = nm + b"\0" * (13 - len(nm))
    g.slp = slp
    g.id = gid
    if frames is not None:
        g.frame_count = frames
    if angles is not None and angles != g.angle_count:
        g.angle_count = angles
        g.attack_sound_used = 0
        g.attack_sounds = []
    gfx.ptrs.append(gfx.ptrs[src])
    gfx.graphics.append(g)
    return gid


def apply_graphics(dat, unit, spec, where):
    if not isinstance(spec, dict):
        raise BuildError("%s: graphics must be an object" % where)
    for anim, v in spec.items():
        w = "%s graphics.%s" % (where, anim)
        if anim not in ANIMS:
            raise BuildError("%s: unknown animation %r (use %s)" % (w, anim, ", ".join(ANIMS)))
        field, idx = ANIMS[anim]
        if field not in unit.__dict__:
            raise BuildError("%s: this unit type has no %s" % (w, field))
        if _is_int(v):
            v = {"slp": v}
        if not isinstance(v, dict) or not _is_int(v.get("slp")):
            raise BuildError("%s: needs an SLP id or {\"slp\": id, \"frames\": n, \"angles\": n}" % w)
        extra = set(v) - {"slp", "frames", "angles"}
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (w, sorted(extra)))
        slp, frames, angles = v["slp"], v.get("frames"), v.get("angles")
        if not 0 < slp < 2 ** 31:
            raise BuildError("%s: slp must be a positive integer" % w)
        if frames is not None and (not _is_int(frames) or not 1 <= frames <= 1000):
            raise BuildError("%s: frames must be 1..1000" % w)
        if angles is not None and (not _is_int(angles) or not 1 <= angles <= 64):
            raise BuildError("%s: angles must be 1..64" % w)
        cur = unit.__dict__[field]
        src = cur[idx] if idx is not None else cur
        if src < 0:
            raise BuildError("%s: the unit has no %s animation to copy" % (w, anim))
        gid = add_graphic(dat, src, slp, frames, angles, w)
        if idx is not None:
            cur[idx] = gid
        else:
            unit.__dict__[field] = gid


def make_unit_name(text, where):
    """Encode a unit internal name (ASCII, drop non-ASCII, max 30 chars + NUL)."""
    if not text:
        raise BuildError("%s: unit name is empty" % where)
    # Drop non-ASCII characters
    ascii_chars = [c for c in text if ord(c) < 128]
    if not ascii_chars:
        raise BuildError("%s: unit name %r has no ASCII characters" % (where, text))
    ascii_text = ''.join(ascii_chars)
    # Truncate to 30 chars
    if len(ascii_text) > 30:
        ascii_text = ascii_text[:30]
    # Encode and add NUL
    return ascii_text.encode("ascii") + b"\0"


def add_uu_tech_tree(tt, base_avail_id, new_avail_id, new_elite_id, U, E, where):
    """Add the tech tree records of a custom castle unique unit (re/research/techtree.md).

    The shared tech tree (dat section after the researches) draws the castle unit
    lines.  The base civ's UU pair is:
      base record  id=base_uu, units=[base_elite], enabling_research=base avail tech
      elite record id=base_elite, required_research=base elite tech
    The custom civ gets clones of both: U (units=[E], enabling_research=new avail)
    and E (required_research=new elite; its other_connected_ids point at U instead
    of the base UU).  Both are appended to tt.units.  U and E are inserted in the
    castle building (82) unit list and in every age list that holds the base pair.
    The C port (datbuild.c, wl_tt_add_unique_unit) must do exactly the same.
    """
    base_rec = None
    for rec in tt.units:
        if rec.enabling_research == base_avail_id:
            base_rec = rec
            break
    if base_rec is None or len(base_rec.units) < 1:
        raise BuildError("%s: no tech tree unit record for base avail tech %d" % (where, base_avail_id))
    base_uu = base_rec.id
    base_elite = base_rec.units[0]
    elite_rec = None
    for rec in tt.units:
        if rec.id == base_elite:
            elite_rec = rec
            break
    if elite_rec is None:
        raise BuildError("%s: no tech tree unit record for base elite unit %d" % (where, base_elite))

    new_base = copy.deepcopy(base_rec)
    new_base.id = U
    new_base.units = [E]
    new_base.unit_count = 1
    new_base.enabling_research = new_avail_id

    new_elite = copy.deepcopy(elite_rec)
    new_elite.id = E
    new_elite.required_research = new_elite_id
    new_elite.other_connected_ids = [U if x == base_uu else x for x in new_elite.other_connected_ids]

    bld = None
    for b in tt.buildings:
        if b.id == 82:
            bld = b
            break
    if bld is None or base_elite not in bld.units:
        raise BuildError("%s: castle building 82 does not list unit %d" % (where, base_elite))
    i = bld.units.index(base_elite)
    bld.units[i + 1:i + 1] = [U, E]          # U then E right after the base elite
    bld.unit_count = len(bld.units)
    if bld.unit_count > 255:
        raise BuildError("%s: castle building unit list exceeds 255 entries" % where)

    for age in tt.ages:
        for anchor, new in ((base_uu, U), (base_elite, E)):
            if anchor in age.units:
                age.units.insert(age.units.index(anchor) + 1, new)
                age.unit_count = len(age.units)
                if age.unit_count > 255:
                    raise BuildError("%s: age %d unit list exceeds 255 entries" % (where, age.id))

    tt.units.append(new_base)
    tt.units.append(new_elite)
    tt.unit_count = len(tt.units)
    if tt.unit_count > 255:
        raise BuildError("%s: tech tree unit count exceeds 255 (u8 in the dat)" % where)


def apply_civ_json(dat, spec, where, uu_k=0, ub_k=0, uu_total=0, ext=None):
    """Create one civ from a civ json dict and append it. Returns the new civ id.

    Keys: name (display, civs.txt only), dat_name, base, tech_tree (opt),
    team_bonus (opt), units {unit_id: {field: value}} (opt), resources {index: value} (opt),
    disabled_techs [id...] (opt), enabled_techs [id...] (opt),
    disabled_units [id...] (opt), enabled_units [id...] (opt),
    effects [{type, a, b, c, d}...] (opt), team_bonus_effects [{...}...] (opt),
    unique_techs [castle_tech, imp_tech] (opt).
    """
    civs = dat.civilizations.civs
    extend = "extend" in spec
    if extend:
        # "extend": change an existing civ of the game (1..31) in place instead of making a new
        # one; no identity keys (name, AI names, ...), no unique unit / unique techs.
        known = {"extend", "units", "resources", "disabled_techs", "enabled_techs",
                 "disabled_units", "enabled_units", "effects", "team_bonus_effects", "unique_building"}
        extra = set(spec) - known
        if extra:
            raise BuildError("%s: key(s) %s cannot be used with \"extend\" (only %s)"
                             % (where, sorted(extra), ", ".join(sorted(known - {"extend"}))))
        eid = spec["extend"]
        if isinstance(eid, bool) or not isinstance(eid, int) or not 1 <= eid <= 31:
            raise BuildError("%s: extend must be a civ id of the game, 1..31" % where)
        civ = civs[eid]
        label = "WL ext %d" % eid
    else:
        for key in ("name", "dat_name", "base"):
            if key not in spec:
                raise BuildError("%s: missing required key %r" % (where, key))
        # Identity keys are not used by the builder; the DLL reads them (civs.c).
        known = {"name", "dat_name", "base", "tech_tree", "team_bonus", "units", "resources",
                 "description", "ai_names", "sound", "interface_slp",
                 "disabled_techs", "enabled_techs", "disabled_units", "enabled_units",
                 "effects", "team_bonus_effects", "unique_techs", "unique_unit", "unique_building"}
        extra = set(spec) - known
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (where, sorted(extra)))
        civ = clone_civ(civs, spec["base"], where)
        civ.name = make_name(spec["dat_name"], where)
        label = spec["dat_name"]

    if "unique_techs" in spec:
        ut = spec["unique_techs"]
        if not isinstance(ut, list) or len(ut) != 2:
            raise BuildError("%s: unique_techs must be a list of 2 tech ids [castle_tech, imp_tech]" % where)

    has_tt_custom = any(k in spec for k in ("disabled_techs", "enabled_techs", "disabled_units", "enabled_units", "effects"))
    if has_tt_custom:
        base_tt = int(spec["tech_tree"]) if "tech_tree" in spec else civ.tech_tree_id
        if not 0 <= base_tt < len(dat.effects.effects):
            raise BuildError("%s: base tech tree effect %d out of range (0..%d)"
                             % (where, base_tt, len(dat.effects.effects) - 1))
        new_eff = copy.deepcopy(dat.effects.effects[base_tt])
        new_eff.name = make_effect_name(label + " Tech Tree", where)

        if "enabled_techs" in spec:
            en_techs = set(int(t) for t in spec["enabled_techs"])
            new_eff.commands = [cmd for cmd in new_eff.commands if not (cmd.type == 102 and int(cmd.d) in en_techs)]

        if "disabled_techs" in spec:
            existing = {int(cmd.d) for cmd in new_eff.commands if cmd.type == 102}
            for t in spec["disabled_techs"]:
                tid = int(t)
                if tid not in existing:
                    new_eff.commands.append(df.EffectCommand(type=102, a=-1, b=-1, c=-1, d=float(tid)))
                    existing.add(tid)

        if "enabled_units" in spec:
            en_units = set(int(u) for u in spec["enabled_units"])
            for u in spec["enabled_units"]:
                new_eff.commands.append(df.EffectCommand(type=2, a=int(u), b=1, c=-1, d=0.0))

        if "disabled_units" in spec:
            for u in spec["disabled_units"]:
                uid = int(u)
                # Effect type 2 = enable/disable unit (a=unit, b=0 off / 1 on).
                # (Type 103 is "tech research time", not a unit flag; do not emit it.)
                new_eff.commands.append(df.EffectCommand(type=2, a=uid, b=0, c=-1, d=0.0))

        if "effects" in spec:
            for idx, cmd_spec in enumerate(spec["effects"]):
                cmd = parse_effect_command(cmd_spec, "%s effects[%d]" % (where, idx))
                new_eff.commands.append(cmd)

        new_eff.effect_count = len(new_eff.commands)
        dat.effects.effects.append(new_eff)
        dat.effects.count = len(dat.effects.effects)
        civ.tech_tree_id = len(dat.effects.effects) - 1
    elif "tech_tree" in spec:
        civ.tech_tree_id = int(spec["tech_tree"])

    if "team_bonus_effects" in spec:
        if extend and 0 <= civ.team_bonus_id < len(dat.effects.effects):
            # an existing civ keeps its own team bonus and gets the new commands on top
            tb_eff = copy.deepcopy(dat.effects.effects[civ.team_bonus_id])
        else:
            tb_eff = df.Effect()
            tb_eff.commands = []
        tb_eff.name = make_effect_name(label + " Team Bonus", where)
        for idx, cmd_spec in enumerate(spec["team_bonus_effects"]):
            cmd = parse_effect_command(cmd_spec, "%s team_bonus_effects[%d]" % (where, idx))
            tb_eff.commands.append(cmd)
        tb_eff.effect_count = len(tb_eff.commands)
        dat.effects.effects.append(tb_eff)
        dat.effects.count = len(dat.effects.effects)
        civ.team_bonus_id = len(dat.effects.effects) - 1
    elif "team_bonus" in spec:
        civ.team_bonus_id = int(spec["team_bonus"])

    for uid, fields in spec.get("units", {}).items():
        try:
            i = int(uid)
        except ValueError:
            raise BuildError("%s: unit id %r is not a number" % (where, uid))
        if not 0 <= i < len(civ.units) or civ.units[i] is None:
            raise BuildError("%s: unit id %s does not exist in civ %s" % (where, uid, spec["extend"] if extend else spec["base"]))
        if not isinstance(fields, dict):
            raise BuildError("%s: units.%s must be an object" % (where, uid))
        for field, value in fields.items():
            if field == "graphics":
                apply_graphics(dat, civ.units[i], value, "%s unit %s" % (where, uid))
            else:
                _set_field(civ.units[i], field, value, "%s unit %s" % (where, uid))
    for idx, value in spec.get("resources", {}).items():
        i = int(idx)
        if not 0 <= i < len(civ.resources):
            raise BuildError("%s: resource index %s out of range (0..%d)"
                             % (where, idx, len(civ.resources) - 1))
        civ.resources[i] = float(value)

    if "unique_unit" in spec:
        uu = spec["unique_unit"]
        if not isinstance(uu, dict):
            raise BuildError("%s: unique_unit must be an object" % where)
        for req in ("copy_from", "name"):
            if req not in uu:
                raise BuildError("%s: unique_unit missing required key %r" % (where, req))
        known_uu = {"copy_from", "elite_copy_from", "name", "elite_name", "help",
                    "units", "elite_units", "elite_cost", "elite_time", "graphics", "elite_graphics"}
        extra_uu = set(uu) - known_uu
        if extra_uu:
            raise BuildError("%s: unknown unique_unit key(s) %s" % (where, sorted(extra_uu)))

        cid = len(civs)
        if cid > 114:  # SID = 28500 + (civ-35)*20 + slot: the game reads unit/tech name ids as SIGNED int16, so ids must be < 32768 (55000 showed the internal name); 28440..30100 is unused in the string files
            raise BuildError("%s: civ_id %d exceeds 114 for unique_unit" % (where, cid))

        U = 1224 + 2 * uu_k
        E = U + 1
        if E > 1400:
            raise BuildError("%s: unique unit slot %d exceeds 1400" % (where, E))

        civ1 = civs[1]
        for slot in (U, E):
            u_check = civ1.units[slot] if slot < len(civ1.units) else None
            if u_check is None or df.cstr(u_check.name) != "RESERVED":
                raise BuildError("%s: civ 1 unit at %d is not named 'RESERVED'" % (where, slot))

        copy_from = uu["copy_from"]
        if isinstance(copy_from, bool) or not isinstance(copy_from, int):
            raise BuildError("%s: unique_unit copy_from must be an integer" % where)
        if not 0 <= copy_from < len(civ.units) or civ.units[copy_from] is None:
            raise BuildError("%s: unique_unit copy_from unit %d does not exist" % (where, copy_from))
        if getattr(civ.units[copy_from], "type", None) != 70:
            raise BuildError("%s: unique_unit copy_from unit %d must be type 70 (got %r)"
                             % (where, copy_from, getattr(civ.units[copy_from], "type", None)))

        elite_copy_from = uu.get("elite_copy_from", copy_from)
        if isinstance(elite_copy_from, bool) or not isinstance(elite_copy_from, int):
            raise BuildError("%s: unique_unit elite_copy_from must be an integer" % where)
        if not 0 <= elite_copy_from < len(civ.units) or civ.units[elite_copy_from] is None:
            raise BuildError("%s: unique_unit elite_copy_from unit %d does not exist" % (where, elite_copy_from))
        if getattr(civ.units[elite_copy_from], "type", None) != 70:
            raise BuildError("%s: unique_unit elite_copy_from unit %d must be type 70 (got %r)"
                             % (where, elite_copy_from, getattr(civ.units[elite_copy_from], "type", None)))

        base_id = spec["base"]
        base_civ = civs[base_id]
        base_avail_id = None
        uu_base_id = None
        for rid, r in enumerate(dat.researches.researches):
            if r.civ == base_id and 0 <= r.effect_id < len(dat.effects.effects):
                eff = dat.effects.effects[r.effect_id]
                for cmd in eff.commands:
                    if cmd.type == 2 and cmd.b == 1:
                        u = base_civ.units[cmd.a] if 0 <= cmd.a < len(base_civ.units) else None
                        if u and getattr(u, "train_location_id", None) == 82:
                            base_avail_id = rid
                            uu_base_id = cmd.a
                            break
                if base_avail_id is not None:
                    break

        base_elite_id = None
        if uu_base_id is not None:
            for rid, r in enumerate(dat.researches.researches):
                if r.civ == base_id and r.research_location == 82 and 0 <= r.effect_id < len(dat.effects.effects):
                    eff = dat.effects.effects[r.effect_id]
                    for cmd in eff.commands:
                        if cmd.type == 3 and cmd.a == uu_base_id:
                            base_elite_id = rid
                            break
                    if base_elite_id is not None:
                        break

        if base_avail_id is None or base_elite_id is None:
            raise BuildError("%s: base civ %d has no castle unique unit techs" % (where, base_id))

        def SID(slot):
            return 28500 + (cid - 35) * 20 + slot

        avail_eff = df.Effect()
        avail_eff.name = make_effect_name("WL UU avail %d" % cid, where)
        avail_eff.commands = [df.EffectCommand(type=2, a=U, b=1, c=-1, d=0.0)]
        avail_eff.effect_count = 1
        dat.effects.effects.append(avail_eff)
        dat.effects.count = len(dat.effects.effects)
        avail_eff_id = len(dat.effects.effects) - 1

        elite_eff = df.Effect()
        elite_eff.name = make_effect_name("WL UU elite %d" % cid, where)
        elite_eff.commands = [df.EffectCommand(type=3, a=U, b=E, c=-1, d=0.0)]
        elite_eff.effect_count = 1
        dat.effects.effects.append(elite_eff)
        dat.effects.count = len(dat.effects.effects)
        elite_eff_id = len(dat.effects.effects) - 1

        new_avail_id = len(dat.researches.researches)
        avail_clone = copy.deepcopy(dat.researches.researches[base_avail_id])
        avail_clone.civ = cid
        avail_clone.effect_id = avail_eff_id
        avail_clone.language_dll_name = SID(6)
        avail_clone.language_dll_description = SID(6)
        avail_clone.name = ("WL UU %d\0" % cid).encode("ascii")
        avail_clone.name_length = len(avail_clone.name)
        dat.researches.researches.append(avail_clone)

        new_elite_id = len(dat.researches.researches)
        elite_clone = copy.deepcopy(dat.researches.researches[base_elite_id])
        elite_clone.civ = cid
        elite_clone.effect_id = elite_eff_id
        elite_clone.language_dll_name = SID(7)
        elite_clone.language_dll_description = SID(8)
        elite_clone.language_dll_help = 79000 + SID(9)  # game shows string (help - 79000), like 107381 -> 28381

        replaced = False
        for i in range(len(elite_clone.required_techs)):
            if elite_clone.required_techs[i] == base_avail_id:
                elite_clone.required_techs[i] = new_avail_id
                replaced = True
                break
        if not replaced:
            for i in range(len(elite_clone.required_techs)):
                if elite_clone.required_techs[i] != 103 and elite_clone.required_techs[i] >= 0:
                    elite_clone.required_techs[i] = new_avail_id
                    replaced = True
                    break

        elite_cost_spec = uu.get("elite_cost", {"food": 1000, "gold": 800})
        if not isinstance(elite_cost_spec, dict):
            raise BuildError("%s: unique_unit elite_cost must be an object" % where)
        res_map = {"food": 0, "wood": 1, "stone": 2, "gold": 3}
        extra_res = set(elite_cost_spec) - set(res_map)
        if extra_res:
            raise BuildError("%s: unknown elite_cost resource(s) %s" % (where, sorted(extra_res)))
        cost_items = []
        for rname in ("food", "wood", "stone", "gold"):
            if rname in elite_cost_spec:
                val = elite_cost_spec[rname]
                if isinstance(val, bool) or not isinstance(val, (int, float)) or val < 0:
                    raise BuildError("%s: elite_cost %s must be a positive number" % (where, rname))
                if val > 0:
                    cost_items.append((res_map[rname], int(val)))
        if len(cost_items) > 3:
            raise BuildError("%s: unique_unit elite_cost has more than 3 non-zero costs" % where)
        for slot in range(3):
            if slot < len(cost_items):
                elite_clone.resource_costs[slot] = df.ResearchResourceCost(
                    type=cost_items[slot][0], amount=cost_items[slot][1], flag=1)
            else:
                elite_clone.resource_costs[slot] = df.ResearchResourceCost(type=-1, amount=0, flag=0)

        elite_time = uu.get("elite_time", 60)
        if isinstance(elite_time, bool) or not isinstance(elite_time, int) or elite_time < 0:
            raise BuildError("%s: unique_unit elite_time must be a positive integer" % where)
        elite_clone.research_time = elite_time

        dat.researches.researches.append(elite_clone)
        dat.researches.count = len(dat.researches.researches)

        # Tech tree records for the castle unit (lines drawn on the tech tree screen)
        add_uu_tech_tree(dat.tech_tree, base_avail_id, new_avail_id, new_elite_id, U, E, where)

        # Unit U
        u_unit = copy.deepcopy(civ.units[copy_from])
        u_unit.id = U
        u_unit.id2 = U
        u_unit.id3 = U
        u_unit.train_location_id = 82
        u_unit.button_id = 1
        u_unit.enabled = 0
        u_unit.language_dll_name = SID(0)
        u_unit.language_dll_creation = SID(1)
        u_unit.language_dll_help = 79000 + SID(2)  # game shows string (help - 79000), like 105107 -> 26107
        u_unit.language_dll_hotkey_text = 150000 + SID(0)  # real units: hotkey = 150000 + name, needed for rich tooltip
        u_unit_name = make_unit_name(uu["name"], "%s unique_unit" % where)
        u_unit.name = u_unit_name
        u_unit.name_length = len(u_unit_name)
        if "units" in uu:
            if not isinstance(uu["units"], dict):
                raise BuildError("%s: unique_unit units must be an object" % where)
            for field, value in uu["units"].items():
                _set_field(u_unit, field, value, "%s unit %d" % (where, U))
        if "graphics" in uu:
            apply_graphics(dat, u_unit, uu["graphics"], "%s unique_unit" % where)
        civ.units[U] = u_unit

        # Unit E
        e_unit = copy.deepcopy(civ.units[elite_copy_from])
        e_unit.id = E
        e_unit.id2 = E
        e_unit.id3 = E
        e_unit.train_location_id = 82
        e_unit.button_id = 1
        e_unit.enabled = 0
        e_unit.language_dll_name = SID(3)
        e_unit.language_dll_creation = SID(4)
        e_unit.language_dll_help = 79000 + SID(5)
        e_unit.language_dll_hotkey_text = 150000 + SID(3)
        elite_name_text = uu.get("elite_name", "Elite " + uu["name"])
        e_unit_name = make_unit_name(elite_name_text, "%s unique_unit elite" % where)
        e_unit.name = e_unit_name
        e_unit.name_length = len(e_unit_name)
        if "units" in uu:
            for field, value in uu["units"].items():
                _set_field(e_unit, field, value, "%s unit %d" % (where, E))
        if "elite_units" in uu:
            if not isinstance(uu["elite_units"], dict):
                raise BuildError("%s: unique_unit elite_units must be an object" % where)
            for field, value in uu["elite_units"].items():
                _set_field(e_unit, field, value, "%s unit %d" % (where, E))
        if "elite_graphics" in uu or "graphics" in uu:
            apply_graphics(dat, e_unit, uu.get("elite_graphics", uu.get("graphics")), "%s unique_unit elite" % where)
        civ.units[E] = e_unit

    if "unique_building" in spec:
        add_unique_building(dat, civ, spec["extend"] if extend else len(civs), spec["unique_building"],
                            where, ub_k, uu_total,
                            ext if ext is not None else {"slots": 0, "sids": 0, "strings": [], "lua": []})

    if extend:
        return spec["extend"]
    civs.append(civ)
    return len(civs) - 1


AGE_TECHS = {0: 104, 1: 101, 2: 102, 3: 103}    # Dark, Feudal, Castle, Imperial
PAGES = {"mods": 2, "military": 10}  # unit command_attribute; "mods" keeps 2 (economy) and buildpage.c moves slots 1300..1400 to the mod page
UB_TEMPLATE_TECH = 570                            # "Feitoria (make avail)": civ building, no cost, auto


CMD_SID_BASE = 31210      # strings of building commands: triples (name, create/research, rich help)
                          # 31207..32767 and +20000/+21000/+79000/+150000 are unused by the game


def add_unique_building(dat, civ, cid, ub, where, ub_k, uu_total, ext):
    """A civ-only building cloned from an existing one, built by villagers.

    The C port (datbuild.c, wl_add_unique_building) must do exactly the same.
    Unit slots count down from 1400 (ub_k = slots used by earlier civs; the unique units count up
    from 1224): the building, then one slot per "train" command. ext["slots"] / ext["sids"] get the
    number of slots / string triples used; ext["strings"] (id, text) and ext["lua"] (lua buttons)
    are written to cache/strings.txt and cache/commands.txt for the DLL.
    A new graphic (clone of the copied building's standing graphic with the mod's SLP id) is
    appended when "slp" is given. A tech cloned from 570 (Feitoria make-available: civ-only,
    free, researched automatically) enables B when the age tech is researched.
    Strings: SID(10) name, SID(11) 'Build <name>', SID(12) help (served by the DLL, civs.c).
    """
    if not isinstance(ub, dict):
        raise BuildError("%s: unique_building must be an object" % where)
    for req in ("copy_from", "name"):
        if req not in ub:
            raise BuildError("%s: unique_building missing required key %r" % (where, req))
    known_ub = {"copy_from", "name", "help", "page", "button", "age", "slp", "units", "commands"}
    extra_ub = set(ub) - known_ub
    if extra_ub:
        raise BuildError("%s: unknown unique_building key(s) %s" % (where, sorted(extra_ub)))
    if cid > 114:
        raise BuildError("%s: civ_id %d exceeds 114 for unique_building" % (where, cid))
    game_civ = cid < 35   # a building for an existing civ ("extend"): its names go to strings.txt
    B = 1400 - ub_k
    if B < 1300 or B <= 1225 + 2 * uu_total:     # buildpage.c treats 1300..1400 as mod buildings
        raise BuildError("%s: unique building slot %d collides with unique unit slots" % (where, B))
    civ1 = dat.civilizations.civs[1]
    u_check = civ1.units[B] if B < len(civ1.units) else None
    if u_check is None or df.cstr(u_check.name) != "RESERVED":
        raise BuildError("%s: civ 1 unit at %d is not named 'RESERVED'" % (where, B))

    copy_from = ub["copy_from"]
    if isinstance(copy_from, bool) or not isinstance(copy_from, int):
        raise BuildError("%s: unique_building copy_from must be an integer" % where)
    if not 0 <= copy_from < len(civ.units) or civ.units[copy_from] is None:
        raise BuildError("%s: unique_building copy_from unit %d does not exist" % (where, copy_from))
    if getattr(civ.units[copy_from], "type", None) != 80:
        raise BuildError("%s: unique_building copy_from unit %d must be a building (type 80, got %r)"
                         % (where, copy_from, getattr(civ.units[copy_from], "type", None)))
    page = ub.get("page", "mods")
    if not isinstance(page, str) or page not in PAGES:
        raise BuildError("%s: unique_building page must be 'mods' or 'military'" % where)
    button = ub.get("button", 13)
    if isinstance(button, bool) or not isinstance(button, int) or not 1 <= button <= 15:
        raise BuildError("%s: unique_building button must be 1..15" % where)
    age = ub.get("age", 2)
    if isinstance(age, bool) or not isinstance(age, int) or age not in AGE_TECHS:
        raise BuildError("%s: unique_building age must be 0..3" % where)
    if game_civ:
        # same texts the DLL makes for custom civs (civs.c load_identity), via strings.txt
        b_name, b_help = ub["name"], ub.get("help", ub["name"])
        if not isinstance(b_name, str) or not isinstance(b_help, str):
            raise BuildError("%s: unique_building name and help must be strings" % where)
        ext_sid = _new_sids(ext)
        ext["strings"] += [(ext_sid, b_name), (ext_sid + 1, "Build %s" % b_name),
                           (ext_sid + 2, "Build <b>%s<b> (<cost>) \n%s \n<hp> <armor> <piercearmor>"
                            % (b_name, b_help or b_name))]

    def SID(slot):
        if game_civ:
            return ext_sid + slot - 10   # slots 10, 11, 12 -> the strings.txt triple
        return 28500 + (cid - 35) * 20 + slot

    b_unit = copy.deepcopy(civ.units[copy_from])
    b_unit.id = B
    b_unit.id2 = B
    b_unit.id3 = B
    b_unit.train_location_id = 118
    b_unit.button_id = button
    b_unit.command_attribute = PAGES[page]
    b_unit.enabled = 0
    b_unit.language_dll_name = SID(10)
    b_unit.language_dll_creation = SID(11)
    b_unit.language_dll_help = 79000 + SID(12)
    b_unit.language_dll_hotkey_text = 150000 + SID(10)
    b_name = make_unit_name(ub["name"], "%s unique_building" % where)
    b_unit.name = b_name
    b_unit.name_length = len(b_name)

    if "slp" in ub:
        slp = ub["slp"]
        if isinstance(slp, bool) or not isinstance(slp, int) or not 0 < slp < 2 ** 31:
            raise BuildError("%s: unique_building slp must be a positive integer" % where)
        gsrc = b_unit.standing_graphic[0]
        gfx = dat.graphics
        if not 0 <= gsrc < len(gfx.ptrs) or gfx.graphics[gsrc] is None:
            raise BuildError("%s: building %d has no standing graphic" % (where, copy_from))
        g = copy.deepcopy(gfx.graphics[gsrc])
        gid = len(gfx.ptrs)
        g.slp = slp
        g.id = gid
        nm = ("WLB%d" % cid).encode("ascii")
        g.name = nm + b"\0" * (21 - len(nm))
        g.filename = nm + b"\0" * (13 - len(nm))
        g.frame_count = 1
        g.angle_count = 1
        g.delta_count = 0
        g.deltas = []
        g.attack_sound_used = 0
        g.attack_sounds = []
        gfx.ptrs.append(gfx.ptrs[gsrc])
        gfx.graphics.append(g)
        b_unit.standing_graphic[0] = gid
        b_unit.snow_graphic_id = gid

    if "units" in ub:
        if not isinstance(ub["units"], dict):
            raise BuildError("%s: unique_building units must be an object" % where)
        for field, value in ub["units"].items():
            _set_field(b_unit, field, value, "%s unit %d" % (where, B))
    civ.units[B] = b_unit
    ext["slots"] = ub_k + 1

    eff = df.Effect()
    eff.name = make_effect_name("WL UB avail %d" % cid, where)
    eff.commands = [df.EffectCommand(type=2, a=B, b=1, c=-1, d=0.0)]
    eff.effect_count = 1
    dat.effects.effects.append(eff)
    dat.effects.count = len(dat.effects.effects)
    eff_id = len(dat.effects.effects) - 1          # before the commands append their own effects
    add_building_commands(dat, civ, cid, B, ub, age, eff, where, uu_total, ext)
    eff.effect_count = len(eff.commands)

    tech = copy.deepcopy(dat.researches.researches[UB_TEMPLATE_TECH])
    tech.civ = cid
    tech.effect_id = eff_id
    tech.required_techs[0] = AGE_TECHS[age]
    for i in range(1, len(tech.required_techs)):
        tech.required_techs[i] = -1
    tech.required_tech_count = 1
    tech.language_dll_name = SID(10)
    tech.language_dll_description = SID(10)
    tech.name = ("WL UB %d\0" % cid).encode("ascii")
    tech.name_length = len(tech.name)
    dat.researches.researches.append(tech)
    dat.researches.count = len(dat.researches.researches)


def _new_sids(ext):
    sid = CMD_SID_BASE + 3 * ext["sids"]
    ext["sids"] += 1
    if sid + 2 > 32767:
        raise BuildError("too many named building commands (string ids above 32767)")
    return sid


RES_IDS = {"food": 0, "wood": 1, "stone": 2, "gold": 3}


def add_building_commands(dat, civ, cid, B, ub, age, avail_eff, where, uu_total, ext):
    """unique_building "commands": [{"button": n, "train": ...} | {"button": n, "research": {...}} |
    {"button": n, "lua": "Name", ...}]. The C port (datbuild.c) must do exactly the same."""
    cmds = ub.get("commands", [])
    if not isinstance(cmds, list):
        raise BuildError("%s: unique_building commands must be a list" % where)
    for k, c in enumerate(cmds):
        w = "%s commands[%d]" % (where, k)
        if not isinstance(c, dict):
            raise BuildError("%s: must be an object" % w)
        kinds = [x for x in ("train", "research", "lua") if x in c]
        if len(kinds) != 1:
            raise BuildError("%s: needs exactly one of 'train', 'research', 'lua'" % w)
        kind = kinds[0]
        known = {"button", kind} | {"train": set(), "research": set(), "lua": {"text", "help", "icon"}}[kind]
        extra = set(c) - known
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (w, sorted(extra)))
        button = c.get("button")
        if isinstance(button, bool) or not isinstance(button, int) or not 1 <= button <= 15:
            raise BuildError("%s: button must be 1..15" % w)

        if kind == "train":
            t = c["train"]
            if isinstance(t, int) and not isinstance(t, bool):
                t = {"copy_from": t}
            if not isinstance(t, dict) or "copy_from" not in t:
                raise BuildError("%s: train must be a unit id or an object with copy_from" % w)
            extra = set(t) - {"copy_from", "name", "help", "units", "graphics"}
            if extra:
                raise BuildError("%s: unknown train key(s) %s" % (w, sorted(extra)))
            src = t["copy_from"]
            if isinstance(src, bool) or not isinstance(src, int) or not 0 <= src < len(civ.units) or civ.units[src] is None:
                raise BuildError("%s: train copy_from unit %r does not exist" % (w, src))
            if getattr(civ.units[src], "type", None) != 70:
                raise BuildError("%s: train copy_from unit %d must be a trainable unit (type 70)" % (w, src))
            slot = 1400 - ext["slots"]
            ext["slots"] += 1
            if slot < 1300 or slot <= 1225 + 2 * uu_total:
                raise BuildError("%s: no free unit slot left (1300..1400)" % w)
            civ1 = dat.civilizations.civs[1]
            if civ1.units[slot] is None or df.cstr(civ1.units[slot].name) != "RESERVED":
                raise BuildError("%s: civ 1 unit at %d is not named 'RESERVED'" % (w, slot))
            u = copy.deepcopy(civ.units[src])
            u.id = slot
            u.id2 = slot
            u.id3 = slot
            u.train_location_id = B
            u.button_id = button
            u.enabled = 0
            if "name" in t:
                nm, hp = t["name"], t.get("help", t["name"])
                if not isinstance(nm, str) or not isinstance(hp, str):
                    raise BuildError("%s: train name and help must be strings" % w)
                sid = _new_sids(ext)
                ext["strings"] += [(sid, nm), (sid + 1, "Create %s" % nm),
                                   (sid + 2, "Create <b>%s<b> (<cost>) \n%s \n<hp> <attack> <armor> <piercearmor> <range>" % (nm, hp))]
                u.language_dll_name = sid
                u.language_dll_creation = sid + 1
                u.language_dll_help = 79000 + sid + 2
                u.language_dll_hotkey_text = 150000 + sid
                un = make_unit_name(nm, w)
                u.name = un
                u.name_length = len(un)
            if "units" in t:
                if not isinstance(t["units"], dict):
                    raise BuildError("%s: train units must be an object" % w)
                for field, value in t["units"].items():
                    _set_field(u, field, value, "%s unit %d" % (w, slot))
            if "graphics" in t:
                apply_graphics(dat, u, t["graphics"], "%s unit %d" % (w, slot))
            civ.units[slot] = u
            avail_eff.commands.append(df.EffectCommand(type=2, a=slot, b=1, c=-1, d=0.0))

        elif kind == "research":
            r = c["research"]
            if not isinstance(r, dict) or not isinstance(r.get("name"), str) or not isinstance(r.get("help", ""), str):
                raise BuildError("%s: research must be an object with a name (string)" % w)
            extra = set(r) - {"copy_from", "name", "help", "cost", "time", "effects", "age", "icon"}
            if extra:
                raise BuildError("%s: unknown research key(s) %s" % (w, sorted(extra)))
            src = r.get("copy_from", 22)
            if isinstance(src, bool) or not isinstance(src, int) or not 0 <= src < ext["orig_res"]:
                raise BuildError("%s: research copy_from %r does not exist" % (w, src))
            r_age = r.get("age", age)
            if isinstance(r_age, bool) or not isinstance(r_age, int) or r_age not in AGE_TECHS:
                raise BuildError("%s: research age must be 0..3" % w)
            effs = r.get("effects", [])
            if not isinstance(effs, list):
                raise BuildError("%s: research effects must be a list" % w)
            e = df.Effect()
            e.name = make_effect_name("WL UB tech %d.%d" % (cid, k), w)
            e.commands = [parse_effect_command(x, w) for x in effs]
            e.effect_count = len(e.commands)
            dat.effects.effects.append(e)
            dat.effects.count = len(dat.effects.effects)
            tech = copy.deepcopy(dat.researches.researches[src])
            tech.civ = cid
            tech.effect_id = len(dat.effects.effects) - 1
            tech.research_location = B
            tech.button_id = button
            tech.required_techs[0] = AGE_TECHS[r_age]
            for i in range(1, len(tech.required_techs)):
                tech.required_techs[i] = -1
            tech.required_tech_count = 1
            cost = r.get("cost", {})
            if not isinstance(cost, dict) or set(cost) - set(RES_IDS):
                raise BuildError("%s: research cost must be an object with food/wood/stone/gold" % w)
            items = [(RES_IDS[n], cost[n]) for n in ("food", "wood", "stone", "gold") if n in cost]
            for n, v in items:
                if isinstance(v, bool) or not isinstance(v, int) or v < 0:
                    raise BuildError("%s: research cost values must be whole numbers >= 0" % w)
            items = [(n, v) for n, v in items if v > 0]
            if len(items) > 3:
                raise BuildError("%s: research cost has more than 3 resources" % w)
            for i in range(3):
                tech.resource_costs[i] = (df.ResearchResourceCost(type=items[i][0], amount=items[i][1], flag=1)
                                          if i < len(items) else df.ResearchResourceCost(type=-1, amount=0, flag=0))
            rt = r.get("time", 30)
            if isinstance(rt, bool) or not isinstance(rt, int) or rt < 0:
                raise BuildError("%s: research time must be a whole number >= 0" % w)
            tech.research_time = rt
            if "icon" in r:
                if isinstance(r["icon"], bool) or not isinstance(r["icon"], int):
                    raise BuildError("%s: research icon must be a number" % w)
                tech.icon_id = r["icon"]
            sid = _new_sids(ext)
            nm = r["name"]
            hp = r.get("help", nm)
            ext["strings"] += [(sid, nm), (sid + 1, "Research %s" % nm),
                               (sid + 2, "Research <b>%s<b> (<cost>) \n%s" % (nm, hp))]
            tech.language_dll_name = sid
            tech.language_dll_description = sid + 1
            tech.language_dll_help = 79000 + sid + 2
            tech.name = ("WL UBT %d.%d\0" % (cid, k)).encode("ascii")
            tech.name_length = len(tech.name)
            dat.researches.researches.append(tech)
            dat.researches.count = len(dat.researches.researches)

        else:  # lua button: nothing in the dat; the DLL adds it to the building's panel
            name = c["lua"]
            if not isinstance(name, str) or not name or "\t" in name or "\n" in name:
                raise BuildError("%s: lua must be a handler name" % w)
            icon = c.get("icon", 32)
            if isinstance(icon, bool) or not isinstance(icon, int):
                raise BuildError("%s: lua icon must be a number (command icon frame)" % w)
            text = c.get("text", name)
            hp = c.get("help", "")
            if not isinstance(text, str) or not isinstance(hp, str):
                raise BuildError("%s: lua text and help must be strings" % w)
            sid = _new_sids(ext)
            ext["strings"] += [(sid, text), (sid + 1, text), (sid + 2, "<b>%s<b> \n%s" % (text, hp))]
            ext["lua"].append("%d\t%d\t%d\t%s\t%d\t%d" % (cid, B, button, name, icon, sid + 2))


def copy_civ_sounds(dat, base, cid):
    """Unit sounds pick items by player civ (SoundItem.civilization); a new civ
    with no items is silent. Give it copies of the base civ's items."""
    added = 0
    for snd in dat.sounds.items:
        extra = []
        for it in snd.items:
            if it.civilization == base:
                dup = copy.deepcopy(it)
                dup.civilization = cid
                extra.append(dup)
        if extra:
            snd.items.extend(extra)
            snd.file_count = len(snd.items)
            added += len(extra)
    return added


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise BuildError("%s: %s" % (path, e))


def find_civ_files(mods_dir):
    """[(mod_folder, json_filename, path)] for enabled mods, sorted."""
    out = []
    if not os.path.isdir(mods_dir):
        return out
    for mod in sorted(os.listdir(mods_dir)):
        mdir = os.path.join(mods_dir, mod)
        if not os.path.isdir(mdir):
            continue
        meta = os.path.join(mdir, "mod.json")
        if os.path.isfile(meta) and load_json(meta).get("enabled", True) is False:
            print("skipping disabled mod %s" % mod)
            continue
        cdir = os.path.join(mdir, "civs")
        if os.path.isdir(cdir):
            for fn in sorted(os.listdir(cdir)):
                if fn.lower().endswith(".json"):
                    out.append((mod, fn, os.path.join(cdir, fn)))
    return out


def build(game_dir):
    dat_path = os.path.join(game_dir, DAT_REL)
    cache = os.path.join(game_dir, "wololoader", "cache")
    with open(dat_path, "rb") as f:          # read-only
        original = df.decompress(f.read())
    dat = df.DatFile.parse(original)
    civs = dat.civilizations.civs
    n_orig = len(civs)
    if n_orig != FIRST_RESERVED_ID:
        raise BuildError("expected %d civs in the base dat, found %d" % (FIRST_RESERVED_ID, n_orig))

    files = find_civ_files(os.path.join(game_dir, "wololoader", "mods"))
    lines = []
    ext = {"slots": 0, "sids": 0, "strings": [], "lua": [], "orig_res": len(dat.researches.researches)}
    if files:
        for k in range(NUM_RESERVED):
            ph = copy.deepcopy(civs[1])
            ph.name = make_name("WL_RESERVED_%d" % (FIRST_RESERVED_ID + k), "placeholder")
            civs.append(ph)
        uu_k = 0
        ub_k = 0
        uu_total = 0
        for mod, fn, path in files:      # unique unit slots count up, building slots count down
            spec = load_json(path)
            if isinstance(spec, dict) and "unique_unit" in spec:
                uu_total += 1
        for mod, fn, path in files:
            spec = load_json(path)
            where = "%s/civs/%s" % (mod, fn)
            if not isinstance(spec, dict):
                raise BuildError("%s: top level must be an object" % where)
            cid = apply_civ_json(dat, spec, where, uu_k, ub_k, uu_total, ext)
            if "unique_unit" in spec:
                uu_k += 1
            if "unique_building" in spec:
                ub_k = ext["slots"]
            if "extend" in spec:
                print("civ %d: extended by %s" % (cid, where))
                continue
            if "unique_techs" in spec:
                lines.append("%d\t%d\t%s\t%s\t%s\t%d\t%d" % (cid, spec["base"], spec["name"], mod, fn, int(spec["unique_techs"][0]), int(spec["unique_techs"][1])))
            else:
                lines.append("%d\t%d\t%s\t%s\t%s" % (cid, spec["base"], spec["name"], mod, fn))
            n_snd = copy_civ_sounds(dat, spec["base"], cid)
            print("civ %d: copied %d sound items from civ %d" % (cid, n_snd, spec["base"]))
            print("civ %d: %s (dat name %s) cloned from %d  [%s]"
                  % (cid, spec["name"], df.cstr(civs[cid].name), spec["base"], where))

    out = dat.serialize()
    os.makedirs(cache, exist_ok=True)
    out_dat = os.path.join(cache, "empires2_x2_p1.dat")
    for path, data, mode in ((out_dat, df.compress(out), "wb"),
                             (os.path.join(cache, "civs.txt"),
                              "".join(l + "\n" for l in lines).encode("utf-8"), "wb"),
                             # building command strings (id<TAB>text, "\n" kept as backslash-n)
                             # and Lua buttons (civ<TAB>building<TAB>button<TAB>name<TAB>icon<TAB>help id)
                             (os.path.join(cache, "strings.txt"),
                              "".join("%d\t%s\n" % (i, t.replace("\t", " ").replace("\n", "\\n"))
                                      for i, t in ext["strings"]).encode("utf-8"), "wb"),
                             (os.path.join(cache, "commands.txt"),
                              "".join(l + "\n" for l in ext["lua"]).encode("utf-8"), "wb")):
        tmp = path + ".tmp"
        with open(tmp, mode) as f:
            f.write(data)
        os.replace(tmp, path)
    print("wrote %s (%d civs)" % (out_dat, len(civs)))
    return original, out_dat, len(civs)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write('usage: python3 -I build.py "<game dir>"\n')
        return 2
    try:
        build(argv[1])
    except BuildError as e:
        sys.stderr.write("build error: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
