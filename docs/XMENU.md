# Native X menu (P2-2)

With the panel off, X in the field shows the X menu on the top screen. The
game's own X menu is still what runs: it sits on the hidden bottom screen, and
the native menu drives it with taps through the action layer (`docs/ACTIONS.md`).

- `Includes/Screens/XMenuGrid.hpp`, `Sources/Screens/XMenuGrid.cpp`: the pure
  part (no CTRPF): entries, grid, cursor moves and memory, the action for each
  entry, drawing. Host tests in `tests/xmenu_test.cpp` (`make -C tests` also
  writes a preview to `analysis/private/xmenu-preview.ppm`).
- `Includes/Screens/XMenu.hpp`, `Sources/Screens/XMenu.cpp`: the plugin glue:
  when to show, the panel, input, reading the game's cursor, the OSD layer.

## What it shows

The game's two pages side by side (page 1 on the left, page 2 on the right, in
the game's slot order), and a bottom row with **Map** (the native map,
`docs/MAP.md`) and
**Save**:

| Page 1 | | Page 2 | |
|---|---|---|---|
| Pokémon | Pokédex | Pokémon Refresh | QR Scanner |
| Bag | Quick Link | Battle Video | Options |
| Festival Plaza | Trainer Passport | (empty) | (empty) |
| Map (2 wide) | | Save (2 wide) | |

Poké Finder and Poké Pelago are not in this save's X menu (both pages checked
by screenshot); where they show up later in the game was not checked. They are
not entries; page 2's two empty slots are drawn as outlines. If a later save
fills those slots, they need entries here (and measured taps in the layouts).

Options, Trainer Passport and Battle Video open native top-screen screens
(`docs/NATIVE-SCREENS.md`; `XMenuOpensNative` lists them). With the cursor on
another entry that opens a scene, the footer says it opens on the bottom
screen (L+R shows it).

QR Scanner is shown but greyed out: its tap was never verified (Azahar has no
camera and hangs in it), so the action layer refuses it, and A logs
`xmenu: qr_scanner not available`. Mark it `verified: tap` in
`layouts/usum-v1.2.json` once it has been tried on hardware and it is enabled.

## Keys

| Key | Does |
|---|---|
| D-pad / circle pad | move (left/right stay in the row, up/down prefer the same column; no wrap); held: repeats after 15 frames, then every 5 |
| L / R | the same slot on page 1 / page 2 |
| A | open the entry |
| B, X | left to the game: close its X menu (the native one goes with it) |
| Y | left to the game: its save dialog |

While the native menu waits for input, A, the D-pad, L, R, Start and the
circle pad are captured (`InputCaptureBegin`), so the game's own cursor
doesn't move. Touch is not captured. When the menu goes away the capture ends;
a key still held then stays hidden until released.

## Opening an entry

Each entry is an `ActionDef` (`XMenuAction`), named `xmenu-<entry>`:

- Grid entries: if the game shows the other page, tap its page arrow
  (`xmenu.next_page` / `xmenu_page2.prev_page`) and wait for the page probe,
  10 more frames, then tap the entry. Done when its scene shows (Party, Zukan,
  Bag, TrainerPass, Kawaigari, BattleVideoPlayer, Config) or, for Quick Link
  and Festival Plaza (no scene change), when the X menu is gone.
  The Bag opens the native Bag (`docs/BAG.md`): it shows as soon as the
  game's Bag is up, and holds the panel off from the tap on.
- Save: press Y; done when the X menu is gone (the game's "Would you like to
  save your adventure so far?" is up). The player answers it on the bottom
  screen (the panel shows it); nothing here presses A.
- Map: `xmenu-close` (B; done when the field shows without the X menu), then
  the native map opens (`MapRequestOpen`, `docs/MAP.md`). With `map off`,
  `xmenu-map` as before: B, wait for the field, tap `field.map`; done in
  `TownMap`.

The page is read from the bottom screen: the blue page arrow is at 303,12 on
page 1 and at 16,12 on page 2 (`WorldXMenuPage2`, `Sources/Actions.cpp`).

## Cursor memory

The game remembers its X menu page and entry: it reopens where it was left,
and tapping an entry moves its cursor there. The native menu follows that:

- When the menu shows, it reads the game's cursor (the red arrow left of a
  pill; x 14-39 / 156-181, y 50-51 + 60·row, see `docs/LAYOUTS.md`) for up to
  60 frames and moves the native cursor there once three readings agree.
  Logged as `xmenu: game cursor page P slot S, native cursor NAME`.
- If the game's cursor is where it was last time (nothing was chosen on the
  bottom screen meanwhile), the native cursor stays where the player left it,
  including Map and Save, which the game doesn't have. So a cursor moved and
  closed with B is still there next time, as in the game.
- Opening an entry counts as the game's cursor moving there (it does).

## Native screens behind entries

