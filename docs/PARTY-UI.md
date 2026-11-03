# Native Party screen and summary (P2-3, W7-2)

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
  names, plus new reads in `Sources/Party.cpp` (`docs/OFFSETS.md`, P2-3 and
  W7-2 entries): the Party screen's cursor, the Bag's list cursor and scroll,
  the summary's move / ability / nature names, the PK7 gender, and the
  teaching screen (`ReadTeachScreen`). In the Bag it also reads the native
  Bag's `ReadBagGame` (`Screens/Bag.hpp`, read only): the open pocket, its
  items, the menu's entries and whether a message or question is up.
- The look: `UI::Theme` (`docs/VISUAL-STYLE.md`) with the game's font, icons
  and move table from the shared asset service (`UI/Assets.hpp`, W7-0; the
  screen asks for them when it opens). Without them it draws the same layout
  with the system font and no type plates.
- For other screens (the battle's Pokémon command, W7-1):
  `PartyScreenMons(sceneManager)` returns the party as the list shows it, and
  `DrawParty` with a `PartyView` on the List page draws the plates.

## What it shows

The party in the game's screen order (two columns, as on the bottom screen):
nickname, gender, level, HP bar and numbers, status (`PSN` ...) and held item. The
screen order is the Party screen's own (it reorders the real party only when
it closes, RE3), so a swap shows at once.

The summary (opened from it) shows the Pokémon the game's summary shows:
name, gender, species, level, HP, status, held item, ability, nature, and
the four moves by name with their type, category and PP. The game's summary keeps the top screen for its
stats and memo pages and puts name, level, moves and ability on the bottom
screen, so the native page replaces that split.

## Keys

Party list:

| Key | Does |
|---|---|
| D-pad / circle pad | move (left/right in the row, up/down a row; no wrap); held: repeats after 15 frames, then every 5 |
| A | menu for that Pokémon: Summary, Use item, Give item, Take item (only while it holds one), Move (only with two or more), Cancel |
| Y | move it (as Move) |
| B | left to the game: closes its Party screen (back to the X menu) |

Menu: Up/Down (wraps), A chooses, B back. Move: D-pad to the other Pokémon, A
(or Y) swaps, A on the same one or B cancels. Use item / Give item: a pocket's
items (L/R or left/right change pocket: Medicine, Items, TMs, Berries for Use;
TMs are left out for Give, they can't be held; the page opens on the pocket
used last), Up/Down (wraps), A uses / gives it, B back to the menu.

When the game asks something on the way, the screen asks it natively (the Ask
page): Up/Down, A answers.

- *"Torracat is already holding Oran Berry. Switch it for the Escape
  Rope?"* (Give to a Pokémon that holds an item): Yes / No; B is No.
- *"Torracat wants to learn Work Up. Forget which move?"* (a TM, when the
  Pokémon knows four moves): its four moves with type plate and PP, and
  *Don't learn Work Up*; B does nothing (as the game's own list).
- A menu the plugin doesn't know (an Ether's "which move", not seen): the
  Pokémon's moves and Cancel when the entry count fits, else *Option 1..n*;
  B is Cancel when the last entry is Cancel.

While the list waits for input, A, the D-pad, X, Y, L, R, Start and the circle
pad are captured; B too off the list; the same while a question waits. Touch
is not captured.

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
- **Take item** (`party-take`): tap the slot, Held Item, Take; "You received
  the Quick Claw from Torracat." stays until B (the bar under it doesn't read
  as the plain list); one B, pressed again only if the list hasn't come back
  within 60 frames (one B too many would close the Party screen). The game
  lists Take with nothing held too; the native menu only offers it while the
  Pokémon holds something.
