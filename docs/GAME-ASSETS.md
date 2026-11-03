# The game's own font and icons at runtime (W6-3)

The plugin draws its menus with Pokémon Ultra Sun's own font and icons,
read from the running game's RomFS when they are needed. The repo has no
game data: only the formats and the paths below. Code:
`Includes/UI/GameAssets.hpp`, `Sources/UI/GameAssets.cpp` (pure decoders,
host-tested on synthetic data), loaded once and shared by
`Includes/UI/Assets.hpp`, `Sources/UI/Assets.cpp` (W7-0, below).

## The shared service (W7-0)

Every native screen reads the same set; nothing loads it twice.

- **When**: the first frame the scene is `FieldRo` (after the game's own
  boot loading, so boot to the field isn't slowed), or earlier when a
  screen calls `UI::RequestAssets()` (`uitheme on` / `uitheme load` do).
- **Where**: a CTRPF `Task` (its own thread), so neither the game nor the
  plugin thread waits the ~0.6 s. The plugin thread (`UI::AssetsUpdate`,
  called from `main.cpp` every frame) writes the load's log lines when it
  ends.
- **API** (any thread): `UI::AssetsStatus()` (`NotStarted`, `Loading`,
  `Ready`, `Failed`), `UI::Assets()` (the set, `nullptr` until `Ready`),
  `UI::GameFontOrNull()` / `UI::HaveGameFont()`, `UI::AssetIcon()`,
  `UI::AssetBall()`, `UI::AssetMove()`, `UI::AssetMoveName(move, fallback)`,
  `UI::AssetTypeName(type, fallback)`, `UI::AssetsSummary()`. Until
  `Ready` they return the fallback, so a screen draws its fallback look and
  switches when the set arrives (check `UI::AssetsReady()` per frame and
  redraw on the change, as `Screens/ThemeScreen.cpp` does). `Theme::`
  drawing picks the game font and icons by itself (`GetGameAssets()` is
  published by the service).
- **Log**: `assets: loading (field reached)`, the per-part lines below,
  then `assets: ready, loaded in N ms, N KB kept (heap +N KB), game font on`.
  "kept" is computed from the parts; "heap" is newlib's `mallinfo()` in-use
  bytes after the load minus before (other threads may allocate meanwhile).

## How it is read

As for the map's place names (`docs/MAP.md`):
`FSUSER_OpenFileDirectly(ARCHIVE_ROMFS, empty path, binary {0, 0, 0})`
gives the running title's RomFS at IVFC level 3; `RomfsFind` walks its file
tree and `GarcEntry` finds an entry of a GARC (`Sources/RomfsText.cpp`).
Everything is read once, in the service's Task, and kept.

Measured in Azahar (instance `w63`, 2026-11-02, USA v1.2 with the v1.2
update installed):

```
assets: font: a/0/2/5 entry 0, cell 15x19, line 19, 768 glyphs kept, 129 KB
assets: icons: 8 of 8
assets: balls: 16 of 16 from a/0/6/1
assets: moves: 729 from a/0/1/1
assets: text: 18 type names, 729 move names from a/0/3/2
uitheme: assets loaded in 657 ms
```

Kept in memory: the font 129 KB (logged), ball icons 64 KB (16 x 32x32
RGBA), the other icons ~22 KB, the move table 4 KB, names ~25 KB: about
250 KB (computed from the sizes, not measured on the heap). While loading,
the largest temporary buffer is one decompressed layout archive (285 KB,
`a/1/3/9` entry 10).

Fallback: each part that fails is logged (`assets: ...: not loaded
(why)`) and stays empty; the theme then draws with the system font and its
own shapes (`Theme::UsingGameFont()` is false), so a failed load never
breaks a screen. `uitheme status` lists what loaded.

## Containers

- **GARC** (`CRAG`): `docs/RE2-text.md`; entries are read with their first
  variant (language variants share a layout's icons).
- **LZ11**: most GARC entries start with `0x11` and a 24-bit size (or 0
  and a 32-bit size). `Lz11Decompress`.
- **ALYT / SARC**: a layout archive (`ALYT` header) holds a `SARC` with
  `blyt/*.bflyt`, `anim/*.bflan` and `timg/*.bflim`. SARC: header (0x14),
  `SFAT` (node count, nodes of {name hash, 0x01000000 | name offset / 4,
  start, end}), `SFNT` (names), data at the header's data offset.
  `SarcFind` looks files up by name.
- **Two-letter packs** (`WD`, `BL`, ...): magic, u16 count, u32 offsets
  `[count + 1]`. `MiniPackEntry`.

## Font: `a/0/2/5`, entry 0

`a/0/2/5` holds 15 `FFNT` fonts (BFFNT, version 4). Entry 0 is the menu
font (Latin and Japanese, 11 575 characters). FFNT layout used:

- Header: `FFNT`, BOM `FEFF`, u16 header size (0x14).
- `FINF` at 0x14: type, height, width, ascent, u16 line feed, u16
  replacement glyph, default widths (s8 left, u8 glyph width, u8 advance),
  encoding, then u32 offsets (+8 past each block's start) of `TGLP`,
  `CWDH`, `CMAP`.
- `TGLP`: u8 cell width, cell height, sheet count, max width; u32 sheet
  size; u16 baseline, sheet format, cells per row, cell rows, sheet width,
  sheet height; u32 sheet data offset. USUM: 15x19 cells, baseline 15, four
  1024x1024 A4 sheets (512 KB each), 64 x 51 cells per sheet.
- Sheets: A4, 8x8 tiles row-major, Morton order inside a tile, low nibble
  first; cell (c, r) at (1 + 16c, 1 + 20r). Same as the system font
  (`docs/UI.md`), so `DecodeGlyphCell` decodes them.
- `CWDH`: u16 first, last glyph, u32 next, then {s8 left, u8 glyph width,
  u8 advance} per glyph.
- `CMAP`: u16 first, last code, u16 method (0 direct: first glyph index;
  1 table: u16 per code, `FFFF` = none; 2 scan: u16 count, {u16 code, u16
  index} pairs), u16 reserved, u32 next.

The Latin glyphs sit in the first rows of sheet 0 (checked on the dump:
every printable Latin-1 character is mapped, all below glyph 191; `’`
448, `…` 458, `♀` 605, `♂` 606), so `GameFont::Load`
reads only the map, the widths and the top 12 cell rows (768 glyphs) of
sheet 0 in one read: 129 KB instead of 2.1 MB. A character outside them
draws as the font's replacement glyph. Text is spaced by the width table
(left bearing, glyph width, advance), not by the ink as the system font
renderer does (`docs/UI.md`); this font's table matches its glyphs.

The other fonts in `a/0/2/5` (not used): entry 1 a 6-sheet variant with
more characters, 2 another 4-sheet variant, 3 and 5 smaller cells (13x15,
14x15), 4 a 24x24 symbol font, 6 a per-language 13x11 font with 31 symbols.

## Textures: BFLIM

A `FLIM` footer in the last 0x28 bytes: `FLIM`, BOM, header size 0x14,
version, file size, then `imag`: u16 width, height, alignment, u8 format,
u8 flags, u32 data size. The texture is the bytes before the footer, 8x8
tiles in Morton order at power-of-two size. Formats decoded (`DecodeFlim`):
L8, A8, LA4, LA8, RGB565, RGB8, RGBA5551, RGBA4, RGBA8, ETC1, ETC1A4, L4,
A4 (not HILO8). **Flag 4** in the flags byte means the texture is stored
transposed: a `w` x `h` image is a `pow2(h)` x `pow2(w)` texture and image
pixel (x, y) is texel (texW - 1 - y, x). Every icon below has it.

## Icons

| Icon | Where | File | Format |
|---|---|---|---|
| A, B buttons, red cursor | `a/1/0/4` entry 0 (LZ11 ALYT) | `timg/ButtonIcon_A.bflim`, `ButtonIcon_B`, `Common_Cursor_02` | LA4 16x16, LA4, RGBA4 16x16 |
| X, Y buttons, move categories | `a/0/6/6` entry 0 (LZ11 ALYT) | `timg/ButtonIcon_X.bflim`, `ButtonIcon_Y`, `waza_icon_all` | LA4 16x16, RGBA8 64x64 |
| Gender signs | `a/1/3/9` entry 10 (LZ11 ALYT) | `timg/Icon_Gender_M.bflim`, `Icon_Gender_F` | LA4 12x12, white (the game tints them) |
| Poké Balls | `a/0/6/1` entries 0-15 (LZ11 BFLIM each) | item icons | RGBA5551 32x32 |

- `Common_Cursor_02` points down; the game turns it to point right, and so
  does the theme.
- `waza_icon_all` holds the three category icons stacked 20 px apart
  (status, physical, special), each about 42x20 (`CategoryIconRect`).
- `a/0/6/1` holds 769 item icons; entry = item ID - 1 for the first items
  (entry 0 Master Ball, 3 Poké Ball, 15 Cherish Ball, 16 Potion), checked
  by decoding entries 0-39 and in the demo. Higher item IDs were not
  mapped (USUM has ~960 items and 769 icons, so the game uses a table).

## Data

- **Move table**: `a/0/1/1` entry 0 is a `WD` pack of 729 entries (one per
  move ID), each starting: type, (quality), category, power, accuracy, PP.
  Checked: Scratch (10) Normal, physical, 40, 100, 35 PP; Fire Fang (424)
  Fire, physical, 65, 95, 15 PP. `DecodeMoveEntry`.
- **Type and move names**: message files 112 and 118 of `a/0/3/<lang>`
  (`docs/RE2-text.md`), read with `ReadMessageFile`.
- **Colours** in the theme (move buttons per type, HP gauge) were read once
  from the battle layout's animations (`a/0/6/7` entries 8 and 9, `FLAN`
  files, `pai1` → `FLVC`/`FLMC` keys) and are written as numbers in
  `Theme.cpp`; the plugin doesn't parse animations at runtime.

## Looked for, not found

- **Type icons with names** (the "FIRE" plates): every layout holds only
  `type_icon_00_normal.bflim` (50x20 RGBA4, the name baked in, per
  language). No other 50x20 texture exists in any GARC entry of the base
  RomFS or the update (searched all entries, LZ11 and `BL` packs included).
  In RAM dumps (linear heap, heap, VRAM) taken while the summary showed
  "NORMAL" and "FIRE", the English Normal plate's bytes were not found; the
  only 50x20 BFLIMs there were the layouts' Japanese placeholders (Normal
  and Paralysis).
  Where the game gets them from is not known. The theme draws type plates itself
  (`Theme::DrawTypePlate`).
- **Status icons**: the same, only `sick_icon_01_mahi` (paralysis) is in
  the layouts.
- **Bag pocket icons**: `a/0/6/5` (the Bag layout) has `Bag_Icon_01b..g`
  (24x24 ETC1A4) and others; which is which pocket was not worked out, so
  they aren't loaded yet.
- **L, R, START button icons**: only `ButtonIcon_R` (in `a/1/0/1`) and
  `ButtonIcon_CursorKey` exist; not loaded.

## Tools (local, not in git)

The research scripts (GARC/SARC/BFLIM/FFNT/FLAN readers in Python) are in
`analysis/private/w6-3/tools/` of the `w6-3` worktree and their outputs in
`analysis/private/w6-3/`; they
read the user's own decrypted dumps (`tools/instance.py`'s `ROM` path).
