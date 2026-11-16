#!/usr/bin/env python3
"""
datfile.py - reader / writer for the Age of Empires II HD (v5.x) ``empires2_x1_p1.dat``.

The HD dat is a raw-deflate stream (wbits=-15) wrapping a Genie-engine data file
(same family as the formats handled by genieutils / openage).  This module:

  * decompress()/compress()      - raw deflate helpers
  * DatFile.parse(bytes)         - parses the whole file into plain Python records
  * DatFile.serialize()          - writes the records back (byte-identical when
                                   nothing was modified; verified by `verify`)
  * DatFile.offsets              - {section: (start, end)} byte offsets inside the
                                   *decompressed* data, so a patcher can splice
                                   modified sections back in
  * CLI: ``python3 -I datfile.py info <dat>`` / ``verify <dat>``

Layout knowledge is expressed declaratively: every record type is a ``Struct``
subclass with a ``FIELDS`` list; reading and writing are generated from it, so
parse -> serialize is symmetric by construction.  Fixed char arrays are kept as
raw ``bytes`` (including any garbage after the NUL) to stay byte exact.

Only the HD 5.x variant is supported (no conditional-on-version fields).
"""

import struct
import sys
import zlib

# --------------------------------------------------------------------------
# Compression helpers
# --------------------------------------------------------------------------


def decompress(data: bytes) -> bytes:
    """Raw-deflate decompress (no zlib header)."""
    return zlib.decompress(data, -15)


def compress(data: bytes, level: int = 9) -> bytes:
    """Raw-deflate compress (no zlib header)."""
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


# --------------------------------------------------------------------------
# Binary reader / writer
# --------------------------------------------------------------------------


class Reader:
    """Cursor over a bytes object (little endian)."""

    def __init__(self, data: bytes, pos: int = 0):
        self.data = data
        self.pos = pos

    def unpack(self, st: struct.Struct):
        v = st.unpack_from(self.data, self.pos)
        self.pos += st.size
        return v

    def read(self, n: int) -> bytes:
        if n < 0 or self.pos + n > len(self.data):
            raise EOFError("read of %d bytes at 0x%x past end" % (n, self.pos))
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def scalar(self, fmt: str):
        st = _SCALAR[fmt]
        return self.unpack(st)[0]

    def remaining(self) -> int:
        return len(self.data) - self.pos


class Writer:
    """Append-only byte sink."""

    def __init__(self):
        self.buf = bytearray()

    def pack(self, st: struct.Struct, *vals):
        self.buf += st.pack(*vals)

    def write(self, b: bytes):
        self.buf += b

    def scalar(self, fmt: str, v):
        self.buf += _SCALAR[fmt].pack(v)

    def tell(self) -> int:
        return len(self.buf)


# primitive type name -> struct char
PRIM = {
    "u8": "B", "i8": "b", "u16": "H", "i16": "h",
    "u32": "I", "i32": "i", "f32": "f", "f64": "d",
}
_SCALAR = {k: struct.Struct("<" + v) for k, v in PRIM.items()}


# --------------------------------------------------------------------------
# Declarative struct machinery
# --------------------------------------------------------------------------
#
# FIELDS entries:  (name, type)                     scalar / nested struct
#                  (name, type, count)              array; count is an int or the
#                                                   name of an earlier field
#                  (name, "bytes", n)               raw bytes (fixed char arrays)
#                  (name, "pstr", lenfield_type)    length-prefixed raw bytes
#                  (name, "ptrs")                   see ptr arrays below
#
# Consecutive fixed-size primitives are merged into one struct.Struct for speed.


class Struct:
    """Base for records described by FIELDS."""

    FIELDS = ()
    _plan = None

    def __init__(self, **kw):
        self.__dict__.update(kw)

    def __repr__(self):
        items = []
        for k, v in self.__dict__.items():
            r = repr(v)
            items.append("%s=%s" % (k, r if len(r) < 40 else r[:37] + "..."))
        return "%s(%s)" % (type(self).__name__, ", ".join(items))

    # -- compile FIELDS into a plan of (run | dynamic) steps ---------------
    @classmethod
    def _compile(cls):
        plan = []
        run_fmt, run_items = "<", []

        def flush():
            nonlocal run_fmt, run_items
            if run_items:
                plan.append(("run", struct.Struct(run_fmt), run_items))
            run_fmt, run_items = "<", []

        for f in cls.FIELDS:
            name, typ = f[0], f[1]
            cnt = f[2] if len(f) > 2 else None
            if typ in PRIM and (cnt is None or isinstance(cnt, int)):
                n = 1 if cnt is None else cnt
                run_fmt += (str(n) if n != 1 or cnt is not None else "") + PRIM[typ]
                run_items.append((name, "s" if cnt is None else "a", n))
            elif typ == "bytes" and isinstance(cnt, int):
                run_fmt += "%ds" % cnt
                run_items.append((name, "b", 1))
            else:
                flush()
                plan.append(("dyn", name, typ, cnt))
        flush()
        cls._plan = plan

    # -- read ----------------------------------------------------------------
    @classmethod
    def read(cls, r: Reader, ctx=None):
        if cls._plan is None or "_plan" not in cls.__dict__:
            cls._compile()
        o = cls.__new__(cls)
        d = o.__dict__
        for step in cls._plan:
            if step[0] == "run":
                vals = r.unpack(step[1])
                i = 0
                for name, kind, n in step[2]:
                    if kind == "a":
                        d[name] = list(vals[i:i + n])
                        i += n
                    else:
                        d[name] = vals[i]
                        i += 1
            else:
                _, name, typ, cnt = step
                if isinstance(cnt, str):
                    cnt = d[cnt]
                d[name] = _read_dyn(r, typ, cnt, d)
        return o

    # -- write ---------------------------------------------------------------
    def write(self, w: Writer):
        cls = type(self)
        if cls._plan is None or "_plan" not in cls.__dict__:
            cls._compile()
        d = self.__dict__
        for step in cls._plan:
            if step[0] == "run":
                vals = []
                for name, kind, n in step[2]:
                    if kind == "a":
                        vals.extend(d[name])
                    else:
                        vals.append(d[name])
                w.pack(step[1], *vals)
            else:
                _, name, typ, cnt = step
                if isinstance(cnt, str):
                    cnt = d[cnt]
                _write_dyn(w, typ, cnt, d[name])


