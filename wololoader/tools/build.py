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
import math
import os
import struct
import sys

# `python3 -I` drops the script directory from sys.path; add it back for datfile.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile as df  # noqa: E402

DAT_REL = os.path.join("resources", "_common", "dat", "empires2_x2_p1.dat")
FIRST_RESERVED_ID = 32          # RANDOM / SPECTATOR / MIRROR are hard-coded in the exe
NUM_RESERVED = 3
CIV_NAME_BYTES = 20             # Civilization.name is a fixed char[20] (19 chars + NUL)
CUSTOM_RES_FIRST = 211          # AoE2 HD 5.8 vanilla resource_count = 211 (indices 0..210)
CUSTOM_RES_MAX = 32766          # engine reads resource_count as signed int16 (max 32767)


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


def _is_plain_int_str(s):
    if not isinstance(s, str) or not s:
        return False
    t = s[1:] if s[0] in "+-" else s
    return bool(t) and t.isdigit() and t.isascii()


def fmt_res_float(v):
    f = float(v)
    if f == 0.0:
        return "0"
    return "%.6g" % f


def find_custom_resources(mods_dir):
    """Discover custom resources from enabled mods' mod.json in deterministic order.

    Order: enabled mods sorted by folder name, then declaration order within each mod.json.
    IDs are assigned contiguously from CUSTOM_RES_FIRST (211) up to CUSTOM_RES_MAX (32766).
    Mirrors wl_find_custom_resources in datbuild.c byte-for-byte.
    """
    out = []
    if not os.path.isdir(mods_dir):
        return out
    vanilla_names = {"food", "wood", "stone", "gold"}
    known_keys = {"id", "name", "icon", "start", "max", "show", "hidden", "game_mode", "description", "gather"}
    known_gather_keys = {
        "deposit_name", "amount", "copy_from", "miner_name",
        "work_rate", "carry", "drop_sites", "minimap_color",
        "deposit_slp", "carry_male_slp", "carry_female_slp",
    }
    for mod in sorted(os.listdir(mods_dir)):
        mdir = os.path.join(mods_dir, mod)
        if not os.path.isdir(mdir):
            continue
        meta_path = os.path.join(mdir, "mod.json")
        if not os.path.isfile(meta_path):
            continue
        meta = load_json(meta_path)
        if not isinstance(meta, dict):
            continue
        if meta.get("enabled", True) is False:
            continue
        res_list = meta.get("resources")
        if res_list is None:
            continue
        if not isinstance(res_list, list):
            raise BuildError("%s/mod.json: resources must be a list" % mod)
        seen = set()
        for k, r in enumerate(res_list):
            w = "%s/mod.json resources[%d]" % (mod, k)
            if not isinstance(r, dict):
                raise BuildError("%s: must be an object" % w)
            extra = {key for key in r if not key.startswith("_")} - known_keys
            if extra:
                raise BuildError("%s: unknown key(s) %s" % (w, sorted(extra)))
            rkey = r.get("id")
            if (not isinstance(rkey, str) or not rkey or len(rkey) > 64 or
                    any(c in rkey for c in "\t\n\r:")):
                raise BuildError("%s: id must be a non-empty string (max 64 chars, no ':'/tab/newline)" % w)
            if rkey.lower() in vanilla_names or _is_plain_int_str(rkey):
                raise BuildError("%s: id %r conflicts with vanilla/numeric resource ids" % (w, rkey))
            if rkey in seen:
                raise BuildError("%s: duplicate resource id %r in mod %s" % (w, rkey, mod))
            seen.add(rkey)
            rname = r.get("name", rkey)
            if rname is None or rname == "":
                rname = rkey
            if not isinstance(rname, str) or len(rname) > 128 or any(c in rname for c in "\t\n\r"):
                raise BuildError("%s: name must be a string (max 128 chars, no tab/newline)" % w)
            ricon = r.get("icon", "")
            if ricon is None:
                ricon = ""
            if (not isinstance(ricon, str) or len(ricon) > 256 or ".." in ricon or
                    ricon.startswith("/") or ricon.startswith("\\") or
                    (len(ricon) >= 2 and ricon[1] == ":") or
                    any(c in ricon for c in "\t\n\r")):
                raise BuildError("%s: icon must be a relative path inside the mod folder" % w)
            rstart = r.get("start", 0)
            if rstart is None:
                rstart = 0
            if isinstance(rstart, bool) or not isinstance(rstart, (int, float)):
                raise BuildError("%s: start must be a number" % w)
            rmax = r.get("max", 0)
            if rmax is None:
                rmax = 0
            if isinstance(rmax, bool) or not isinstance(rmax, (int, float)) or rmax < 0:
                raise BuildError("%s: max must be a number >= 0" % w)
            rshow = r.get("show", True)
            if rshow is None:
                rshow = True
            if not isinstance(rshow, bool):
                raise BuildError("%s: show must be a boolean" % w)
            rhidden = r.get("hidden", False)
            if rhidden is None:
                rhidden = False
            if not isinstance(rhidden, bool):
                raise BuildError("%s: hidden must be a boolean" % w)
            rgmode = r.get("game_mode", "")
            if rgmode is None:
                rgmode = ""
            if not isinstance(rgmode, str) or len(rgmode) > 64 or any(c in rgmode for c in "\t\n\r"):
                raise BuildError("%s: game_mode must be a string (max 64 chars, no tab/newline)" % w)
            rdesc = r.get("description", "")
            if rdesc is None:
                rdesc = ""
            if not isinstance(rdesc, str):
                raise BuildError("%s: description must be a string" % w)
            gather_spec = None
            raw_gather = r.get("gather")
            if raw_gather is not None:
                gw = "%s gather" % w
                if not isinstance(raw_gather, dict):
                    raise BuildError("%s: must be an object" % gw)
                g_extra = {key for key in raw_gather if not key.startswith("_")} - known_gather_keys
                if g_extra:
                    raise BuildError("%s: unknown key(s) %s" % (gw, sorted(g_extra)))

                dep_name = raw_gather.get("deposit_name")
                if dep_name is None:
                    dep_name = "%s Deposit" % rname
                else:
                    if (not isinstance(dep_name, str) or not dep_name or len(dep_name) > 128 or
                            any(c in dep_name for c in "\t\n\r")):
                        raise BuildError("%s: deposit_name must be a non-empty string (max 128 chars, no tab/newline)" % gw)

                miner_name = raw_gather.get("miner_name")
                if miner_name is None:
                    miner_name = "%s Miner" % rname
                else:
                    if (not isinstance(miner_name, str) or not miner_name or len(miner_name) > 128 or
                            any(c in miner_name for c in "\t\n\r")):
                        raise BuildError("%s: miner_name must be a non-empty string (max 128 chars, no tab/newline)" % gw)

                g_amount = raw_gather.get("amount")
                if g_amount is not None:
                    if (isinstance(g_amount, bool) or not isinstance(g_amount, (int, float)) or
                            not math.isfinite(g_amount) or g_amount < 1 or g_amount > 65535):
                        raise BuildError("%s: amount must be a number 1..65535" % gw)
                    g_amount = float(g_amount)

                g_copy_from = raw_gather.get("copy_from", 66)
                if g_copy_from is None:
                    g_copy_from = 66
                if isinstance(g_copy_from, bool) or not isinstance(g_copy_from, int) or g_copy_from < 0:
                    raise BuildError("%s: copy_from must be a non-negative integer" % gw)

                g_work_rate = raw_gather.get("work_rate")
                if g_work_rate is not None:
                    if (isinstance(g_work_rate, bool) or not isinstance(g_work_rate, (int, float)) or
                            not math.isfinite(g_work_rate) or g_work_rate <= 0 or g_work_rate > 1000):
                        raise BuildError("%s: work_rate must be a number > 0 and <= 1000" % gw)
                    g_work_rate = float(g_work_rate)

                g_carry = raw_gather.get("carry")
                if g_carry is not None:
                    if isinstance(g_carry, bool) or not isinstance(g_carry, int) or not 1 <= g_carry <= 32767:
                        raise BuildError("%s: carry must be an integer 1..32767" % gw)

                g_drop_sites = raw_gather.get("drop_sites")
                if g_drop_sites is not None:
                    if not isinstance(g_drop_sites, list) or not 1 <= len(g_drop_sites) <= 2:
                        raise BuildError("%s: drop_sites must be a list of 1 or 2 unit ids" % gw)
                    ds_parsed = []
                    for di, ds in enumerate(g_drop_sites):
                        if isinstance(ds, bool) or not isinstance(ds, int) or ds < 0:
                            raise BuildError("%s: drop_sites[%d] must be a non-negative integer" % (gw, di))
                        ds_parsed.append(ds)
                    if len(ds_parsed) == 1:
                        ds_parsed.append(-1)
                    g_drop_sites = ds_parsed

                g_minimap_color = raw_gather.get("minimap_color")
                if g_minimap_color is not None:
                    if (isinstance(g_minimap_color, bool) or not isinstance(g_minimap_color, int) or
                            not 0 <= g_minimap_color <= 255):
                        raise BuildError("%s: minimap_color must be an integer 0..255" % gw)

                g_slps = {}
                for sk in ("deposit_slp", "carry_male_slp", "carry_female_slp"):
                    sv = raw_gather.get(sk)
                    if sv is not None:
                        if isinstance(sv, bool) or not isinstance(sv, int) or not 0 < sv < 2 ** 31:
                            raise BuildError("%s: %s must be a positive integer" % (gw, sk))
                    g_slps[sk] = sv

                gather_spec = {
                    "deposit_name": dep_name,
                    "miner_name": miner_name,
                    "amount": g_amount,
                    "copy_from": g_copy_from,
                    "work_rate": g_work_rate,
                    "carry": g_carry,
                    "drop_sites": g_drop_sites,
                    "minimap_color": g_minimap_color,
                    "deposit_slp": g_slps["deposit_slp"],
                    "carry_male_slp": g_slps["carry_male_slp"],
                    "carry_female_slp": g_slps["carry_female_slp"],
                    "where": gw,
                }
            rid = CUSTOM_RES_FIRST + len(out)
            if rid > CUSTOM_RES_MAX:
                raise BuildError("%s: too many custom resources (max id %d)" % (w, CUSTOM_RES_MAX))
            out.append({
                "id": rid, "mod": mod, "key": rkey, "name": rname, "icon": ricon,
                "start": float(rstart), "max": float(rmax), "show": 1 if rshow else 0,
                "hidden": 1 if rhidden else 0, "game_mode": rgmode,
                "description": rdesc, "gather": gather_spec,
            })
    return out


