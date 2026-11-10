# Native Options, Trainer Passport and Battle Video (W5-3)

Three of the X menu's page-2 screens have a top-screen version. As with the
native X menu (`docs/XMENU.md`), the game's own screen still runs, on the
hidden bottom screen; the native screen mirrors it and drives it through the
action layer (`docs/ACTIONS.md`). Each opens from the native X menu, whose
footer now says which entries open on the top screen (`XMenuOpensNative`).

| Screen | Scene | Reads | Keys |
|---|---|---|---|
| Options | `Config` | bottom-screen pixels (cursor, values); the save's ConfigSave word to check them | D-pad / circle pad, A (B left to the game) |
| Trainer Passport | `TrainerPass` | memory (card values, page, stamp text); pixels (stamp earned) | Left/Right, L/R (A kept, B left to the game) |
| Battle Video | `BattleVideoPlayer` | bottom-screen pixels (intro / empty recorder) | A (B left to the game) |

## Files

- `Includes/Screens/NativeScreen.hpp`, `Sources/Screens/NativeScreen.cpp`: what
  the screens share: the interface the glue runs (`NativeScreen`), key bits,
  sample points and colour tests, the one-press action (`ScreenPressAction`),
  the shared drawing (water background, message window, button prompts).
- `Options.*`, `TrainerPassport.*`, `BattleVideo.*` (in `Includes/Screens/`
  and `Sources/Screens/`): each screen, pure C++ (no CTRPF). Host tests in
  `tests/native_screens_test.cpp`; `make -C tests` also writes previews to
  `analysis/private/native-*.ppm`.
- `Includes/Screens/SubScreens.hpp`, `Sources/Screens/SubScreens.cpp`: the
  plugin glue for all three (memory reads, the OSD callback, panel, input).

## How a screen runs (SubScreens.cpp)

- When the scene (`SceneModule()`, checked 4 times a second) is one of the
  three, that screen begins. Every frame the OSD callback samples the
  bottom-screen points the screen asks for (at most 160) and the plugin
  thread hands them over; the screen decodes them and logs what changed.
- It shows (and takes keys) once its samples decode (`Ready`). From the
  scene's start until it ends, the panel is held off with AutoPanel's manual
  override, as the X menu does. If the player turns the panel on by hand
  (L+R), the screen steps aside until the scene ends.
- Keys: the screen's keys are captured (`InputCaptureBegin`) while it shows
  and no action runs; each press (or repeat of a held direction: 15 frames,
  then every 5) becomes at most one action, a key press on the game's screen
  (`options-up`, `passport-next`, `video-open`, ...); a key pressed while
  one runs is handled after it (the last such key only). B is never captured: the
  game's B does what it always does, and the screen goes when the scene does.
- Drawing: one `UI::Layer` over the screen's area, redrawn only when its
  `DrawKey()` changes or the game font finishes loading (a screen opening
  calls `UI::RequestAssets()`); screen state is shared with the OSD callback under a
  mutex (the callback skips a frame rather than wait). Registered before the
  panel, so `tools/shot.py` includes it.
- Commands: `screens on|off|status` (default on).

## Look (W7-9)

Each screen draws the game's own bottom screen with `UI::Theme`
(`docs/VISUAL-STYLE.md`), in the game font once it has loaded:

