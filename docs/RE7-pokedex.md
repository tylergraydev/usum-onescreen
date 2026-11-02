# RE7: the Pokédex (Ultra Sun USA v1.2)

Where the game keeps the Pokédex's seen and caught flags, which dexes are open, and what the Pokédex app (`Zukan` CRO) shows: its current screen, list, cursor and records. Found on 2026-11-02 in the isolated instance `w52` (start point `pokecenter`, the early Route 2 save: 22 caught, 38 seen, Melemele open) and checked against the game's screens. The native Pokédex (`Sources/Screens/Pokedex.cpp`, `docs/POKEDEX.md`) reads all of it; `pokedex rows` prints the decoded lists.

## Seen and caught: `Savedata::ZukanData`

```
save   = *( *( *(0x6A59C4) + 0x24) + 0x04)     Savedata::SaveData, vtable 0x64F4A0 (docs/RE3-party-bag.md)
zukan  = save + 0x23DC                          Savedata::ZukanData, vtable 0x64F550; +4 magic 0x2F120F17
```

| Offset (from the object) | Size | What |
|---|---|---|
| `+0x8C` | 0x68 | caught: bit `species - 1` |
| `+0xF4` / `+0x180` / `+0x20C` / `+0x298` | 0x8C each | seen: male / female / shiny male / shiny female |

Seen bits are per form. Form 0 of a species is bit `species - 1`; the extra forms come after bit 806 (`0x326`), allotted in the order of the game's form table at `0x5D63DC` (123 × `{u16 species, u16 forms}`, read through the literal at `0x4C8068`): for each table entry, its forms 1..n-1 take the next n-1 bits. The game's functions (decompiled with the Zukan CRO and code.bin):

