# USUM menu style (W6-3)

What the game's own menus look like, measured on screenshots of Pokémon
Ultra Sun v1.2 in Azahar, and how `UI::Theme` (`Includes/UI/Theme.hpp`)
reproduces it for the native top-screen menus. Restyling the existing
screens is the next step; the plan is at the end.

Screenshots: instance `w63`, 2026-11-02, every native screen turned off so
the game drew its own bottom-screen menus (`battleui off`, `xmenu off`,
`partyui off`, `bagui off`, `pokedex off`, ...): X menu, Party, Party
submenu, summary (moves page and the two detail pages), Bag (Medicine and
Items), Pokédex top level, battle commands and move list. They are game
imagery, so they are kept in `analysis/private/shots/real-menus-*.png` and
`real-battle-*.png` of the `w6-3` worktree and not committed. Colours below
were read from those PNGs pixel by pixel (`analysis/private/w6-3/prof.py`),
except where a line says they come from the game's layout files.

## Text

- **Font**: the game's own font (`a/0/2/5` entry 0, `docs/GAME-ASSETS.md`),
  drawn **1:1**: 15x19 cells, baseline 15, 19 px line. Menu text in every
  screenshot is this font at full size; the theme's `TextSize::Body` draws
  it the same way and the demo's move names and Bag-style rows look the
  same as the game's next to each other (`compare2.png`, below). Spacing
  comes from the font's own width table.
- **Small text** (Lv., PP numbers, "Effective", notes) is smaller in the
  game; `TextSize::Small` is the same font at 4/5 (15 px line).
