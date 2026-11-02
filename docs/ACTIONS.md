# Action layer (UI3)

Phase 2 top-screen menus carry out the player's choice by driving the hidden bottom screen. The action layer does that: an action is a short list of steps (tap a named button, press keys, wait, wait for a scene or screen state) run once per frame, with timeouts, reporting success or failure to `usum-onescreen.log`.

- `Includes/Layouts.hpp`: the buttons of `layouts/usum-v1.2.json` as a C++ table (`"screen.button"`, rect, tap point, how it was verified). **Generated** by `py -3 -I tools/gen_layouts.py` and committed; `make -C tests` runs `gen_layouts.py --check`, which fails if the header doesn't match the JSON. Compiled in rather than parsed at runtime: the plugin has no JSON parser, reading the SD card at boot can fail, and the layouts only change when someone re-measures them (then rerun the script).
- `Includes/ActionQueue.hpp`, `Sources/ActionQueue.cpp`: the pure part (no CTRPF): steps, the runner, the built-in actions. Host tests in `tests/action_queue_test.cpp` run every action against a small fake of the game's screens.
- `Includes/Actions.hpp`, `Sources/Actions.cpp`: the plugin glue. Each frame it reads the world (scene every 5 frames, X-menu pixel probe every frame, battle command menu), steps the runner and applies its output.

## Input

While an action runs, all of the player's keys, the circle pad and touch are captured (`InputCaptureBegin`), test input from `press`/`tap` included. The action's own keys and touch go in **after** capture (`ButtonsActionSet` in `Buttons.cpp`, `CursorActionTouch` in `Cursor.cpp`), with their own press/release edges, so AutoPanel still sees an action's X and B. When the action ends, the capture request from before it is restored; a key the player still holds stays hidden until released (InputCapture's rule), so nothing leaks into the game.

Only buttons verified by a tap (`verified: tap` in the JSON) can be tapped: `CheckAction` refuses an action that names a `measured` button such as Save or an item's Use, and the host test checks every built-in action.

## Commands

`action NAME` starts an action (refused, with the reason, if one is running or the game isn't where it must start); `action status` (running step, last result, what it sees); `action cancel`; `action list`. Log lines: `action: NAME started`, `action: sees scene … (frame N)` on every change while running, retries, `action: NAME ok in N frames, scene S` or `action: NAME FAILED at step i/n (step): reason; scene S after N frames`.

| Action | Starts in | Does | Ends in |
|---|---|---|---|
| `open-pokedex` | field, X menu closed | taps Rotom's face (twice if needed); if that doesn't open it, X menu → Pokédex | `Zukan` |
| `open-party` | field | X (only if the X menu isn't up), taps Pokémon | `main:00647FCC` |
| `open-bag` | field | X (only if the X menu isn't up), taps Bag | `main:006488C8` |
| `battle-run` | `Battle` | waits for the command menu (+30 frames), taps Run, again if the menu stays | `FieldRo` |
| `close` | anything but `Battle` | back button of the Pokédex / Party / Bag / Town Map, B in the X menu, until the field shows with the X menu closed | `FieldRo` |

The X menu remembers its cursor, so entries are always tapped, never chosen with the D-pad.

Screens add their own actions with `ActionStart` (the native X menu's `xmenu-*`, the PC boxes' `box-pick-N`, `box-drop-N`, `box-prev`, `box-next`, `docs/BOXES.md`). In the `Box` scene the world also carries the Box screen's mode, read from memory every frame (`BoxWorldFlags`, `docs/RE6-boxes.md`): `WorldBoxIdle`, `WorldBoxModal` (its slot menu or a message), `WorldBoxCarrying` (a Pokémon held).

## What the runs showed (2026-11-02, instance `ui3`)

- **X menu probe.** The X menu is an overlay in `FieldRo`; three bottom-screen pixels tell it apart from the field: (10,120) yellow background, (300,225) the Save bar, (260,230) its blue Y corner (±40 per channel). In the field they are Rotom's red frame. After Party or Bag's back, `FieldRo` returns about 20 frames before the X menu shows, so `close` waits until the field has looked the same for 60 frames before it calls it done.
- **Rotom's face** often ignores the first tap after boot (the second works, ~30 frames to `(none)`), and while Rotom chats ("Which do you prefer…?", two answers) it ignores the face entirely. B doesn't end the chat and the X menu opens over it; answering would change the game, so `open-pokedex` falls back to the X menu. Its Pokédex returns to the X menu, which `close` handles.
- **Pokédex back**: the scene stays `Zukan` for more than a second after the tap, so `close` taps again only once the screen changed or after 120 frames (it tapped three times before this).
- **X menu back by touch** isn't seen by AutoPanel (it follows X and B), which left the panel up; `close` presses B there instead.
- **Run**: a tap the moment the command menu appeared didn't run (the battle log then showed the command cursor on Run, `cursor 3 of 4`); the retry did. `battle-run` now waits 30 frames first, and still retries if the menu is up 2 s after the tap.

`scenarios/actions.txt` runs all five and checks scene and panel mode after each, holds B through `open-bag` (the Bag stays open: the game never saw it), checks that `battle-run` is refused in the field, and finishes with a wild battle and `battle-run`.

## Adding an action

Add it to `MakeActions()` in `Sources/ActionQueue.cpp` with the step helpers (`StepTap`, `StepPress`, `StepWait`, `StepWaitFor(...).Retry(from, n).Optional()`, `StepSucceedIf`, `.SkipIf(cond)`), using buttons from `Layouts.hpp`; add a host test with the fake game and a scenario step. A screen state that the scene doesn't show needs a World flag and a probe in `Actions.cpp`.
