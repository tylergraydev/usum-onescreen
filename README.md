# USUM One Screen

Play Pokémon Ultra Sun and Ultra Moon on **one screen** in the [Azahar](https://github.com/azahar-emu/azahar) 3DS emulator.

USUM One Screen is a 3GX plugin (built with [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework)) that draws the game's bottom-screen menus on the top screen. You can hide the bottom screen and play with buttons only. Anything without a top-screen version yet can still be reached: **L+R** shows the bottom screen on the top screen, with a cursor to tap it.

> [!IMPORTANT]
> **No game files or keys come with this project.** You need your own legally dumped copy of Pokémon Ultra Sun or Ultra Moon (USA) and its v1.2 update. This project does not provide them or explain how to get them.

**Status:** beta. Tested only in Azahar 2126.2 on Windows; real 3DS hardware has not been tried. See [Limits](#limits).

## Supported games

| Game | Title ID | Update |
|---|---|---|
| Pokémon Ultra Sun (USA) | `00040000001B5000` | v1.2 (title version `0x0820`; a rebuilt `0x0800` copy also loads) |
| Pokémon Ultra Moon (USA) | `00040000001B5100` | v1.2 (title version `0x0820`) |

On any other game or version the plugin shows an `unsupported game` notice and does nothing else.

## Features

Native top-screen menus (the game's own screen keeps running on the hidden bottom screen, and the plugin presses its buttons for you):

- **Battle**: Fight / Bag / Pokémon / Run, the move list with type, PP, effectiveness and the selected move's category, power and accuracy; switching, items and catching. Three layouts: `classic`, `bar` and `rail`.
- **Move learning** and TM teaching.
- **X menu**, **Party** and summary, **Bag**, **Pokédex**, **PC boxes** (with each Pokémon's box icon), **Options**, **Trainer Passport**, **Battle Video**.
- **Nickname keyboard**.
- **Map** on **ZR**, live while you walk.
- **Rotom's prompts**: chats and the Roto Loto.
- **PC anywhere** (optional key): open the boxes from the overworld.

Extras:

- **Bottom-screen fallback**: **L+R** cycles off / corner panel / full view; **C-stick** moves a touch cursor and **ZL** taps.
- **Instant text and faster battles** (off by default).
- **Experimental widescreen** (16:9 / 21:9), with side bands beside full-screen menus.
- **Settings file** `usum-onescreen.ini`, written with comments on first run.

Controls and details for every screen: [docs/PLAYING.md](docs/PLAYING.md). What changed per version: [CHANGELOG.md](CHANGELOG.md).

## Quick start (Windows launcher)

The easiest way to play on Windows is the launcher in [`player/`](player/README.md):

1. Download `UsumOneScreen-<version>.exe` from [Releases](https://github.com/tylergraydev/usum-onescreen/releases) and put it in a folder you can write to.
2. Run it. On first setup it downloads Azahar 2126.2 and checks the download's SHA-256.
3. Drag your own **decrypted** Ultra Sun or Ultra Moon (USA) ROM and its matching **decrypted** v1.2 update onto the window (or a zip containing them).
4. Press **Play**.

The plugin is built into the exe. The launcher checks the game, update and version, sets up its own portable Azahar copy, and remembers your choices. Its window also has controller presets, plugin settings, graphics and battle-speed options, save backups and cheats. Encrypted dumps are refused, because Azahar 2126.2 cannot install them.

For offline setup, `usum-onescreen-<version>-windows.zip` includes Azahar 2126.2, its licence and source notice. Details, tests and open issues: [docs/PLAYER-APP.md](docs/PLAYER-APP.md).

## Manual install (any Azahar)

1. In Azahar: **Emulation → Configure… → System**, tick **Enable 3GX plugin loader**.
2. Copy `usum-onescreen.3gx` from [Releases](https://github.com/tylergraydev/usum-onescreen/releases) into the emulated SD card:
   - Ultra Sun: `sdmc\luma\plugins\00040000001B5000\usum-onescreen.3gx`
   - Ultra Moon: `sdmc\luma\plugins\00040000001B5100\usum-onescreen.3gx`

   On Windows the SD card is `%APPDATA%\Azahar\sdmc\` by default (**File → Open Azahar Folder → SDMC Folder**).
3. Start the game. A `USUM One Screen: …` notice appears when the plugin has loaded.

Keep Azahar's **Use custom textures** off: with an HD texture pack, Ultra Sun crashes at boot, with or without the plugin. Full steps and troubleshooting: [docs/PLAYING.md](docs/PLAYING.md).

## Settings

On first run the plugin writes `usum-onescreen.ini` at the root of the emulated SD card, with a comment on every line. **Select** opens the plugin menu, which can reload the file without restarting. Unknown keys and bad values are skipped and logged in `usum-onescreen.log` (same folder).

The defaults (the comments in the real file explain each one):

```ini
[display]
aspect = 0              ; widescreen: 0/off, 16:9, 21:9 or a number (docs/WIDESCREEN.md)
pillarbox = tint        ; side bands beside full-screen menus: tint, pattern or off

[battle]
layout = classic        ; native battle menu: classic, bar or rail

[speed]
instant_text = off      ; docs/FAST-TEXT.md
battle_messages = normal ; normal, fast or instant
battle_bars = normal     ; normal, fast or instant

[controls]
panel_key = L+R         ; cycles the bottom-screen view: off / panel / full
touch_key = ZL          ; touches at the cursor while held
map_key = ZR            ; opens and closes the map
pc_key = none           ; opens the PC boxes in the field, e.g. R+X (docs/PC-ANYWHERE.md)
cursor_move = cstick    ; cstick, dpad or circlepad
cursor_speed = 6        ; 1 to 20

[panel]
size = 1/2              ; 1/2 or 2/3 (docs/PERF.md)
corner = bottom-right   ; bottom-right, bottom-left, top-right, top-left
auto = off              ; on: the view follows [scenes]

[scenes]                ; view per scene when auto = on: off, panel or full
TitleMenu = off
FieldRo = off           ; overworld
FieldRo.xmenu = panel   ; X menu over the overworld
Zukan = panel           ; Pokédex
Battle = panel
main:00647FCC = full    ; Party
main:006488C8 = panel   ; Bag
Status = panel          ; Pokémon summary
Box = full              ; PC boxes
TownMap = full
TrainerPass = panel
Config = full           ; Options
Kawaigari = full        ; Pokémon Refresh
BattleVideoPlayer = panel
default = panel         ; any scene not listed
```

Keys for the `*_key` settings: `A B X Y L R ZL ZR Start Select Up Down Left Right`, joined with `+` for a combo, or `none`. Comments start with `;` or `#`; names and values are case-insensitive.

## Building from source

### Plugin (`.3gx`)

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS packages (devkitARM, libctru).
2. Add the ThePixellizerOSS package repo, as the [CTRPluginFramework README](https://gitlab.com/thepixellizeross/ctrpluginframework) describes. On Windows, in `C:\devkitPro\msys2\msys2_shell.bat`:

   ```bash
   if ! grep -Fxq "[thepixellizeross-lib]" /etc/pacman.conf; then echo -e "\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional" | tee -a /etc/pacman.conf > /dev/null; fi; if ! grep -Fxq "[thepixellizeross-win]" /etc/pacman.conf; then echo -e "\n[thepixellizeross-win]\nServer = https://thepixellizeross.gitlab.io/packages/x86_64/win\nSigLevel = Optional" | tee -a /etc/pacman.conf > /dev/null; fi
   ```

3. Install the framework and the 3GX packer:

   ```bash
   pacman -Sy && pacman -S libctrpf 3gxtool
   ```

4. Run `make` in the repo root (the path must not contain spaces). Output: `usum-onescreen.3gx`.

CI does the same in the `devkitpro/devkitarm` container; see [.github/workflows/build.yml](.github/workflows/build.yml) and [tools/ci/install-deps.sh](tools/ci/install-deps.sh).

### Host tests

The pure logic (settings parser, input capture, menu models, drawing) builds and runs on a PC with any g++ or clang++ (WSL works on Windows):

```bash
make -C tests
```

### Windows launcher

Needs the .NET SDK; the app targets .NET Framework 4.8. Build the plugin first so the exe can embed it, then:

```bash
dotnet build player/UsumOneScreen.csproj -c Release
```

`player/build.ps1` makes the release layout. See [player/README.md](player/README.md).

### Emulator tests

Scripted tests in isolated Azahar instances, a command channel into the running plugin, screenshots and memory tools: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). They need your own game dump.

## Limits

- Tested only in Azahar 2126.2 on Windows. Real 3DS hardware and other Azahar versions have not been tried.
- USA v1.2 only. Ultra Moon has had less testing than Ultra Sun, and a brand-new Ultra Moon game stalls in the opening bedroom scene in Azahar.
- With a rebuilt `0x0800` Ultra Sun update, a brand-new game stalls at the language screen (existing saves play). The genuine `0x0820` update works.
- No native screen yet for the Ride Pager, Poké Pelago, Festival Plaza, Quick Link, Pokémon Refresh or QR Scanner: use **L+R**. SOS / double battles need **L+R** for targeting.
- Widescreen is experimental: [docs/WIDESCREEN.md](docs/WIDESCREEN.md).
- Real game-controller input has not been tested.

The full list of known gaps is in [CHANGELOG.md](CHANGELOG.md) and [docs/PLAYING.md](docs/PLAYING.md).

## Documentation

| For | Read |
|---|---|
| Players | [PLAYING.md](docs/PLAYING.md), [PLAYER-APP.md](docs/PLAYER-APP.md), [WIDESCREEN.md](docs/WIDESCREEN.md), [FAST-TEXT.md](docs/FAST-TEXT.md), [PC-ANYWHERE.md](docs/PC-ANYWHERE.md) |
| Native screens | [BATTLE-UI.md](docs/BATTLE-UI.md), [XMENU.md](docs/XMENU.md), [PARTY-UI.md](docs/PARTY-UI.md), [BAG.md](docs/BAG.md), [POKEDEX.md](docs/POKEDEX.md), [BOXES.md](docs/BOXES.md), [MAP.md](docs/MAP.md), [KEYBOARD.md](docs/KEYBOARD.md), [MOVE-LEARN.md](docs/MOVE-LEARN.md), [ROTOM-FIELD.md](docs/ROTOM-FIELD.md) |
| Developers | [DEVELOPMENT.md](docs/DEVELOPMENT.md), [OFFSETS.md](docs/OFFSETS.md), [PORTING.md](docs/PORTING.md), [UI.md](docs/UI.md), [GAME-ASSETS.md](docs/GAME-ASSETS.md), [PERF.md](docs/PERF.md), [RELEASING.md](docs/RELEASING.md) |

## Contributing

Bug reports and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Please attach `usum-onescreen.log` to bug reports. Never attach game files, keys or saves.

## Licence and credits

Licensed under the [GNU General Public License v3.0 only](LICENSE) (`GPL-3.0-only`). Why: [docs/LICENSING.md](docs/LICENSING.md).

Third-party code and credits: [THIRD-PARTY.md](THIRD-PARTY.md).

Pokémon and Pokémon Ultra Sun / Ultra Moon are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc. This is an unofficial fan project, not affiliated with or endorsed by them. The plugin reads the game's own fonts, icons and text from your copy at runtime; none of the game's data is in this repository.