- **Colours**: black on light rows and options; white on dark windows and
  on type-coloured move buttons (the game gives those a dark shadow; the
  theme uses a 1 px outline in the type's dark line colour); values in the
  summary in blue `#008CFF` ("Impish", "Route 1"). Type plates and X menu
  labels: white capitals with a dark outline.
- Without the game font (it didn't load) the theme falls back to the
  system font (18 px line) with the same layout.

## Shapes

### Dark message window (`DrawWindow`)

The game's `Window_Basic`: "What would you like to do with Torracat?", the
summary's detail pages. From the edge in: 2 px black `#000000`, 1 px tan
`#A38B6A`, a 3 px bevel `#4E4D2D` → `#373624`, then the fill `#2B2A19`
(summary; the Party question is a touch darker, `#261F14`). Corner radius
about 6 px. Text inset 8 px, one 19 px line per line of text.

### List row (`DrawRow`, `RowKind::List`)

The Bag's item rows: rounded at both ends, 28 px pitch with rows touching.
Top to bottom: 2 px white rim `#F2FFFF`, 2 px lime `#CEF024`, fill from
white `#FFFFFF` through `#D6FFFF` to mint `#AEF0AE`, a 2 px dark green line
`#297E29` and 2 px light green `#AAEEAA` at the bottom. Label black, about
22 px in from the left end (room for the cursor).

### Menu option (`DrawRow`, `RowKind::Option`)

The Party submenu (Summary / Restore / Held Item / Back): about 32 px tall
pills, 2 px black outline, white rim, fill white `#F2FFFF` → `#CDFDFF` →
sky `#78E8FF`, a yellow glint `#FFF54E` along the top right, a 2 px dark
blue `#004CA4` base line. Label black, centered.

### Selection

- **The cursor** is the red arrow (`Common_Cursor_02`, red `#EE0000` with a
  dark edge), pointing right, its tip about 12-14 px inside the row's left
  end. Lists, options and the move list mark the selected entry **only**
  with the cursor; the row itself doesn't change (`DrawCursor`; `DrawRow`
  draws it for a selected row).
- The **Party** plates add an orange frame `#FF5E04` around the selected
  Pokémon (`DrawHighlightFrame`).
- The **Pokédex** top level colours the selected row red `#FF2211`, other
  rows orange `#FF6600`, empty ones brown `#4C1D00`, all with a dark red
  line `#441111` (`DexSelected`, `DexRow`, `DexEmpty`, `DexLine`).

### Battle move buttons (`DrawMoveButton`, `DrawTypePlate`)

Four buttons of about 170x46 at the right, each: move name (white, dark
shadow), a type plate, "PP 35 / 35", and a small "Effective" note. The
button colours per type come from the battle layout itself: its colour
animations `Battle_PLT_LOW_002_color_00..17` (`a/0/6/7` entry 8) set the
button's dark and light material colours and its outline's, one animation
per type ID (`TypeColor`, the table in `Theme.cpp`). The button is a light
upper half over a darker lower half with a light line on top. The selected
button is marked by the cursor.

Type plates (the "FIRE" chips, 50x20 in the game) are pills with a white
rim and the type name in white capitals with a dark outline. The game's
plates are textures with the name baked in; only Normal's is in the RomFS
(`docs/GAME-ASSETS.md`), so the theme draws them: type-coloured fill, the
type's name from the game's own text.

### HP and PP gauges (`DrawHpBar`, `DrawPpBar`)

The Party HP bar: a black frame (2 px on top, 1 px elsewhere) around a 6 px
fill that runs from `#B5FF37` on the left to `#07FF20` on the right. The
battle layout's gauge animations give the three states exactly
(`Battle_GAG_002_color_normal/half/few`): green `#B8FF37` → `#00FF1F`,
yellow `#FF9E37` → `#FFFF1F`, red `#FF0050` → `#FF000C`. Thresholds as in
the games: above 1/2 green, above 1/5 yellow, else red (`HpLevel`). Every
Pokémon was at full HP in the screenshots, so the empty part's colour was
not seen; `GaugeEmpty` is our pick.

The game shows PP as text only. `DrawPpBar` and `PpTextColor` (yellow at
1/2, orange at 1/4, red at 0) are the plugin's own, in the HP gauge's
colours; not checked against the game.

### Button prompts (`DrawButtonPrompt`)

The game uses its 16x16 button icons (white letter in a black circle) next
to a label: "(A) Check Moves" on the summary's top screen, "(X) SORT
(Y) MOVE" on the Bag's top screen, "L+A Move info" over the move list, and
the back button with an orange (B) badge at the bottom left. The theme's
chip is that icon on a translucent dark pill (`PromptFill`) with a white
label; L, R, START and SELECT get a small badge with their name (the game
has no L or START icon in the files the plugin reads).

### Other screens, for reference

- **X menu**: a pale yellow sun background `#FFF7C4`, big coloured pill
  buttons with an icon each and a white capital label with a dark outline
  under it, two columns; a blue SAVE bar `#11B6FF` at the bottom right.
- **Summary (top screen)**: label column in dark green bars `#146E1C`
  with white text, values on light green `#C6F88D` → `#E2FAB8` with black
  text; a type plate next to Type.
- **Backgrounds**: each screen has its own (Bag orange `#FAB813`, Party
  and summary green waves, Pokédex light blue `#90DCF2`, battle the blurred
  scene). The demo uses a plain gradient; screens should keep their own
  background colour family.

## Spacing and sizes (top screen pixels, all 1:1 with the game)

| Thing | Size | Theme constant |
|---|---|---|
| List row | 28 px pitch, rows touching | `RowHeight`, `RowPitch` |
| Menu option | 30-32 px tall, 2-4 px apart | `OptionHeight` |
| Row label inset | ~22 px from the left end | `RowTextInset` |
| Window text inset | 8 px | `WindowPad` |
| Window corner | ~6 px radius | `WindowRadius` |
| Cursor | 16x16 | `CursorW`, `CursorH` |
| Button icon | 16x16; chip 20 px tall | `PromptHeight` |
| HP fill | 6 px tall inside the frame | `GaugeHeight` |
| Type plate | 50x20 (the theme's, 56x16 in the demo) | `TypePlateW`, `TypePlateH` |
| Text line | 19 px (Body), 15 px (Small) | `LineHeight()` |

## The demo, next to the game

`uitheme on` (`Screens/ThemeScreen.hpp`, scenario `scenarios/theme-demo.txt`)
draws a 320x240 sample in the middle of the top screen: page 0 like the
battle move list (the lead Pokémon's real moves, types, PP from the game's
move table, type names from its text), page 1 like the Bag and the Party
submenu (the real party's names and levels). `uitheme page N` and
`uitheme select N` change page and cursor.

Side by side (local only, game imagery): `analysis/private/w6-3/compare2.png`
puts the game's move list next to page 0 and the Party submenu next to
page 1. What matches: the font and text size (identical glyphs), the type
plates' place and text, the PP text, the option pills and the Bag-style
rows, the window, the cursor, the button icons and the ball icons. What
doesn't (yet): the move buttons' arrow-shaped left end and their
"Effective" line, the game's backgrounds, and the move buttons' texture
detail (the theme uses flat gradients in the game's colours).

The host test (`tests/theme_test.cpp`) draws the same demo with a
synthetic font and icons and without any, as
`analysis/private/theme-preview-*.ppm`.

## Menu motion

`[display] ui_motion = on` (default) eases the X-menu arrow, Party selection
frame and Bag-list arrow over eight top-screen callbacks using integer
quadratic ease-out. Party-list and Bag party-panel HP bars ease in either
direction over 24 callbacks. Numbers and input selection update immediately;
only the drawn frame/bar moves. A new target starts at the displayed value.
Opening or borrowing a fresh layer snaps to the first target. Replaced members
and maximum-HP changes snap; view identity hashes cannot distinguish identical
members because these views do not expose persistent Pokemon IDs.

Motion state uses fixed storage and no clock or allocation. The existing layer
is redrawn only for a changed view or active motion, including the final target
frame; a busy view lock keeps that redraw pending. Once settled, the original
cached-layer presentation resumes. There are no extra GPU passes or drawing
commands per rendered frame. Existing full-screen renderers still create their
usual temporary strings/vectors on redraw, so animation repeats those costs.
Opening slide/fade is omitted: this layer path would require extra full redraws
or a presentation change. Battle and summary rendering remain instantaneous.

`ui_motion = off` restores the original pixels and drawing order. Host checks
in `tests/check_motion.py` expand the Makefile source lists into direct MSVC
commands, run easing/settling/setting tests, render existing previews with
motion explicitly off, and compare eight SHA-256 hashes captured from `HEAD`.
The preview tests also exercise the guarded 2.5x layer path. Commands are saved
under `tests/build/motion-commands.json`. Run from the repository root:
`python tests/check_motion.py`; make and WSL are unnecessary.

## Restyle plan (next step, one session per screen)

Each screen only changes its own `Sources/Screens/*` drawing code and uses
`UI::Theme`; nothing else is shared, so these can run in parallel. One
shared change first, so every screen gets the game's font and icons: load
`GameAssets` once at boot on the plugin thread (move `Load` out of
`Screens/ThemeScreen.cpp` into a small service, ~250 KB kept; see
`docs/GAME-ASSETS.md`), then:

| Screen | Files | What it gets |
|---|---|---|
| Battle menu | `BattleScreen.cpp`, `BattleMenu.cpp` | `DrawMoveButton` + `DrawTypePlate` per move (type and PP from `GameAssets::Move`), `PpTextColor`, cursor only for the selection; command bar as option pills; "L+A Move info" style header and prompts |
| X menu | `XMenu.cpp`, `XMenuGrid.cpp` | option pills in the game's grid, white outlined capital labels, cursor, B/Y prompts; pale yellow background |
| Keyboard | `Keyboard.cpp`, `KeyboardGrid.cpp` | keys as small option pills, the name field in a `DrawWindow`, prompts |
| Pokédex | `Pokedex.cpp`, `PokedexList.cpp` | `DexSelected` / `DexRow` / `DexEmpty` rows, ball icon for caught counts, light blue background |
| Options, Passport, Battle Video | `SubScreens.cpp`, `Options.cpp`, `TrainerPassport.cpp`, `BattleVideo.cpp` | `DrawWindow` panels, list rows, option pills for choices |
| Bag | `Bag.cpp`, `BagList.cpp` | done in W7-3 (`docs/BAG.md`): list rows (28 px), the party column with `DrawHpBar`, the item description in a `DrawWindow`, A/B prompts (X and Y are not used: the native Bag doesn't sort or move), the game's pocket icons |
| Map | `Map.cpp`, `MapView.cpp` | place name in a `DrawWindow`, prompts |
| Party and summary | `PartyScreen.cpp`, `PartyMenu.cpp` | plates with `DrawHighlightFrame` and `DrawHpBar`, gender icons, the submenu as option pills; summary moves with type plates and category icons, blue values |
| PC boxes | `Boxes.cpp`, `BoxesGrid.cpp` | box title in a `DrawWindow`, party as list rows, ball icons, prompts |
| Move learning, Rotom prompts | `MoveLearn.cpp`, `RotomPrompt.cpp` (in progress elsewhere) | question in a `DrawWindow`, answers as option pills, cursor |
