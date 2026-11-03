# Native keyboard (W5-1)

When the game opens its keyboard on the bottom screen (the nickname after a catch: *Would you like to give X a nickname?* → Yes), the top screen shows the same keyboard and the name typed so far, and the player types with buttons. The panel stays off. Code: `Sources/Screens/KeyboardGrid.cpp` (pure: keys, cursor, presses, actions, drawing; host-tested by `tests/keyboard_test.cpp`) and `Sources/Screens/Keyboard.cpp` (plugin glue: finding the game's keyboard, input, the panel). Scenario: `scenarios/native-nickname.txt`.

## Keys

| Button | Does |
|---|---|
| D-pad / circle pad | move the cursor (held: repeats). Left/right wrap along the row; up/down go to the nearest key in the next row and wrap top ↔ bottom. The rows are staggered by half a key, so two keys are often equally near: down takes the right one, up the left one, so up then down comes back. |
| A | the key under the cursor |
| B | Delete |
| Y | Shift |
| X | Space |
| START | cursor to OK (then A confirms) |

A, B, X, Y, START, L, R, the D-pad and the circle pad are captured while the keyboard shows (L and R aren't used, but would reach the game's keyboard). Touch is left alone. L+R (the panel key) still works: turning the panel on by hand hides the native keyboard until the game's keyboard closes.

## What it shows

The game's QWERTY page, key for key, in the same staggered rows (scaled 13/10 across, 17/20 down), the name so far, and button prompts. Key labels follow the game's shift: with shift on, letters are capitals and the top row is `! ♂ ♀ + - * / = % ( )`; with it off, `! 1 2 … 0`.

### Look (W7-8)

Drawn to match the game's own name-entry keyboard (its bottom screen), colours measured from screenshots of it:

- A light grey board (`#F2F2F2`) under a sky-blue name band (`#B4E0FF` → `#DCF1FF`, grey line above, white strip below).
- The name one character per slot, over a dark slot line (`#6C6D6C`) per character the limit allows; the next slot is yellow (`#FFEB00`, dark edge) and the blue caret (`#0096FF`) stands before it, as in the game.
- Character keys: pale rim, `#B9F9FF` → `#50A8E6` top to bottom, blue base and dark edge, black labels. Delete, OK (a drawn ↵) and Space are green (`#B9F449` → `#6BA008`) with white labels; Shift is green with an outlined arrow while off and orange (`#FFA847` → `#EA4507`) with a filled arrow while on.
- The selected key has the game's yellow frame (a touch lighter, `#FFD22E`, than its `#FDC52E` so it shows on the top screen).
- Text uses the game's font once `UI::Assets` has loaded it (`♂ ♀ ’ “ ” é É` are all in its loaded part; ↵ and the shift arrow aren't, so those are drawn). Before that, the system font.
- `UI::Theme` button prompts along the bottom: A Type, B Delete, Y Shift, X Space, START OK. A note (*The name is full*) takes their place while it shows.

