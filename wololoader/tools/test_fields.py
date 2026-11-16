#!/usr/bin/env python3
"""
test_fields.py - Exhaustive differential test script for unit field overrides.

Compares Python builder (tools/build.py) and C builder (src/datbuild.c)
across every unit class in datfile.py and every scalar / fixed-length-array field.
"""

import argparse
import copy
import json
import os
import tempfile
import shutil
import subprocess
import sys
import time

# Ensure script dir is in sys.path even when run with python3 -I
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import datfile as df  # noqa: E402

DEFAULT_GAME_DIR = os.environ.get("WL_GAME", os.path.expanduser("~/.steam/steam/steamapps/common/Age2HD"))
DEFAULT_SCRATCH = os.path.join(tempfile.gettempdir(), "wl_fields")
DEFAULT_DAT_REL = os.path.join("resources", "_common", "dat", "empires2_x2_p1.dat")

UNIT_CLASSES = [
    df.Unit,
    df.UnitFlag,
    df.UnitDeadFish,
    df.UnitBird,
    df.UnitType50,
    df.UnitProjectile,
    df.UnitCreatable,
    df.UnitBuilding,
]


def pick_unit_id(civ1, cls):
    """Pick a representative real unit ID of the given class from civ 1."""
    # Try exact match first
    for uid, u in enumerate(civ1.units):
        if u is not None and type(u) is cls:
            return uid
    # If no exact instance exists in civ 1 (UnitBird, UnitType50), pick isinstance
    # preferring non-building creatable (e.g. Monk 125)
    for uid, u in enumerate(civ1.units):
        if u is not None and isinstance(u, cls) and not isinstance(u, df.UnitBuilding):
            return uid
    # Fallback to any isinstance
    for uid, u in enumerate(civ1.units):
        if u is not None and isinstance(u, cls):
            return uid
    raise ValueError(f"No unit in civ 1 for class {cls.__name__}")


def get_override_fields(cls):
    """
    Return list of (field_name, field_type, count) for all scalar or
    fixed-length-array fields that build.py accepts as unit overrides.
    """
    fields = []
    for f in cls.FIELDS:
        name = f[0]
        ftype = f[1]
        count = f[2] if len(f) > 2 else 1
        # Skip raw / structured fields (build.py rejects them)
        if ftype == "bytes":
            continue
        if isinstance(ftype, type) and issubclass(ftype, df.Struct):
            continue
        # Ensure count is an int (not a string pointing to another field)
        if not isinstance(count, int):
            continue
        fields.append((name, ftype, count))
    return fields


def make_scalar_val(cur_val, ftype, offset=0):
    """Generate a valid, distinctive value different from cur_val."""
    if ftype == "f32":
        c = float(cur_val) if isinstance(cur_val, (int, float)) else 0.0
        val = 7.5 + offset * 2.0
        if abs(c - val) < 0.001:
            val += 8.0
        return val
    elif ftype == "u8":
        c = int(cur_val) if isinstance(cur_val, (int, float)) else 0
        return (c + 42 + offset) % 250 + 1
    elif ftype == "i8":
        c = int(cur_val) if isinstance(cur_val, (int, float)) else 0
        return (c + 42 + offset) % 100 + 1
    elif ftype == "u16":
        c = int(cur_val) if isinstance(cur_val, (int, float)) else 0
        return (c + 123 + offset) % 60000 + 10
    elif ftype == "i16":
        c = int(cur_val) if isinstance(cur_val, (int, float)) else 0
        return (c + 123 + offset) % 30000 + 10
    elif ftype in ("u32", "i32"):
        c = int(cur_val) if isinstance(cur_val, (int, float)) else 0
        return (c + 12345 + offset) % 1000000 + 10
    else:
        raise ValueError(f"Unsupported field type: {ftype}")


def make_override_value(cur_val, ftype, count):
    """Generate a distinctive override value (scalar or array)."""
    if count > 1:
        out = []
        for k in range(count):
            elem_cur = cur_val[k] if (isinstance(cur_val, list) and k < len(cur_val)) else 0
            out.append(make_scalar_val(elem_cur, ftype, offset=k))
        return out
    else:
        return make_scalar_val(cur_val, ftype, offset=0)


