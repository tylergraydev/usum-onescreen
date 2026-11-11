# Optional widescreen side panels

`[display] side_panels = off|battle|field|both` defaults to `off`.
`panels <mode>` changes it live without saving the INI; `panels` reports it.
Invalid modes keep the current setting. Requires aspect >5:3. Field mode stays
an overlay on the selected wide projection; it is not a minimap.

## Battle framing

The first implementation merely disabled Battle.cro's perspective hook.
The supplied Azahar captures confirmed that this left a stock picture stretched
across the full viewport and let HP-box decoration extend beneath the strip.

The hook now stays installed at non-stock aspect. When battle panels are selected,
its dedicated callback calls the original builder with the incoming stock 5:3
aspect, then divides the four entries of its clip-X row by `WidescreenSqueeze()`.
This preserves FOV/near/far and the Y/Z/W rows. Stock clip edges +/-1 land at
raw x=57.14/342.86 at 21:9; opaque side panels cover the extra peripheral scene.
After Azahar stretches the framebuffer, the middle picture has stock proportions.
This is projection scaling plus masking, not a framebuffer copy or viewport patch.
With panels off, the callback delegates to the previous shared widescreen callback.
All seven statically identified Battle+0x736D0 callers pass pivot 0; pivot 4 is
also unrotated. Other pivots/non-stock incoming aspects retain previous behavior.
The original signature/lifetime/pre-unload guard is retained. Hook failure hides
battle panels rather than showing them over an uncorrected projection.

Static proof: Battle+0x73774 divides cot(fovy/2) by aspect; +0x7377C stores that
at matrix+0, and +0x73764 stores the vertical coefficient at +0x14. Matrix is
row-major 4x4. Main CalculateMtx disassembly at 0x2A23C0 reads the parent at
pane+0xC; 0x2A2724..0x2A2750 traverses child nodes at pane+4, sentinel +0x14.
Both were inspected in the user's local binaries; no game bytes are distributed.

## Exactly which 2D layouts are affected

The existing top templates at **0x5C3180 and 0x5E8468** already set horizontal
bounds to +/-200*k (at 21:9, +/-280), squeezing every top layout created from
them by 1/k. The supplied captures show this: the player name begins near x=57.
We do **not** apply another 1/k to those panes. The decorative HP backgrounds can
extend past the authored +/-200 range, so the CalculateMtx hook adds a bounded fit:

- Only while the current Battle CRO's projection hook is active and battle
  panels are selected, for a **parentless pane named `RootPane`, vtable
  0x646B28, authored size exactly 400x240**.
- The fit includes visible, unrotated panes of that root's ordinary child tree
  whose width is 1..400, bounds lie within +/-256, and overlap +/-200 by at
  least 30%. HP/name/status/EXP/message pieces in such a top root share one
  fit factor `200 / max(200, -left, right)`; it never enlarges a layout.
- It scales the completed global matrices' X row and X translation together.
  One post-recursion walk applies the transform once to each ordinary child.
  Root dirty propagation rebuilds original matrices every frame and once after
  disabling panels, avoiding cumulative squeeze or stale fitted coordinates.
- Traversal is capped at 1024 panes and checks child/parent links. An incomplete
  first walk makes no fitting writes. Nothing is cached across CRO/layout frees.

This is a root-shape rule, **not a filename allowlist**. The shared-template top
HUD (both HP gauges and messages) keeps its existing 1/k squeeze; only matching
400x240 top roots get the additional overflow fit. `aspect` now logs cumulative
`battle HUD roots`, `fits`, and the last factor: emulator verification must
confirm the intended HUD roots match, rather than assuming every battle asset does.

Unaffected / not safely normalized by the new fit:

- **320x240 bottom layouts**, including the game's command/move list, Party,
  Bag, question and target screens. Plugin Classic/Bar/Rail command and move
  replacements already squeeze through Layer/GpuQuad; their menu code is unchanged.
- Root panes of other sizes/names/vtables, parented roots, parts layouts outside
  the ordinary child tree, and rotated/parked/oversized panes as bound sources.
- No new guarantees for intro/trainer/Z-move cut-ins, weather/fade/flash layouts,
  or SequencePlayer.cro's independent perspective builder. TextBox_00 or other
  logical text boxes extending beyond +/-256 are not fit-bound sources; visible
  glyph bounds are not inferred from the text box size. Shared-template ones
  retain that template's existing squeeze, but are not individually verified.

The fit assumes the layout was created with the selected aspect's templates.
After changing `aspect` live, reopen/reenter battle to recreate old layout
environments, as required by the existing widescreen implementation. Switching
`panels` alone does not require recreating layouts.

## Both sides and compact content

Battle backgrounds cover **every side pixel for the full 240-pixel height**.
The same nearest-column boundaries as Layer give centre [57,343) at 21:9 and
[13,388) at 16:9. Left: six own-party slots in ReadBattle order. Right: **client
1, record 0** from ReadBattle.opponent, with short species name, level, HP bar
and checked status. No other SOS/Royal clients are exposed by the existing
reader; these are deliberately unlisted. The header reads `FOE`, with the
level (for example `Lv7`) retained below the row; there is no client/debug label.
Missing foe data says `No data`; no turn/weather values are invented.

