# USUM One Screen v0.1.0: release notes (draft)

**Draft. Nothing is tagged or published.** Reconciled against locally available `origin/main` at `9b2c04a` (2026-11-03). The text below describes behaviour only. The pre-tag checks remain required on the commit to release.

## Release notes text (for the GitHub release)

USUM One Screen is a 3GX plugin that makes Pokémon Ultra Sun playable on the top screen only in the Azahar 3DS emulator. The game's menus are drawn on the top screen with buttons to drive them; anything not covered yet can be brought up with **L+R** as a copy of the bottom screen on top of the top screen, with a touch cursor on the **C-stick** and **ZL**.

This is a **beta**.

**Needs:** Azahar with the 3GX plugin loader (tested on 2126.2, Windows), and your own copy of Pokémon Ultra Sun (USA) with the v1.2 update (title `00040000001B5000`, version `0x0800`). Ultra Moon is not supported yet. Install and controls: [docs/PLAYING.md](PLAYING.md).

**On the top screen:** battle (commands, moves with effectiveness, native Pokémon switching and Bag item selection, catching, the post-faint Use Next Pokémon question, move learning), the X menu, the Pokédex, Bag, Party and summary, PC boxes, Options, Trainer Passport and Battle Video, the nickname keyboard, the map (**ZR**) and Rotom's chat prompts. Party now supports Give / Take held items, replacement confirmation and TM teaching. The keyboard mirrors QWERTY and the typed name with the game's character limit; the Pokédex lists caught / seen counts and entries while leaving the game's model or map visible.

**Still needs L+R (bottom screen):** Ride Pager, Poké Pelago, Festival Plaza, Quick Link, Pokémon Refresh, Save, SOS / double battles' target selection (the native page is host-tested only and refuses unverified taps), follow-up uses in the field Bag, the PC's Summary / Held Item / Release / Marking, and the Pokédex's Habitat / Appearance / search / Evaluation. Party TM teaching is native; Ether / Elixir, evolution stones and Rare Candy follow-ups there are host-tested only and may need the panel. Battle Ether move selection, Pokémon Restore / Summary and Y quick ball are not handled natively.

### Optional Windows launcher

The app in [`player/`](../player/README.md) checks your own game and v1.2 update, copies your installed Azahar into a separate portable folder, enables the plugin loader and top-screen layout, disables custom textures, installs the plugin and update, and starts the game. It offers controller presets, mapping in Azahar, plugin settings and windowed / fullscreen play. It does not change your existing Azahar profile or provide game files.

The recorded Azahar 2126.2 setup and launch test reached the field with an existing test save. **A brand-new game stalls at the language screen in the tested v1.2 setup**, even with the plugin loader off. The launcher and plugin accept `0x0800` and refuse `0x0820`; genuine v1.2 update-version compatibility remains unchecked. Gamepad input on a real pad, mapping in Azahar, fullscreen, decrypted base-game CIA installation and other Azahar versions were not tested. See [PLAYER-APP.md](PLAYER-APP.md) for evidence and open issues.

**Known gaps:** see the "Known gaps" section of [docs/PLAYING.md](PLAYING.md) and [CHANGELOG.md](../CHANGELOG.md). Notably: Ultra Moon is unsupported; a game controller has not been tested on a real pad; real 3DS hardware has not been tried; the Ride Pager and Pelago were never seen because the test save predates them; trainer battles and double battles were not played.

**Bugs:** attach `usum-onescreen.log` (root of the emulated SD card) and your Azahar version to an issue. Never attach game files, saves or keys.

## Checklist: must be true before tagging

Tick these on the commit that will be tagged. Checked merge entries record repository history only; they do not certify the release commit's build or runtime behaviour.

### Code on `main`

