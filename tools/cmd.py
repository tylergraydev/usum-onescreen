"""Send test commands to the running plugin and print what it logged back.

Usage: py -3 -I tools/cmd.py [--instance NAME] "move 160 120" "tap 10" ...

Commands: move X Y | touch on | touch off | tap FRAMES | mode off|panel|full | status
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import emu  # noqa: E402


if __name__ == "__main__":
    argv = sys.argv[1:]
    sdmc = emu.sdmc_for(argv)
    if not argv:
        sys.exit(__doc__)
    try:
        print(emu.send(argv, sdmc), end="")
    except emu.PluginTimeout as e:
        sys.exit(str(e))