The Pokédex entry opens the game's Pokédex as usual, and the native Pokédex
(`docs/POKEDEX.md`) takes over once the game's top level is up
(`XMenuNativeScreen`). While that entry's action runs (the X menu has gone,
the field and loading frames remain), the X menu keeps holding the panel off,
so it doesn't show for the second the Pokédex takes to load; in the Pokédex
the native screen holds it. `pokedex off` turns this off with it.

## The panel

AutoPanel's default for `FieldRo.xmenu` is the panel, and the field state it
follows is 4 for the X menu but also for Quick Link, the save dialog and
Festival Plaza's menu. So `XMenu.cpp` decides, without changing AutoPanel:

- From the frame the field state turns 4 (up to 45 frames, until the X menu
  probe sees the menu) and while the native menu shows, it holds the panel off
  with AutoPanel's manual override.
- When the X menu goes away it clears the override, and AutoPanel picks the
  mode for what is up now: panel for Quick Link and the save dialog, off for
  the field. Party is the native Party screen's (`docs/PARTY-UI.md`): opening
  Pokémon calls `PartyScreenExpect()`, which holds the panel off from the tap
  until the native Party list shows.
- If the player turns the panel on by hand (ZR) while the native menu shows,
  the native menu steps aside until the X menu closes (`xmenu: panel turned on
  by hand ...`).

## Commands and log

`xmenu on|off` (default on), `xmenu status`, `xmenu select NAME` (moves the
cursor; for tests), `xmenu open NAME` (as A on NAME). NAME: `pokemon pokedex
bag quick_link festival_plaza trainer_passport pokemon_refresh qr_scanner
battle_video options map save`. Log lines: `xmenu: shown (game page P)`,
`xmenu: hidden`, `xmenu: cursor NAME`, `xmenu: open NAME` (`... (native)` for
an entry with a native screen), then the action's
own `action: xmenu-NAME ok ...` / `FAILED ...`.

## Drawing

The menu looks like the game's own X menu (W7-4, `docs/VISUAL-STYLE.md`),
drawn with `UI::Theme`:

- **Background**: the game's yellow sun, `#E2C924` at the edge to `#FFFFC4`
  in the middle in rings, with faint rays; a dark gold `#A08B36` frame.
- **Entries**: pills in the colour of the game's pill for that entry (page 1
  and Save measured on the game's bottom screen: Pok�mon `#00A860`, Pok�dex
  `#E82C40`, Bag `#F47C14`, Quick Link `#1048E0`, Festival Plaza `#10A4AA`,
  Trainer Passport `#1868E8`, Save `#11B6FF`; page 2 and Map picked to
  match): a white rim, a dark line, a lighter upper half. Labels are white
  capitals with a dark outline in the pill's colour, in the game font at full
  size where they fit, else at 4/5. QR Scanner (disabled) is greyed.
- **Selection**: only the game's red cursor at the pill's left end, as in the
  game (no frame or colour change).
- **Save**: the blue bar with the Y button at its left end.
- **Prompts**: button chips (A Open, B Close, Y Save, L R Page); "Open (L+R)"
  for entries that only show on the bottom screen (Pok�mon Refresh). While an
  entry opens, or on a disabled entry, a message window instead.
- **Fallback**: before the game's font and icons are loaded (`UI/Assets.hpp`:
  normally at the first field; the menu asks for them when it shows), the
  system font, labels that don't fit in capitals in their own case, and a
  drawn cursor and button badges. The menu redraws once they arrive.

Not like the game (yet): the game's pills carry an icon each (Pok� Ball,
Pok�dex, Bag, ...) with the label under the pill in dark text, and a leaf
pattern; those textures are not loaded by the asset service, and the
native menu has both pages and Map side by side, so labels sit inside the
pills. The game's page dots and "NEW" badges are not shown.

One `UI::Layer` over `XMenuArea` (376x224 at 12,8), redrawn only when the
cursor or state changes and copied every frame (`docs/UI.md`). Its buffer
(337 KB at most) is allocated the first time the menu shows. The OSD callback
is registered before the panel's, so screenshots (`tools/shot.py`) include it.

## Tests

- `tests/xmenu_test.cpp`: the entry table (every grid entry's button exists and
  is verified, except QR Scanner), cells inside the area and not overlapping,
  every move and page flip, cursor memory, each action against a fake X menu
  from either page (one page tap only when needed), Save and Map, a page arrow
  the game ignores (retried, then failed without tapping an entry), drawing,
  and `xmenu-close` (the Map entry's hand-over to the native map).
- `scenarios/native-xmenu.txt`: X → native menu with the panel off; moves;
  every entry opened with A and backed out of (Save stops at the confirm
  dialog and backs out with B, then the same with Y); cursor memory after B;
  Festival Plaza in and out; B closes to the field with input given back.
  `check-save` after Save and at the end.