- **Use item / Give item**: in steps, because the Bag has to be read in
  between:
  1. Into the Bag for that Pokémon: Use from Medicine or Berries through
     Restore (`party-restore`, P2-3's path); Give, and Use from Items or TMs,
     through Held Item → Use/Give (`party-held-bag`), whose Bag has every
     pocket. Then the pocket's tab (the Bag reopens on the pocket it showed
     last). If the Bag still shows another pocket, the tab is tapped again
     (`party-bag-tab`, 3 tries).
  2. The plugin finds the item in the open pocket as the Bag lists it (by
     ID: a taken item goes to the end of its pocket) and where the Bag's
     window and cursor are. If it is on a shown row it taps that row
     (`party-bag-row`: opens the item menu); otherwise one D-pad step
     (`party-bag-up` / `-down`) and read again. The Bag keeps cursor and
     scroll per pocket, so a fixed tap could hit the wrong item; stepping up
     never wraps (the item is above the view, so the cursor is not on the
     first).
  3. The item menu's entries are read (they depend on the item; in the
     Party's Bag Use is `0x06`, Give `0x07`, Cancel `0x11`) and the wanted one
     is tapped by its place from the bottom (`party-bag-menuN`, layout
     `party_bag_menu`; the menu grows upwards from Cancel). If the menu has
     no such entry (Use on a Poké Ball, Give on a TM) it backs out and says
     so.
  4. **Following the game** (`PartyFollow`): every frame the plugin reads
     what the game shows and, once it has held still (8-20 frames), does the
     next thing: a Bag message → A (`party-message-a`); a message on the Party
     screen → B; the Bag's list → back out of the Bag (`party-bag-back`); a
     Yes/No or an unknown menu → the Ask page; the teaching screen → the Ask
     page with the moves; the plain Party list → done. In any other scene (an
     evolution) it presses nothing, holds the panel off and lets the game's
     top screen show; a B there could stop the evolution. After 900 frames
     without progress in the Bag (3600 elsewhere) or 40 steps, it gives up,
     says so and steps aside for the panel (L+R).
  5. Answers: Yes / No taps `bag_yes_no`; a menu answer taps its entry from
     the bottom; on the teaching screen `party-teach-forgetN` / `-skip` taps
     the row, waits 30 frames, taps *Forget this move* / *Give up on learning
     the move* and waits for the screen to go.
  6. The result is read back: `party-ui: used Potion on Pichu: hp 2/27 ->
     22/27, status 0 -> 0, Potion x9 -> x8` (an earlier run) or `no effect:`
     when nothing changed ("It won't have any effect."); a TM counts as used
     when the moves changed (it stays in the Bag): `used TM01 on Torracat:
     ..., TM01 x1 -> x1, moves Scratch, Fire Fang, Double Kick, Lick ->
     Scratch, Fire Fang, Double Kick, Work Up`. Give: `gave Escape Rope to
     Torracat: held Oran Berry -> Escape Rope, Escape Rope x3 -> x2`; Take:
     `took Quick Claw from Torracat: held Quick Claw -> -, Quick Claw x0 ->
     x1`.

