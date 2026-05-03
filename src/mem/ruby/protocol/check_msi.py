#!/usr/bin/env python3
"""
Sanity-check that a gem5 stats.txt looks like MSI (not MESI).

MUST_FIRE: counters proving the MSI upgrade path is exercised.
MUST_BE_ABSENT: counters that would only exist if the E state leaked through.

Usage: python3 check_msi.py [path/to/stats.txt]
"""
import re
import sys
from pathlib import Path

MUST_FIRE = [
    # L1 issued GETM on a write miss (I -> IM)
    r"L1Cache_Controller\.(?:[\w.]+\.)?IM\.",
    # L1 upgrade S -> M (only path to write-after-read in MSI)
    r"L1Cache_Controller\.(?:[\w.]+\.)?SM\.",
    # S, Store transition must fire (proves upgrade path is taken)
    r"L1Cache_Controller\.(?:[\w.]+\.)?S\.Store",
    # L2 blocked-on-GETM cycles must happen
    r"L2Cache_Controller\.(?:[\w.]+\.)?(SS_MB|MT_MB)\.",
    # GETM activity (raw GETM event or Fwd_GETM forwarding) must occur
    r"GETM\b",
]

# Anything matching these names should not appear with a nonzero value.
MUST_BE_ABSENT = [
    r"\bData_Exclusive\b",
    r"\bLLSC_E\b",
    r"\bE_IL0\b",
    r"\bEE\b",
    r"\.UPGRADE\b",
    r"\.PUTX\b",
    r"\.GETX\b",
    # Bare "E" state name as a per-state counter (avoid matching things like "MEM_Inv")
    r"L[012]Cache_Controller\.(?:[\w.]+\.)?E\.",
]

STAT_LINE = re.compile(r"^\s*(\S+)\s+([0-9eE.+-]+)")


def parse_stats(path):
    out = []
    for line in path.read_text().splitlines():
        if not line or line.startswith("#") or line.startswith("---"):
            continue
        m = STAT_LINE.match(line)
        if not m:
            continue
        try:
            out.append((m.group(1), float(m.group(2))))
        except ValueError:
            pass
    return out


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "m5out/stats.txt")
    if not path.exists():
        print(f"stats file not found: {path}")
        return 2

    stats = parse_stats(path)
    print(f"loaded {len(stats)} stats from {path}\n")

    fail = False

    print("=== MUST_FIRE (nonzero confirms MSI path active) ===")
    for pat in MUST_FIRE:
        rx = re.compile(pat)
        hits = [(n, v) for n, v in stats if rx.search(n) and v > 0]
        if hits:
            total = sum(v for _, v in hits)
            print(f"  OK   {pat:55s}  {len(hits):4d} stats, sum={total:.0f}")
        else:
            print(f"  FAIL {pat:55s}  no nonzero match")
            fail = True

    print("\n=== MUST_BE_ABSENT (nonzero means E-state behavior leaked) ===")
    for pat in MUST_BE_ABSENT:
        rx = re.compile(pat)
        hits = [(n, v) for n, v in stats if rx.search(n) and v > 0]
        if hits:
            print(f"  FAIL {pat:55s}  {len(hits)} nonzero matches:")
            for n, v in hits[:5]:
                print(f"         {n} = {v:.0f}")
            fail = True
        else:
            print(f"  OK   {pat:55s}  absent or zero")

    print()
    print("RESULT:", "FAIL" if fail else "PASS (looks like MSI)")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
