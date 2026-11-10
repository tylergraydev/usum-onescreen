# Quick save: review and TEST-instance validation

Implementation is uncommitted. Host tests prove the pure rules/sequence, not
runtime responder discovery or disk I/O. Claude must build the plugin and use
an isolated TEST instance with a copy of a disposable save. Never run these
checks against the player's primary instance. No Computer Use was used.

## Why the action route

The game already exposes save through X then Y. Using those handlers preserves
its permission checks and the normal save preparation, checksums and write
lifecycle. Direct EventFieldReport/SaveStart calls would need additional
reverse engineering of thread context, parameters and eligibility, and could
skip restrictions. The new observer is read-only; presses use the existing
action layer and capture. It issues X, waits for X menu, Y, waits for the
specific unanswered ReportDraw confirmation, A once, waits for observed writing
then success, waits 90 frames for the report to close, and presses B only if
the X menu is still showing. There is no final wait for idle field. No retries.

Two Ultra Sun emulator runs changed the save file and logged ReportDraw states
0, 1, 3, 4, 5, 6, 7, 10, then -1 (report gone). The game closes the X menu
itself and returns straight to the field, so the old required X-menu wait
timed out after the successful write. On one run random Rotom chat ("I wazzz
wondering...") followed immediately, leaving the field non-idle. The revised
ending accepts both cases without sending B into Rotom chat. SkipIf is already
evaluated before Guard in the action runner.

`SaveRefusal` reuses the PC rules; native map, pending PC and repeated save
requests are refused too. The field is checked again before X; Y and A have
input guards on their first frame. Failed/cancelled actions release capture
using the existing held-until-release rules. Failure does not inject recovery
A/B into an unknown prompt; a remaining menu gets the panel for manual recovery.
A completion timeout after writing can mean the disk save succeeded even
though completion was not observed; it must not be retried automatically.
Once write success has been observed, the result is `save: saved` even if the
action fails, is cancelled or times out afterward.

## Host verification (2026-11-09)

For the revised ending, `quick_save_test.exe` and `action_queue_test.exe` were
compiled with the Makefile's recipes directly using native Windows Clang 22,
`-std=c++17 -Wall -Wextra -Werror -I ../Includes`, and both passed. The sequence
test covers direct field return, immediate Rotom chat and a remaining X menu,
as well as missing confirmation, changed confirmation and write timeout.
No WSL, make, plugin build or emulator run was used for this revision.

Previous baseline verification: 27 C++ test executables built and passed using Windows Clang 22 with C++17,
`-Wall -Wextra -Werror` (plus `_CRT_SECURE_NO_WARNINGS` for the Windows C runtime).
The focused quick-save test also passed without that Windows macro. The
Makefile's compile recipes were expanded and invoked directly: MSYS make could
not create its signal pipe, and WSL returned Access Denied in this sandbox.
The existing `screen_buffer_test` requires Unix `unistd.h`/`mallinfo` and was
not run. Optional real GARC tests were skipped when their local files were
absent. Layout generation verification and synthetic/real/shifted Ultra Sun
signature-scanner tests passed. No plugin build or emulator run was attempted.

## Emulator checklist (both Ultra Sun and Ultra Moon v1.2)

1. Back up a disposable TEST save; record the actual `main` save file's
   modified time and a known in-game value (money, item count or party order).
   Boot and change that value without manually saving. Record its new value.
2. In idle field, send `save`. Inspect action logs: exactly one X, one Y,
   one A confirmation, then observed write success and a 90-frame wait.
   Expect no B when the game returns to the field (including Rotom chat),
   or one B only if the X menu remains. Idle field is not required for success.
   Confirm the ReportDraw path/state bytes against the static offsets if the
   action times out. Do not weaken the observer to fixed A delays.
3. Check that only a small Saving.../Saved notice appears, without native
   X-menu/panel flicker. Verify walking, buttons, touch and ordinary X/Y saving
   work afterward; manual X/Y must still leave Yes to the player.
4. Verify `main` modified time changed. Exit normally, cold boot the TEST
   instance (no emulator save-state), Continue, and confirm the changed value
   persisted. An in-memory change or `save: saved` alone is insufficient proof.
5. Set `[controls] save_key = R+Y`, reload, change the value again and repeat
   steps 2?4. Hold the combo: one request only. Release Y while R remains held:
   R stays hidden until released; release all and press again to save anew.
6. Repeat with Select ? Quick save, closing the plugin menu afterward. Check
   repeat requests while saving are refused; cancellation before observed
   write success must not log Saved. After observed success, Saved is correct.
7. Try command and combo in battle, X menu, NPC dialogue/shop, encounter start,
   door/cutscene/script/event, loading/non-field, Festival Plaza, native map,
   another plugin action, and pending PC. Expect a native reason and refusal
   log, no confirmation, no save-file mtime change attributable to quick save.
8. Test a game state/map where saving is disabled: X/Y must not be bypassed,
   no A without the specific save confirmation, and no Saved claim. Recover
   manually if a menu remains. Check keys from a refused combo stay hidden.
9. On the TEST instance only, exercise an unavailable observer/timeout,
   `action cancel` before Yes and after writing begins, and a safe simulated
   write error if available. Expect failed/refused unless write success was
   already observed, in which case expect Saved. No repeated Yes; controls/panel
   are restored and there is no automatic retry.

Risks: the revised ending and Ultra Moon still need emulator verification;
unusual maps, localization, L=A, fast text/speed options,
real controllers and disk-error behavior require the above checks. First-run
failure must remain fail-closed. Hosts cannot prove a valid on-disk save.
