# Plan: Linux and Steam Deck launcher (backlog #21)

Written 2026-11-09. This is a plan only; no product code changes come with it.

Backlog #21 says "Linux and Steam Deck launcher. The DS widescreen project already ships a Deck build". The Windows launcher (`player/`, see [PLAYER-APP.md](PLAYER-APP.md)) is a .NET Framework 4.8 WinForms exe. This document covers what exists on Linux, the options for us, and a phased recommendation.

Tags used below:
- **[V]** verified this session: source, release metadata or a page was read; the URL or file:line is given.
- **[U]** unverified: inferred or from memory. Check it before building on it.

All `player/src` line numbers refer to `origin/main` at `ae4d696`.

## 1. What exists

### Azahar on Linux

| Fact | Status |
|---|---|
| Current release is **2126.2**, published 2026-10-08. It is still `releases/latest`, so it is also the version we pin on Windows. | [V] `gh api repos/azahar-emu/azahar/releases/latest` |
| The Linux desktop build is one asset named **`azahar.AppImage`** (no version in the name), 103 684 600 bytes, `sha256:af037f8b775198f636b14bcfe63fb401b02246ebfefe1025bb905ce042556960`. URL: https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar.AppImage | [V] release asset metadata |
| There is no Linux tarball of the Qt app. There is a libretro core (`azahar-libretro-linux-x86_64-2126.2.zip`, sha256 `2223444b…dc26e`), but it is useless to us because it is not the Qt frontend and does not read `qt-config.ini`. | [V] release asset list |
| The AppImage is built with linuxdeploy plus its Qt plugin, using Clang, with the Wayland platform plugins bundled. | [V] `.ci/linux.sh` and `CMakeModules/BundleTarget.cmake` at tag 2126.2 |
| The Flatpak on Flathub is `org.azahar_emu.Azahar`, version 2126.2 (2026-10-09), on KDE runtime 6.11. Its `finish-args` include **`--filesystem=host:ro`**, which makes host files read-only inside the sandbox. | [V] https://flathub.org/api/v2/appstream/org.azahar_emu.Azahar, https://github.com/flathub/org.azahar_emu.Azahar (`org.azahar_emu.Azahar.json`) |
| **User directory on Linux** (`SetUserPath`, `src/common/file_util.cpp` at 2126.2): if **the current working directory** contains a `user/` folder, that folder is the user dir (portable mode). Otherwise the data dir is `$XDG_DATA_HOME/azahar-emu`, config is `$XDG_CONFIG_HOME/azahar-emu`, and cache is `$XDG_CACHE_HOME/azahar-emu`. Windows behaves differently: it checks for `user/` next to the **exe**. | [V] file_util.cpp `SetUserPath`; `common_paths.h` (`EMU_DATA_DIR "azahar-emu"`) |
| The Flatpak's user dir would be `~/.var/app/org.azahar_emu.Azahar/...` because XDG paths are remapped. Because host files are `:ro`, a portable `user/` folder on the host could not be written from inside the Flatpak. | [U] inferred from the finish-args |
| The plugin loader is platform-neutral. `plgldr.cpp` has no `#if` blocks. It loads `<SDMC>/luma/plugins/{:016X}/*.3gx`, with the title ID in upper case, which matches `Dump.Title` (`X16`, `Dump.cs:52`). | [V] `src/core/hle/service/plgldr/plgldr.cpp:108-140` at 2126.2 |
| Cheat files are `{:016X}.txt`, upper case, which matches the launcher. | [V] `src/core/cheats/cheats.cpp:66` |
| Command-line flags `-i`, `-f` and `-w` exist on every platform. On non-Windows, `ShowCommandOutput` writes to stdout instead of a message box, so **`-i` shows no "Success"/"Failure" dialog on Linux**. The exit codes are the same (0, or InstallStatus + 2). | [V] `src/citra_qt/citra_qt.cpp:225-232, 312-342` |
| `-i` is parsed inside `GMainWindow`, so Qt is initialised and a display (or `QT_QPA_PLATFORM=offscreen`) is needed even for an install. | [U] inferred from where the parsing sits |
| The AppImage runtime keeps the caller's working directory, and linuxdeploy's AppRun does not `cd`, so the portable `user/` trick works when we start it with cwd = the home's `azahar/` folder. | [U] Not run. This is the first thing Phase 0 checks. |

