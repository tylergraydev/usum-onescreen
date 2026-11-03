# Native map (P2-5)

With the panel off, **ZR** in the field shows a map on the top screen:

- the place name (and its second line, e.g. "Hau'oli City / Shopping District"),
- the island,
- inside or outside, and the player's facing (compass and words),
- the player's world position (x, z),
- the game's own Rotom Dex map around the player, live: it scrolls as the
  player walks, with the player marker and the goal flag.

**ZR** or **B** closes it. The player can keep walking with it up (the circle
pad, A and X stay with the game). X opens the X menu as usual and closes the
map. The native X menu's **Map** entry opens it too.

A host-rendered preview is written to `analysis/private/map-preview.ppm` by
`make -C tests`, and the scenario's screenshots to
`analysis/private/shots/native-map-*` (neither is in git).

## Files

- `Includes/Screens/MapView.hpp`, `Sources/Screens/MapView.cpp`: the pure part
  (no CTRPF): the view built from a field reading, island and compass names,
  the zone filter, the keys, layout and drawing. Host tests in
  `tests/map_test.cpp`.
- `Includes/RomfsText.hpp`, `Sources/RomfsText.cpp`: reads a message file out
  of the game's RomFS (RomFS level 3 → GARC → message file → UTF-8 lines),
  pure functions over a read callback. Host tests in `tests/map_test.cpp`,
  including the user's own `a/0/3/2` when it is in `analysis/private/`.
- `Includes/Screens/Map.hpp`, `Sources/Screens/Map.cpp`: the plugin glue: keys,
  when to show, reading the field (`ReadWhere`, `Sources/Where.cpp`), the
  RomFS read, the panel, input capture, the OSD layer and the GPU copy.
- `PanelCopyBottomRect` (`Sources/Panel.cpp`): one GPU TextureCopy of a
  bottom-screen rectangle onto the top screen.
- `UI::Layer::Present(screen, skip)` (`Sources/UI/Layer.cpp`): presents a layer
  but leaves a rectangle alone for the GPU copy.

## Drawing: the game's own map, copied at runtime

Two ways were considered:

1. **Copy the game's bottom-screen map at runtime** through the GPU path the
   panel already uses (PR #23). The bottom screen in the field is Rotom's
   screen, and its map window (layouts: `field.map`, 29,84 – 290,231) is the
   island map centred on the player (RE4: `+0x174/+0x178` follow the player
   outdoors, the zone's fixed map point indoors), with the player marker,
   its facing and the goal flag. The game keeps drawing it while the bottom
   screen is hidden.
2. **A drawn schematic** (zones as boxes, a dot for the player). It would need
   the island's shape, which only exists as the game's tiles (RE4: in the
   FieldMenu layout archive), so it could only show coordinates without a
   real map. Shipping or extracting tiles is game data.

**Chosen: 1.** It ships nothing of the game, it is the real map (terrain,
paths, the goal flag), it follows the player with no plugin code, and it is
cheap: one TextureCopy per frame, entirely on the emulated GPU (no
framebuffer download; `docs/PERF.md`). The plugin draws only the frame
around it (header, compass, footer) into a `UI::Layer` that is redrawn when
the view changes.

The copy (`MapSource` → `MapWindow` in `MapView.hpp`): bottom (30,104) 260 ×
128 to top (70,72), 1:1. Framebuffer lines are screen columns of 240 BGR8
pixels, and TextureCopy moves whole 16-byte units at 8-byte aligned
addresses, so each column's run must be a multiple of 16 bytes (128 px =
384 bytes) starting at a multiple of 8 px from either end of the column (y
and y + h multiples of 8, on both screens; `tests/map_test.cpp` checks it).
The source starts at y 104, below Rotom's eyes, which reach y 97 at the
window's sides (measured on a screenshot); the player marker (160,158) is 10
px above the window's middle.

Order in the OSD callback: the layer is presented first with the map window
left out (`Present(fb, MapWindow)`), then the TextureCopy fills the window.
The CPU never writes the pixels the GPU copies, so the two don't fight over
Azahar's cached surface. If the copy is refused (no GPU path, unexpected
framebuffer format), the window shows "Rotom's map isn't up" instead and the
log says `map: GPU copy of the Rotom map refused`. Without the Rotom map view
(`where.mapView == 0`) the window shows the same note.

## The look (W7-6)

