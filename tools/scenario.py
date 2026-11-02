"""Run scripted emulator tests (scenarios) against an isolated Azahar instance.

Usage: py -3 -I tools/scenario.py --instance NAME [--keep] [-v] SCENARIO [SCENARIO ...]
       py -3 -I tools/scenario.py --check SCENARIO [...]   (parse only, no emulator)

SCENARIO is a file, or a name in scenarios/ (scenarios/NAME.txt). One step per
line; # starts a comment (at the start of a line, or after a space; not in
expect-log / reject-log lines, where # is part of the regex). Steps:

  start [WAIT]                 tools/instance.py start NAME --wait WAIT (default 90)
  stop                         tools/instance.py stop NAME
  send CMD [; CMD ...]         plugin test commands, as tools/cmd.py takes them
  sleep SECONDS
  shot LABEL                   screenshot -> analysis/private/shots/<scenario>-LABEL-top/bottom.png
  wait-scene SCENE [TIMEOUT]   until the last "scene:" log line is SCENE (default 60 s)
  expect-scene SCENE           the last "scene:" log line is SCENE now
  expect-mode MODE [TIMEOUT]   the last "panel: mode" log line is MODE (off|panel|full), default 5 s
  mark                         later expect-log / reject-log only look at log lines after here
  expect-log REGEX [TIMEOUT]   a log line since the mark matches REGEX (default 10 s; a
                               trailing number is always read as the timeout)
  reject-log REGEX             no log line since the mark matches REGEX
  check-save                   the real save is unchanged since the run began (also done at the end)
  if-scene SCENE STEP          run the one STEP only if the scene is SCENE now
  include FILE                 the steps of FILE (next to this one, or in scenarios/; .txt optional)
  repeat N [until-scene SCENE] the steps up to the matching `end`, N times; with until-scene,
  ...                          stop as soon as that is the scene (checked before each step)
  end                          and fail if it never was

"The scene" is the last `scene:` line in the plugin log (an empty one counts
as (none), the loading gap between scenes); "the mode" is the last
`panel: mode` line. The real save is
%APPDATA%\\Azahar\\sdmc\\...\\title\\00040000\\001b5000\\data\\00000001\\main; its
mtime and SHA-256 are recorded when a scenario begins and compared at its end.

`start` resets the mark (the instance starts a fresh log). The first failing
step stops the scenario: its line is printed, a <scenario>-FAIL screenshot is
taken if the plugin answers, and the last screenshot paths are listed. An
instance that this run started is stopped on failure unless --keep. Exit code
0 if every scenario passed, else 1.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import emu  # noqa: E402

SCENARIOS = emu.REPO / "scenarios"
INSTANCE_PY = Path(__file__).resolve().parent / "instance.py"
REAL_SAVE = (Path(os.environ.get("APPDATA", ".")) / "Azahar" / "sdmc" / "Nintendo 3DS" / ("0" * 32) / ("0" * 32)
             / "title" / "00040000" / "001b5000" / "data" / "00000001" / "main")


class StepFailed(Exception):
    pass


class Step:
    def __init__(self, where, text, op, args, body=None):
        self.where, self.text, self.op, self.args, self.body = where, text, op, args, body


# op -> (min args, max args); None = the rest of the line is one argument
ARITY = {"start": (0, 1), "stop": (0, 0), "send": None, "sleep": (1, 1), "shot": (1, 1),
         "wait-scene": (1, 2), "expect-scene": (1, 1), "expect-mode": (1, 2), "mark": (0, 0),
         "expect-log": None, "reject-log": None, "check-save": (0, 0), "repeat": (1, 3),
         "include": (1, 1), "if-scene": None}


def include_path(name, base):
    for p in (base / name, base / f"{name}.txt", SCENARIOS / name, SCENARIOS / f"{name}.txt"):
        if p.is_file():
            return p
    return None


def parse_step(text, where):
    """One step line (not end/include) -> Step."""
    op, _, rest = text.partition(" ")
    rest = rest.strip()
    if op not in ARITY or op in ("end", "include"):
        raise SyntaxError(f"{where}: unknown step '{op}'")
    if ARITY[op] is None:
        if not rest:
            raise SyntaxError(f"{where}: {op} needs an argument")
        args = [rest]
    else:
        args = rest.split()
        lo, hi = ARITY[op]
        if not lo <= len(args) <= hi:
            raise SyntaxError(f"{where}: {op} takes {lo}-{hi} arguments")
    step = Step(where, text, op, args)
    if op == "repeat":
        if not args[0].isdigit() or (len(args) > 1 and (args[1] != "until-scene" or len(args) != 3)):
            raise SyntaxError(f"{where}: use 'repeat N' or 'repeat N until-scene SCENE'")
        step.body = []
    elif op == "if-scene":
        scene, _, inner = rest.partition(" ")
        if not inner.strip() or inner.split()[0] in ("repeat", "if-scene"):
            raise SyntaxError(f"{where}: use 'if-scene SCENE STEP' with a single step")
        step.args = [scene]
        step.body = [parse_step(inner.strip(), where)]
    elif op == "expect-log":
        m = re.fullmatch(r"(.*?)\s+(\d+(?:\.\d+)?)", rest)
        step.args = [m.group(1), m.group(2)] if m else [rest]
    if op in ("expect-log", "reject-log"):
        try:
            re.compile(step.args[0])
        except re.error as e:
            raise SyntaxError(f"{where}: bad regex: {e}")
    return step


def parse(path, seen=()):
    """Return the scenario's steps; repeat and if-scene steps hold theirs in .body."""
    if path.resolve() in seen:
        raise SyntaxError(f"{path.name}: includes itself")
    seen = seen + (path.resolve(),)
    root = []
    stack = [root]
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        # Comments: a line starting with #, or " # ..." after a step (not in a regex step).
        text = raw.strip()
        if not text.startswith(("expect-log", "reject-log")):
            text = re.split(r"(?:^|\s)#", text, maxsplit=1)[0].strip()
        if not text:
            continue
        where = f"{path.name}:{lineno}"
        op, _, rest = text.partition(" ")
        if op == "end":
            if len(stack) == 1:
                raise SyntaxError(f"{where}: end without repeat")
            stack.pop()
        elif op == "include":
            inc = include_path(rest.strip(), path.parent) if rest.strip() else None
            if inc is None:
                raise SyntaxError(f"{where}: no file to include for '{rest.strip()}'")
            stack[-1].extend(parse(inc, seen))
        else:
            step = parse_step(text, where)
            stack[-1].append(step)
            if op == "repeat":
                stack.append(step.body)
    if len(stack) != 1:
        raise SyntaxError(f"{path.name}: repeat without end")
    return root


