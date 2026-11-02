# USUM One Screen

A 3GX plugin (CTRPluginFramework) that makes Pokémon Ultra Sun / Ultra Moon playable on the top screen only, in Azahar with the 3GX plugin loader enabled. Plan: https://claude.ai/artifact/LxD6pmGFR8x91NrRRF8P7R

**Status:** Phase 1 started. The plugin draws the bottom screen onto the top screen (CTRPF OSD callback, `Sources/Panel.cpp`): a half-size panel in the lower-right corner by default, and **ZR** cycles off / panel / full view. The panel follows the scene automatically (`Sources/AutoPanel.cpp`): title and overworld off, X menu / battle / Pokédex / Bag / unknown scenes as a panel, Party as full view; a ZR choice holds until the next scene change. The X menu is tracked from the game's X/B presses, so closing it by touch isn't noticed. A touch cursor (`Sources/Cursor.cpp`) is drawn inside the panel/full view: **C-stick** moves it, **ZL** touches. It hooks the game's touch reader (`0x2826F0`) and returns the cursor's point while touching.  Nothing here has been compiled yet.

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

## Testing without watching the screen

- `usum-onescreen.log` (emulated SD root) gets the boot line, the hook check, and panel status after ~5 s and on every mode change.
- `py -3 -I tools/cmd.py "move 160 120" "touch on" "tap 10" "touch off" "mode full" "status"` sends test commands to the plugin (via `usum-cmd.txt`) and prints the log lines they produce, including what the game's touch reader last returned.
- Buttons too: `press KEY [FRAMES]` (default 3; menus key-repeat by 8 frames), `hold KEY`, `release KEY`, with KEY one of `a b x y l r start select up down left right`, and `stick X Y [FRAMES]` for the circle pad (walking uses the circle pad, not the D-pad). Injected through the game's pad reader (`0x282548`), so the game sees them as real presses.
- The log records every scene change (`scene: FieldRo`, `Zukan`, `Battle`, …); `scene` logs the current one. Memory tools: `snap NAME`, `vtscan NAME`, `peek ADDR [N]` (see `docs/OFFSETS.md`).
- `py -3 -I tools/shot.py [name]` asks the running plugin for a screenshot and saves `analysis/private/shots/<name>-top.png` and `-bottom.png`. The top shot includes anything the plugin draws.

## Layout

- `Sources/main.cpp`: plugin entry points (`PatchProcess`, `main`, menu).
- `Sources/Game.cpp`, `Includes/Game.hpp`: supported games and their offset tables. Phase 0 fills in `gspPresent`, `hidTouchRead` and `sceneState` from Ghidra.
- `Makefile`, `3gx.ld`, `usum-onescreen.plgInfo`: build setup, adapted from [Gen6CTRPFrameworkOverhauled](https://github.com/samaBR85/Gen6CTRPFrameworkOverhauled).
- `docs/RE2-text.md`: how the game stores and looks up text (species/move/item names), with `tools/romfs_text.py` to extract and decode it from your own dump into `analysis/private/`.
