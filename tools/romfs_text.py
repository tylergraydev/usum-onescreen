"""List / extract RomFS files from a decrypted CCI or CIA, and decode Gen 7 GARC text.

Usage:
  py -3 -I tools/romfs_text.py list    <decrypted.cci|cia> [path-prefix]
  py -3 -I tools/romfs_text.py extract <decrypted.cci|cia> <romfs-path> <out_dir>
  py -3 -I tools/romfs_text.py garc    <file.garc>                      # list entries / variants
  py -3 -I tools/romfs_text.py text    <file.garc> <entry> [variant] [--raw]
  py -3 -I tools/romfs_text.py names   <base.cci> <update.cia> <out_dir> [variant]
  py -3 -I tools/romfs_text.py decrypt <line> <u32 words...>          # words as `peek` logs them

`names` extracts a/0/3/2 (English; taking the update's copy when it has one) and
writes the name lists in NAME_FILES as UTF-8 text under <out_dir>.
`decrypt` decodes a line read from RAM with the plugin's `peek` (little-endian
u32 words, two UTF-16 code units each) so runtime reads can be checked.
Only reads NoCrypto NCCHs. Output is game data: keep it in analysis/private/.
"""
import struct
import sys
from pathlib import Path

MEDIA = 0x200


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def align(x, a): return (x + a - 1) // a * a


SIG_SIZES = {0x10000: 0x200 + 0x3C, 0x10001: 0x100 + 0x3C, 0x10002: 0x3C + 0x40,
             0x10003: 0x200 + 0x3C, 0x10004: 0x100 + 0x3C, 0x10005: 0x3C + 0x40}


def ncch_offset(f):
    """Offset of the main NCCH (partition / content 0) in a CCI or CIA."""
    f.seek(0x100)
    if f.read(4) == b"NCSD":
        f.seek(0x120)
        return u32(f.read(4), 0) * MEDIA
    f.seek(0)
    hdr = f.read(0x20)
    hdr_size, _, _, cert_size, tik_size, tmd_size = struct.unpack_from("<IHHIII", hdr, 0)
    tmd_off = align(align(align(hdr_size, 64) + cert_size, 64) + tik_size, 64)
    f.seek(tmd_off)
    tmd = f.read(tmd_size)
    sig = struct.unpack_from(">I", tmd, 0)[0]
    th = 4 + SIG_SIZES[sig]
    c_type = struct.unpack_from(">H", tmd, th + 0xC4 + 64 * 0x24 + 6)[0]
    if c_type & 1:
        sys.exit("CIA content 0 is encrypted")
    return align(tmd_off + tmd_size, 64)


class RomFS:
    def __init__(self, path):
        self.f = open(path, "rb")
        base = ncch_offset(self.f)
        self.f.seek(base)
        ncch = self.f.read(0x200)
        if ncch[0x100:0x104] != b"NCCH":
            sys.exit("no NCCH at partition 0")
        if not ncch[0x18F] & 0x4:
            sys.exit(f"NCCH is not NoCrypto (flags[7]={ncch[0x18F]:#x})")
        romfs = base + u32(ncch, 0x1B0) * MEDIA
        self.f.seek(romfs)
        ivfc = self.f.read(0x60)
        if ivfc[:4] != b"IVFC":
            sys.exit("RomFS has no IVFC header")
        master_size = u32(ivfc, 0x08)
        l3_log2 = u32(ivfc, 0x4C)
        self.l3 = romfs + align(0x60 + master_size, 1 << l3_log2)
        self.f.seek(self.l3)
        h = struct.unpack("<10I", self.f.read(0x28))
        self.dir_meta = self._read(h[3], h[4])
        self.file_meta = self._read(h[7], h[8])
        self.data = self.l3 + h[9]

    def _read(self, off, size):
        self.f.seek(self.l3 + off)
        return self.f.read(size)

    @staticmethod
    def _name(meta, off, base):
        n = u32(meta, off + base)
        return meta[off + base + 4:off + base + 4 + n].decode("utf-16-le")

    def walk(self, d=0, prefix=""):
        """Yield (path, data_offset, size) for every file."""
        m = self.dir_meta
        f = u32(m, d + 0x0C)
        while f != 0xFFFFFFFF:
            name = self._name(self.file_meta, f, 0x1C)
            off, size = struct.unpack_from("<QQ", self.file_meta, f + 0x08)
            yield prefix + name, self.data + off, size
            f = u32(self.file_meta, f + 0x04)
        c = u32(m, d + 0x08)
        while c != 0xFFFFFFFF:
            yield from self.walk(c, prefix + self._name(m, c, 0x14) + "/")
            c = u32(m, c + 0x04)

    def get(self, path):
        for p, off, size in self.walk():
            if p == path:
                self.f.seek(off)
                return self.f.read(size)
        return None