def _read_dyn(r, typ, cnt, d):
    if typ == "bytes":
        return r.read(cnt)
    if typ in PRIM:  # dynamic-length primitive array
        st = struct.Struct("<%d%s" % (cnt, PRIM[typ]))
        return list(r.unpack(st))
    if cnt is None:  # nested struct
        return typ.read(r)
    return [typ.read(r) for _ in range(cnt)]  # array of structs


def _write_dyn(w, typ, cnt, v):
    if typ == "bytes":
        w.write(v)
    elif typ in PRIM:
        w.pack(struct.Struct("<%d%s" % (len(v), PRIM[typ])), *v)
    elif cnt is None:
        v.write(w)
    else:
        for x in v:
            x.write(w)


def cstr(b: bytes) -> str:
    """Decode a NUL terminated (or padded) char array for display."""
    return b.split(b"\0", 1)[0].decode("latin-1")


# --------------------------------------------------------------------------
# Section records
# --------------------------------------------------------------------------


class TerrainPassGraphic(Struct):
    FIELDS = [("exit_tile_sprite", "i32"), ("enter_tile_sprite", "i32"),
              ("walk_tile_sprite", "i32"), ("walk_rate", "f32")]


class TerrainRestriction(Struct):
    """Per-terrain passability multipliers + pass graphics (counts come from the header)."""
    N_TERRAINS = 0  # set by the parser (header value)

    @classmethod
    def read_n(cls, r, n):
        o = cls.__new__(cls)
        o.passable = list(r.unpack(struct.Struct("<%df" % n)))
        o.pass_graphics = [TerrainPassGraphic.read(r) for _ in range(n)]
        return o

    def write(self, w):
        w.pack(struct.Struct("<%df" % len(self.passable)), *self.passable)
        for g in self.pass_graphics:
            g.write(w)


class PlayerColour(Struct):
    FIELDS = [("id", "i32"), ("player_color_base", "i32"), ("outline_color", "i32"),
              ("selection_color1", "i32"), ("selection_color2", "i32"),
              ("minimap_color1", "i32"), ("minimap_color2", "i32"),
              ("minimap_color3", "i32"), ("statistics_text_color", "i32")]


class SoundItem(Struct):
    FIELDS = [("filename", "bytes", 13), ("resource_id", "i32"), ("probability", "i16"),
              ("civilization", "i16"), ("icon_set", "i16")]


class Sound(Struct):
    FIELDS = [("id", "i16"), ("play_delay", "i16"), ("file_count", "i16"),
              ("cache_time", "i32"), ("items", SoundItem, "file_count")]


class GraphicDelta(Struct):
    # 16 bytes: id, padding, runtime pointer slot, then offsets / angle
    FIELDS = [("graphic_id", "i16"), ("padding1", "i16"), ("unknown_ptr", "i32"),
              ("direction_x", "i16"),
              ("direction_y", "i16"), ("display_angle", "i16"), ("padding2", "i16")]


class GraphicAttackSound(Struct):
    # three (delay, sound) pairs per angle
    FIELDS = [("sound_delay1", "i16"), ("sound_id1", "i16"),
              ("sound_delay2", "i16"), ("sound_id2", "i16"),
              ("sound_delay3", "i16"), ("sound_id3", "i16")]


class Graphic(Struct):
    FIELDS = [
        ("name", "bytes", 21), ("filename", "bytes", 13), ("slp", "i32"),
        ("is_loaded", "i8"), ("old_color_flag", "i8"), ("layer", "i8"),
        ("player_color", "i16"), ("transparent_selection", "i8"),
        ("coordinates", "i16", 4), ("delta_count", "u16"), ("sound_id", "i16"),
        ("attack_sound_used", "i8"), ("frame_count", "u16"), ("angle_count", "u16"),
        ("new_speed", "f32"), ("frame_rate", "f32"), ("replay_delay", "f32"),
        ("sequence_type", "i8"), ("id", "i16"), ("mirroring_mode", "i8"),
        ("unknown2", "i8"),
        ("deltas", GraphicDelta, "delta_count"),
    ]

    @classmethod
    def read(cls, r, ctx=None):
        o = super().read(r)
        o.attack_sounds = []
        if o.attack_sound_used:
            o.attack_sounds = [GraphicAttackSound.read(r) for _ in range(o.angle_count)]
        return o

    def write(self, w):
        super().write(w)
        for a in self.attack_sounds:
            a.write(w)


class TileSize(Struct):
    FIELDS = [("width", "i16"), ("height", "i16"), ("delta_z", "i16")]


class FrameData(Struct):
    FIELDS = [("frame_count", "i16"), ("angle_count", "i16"), ("shape_id", "i16")]