The frame around the copied map is drawn like the game's own map screens,
with `UI::Theme` (`docs/VISUAL-STYLE.md`) for text, windows and prompts.
Colours were read from screenshots of the Rotom map (the field's bottom
screen) and the Town Map (`analysis/private/shots/native-map-*-bottom.png`,
`native-map-townmap-bottom.png`; game imagery, not committed):

- **Body**: Rotom's red, `#E74A42` at the top through `#CB3E36` to
  `#A83131` at the bottom, with a dark edge `#6B2121`.
- **Map window**: recessed into the body as on Rotom: 6 px `#9C3030` with a
  2 px dark line `#3B3333` next to the map (`MapRecess`). Under the copy,
  and visible when there is none, the Town Map's sea `#0AB5EA` with its
  22 px grid `#32BEE9`; the "Rotom's map isn't up" note sits in a
  `DrawWindow` on it.
- **Header** (`MapHeader`): the theme's dark window, place name in the game
  font (Body, white) with its second line below it (Small, tan); the island
  (Body) and the position (Small) right-aligned.
- **Compass** (`MapCompassBox`): a small dark window, the facing as the
  game's red cursor arrow.
- **Footer**: "Outside, facing S" in white ("Inside" in yellow) with a dark
  red outline; the prompts `ZR` (a named badge, as the theme draws L/R) and
  the game's (B) icon with "Close".

The layer redraws once the game font has loaded (`UI::RequestAssets()` on
open), as the X menu does.

## Place names: read from the game's RomFS

Place names are not resident in the field (RE4: no MsgData for file 72), and
the plugin must not ship game text. The plugin reads them once, the first
time the map opens, from the running game's own RomFS:
`FSUSER_OpenFileDirectly(ARCHIVE_ROMFS, empty path, binary path {0, 0, 0})`
opens the title's RomFS at IVFC level 3 (the reader also accepts a full image
with its IVFC header). From there: directory `a/0/3/<lang>`, file → GARC
entry 72 → message file → 234 lines decrypted with the key from
`docs/RE2-text.md` (`DecryptMsgLine`, shared with the Party code), control
tags dropped. `<lang>` is the byte at `0x671A10` (2 = English; RE2).

Measured in Azahar: `map: place names: 234 lines from a/0/3/2 file 72 (17 ms)`,
on the plugin thread, once per boot. Line 12 "Route 2", 20 "Hau’oli City",
21 "Shopping District" match the in-game banners (RE4) and the user's dump
(`tests/map_test.cpp`, real-file check). The zone's name is line `nameLine`
(zone data `+0x1C`) and its second line is `nameLine + 1`, as RE4 found for
Hau'oli City; often empty. `’` (U+2019) is drawn by the system font.

If the RomFS can't be read, the map shows "Zone N" and logs why
(`map: place names: ...`).

## Island and inside / outside

From the whole zone table, dumped at runtime and read next to the place
names (docs/RE4-map.md, "Zone table, all 380 zones"):

