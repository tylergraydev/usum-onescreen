"""Send test commands to the running plugin and print what it logged back.

Usage: py -3 -I tools/cmd.py [--instance NAME] "move 160 120" "tap 10" ...

Commands: move X Y | touch on | touch off | tap FRAMES | mode off|panel|full | status
"""
import os
import sys
import time
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


def main(commands, sdmc):
    commands_file = sdmc / "usum-cmd.txt"
    log = sdmc / "usum-onescreen.log"
    before = log.read_text(errors="replace") if log.exists() else ""
    commands_file.write_text("\n".join(commands) + "\n")

    deadline = time.time() + 15
    while commands_file.exists() and time.time() < deadline:
        time.sleep(0.2)
    if commands_file.exists():
        commands_file.unlink(missing_ok=True)
        sys.exit("no answer from the plugin within 15 s (is the game running with the plugin?)")

    time.sleep(0.3)
    after = log.read_text(errors="replace") if log.exists() else ""
    print(after[len(before):] if after.startswith(before) else after, end="")


if __name__ == "__main__":
    argv = sys.argv[1:]
    sdmc = sdmc_for(argv)
    if not argv:
        sys.exit(__doc__)
    main(argv, sdmc)
