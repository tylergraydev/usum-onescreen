# Changelog

All notable changes for players are listed here. Versions follow [Semantic Versioning](https://semver.org/); the version in `usum-onescreen.plgInfo` should match the release tag.

## [Unreleased]

### v0.1.0 (planned first beta)

Supports Pokémon Ultra Sun (USA) v1.2 (`00040000001B5000`, title version `0x0800`) in Azahar with the 3GX plugin loader. See [docs/PLAYING.md](docs/PLAYING.md).

#### Added

Native top-screen menus (the panel is off while they show; the game's own screen keeps running on the hidden bottom screen and the plugin drives it). Details and controls per screen are in [docs/PLAYING.md](docs/PLAYING.md).

- **Battle** (single battles): command bar (Fight / Bag / Pokémon / Run) and move list with type, PP and how effective each move is. **Up/Down** and **A**, **B** goes back. Bag and Pokémon open the game's own screens with a helper box saying what is highlighted. Catching works: Bag → Poké Balls → a ball, then the Pokédex page and the nickname question.
- **Move learning**: when a Pokémon wants a fifth move, the four moves and the new one are listed (type, PP; category, power and accuracy of the highlighted one). **Up/Down** and **A** pick the move to forget or *Don't learn*, **A** confirms, **B** goes back.
- **X menu**: **X** in the field shows Pokémon, Pokédex, Bag, Quick Link, Festival Plaza, Trainer Passport, Pokémon Refresh, Battle Video, Options, the map and Save as a grid (D-pad and L/R move, **A** opens, **B** closes). It starts on the entry the game's own X menu remembers. QR Scanner is shown but not available yet.
- **Keyboard**: the nickname keyboard after a catch (D-pad moves, **A** types, **B** deletes, **Y** Shift, **X** space, **START** jumps to OK).
- **Pokédex** (from the X menu or Rotom's face): the top level with each Pokédex's caught / seen counts and locked islands, the Alola list, an island Pokédex's entries around the cursor, and an entry page's number, name and caught / seen. The game's own keys drive it (D-pad, **A**, **B**; Up/Down on an entry go to the next seen one). Habitat, Appearance, search and Evaluation stay on the bottom screen.
- **Options, Trainer Passport, Battle Video** (from the X menu). Options: Up/Down pick a row, Left/Right change its value, **A** confirms, **B** leaves (asks first if something changed). Passport: the trainer card values and stamp pages (Left/Right or L/R turn the page). Battle Video: **A** opens the Vs. Recorder.
- **Bag**: the pockets and their items (counts, NEW marks), the party with HP, the item menu. **Use** (Medicine and Berries) and **Give** are native; other Use entries are greyed out and say to use the bottom screen. D-pad up/down move, **L/R** or left/right change pocket, **A** opens an item, **B** goes back or closes.
- **Map**: **ZR** in the overworld (or the X menu's **Map**) shows the place name, the island, inside/outside, the player's position and facing, and the game's Rotom Dex map around the player, live while walking. **ZR** or **B** closes it (`map_key` in the settings file). Place names are read from the game's own files at runtime; nothing of the game is shipped.
- **Rotom's prompts**: when Rotom wants to talk, a corner badge says so and **Up** starts the chat. Rotom's question and its two answers show at the bottom of the top screen (**Up/Down** choose, **A** answers, then Rotom's reply); the Roto Loto's steps show there too (**A** goes on and stops the reel).
- **Party and summary** (X menu → Pokémon): name, level, HP, status, held item. **A** opens Summary / Use item / Move, **Y** moves a Pokémon, **B** closes. The summary shows name, level, HP, item, ability, nature and moves with PP (Up/Down change Pokémon, **Y** shows the game's own page). Use item uses Medicine or Berries.
- **PC boxes**: at a Pokémon Center PC, the box (name and each slot's Pokémon) and the party. The D-pad moves the game's cursor, **A** picks up / puts down (swapping with one already there; box ↔ party and across boxes), **L/R** turn the box, **Y** shows the game's own details, **B** puts a held Pokémon back or leaves.
- **Bottom-screen fallback**: **L+R** cycles the bottom screen on the top screen off / corner panel (1/2 or 2/3 size, `size` in the settings file) / full view (`panel_key`). The copy runs on the emulated GPU, so the half panel and the full view cost Azahar a few percent of speed at most; 2/3 costs ~8% (docs/PERF.md). Screens without a native view, or parts of one (an item that asks a follow-up question, say), use it.
- **Touch cursor**: **C-stick** moves it, **ZL** touches the bottom screen at the cursor.
- **Automatic view** (optional, `auto = on`; off by default since native menus come first): picks off / panel / full per scene, and **L+R** then holds your choice until the next scene change. Scene views can be set in the `[scenes]` section.
- **Settings file** `usum-onescreen.ini` at the emulated SD root: keys, panel size and corner, auto view, per-scene views, written with comments on first run. Bad values are skipped and logged. The plugin menu (**Select**) can reload it.
- Startup check of the game, its version and every patched function; the plugin does nothing on an unsupported game or if the check fails, and shows an on-screen notice either way.
- `usum-onescreen.log` at the emulated SD root with the startup check, scene changes and view changes, for bug reports.
- Developer test harness (command file, headless screenshots, memory tools, scenarios) and analysis tools; see `README.md` and `docs/OFFSETS.md`.

#### Known gaps

- Ultra Moon is not supported yet (its addresses have not been found; the startup check fails and the plugin does nothing).
- No native screen for the Ride Pager (**Y** in the field), Poké Pelago, Festival Plaza, Quick Link and Pokémon Refresh: use the bottom-screen panel (**L+R**). The Ride Pager and Pelago need a later save than the one used for testing, so they have not been looked at.
- Double battles: the target screen was never found, so it needs the panel (**L+R**). Trainer battles were not played.
- Not native yet: Pokémon summary from the PC, Held Item / Release / Marking in the boxes, Habitat and Appearance in the Pokédex, TM and evolution-stone follow-ups in the Bag and Party, QR Scanner, Battle Video lists (the test save has none).
- Moves learned outside battle (TMs, Rare Candy) use the game's own screen on the bottom screen.
- Game controller input has not been tested on a real pad yet.
- Only tested in Azahar 2126.2 on Windows; real 3DS hardware has not been tried.
