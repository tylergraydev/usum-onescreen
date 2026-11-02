"""Tests for tools/sigscan.py.

Usage: python3 -I tests/sigscan_test.py [code.bin]

Always: builds a synthetic image from signatures/usum.json (random filler, each
signature placed with its wildcards filled and its LDR/BL fixed up) and checks
every target resolves to where it was placed, and that zero or duplicate
matches fail. This part needs no game data and runs in CI.

With an Ultra Sun v1.2 code.bin (argument, or SUNNYMOON_CODE_BIN): checks every
target resolves to its known address, then builds a shifted copy (padding
inserted in .text with B/BL relinked, data pointers moved) and checks every
target follows.
"""
import json
import os
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sigscan  # noqa: E402

SIGS = json.loads((ROOT / "signatures" / "usum.json").read_text(encoding="utf-8"))
BASE = int(SIGS["base"], 16)
failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def fill(pattern, rng):
    """Pattern -> list of words with each wildcard nibble randomised."""
    words = []
    for text in pattern.split():
        word = 0
        for c in text:
            word = word * 16 + (rng.randrange(16) if c == "?" else int(c, 16))
        words.append(word)
    return words


def synthetic():
    rng = random.Random(1234)
    size = 0x40000
    image = bytearray(rng.randbytes(size))
    expected = {}
    cursor = 0x1000

    def put(offset, word):
        struct.pack_into("<I", image, offset, word)

    for target in SIGS["targets"]:
        # Functions get their own home; data/vtable values are made up.
        if target["kind"] == "function":
            value = BASE + 0x30000 + len(expected) * 0x100
        else:
            value = 0x00600000 + len(expected) * 0x1000
        expected[target["name"]] = value
        for sig in target["signatures"]:
            words = fill(sig["pattern"], rng)
            at = cursor
            for i, w in enumerate(words):
                put(at + i * 4, w)
            end = at + len(words) * 4
            if "ldr" in sig:
                pc = at + sig["ldr"]
                ldr = struct.unpack_from("<I", image, pc)[0]
                put(pc, (ldr & ~0x00800FFF) | 0x00800000 | (end - (pc + 8)))
                put(end, value)
            elif "bl" in sig:
                pc = at + sig["bl"]
                bl = struct.unpack_from("<I", image, pc)[0]
                put(pc, (bl & 0xFF000000) | (((value - BASE - (pc + 8)) >> 2) & 0xFFFFFF))
            else:
                # The function itself: move it to its home.
                image[value - BASE:value - BASE + len(words) * 4] = image[at:end]
                image[at:end] = rng.randbytes(end - at)
            cursor = end + 0x40

    data = bytes(image)
    found = {t["name"]: a for t, a, _ in sigscan.scan(data, SIGS, BASE)}
    for name, value in expected.items():
        check(found.get(name) == value, f"synthetic: {name} at {value:08X} (got {found.get(name, 0):08X})")

    # A second copy of a signature must fail, as must a missing one.
    first = SIGS["targets"][0]
    words = fill(first["signatures"][0]["pattern"], rng)
    dup = bytearray(data)
    for i, w in enumerate(words):
        struct.pack_into("<I", dup, 0x38000 + i * 4, w)
    check(raises(bytes(dup), first), "synthetic: duplicate match fails")
    missing = bytearray(data)
    home = expected[first["name"]] - BASE
    missing[home:home + 4] = b"\0\0\0\0"
    check(raises(bytes(missing), first), "synthetic: missing match fails")


def raises(data, target):
    try:
        sigscan.resolve(data, BASE, target)
    except sigscan.ScanError as e:
        print(f"     ({e})")
        return True
    return False


def shift(data, inserts, text_end, data_moves):
    """Insert zero padding at the given addresses in .text and relink every ARM
    B/BL that crosses an insertion; then rewrite literal words equal to a moved
    data address. Returns (new code.bin, address mapping function)."""
    inserts = sorted(inserts.items())

    def moved(addr):
        if addr >= text_end:
            return addr + sum(n for _, n in inserts)
        return addr + sum(n for a, n in inserts if a <= addr)

    out = bytearray()
    prev = BASE
    for at, n in inserts + [(BASE + len(data), 0)]:
        chunk = bytearray(data[prev - BASE:at - BASE])
        for i in range(0, len(chunk) - 3 if prev < text_end else 0, 4):
            src = prev + i
            if src >= text_end:
                break
            w = struct.unpack_from("<I", chunk, i)[0]
            if (w >> 28) == 0xF or (w >> 25) & 7 != 5:
                continue
            imm = w & 0xFFFFFF
            if imm & 0x800000:
                imm -= 0x1000000
            dst = src + 8 + imm * 4
            if not BASE <= dst < text_end:
                continue
            new = (moved(dst) - (moved(src) + 8)) >> 2
            struct.pack_into("<I", chunk, i, (w & 0xFF000000) | (new & 0xFFFFFF))
        out += chunk + bytes(n)
        prev = at
    for old, new in data_moves.items():
        for i in range(0, len(out) - 3, 4):
            if struct.unpack_from("<I", out, i)[0] == old:
                struct.pack_into("<I", out, i, new)
    return bytes(out), moved


def real(path):
    data = Path(path).read_bytes()
    known = {t["name"]: int(t["known"]["usun-1.2"], 16) for t in SIGS["targets"]}
    found = {t["name"]: a for t, a, _ in sigscan.scan(data, SIGS, BASE)}
    for name, value in known.items():
        check(found[name] == value, f"usun-1.2: {name} at {value:08X} (got {found[name]:08X})")

    # Padding before a caller, before gspSetFramebuffer and before hidPadRead,
    # so every function moves by a different amount and caller BLs change.
    inserts = {0x17902C: 0x150, 0x279BB4: 0x1000, 0x282548: 0x24}
    data_moves = {known[t["name"]]: known[t["name"]] + 0x2000
                  for t in SIGS["targets"] if t["kind"] != "function"}
    shifted, moved = shift(data, inserts, 0x5BC000, data_moves)
    found = {t["name"]: a for t, a, _ in sigscan.scan(shifted, SIGS, BASE)}
    for t in SIGS["targets"]:
        name = t["name"]
        want = moved(known[name]) if t["kind"] == "function" else data_moves[known[name]]
        check(found[name] == want and want != known[name],
              f"shifted: {name} {known[name]:08X} -> {want:08X} (got {found[name]:08X})")


def main():
    synthetic()
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SUNNYMOON_CODE_BIN")
    if path:
        real(path)
    else:
        print("skip real code.bin checks (no path given and SUNNYMOON_CODE_BIN unset)")
    print("sigscan_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
