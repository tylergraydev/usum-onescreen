# Changelog

All notable changes for players are listed here. Versions follow [Semantic Versioning](https://semver.org/); the version in `usum-onescreen.plgInfo` should match the release tag.

## [Unreleased]

### Added

- Windows launcher: permanent named save slots per game (save/load/rename/delete), a Saves dialog with separate slots and rotating backups, and ZIP import/export of current saves, slots and backups. Slot loads make a safety backup first; imports verify the declared game title and reject unsafe ZIP paths. New `slot`, `export` and `import` CLI commands. Save data only; extdata is excluded.

- Controller prompts: Xbox, PlayStation and Nintendo vector badges with positional button mapping, live INI reload and `glyphs` test commands. The default `3ds` look is preserved.
- Native battle move descriptions: L toggles the selected move's own game text in all three layouts; L/B closes it.
- Battle party shiny stars and HA pills, plus active player/first-foe command badges when their checked Pokemon data is available (emulator verification pending).

- Windows launcher: automatic save backups before Play (ten per game), Backups dialog with restore and folder access, and backup/list/restore CLI commands. Portable saves only; extdata is excluded.
- Game options for graphics resolution (Keep, 1x, 3x, 5x), battle layout, text/battle speed and automatic or manual controller icon families. Native plugin prompts now draw the selected family.
- Cheats dialog and CLI with six embedded own-code cheats, separate Sun/Moon selections, preserved boot safety guards and exclusive shiny/undo choices. Back up first; changes apply next start.
- The native PC boxes show each Pokémon's own box icon (the game's, read from your ROM) next to its name, in the box, the party row and under the cursor while moving one.

### Fixed

- Battle teams selected from part of the field party no longer borrow nicknames/items from the wrong slot; metadata matches Pokemon identity and suppresses ambiguous matches.

- The native map no longer crashes the game with widescreen (16:9 or 21:9) on. The map picture now squeezes like the rest of the game's 2D.
- At a higher Azahar internal resolution, the game stays sharp while the plugin's menus, battle menu and Rotom prompt are on screen (they used to drop the whole picture to 400x240). The menus themselves are still 400x240 pictures, now with square corners. `layergpu off` brings back the old drawing.

## [0.1.1] - 2026-11-05

### Changed

- Windows launcher ships as one exe with the plugin embedded and verified Azahar 2126.2 download on first setup. Drag your own decrypted ROM and v1.2 update onto the window, or a zip containing them. The existing bundled Windows zip remains available for offline setup.

## [0.1.0] - 2026-11-04

First beta. Supports Pokémon Ultra Sun and Ultra Moon (USA) v1.2 (`00040000001B5000` / `00040000001B5100`, title version `0x0820`; Sun also accepts rebuilt `0x0800` with a warning) in Azahar with the 3GX plugin loader. See [docs/PLAYING.md](docs/PLAYING.md).

### Added

- The native X menu follows the game's own X-menu layout (entries the player rearranged, and Photo Album and Poké Pelago once unlocked). It used to assume the default layout and could open the wrong entry. Taps are made at the slot the entry is in now.
- The native Pokédex shows three-digit caught / seen counts in full.

- Genuine Ultra Sun v1.2 updates (title version `0x0820`) are accepted. Before, only `0x0800`, a decrypted copy with a rewritten TMD, was. The code is identical.
- Ultra Moon (USA) v1.2 (`00040000001B5100`, `0x0820`) is accepted, with the same addresses as Ultra Sun (checked statically, then in Azahar: boot, panel, native X menu, Party, Bag). The native X menu also recognises Ultra Moon's light-blue menu background.

- **Windows launcher** (`player/`): one Windows release zip includes Azahar 2126.2, its licence/source notice, the launcher and plugin. Unzip, run `UsumOneScreen.exe`, pick your own decrypted Ultra Sun or Ultra Moon (USA) ROM and matching v1.2 update once, then play. Detects and remembers the game, installs the plugin under its title ID and refuses mismatched updates or Moon `0x0800`. Without a bundle it copies an installed Azahar as before. Controller presets and plugin settings are in its window. Release CI verifies the pinned Azahar SHA-256 and attaches the plugin and zip to a draft release; publishing stays manual. No game files or keys provided. See [docs/PLAYER-APP.md](docs/PLAYER-APP.md).

