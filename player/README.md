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
| `Backups.cs` / `SaveSlots.cs` | Portable save discovery, rotating backups, permanent named slots and validated save ZIP import/export. |
| `MainForm.cs` | The window. |
| `Cli.cs` | Command line, for scripts and headless tests. |

Azahar config written: `[System] plugin_loader`, `allow_plugin_loader` = true; `[Layout] layout_option` = 1 (Single Screen), `swap_screen` = false; `[Utility] custom_textures`, `preload_textures` = false; `[Miscellaneous] check_for_update_on_start` = false; `[UI] showStatusBar`, `showFilterBar`, `confirmClose` = false. Per-game overrides of the layout and textures are removed. Key names checked against Azahar 2126.2's `src/citra_qt/configuration/config.cpp` and `src/common/settings.h`.

The source Azahar folder is only read; configuration, updates, saves and plugins go into the home's portable `azahar\` copy. Setup keeps an existing portable copy unless `--azahar` requests another source. When a copy is needed, source order is: `azahar-bundled\` next to the exe (also with `--home`), explicit `--azahar`, verified download of 2126.2, then installed-Azahar discovery if the download fails. The UI can pick an Azahar folder. Downloads use TLS 1.2 and report progress in roughly 10% steps on the existing setup worker.

The archive's SHA-256 is verified before extraction, and it must contain exactly one `azahar.exe`. The separately downloaded licence is checked using its git blob SHA-1 (`blob <length>\0` plus bytes) and saved as `azahar\LICENSE-azahar.txt`. Pins and URLs match [RELEASING.md](../docs/RELEASING.md). Temporary downloads are inside the home and removed afterwards. Overrides for offline testing: `USUM_AZAHAR_URL`, `USUM_AZAHAR_SHA256`, `USUM_AZAHAR_LICENSE_URL`, `USUM_AZAHAR_LICENSE_SHA1`; URLs accept `file:///`.

Plugin source order is explicit `--plugin`, `usum-onescreen.3gx` next to the exe, then embedded resource `UsumOneScreen.Plugin`. The resource is written to a temporary file inside the home, installed under the selected title, then removed. A missing plugin gives a setup error.

Drop one or several `.3ds`, `.cci`, `.cia` or `.zip` files anywhere on the window, or use Browse. Headers classify games and updates; invalid files show a reason. Zip entries at any depth with `.3ds`, `.cci` or `.cia` extensions are flattened into `<home>\games\` using only their file names. Free space is checked first; name collisions get suffixes. Existing files with the same size and validated header fields are reused. State stores the extracted ROM path. `check`, `setup --rom` and `setup --update` accept zips; a ROM zip also supplies the update unless `--update` is given. Direct `.cxi` input remains supported.

After building with the plugin available, run `python player/test.py` from the repository root. It uses synthetic headers, local archives and `file:///` downloads only; it never starts Azahar. It covers widescreen sync and comment preservation, validation, title state, bundle priority, source fallback, download/hash refusal, zip traversal/reuse and embedded-plugin priority. `check`, `setup` and `status` identify the game. Older state files without `title_id` retain their Sun behaviour.

The Game options group's experimental Widescreen dropdown offers Auto, Original (5:3),
16:9 and 21:9. Auto uses the primary monitor: ratios at least 2.2 select 21:9,
at least 1.7 select 16:9, otherwise Original. The choice is saved in app state;
setup and every launch write a concrete `[display] aspect` to
`azahar/user/sdmc/usum-onescreen.ini` and sync `[Layout] screen_top_stretch`
in `azahar/user/config/qt-config.ini`. Original turns stretch off. Existing custom
float aspects are kept when saving the window's settings.

## Command line

```
UsumOneScreen check FILE...
UsumOneScreen setup --rom FILE [--update FILE] [--azahar DIR] [--plugin 3GX] [--controls keyboard|gamepad|custom]
UsumOneScreen play [--fullscreen] [--wait SECONDS]
UsumOneScreen set KEY=VALUE...      (plugin keys, or controls=PRESET)
UsumOneScreen status
```

