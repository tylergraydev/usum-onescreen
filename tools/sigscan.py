"""Find the plugin's hook targets and data addresses in a code.bin by byte signature.

Usage: py -3 -I tools/sigscan.py <code.bin> <signatures.json> [options]
  --base HEX        load address of code.bin (default: the signature file's "base")
  --expect BUILD    also fail unless every address equals the signature file's
                    "known" address for BUILD (e.g. usun-1.2)
  --emit-cpp        print a GameInfo entry for Sources/Game.cpp and the main-image
                    scene IDs for the settings defaults
  --name, --title-id, --version   fill in the emitted GameInfo entry

Signature file: targets in Offsets struct order. Each target has one or more
signatures; every signature must match exactly once and all of a target's
signatures must agree, otherwise the scan fails. A pattern is ARM words written
as 8 hex digits (most significant first, as a disassembler shows them), with
`?` for a wildcard nibble. Matches are 4-byte aligned.
  "at": byte offset in the pattern of the address to report (function start).
  "ldr": byte offset in the pattern of an `LDR Rt, [PC, #imm]`; the target is the
         literal-pool word that instruction loads (a data address or vtable).
  "bl":  byte offset in the pattern of a `BL imm`; the target is its destination
         (pins a function by a known caller).
"""
import argparse
import json
import re
import struct
import sys


class ScanError(Exception):
    pass


def compile_pattern(text):
    """ARM-word pattern -> (bytes regex, length in bytes)."""
    parts = []
    words = text.split()
    for word in words:
        if not re.fullmatch(r"[0-9A-Fa-f?]{8}", word):
            raise ScanError(f"bad pattern word {word!r}")
        # Little-endian: the last two digits are the first byte in memory.
        for i in (6, 4, 2, 0):
            hi, lo = word[i], word[i + 1]
            if hi == "?" and lo == "?":
                parts.append(b".")
            elif "?" not in (hi, lo):
                parts.append(re.escape(bytes([int(hi + lo, 16)])))
            else:
                values = [h * 16 + l
                          for h in (range(16) if hi == "?" else [int(hi, 16)])
                          for l in (range(16) if lo == "?" else [int(lo, 16)])]
                parts.append(b"[" + b"".join(re.escape(bytes([v])) for v in values) + b"]")
    return re.compile(b"(?=(" + b"".join(parts) + b"))", re.DOTALL), len(words) * 4


def find(data, pattern):
    regex, _ = compile_pattern(pattern)
    return [m.start() for m in regex.finditer(data) if m.start() % 4 == 0]


def ldr_literal(data, base, offset):
    """Value loaded by the ARM `LDR Rt, [PC, #+/-imm]` at file offset `offset`."""
    word = struct.unpack_from("<I", data, offset)[0]
    if (word & 0x0F7F0000) != 0x051F0000 or (word >> 28) == 0xF:
        raise ScanError(f"{base + offset:08X} is not LDR Rt, [PC, #imm] ({word:08X})")
    imm = word & 0xFFF
    literal = offset + 8 + (imm if word & 0x00800000 else -imm)
    if not 0 <= literal <= len(data) - 4:
        raise ScanError(f"literal for LDR at {base + offset:08X} is outside code.bin")
    return struct.unpack_from("<I", data, literal)[0], base + literal


def bl_target(data, base, offset):
    """Destination of the ARM `BL imm` at file offset `offset`."""
    word = struct.unpack_from("<I", data, offset)[0]
    if (word >> 24) & 0xF != 0xB or (word >> 28) == 0xF:
        raise ScanError(f"{base + offset:08X} is not BL ({word:08X})")
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 0x1000000
    return base + offset + 8 + imm * 4


