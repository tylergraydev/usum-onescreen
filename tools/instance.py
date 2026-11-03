"""Isolated Azahar instances, so several emulator tests can run at once.

Usage: py -3 -I tools/instance.py COMMAND NAME [options]

  create NAME [--force]     make analysis/private/instances/NAME/
  install NAME [3GX]        copy a built plugin in (default: ./usum-onescreen.3gx)
  start NAME [--rom PATH] [--wait SECONDS] [--movie CTM] [--no-plugin] [--no-queue]
                            launch Ultra Sun; with --wait, wait for the plugin's
                            boot log and print it; --movie plays an input movie
                            (tools/movie.py); --no-plugin turns the 3GX plugin
                            loader off for this run (each start sets it).
                            Waits first while USUM_EMU_SLOTS (default 4) instance
                            emulators already run, from any checkout or worktree;
                            --no-queue skips that wait
  stop NAME                 stop that instance's own Azahar process
  status [NAME]             show instances and whether they are running, and
                            every running instance emulator on this PC
  sdmc NAME                 print the instance's emulated SD path

Azahar (checked against 2126.2 and azahar-emu/azahar src/common/file_util.cpp)
has no user-dir flag or environment variable: on Windows it uses a `user`
folder next to azahar.exe if one exists, else %APPDATA%\\Azahar. So each
instance is a copy of the Azahar install with its own `user` folder: config,
nand, sysdata and sdmc copied from %APPDATA%\\Azahar, holding only the Ultra
Sun v1.2 update, a copy of its save and extdata, and the plugin. Nothing here
writes to %APPDATA%\\Azahar.
"""
import argparse
import ctypes
import json
import msvcrt
import os
import re
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INSTANCES = REPO / "analysis" / "private" / "instances"
AZAHAR_DIR = Path(r"C:\Program Files\Azahar")
APPDATA = Path(os.environ["APPDATA"]) / "Azahar"
ROM = Path("H:/3DS Roms/POKUS-WORLD-(Update1.2)-Ziperto/"
           "00040000001B5000 Pokemon Ultra Sun (CTR-P-A2AA) (v0.0.0) (W).piratelegit (1)-decrypted.cci")
TITLE = "00040000001B5000"
SD_ID = Path("Nintendo 3DS") / ("0" * 32) / ("0" * 32)
# Paths under the SD ID folder that Ultra Sun needs: the v1.2 update, the save, the extdata.
SD_COPIES = [Path("title/0004000e/001b5000"), Path("title/00040000/001b5000"),
             Path("extdata/00000000/00001B50")]
# How many instance emulators may run at once on this PC, across every
# checkout and worktree; start waits for a free one. The lock file serialises
# that check, so it lives outside the repo, where all worktrees see it.
EMU_SLOTS = int(os.environ.get("USUM_EMU_SLOTS", "4"))
EMU_LOCK = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "usum-onescreen" / "emulator-queue.lock"
INSTANCE_EXE = re.compile(r"[\\/]analysis[\\/]private[\\/]instances[\\/][^\\/]+[\\/]azahar[\\/]azahar\.exe$", re.I)


def instance_dir(name):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        sys.exit(f"bad instance name '{name}' (letters, digits, _ and - only)")
    return INSTANCES / name


def exe_path(name):
    return instance_dir(name) / "azahar" / "azahar.exe"


def user_dir(name):
    return instance_dir(name) / "azahar" / "user"


def sdmc_dir(name):
    return user_dir(name) / "sdmc"


def state_path(name):
    return instance_dir(name) / "instance.json"


def set_ini(text, section, key, value):
    """Set key=value in [section], with key\\default=false so Azahar reads it."""
    lines = text.splitlines()
    try:
        start = lines.index(f"[{section}]")
    except ValueError:
        lines += ["", f"[{section}]"]
        start = len(lines) - 1
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("[")), len(lines))
    body = [l for l in lines[start + 1:end]
            if not l.startswith((f"{key}=", f"{key}\\default="))]
    while body and not body[-1]:
        body.pop()
    body += [f"{key}\\default=false", f"{key}={value}", ""]
    return "\n".join(lines[:start + 1] + body + lines[end:]) + "\n"