- [x] Merged by `9b2c04a`: #42 (Party held items / TMs), #43 (emulator queue), #45 (shared screen buffer), #50 (native battle Party / Bag and targeting fallback), #51 (Pokédex), #53 (keyboard), #54 (Windows launcher), #55 (host-test linking and Party drawing bounds). Shared game assets are included in the merged work; check remaining Wave 7 PRs separately.
- [ ] Re-list remaining Wave 7 work and follow-ups before tagging, including X menu, Bag, map, boxes, subscreens and prompts. Do not infer completion from the merged PRs above.
- [ ] Memory verified on the release commit. #45 replaces per-screen allocations with a shared startup buffer; [MEMORY.md](MEMORY.md) records a 617-second memory-tour pass with the real save unchanged on the 5 MiB build. Repeat that scenario on the release artifact; it rejects LOW / NO MEMORY / bad_alloc / PANIC and emulator crashes. A separate 10 MiB boot was verified under Azahar, but the shipped setting remains 5 MiB and real hardware was not verified.
- [ ] Version consistent: `usum-onescreen.plgInfo` (`Major: 0`, `Minor: 1`, `Revision: 0`) and the `PluginMenu` line in `Sources/main.cpp` (currently `0, 1, 0`). The workflow only checks the first.
- [ ] `CHANGELOG.md`, `docs/PLAYING.md`, README and these notes match the final merged code. This draft includes native battle Party / Bag, Party Give / Take and TM teaching, launcher limitations and targeting fallback. Check remaining Bag changes when they merge.
- [ ] CI green on the commit to tag: Build plugin (build + host tests + scenario files parse). #55 records host tests and a plugin build run in WSL/MSYS2 after fixing move-learning test dependencies and Party drawing outside the battle page; that is not a CI pass on the release commit.

### Scenario pass (needs the Azahar test machine, an Ultra Sun v1.2 save and the start points)

- [ ] `py -3 -I tools/regress.py --instance NAME` (add `--main` to test the newest green CI build of `main` instead of a local build): every scenario in `scenarios/` passes, with `check-save` confirming the real save is untouched. Scenarios: `boot-to-field`, `open-each-app`, `actions`, `xmenu-touch-close`, `wild-battle-run`, `panel-default`, `native-xmenu`, `native-party`, `native-bag`, `native-boxes`, `native-map`, `native-move-learn`, `native-nickname`, `native-options`, `native-passport-video`, `native-pokedex`, `native-rotom-chat` (waits up to 12 minutes for Rotom), `native-wild-battle`, `theme-demo`. Add new Wave 7 scenarios to the pass.
- [ ] The 3GX tested is the exact build artifact from the commit to tag (not a local build).
- [ ] Include `memory-tour` and the updated `native-party`, `native-wild-battle` and `native-nickname` scenarios. Verify Party Give / Take / TM teaching, battle switching / Potion / catching / post-faint choice, and keyboard behaviour on the release artifact.
- [ ] Anything a scenario does not cover that the notes above claim: catching with the native Bag and nickname keyboard by hand, the Roto Loto, and any remaining Wave 7 change. Keep SOS / double targeting and untried item follow-ups explicitly unverified until exercised in-game.

### Tyler's controller test

- [ ] With a real controller (not Azahar's keyboard mapping) through the Azahar window: L+R, C-stick cursor, ZL touch, ZR map, X menu, Up for Rotom, and a battle with the native commands, move list and catching. Check the circle pad and D-pad do the same in the menus (scenarios drive the game with injected input, which does not prove the real pad path). Note the controller model and mapping.

### Release mechanics

- [ ] Tyler decides the licence and whether the repo goes public (below).
- [ ] The release steps in [RELEASING.md](RELEASING.md) were followed; the draft release is checked by hand and published by Tyler as a pre-release. The release workflow only creates a draft.
- [ ] A fresh-machine install following `docs/PLAYING.md` works with the downloaded `.3gx`.
- [ ] Resolve or explicitly scope the launcher's new-game language-screen failure and `0x0820` update-version question before offering it to players. Test the intended launcher download and document its limits; the existing-save launch test is not a fresh-game pass.

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
6. **Plugin metadata.** `usum-onescreen.plgInfo` and the plugin menu still say "Play Ultra Sun / Ultra Moon", while only Ultra Sun is supported. `MemorySize: 5MiB` remains unchanged; #45's shared buffer and documented memory tour address the earlier per-screen allocation failures. Recheck the budget on the release artifact.
7. **Licensing.** `RELEASING.md` ends with "see `docs/LICENSING.md`", still true: the open items above come first, and releasing from a private repo does not put the GPL source offer in place.
8. **Run steps in README.** "Run in Azahar" still says to check the folder layout against your Azahar setup; `docs/PLAYING.md` has the confirmed path (`luma\plugins\00040000001B5000\`).
