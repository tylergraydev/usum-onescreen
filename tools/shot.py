"""Take a screenshot of the running game through the plugin, no screen access needed.

Usage: py -3 -I tools/shot.py [--instance NAME] [name]

Creates the request file on Azahar's emulated SD card, waits for the plugin to
write both screens, and saves them as PNGs under analysis/private/shots/.
Prints the PNG paths.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import emu  # noqa: E402


if __name__ == "__main__":
    argv = sys.argv[1:]
    sdmc = emu.sdmc_for(argv)
    try:
        for path in emu.shot(argv[0] if argv else time.strftime("%Y%m%d-%H%M%S"), sdmc):
            print(path)
    except emu.PluginTimeout as e:
        sys.exit(str(e))
