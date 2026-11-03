# USUM One Screen v0.1.0: release notes (draft)

**Draft. Nothing is tagged or published.** Written against `main` at `cbf25e7` (2026-11-02). Wave 7 PRs change some screens' look after this; the text below describes behaviour only. Re-read it after they merge (see the checklist).

## Release notes text (for the GitHub release)

USUM One Screen is a 3GX plugin that makes Pokémon Ultra Sun playable on the top screen only in the Azahar 3DS emulator. The game's menus are drawn on the top screen with buttons to drive them; anything not covered yet can be brought up with **L+R** as a copy of the bottom screen on top of the top screen, with a touch cursor on the **C-stick** and **ZL**.

This is a **beta**.

**Needs:** Azahar with the 3GX plugin loader (tested on 2126.2, Windows), and your own copy of Pokémon Ultra Sun (USA) with the v1.2 update (title `00040000001B5000`, version `0x0800`). Ultra Moon is not supported yet. Install and controls: [docs/PLAYING.md](PLAYING.md).

**On the top screen:** battle (commands, moves with effectiveness, catching, move learning), the X menu, the Pokédex, Bag, Party and summary, PC boxes, Options, Trainer Passport and Battle Video, the nickname keyboard, the map (**ZR**) and Rotom's chat prompts.

**Still needs L+R (bottom screen):** Ride Pager, Poké Pelago, Festival Plaza, Quick Link, Pokémon Refresh, Save, double battles' target screen, items with a follow-up question (TMs, Ether, evolution stones, Rare Candy), the PC's Summary / Held Item / Release, and the Pokédex's Habitat and Appearance.

**Known gaps:** see the "Known gaps" section of [docs/PLAYING.md](PLAYING.md) and [CHANGELOG.md](../CHANGELOG.md). Notably: Ultra Moon is unsupported; a game controller has not been tested on a real pad; real 3DS hardware has not been tried; the Ride Pager and Pelago were never seen because the test save predates them; trainer battles and double battles were not played.

**Bugs:** attach `usum-onescreen.log` (root of the emulated SD card) and your Azahar version to an issue. Never attach game files, saves or keys.

## Checklist: must be true before tagging

Tick these on the commit that will be tagged. Nobody has run them yet.

### Code on `main`

