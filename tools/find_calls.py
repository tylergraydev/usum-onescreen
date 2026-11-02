"""Find ARM-mode BL/BLX(imm) and B instructions that target a given address.

Usage: py -3 -I tools/find_calls.py <code.bin> <base_hex> <text_end_hex> <target_hex> [...]
"""
import struct
import sys


def main(path, base, text_end, targets):
    data = open(path, "rb").read()
    n = (text_end - base) // 4
    words = struct.unpack_from(f"<{n}I", data)
    want = set(targets)
    hits = {t: [] for t in targets}
    for i, w in enumerate(words):
        cond = w >> 28
        op = (w >> 24) & 0xF
        if op in (0xA, 0xB) and cond != 0xF:          # B / BL
            off = w & 0xFFFFFF
            if off & 0x800000:
                off -= 0x1000000
            tgt = base + i * 4 + 8 + off * 4
            kind = "bl" if op == 0xB else "b"
        elif cond == 0xF and (w >> 25) & 0x7 == 0x5:  # BLX imm (to Thumb)
            off = w & 0xFFFFFF
            if off & 0x800000:
                off -= 0x1000000
            tgt = base + i * 4 + 8 + off * 4 + ((w >> 23) & 2)
            kind = "blx"
        else:
            continue
        if tgt in want:
            hits[tgt].append(f"{base + i * 4:08X}({kind})")
    for t in targets:
        print(f"{t:08X}: {len(hits[t])} caller(s) " + " ".join(hits[t][:40]))


if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), [int(x, 16) for x in sys.argv[4:]])