def resolve_resource_id(ref, cur_mod, max_res, custom_res, where):
    """Resolve a resource id (int, numeric string, vanilla name, 'key', or 'mod:key')."""
    if isinstance(ref, bool):
        raise BuildError("%s: invalid resource %r" % (where, ref))
    if isinstance(ref, int):
        if not 0 <= ref < max_res:
            raise BuildError("%s: resource index %d out of range (0..%d)" % (where, ref, max_res - 1))
        return ref
    if not isinstance(ref, str) or not ref:
        raise BuildError("%s: invalid resource %r" % (where, ref))
    if ref in RES_IDS:
        return RES_IDS[ref]
    if ":" in ref:
        m_part, k_part = ref.split(":", 1)
        for r in custom_res:
            if r["mod"] == m_part and r["key"] == k_part:
                return r["id"]
        raise BuildError("%s: unknown resource %r" % (where, ref))
    for r in custom_res:
        if r["mod"] == cur_mod and r["key"] == ref:
            return r["id"]
    for r in custom_res:
        if r["key"] == ref:
            return r["id"]
    if _is_plain_int_str(ref):
        v = int(ref)
        if not 0 <= v < max_res:
            raise BuildError("%s: resource index %s out of range (0..%d)" % (where, ref, max_res - 1))
        return v
    raise BuildError("%s: unknown resource %r" % (where, ref))


def parse_cost_items(cost_spec, cur_mod, max_res, custom_res, where, allow_enabled=True):
    """Parse cost as either dict ({'food': 50, 'faith': 25}) or array ([{'resource': 'faith', 'amount': 50}]).
    Returns a list of up to 3 tuples (res_id, amount, enabled).
    """
    items = []
    if isinstance(cost_spec, dict):
        # Vanilla names first in canonical order (food, wood, stone, gold) for full backwards compatibility,
        # then any custom/numeric keys sorted by resolved resource id ascending.
        extra_resolved = []
        for k, v in cost_spec.items():
            rid = resolve_resource_id(k, cur_mod, max_res, custom_res, where)
            if isinstance(v, bool) or not isinstance(v, int) or not 0 <= v <= 32767:
                raise BuildError("%s: cost %s must be a whole number 0..32767" % (where, k))
            if v > 0:
                if k in RES_IDS:
                    continue
                extra_resolved.append((rid, v, 1))
        for rname in ("food", "wood", "stone", "gold"):
            if rname in cost_spec and cost_spec[rname] > 0:
                items.append((RES_IDS[rname], int(cost_spec[rname]), 1))
        extra_resolved.sort(key=lambda x: x[0])
        items.extend(extra_resolved)
    elif isinstance(cost_spec, list):
        allowed = {"resource", "type", "amount", "enabled"} if allow_enabled else {"resource", "type", "amount"}
        for idx, entry in enumerate(cost_spec):
            ew = "%s cost[%d]" % (where, idx)
            if not isinstance(entry, dict):
                raise BuildError("%s: must be an object" % ew)
            extra = set(entry) - allowed
            if extra:
                raise BuildError("%s: unknown key(s) %s" % (ew, sorted(extra)))
            ref = entry.get("resource", entry.get("type"))
            if ref is None:
                raise BuildError("%s: missing 'resource'" % ew)
            rid = resolve_resource_id(ref, cur_mod, max_res, custom_res, ew)
            amt = entry.get("amount")
            if isinstance(amt, bool) or not isinstance(amt, int) or not 0 <= amt <= 32767:
                raise BuildError("%s: amount must be a whole number 0..32767" % ew)
            en_raw = entry.get("enabled", 1) if allow_enabled else 1
            if isinstance(en_raw, bool):
                en = 1 if en_raw else 0
            elif isinstance(en_raw, int) and en_raw in (0, 1):
                en = en_raw
            else:
                raise BuildError("%s: enabled must be 0, 1, or boolean" % ew)
            if amt > 0:
                items.append((rid, amt, en))
    else:
        raise BuildError("%s: cost must be an object or array" % where)
    if len(items) > 3:
        raise BuildError("%s: cost has more than 3 resources" % where)
    return items


