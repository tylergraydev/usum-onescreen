# Native Party screen and summary (P2-3)

With the panel off, the game's Party screen (X menu → Pokémon) shows on the top
screen as a native list. The game's own Party screen is still what runs: it sits
on the hidden bottom screen, and the native screen drives it with taps and
presses through the action layer (`docs/ACTIONS.md`). Nothing is written to game
memory.

- `Includes/Screens/PartyMenu.hpp`, `Sources/Screens/PartyMenu.cpp`: the pure
  part (no CTRPF): what a party member shows, cursor moves, the pages, the
  actions for each choice, drawing. Host tests in `tests/party_menu_test.cpp`
  (`make -C tests` also writes `analysis/private/party-preview.ppm` and
  `summary-preview.ppm`).
- `Includes/Screens/PartyScreen.hpp`, `Sources/Screens/PartyScreen.cpp`: the
  plugin glue: when to show, the panel, input, reading the party and the bag,
  the Use item steps, the OSD layer. Named `PartyScreen`, not `Party`: the
  Makefile names objects by file name only, and `Sources/Party.cpp` (RE3's
  reader) is already `Party.o`.
- Data: RE3's `ReadParty` / `ReadBag` (`docs/RE3-party-bag.md`) and RE2's
  names, plus new reads in `Sources/Party.cpp` (`docs/OFFSETS.md`, P2-3
  entries): the Party screen's cursor, the Bag's list cursor and scroll, and
  the summary's move / ability / nature names.

## What it shows

The party in the game's screen order (two columns, as on the bottom screen):
nickname, level, HP bar and numbers, status (`PSN` ...) and held item. The
screen order is the Party screen's own (it reorders the real party only when
it closes, RE3), so a swap shows at once.

The summary (opened from it) shows the Pokémon the game's summary shows:
name, species, level, HP, status, held item, ability, nature, and the four
moves with their PP, by name. The game's summary keeps the top screen for its
stats and memo pages and puts name, level, moves and ability on the bottom
screen, so the native page replaces that split.

## Keys

Party list:

| Key | Does |
|---|---|
| D-pad / circle pad | move (left/right in the row, up/down a row; no wrap); held: repeats after 15 frames, then every 5 |
| A | menu for that Pokémon: Summary, Use item, Move, Cancel |
| Y | move it (as Move) |
| B | left to the game: closes its Party screen (back to the X menu) |

Menu: Up/Down, A chooses, B back. Move: D-pad to the other Pokémon, A (or Y)
swaps, A on the same one or B cancels. Use item: the Medicine pocket (L/R or
left/right: Berries), Up/Down (wraps), A uses it, B back to the menu.

While the list waits for input, A, the D-pad, X, Y, L, R, Start and the circle
pad are captured; B too off the list. Touch is not captured.

Summary: the game keeps its keys (Up/Down change Pokémon, Left/Right its pages,
A its move check, B back to the list). **Y** hides the native page to show the
game's own top page (stats hexagon, memo) and shows it again; only Y is
captured.

## Carrying a choice out

Each choice is an `ActionDef` built for it (`MakeParty*Action`):

- **Summary**: tap the slot (its menu dims the bottom bar), wait 12 frames, tap
  Summary; done in `Status`.
- **Move**: tap the Y button ("Which Pokémon do you want to move?"), tap the
  first Pokémon (in move mode a tap only moves the game's cursor; skipped when
  the cursor is already there, StaticWork `+0x52`), A picks it, tap the second
  (swaps at once), wait 45 frames, B leaves move mode; done on the plain list.
  No retry on that B: one too many would close the Party screen. Checked
  afterwards against the screen order (`party-ui: swap ok, screen order ...`).