def save_fingerprint():
    if not REAL_SAVE.is_file():
        return None
    return REAL_SAVE.stat().st_mtime_ns, hashlib.sha256(REAL_SAVE.read_bytes()).hexdigest()


def describe_fingerprint(fp):
    if fp is None:
        return "missing"
    return f"mtime {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(fp[0] / 1e9))}, sha256 {fp[1][:16]}"


def last_match(pattern, text):
    found = re.findall(pattern, text, re.M)
    return found[-1] if found else None


class Runner:
    def __init__(self, name, instance, verbose):
        self.name = name
        self.instance = instance
        self.verbose = verbose
        self.sdmc = emu.sdmc_path(instance)
        self.mark = 0
        self.shots = []
        self.started = False
        self.save = save_fingerprint()

    def log(self):
        return emu.read_log(self.sdmc)

    def scene(self):
        """The last scene the log names; an empty `scene` answer counts as (none)."""
        scene = last_match(r"^scene: (.*?)[ \t]*$", self.log())
        return "(none)" if scene == "" else scene

    def instance_py(self, *args):
        proc = subprocess.run([sys.executable, "-I", str(INSTANCE_PY), *args], capture_output=True, text=True)
        out = (proc.stdout + proc.stderr).strip()
        if out and self.verbose:
            print("      " + out.replace("\n", "\n      "))
        if proc.returncode != 0:
            raise StepFailed(out.splitlines()[-1] if out else f"instance.py {args[0]} failed")
        return out

    def poll(self, timeout, check, what):
        deadline = time.time() + timeout
        while True:
            if check():
                return
            if time.time() >= deadline:
                raise StepFailed(what())
            time.sleep(0.25)

    def run_steps(self, steps):
        for step in steps:
            self.run_step(step)

    def run_step(self, step, quiet=False):
        if not quiet or self.verbose:
            print(f"  {step.where}  {step.text}", flush=True)
        op, args = step.op, step.args
        try:
            if op == "start":
                out = self.instance_py("start", self.instance, "--wait", args[0] if args else "90")
                self.started = True
                self.mark = 0
                if "hooks: all ok" not in out:
                    raise StepFailed("plugin boot log has no 'hooks: all ok'")
            elif op == "stop":
                self.instance_py("stop", self.instance)
                self.started = False
            elif op == "send":
                out = emu.send([c.strip() for c in args[0].split(";") if c.strip()], self.sdmc)
                if self.verbose:
                    print("      " + out.rstrip().replace("\n", "\n      "))
                bad = re.findall(r"^cmd: unknown.*$", out, re.M)
                if bad:
                    raise StepFailed(bad[0])
            elif op == "sleep":
                time.sleep(float(args[0]))
            elif op == "shot":
                paths = emu.shot(f"{self.name}-{args[0]}", self.sdmc)
                self.shots.append(paths)
                if self.verbose:
                    print("      " + "  ".join(str(p) for p in paths))
            elif op == "wait-scene":
                self.poll(float(args[1]) if len(args) > 1 else 60, lambda: self.scene() == args[0],
                          lambda: f"scene is {self.scene()}, not {args[0]}")
            elif op == "expect-scene":
                if self.scene() != args[0]:
                    raise StepFailed(f"scene is {self.scene()}, not {args[0]}")
            elif op == "expect-mode":
                mode = lambda: last_match(r"panel: mode (\w+)", self.log())  # noqa: E731
                self.poll(float(args[1]) if len(args) > 1 else 5, lambda: mode() == args[0],
                          lambda: f"panel mode is {mode()}, not {args[0]}")
            elif op == "mark":
                self.mark = len(self.log())
            elif op == "expect-log":
                since = lambda: self.log()[self.mark:]  # noqa: E731
                self.poll(float(args[1]) if len(args) > 1 else 10,
                          lambda: re.search(args[0], since(), re.M) is not None,
                          lambda: f"no log line matching /{args[0]}/")
            elif op == "reject-log":
                m = re.search(r"^.*(?:" + args[0] + r").*$", self.log()[self.mark:], re.M)
                if m:
                    raise StepFailed(f"log line matches /{args[0]}/: {m.group(0)}")
            elif op == "if-scene":
                if self.scene() == args[0]:
                    self.run_step(step.body[0], quiet)
            elif op == "check-save":
                self.check_save()
            elif op == "repeat":
                # Steps print in the first round only (all rounds with -v).
                until = args[2] if len(args) == 3 else None
                rounds = 0
                for rounds in range(1, int(args[0]) + 1):
                    for sub in step.body:
                        if until and self.scene() == until:
                            break
                        self.run_step(sub, quiet=quiet or rounds > 1)
                    if until and self.scene() == until:
                        break
                if until and self.scene() != until:
                    raise StepFailed(f"scene is {self.scene()}, not {until}, after {args[0]} rounds")
                if not quiet:
                    print(f"  {step.where}  end of repeat after {rounds} round(s)"
                          + (f", scene {until}" if until else ""), flush=True)
        except emu.PluginTimeout as e:
            raise StepFailed(str(e))
        except StepFailed as e:
            if not hasattr(e, "step"):
                e.step = step
            raise

    def check_save(self):
        now = save_fingerprint()
        if self.save is None:
            raise StepFailed(f"real save not found at {REAL_SAVE}")
        if now != self.save:
            raise StepFailed(f"real save changed: was {describe_fingerprint(self.save)}, "
                             f"now {describe_fingerprint(now)}")