def resolve(data, base, target):
    """Returns (address, notes) for one target, or raises ScanError."""
    name = target["name"]
    results = []
    notes = []
    for n, sig in enumerate(target["signatures"], 1):
        hits = find(data, sig["pattern"])
        if len(hits) != 1:
            where = " ".join(f"{base + h:08X}" for h in hits[:10])
            raise ScanError(f"{name}: signature {n} has {len(hits)} matches"
                            + (f" ({where})" if hits else "") + "; need exactly 1")
        hit = hits[0]
        if "ldr" in sig:
            value, literal = ldr_literal(data, base, hit + sig["ldr"])
            notes.append(f"sig {n} at {base + hit:08X}, literal at {literal:08X}")
        elif "bl" in sig:
            value = bl_target(data, base, hit + sig["bl"])
            notes.append(f"sig {n} at {base + hit:08X}, bl at {base + hit + sig['bl']:08X}")
        else:
            value = base + hit + sig.get("at", 0)
            notes.append(f"sig {n} at {base + hit:08X}")
        results.append(value)
    if len(set(results)) != 1:
        raise ScanError(f"{name}: signatures disagree: "
                        + " ".join(f"{v:08X}" for v in results))
    return results[0], notes


def scan(data, sigfile, base):
    """Returns [(target, address, notes)] or raises ScanError on the first failure."""
    return [(t, *resolve(data, base, t)) for t in sigfile["targets"]]


def prologue(data, base, address):
    offset = address - base
    if not 0 <= offset <= len(data) - 16:
        raise ScanError(f"{address:08X} is outside code.bin")
    return struct.unpack_from("<4I", data, offset)


def emit_cpp(data, base, found, name, title_id, version):
    lines = [f'        {{ "{name}", 0x{title_id:016X}ULL, 0x{version:04X}, true,',
             "          {"]
    for target, address, _ in found:
        if target["kind"] == "function":
            words = ", ".join(f"0x{w:08X}" for w in prologue(data, base, address))
            lines.append(f"              {{ 0x{address:08X}, {{ {words} }} }},"
                         f"   // {target['name']}")
        elif target["kind"] == "pointer":
            lines.append(f"              0x{address:08X},   // {target['name']}")
    lines += ["          } },", ""]
    scenes = [(t, a) for t, a, _ in found if t["kind"] == "scene"]
    if scenes:
        lines.append("// Main-image scene IDs for the settings defaults "
                     "(Includes/Settings.hpp, Sources/Settings.cpp, README.md):")
        lines += [f"//   main:{a:08X}   {t['name']}" for t, a in scenes]
    return "\n".join(lines)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("code")
    p.add_argument("signatures")
    p.add_argument("--base", type=lambda s: int(s, 16))
    p.add_argument("--expect")
    p.add_argument("--emit-cpp", action="store_true")
    p.add_argument("--name", default="Pokemon Ultra Moon")
    p.add_argument("--title-id", type=lambda s: int(s, 16), default=0x00040000001B5100)
    p.add_argument("--version", type=lambda s: int(s, 16), default=0x0800)
    args = p.parse_args(argv)

    with open(args.code, "rb") as f:
        data = f.read()
    with open(args.signatures, encoding="utf-8") as f:
        sigfile = json.load(f)
    base = args.base if args.base is not None else int(sigfile["base"], 16)

    failed = False
    found = []
    for target in sigfile["targets"]:
        try:
            address, notes = resolve(data, base, target)
        except ScanError as e:
            print(f"FAIL {e}")
            failed = True
            continue
        found.append((target, address, notes))
        check = ""
        if args.expect:
            known = target.get("known", {}).get(args.expect)
            if known is None:
                check = f"  (no known {args.expect} address)"
                failed = True
            elif int(known, 16) == address:
                check = f"  (= known {args.expect})"
            else:
                check = f"  (MISMATCH: known {args.expect} is {known})"
                failed = True
        n = len(target["signatures"])
        print(f"ok   {target['name']:<18} {address:08X}  {n} signature(s), 1 match each"
              f"  [{'; '.join(notes)}]{check}")

    if failed:
        print("sigscan: FAILED")
        return 1
    if args.emit_cpp:
        print()
        print(emit_cpp(data, base, found, args.name, args.title_id, args.version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