### Steam Deck basics

| Fact | Status |
|---|---|
| Azahar on the Deck usually comes from EmuDeck (AppImage), RetroDECK (Flatpak) or Flathub. Players set it up in Desktop Mode and play in Game Mode through Steam shortcuts or Steam ROM Manager. | [V] https://manual.emudeck.com/tricks/azahar, https://retrodeck.readthedocs.io/en/latest/wiki_emulator_guides/azahar/azahar-guide/ (search results; pages not read in full) |
| A non-Steam game is added in Desktop Mode with Steam > Games > Add a Non-Steam Game to My Library > browse (file type "All Files"). It is then started in Gaming Mode with a gamepad controller layout. | [V] Fulldex's HOW-TO-LINUX-APPIMAGE.txt (below) |
| In Game Mode, Steam Input gives games a virtual Xbox-style pad (Valve vendor ID `28DE`). Azahar's SDL `maptype:all` mapping should work with it. | [U] |
| The Deck screen is 1280x800 (16:10). Our Auto aspect rule (≥1.7 picks 16:9, see `PluginSettings`) would pick **Original** on the Deck. | [U] that the Deck is 1280x800, from common knowledge. [V] the thresholds are in PLAYER-APP.md. |
| SteamOS's root filesystem is read-only, and `/home/deck` is writable. AppImages run there (Fulldex states SteamOS 3.5+). | [U] the read-only root. [V] Fulldex's SteamOS claim. |

### The "DS widescreen project": Pokémon Fulldex (mamesj)

The project behind the backlog line is **mamesj/pokemon-ds-widescreen**, now renamed **https://github.com/mamesj/pokemon-fulldex** ("Pokémon DS Widescreen"; it is linked under that name from bernarmillan/pokemon-es-in-usa). [V] `gh repo view` shows the redirect, 973 stars, updated 2026-11-10.

How it ships Linux and Deck builds [V] (release `fulldex-2026-11-08`, its README and HOW-TO-LINUX-APPIMAGE.txt):
- Assets: `pokemon-ds-wide-pc-linux-x86_64.AppImage` with a `.AppImage.zsync` file, so Gear Lever and other AppImage managers can update it themselves; `…-linux-x86_64.tar.gz`; aarch64 builds; and `SHA256SUMS.txt`. A plain-text HOW-TO comes with each platform.
- It needs glibc 2.38+, which it describes as "Ubuntu 24.04, Fedora 39, SteamOS 3.5 or newer".
- The tarball is portable (`roms/`, `saves/` and `cheats/` sit next to the program). The AppImage keeps its data in `~/.local/share/pokemon-ds-wide/`.
- Deck flow: unpack it in Desktop Mode, add it as a non-Steam game, play in Gaming Mode with a gamepad layout, set 3D resolution to 720P. Its own UI works with a controller: the game list takes A to play, Y for settings, and Start/Guide for the pause menu. A "Desktop shortcuts" setting writes `.desktop` entries, so each game shows up in Steam's "Add a Non-Steam Game" list.
- It is a native C++ frontend around an embedded emulator core. Ours drives an external Azahar instead, so we can copy its **packaging and Deck flow**, but not its architecture.

## 2. What in `player/src` is Windows-specific

About 3 200 lines in total. Most of it is portable file and INI logic.

**Portable as is** (plain System.IO, zip and crypto), apart from path-separator and case fixes:
- `Dump.cs` (282 lines: header parsing), `Ini.cs` (200), `PluginSettings.cs` (91), `GameOptions.cs` (61), `Controls.cs` (53: SDL strings, platform-neutral), `Cheats.cs` (79), `GameFiles.cs` (115), `Setup.cs` (117), `SaveSlots.cs` (260), `Backups.cs` (142).
- The core of `Cli.cs` (325).

**Windows-specific, with what replaces it:**

