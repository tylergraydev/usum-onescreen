# RE3: party and bag (Ultra Sun USA v1.2)

Where the player's party and bag live, how the plugin reads them, and how it was found. Checked at runtime on 2026-11-02 in the isolated instance `re3` (start points `pokecenter` and `grass`), against the Party and Bag screens, before and after: battle damage (wild Spearow Lv8, Torracat 51 → 47 HP, Scratch PP 35 → 34), a Potion (Torracat 47 → 51, Potion ×9 → ×8), two party reorders on the Party screen and giving Quick Claw to Ledyba. The `party` and `bag` commands (`Sources/Party.cpp`) print all of it.

```
party: 6 (33F7F9B8)
  1 33F80194 Ledyba(165) "Ledyba" L13 hp 37/37 status 0 item Quick Claw(217) moves 33:35 48:20 129:20 113:30 [enc]
  5 33F7FA04 Torracat(726) "Torracat" L17 hp 51/51 status 0 item - moves 10:34 424:15 24:30 122:30 [enc]
  party screen order (slot per position): 5 2 3 4 1 6      (only while the Party screen is open)
bag: MyItem 33011930
  Medicine 9/60: Potion(17) x8, Paralyze Heal(22) x5, ..., Burn Heal(19) x3 new, ...
  Items 9/427: Escape Rope(78) x3, Poké Ball(4) x37, ... | count 0: Quick Claw(217) Premier Ball(12)
```

`moves` are `move:PP` (move IDs; names are not resident, see "Gaps"). `[enc]` / `[dec]`: which stored form the core data was in (below). Heap addresses are examples: they were identical in every run here, but the code follows the pointers every time.

## Roots

```
manager  = *(0x6A59C4)                    GameSys::GameManager (heap)
gameData = *(manager + 0x24)              GameSys::GameData; ctor 0x4070E4, stored by 0x402360
save     = *(gameData + 0x04)             Savedata::SaveData, vtable 0x64F4A0 (checked); ctor 0x45BDFC
party    = *(gameData + 0x0C)             pml::PokeParty (0x1C bytes, player); +0x10 is a second, empty one
myItem   = save + 0x08                    Savedata::MyItem, vtable 0x64F3C4 (checked); ctor 0x45A370
```

