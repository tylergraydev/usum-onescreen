# Playing with USUM One Screen

USUM One Screen is a 3GX plugin for the Azahar 3DS emulator. It lets you play Pokémon Ultra Sun with only the top screen showing: when the game needs the bottom (touch) screen, the plugin draws it on top of the top screen, and a cursor you move with the C-stick lets you tap it.

This is a beta. It has only been tested on Windows.

## Requirements

- **Azahar** with the 3GX plugin loader. Tested on Azahar **2126.2** (build `HEAD-219d743`, Windows MSVC build). Older or newer versions may work but have not been tried.
- **Pokémon Ultra Sun (USA)**, title ID `00040000001B5000`, with the **v1.2 update** installed (title version `0x0800`). Use your own legally dumped copy of the game and update; this project does not provide them or explain how to get them.
- `usum-onescreen.3gx` from the project's Releases page (or built from source, see `README.md`).

Ultra Moon is **not supported yet**: its addresses have not been found, so the plugin's startup check fails and it does nothing.

## Install

1. In Azahar, open **Emulation → Configure… → System**, and on the **System** tab tick **Enable 3GX plugin loader** (under "3GX Plugin Loader"). This is a global setting; it does not appear in the per-game configuration.
2. Open Azahar's emulated SD card with **File → Open Azahar Folder → SDMC Folder**. On Windows with default settings it is `%APPDATA%\Azahar\sdmc\`.
3. Create the folder `luma\plugins\00040000001B5000\` inside it and copy `usum-onescreen.3gx` there:

   ```
   %APPDATA%\Azahar\sdmc\luma\plugins\00040000001B5000\usum-onescreen.3gx
   ```

4. Start Ultra Sun. A notice `USUM One Screen: Pokemon Ultra Sun 00040000001B5000 v2048 (0x0800)` appears at the top of the screen when the plugin has loaded.

If the notice says `unsupported game …`, the game or its update version doesn't match; check that the v1.2 update is installed. If it says `hook check failed`, see "Reporting a bug" below.

### Custom textures must be off

If you use an HD texture pack, turn off **Emulation → Configure… → Graphics → Enhancements → Use custom textures**. With the HD texture pack we tested, Ultra Sun crashes at boot when custom textures are on (`Surface is not suitable for custom upload` in Azahar's log). This happens with or without the plugin.

## Controls

| Button | What it does |
|---|---|
| **ZR** | Cycle the bottom-screen view by hand: off → corner panel → full view. Your choice holds until the next scene change, then the automatic view takes over again. |
| **C-stick** | Move the touch cursor (a crosshair on the panel or full view). It starts slow for precise taps and speeds up while you hold it. |
| **ZL** | Touch the bottom screen at the cursor, for as long as you hold it. The crosshair turns red while touching. |

Every other button works as normal.

## What shows automatically

The plugin watches which scene the game is in and picks a view:

| Scene | View |
|---|---|
| Title screen | off |
| Overworld | off |
| X menu (opened with X in the overworld) | corner panel |
| Battle, Pokédex, Bag | corner panel |
| Party | full view |
| Anything not listed yet | corner panel |

The corner panel is a half-size copy of the bottom screen in the lower-right corner of the top screen. Full view replaces the top screen with the bottom screen.

## Known gaps

- Ultra Moon isn't supported yet (see Requirements).
- The X menu is detected from X and B presses. If you close it by touch, the plugin doesn't notice and keeps the panel up; press ZR to hide it, or open and close the menu with X.
- The panel covers part of the top screen while it's shown.
- Only some scenes are mapped; anything else gets the corner panel.
- Only tested in Azahar 2126.2 on Windows. Real 3DS hardware has not been tried.

## Reporting a bug

Each time the game starts, the plugin rewrites `usum-onescreen.log` in the root of the emulated SD card (`%APPDATA%\Azahar\sdmc\usum-onescreen.log`). It records the detected game and version, the startup check, scene changes and view changes.

When something goes wrong:

1. Note what you were doing and which screen you were on.
2. Quit the game (the log is kept until the next start).
3. Open an issue on the project's GitHub page and attach:
   - `usum-onescreen.log`
   - your Azahar version (shown in the window title, or **Help → About Azahar**)
   - a screenshot if the problem is visual

Do not attach game files, save files or keys.
