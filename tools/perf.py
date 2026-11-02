"""Measure what the bottom-screen panel costs, per panel mode (see docs/PERF.md).

Usage: py -3 -I tools/perf.py --instance NAME [--modes off,panel,full]
           [--rounds 2] [--windows 6] [--seconds 10] [--csv OUT]

For each round and mode: sets the mode, lets it settle, then takes WINDOWS
timing windows of SECONDS each with the plugin's `perf` command. Each window
pairs the plugin's numbers (emulated time from svcGetSystemTick, top and
bottom frames, capture and draw time) with the PC's wall-clock time between the
moments the plugin picked up the command file. Prints one line per window and a
summary (mean and standard deviation per mode). Auto panel is turned off so the
mode stays as set; turn it back on with `cmd.py "auto on"` if needed.
"""
import argparse
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

PERF = re.compile(r"perf: (?P<state>\w+), mode (?P<mode>\w+), window (?P<window>\d+) us, "
                  r"top frames (?P<top>\d+), bottom frames (?P<bottom>\d+), "
                  r"capture n (?P<cn>\d+) mean (?P<cmean>\d+) us max (?P<cmax>\d+) us, "
                  r"draw n (?P<dn>\d+) mean (?P<dmean>\d+) us max (?P<dmax>\d+) us")


def sdmc_for(name):
    sdmc = (Path(__file__).resolve().parent.parent / "analysis" / "private" / "instances"
            / name / "azahar" / "user" / "sdmc")
    if not sdmc.is_dir():
        sys.exit(f"no instance '{name}' (see tools/instance.py)")
    return sdmc


def azahar_count():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq azahar.exe", "/NH"],
                         capture_output=True, text=True).stdout
    return sum(1 for line in out.splitlines() if line.lower().startswith("azahar.exe"))


class Plugin:
    def __init__(self, sdmc):
        self.commands = sdmc / "usum-cmd.txt"
        self.log = sdmc / "usum-onescreen.log"

    def send(self, *commands):
        """Writes the command file and returns (wall time it was picked up, new log text)."""
        before = self.log.stat().st_size if self.log.exists() else 0
        tmp = self.commands.with_suffix(".tmp")
        tmp.write_text("\n".join(commands) + "\n")
        os.replace(tmp, self.commands)
        deadline = time.time() + 15
        while self.commands.exists():
            if time.time() > deadline:
                self.commands.unlink(missing_ok=True)
                sys.exit("no answer from the plugin within 15 s")
            time.sleep(0.002)
        picked = time.perf_counter()
        # The plugin logs right after removing the file; wait for its lines.
        text = ""
        for _ in range(100):
            time.sleep(0.02)
            with open(self.log, "rb") as f:
                f.seek(before)
                text = f.read().decode(errors="replace")
            if "cmd: " + commands[-1] in text:
                break
        return picked, text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--instance", required=True)
    ap.add_argument("--modes", default="off,panel,full")
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--windows", type=int, default=6)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--settle", type=float, default=3)
    ap.add_argument("--csv")
    args = ap.parse_args()

    plugin = Plugin(sdmc_for(args.instance))
    plugin.send("auto off", "perf on")
    rows = []
    for rnd in range(args.rounds):
        for mode in args.modes.split(","):
            plugin.send(f"mode {mode}")
            time.sleep(args.settle)
            last, _ = plugin.send("perf")      # starts the first window
            for _ in range(args.windows):
                time.sleep(args.seconds)
                now, text = plugin.send("perf")
                m = PERF.search(text)
                if not m:
                    sys.exit("no perf line in:\n" + text)
                d = {k: (int(v) if v.isdigit() else v) for k, v in m.groupdict().items()}
                wall = now - last
                last = now
                emu = d["window"] / 1e6
                row = dict(round=rnd + 1, mode=d["mode"], wall_s=wall, emu_s=emu, speed=emu / wall,
                           top_fps_wall=d["top"] / wall, top_fps_emu=d["top"] / emu,
                           bottom_fps_wall=d["bottom"] / wall,
                           capture_mean_us=d["cmean"], capture_max_us=d["cmax"],
                           draw_mean_us=d["dmean"], draw_max_us=d["dmax"],
                           panel_ms_per_s_emu=(d["cn"] * d["cmean"] + d["dn"] * d["dmean"]) / 1000 / emu,
                           azahar=azahar_count())
                rows.append(row)
                print(f"r{row['round']} {row['mode']:5} wall {wall:6.2f}s emu {emu:6.2f}s speed {row['speed']:5.3f} "
                      f"top {row['top_fps_wall']:5.1f}/s wall {row['top_fps_emu']:5.1f}/s emu "
                      f"capture {d['cmean']:5d}/{d['cmax']:5d} us draw {d['dmean']:5d}/{d['dmax']:5d} us "
                      f"azahar {row['azahar']}", flush=True)

    if args.csv:
        keys = list(rows[0].keys())
        with open(args.csv, "w") as f:
            f.write(",".join(keys) + "\n")
            for r in rows:
                f.write(",".join(f"{r[k]:.4f}" if isinstance(r[k], float) else str(r[k]) for k in keys) + "\n")

    print("\nmode   n  speed (mean sd)   top fps wall   top fps emu   capture us   draw us   panel ms per emu s")
    for mode in args.modes.split(","):
        rs = [r for r in rows if r["mode"] == mode]
        def ms(k):
            v = [r[k] for r in rs]
            return f"{statistics.mean(v):7.2f} {statistics.stdev(v) if len(v) > 1 else 0:5.2f}"
        print(f"{mode:5} {len(rs):2}  {ms('speed')}  {ms('top_fps_wall')}  {ms('top_fps_emu')}  "
              f"{ms('capture_mean_us')}  {ms('draw_mean_us')}  {ms('panel_ms_per_s_emu')}")


if __name__ == "__main__":
    main()