def apply_unit_cost(u, cost_spec, cur_mod, max_res, custom_res, where):
    if getattr(u, "type", 0) not in (70, 80):
        raise BuildError("%s: only creatable units and buildings (type 70/80) have cost" % where)
    items = parse_cost_items(cost_spec, cur_mod, max_res, custom_res, where, allow_enabled=True)
    has_pop_item = any(t == 4 for t, _, _ in items)
    pop_cost = None if has_pop_item else next(
        (copy.deepcopy(c) for c in u.resource_costs if c.type == 4 and c.enabled == 0), None)
    if len(items) <= 2 and pop_cost is not None:
        for i in range(2):
            u.resource_costs[i] = (df.ResourceCost(type=items[i][0], amount=items[i][1], enabled=items[i][2])
                                   if i < len(items) else df.ResourceCost(type=-1, amount=0, enabled=0))
        u.resource_costs[2] = pop_cost
    else:
        for i in range(3):
            u.resource_costs[i] = (df.ResourceCost(type=items[i][0], amount=items[i][1], enabled=items[i][2])
                                   if i < len(items) else df.ResourceCost(type=-1, amount=0, enabled=0))


def apply_research_cost(tech, cost_spec, cur_mod, max_res, custom_res, where):
    items = parse_cost_items(cost_spec, cur_mod, max_res, custom_res, where, allow_enabled=False)
    for i in range(3):
        tech.resource_costs[i] = (df.ResearchResourceCost(type=items[i][0], amount=items[i][1], flag=1)
                                  if i < len(items) else df.ResearchResourceCost(type=-1, amount=0, flag=0))


def parse_effect_command(cmd_spec, where, cur_mod="", max_res=211, custom_res=None):
    if not isinstance(cmd_spec, dict):
        raise BuildError("%s: effect command must be a JSON object" % where)
    if "type" not in cmd_spec:
        raise BuildError("%s: effect command missing 'type'" % where)
    try:
        cmd_type = int(cmd_spec["type"])
        a_raw = cmd_spec.get("a", cmd_spec.get("resource", cmd_spec.get("unit", -1)))
        if isinstance(a_raw, str) and not _is_plain_int_str(a_raw):
            a = resolve_resource_id(a_raw, cur_mod, max_res, custom_res or [], where)
        else:
            a = int(a_raw)
        b = int(cmd_spec.get("b", cmd_spec.get("class", cmd_spec.get("mode", -1))))
        c = int(cmd_spec.get("c", cmd_spec.get("attr", -1)))
        d = float(cmd_spec.get("d", cmd_spec.get("value", cmd_spec.get("amount", 0.0))))
    except BuildError:
        raise
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


# ---- automatic renumbering of new mod sprites (mirrors wl_slp_remap_compute in datbuild.c) ----
# Mods add sprites as <mod>/resources/_common/drs/graphics/<id>.slp. An id that no game file uses
# is "new". When two enabled mods ship the same new id, the first mod (folder names in sorted
# order) keeps it and every later one gets a free id from SLP_REMAP_FIRST up; the "slp" values in
# that mod's civ files are changed to match, and files.c serves the file under the new id.
# Ids the game already has are overrides of game sprites: those are never renumbered.
SLP_REMAP_FIRST = 90000   # above every id in the game's drs folders (gamedata_x2 ends at 88713)
SLP_REMAP = {}            # {from: to} of the mod whose civ file is being applied


def _slp_ids(folder):
    """Ids of the files named <digits>.slp in folder (any case for .slp)."""
    out = set()
    if os.path.isdir(folder):
        for fn in os.listdir(folder):
            stem, ext = fn[:-4], fn[-4:]
            if ext.lower() == ".slp" and stem.isdigit() and stem.isascii() and len(stem) <= 9:
                out.add(int(stem))
    return out


