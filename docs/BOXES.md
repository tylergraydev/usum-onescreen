# Native PC boxes (P2-6)

With the panel off, the PC at a Pokémon Center shows its boxes on the top
screen. The game's own Box screen is still what runs: it sits on the hidden
bottom screen, the native screen reads it from memory (`docs/RE6-boxes.md`)
and drives it with taps through the action layer (`docs/ACTIONS.md`).

- `Includes/BoxData.hpp`, `Sources/BoxData.cpp`: pure decoding (where the
  boxes are in the save data, cursor values, modes, names).
- `Includes/Box.hpp`, `Sources/Box.cpp`: memory reads (`ReadBoxStorage`,
  `ReadBoxView`, `BoxWorldFlags`) and the `box [N]` command.
- `Includes/Screens/BoxesGrid.hpp`, `Sources/Screens/BoxesGrid.cpp`: the pure
  part of the screen: actions, what each key does, drawing. Host tests in
  `tests/boxes_test.cpp` (`make -C tests` also writes a preview to
  `analysis/private/boxes-preview.ppm`).
- `Includes/Screens/Boxes.hpp`, `Sources/Screens/Boxes.cpp`: the plugin glue:
  when to show, the panel, input, the OSD layer.

## What it shows

```
        ◀ [        Box 1         ] ▶      1/8
  [Buneary][Ledyba ][Yungoos][Pikipek][Yungoos][Caterpie]
  [Meowth ][Grubbin][Magnem…][Wingull][Mime Jr][Abra    ]
  ...                                  (5 rows of 6, empty slots as outlines)
  ----------------------------------------------------------
  [Torracat][Zorua ][Pikipek][Pichu  ][Ledyba ][Buneary ]   the party
  A Move   Y Details   L/R Box   B Exit
```

- The box the game shows (its name, which of the unlocked boxes), every slot
  with the Pokémon's nickname ("Egg" for eggs), and the party in one row in
  the game's cursor order (30-35). The party is the Box screen's own copy,
  so it is compacted as the game does it while you move Pokémon.
- The cursor is the game's own (yellow ring); it is also on the box name
  when the game's cursor is there.
- Holding a Pokémon: the ring turns orange, an empty cell under it shows the
  held Pokémon's name, its old slot is dimmed, and the footer says what is
  held.
- While an action runs, the footer says what it does ("Picking up Ledyba...").

## Keys

| Key | Does |
|---|---|
| D-pad / circle pad | left to the game: moves its cursor (the native screen follows it) |
| A | on a Pokémon: pick it up (`box-pick-N`); holding one: put it down here (`box-drop-N`; swaps with a Pokémon already there) |
| L / R | previous / next box (`box-prev` / `box-next`), also while holding; act on release, so L+R still toggles the panel |
| Y | details: the native screen steps aside (a small "Y Boxes" badge stays) and the game's own top screen shows the Pokémon at its cursor: stats, nature, ability, held item, moves. The D-pad still moves the cursor; Y again comes back |
| B | left to the game: with nothing held it leaves the PC |
| X | taken and ignored, so the game doesn't switch to another Box mode under the native screen |

While the native screen waits for input, A, L, R, X and Y are captured
(`InputCaptureBegin`); the D-pad, circle pad, B and touch are not. While the
game's slot menu or a message is up (mode modal) nothing is captured, so A and
B answer it; the footer says so (L+R shows the bottom screen).

## Actions

Each is an `ActionDef` (`BoxesGrid.cpp`), with the Box screen's mode as a
World flag (`WorldBoxIdle`, `WorldBoxModal`, `WorldBoxCarrying`, read every
frame in `Actions.cpp` through `BoxWorldFlags`):

- `box-pick-N` (needs idle): tap `box.slot_N` / `box.party_N` (the game opens
  the slot menu, ~8 frames), wait 10 frames, tap `box_menu.move`; done when the
  game holds the Pokémon. Each tap is retried once.
- `box-drop-N` (needs holding): tap the slot; done when the game is idle
  again. Not retried: if the game refuses it shows a message, and a second tap
  would land there.
- `box-prev` / `box-next`: tap the arrow beside the box name, wait 20 frames.

Release, Held Item and Marking are in the layouts as `measured` and never
tapped; the host test checks that no action taps Release.

## The panel

AutoPanel's default for the Box scene is the full view. While the native
boxes show (and for up to 90 frames after the Box scene starts, while the
screen isn't readable yet), the panel is held off with AutoPanel's manual
override, as the native X menu does. If the player turns the panel on by hand
(L+R) the native boxes step aside until the PC closes (`boxes: panel turned on
by hand`). Box screens the native one doesn't know (the box list, Summary,
the tabs at the top) read as an unknown mode: after 20 frames the native
screen hides and lets AutoPanel decide.

## Commands and log

`boxes on|off` (default on), `boxes status`, `boxes details`, `boxes
pick|drop|prev|next` (as A / L / R, for tests), `box [N]` (the box data and
the screen's state). Log lines: `boxes: shown (box 1 "Box 1", idle, cursor slot
0 (Buneary))`, `boxes: cursor …`, `boxes: mode idle|modal|carrying`,
`boxes: box N`, `boxes: pick …`, `boxes: drop at …`, `boxes: details on|off`,
`boxes: hidden`, then the action's own `action: box-… ok …` / `FAILED …`.

## Drawing

One `UI::Layer` over `BoxesArea` (384x228 at 8,6) and one for the details
badge, redrawn only when the view changes and copied every frame
(`docs/UI.md`). Buffers (350 KB and 9 KB at most) are allocated the first
time the screen shows. The slots are read again every 30 frames and whenever
the box or mode changes. The OSD callback is registered before the panel's,
so screenshots include it.

## Tests

- `tests/boxes_test.cpp`: save layout and cursor values, modes, names and
  labels (with encrypted test Pokémon, an egg, an empty and a corrupt slot);
  every slot button exists, is tap-verified and doesn't overlap; every action
  passes `CheckAction` and never taps Release; what each key does in each
  mode; pick / drop / swap / box turn against a fake Box screen, a game that
  ignores taps (retried once, then failed without tapping Move), a refused
  drop (one tap, no tap into the message); cells, footer, cursor ring
  (yellow, orange while holding), drawing.
- `scenarios/native-boxes.txt`: see README (Testing → Scenarios).

## Not done

- Summary, Held Item, Marking, Release, the box list, team/battle box tabs,
  box names and wallpapers: not native (the panel, L+R, shows them).
- Levels of box Pokémon (the box data has experience, not levels).