Still different from the game: no Pokémon icon and gender left of the name, no page arrow (the grid page isn't mirrored), no grey shading strips between key rows, and the keys are a little wider and flatter (the top screen is wider than the bottom one).

Pressing a key taps the same key on the hidden bottom screen (one action per key, `keyboard-<key>`: `StepTap("strinput.<key>")` then 4 frames). The game types it with its own rules, and the name shown is read back from the game each frame. So:

- Shift is one-shot and the keyboard starts with it on (the first letter is a capital). Delete also turns it off. The native keyboard just shows what the game says.
- The limit (12 for a nickname) is the game's: a character key with no room left is not sent; the footer says *The name is full* and the log `keyboard: q refused, full (12 of 12)`. Delete with nothing to delete is not sent either.
- Keys pressed faster than the taps run wait in a queue (up to 16); queued characters count towards the limit.
- For 30 frames after the keyboard appears (it slides in) keys only queue.
- If the player switched the game to its grid page by touch, the native keyboard taps the page arrow back to QWERTY before typing.

OK confirms; the game closes its keyboard and goes on (*Bobo has been sent to a Box!*).

## Finding the game's keyboard

The keyboard is the `StrInput` CRO. It does not change the scene: after a catch the scene stays `Zukan` throughout (StrInput is loaded along with Zukan, before the question is even asked). Runtime research, 2026-11-02, instance `w51`:

- Typed `Qwez` and searched a 64 MB heap dump (`dump 30000000 4184000`) for it in UTF-16: one hit, inside an object with vtable StrInput+`0xBFC0` (the **text object**).
- Pointers to it, walked back: scene manager stack (`*(0x6A599C) + 0..0x1C`) entries 4 and 5 (vtables Zukan+`0x35114` / +`0x34E34`) → `+0xB0` a Zukan object (Zukan+`0x351F4`) → `+0xB4` the **keyboard object** (StrInput+`0xBFE4`) → `+0x1C` the text object. No global points to either.
- At the Yes/No question the Zukan object's `+0xB4` is 0; it is set the moment Yes is pressed. After OK the Zukan object is freed and its memory reused, and the stack entries change.

Text object fields (verified by typing, Delete, Shift and the page arrow, each followed by a `peek`):

| Offset | Field |
|---|---|
| `+0x30` | u16[16] text, 0-terminated (UTF-16; ♂ and ♀ are the game's own `U+E08E` / `U+E08F`) |
| `+0x54` | page: 1 QWERTY, 0 the alphabetical grid |
| `+0x5C` | shift: 1 on |
| `+0x60` | length |
| `+0x64` | limit (12) |
| `+0x90` | 1 while the keyboard is up, 0 after OK |

(`+0x20`..`+0x2C` hold lengths that don't follow typing; unused.)

`Keyboard.cpp` finds the StrInput module from the current scene's module along the loaded-module links (`+0x88` next, `+0x8C` previous; the head's previous is the tail), then scans two levels of pointers (the first `0x100` bytes of each stack entry, then of each heap object found there) for an object whose vtable is StrInput+`0xBFE4`. The path (stack slot, two offsets) is kept and re-checked every frame: both vtables, `+0x90` = 1, limit 1-16, length ≤ limit. A scan runs every 15 frames while nothing is found (heap reads are direct, bounded by the linear heap's block from `svcQueryMemory`). The keyboard counts as gone after 4 frames without a valid path.

Only the nickname after a catch was reached. The scan doesn't assume the Zukan path, so other places that open StrInput (a Box name, the Name Rater, the player's name) may work, but none was tried: they need a permanent change to this save or aren't reachable on it.

## Key sweep

Every key was tapped on the QWERTY page with shift off and on, and the typed character read back from the text object (`peek`); the table in `KeyboardGrid.cpp` is that result. Taps 8+ frames apart were all typed. The 13th character of a nickname is ignored by the game. Rects: 18 × 29 px keys at a 20 px pitch, rows at y 60 / 95 / 130 / 165 starting at x 21 / 31 / 41 / 51; Delete 240-289 × 60-88, OK 230-289 × 95-123, Space 20-218 × 200-228, Shift 221-289 × 200-228 (`layouts/usum-v1.2.json`, screen `strinput`). The grid page (`strinput_grid`: CAPS / lower / Sym. tabs, Delete, OK) is not mirrored; only its arrow back (304,140) is recorded.

## Commands and log

    keyboard on|off|status|read

`read` scans for the game's keyboard right now and logs its text, page, shift and path. Log lines: `keyboard: game keyboard found (stack 4 +B0 +B4), 0 of 12: ""`, `keyboard: shown`, `keyboard: cursor b`, `keyboard: key b`, `keyboard: text "Bo" (2 of 12)`, `keyboard: shift off`, `keyboard: q refused, full (12 of 12)`, `keyboard: game keyboard gone, last text "Bobo"`, `keyboard: hidden`, plus the actions' own `action: keyboard-b ok in 12 frames`.

## Other bottom-screen choices outside battle (candidates, not built)

Met in this work:

- **Nickname question** after a catch (`Zukan`): *Would you like to give X a nickname?* Yes / No on the bottom screen, the game's cursor on Yes; the question is on the top screen. A = Yes, B = No; buttons work, so only a top-screen hint is needed. A new species shows its Pokédex entry first.

Reported by other sessions (from their notes, not re-checked here):

- **Save** (X menu → Y): *Would you like to save your adventure so far?* Yes / No, cursor on Yes (`FieldRo`, field state 4).
- **Festival Plaza** back button → *quit?* Yes / No.
- **Rotom chat** in the field: two answers at (210,135) / (210,160) on Rotom's screen, only when Rotom starts a chat.
- **Pokédex evaluation** (Pokédex top → Evaluation): Rotom asks which Pokédex to evaluate.
- In battle (not this item's scope): *Forget an old move?*, *Use next Pokémon / Run*.

## Limits

- The text the player sees is the game's buffer, so a key the game ignores (the 13th character) shows no change rather than a wrong name.
- The native battle menu (PR #24) draws a helper over the post-catch question in `Zukan`; once both are merged, check that its helper hides while the keyboard is up.
- Only the QWERTY page is mirrored: its keys hold every letter, digit and the symbols ! ♂ ♀ + - * / = % ( ) ’ : é É , . ? “ ” and space. The grid page's Sym. tab may have more; not looked at.