def slp_remap(game_dir, mods_dir):
    """{mod folder: {from id: to id}} for the enabled mods that need renumbered sprites."""
    drs = os.path.join(game_dir, "resources", "_common", "drs")
    game = set()
    if os.path.isdir(drs):
        for sub in os.listdir(drs):
            game |= _slp_ids(os.path.join(drs, sub))
    mods = []
    if os.path.isdir(mods_dir):
        for mod in sorted(os.listdir(mods_dir)):
            mdir = os.path.join(mods_dir, mod)
            if not os.path.isdir(mdir):
                continue
            meta = os.path.join(mdir, "mod.json")
            if os.path.isfile(meta) and load_json(meta).get("enabled", True) is False:
                continue
            mods.append((mod, _slp_ids(os.path.join(mdir, "resources", "_common", "drs", "graphics"))))
    used = set(game)
    for _, ids in mods:
        used |= ids
    owner, out, nxt = {}, {}, SLP_REMAP_FIRST
    for mod, ids in mods:
        for n in sorted(ids):
            if n in game:
                continue
            if n not in owner:
                owner[n] = mod
                continue
            while nxt in used:
                nxt += 1
            out.setdefault(mod, {})[n] = nxt
            used.add(nxt)
            print("sprite %d of %s is also in %s: renumbered to %d" % (n, mod, owner[n], nxt))
    return out


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
        slp = SLP_REMAP.get(slp, slp)
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
    if ext is None:
        ext = {"slots": 0, "sids": 0, "strings": [], "lua": [], "cu_k": 0, "cu_total": 0}
    civs = dat.civilizations.civs
    extend = "extend" in spec
    if extend:
        # "extend": change an existing civ of the game (1..31) in place instead of making a new
        # one; no identity keys (name, AI names, ...), no unique unit / unique techs.
        known = {"extend", "units", "resources", "disabled_techs", "enabled_techs",
                 "disabled_units", "enabled_units", "effects", "team_bonus_effects",
                 "unique_building", "custom_units"}
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
                 "description", "ai_names", "sound", "victory_sound", "interface_slp", "history", "icon",
                 "disabled_techs", "enabled_techs", "disabled_units", "enabled_units",
                 "effects", "team_bonus_effects", "unique_techs", "unique_unit", "unique_building",
                 "custom_units"}
        extra = {k for k in spec if not k.startswith("_")} - known
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (where, sorted(extra)))
        civ = clone_civ(civs, spec["base"], where)
        civ.name = make_name(spec["dat_name"], where)
        label = spec["dat_name"]

    cur_mod = where.split("/", 1)[0]
    custom_res = ext.get("custom_res", [])
    max_res = len(civ.resources)

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
                cmd = parse_effect_command(cmd_spec, "%s effects[%d]" % (where, idx), cur_mod, max_res, custom_res)
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
            cmd = parse_effect_command(cmd_spec, "%s team_bonus_effects[%d]" % (where, idx), cur_mod, max_res, custom_res)
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
            elif field == "cost":
                apply_unit_cost(civ.units[i], value, cur_mod, max_res, custom_res, "%s unit %s" % (where, uid))
            else:
                _set_field(civ.units[i], field, value, "%s unit %s" % (where, uid))
    for idx, value in spec.get("resources", {}).items():
        i = resolve_resource_id(idx, cur_mod, len(civ.resources), custom_res, where)
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
        apply_research_cost(elite_clone, elite_cost_spec, cur_mod, max_res, custom_res, "%s unique_unit elite_cost" % where)

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
                if field == "cost":
                    apply_unit_cost(u_unit, value, cur_mod, max_res, custom_res, "%s unit %d" % (where, U))
                else:
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
                if field == "cost":
                    apply_unit_cost(e_unit, value, cur_mod, max_res, custom_res, "%s unit %d" % (where, E))
                else:
                    _set_field(e_unit, field, value, "%s unit %d" % (where, E))
        if "elite_units" in uu:
            if not isinstance(uu["elite_units"], dict):
                raise BuildError("%s: unique_unit elite_units must be an object" % where)
            for field, value in uu["elite_units"].items():
                if field == "cost":
                    apply_unit_cost(e_unit, value, cur_mod, max_res, custom_res, "%s unit %d" % (where, E))
                else:
                    _set_field(e_unit, field, value, "%s unit %d" % (where, E))
        if "elite_graphics" in uu or "graphics" in uu:
            apply_graphics(dat, e_unit, uu.get("elite_graphics", uu.get("graphics")), "%s unique_unit elite" % where)
        civ.units[E] = e_unit

    ub_slot = -1
    if "unique_building" in spec:
        ub_slot = 1400 - ub_k
        add_unique_building(dat, civ, spec["extend"] if extend else len(civs), spec["unique_building"],
                            where, ub_k, uu_total, ext)

    if "custom_units" in spec:
        add_custom_units(dat, civ, spec["extend"] if extend else len(civs), spec["custom_units"],
                         spec.get("unique_building"), ub_slot, where, uu_total, ext)

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
    known_ub = {"copy_from", "name", "help", "page", "button", "age", "slp", "icon", "units", "commands", "building_type"}
    extra_ub = {k for k in ub if not k.startswith("_")} - known_ub
    if extra_ub:
        raise BuildError("%s: unknown unique_building key(s) %s" % (where, sorted(extra_ub)))
    if cid > 114:
        raise BuildError("%s: civ_id %d exceeds 114 for unique_building" % (where, cid))
    game_civ = cid < 35   # a building for an existing civ ("extend"): its names go to strings.txt
    B = 1400 - ub_k
    if B < 1300 or B <= 1225 + 2 * uu_total + ext.get("cu_total", 0) + ext.get("gather_slots", 0):     # buildpage.c treats 1300..1400 as mod buildings
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

    if "icon" in ub:
        icon = ub["icon"]
        if isinstance(icon, bool) or not isinstance(icon, int) or not 0 <= icon <= 65535:
            raise BuildError("%s: unique_building icon must be an integer (0..65535)" % where)
        cur_mod = where.split("/", 1)[0]
        resolved = resolve_building_icon(ext, cur_mod, icon)
        if resolved is not None:
            b_unit.icon_id = resolved

    if "slp" in ub:
        slp = ub["slp"]
        if isinstance(slp, bool) or not isinstance(slp, int) or not 0 < slp < 2 ** 31:
            raise BuildError("%s: unique_building slp must be a positive integer" % where)
        slp = SLP_REMAP.get(slp, slp)
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
        cur_mod_ub = where.split("/", 1)[0]
        custom_res_ub = ext.get("custom_res", [])
        for field, value in ub["units"].items():
            if field == "cost":
                apply_unit_cost(b_unit, value, cur_mod_ub, len(civ.resources), custom_res_ub, "%s unit %d" % (where, B))
            else:
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
        known = {"button", "page", "condition", kind} | {"train": set(), "research": set(), "lua": {"text", "help", "icon"}}[kind]
        extra = {k for k in c if not k.startswith("_")} - known
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (w, sorted(extra)))
        button = c.get("button")
        if isinstance(button, bool) or not isinstance(button, int) or not 1 <= button <= 14:
            raise BuildError("%s: button must be 1..14 (button 15 is reserved for Next Page)" % w)
        page = c.get("page", 1)
        if isinstance(page, bool) or not isinstance(page, int) or page < 1:
            page = 1

        cond_act = "disable"
        cond_name = ""
        if "condition" in c and c["condition"] is not None:
            cond = c["condition"]
            if not isinstance(cond, dict):
                raise BuildError("%s: condition must be an object" % w)
            extra_c = set(cond) - {"action", "name"}
            if extra_c:
                raise BuildError("%s: unknown condition key(s) %s" % (w, sorted(extra_c)))
            cond_act = cond.get("action", "disable")
            if not isinstance(cond_act, str) or cond_act not in ("disable", "hide"):
                raise BuildError("%s: condition action must be 'disable' or 'hide'" % w)
            cond_name = cond.get("name", "")
            if not isinstance(cond_name, str) or "\t" in cond_name or "\n" in cond_name or "\r" in cond_name:
                raise BuildError("%s: condition name must be a string without tab or newline" % w)

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
            if slot < 1300 or slot <= 1225 + 2 * uu_total + ext.get("cu_total", 0) + ext.get("gather_slots", 0):
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
            sid = 0
            nm = ""
            if "name" in t:
                nm, hp = t["name"], t.get("help", t["name"])
                if not isinstance(nm, str) or not isinstance(hp, str):
                    raise BuildError("%s: train name and help must be strings" % w)
                if "\t" in nm or "\n" in nm or "\r" in nm:
                    raise BuildError("%s: train name must not contain tab or newline" % w)
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
                cur_mod_t = where.split("/", 1)[0]
                custom_res_t = ext.get("custom_res", [])
                for field, value in t["units"].items():
                    if field == "cost":
                        apply_unit_cost(u, value, cur_mod_t, len(civ.resources), custom_res_t, "%s unit %d" % (w, slot))
                    else:
                        _set_field(u, field, value, "%s unit %d" % (w, slot))
            if "graphics" in t:
                apply_graphics(dat, u, t["graphics"], "%s unit %d" % (w, slot))
            civ.units[slot] = u
            avail_eff.commands.append(df.EffectCommand(type=2, a=slot, b=1, c=-1, d=0.0))
            u_icon = int(getattr(civ.units[src], "icon_id", 0))
            ext["lua"].append("%d\t%d\t%d\t%d\ttrain\t%d\t%d\t%d\t%s\t%s\t%s" %
                              (cid, B, page, button, slot, u_icon, sid + 2 if sid else 0, nm, cond_act, cond_name))

        elif kind == "research":
            r = c["research"]
            if not isinstance(r, dict) or not isinstance(r.get("name"), str) or not isinstance(r.get("help", ""), str):
                raise BuildError("%s: research must be an object with a name (string)" % w)
            if "\t" in r["name"] or "\n" in r["name"] or "\r" in r["name"]:
                raise BuildError("%s: research name must not contain tab or newline" % w)
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
            cur_mod_r = where.split("/", 1)[0]
            custom_res_r = ext.get("custom_res", [])
            e = df.Effect()
            e.name = make_effect_name("WL UB tech %d.%d" % (cid, k), w)
            e.commands = [parse_effect_command(x, w, cur_mod_r, len(civ.resources), custom_res_r) for x in effs]
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
            apply_research_cost(tech, cost, cur_mod_r, len(civ.resources), custom_res_r, w)
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
            res_idx = len(dat.researches.researches) - 1
            ext["lua"].append("%d\t%d\t%d\t%d\tresearch\t%d\t%d\t%d\t%s\t%s\t%s" %
                              (cid, B, page, button, res_idx, int(tech.icon_id), sid + 2, nm, cond_act, cond_name))

        else:  # lua button: nothing in the dat; the DLL adds it to the building's panel
            name = c["lua"]
            if not isinstance(name, str) or not name or "\t" in name or "\n" in name or "\r" in name:
                raise BuildError("%s: lua must be a handler name" % w)
            cur_mod_l = where.split("/", 1)[0]
            icon = resolve_cmd_icon(ext, cur_mod_l, c.get("icon", 32), w)
            text = c.get("text", name)
            hp = c.get("help", "")
            if not isinstance(text, str) or not isinstance(hp, str):
                raise BuildError("%s: lua text and help must be strings" % w)
            sid = _new_sids(ext)
            ext["strings"] += [(sid, text), (sid + 1, text), (sid + 2, "<b>%s<b> \n%s" % (text, hp))]
            ext["lua"].append("%d\t%d\t%d\t%d\tlua\t0\t%d\t%d\t%s\t%s\t%s" %
                              (cid, B, page, button, icon, sid + 2, name, cond_act, cond_name))


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


