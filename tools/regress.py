"""Regression run: put a plugin build into an instance and run every scenario from a start point.

Usage: py -3 -I tools/regress.py [--instance NAME] [--plugin 3GX | --run RUN_ID | --main]
                                 [--fresh] [--point POINT] [--keep] [SCENARIO ...]

  --plugin 3GX   a local build (default: ./usum-onescreen.3gx)
  --run RUN_ID   the usum-onescreen artifact of that GitHub Actions run (gh run download)
  --main         the artifact of the newest successful Build plugin run on main
  --fresh        rebuild the instance (tools/instance.py create --force)
  --point POINT  the start point restored before each scenario (default pokecenter)

The instance (default `regress`) is created if missing, must not be running,
and gets the plugin installed. Before each scenario the start point's save is
restored (tools/startpoint.py restore), so every scenario begins from the same
known save whatever the one before it did; then tools/scenario.py runs it,
with its log guard on. With no SCENARIO arguments it runs every
scenarios/*.txt. Prints PASS/FAIL per scenario and a summary; exit code 0 only
if all passed. With --keep, the first failing scenario leaves the instance
running and ends the run, for a look at it.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
DOWNLOADS = REPO / "analysis" / "private" / "regress"
ARTIFACT = "usum-onescreen"


def tool(script, *args, capture=False):
    """Run tools/<script> with this Python; the caller checks the result."""
    proc = subprocess.run([sys.executable, "-I", str(TOOLS / script), *map(str, args)],
                          capture_output=capture, text=True)
    return proc


def must(proc, what):
    if proc.returncode:
        out = ((proc.stdout or "") + (proc.stderr or "")).strip()
        sys.exit(f"{what} failed" + (f": {out}" if out else ""))
    return proc


def gh(*args):
    proc = subprocess.run(["gh", *args], capture_output=True, text=True, cwd=REPO)
    if proc.returncode:
        sys.exit(f"gh {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout


def latest_main_run():
    runs = json.loads(gh("run", "list", "--workflow", "build.yml", "--branch", "main", "--status", "success",
                         "--limit", "1", "--json", "databaseId,headSha,createdAt"))
    if not runs:
        sys.exit("no successful Build plugin run on main")
    run = runs[0]
    print(f"newest green main build: run {run['databaseId']}, {run['headSha'][:7]}, {run['createdAt']}")
    return str(run["databaseId"])


def download(run_id):
    dest = DOWNLOADS / f"run-{run_id}"
    plugin = dest / f"{ARTIFACT}.3gx"
    if not plugin.is_file():
        dest.mkdir(parents=True, exist_ok=True)
        gh("run", "download", run_id, "--name", ARTIFACT, "--dir", str(dest))
        if not plugin.is_file():
            sys.exit(f"artifact of run {run_id} has no {plugin.name}")
    return plugin


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--instance", default="regress")
    src = parser.add_mutually_exclusive_group()
    src.add_argument("--plugin", type=Path)
    src.add_argument("--run")
    src.add_argument("--main", action="store_true")
    parser.add_argument("--fresh", action="store_true", help="rebuild the instance first")
    parser.add_argument("--point", default="pokecenter", help="start point restored before each scenario")
    parser.add_argument("--keep", action="store_true", help="stop at the first failure, instance left running")
    parser.add_argument("scenarios", nargs="*")
    args = parser.parse_args()

    if args.main:
        args.run = latest_main_run()
    plugin = download(args.run) if args.run else (args.plugin or REPO / f"{ARTIFACT}.3gx")
    if not Path(plugin).is_file():
        sys.exit(f"no plugin at {plugin}")
    sha = hashlib.sha256(Path(plugin).read_bytes()).hexdigest()
    print(f"plugin: {plugin} (sha256 {sha[:16]})")

    scenarios = args.scenarios or sorted(p.stem for p in (REPO / "scenarios").glob("*.txt"))
    must(tool("scenario.py", "--check", *[s if Path(s).is_file() else REPO / "scenarios" / f"{s}.txt"
                                          for s in scenarios], capture=True), "parsing the scenarios")

    exists = (REPO / "analysis" / "private" / "instances" / args.instance / "azahar" / "azahar.exe").is_file()
    status = must(tool("instance.py", "status", args.instance, capture=True), "instance status").stdout \
        if exists else ""
    if "running" in status:
        sys.exit(f"instance '{args.instance}' is running; stop it first (tools/instance.py stop {args.instance})")
    if args.fresh or not exists:
        print(must(tool("instance.py", "create", args.instance, *(["--force"] if exists else []), capture=True),
                   "creating the instance").stdout.strip())
    print(must(tool("instance.py", "install", args.instance, plugin, capture=True),
               "installing the plugin").stdout.strip())

    results = []
    t0 = time.time()
    for name in scenarios:
        print(f"--- {name}: restore start point '{args.point}'", flush=True)
        restored = tool("startpoint.py", "restore", args.instance, args.point, capture=True)
        if restored.returncode:
            print(f"FAIL {name}: restoring '{args.point}': {(restored.stdout + restored.stderr).strip()}")
            results.append((name, False))
            continue
        ok = tool("scenario.py", "--instance", args.instance, *(["--keep"] if args.keep else []),
                  name).returncode == 0
        results.append((name, ok))
        if not ok and args.keep:
            print(f"instance '{args.instance}' left running (--keep)")
            break
        # A failed scenario stops the instance itself; a passing one ends with `stop`. Make sure.
        if "running" in tool("instance.py", "status", args.instance, capture=True).stdout:
            tool("instance.py", "stop", args.instance)

    passed = sum(ok for _, ok in results)
    print(f"regress: {passed}/{len(results)} passed in {time.time() - t0:.0f} s with {Path(plugin).name} "
          f"(sha256 {sha[:16]}): " + ", ".join(f"{n} {'PASS' if ok else 'FAIL'}" for n, ok in results))
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