- **Options**: the game's blue water background (`DrawWaterBackground`,
  row averages of the empty recorder), five menu-option pills at a 30 px
  pitch with the label on the left and the light value strip `#D6FFFF` on
  the right, ◀ ▶ only where the value can move (blue `#0077EE` on the
  cursor's row, grey `#9E9E9E` elsewhere), the red cursor on the row; the
  question in a `DrawWindow` with Yes / No pills at the right over the
  dimmed list. Prompts (B) Back and (A) Confirm (question: (B) No,
  (A) Choose).
- **Trainer Passport**: the page's beige paper `#F5F5E4` with its tan top
  band, the trainer's card (name, ID, money, play time) in a `DrawWindow`,
  the stamp's name on the peach plate `#FFD7BE`, its text on a white strip,
  "1/15" at the bottom right and the blue `#33AAFF` page arrows at the sides.
- **Battle Video**: water background, "Open the Vs. Recorder!" in a
  `DrawWindow`, the one option pill with the cursor; the empty recorder
  shows the game's own top-screen line, "There is no Battle Video."

What still differs from the game: no water texture (a flat gradient), no
stamp emblem on the passport (not loaded from the RomFS), the game's
orange-badge back button is drawn as a (B) prompt chip, and the Options
row text is the game's English text typed into `Options.cpp`, not read
from its message files.

## Options

The game's list: Text Speed (Slow, Normal, Fast), Battle Effects (On, Off),
Battle Style (Switch, Set), Button Mode (Default, L=A), Party/Box (Manual,
Automatic). Its rows wrap (up from Text Speed is Party/Box); its values don't,
and it draws ◀ / ▶ only where the value can still move. So the bottom screen
says everything:

| What | Where (bottom screen) | Colour |
|---|---|---|
| cursor | x 3-9, y 19 + 32·row | red (238,0,0) |
| ◀ | x 170, 172 | grey (118,118,118), blue (0,119,238) on the cursor's row |
| ▶ | x 306, 308 | same |
| no arrow | | the value strip, (205,253,255) / (191,249,255) |

Left only: last value; right only: first; both: Normal (Text Speed). A reading
counts after 3 equal frames; one that doesn't decode (the screen sliding)
keeps the last. On open the reading is compared with the save's ConfigSave
word (`docs/OFFSETS.md`) and logged (`options: screen matches the save word`).

- D-pad: pressed on the game's list; at a value's end nothing is pressed.
- A: Confirm (the game keeps the settings and goes back to X menu page 2).
- B (the game's): with nothing changed, back to the X menu. With changes the
  game asks "Would you like to make these changes?" (Yes / No, cursor on
  Yes; dark box at y 20, pills at x 285, y 107 / 135, red cursor at x
  205-213). The native screen shows the question; Up/Down move the game's
  cursor, A answers. Yes keeps the changes, No and B leave without them; all
  three go back to the X menu.
- When the scene ends, the save word is read again and logged
  (`options: closed, save word ...`), which is how the scenario checks what
  the game kept. That is the running game's memory only: nothing is written
  to the save file unless the player saves.

The game's own description of the selected row stays visible: the native
screen covers the top screen down to y 188, and the game's text box starts
at y 193.

## Trainer Passport

The game shows the trainer card on the top screen and a book of 15 stamp
pages on the bottom screen; ◀ ▶ or L / R turn the page (no wrap), A opens
Portrait Photo. The native screen covers the whole top screen and shows:

- the card's Name, ID No., Money and Play time (the game's labels, from its
  message file; values from MyStatus, Misc and the play time object). Not
  shown: Alola Pokédex count, Battle Points, Adventure started on (not
  located);
- the stamp page the game is on (`passport: page N/15`), read from memory
  every frame (the game remembers the page between visits);
- whether that page has a stamp: its emblem area (x 186-294, y 24-144, a 12 px
  grid) has dark strokes (at least 8 samples darker than 110) on an earned
  page and none on a blank one, and the paper (241,241,224) at y 20 says a
  page is up; if earned, the stamp's name and text from the message file. An
  unearned page shows "Not earned yet." (the game shows an empty page and
  doesn't name the stamp either).

Left/Right and L/R turn the page (pressing Left/Right on the game's book, one
turn at a time, 24 frames to settle); A is captured and ignored. Only page 1
is earned on this save, so "earned" was only seen on page 1.

## Battle Video

- Intro ("Open the Vs. Recorder!"): dark message box at y 20 (x 40, 280) and
  the light button at y 112 (x 40, 260). A presses A (the game's cursor is on
  its only button).
- Empty recorder ("There is no Battle Video." on the top screen): the bottom
  screen is only the blue water background; all of a 7 × 9 grid (x 40-280,
  y 30-190) is much bluer than red (measured: b − r ≥ 107, r ≤ 112, b ≥ 158).
- Anything else that holds for 150 frames (the fade between the two reads as
  neither for about 45 frames) is "other": most likely a list of videos,
  which this save doesn't have, so it was never seen. The screen then says
  so, captures nothing, and the player can use the panel (L+R).
- B (the game's) leaves either state for X menu page 2 in one press.

## Not verified

- A Battle Video list and playing a video (no videos on this save).
- Stamps other than the first; pages earned later in the game.
- The panel turned on by hand while one of these screens shows (the code path
  is the X menu's, which is tested; these screens' scenarios don't press
  L+R).
- Other languages and builds (all positions and colours are USA v1.2,
  English).

## Scenarios

- `scenarios/native-options.txt`: open from the native X menu with auto on
  (Config's automatic view is full; held off); values match the save word;
  moves, value ends, row wrap; B with no changes; a change, B, No (save word
  unchanged); a change, B, Yes (save word changed in memory); back with
  Confirm (save word as it started); `check-save` throughout.
- `scenarios/native-passport-video.txt`: the passport's card and pages 1-3
  and back, no turn left of page 1, A kept; B. Battle Video: intro, A, empty
  recorder, A ignored, B; B from the intro; `check-save`.

## Ride Pager (backlog #10)

The native Pager is also dispatched by SubScreens, but is recognised by its
FieldMenu scene-stack applet/list vtables, not by a separate scene: FieldRo
stays active. `RidePager.hpp` / `RidePager.cpp` own recognition, row memory,
colour decoding, guarded selection and drawing. It follows the game's Y;
field D-pad shortcuts remain with the game. Inside it, Up/Down navigate,
A/Y selects or gets down, B taps Back. Grey rows send no action; Riding and
orange + mark the current and registered rides. Registration controls are
omitted pending tile-selection research. Names currently use a fixed English
ride-ID table; the game's ride-label text mapping is not located.

The existing `screens` toggle, panel ownership, action lifecycle and
SetupHiRes drawing apply. The four measured-only row coordinates use the
game's A at its memory cursor rather than bypassing tap verification.
See [RIDE-PAGER.md](RIDE-PAGER.md#native-implementation-2026-11-09-emulator-verification-pending)
for evidence limits and emulator checks. `tests/run_ride_pager.ps1` directly
builds/runs the host test and writes `tests/build/ride-pager-preview.ppm`;
the test is also wired into tests/Makefile. Runtime verification is pending.