BLDG_ICON_BASE = 55  # AoE2 HD 5.8 gamedata_x2/50706.slp frame count (0..54); matches iconmerge.c
UNIT_ICON_BASE = 249  # AoE2 HD 5.8 gamedata_x2/50730.slp frame count (0..248); matches iconmerge.c
CMD_ICON_BASE = 70  # AoE2 HD 5.8 interface/50721.slp frame count (0..69); matches iconmerge.c


def _is_valid_1frame_slp(path):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return False
    if len(data) <= 64 or len(data) >= 16 * 1024 * 1024 or data[:4] != b"2.0N":
        return False
    fc = struct.unpack_from("<i", data, 4)[0]
    cmd, outl, _pal, _prop, _w, h = struct.unpack_from("<IIIIii", data, 32)
    return fc == 1 and cmd >= 64 and outl >= 64 and 0 < h <= 4096 and cmd + 4 * h <= len(data)


def _is_valid_png(path):
    try:
        sz = os.path.getsize(path)
        if sz < 33 or sz >= 16 * 1024 * 1024:
            return False
        with open(path, "rb") as f:
            hdr = f.read(33)
    except OSError:
        return False
    if len(hdr) < 33 or hdr[:8] != b"\x89PNG\r\n\x1a\n":
        return False
    clen = struct.unpack_from(">I", hdr, 8)[0]
    if clen != 13 or hdr[12:16] != b"IHDR":
        return False
    w, h, bd, ct, comp, filt, il = struct.unpack_from(">IIBBBBB", hdr, 16)
    if not (1 <= w <= 4096 and 1 <= h <= 4096):
        return False
    if comp != 0 or filt != 0 or il not in (0, 1):
        return False
    if ct == 0 and bd in (1, 2, 4, 8, 16):
        return True
    if ct == 2 and bd in (8, 16):
        return True
    if ct == 3 and bd in (1, 2, 4, 8):
        return True
    if ct in (4, 6) and bd in (8, 16):
        return True
    return False


def find_icons(mods_dir, folder, base_idx):
    """Deterministic [(mod, stem, frame_index)] matching iconmerge.c and datbuild.c."""
    out = []
    if not os.path.isdir(mods_dir):
        return out
    idx = base_idx
    for mod in sorted(os.listdir(mods_dir)):
        mdir = os.path.join(mods_dir, mod)
        if not os.path.isdir(mdir):
            continue
        meta = os.path.join(mdir, "mod.json")
        if os.path.isfile(meta) and load_json(meta).get("enabled", True) is False:
            continue
        idir = os.path.join(mdir, "icons", folder)
        if not os.path.isdir(idir):
            continue
        stems = []
        for fn in os.listdir(idir):
            p = os.path.join(idir, fn)
            if fn.lower().endswith(".slp") and os.path.isfile(p):
                stems.append((fn[:-4], p))
        stems.sort(key=lambda x: x[0])
        for stem, p in stems:
            if _is_valid_1frame_slp(p):
                out.append((mod, stem, idx))
                idx += 1
    return out


def find_building_icons(mods_dir):
    return find_icons(mods_dir, "buildings", BLDG_ICON_BASE)


def find_cmd_icons(mods_dir):
    """Deterministic [(mod, stem, frame_index)] for icons/commands/*.png matching iconmerge.c and datbuild.c."""
    slp_50721 = find_icons(mods_dir, "50721", CMD_ICON_BASE)
    idx = CMD_ICON_BASE + len(slp_50721)
    out = []
    if not os.path.isdir(mods_dir):
        return out
    for mod in sorted(os.listdir(mods_dir)):
        mdir = os.path.join(mods_dir, mod)
        if not os.path.isdir(mdir):
            continue
        meta = os.path.join(mdir, "mod.json")
        if os.path.isfile(meta) and load_json(meta).get("enabled", True) is False:
            continue
        idir = os.path.join(mdir, "icons", "commands")
        if not os.path.isdir(idir):
            continue
        stems = []
        for fn in os.listdir(idir):
            p = os.path.join(idir, fn)
            if fn.lower().endswith(".png") and os.path.isfile(p):
                stems.append((fn[:-4], p))
        stems.sort(key=lambda x: x[0])
        for stem, p in stems:
            if _is_valid_png(p):
                out.append((mod, stem, idx))
                idx += 1
    return out


def resolve_cmd_icon(ext, mod, icon, where):
    if _is_int(icon):
        return icon
    if isinstance(icon, str):
        s = icon
        if s.lower().endswith(".png"):
            s = s[:-4]
        if not s or "/" in s or "\\" in s or ".." in s or "\t" in s or "\n" in s or "\r" in s:
            raise BuildError("%s: invalid command icon name %r" % (where, icon))
        stem = s.lower()
        for m, st, idx in ext.get("cmd_icons", []):
            if m == mod and st.lower() == stem:
                return idx
        raise BuildError("%s: command icon %r not found in icons/commands/" % (where, icon))
    raise BuildError("%s: lua icon must be a number or command icon name" % where)


def resolve_building_icon(ext, mod, icon):
    if icon < 1000:
        return icon
    stem = str(icon).lower()
    icons = ext.get("bldg_icons", [])
    for m, s, idx in icons:
        if m == mod and s.lower() == stem:
            return idx
    for m, s, idx in icons:
        if s.lower() == stem:
            return idx
    return None


def resolve_unit_icon(ext, mod, icon, where):
    if _is_int(icon):
        if not 0 <= icon <= 65535:
            raise BuildError("%s: icon must be an integer (0..65535)" % where)
        if icon < 1000:
            return icon
        stem = str(icon).lower()
    elif isinstance(icon, str):
        s = icon
        if s.lower().endswith(".slp"):
            s = s[:-4]
        if not s or "/" in s or "\\" in s or ".." in s or "\t" in s or "\n" in s or "\r" in s:
            raise BuildError("%s: invalid icon name %r" % (where, icon))
        stem = s.lower()
    else:
        raise BuildError("%s: icon must be an integer (0..65535) or an icon name in icons/units/" % where)
    icons = ext.get("unit_icons", [])
    for m, st, idx in icons:
        if m == mod and st.lower() == stem:
            return idx
    for m, st, idx in icons:
        if st.lower() == stem:
            return idx
    raise BuildError("%s: icon %r not found in icons/units/" % (where, icon))


def _set_unit_armor(u, cls_id, val):
    if cls_id == 4:
        u.displayed_melee_armour = val
    elif cls_id == 3:
        u.displayed_pierce_armour = val
    for a in u.armours:
        if getattr(a, "class") == cls_id:
            a.amount = val
            return
    u.armours.append(df.AttackOrArmour(**{"class": cls_id, "amount": val}))
    u.armour_count = len(u.armours)


