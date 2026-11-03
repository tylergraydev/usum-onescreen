# Player app: launcher or Azahar fork?

Written 2026-11-02. The question: how should a player get from "I have my own Ultra Sun dump" to "the game is running, top screen only, with the plugin", without the manual steps in [PLAYING.md](PLAYING.md)?

**Recommendation: (a), a launcher that drives a portable copy of stock Azahar.** It does everything a player needs today. A fork only adds the soft reset fix, and that fix is better sent upstream as a small Azahar PR (Tyler's call, since it is public). The launcher is built: [`player/`](../player/README.md).

## The two routes

**(a) Launcher.** A small Windows app that makes its own portable Azahar copy (the `user` folder next to `azahar.exe`, the trick `tools/instance.py` already uses), writes the config, installs the plugin and the player's update, checks the dump and starts Azahar with the ROM.

**(b) Fork.** Our own build of Azahar with a "USUM One Screen" mode: no game list, the single-screen layout fixed, the plugin loader on and the plugin built in, ROM checks in the emulator itself.

## Comparison

| | (a) Launcher | (b) Fork |
|---|---|---|
| Effort to first version | Small: about 1 000 lines of C#, no emulator build. Done in this session. | Large: an Azahar build environment (MSVC, Qt 6, CMake, ~40 submodules), a Windows CI that builds it, then the UI work on top. Weeks, not days. |
| Keeping up with Azahar | Only the config keys and two command-line flags (`-i`, `-f`) are relied on. They have been stable for years (they come from Citra). A new Azahar works unless it renames a key. | Every Azahar release is a rebase of our UI changes onto a moving Qt frontend (`citra_qt.cpp` alone is 4 400 lines). Falling behind means missing emulation fixes. |
| Licence | Azahar is used as is, never modified or redistributed by us: the launcher copies the player's own install. Nothing to publish beyond our own code. If we ever bundle Azahar binaries, we must also give its source (see below). | Must publish the fork's complete source under the same licence with every binary, keep notices, mark changes. Must not call it "Azahar" in a way that suggests it is the official build. |
| What the player installs | Azahar (official) + our zip | Our build only |
| Soft reset crash | Not fixable. Still crashes ([SOFT-RESET.md](SOFT-RESET.md)). | Fixable: clear the restored plugin loader context in `System::Reset`. |
| Top screen only | Yes: Azahar's "Single Screen" layout (`layout_option = 1`), status bar and filter bar off, optional fullscreen. The menu bar stays in windowed mode (Azahar has no setting to hide it; fullscreen hides it). | Yes, and the menu bar can go too. |
| Installing the update CIA | Through `azahar.exe -i`, which on Windows shows a "Success"/"Failure" box; the launcher closes it itself. | Can call the installer directly. |

### Licence details (checked 2026-11-02 against github.com/azahar-emu/azahar)

- Azahar **2126.2** (the version we test with) ships `license.txt` = **GPL v2**, and its source headers say "Licensed under GPLv2 or any later version".
- Azahar **master** was relicensed on 2026-10-03 (commit `86a9f923`, "Update project license to GPLv3+, was GPLv2+"); `LICENSE.txt` is now **GPL v3**, headers say "GPLv3 or any later version", and `misc/licenses/gplv2.txt` is kept for files still under v2 (commit `0ab6399c`). The GitHub API reports `gpl-3.0`.
- So a fork that follows master is **GPL-3.0-or-later**. Distributing a fork's binary means: the complete corresponding source of that exact build (including our changes and build scripts) is available to everyone who gets the binary, under GPL-3.0; the licence text and copyright notices stay; modified files carry a notice of the change. A public repo at the tagged commit does this.
- A launcher that only starts Azahar as a separate program is not a derivative work of it, so the launcher's own licence is free. Our plugin is GPL-3.0 already ([LICENSING.md](LICENSING.md)), so GPL-3.0 for the launcher keeps one licence for the project.
- Bundling unmodified Azahar binaries in our zip is allowed, but then we also have to provide Azahar's source for that version (a link to the matching upstream tag is the usual way, with the licence text in the zip). The launcher avoids that by copying the player's own Azahar install.

### What only a fork can do

1. **Fix the soft reset crash** (L+R+Start). `System::Reset` restores the old `PluginLoaderContext` with a handle from the dead process; the fix is to clear `plugin_loaded`, `plugin_process_id`, `memory_changed_handle`, `memory_region` and `plg_event`/`plg_reply` after the restore. That also makes the plugin load again after the reset (upstream issue azahar-emu/azahar#2569). It is a few lines, which is why **an upstream PR beats a fork** here: every player gets it in the next Azahar, and we keep using stock builds.
2. Hide the menu bar in windowed mode, and remove menus a player doesn't need (game list, multiplayer, movie, debugging).
3. Install the update without a dialog box, and check ROMs inside the emulator.
4. Ship one download with the plugin built in.

Items 2 to 4 are polish. None of them stops a player from playing, and the launcher covers the same ground (it hides the dialog itself, checks the dump before Azahar sees it, and runs fullscreen if asked).

Until Azahar has the fix, the plugin-side mitigation in [SOFT-RESET.md](SOFT-RESET.md) (hide Start from the game while L and R are held) is still open as a decision for Tyler.

## What the launcher does

See [`player/README.md`](../player/README.md) for use and build. In short:

1. **Azahar**: finds the player's install (`C:\Program Files\Azahar` by default, or asks), copies it to `azahar\` next to the launcher and creates `azahar\user\`, which makes it portable. The player's own Azahar and `%APPDATA%\Azahar` are only read from (the program folder) and never written.
2. **The dump**: the player picks their Ultra Sun ROM (`.3ds`, `.cci`, `.cxi` or `.cia`) and the v1.2 update (`.cia`). The launcher reads the headers itself and refuses anything that is not title `00040000001B5000`, an update that is not `0004000E001B5000` version `0x0800`, or a dump that is still encrypted. Each refusal says what was found.
3. **Config**: plugin loader on, single-screen layout (top), custom textures off, update check off, status and filter bars off, close without confirming. Controller: "Keyboard" (Azahar's defaults), "Gamepad" (any controller, Xbox layout) or "Set in Azahar" (opens Azahar's own window to map buttons).
4. **Plugin**: copies `usum-onescreen.3gx` into `sdmc\luma\plugins\00040000001B5000\`, and edits `sdmc\usum-onescreen.ini` (panel key, touch key, map key, cursor, panel size and corner, auto panel) in place, keeping its comments.
5. **Update**: installs the update CIA with `azahar.exe -i` (and a CIA base game the same way), then checks the installed TMD says version `0x0800`.
6. **Play**: starts Azahar with the ROM, windowed or fullscreen.

It never ships, downloads or explains how to get game files or keys.

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

So the launcher accepts `0x0820`, and accepts `0x0800` with a warning (existing saves play; a new game doesn't start). Azahar 2126.2 can't install the encrypted update at all: `InstallCIA` refuses any CIA with encrypted content (exit code 7), even with `aes_keys.txt` in `sysdata`. Players need a decrypted dump.

## Not tested

The Gamepad preset with a real controller, "Map buttons in Azahar", fullscreen, a decrypted base-game `.cia` (none available), Ultra Moon (the launcher only takes Ultra Sun), and a player's Azahar that isn't 2126.2.
