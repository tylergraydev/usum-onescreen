"""Named start points: an instance's save, snapshotted at a known place, so tests
can begin there instead of booting the save and walking.

Usage: py -3 -I tools/startpoint.py COMMAND ...

  list                      show the start points and how each one is reached
  save INSTANCE POINT [--kind title|field] [--note TEXT] [--force]
                            snapshot the (stopped) instance's save as POINT
  restore INSTANCE POINT    put POINT's save into the (stopped) instance
  go INSTANCE POINT [--timeout SECONDS]
                            restore, start the instance and drive the game to
                            POINT; prints the scene log; the instance stays up
  check                     print the real save's mtime and SHA-256

Azahar 2126.2 has no headless save states (no command-line option; its RPC
server has no save/load request; that arrived in 2127.0-alpha4 with a fixed UDP
port), so a start point is an in-game save: drive the instance there, save in
the game (X, then Y: Save), `stop`, then `save`. Points live in
analysis/private/startpoints/POINT/ (the save folder, the extdata folder and
point.json). Points are per game and can be restored into instances of that game.

Kinds: `title` stops on the title screen; `field` goes on with Start, A
(Continue) and waits for `scene: FieldRo`, where the save left the player.
Only instance folders are written; %APPDATA%\\Azahar is never touched.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True  # no tools/__pycache__
sys.path.insert(0, str(Path(__file__).resolve().parent))
import instance  # noqa: E402  (tools/instance.py)

POINTS = instance.REPO / "analysis" / "private" / "startpoints"
# The save folder and the extdata folder, under the SD ID folder.
SAVE_DIRS = {
    "sun": [Path("title/00040000/001b5000/data"), Path("extdata/00000000/00001B50")],
    "moon": [Path("title/00040000/001b5100/data"), Path("extdata/00000000/00001B50")],
}
REAL_SAVE = instance.APPDATA / "sdmc" / instance.SD_ID / "title/00040000/001b5000/data/00000001/main"
# Seconds after the plugin's boot line before the title is up. Measured on 2126.2:
# Start was taken 3 s after the boot line.
TITLE_SETTLE = 6


def save_dirs(game):
    """Save and extdata paths for the game, under the SD ID folder."""
    return SAVE_DIRS[game]


def point_dir(point):
    if not point.replace("-", "").replace("_", "").isalnum():
        sys.exit(f"bad start point name '{point}' (letters, digits, _ and - only)")
    return POINTS / point


def sd_root(name):
    return instance.sdmc_dir(name) / instance.SD_ID


def need_stopped(name):
    if not instance.user_dir(name).is_dir():
        sys.exit(f"no instance '{name}' (run tools/instance.py create first)")
    if instance.running_pid(name):
        sys.exit(f"instance '{name}' is running; stop it first (tools/instance.py stop {name})")


def inside(path, root):
    path, root = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(root))
    return os.path.commonpath([path, root]) == root


def file_hashes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def read_point(point):
    try:
        return json.loads((point_dir(point) / "point.json").read_text())
    except OSError:
        sys.exit(f"no start point '{point}' (see: list)")


def cmd_list(args):
    for meta in sorted(POINTS.glob("*/point.json")):
        info = json.loads(meta.read_text())
        print(f"{meta.parent.name}: {info.get('game', 'sun')}, {info['kind']}, from '{info['instance']}' {info['saved']}"
              f"{'  ' + info['note'] if info.get('note') else ''}")


def cmd_save(args):
    need_stopped(args.instance)
    game = instance.game_of(args.instance)
    dst = point_dir(args.point)
    if dst.exists():
        if not args.force:
            sys.exit(f"start point '{args.point}' exists (use --force to replace it)")
        shutil.rmtree(dst)
    for rel in save_dirs(game):
        shutil.copytree(sd_root(args.instance) / rel, dst / "sd" / rel)
    info = {"game": game, "kind": args.kind, "instance": args.instance, "note": args.note,
            "saved": datetime.now().isoformat(timespec="seconds"), "files": file_hashes(dst / "sd")}
    (dst / "point.json").write_text(json.dumps(info, indent=1) + "\n")
    print(f"saved '{args.point}' ({args.kind}) from '{args.instance}': {dst}")


def restore(name, point):
    need_stopped(name)
    info = read_point(point)
    game = info.get("game", "sun")
    instance_game = instance.game_of(name)
    if game != instance_game:
        sys.exit(f"start point '{point}' is for {game}, but instance '{name}' is for {instance_game}")
    src = point_dir(point) / "sd"
    if file_hashes(src) != info["files"]:
        sys.exit(f"start point '{point}' does not match its point.json hashes")
    for rel in save_dirs(game):
        dst = sd_root(name) / rel
        if not inside(dst, instance.INSTANCES):
            sys.exit(f"refusing to write outside the instances folder: {dst}")
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src / rel, dst)
    if any(file_hashes(sd_root(name) / rel) != file_hashes(src / rel) for rel in save_dirs(game)):
        sys.exit("restored files do not match the start point")
    return info


def cmd_restore(args):
    info = restore(args.instance, args.point)
    print(f"restored '{args.point}' ({info['kind']}) into '{args.instance}'")


def plugin(name, *commands):
    """Send commands through tools/cmd.py; return what the plugin logged."""
    out = subprocess.run([sys.executable, "-I", str(Path(__file__).parent / "cmd.py"),
                          "--instance", name, *commands], capture_output=True, text=True)
    if out.returncode:
        sys.exit(out.stderr.strip() or out.stdout.strip())
    return out.stdout


def scene(name):
    for line in plugin(name, "scene").splitlines():
        if line.startswith("scene:"):
            return line[len("scene:"):].strip()
    return None


def wait_scene(name, want, deadline, step=None):
    """Poll the scene until want(scene); run step() between polls."""
    while time.time() < deadline:
        now = scene(name)
        if want(now):
            return now
        if step:
            step()
        time.sleep(1.5)
    sys.exit(f"timed out; scene is '{scene(name)}'")


def cmd_go(args):
    info = restore(args.instance, args.point)
    log = instance.sdmc_dir(args.instance) / "usum-onescreen.log"
    start = subprocess.run([sys.executable, "-I", str(Path(__file__).parent / "instance.py"),
                            "start", args.instance, "--wait", "90"], capture_output=True, text=True)
    if start.returncode:
        sys.exit(start.stderr.strip() or start.stdout.strip())
    deadline = time.time() + args.timeout
    time.sleep(TITLE_SETTLE)
    if info["kind"] == "field":
        # Start opens TitleMenu (Continue preselected); A loads the save. Presses that
        # land too early are ignored, so repeat each until the scene moves on, and
        # never press A once TitleMenu is gone (in the field it would talk or use).
        wait_scene(args.instance, lambda s: s == "TitleMenu", deadline,
                   lambda: plugin(args.instance, "press start"))
        wait_scene(args.instance, lambda s: s != "TitleMenu", deadline,
                   lambda: plugin(args.instance, "press a"))
        wait_scene(args.instance, lambda s: s == "FieldRo", deadline)
    lines = [l for l in log.read_text(errors="replace").splitlines() if l.startswith("scene: ")]
    if lines:
        print("\n".join(lines))
    print(f"at '{args.point}' in '{args.instance}', scene now: {scene(args.instance) or '(none)'}")


def cmd_check(args):
    st = REAL_SAVE.stat()
    print(f"{REAL_SAVE}\nmtime {datetime.fromtimestamp(st.st_mtime).isoformat()}  "
          f"sha256 {hashlib.sha256(REAL_SAVE.read_bytes()).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list").set_defaults(func=cmd_list)
    sub.add_parser("check").set_defaults(func=cmd_check)
    p = sub.add_parser("save")
    p.add_argument("instance")
    p.add_argument("point")
    p.add_argument("--kind", choices=("title", "field"), default="field")
    p.add_argument("--note", default="")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_save)
    for cmd, func in (("restore", cmd_restore), ("go", cmd_go)):
        p = sub.add_parser(cmd)
        p.add_argument("instance")
        p.add_argument("point")
        p.set_defaults(func=func)
    p.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