def add_custom_units(dat, civ, cid, cu_list, ub_spec, ub_slot, where, uu_total, ext):
    """Brand-new trainable units in RESERVED slots after unique_unit slots (1224 + 2*uu_total + cu_k).

    Enabled only for `cid` via a cloned auto-tech (template 570) unlocked in `available_in_age`.
    When `trained_at` is set, registers a train button in cache/commands.txt (same mechanism as
    unique_building commands) so buildpage.c shows it on the chosen building and page.
    The C port (datbuild.c, wl_add_custom_units) must do exactly the same.
    """
    if not isinstance(cu_list, list):
        raise BuildError("%s: custom_units must be a list" % where)
    known_cu = {
        "id", "name", "copy_from", "help", "cost", "train_time", "hit_points",
        "attack", "armor", "pierce_armor", "range", "speed", "line_of_sight",
        "units", "graphics", "icon", "available_in_age", "trained_at"
    }
    civ1 = dat.civilizations.civs[1]
    cur_mod = where.split("/", 1)[0]
    seen_ids = set()

    for k, cu in enumerate(cu_list):
        w = "%s custom_units[%d]" % (where, k)
        if not isinstance(cu, dict):
            raise BuildError("%s: must be an object" % w)
        for req in ("id", "name", "copy_from"):
            if req not in cu:
                raise BuildError("%s: missing required key %r" % (w, req))
        extra = {key for key in cu if not key.startswith("_")} - known_cu
        if extra:
            raise BuildError("%s: unknown key(s) %s" % (w, sorted(extra)))

        uid_key = cu["id"]
        if not isinstance(uid_key, str) or not uid_key or len(uid_key) > 64 or any(c in uid_key for c in "\t\n\r"):
            raise BuildError("%s: id must be a non-empty string (max 64 chars)" % w)
        if uid_key in seen_ids:
            raise BuildError("%s: duplicate custom unit id %r" % (w, uid_key))
        seen_ids.add(uid_key)

        nm = cu["name"]
        if not isinstance(nm, str) or not nm or any(c in nm for c in "\t\n\r"):
            raise BuildError("%s: name must be a non-empty string without tab or newline" % w)
        hp = cu.get("help", "")
        if hp is None:
            hp = ""
        if not isinstance(hp, str):
            raise BuildError("%s: help must be a string" % w)
        if not hp:
            hp = nm

        copy_from = cu["copy_from"]
        if not _is_int(copy_from) or not 0 <= copy_from < len(civ.units) or civ.units[copy_from] is None:
            raise BuildError("%s: copy_from unit %r does not exist" % (w, copy_from))
        if getattr(civ.units[copy_from], "type", None) != 70:
            raise BuildError("%s: copy_from unit %d must be a trainable unit (type 70)" % (w, copy_from))

        slot = 1224 + 2 * uu_total + ext["cu_k"]
        ext["cu_k"] += 1
        if slot > 1400:
            raise BuildError("%s: custom unit slot %d exceeds 1400" % (w, slot))
        if slot > 1400 - ext["slots"]:
            raise BuildError("%s: custom unit slot %d collides with unique building slots" % (w, slot))
        u_check = civ1.units[slot] if slot < len(civ1.units) else None
        if u_check is None or df.cstr(u_check.name) != "RESERVED":
            raise BuildError("%s: civ 1 unit at %d is not named 'RESERVED'" % (w, slot))

        u = copy.deepcopy(civ.units[copy_from])
        u.id = slot
        u.id2 = slot
        u.id3 = slot
        u.enabled = 0
        u.train_location_id = -1
        u.button_id = 0

        sid = _new_sids(ext)
        ext["strings"] += [
            (sid, nm),
            (sid + 1, "Create %s" % nm),
            (sid + 2, "Create <b>%s<b> (<cost>) \n%s \n<hp> <attack> <armor> <piercearmor> <range>" % (nm, hp)),
        ]
        u.language_dll_name = sid
        u.language_dll_creation = sid + 1
        u.language_dll_help = 79000 + sid + 2
        u.language_dll_hotkey_text = 150000 + sid
        un = make_unit_name(nm, w)
        u.name = un
        u.name_length = len(un)

        custom_res_cu = ext.get("custom_res", [])
        if "units" in cu and cu["units"] is not None:
            if not isinstance(cu["units"], dict):
                raise BuildError("%s: units must be an object" % w)
            for field, value in cu["units"].items():
                if field == "graphics":
                    apply_graphics(dat, u, value, "%s unit %d" % (w, slot))
                elif field == "cost":
                    apply_unit_cost(u, value, cur_mod, len(civ.resources), custom_res_cu, "%s unit %d" % (w, slot))
                else:
                    _set_field(u, field, value, "%s unit %d" % (w, slot))

        for stat_key in ("hit_points", "train_time", "speed", "line_of_sight"):
            if stat_key in cu and cu[stat_key] is not None:
                _set_field(u, stat_key, cu[stat_key], "%s unit %d" % (w, slot))
        if "range" in cu and cu["range"] is not None:
            _set_field(u, "max_range", cu["range"], "%s unit %d" % (w, slot))
            _set_field(u, "displayed_range", cu["range"], "%s unit %d" % (w, slot))

        if "cost" in cu and cu["cost"] is not None:
            apply_unit_cost(u, cu["cost"], cur_mod, len(civ.resources), custom_res_cu, w)

        if "attack" in cu and cu["attack"] is not None:
            atk = cu["attack"]
            if not _is_int(atk) or not 0 <= atk <= 32767:
                raise BuildError("%s: attack must be an integer 0..32767" % w)
            u.displayed_attack = atk
            has_c3 = any(getattr(a, "class") == 3 for a in u.attacks)
            has_c4 = any(getattr(a, "class") == 4 for a in u.attacks)
            target_cls = 3 if (has_c3 and (u.projectile_unit_id >= 0 or not has_c4)) else (4 if has_c4 else (3 if u.projectile_unit_id >= 0 else 4))
            updated = False
            for a in u.attacks:
                if getattr(a, "class") == target_cls:
                    a.amount = atk
                    updated = True
                    break
            if not updated:
                u.attacks.append(df.AttackOrArmour(**{"class": target_cls, "amount": atk}))
                u.attack_count = len(u.attacks)

        if "armor" in cu and cu["armor"] is not None:
            arm = cu["armor"]
            if _is_int(arm):
                if not 0 <= arm <= 32767:
                    raise BuildError("%s: armor must be an integer 0..32767" % w)
                _set_unit_armor(u, 4, arm)
            elif isinstance(arm, dict):
                extra_arm = set(arm) - {"melee", "pierce"}
                if extra_arm:
                    raise BuildError("%s: unknown armor key(s) %s" % (w, sorted(extra_arm)))
                if "melee" in arm:
                    if not _is_int(arm["melee"]) or not 0 <= arm["melee"] <= 32767:
                        raise BuildError("%s: armor.melee must be an integer 0..32767" % w)
                    _set_unit_armor(u, 4, arm["melee"])
                if "pierce" in arm:
                    if not _is_int(arm["pierce"]) or not 0 <= arm["pierce"] <= 32767:
                        raise BuildError("%s: armor.pierce must be an integer 0..32767" % w)
                    _set_unit_armor(u, 3, arm["pierce"])
            else:
                raise BuildError("%s: armor must be an integer or {\"melee\", \"pierce\"}" % w)

        if "pierce_armor" in cu and cu["pierce_armor"] is not None:
            pa = cu["pierce_armor"]
            if not _is_int(pa) or not 0 <= pa <= 32767:
                raise BuildError("%s: pierce_armor must be an integer 0..32767" % w)
            _set_unit_armor(u, 3, pa)

        if "graphics" in cu and cu["graphics"] is not None:
            apply_graphics(dat, u, cu["graphics"], w)

        if "icon" in cu and cu["icon"] is not None:
            u.icon_id = resolve_unit_icon(ext, cur_mod, cu["icon"], w)

        age = cu.get("available_in_age", 0)
        if age is None:
            age = 0
        if not _is_int(age) or age not in AGE_TECHS:
            raise BuildError("%s: available_in_age must be 0..3" % w)

        if "trained_at" in cu and cu["trained_at"] is not None:
            ta = cu["trained_at"]
            if not isinstance(ta, dict):
                raise BuildError("%s: trained_at must be an object" % w)
            extra_ta = set(ta) - {"building", "page", "button"}
            if extra_ta:
                raise BuildError("%s: unknown trained_at key(s) %s" % (w, sorted(extra_ta)))
            if "building" not in ta:
                raise BuildError("%s: trained_at missing required key 'building'" % w)
            bref = ta["building"]
            if isinstance(bref, str):
                ub_name = ub_spec.get("name") if isinstance(ub_spec, dict) else None
                if bref == "unique_building" or (ub_name and bref == ub_name):
                    if ub_slot < 0:
                        raise BuildError("%s: trained_at building %r requires a unique_building on this civ" % (w, bref))
                    bldg_id = ub_slot
                else:
                    raise BuildError("%s: trained_at unknown building key %r" % (w, bref))
            elif _is_int(bref):
                if not 0 <= bref < len(civ.units) or civ.units[bref] is None:
                    raise BuildError("%s: trained_at building unit %d does not exist" % (w, bref))
                if getattr(civ.units[bref], "type", None) != 80:
                    raise BuildError("%s: trained_at building unit %d must be a building (type 80)" % (w, bref))
                bldg_id = bref
            else:
                raise BuildError("%s: trained_at building must be a unit id or 'unique_building'" % w)

            page = ta.get("page", 1)
            if not _is_int(page) or page < 1:
                raise BuildError("%s: trained_at page must be an integer >= 1" % w)
            button = ta.get("button", 1)
            if not _is_int(button) or not 1 <= button <= 14:
                raise BuildError("%s: trained_at button must be 1..14 (button 15 is reserved for Next Page)" % w)

            u.train_location_id = bldg_id
            u.button_id = button
            ext["lua"].append("%d\t%d\t%d\t%d\ttrain\t%d\t%d\t%d\t%s\tdisable\t" %
                              (cid, bldg_id, page, button, slot, int(u.icon_id), sid + 2, nm))

        civ.units[slot] = u

        eff = df.Effect()
        eff.name = make_effect_name("WL CU avail %d.%d" % (cid, k), w)
        eff.commands = [df.EffectCommand(type=2, a=slot, b=1, c=-1, d=0.0)]
        eff.effect_count = 1
        dat.effects.effects.append(eff)
        dat.effects.count = len(dat.effects.effects)
        eff_id = len(dat.effects.effects) - 1

        tech = copy.deepcopy(dat.researches.researches[UB_TEMPLATE_TECH])
        tech.civ = cid
        tech.effect_id = eff_id
        tech.required_techs[0] = AGE_TECHS[age]
        for i in range(1, len(tech.required_techs)):
            tech.required_techs[i] = -1
        tech.required_tech_count = 1
        tech.language_dll_name = sid
        tech.language_dll_description = sid
        tech.name = ("WL CU %d.%d\0" % (cid, k)).encode("ascii")
        tech.name_length = len(tech.name)
        dat.researches.researches.append(tech)
        dat.researches.count = len(dat.researches.researches)