class Terrain(Struct):
    """One terrain slot (436 bytes).  HD stores 42 slots; unused ones are all zero."""
    FIELDS = [
        ("enabled", "i8"), ("random", "i8"), ("name", "bytes", 13), ("name2", "bytes", 13),
        ("slp", "i32"), ("shape_ptr", "i32"), ("sound_id", "i32"),
        ("blend_priority", "i32"), ("blend_mode", "i32"),
        ("colors", "u8", 3), ("cliff_colors", "u8", 2),
        ("passable_terrain", "i8"), ("impassable_terrain", "i8"),
        ("is_animated", "i8"), ("animation_frames", "i16"), ("pause_frames", "i16"),
        ("interval", "f32"), ("pause_between_loops", "f32"),
        ("frame", "i16"), ("draw_frame", "i16"), ("animate_last", "f32"),
        ("frame_changed", "i8"), ("drawn", "i8"),
        ("elevation_graphics", FrameData, 19),
        ("terrain_to_draw", "i16"), ("terrain_dimensions", "i16", 2),
        ("borders", "i16", 42),
        ("terrain_unit_id", "i16", 30), ("terrain_unit_density", "i16", 30),
        ("terrain_unit_priority", "i8", 30),
        ("number_of_terrain_units_used", "i16"), ("padding", "i16"),
    ]


class TerrainBorder(Struct):
    """One terrain border slot (1440 bytes); HD stores 16."""
    FIELDS = [
        ("enabled", "i8"), ("random", "i8"), ("name", "bytes", 13), ("name2", "bytes", 13),
        ("slp", "i32"), ("shape_ptr", "i32"), ("sound_id", "i32"),
        ("colors", "u8", 3), ("is_animated", "i8"),
        ("animation_frames", "i16"), ("pause_frames", "i16"),
        ("interval", "f32"), ("pause_between_loops", "f32"),
        ("frame", "i16"), ("draw_frame", "i16"), ("animate_last", "f32"),
        ("frame_changed", "i8"), ("drawn", "i8"),
        ("borders", FrameData, 19 * 12),
        ("draw_terrain", "i16"), ("underlay_terrain", "i16"), ("border_style", "i16"),
    ]


class TerrainBlock(Struct):
    FIELDS = [
        ("virtual_function_ptr", "i32"), ("map_pointer", "i32"),
        ("map_width", "i32"), ("map_height", "i32"),
        ("world_width", "i32"), ("world_height", "i32"),
        ("tile_sizes", TileSize, 19), ("padding_ts", "i16"),
        ("terrains", Terrain, 42),
        ("terrain_borders", TerrainBorder, 16),
        ("map_row_offset", "i32"),
        ("map_min_x", "f32"), ("map_min_y", "f32"), ("map_max_x", "f32"),
        ("map_max_y", "f32"), ("map_max_x_plus1", "f32"), ("map_max_y_plus1", "f32"),
        ("terrains_used2", "u16"), ("borders_used", "u16"), ("max_terrain", "i16"),
        ("tile_width", "i16"), ("tile_height", "i16"), ("tile_half_height", "i16"),
        ("tile_half_width", "i16"), ("elev_height", "i16"),
        ("cur_row", "i16"), ("cur_col", "i16"), ("block_beg_row", "i16"),
        ("block_end_row", "i16"), ("block_beg_col", "i16"), ("block_end_col", "i16"),
        ("search_map_ptr", "i32"), ("search_map_rows_ptr", "i32"),
        ("any_frame_change", "i8"), ("map_visible_flag", "i8"), ("fog_flag", "i8"),
        ("unknown_blob0", "bytes", 21),
        ("unknown_blob1", "i32", 157),
    ]


_TB_CACHE = {}


def terrain_block_class(slots: int):
    """TerrainBlock variant with `slots` terrain slots.

    The per-terrain `borders` array has one i16 per slot, so the Terrain record
    size depends on it: 42 slots -> 436 bytes (x1_p1), 100 slots -> 552 (x2_p1).
    """
    if slots not in _TB_CACHE:
        tf = [(f[0], f[1], slots) if f[0] == "borders" else f for f in Terrain.FIELDS]
        t_cls = type("Terrain%d" % slots, (Terrain,), {"FIELDS": tf})
        bf = [(f[0], t_cls, slots) if f[0] == "terrains" else f for f in TerrainBlock.FIELDS]
        _TB_CACHE[slots] = type("TerrainBlock%d" % slots, (TerrainBlock,), {"FIELDS": bf})
    return _TB_CACHE[slots]


def read_terrain_block(r: Reader, terrain_count: int) -> TerrainBlock:
    """Detect the terrain slot count (it is not stored) and read the block.

    A terrain block is correct when the first terrain border's sound id (-1) sits
    at the expected place for both the first and second border record.
    """
    base = r.pos + 24 + 114 + 2  # after 6 ints, 19 tile sizes, padding
    for slots in (terrain_count + 1, terrain_count, 42, 100):
        stride = 436 + 2 * (slots - 42)
        b = base + slots * stride + 36
        d = r.data
        if (b + 1440 + 4 <= len(d) and d[b:b + 4] == b"\xff\xff\xff\xff"
                and d[b + 1440:b + 1444] == b"\xff\xff\xff\xff"):
            return terrain_block_class(slots).read(r)
    raise ValueError("cannot determine terrain slot count (header count %d)" % terrain_count)


# ---- random maps ---------------------------------------------------------

