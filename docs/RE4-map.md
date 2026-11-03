# RE4: where the player is (Ultra Sun USA v1.2)

What a native top-screen map (Phase 2) needs: the current zone and its name, the player's position and facing, whether the zone is an interior, and how the game's own Rotom Dex map places the player. Everything here was found and checked at runtime in an isolated Azahar instance on 2026-11-02, by walking from inside the Route 2 Pokémon Center, out to Route 2, along Route 2 (through the tall grass, about 600 m) and into Hau'oli City (Shopping District), over five instance restarts. The `where` command (`Sources/Where.cpp`) prints all of it:

```
where: zone 79 (entry 79, parent 79) name line 12 indoor at 12579 6323 pos 2175.9 4.0 2522.7 facing -58 SW player 33F81E20 | map 0 centre 12579.0 6323.0 fixed flag 13742.0 7517.0
where: zone 7 (entry 7, parent 7) name line 12 outdoor pos 12600.9 290.1 6389.7 facing 18 S player 33F81E20 | map 0 centre 12600.9 6389.7 follows flag 13742.0 7517.0
where: zone 13 (entry 13, parent 13) name line 20 outdoor pos 7524.6 -995.2 16323.3 facing 0 S player 33F81E20 | map 0 centre 7524.6 16323.3 follows flag 13742.0 7517.0
```

Outside the field (battle, Pokédex, Party, …) it prints `where: not in the field`.

## Object paths

Heap addresses are examples from these runs; the code follows the pointers every time and checks the vtables marked "checked".

```
gm       = *(0x6A59C4)                 game manager (no vtable at +0). Check: *(gm+0x1C) == *(0x6A599C), the scene manager
field    = *(gm + 0x24)                field object (persists across zone changes)
models   = *(field + 0xCC)             move model manager, vtable 0x64C628 (checked): 35 model pointers from +0x0C
player   = *(models + 0x0C)            model [0] = the player, vtable 0x64C538 (checked; NPC models are 0x64C1E8)
location = *(gm + 0x2C)                +0x0A u16 zone of the last map entry, then that entry's position and rotation
work     = *(location + 0x88)          field work: +0x18 the player model (checked == player), +0x1C u32 current zone
zones    = *(field + 0x54)             zone data loader, vtable 0x64BDD0 (checked): +0x14 table, +0x18 size in bytes
entry    = table + zone * 0x54         one entry per zone (380 in v1.2: 0x7CB0 / 0x54)
```

### Player model (vtable `0x64C538`, `0x7C0` bytes)

| Offset | Type | Field | Checked against |
|---|---|---|---|
| `+0x3B0` | float x, y, z | position (two more copies at `+0x3BC`, `+0x3C8`; another at `+0x418`) | see "Walk" below |
| `+0x424` | float x, y, z, w | rotation quaternion, a pure yaw: (0, sin θ/2, 0, cos θ/2) | facing in screenshots and the Rotom map arrow |

