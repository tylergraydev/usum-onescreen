# RE1: battle state (Ultra Sun USA v1.2)

What the native battle UI (P2-1) needs to read, where it lives, and how it was found. Everything here was checked at runtime in an isolated Azahar instance on 2026-11-02, in six wild battles on Route 2 (two instance restarts): Drowzee Lv10, Smeargle Lv7, Yungoos ♀ Lv8, Yungoos ♂ Lv9, Makuhita Lv10 (final build). The `battle` command (`Sources/Battle.cpp`) prints all of it; the plugin also logs it on every change while the scene is `Battle`.

```
battle: menu moves (30FDB930 vt 0010B724) cursor 1 of 4 [type/eff 0/1 9/1 1/2 7/0]
  player 30002768 sp 726 L17 hp 51/51 ab 66 party 6 moves 10:35/35 424:15/15 24:30/30 122:30/30
  opponent 30004DA8 sp 734 L8 hp 27/27 ab 198 party 1 moves 33:35/35 43:30/30 228:20/20 0:0/0
```

`vt` is module-relative when the class is in a CRO (Battle or Bag), absolute when it is in the main image. `sp`/`ab`/move numbers are national dex / ability / move IDs (names: `docs/RE2-text.md`).

## Object paths

All heap addresses above are examples; the code follows pointers every time. CRO addresses come from the module header (`CroModuleOf` in `Sources/Scene.cpp`: walk back from a vtable to the page with `FIXD` at `+0x80`), never a fixed load address. In these runs Battle loaded at `0x6DF000` and Bag at `0x82A000`.

```
mgr      = *(0x6A599C)                         scene manager (docs/OFFSETS.md)
scene    = *(mgr + 0)                          vtable Battle+0x10B278; module name "Battle"
view     = *(scene + 0x94)                     vtable Battle+0x10A528 (checked); also at scene +0x170, +0xEA8
pokecon  = *(view + 0xD0)                      poke container; +0 = pointer shared with view +4
party[c] = pokecon + 0x04 + c * 0x1C           c = client: 0 player, 1 opponent (single wild battle)
           { u32 record[6]; u8 count (+0x18) }
active   = party[c].record[0]                  slot 0 is the battler on the field (see "Switching")
```

### Battler record (0x330 bytes apart in the pool, same layout for both sides)

| Offset | Type | Field | Checked against |
|---|---|---|---|
| `+0x0C` | u16 | species | Torracat 726, Zorua 570, Drowzee 96, Smeargle 235, Yungoos 734 |
| `+0x0E` | u16 | max HP | 51 (Torracat), 35 (Zorua), opponents 33 / 25 / 27 / 29 |
| `+0x10` | u16 | current HP | Torracat 51→45 after Drowzee's Confusion (screen 45/51); Drowzee 33→20; Yungoos 27→0 (fainted) |
| `+0x16` | u16 | ability | Blaze 66, Illusion 149, Insomnia 15, Technician 101, Stakeout 198, Strong Jaw 173 |
| `+0x18` | u8 | level | 17, 14, 10, 7, 8, 9 (screen) |
| `+0x1FC + i*0xE` | u16 | move i (4 slots, 0 = empty) | the move lists on screen |
| `+0x1FE + i*0xE` | u8, u8 | PP, max PP | Scratch 35→34, Fire Fang 15→14, Drowzee Confusion 25→24, Yungoos Leer 30→29 |

Each move slot is 0xE bytes: `u16 move, u8 pp, u8 maxPp, u16 ?, u16 move, u8 pp, u8 maxPp, u16 ?, u16 ?`. The second copy is presumably the original move (before Mimic/Sketch/Transform) and changed together with the first in every test; the reader uses the first. At `+0x1D8` there is a second species copy followed by what look like stats (not checked).

The opponent's HP is the exact value; the game shows wild Pokémon as a bar only, and the bar matched (Drowzee 20/33 ≈ 60 %).

### Switching

After switching Torracat out for Zorua, `party[0].record[0]` changed from Torracat's record (`0x30002768`) to Zorua's (`0x30002A98`, previously slot 1), so the battle party is reordered and slot 0 is the active battler. The Bag's party list showed Zorua first too.

**Illusion:** Zorua was displayed as "Buneary" (the last party member) on the top screen, while the record says 570 (Zorua). A UI that draws from the record would leak the illusion. The displayed identity is not yet located (see "Unknown").

## Which menu is showing

The scene manager keeps a stack of scene objects at `mgr + 0 .. + 0x1C` (null-terminated). In battle: `+0` scene (Battle+0x10B278), `+4` a main-image object (`0x648E38`), `+8` Battle+0x10B314, `+0xC` Battle+0x10B43C, then the menus are pushed on top. The top (last non-null) entry tells which bottom-screen menu is up:

