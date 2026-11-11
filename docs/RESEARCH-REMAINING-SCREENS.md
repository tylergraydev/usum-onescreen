# Research: the remaining bottom-screen screens (backlog #11)

Poké Pelago, Festival Plaza and the QR Scanner, looked at in Azahar on
2026-11-09 for a decision on native top-screen versions. The Ride Pager is
handled separately (`docs/RIDE-PAGER.md`). No plugin code was changed.

- Game: Ultra Sun USA v1.2. Plugin: a recent `main` build (from
  `H:/SunnyMoon-ui-motion`), `xmenu off`, auto view off (so every screenshot
  shows the game's own screens).
- Instances: `lrtoggle` (mid-game save on a route, zone 7, in tall grass)
  for Festival Plaza and the QR Scanner. **That save has no Poké Pelago**: its
  X menu page 2 shows Pokémon Refresh, QR Scanner, Battle Video, Options and two
  empty slots, and the FieldMenu unlock word (`+0x30` = `0x47BB8EC7`) has
  bit 22 (Pelago) and bit 18 (Photo Album) clear, although the layout table puts
  Pelago at page 2 slot 6. Pelago was therefore looked at in a new private
  instance, `research`, restored from the `postgame` start point (completed
  Ultra Sun save, every isle fully developed).
- Nothing was saved in game. Both saves were checked afterwards: `lrtoggle`
  `20ceb89a…` (mtime 2026-11-09 21:04, unchanged), `research` `ad8f840d…`
  (the start point's hash, unchanged).
- **Seen** = observed on screen or in the plugin / Azahar log in this session.
  **Unverified** = inferred or from memory, not checked.
- Coordinates are bottom-screen pixels (320×240). "Measured" rects come from
  `tools/layout_measure.py` (flood fill; gradient pills give only their flat
  inner band); "approx." rects are read off the screenshot. Every tap listed
  was tapped and did what is written unless it says otherwise.
- Screenshots: `analysis/private/research/remaining-screens/` (not in git),
  `NAME-top.png` / `NAME-bottom.png`; names are given below.

## Summary

| Screen | Scene / CRO | Leave | Recommendation | Effort |
|---|---|---|---|---|
| Poké Pelago | `main:0064787C`, CRO `Resort` | back 16,223 → Yes 160,92 → A | Small native **isle bar**: isle switch, Mohn, Leave (B). Keep the isles' touch play in full mode. | small (≈ Battle Video screen) |
| Festival Plaza | `FieldRo` (field state 4), CRO `JoinFestaSubScreen` (+ `JoinFesta`, `FieldEffectJoinFesta`, `FieldGimmickJoinFesta`, `KisekaeData`) | back 16,223 → Y (or tap the pill 160,118) | Small native **hub menu** over the walkable plaza; leave its sub-scenes (lists, profile, link menus) to full mode. Low priority: most of it is online. | small-medium |
| QR Scanner | `QRReader` | not reached (Azahar hangs) | No native version; keep it greyed out until tried on hardware. | none now |

## Poké Pelago

### Opening, scene, leaving (seen)

- X menu → Poké Pelago (on this save page 2, slot 1: tap 88,48). No dialogue on
  this visit (the first visit has Mohn's tutorial, `docs/XMENU.md`). About 8 s
  of loading.
- Azahar log: `FieldRo` unloaded, `SequencePlayer` / `FieldDemo` / `Object`
  loaded and unloaded (the boat cut-scene), then **CRO `Resort` at
  `0x006DF000`-`0x00717000`**.
- Scene: `main:0064787C`. The scene manager (`*0x6A599C` = `0x34021048`) holds
  at `+0` and `+4` two objects with vtable `0x0064787C`, then objects with
  `Resort` vtables `0x709EDC`, `0x70A73C`, `0x70A3B4`, `0x709D38`, `0x70A4D4`
  (Resort `+0x2AEDC`, `+0x2B73C`, `+0x2B3B4`, `+0x2AD38`, `+0x2B4D4`) and one
  more main-image object `0x0064886C`. `0x0064787C` is a main-image class, so
  it may be a generic app wrapper used by other apps too (unverified); a
  detector should also check that the `Resort` CRO is loaded.
- Field state reads 4 ("x menu open") the whole time.
- **B does nothing** on the isle view (`rp-b`).
- **Leave**: back button 16,223 → Mohn on the top screen "Do you want to leave
  Poké Pelago?" with Yes / No pills on the bottom (`rp-leave`) → Yes 160,92 →
  "OK! Then come back soon!" (top, needs **A**, `rp-yes`) → about 10 s later
  `FieldRo` with the X menu open where it was (`rp-out`). No save prompt. No at
  160,152 returns to the isle (`rp-no`).

### Bottom screen (seen)

Isle view (`rp-p1`, `rp-isle2`..`rp-isle5`): a full-screen picture of the
current isle with the player's Pokémon wandering on it. Fixed parts:

| Element | Rect | Tap | Does |
|---|---|---|---|
| ◀ previous isle | glyph 11,113-21,122 (measured), arrow approx. 6,100-26,140 | 16,120 | previous isle (wraps) — tapped ▶ only; ◀ unverified |
| ▶ next isle | glyph 298,113-308,122 (measured), approx. 294,100-314,140 | 303,120 | next isle, wraps after 5 (seen: Abeens → Aplenny → Aphun → Evelup → Avue → Abeens) |
| Back | 0,214-33,239 (measured) | 16,223 | leave prompt (above) |
| Mohn / Raft Hut | 288,210-319,238 (measured) | 300,222 | Mohn's menu |
| Bean counters | approx. 205,5-319,35 | not tapped | display: Poké Beans, Patterned, Rainbow (1785 / 1785 / 255 here) |
| Isle objects (Pokémon, signs, bean stump / box, cart) | vary per isle | Aplenny "Field 1" sign 112,100 tapped | the sign: Mohn says the field is full (`rp-field1`). Others not tapped (they spend beans or move Pokémon). |

Mohn's menu (`rp-p2`, top: Raft Hut with Mohn and the counts of Poké Balls /
beans): pills approx. 16,64-303,90 **Develop Isles** (tap 160,78; on this
save Mohn says every isle is complete, A × 7 back to the menu), 16,104-303,130
**Exchange Poké Beans** (160,118, not tapped), 16,144-303,170 **Send out a Bean
Bottle** (160,158, not tapped: sends to other players), back 16,223.

Leave prompt (`rp-leave`): Yes approx. 16,78-303,104 (measured band
25,83-294,95), tap 160,92; No approx. 16,138-303,164 (band 25,141-294,154), tap
160,152.

### Top screen meanwhile (seen)

Each isle shows its own status board, already useful on its own:

- **Isle Abeens** (`rp-p1`): wild Pokémon visiting (Jynx), bean effect time.
- **Isle Aplenny** (`rp-isle2`): the berries in Fields 1-3, harvest time, bean
  effect time.
- **Isle Aphun** (`rp-isle3`): exploration path, explorers' return time.
- **Isle Evelup** (`rp-isle4`): training Groups 1-3 with each Pokémon's icon,
  level and gender; playtime left.
- **Isle Avue** (`rp-isle5`): hot-spring Groups 1-3, same layout.
- Mohn's menu and every prompt: the Raft Hut with Mohn's message box.

### Readable state

The isle on screen and the counts are not located. The bean counters read
1785 / 1785 / 255, i.e. seven 255s, seven 255s and one 255: the beans are
probably 15 one-byte counts (unverified). A search of the 512 KiB SaveData
dump for a 15 × `FF` run found only long `FF` fill areas, so the beans need a
before/after diff (spend one bean) to be pinned down. PKHeX's Gen 7 resort
block would be the lead (unverified). Not needed for the small native piece
below, which only has to know that Pelago is up (scene + `Resort` CRO) and can
read the isle from the top-screen title board by pixels if wanted.

### Recommendation

Pelago is five touch-driven playgrounds (tap Pokémon, signs, bean boxes); a
native copy of each isle is a large job for little gain, and the top screen
already shows the useful status. Smallest useful native piece: **a Pelago bar**
on the top screen, over the bottom edge of the game's status board or as a
thin strip, with:

- ◀ isle name ▶ on Left/Right or L/R (taps 16,120 / 303,120);
- Mohn (Y? or A on a "Mohn" chip → tap 300,222; his pill menu is three rows,
  easy to mirror with D-pad + A later);
- **B = Leave**: tap 16,223, tap Yes 160,92, press A, done at `FieldRo`. This
  also fixes the current gap (B does nothing; `action close` has no back
  button for Pelago).

Everything else (tapping things on the isles) stays in the full panel view,
which shows the isle picture as the game draws it. Effort: small, about the
size of the Battle Video native screen: a detector (scene + CRO), three
one-press actions and a leave action with a top-screen-message check, a few
pixel probes (isle view vs. menu vs. prompt), host tests and one scenario on
the postgame start point.

### Native isle bar implementation (backlog #11)

Implemented in `Screens/Pelago.*`, dispatched by `SubScreens.cpp`: a 392 x 24
strip at top-screen (4,216), preserving everything above y=216. L/R tap the
isle arrows, Start taps Mohn, and B starts `PelagoLeaveAction`. A, D-pad,
circle pad and touch remain game controls while idle. No held-key repeats or
queued inputs for Pelago. L+R retains the existing panel fallback (native
screen yields until the scene ends); isle objects remain bottom/full-screen
play. The bar hides and releases its keys immediately on an unrecognised
bottom view, including Mohn's menus and prompts.

Detection requires `main:0064787C` AND a scene-stack responder owned by the
loaded `Resort` CRO, resolved by its name rather than a fixed load address.
`action close` uses the same leave prefix even with `screens off`, then the
existing BackOut step closes the returned X menu. B leaves the X menu open,
as in the observed touch exit. Close is deliberately refused from Mohn's
menus or an already-open question: return to the isle first.

Leave: guarded Back once -> wait up to 240 frames for stable Yes/No and a
top message -> 20 frames -> guarded Yes **once** -> wait up to 240 frames
for dimmed isle arrows and a top message -> 45 frames ->
guarded A **once** -> wait up to 1200 frames for FieldRo. No input retry loops.
Scene loss, changed guards or a timeout fail the action. Cancellation uses
the existing action runner. A failed exit can be finished with the game's
bottom-screen controls; the plugin never repeats Yes automatically.

**Capture calibration (follow-up):** Azahar built successfully and opened
Pelago, but the initial bar never appeared. The provisional white-arrow /
dark-window tests were wrong. Read-only 1x captures in
`H:/SunnyMoon/analysis/private/shots/` (`pl-isle`, `pl-afterno`, `pl-ask`,
plus `pl-xmenu2` as a negative control) provided the first calibration.
A second retest showed the bar on Abeens and R working, but it vanished on
the next isle: the assumed outer plates were variable isle scenery. All five
`pl3-isle1..5-bottom.png` captures and `pl2-left-bottom.png` are now sampled
read-only, and the detector uses the arrow glyphs alone:

- Isle: mirrored interiors (16,117)/(303,117) match (136,204,255), lower bands
  (16,120)/(303,120) match (170,221,255), and upper edges
  (20,113)/(299,113) match (102,187,255), each within 24 per RGB channel.
  All six points are identical across the six new captures. No outer plate
  or scenery colour is sampled or required for Isle. The right points are
  verified image mirrors (x -> 319-x) of the left points.
- Ask: four Yes/No pill points match (220,255,255) within 20, while all six
  arrow glyph positions and both y=170 points match beige (247,231,198)
  within 20. This also excludes a third cyan menu pill and cannot match Isle.
  The shared action observer buffer grows to the new 12-point sample count;
  both native detection and `action close` continue using the same probes.
- Message: (48,174)/(140,174) on Mohn's blue nameplate match (5,78,254),
  and (40,210)/(360,210) on the message edges match
  (207,249,240)/(196,255,255), each within 24. These points avoid lettering
  and stay above the native bar at y=216. Both captured isle top screens,
  the X menu, black fades and white screens fail this guard.
- Goodbye after Yes: `pl4-afteryes-bottom.png` shows a **dimmed isle**, not
  beige. Interiors (16,117)/(303,117) match (35,52,65), lower bands
  (16,120)/(303,120) match (43,56,65), upper edges (20,113)/(299,113)
  match (26,48,65), each within 20 per channel. All six are required and
  bright Isle/Ask must be absent. Black fades and an empty beige background
  cannot authorize A. `pl4-afteryes-top.png` matches all four existing Mohn
  message points exactly. The goodbye condition combines both surfaces.
  The previous beige-clear assumption caused the observed step-5 timeout
  after Yes and has been removed. Hand-pressing A on this captured state
  returned to FieldRo, state 4 (X menu open), as reported by the emulator test.
- Ask stability: the logs alternated between Ask+message and message alone.
  `PelagoBottomProbe` now accepts three matching Ask observations separated
  by short unknown gaps, then holds Ask for up to six missing bottom samples
  after its last match. A recognised isle or dimmed goodbye clears Ask
  immediately; a longer unknown gap expires it. Isle/goodbye and the top
  message still need three consecutive matches. Scene/CRO loss resets the
  observer. The WaitFor/20-frame settle/guarded Yes uses this shared observer,
  so brief misses no longer restart detection or invalidate the Yes guard.
  No action retries Yes or A; the final wait remains **FieldRo**, not the
  Pelago wrapper scene.

`tests/pelago_samples.hpp` preserves actual RGB samples extracted from the
captures, without copying or modifying the private screenshots. Host tests
check the two original isle captures, all five new isles, the left-arrow
capture, Ask, and the X menu negative control. Every new isle must decode as
Isle, show the native screen after three readings, and offer the B leave
action. Per-glyph rejection and tolerance tests require all six arrow points;
changing unrelated isle content cannot hide the bar. Existing action
scenarios and top-message checks remain in place.
The shared bottom-filter tests cover alternating Ask/missing samples,
expiry, and immediate invalidation by known views. Action tests now drive
that same filter from captured Ask and dimmed-goodbye samples, including
flickering Ask for both B leave and `action close`, with exactly one Yes and
one A on successful exits. The final scene must become FieldRo. The guarded sequence, scene/CRO detection and input mappings
are unchanged. Rerun in Azahar to confirm the bar and full leave flow;
do not replace a failing guard with timed Yes taps. The previous-arrow
layout uses the task coordinate; original research only exercised right.

Host verification: `powershell -NoProfile -ExecutionPolicy Bypass -File
tests/run_pelago.ps1` directly compiles and runs Pelago, native-screen and
action-queue tests with MSVC (no make/WSL), and checks regenerated layouts.
`tests/build/pelago-1x.ppm` and `pelago-2.5x.ppm` show the bar, with untouched
pixels above it; the real hi-res Layer path is also exercised. The test-only
MSVC compatibility header suppresses GCC's printf attribute, with no change
to production code. `tests/Makefile` also includes the Pelago target.

Emulator handoff: postgame save -> Pelago -> confirm status boards remain
visible on all five isles; L and R wrap in both directions; Start opens
Mohn; his menu/dialogue keys and isle touches work in full mode. Return to
the isle and test B, then `action close` with native screens both on and off.
Check one Back/Yes/A sequence, FieldRo return (~10 s), correct X-menu
behavior, a cancelled/timed-out action, re-entry, and first-visit tutorial
fallback. Confirm the strip does not overlap useful bottom status lines.

## Festival Plaza

### Opening, scene, leaving (seen)

- X menu → Festival Plaza (page 1 slot 5 on `lrtoggle`, 88,168). Took two
  tries once: a tap right after the X menu appeared did nothing.
- The scene stays `FieldRo` (a new FieldRo scene object, vtable `0x7B4F44`);
  field state 4. Azahar log: `FieldEffectCommon` and **`FieldMenu` unloaded**,
  then **`JoinFestaSubScreen` at `0x7F1000`-`0x805000`**, `FieldEffectJoinFesta`
  `0x809000`, `FieldGimmickJoinFesta` `0x812000`, `KisekaeData` `0x818000`,
  **`JoinFesta` `0x837000`-`0x859000`**. The scene manager lists, after the
  FieldRo scene, objects with JoinFestaSubScreen vtables `0x7FE078`, `0x7FE0D8`,
  `0x7FE22C`, `0x7FE358`, `0x7FE2CC`, `0x7FE1A4`, `0x7FE3E4` (CRO
  `+0xD078` … `+0xD3E4`). Detector: FieldRo + `JoinFestaSubScreen` loaded.
- **The player walks the plaza on the top screen while the hub is up**:
  `stick 0 100 40` moved the player (zone 318, z 5600 → 5150) with field
  state 4 throughout (`rs-fp-walk`, `where`).
- **Leave**: back 16,223 → "Would you like to quit playing in Festival Plaza?"
  with one pill "Press the Y Button to Quit" (`rs-fp-quit`) → **Y**, or a tap
  on the pill 160,118 (both seen) → back on the route, field state 0, no save
  prompt. B on the hub was not tried.

### Bottom screen (seen)

Hub (`rs-fp1`): banner "Connected to local wireless communication", two big
circles and a row of icons.

| Element | Rect | Tap | Does |
|---|---|---|---|
| Battle | approx. 22,58-152,188 (red core measured 26,62-149,185) | 80,120 | "What would you like to do?" Link Battle / Visit the Battle Spot / Download Rules (`rs-fp-battle`) |
| Trade | approx. 160,58-290,188 | 230,120 | Link Trade / GTS / Wonder Trade (`rs-fp-trade`) |
| ≡ Player lists | approx. 86,196-120,230 | 103,213 | scene **`JoinFestaPlayerListShow`** (CRO at `0x6DF000`, scene vtable `0x6E5B9C`; FieldRo unloaded) |
| Profile | approx. 128,196-162,230 | 145,213 | scene **`JoinFestaProfile`** (CRO `0x6F2000`, vtable `0x6F9D54`) |
| Poké Ball | approx. 170,196-204,230 | 187,213 | Party (`main:00647FCC`): the plugin's **native Party** shows (`rs-fp-ball` not copied) |
| Bag | approx. 212,196-246,230 | 229,213 | Bag (`main:006488C8`): the **native Bag** shows |
| Wireless | approx. 276,200-314,230 | not tapped | switches to Internet (would go online) |
| Back | 0,210-33,239 | 16,223 | quit prompt |

Battle / Trade menus: three pills, approx. 20,66-300,90 / 106-130 / 146-170
(taps 160,78 / 118 / 158; flat bands measured 24,72-295,84 etc.), back
16,223 returns to the hub. Link Battle (160,78) opens "Which battle would you
like to do?" with four pills at a 32 px pitch (taps 160,72 / 104 / 136 / 168:
Single, Double, Multi, Battle Royal, `rs-fp-link`); none chosen. Battle Spot,
Download Rules, GTS, Wonder Trade go online and were not tapped; Link Trade was
not tapped.

Player lists (`rs-fp-list`): the first visit shows a 6-page tutorial (Right
pages it, A does nothing, **B** closes it). Then "Guest List" with ◀ 8,12 / ▶
311,12 (▶ seen: VIP List; the third is the block list), back 16,223 (back to
the hub, `FieldRo` reloads, ~5 s), X refresh approx. 48,212-80,239, info
approx. 296,214-316,236. Empty on this save.

Profile (`rs-fp-prof2`): first-visit tutorial (2 pages, B closes); then
"Tyler's Profile", a scrolling list of greeting messages (When You Meet
Someone, …) with the red cursor, back 16,223, info bottom right. **The top
screen shows the plaza's records**: Current Rank 2, FC Collected 6, FC to the
Next Rank 10, Current FC 6, Guests Interacted With, Facilities Hosted, Missions
Participated In.

### Top screen meanwhile (seen)

The 3D plaza with the player (walkable). In the player list and profile scenes
the field is unloaded and the top shows the castle emblem art (lists) or the
records board (profile).

### Readable state

Rank and FC are on the top screen in the profile; not located in memory
(unverified where; a save diff after earning FC would find them).

### Recommendation

The hub is a launcher: two circles leading to link/online menus that cannot be
tested in Azahar (local wireless only, no partners, Internet must not be used),
two icons that already open native Party and Bag, and two scenes (lists,
profile) that replace the field anyway. But it sits over a **walkable field**,
so the full panel view is a poor fit (it would hide the plaza the player is
walking in). Worth a **small native hub menu**: a compact top-screen menu
(Battle, Trade, Player Lists, Profile, Pokémon, Bag, Quit) shown on X or while
the hub is idle, each a one-tap action; Quit = back + Y. The link menus and the
two sub-scenes stay in full mode (simple pill lists; could be mirrored later
with the Options-style list code). Low priority: most of the plaza's value is
online. Also check before building: with auto view on, field state 4 makes
AutoPanel treat the plaza as `FieldRo.xmenu` and show the panel over the
walkable plaza (inferred from `docs/XMENU.md`, not observed: auto was off).
Not looked at: the castle's front desk and plaza facilities (talking to staff,
on the top screen like any field talk).