class MapLand(Struct):  # 44 bytes
    FIELDS = [("land_id", "i32"), ("terrain", "i32"), ("land_spacing", "i32"),
              ("base_size", "i32"), ("zone", "i8"), ("placement_type", "i8"),
              ("padding1", "i16"), ("base_x", "i32"), ("base_y", "i32"),
              ("land_proportion", "i8"), ("by_player_flag", "i8"), ("padding2", "i16"),
              ("start_area_radius", "i32"), ("terrain_edge_fade", "i32"),
              ("clumpiness", "i32")]


class MapTerrain(Struct):  # 24 bytes (also used for elevations, same size)
    FIELDS = [("proportion", "i32"), ("terrain", "i32"), ("number_of_clumps", "i32"),
              ("edge_spacing", "i32"), ("placement_terrain", "i32"),
              ("clumpiness", "i32")]


class MapUnit(Struct):  # 44 bytes
    FIELDS = [("unit", "i32"), ("host_terrain", "i32"), ("group_placing", "i8"),
              ("scale_flag", "i8"), ("padding1", "i16"), ("objects_per_group", "i32"),
              ("fluctuation", "i32"), ("groups_per_player", "i32"),
              ("group_radius", "i32"), ("own_at_start", "i32"),
              ("set_place_for_all_players", "i32"), ("min_distance_to_players", "i32"),
              ("max_distance_to_players", "i32")]


class MapElevation(Struct):  # 24 bytes
    FIELDS = [("proportion", "i32"), ("terrain", "i32"), ("clump_count", "i32"),
              ("base_terrain", "i32"), ("base_elevation", "i32"),
              ("tile_spacing", "i32")]


class RandomMapHeader(Struct):
    """Summary entry (18 ints); the real data follows in RandomMapBody."""
    FIELDS = [("map_id", "i32"), ("border_south_west", "i32"),
              ("border_north_west", "i32"), ("border_north_east", "i32"),
              ("border_south_east", "i32"), ("border_usage", "i32"),
              ("water_shape", "i32"), ("non_base_terrain", "i32"),
              ("base_zone_coverage", "i32"), ("unknown9", "i32"),
              ("land_count", "u32"), ("land_ptr", "i32"),
              ("terrain_count", "u32"), ("terrain_ptr", "i32"),
              ("unit_count", "u32"), ("unit_ptr", "i32"),
              ("elevation_count", "u32"), ("elevation_ptr", "i32")]


class RandomMapBody(Struct):
    FIELDS = [("border_south_west", "i32"), ("border_north_west", "i32"),
              ("border_north_east", "i32"), ("border_south_east", "i32"),
              ("border_usage", "i32"), ("water_shape", "i32"),
              ("non_base_terrain", "i32"), ("base_zone_coverage", "i32"),
              ("unknown9", "i32"),
              ("land_count", "u32"), ("land_ptr", "i32"),
              ("lands", MapLand, "land_count"),
              ("terrain_count", "u32"), ("terrain_ptr", "i32"),
              ("terrains", MapTerrain, "terrain_count"),
              ("unit_count", "u32"), ("unit_ptr", "i32"),
              ("units", MapUnit, "unit_count"),
              ("elevation_count", "u32"), ("elevation_ptr", "i32"),
              ("elevations", MapElevation, "elevation_count")]


class RandomMaps(Struct):
    FIELDS = [("map_count", "u32"), ("maps_ptr", "i32"),
              ("headers", RandomMapHeader, "map_count"),
              ("maps", RandomMapBody, "map_count")]


# ---- effects (techages) ---------------------------------------------------

class EffectCommand(Struct):
    FIELDS = [("type", "u8"), ("a", "i16"), ("b", "i16"), ("c", "i16"), ("d", "f32")]


class Effect(Struct):
    FIELDS = [("name", "bytes", 31), ("effect_count", "u16"),
              ("commands", EffectCommand, "effect_count")]


class Effects(Struct):
    FIELDS = [("count", "u32"), ("effects", Effect, "count")]


# ---- unit headers ---------------------------------------------------------

class UnitCommand(Struct):  # 59 bytes
    FIELDS = [
        ("one", "i16"), ("id", "i16"), ("unknown1", "i8"), ("type", "i16"),
        ("class_id", "i16"), ("unit_id", "i16"), ("terrain_id", "i16"),
        ("resource_in", "i16"), ("resource_productivity_multiplier", "i16"),
        ("resource_out", "i16"), ("resource", "i16"),
        ("work_rate_multiplier", "f32"), ("execution_radius", "f32"),
        ("extra_range", "f32"), ("unknown4", "i8"), ("unknown5", "f32"),
        ("selection_enabler", "i8"), ("unknown7", "i8"), ("unknown8", "i16"),
        ("unknown9", "i16"), ("unknown10", "i8"), ("unknown11", "i8"),
        ("unknown12", "i8"), ("graphics", "i16", 6),
    ]


class UnitHeader(Struct):
    """exists byte, then (if exists) a command list.  `commands` is None if !exists."""

    @classmethod
    def read(cls, r, ctx=None):
        o = cls.__new__(cls)
        o.exists = r.scalar("u8")
        o.commands = None
        if o.exists:
            n = r.scalar("u16")
            o.commands = [UnitCommand.read(r) for _ in range(n)]
        return o

    def write(self, w):
        w.scalar("u8", self.exists)
        if self.exists:
            w.scalar("u16", len(self.commands))
            for c in self.commands:
                c.write(w)


class UnitHeaders(Struct):
    FIELDS = [("count", "u32"), ("headers", UnitHeader, "count")]


# ---- units -----------------------------------------------------------------
#
# A unit is stored as: type byte, then the fields of its class.  The classes are
# cumulative: 10 < 20 < 30 < 40 < 50 < {60 projectile, 70 creatable < 80 building}.
# (type 25 has the same layout as 20.)  Names follow genieutils where the data
# confirmed them; a few "unknownN" fields are not understood.

