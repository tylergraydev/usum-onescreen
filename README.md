# USUM One Screen

A 3GX plugin (CTRPluginFramework) that makes Pokémon Ultra Sun / Ultra Moon playable on the top screen only, in Azahar with the 3GX plugin loader enabled. Plan: https://claude.ai/artifact/LxD6pmGFR8x91NrRRF8P7R

**Status:** Phase 2 under way. The goal is native menus on the top screen; the X menu (`Sources/Screens/XMenu.cpp`), the nickname keyboard (`Sources/Screens/Keyboard.cpp`), the Party screen with the summary (`Sources/Screens/PartyScreen.cpp`) and the PC boxes (`Sources/Screens/Boxes.cpp`) have one. Until every screen has one, **L+R** shows the bottom screen on the top screen as a half-size panel or a full view (`Sources/Panel.cpp`, copied on the emulated GPU), and a touch cursor (`Sources/Cursor.cpp`) on it: **C-stick** moves it, **ZL** touches. The panel can also follow the scene automatically (`auto = on`, `Sources/AutoPanel.cpp`). See [docs/PLAYING.md](docs/PLAYING.md).

**Players:** see [docs/PLAYING.md](docs/PLAYING.md) for install, controls and bug reports, and [CHANGELOG.md](CHANGELOG.md) for what's new.

## Targets