### Native hub bar implementation (backlog #11)

Implemented `Screens/FestivalPlaza.*`: a 392 x 24 strip at top-screen
(4,216), leaving every pixel above y=216 untouched. L opens Pokemon (labelled
"Party" to fit), R Bag, Start Profile, B Leave. These send the measured taps
(187,213), (229,213), (145,213), (16,223). Only these four keys are captured on
a stable hub; D-pad, circle pad, A and touch remain game controls. There are
no held-key repeats or queued bar inputs. Party/Bag use the existing native
screens; Profile, player lists, Battle/Trade/link menus and facilities stay
with the game. L+R retains the panel fallback and yields until this hub visit
ends. Unknown views hide the strip and release the panel override; automatic
view resumes its configured policy (with auto off, use the manual panel).
No online or wireless action was added. `action close` has no Plaza override;
B on this bar is the supported native exit.

Detection combines FieldRo with a scene-stack responder owned by
JoinFestaSubScreen, resolved by CRO name rather than its captured address.
Bottom pixels must then match three consecutive observations before the
hub can draw or take keys. A mismatch releases hub keys immediately.

Capture calibration: PNGs read **read-only** with Python/Pillow from
`H:/SunnyMoon/analysis/private/research/remaining-screens/` (the local
worktree has no private captures). `rs-fp1-bottom.png` and
`rs-fp-walk-bottom.png` have identical values at all chosen hub points:
(40,113) = (229,55,14), (280,113) = (47,212,229), (103,203) = (255,238,68),
(145,203) = (238,136,255), (187,203) = (136,221,0). These are button interiors
and icon colours, not plaza scenery, text or connection-banner wording.
Every point is required, within 20 per RGB channel.