class ResourceStorage(Struct):
    FIELDS = [("type", "i16"), ("amount", "f32"), ("store_mode", "i8")]


class DamageGraphic(Struct):
    FIELDS = [("graphic_id", "i16"), ("damage_percent", "i8"), ("apply_mode", "i8"),
              ("unknown2", "i8")]


class AttackOrArmour(Struct):
    FIELDS = [("class", "i16"), ("amount", "i16")]


class ResourceCost(Struct):
    FIELDS = [("type", "i16"), ("amount", "i16"), ("enabled", "i16")]


class BuildingAnnex(Struct):
    FIELDS = [("unit_id", "i16"), ("misplacement_x", "f32"), ("misplacement_y", "f32")]


class Unit(Struct):
    """Common (type 10) part.  Subclasses extend FIELDS."""
    FIELDS = [
        ("type", "u8"), ("name_length", "i16"), ("id", "i16"),
        ("language_dll_name", "u16"), ("language_dll_creation", "u16"),
        ("class_", "i16"), ("standing_graphic", "i16", 2), ("dying_graphic", "i16", 2),
        ("death_mode", "i8"), ("hit_points", "i16"), ("line_of_sight", "f32"),
        ("garrison_capacity", "i8"), ("size_radius", "f32", 2),
        ("hp_bar_height1", "f32"), ("train_sound", "i16", 2), ("dead_unit_id", "i16"),
        ("placement_mode", "i8"), ("air_mode", "i8"), ("icon_id", "i16"),
        ("hide_in_editor", "i8"), ("unknown1", "i16"), ("enabled", "i16"),
        ("placement_bypass_terrain", "i16", 2), ("placement_terrain", "i16", 2),
        ("editor_radius", "f32", 2), ("hill_mode", "i8"), ("visible_in_fog", "i8"),
        ("terrain_restriction", "i16"), ("fly_mode", "i8"),
        ("resource_capacity", "i16"), ("resource_decay", "f32"),
        ("blast_armor_level", "i8"), ("trigger_type", "i8"), ("interaction_mode", "i8"),
        ("minimap_mode", "i8"), ("command_attribute", "i8"), ("unknown3a", "f32"),
        ("minimap_color", "u8"), ("language_dll_help", "i32"),
        ("language_dll_hotkey_text", "i32"), ("hotkey", "i32"),
        ("unselectable", "i8"), ("unknown6", "i8"), ("unknown7", "i8"), ("unknown8", "i8"),
        ("selection_mask", "i8"), ("selection_shape_type", "i8"), ("selection_shape", "i8"),
        ("attribute", "i8"), ("civilization", "i8"), ("nothing", "i16"),
        ("selection_effect", "i8"), ("editor_selection_colour", "i8"),
        ("selection_radius", "f32", 2), ("hp_bar_height2", "f32"),
        ("resource_storages", ResourceStorage, 3),
        ("damage_graphic_count", "u8"),
        ("damage_graphics", DamageGraphic, "damage_graphic_count"),
        ("selection_sound", "i16"), ("dying_sound", "i16"),
        ("attack_mode", "i8"), ("convert_terrain", "i8"),
        ("name", "bytes", "name_length"),
        ("id2", "i16"), ("id3", "i16"),
    ]


class UnitFlag(Unit):  # type 20 / 25
    FIELDS = Unit.FIELDS + [("speed", "f32")]


class UnitDeadFish(UnitFlag):  # type 30
    FIELDS = UnitFlag.FIELDS + [
        ("walking_graphic", "i16", 2), ("rotation_speed", "f32"), ("unknown11", "i8"),
        ("tracking_unit", "i16"), ("tracking_unit_used", "i8"),
        ("tracking_unit_density", "f32"), ("unknown16", "i8"), ("unknown16b", "f32", 5),
    ]


class UnitBird(UnitDeadFish):  # type 40
    FIELDS = UnitDeadFish.FIELDS + [
        ("sheep_conversion", "i16"), ("search_radius", "f32"), ("work_rate", "f32"),
        ("drop_site", "i16", 2), ("villager_mode", "i8"), ("attack_sound", "i16"),
        ("move_sound", "i16"), ("animal_mode", "i8"),
    ]


class UnitType50(UnitBird):  # type 50
    FIELDS = UnitBird.FIELDS + [
        ("default_armor", "i16"),
        ("attack_count", "u16"), ("attacks", AttackOrArmour, "attack_count"),
        ("armour_count", "u16"), ("armours", AttackOrArmour, "armour_count"),
        ("ter_restriction_for_dmg_multiplier", "i16"),
        ("max_range", "f32"), ("blast_width", "f32"), ("reload_time", "f32"),
        ("projectile_unit_id", "i16"), ("accuracy_percent", "i16"), ("tower_mode", "i8"),
        ("frame_delay", "i16"), ("graphic_displacement", "f32", 3),
        ("blast_attack_level", "i8"), ("min_range", "f32"),
        ("accuracy_error_radius", "f32"), ("attack_graphic", "i16"),
        ("displayed_melee_armour", "i16"), ("displayed_attack", "i16"),
        ("displayed_range", "f32"), ("displayed_reload_time", "f32"),
    ]


class UnitProjectile(UnitType50):  # type 60
    FIELDS = UnitType50.FIELDS + [
        ("stretch_mode", "i8"), ("compensation_mode", "i8"),
        ("drop_animation_mode", "i8"), ("penetration_mode", "i8"),
        ("unknown24", "i8"), ("projectile_arc", "f32"),
    ]