Native top-screen menus (the panel is off while they show; the game's own screen keeps running on the hidden bottom screen and the plugin drives it). Details and controls per screen are in [docs/PLAYING.md](docs/PLAYING.md).

- **Battle** (single battles): command bar (Fight / Bag / Pokémon / Run) and move list with type, PP and how effective each move is. **Up/Down** and **A**, **B** goes back. Pokémon opens the native party list (**D-pad**, **A** for Switch / Cancel); after a faint, *Use next Pokémon?* opens it without a back option. Bag opens native pockets (**Left/Right** or **L/R**, **Up/Down**, **A** use); a Potion asks for a Pokémon, starting on one it would help. HP-only items refuse full-HP or fainted targets. Catching: Bag → Poké Balls → a ball → **A**, then the Pokédex page and nickname question.
- **Move learning**: when a Pokémon wants a fifth move, the four moves and the new one are listed (type, PP; category, power and accuracy of the highlighted one). **Up/Down** and **A** pick the move to forget or *Don't learn*, **A** confirms, **B** goes back.
- **X menu**: **X** in the field shows Pokémon, Pokédex, Bag, Quick Link, Festival Plaza, Trainer Passport, Pokémon Refresh, Battle Video, Options, the map and Save as a grid (D-pad and L/R move, **A** opens, **B** closes). It starts on the entry the game's own X menu remembers. QR Scanner is shown but not available yet.
- **Keyboard**: the nickname keyboard after a catch (D-pad moves, **A** types, **B** deletes, **Y** Shift, **X** space, **START** jumps to OK).
- **Pokédex** (from the X menu or Rotom's face): the top level with each Pokédex's caught / seen counts and locked islands, the Alola list, an island Pokédex's entries around the cursor, and an entry page's number, name and caught / seen. The game's own keys drive it (D-pad, **A**, **B**; Up/Down on an entry go to the next seen one). Habitat, Appearance, search and Evaluation stay on the bottom screen.
- **Options, Trainer Passport, Battle Video** (from the X menu). Options: Up/Down pick a row, Left/Right change its value, **A** confirms, **B** leaves (asks first if something changed). Passport: the trainer card values and stamp pages (Left/Right or L/R turn the page). Battle Video: **A** opens the Vs. Recorder.
- **Bag**: the pockets and their items (counts, NEW marks), the party with HP, the item menu. **Use** (Medicine and Berries) and **Give** are native; other Use entries are greyed out and say to use the bottom screen. D-pad up/down move, **L/R** or left/right change pocket, **A** opens an item, **B** goes back or closes.
- **Map**: **ZR** in the overworld (or the X menu's **Map**) shows the place name, the island, inside/outside, the player's position and facing, and the game's Rotom Dex map around the player, live while walking. **ZR** or **B** closes it (`map_key` in the settings file). Place names are read from the game's own files at runtime; nothing of the game is shipped.
- **Rotom's prompts**: when Rotom wants to talk, a corner badge says so and **Up** starts the chat. Rotom's question and its two answers show at the bottom of the top screen (**Up/Down** choose, **A** answers, then Rotom's reply); the Roto Loto's steps show there too (**A** goes on and stops the reel).
- **Party and summary** (X menu → Pokémon): name, gender, level, HP, status, held item. **A** opens Summary / Use item / Give item / Take item / Move / Cancel, **Y** moves a Pokémon, **B** closes. The summary adds species, ability, nature and moves with type, category and PP (Up/Down change Pokémon, **Y** toggles the game's own page). Use item offers Medicine, Items, TMs and Berries; Give excludes TMs, asks before replacing a held item, and Take returns it to the Bag. TM teaching shows the four moves and Don't learn (**Up/Down**, **A**; **B** does nothing on that list).
- **PC boxes**: at a Pokémon Center PC, the box (name and each slot's Pokémon) and the party. The D-pad moves the game's cursor, **A** picks up / puts down (swapping with one already there; box ↔ party and across boxes), **L/R** turn the box, **Y** shows the game's own details, **B** puts a held Pokémon back or leaves.
- **Bottom-screen fallback**: **L+R** cycles the bottom screen on the top screen off / corner panel (1/2 or 2/3 size, `size` in the settings file) / full view (`panel_key`). The copy runs on the emulated GPU, so the half panel and the full view cost Azahar a few percent of speed at most; 2/3 costs ~8% (docs/PERF.md). Screens without a native view, or parts of one (an item that asks a follow-up question, say), use it.
- **Touch cursor**: **C-stick** moves it, **ZL** touches the bottom screen at the cursor.
- **Automatic view** (optional, `auto = on`; off by default since native menus come first): picks off / panel / full per scene, and **L+R** then holds your choice until the next scene change. Scene views can be set in the `[scenes]` section.
- **Settings file** `usum-onescreen.ini` at the emulated SD root: keys, panel size and corner, auto view, per-scene views, written with comments on first run. Bad values are skipped and logged. The plugin menu (**Select**) can reload it.
- Startup check of the game, its version and every patched function; the plugin does nothing on an unsupported game or if the check fails, and shows an on-screen notice either way.
- `usum-onescreen.log` at the emulated SD root with the startup check, scene changes and view changes, for bug reports.
- Developer test harness (command file, headless screenshots, memory tools, scenarios) and analysis tools; see `README.md` and `docs/OFFSETS.md`.
- Native screens share a buffer allocated at startup instead of allocating a separate drawing layer for each screen. The documented memory tour passed on Azahar with the 5 MiB build and left the real save unchanged; see [docs/MEMORY.md](docs/MEMORY.md). Emulator test instances queue across checkouts and worktrees (four at once by default); the player's own Azahar is not counted.

### Fixed

- Host-test linking after Party's theme changes, and Party drawing outside the battle page's bounds (#55). That change records host tests and a plugin build run in WSL/MSYS2; release-commit CI and scenario checks are still required.

### Known gaps

- Ultra Moon has only been checked briefly (boot, panel, X menu, Party, Bag). A brand-new Ultra Moon game stalls in the opening bedroom scene in Azahar; Ultra Sun passes the same scene.
- No native screen for the Ride Pager (**Y** in the field), Poké Pelago, Festival Plaza, Quick Link and Pokémon Refresh: use the bottom-screen panel (**L+R**). The Ride Pager and Pelago need a later save than the one used for testing, so they have not been looked at.
- SOS / double targeting has a host-tested native page, but its taps are unverified and refused; use the panel (**L+R**). No SOS call was reached during the documented search. Trainer battles were not played.
- Not native yet: Pokémon summary from the PC, Held Item / Release / Marking in the boxes, Habitat / Appearance / search / Evaluation in the Pokédex, follow-up uses in the field Bag, QR Scanner, Battle Video lists (the test save has none). Party TM teaching is native; Party Ether / Elixir, evolution stones and Rare Candy follow-ups are host-tested only, with unverified taps falling back to the panel.
- Battle Ether move selection, Pokémon Restore / Summary and Y quick ball are not handled natively; Status Restore, Rotom Powers and Battle Items were not used in the documented testing. Z-moves and Mega Evolution were unavailable. Illusion names the real Pokémon rather than its disguise. Run from the post-faint question was not observed.
- With the rebuilt `0x0800` v1.2 update, a brand-new game stalls at its language screen (also with the plugin loader off); existing saves play. With a genuine `0x0820` update a new game works, also through the launcher, which now accepts `0x0820` and warns about `0x0800` (docs/PLAYER-APP.md). Launcher gamepad input, mapping in Azahar, fullscreen and decrypted base-game CIA installation remain untested; see docs/PLAYER-APP.md.
- Game controller input has not been tested on a real pad yet.
- Only tested in Azahar 2126.2 on Windows; real 3DS hardware has not been tried.