`rs-fp-quit-bottom.png` supplies the quit guard: (40,113)/(280,113) =
(51,167,255), (40,124)/(280,124) = (113,187,253), (5,8)/(5,38) = (78,64,32).
All six points must match within 20; the dark message border distinguishes
Quit from the Battle/Trade pill at the same height. Negative controls read
and sampled: `rs-fp-battle`, `rs-fp-trade`, `rs-fp-link`, `rs-fp-list`,
`rs-fp-prof2` (all `-bottom.png`); all reject both Hub and Quit. Real RGB
fixtures are in `tests/festival_plaza_samples.hpp`; private PNGs are not copied.

Exit: guarded Back once -> wait up to 240 frames for Quit -> settle 20 frames
-> guarded tap (160,118) **once** (the observed equivalent of Y) -> wait up to
1200 frames for FieldRo, state **0**, with the Plaza responder gone. FieldRo
alone cannot prove exit because it stays active throughout Plaza. The action
observer runs independently of the bar; Quit needs three matches, allowing
unknown gaps of up to six bottom observations, then holds for at most six
missing observations. A recognised hub clears Quit immediately. CRO loss
resets the observer. No confirmation retries; cancellation or failed guards
return control through the existing runner. An already-open quit prompt is
left to the game, never automatically confirmed by the bar.

