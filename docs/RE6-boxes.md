# RE6: PC boxes (Ultra Sun USA v1.2)

Where the PC boxes live, how the open Box screen keeps its cursor, box and
held Pokémon, and how it was found. Checked at runtime on 2026-11-02 in the
isolated instance `p26` (start point `pc`, Route 2 Pokémon Center), against
screenshots of the Box screen, through: cursor moves over every slot, the
party, the box name and the box list button; box changes with the arrows
(including Box 1 → Box 8); picking up and dropping (empty slot, swap, party →
box, box → party, box → another box while holding); the slot menu opened and
cancelled; leaving the PC with B. The `box [N]` command (`Sources/Box.cpp`)
prints it:

```
box: 1 "Box 1", 8 of 32 unlocked (BoxPokemon 33015AAC)
   0 Buneary(427)
   1 Ledyba(165)
  ...
  21 of 30 slots used
  box screen: box 1, mode idle, cursor 0, draw 3007A88C
  box screen party: Torracat(726) L17 Zorua(570) L14 Pikipek(731) L13 Pichu(172) L12 Ledyba(165) L13 Buneary(427) L12
```

Heap addresses are examples: the save data's were the same in every run here,
the Box screen's objects move each time the PC opens (`BoxLowBgDraw` was at
`3007A88C` and `3007A64C`), so the code follows the pointers every time.

## Box data (save data, every scene)

Both blocks are members of `Savedata::SaveData` (RE3: `*(*(0x6A59C4) + 0x24)
+ 4`, vtable `0x64F4A0`), constructed in place, so their offsets are fixed:

| SaveData offset | Object | vtable (RTTI) | Data (from object +4) |
|---|---|---|---|
| `+0x3B98` | `Savedata::BOX` | `0x64F364` (`N8Savedata3BOXE`) | `0x5E0` bytes = the save file's block at `0x4C00` |
| `+0x4184` | `Savedata::BoxPokemon` | `0x64F044` (`N8Savedata10BoxPokemonE`) | `0x36600` bytes = the save file's block at `0x5200` |

Both were compared byte for byte with the `pc` start point's save file (equal
before any move).

**BoxPokemon:** 32 boxes × 30 slots × `0xE8` bytes, box by box, slot by slot
(slot = row × 6 + column). Each slot is a PK7 core in the stored (encrypted,
shuffled) format, as in RE3; there are no party stats (no level or HP: the
level would need the experience tables). An empty slot is all zeros, which
decodes with checksum 0 and species 0. `DecodePk7(core, nullptr)` reads them
(`ReadBoxMon`).

**BOX** (offsets into the data, i.e. object + 4):

| Offset | What | Seen |
|---|---|---|
| `0x000` | 32 box names, `0x22` bytes each (UTF-16, 0-terminated) | `Box 1` … `Box 32` stored as text |
| `0x440` | 6 team names (`Team 1` …) | not used |
| `0x4C4` | team slots (`0xFF` = none) | not used |
| `0x5C0` | 32 wallpaper bytes | `00..0F 00..0F` |
| `0x5E1` | u8 boxes unlocked | `8`: the left arrow on Box 1 goes to Box 8 (tray 7) |

What an empty name shows was not seen (every name is stored here); the
plugin shows `Box N` then.

**When the data changes:** a drop writes BoxPokemon at once (the slot
data moved in memory the moment the held Pokémon was put down: Buneary slot
0 → 21, Torracat into slot 1). While a Pokémon is held its data stays in its
old slot. Nothing reaches the save file until the game is saved; leaving the
PC with B asked nothing on this save (no "save?" question), and the real
save's time stayed 2026-02-08 16:52:50.

## The Box screen (scene `Box`)

The Box CRO (`Box`, loaded at `0x6DF000` here; the `RaderChart` CRO comes with
it) owns the screen. RTTI names in the module give the classes; offsets below
are vtable addresses minus the module's load address.

**Finding it:** the scene manager (`*(0x6A599C)`) holds the screen's views
in its first eight words (`+0` is the scene object SceneModule() names; here
`BoxUppPokeStatusDraw`, `BoxUppBgDraw`, `BoxUppFolderDraw`,
`app::tool::MenuWindow`, `BoxLowFolderDraw`, `BoxLowBgDraw`). The plugin
looks for the word whose object has vtable module + `0x2F4D8`
(`App::Box::BoxLowBgDraw`).