What the game did (instance `w72`, 2026-11-02): Give with nothing held: a
message, then the list. Give over a held item: a message, then "Would you
like to switch the two items?" (Yes / No); Yes: a message, the list. TM01 on
Torracat: three messages (A through each), the teaching screen (scene
`WazaOshie`, the same screen as the battle's), "Forget this move": back in the
Bag with more messages. If one of the three messages before the teaching
screen was a Yes/No question read as a message, A took its default; the
result was what was asked for (Work Up learned).

The plain list is a pixel probe (`WorldPartyList`, `Sources/Actions.cpp`): the
bottom bar's Y and X buttons (blue at 120,226 and 310,226, white at 205,226).

## The panel

AutoPanel's default for Party is the full view and for Status the panel. The
native screen holds the panel off (AutoPanel's manual override) while the Party
screen, the summary opened from it, the Bag of a running Use / Give item or the
teaching screen it leads to is up (and any other scene the item leads to, such
as an evolution, with the native layer hidden), and in the loading gaps
between them. AutoPanel drops the override on every
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

`partyui on|off|status` (default on; `status` also says whether it holds
the shared screen buffer and prints the heap). Log lines:
`party-ui: shown (SCENE, cursor N)` and `mem: party shown, heap ...`,
`party-ui: party N: Name L12 27/27 PSN Item, ...` (whenever it changes),
`party-ui: cursor N (Name)`, `party-ui: page menu|swap|items|ask ...`,
`party-ui: summary N (Name)` (opening), `party-ui: summary N of M: ...
ability ..., nature ..., moves Name PP, ...` (whenever the shown Pokémon
changes), `party-ui: summary hidden (game page)` / `shown`, `party-ui: swap
...`, `party-ui: use|give ...`, `party-ui: bag item I of N, scroll S row R,
tapping row T`, `party-ui: item menu 06 07 11, Use is 3 from the bottom`,
`party-ui: ask yes/no: ...`, `party-ui: ask menu (N entries: ...): ...`,
`party-ui: teach screen: Torracat wants Work Up (526, param 0); knows ...`,
`party-ui: answer N (label)`, `party-ui: used|no effect:|FAILED ...`,
`party-ui: gave|kept:|give FAILED: ...`, `party-ui: take ...` / `took ...`,
`party-ui: ... FAILED: why; back to the Party screen`, `party-ui: follow
FAILED: ...`, `party-ui: hidden` and `mem: party hidden, heap ...`; the actions' own `action: party-... ok` /
`FAILED`.

## Drawing

The game's look (`UI::Theme`, `docs/VISUAL-STYLE.md`, PR #38's plan):

- **List**: the Party screen's green backdrop; each Pokémon on a light list
  plate (`DrawRowBackground`), name in the game font with its gender sign
  at the right, Lv., status badge and `DrawHpBar` on the second line, held
  item and HP numbers on the third; the right column a little lower, as on
  the game's screen. The selected plate gets the game's orange frame
  (`DrawHighlightFrame`); the Pokémon being moved a blue one. Prompts along
  the bottom (`DrawButtonPrompt`): A Choose, Y Move, B Close.
- **Menu**: the list dimmed, the game's question window ("What would you like
  to do with Torracat?", `DrawWindow`) and the entries as option pills down
  the right (`RowKind::Option`, 28 px pitch), the red cursor on the chosen
  one.
- **Items**: the question window names the Pokémon and the pocket
  (`< Items >`); five list rows with the count at the right.
- **Ask**: the question in the window; Yes / No as option pills, or the moves
  as list rows with type plate and PP (`DrawTypePlate`, PP max from the game's
  move table) and *Don't learn ...* as a pill.
- **Footer**: a running action or the last result in a small window along the
  bottom ("Torracat forgot Lick and learned Work Up").
- **Summary**: a window with name, gender, Lv., species and position; HP,
  Item, Ability and Nature as dark green label bars with the values in the
  game's blue on light green; the four moves as light panels with type plate,
  category icon (`DrawCategoryIcon`), name and PP.

One `UI::Layer` over `PartyArea` (376x224 at 12,8, the X menu's area), redrawn
only when what it shows changes (`PartyViewKey` / `SummaryViewKey`) and copied
every frame. The plugin thread hands the view to the OSD callback under a mutex
(the callback only `TryLock`s). The layer draws into the native screens'
shared buffer (`UI/ScreenBuffer.hpp`, W7-M: one 400x240 BGR8 buffer
allocated at boot): the OSD callback borrows it while the layer shows and
gives it back when it doesn't, so the Party screen allocates nothing for its
drawing. Before that its own 246 KB buffer, kept or freed, ran the heap out
(freed, it could not be had again with 328 KB free; with Rotom's prompt and
the X menu's layer also kept, the game stopped with a PANIC as the Party
screen opened). Showing and hiding log the heap
(`mem: party shown, heap ...`, `ScreenBuffer::HeapLog`). The OSD callback is registered
before the panel's, so screenshots include it.

## Tests

- `tests/party_menu_test.cpp`: cursor moves on 1-6 Pokémon; every page and
  key (B left to the game on the list), the menu with and without Take,
  pockets for Use and Give, the Ask page; status names; the Bag plan; which
  item-menu entry is Use / Give (the menus seen, and the field Bag's);
  `PartyFollow` on its own and over runs of game screens: Potion, Give over a
  held item, TM (seen), Ether, evolution stone, Rare Candy (not seen; built
  from what the Bag doc and the move-learn doc describe); every action's taps
  are verified buttons (the Bag menu's 4th and 5th entries from the bottom
  are not: an Ether's move list would need them); each action against a fake
  Party screen / its Bags / the teaching screen / summary (summary, swap,
  Use on an item scrolled out of view, Give, Give over a held item with Yes
  and No, Take with a single B, each teaching row, a lost tap on the teaching
  screen); drawing (previews `party-preview`, `party-menu-preview`,
  `party-teach-preview`, `summary-preview` in `analysis/private/`).
- `scenarios/native-party.txt` (~6 min): X → native X menu → Pokémon → the
  native list (all six with level, HP, status, item, panel off); moves; Summary
  of Zorua (names), Down to Pikipek, Y and back, B; swap Torracat (1) and
  Buneary (6), the order holds after closing; a wild battle with Buneary in
  front (Defense Curl ×2, Run until it works) for damage; Use item → Potion on
  Buneary (HP up, Potion ×9 → ×8); Give item → Quick Claw to Buneary, Take
  item back; Use item → TM01 on Torracat, the native teaching question,
  forget Lick (Work Up learned); B to the X menu, B to the field;
  `check-save` twice. The battle is random: if the foe never hits, the
  scenario fails at the HP check after the battle and says so (rerun).

## Not done

- The battle's Pokémon command (W7-1 calls into the list).
- Moving a held item (Held Item → Move) and the game's X button: not mapped;
  Give and Take cover it.
- **Ether / Elixir, evolution stones, Rare Candy**: none in this save, so
  never tried in the game. The follow-up logic handles them as described
  above (an unknown menu → the Ask page; another scene → hands off, no keys),
  host-tested only. An Ether's move list was not seen: if it is a menu of the
  moves and Cancel, the plugin asks which move and taps it, but the 4th and
  5th entries from the bottom were never tapped, so the action layer refuses
  them and the native screen steps aside for the panel (L+R); if it isn't a
  menu the plugin reads, the native screen steps aside after 900 frames.
- A level-up outside battle (Rare Candy) asks about new moves; whether that is
  the Bag's Yes/No and then the same teaching screen was not seen.
- *Don't learn* on the teaching screen: host-tested only (the scenario
  forgets Lick).
- The Ask page for a Yes/No that isn't the Give switch can't show the game's
  text (it isn't read): it says "The game asks about the X" and points to L+R.
- Status values other than 5 (poison) were not seen.
- Eggs are drawn as "Egg" (no egg in this save).
- Fewer than six Pokémon: host-tested only.