`SaveData` also holds `Savedata::PokePartySave` at `+0xFB8` (vtable `0x64F194`): the serialized party for the save file (Torracat's encryption constant appears there too). It is not the live party; the reader does not use it.

## Party

`pml::PokeParty` (`0x1C` bytes): `PokemonParam* member[6]`, `u8 count` at `+0x18`.

`pml::pokepara::PokemonParam` (vtable `0x649374`, checked; stride `0x1E4` in the pool), a `CoreParam`:

| Offset | Field |
|---|---|
| `+0x04` | core data (`0xE8` bytes: the PK7 stored format) |
| `+0x08` | party stats (`0x1C` bytes: PK7 `0xE8..0x104`) |
| `+0x0C` | accessor (vtable `0x64939C`): `+4` stats, `+8` core, `u8 +0xC` 1 = encrypted, 0 = decrypted |

**Encrypted in RAM, yes.** The data rests encrypted exactly like a Gen 6/7 PK7 file: core `+0` encryption constant (EC), `+6` checksum (sum of the u16s of `+8..+0xE8`), then four `0x38`-byte blocks shuffled by `((EC >> 13) & 31) % 24` and XORed with the LCRNG stream `seed = seed * 0x41C64E6D + 0x6073; key = seed >> 16`, seeded with the EC. The party stats are XORed with a fresh stream from the same EC.

**How the game reads it:** the accessor decrypts in place (`0x322CB0`: XOR of core `+8` (`0xE0` bytes) and of the stats via `0x320BBC`, sets `+0xC = 0`, and on a checksum mismatch sets bit 2 of the core's `+4` word, the bad-egg flag). Setters work on the decrypted data and re-encrypt (`0x322BE0` / `0x322D30`: new checksum `0x320B5C`, XOR, `+0xC = 1`). The blocks are never unshuffled in RAM, so field getters must map block positions. The plugin never writes: `DecodePk7` (`Sources/PartyData.cpp`) copies the bytes, tries the encrypted form, then the decrypted (XOR-free, still shuffled) form, and keeps the one whose checksum matches; a slot read mid-update comes back `unreadable`. The checksum is a plain sum and cannot tell block orders apart, which is why "fully decoded" is not a candidate.

Decoded fields (PK7 offsets, after unshuffling):

| Offset | Field | Checked against |
|---|---|---|
| `0x08` | u16 species | all six (Torracat 726 … Buneary 427), Party screen |
| `0x0A` | u16 held item | Quick Claw 217 after giving it to Ledyba (item icon on the Party/Bag list); 0 before |
| `0x40` | u16[13] nickname | the default names shown on the Party screen |
| `0x5A` | u16 move ×4 | Torracat's battle move list (Scratch 10, Fire Fang 424, Double Kick 24, Lick 122) |
| `0x62` | u8 PP ×4 | Scratch 35 → 34 after one use (the move list showed 35/35 before) |
| `0x74` bit 30 | egg | not seen (no egg); synthetic test only |
| stats `+0x00` | u32 status condition | always 0 here: **mapping not checked** |
| stats `+0x04` | u8 level | 17 14 13 12 13 12 (Party screen) |
| stats `+0x08` / `+0x0A` | u16 HP / max HP | 51/51 → 47/51 after the battle → 51/51 after the Potion (Party and Bag screens) |

Max PP is not stored (it comes from move data plus PP Ups). The PK7 status word layout used by other tools (sleep turns in bits 0-2, then poison, burn, freeze, paralysis, toxic) was **not verified**; the reader prints it raw.

### When changes reach the party

- **Battle:** the battle engine works on its own records (`docs/RE1-battle.md`). During the battle `party` still showed 51/51; damage and PP reached the party when the battle ended (Run).
- **Bag (field):** a Potion wrote the party at once: Torracat 51/51 while the Bag scene was still open, as the "restored by 4 points" message showed. The Bag count went ×9 → ×8 in memory when that message was dismissed, at the same moment the top screen changed.
- **Party screen:** moving Pokémon (Y, A, A) does **not** touch the party while the screen is open. The screen shows the party through an order array, and the party's member pointers are swapped when the screen closes (checked twice: Torracat ↔ Zorua, Torracat ↔ Ledyba). The plugin reads that order while the screen is up:

```
scene    = *(*(0x6A599C) + 0)                 vtable 0x647FCC (Party scene)
work     = *(scene + 0xA8)                    App::PokeList::StaticWork, vtable 0x647F9C (checked)
*(*(work + 0x08)) == party                    (checked)
order    = u8[6] at work + 0x53               screen position i shows party slot order[i]
cursor   = u8 at work + 0x52                  (same field as RE1's battle Pokémon list)
```

After Torracat ↔ Ledyba the screen showed Ledyba, Zorua, Pikipek, Pichu, Torracat, Buneary and `party` printed `5 2 3 4 1 6`; after closing, the party itself was in that order and the order bytes were back to `0 1 2 3 4 5`. The Bag's party list showed the new order.

## Bag

`Savedata::MyItem` keeps the item words at `+4 .. +0xE2C` (the save file's MyItem block, offset by the vtable) and a pocket table of 7 pointers at `+0xE2C` (set in the ctor `0x45A370`):

| Table index | Pocket | MyItem offset | Slots | Checked against |
|---|---|---|---|---|
| 0 | Medicine | `+0xB78` | 60 | Potion ×9, Paralyze Heal ×5, … Fresh Water ×1, in screen order |
| 1 | Items | `+0x004` | 427 | Quick Claw … Poison Barb, in screen order (scrolled to the end) |
| 2 | TMs | `+0x9C8` | 108 | TM01 Work Up, TM100 Confide |
| 3 | Berries | `+0xC68` | 67 | Oran Berry ×7 |
| 4 | Key Items | `+0x6B0` | 198 | Z-Power Ring, Exp. Share, Lens Case |
| 5 | Z-Crystals | `+0xD74` | 35 | empty in this save (tab hidden) |
| 6 | Rotom Powers | `+0xE00` | 11 | Roto Boost ×2 |

Slot counts are the gaps between the pockets. Each slot is a u32: item ID bits 0-9, count bits 10-19, Free Space position bits 20-29, NEW flag bit 30 (the blinking "NEW" badge: Burn Heal, Ice Heal, Great Ball, Tiny Mushroom …; Escape Rope's bit cleared once the Bag cursor landed on it, and it showed no badge after that). Slots stay in place: an item used up keeps its ID with count 0 and the Bag hides it (Premier Ball, Sparkling Stone, and Quick Claw after giving it away). The Bag lists the non-zero slots in array order.

Tab order on screen: Medicine, Items, TMs, Berries, Key Items, Free Space, Rotom Powers. Free Space is a view of slots with a Free Space position (none here, and the tab was empty). Where the Z-Crystals tab goes was not seen.

## Names

From the resident message files (`docs/RE2-text.md`), decoded in the plugin without calling game code: species `*(0x671088)` (file 60, line = species), items from the global message cache `0x6A7368` (the slot with dat ID 42). Both MsgData objects are validated (vtable `0x64B104`, full load, dat ID, line count, bounds) before reading. File 42 lines carry a plural tag `0x0010, 3, 0x1101, 0x00FE, n`: the low byte of `n` is the length of the singular ending after the tag, the high byte the plural ending after that (`Oran Berr` + `y` + `ies`); `DecryptMsgLine` keeps the singular. All names printed in the runs above matched the screens.

## Gaps

- **Move names:** not resident (RE2); the commands print move IDs. Options are in `docs/RE2-text.md`.
- **Status conditions:** layout not verified (nobody was poisoned/paralysed in these runs). Inflict one in battle, finish the battle, run `party`.
- **Eggs, forms, gender, shininess, nature, stats other than HP:** in the decoded data (PK7 layout) but not printed or checked.
- **Z-Crystals pocket and Free Space:** no data in this save.
- **Bag screen state:** which pocket and item the field Bag has selected was not looked for (RE1 found them for the battle Bag only). The counts and the party are written to `MyItem` / the party directly, so no copy needs to be read.
- **Other builds:** all addresses are USA v1.2. `signatures/usum.json` has no entries for these yet.

## How it was found

1. RTTI names (`N8Savedata6MyItemE`, `N8Savedata13PokePartySaveE`, `N8Savedata8SaveDataE`, `N3pml8pokepara12PokemonParamE`, `N3App8PokeList10StaticWorkE`) → typeinfo → vtables `0x64F3C4`, `0x64F194`, `0x64F4A0`, `0x649374`, `0x647F9C`.
2. `tools/find_refs.py` on the vtables gave the constructors; the SaveData ctor `0x45BDFC` builds MyItem at `+8` and PokePartySave at `+0xFB8`; its only caller is the GameData ctor `0x4070E4`, whose caller `0x402360` stores GameData at GameManager `+0x24`.
3. A heap dump (`dump 30000000 4184000`) in the field: one SaveData (`0x33011928`), 12 PokemonParams; a backward pointer search found the 6-member party at `0x33F7F9B8`, held at GameData `+0xC` (GameData `0x33F6D6E8`, start of its heap block); GameManager `0x33F67FE0` is the global `0x6A59C4` (from a `snap` of `.data`/`.bss`).
4. A scratch Python decoder applied the PK7 scheme to the dump: all six checksums matched and the values matched the Party screen. The game's own crypt (`0x320BBC`, constants `0x41C64E6D` / `0x6073`) and its decrypt/encrypt callers were then decompiled to confirm it.
5. The Party screen's order array: after a move on the screen the global party was unchanged; a second dump had no other PokeParty, so the StaticWork (RE1's battle Pokémon-list param) was peeked: bytes `+0x53..+0x58` read `01 00 02 03 04 05` after the first swap and `00 01 02 03 04 05` after closing.
6. Every value was then read live with `party` / `bag` and compared with screenshots (`analysis/private/shots/`, local only); the command outputs are in `analysis/private/re3/` (local only).

## Confidence

- High: roots and validation chain; party count, species, nickname, level, HP, held item, moves, PP; encryption scheme and both stored forms; bag pockets, IDs, counts, order, NEW flag, count-0 hiding; species/item names; Party-screen order.
- Medium: when the game decrypts in place (the `[dec]` form was never observed; only the code says it exists); pocket table order = the order the game itself uses for tabs (matches for the 6 non-empty pockets).
- Not verified: status condition layout, egg flag in a real save, Z-Crystals / Free Space, other languages and builds.

## Code

- `Includes/PartyData.hpp`, `Sources/PartyData.cpp`: pure decoding (PK7, bag slot, message line, UTF-16 → UTF-8), tested by `tests/party_data_test.cpp` with synthetic data.
- `Includes/Party.hpp`, `Sources/Party.cpp`: `ReadParty(sceneManager)`, `ReadBag()`, `ReadBagSlot`, `SpeciesName`, `ItemName`, and the `party` / `bag` log lines. Memory is only read (`Process::Read*` after `CheckAddress`), from the plugin thread.