def run(path, instance, keep, verbose):
    name = path.stem
    print(f"scenario {name} ({path})")
    try:
        steps = parse(path)
    except SyntaxError as e:
        print(f"FAIL {name}: {e}")
        return False
    runner = Runner(name, instance, verbose)
    print(f"  real save: {describe_fingerprint(runner.save)}")
    t0 = time.time()
    try:
        runner.run_steps(steps)
        runner.check_save()
    except StepFailed as e:
        step = getattr(e, "step", None)
        where = f"{step.where} '{step.text}'" if step else "end of scenario"
        print(f"FAIL {name} at {where}: {e}")
        try:
            runner.shots.append(emu.shot(f"{name}-FAIL", runner.sdmc, timeout=5))
        except (emu.PluginTimeout, OSError, ValueError):
            pass
        if runner.shots:
            print("  last screenshots:")
            for paths in runner.shots[-3:]:
                for p in paths:
                    print(f"    {p}")
        scene = runner.scene()
        print(f"  last scene in log: {scene}")
        if runner.started and not keep:
            try:
                runner.instance_py("stop", instance)
                print(f"  stopped instance '{instance}'")
            except StepFailed as stop_error:
                print(f"  could not stop instance '{instance}': {stop_error}")
        try:
            runner.check_save()
        except StepFailed as save_error:
            print(f"  {save_error}")
        return False
    print(f"PASS {name} in {time.time() - t0:.0f} s (real save unchanged: {describe_fingerprint(runner.save)})")
    return True


def resolve(arg):
    p = Path(arg)
    if p.is_file():
        return p
    p = SCENARIOS / f"{arg}.txt"
    if p.is_file():
        return p
    sys.exit(f"no scenario '{arg}' (not a file, and no {p})")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--instance", help="instance name (tools/instance.py)")
    parser.add_argument("--check", action="store_true", help="only parse the scenarios (no emulator)")
    parser.add_argument("--keep", action="store_true", help="leave the instance running on failure")
    parser.add_argument("-v", "--verbose", action="store_true", help="print plugin output and shot paths")
    parser.add_argument("scenarios", nargs="+")
    args = parser.parse_args()
    if args.check:
        ok = True
        for p in [resolve(s) for s in args.scenarios]:
            try:
                print(f"{p.name}: {len(parse(p))} steps")
            except SyntaxError as e:
                print(f"{p.name}: {e}")
                ok = False
        sys.exit(0 if ok else 1)
    if not args.instance:
        parser.error("--instance is required (or --check)")
    if not emu.sdmc_path(args.instance).is_dir():
        sys.exit(f"no instance '{args.instance}' (see tools/instance.py)")
    paths = [resolve(s) for s in args.scenarios]
    results = [(p.stem, run(p, args.instance, args.keep, args.verbose)) for p in paths]
    if len(results) > 1:
        print("summary: " + ", ".join(f"{n} {'PASS' if ok else 'FAIL'}" for n, ok in results))
    sys.exit(0 if all(ok for _, ok in results) else 1)


if __name__ == "__main__":
    main()
