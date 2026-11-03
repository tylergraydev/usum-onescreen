# Playing with USUM One Screen

USUM One Screen is a 3GX plugin for the Azahar 3DS emulator. It lets you play Pokémon Ultra Sun with only the top screen showing: the game's menus are drawn on the top screen, and for anything that isn't yet, the plugin can draw the bottom screen on top of the top screen, with a cursor you move with the C-stick to tap it.

This is a beta. It has only been tested on Windows.

## Requirements

- **Azahar** with the 3GX plugin loader. Tested on Azahar **2126.2** (build `HEAD-219d743`, Windows MSVC build). Older or newer versions may work but have not been tried.
- **Pokémon Ultra Sun (USA)**, title ID `00040000001B5000`, with the **v1.2 update** installed (title version `0x0820`; a decrypted copy reporting `0x0800` works too). Use your own legally dumped copy of the game and update; this project does not provide them or explain how to get them.
- `usum-onescreen.3gx` from the project's Releases page (or built from source, see `README.md`).

**Pokémon Ultra Moon (USA)** v1.2 (`00040000001B5100`, `0x0820`) is accepted: every address the plugin uses is the same as Ultra Sun's. It has not been played with the plugin yet. For Ultra Moon the plugin goes in `luma\plugins\00040000001B5100\`.

## Windows launcher (optional)

The app in [`player/`](../player/README.md) checks your own Ultra Sun dump and v1.2 update, copies your installed Azahar into its own portable folder, enables the plugin loader, selects the top screen only, turns custom textures off, installs the plugin and update, and starts the game. Its window offers Keyboard / Gamepad presets, mapping in Azahar, plugin settings and windowed / fullscreen play. It does not modify your existing Azahar profile or provide game files. See [PLAYER-APP.md](PLAYER-APP.md) for setup, recorded tests and open issues; the manual install below remains available.

**Before using it:** a brand-new game with no save currently stalls at the first language screen on the tested v1.2 setup, including with the plugin loader off. The launcher accepts only version `0x0800`, but genuine v1.2 updates report `0x0820` (the plugin accepts both), and with a genuine update a new game gets past the language screen (docs/PORTING.md). The recorded launch test reached the field using an existing test save; it does not prove a new game works. Real gamepad input, mapping in Azahar, fullscreen, decrypted base-game CIA installation and Azahar versions other than 2126.2 were not tested.

## Manual install

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

These work everywhere:

| Button | What it does |
|---|---|
| **L+R** | Show the bottom screen on the top screen when you need it: off → corner panel → full view → off. It stays as you set it. |
| **C-stick** | Move the touch cursor (a crosshair on the panel or full view). It starts slow for precise taps and speeds up while you hold it. |
| **ZL** | Touch the bottom screen at the cursor, for as long as you hold it. The crosshair turns red while touching. |
| **Select** | Open the plugin's own menu (CTRPF's default hotkey): Bottom screen on top, Auto panel, View settings, Reload settings file. |

Every other button works as normal. Each native screen below also takes the buttons listed for it.

## Native screens

The aim is that you rarely need the bottom screen. These screens are drawn on the top screen with the panel off. The game's own screen keeps running on the hidden bottom screen, and the plugin presses its buttons for you, so the game does every change itself.

### In the overworld

| Button | What it does |
|---|---|
| **X** | Open the X menu (below). |
| **ZR** | Show the map: place name, island, inside/outside, your position and Rotom's map around you, live while walking. **ZR** or **B** closes it. You can keep walking with it open. See [MAP.md](MAP.md). |
| **Up** (D-pad) | When the corner badge says *Rotom wants to talk*: start Rotom's chat. Rotom's question and its two answers show at the bottom of the top screen: **Up/Down** choose, **A** answers, and Rotom's reply follows. The Roto Loto shows there too (**A** goes on and stops the reel). The map steps aside while Rotom asks. See [ROTOM-FIELD.md](ROTOM-FIELD.md). |

`map_key` in the settings file changes the map key.

### X menu

D-pad and **L/R** move, **A** opens, **B** closes. It lists Pokémon, Pokédex, Bag, Quick Link, Festival Plaza, Trainer Passport, Pokémon Refresh, Battle Video, Options, the map and Save, and starts on the entry the game's own X menu remembers. Pokémon, Pokédex, Bag, Trainer Passport, Battle Video, Options and the map open a native screen; Quick Link, Festival Plaza, Pokémon Refresh and Save open the game's own screens (use **L+R** to see them). QR Scanner is shown greyed out. See [XMENU.md](XMENU.md).

