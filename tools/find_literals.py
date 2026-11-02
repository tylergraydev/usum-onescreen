"""List addresses of 32-bit literals and strings in a code.bin.

Usage: py -3 -I tools/find_literals.py <code.bin> <base_hex> <value_or_"string"> [...]
Values are matched as aligned little-endian u32; quoted args are matched as ASCII.
"""
import struct
import sys


def main(path, base, needles):
    data = open(path, "rb").read()
    for n in needles:
        hits = []
        if n.startswith('"'):
            pat = n.strip('"').encode()
            i = data.find(pat)
            while i != -1 and len(hits) < 40:
                hits.append(base + i)
                i = data.find(pat, i + 1)
        else:
            pat = struct.pack("<I", int(n, 16))
            i = data.find(pat)
            while i != -1 and len(hits) < 40:
                if i % 4 == 0:
                    hits.append(base + i)
                i = data.find(pat, i + 1)
        print(f"{n}: {len(hits)} hit(s) " + " ".join(f"{h:08X}" for h in hits))


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2], 16), sys.argv[3:])