Axes: **+x is east, +z is south** (right and down on the Rotom map); y is height. Yaw θ = 2·atan2(qy, qw): **0 = south, 90 = east, 180 = north, −90 = west**. In the Pokémon Center the camera looks north, so stick right / up moved x up / z down and turned the player to 90 / 180, as on screen. Outdoors the camera turns with the path, so screen directions and world directions differ (on Route 2's lower path, pushing down walked the player west, and both `where` and the Rotom arrow said west). `where` rounds to 8 compass points.

Interiors use their own local coordinates (the Pokémon Center is around x 2100–2450, z 2300–2680); outdoor zones of one island share world coordinates (Route 2 and Hau'oli City are continuous: crossing between them kept x and z).

### Zone ID

Two sources that agreed everywhere in the field:

- `work + 0x1C` (u32): the current zone. It changed the moment the zone changed: on the Pokémon Center door transition (79 → 7, scene reloads) and on the seamless walk into Hau'oli City (7 → 13 at z ≈ 16300, the frame the place-name banner appeared). **In battle it reads 0**, which is also a real zone (Route 1), so `where` first checks that the scene is `FieldRo`.
- `location + 0x0A` (u16): the zone of the last entry, followed by the entry point's position and rotation (the door spawn). The save's own copy of this record (zone `0x4F`, position 2175.856, 3.938, 2522.739, quaternion y −0.484 / w 0.875) is what the game loaded at Continue.

Zones seen: **79** Route 2 Pokémon Center (interior), **7** Route 2, **13** Hau'oli City (Shopping District).

### Zone data (place name, interiors)

The table is the game's zone data (0x54 bytes per zone), resident on the heap while in the field. Fields used:

| Offset | Type | Field | Values seen |
|---|---|---|---|
| `+0x0A` | u16 | parent zone (sub-zones of one area share it: 36–38 → 35, 40–42 → 39, 50 → 49, 52–53 → 51) | 79, 7, 13 → themselves |
| `+0x0C`.. | s32 ×3 at `+0x10` | default position (x, y, z) | 79: 2100, 0, 2680 (inside the door) |
| `+0x1C` | u16 | **place name**: line in message file 72 (`a/0/3/<lang>`); `line + 1` is the sub-name | 7 → 12 "Route 2", 79 → 12, 13 → 20 "Hau'oli City" + 21 "Shopping District" (the banner text), 0 → 6 "Route 1", 6 → 10 "Route 3" |
| `+0x44`, `+0x48` | s32 | **fixed map point** in island-map world units; 0, 0 for outdoor zones | 79: 12579, 6323 = exactly the Rotom map centre while inside |

**Indoors** = the zone has a fixed map point (`+0x44/+0x48` not both 0). This is what matters for drawing a map: an interior's own coordinates don't place it on the island, the map point does. Cross-check: inside the Pokémon Center the Rotom map centre stayed at (12579, 6323) while the player moved; outdoors it equals the player's position (`where` prints `fixed` / `follows`). Other bytes that differ between the interior and the outdoor zones (`+0x26/+0x27` 08 02 vs 09 01, `+0x3D` 00 vs 01, `+0x4F` 00 vs 01) may be map-type flags; with only one interior seen they are not interpreted.

**Name text:** not resident. In the field there is no full-load `MsgData` (vtable `0x64B104`) for file 72, and the global message cache (`0x6A7368`) holds only items. The game's WordSet path reads a place name from the RomFS per lookup (`docs/RE2-text.md`). So `where` prints the line number; for display, decode file 72 from the user's own dump (`tools/romfs_text.py text <romfs>/a/0/3/2 72`) or call the game's loader.

## Zone table, all 380 zones (P2-5, 2026-11-02)

The whole table was dumped at runtime (instance p25, in the Route 2 Pokémon
Center: `peek` from `*(0x6A59C4)` → `+0x24` → `+0x54` (vtable `0x64BDD0`) →
`+0x14` = `0x33FA609C`, `+0x18` = `0x7CB0`; `dump 33FA609C 7CB0 zones`) and
read offline next to the place names from the user's own dump (file 72). The
dump and the list stay in `analysis/private/`. What it shows:

- **Fixed map point (`+0x44/+0x48`)**: 324 zones have one. The 56 that don't
  are exactly the outdoor routes, towns, beaches and seas, in four contiguous
  blocks: zones 0-18 (Route 1-3, Kala'e Bay, Melemele Sea, Big Wave Beach,
  Hau'oli City, Iki Town) plus 318-320 (Route 1), 80-92 (Route 4-9, Hano,
  Heahea, Paniola, Konikoni, Royal Avenue), 164-175 (Route 10-17, Ula'ula
  Beach, Tapu Village, Malie City), 264-272 (Poni Wilds ... Seafolk Village).
  Everything else (buildings like zone 79, caves, trial sites, Aether
  Paradise) has a point. So the rule "map point set = not placed by world
  coordinates" holds across the whole table: **"indoor" means any zone that
  is not one of the outdoor island areas** (caves included), not only
  buildings. Runtime checks are still the three zones above.
- **`+0x4E` (u8), the zone's map**: 1 for every Melemele outdoor zone, 2
  Akala, 3 Ula'ula, 4 Poni, 5 the 57 Aether Paradise zones, and 6-21 one
  value each for single areas with their own small map (Hau'oli Cemetery,
  Berry Fields, Po Town and Shady House, Memorial Hill, Paniola Ranch,
  Mount Hokulani, Poni Meadow, Exeggutor Island, Pikachu Valley, ...).
  The Route 2 Pokémon Center (zone 79) has 1; 72 zones have 1, 22 of them outdoor. 21 values for the 21
  entries of the map table at `0x5BE110` (0-3 islands, 4-19 smaller maps,
  above), and zones 7, 13, 79 (value 1) showed Rotom map index 0: so `+0x4E`
  = map index + 1. This gives the island from the zone data alone, without
  the Rotom map view. Only value 1 / index 0 was seen at runtime.

## The Rotom Dex map (what it reads)

The bottom-screen map in the field is a main-image class (vtable **`0x64786C`**) owned by the FieldMenu CRO: scene manager stack (`*(0x6A599C) + 0..0x1C`) → the entry whose vtable is FieldMenu + `0x183A0` (module name checked) → `+0xD0` → map view.

| Offset | Field | Seen |
|---|---|---|
| `+0x14C`, `+0x150` | float scale x, z: world → map pixels (`0x2CF230` = 0.0270139, `0x2CF234` = 0.0270536) | |
| `+0x154`, `+0x158` | the inverses (37.018, 36.964) | |
| `+0x134`, `+0x138` | map tile size (168 × 84) | |
| `+0x15C`..`+0x16E` | the current map's table entry, copied from `0x5BE110 + index * 0x14` (21 entries: 0–3 one per island, 16 × 8 tiles; 4–19 smaller maps) | entry 0 |
| `+0x170` | map index | 0 on Route 2, in the Pokémon Center and in Hau'oli City (Melemele) |
| `+0x174`, `+0x178` | **map centre in world x, z** | = player x, z outdoors (every step of the walk); = the zone's map point (12579, 6323) indoors |
| `+0x17C`..`+0x188` | centre bounds: min x, min z, max x, max z | 5922.9, 4435.6, 43829.3, 45243.6 on Melemele |
| `+0x19C`, `+0x1A0`, `+0x1A4` | goal flag world x, z, shown | 13742, 7517, 1 (the red flag east of the Pokémon Center) |

The player icon is drawn at the screen centre with the facing arrow; the map scrolls under it. Measured on a heap node that moves with the map: its translation went 30.8 → 71.2 → 77.9 for x 12600.9 → 11105.9 → 10857.3 (slope −0.02701) and −68.5 → −35.7 → −27.8 for z 6389.7 → 7603.8 → 7895.1 (slope +0.02706), i.e. exactly the scale constants. On screen, walking (−1818, +1233) world units moved the map by about (+45, −39) px against a predicted (+49, −33).

Functions: `0x2CF164` sets the scales and tile sizes; `0x2CF5DC (this, mapIndex, centreX, centreZ)` loads the map entry, computes the bounds and clamps the centre; `0x2CE6D0` lays out the visible map tiles from the centre (`centre * scale + entry offset`); `0x2CF050` re-applies and places the goal flag pane (`+0x19C/+0x1A0` → pane translate at `+0x1C`). Which field code passes the player's position in each frame was not traced (it is not one of the `vstr …,[r,#0x174]` sites decompiled so far).

For a native map: `map_px = world * (0.0270139, 0.0270536)` (plus the map entry's offset, 0 for island 0), centred on the player outdoors or on the zone's map point indoors, with the island chosen by the map index.

## How it was found

1. Heap dumps (`dump 30000000 4184000 NAME`) inside the Pokémon Center before and after two straight moves with the circle pad, plus a dump after each move without moving (to drop anything animating). Floats that changed only with the matching move and held still gave the position: dozens of copies of (x, 3.94, z), x 2321.9 → 2066.4 walking left, z 2364.3 → 2620.0 walking down.
2. A backward pointer search from those copies (skipping heap block headers, tag `0x5544`) found only the move-model pool (`0x6A70DC`, reordered by distance every frame) and heap links; the player model's own vtable (`0x64C538`, one instance) and the manager (`0x64C628`, ctor `0x3B5BE4`: 35 models at `+0x0C`) came from looking at those objects. A forward search from the globals that point at the scene manager's owner found `*(0x6A59C4) + 0x24 → +0xCC → +0x0C`.
3. Zone: global/heap diffs inside vs. outside; `work + 0x1C` went `0x4F` → 7 and matched the saved start record's `0x4F`. A third zone (13) came from walking into Hau'oli City.
4. Zone table: searched for a fixed stride where the u16 at zones 7 and 79 is 12 and at zone 13 is 20 ("Route 2" / "Hau'oli City" lines) and every value is a valid even line; one hit, stride 0x54, whose `+0x0A` field equals the index. The pointer to it led to the loader (`+0x14` table, `+0x18` size) and from there to `field + 0x54`.
5. Rotom map: floats linear in the player's x or z across three outdoor dumps gave the scroll slopes (≈ ±0.027); the same constants are literals at `0x2CF230`/`0x2CF234` loaded by `0x2CF164`, whose object (vtable `0x64786C`) holds the centre. Back-search from it led to the FieldMenu frame on the scene stack.
6. Verified with the final `where`: walks inside the Pokémon Center (right: x 2175.9 → 2431.4, facing 90 E, screenshot facing right; up: z 2524.7 → 2329.1, facing 180 N, back to the camera), out of the door (zone 79 → 7, map `fixed` → `follows`), about 40 steps along Route 2 (x 12600 → 7507, z 6390 → 16049, map centre equal to the position at every step, the Rotom arrow matching the compass point), into Hau'oli City (zone 13, name line 20, banner "Hau'oli City / Shopping District"), and a wild battle (`not in the field`, then zone 7 again after Run). Screenshots are in `analysis/private/shots/` (local only).

## Confidence

- High: position, facing (quaternion → yaw and its axis convention), the player path and its vtable checks, the current zone (`work + 0x1C`) in the three zones tested, the place-name line field, the Rotom map centre / scale / bounds.
- Firmer since P2-5 (whole zone table, above): indoor = fixed map point (only the 56 outdoor island zones lack one; caves count as "indoor"); island = zone `+0x4E` - 1 = Rotom map index (four outdoor blocks, 1-4; one value per small map). Still only one interior and one island seen at runtime.
- Medium: the meaning of `location + 0x0A` (last entry zone; it equalled the current zone in every test because no seamless boundary was crossed back and forth before a door). P2-5: right after a door it already holds the new zone while `work + 0x1C` still reads 0 (docs/MAP.md).
- The heap addresses were the same in every run (`gm` `0x33F67FE0`, player `0x33F81E20`); the paths were not exercised against a different heap layout, but the code follows them and checks vtables each time.

## Open questions

- Another island (map index 1–3) and another interior (a house, a gate, a cave) to confirm the map index = island and the indoor rule. Caves and multi-floor buildings may use a map point too, or not.
- Zone data fields not decoded: `+0x00`..`+0x08`, `+0x0C` (`0x2E8`, `0x31E`), the bytes at `+0x24`..`+0x27` and `+0x38`..`+0x4F`. Weather, BGM, map type and the "area" for fly points are probably among them.
- Where the field code feeds the player's position into the map view each frame (`0x2CF5DC`'s callers `0x2CE6D8`, `0x2CF074` take it from `+0x174`; the writer outside the class was not found).
- The island map images themselves (the tiles drawn by `0x2CE6D0`, tile index `= x / 168 + (y / 84) * width + first`, first tile from the map entry) live in the FieldMenu layout archive in the RomFS; not extracted.
- Place-name text at runtime without the RomFS: the game's own WordSet loader could be called, or the plugin could ship nothing and read a names file the user generates (`docs/RE2-text.md`, option 3).