### Party and summary (X menu → Pokémon)

Shows name, gender, level, HP, status and held item. D-pad or circle pad moves between Pokémon. **A** opens Summary / Use item / Give item / Take item / Move / Cancel (Take appears only with an item held; Move needs two Pokémon), **Y** moves a Pokémon (swap), **B** closes. The summary also shows species, ability, nature and moves with type, category and PP; **Up/Down** change Pokémon, **Left/Right** change pages, **A** checks moves and **Y** toggles the game's own top page.

Use item offers Medicine, Items, TMs and Berries; Give item leaves out TMs. **L/R** or **Left/Right** change pocket, **Up/Down** pick an item, **A** uses or gives it, **B** goes back. Giving over a held item asks Yes / No (**B** is No); Take returns the held item to the Bag. TM teaching lists the four moves and *Don't learn*: **Up/Down**, **A** chooses; **B** does nothing on that list. Medicine, Give/Take and TM teaching were exercised in-game. Ether / Elixir, evolution stones and Rare Candy follow-ups are host-tested only and may need **L+R**; some unverified menu taps are refused. See [PARTY-UI.md](PARTY-UI.md).

### Bag (X menu → Bag)

**Up/Down** move, **L/R** or **Left/Right** change pocket, **A** opens an item's menu, **B** goes back or closes. **Use** (Medicine, Berries) and **Give** are native; other Use entries are greyed out and say to use the bottom screen. See [BAG.md](BAG.md).

### Pokédex (X menu → Pokédex, or Rotom's face)

The game's own keys: D-pad moves, **A** opens, **B** goes back, **Up/Down** on an entry page go to the previous / next seen entry. It lists each Pokédex's caught / seen counts, the Alola list and an island's entries. Habitat, Appearance, search and Evaluation have no native view; the panel is free for them. See [POKEDEX.md](POKEDEX.md).

### Options, Trainer Passport and Battle Video (X menu)

- **Options**: **Up/Down** pick a row, **Left/Right** change its value, **A** confirms. **B** leaves; if something changed the game asks first (**Up/Down** and **A** answer).
- **Trainer Passport**: **Left/Right** or **L/R** turn the stamp page, **B** leaves.
- **Battle Video**: **A** opens the Vs. Recorder, **B** goes back. A list of saved videos has no native view yet (use **L+R**).

See [NATIVE-SCREENS.md](NATIVE-SCREENS.md).

### PC boxes (at a Pokémon Center PC)

Shows the box and your party. The D-pad moves the game's cursor, **A** picks a Pokémon up and puts it down (swapping with one already there; box ↔ party and across boxes), **L/R** turn the box, **Y** shows the game's own details, **B** puts a held Pokémon back or leaves. Summary, Held Item, Marking, Release and the box list stay on the bottom screen (**L+R**). See [BOXES.md](BOXES.md).

### Naming a Pokémon

When the game asks for a nickname (Yes to *Would you like to give X a nickname?* after a catch), its QWERTY keyboard and the name typed so far appear on the top screen. D-pad or circle pad moves, **A** types the key, **B** deletes, **Y** is Shift, **X** types a space, **START** jumps to OK and **A** on OK confirms. The first letter is a capital; Shift only lasts for one character, as in the game. The game's name limit applies (12 characters for a nickname); the grid page is not mirrored. **L+R** hides the native keyboard in favour of the panel until the keyboard closes. See [KEYBOARD.md](KEYBOARD.md).

### Battles

Battles are played with buttons, no panel: when it is your turn a menu appears on the right of the top screen.