| Where | What | Linux replacement |
|---|---|---|
| `Program.cs:1-19`, `MainForm.cs` (548 lines), `OptionDialogs.cs` (181 lines), `Cli.cs:114-131` (window, `snapshot`) | WinForms and System.Drawing | A new UI (Avalonia), or CLI only at first |
| `Win32.cs:11-20, 76-92` `CloseWindowTitled` (EnumWindows/PostMessage), used at `Home.cs:295` | Closes the `-i` result box | Not needed: Linux `-i` prints to stdout [V] |
| `Win32.cs:94-106` `ProcessPath` (QueryFullProcessImageName), `Home.cs:318-327` `RunningPid` matches processes named `azahar` | Is our Azahar running? | Read `/proc/<pid>/exe`. With a FUSE-mounted AppImage the process path is `/tmp/.mount_*`, so extract the AppImage (see Phase 1) or use a pid/lock file. |
| `Win32.cs:33-69` `DetectGlyphs` (Raw Input HID vendor IDs), used at `GameOptions.cs:44`, `MainForm.cs:269, 409` | Controller glyph detection | Read `/sys/class/input/js*/device/id/vendor` or `/proc/bus/input/devices`. Treat Valve `28DE` (Steam Input) as Xbox [U]. |
| `Win32.cs:110-117` `UseParentConsole` (AttachConsole), `Cli.cs:40` | Console for a GUI-subsystem exe | Not needed (a Linux binary always has stdio) |
| `Home.cs:178` `Screen.PrimaryScreen.Bounds` (WinForms) for Auto aspect | Monitor size | Avalonia `Screens`, or `xrandr`/env, or default Original on the Deck |
| `Home.cs:27, 73, 77, 87-88, 97-108, 119-120`, `AzaharDownload.cs:52-72` | `azahar.exe`, `uninst.exe`, Program Files / LocalAppData discovery, the Windows msvc zip pin, version read from exe bytes | `Exe` becomes a per-platform path (the extracted AppRun). Pin `azahar.AppImage` by SHA-256. Discovery looks in `~/Applications`, the EmuDeck folder [U] and `PATH`. The version comes from the pin (the AppImage is squashfs, so the byte regex won't find it). |
| `Home.cs:360-366` `Detached`: `WorkingDirectory = Root`, `UseShellExecute = true` | Start Azahar without inheriting stdio | **cwd must be `AzaharDir`**, because Linux portable mode keys on cwd [V]. Redirect stdio to a log file instead of relying on ShellExecute. |
| `Home.cs:114, 119, 387-390`, `SaveSlots.cs:21, 120-125`, `Backups.cs:122` | `TrimEnd('\\')`, `user\\` prefix, case-insensitive `SamePath`, Windows device names and reserved characters | Use `Path.DirectorySeparatorChar` and ordinal (case-sensitive) comparison on Linux. Keep the stricter name rules on both platforms so exported ZIPs stay portable. |
| `Backups.cs:19` `FileAttributes.ReparsePoint` | Refuses links | This works on .NET 6+ on Linux for symlinks (`LinkTarget`) [U]. Use `FileSystemInfo.LinkTarget != null`. |
| `OptionDialogs.cs:106` `Process.Start(folder){UseShellExecute}` | Opens a folder | `xdg-open` |
| `AzaharDownload.cs:49` `ServicePointManager…Tls12`, `WebRequest` | Download | `HttpClient` (`WebRequest` is obsolete on .NET 6+ but still works) |
| `UsumOneScreen.csproj` `net48`, `UseWindowsForms`, `app.manifest`; `build.ps1`; `test.py:19, 242, 394` (exe path, PowerShell) | Build and tests | Multi-target, add `build.sh`, make `test.py` platform-aware |
| Unmerged: `origin/claude/update-check` `UpdateCheck.cs` (JavaScriptSerializer, `Application.ExecutablePath`, `.exe` swap helper, MessageBox); `origin/claude/texture-packs` `TexturePacks.cs` (file logic only) | Self-update; texture packs | On Linux, update with AppImage zsync or by downloading the new AppImage. Texture packs should port as is. |

Watch out for **case sensitivity**. Windows hides case mismatches. The save paths use lower-case `title/00040000/001b5000` (`Backups.cs`, `Home.cs:241-248`). Azahar's NAND/SD layout should be the same on Linux because it is the same source, but it has never run on a case-sensitive filesystem [U]. The plugin and cheat directory case match [V].

## 3. Options

| Option | What | Effort | For | Against |
|---|---|---|---|---|
| **(a) Cross-platform .NET 10 + Avalonia**, sharing logic | Move the portable files into a `UsumOneScreen.Core` library (multi-target `net48;net10.0`). Windows keeps WinForms on net48. Linux gets a CLI first, then an Avalonia window. Publish as a self-contained single-file `linux-x64`, wrapped in an AppImage. | Core split and Linux platform layer: 2-4 days. CLI MVP: +1-2 days. Avalonia UI with feature parity: 1-2 weeks. | One implementation of dump checks, INI, saves and cheats. `test.py` covers both. Avalonia 12.1.6 is current and MIT [V]. | It is a second UI to maintain. The self-contained binary is about 70-100 MB [U]. Avalonia gamepad navigation needs extra work. |
| (a′) Port everything to .NET 10 and Avalonia on Windows too | One UI everywhere | Larger (redo the WinForms UI, retest Windows) | One codebase | Breaks the "no runtime, small exe" Windows story (net48 ships with Windows). Risky just after v0.1. |
| **(b) Shell or Python setup tool** | `usum-onescreen-setup.sh`/`.py`: download and verify the AppImage, write `qt-config.ini` and the plugin INI, copy the plugin, `-i` the update, create a `.desktop` entry and a launch script | 2-3 days | Smallest. No .NET on Linux. SteamOS has Python 3 [U]. | It reimplements `Dump.cs`, `Ini.cs` and the save logic a second time, so they can drift apart. No saves, slots or cheats UI. Bad for Game Mode. |
| (c) Windows launcher under Wine/Proton | Run `UsumOneScreen.exe` through Proton | Low to start, high to make work | No new code | Needs wine-mono or .NET 4.8 under Wine [U]. It would drive the **Windows** azahar.exe under Wine (slow, and Vulkan through a translation layer) or have to start a Linux binary across the Wine boundary. Raw Input and window handling are fragile. Not recommended. |
| (d) Deck-friendly flow (on top of a or b) | `play` as the Steam shortcut target (`usum-onescreen play --fullscreen`), a `.desktop` per game, a recommended Steam Input layout, and controller-only navigation in the launcher | +2-4 days on top of (a) | Matches Fulldex's flow | Needs a real Deck to test. The Steam Input template can't be shipped as a file we control [U]. |

## 4. Recommendation

Take **(a) with (d) on top. Keep the Windows WinForms launcher unchanged on net48.** Split out a shared `Core`, ship a **Linux CLI first** (MVP), then a small Avalonia window, then the Deck flow. Use the **official AppImage**, extracted into the home, as Linux's Azahar. Don't support the Flatpak: its `host:ro` sandbox and remapped paths break our isolated portable home.

Why: about 70% of `player/src` is already portable, and it carries the subtle rules (dump validation, `\default=false`, staged save restore, ZIP import checks). A second implementation in Python (b) would drift from it. Wine (c) emulates the wrong half of the problem.

### Phases

**Phase 0: spike (half a day, on WSL2 Ubuntu or a Deck).** WSL2 Ubuntu is present on Tyler's PC [V]. WSLg graphics have not been checked [U].
1. Download `azahar.AppImage` 2126.2 and check its SHA-256.
2. Run `./azahar.AppImage --appimage-extract`.
3. Create `azahar/user/`. With cwd = `azahar/`, run `AppRun -i update.cia` and confirm the stdout line, the exit code, and that the files land in `azahar/user/`, not `~/.local/share/azahar-emu`.
4. Write the launcher's `qt-config.ini`, copy the plugin, and boot with `-w`. Confirm `hooks: all ok` in `sdmc/usum-onescreen.log`.

This settles the two [U] items that matter most: portable mode keyed on cwd, and the plugin loading on Linux.

**Phase 1: Core and Linux CLI (MVP, about 4-6 days).**
- `player/core/` (multi-target `net48;net10.0`): the portable files from §2, plus an `IPlatform` interface with `AzaharExe`, `IsRunning`, `DetectGlyphs`, `ScreenSize`, `OpenFolder` and `StartDetached`.
- `Platform.Windows` wraps the current `Win32.cs`. `Platform.Linux` uses `/proc`, `/sys` and `xdg-open`.
- `player/linux/` is a `net10.0` console app reusing `Cli.cs` without `Window` and `snapshot`, published self-contained as `linux-x64` single-file.
- The default home is `$XDG_DATA_HOME/usum-onescreen`. The AppImage mount is read-only, so `AppDomain.BaseDirectory` can't be the home.
- `AzaharDownload` pins `azahar.AppImage` (SHA-256 above), extracts it into `<home>/azahar/` and saves the licence as on Windows.
- Launch with cwd = `<home>/azahar`.
- `test.py` runs the same synthetic tests against the Linux build in WSL. A CI job builds on `ubuntu-latest`.
- Release asset `usum-onescreen-<tag>-linux-x86_64` (plain binary plus README), with the same GPL source notice for Azahar as the Windows zip.

**Phase 2: Desktop window (about 1-2 weeks).** An Avalonia window with the same groups as `MainForm` (drop files, game options, Saves and Cheats dialogs). Package it as an AppImage with `.zsync` (Fulldex-style) so Gear Lever can update it. The Linux update check uses zsync or downloads the new AppImage, instead of the Windows exe swap.

**Phase 3: Steam Deck (about 2-4 days plus device testing).**
- An "Add to Steam" helper: write a `.desktop` file for `usum-onescreen play --fullscreen`, so it appears in Steam's "Add a Non-Steam Game" list (Fulldex's approach). Optionally write `shortcuts.vdf` directly [U]; needs Tyler's OK.
- Recommend the Steam Input "Gamepad" template.
- Auto aspect on 16:10 picks Original, or offer 16:9 with stretch.
- Suggest Graphics "Native" or "High" for the Deck's GPU [U].
- Controller navigation in the Avalonia window, for Game Mode use of the Saves and Cheats screens.
- A HOW-TO-STEAM-DECK.txt like Fulldex's.

