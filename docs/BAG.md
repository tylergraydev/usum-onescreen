# Native Bag (P2-4, W7-3)

With the panel off, the field Bag shows on the top screen. The game's own Bag
still runs on the hidden bottom screen; the native Bag reads it from memory
and carries out each choice there with taps and presses through the action
layer (`docs/ACTIONS.md`), so the game does every change itself.

- `Includes/Screens/BagList.hpp`, `Sources/Screens/BagList.cpp`: the pure part
  (no CTRPF): tabs, the game state as read (`BagGame`), the native list
  cursor, which menu entries are native, items with a follow-up and the
  result lines, the pocket icons and descriptions read from the RomFS, the
  driver, the actions, drawing.
  Host tests in `tests/bag_test.cpp` (`make -C tests` also writes a preview to
  `analysis/private/bag-preview.ppm`).
- `Includes/Screens/Bag.hpp`, `Sources/Screens/Bag.cpp`: the plugin glue:
  reading the game's Bag, when to show, the panel, input, the result line,
  the OSD layer.

The in-battle Bag (Poké Balls, battle items) is not part of this. Its
layout is the same archive (`a/0/6/5` has `visible_battle` / `bag_battle`
animations), but whether the battle's Bag uses the same scene and frame
objects that `ReadBagGame` checks was not looked at. The entry point for a
screen that opens the field Bag is `BagExpectOpen()` (`Screens/Bag.hpp`):
call it when the tap that opens the Bag is sent, so the panel stays off
until the Bag's first frame (the X menu does this).

## What it shows

The whole top screen, in the game's Bag look (`docs/VISUAL-STYLE.md`, drawn
with `UI::Theme`, so in the game's font and with its cursor once the shared
game assets are loaded):