- **Up/Down** pick Fight, Bag, Pokémon or Run, **A** chooses.
- **Fight** lists your moves with their type, PP, and how effective each is against the opponent. **A** uses one, **B** goes back.
- **Pokémon** shows your party on the top screen. Move with the **D-pad**, **A** opens *Switch* / *Cancel*, **B** goes back (after a faint you must pick one). Switching to the active Pokémon, a fainted one or an Egg is refused.
- **Bag** shows the battle Bag's pockets and item counts on the top screen. **Left/Right** (or **L/R**) change pocket, **Up/Down** pick an item, **A** uses it: a ball is thrown; a Potion asks which Pokémon (it starts on the first one it would help), **A** again uses it. HP-only items are refused on a full-HP or fainted Pokémon. **B** goes back. To catch: Bag → Poké Balls → a ball → A.
- After a catch, **B** goes through the Pokédex page and the nickname question (No). **A** (Yes) opens the native keyboard above.
- When a Pokémon wants to learn a fifth move, the top screen lists its four moves and the new one (type, PP; category, power and accuracy of the highlighted one). **Up/Down** and **A** pick the move to forget, or **Don't learn**; **B** jumps to Don't learn. **A** again confirms, **B** goes back.
- When your Pokémon faints in a wild battle, *Use next Pokémon?* shows on the top screen: **Up/Down**, **A** (Use Next Pokémon opens the party). The Run outcome from this question was not seen in-game.
- When the game asks something else in battle, a box at the top says so; the choices are on the bottom screen (**L+R**).

For SOS / double battles, a target page is built and host-tested, but its target taps have not been verified in-game: **A** and **B** say to use the bottom screen. Use **L+R** to choose the target. See [BATTLE-UI.md](BATTLE-UI.md) and [MOVE-LEARN.md](MOVE-LEARN.md).

## The bottom-screen view (fallback)

Anything without a native screen still needs the bottom screen, so **L+R** brings it up. The corner panel is a half-size copy of the bottom screen in the lower-right corner of the top screen (`size = 2/3` in the settings file makes it bigger, at a small cost in speed). Full view replaces the top screen with the bottom screen. Move the touch cursor with the **C-stick** and touch with **ZL**.

While a native screen is up the panel stays off. If you turn it on by hand (**L+R**) the native screen steps aside until you leave that screen.

### Automatic view (optional)

Set `auto = on` in `usum-onescreen.ini` (or **Auto panel** in the plugin menu) and the plugin picks a view for scenes that have no native screen. **L+R** then holds your choice until the next scene change. It is off by default.

The defaults in the settings file's `[scenes]` section are: off for the title screen and overworld; the corner panel for the X menu's Quick Link, the save dialog, Festival Plaza and any scene not listed; the full view for Pokémon Refresh and the game's own Town Map. The Pokédex, Bag, Pokémon summary, Party, PC boxes, Options, Trainer Passport and Battle Video also have entries (corner panel or full view); those only apply when the native screen is off or doesn't cover that part of the game, since a native screen holds the panel off while it shows. Each scene's view can be changed in that section.

## Settings file

On the first run the plugin writes a commented `usum-onescreen.ini` in the root of the emulated SD card (`%APPDATA%\Azahar\sdmc\usum-onescreen.ini`). It sets the keys (`panel_key`, `touch_key`, `map_key`, cursor speed), the panel `size` and `corner`, `auto`, and the per-scene views. Unknown keys and bad values are skipped and logged. **Reload settings file** in the plugin menu (**Select**) reads it again without restarting. The full list is in the "Settings" section of [README.md](../README.md).

## Known gaps

- Ultra Moon is accepted but has not been played with the plugin yet (see Requirements).
- No native screen yet for the Ride Pager, Poké Pelago, Festival Plaza, Quick Link, Pokémon Refresh or Save. The Ride Pager and Pelago need a later save than the one the plugin was tested with, so they are untested. Use **L+R**.
- SOS / double targeting is host-tested only; use **L+R** to pick a target. Trainer battles haven't been played; no SOS call was reached during the documented search.
- Not native yet: the PC's Summary / Held Item / Release / Marking, the Pokédex's Habitat / Appearance / search / Evaluation, QR Scanner, saved Battle Video lists. The field Bag's TM and other follow-up uses still need the panel; Party TM teaching is native.
- Party Ether / Elixir, evolution stones and Rare Candy (including move learning after a level-up) have not been tried in-game; their follow-up logic is host-tested only. Battle Ether move selection, Pokémon Restore / Summary and the Y quick ball are not handled natively; battle Status Restore, Rotom Powers and Battle Items were not used in the documented testing. Z-moves and Mega Evolution were unavailable in the test save. Illusion names the real Pokémon rather than its disguise.
- The launcher's new-game and update-version blockers are described above; do not treat its existing-save launch test as a fresh-game pass.
- Controller input hasn't been tested on a real pad.
- The panel covers part of the top screen while it's shown.
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
