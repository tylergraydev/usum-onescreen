"""Find ARM instructions that load a given 32-bit value through a PC-relative LDR.

Usage: py -3 -I tools/find_refs.py <code.bin> <base_hex> <text_end_hex> <value_hex> [...]
For each value: finds aligned literal-pool words equal to it, then every
`LDR Rt, [PC, #+/-imm]` (ARM mode) in .text that reads that word.
"""
import struct
import sys


def main(path, base, text_end, values):
    data = open(path, "rb").read()
    words = struct.unpack_from(f"<{len(data) // 4}I", data)
    text_words = (text_end - base) // 4
    for v in values:
        pools = [i for i, w in enumerate(words) if w == v]
        refs = []
        for p in pools:
            lit = base + p * 4
            lo = max(0, p - 1026)
            for i in range(lo, min(text_words, p + 1026)):
                w = words[i]
                if (w & 0x0F7F0000) == 0x051F0000 and (w >> 28) != 0xF:
                    imm = w & 0xFFF
                    addr = base + i * 4
                    tgt = addr + 8 + (imm if w & 0x00800000 else -imm)
                    if tgt == lit:
                        refs.append((addr, (w >> 12) & 0xF))
        print(f"{v:08X}: pools " + " ".join(f"{base + p * 4:08X}" for p in pools[:10])
              + " | loads " + " ".join(f"{a:08X}(r{r})" for a, r in refs[:20]))


if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), [int(x, 16) for x in sys.argv[4:]])
