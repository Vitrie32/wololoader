#!/usr/bin/env python3
"""Compare selection-trace runs (make TRACE=1) with the static analysis, per function.

usage: seltrace_compare.py <exe> <selection dir> [trace.txt ...]
  exe            AoK HD.exe (read only)
  selection dir  re/selection: static verdicts from selpart_*.md table rows
                 "| `0047a930` | **FALSE** | ..." (TRUE / FALSE)
  traces         seltrace files ("eip hits" lines); default: <dir>/seltrace_run*.txt

Each traced EIP is mapped to the function containing it: the nearest function start
at or below it, where starts are the targets of direct `call` instructions in the exe
plus every function named in the static analysis (some are only reached through vtables). Prints one row per function:
start, traced EIPs, hits, static verdict (TRUE / FALSE / absent).
"""
import bisect
import collections
import glob
import os
import re
import subprocess
import sys


def call_targets(exe):
    """Start addresses of functions: every direct call target in .text."""
    out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", exe],
                         capture_output=True, text=True).stdout
    targets = {int(m, 16) for m in re.findall(r"\scall\s+0x([0-9a-f]+)\b", out)}
    return sorted(targets)


def static_verdicts(sel_dir):
    v = {}
    for f in sorted(glob.glob(os.path.join(sel_dir, "selpart_*.md"))):
        text = open(f).read()
        # table rows "| `0047a930` | **FALSE** |" and headings "## 1. `006bb100` — **TRUE**"
        for m in re.finditer(r"`(?:0x)?([0-9a-fA-F]{6,8})`[^\n`]{0,12}?\**(TRUE|FALSE)\**", text):
            v[int(m.group(1), 16)] = m.group(2)
        # summary lines "- **TRUE** (8 functions): `006bb100`, `006bc6d0`, ..."
        for m in re.finditer(r"\*\*(TRUE|FALSE)\*\*[^\n]*?:([^\n]*)", text):
            for a in re.findall(r"`(?:0x)?([0-9a-fA-F]{6,8})`", m.group(2)):
                v.setdefault(int(a, 16), m.group(1))
    return v


def traces(paths):
    hits = collections.Counter()
    for p in paths:
        for line in open(p):
            p2 = line.split()
            if len(p2) >= 2 and not line.startswith("#"):
                try:
                    hits[int(p2[0], 16)] += int(p2[1])
                except ValueError:
                    pass
    return hits


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    exe, sel_dir = sys.argv[1], sys.argv[2]
    paths = sys.argv[3:] or sorted(glob.glob(os.path.join(sel_dir, "seltrace_run*.txt")))
    verdict = static_verdicts(sel_dir)
    # Functions named in the static analysis count as starts too (some are only
    # reached through vtables, so they are not direct call targets).
    starts = sorted(set(call_targets(exe)) | set(verdict))
    funcs = collections.defaultdict(lambda: [[], 0])
    for eip, n in traces(paths).items():
        i = bisect.bisect_right(starts, eip) - 1
        f = starts[i] if i >= 0 else 0
        funcs[f][0].append(eip)
        funcs[f][1] += n
    print("traces: %s" % ", ".join(os.path.basename(p) for p in paths))
    print("%-10s %-6s %10s  %-7s  eips" % ("function", "count", "hits", "static"))
    absent = []
    for f in sorted(funcs):
        eips, n = funcs[f]
        st = verdict.get(f, "absent")
        if st != "TRUE":
            absent.append((f, st))
        print("0x%08x %-6d %10d  %-7s  %s" % (f, len(eips), n, st, " ".join("%x" % e for e in sorted(eips))))
    print("\n%d functions hit; %d TRUE in the static list; %d absent or FALSE:" %
          (len(funcs), len(funcs) - len(absent), len(absent)))
    for f, st in absent:
        print("  0x%08x  %s" % (f, st))


if __name__ == "__main__":
    main()