class UnitCreatable(UnitType50):  # type 70
    FIELDS = UnitType50.FIELDS + [
        ("resource_costs", ResourceCost, 3), ("train_time", "i16"),
        ("train_location_id", "i16"), ("button_id", "i8"), ("unknown26", "f32"),
        ("unknown27", "f32"), ("unknown28", "i8"), ("hero_mode", "i8"),
        ("garrison_graphic", "i32"), ("total_projectiles", "f32"),
        ("max_total_projectiles", "i8"), ("projectile_spawning_area", "f32", 3),
        ("secondary_projectile_unit", "i32"), ("special_graphic", "i32"),
        ("special_ability", "i8"), ("displayed_pierce_armour", "i16"),
    ]


class UnitBuilding(UnitCreatable):  # type 80
    FIELDS = UnitCreatable.FIELDS + [
        ("construction_graphic_id", "i16"), ("snow_graphic_id", "i16"),
        ("adjacent_mode", "i8"), ("graphics_angle", "i16"),
        ("disappears_when_built", "i8"), ("stack_unit_id", "i16"),
        ("foundation_terrain_id", "i16"), ("old_terrain_like_id", "i16"),
        ("research_id", "i16"), ("unknown33", "i8"),
        ("annexes", BuildingAnnex, 4), ("head_unit", "i16"), ("transform_unit", "i16"),
        ("unknown_sound", "i16"), ("construction_sound", "i16"),
        ("garrison_type", "i8"), ("garrison_heal_rate", "f32"), ("unknown35", "f32"),
        ("pile_unit", "i16"), ("looting_table", "i8", 6),
    ]


UNIT_CLASSES = {10: Unit, 20: UnitFlag, 25: UnitFlag, 30: UnitDeadFish, 40: UnitBird,
                50: UnitType50, 60: UnitProjectile, 70: UnitCreatable, 80: UnitBuilding}


def read_unit(r: Reader) -> Unit:
    """Peek at the type byte and read the matching unit class."""
    t = r.data[r.pos]
    cls = UNIT_CLASSES.get(t)
    if cls is None:
        raise ValueError("unknown unit type %d at 0x%x" % (t, r.pos))
    return cls.read(r)


# ---- civilizations -----------------------------------------------------------

class Civilization(Struct):
    """Civ header, resource table, and the sparse unit table (pointer array + units)."""
    FIELDS = [
        ("enabled", "u8"), ("name", "bytes", 20),
        ("resource_count", "u16"), ("tech_tree_id", "i16"), ("team_bonus_id", "i16"),
        ("resources", "f32", "resource_count"),
        ("icon_set", "i8"), ("unit_count", "u16"),
        ("unit_ptrs", "i32", "unit_count"),
    ]

    @classmethod
    def read(cls, r, ctx=None):
        o = super().read(r)
        # units: one record per non-zero pointer, in index order; None for holes
        o.units = [read_unit(r) if p else None for p in o.unit_ptrs]
        return o

    def write(self, w):
        super().write(w)
        for u in self.units:
            if u is not None:
                u.write(w)


# ---- researches (techs) ---------------------------------------------------------

class ResearchResourceCost(Struct):
    FIELDS = [("type", "i16"), ("amount", "i16"), ("flag", "i8")]


class Research(Struct):
    FIELDS = [
        ("required_techs", "i16", 6), ("resource_costs", ResearchResourceCost, 3),
        ("required_tech_count", "i16"), ("civ", "i16"), ("full_tech_mode", "i16"),
        ("research_location", "i16"), ("language_dll_name", "u16"),
        ("language_dll_description", "u16"), ("research_time", "i16"),
        ("effect_id", "i16"), ("type", "i16"), ("icon_id", "i16"), ("button_id", "i8"),
        ("language_dll_help", "i32"), ("language_dll_tech_tree", "i32"),
        ("hotkey", "i32"), ("name_length", "u16"), ("name", "bytes", "name_length"),
    ]


class Researches(Struct):
    FIELDS = [("count", "u16"), ("researches", Research, "count")]


# ---- tech tree --------------------------------------------------------------------

class TechTreeAge(Struct):
    FIELDS = [
        ("id", "i32"), ("status", "i8"),
        ("building_count", "u8"), ("buildings", "i32", "building_count"),
        ("unit_count", "u8"), ("units", "i32", "unit_count"),
        ("research_count", "u8"), ("researches", "i32", "research_count"),
        ("connected_slots_used", "i32"), ("other_connected_ids", "i32", 10),
        ("other_connections", "i32", 10),
        ("building_level_count", "i8"), ("buildings_per_zone", "i8", 10),
        ("group_length_per_zone", "i8", 10), ("max_age_length", "i8"),
        ("line_mode", "i32"),
    ]


class TechTreeBuilding(Struct):
    FIELDS = [
        ("id", "i32"), ("status", "i8"),
        ("building_count", "u8"), ("buildings", "i32", "building_count"),
        ("unit_count", "u8"), ("units", "i32", "unit_count"),
        ("research_count", "u8"), ("researches", "i32", "research_count"),
        ("connected_slots_used", "i32"), ("other_connected_ids", "i32", 10),
        ("other_connections", "i32", 10),
        ("location_in_age", "i8"), ("unit_techs_total", "i8", 5),
        ("unit_techs_first", "i8", 5), ("line_mode", "i32"),
        ("enabling_research", "i32"),
    ]


