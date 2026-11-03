# Changelog

All notable changes for players are listed here. Versions follow [Semantic Versioning](https://semver.org/); the version in `usum-onescreen.plgInfo` should match the release tag.

## [Unreleased]

### v0.1.0 (planned first beta)

Supports Pokémon Ultra Sun (USA) v1.2 (`00040000001B5000`, title version `0x0800`) in Azahar with the 3GX plugin loader. See [docs/PLAYING.md](docs/PLAYING.md).

#### Added

- Bottom screen drawn on the top screen, as a corner panel (1/2 or 2/3 size, `size` in the settings file) or a full view. The copy runs on the emulated GPU, so the half panel and the full view cost Azahar a few percent of speed at most (was ~40%); 2/3 costs ~8% (docs/PERF.md).
- Optional automatic view per scene (`auto = on`; off by default, since native top-screen menus come first): off on the title screen and overworld; corner panel for the X menu (also Quick Link, the save dialog and Festival Plaza), battle, Pokédex, Bag, Pokémon summary, Trainer Passport, Battle Video and unmapped scenes; full view for Party, PC boxes, Town Map, Options and Pokémon Refresh.
- The X menu is detected from the game's field state, so closing it by touch turns the panel off too.
- Native X menu on the top screen: **X** in the field shows Pokémon, Pokédex, Bag, Quick Link, Festival Plaza, Trainer Passport, Pokémon Refresh, Battle Video, Options, the map and Save as a grid (D-pad and L/R move, **A** opens, **B** closes) with the panel off. It starts on the entry the game's own X menu remembers. QR Scanner is shown but not available yet.
- Native keyboard on the top screen: when the game asks for a nickname after a catch, its keyboard shows on the top screen with the name typed so far (D-pad moves, **A** types, **B** deletes, **Y** Shift, **X** space, **START** jumps to OK) with the panel off.

- Native Pokédex on the top screen (from the X menu or Rotom's face, with the panel off): the top level with each Pokédex's caught / seen counts and locked islands, the Alola list, an island Pokédex's entries around its cursor, and an entry page's number, name and caught / seen, each next to what the game shows on top (the Pokémon's model, the island map). The game's own keys drive it (D-pad, **A**, **B**; Up/Down on an entry go to the next seen one).
- **L+R** cycles the bottom-screen view off / panel / full by hand (`panel_key`); with `auto = on` the choice holds until the next scene change.
- Touch cursor: **C-stick** moves it, **ZL** touches the bottom screen at the cursor.
- Startup check of the game, its version and every patched function; the plugin does nothing on an unsupported game or if the check fails, and shows an on-screen notice either way.
- `usum-onescreen.log` at the emulated SD root with the startup check, scene changes and view changes, for bug reports.
- Developer test harness (command file, headless screenshots, memory tools) and analysis tools; see `README.md` and `docs/OFFSETS.md`.

#### Known gaps

- Ultra Moon is not supported yet.
