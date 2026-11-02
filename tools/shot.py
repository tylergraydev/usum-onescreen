"""Take a screenshot of the running game through the plugin, no screen access needed.

Usage: py -3 -I tools/shot.py [--instance NAME] [name]

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


def sdmc_for(argv):
    """Pop --instance NAME from argv; return that instance's emulated SD (see tools/instance.py)."""
    if "--instance" not in argv:
        return Path(os.environ["APPDATA"]) / "Azahar" / "sdmc"
    i = argv.index("--instance")
    if i + 1 >= len(argv):
        sys.exit("--instance needs a name")
    name = argv.pop(i + 1)
    argv.pop(i)
    sdmc = (Path(__file__).resolve().parent.parent / "analysis" / "private" / "instances"
            / name / "azahar" / "user" / "sdmc")
    if not sdmc.is_dir():
        sys.exit(f"no instance '{name}' (see tools/instance.py)")
    return sdmc


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


def main(name, sdmc):
    request = sdmc / "usum-shot.req"
    outputs = {"top": sdmc / "usum-shot-top.ppm", "bottom": sdmc / "usum-shot-bottom.ppm"}
    for p in outputs.values():
        p.unlink(missing_ok=True)
    request.write_bytes(b"")

    deadline = time.time() + 15
    while request.exists() and time.time() < deadline:
        time.sleep(0.2)
    if request.exists():
        request.unlink(missing_ok=True)
        sys.exit("no answer from the plugin within 15 s (is the game running with the plugin?)")

    SHOTS.mkdir(parents=True, exist_ok=True)
    for screen, ppm in outputs.items():
        width, height, rgb = read_ppm(ppm)
        out = SHOTS / f"{name}-{screen}.png"
        write_png(out, width, height, rgb)
        print(out)


if __name__ == "__main__":
    argv = sys.argv[1:]
    sdmc = sdmc_for(argv)
    main(argv[0] if argv else time.strftime("%Y%m%d-%H%M%S"), sdmc)