At 21:9 each party member occupies one 12-pixel-high line: measured name,
HP bar and one condition glyph. The foe uses the same row renderer. Hi-res
uses the full side width, proportional 5x7 glyph ink and one physical pixel
between characters. Names stay at native size when they fit, tightening ink
width to at most 85% at 2.5x or 80% at 1x before truncating whole UTF-8 characters.
The 1x bar is 7 pixels wide, hi-res 10 logical pixels; both are 4 logical pixels
high at 21:9. Healthy rows reclaim the condition column and have no `OK` label.
Torracat, Pikipek, Buneary and Rattata fit in the tested 21:9 rows at both scales.
Glyphs: yellow P=PAR,
purple S=SLP, cyan F=FRZ, orange B=BRN, purple P=PSN, red F=fainted, gray ?=unknown.
At 16:9 there is only room for slot number, HP bar and a colored status marker.
The 1x cache limits content to 56 pixels; hi-res uses the full side width.
Species text falls back to dex number. No icons or new assets are loaded.

Status comes from the battler's checked PokemonParam (matching species and valid
stats), otherwise unknown. Party samples every six plugin frames; unchanged
snapshots do not rasterize/upload. Field uses ReadParty and draws only the left
panel while walking. Scene changes, full menus, Map and manual bottom panels
suppress strips. Retained native Battle Party/Bag pages are checked through the
existing read-only BattleScreenStatus page field; unknown formats hide them.

## GPU and memory

Left uses existing BadgeSlot, right existing RotomSlot. Boxes/field Rotom and
battle strips are mutually exclusive; Layer ownership/generation/atlas-validity
checks remain in use. No slot, atlas pool or GpuQuad pass structure was added.
Two full-height 2.5x textures are inverse-mapped before the common quad squeeze.
At 21:9 each is 143x600 RGBA: **343,200 bytes**, **686,400 total** in existing
atlas/staging storage, not a new allocation. Both fit beside the existing
Classic/Bar/Rail command/move rectangles (packing is host-tested).

GX added per present: **0 extra submissions** to the shared target-mode list;
if these are the only overlays, the existing backend submits **one list** with
two quads. Fallback: **0 GX**, CPU writes only. No per-column TextureCopies,
framebuffer reads, render-target copies or extra passes. This remains below <=8.

The fallback uses **two 56x96x4 caches = 43,008 bytes total**, plus **960 bytes**
for one solid background column reused on both sides. Text fits in the first
96 rows; background fills the remainder without storing it. ARM -Os compilation
of SidePanels.cpp reports 43,996 data + 1,324 BSS = **45,320 bytes** of static
storage including its two Layers/snapshot (less than the previous 50,964).
There are a few small additional HUD counters in Widescreen.cpp. No new heap
pixel buffers or linear allocations. Atlas allocation failure uses the CPU
cache and retries; still check memory, frame time and fallback counters in-game.

## Minimap follow-up

MAP.md / RE4-map.md expose position, zone, facing and the scrolling Rotom map,
but the current 260x128 1:1 GPU copy cannot fit the 13/57-pixel side area. A real
side minimap needs validated crop/scale or game-pane projection, readable markers,
a bounded single GPU copy/composition, coexistence with Map's pane override,
and indoor/scrolling/lifetime tests. An independent renderer also needs tile
atlas/layout, streaming lifetime and world-to-tile/marker transforms. No map
assets are shipped or extracted here.

## Verification

Direct MSVC C++17 compiler recipes, without WSL or make, build/run the new test
and settings_test (219 checks), battle_widescreen_test and hires_test. Render
recipes suppress existing Raster.cpp conversion warnings with /wd4244.
ARM C++17 syntax checks cover SidePanels, SidePanelsView, BattleWidescreen,
Widescreen, Settings and main, with -Wall -Wextra -Werror and only existing
main callback unused parameters suppressed. This is not a linked plugin build.

The new host test covers mode parsing, stock-X projection scaling and untouched
other rows, 16:9/21:9 full-height margins, inverse GPU destinations, HUD fitting,
root filtering, bounded tree traversal/corruption, non-cumulative matrix math,
live mode changes without hook reinstall, slot packing, foe change detection,
condition glyphs, measured 7-8-character names at both scales, removed debug
text, foe bar/status/level, opaque fractional-scale edge texels, CPU-cache equivalence
and four synthetic 1x/2.5x PNG previews.
Previews include synthetic HP boxes, not emulator output or extracted game art.
PPM intermediates are ignored; tests/Makefile includes the test normally.

Fresh emulator checks for this revision:

1. At 21:9/5x, compare the SAME camera pose stock vs panels: Pokemon proportions
   and stock framing should match in the centre, with solid margins both sides.
2. Toggle panels off/both repeatedly: no hook reinstall is needed; the shared
   callback restores full-wide behavior and HUD fitting rebuilds away.
3. Run `aspect`: HUD root/fit counters must grow when decorative overflow is
   present. Verify both HP boxes, party balls, EXP, messages and all three native
   command/move layouts stay inside the margins, including battle transitions.
4. Test faint/status/switch, trainer battles, SOS/Royal (right shows client 1
   record 0 only), cut-ins, fades and weather. Report any unmatched root/layout.
5. Open Party/Bag/Map/Boxes/dialogs/manual panel; return repeatedly. Toggle
   hires off/on, test 16:9 and atlas fallback, and check memory/frame/GX counters.
6. Verify unload logs across consecutive battles on both games. No emulator
   was driven in this sandbox; the supplied captures validate the old failure.