class Garc:
    """GARC v6 (Gen 7). entries[i] = {variant_bit: (start, end, length)}."""

    def __init__(self, b):
        if b[:4] != b"CRAG":
            raise ValueError("not a GARC")
        hsize, _bom, self.version = u32(b, 4), u16(b, 8), u16(b, 0x0A)
        self.data_off = u32(b, 0x10)
        o = hsize
        assert b[o:o + 4] == b"OTAF"
        o += u32(b, o + 4)
        assert b[o:o + 4] == b"BTAF"
        count = u32(b, o + 8)
        p = o + 12
        self.entries = []
        for _ in range(count):
            bits = u32(b, p)
            p += 4
            e = {}
            for bit in range(32):
                if bits >> bit & 1:
                    e[bit] = struct.unpack_from("<III", b, p)
                    p += 12
            self.entries.append(e)
        self.b = b

    def file(self, i, variant=None):
        e = self.entries[i]
        v = variant if variant is not None else min(e)
        start, end, length = e[v]
        return self.b[self.data_off + start:self.data_off + start + length]


# Message data (gfl2::str::MsgData). Header: u16 sections, u16 lines, u32 size,
# u32 key, u32 section offsets. Section: u32 size, then per line {u32 offset from
# section start, u16 length in chars, u16 extra}, then the encrypted UTF-16 text.
# Line N's key starts at 0x2983 * (N + 3) and rotates left 3 bits per char
# (the game's decoder at 0x4B877C does the same; see docs/RE2-text.md).
KEY_BASE, KEY_ADV = 0x7C89, 0x2983


def decrypt_line(words, line):
    """Decrypt one line's u16 code units (as stored in the file or in RAM)."""
    k = (KEY_ADV * (line + 3)) & 0xFFFF
    out = []
    for w in words:
        out.append(w ^ k)
        k = ((k << 3) | (k >> 13)) & 0xFFFF
    return out


def text_lines(b, raw=False):
    sections, nlines = u16(b, 0), u16(b, 2)
    sec = u32(b, 0x0C)  # first section only (Gen 7 files have one)
    out = []
    for i in range(nlines):
        off, ln = struct.unpack_from("<IH", b, sec + 4 + i * 8)
        chars = decrypt_line([u16(b, sec + off + j * 2) for j in range(ln)], i)
        out.append(chars if raw else render(chars))
    return out


def render(chars):
    s, i = [], 0
    while i < len(chars):
        c = chars[i]
        if c == 0:
            break
        if c == 0x10:  # variable: 0x10, count, code, args...
            n = chars[i + 1]
            args = chars[i + 2:i + 2 + n]
            s.append("[VAR " + " ".join(f"{a:04X}" for a in args) + "]")
            i += 2 + n
            continue
        if c == 0xE07F:
            s.append(" ")
        elif c == 0xE08D:
            s.append("…")
        elif c == 0xE08E:
            s.append("♂")
        elif c == 0xE08F:
            s.append("♀")
        elif c in (0x0A,):
            s.append("\\n")
        elif c < 0x20 or 0xE000 <= c < 0xF900:
            s.append(f"[{c:04X}]")
        else:
            s.append(chr(c))
        i += 1
    return "".join(s)


# Entry numbers inside the message GARC (a/0/3/<language>), checked by decoding
# and against the game's own callers: see docs/RE2-text.md.
NAME_FILES = {"species": 60, "moves": 118, "items": 40, "items-wordset": 42,
              "abilities": 101, "types": 112, "natures": 92}


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd = argv[0]
    if cmd == "list":
        prefix = argv[2] if len(argv) > 2 else ""
        for p, off, size in RomFS(argv[1]).walk():
            if p.startswith(prefix):
                print(f"{size:>10}  {p}")
    elif cmd == "extract":
        data = RomFS(argv[1]).get(argv[2])
        if data is None:
            sys.exit(f"{argv[2]} not found")
        out = Path(argv[3]) / argv[2]
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        print(f"{out} ({len(data)} bytes)")
    elif cmd == "garc":
        g = Garc(Path(argv[1]).read_bytes())
        print(f"version {g.version:#x}, {len(g.entries)} entries")
        for i, e in enumerate(g.entries):
            print(i, " ".join(f"v{v}:{length}" for v, (_, _, length) in sorted(e.items())))
    elif cmd == "text":
        g = Garc(Path(argv[1]).read_bytes())
        variant = int(argv[3]) if len(argv) > 3 and argv[3] != "--raw" else None
        raw = "--raw" in argv
        for i, line in enumerate(text_lines(g.file(int(argv[2]), variant), raw)):
            print(i, " ".join(f"{c:04X}" for c in line) if raw else line)
    elif cmd == "names":
        base, upd, out = argv[1], argv[2], Path(argv[3])
        variant = int(argv[4]) if len(argv) > 4 else None
        path = "a/0/3/2"
        b = RomFS(upd).get(path)
        src = "update"
        if b is None:
            b, src = RomFS(base).get(path), "base"
        out.mkdir(parents=True, exist_ok=True)
        g = Garc(b)
        for name, idx in NAME_FILES.items():
            lines = text_lines(g.file(idx, variant))
            (out / f"{name}.txt").write_text(
                "".join(f"{i}\t{s}\n" for i, s in enumerate(lines)), encoding="utf-8")
            print(f"{name}: {path} entry {idx} ({src}), {len(lines)} lines")
    elif cmd == "decrypt":
        line = int(argv[1], 0)
        words = []
        for w in argv[2:]:
            v = int(w, 16)
            words += [v & 0xFFFF, v >> 16]
        chars = decrypt_line(words, line)
        print(render(chars))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