- **Island**: zone data `+0x4E` is the zone's map, map index + 1: 1-4 are the
  four islands' zones (outdoor ones in contiguous blocks: Melemele 0-18, Akala
  80-92, Ula'ula 164-175, Poni 264-272; the Route 2 Pokémon Center has 1),
  5 is Aether Paradise and 6-21 single areas with their own map. The
  map uses it (falling back to the Rotom map view's index, RE4 `+0x170`) and
  names 0-3 Melemele, Akala, Ula'ula, Poni Island; other maps get no island
  name. At runtime only Melemele (value 1, Rotom index 0) was seen. The
  island names are written in the plugin (like the X menu's labels).
- **Inside / outside**: only those 56 outdoor island zones lack a fixed map
  point; every other zone (buildings, caves, trial sites) has one. So the
  map's "Inside" means "not on the island's open map": in a cave it says
  Inside too, which is also how the Rotom map behaves (fixed point).

## Zone changes

The map reads the field (`ReadWhere`) every 4 frames while it shows. Right
after a door, the field work's zone reads 0 for a few frames while the
player still has the old position and the entry zone (`location + 0x0A`)
already says the new zone. Logged with a trace build (instance p25):

```
map: trace zone 0 entry 7 valid 1 x 2162 z 2769 mapView 00000000 centre 0 0
map: trace zone 0 entry 7 valid 1 x 2162 z 2769 mapView 31846D44 centre 12579 6323
map: trace zone 0 entry 7 valid 1 x 2162 z 2769 mapView 31846D44 centre 12579 6323
map: trace zone 7 entry 7 valid 1 x 12600 z 6389 mapView 31846D44 centre 12600 6389
```

Settled readings always had zone == entry zone (here and in RE4, including
the seamless walk into Hau'oli City). So `MapZoneFilter` takes a new zone
after 3 readings in a row that agree with the entry zone, or after 30
readings (~4 s) whatever the entry zone says; anything else is logged as
`map: zone N not settled, ignored`. Without it the map showed "Route 1" for a
moment at the Pokémon Center door.

## Keys and other screens

| Key | Does |
|---|---|
| ZR (`map_key` in `usum-onescreen.ini`, `none` to turn it off) | opens / closes the map in the field |
| B | closes the map (captured: the game doesn't see it) |
| X | the game's X menu; the native X menu shows and the map closes |
| L+R | the panel as usual; turning it on closes the map |
| circle pad, A | the game's: the player can walk and talk with the map up |

ZR doesn't clash with L+R (panel) or X (X menu), and the plugin already uses
ZL. That the game itself does nothing with ZR is only observed, not checked
in its code: in the scenario, ZR with `map off` changed nothing (the scene
stayed `FieldRo`, the screenshot shows the field, and the X menu then opened
normally). While the map shows, B and map_key are captured
(`InputCaptureBegin`); a capture is only ended if it is still the map's, so
the X menu's own capture survives the frame both change.

The map opens only in the field (scene `FieldRo`), not in the X menu (field
state 4) and not while an action runs. It closes on another scene (battle,
Pokédex...; a `""` transition keeps it), the X menu, or the panel turned on
by hand. While it shows, the panel is held off with AutoPanel's manual
override, as the X menu does.

**X menu → Map**: the entry now runs `xmenu-close` (B, wait for the field
without the X menu) and asks the map to open (`MapRequestOpen`, waits up to
90 frames for the field). With `map off` the entry opens the game's Town Map
as before (`xmenu-map`).

## Commands and log

`map on|off` (default on; off closes it and gives the X menu's Map back to
the Town Map), `map open` (as ZR), `map close`, `map status`. Log lines:

```
map: place names: 234 lines from a/0/3/2 file 72 (17 ms)
map: open (ZR)
map: shown zone 79 "Route 2", indoors, Melemele Island, x 2176 z 2523 facing SW, picture
map: drawing
map: zone 0 not settled, ignored
map: now zone 7 "Route 2", outdoors, Melemele Island, x 12601 z 6390 facing S, picture
map: closed (B)            # or (ZR), (X menu), (scene Battle), (panel turned on by hand), (map off)
map: hidden
```

## Tests

- `tests/map_test.cpp`: RomFS lookup (level 3 and IVFC images, sibling
  walk, missing paths), GARC entries, message decoding (tags, damaged
  files), the real place names when the user's `a/0/3/2` is present
  (`PLACE_GARC=...`), the view and its log line, compass and island names,
  the keys, the zone filter (the door sequence above), the copy's alignment
  and drawing (writes `analysis/private/map-preview.ppm`).
- `scenarios/native-map.txt` (instance p25): ZR inside the
  Route 2 Pokémon Center (zone 79, indoors), B closes, ZR again, a step east
  (x 2176 → 2371, facing E), out of the door with the map up (zone 7, Route 2,
  outdoors, centred on the player, no "Route 1" flash), a step down the path
  (z 6390 → 6574), ZR closes, X menu → Map opens it, X closes it for the X
  menu, `map off` → ZR does nothing and X menu → Map starts the Town Map
  action (`xmenu-map`). `check-save` at the end. One run failed there: Rotom
  had started a chat on the bottom screen, so the Town Map tap landed on the
  chat (as in docs H2); the scenario now only requires the action to start
  and backs out of the Town Map if it opened. Last run: PASS in 91 s,
  `xmenu-map ok in 89 frames, scene TownMap`.
- `scenarios/native-xmenu.txt` (updated for the Map entry): PASS, 190 s.

## Not done / open

- Walking back into the Pokémon Center from Route 2 with injected circle-pad
  input stopped at the door (facing north, position unchanged) in three
  tries, so the scenario walks inside → out → along the path rather than out
  and back in. A different place name is not exercised by the scenario
  (Hau'oli City is ~10 000 units down Route 2 through tall grass, and wild
  battles interrupt the walk); RE4 verified zone 13's name line.
- Islands other than Melemele, and interiors other than the Route 2 Pokémon
  Center, were not visited at runtime; the island and inside rules rest on
  the zone table (see "Island and inside / outside").
- Running (holding B) with the map up isn't possible: B closes the map.