class TechTreeUnit(Struct):
    FIELDS = [
        ("id", "i32"), ("status", "i8"), ("upper_building", "i32"),
        ("connected_slots_used", "i32"), ("other_connected_ids", "i32", 10),
        ("other_connections", "i32", 10), ("vertical_line", "i32"),
        ("unit_count", "u8"), ("units", "i32", "unit_count"),
        ("location_in_age", "i32"), ("required_research", "i32"),
        ("line_mode", "i32"), ("enabling_research", "i32"),
    ]


class TechTreeResearch(Struct):
    FIELDS = [
        ("id", "i32"), ("status", "i8"), ("upper_building", "i32"),
        ("building_count", "u8"), ("buildings", "i32", "building_count"),
        ("unit_count", "u8"), ("units", "i32", "unit_count"),
        ("research_count", "u8"), ("researches", "i32", "research_count"),
        ("connected_slots_used", "i32"), ("other_connected_ids", "i32", 10),
        ("other_connections", "i32", 10), ("vertical_line", "i32"),
        ("location_in_age", "i32"), ("line_mode", "i32"),
    ]


class TechTree(Struct):
    """Seven game-rule ints (time slice, kill rates...) followed by the tech tree proper."""
    FIELDS = [
        ("time_slice", "i32"), ("unit_kill_rate", "i32"), ("unit_kill_total", "i32"),
        ("unit_hitpoint_rate", "i32"), ("unit_hitpoint_total", "i32"),
        ("razing_kill_rate", "i32"), ("razing_kill_total", "i32"),
        ("age_count", "u8"), ("building_count", "u8"), ("unit_count", "u8"),
        ("research_count", "u8"), ("total_unit_tech_groups", "i32"),
        ("ages", TechTreeAge, "age_count"),
        ("buildings", TechTreeBuilding, "building_count"),
        ("units", TechTreeUnit, "unit_count"),
        ("researches", TechTreeResearch, "research_count"),
    ]


# --------------------------------------------------------------------------
# Whole file
# --------------------------------------------------------------------------

class TerrainRestrictions:
    """Counts, two pointer tables, then one TerrainRestriction per entry."""

    @classmethod
    def read(cls, r):
        o = cls()
        o.restriction_count = r.scalar("u16")
        o.terrain_count = r.scalar("u16")
        n = o.restriction_count
        o.ptrs1 = list(r.unpack(struct.Struct("<%di" % n)))
        o.ptrs2 = list(r.unpack(struct.Struct("<%di" % n)))
        o.restrictions = [TerrainRestriction.read_n(r, o.terrain_count) for _ in range(n)]
        return o

    def write(self, w):
        w.scalar("u16", len(self.restrictions))
        w.scalar("u16", self.terrain_count)
        w.pack(struct.Struct("<%di" % len(self.ptrs1)), *self.ptrs1)
        w.pack(struct.Struct("<%di" % len(self.ptrs2)), *self.ptrs2)
        for t in self.restrictions:
            t.write(w)


class CountedList:
    """u16 count + that many records of `cls` (player colours, sounds)."""

    def __init__(self, items=None):
        self.items = items or []

    @classmethod
    def read(cls, r, item_cls):
        n = r.scalar("u16")
        return cls([item_cls.read(r) for _ in range(n)])

    def write(self, w):
        w.scalar("u16", len(self.items))
        for i in self.items:
            i.write(w)


class Graphics:
    """u16 count, i32 pointer table (0 = empty slot), then Graphic records for non-null slots."""

    @classmethod
    def read(cls, r):
        o = cls()
        n = r.scalar("u16")
        o.ptrs = list(r.unpack(struct.Struct("<%di" % n)))
        o.graphics = [Graphic.read(r) if p else None for p in o.ptrs]
        return o

    def write(self, w):
        w.scalar("u16", len(self.ptrs))
        w.pack(struct.Struct("<%di" % len(self.ptrs)), *self.ptrs)
        for g in self.graphics:
            if g is not None:
                g.write(w)


class Civilizations:
    @classmethod
    def read(cls, r):
        o = cls()
        n = r.scalar("u16")
        o.civs = [Civilization.read(r) for _ in range(n)]
        return o

    def write(self, w):
        w.scalar("u16", len(self.civs))
        for c in self.civs:
            c.write(w)


def _read_terrain_block_section(r, dat):
    return read_terrain_block(r, dat.terrain_restrictions.terrain_count)


_read_terrain_block_section.needs_dat = True