| Top vtable | Menu | Cursor | Count |
|---|---|---|---|
| Battle+`0x10B43C` | none: animations, messages, between turns (`menu none`) | | |
| Battle+`0x10B598` | command menu | u8 `+0x278`: 0 Fight, 1 Pokémon, 2 Bag, 3 Run | 4 |
| Battle+`0x10B724` | move list | u8 `+0x114` | u8 `+0xA8` |
| main `0x648394` | Pokémon list | `p = *(top+0xA8)` (vtable `0x647F9C`), u8 `p+0x52` (party slot; drawn row by row: 0 1 / 2 3 / 4 5) | u8 `p+0x28` |
| Bag+`0x16E24` | Bag (Bag CRO) | see below | |
| Battle+`0x10BCD0` | Pokémon action screen (Switch / Restore / Summary) | not located (`menu other`) | |

The command object also holds the player and opponent records at `+0x24C` and `+0x270` (unused; the view path above is independent of the menus).

Move list entries (`top + 0xB0 + i*0x18`): `u16 move`, `u8 effectiveness` at `+2` (0 no effect, 1 effective, 2 super effective, 3 not very effective; matched the labels in two battles), `u8 pp` `+4`, `u8 maxPp` `+5`, `u8 type` at `+0x15` (0 Normal, 1 Fighting, 7 Ghost, 9 Fire: Gen 7 type order).

Bag: `child = *(top + 0x40) - 4` (vtable Bag+`0x16EB0`; `+0x40` is a list link into the child's `+4`), pocket = u8 `child+0xD8` (0 = leftmost tab), `view = *(child+0xCC)` (vtable `0x6488C8`, the main-image Bag class also used for the field Bag), `list = *(view+0x88)`, item count = `list+0x10`, cursor = `list+0x18`. Checked in two battles: pocket 0 (1 item), pocket 1 (4 items, cursor 0..3), pocket 2 (6 items).

## How it was found

1. Plugin commands `memmap` (readable blocks; the game heap is `0x30000000`-`0x34184000`) and `dump ADDR SIZE NAME` (raw memory to `/usum-dump-NAME.bin`).
2. Full heap dumps in battle, searched for Torracat's species/level/HP (`726`, 17, 51/51) and then diffed before/after a turn: only two records changed HP 51→45 and PP 35→34 (two copies; `0x30002768` and one at `0x30009750`, the second not used).
3. Backward pointer search (scratch script: every heap word that points into or near a target, iterated) from the records to the scene object: `scene+0x94 → +0xD0 → +4 → record`.
4. Cursor fields: dumps with the cursor on 3 different entries, searched for bytes going 0→1→2 (or 3→2→0), then the owning heap block (allocator header `0x5544` at block −0x30) and a pointer search from the menu object.
5. Every value was then read live with `battle` and compared with screenshots (`analysis/private/shots/`, local only).

## Confidence

- High: records, species/level/HP/ability/moves/PP for single wild battles; command / move list / Pokémon list / Bag cursors and move effectiveness/type; menu identification by vtable.
- Medium: slot 0 = active battler (seen with one switch); `party[1]` = the opponent (only single wild battles); the bag child link at `+0x40` (a list node, stable in all dumps but not understood).
- The heap addresses happened to be identical in every battle in these runs (`0x30002768`, `0x30004DA8`), so the pointer path was not exercised against a different layout. It is still the path the code follows, with vtable checks on the view, Pokémon-list param, bag child and bag view.

## Unknown, and how to find it

Found since (P2-1, see `docs/OFFSETS.md` "Battle state"): move names (the battle WordSet's move-name file, via the busy object `+0x94`), the Bag's item page flag (frame `+0x6E`), and the player's whole battle party (`party[0].record[0..count-1]`).

- **Move power / accuracy / category**: not in the battle record or the move list. They come from the move data table (romfs `a/0/1/1`, one file per move). Find the resident copy the same way as RE2's message files, or read it from romfs at boot. Type and effectiveness are already in the move list.
- **Double / multi / SOS battles**: `party[c]` for c = 2, 3 and positions 0/1 within a client are unverified. Check with an SOS battle (wild ally appears in client 1? or 3?) and a double battle; in a double, records 0 and 1 of the party should be the two active battlers.
- **Targets** (double battles): a target-select screen will be another stack object; find its cursor the same way (dump with the cursor on two targets and diff).
- **Z-moves / Mega**: the move list's Z button state and the Z-move list are not located; get a Z-Crystal, diff the move list object with Z toggled.
- **Illusion / displayed identity**: the top-screen HP boxes (Battle objects with vtable Battle+`0x10A3F4`, which point to the record at `+0xC`) are a good place to look for the displayed species and nickname.
- **Status conditions, stat stages, held item, gender, nickname**: not decoded. The record past `+0x18` (`+0x1A`..`+0x1D8`) is the place to look; diff before/after a status move (Leer → Defense stage, Hypnosis → sleep).
- **Pokémon action screen** (Switch/Restore/Summary) cursor, Bag item-use target, Run confirmation.
