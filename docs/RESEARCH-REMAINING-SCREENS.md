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
