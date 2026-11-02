# Native Bag (P2-4)

With the panel off, the field Bag shows on the top screen. The game's own Bag
still runs on the hidden bottom screen; the native Bag reads it from memory
and carries out each choice there with taps and presses through the action
layer (`docs/ACTIONS.md`), so the game does every change itself.

- `Includes/Screens/BagList.hpp`, `Sources/Screens/BagList.cpp`: the pure part
  (no CTRPF): tabs, the game state as read (`BagGame`), the native list
  cursor, which menu entries are native, the driver, the actions, drawing.
  Host tests in `tests/bag_test.cpp` (`make -C tests` also writes a preview to
  `analysis/private/bag-preview.ppm`).
- `Includes/Screens/Bag.hpp`, `Sources/Screens/Bag.cpp`: the plugin glue:
  reading the game's Bag, when to show, the panel, input, the result line,
  the OSD layer.

The in-battle Bag (Poké Balls, battle items) is not part of this; it belongs
with the native battle menu (PR #24) once that is merged.

## What it shows

- Seven pocket tabs in the game's order: Rotom Powers, Medicine, Items, TMs,
  Berries, Key Items, Free Space. The open one is the game's.
- The open pocket's items with counts and a red dot for NEW, eight rows,
  scrolling with the cursor. The list is the game's own (frame `+0x24`), so
  sorting and Free Space look exactly as on the bottom screen.
- The party: nickname, HP and a bar; an orange dot if it holds an item.
- When the game's item menu is up, its entries over the party column, with
  the item's name; "Switch items?" with Yes / No when the game asks that.
- The footer: the keys, `...` while the game is busy, or the result of the
  last Use / Give ("Pichu: HP 20 -> 27/27", "Pichu now holds Escape Rope
  (Quick Claw back in the Bag)", "Potion had no effect on Torracat").

Not shown: item descriptions (not read), the money and Z-Crystals (no tab in
this save).

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

Native: **Use** for Medicine and Berries (they only ask for a Pokémon),
**Give** everywhere, **Cancel**, **Yes**, **No**. Use in the other pockets can
leave the Bag (Escape Rope), start an evolution or teach a move, and Move to
Free Space was never tapped: those entries are greyed and say "use the bottom
screen (L+R)".

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

`bagui on|off` (default on), `bagui status`, `bagui read` (what the plugin
reads of the game's Bag), `bagui select N` (moves the native list cursor; for
tests). Log lines: `bag: shown (pocket P, N items)`, `bag: pocket P, N items,
game cursor C`, `bag: cursor I NAME`, `bag: open I NAME`, `bag: menu ITEM:
entries`, `bag: question ITEM: Yes, No`, `bag: game pick|message|list`,
`bag: pick cursor S NAME`, `bag: choose ENTRY (ITEM)`, `bag: pick slot S NAME`,
`bag: answer yes|no`, `bag: GOAL done in N frames, M commands`,
`bag: result use|give ITEM(id) on slot S NAME: count A -> B, hp .. -> .., held
.. -> ..`, `bag: hidden`; and the action layer's own `action: bag-... ok`.

## The panel and the X menu

AutoPanel's default for the Bag scene is the panel. While the native Bag
shows it holds the panel off with AutoPanel's manual override (as the X menu
does), and clears the override when the Bag closes. If the player turns the
panel on by hand (L+R), the native Bag stops what it was doing and stays
hidden until the Bag closes. The X menu's Bag entry tells the Bag it is
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

- `tests/bag_test.cpp`: tabs and their buttons; every command's action exists
  and may run (except menu positions 2 and 5, never tapped); the cursor and
  window; which entries are native; the driver against a fake Bag (pocket
  switch, open in / below / above the window, a changed list, Use with the
  two-tap pick and messages, Give over a held item with Yes, Cancel, Close, a
  game that ignores taps, a wrong starting screen, the Bag closing, settling);
  drawing every mode inside the area.
- `scenarios/native-bag.txt`: Pichu is hurt in a wild battle (switched in
  twice, then Run); X menu -> Bag with auto on and the panel held off; every
  pocket with R (wrapping) and L / left / right; the cursor wrapping; a Potion
  on Pichu (HP back to full, count 9 -> 8) and on Torracat (no effect); Quick
  Claw given to Pichu; Escape Rope given over it (Yes); a Poké Ball (No);
  a menu cancelled with B; B closes the Bag to the native X menu.
  `check-save` at the end.
