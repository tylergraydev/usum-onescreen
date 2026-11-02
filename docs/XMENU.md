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
the game's slot order), and a bottom row with **Map** (Rotom's field map) and
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
- Save: press Y; done when the X menu is gone (the game's "Would you like to
  save your adventure so far?" is up). The player answers it on the bottom
  screen (the panel shows it); nothing here presses A.
- Map: B (closes the X menu), wait for the field, tap `field.map`; done in
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

## The panel

AutoPanel's default for `FieldRo.xmenu` is the panel, and the field state it
follows is 4 for the X menu but also for Quick Link, the save dialog and
Festival Plaza's menu. So `XMenu.cpp` decides, without changing AutoPanel:

- From the frame the field state turns 4 (up to 45 frames, until the X menu
  probe sees the menu) and while the native menu shows, it holds the panel off
  with AutoPanel's manual override.
- When the X menu goes away it clears the override, and AutoPanel picks the
  mode for what is up now: panel for Quick Link and the save dialog, full for
  Party, off for the field.
- If the player turns the panel on by hand (ZR) while the native menu shows,
  the native menu steps aside until the X menu closes (`xmenu: panel turned on
  by hand ...`).

## Commands and log

`xmenu on|off` (default on), `xmenu status`, `xmenu select NAME` (moves the
cursor; for tests), `xmenu open NAME` (as A on NAME). NAME: `pokemon pokedex
bag quick_link festival_plaza trainer_passport pokemon_refresh qr_scanner
battle_video options map save`. Log lines: `xmenu: shown (game page P)`,
`xmenu: hidden`, `xmenu: cursor NAME`, `xmenu: open NAME`, then the action's
own `action: xmenu-NAME ok ...` / `FAILED ...`.

## Drawing

One `UI::Layer` over `XMenuArea` (376x224 at 12,8), redrawn only when the
cursor or state changes and copied every frame (`docs/UI.md`). Its buffer
(337 KB at most) is allocated the first time the menu shows. The OSD callback
is registered before the panel's, so screenshots (`tools/shot.py`) include it.

## Tests

- `tests/xmenu_test.cpp`: the entry table (every grid entry's button exists and
  is verified, except QR Scanner), cells inside the area and not overlapping,
  every move and page flip, cursor memory, each action against a fake X menu
  from either page (one page tap only when needed), Save and Map, a page arrow
  the game ignores (retried, then failed without tapping an entry), drawing.
- `scenarios/native-xmenu.txt`: X → native menu with the panel off; moves;
  every entry opened with A and backed out of (Save stops at the confirm
  dialog and backs out with B, then the same with Y); cursor memory after B;
  Festival Plaza in and out; B closes to the field with input given back.
  `check-save` after Save and at the end.
