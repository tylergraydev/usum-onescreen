"""Find global -> object -> vtable slots that track the current scene.

Usage: py -3 -I tools/vtdiff.py field=a,b menu=c dex=d [...]

Reads analysis/private/snaps/vt-<name>.bin (u32 triples: global, offset,
vtable, from the plugin's `vtscan`). Prints every (global, offset) whose
vtable is the same within each group and differs between at least two groups.
A slot missing from a scan counts as "-".
"""
import struct
import sys
from pathlib import Path

SNAPS = Path(__file__).resolve().parent.parent / "analysis" / "private" / "snaps"


def load(name):
    data = (SNAPS / f"vt-{name}.bin").read_bytes()
    out = {}
    for g, off, vt in struct.iter_unpack("<III", data):
        out[(g, off)] = vt
    return out


def main(specs):
    groups = []
    for spec in specs:
        label, names = spec.split("=", 1)
        groups.append((label, [load(n) for n in names.split(",")]))

    keys = set()
    for _, scans in groups:
        for s in scans:
            keys |= s.keys()

    hits = []
    for key in sorted(keys):
        values = []
        for _, scans in groups:
            vals = {s.get(key) for s in scans}
            if len(vals) != 1:
                break
            values.append(vals.pop())
        else:
            if len(set(values)) > 1:
                hits.append((key, values))

    print(f"{len(hits)} candidate(s)")
    print("global    off  " + " ".join(f"{label:>9}" for label, _ in groups))
    for (g, off), values in hits:
        print(f"{g:08X} {off:4X}  " + " ".join(f"{v:9X}" if v is not None else f"{'-':>9}" for v in values))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:])
