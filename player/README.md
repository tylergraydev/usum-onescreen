# USUM One Screen app

A Windows launcher that turns "I have my own Ultra Sun or Ultra Moon dump" into "the game is running, top screen only, with the plugin". It is route (a) in [docs/PLAYER-APP.md](../docs/PLAYER-APP.md): stock Azahar driven from outside, no fork. What players read is [PLAYER-README.txt](PLAYER-README.txt), shipped as `README.txt`.

## Build

Needs the .NET SDK (any recent one; the app targets .NET Framework 4.8, which ships with Windows 10 and 11, so players need no runtime). Build the plugin first (`make` in the repo root), then:

```powershell
.\build.ps1
# Or bundle a clean Azahar distribution:
.\build.ps1 -AzaharDir PATH -AzaharLicense FILE
```

`dist\UsumOneScreen.exe` embeds the plugin when `usum-onescreen.3gx` exists in the repo root at build time. The csproj also builds without it, but that exe needs an external plugin. `build.ps1` requires the plugin and also keeps the separate `.3gx` and `README.txt` in `dist\` for the offline zip format.

The recommended release is `UsumOneScreen-<tag>.exe`: put it in a writable folder, run it, drag your own decrypted USA ROM and matching v1.2 update onto the window (or a zip containing them), and press Play. It downloads tested Azahar 2126.2 on first setup. No separate emulator or plugin install is needed. The offline `usum-onescreen-<tag>-windows.zip` keeps the existing contents: launcher, plugin, README, clean `azahar-bundled\`, licence and source notice. Build that with `-AzaharDir PATH -AzaharLicense FILE`; the source folder must hold `azahar.exe` and either a licence (`COPYING` / `license*`) or the supplied licence. No game files or keys are included.

## What it does

All in [`src/`](src):

| File | Job |
|---|---|
| `Dump.cs` | Reads NCSD/NCCH/CIA headers and TMDs. Game: Ultra Sun `00040000001B5000` or Ultra Moon `00040000001B5100` (USA). Requires its matching update (`0004000E001B5000` / `0004000E001B5100`), version `0x0820` (genuine v1.2). Sun alone also accepts `0x0800` with the existing warning (a rebuilt copy on which new games stall); Moon refuses it. Refuses encrypted dumps (NCCH NoCrypto flag; CIA content flag). |
| `Home.cs` | The app folder: `azahar\` (a copy of bundled, explicitly selected, downloaded or discovered Azahar, made portable by `azahar\user\`) and `usum-onescreen-app.ini` (ROM path, selected `title_id`, controls, fullscreen). Writes Azahar's config, installs the plugin, installs CIAs with `azahar.exe -i` (closing the "Success"/"Failure" box Azahar shows on Windows), launches with `-w`/`-f`. |
| `Ini.cs` | Edits Azahar's `qt-config.ini` (`key\default=false` + `key=value`, as `tools/instance.py` does) and the plugin's `usum-onescreen.ini` in place, keeping its comments. |
| `Controls.cs` | Controller presets: Keyboard (Azahar's defaults), Gamepad (SDL `maptype:all`, so any controller works without its GUID), Custom (left alone; mapped in Azahar's own window). |
| `Setup.cs` | The first-run flow, shared by the window and the command line. |
| `MainForm.cs` | The window. |
| `Cli.cs` | Command line, for scripts and headless tests. |

Azahar config written: `[System] plugin_loader`, `allow_plugin_loader` = true; `[Layout] layout_option` = 1 (Single Screen), `swap_screen` = false; `[Utility] custom_textures`, `preload_textures` = false; `[Miscellaneous] check_for_update_on_start` = false; `[UI] showStatusBar`, `showFilterBar`, `confirmClose` = false. Per-game overrides of the layout and textures are removed. Key names checked against Azahar 2126.2's `src/citra_qt/configuration/config.cpp` and `src/common/settings.h`.

The source Azahar folder is only read; configuration, updates, saves and plugins go into the home's portable `azahar\` copy. Setup keeps an existing portable copy unless `--azahar` requests another source. When a copy is needed, source order is: `azahar-bundled\` next to the exe (also with `--home`), explicit `--azahar`, verified download of 2126.2, then installed-Azahar discovery if the download fails. The UI can pick an Azahar folder. Downloads use TLS 1.2 and report progress in roughly 10% steps on the existing setup worker.

The archive's SHA-256 is verified before extraction, and it must contain exactly one `azahar.exe`. The separately downloaded licence is checked using its git blob SHA-1 (`blob <length>\0` plus bytes) and saved as `azahar\LICENSE-azahar.txt`. Pins and URLs match [RELEASING.md](../docs/RELEASING.md). Temporary downloads are inside the home and removed afterwards. Overrides for offline testing: `USUM_AZAHAR_URL`, `USUM_AZAHAR_SHA256`, `USUM_AZAHAR_LICENSE_URL`, `USUM_AZAHAR_LICENSE_SHA1`; URLs accept `file:///`.

Plugin source order is explicit `--plugin`, `usum-onescreen.3gx` next to the exe, then embedded resource `UsumOneScreen.Plugin`. The resource is written to a temporary file inside the home, installed under the selected title, then removed. A missing plugin gives a setup error.

Drop one or several `.3ds`, `.cci`, `.cia` or `.zip` files anywhere on the window, or use Browse. Headers classify games and updates; invalid files show a reason. Zip entries at any depth with `.3ds`, `.cci` or `.cia` extensions are flattened into `<home>\games\` using only their file names. Free space is checked first; name collisions get suffixes. Existing files with the same size and validated header fields are reused. State stores the extracted ROM path. `check`, `setup --rom` and `setup --update` accept zips; a ROM zip also supplies the update unless `--update` is given. Direct `.cxi` input remains supported.

After building with the plugin available, run `python player/test.py` from the repository root. It uses synthetic headers, local archives and `file:///` downloads only; it never starts Azahar. It covers validation, title state, bundle priority, source fallback, download/hash refusal, zip traversal/reuse and embedded-plugin priority. `check`, `setup` and `status` identify the game. Older state files without `title_id` retain their Sun behaviour.

## Command line

```
UsumOneScreen check FILE...
UsumOneScreen setup --rom FILE [--update FILE] [--azahar DIR] [--plugin 3GX] [--controls keyboard|gamepad|custom]
UsumOneScreen play [--fullscreen] [--wait SECONDS]
UsumOneScreen set KEY=VALUE...      (plugin keys, or controls=PRESET)
UsumOneScreen status
```

`--home DIR` works with any command and puts everything in DIR instead of next to the exe. For emulator tests in this repo, use a home under `analysis/private/instances/NAME/`: its `azahar\azahar.exe` then counts toward the `tools/instance.py` emulator queue, and `tools/cmd.py --instance NAME` / `tools/shot.py --instance NAME` work on it.
