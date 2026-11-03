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
| **L+R** | Show the bottom screen on the top screen when you need it: off → corner panel → full view → off. It stays as you set it. |
| **C-stick** | Move the touch cursor (a crosshair on the panel or full view). It starts slow for precise taps and speeds up while you hold it. |
| **ZL** | Touch the bottom screen at the cursor, for as long as you hold it. The crosshair turns red while touching. |
| **ZR** | In the overworld: show the map on the top screen (place name, island, your position and Rotom's map around you). **ZR** or **B** closes it. You can keep walking with it open. The X menu's **Map** opens it too. See [MAP.md](MAP.md). |

| **Up** (D-pad) | In the overworld, when the corner badge says *Rotom wants to talk*: start Rotom's chat. Rotom's question and its two answers then show at the bottom of the top screen: **Up/Down** choose, **A** answers. The Roto Loto shows there too (**A** goes on and stops the reel). See [ROTOM-FIELD.md](ROTOM-FIELD.md). |

Every other button works as normal.

### Naming a Pokémon

When the game asks for a nickname (Yes to *Would you like to give X a nickname?* after a catch), the keyboard appears on the top screen. D-pad or circle pad moves, **A** types the key, **B** deletes, **Y** is Shift, **X** types a space, **START** jumps to OK and **A** on OK confirms. The first letter is a capital; Shift only lasts for one character, as in the game.

## The bottom-screen view

The goal is native menus on the top screen (the X menu and the Pokédex have one already), so the bottom screen is hidden by default and **L+R** brings it up when a screen has no native menu yet. The corner panel is a half-size copy of the bottom screen in the lower-right corner of the top screen. Full view replaces the top screen with the bottom screen.

### Automatic view (optional)

Set `auto = on` in `usum-onescreen.ini` (or **Auto panel** in the plugin menu) and the plugin picks a view by scene instead. **L+R** then holds your choice until the next scene change.

| Scene | View |
|---|---|
| Title screen | off |
| Overworld | off |
| X menu (opened with X in the overworld), Quick Link, the save dialog, Festival Plaza | corner panel |
| Pokédex, Bag, Pokémon summary, Trainer Passport, Battle Video | corner panel |
| Battle | nothing: the battle menu is drawn on the top screen (see "Battles" below) |
| Party, PC boxes, Town Map, Options, Pokémon Refresh | full view |
| Talking to people, Poké Mart lists (top screen only) | off |
| Anything not listed yet | corner panel |

## Battles

Battles are played with buttons, no panel: when it is your turn a menu appears on the right of the top screen.

- **Up/Down** pick Fight, Bag, Pokémon or Run, **A** chooses.
- **Fight** lists your moves with their type, PP, and how effective each is against the opponent. **A** uses one, **B** goes back.
- **Bag** and **Pokémon** open the game's own screens; a small box on the top screen says what is highlighted (on the Poké Balls tab, which ball). Use **Left/Right** for the Bag's tabs, **Up/Down**, **A** and **B** as on the bottom screen. To catch: Bag → Poké Balls → a ball → A → A (Use).
- After a catch, **B** goes through the Pokédex page and the nickname question (No). Yes opens a keyboard on the bottom screen: use **L+R** to see it.
- When a Pokémon wants to learn a fifth move, the top screen lists its four moves and the new one (type, PP; power and accuracy of the highlighted one). **Up/Down** and **A** pick the move to forget, or **Don't learn**; **B** jumps to Don't learn. **A** again confirms, **B** goes back.
- When the game asks something else in battle (sending in the next Pokémon), a box at the top says so; the choices are on the bottom screen (**L+R**).

If something in a battle isn't covered (a double battle's target screen, for example), **L+R** still shows the bottom screen.

The native X menu and Pokédex keep the panel off while they show; in the Pokédex the panel comes back for its screens that have no native view yet (Habitat, Appearance, search, Evaluation once chosen).

## Known gaps

- Ultra Moon isn't supported yet (see Requirements).
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
