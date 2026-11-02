"""Find words in global-data snapshots that track the current scene.

Usage: py -3 -I tools/snapdiff.py field=a,b menu=c,d dex=e [...]

Each group is a scene label and the snapshot names taken in it (files
analysis/private/snaps/<name>.bin, base 0x669000). Prints every aligned u32
that is identical within each group and differs between at least two groups,
with a hint for values that look like small IDs or rodata (vtable) pointers.
"""
import struct
import sys
from pathlib import Path

BASE = 0x669000
SNAPS = Path(__file__).resolve().parent.parent / "analysis" / "private" / "snaps"
RODATA = (0x5BC000, 0x669000)
GLOBALS = (0x669000, 0x6D6000)


def words(name):
    data = (SNAPS / f"{name}.bin").read_bytes()
    return struct.unpack_from(f"<{len(data) // 4}I", data)


def hint(v):
    if v < 0x100:
        return "small"
    if RODATA[0] <= v < RODATA[1]:
        return "rodata ptr"
    if GLOBALS[0] <= v < GLOBALS[1]:
        return "global ptr"
    if 0x08000000 <= v < 0x10000000 or 0x30000000 <= v < 0x40000000:
        return "heap ptr"
    return ""


def main(specs):
    groups = []
    for spec in specs:
        label, names = spec.split("=", 1)
        groups.append((label, [words(n) for n in names.split(",")]))

    n = min(len(w) for _, snaps in groups for w in snaps)
    hits = []
    for i in range(n):
        values = []
        for _, snaps in groups:
            first = snaps[0][i]
            if any(s[i] != first for s in snaps[1:]):
                break
            values.append(first)
        else:
            if len(set(values)) > 1:
                hits.append((BASE + i * 4, values))

    header = "address   " + " ".join(f"{label:>12}" for label, _ in groups)
    print(f"{len(hits)} candidate(s)")
    print(header)
    for addr, values in hits:
        hints = {hint(v) for v in values} - {""}
        print(f"{addr:08X}  " + " ".join(f"{v:>12X}" for v in values) + ("  " + ",".join(sorted(hints)) if hints else ""))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:])
