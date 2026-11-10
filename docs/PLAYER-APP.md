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
Graphics, Visual quality and Button icons; Saves and Cheats buttons sit below them. The layout
uses autosizing and scrolling for smaller windows and high DPI. Options read back
from the INIs on opening. Mixed speed settings remain Custom until changed.
Graphics defaults to Keep (no resolution writes); Native/High/Ultra set
`[Renderer] resolution_factor` to 1/3/5 with its `\default=false` flag, clearing
per-game resolution overrides. The key/section were checked against upstream
Azahar 2126.2 `config.cpp` and `settings.h`. 21:9 benefits from High/Ultra.

Visual quality offers Keep Azahar's setting (initial default), Off, Sharpen,
Anti-aliasing (FXAA-style), and Sharpen + AA. CLI:
`set visual_quality=keep|off|sharpen|aa|sharpen-aa`. The choice persists as
`visual_quality` in `usum-onescreen-app.ini` and is reapplied during Setup/Play.
Keep preserves custom shaders. Other choices write `[Layout] pp_shader_name`
with `pp_shader_name\default=false` and remove per-game shader overrides
(including `\use_global`) so they inherit the launcher selection.
Off writes exactly `None (builtin)`; installed shader files may remain unused.

Verified against Azahar **2126.2**:

- [config.cpp](https://github.com/azahar-emu/azahar/blob/2126.2/src/citra_qt/configuration/config.cpp)
  reads the shader from `Layout`; per-game `\use_global=false` selects local settings.
- [settings.h](https://github.com/azahar-emu/azahar/blob/2126.2/src/common/settings.h)
  identifies OpenGL as API 1, Vulkan as API 2; the Windows OpenGL-enabled build
  defaults to OpenGL when the API is absent or `\default=true`.
- [OpenGL renderer](https://github.com/azahar-emu/azahar/blob/2126.2/src/video_core/renderer_opengl/renderer_opengl.cpp)
  uses `None (builtin)` for ordinary presentation, `Dubois (builtin)` for anaglyph,
  and the fixed Horizontal interlaced shader. Sharpen and FXAA are not built-ins.
- [Vulkan renderer](https://github.com/azahar-emu/azahar/blob/2126.2/src/video_core/renderer_vulkan/renderer_vulkan.cpp)
  compiles fixed presentation shaders and does not load `pp_shader_name`.

Verified on [upstream master loader](https://github.com/azahar-emu/azahar/blob/master/src/video_core/renderer_opengl/post_processing_opengl.cpp):
ordinary user shaders are `user/shaders/<name>.glsl`, directly in `shaders/`,
and the config value is the case-sensitive filename stem, without an extension,
path, or `(builtin)` suffix. Anaglyph shaders use `shaders/anaglyph/`.
The loader supplies Dolphin-style GLSL helpers and the renderer supplies the version.
Verified on Azahar 2126.2 (2026-11-09, OpenGL, NVIDIA driver): with
`pp_shader_name=usum-sharpen-aa` and the file in `user/shaders/`, the game booted to the
field with no shader errors in `azahar_log.txt`; a deliberately broken
`user/shaders/usum-broken.glsl` logged "Error compiling fragment shader", so the
2126.2 loader does read that folder.
The existing `shaders/vulkan/` directory does not establish Vulkan post-processing support.

`player/shaders/post-process.glsl` is original GPL-3.0-only code, embedded in the
EXE and installed as `usum-sharpen.glsl`, `usum-aa.glsl`, or
`usum-sharpen-aa.glsl` when selected. AA is a small original FXAA-style
contrast/edge filter, not NVIDIA's reference FXAA implementation. Combined mode
attenuates sharpening where AA acts. It may soften text; GPU cost and appearance
remain **UNVERIFIED in-game**, as does compilation on non-NVIDIA drivers.

The UI disables Visual quality with a reason when the effective API is not
OpenGL, including the selected title's API override. It also disables it for
stereoscopic 3D, which uses other presentation paths. Change those options
explicitly in Azahar and reopen the launcher. The launcher never switches API
or stereo mode. A saved effect is suspended if those settings later change;
its selection remains saved for a return to OpenGL/2D. CLI `off` still works on
Vulkan. The UI reads the current INI shader when reopened; unknown shaders show
Keep. Changes apply on the next start.

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