- The Bag's orange background.
- At the top, the pocket's name on a green tab over a dark window
  (`DrawWindow`): the cursor's item with "× 9" and its description (two
  lines, the game's own text); while a menu is up the game's question
  ("What do you want to do with the Potion?", "Which Pokémon do you want to
  teach it to?", "Would you like to switch the two items?"); after a choice
  its result in blue ("Pichu: HP 20 -> 27/27", "Torracat did not learn the
  move", "Exp. Share turned off", "Lens Case moved to the Free Space").
- On the left the party on green plates: nickname, level when it fits, the
  HP bar (`DrawHpBar`), an orange dot if it holds an item. During "Which
  Pokémon...?" the chosen one gets the orange frame and the red cursor; in
  the menu the plates are dimmed.
- On the right the open pocket's items on the theme's green list rows (28
  px), five at a time, scrolling with the cursor (arrows when the list goes
  on); count and a red dot for NEW. The red cursor marks the item. The list
  is the game's own (frame `+0x24`), so sorting and Free Space look as on
  the bottom screen.
- The item menu as option pills anchored at the bottom right, as the game's
  (Yes / No the same way); entries the native Bag can't carry out are grey.
- Along the bottom: the button prompts (A Choose, B Close; A OK, B Back) and
  the seven pocket icons between L and R, the open one on a white slot. The
  icons are the game's own (below); without them each pocket is a green dot
  with its first letter.

Not shown: the money and Z-Crystals (no tab in this save), item icons other
than the pockets', TM compatibility ("Able!" / "Unable!", the game shows it
on the bottom screen only).

## Pocket icons and descriptions

Read once, the first time the Bag shows, in a CTRPF Task (`LoadBagAssets`,
logged as `bag assets: ...`), from the running game's RomFS; nothing of the
game is in the repo.

- **Pocket icons**: `a/0/6/5` entry 0, the Bag layout (LZ11 ALYT, 74 KB
  packed, 286 KB unpacked), `timg/Bag_Icon_*.bflim`, 24x24 ETC1A4 (flags 8,
  not transposed). Which icon belongs to which pocket comes from the tab
  animations `anim/Bag_BTN_LOW_001_icon_00..15.bflan`: their texture-pattern
  (`FLTP`) key on the `Icon_Field` pane ends on the selected icon in
  `icon_00..06` (and `icon_14`) and on its `2` variant in `icon_07..13`
  (`icon_15`). By the game's tab index (Bag param `+0x54`): 0 Rotom Powers
  `Bag_Icon_06f`, 1 Medicine `01b`, 2 Items `01c`, 3 TMs `01d`, 4 Berries
  `01e`, 5 Key Items `01f`, 6 Free Space `01g`; the `2` variant (`01b2`,
  ...) is the white-rimmed unselected icon. The pictures match: a green
  bottle, a capsule, a disc, a berry, a key, a heart, Rotom. `Bag_Icon_04`
  (a white diamond, `icon_02`'s first frame) is probably Z-Crystals: not
  used. The archive is not unpacked whole: `Lz11ArchiveFiles` streams it
  through a 4 KB window and keeps only its header (47 KB) and the 14 icons,
  because the plugin's heap is short (below). 14 of 14 loaded in 140 ms in
  Azahar (instance `w73`).
- **Descriptions**: message file 39 of `a/0/3/<language>`, line = item ID
  ("A spray-type medicine for treating wounds..." is line 17, Potion). The
  file is 190 KB, so only its place is kept; each description is read
  (header, line entry, ~200 bytes of text) and decrypted when the cursor
  first lands on that item (`ReadMessageLine`), then kept.

## Memory

The plugin has 5 MiB (`usum-onescreen.plgInfo`) for code, data and heap,
and it is built without exceptions: a failed `new` in a container aborts
the plugin ("Fatal plugin error", `abort()` in Azahar's log). Measured with
`bagui status` and the `bag: shown` line (heap from `mallinfo`, instance
`w73`): 1148 KB used after boot, about +248 KB for the shared game assets
(W7-0), about +516 KB once the X menu had shown, 2196 KB at the Bag's first
show in `native-bag` (after the wild battles). Every native screen keeps its
own layer once it has shown.

What went wrong, and what the Bag does now:

- The Bag's own full-screen layer (first 384 KB, then 281 KB sized for the
  BGR8 top screen) failed to allocate after the game assets and the X menu,
  and, freed on hide, could not be had again after a TM's forget screen
  (fragmentation). It now draws into the shared screen buffer
  (`UI/ScreenBuffer.hpp`, W7-M): one 281 KB buffer allocated at boot, which
  the OSD callback borrows while the Bag shows (owner `"bag"`) and gives back
  when it hides. While another screen still holds it the Bag skips that
  frame and asks again. The `bag: shown` / `bag: hidden` lines and `bagui
  status` carry `ScreenBuffer::HeapStatus()`.
- The first icon loader unpacked the whole layout archive (74 + 286 KB) and
  aborted the plugin in `bad_alloc`; then, after the battles, even
  `RomfsFind` ran out. The loader now streams the archive (about 60 KB at
  most) and only starts if a 192 KB probe allocation succeeds; otherwise it
  logs `bag assets: not loaded, less than 192 KB of heap free` and the Bag
  draws its own pocket dots.

## Keys

| Where | Key | Does |
|---|---|---|
| list | D-pad / circle pad up, down | move (wraps; held: repeats after 15 frames, then every 4) |
| list | L / R, left / right | previous / next pocket (wraps) |
| list | A | open the item's menu |
| list | B | close the Bag (back to the X menu) |
| menu | up, down, A | choose an entry; B: back to the list |
| party pick | up, down, A | choose a Pokémon; B: back to the list |
| Yes / No | up, down, A | answer; B: No |

While the native Bag shows, A, B, X, Y, L, R, Start, the D-pad and the circle
pad are captured (`InputCaptureBegin`); X (sort) and Y (move) would open game
screens the native Bag doesn't know. Touch is not captured. Messages ("It won't
have any effect.", "Your Pokémon is now holding...") are passed with A by the
native Bag itself.

Native: every entry of every item menu seen (`BagEntryEnabled`): **Use** in
every pocket, **Give**, **Move to Free Space**, **Return to Pocket**, Key
Items' **On** / **Off**, **Cancel**, **Yes**, **No**. An entry at the fifth
position from the bottom (never seen) is greyed and says "use the bottom
screen (L+R)".

### Items with a follow-up

What the game does after Use depends on the item (`BagItemKindOf`):

| Item | The game | The native Bag | Checked |
|---|---|---|---|
| Potion, status heals, Berries | "Which Pokémon...?", HP bar, message, back to the pick | pick, A through the messages | in game (P2-4) |
| TMs (TM01-100) | "Which Pokémon do you want to teach it to?" (plates say Able! / Unable!), "You booted up the TM.", "Please choose a move that will be replaced with Work Up.", then its own move-forget screen (scene `WazaOshie`); back in the Bag "Torracat did not learn Work Up." | pick, A through the messages; hands over to the forget screen (shown on the panel, AutoPanel's `default`) and takes the flow back when the Bag returns; the result compares the moves | in game: TM01 on Torracat, backed out of the forget screen (`w73`, by hand and in the scenario) |
| Exp. Share | its menu has Off (0x02) or On (0x01) instead of Use; a message | choose, A | in game (`w73`, scenario) |
| Escape Rope indoors | "Kukui's words echoed...", two messages, the list | A through them; "Escape Rope wasn't used" | in game (`w73`, by hand) |
| Ether, Max Ether, PP Up, PP Max | after the Pokémon, a PP screen with its four moves (layout `Bag_PPX_LOW_000`: `SkillButton_00..03` centred at y 76, 106, 136, 166, back at 22,224) | not read: when the game stays busy for 3 s during a Use (`BagAsideFrames`), the native Bag steps aside and the panel shows until the game is back in its list or pick (`BagFollowGame`); the result then compares PP | host tests only (no Ether in the save) |
| Elixir, Max Elixir | a Pokémon, a message | as Potion | host tests only |
| Repel, Super Repel, Max Repel | a message (a Yes / No if one still works) | A, Yes / No | host tests only (no Repel) |
| Evolution stones | a Pokémon, then the evolution scene | hands over like a TM; the result says "evolved into ..." | host tests only (no stone; the Sparkling Stone slot is at count 0) |
| Rare Candy | a Pokémon, the level-up stats, maybe a move and an evolution | messages with A, the stats by stepping aside, scenes by handing over; "grew to Lv. 13, learned ..." | host tests only (no Rare Candy) |
| Z-Power Ring, Lens Case, Roto Boost | no Use in the field (Free Space, Give) | - | in game (menus read) |

Registering a Key Item to a button is not in USUM's menus (none seen).

## How a choice is carried out

The game's Bag state is read every frame (`ReadBagGame`, docs/OFFSETS.md
"Field Bag screen"): mode (list, menu, pick, message, busy), tab, the open
pocket, the cursor and first row of its window, the menu entries, the party
cursor, the item the menu is about.

`BagDriver` turns a goal into taps and presses, one at a time: it sends one
command (a small `ActionDef`, `bag-...`), waits until the action is done and
the game has looked the same for 6 frames (12 for messages), looks again, and
decides the next. Goals:

- **Tab**: tap the pocket tab until the game shows it (3 tries).
- **Open** item i: if it is in the game's window, tap its row (rows 1-7);
  otherwise press down / up (one press, then look) until it is. Done when the
  game's menu is up for that item. If the item at i isn't the one chosen any
  more, it stops without tapping.
- **Choose** entry k of n: tap the menu at position n - k from the bottom.
  If the game leaves the Bag screen after a tap (a hand-over), the goal is
  done, not failed; the same for Pick, Answer and Advance.
- **Pick** slot s: tap the slot; a tap on a slot not under the game's cursor
  only moves the cursor, so it taps again once the cursor is there.
- **Answer**: tap Yes / No.
- **Advance**: A while a message waits.
- **Cancel**: Cancel in the item menu, else B; **Close**: the back button.

A goal fails (logged as `bag: GOAL: why FAILED`, shown in the footer) on a
screen it doesn't expect, after 600 frames or 64 commands; the game is left
where it is and the panel (L+R) can finish by hand.

D-pad presses: a 3-frame press was sometimes not seen by the Bag right after
a pocket change, and a 10-frame press moved two rows (key repeat); the
action layer's 6-frame press moved exactly one row in every try. The driver
doesn't count on either: it looks after each press.

## Commands and log

`bagui on|off` (default on), `bagui status` (also the icons loaded, the
descriptions read so far and the heap), `bagui read` (what the plugin
reads of the game's Bag), `bagui select N` (moves the native list cursor; for
tests). Log lines: `bag: shown (pocket P, N items)`, `bag: pocket P, N items,
game cursor C`, `bag: cursor I NAME`, `bag: open I NAME`, `bag: menu ITEM:
entries`, `bag: question ITEM: Yes, No`, `bag: game pick|message|list`,
`bag: pick cursor S NAME`, `bag: choose ENTRY (ITEM)`, `bag: pick slot S NAME`,
`bag: answer yes|no`, `bag: GOAL done in N frames, M commands`, `bag: choose
ENTRY (ITEM, KIND)`, `bag: result use|give|free-space|return|on|off ITEM(id)
on slot S NAME: count A -> B, hp .. -> .., held .. -> .., level .. -> ..,
moves a,b,c,d -> a,b,c,d, pp A -> B` (or `..., no Pokémon chosen`), `bag:
handed over to SCENE (ITEM)`, `bag: back from SCENE`, `bag: the game shows a
screen the native Bag doesn't read (ITEM), the panel takes over`, `bag: back
from the bottom screen`, `bag: the Bag closed during ITEM`, `bag assets:
...`, `bag: hidden`; and the action layer's own `action: bag-... ok`.

## The panel and the X menu

AutoPanel's default for the Bag scene is the panel. While the native Bag
shows it holds the panel off with AutoPanel's manual override (as the X menu
does), and clears the override when the Bag closes. If the player turns the
panel on by hand (L+R), the native Bag stops what it was doing and stays
hidden until the Bag closes. On the frame the scene changes AutoPanel clears
its override itself and turns the panel on for the Bag; that one is not the
player's doing and is just turned off again. The X menu's Bag entry tells the Bag it is
opening (`BagExpectOpen`), so the panel stays off between the tap and the
Bag's first frame.

## What the game does (seen in P2-4)

- The item menu grows upwards from Cancel; its entries depend on the item
  (docs/LAYOUTS.md). After Use the game asks "Which Pokémon...?"; after a
  Potion it shows the HP bar filling (the frame's mode bytes read as the list
  then, but the param state byte doesn't), then "...'s HP was restored", and
  stays on the pick for another use; B goes back to the list. "It won't have
  any effect." also returns to the pick. The item count drops when that
  message is dismissed.
- Give: "Your Pokémon is now holding the X!", then the list. Over a held
  item: "P is already holding a Y." then "Would you like to switch the two
  items?" (Yes / No, the item menu object with entries 0x12 / 0x13); Yes:
  "You took the Pokémon's Y and gave it the X.".
- A pocket remembers its own cursor and first row; the list hides count-0
  items.

## Tests

- `tests/bag_test.cpp`: tabs, their buttons and icons; every command's action
  exists and may run (except menu position 5, never tapped); the cursor and
  window; which entries are native (every menu seen); item kinds, when to
  step aside, every result line (Potion, Ether, Rare Candy, stone, TM learned
  or not, Repel, Exp. Share, Free Space, Give); descriptions read one line
  at a time from a made-up message file; the pocket icons streamed out of a
  made-up LZ11 layout archive bigger than the window (and a cut stream, not
  LZ11, not an archive); a RomFS that isn't there; the driver against a fake
  Bag (pocket switch, open in / below / above the window, a changed list, Use
  with the two-tap pick and messages, Give over a held item with Yes, a TM
  handing over to another screen and coming back, Move to Free Space,
  Cancel, Close, a game that ignores taps, a wrong starting screen, the Bag
  closing, settling); drawing every mode over the whole screen, with and
  without the game's icons.
- `scenarios/native-bag.txt`: Pichu is hurt in a wild battle (switched in for
  a free hit, then Run; battles repeat until `party` shows Pichu at 7-26 HP,
  `repeat N until-log` in `tools/scenario.py`); X menu -> Bag with auto on and
  the panel held off; every
  pocket with R (wrapping) and L / left / right; the cursor wrapping; a Potion
  on Pichu (HP back to full, count 9 -> 8) and on Torracat (no effect); Quick
  Claw given to Pichu; Escape Rope given over it (Yes); a Poké Ball (No);
  TM01 on Torracat through the game's move-forget screen (on the panel) and
  back, not learned; Exp. Share off and on; Lens Case to the Free Space and
  back; a menu cancelled with B; B closes the Bag to the native X menu.
  `check-save` at the end. Passed in 187 s (instance `p24`, 2026-11-02).
- `actions.txt` and `open-each-app.txt` check the Bag's panel: they send
  `bagui off` first.
