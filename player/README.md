# USUM One Screen app

A Windows launcher that turns "I have my own Ultra Sun or Ultra Moon dump" into "the game is running, top screen only, with the plugin". It is route (a) in [docs/PLAYER-APP.md](../docs/PLAYER-APP.md): stock Azahar driven from outside, no fork. What players read is [PLAYER-README.txt](PLAYER-README.txt), shipped as `README.txt`.

## Build

Needs the .NET SDK (any recent one; the app targets .NET Framework 4.8, which ships with Windows 10 and 11, so players need no runtime). Build the plugin first (`make` in the repo root), then:

```powershell
.\build.ps1
# Or bundle a clean Azahar distribution:
.\build.ps1 -AzaharDir PATH -AzaharLicense FILE
```

`dist\` holds `UsumOneScreen.exe`, `usum-onescreen.3gx` and `README.txt`. With `-AzaharDir PATH` (the clean extracted folder holding `azahar.exe`), it also holds `azahar-bundled\`, the upstream licence and `AZAHAR-SOURCE.txt`. If the folder lacks `COPYING` / `license*`, supply the upstream licence as `-AzaharLicense FILE`; it is copied to `azahar-bundled\LICENSE-azahar.txt` and the zip root. Licences already in the folder are kept. The release workflow bundles unmodified Azahar 2126.2 after checking its pinned SHA-256 and separately verifies the licence's pinned git blob hash; see [RELEASING.md](../docs/RELEASING.md). No game files or keys are included. Unzip the Windows release, run `UsumOneScreen.exe`, pick your own decrypted USA ROM and matching v1.2 update once, then press Play. Encrypted dumps are refused because Azahar 2126.2 cannot install them.

## What it does

All in [`src/`](src):

| File | Job |
|---|---|
| `Dump.cs` | Reads NCSD/NCCH/CIA headers and TMDs. Game: Ultra Sun `00040000001B5000` or Ultra Moon `00040000001B5100` (USA). Requires its matching update (`0004000E001B5000` / `0004000E001B5100`), version `0x0820` (genuine v1.2). Sun alone also accepts `0x0800` with the existing warning (a rebuilt copy on which new games stall); Moon refuses it. Refuses encrypted dumps (NCCH NoCrypto flag; CIA content flag). |
| `Home.cs` | The app folder: `azahar\` (a copy of bundled Azahar when `azahar-bundled\azahar.exe` is next to the launcher, otherwise the player's installed Azahar, made portable by `azahar\user\`) and `usum-onescreen-app.ini` (ROM path, selected `title_id`, controls, fullscreen). Writes Azahar's config, installs the plugin, installs CIAs with `azahar.exe -i` (closing the "Success"/"Failure" box Azahar shows on Windows), launches with `-w`/`-f`. |
| `Ini.cs` | Edits Azahar's `qt-config.ini` (`key\default=false` + `key=value`, as `tools/instance.py` does) and the plugin's `usum-onescreen.ini` in place, keeping its comments. |
| `Controls.cs` | Controller presets: Keyboard (Azahar's defaults), Gamepad (SDL `maptype:all`, so any controller works without its GUID), Custom (left alone; mapped in Azahar's own window). |
| `Setup.cs` | The first-run flow, shared by the window and the command line. |
| `MainForm.cs` | The window. |
| `Cli.cs` | Command line, for scripts and headless tests. |

Azahar config written: `[System] plugin_loader`, `allow_plugin_loader` = true; `[Layout] layout_option` = 1 (Single Screen), `swap_screen` = false; `[Utility] custom_textures`, `preload_textures` = false; `[Miscellaneous] check_for_update_on_start` = false; `[UI] showStatusBar`, `showFilterBar`, `confirmClose` = false. Per-game overrides of the layout and textures are removed. Key names checked against Azahar 2126.2's `src/citra_qt/configuration/config.cpp` and `src/common/settings.h`.

The source Azahar folder is only read; configuration, updates, saves and plugins go into the home's portable `azahar\` copy. A bundle takes priority over `--azahar` when a source copy is needed, including with `--home`. Without a bundle, install discovery and folder selection work as before. `check`, `setup` and `status` identify the game. Older state files without `title_id` retain their Sun behaviour.

After building, run `python player/test.py` from the repository root. It checks validation, matching updates, title state and paths, bundled source priority and immutability, and fallback setup using synthetic headers only; it never runs Azahar or needs game content.

## Command line

```
UsumOneScreen check FILE...
UsumOneScreen setup --rom FILE [--update CIA] [--azahar DIR] [--plugin 3GX] [--controls keyboard|gamepad|custom]
UsumOneScreen play [--fullscreen] [--wait SECONDS]
UsumOneScreen set KEY=VALUE...      (plugin keys, or controls=PRESET)
UsumOneScreen status
```

`--home DIR` works with any command and puts everything in DIR instead of next to the exe. For emulator tests in this repo, use a home under `analysis/private/instances/NAME/`: its `azahar\azahar.exe` then counts toward the `tools/instance.py` emulator queue, and `tools/cmd.py --instance NAME` / `tools/shot.py --instance NAME` work on it.