- **Use item**: in steps, because the Bag has to be read in between:
  1. `party-restore`: tap the slot, Restore (the Bag for that Pokémon), tap the
     pocket's tab (the Bag reopens on the pocket it showed last).
  2. The plugin reads the Bag's list (count, scroll, cursor row). The count
     must match the pocket's non-zero items, or it backs out. If the item is
     on a shown row it taps that row (`party-bag-row`: opens the item menu);
     otherwise one D-pad step (`party-bag-up` / `-down`) and read again. The
     Bag keeps cursor and scroll per pocket, so a fixed tap could hit the
     wrong item; stepping up never wraps (the item is above the view, so the
     cursor is not on the first).
  3. `party-use`: tap Use this item, wait 60 frames (the HP is written at once;
     the message stays), B through the message, then B until the Bag is gone
     (up to 4 more if one lands mid-message), done on the plain list.
  4. The result is read back: `party-ui: used Potion on Pichu: hp 2/27 ->
     22/27, status 0 -> 0, Potion x9 -> x8` (an earlier run) (or `no effect:` when the count
     didn't change: "It won't have any effect.").

The plain list is a pixel probe (`WorldPartyList`, `Sources/Actions.cpp`): the
bottom bar's Y and X buttons (blue at 120,226 and 310,226, white at 205,226).

## The panel

AutoPanel's default for Party is the full view and for Status the panel. The
native screen holds the panel off (AutoPanel's manual override) while the Party
screen, the summary opened from it, or the Restore bag of a running Use item is
up, and in the loading gaps between them. AutoPanel drops the override on every
scene change and sets that scene's mode in the same frame, so the full view can
show for one frame as the Party scene appears; the native screen turns it off
again in that frame (logged as `party-ui: native screen up -> panel: mode off`).
A panel turned on by hand (L+R) makes the native screen step aside until the
Party screen closes.

The native X menu's Pokémon entry calls `PartyScreenExpect()`, which holds the
panel off from the tap until the Party screen shows (at most 240 frames).

## Summary position

The game's summary shows its party as balls down the left of the bottom screen;
the current one has a green ring (x 5, centre y 16 + 32·position; screenshots
of positions 1, 2 and 3). The plugin reads that every frame while the summary is
up and follows it once three readings agree (the ring is not visible while the
game slides to the next Pokémon). It starts on the Pokémon it opened. Back on
the list, the native cursor goes to the Pokémon the summary showed last, as the
game's does.

## Commands and log

`partyui on|off|status` (default on). Log lines: `party-ui: shown (SCENE,
cursor N)`, `party-ui: party N: Name L12 27/27 PSN Item, ...` (whenever it
changes), `party-ui: cursor N (Name)`, `party-ui: page menu|swap|items ...`,
`party-ui: summary N (Name)` (opening), `party-ui: summary N of M: ... ability
..., nature ..., moves Name PP, ...` (whenever the shown Pokémon changes),
`party-ui: summary hidden (game page)` / `shown`, `party-ui: swap ...`,
`party-ui: use ...` / `used ...` / `no effect: ...` / `use FAILED: ...`,
`party-ui: hidden`; the actions' own `action: party-... ok` / `FAILED`.

## Drawing

One `UI::Layer` over `PartyArea` (376x224 at 12,8, the X menu's area), redrawn
only when what it shows changes (`PartyViewKey` / `SummaryViewKey`) and copied
every frame. The plugin thread hands the view to the OSD callback under a mutex
(the callback only `TryLock`s). Its buffer (337 KB at most) is allocated the
first time it shows. The OSD callback is registered before the panel's, so
screenshots include it.

## Tests

- `tests/party_menu_test.cpp`: cursor moves on 1-6 Pokémon; every page and
  key (B left to the game on the list); status names; the Bag plan; every
  action's taps are verified buttons; each action against a fake Party screen
  / Restore bag / summary (summary, swap with the cursor elsewhere and already
  there, Use on an item scrolled out of view: 8 steps then the row, stepping up
  without wrapping, backing out); drawing.
- `scenarios/native-party.txt` (~4 min): X → native X menu → Pokémon → the
  native list (all six with level, HP, status, item, panel off); moves; Summary
  of Zorua (names), Down to Pikipek, Y and back, B; swap Torracat (1) and
  Buneary (6), the order holds after closing; a wild battle with Buneary in
  front (Defense Curl ×3, Run) for damage; Use item → Potion on Buneary (HP up,
  Potion ×9 → ×8); B to the X menu, B to the field; `check-save` twice.
  The battle is random: if the foe never hits, the scenario fails at the HP
  check after the battle and says so (rerun).

## Not done

- The battle's Pokémon button (after PR #24 merges).
- Held Item (give / take / move) and the game's X button: not mapped. Restore's
  Give was not tapped (`measured` in the layouts).
- Items that ask a follow-up question (Ether: which move; evolution stones;
  Rare Candy's level-up and move prompts): the Use step backs out with B, which
  should cancel those, but none was tried. Only Potion (heals), Fresh Water and
  Potion at full HP ("no effect") were.
- TMs, Free Space, Key Items: not offered.
- Status values other than 5 (poison) were not seen.
- Eggs are drawn as "Egg" (no egg in this save).
- Fewer than six Pokémon: host-tested only.