### Risks

- Portable mode depends on the cwd, not a flag [V]. If a future Azahar changes `SetUserPath`, the player's real `~/.local/share/azahar-emu` gets used silently. Mitigation: after launch, check that `azahar/user/log/` was written, and refuse to continue otherwise.
- The AppImage asset is named `azahar.AppImage` without a version [V], so the URL by tag is stable but the name doesn't show the version. Rely on the SHA-256 pin.
- `-i` may need a display [U]. Headless tests need `QT_QPA_PLATFORM=offscreen`.
- Case sensitivity in save, backup and slot paths has never been tested.
- The soft reset crash and the menu bar are the same as on Windows (Azahar-side).
- Game Mode (gamescope) fullscreen behaviour and Azahar's Qt menus with a controller are untested [U].
- Self-contained .NET binaries need glibc and ICU. Set `InvariantGlobalization=true` to drop ICU [U].
- .NET 8 and 9 reach end of support on 2026-11-10 [V] (https://dotnet.microsoft.com/en-us/platform/support/policy/dotnet-core), so target **.NET 10 LTS** (supported to 2028-11-14).

### Tyler's decisions

1. **Path**: (a) shared Core + Linux CLI → Avalonia → Deck, as recommended? Or the quicker but duplicated (b) script?
2. **Keep Windows on net48 WinForms** (recommended), or move Windows to .NET 10 and Avalonia too?
3. **Flatpak**: unsupported (recommended). Or also support the player's existing EmuDeck or Flatpak Azahar as a source?
4. **Packaging**: AppImage with zsync self-update (Fulldex model), a tarball, or both? Download Azahar on first setup (as on Windows), or bundle it (100 MB, plus GPL source notice)?
5. **Steam integration**: a `.desktop` file only, or also write Steam's `shortcuts.vdf` (it touches the player's Steam config)?
6. **Hardware**: is a Steam Deck available for Phase 3? Without one, Deck support stays "should work", as it does for Fulldex.
7. Should the unmerged update-check branch be made platform-aware before it merges, or after?
