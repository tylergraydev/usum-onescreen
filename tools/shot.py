"""Take a screenshot of the running game through the plugin, no screen access needed.

Usage: py -3 -I tools/shot.py [name]

Creates the request file on Azahar's emulated SD card, waits for the plugin to
write both screens, and saves them as PNGs under analysis/private/shots/.
Prints the PNG paths.
"""
import os
import struct
import sys
import time
import zlib
from pathlib import Path

SDMC = Path(os.environ["APPDATA"]) / "Azahar" / "sdmc"
REQUEST = SDMC / "usum-shot.req"
OUTPUTS = {"top": SDMC / "usum-shot-top.ppm", "bottom": SDMC / "usum-shot-bottom.ppm"}
SHOTS = Path(__file__).resolve().parent.parent / "analysis" / "private" / "shots"


def read_ppm(path):
    data = path.read_bytes()
    parts = data.split(b"\n", 3)
    if parts[0] != b"P6":
        raise ValueError(f"{path} is not a binary PPM")
    width, height = map(int, parts[1].split())
    return width, height, parts[3]


def write_png(path, width, height, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    row = width * 3
    raw = b"".join(b"\0" + rgb[y * row:(y + 1) * row] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


def main(name):
    for p in OUTPUTS.values():
        p.unlink(missing_ok=True)
    REQUEST.write_bytes(b"")

    deadline = time.time() + 15
    while REQUEST.exists() and time.time() < deadline:
        time.sleep(0.2)
    if REQUEST.exists():
        REQUEST.unlink(missing_ok=True)
        sys.exit("no answer from the plugin within 15 s (is the game running with the plugin?)")

    SHOTS.mkdir(parents=True, exist_ok=True)
    for screen, ppm in OUTPUTS.items():
        width, height, rgb = read_ppm(ppm)
        out = SHOTS / f"{name}-{screen}.png"
        write_png(out, width, height, rgb)
        print(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else time.strftime("%Y%m%d-%H%M%S"))
