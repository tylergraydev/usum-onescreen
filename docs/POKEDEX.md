# Native Pokédex (W5-2)

With the panel off, the Pokédex shows on the top screen. The game's own Pokédex app (`Zukan`) still runs on the hidden bottom screen and takes the keys exactly as without the plugin; the native screen reads its frame, cursor and records and the save's dex flags (`docs/RE7-pokedex.md`) and draws what it lists next to what the game already shows on top.

- `Includes/Screens/PokedexList.hpp`, `Sources/Screens/PokedexList.cpp`: the pure part (no CTRPF): seen/caught from the flags and the form table, rows per dex, the window around the cursor, which frame is which screen, drawing. Host tests in `tests/pokedex_test.cpp` (`make -C tests` also writes previews to `analysis/private/pokedex-preview-*.ppm`).
- `Includes/Screens/Pokedex.hpp`, `Sources/Screens/Pokedex.cpp`: the plugin glue: reading the game, the panel, the OSD layer, the `pokedex` command and log.

## What it shows

| Game screen | Native screen | Where (top screen) | Left uncovered |
|---|---|---|---|
| Top level | Alola and the four island Pokédexes with caught / seen counts (locked ones as empty brown rows, as the game), Pokédex Evaluation if the game has it; the game's cursor | 12,8 304×224 | the completion % (bottom right); the rest is a title card |
| Alola list | 8 rows around the cursor: number, name (seen ones; `?????` otherwise, as the game), caught / seen mark; dex counts; scroll bar | 196,8 196×224 | the selected Pokémon's model and name plate (left) |
| Island grid | the cursor's entry with the one before and after, and the dex's counts | 236,4 158×88 | the island map, habitat and model (the game shows the number and name itself) |
| Entry page | dex, number, name, seen / caught | 8,8 150×124 | the 3D model (middle) |

Anything else in the Pokédex (Habitat, Appearance, search, Evaluation once chosen, registering a catch) has no native screen: the native one goes away and the panel is free (`auto = on` shows it; L+R by hand).

## Keys

The game's own (`docs/RE7-pokedex.md`): D-pad moves its cursor (top level, list, grid), **A** opens, **B** goes back, **Up/Down** on an entry page go to the previous / next seen entry, **A** there opens Habitat. Nothing is captured: the native screen follows the game.

To switch between dexes, **B** back to the top level, pick another row, **A**. On this save only Alola and Melemele are open.

## The panel

From the moment the Pokédex opens (up to 90 frames until its first screen shows: the app fills its records first), while the native screen shows, and for 20 frames after it goes (a list opening or closing passes through other frames), the panel is held off with AutoPanel's manual override, like the native X menu. When it is released, AutoPanel picks the mode for what is up (panel for the Pokédex by default with `auto = on`). If the player turns the panel on by hand (L+R) while it is held off, the native screen steps aside until the Pokédex closes (`pokedex: panel turned on by hand ...`).

## From the X menu

The native X menu's Pokédex entry taps the game's (`xmenu-pokedex`), and the native Pokédex takes over as soon as the game's top level is up; **B** from the top level returns to the X menu, where the native X menu shows again. Rotom's face in the field opens the same app.

## Commands and log

`pokedex on|off` (default on), `pokedex status`, `pokedex rows` (every dex's seen entries). Log lines:

- `pokedex: data 403 records, alola 22 caught 38 seen (126 rows), melemele open 22/38, akala locked 7/11, ...` when the data is (re)read: once per visit and whenever the screen changes;
- `pokedex: shown top|list|grid|entry`, then one line per change: `pokedex: top cursor melemele`, `pokedex: list alola 6/126 #007 Popplio seen`, `pokedex: grid melemele 3/150 #004 Litten caught`, `pokedex: entry alola 4/126 #005 Torracat caught` (index / rows, the dex number, name, state);
- `pokedex: hidden (why)` while nothing native shows in the Pokédex, `pokedex: closed` on leaving it.

## Drawing

One `UI::Layer` over the current screen's area, redrawn only when what it shows changes (and once more when the game's font has loaded) and copied every frame (`docs/UI.md`). The buffer (272 KB) is allocated the first time it shows.

The look is the game's own Pokédex (W7-5, `UI::Theme`, measured on the game's bottom screens; `docs/VISUAL-STYLE.md`): a light blue background with lighter squares; rows are bars leaning right with a 2 px dark line, a darker band, a shadow and a dark notch at the left. Top level: the selected dex row red (`DexSelected`) and bigger, the others orange (`DexRow`), locked ones brown (`DexEmpty`), Evaluation yellow and indented; caught and seen counts in dark pills after a ball and a grey dot. The top level's pills are sized for 999, so three-digit counts fit, as in the game. A row label that wouldn't fit beside them drops to the smaller size. Before, a fixed 50 px pill cut the Ultra Moon postgame save's 182 seen to "1…"; that save now reads Alola 14 / 182 with every label at full size in the game font. List and grid: the game's list colours (seen light red `#FF5544`, unseen dark red `#963228` with a pink `?????`), the ball for caught, the dot for seen. Entry page: a white name bar like the game's entry header. The red arrow cursor marks the selection; the game font and button icons come from W7-0's assets (`UI::RequestAssets` when it shows); prompts are theme chips (B Back, A Open, Up/Down). The view is handed to the OSD callback under a mutex the callback only tries to take. The callback is registered before the panel's, so screenshots include it.

## Tests

- `tests/pokedex_test.cpp`: the top level with 403 / 403 in every row (the counts' text must fit its pill; preview `analysis/private/pokedex-preview-top-403.ppm`); form bits and counts against a made-up form table; seen in each region and through another form only; rows per dex (order, island members, the Alola cut); counts; the window around the cursor (top, middle, end, clamping); frame classes; each screen draws only inside its area and areas stay off the game's name plate.
- `scenarios/native-pokedex.txt`: X → native X menu → Pokédex; top level (cursor moves, locked rows skipped, both ends wrap); Melemele grid (moves, an entry, next seen, back); Alola list (an entry, next seen, back, one Down, held key repeat); B out to the X menu and the field. Then from Rotom's face: the panel turned on by hand, and `pokedex off`. `check-save` twice.
