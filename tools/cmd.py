"""Send test commands to the running plugin and print what it logged back.

Usage: py -3 -I tools/cmd.py "move 160 120" "tap 10" ...

Commands: move X Y | touch on | touch off | tap FRAMES | mode off|panel|full | status
"""
import os
import sys
import time
from pathlib import Path

SDMC = Path(os.environ["APPDATA"]) / "Azahar" / "sdmc"
COMMANDS = SDMC / "usum-cmd.txt"
LOG = SDMC / "usum-onescreen.log"


def main(commands):
    before = LOG.read_text(errors="replace") if LOG.exists() else ""
    COMMANDS.write_text("\n".join(commands) + "\n")

    deadline = time.time() + 15
    while COMMANDS.exists() and time.time() < deadline:
        time.sleep(0.2)
    if COMMANDS.exists():
        COMMANDS.unlink(missing_ok=True)
        sys.exit("no answer from the plugin within 15 s (is the game running with the plugin?)")

    time.sleep(0.3)
    after = LOG.read_text(errors="replace") if LOG.exists() else ""
    print(after[len(before):] if after.startswith(before) else after, end="")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
