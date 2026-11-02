"""Extract the decompressed ExeFS .code (code.bin) and exheader from a decrypted CIA.

Usage: py -3 -I tools/extract_code.py <decrypted.cia> <out_dir>

Only reads content 0 (the main NCCH). Refuses encrypted content instead of
guessing. Output stays local (analysis/private/ is gitignored).
"""
import hashlib
import json
import struct
import sys
from pathlib import Path


def align(x, a=64):
    return (x + a - 1) // a * a


def u16be(b, o): return struct.unpack_from(">H", b, o)[0]
def u32be(b, o): return struct.unpack_from(">I", b, o)[0]
def u64be(b, o): return struct.unpack_from(">Q", b, o)[0]
def u32le(b, o): return struct.unpack_from("<I", b, o)[0]


SIG_SIZES = {0x10000: 0x200 + 0x3C, 0x10001: 0x100 + 0x3C, 0x10002: 0x3C + 0x40,
             0x10003: 0x200 + 0x3C, 0x10004: 0x100 + 0x3C, 0x10005: 0x3C + 0x40}


def blz_decompress(comp):
    """Backwards LZSS used for 3DS ExeFS .code (port of ctrtool's lzss.c)."""
    size = len(comp)
    top_bottom = u32le(comp, size - 8)
    extra = u32le(comp, size - 4)
    out_size = size + extra
    out = bytearray(out_size)
    out[:size] = comp
    index = size - ((top_bottom >> 24) & 0xFF)
    stop = size - (top_bottom & 0xFFFFFF)
    dst = out_size
    while index > stop:
        index -= 1
        control = comp[index]
        for _ in range(8):
            if index <= stop or index <= 0 or dst <= 0:
                break
            if control & 0x80:
                index -= 2
                seg = comp[index] | (comp[index + 1] << 8)
                seg_size = ((seg >> 12) & 0xF) + 3
                seg_off = (seg & 0x0FFF) + 2
                for _ in range(seg_size):
                    if dst + seg_off >= out_size:
                        raise ValueError("BLZ: back-reference past end")
                    v = out[dst + seg_off]
                    dst -= 1
                    out[dst] = v
            else:
                index -= 1
                dst -= 1
                out[dst] = comp[index]
            control = (control << 1) & 0xFF
    return bytes(out)


def main(cia_path, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(cia_path, "rb") as f:
        hdr = f.read(0x20)
        hdr_size, _, _, cert_size, tik_size, tmd_size, _meta = struct.unpack_from("<IHHIIII", hdr, 0)
        tmd_off = align(align(align(hdr_size) + cert_size) + tik_size)
        content_off = align(tmd_off + tmd_size)

        f.seek(tmd_off)
        tmd = f.read(tmd_size)
        sig_type = u32be(tmd, 0)
        th = 4 + SIG_SIZES[sig_type]
        title_id = u64be(tmd, th + 0x4C)
        title_ver = u16be(tmd, th + 0x9C)
        count = u16be(tmd, th + 0x9E)
        chunks = th + 0xC4 + 64 * 0x24
        c_id, c_idx, c_type, c_size = struct.unpack_from(">IHHQ", tmd, chunks)
        if c_type & 1:
            sys.exit(f"content {c_idx} is encrypted (type {c_type:#x}); need a decrypted CIA")

        f.seek(content_off)
        ncch = f.read(0x200)
        if ncch[0x100:0x104] != b"NCCH":
            sys.exit("content 0 is not an NCCH")
        flags7 = ncch[0x18F]
        if not flags7 & 0x4:
            sys.exit(f"NCCH is not marked NoCrypto (flags[7]={flags7:#x}); refusing to guess")
        exh_size = struct.unpack_from("<I", ncch, 0x180)[0]
        exefs_off = struct.unpack_from("<I", ncch, 0x1A0)[0] * 0x200
        exefs_size = struct.unpack_from("<I", ncch, 0x1A4)[0] * 0x200

        f.seek(content_off + 0x200)
        exheader = f.read(0x800)

        f.seek(content_off + exefs_off)
        exefs = f.read(exefs_size)

    code = None
    for i in range(10):
        name, off, size = struct.unpack_from("<8sII", exefs, i * 16)
        if name.rstrip(b"\0") == b".code":
            code = exefs[0x200 + off:0x200 + off + size]
            break
    if code is None:
        sys.exit("no .code in ExeFS")

    compressed = exheader[0x0D] & 1
    if compressed:
        code = blz_decompress(code)

    def codeset(o):
        addr, pages, size = struct.unpack_from("<III", exheader, o)
        return {"address": hex(addr), "pages": pages, "size": hex(size)}

    text, ro, data = codeset(0x10), codeset(0x20), codeset(0x30)
    info = {
        "title_id": f"{title_id:016X}",
        "title_version": title_ver,
        "content_count": count,
        "exheader_name": exheader[0:8].rstrip(b"\0").decode("ascii", "replace"),
        "code_compressed": bool(compressed),
        "text": text, "ro": ro, "data": data,
        "bss_size": hex(struct.unpack_from("<I", exheader, 0x3C)[0]),
        "code_bin_size": hex(len(code)),
        "code_bin_sha256": hashlib.sha256(code).hexdigest(),
    }
    (out_dir / "code.bin").write_bytes(code)
    (out_dir / "exheader.bin").write_bytes(exheader)
    (out_dir / "info.json").write_text(json.dumps(info, indent=2))
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