`App::Box::BoxLowBgDraw` (the bottom screen's box view):

| Offset | What |
|---|---|
| `+0x8C` | the input listener in charge: its vtable gives the mode (below) |
| `+0x90` | → `App::Box::BoxLowBgFrame` (+`0x18` sub-object) |
| `+0xC0` | → the Box app's parameter block |
| `+0xC4` | → { → work, … } |
| `+0xDC` | u32 cursor (`+0xE0` a copy) |

**Mode** (listener vtable, module offset):

| Listener | Offset | Mode | Seen |
|---|---|---|---|
| `BoxLowBgFrame::DInnerNormalListener` | `0x2F6F0` | idle (browsing) | every rest state |
| `BoxLowBgFrame::DInnerNoneListener` | `0x2F6B0` | modal: the slot menu, or a message | menu after A / a tap on a Pokémon |
| `BoxNormalState::DInnerPokeMoveListener` | `0x2FA80` | holding a Pokémon | after Move, until dropped |

Between modes (e.g. ~8 frames after a drop) the listener is briefly none of
these.

**Cursor:** 0-29 box slots (row × 6 + column), 30-35 the party (left column
30, 32, 34, right column 31, 33, 35, top to bottom), 36 the box name, 38 the
box list button below the box. Moves seen: right from column 5 goes to 30
(row 0-1) and on to 31; up from row 0 to 36, from 36 to 38, from 38 to row 4;
down from 36 to row 0. 37 was not seen.

**Parameter block** (no vtable; a heap word at `0x33F681E0` also points to
it, not followed up): `+0` → BOX, `+4` → BoxPokemon, `+8` → `ResortSave`,
`+0xC` → `ZukanData`,
`+0x10` → MyItem, `+0x14` → the real party, **`+0x60` u32 the box on screen**
(0-based; 0 → 1 → 2 with the right arrow, 0 → 7 with the left). The plugin
checks `+0` and `+4` against the save data before trusting it.

**The screen's party:** like the Party screen (RE3), the Box screen works on
its own copy and the real party changes when the PC closes. The work object
(`*(*(draw + 0xC4))`, its `+0` is the parameter block) holds six
`PokemonParam*` at `+0x1C`; the screen compacts it as Pokémon leave (Zorua
moved out: `Torracat Pikipek Pichu Ledyba Buneary` + an empty param with a
zero core). After leaving the PC the real party had the same order. The same
work object has the last moved Pokémon at `+0x8` and the last drop / pick
positions at `+0x50` / `+0x58` (seen once each, not used).

## The bottom screen

Taps (all in `layouts/usum-v1.2.json`, screens `box` and `box_menu`):

- A tap on a slot moves the game's cursor there; on a Pokémon it also opens
  the slot menu (Move, Summary, Held Item, Marking, Release, Cancel; the
  game asks "What do you want to do with Ledyba?"). Checked for all 36 slots
  (cursor read back after each tap).
- Move picks the Pokémon up (mode holding). While holding, a tap on a slot
  drops it there; on another Pokémon the two swap places.
- The arrows beside the box name turn the box, also while holding. L and R
  do **not** turn the box (pressed in idle: nothing).
- B leaves the PC from idle (straight to the field).

## How it was found

1. The `pc` start point's save file was scanned for PK7 cores with good
   checksums: party at `0x1600`, boxes at `0x5200`; box names as text at
   `0x4C00`.
2. A heap dump with the Box open: the save's box bytes appear once at
   `0x33015AB0`, the names at `0x330154C4`; the vtables just before them name
   `Savedata::BoxPokemon` and `Savedata::BOX`; both lie inside the SaveData
   object at `0x33011928`.
3. Cursor: heap dumps after D-pad moves (cursor 2, 8, 9), searched for words
   equal to the index in all three: `BoxLowBgDraw +0xDC/+0xE0` and
   `BoxLowBgFrame +0x108`; then read live (`peek`) for every slot, the party,
   the box name and the box list button.
4. Mode: dumps idle / menu / holding / idle again, compared word by word over
   `BoxLowBgDraw`: `+0x8C` switches between three listener objects.
5. Box on screen: dumps on Box 1, 2, 3 (arrow taps); one word in the heap
   equals 0, 1, 2: parameter block `+0x60`.
6. Screen party: after a party → box move the real party was unchanged; the
   party members' encryption constants were searched in a dump and found in
   six PokemonParams listed by the work object.

## Confidence

- High: box data location and format, names, unlocked count, cursor values,
  the three modes, the box on screen, the screen's party, every tap.
- Not seen: what the game shows for an empty box name; cursor value 37; other
  Box modes (box list, Summary, Held Item, Marking, Release, the battle team
  and other tabs at the top) read as an unknown listener; eggs in the box;
  the "last Pokémon" refusal (the party never got that small).
- Other builds: all addresses are USA v1.2.