def write_config(name):
    src = APPDATA / "config"
    dst = user_dir(name) / "config"
    shutil.copytree(src, dst)
    user = str(user_dir(name)).replace("\\", "/") + "/"
    ini = dst / "qt-config.ini"
    text = ini.read_text(encoding="utf-8")
    # Point any stored path at this instance instead of %APPDATA%\Azahar.
    # Azahar writes them with mixed separators: C:\\Users\\...\\Roaming/Azahar/nand/
    roaming = str(APPDATA.parent)
    for old in (roaming.replace("\\", "\\\\") + "/Azahar/", roaming.replace("\\", "/") + "/Azahar/"):
        text = text.replace(old, user)
    for section, key, value in [("Utility", "custom_textures", "false"),
                                ("Utility", "preload_textures", "false"),
                                ("System", "plugin_loader", "true"),
                                ("System", "allow_plugin_loader", "true"),
                                ("Data%20Storage", "use_custom_storage", "false"),
                                ("Miscellaneous", "check_for_update_on_start", "false")]:
        text = set_ini(text, section, key, value)
    ini.write_text(text, encoding="utf-8")
    # Per-game files may turn custom textures back on; force them off there too.
    for game in (dst / "custom").glob("*.ini"):
        g = game.read_text(encoding="utf-8")
        g = re.sub(r"(?m)^custom_textures=.*$", "custom_textures=false", g)
        g = re.sub(r"(?m)^custom_textures\\use_global=.*$", "custom_textures\\\\use_global=true", g)
        game.write_text(g, encoding="utf-8")


def cmd_create(args):
    root = instance_dir(args.name)
    if root.exists():
        if not args.force:
            sys.exit(f"{root} exists (use --force to rebuild it)")
        if running_pid(args.name):
            sys.exit(f"instance '{args.name}' is running; stop it first")
        shutil.rmtree(root)
    if not (AZAHAR_DIR / "azahar.exe").is_file():
        sys.exit(f"Azahar not found at {AZAHAR_DIR}")

    shutil.copytree(AZAHAR_DIR, root / "azahar", ignore=shutil.ignore_patterns("uninst.exe"))
    user = user_dir(args.name)
    user.mkdir()
    write_config(args.name)
    for sub in ("nand", "sysdata"):
        shutil.copytree(APPDATA / sub, user / sub)
    for rel in SD_COPIES:
        src = APPDATA / "sdmc" / SD_ID / rel
        if not src.is_dir():
            sys.exit(f"missing {src}")
        shutil.copytree(src, sdmc_dir(args.name) / SD_ID / rel)
    (sdmc_dir(args.name) / "luma" / "plugins" / TITLE).mkdir(parents=True)
    print(root)


def cmd_install(args):
    plugin = Path(args.plugin) if args.plugin else REPO / "usum-onescreen.3gx"
    if not plugin.is_file():
        sys.exit(f"no plugin at {plugin} (run make first)")
    if not user_dir(args.name).is_dir():
        sys.exit(f"no instance '{args.name}' (run create first)")
    dst = sdmc_dir(args.name) / "luma" / "plugins" / TITLE / "usum-onescreen.3gx"
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(plugin, dst)
    print(dst)


# --- processes (Win32 through ctypes, stdlib only) ---

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.restype = wintypes.HANDLE
PROCESS_TERMINATE = 0x0001
SYNCHRONIZE = 0x00100000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
STILL_ACTIVE = 259


def process_info(pid):
    """(exe path, creation time) of a live process, or None."""
    h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return None
    try:
        code = wintypes.DWORD()
        if not kernel32.GetExitCodeProcess(h, ctypes.byref(code)) or code.value != STILL_ACTIVE:
            return None
        buf = ctypes.create_unicode_buffer(32768)
        size = wintypes.DWORD(len(buf))
        if not kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            return None
        times = [wintypes.FILETIME() for _ in range(4)]
        kernel32.GetProcessTimes(h, *[ctypes.byref(t) for t in times])
        created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        return buf.value, created
    finally:
        kernel32.CloseHandle(h)


psapi = ctypes.WinDLL("psapi", use_last_error=True)


