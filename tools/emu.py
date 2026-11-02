"""Shared helpers for talking to the running plugin through Azahar's emulated SD card.

Used by tools/cmd.py, tools/shot.py and tools/scenario.py.
"""
import os
import struct
import sys
import time
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SHOTS = REPO / "analysis" / "private" / "shots"


class PluginTimeout(Exception):
    pass


def sdmc_path(name=None):
    """The emulated SD of instance NAME (see tools/instance.py), or of %APPDATA%\\Azahar."""
    if name is None:
        return Path(os.environ["APPDATA"]) / "Azahar" / "sdmc"
    return REPO / "analysis" / "private" / "instances" / name / "azahar" / "user" / "sdmc"


def sdmc_for(argv):
    """Pop --instance NAME from argv; return that instance's emulated SD."""
    if "--instance" not in argv:
        return sdmc_path()
    i = argv.index("--instance")
    if i + 1 >= len(argv):
        sys.exit("--instance needs a name")
    name = argv.pop(i + 1)
    argv.pop(i)
    sdmc = sdmc_path(name)
    if not sdmc.is_dir():
        sys.exit(f"no instance '{name}' (see tools/instance.py)")
    return sdmc


def read_log(sdmc):
    log = sdmc / "usum-onescreen.log"
    return log.read_text(errors="replace") if log.exists() else ""


def wait_gone(path, timeout):
    deadline = time.time() + timeout
    while path.exists() and time.time() < deadline:
        time.sleep(0.2)
    if path.exists():
        path.unlink(missing_ok=True)
        raise PluginTimeout(f"no answer from the plugin within {timeout:g} s "
                            "(is the game running with the plugin?)")


def send(commands, sdmc, timeout=15):
    """Send test commands (via usum-cmd.txt); return the log text they produced."""
    commands_file = sdmc / "usum-cmd.txt"
    before = read_log(sdmc)
    commands_file.write_text("\n".join(commands) + "\n")
    wait_gone(commands_file, timeout)
    time.sleep(0.3)
    after = read_log(sdmc)
    return after[len(before):] if after.startswith(before) else after


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


def shot(name, sdmc, timeout=15):
    """Ask the plugin for both screens; save SHOTS/<name>-top.png and -bottom.png; return the paths."""
    request = sdmc / "usum-shot.req"
    outputs = {"top": sdmc / "usum-shot-top.ppm", "bottom": sdmc / "usum-shot-bottom.ppm"}
    for p in outputs.values():
        p.unlink(missing_ok=True)
    request.write_bytes(b"")
    wait_gone(request, timeout)

    SHOTS.mkdir(parents=True, exist_ok=True)
    paths = []
    for screen, ppm in outputs.items():
        width, height, rgb = read_ppm(ppm)
        out = SHOTS / f"{name}-{screen}.png"
        write_png(out, width, height, rgb)
        paths.append(out)
    return paths