Host verification: `powershell -NoProfile -ExecutionPolicy Bypass -File
tests/run_festival_plaza.ps1` compiles directly with MSVC (no make/WSL), runs
capture-backed detection/tolerance/negative tests, key/tap and refusal tests,
probe flicker/expiry tests, and the real runner's successful/flickering,
missing/changed/expired prompt, CRO/scene loss, stuck exit, cancellation,
still-in-Plaza and FieldRo-without-walking-state cases. Confirms at most one
Quit input. It also runs the existing Pelago, native-screen and action-queue
regressions and generated-layout check. `-FocusedOnly` runs only the Plaza
suite plus layout check. `tests/Makefile` includes the new suite.
`tests/build/festival-plaza-1x.ppm` and `festival-plaza-2.5x.ppm` preview the
strip; the actual hi-res Layer lifecycle and untouched upper pixels are
checked. Direct devkitARM `-fsyntax-only` passed on FestivalPlaza.cpp,
SubScreens.cpp and Actions.cpp; no linked plugin build was run.

**Live calibration remains required.** Captures establish colours at two
hub moments, not all animations, text languages, first visits or facility
dialogues. On USA v1.2 English, enter Plaza with auto view on/off, walk, test
L/R/Start and return, check the bar is absent in all submenus, test L+R full
fallback, B exit/re-entry, cancellation and timeout. Confirm the observed
CRO responder lifetime through Quit, field state 0 on return, exactly one
Back/Quit touch, prompt flicker tolerance and an unobstructed walkable plaza.
Castle/facilities, Link Trade, online menus and non-empty lists remain
unverified; this implementation does not expand that research scope.

