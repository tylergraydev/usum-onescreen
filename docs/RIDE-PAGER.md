# Ride Pager (W5-4)

**Status: blocked. The test save has no Ride Pager, so there is nothing to
build a native screen against yet. A later save from Tyler is needed** (one
with at least the Ride Pager and Tauros; more Ride Pokémon make a better test).

## What this save has (checked 2026-11-02, instance w54)

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

## How the Ride Pager is normally opened (from guides, not checked in game)

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

## Plan once a save with the Pager exists

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
