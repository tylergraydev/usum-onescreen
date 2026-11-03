# USUM One Screen app

A Windows launcher that turns "I have my own Ultra Sun dump" into "the game is running, top screen only, with the plugin". It is route (a) in [docs/PLAYER-APP.md](../docs/PLAYER-APP.md): stock Azahar driven from outside, no fork. What players read is [PLAYER-README.txt](PLAYER-README.txt), shipped as `README.txt`.

## Build

Needs the .NET SDK (any recent one; the app targets .NET Framework 4.8, which ships with Windows 10 and 11, so players need no runtime). Build the plugin first (`make` in the repo root), then:

```powershell
.\build.ps1
```

`dist\` then holds `UsumOneScreen.exe`, `usum-onescreen.3gx` and `README.txt`: the whole player download. It never contains Azahar, game files or keys.

## What it does

All in [`src/`](src):

| File | Job |
|---|---|
| `Dump.cs` | Reads NCSD/NCCH/CIA headers and TMDs. Game: title `00040000001B5000`. Update: `0004000E001B5000`, version `0x0800`. Refuses encrypted dumps (NCCH NoCrypto flag; CIA content flag). |
| `Home.cs` | The app folder: `azahar\` (a copy of the player's Azahar, made portable by `azahar\user\`) and `usum-onescreen-app.ini` (ROM path, controls, fullscreen). Writes Azahar's config, installs the plugin, installs CIAs with `azahar.exe -i` (closing the "Success"/"Failure" box Azahar shows on Windows), launches with `-w`/`-f`. |
| `Ini.cs` | Edits Azahar's `qt-config.ini` (`key\default=false` + `key=value`, as `tools/instance.py` does) and the plugin's `usum-onescreen.ini` in place, keeping its comments. |
| `Controls.cs` | Controller presets: Keyboard (Azahar's defaults), Gamepad (SDL `maptype:all`, so any controller works without its GUID), Custom (left alone; mapped in Azahar's own window). |
| `Setup.cs` | The first-run flow, shared by the window and the command line. |
| `MainForm.cs` | The window. |
| `Cli.cs` | Command line, for scripts and headless tests. |

Azahar config written: `[System] plugin_loader`, `allow_plugin_loader` = true; `[Layout] layout_option` = 1 (Single Screen), `swap_screen` = false; `[Utility] custom_textures`, `preload_textures` = false; `[Miscellaneous] check_for_update_on_start` = false; `[UI] showStatusBar`, `showFilterBar`, `confirmClose` = false. Per-game overrides of the layout and textures are removed. Key names checked against Azahar 2126.2's `src/citra_qt/configuration/config.cpp` and `src/common/settings.h`.

## Command line

```
UsumOneScreen check FILE...
UsumOneScreen setup --rom FILE [--update CIA] [--azahar DIR] [--plugin 3GX] [--controls keyboard|gamepad|custom]
UsumOneScreen play [--fullscreen] [--wait SECONDS]
UsumOneScreen set KEY=VALUE...      (plugin keys, or controls=PRESET)
UsumOneScreen status
```

`--home DIR` works with any command and puts everything in DIR instead of next to the exe. For emulator tests in this repo, use a home under `analysis/private/instances/NAME/`: its `azahar\azahar.exe` then counts toward the `tools/instance.py` emulator queue, and `tools/cmd.py --instance NAME` / `tools/shot.py --instance NAME` work on it.