- `0x4C7A7C` caught(species): the bit in `+0x8C`.
- `0x4C7B7C` seen(species): for each form (`0x4C7664` form count, 1 if the species isn't in the table), `0x4C76C8` tests the form's bit (`0x4C7FD0`) in each of the four seen regions.

So a species seen only in another form (Alolan Rattata, Meowth, Grimer on this save: bits 813, 830, 837) is seen although its own bit is clear. The plugin applies the same rule (`DexStateOf`, tested in `tests/pokedex_test.cpp`). Checked: 22 caught and 38 seen, the counts on the game's top-level rows; all 403 Alola rows agree with the app's own state byte (below) except two caught entries the app marks 5 (meaning unknown).

## Which dexes are open: event flags

The top level greys out island rows from Poni upwards until one is open, and shows Evaluation only with a flag (the frame's setup at `0x6E6870` = Zukan `+0x7870`; checks `0x6F8CD8` / `0x6F90F0`, all with the CRO at `0x6DF000`). Flags are the event work at SaveData `+0x15D8`, bits at `+0x7D4` (`0x4BCEE0`, flags below `0x1340`):

| Flag | Row |
|---|---|
| `0x101F` | Melemele Pokédex |
| `0x1020` / `0x1021` / `0x1022` | Akala / Ula'ula / Poni Pokédex |
| `0xFE7` | Pokédex Evaluation |

This save: `0x101F` and `0xFE7` set, the other three clear, as the screen shows (Melemele open, three blank rows). Akala and later were not reachable, so a set island flag was only seen for Melemele.

## The app (`Zukan` CRO)

The Zukan CRO loads at `0x6DF000` here (`Zukan` in the scene name, `docs/OFFSETS.md`); offsets below are from the module header (`CroModuleOf`).

**Current frame** = scene manager `+0x08` (`*(*(0x6A599C) + 8)`). Its class says what is up:

| Frame vtable | Screen |
|---|---|
| Zukan `+0x34CD4` | top level (Alola, the four islands, Evaluation) |
| Zukan `+0x34DD8` | Alola Pokédex list |
| Zukan `+0x34F68` | island Pokédex grid (15 columns of icons) |
| Zukan `+0x34C24` | entry page |

Seen in all four screens at the same heap address (the slot is reused). Other screens (habitat, appearance, search, Evaluation's results, registering a catch) were not mapped; the plugin treats any other class as "other".

Top-level frame: `+0x134` u32 cursor row (0 Alola .. 4 Poni, 5 Evaluation; checked 0, 1, 5 by screenshot), `+0x19D` u8 state: 7 menu up, 0x10 / 0x11 Evaluation (choosing a dex / Rotom talking), others while a list opens or closes.

**Shared data** = frame `+0xA4` (the same object from every frame above; the frames carry a copy of the app's parameter block at `+0x80`), vtable Zukan `+0x34C14`:

| Offset | What |
|---|---|
| `+0x11` | u8 dex shown: 0 Alola, 1 Melemele (2-4 Akala, Ula'ula, Poni: not reachable) |
| `+0xA0` | u16 record count (403) |
| `+0xA4` | records (below) |
| `+0xAC` | u32 rows in the Alola list: the highest Alola number seen (126, Rockruff, on this save) |
| `+0xB4` | u32 cursor: index in the current list or grid; on an entry page, the entry shown (Up/Down there go to the next seen entry and move it) |

Checked by heap dumps at cursor 0 / 3 / 5 in the list and 0 / 2 / 17 in the grid, then live: the list's own PaneList (`+0x18` cursor row, `+0x1C` scroll as float, under the `app::tool::PaneListView` at the top of the scene manager's stack) agrees.

**Records**: 403 × `0x1F0` bytes, in Alola order. `+0` u16 species, `+2 .. +0xA` u16 number in Alola, Melemele, Akala, Ula'ula, Poni (0 = not in that dex), `+0xE` u8 state (0 unseen, 2 seen, 3 caught; 5 on two caught entries). The app fills them over the first frames after it opens (species 0 until then). The plugin takes the order and numbers from here and seen/caught from the save flags. Island orders match the sort tables the app loads (`PokeTool::PersonalSort`, vtable `0x64EF0C`, at `+0x88` of the object at the param block's `+0x20` (vtable Zukan `+0x3570C`); its `+0x109C` → a "BL" pack of 11 files: national, Alola, the four islands, four more sorts, evolution families); island sizes 150 / 160 / 160 / 130.

Names: species names from the resident message file (`docs/RE2-text.md`, `SpeciesName`).

## Keys (the game's own)

| Screen | Key | Does |
|---|---|---|
| top | Up / Down | moves; locked rows are skipped |
| top | A | opens the row; B leaves the Pokédex (to the field or the X menu) |
| top, list | Up / Down | wrap around at both ends |
| list | Up / Down (held: repeats) | moves; L / R do nothing; the list reopens where it was left (on #001 the first time) |
| grid | D-pad | moves in the 15-column grid |
| list, grid | A | opens the entry (seen ones); B back to the top |
| entry | Up / Down | previous / next **seen** entry |
| entry | A / Y / X | Habitat / Appearance and cry / QR code (not mapped) |

An injected A right as the list opens can be taken twice (the list, then the first entry): the scenario waits after each press.

## How it was found

1. Heap dumps (`dump 30000000 4184000`) with the list cursor at 0, 3 and 5, then 0, 2 and 17 in the grid; diffs for those values found the PaneList fields and the shared object's `+0xB4`; its block led to the records and the PersonalSort.
2. RTTI: `N8Savedata9ZukanDataE` → vtable `0x64F550` → the object at SaveData `+0x23DC`; the magic `0x2F120F17` led to the ZukanData methods (`find_refs.py`), decompiled with `tools/ghidra/decompile.sh`.
3. The Zukan CRO was dumped from RAM (`dump 6DF000 41000`, relocations applied) and appended to code.bin at its load address for Ghidra; the top-level frame's setup showed the row checks and the event flags.
4. Every value was then read live by the plugin (`pokedex` log lines) and compared with screenshots in `scenarios/native-pokedex.txt`.

Dumps, decompiler output and screenshots are in `analysis/private/` (local only).

## Gaps

- Akala, Ula'ula and Poni are locked on the only save: their grids, flags set, and `+0x11` values 2-4 are inferred, not seen.
- The state value 5 in the records is unexplained (the plugin doesn't use it).
- Entry-page details (category, height, weight, description, types) are on the bottom screen only and were not looked for.
- Habitat, appearance, search, Evaluation's dex choice, and the catch registration screens are not mapped.
