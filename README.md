# USUM One Screen

A 3GX plugin (CTRPluginFramework) that makes Pokémon Ultra Sun / Ultra Moon playable on the top screen only, in Azahar with the 3GX plugin loader enabled. Plan: https://claude.ai/artifact/LxD6pmGFR8x91NrRRF8P7R

**Status:** Phase 1 started. The plugin draws the bottom screen onto the top screen (CTRPF OSD callback, `Sources/Panel.cpp`): a half-size panel in the lower-right corner by default, and **ZR** cycles off / panel / full view. The panel follows the scene automatically (`Sources/AutoPanel.cpp`): title and overworld off, X menu / battle / Pokédex / Bag / unknown scenes as a panel, Party as full view; a ZR choice holds until the next scene change. The X menu is tracked from the game's X/B presses, so closing it by touch isn't noticed. A touch cursor (`Sources/Cursor.cpp`) is drawn inside the panel/full view: **C-stick** moves it, **ZL** touches. It hooks the game's touch reader (`0x2826F0`) and returns the cursor's point while touching.  Nothing here has been compiled yet.

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

CI (`.github/workflows/build.yml`) does the same in the `devkitpro/devkitarm` container and uploads the `.3gx` as a build artifact.

## Run in Azahar

1. Configure → System → enable **3GX plugin loader**.
2. Copy `usum-onescreen.3gx` into Azahar's emulated SD card at `luma/plugins/00040000001B5000/` (Ultra Sun). This is the Luma plugin folder layout; check it against your Azahar setup.
3. Start the game. You should see a `USUM One Screen: ...` notice.

## Settings (`usum-onescreen.ini`)

At boot the plugin reads `usum-onescreen.ini` at the emulated SD root (next to the log). If it is missing, the plugin writes a commented default file there. The defaults reproduce the behaviour described above, so an untouched file changes nothing.

```ini
[controls]
panel_key = ZR          ; cycles off / panel / full; A B X Y L R ZL ZR Start Select Up Down Left Right, L+R for a combo, or none
touch_key = ZL          ; touches at the cursor while held
cursor_move = cstick    ; cstick, dpad or circlepad (dpad / circlepad still reach the game too)
cursor_speed = 6        ; top speed in px/frame, 1-20; starts at 1, then 3 (or less), then this

[panel]
size = 1/2              ; 1/2; 2/3 is accepted but drawn as 1/2 for now
corner = bottom-right   ; bottom-right, bottom-left, top-right, top-left
auto = on               ; on: the view follows [scenes]; off: it stays as panel_key sets it

[scenes]                ; off, panel or full per scene ID (as logged: "scene: ...")
TitleMenu = off
FieldRo = off           ; overworld
FieldRo.xmenu = panel   ; the X menu, open over the overworld
Zukan = panel           ; Pokédex
Battle = panel
main:00647FCC = full    ; Party
main:006488C8 = panel   ; Bag
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
- The log records every scene change (`scene: FieldRo`, `Zukan`, `Battle`, …); `scene` logs the current one. Memory tools: `snap NAME`, `vtscan NAME`, `peek ADDR [N]` (see `docs/OFFSETS.md`).
- `uidemo on` / `uidemo off` shows or hides a sample plugin-drawn menu on the top screen (off by default), for checking the UI drawing layer.
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

Azahar 2126.2 has no command-line flag or environment variable for its user folder; on Windows it uses a folder named `user` next to `azahar.exe` if there is one (`SetUserPath` in `src/common/file_util.cpp`), else `%APPDATA%\Azahar`. So an instance is `analysis/private/instances/<name>/azahar/`: a copy of `C:\Program Files\Azahar` (~100 MB) plus its own `user\` folder holding:

- `config`: copied from `%APPDATA%\Azahar\config`, with custom textures off, the 3GX plugin loader on, update checks off, and stored paths pointed at the instance.
- `nand`, `sysdata`: copied whole (small; holds the update's ticket).
- `sdmc`: only the Ultra Sun v1.2 update title, a **copy** of the save and extdata, and the plugin in `luma/plugins/00040000001B5000/`. Tests never touch the real save.

`stop` ends only the process `start` recorded, after checking its exe path is that instance's `azahar.exe` and its start time matches. Instance names are per checkout (each worktree has its own `analysis/private/`). Limits: each instance uses ~170 MB of disk and its own emulator window; game controllers are seen by every instance, so don't touch a pad during tests; after Azahar updates, re-`create --force` to pick up the new build. Without `--instance`, `cmd.py` and `shot.py` work on `%APPDATA%\Azahar` as before.

## Layout

- `Sources/main.cpp`: plugin entry points (`PatchProcess`, `main`, menu).
- `Sources/Settings.cpp`, `Includes/Settings.hpp`: `usum-onescreen.ini` parser and settings model (plain C++, tested by `tests/`).
- `Sources/Game.cpp`, `Includes/Game.hpp`: supported games and their offset tables. Phase 0 fills in `gspPresent`, `hidTouchRead` and `sceneState` from Ghidra.
- `Sources/InputCapture.cpp`, `Includes/InputCapture.hpp`: pure masking logic for input capture, used by the pad and touch hooks (`Buttons.cpp`, `Cursor.cpp`). Plugin menus read the real input with `CaptureRealHeld/Pressed/Stick` and `CaptureRealTouch`: the game's latest read before masking. That view (not CTRPF's `Controller`, which reads HID directly) includes injected test input, so `press`/`stick`/`tap` drive plugin menus too, and it has the same timing as what the game sees. It goes stale if the game stops reading input; `Controller` is the fallback then.
- `tests/`: host tests for the pure logic (`make -C tests` with any g++, e.g. `wsl make -C tests` on Windows; CI runs them). The UI test also writes a preview of the `uidemo` menu to `analysis/private/ui-demo.png`.
- `Sources/UI`, `Includes/UI`: drawing layer for plugin-drawn top-screen menus. `Raster` (rects, rounded rects, lines, triangles, alpha blending, clipping), `Text` (system font via CTRPF, built-in 5x7 font, measuring, alignment, ellipsis), `Icons`, `Layout` and the `uidemo` menu are free of CTRPF; `Device.cpp` is the CTRPF glue.
- `Makefile`, `3gx.ld`, `usum-onescreen.plgInfo`: build setup, adapted from [Gen6CTRPFrameworkOverhauled](https://github.com/samaBR85/Gen6CTRPFrameworkOverhauled).
- `docs/PORTING.md`: moving the plugin to another build (the Ultra Moon checklist). `tools/sigscan.py` finds every offset-table address in a new `code.bin` from the byte signatures in `signatures/usum.json` and prints a ready-to-paste `Game.cpp` entry.
- `docs/RE2-text.md`: how the game stores and looks up text (species/move/item names), with `tools/romfs_text.py` to extract and decode it from your own dump into `analysis/private/`.