def instance_emulators():
    """(pid, exe) of every running azahar.exe that is an instance copy made by
    this tool, in any checkout or worktree. The user's own Azahar is not one."""
    pids = (wintypes.DWORD * 4096)()
    needed = wintypes.DWORD()
    if not psapi.EnumProcesses(pids, ctypes.sizeof(pids), ctypes.byref(needed)):
        return []
    found = []
    for pid in pids[:needed.value // ctypes.sizeof(wintypes.DWORD)]:
        info = process_info(pid) if pid else None
        if info and INSTANCE_EXE.search(info[0]):
            found.append((pid, info[0]))
    return found


def wait_for_slot(name):
    """Hold the shared lock until fewer than EMU_SLOTS instance emulators run.
    Returns the open lock file; the caller closes it once its Azahar is up."""
    EMU_LOCK.parent.mkdir(parents=True, exist_ok=True)
    lock = open(EMU_LOCK, "a+")
    told = False
    while True:
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            break
        except OSError:
            if not told:
                print(f"'{name}': waiting for the emulator queue lock", flush=True)
                told = True
            time.sleep(1)
    told = False
    while True:
        running = instance_emulators()
        if len(running) < EMU_SLOTS:
            return lock
        if not told:
            print(f"'{name}': {len(running)} of {EMU_SLOTS} emulator slots in use, waiting", flush=True)
            told = True
        time.sleep(2)


def release_slot(lock):
    try:
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    except OSError:
        pass
    lock.close()


def same_path(a, b):
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def running_pid(name):
    """The recorded PID if it is still this instance's own azahar.exe."""
    try:
        state = json.loads(state_path(name).read_text())
    except (OSError, ValueError):
        return None
    info = process_info(state.get("pid", 0))
    if info and same_path(info[0], exe_path(name)) and info[1] == state.get("created"):
        return state["pid"]
    return None


def cmd_start(args):
    if not exe_path(args.name).is_file():
        sys.exit(f"no instance '{args.name}' (run create first)")
    if running_pid(args.name):
        sys.exit(f"instance '{args.name}' is already running")
    log = sdmc_dir(args.name) / "usum-onescreen.log"
    log.unlink(missing_ok=True)
    ini = user_dir(args.name) / "config" / "qt-config.ini"
    ini.write_text(set_ini(ini.read_text(encoding="utf-8"), "System", "plugin_loader",
                           "false" if args.no_plugin else "true"), encoding="utf-8")
    command = [str(exe_path(args.name))]
    if args.movie:
        command += ["-p", str(Path(args.movie).resolve())]
    lock = None if args.no_queue else wait_for_slot(args.name)
    try:
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
        proc = subprocess.Popen(command + [str(args.rom)], cwd=instance_dir(args.name),
                                creationflags=flags, close_fds=True,
                                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        info = process_info(proc.pid)
    finally:
        if lock:
            release_slot(lock)
    if not info:
        sys.exit("Azahar exited at once")
    state_path(args.name).write_text(json.dumps({"pid": proc.pid, "created": info[1], "exe": info[0]}))
    print(f"started '{args.name}' pid {proc.pid}")

    if args.wait:
        deadline = time.time() + args.wait
        while time.time() < deadline:
            text = log.read_text(errors="replace") if log.exists() else ""
            if "hooks:" in text or "unsupported game" in text:
                print(text, end="" if text.endswith("\n") else "\n")
                return
            if not running_pid(args.name):
                sys.exit("Azahar exited before the plugin wrote its log")
            time.sleep(0.5)
        sys.exit(f"no plugin boot log within {args.wait} s ({log})")


def cmd_stop(args):
    pid = running_pid(args.name)
    if not pid:
        print(f"instance '{args.name}' is not running")
        state_path(args.name).unlink(missing_ok=True)
        return
    # running_pid() checked the exe path and start time, so this is our own process.
    h = kernel32.OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, False, pid)
    if not h:
        sys.exit(f"cannot open pid {pid}: error {ctypes.get_last_error()}")
    try:
        if not kernel32.TerminateProcess(h, 0):
            sys.exit(f"cannot stop pid {pid}: error {ctypes.get_last_error()}")
        kernel32.WaitForSingleObject(h, 10000)
    finally:
        kernel32.CloseHandle(h)
    state_path(args.name).unlink(missing_ok=True)
    print(f"stopped '{args.name}' pid {pid}")


def cmd_status(args):
    names = [args.name] if args.name else sorted(
        p.name for p in INSTANCES.glob("*") if (p / "azahar" / "azahar.exe").is_file())
    for name in names:
        pid = running_pid(name)
        print(f"{name}: {'running pid ' + str(pid) if pid else 'stopped'}  {instance_dir(name)}")
    running = instance_emulators()
    print(f"emulator slots: {len(running)} of {EMU_SLOTS} in use on this PC")
    for pid, exe in running:
        print(f"  pid {pid}  {exe}")


def cmd_sdmc(args):
    print(sdmc_dir(args.name))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("create")
    p.add_argument("name")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_create)
    p = sub.add_parser("install")
    p.add_argument("name")
    p.add_argument("plugin", nargs="?")
    p.set_defaults(func=cmd_install)
    p = sub.add_parser("start")
    p.add_argument("name")
    p.add_argument("--rom", default=ROM)
    p.add_argument("--wait", type=float, default=0)
    p.add_argument("--movie")
    p.add_argument("--no-plugin", action="store_true")
    p.add_argument("--no-queue", action="store_true", help="don't wait for a free emulator slot")
    p.set_defaults(func=cmd_start)
    for cmd, func in (("stop", cmd_stop), ("sdmc", cmd_sdmc)):
        p = sub.add_parser(cmd)
        p.add_argument("name")
        p.set_defaults(func=func)
    p = sub.add_parser("status")
    p.add_argument("name", nargs="?")
    p.set_defaults(func=cmd_status)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