- [ ] Wave 7 PRs merged into `main`, and their follow-ups, as of this draft: [#39](https://github.com/tylergraydev/usum-onescreen/pull/39) (shared game-asset service), [#40](https://github.com/tylergraydev/usum-onescreen/pull/40) (X menu in the game's look) and [#42](https://github.com/tylergraydev/usum-onescreen/pull/42) (Party give/take, follow-up items, restyle) are open; the battle (W7-1), Bag (W7-3), shared memory buffer (W7-m) and the Pokédex / map / boxes / keyboard / subscreens / prompts restyles (W7-5 to W7-10) had no PR yet. Re-list open PRs before tagging. (#43 is test infrastructure and not a release blocker.)
- [ ] Memory is settled. Wave 7 sessions report the plugin's 5 MiB heap running out (an allocation failure for a screen's layer, a game PANIC with the X menu, Party and Rotom's prompt all drawn) before the shared-buffer work lands. The release must not ship with that open.
- [ ] Version consistent: `usum-onescreen.plgInfo` (`Major: 0`, `Minor: 1`, `Revision: 0`) and the `PluginMenu` line in `Sources/main.cpp` (currently `0, 1, 0`). The workflow only checks the first.
- [ ] `CHANGELOG.md`, `docs/PLAYING.md` and the README match what merged. Wave 7 adds battle buttons that open the native Party and Bag, Party give/take and TM teaching, and item descriptions and pocket icons in the Bag: move them from "Known gaps" to the changelog's Added list and update the controls.
- [ ] CI green on the commit to tag: Build plugin (build + host tests + scenario files parse).

### Scenario pass (needs the Azahar test machine, an Ultra Sun v1.2 save and the start points)

- [ ] `py -3 -I tools/regress.py --instance NAME` (add `--main` to test the newest green CI build of `main` instead of a local build): every scenario in `scenarios/` passes, with `check-save` confirming the real save is untouched. Scenarios: `boot-to-field`, `open-each-app`, `actions`, `xmenu-touch-close`, `wild-battle-run`, `panel-default`, `native-xmenu`, `native-party`, `native-bag`, `native-boxes`, `native-map`, `native-move-learn`, `native-nickname`, `native-options`, `native-passport-video`, `native-pokedex`, `native-rotom-chat` (waits up to 12 minutes for Rotom), `native-wild-battle`, `theme-demo`. Add new Wave 7 scenarios to the pass.
- [ ] The 3GX tested is the exact build artifact from the commit to tag (not a local build).
- [ ] Anything a scenario does not cover that the notes above claim: catching with the native Bag and nickname keyboard by hand, the Roto Loto, and any Wave 7 screen change.

### Tyler's controller test

- [ ] With a real controller (not Azahar's keyboard mapping) through the Azahar window: L+R, C-stick cursor, ZL touch, ZR map, X menu, Up for Rotom, and a battle with the native commands, move list and catching. Check the circle pad and D-pad do the same in the menus (scenarios drive the game with injected input, which does not prove the real pad path). Note the controller model and mapping.

### Release mechanics

- [ ] Tyler decides the licence and whether the repo goes public (below).
- [ ] The release steps in [RELEASING.md](RELEASING.md) were followed; the draft release is checked by hand and published by Tyler as a pre-release. The release workflow only creates a draft.
- [ ] A fresh-machine install following `docs/PLAYING.md` works with the downloaded `.3gx`.

## Licensing: what is still open

Not decided here. [docs/LICENSING.md](LICENSING.md) (checked 2026-11-02, not legal advice) says:

- The repo has no `LICENSE` file; choosing one and making the repo public is Tyler's call.
- `3gx.ld` is identical to Gen6CTRPFrameworkOverhauled's (GPL-3.0); the `Makefile` and `usum-onescreen.plgInfo` are adapted from it. Cautious reading: those parts make this repo a derivative work, so distributing the `.3gx` would need the whole under GPL-3.0 with source offered. The linker script may originate from CTRPluginFramework's sample plugin; the Makefile's lower half is the standard devkitPro pattern. Neither was settled.
- CTRPluginFramework (statically linked) has a custom licence: a binary must not be restricted to paying users unless its source is provided. A free download with public source meets it. Its compatibility with the GPL was not found settled anywhere.
- libctru (zlib): nothing strictly required; a credit line is good practice.
- LICENSING.md's recommendation is GPL-3.0 before going public. The alternative is to replace `3gx.ld` and the `Makefile` first.
- Before public: add `LICENSE` and a README line; add `THIRD-PARTY.md` crediting the three; add "modified from" notes to `3gx.ld` and `Makefile` (neither has one now); check the repo and history for game data, keys or dumps; keep releases free with source at the tagged commit public.

## Audit of `docs/RELEASING.md` and the release workflow (2026-11-02)

The build steps still match: `release.yml` runs the same dependency install, cache and `make` as `build.yml` in the `devkitpro/devkitarm:latest` container, uploads `usum-onescreen.3gx`, and creates or updates a draft release (`--draft --verify-tag`). The tag check against `usum-onescreen.plgInfo` is as `RELEASING.md` describes. Stale or loose items:

1. **Unreleased section.** `RELEASING.md` step 2 says to rename the "Unreleased" section. `CHANGELOG.md` has a nested `### v0.1.0 (planned first beta)` under `## [Unreleased]`; at release the nested heading becomes `## [0.1.0] - DATE` and `[Unreleased]` is left empty. (`RELEASING.md` is reworded to say so.)
2. **Version in `main.cpp` is unchecked.** The workflow compares the tag with the plgInfo only; the `PluginMenu` line in `Sources/main.cpp` (`0, 1, 0`) is not read. A mismatch would ship silently.
3. **No tests in the release workflow.** `release.yml` builds only. It relies on `build.yml` having passed on that commit (step 2 says to wait). Nothing stops tagging a commit whose host tests failed, or a commit not on `main`.
4. **Release body is generic.** The workflow writes a fixed one-line note; `RELEASING.md` step 4 asks you to paste the changelog by hand. The "Release notes text" above could be used instead (`gh release create --notes-file`), but that is a workflow change and was not made.
5. **Unpinned toolchain.** Both workflows use `devkitpro/devkitarm:latest` and the ThePixellizerOSS repo's current `libctrpf` / `3gxtool`, so a release build can differ from the build tested. The Actions cache keeps the last good package files but is not a pin.
6. **Plugin metadata.** `usum-onescreen.plgInfo` and the plugin menu still say "Play Ultra Sun / Ultra Moon", while only Ultra Sun is supported; `MemorySize: 5MiB` is the limit the Wave 7 memory work is bumping against. Not changed here.
7. **Licensing.** `RELEASING.md` ends with "see `docs/LICENSING.md`", still true: the open items above come first, and releasing from a private repo does not put the GPL source offer in place.
8. **Run steps in README.** "Run in Azahar" still says to check the folder layout against your Azahar setup; `docs/PLAYING.md` has the confirmed path (`luma\plugins\00040000001B5000\`).