GATHER_MALE_TEMPLATE = 579      # VMGLD: male Gold Miner (villager_mode=1)
GATHER_FEMALE_TEMPLATE = 581    # VFGLD: female Gold Miner (villager_mode=2)


def apply_gather_resources(dat, custom_res, uu_total, ext):
    """Allocate RESERVED unit slots (deposit, male miner, female miner) for gatherable custom resources.

    Slots count up right after custom units: 1224 + 2*uu_total + cu_total + 3*k.
    Mirrors wl_apply_gather_resources in datbuild.c byte-for-byte.
    Returns list of tab-separated lines for cache/gather.txt:
        key<TAB>resource_id<TAB>deposit_id<TAB>male_id<TAB>female_id
    """
    civs = dat.civilizations.civs
    civ0 = civs[0]
    civ1 = civs[1]
    headers = dat.unit_headers.headers
    gather_lines = []
    g_k = 0
    for r in custom_res:
        g = r.get("gather")
        if g is None:
            continue
        gw = g["where"]
        rid = r["id"]
        dep_id = 1224 + 2 * uu_total + ext.get("cu_total", 0) + 3 * g_k
        male_id = dep_id + 1
        female_id = dep_id + 2
        g_k += 1

        if female_id > 1400:
            raise BuildError("%s: gather unit slot %d exceeds 1400" % (gw, female_id))
        if female_id > 1400 - ext["slots"]:
            raise BuildError("%s: gather unit slot %d collides with unique building slots" % (gw, female_id))
        if female_id >= len(headers):
            raise BuildError("%s: gather unit slot %d exceeds unit header count" % (gw, female_id))

        for slot in (dep_id, male_id, female_id):
            u_check = civ1.units[slot] if slot < len(civ1.units) else None
            if u_check is None or df.cstr(u_check.name) != "RESERVED":
                raise BuildError("%s: civ 1 unit at %d is not named 'RESERVED'" % (gw, slot))

        copy_from = g["copy_from"]
        if not 0 <= copy_from < len(civ0.units) or civ0.units[copy_from] is None:
            raise BuildError("%s: copy_from unit %d does not exist in Gaia" % (gw, copy_from))
        if copy_from >= len(headers):
            raise BuildError("%s: copy_from unit %d exceeds unit header count" % (gw, copy_from))

        if g["drop_sites"] is not None:
            for ds in g["drop_sites"]:
                if ds >= 0 and ds >= len(civ1.units):
                    raise BuildError("%s: drop_sites unit %d out of range" % (gw, ds))

        dep_uname = make_unit_name(g["deposit_name"], gw)
        miner_uname = make_unit_name(g["miner_name"], gw)

        sid_dep = _new_sids(ext)
        ext["strings"] += [(sid_dep, g["deposit_name"]),
                           (sid_dep + 1, g["deposit_name"]),
                           (sid_dep + 2, g["deposit_name"])]
        sid_miner = _new_sids(ext)
        ext["strings"] += [(sid_miner, g["miner_name"]),
                           (sid_miner + 1, g["miner_name"]),
                           (sid_miner + 2, g["miner_name"])]

        # Section 9: UnitHeaders for deposit (mirrors copy_from) and male/female miners (from 579/581).
        headers[dep_id] = copy.deepcopy(headers[copy_from])
        for tmpl_id, slot_id, carry_key in ((GATHER_MALE_TEMPLATE, male_id, "carry_male_slp"),
                                            (GATHER_FEMALE_TEMPLATE, female_id, "carry_female_slp")):
            src_hdr = headers[tmpl_id] if tmpl_id < len(headers) else None
            if src_hdr is None or not src_hdr.exists or not src_hdr.commands:
                raise BuildError("%s: template miner unit header %d has no commands" % (gw, tmpl_id))
            new_hdr = copy.deepcopy(src_hdr)
            # Drop task type 110 (ROCKGX 841) so custom miners only gather their new deposit.
            kept = [cmd for cmd in new_hdr.commands if cmd.type != 110]
            found_gather = False
            carry_slp = g[carry_key]
            for idx, cmd in enumerate(kept):
                cmd.id = idx
                if cmd.type == 5:
                    cmd.class_id = -1
                    cmd.unit_id = dep_id
                    cmd.terrain_id = -1
                    cmd.resource_in = rid
                    cmd.resource_productivity_multiplier = -1
                    cmd.resource_out = rid
                    if carry_slp is not None and cmd.graphics[3] >= 0:
                        remapped = SLP_REMAP.get(carry_slp, carry_slp)
                        cmd.graphics[3] = add_graphic(dat, cmd.graphics[3], remapped, None, None, gw)
                    found_gather = True
            if not found_gather:
                raise BuildError("%s: template miner unit header %d has no Gather (type 5) task" % (gw, tmpl_id))
            new_hdr.commands = kept
            headers[slot_id] = new_hdr

        dep_gid = None
        if g["deposit_slp"] is not None:
            src_dep_gid = civ0.units[copy_from].standing_graphic[0]
            if src_dep_gid < 0:
                raise BuildError("%s: copy_from unit %d has no standing graphic" % (gw, copy_from))
            remapped = SLP_REMAP.get(g["deposit_slp"], g["deposit_slp"])
            dep_gid = add_graphic(dat, src_dep_gid, remapped, None, None, gw)

        # Section 10: Deposit in Gaia (and any civ where copy_from is present; absent in civs where copy_from is None).
        for civ in civs:
            src_dep = civ.units[copy_from] if copy_from < len(civ.units) else None
            if src_dep is None:
                civ.unit_ptrs[dep_id] = 0
                civ.units[dep_id] = None
            else:
                du = copy.deepcopy(src_dep)
                du.id = dep_id
                du.id2 = dep_id
                du.id3 = dep_id
                du.name = dep_uname
                du.name_length = len(dep_uname)
                du.enabled = 1
                du.language_dll_name = sid_dep
                du.language_dll_creation = sid_dep + 1
                du.language_dll_help = 79000 + sid_dep + 2
                du.language_dll_hotkey_text = 150000 + sid_dep
                if dep_gid is not None:
                    du.standing_graphic[0] = dep_gid
                du.resource_storages[0].type = rid
                if g["amount"] is not None:
                    du.resource_storages[0].amount = float(g["amount"])
                du.resource_storages[1] = df.ResourceStorage(type=-1, amount=0.0, store_mode=0)
                du.resource_storages[2] = df.ResourceStorage(type=-1, amount=0.0, store_mode=0)
                if g["minimap_color"] is not None:
                    du.minimap_color = g["minimap_color"]
                civ.unit_ptrs[dep_id] = 1
                civ.units[dep_id] = du

            # Male & female miners in every civ that has villagers (579/581).
            has_miners = (GATHER_MALE_TEMPLATE < len(civ.units) and civ.units[GATHER_MALE_TEMPLATE] is not None and
                          GATHER_FEMALE_TEMPLATE < len(civ.units) and civ.units[GATHER_FEMALE_TEMPLATE] is not None)
            if has_miners:
                for tmpl_id, slot_id in ((GATHER_MALE_TEMPLATE, male_id), (GATHER_FEMALE_TEMPLATE, female_id)):
                    mu = copy.deepcopy(civ.units[tmpl_id])
                    mu.id = slot_id
                    mu.id2 = slot_id
                    mu.id3 = slot_id
                    mu.name = miner_uname
                    mu.name_length = len(miner_uname)
                    mu.language_dll_name = sid_miner
                    mu.language_dll_creation = sid_miner + 1
                    mu.language_dll_help = 79000 + sid_miner + 2
                    mu.language_dll_hotkey_text = 150000 + sid_miner
                    if g["work_rate"] is not None:
                        mu.work_rate = float(g["work_rate"])
                    if g["carry"] is not None:
                        mu.resource_capacity = int(g["carry"])
                    if g["drop_sites"] is not None:
                        mu.drop_site = list(g["drop_sites"])
                    civ.unit_ptrs[slot_id] = 1
                    civ.units[slot_id] = mu
            else:
                civ.unit_ptrs[male_id] = 0
                civ.units[male_id] = None
                civ.unit_ptrs[female_id] = 0
                civ.units[female_id] = None

        gather_lines.append("%s\t%d\t%d\t%d\t%d" % (r["key"], rid, dep_id, male_id, female_id))
    return gather_lines


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

    mods_dir = os.path.join(game_dir, "wololoader", "mods")
    custom_res = find_custom_resources(mods_dir)
    if custom_res:
        # Grow every base civ (0 = Gaia, 1..31) identically so placeholders (32..34)
        # and custom civs (35+) inherit the exact same resource_count and start values.
        for c in civs:
            while len(c.resources) < CUSTOM_RES_FIRST:
                c.resources.append(0.0)
            for r in custom_res:
                c.resources.append(float(r["start"]))
            c.resource_count = len(c.resources)

    files = find_civ_files(mods_dir)
    remap = slp_remap(game_dir, mods_dir)
    lines = []
    ext = {"slots": 0, "sids": 0, "strings": [], "lua": [], "orig_res": len(dat.researches.researches),
           "bldg_icons": find_building_icons(mods_dir),
           "unit_icons": find_icons(mods_dir, "units", UNIT_ICON_BASE),
           "cmd_icons": find_cmd_icons(mods_dir),
           "custom_res": custom_res,
           "cu_k": 0, "cu_total": 0,
           "gather_slots": 3 * sum(1 for r in custom_res if r["gather"] is not None)}
    uu_total = 0
    if files:
        for k in range(NUM_RESERVED):
            ph = copy.deepcopy(civs[1])
            ph.name = make_name("WL_RESERVED_%d" % (FIRST_RESERVED_ID + k), "placeholder")
            civs.append(ph)
        uu_k = 0
        ub_k = 0
        for mod, fn, path in files:      # unique unit slots count up, building slots count down
            spec = load_json(path)
            if isinstance(spec, dict):
                if "unique_unit" in spec:
                    uu_total += 1
                cu = spec.get("custom_units")
                if isinstance(cu, list):
                    ext["cu_total"] += len(cu)
        for mod, fn, path in files:
            spec = load_json(path)
            where = "%s/civs/%s" % (mod, fn)
            SLP_REMAP.clear()
            SLP_REMAP.update(remap.get(mod, {}))
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

    gather_lines = apply_gather_resources(dat, custom_res, uu_total, ext)

    out = dat.serialize()
    os.makedirs(cache, exist_ok=True)
    out_dat = os.path.join(cache, "empires2_x2_p1.dat")
    res_txt = "".join(
        "%d\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%s\n" % (
            r["id"], r["mod"], r["key"], r["name"], r["icon"],
            fmt_res_float(r["start"]), fmt_res_float(r["max"]), r["show"],
            r["description"].replace("\t", " ").replace("\r", "").replace("\n", "\\n"),
            r["hidden"], r["game_mode"],
        )
        for r in custom_res
    ).encode("utf-8")
    gather_txt = "".join(l + "\n" for l in gather_lines).encode("utf-8")
    for path, data, mode in ((out_dat, df.compress(out), "wb"),
                             (os.path.join(cache, "civs.txt"),
                              "".join(l + "\n" for l in lines).encode("utf-8"), "wb"),
                             # building command strings (id<TAB>text, "\n" kept as backslash-n)
                             # and Lua buttons (civ<TAB>building<TAB>button<TAB>name<TAB>icon<TAB>help id)
                             (os.path.join(cache, "strings.txt"),
                              "".join("%d\t%s\n" % (i, t.replace("\t", " ").replace("\n", "\\n"))
                                      for i, t in ext["strings"]).encode("utf-8"), "wb"),
                             (os.path.join(cache, "commands.txt"),
                              "".join(l + "\n" for l in ext["lua"]).encode("utf-8"), "wb"),
                             (os.path.join(cache, "resources.txt"), res_txt, "wb"),
                             (os.path.join(cache, "gather.txt"), gather_txt, "wb")):
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