| Game | Title ID | Version | Confirmed |
|---|---|---|---|
| Ultra Sun (USA) | `00040000001B5000` | v1.2 = title version `0x0800` (2048) | Yes, in Azahar 2026-11-02 (matches the update's TMD) |
| Ultra Moon (USA) | `00040000001B5100` | assumed `0x0800` | No local dump |

On each boot the plugin writes one line to `usum-onescreen.log` at the emulated SD root (`%APPDATA%\Azahar\sdmc\`) and shows the same text as an on-screen notice. On an unsupported title/version that line says `unsupported game <titleid> v<version>` and the plugin does nothing else.

Azahar's custom texture option crashes Ultra Sun at boot with the installed HD texture pack (`Surface is not suitable for custom upload`), with or without this plugin. Keep custom textures off while testing.

No game code, assets, or keys go in this repo (`.gitignore` blocks the common file types).

## Build (Windows)

1. Install devkitPro with the 3DS packages (devkitARM, libctru) using the devkitPro Windows installer.
2. Open `C:\devkitPro\msys2\msys2_shell.bat` and add the ThePixellizerOSS package repo (from the CTRPluginFramework README):

   ```bash
   if ! grep -Fxq "[thepixellizeross-lib]" /etc/pacman.conf; then echo -e "\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional" | tee -a /etc/pacman.conf > /dev/null; fi; if ! grep -Fxq "[thepixellizeross-win]" /etc/pacman.conf; then echo -e "\n[thepixellizeross-win]\nServer = https://thepixellizeross.gitlab.io/packages/x86_64/win\nSigLevel = Optional" | tee -a /etc/pacman.conf > /dev/null; fi
   ```

3. Install the framework and the 3gx packer:

   ```bash
   pacman -Sy
   pacman -S libctrpf 3gxtool
   ```

   `libctrpf` is the documented package name. `3gxtool` is assumed; if pacman can't find it, list what the repo has with `pacman -Sl thepixellizeross-win` and install the matching package, or point the Makefile at an existing binary with `make TOOL=/path/to/3gxtool`.
4. From this folder (no spaces in the path), run `make`. Output: `usum-onescreen.3gx`.

CI (`.github/workflows/build.yml`) does the same in the `devkitpro/devkitarm` container and uploads the `.3gx` as a build artifact. `tools/ci/install-deps.sh` syncs only the ThePixellizerOSS repos (pkg.devkitpro.org's `-Sy` sometimes answered 403, and the image already has everything else), retries with backoff, and falls back to the two package files kept in the Actions cache by an earlier run; the `deps_offline` input of a manual run tests that fallback.

## Run in Azahar

1. Configure → System → enable **3GX plugin loader**.
2. Copy `usum-onescreen.3gx` into Azahar's emulated SD card at `luma/plugins/00040000001B5000/` (Ultra Sun). This is the Luma plugin folder layout; check it against your Azahar setup.
3. Start the game. You should see a `USUM One Screen: ...` notice.

## Settings (`usum-onescreen.ini`)

At boot the plugin reads `usum-onescreen.ini` at the emulated SD root (next to the log). If it is missing, the plugin writes a commented default file there. The defaults are shown below: the bottom-screen view stays off until **L+R**.

```ini
[controls]
panel_key = L+R         ; cycles off / panel / full; A B X Y L R ZL ZR Start Select Up Down Left Right, L+R for a combo, or none
touch_key = ZL          ; touches at the cursor while held
cursor_move = cstick    ; cstick, dpad or circlepad (dpad / circlepad still reach the game too)
cursor_speed = 6        ; top speed in px/frame, 1-20; starts at 1, then 3 (or less), then this

[panel]
size = 1/2              ; 1/2 or 2/3 (easier to read, costs more speed: docs/PERF.md)
corner = bottom-right   ; bottom-right, bottom-left, top-right, top-left
auto = off              ; on: the view follows [scenes]; off (default): it stays as panel_key sets it

[scenes]                ; off, panel or full per scene ID (as logged: "scene: ...")
TitleMenu = off
FieldRo = off           ; overworld
FieldRo.xmenu = panel   ; the X menu, open over the overworld
Zukan = panel           ; Pokédex
Battle = panel
main:00647FCC = full    ; Party
main:006488C8 = panel   ; Bag
Status = panel          ; Pokémon summary
Box = full              ; PC boxes
TownMap = full
TrainerPass = panel
Config = full           ; Options
Kawaigari = full        ; Pokémon Refresh
BattleVideoPlayer = panel
default = panel         ; any scene not listed
```

- Comments start with `;` or `#`, anywhere on a line. Names and values are case-insensitive; spaces, CRLF and a UTF-8 BOM are fine.
- Unknown keys, unknown sections and bad values are skipped (the default stays) and logged as `settings: usum-onescreen.ini line N: ...`. The log also gets one `settings: ...` summary of what is in use.
- Any scene ID can go in `[scenes]`, including ones not listed yet (scene IDs are in `docs/OFFSETS.md` and in the log).
- The parser and settings model (`Sources/Settings.cpp`) are plain C++. Host tests: `make -C tests` (g++ / clang++; WSL works) builds and runs every test in `tests/`, also in CI.

### Plugin menu

CTRPF opens its menu with **Select** (its default menu hotkey; `Preferences::MenuHotkeys = 0x4` in libctrpf, changeable under Tools → Settings → "Change menu hotkeys"). Entries:

- **Bottom screen on top**: the main feature (on by default).
- **Auto panel: on/off**: A switches auto panel until the next boot or reload. The file is not rewritten.
- **View settings**: shows the settings in use.
- **Reload settings file**: reads `usum-onescreen.ini` again.

## Testing without watching the screen

- `usum-onescreen.log` (emulated SD root) gets the boot line, the hook check, and panel status after ~5 s and on every mode change.
- `py -3 -I tools/cmd.py "move 160 120" "touch on" "tap 10" "touch off" "mode full" "status"` sends test commands to the plugin (via `usum-cmd.txt`) and prints the log lines they produce, including what the game's touch reader last returned.
- Buttons too: `press KEY [FRAMES]` (default 3; menus key-repeat by 8 frames), `hold KEY`, `release KEY`, with KEY one of `a b x y l r start select up down left right`, and `stick X Y [FRAMES]` for the circle pad (walking uses the circle pad, not the D-pad). Injected through the game's pad reader (`0x282548`), so the game sees them as real presses.
- Input capture (for plugin-drawn menus): `capture on [KEY|stick|touch ...]` hides those from the game (no list = everything), `capture off [...]` gives them back (no list = all). A key still held when it is given back stays hidden until released, so the game never sees a fresh press. The status lines show `capture`/`suppressing` (pad) and `touch captured`/`suppressed`.
- The log records every scene change (`scene: FieldRo`, `Zukan`, `Battle`, …); `scene` logs the current one. Memory tools: `snap NAME`, `vtscan NAME`, `peek ADDR [N]` (see `docs/OFFSETS.md`), `memmap` (readable blocks), `dump ADDR SIZE NAME` (hex; raw memory to `/usum-dump-NAME.bin`).
- `battle` logs the battle state (active Pokémon on both sides, moves/PP, which bottom-screen menu is up and its cursor); in battle the same is logged on every change. See `docs/RE1-battle.md`.
- `where` logs the zone (ID, place-name line, indoor), the player's position and facing, and the Rotom map's centre. See `docs/RE4-map.md`.
- `party` logs the party (species, nickname, level, HP, status, held item, moves/PP, and the Party screen's order while it is open); `bag` logs every pocket's items and counts in the game's order, with names. See `docs/RE3-party-bag.md`.
- `box [N]` logs PC box N (default: the one on screen, else 1): name, unlocked boxes, the Pokémon in each slot, and with the PC open its mode, cursor and the screen's own party. See `docs/RE6-boxes.md`.
- `perf on` / `perf off` / `perf`: panel timing (off by default). `perf` logs emulated time, frames, and the mean/max time spent copying the bottom screen and drawing the panel since the last `perf`. `py -3 -I tools/perf.py --instance NAME` runs the measurement in `docs/PERF.md`.
- `battleui on` / `battleui off` turns the native battle menu (on by default) on or off; `battleui` logs its state. While it is on, battles have no panel: the top screen shows the command bar and move list, and every page change and choice is logged (`battle-ui: page …`, `battle-ui: chose …`). See `docs/BATTLE-UI.md`.
- `action NAME` runs a bottom-screen action (`open-pokedex`, `open-party`, `open-bag`, `battle-run`, `close`) with the player's input captured meanwhile; `action status`, `action cancel`, `action list`. Results are logged as `action: NAME ok …` / `FAILED …`. See `docs/ACTIONS.md`.
- `map on|off|open|close|status`: the native map (ZR in the field; B or ZR closes). Logs `map: open`, `shown zone ...`, `now zone ...`, `closed (WHY)`, `hidden`, and once `map: place names: ...`. See `docs/MAP.md`.
- `rotom on|off|status|read|select N|answer [N]|talk|next`: Rotom's field prompts (a chat's question and answers, the Roto Loto, the *Rotom wants to talk* badge; Up starts the chat). Logs `rotom: ready ...`, `choice "..." | 1 "..." | 2 "..."`, `answer N`, `answered N`, `reply "..."`, `roto loto ...`. See `docs/ROTOM-FIELD.md`.
- `partyui on|off|status`: the native Party screen and summary (the game's Party screen, from the X menu, shows on the top screen with the panel off; A opens Summary / Use item / Move, Y moves, B closes). Logs `party-ui: shown`, `party ...`, `cursor N`, `summary ...`, `swap ...`, `used ...`, `hidden`. See `docs/PARTY-UI.md`.
- `xmenu on|off|status`, `xmenu select NAME`, `xmenu open NAME`: the native X menu (X in the field shows it on the top screen with the panel off; D-pad/L/R move, A opens through the action layer, B closes). Logs `xmenu: shown`, `cursor NAME`, `open NAME`, `hidden`. See `docs/XMENU.md`.
- `keyboard on|off|status|read`: the native keyboard (the game's keyboard, e.g. a nickname after a catch, shows on the top screen with the panel off; D-pad moves, A types, B deletes, Y Shift, X space, START to OK). `read` looks for the game's keyboard now and logs its text. Logs `keyboard: shown`, `key NAME`, `text "…" (n of max)`, `hidden`. See `docs/KEYBOARD.md`.
- `boxes on|off|status|details|pick|drop|prev|next`: the native PC boxes (at a PC, the box and party on the top screen with the panel off; the D-pad moves the game's cursor, A picks up / drops, L/R turn the box, Y shows the game's own details). Logs `boxes: shown`, `cursor …`, `pick …`, `drop at …`, `box N`, `mode …`, `hidden`. See `docs/BOXES.md`.
- `uidemo on|off|shade|direct` shows or hides a sample plugin-drawn menu on the top screen (off by default), for checking the UI drawing layer: `on` draws it into an off-screen layer when it changes and copies it every frame, `shade` adds a translucent full-screen shade (reads the framebuffer), `direct` redraws everything on the framebuffer every frame (the old way). `perf` also logs the demo's time per frame; `py -3 -I tools/perf.py --instance NAME --modes off,ui:on,ui:shade` compares them (`docs/UI.md`). `uifont` logs the system font's metrics and the glyph cache counters.
- `uitheme on|off|load|status|page N|select N` shows the USUM theme demo: loads the game's own font, icons, move table and type names from its RomFS (once) and draws a sample move list (page 0) and list menu (page 1) with them. See `docs/VISUAL-STYLE.md` and `docs/GAME-ASSETS.md`.
- `py -3 -I tools/shot.py [name]` asks the running plugin for a screenshot and saves `analysis/private/shots/<name>-top.png` and `-bottom.png`. The top shot includes anything the plugin draws.

### Isolated Azahar instances

`tools/instance.py` runs Ultra Sun in Azahar instances that share nothing with each other or with your own Azahar (`%APPDATA%\Azahar`), so several tests can run at once:

```bash
py -3 -I tools/instance.py create a          # once; --force rebuilds it
py -3 -I tools/instance.py install a         # copies ./usum-onescreen.3gx in (or give a path)
py -3 -I tools/instance.py start a --wait 90 # boots Ultra Sun, prints the plugin's boot log
py -3 -I tools/cmd.py --instance a "press a"
py -3 -I tools/shot.py --instance a a-title
py -3 -I tools/instance.py stop a
py -3 -I tools/instance.py status
```

At most 4 instance emulators run at once on the PC, counted across every checkout and worktree: `start` waits for a free slot (set `USUM_EMU_SLOTS` to change the limit, `start --no-queue` to skip the wait). `status` lists every running instance emulator. Your own Azahar is never counted.

`start --no-plugin` turns the 3GX plugin loader off for that run. `start --movie FILE` plays an Azahar input movie, for input without the plugin: `tools/movie.py FILE SECONDS:KEYS ...` writes one (e.g. `25:- 5:l+r+start 300:-`). `docs/SOFT-RESET.md` uses both.

Azahar 2126.2 has no command-line flag or environment variable for its user folder; on Windows it uses a folder named `user` next to `azahar.exe` if there is one (`SetUserPath` in `src/common/file_util.cpp`), else `%APPDATA%\Azahar`. So an instance is `analysis/private/instances/<name>/azahar/`: a copy of `C:\Program Files\Azahar` (~100 MB) plus its own `user\` folder holding:

- `config`: copied from `%APPDATA%\Azahar\config`, with custom textures off, the 3GX plugin loader on, update checks off, and stored paths pointed at the instance.
- `nand`, `sysdata`: copied whole (small; holds the update's ticket).
- `sdmc`: only the Ultra Sun v1.2 update title, a **copy** of the save and extdata, and the plugin in `luma/plugins/00040000001B5000/`. Tests never touch the real save.

`stop` ends only the process `start` recorded, after checking its exe path is that instance's `azahar.exe` and its start time matches. Instance names are per checkout (each worktree has its own `analysis/private/`).

### Start points

`tools/startpoint.py` starts an instance at a known place instead of booting the save and walking:

```bash
py -3 -I tools/startpoint.py go a grass      # restore, start, drive to the point; prints the scene log
py -3 -I tools/startpoint.py list
py -3 -I tools/startpoint.py restore a pc    # only put the save in place (instance stopped)
py -3 -I tools/startpoint.py check           # mtime and SHA-256 of the real save, to compare
```

| Point | Where | `go` ends with |
|---|---|---|
| `title` | title screen | the title; Start opens `TitleMenu` |
| `pokecenter` | inside the Route 2 Pokémon Center, the save as copied | `scene: FieldRo` |
| `grass` | standing in the tall grass south of that Pokémon Center | `scene: FieldRo` (the next steps may start a wild battle) |
| `pc` | facing the PC at the right end of the nurse's counter | `scene: FieldRo`; A opens `Box` |

Azahar 2126.2 has no headless save states: no command-line option for them, and its RPC server has no save/load request (added upstream in 2127.0-alpha4, on a fixed UDP port shared by every instance). So a start point is an in-game save: `go` copies the point's save and extdata folders into the stopped instance, starts it, waits for the title, and for field points presses Start until `TitleMenu`, A until it leaves, then waits for `FieldRo`. The instance stays running; `go` took 14 s (`title`) and 22–25 s (field points) in testing. Points live in `analysis/private/startpoints/<point>/` with a `point.json` of file hashes, checked on every restore, and can go into any instance of the checkout; another checkout can copy that folder. To make a new one: drive an instance there, save in the game (X, then Y: Save, then A on Yes), `instance.py stop`, then `startpoint.py save <instance> <point> [--kind title|field] [--note TEXT]`. Restores write only inside `analysis/private/instances/` (checked before writing); the real save is only read, by `check`. Limits: each instance uses ~170 MB of disk and its own emulator window; game controllers are seen by every instance, so don't touch a pad during tests; after Azahar updates, re-`create --force` to pick up the new build. Without `--instance`, `cmd.py` and `shot.py` work on `%APPDATA%\Azahar` as before.

### Scenarios (scripted tests)

`tools/scenario.py` runs scripted tests from `scenarios/` against an instance, unattended:

```bash
py -3 -I tools/scenario.py --instance a boot-to-field open-each-app wild-battle-run
```

It prints each step, then `PASS name` or `FAIL name at file:line 'step': reason` with the last screenshot paths (plus a `<name>-FAIL` shot if the plugin still answers), and exits 0 only if every scenario passed. On failure it stops the instance it started, unless `--keep`; `-v` also prints the plugin's replies. `--check` only parses the files (no emulator; CI runs it). At the start and end of each scenario it records the real save's (`%APPDATA%\Azahar\...\00000001\main`) mtime and SHA-256 and fails if they changed.

A scenario is a text file, one step per line; `#` starts a comment (except in `expect-log`/`reject-log`, where it is part of the regex):

| Step | Does |
|---|---|
| `start [WAIT]` / `stop` | `tools/instance.py start NAME --wait WAIT` (default 90; fails without `hooks: all ok`) / `stop NAME` |
| `restore POINT` | stops the instance if it runs and puts start point POINT's save in (`tools/startpoint.py restore NAME POINT`); the next `start` boots into it |
| `send CMD [; CMD ...]` | plugin commands as `tools/cmd.py` takes them; fails on `cmd: unknown` |
| `sleep S` | waits S seconds |
| `shot LABEL` | screenshot to `analysis/private/shots/<scenario>-LABEL-top/bottom.png` |
| `wait-scene SCENE [T]` | waits until the scene is SCENE (default 60 s) |
| `expect-scene SCENE` | the scene is SCENE now |
| `expect-mode off\|panel\|full [T]` | the panel mode is that within T s (default 5) |
| `mark` | later `expect-log`/`reject-log` only see log lines after this point (`start` resets it) |
| `expect-log REGEX [T]` | a log line since the mark matches, within T s (default 10; a trailing number is the timeout) |
| `reject-log REGEX` | no log line since the mark matches |
| `check-save` | the real save is unchanged so far |
| `guard-ignore REGEX` | the log guard skips lines matching REGEX from here on |
| `if-scene SCENE STEP` | runs STEP only if the scene is SCENE now |
| `include FILE` | inlines FILE's steps (looked up next to the scenario, then in `scenarios/`; `.txt` optional) |
| `repeat N [until-scene SCENE]` … `end` | runs the block N times; with `until-scene`, stops once that is the scene (checked before each step) and fails if it never is |
| `repeat N until-log REGEX` … `end` | the same, stopping after a round once a log line since the mark matches REGEX (one word: `\s` for spaces); fails if none did |

Log guard: after every step it reads what was newly logged to the instance's `user/log/azahar_log.txt` and `usum-onescreen.log`, and fails that step on an Azahar `<Critical>`, `Assertion Failed`, `Unreachable code` or `unmapped read/write` line, or on a plugin `hooks: FAILED`, `MISMATCH at`, `unreadable at`, a `cursor:`/`buttons:` hook that returned non-zero, or `unsupported game`. Azahar's `<Error>` lines (unimplemented services) are normal and ignored. `--no-guard` turns it off.

"The scene" is the last `scene:` line in the log (an empty one counts as `(none)`, the loading gap between scenes); "the mode" is the last `panel: mode` line. Included scenarios:

- `boot-to-field`: title → Continue → `FieldRo`, mode off. The shared boot steps are in `scenarios/common/boot.txt`.
- `open-each-app`: Pokédex via Rotom (answering Rotom's chat if it pops up) and back; X menu → Party and back; X menu → Bag and back. Entries are tapped, so the X menu's remembered cursor doesn't matter. Checks off (field), panel (X menu, Pokédex, Bag), full (Party).
- `actions`: each action (`docs/ACTIONS.md`) from its start point, checking the scene and panel mode it ends in, B held through `open-bag` (captured, so the Bag stays open), then a wild battle and `battle-run`.
- `xmenu-touch-close`: Town Map from Rotom's screen and back (full, then off with no panel flash); X menu opened with X and closed by tapping its back button (panel, then off); again after visiting Party. Checks that auto panel follows the field state, not X/B presses.
- `native-boxes`: starts from the `pc` start point (`restore pc`); A at the PC shows the native boxes with the panel off; browses (D-pad, L/R), details (Y), moves a Pokémon box → another box while holding it, party → box and box → party, checks every move with `box`, the panel by hand, then B back to the field with the party changed and nothing saved. See `docs/BOXES.md`.
- `native-map`: ZR shows the native map inside the Route 2 Pokémon Center; B closes; a step inside, then out of the door and down Route 2 with the map up (zone 79 → 7, inside → outside, position follows); ZR closes; X menu → Map opens it; `map off` gives the Town Map back. See `docs/MAP.md`.
- `native-move-learn`: starts from the `grass` start point; wild battles until Buneary levels up and wants Baby-Doll Eyes; the native move list (panel off, no *game asks* helper), B to *Don't learn*, Down to Splash, the confirm page and back, A A forgets Splash through the game's own screens, and the party has Baby-Doll Eyes after the battle. Nothing saved. See `docs/MOVE-LEARN.md`.

- `native-rotom-chat`: waits (up to 12 min) for Rotom to want to talk, Up starts it (a Roto Loto in between is played with the prompt), the question and answers show on the top screen, ZR's map steps aside, Down + A answers 2, the reply shows, the map comes back. See `docs/ROTOM-FIELD.md`.
- `native-party`: X menu → Pokémon shows the native Party screen with the panel off; moves; Summary of Zorua (move, ability, nature names), the next Pokémon, Y for the game's page, B; swap 1 and 6; a wild battle for damage; Use item → Potion on the hurt Pokémon (HP up, Potion x9 → x8). See `docs/PARTY-UI.md`.
- `native-xmenu`: X shows the native X menu with the panel off; D-pad/L/R moves; every entry opened with A and backed out of (Save stops at the game's confirm dialog and backs out with B); cursor memory after B; Festival Plaza in and out; B closes to the field with input given back. See `docs/XMENU.md`.
- `native-nickname`: into the tall grass, catches a wild Pokémon with Poké Balls (taps), Yes to the nickname question, then types "Bobo" on the native keyboard with the D-pad and A only (panel off), X/Y/B, the 12-character limit, START → OK → A; the game says "Bobo has been sent to a Box!" (shot) and input is given back. See `docs/KEYBOARD.md`.
- `wild-battle-run`: out of the Pokémon Center, south into the tall grass, back and forth until `Battle` (panel), Run by tap, back to `FieldRo` (off). One long circle-pad push stops partway down the path, so it walks in short steps.

Screenshots come before every A press in a menu (only the Continue menu here).

### Regression run

`tools/regress.py` installs a build into an instance (default `regress`; created if missing, `--fresh` rebuilds it) and runs every scenario, restoring a start point's save (default `pokecenter`, from `analysis/private/startpoints/`) before each one, then prints a summary and exits 0 only if all passed:

```bash
py -3 -I tools/regress.py --instance a                   # ./usum-onescreen.3gx
py -3 -I tools/regress.py --instance a --main            # newest green CI build on main (gh run download)
py -3 -I tools/regress.py --instance a --run 41489129071 boot-to-field
```

`--keep` stops at the first failing scenario and leaves the instance running. Downloaded artifacts go to `analysis/private/regress/run-<id>/`.

## Layout

- `Sources/main.cpp`: plugin entry points (`PatchProcess`, `main`, menu).
- `Sources/Settings.cpp`, `Includes/Settings.hpp`: `usum-onescreen.ini` parser and settings model (plain C++, tested by `tests/`).
- `Sources/Game.cpp`, `Includes/Game.hpp`: supported games and their offset tables. Phase 0 fills in `gspPresent`, `hidTouchRead` and `sceneState` from Ghidra.
- `Sources/InputCapture.cpp`, `Includes/InputCapture.hpp`: pure masking logic for input capture, used by the pad and touch hooks (`Buttons.cpp`, `Cursor.cpp`). Plugin menus read the real input with `CaptureRealHeld/Pressed/Stick` and `CaptureRealTouch`: the game's latest read before masking. That view (not CTRPF's `Controller`, which reads HID directly) includes injected test input, so `press`/`stick`/`tap` drive plugin menus too, and it has the same timing as what the game sees. It goes stale if the game stops reading input; `Controller` is the fallback then.
- `tests/`: host tests for the pure logic (`make -C tests` with any g++, e.g. `wsl make -C tests` on Windows; CI runs them). The UI test also writes a preview of the `uidemo` menu to `analysis/private/ui-demo.png`.
- `Sources/UI`, `Includes/UI`: drawing layer for plugin-drawn top-screen menus. `Raster` (rects, rounded rects, lines, triangles, alpha blending, clipping), `Text` (system font, built-in 5x7 font, measuring, alignment, ellipsis), `Font` (glyphs decoded from the shared system font's sheets, cached), `Layer` (off-screen menus copied to the framebuffer without reading it), `Icons`, `Layout` and the `uidemo` menu are free of CTRPF; `Device.cpp` is the CTRPF/libctru glue. See `docs/UI.md`.
- `tools/scenario.py`, `scenarios/`: scripted emulator tests; `tools/emu.py` is the shared command/screenshot code behind `cmd.py`, `shot.py` and `scenario.py`.
- `Makefile`, `3gx.ld`, `usum-onescreen.plgInfo`: build setup, adapted from [Gen6CTRPFrameworkOverhauled](https://github.com/samaBR85/Gen6CTRPFrameworkOverhauled).
- `docs/PORTING.md`: moving the plugin to another build (the Ultra Moon checklist). `tools/sigscan.py` finds every offset-table address in a new `code.bin` from the byte signatures in `signatures/usum.json` and prints a ready-to-paste `Game.cpp` entry.
- `Sources/Box.cpp`, `Sources/BoxData.cpp`: the PC box reader (`box`) and its pure part (layout, cursor values, modes, names; tested by `tests/boxes_test.cpp`). `Sources/Screens/Boxes.cpp`, `Sources/Screens/BoxesGrid.cpp`: the native PC boxes (`docs/BOXES.md`). See `docs/RE6-boxes.md`.
- `Sources/Party.cpp`, `Sources/PartyData.cpp`: the `party` and `bag` readers; `PartyData` (Gen 7 Pokémon decryption, bag slots, message lines) is pure and tested by `tests/party_data_test.cpp`. See `docs/RE3-party-bag.md`.
- `docs/RE2-text.md`: how the game stores and looks up text (species/move/item names), with `tools/romfs_text.py` to extract and decode it from your own dump into `analysis/private/`.
- `docs/LAYOUTS.md`, `layouts/usum-v1.2.json`: verified tap points and button bounds on the game's bottom screens (field, X menu, Party, Bag, Pokédex, battle, keyboard), for driving the hidden bottom screen from plugin menus. `tools/layout_measure.py` measures button bounds on a screenshot. `tools/gen_layouts.py` turns the JSON into `Includes/Layouts.hpp` (generated, committed; the host tests check it is current).
- `Sources/ActionQueue.cpp`, `Includes/ActionQueue.hpp`: the action layer's pure sequencing (steps, runner, built-in actions; tested by `tests/`); `Sources/Actions.cpp` is its CTRPF glue (`docs/ACTIONS.md`).