def ensure_c_binary(bin_path, project_root):
    """Compile test_datbuild if binary does not exist."""
    if os.path.isfile(bin_path) and os.access(bin_path, os.X_OK):
        return
    os.makedirs(os.path.dirname(bin_path), exist_ok=True)
    cmd = [
        "gcc", "-O2", "-Wall", "-Wextra", "-std=c11",
        "-o", bin_path,
        os.path.join(project_root, "wololoader", "tools", "test_datbuild.c"),
        os.path.join(project_root, "wololoader", "src", "datbuild.c"),
        os.path.join(project_root, "wololoader", "src", "json.c"),
        "-lz"
    ]
    print(f"Compiling test binary: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def setup_fake_game_dir(scratch_dir, game_dir):
    """Set up fake game directory structure with symlink to real resources."""
    os.makedirs(scratch_dir, exist_ok=True)
    res_link = os.path.join(scratch_dir, "resources")
    target_res = os.path.join(game_dir, "resources")
    if not os.path.exists(res_link):
        os.symlink(target_res, res_link)
    mods_dir = os.path.join(scratch_dir, "wololoader", "mods")
    cache_dir = os.path.join(scratch_dir, "wololoader", "cache")
    os.makedirs(mods_dir, exist_ok=True)
    os.makedirs(cache_dir, exist_ok=True)
    return mods_dir, cache_dir


def run_batch(test_cases, scratch_dir, bin_path, base_dat_path, verbose=False):
    """
    Run build.py and C binary on a batch of test cases.
    Each test case is a dict:
        {"class_name": ..., "field_name": ..., "unit_id": ..., "value": ..., "orig_val": ...}
    Returns a list of failed test cases with details:
        [(test_case, reason)]
    """
    mods_dir = os.path.join(scratch_dir, "wololoader", "mods")
    cache_dir = os.path.join(scratch_dir, "wololoader", "cache")

    # Clean out previous mods and cache
    if os.path.isdir(mods_dir):
        shutil.rmtree(mods_dir)
    if os.path.isdir(cache_dir):
        shutil.rmtree(cache_dir)
    os.makedirs(mods_dir, exist_ok=True)
    os.makedirs(cache_dir, exist_ok=True)

    # Create mod folder
    mod_name = "test_mod"
    mod_dir = os.path.join(mods_dir, mod_name)
    civs_dir = os.path.join(mod_dir, "civs")
    os.makedirs(civs_dir, exist_ok=True)

    with open(os.path.join(mod_dir, "mod.json"), "w", encoding="utf-8") as f:
        json.dump({"name": mod_name, "enabled": True}, f)

    # Create 1 civ json per test case
    for idx, tc in enumerate(test_cases):
        spec = {
            "name": f"Civ_{idx:03d}",
            "dat_name": f"C_{idx:03d}",
            "base": 1,
            "units": {
                str(tc["unit_id"]): {
                    tc["field_name"]: tc["value"]
                }
            }
        }
        json_path = os.path.join(civs_dir, f"civ_{idx:03d}.json")
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(spec, f)

    # 1. Run Python builder
    py_script = os.path.join(SCRIPT_DIR, "build.py")
    res_py = subprocess.run(
        ["python3", "-I", py_script, scratch_dir],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    if res_py.returncode != 0:
        raise RuntimeError(f"build.py failed on batch:\n{res_py.stderr}")

    py_out_dat = os.path.join(cache_dir, "empires2_x2_p1.dat")
    with open(py_out_dat, "rb") as f:
        py_decomp = df.decompress(f.read())

    # 2. Run C binary
    c_out_dat = os.path.join(cache_dir, "c_out.dat")
    c_out_civs = os.path.join(cache_dir, "c_civs.txt")
    res_c = subprocess.run(
        [bin_path, base_dat_path, mods_dir, c_out_dat, c_out_civs],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    if res_c.returncode != 0:
        raise RuntimeError(f"C binary failed on batch:\n{res_c.stderr}")

    with open(c_out_dat, "rb") as f:
        c_decomp = df.decompress(f.read())

    # 3. Fast byte-level comparison
    if py_decomp == c_decomp:
        return []

    # Batch failed: parse both dats to isolate exact failing (class, field) pairs
    if verbose:
        print("  Batch raw output mismatch! Parsing dats to locate failing pairs...")
    dat_py = df.DatFile.parse(py_decomp)
    dat_c = df.DatFile.parse(c_decomp)

    failures = []
    # Custom civs start at index 35 (32 base + 3 reserved placeholders)
    first_custom_cid = 35
    for idx, tc in enumerate(test_cases):
        cid = first_custom_cid + idx
        py_unit = dat_py.civilizations.civs[cid].units[tc["unit_id"]]
        c_unit = dat_c.civilizations.civs[cid].units[tc["unit_id"]]

        py_field_val = getattr(py_unit, tc["field_name"])
        c_field_val = getattr(c_unit, tc["field_name"])

        if py_field_val != c_field_val:
            reason = f"field value mismatch: expected {py_field_val!r}, got {c_field_val!r}"
            failures.append((tc, reason))
        else:
            # Check if C corrupted other fields in the unit
            diffs = []
            for k in py_unit.__dict__:
                pv = getattr(py_unit, k)
                cv = getattr(c_unit, k)
                if pv != cv:
                    diffs.append(k)
            if diffs:
                reason = f"unintended corruption in other fields: {diffs}"
                failures.append((tc, reason))

    return failures


def main():
    parser = argparse.ArgumentParser(description="Differential unit field test between build.py and datbuild.c")
    parser.add_argument("--game-dir", default=DEFAULT_GAME_DIR, help="Age of Empires II HD game directory")
    parser.add_argument("--scratch", default=DEFAULT_SCRATCH, help="Scratch directory for fake game dir and outputs")
    parser.add_argument("--bin", default=None, help="Path to test_datbuild C binary")
    parser.add_argument("--batch-size", type=int, default=60, help="Number of field overrides per batch run")
    parser.add_argument("--class", dest="class_filter", default=None, help="Filter to single unit class name")
    parser.add_argument("--field", dest="field_filter", default=None, help="Filter to single field name")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    project_root = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
    bin_path = args.bin or os.path.join(args.scratch, "test_datbuild_bin")
    ensure_c_binary(bin_path, project_root)

    setup_fake_game_dir(args.scratch, args.game_dir)
    base_dat_path = os.path.join(args.game_dir, DEFAULT_DAT_REL)

    print(f"Reading base dat from {base_dat_path}...")
    with open(base_dat_path, "rb") as f:
        decomp = df.decompress(f.read())
    base_dat = df.DatFile.parse(decomp)
    civ1 = base_dat.civilizations.civs[1]

    # Collect all test cases
    test_cases = []
    for cls in UNIT_CLASSES:
        if args.class_filter and cls.__name__ != args.class_filter:
            continue
        uid = pick_unit_id(civ1, cls)
        unit_obj = civ1.units[uid]
        fields = get_override_fields(cls)
        for fname, ftype, count in fields:
            if args.field_filter and fname != args.field_filter:
                continue
            cur_val = getattr(unit_obj, fname)
            new_val = make_override_value(cur_val, ftype, count)
            test_cases.append({
                "class_name": cls.__name__,
                "field_name": fname,
                "unit_id": uid,
                "ftype": ftype,
                "count": count,
                "orig_val": cur_val,
                "value": new_val,
            })

    total_cases = len(test_cases)
    print(f"Collected {total_cases} (class, field) test cases across {len(UNIT_CLASSES)} unit classes.")

    if total_cases == 0:
        print("No test cases matched filters.")
        return 0

    batch_size = max(1, min(args.batch_size, 200))
    all_failures = []
    passed_count = 0
    t_start = time.time()

    num_batches = (total_cases + batch_size - 1) // batch_size
    for b_idx in range(num_batches):
        batch = test_cases[b_idx * batch_size : (b_idx + 1) * batch_size]
        b_name = f"Batch {b_idx + 1}/{num_batches} ({len(batch)} fields)"
        print(f"Running {b_name}...", end="", flush=True)
        t0 = time.time()
        failures = run_batch(batch, args.scratch, bin_path, base_dat_path, verbose=args.verbose)
        t_batch = time.time() - t0

        if not failures:
            passed_count += len(batch)
            print(f" PASS ({t_batch:.1f}s)")
        else:
            n_pass = len(batch) - len(failures)
            passed_count += n_pass
            all_failures.extend(failures)
            print(f" FAIL ({len(failures)} failed, {n_pass} passed, {t_batch:.1f}s)")
            for tc, reason in failures:
                print(f"   [FAIL] ({tc['class_name']}, {tc['field_name']}): {reason}")

    total_time = time.time() - t_start
    print("\n" + "=" * 60)
    print(f"TEST SUMMARY: {total_cases} tested, {passed_count} passed, {len(all_failures)} failed ({total_time:.1f}s)")
    if all_failures:
        print("\nFailing (class, field) pairs:")
        for tc, reason in all_failures:
            print(f"  - ({tc['class_name']}, {tc['field_name']}): {reason}")
        print("=" * 60)
        return 1
    else:
        print("All (class, field) override pairs passed byte-for-byte!")
        print("=" * 60)
        return 0


if __name__ == "__main__":
    sys.exit(main())