`--home DIR` works with any command and puts everything in DIR instead of next to the exe. For emulator tests in this repo, use a home under `analysis/private/instances/NAME/`: its `azahar\azahar.exe` then counts toward the `tools/instance.py` emulator queue, and `tools/cmd.py --instance NAME` / `tools/shot.py --instance NAME` work on it.

`UsumOneScreen set aspect=16:9` changes both files immediately when Azahar is
present, or applies stretch on later setup. Also accepts `auto`, `0`, `off`,
`21:9` and positive finite floats (invariant culture); keys can be combined,
for example `set aspect=21:9 size=2/3`. `status` marks Auto with `(auto)`.


## Saves and game options

`Saves...` has separate **Named slots** and **Backups** tabs, each with sizes and
folder access. Backups offers **Back up now**, **Restore**
(with confirmation) and **Export backup**. Every Play backs up first. Missing/empty
saves are skipped; a backup failure stops Play. Restore refuses a running portable
Azahar and first backs up the current save. Ten newest snapshots per title are
kept in `<home>\backups\<16-digit TITLEID>\yyyyMMdd-HHmmss\` (same-second
requests get unique increasing timestamps). Restore stages the selected data before
retention can remove it, replaces the whole data folder, and rolls back a failed
swap. Linked paths and multiple matching saves are refused.

Save discovery follows `Home.ContentDir`'s existing ID0/ID1 enumeration:
`azahar\user\sdmc\Nintendo 3DS\<id0>\<id1>\title\00040000\<lowercase title low>\data\`
(`001b5000` Sun / `001b5100` Moon). Each snapshot contains `data/` plus a portable
relative `source.txt` for restoring a missing save. **Extdata is not included**:
Home.cs has no known extdata path. Nothing uses `%APPDATA%\Azahar`.

Named slots are independent of backup retention and stay until explicitly deleted.
Use **Save current to slot**, **Load**, **Rename**, **Delete** or **Export slot**.
Load confirms and uses the same staging, safety backup and rollback as restore.
A new save never overwrites a slot: choose another name or explicitly delete it.
Names contain 1-64 characters, are unique per game ignoring case, and cannot
contain control characters, `<>:"/\|?*`, leading/trailing whitespace or a trailing dot;
`.` and `..` are refused. Names are display labels, not filesystem paths.
Slots live in `<home>/slots/<UPPERCASE TITLEID>/<32-character GUID>/` with UTF-8
`name.txt`, `data/` and (for locally captured saves) `source.txt`. Renaming changes
only `name.txt`. Incomplete operations use `pending-<GUID>` and are not listed.

The dialog's **Export current save** and **Import ZIP to new slot** buttons use
file pickers. Exports require a new filename (existing files are refused). A ZIP
contains `data/` plus UTF-8 `manifest.txt`: `format=1`, `title_id`, UTC `date`
(export time), `source` (current / slot name / backup timestamp) and
`launcher_version` (assembly version). This records the launcher version, not a
claim about the version that originally wrote the save. Archives contain no ROM,
update, extdata or machine-specific SD identity. Exports cannot go into the
emulator, slots or backups directories. Close Azahar to export the current save.

Import accepts only this manifest-bearing ZIP format; raw saves lack reliable
title identification and are refused. The manifest title must match the selected
game. Import creates a new slot and never changes the active save. Without a name,
CLI import uses the ZIP filename without its extension. Unsafe, absolute or
traversing paths, Windows device/stream names, links, duplicate paths and unexpected
root entries are refused. Limits: 256 MiB uncompressed and 4096 entries, including
the manifest. Actual decompressed lengths are also checked. Imported slots carry
no `source.txt`: loading uses the existing save or the sole portable SD identity.
If no unambiguous identity exists, start the game once first. No other Azahar home
is consulted. A manifest declares a game; it does not authenticate or inspect the
save's internal format. Use trusted saves and keep your own backups.

