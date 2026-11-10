# Ride Pager (W5-4, backlog #10)

**Status: researched on 2026-11-09 (instance `postgame`, Ultra Sun USA v1.2,
100 % save, start point `postgame` = Heahea City, zone 90, outdoors, at night).
Everything found for a native top-screen Ride Pager is below.** Plugin as
installed in that instance, `auto off`. Nothing was saved in game; the
instance was stopped afterwards. Evidence: screenshots
`analysis/private/research/ride-pager/rp-*.png` (`-top` / `-bottom`), the
plugin log `analysis/private/instances/postgame/azahar/user/sdmc/usum-onescreen.log`,
heap dumps `usum-dump-rpopen0/rpopen1/rpclosed.bin` (whole linear heap,
`30000000+4DE8000`) and player-object dumps `usum-dump-plm/plu/plm2.bin` in
the same folder.

"Seen" = checked in game this session (screenshot, `peek` or log line).
"Unverified" = inferred, or seen once without a cross-check.

## Summary for building the native screen

- **No scene change.** The Pager belongs to the `FieldMenu` CRO (loaded at
  `0x7F1000` here); no CRO loads when it opens (Azahar log). `scene` stays
  `FieldRo` and the field state goes 0 → 4, the same as the X menu (the
  plugin's status line then says "x menu open"). Seen.
- **Finding it:** the scene manager stack (`*(0x6A599C)` = `0x34021048`, first
  eight words) has an entry whose vtable is FieldMenu `+0x18310` while the
  Pager is closed and FieldMenu **`+0x18420`** while it is open (same object,
  `0x31842758` here). That entry's `+0x14` then points to the **Pager list
  object, vtable FieldMenu `+0x1848C`** (`0x318427D8`, the entry's `+0x80`).
  Closed, `+0x14` is 0 and that memory holds a FieldMenu `+0x1837C` object
  (Rotom's). The stack entry with vtable `0x6486A8` (main) is also gone while
  the Pager is open (seen 3 times; what it is: unverified). Found by diffing
  two whole-heap dumps across one Down press: the only changed word near a
  FieldMenu object was `0x31842824` (0 → 1). Seen.
- **Taps:** one tap on a row picks it (no confirmation); the back button
  closes. Rects below. Seen.
- **The game already has a path that needs no bottom screen:** the four Ride
  Pokémon registered with the orange "+" are on the **D-pad in the field**
  (this save: Up Charizard Glide, Down Stoutland, Left Mudsdale, Right
  Tauros). Pressing the direction calls that Pokémon (Charizard opens the fly
  map); pressing the direction of the one you are riding gets off. Seen for
  all four directions. So a native Pager is needed only for the other three
  (Machamp, Lapras, Sharpedo here), for getting off a non-registered one, and
  for registering; and the D-pad must stay the game's in the field.

## Native implementation (2026-11-09; emulator verification pending)

`RidePagerScreen` runs through the existing `SubScreens` dispatch (`screens
on|off|status`, default on). No new setting or opening hotkey: Y remains the
game's own field key. Nothing captures the field D-pad when the Pager object
is absent. No save-memory writes are used.

- Recognition scans eight scene-stack entries for FieldMenu `+0x18420`,
  then validates its `+0x14` list pointer against FieldMenu `+0x1848C`.
  Field state 4 alone never activates this screen. Leaving FieldRo, closing
  the Pager, or losing either vtable ends recognition.
- The list's count, cursor, current ride and `+0x59` ID table drive the
  view. Counts outside 1-7, invalid cursors/current IDs, duplicate IDs and
  out-of-range IDs are rejected. The ID table remains an **unverified
  interpretation**; a valid reduced list is supported but needs a mid-game
  save to prove its meaning and the fixed 30 px geometry.
- Names use a **fixed English table keyed by ride ID**. The repo has item
  and move text readers, but no identified Ride Pager message file/line
  mapping or ride-label accessor. Those unrelated text IDs are not reused.
- Samples are `(290,30r+15)` for availability and `(44,30r+15)` for
  registration. Three identical decoded frames are required. Unknown colours,
  fades, changed row tables and a nonzero dialog byte `+0x64` invalidate
  readiness immediately. Light cyan rows are usable; grey rows say
  Unavailable. The current ride says Riding; orange `+` marks registration.
  Registration **directions** are not inferred from the unverified save slots.
- Up/Down press the game's own direction through the action queue; the
  memory cursor mirrors its wrap, including grey rows. Left/Right are
  captured and ignored inside the Pager. A (also Y) selects only a decoded
  usable row, including the current ride for getting down. Disabled choices
  send no action. B queues the verified Back tap `(14,224)`.
- Rows 0/3/6 queue their verified row tap. **Rows 1/2/4/5 use a game A press
  at the memory cursor**, because their tap points were only measured and
  the existing action queue refuses unverified taps. Layout metadata retains
  that distinction; the queue's verification policy is unchanged. Each
  selection/Back action holds for 6 frames, releases, then waits 60 frames;
  directions settle for 24 frames. No automatic retries or confirmation taps.
- No native registration controls: only opening the `+` dialog and cancelling
  it were observed; choosing a D-pad tile was not. X remains with the game
  for its top-screen help. A register dialog reached through the panel is
  left to the game; L+R exposes the bottom screen if needed.
- Rendering uses the existing water background, theme, shared screen buffer
  and `Layer::SetupHiRes`: 1x fallback or 2.5x staging. The panel is held off
  while recognised, and manual L+R yields until the Pager closes, as with
  other native screens. No change in main.cpp is needed: its existing
  StartSubScreens/SubScreensUpdate calls run this screen.

### Host checks and preview

From the repo root, `./tests/run_ride_pager.ps1` compiles directly with MSVC
C++17 and runs `tests/build/ride_pager_test.exe`, writing
`tests/build/ride-pager-preview.ppm`. It uses an ignored build-only header to
erase the existing GCC printf attribute for MSVC. The Makefile also includes
this test for environments that support make; make and WSL were not used here.

The test covers valid/invalid and reduced row mappings, all seven row centres,
colour stability and invalidation, registered markers, unavailable refusal,
current-ride selection, busy suppression, direction actions, and exact
selection/Back input hold/release sequences using the real ActionRunner.
Drawing is exercised at 1x and through the guarded 2.5x Layer backend.

Ran: direct MSVC Ride Pager, existing native-screens and action-queue host
builds/tests; ARM GCC C++17 syntax checks for RidePager.cpp
and SubScreens.cpp (`-D__3DS__`, real CTRPF/libctru headers); layout generator
and `python tools/gen_layouts.py --check`; `git diff --check`. MSVC reports
existing warnings in Raster.cpp, GameAssets.cpp, Options.cpp and the existing
action-queue test. The supplied rp-01 bottom screenshot was also checked at
all 14 probe coordinates and matched these classifiers. No full plugin link or emulator run was performed.

### Postgame emulator verification for Claude

1. Outdoors, on foot, with `screens on` and auto both off/on: Y opens the
   native list; verify row names, orange registrations and the two grey water
   rides against the bottom screen. Check `screens status`/ride-pager log.
2. Up/Down wraps and visits grey rows; A/Y there does nothing. Select Tauros
   and Machamp, reopen, then A/Y on the current ride gets down. Try registered
   Stoutland/Mudsdale through the Pager too (the measured-row A path).
3. Charizard opens TownMap; the native Pager vanishes, B cancels the fly map.
   Pager B closes while preserving a mount. After closing, all four field
   D-pad ride shortcuts must still work normally.
4. X help owns the top screen and returns to the Pager. L+R yields to the
   panel until close/reopen. Repeat with `screens off`, then restore on.
5. On water, verify Lapras/Sharpedo colours and selection. Later use a save
   with fewer rides to verify count/ID mapping. Check no save-file changes
   unless the player explicitly saves.

## 1. Opening

- `press y` in the field outdoors, on foot or riding, opens it. Bottom
  screen: the list. Top screen: the field with the game's message window
  "Which Ride Pokémon would you like to ride? / Press X to check what the
  Pokémon can do." (`rp-01-pager`). Seen.
- No other way found: the field's bottom screen (Rotom and the area map,
  `rp-00-field-bottom`) has no Pager button, and the X menu has no Pager
  entry (`docs/XMENU.md` ids 0-11). Whether any other key opens it:
  unverified.
- Indoors: not checked. The Pokémon Center door next to the start point
  could not be entered by stick-walking (the player stopped at the door, on
  Stoutland and on foot, `rp-30-enter3`, `rp-33-indoor`). The old w54 notes
  say Y did nothing in a Pokémon Center, but that save had no Pager.

## 2. Bottom-screen layout (320×240, measured from `rp-01-pager-bottom.png`)

Seven rows at a 30 px pitch, in a fixed order; no scrolling (the object's
count is 7 and all seven are visible). Mantine Surf is not in the list. Seen.

| Row | Ride Pokémon | Row pill (tap point) | "+" button centre |
|---|---|---|---|
| 0 | Tauros Charge | x 59-313, y 1-28 (180,15) | 44,15 |
| 1 | Stoutland Search | y 31-58 (180,45) | 44,45 |
| 2 | Mudsdale Gallop | y 61-88 (180,75) | 44,75 |
| 3 | Machamp Shove | y 91-118 (180,105) | 44,105 |
| 4 | Lapras Paddle | y 121-148 (180,135) | 44,135 |
| 5 | Sharpedo Jet | y 151-178 (180,165) | 44,165 |
| 6 | Charizard Glide | y 181-208 (180,195) | 44,195 |
| back | blue "↩ B" button | x 0-28, y 205-239 (14,224) | |

Row r spans y 30r+1 .. 30r+28 (edges from pixel scans at x 200 and x 44;
the "+" circle spans x 31-56). Taps made: (180,15), (180,105), (180,195),
(14,224), (44,105); all did what is described below (seen). Tap points for
rows 1, 2, 4, 5 come from the pitch and were not tapped (unverified).
Note: a tap at (17,105) or (18,104), left of the "+" circle, did nothing.

Pixel tests (three screenshots: on foot, riding, after the register dialog;
seen):
- Row usable here: (290, 30r+15) = `(209,253,255)`; a greyed row ("can't
  be called here") reads about `(105,155-178,207-225)`.
- Registered to the D-pad: (44, 30r+15) orange `(255,150,0)`; not
  registered grey `(210,210,210)`.
- Cursor: a red arrow at about x 66-74 on the cursor row (it animates; the
  memory cursor below is the better source).
- The message line at the bottom (y ~208-236) describes the cursor's row,
  e.g. "This Tauros's charge can break rocks.", "You can't call this Pokémon
  here." (greyed row), "Press Y or A to get down from this Pokémon" (the row
  of the one you ride).
- A "+NEW" mark flickers in some shots at about x 285-315, y 122-134 (on the
  Lapras row); not looked into.

Keys and taps inside the Pager (seen):
- Up/Down move the cursor over all seven rows, greyed ones included, and
  wrap (6 → 0 on Down, 0 → 6 on Up). One press was lost when presses were
  0.5 s apart; 1.2 s apart all registered.
- A, or a tap on the row, on a usable row: calls it. Tauros, Machamp: the
  Pager closes and the player mounts (~4 s, no question; `rp-17-tauros2`,
  `rp-20-machamp`). Charizard Glide: the fly map opens at once (scene
  `TownMap`, "Fly to Heahea City?", Akala shown, `rp-22-flymap`); B there
  returns to the field, still on the previous mount (`rp-23-flycancel`).
- A on a greyed row (Sharpedo): nothing happens, the Pager stays
  (`rp-08-a-disabled`).
- A or Y on the row of the Pokémon you ride: get off (A on Tauros, Y on
  Tauros, `rp-19-getoff`, `rp-39-ontauros`). Y on another row while riding
  acted like A (cursor on Charizard → fly map; one sample).
- X: the Ride Pokémon's help on the **top** screen (pictures and text;
  bottom screen black), scene `main:0064787C` (the same scene id Poké Pelago
  uses, `docs/OFFSETS.md`), stack `308E6474 308FAFAC`. A pages through the
  text; B did nothing there; after the last page it returns to the Pager
  (`rp-10-x`, `rp-11-xa`, `rp-13-xend`).
- B or the back button: closes the Pager; a mount stays.
- Tap on "+" (44, 30r+15): the register dialog over the list, "Register
  this Pokémon to the +Control Pad?", a cross of four tiles showing the
  current assignment (`rp-35-plus`). Tile centres from that screenshot (not
  tapped, unverified): Up ~160,69, Left ~109,119, Right ~208,119, Down
  ~160,168, each ~44×44 px. B cancels back to the list with nothing changed
  (`rp-36-plus-b`, seen). Choosing a tile was not tried.
- The cursor is remembered between openings (closed on row 5, reopened on
  row 5; closed on 6, reopened on 6 while riding Tauros). It does not jump to
  the ridden Pokémon. Seen.

At the start point (outdoors, land, town): Tauros, Stoutland, Mudsdale,
Machamp and Charizard usable; Lapras and Sharpedo greyed. Called this
session: Tauros (tap `move 180 15` + `tap 6`, and D-pad Right), Machamp
(`move 180 105` + `tap 6`), Mudsdale (D-pad Left), Stoutland (D-pad Down),
Charizard Glide (`move 180 195` + `tap 6`, and D-pad Up; fly map cancelled
with `press b` both times).

## 3. Readable state

**Pager list object** (vtable FieldMenu `+0x1848C`; offsets from the object):

| Offset | What | Evidence |
|---|---|---|
| `+0x0C` u8 (in a u32) | ride id you are on, `0xFF` on foot | FF on foot; 0 on Tauros; 3 on Machamp (seen) |
| `+0x1C` | → `Savedata::FieldMenu` (`0x33015408`, vtable `0x64F510`) | seen |
| `+0x20` | → Rotom's data (`0x33F81310`) | seen |
| `+0x4C` u32 | cursor row 0-6 | Down gave 1, 2, …, 6, 0; Up from 0 twice gave 5 (seen) |
| `+0x50` | → object with vtable `0x648EE0` (main) | not looked into |
| `+0x54` u32 | row count, 7 | seen (never seen with fewer rows) |
| `+0x59..+0x5F` u8[7] | `00 01 02 03 04 05 06`, presumably the ride id of each row | unverified (needs a save with fewer Ride Pokémon) |
| `+0x60` u8 | 3 after opening "+" on row 3 (`0xFF` before) | one sample |
| `+0x64` u8 | 1 while the register dialog is up, 0 otherwise | one sample each |

Ride ids follow the row order: 0 Tauros, 1 Stoutland, 2 Mudsdale, 3 Machamp,
4 Lapras, 5 Sharpedo, 6 Charizard (0 and 3 seen through `+0x0C`; the rest
inferred).

**Savedata::FieldMenu** (`docs/OFFSETS.md`, SaveData `+0x3AE0`): `+0x18`
u8[4] = `06 01 02 00`, read as **D-pad registrations Up, Down, Left,
Right** (Charizard, Stoutland, Mudsdale, Tauros: exactly what the D-pad
did). One save only, so the slot order is inferred from that match
(unverified until a re-registration is diffed). By the X-menu table
(`+0x0C` ↔ save `0x4608`), this would be save offset `0x4614` (unverified).

**Mounted, outside the Pager** (player model `0x33F81E20`, from `where`;
one dump or peek each, so unverified as a rule): `+0x8C0` (`0x33F826E0`)
s32 = -1 on foot; `0x96` Tauros, `0xBE` Stoutland, `0xD2` Mudsdale, `0xE6`
Machamp (peeked after each mount and after getting off). `+0x2C` and
`+0x170` read 1 on foot, 5 on Tauros, 0xE on Machamp (probably motion ids).
The Pager's `+0x0C` is the clean value but exists only while the Pager is
open.

**Unlocked Ride Pokémon:** not found. This save has all seven, so nothing
could be diffed. Candidates: event flags (SaveData `+0x15D8`, like the
Pokédex flags) or the row table at `+0x59`. A native screen can take the
rows from the object (`+0x54`) and the greyed state from pixels, which
needs no unlock flag.

## Open questions

- Unlock state with fewer Ride Pokémon (row count, the `+0x59` table): needs
  a mid-game save with the Pager.
- Indoors, on water, at Mantine Surf spots: what Y does there.
- Picking a register tile: does it write `Savedata::FieldMenu +0x18` at
  once, and which slot is which.
- Hotkey: Y is the game's own Pager key and works without the bottom screen
  (only the choice needs it), so the native screen can open on the game's Y
  (as the native X menu follows the game's X) and mirror the list; D-pad
  stays the game's shortcuts.

## History

Notes from 2026-11-02, before a save with the Pager existed (kept as written):

**Status then: blocked. The test save has no Ride Pager, so there is nothing to
build a native screen against yet. A later save from Tyler is needed** (one
with at least the Ride Pager and Tauros; more Ride Pokémon make a better test).

### What this save has (checked 2026-11-02, instance w54)

Plugin from `main` e336665, Ultra Sun USA v1.2, `include common/boot` (Pokémon
Center on Route 2), `xmenu off`, auto view off.

- **Bag, Key Items:** the `bag` command lists `Z-Power Ring(933) x1,
  Exp. Share(216) x1, Lens Case(705) x1 | count 0: Sparkling Stone(845)`. The
  Ride Pager is item **850** (`analysis/private/names-en/items.txt`, from the
  game's item names); it is not in the pocket, not even as a count-0 slot.
- **Y in the field:** scene `FieldRo` (field state 0, x menu closed) before and
  after. Screenshots 1 s and 4 s after `press y` show the same top and bottom
  screens as before: nothing opens, no message, no sound check (not possible
  here). Pressed twice: once while Rotom's chat was up on the bottom screen,
  once after it was answered (tap 210,135). Not checked outdoors: the save
  starts inside the Pokémon Center and the Pager was missing anyway.
- **Bottom screen in the field:** Rotom's face and the area map (player icon,
  flag marker). No Ride Pager button or icon.
- The real save was unchanged after every run (mtime 2026-02-08 16:52:50,
  sha256 `1b429d566255e593…`).

### How the Ride Pager is normally opened (from guides, not checked in game)

- Received after beating Kahuna Hala in Iki Town (Melemele grand trial),
  together with Tauros Charge.
- Opened with **Y** in the field.
- Ride Pokémon in Ultra Sun / Ultra Moon and where each is unlocked: Tauros
  (Hala, Iki Town), Stoutland (Mallow, Paniola Ranch), Lapras (Lana, Brooklet
  Hill), Charizard (Kiawe's trial, Wela Volcano Park), Sharpedo (Grimsley,
  Route 15), Mudsdale (Hapu, Route 12), Machamp (Hapu's grandmother, Ancient
  Poni Path); Mantine Surf works only at four fixed spots that link islands.

Sources: [Serebii, Poké Ride (USUM)](https://serebii.net/ultrasunultramoon/pokeride.shtml),
[Game Guides, Poké Ride](https://www.gamerguides.com/pokemon-ultra-sun-moon/introduction/journeying-in-alola/poke-ride).
Bulbapedia (Ride Pager page) refused the fetch (403).

None of this was seen in game: what the Pager shows (bottom screen or both),
where its buttons are, how "get off" works, whether it opens indoors, and what
scene it uses are all unknown.

### Plan once a save with the Pager exists

1. Boot it, press Y outdoors, record the scene (`scene`, `where`), screenshots
   of both screens, and the field state. Check indoors too.
2. Measure the bottom-screen buttons (one per Ride Pokémon, plus get off and
   back) into `layouts/usum-v1.2.json` with `tools/layout_measure.py`; every
   address found goes in `docs/OFFSETS.md`.
3. `Sources/Screens/RidePager.cpp` (+ header, pure part host-tested like
   `XMenuGrid`): buttons on the top screen, panel held off, choices carried out
   through the action layer with those taps; `scenarios/native-ride-pager.txt`.
4. Opening it: Y in the field is the game's own button for the Pager, and it
   doesn't clash with L+R (panel), X (native X menu) or ZR (native map, PR #27).
   Y inside the native X menu stays the game's Save. If Y turns out to need to
   stay with the game, pick another key then.