class DatFile:
    """
    Parsed dat.  Attributes (in file order):
        version (bytes[8]), terrain_restrictions, player_colours, sounds, graphics,
        terrain_block, random_maps, effects, unit_headers, civilizations,
        researches, tech_tree
    `offsets[name] = (start, end)` are byte offsets into the *decompressed* data.
    """

    # (attribute name, reader(r) -> object)
    SECTIONS = [
        ("terrain_restrictions", TerrainRestrictions.read),
        ("player_colours", lambda r: CountedList.read(r, PlayerColour)),
        ("sounds", lambda r: CountedList.read(r, Sound)),
        ("graphics", Graphics.read),
        ("terrain_block", _read_terrain_block_section),
        ("random_maps", RandomMaps.read),
        ("effects", Effects.read),
        ("unit_headers", UnitHeaders.read),
        ("civilizations", Civilizations.read),
        ("researches", Researches.read),
        ("tech_tree", TechTree.read),
    ]

    @classmethod
    def parse(cls, data: bytes) -> "DatFile":
        r = Reader(data)
        o = cls()
        o.offsets = {}
        o.version = r.read(8)
        o.offsets["version"] = (0, 8)
        if not o.version.startswith(b"VER "):
            raise ValueError("not a dat file (version %r)" % o.version)
        for name, fn in cls.SECTIONS:
            start = r.pos
            setattr(o, name, fn(r, o) if getattr(fn, "needs_dat", False) else fn(r))
            o.offsets[name] = (start, r.pos)
        if r.pos != len(data):
            raise ValueError("parse ended at 0x%x, file is 0x%x bytes (%d trailing)"
                             % (r.pos, len(data), len(data) - r.pos))
        return o

    def serialize(self, sections_out=None) -> bytes:
        """Write back to bytes.  If `sections_out` is a dict it receives the new offsets."""
        w = Writer()
        w.write(self.version)
        if sections_out is not None:
            sections_out["version"] = (0, 8)
        for name, _ in self.SECTIONS:
            start = w.tell()
            getattr(self, name).write(w)
            if sections_out is not None:
                sections_out[name] = (start, w.tell())
        return bytes(w.buf)

    @classmethod
    def from_file(cls, path: str) -> "DatFile":
        with open(path, "rb") as f:
            return cls.parse(decompress(f.read()))


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _read_dat(path):
    with open(path, "rb") as f:  # read-only; never write into the game folder
        raw = f.read()
    return raw, decompress(raw)


def cmd_info(path):
    raw, data = _read_dat(path)
    dat = DatFile.parse(data)
    print("file            : %s" % path)
    print("compressed size : %d bytes, decompressed: %d bytes" % (len(raw), len(data)))
    print("version         : %s" % cstr(dat.version))
    tr = dat.terrain_restrictions
    print("terrain restrictions: %d (x %d terrains)" % (len(tr.restrictions), tr.terrain_count))
    print("player colours  : %d" % len(dat.player_colours.items))
    print("sounds          : %d (%d sound items)" % (
        len(dat.sounds.items), sum(len(s.items) for s in dat.sounds.items)))
    g = dat.graphics
    print("graphics        : %d slots, %d used" % (len(g.ptrs), sum(1 for x in g.graphics if x)))
    tb = dat.terrain_block
    print("terrains        : %d slots, %d enabled; %d terrain borders" % (
        len(tb.terrains), sum(1 for t in tb.terrains if t.enabled), len(tb.terrain_borders)))
    rm = dat.random_maps
    print("random maps     : %d" % rm.map_count)
    ef = dat.effects
    print("effect bundles  : %d (%d effect commands)" % (
        ef.count, sum(len(e.commands) for e in ef.effects)))
    print("unit headers    : %d (%d existing)" % (
        dat.unit_headers.count, sum(1 for h in dat.unit_headers.headers if h.exists)))
    cv = dat.civilizations.civs
    print("civilizations   : %d" % len(cv))
    for i, c in enumerate(cv):
        nunits = sum(1 for u in c.units if u is not None)
        print("  civ %2d  %-12s units: %3d (of %d slots)  resources: %d  tech tree id: %d  team bonus: %d"
              % (i, cstr(c.name), nunits, c.unit_count, c.resource_count,
                 c.tech_tree_id, c.team_bonus_id))
    print("researches      : %d" % dat.researches.count)
    tt = dat.tech_tree
    print("tech tree       : %d ages, %d buildings, %d units, %d researches, %d unit-tech groups"
          % (tt.age_count, tt.building_count, tt.unit_count, tt.research_count,
             tt.total_unit_tech_groups))
    print("section offsets (decompressed data, [start, end) ):")
    for name, (a, b) in dat.offsets.items():
        print("  %-22s 0x%08x - 0x%08x  (%d bytes)" % (name, a, b, b - a))
    print("  %-22s end of file = 0x%08x" % ("EOF", len(data)))


def cmd_verify(path):
    raw, data = _read_dat(path)
    ok = True

    # 1. compression round-trip
    again = decompress(compress(data))
    print("[1] compress/decompress round-trip identical: %s" % (again == data))
    ok &= again == data
    print("    (original deflate stream %d bytes, recompressed %d bytes; the stream bytes need not match)"
          % (len(raw), len(compress(data))))

    # 2. parse to EOF
    dat = DatFile.parse(data)
    print("[2] parsed exactly to EOF (0x%x): True" % len(data))

    # 3. serialize -> identical bytes, section by section
    new_off = {}
    out = dat.serialize(new_off)
    same = out == data
    print("[3] serialize() byte-identical to original: %s" % same)
    ok &= same
    if not same:
        for name, (a, b) in dat.offsets.items():
            if new_off[name] != (a, b) or out[new_off[name][0]:new_off[name][1]] != data[a:b]:
                print("    first differing section: %s" % name)
                break
    # 4. full file round trip
    full_ok = decompress(compress(out)) == data
    print("[4] compress(serialize()) decompresses to original: %s" % full_ok)
    ok &= full_ok
    return 0 if ok else 1


def cmd_civs(path):
    dat = DatFile.from_file(path)
    print("%3s  %-14s %12s %6s" % ("id", "name", "tech_tree_id", "units"))
    for i, c in enumerate(dat.civilizations.civs):
        print("%3d  %-14s %12d %6d" % (i, cstr(c.name), c.tech_tree_id,
                                       sum(1 for u in c.units if u is not None)))


def main(argv):
    cmds = {"info": cmd_info, "verify": cmd_verify, "civs": cmd_civs}
    if len(argv) == 3 and argv[1] in cmds:
        return cmds[argv[1]](argv[2]) or 0
    sys.stderr.write("usage: python3 -I datfile.py info|verify|civs <empires2_x?_p1.dat>\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