Game options reads existing INIs when the window opens and saves through the same
comment-preserving plugin editor:

- Graphics: Keep Azahar's setting (default; no resolution writes), Native (1x),
  High (3x), Ultra (5x). Presets set `[Renderer] resolution_factor` and
  `resolution_factor\default=false`, and remove per-game resolution overrides.
  Keep preserves the last Azahar value; it does not reset it. 21:9 looks best
  on High or Ultra. Verified against Azahar 2126.2's
  [config.cpp](https://github.com/azahar-emu/azahar/blob/2126.2/src/citra_qt/configuration/config.cpp)
  and [settings.h](https://github.com/azahar-emu/azahar/blob/2126.2/src/common/settings.h).
- Battle menu: Classic / Bar / Rail writes `[battle] layout`.
- Text and battle speed: Normal / Fast / Instant writes `[speed] instant_text`
  (off/on/on), `battle_messages` and `battle_bars` (normal/fast/instant).
  Mixed hand-edited speeds show Custom (keep) and survive saving.
- Button icons: Auto (with detected family), Xbox, PlayStation, Nintendo, 3DS.
  Auto refreshes `[controls] glyphs` on setup and Play. Raw Input HID controller
  vendor IDs select Microsoft `045E`, Sony `054C`, Nintendo `057E`; unknown/absent
  controllers fall back to Xbox for Gamepad/Custom, 3DS for Keyboard. Multiple
  recognised devices use the first enumerated one. Icon drawing is future plugin
  work; this launcher only writes the key. No additional packages are needed.

`Cheats?` ships six own-code cheats, disabled initially, embedded from `cheats/`.
Only enabled markers change: code lines and boot safety guards remain intact.
Files go to `azahar\user\cheats\<UPPERCASE TITLEID>.txt`; Sun and Moon retain
independent selections, synced at setup and Play. Always shiny and its undo
cannot be enabled together. Changes apply on the next game start; close Azahar
before saving cheats. Back up first. Money/BP and item changes remain after
turning a cheat off. Item cheats require an existing slot and clear its NEW badge
and Free Space position. Disable Always shiny and restart to undo the memory patch,
or select its undo for the next start. Moon cheats still need in-game validation.

Additional CLI commands (all accept `--home DIR`):

```
UsumOneScreen backup
UsumOneScreen backups
UsumOneScreen restore yyyyMMdd-HHmmss
UsumOneScreen slot save "before Elite Four"
UsumOneScreen slot load "before Elite Four"
UsumOneScreen slot list
UsumOneScreen slot rename "before Elite Four" "Champion"
UsumOneScreen slot delete "Champion"
UsumOneScreen export PATH.zip [--slot NAME | --backup yyyyMMdd-HHmmss]
UsumOneScreen import PATH.zip [--name NAME]
UsumOneScreen cheats
UsumOneScreen cheats on "Max money (9,999,999)"
UsumOneScreen cheats off "Max money (9,999,999)"
UsumOneScreen set graphics=keep|native|high|ultra
UsumOneScreen set glyphs=auto|xbox|playstation|nintendo|3ds
UsumOneScreen set layout=rail instant_text=on battle_messages=fast battle_bars=fast
```

The `list` alias also lists backups. CLI load, restore and delete are explicit
requests and do not prompt; the window asks
for confirmation. Synthetic tests cover save discovery, retention, pre-restore
backup, missing-save recovery, title isolation, named slot lifecycle/name validation,
slot retention and failed-safety-backup immutability, ZIP round trips, title mismatch,
traversal/link/duplicate refusal, extdata exclusion, cheat code preservation and shiny
exclusion, resolution presets/Keep, vendor mapping/fallbacks, option readback,
battle/speed writes and layout at 100%/150%. Source-only builds without the optional
embedded plugin use a synthetic adjacent plugin in tests. No emulator is started.