## QR Scanner

- X menu page 2 → QR Scanner (230,48 on `lrtoggle`). Seen in the logs: plugin
  `scene: (none)` → **`scene: QRReader`**; Azahar: FieldRo unloaded, **CRO
  `QRReader` at `0x006DF000`-`0x006F5000`**, then
  `unknown / unimplemented function 'SetProbeResponseParam': port='nwm::UDS'`,
  and nothing more. The game and the plugin stopped answering (`cmd.py` and
  `shot.py` timed out; the log did not grow in the following 10 s). Same as
  `docs/OFFSETS.md` reports. No screenshot, so the layout, the scene vtable
  and the exit are not known. Whether the camera or the UDS call causes the hang
  is unverified (the outer camera is configured as `blank`).
- The instance had to be stopped; the save was unchanged.
- Recommendation: **no native version**. It needs the camera, the screen
  could not be seen, and the X menu already greys it out. Revisit only after
  someone opens it on hardware (or a newer Azahar) and records the layout; then
  "full" mode is likely enough.

## What was not reached

- Pelago on the `lrtoggle` save (locked there); the Pelago first-visit
  tutorial; Pelago's ◀ arrow, bean box, Exchange Poké Beans and the isles'
  object taps.
- Festival Plaza: online menus (not allowed), Link Trade, the castle and
  facilities, B on the hub, a non-empty player list.
- QR Scanner: everything past loading.
