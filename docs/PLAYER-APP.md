# Player app: launcher or Azahar fork?

Written 2026-11-02. The question: how should a player get from "I have my own Ultra Sun or Ultra Moon dump" to "the game is running, top screen only, with the plugin", without the manual steps in [PLAYING.md](PLAYING.md)?

**Recommendation: (a), a launcher that drives a portable copy of stock Azahar.** It does everything a player needs today. A fork only adds the soft reset fix, and that fix is better sent upstream as a small Azahar PR (Tyler's call, since it is public). The launcher is built: [`player/`](../player/README.md).

## The two routes

**(a) Launcher.** A small Windows app that makes its own portable Azahar copy (the `user` folder next to `azahar.exe`, the trick `tools/instance.py` already uses), writes the config, installs the plugin and the player's update, checks the dump and starts Azahar with the ROM.

**(b) Fork.** Our own build of Azahar with a "USUM One Screen" mode: no game list, the single-screen layout fixed, the plugin loader on and the plugin built in, ROM checks in the emulator itself.

## Comparison

| | (a) Launcher | (b) Fork |
|---|---|---|
| Effort to first version | Small: about 1 000 lines of C#, no emulator build. Done in this session. | Large: an Azahar build environment (MSVC, Qt 6, CMake, ~40 submodules), a Windows CI that builds it, then the UI work on top. Weeks, not days. |
| Keeping up with Azahar | Only the config keys and two command-line flags (`-i`, `-f`) are relied on. They have been stable for years (they come from Citra). A new Azahar works unless it renames a key. | Every Azahar release is a rebase of our UI changes onto a moving Qt frontend (`citra_qt.cpp` alone is 4 400 lines). Falling behind means missing emulation fixes. |
| Licence | The Windows zip bundles unmodified Azahar 2126.2 with its licence and a corresponding source notice (see below). The launcher copies it into a writable portable home. | Must publish the fork's complete source under the same licence with every binary, keep notices, mark changes. Must not call it "Azahar" in a way that suggests it is the official build. |
| What the player installs | One launcher exe (plugin embedded; downloads Azahar 2126.2), or the offline zip | Our build only |
| Soft reset crash | Not fixable. Still crashes ([SOFT-RESET.md](SOFT-RESET.md)). | Fixable: clear the restored plugin loader context in `System::Reset`. |
| Top screen only | Yes: Azahar's "Single Screen" layout (`layout_option = 1`), status bar and filter bar off, optional fullscreen. The menu bar stays in windowed mode (Azahar has no setting to hide it; fullscreen hides it). | Yes, and the menu bar can go too. |
| Installing the update CIA | Through `azahar.exe -i`, which on Windows shows a "Success"/"Failure" box; the launcher closes it itself. | Can call the installer directly. |

### Licence details (checked 2026-11-02 against github.com/azahar-emu/azahar)

- Azahar **2126.2** (the version we test with) ships `license.txt` = **GPL v2**, and its source headers say "Licensed under GPLv2 or any later version".
- Azahar **master** was relicensed on 2026-10-03 (commit `86a9f923`, "Update project license to GPLv3+, was GPLv2+"); `LICENSE.txt` is now **GPL v3**, headers say "GPLv3 or any later version", and `misc/licenses/gplv2.txt` is kept for files still under v2 (commit `0ab6399c`). The GitHub API reports `gpl-3.0`.
- So a fork that follows master is **GPL-3.0-or-later**. Distributing a fork's binary means: the complete corresponding source of that exact build (including our changes and build scripts) is available to everyone who gets the binary, under GPL-3.0; the licence text and copyright notices stay; modified files carry a notice of the change. A public repo at the tagged commit does this.
- A launcher that only starts Azahar as a separate program is not a derivative work of it, so the launcher's own licence is free. Our plugin is GPL-3.0 already ([LICENSING.md](LICENSING.md)), so GPL-3.0 for the launcher keeps one licence for the project.
- Bundling unmodified Azahar binaries in our zip is allowed, but then we also have to provide Azahar's source for that version (a link to the matching upstream tag is the usual way, with the licence text in the zip). The Windows release now bundles 2126.2, includes its licence, and ships `AZAHAR-SOURCE.txt` identifying `azahar-unified-source-2126.2.tar.xz` (SHA-256 `85c8b56fac703df04720125044b8ff887997911858a6cde545dfb1107ba4cd26`) at https://github.com/azahar-emu/azahar/releases/tag/2126.2. See [RELEASING.md](RELEASING.md) for the pinned binary download and hash.

### What only a fork can do

1. **Fix the soft reset crash** (L+R+Start). `System::Reset` restores the old `PluginLoaderContext` with a handle from the dead process; the fix is to clear `plugin_loaded`, `plugin_process_id`, `memory_changed_handle`, `memory_region` and `plg_event`/`plg_reply` after the restore. That also makes the plugin load again after the reset (upstream issue azahar-emu/azahar#2569). It is a few lines, which is why **an upstream PR beats a fork** here: every player gets it in the next Azahar, and we keep using stock builds.
2. Hide the menu bar in windowed mode, and remove menus a player doesn't need (game list, multiplayer, movie, debugging).
3. Install the update without a dialog box, and check ROMs inside the emulator.
4. Integrate the plugin directly into the emulator (the launcher now embeds the separate plugin in its exe).

Items 2 to 4 are polish. None of them stops a player from playing, and the launcher covers the same ground (it hides the dialog itself, checks the dump before Azahar sees it, and runs fullscreen if asked).

Until Azahar has the fix, the plugin-side mitigation in [SOFT-RESET.md](SOFT-RESET.md) (hide Start from the game while L and R are held) is still open as a decision for Tyler.

## What the launcher does

See [`player/README.md`](../player/README.md) for use and build. In short:

Players run the single `UsumOneScreen-<tag>.exe`, drag their own decrypted ROM and matching v1.2 update onto it (or a zip of them), and press Play. It embeds the plugin and downloads verified Azahar 2126.2 on first setup. The existing Windows zip remains the offline option.

1. **Azahar**: uses `azahar-bundled\azahar.exe` next to the launcher as its source, even with `--home`. If absent, uses explicit `--azahar`, otherwise downloads tested 2126.2 before installed-Azahar discovery. Downloads verify the pinned archive SHA-256 before extraction and the licence git blob SHA-1; temporary files are removed. Failures explain how to select an installed copy. The licence is saved as `azahar\LICENSE-azahar.txt`. Copies it to the home's `azahar\` and creates `azahar\user\` to make it portable. The source bundle/install is never modified.
2. **The dump**: detects Ultra Sun (USA), title `00040000001B5000`, or Ultra Moon (USA), `00040000001B5100`, from the ROM (`.3ds`, `.cci`, `.cxi` or `.cia`). Requires its matching update (`0004000E001B5000` / `0004000E001B5100`), version `0x0820`. Sun alone also accepts rebuilt `0x0800` with the existing new-game warning. Moon refuses `0x0800` and other versions. Encrypted dumps are refused because Azahar 2126.2 cannot install them; each refusal gives a reason.
3. **Config**: plugin loader on, single-screen layout (top), custom textures off, update check off, status and filter bars off, close without confirming. Controller: Keyboard, Gamepad or Custom (map buttons in Azahar).
4. **Plugin**: uses explicit `--plugin`, then the adjacent `.3gx`, then the embedded resource via a temporary home file. Copies `usum-onescreen.3gx` into `sdmc\luma\plugins\<detected title>\`, and edits `sdmc\usum-onescreen.ini` in place, keeping comments.
5. **Update**: installs the update CIA with `azahar.exe -i` (and a CIA base game the same way), then checks that game's installed TMD for the requested version.
6. **Play**: remembers the ROM, boot path and selected `title_id` in `usum-onescreen-app.ini`; starts Azahar windowed or fullscreen. The window and CLI `check`, `setup`, `status` identify the game. Old Sun-only state files remain usable.

It never ships, downloads or explains how to get game files or keys.

## Save protection and game options

The launcher now backs up the selected game's portable save before every Play,
with a Saves dialog and `backup`, `backups` (also `list`), `restore NAME` CLI commands.
It follows Home.cs's existing ID0/ID1 enumeration under
`azahar/user/sdmc/Nintendo 3DS/<id0>/<id1>/title/00040000/<001b5000 or 001b5100>/data/`.
Snapshots live in `<home>/backups/<UPPERCASE TITLEID>/<yyyyMMdd-HHmmss>/`, with
`data/` and a relative source path. Ten newest are retained per title. Restore
requires Azahar to be closed, stages the chosen snapshot, takes a fresh backup,
and replaces the save with rollback on a failed swap. Missing saves are skipped;
ambiguous or linked paths are refused. Extdata is excluded because Home.cs has
no known extdata path. The real `%APPDATA%/Azahar` remains unused.

Permanent named slots are separate from the ten rotating backups:
`<home>/slots/<UPPERCASE TITLEID>/<GUID>/` contains UTF-8 `name.txt`, `data/`,
and a relative `source.txt` for locally captured saves. Names are case-insensitively
unique per game, 1-64 characters, without path/control characters or edge whitespace
or trailing dots. Save/load/rename/delete work through the Saves dialog and
`slot save NAME`, `slot load NAME`, `slot list`, `slot rename OLD NEW`,
`slot delete NAME`. Loading uses the existing staged restore and safety backup;
a failed safety backup leaves the active save intact. Slots are never pruned.

Saves has Named slots and Backups tabs, with sizes, folder access and selected-item
export. A bottom row exports the current save or imports a ZIP into a new named
slot. Loading/restoring/deleting confirms in the UI; explicit CLI commands do not.
`export PATH [--slot NAME | --backup NAME]` exports to a new file; `import PATH
[--name NAME]` creates a slot (default name: filename stem). ZIPs contain only
`data/` and `manifest.txt` with format version 1, game `title_id`, UTC export
`date`, `source`, and `launcher_version` from the assembly. Import checks the
selected game's exact title, refuses missing metadata, traversal, absolute/device
paths, links, duplicate names and unexpected entries, and caps uncompressed data
at 256 MiB / 4096 entries. Raw files/folders are deliberately unsupported because
they lack reliable title metadata. The manifest is a declaration, not proof of
a save's internal game format. Export cannot overwrite an existing file or write
under the emulator, slots or backups trees. Imported slots omit SD identities;
load uses the current save or a sole existing identity, otherwise asks the player
to start the game once. Synthetic regression tests cover these operations;
interactive Saves dialog and real save imports still need player validation.

A Game options group holds Widescreen, Battle menu, Text and battle speed,
Graphics and Button icons; Saves and Cheats buttons sit below them. The layout
uses autosizing and scrolling for smaller windows and high DPI. Options read back
from the INIs on opening. Mixed speed settings remain Custom until changed.
Graphics defaults to Keep (no resolution writes); Native/High/Ultra set
`[Renderer] resolution_factor` to 1/3/5 with its `\default=false` flag, clearing
per-game resolution overrides. The key/section were checked against upstream
Azahar 2126.2 `config.cpp` and `settings.h`. 21:9 benefits from High/Ultra.

Controller Auto uses Raw Input's HID vendor IDs, restricted to Generic Desktop
joystick/gamepad/multi-axis usages: Microsoft 045E, Sony 054C, Nintendo 057E.
Unknown/no pad falls back to Xbox for Gamepad/Custom and 3DS for Keyboard.
Setup and Play refresh `[controls] glyphs`; explicit choices persist. Drawing
these icons is a later plugin feature. Real USB/Bluetooth controllers remain
unverified. Battle writes `[battle] layout`; speed writes `[speed] instant_text`,
`battle_messages`, `battle_bars` using the existing plugin values.

Own-code cheat files are embedded in the launcher and installed per title in
`azahar/user/cheats/<UPPERCASE TITLEID>.txt`. The dialog and CLI only change
`*citra_enabled` markers, preserving the drafted guards and codes. Always shiny
and undo are mutually exclusive. Edits require a closed Azahar and apply next
start; the dialog warns players to back up first. Turning off save-data cheats
does not revert their changes. Moon cheats need in-game validation. Synthetic
launcher tests exercise the new file operations and options without starting Azahar.

## Tested (2026-11-03, Azahar 2126.2, test homes under `analysis/private/instances/`)

- `check` against real files: the decrypted `.cci` is accepted; the genuine decrypted v1.2 update (`0x0820`) is accepted; the rebuilt `0x0800` copy is accepted with a warning; the encrypted base `.cia` and encrypted update are refused (encrypted); Fire Emblem ROMs, a DLC CIA, a text file and the `.3gx` are refused with the title or reason.
- `setup` from nothing: copied Azahar 2126.2, wrote the config, installed the plugin, installed the update with `azahar -i` (the dialog was closed by the app), read the version back from the installed TMD.
- `play` (`player1`, the `0x0800` update and a copy of a test save): Azahar logged `Layout_LayoutOption: 1`, `Layout_SwapScreen: false`, `System_PluginLoader: true`, `Utility_CustomTextures: false`; the plugin logged `hooks: all ok`; title → field (`scene: FieldRo`), top-screen shot taken with `tools/shot.py`.
- **New game** (`player4`, no save, the genuine `0x0820` update, the plugin from PR #56): the plugin logged `Pokemon Ultra Sun 00040000001B5000 v2080 (0x0820)` and `hooks: all ok`; the language screen showed on the top screen and took D-pad/A; the game went on into Kukui's intro (shots taken).
- The window, drawn with `UsumOneScreen snapshot` (empty and ready states).

## The new-game stall (solved: the `0x0800` update file)

Genuine v1.2 is title version `0x0820` (v2.2.0); Tyler's encrypted Ultra Sun and Ultra Moon updates both say so. The decrypted copy the project first tested with is a rebuild whose TMD says `0x0800`. Its `code.bin` is the same, so it plays with an existing save, but a new game on it stops at the first-boot language screen:

- The game loads `LangSelect`, a name lookup in a resource table finds a null table (`0x318194` → `0x35B59C`, string compare `0x4B76A0`), the assert at `0x35B6B4` calls the fatal handler `0x1074BC`, and the main thread spins in `0x3FD518: nop; b 0x3FD518` (read through Azahar's GDB stub). Azahar logs `unmapped Read32 @ 0x0 / 0x4 / 0x80 at PC 0x0035B6E8`. With the main thread spinning, the plugin never writes its boot line.
- It happens with the plugin loader off too, on OpenGL and Vulkan, with the region-free patch off, in Old 3DS mode, and with extdata present. Base v1.0 gets past the screen. Changing only the installed TMD's version to `0x0820` doesn't help, so it is the file's content (the code is identical, so most likely its RomFS), not the number.
- With the genuine update (decrypted locally from Tyler's dump, see docs/PORTING.md) a new game gets past the screen, through the plugin and the launcher alike (above).

So the launcher accepts `0x0820` for both games, and for Sun only accepts `0x0800` with a warning (existing saves play; a new game doesn't start). Azahar 2126.2 can't install the encrypted update at all: `InstallCIA` refuses any CIA with encrypted content (exit code 7), even with `aes_keys.txt` in `sysdata`. Players need a decrypted dump.

## Not tested

The Gamepad preset with a real controller, "Map buttons in Azahar", fullscreen, a decrypted base-game `.cia` (none available), Ultra Moon through the launcher and a fresh bundled-zip setup for each game, and a player's Azahar that isn't 2126.2.

## Launcher regression checks (2026-11-03)

The Release launcher builds on .NET Framework 4.8. `python player/test.py` passes using synthetic headers only, without game content or starting Azahar: Sun/Moon validation, encrypted dumps, supported/refused versions, cross-game update rejection, selected-title state and plugin paths, installed CIA boot paths, bundle priority and immutability, fallback source selection, and legacy Sun state. These checks do not replace the in-game and fresh release-zip checks above.

## Single-exe regression coverage

`dotnet build player/UsumOneScreen.csproj -c Release` and `python player/test.py` cover the single-exe changes using synthetic data and local `file:///` downloads only. Tests check archive and licence hash rejection, temporary-file cleanup, source fallback, embedded/adjacent/explicit plugin priority, nested game/update zips, zip-slip flattening into `games\`, reuse by size and parsed headers, and extracted ROM state. GUI setup and zip extraction run on workers so the window stays responsive. Browse, drag/drop and CLI share the extraction path. Download overrides are documented in [player/README.md](../player/README.md). Live GitHub download, interactive drag/drop and emulator execution are separate manual checks.


## HD texture packs and texture dumping

Open **Game options > Texture packs...** for the currently selected game. Install a
player-supplied ZIP, select its name, then **Use selected**. Install does not enable
it. Only one pack is materialised per game. **Off** disables replacements but keeps
the last copy; **Remove** deletes the library entry and, if selected, disables and
removes its materialised copy. Close Azahar before changes; they apply next start.
Select the other game in setup to manage its packs independently.

The launcher ships only loader/tooling, never textures or game assets. Players
supply their own packs and must have permission to use/distribute the artwork.
Dumped textures may contain copyrighted game assets; do not redistribute them
without the rights holder's permission.

Portable home layout (created on demand):

```text
texture-packs/<TITLEID>/<opaque pack id>/name.txt
texture-packs/<TITLEID>/<opaque pack id>/data/   # launcher library
azahar/user/load/textures/<TITLEID>/            # selected pack copy
azahar/user/dump/textures/<TITLEID>/            # maker output
azahar/user/config/qt-config.ini
azahar/user/config/custom/<TITLEID>.ini
```

Ultra Sun uses `00040000001B5000`; Ultra Moon uses `00040000001B5100`.
Switching stages a complete copy before renaming it into place, with rollback on
copy/config failure. Copies avoid administrator rights and junction hazards, at
the cost of extra disk space (library + active copy + temporary switch copy).
The launcher owns the whole active title folder once enabled; keep edits in your
source pack. It refuses to replace a nonempty unmanaged folder: move it aside
first. Off retains the last pack and its hash options for makers.

ZIPs may contain wrapper folders ending in a matching title-ID folder. Images
must all be under that folder; mixed/other title IDs are refused. Without a title
folder, the dialog asks you to confirm the game; CLI requires `--game TITLEID`.
Flat ZIPs must place `pack.json`, if present, at their root. Names come from the
ZIP filename and must be unique per game (case insensitive). Only lowercase
`.png`, `.dds`, `.ktx`, optional `pack.json`, and `readme`, `readme.txt`, or
`readme.md` are accepted. Wrapper readmes outside the title folder are ignored.
Validation rejects traversal, absolute/device paths, links, duplicate paths,
invalid image signatures, more than 100,000 entries, and compressed or expanded
size over 4 GiB. It does not fully decode images or validate pack.json semantics;
use trusted packs compatible with your Azahar version. Supplied pack.json is
preserved byte for byte. If absent, the launcher generates Azahar's template
options: `skip_mipmap=false`, `flip_png_files=true`, `use_new_hash=true`.
Existing library entries without metadata receive it when enabled. The dialog
and CLI warn that a pack is "made from legacy-hash textures" when its supplied
metadata has `options.use_new_hash=false` or omits that option. Older packs may
need legacy hashing; their supplied metadata is never silently converted.

**Preload textures** uses additional RAM. **Async texture loading** streams in the
background; preloading takes precedence. **Dump textures** is for pack makers and
slows the game. **Open dump folder** creates/opens the selected game's output
folder. Disable dumping when finished.

For pack makers: dump while playing, close Azahar, then choose **Make pack from
dump folder** and name the new library entry. It copies the selected game's
images (including subfolders) and generates new-hash pack.json, ignoring any
metadata in the dump. It leaves the dump unchanged and does not activate the
pack. Select the new entry and **Use selected** to test your edited textures.
Names, image signatures, links, entry count and expanded size use the same
safety limits as installation. Only images are copied; other dump files are
ignored. Azahar 2126.2 may leave the dump without pack.json because its directory
does not exist yet when PrepareDumping attempts to write the template.

```powershell
UsumOneScreen.exe texture-pack list
UsumOneScreen.exe texture-pack from-dump "My edited dump"
UsumOneScreen.exe texture-pack install "My pack.zip"
UsumOneScreen.exe texture-pack install "Flat pack.zip" --game 00040000001B5000
UsumOneScreen.exe texture-pack use "My pack"
UsumOneScreen.exe texture-pack off
UsumOneScreen.exe texture-pack remove "My pack"
UsumOneScreen.exe set preload_textures=off async_custom_loading=on dump_textures=on
UsumOneScreen.exe set dump_textures=off
```

All commands accept `--home DIR`. Settings write `[Utility]` with
`key\default=false` beside each value. The selected title also gets
`key\use_global=false`; the other title's effective value is preserved before a
global value changes, so Sun/Moon do not inherit each other's dump/pack choices.
Azahar loads the per-game file before logging global settings and copies its
values into qt-config.ini at boot. The launcher reads the per-game value whenever
`key\use_global=false`; `key\default=true` or an absent overridden value means
Azahar's default (async on, custom/preload/dump off), even if the global file
contains a conflicting value.
No graphics, plugin, ROM, or save settings are changed.

Source references for **Azahar 2126.2**:
[texture loader/dumper](https://github.com/azahar-emu/azahar/blob/2126.2/src/video_core/custom_textures/custom_tex_manager.cpp),
[user path construction](https://github.com/azahar-emu/azahar/blob/2126.2/src/common/file_util.cpp),
[directory constants](https://github.com/azahar-emu/azahar/blob/2126.2/src/common/common_paths.h), and
[Qt settings](https://github.com/azahar-emu/azahar/blob/2126.2/src/citra_qt/configuration/config.cpp).
The loader scans nested title folders, accepts PNG/DDS/KTX, and reads pack.json
hash/mipmap options and filename mappings. Normal names follow
`tex1_<width>x<height>_<hash>_<format>_mip<level>.png`; dumps use that PNG form.
Keep dumped names or supply pack.json mappings. DDS requires mipmap skipping off.
Without pack.json Azahar uses legacy hashes, which differ from new dump hashes;
the launcher supplies new-hash metadata so these dumps can load correctly.
Dumping skips sizes that are not powers of two.

**Live verified on 2026-11-09** by the player: Azahar 2126.2, Ultra Sun,
Vulkan, NVIDIA. Dumping produced 1,054 files by the field in
`user/dump/textures/00040000001B5000/`, named
`tex1_<w>x<h>_<hash>_<fmt>_mip0.png`, without pack.json. Loading recoloured
textures from `user/load/textures/00040000001B5000/` worked with template
`use_new_hash=true`; without metadata nothing was replaced and the log reported
"using legacy defaults". Both folder paths and new-hash dump/load compatibility
are live verified. Per-game precedence and `\default=true` forcing the default
were also verified live. Ultra Moon, other GPUs/renderers, and arbitrary
community-pack compatibility remain separate manual checks.
