# Changelog

All notable changes for players are listed here. Versions follow [Semantic Versioning](https://semver.org/); the version in `usum-onescreen.plgInfo` should match the release tag.

## [Unreleased]

### v0.1.0 (planned first beta)

Supports Pokémon Ultra Sun (USA) v1.2 (`00040000001B5000`, title version `0x0800`) in Azahar with the 3GX plugin loader. See [docs/PLAYING.md](docs/PLAYING.md).

#### Added

- Bottom screen drawn on the top screen, as a corner panel (1/2 or 2/3 size, `size` in the settings file) or a full view. The copy runs on the emulated GPU, so the half panel and the full view cost Azahar a few percent of speed at most (was ~40%); 2/3 costs ~8% (docs/PERF.md).
- Automatic view per scene: off on the title screen and overworld; corner panel for the X menu (also Quick Link, the save dialog and Festival Plaza), battle, Pokédex, Bag, Pokémon summary, Trainer Passport, Battle Video and unmapped scenes; full view for Party, PC boxes, Town Map, Options and Pokémon Refresh.
- The X menu is detected from the game's field state, so closing it by touch turns the panel off too.
- **ZR** cycles off / panel / full by hand until the next scene change.
- Touch cursor: **C-stick** moves it, **ZL** touches the bottom screen at the cursor.
- Startup check of the game, its version and every patched function; the plugin does nothing on an unsupported game or if the check fails, and shows an on-screen notice either way.
- `usum-onescreen.log` at the emulated SD root with the startup check, scene changes and view changes, for bug reports.
- Developer test harness (command file, headless screenshots, memory tools) and analysis tools; see `README.md` and `docs/OFFSETS.md`.

#### Known gaps

- Ultra Moon is not supported yet.
