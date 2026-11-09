# RE2: game text (static research)

Pokémon Ultra Sun (USA) v1.2, title `00040000001B5000`, version `0x0800`. How the game loads and looks up its text, and how the plugin can get species, move and item names at runtime without shipping any game text.

Everything here comes from the local v1.2 `code.bin` (Ghidra headless, `tools/find_*.py`) and from the decrypted base CCI and v1.2 update CIA (`tools/romfs_text.py`). Nothing has been checked in a running game yet: every runtime claim is **unverified** until the Wave 1 checks at the end pass.

## Summary

- Text lives in one GARC per language: `a/0/3/<lang>` (English = `a/0/3/2`, arc ID 32). Each GARC holds 127 message files. Species = file 60, moves = 118, items = 40 (plain) / 42 (with plural tags), abilities = 101.
- The game's text API is `gfl2::str::MsgData` (RTTI in the binary). A MsgData object loads one message file; `GetString(line, StrBuf*)` decrypts one line into a `gfl2::str::StrBuf`. Text stays **encrypted in RAM** and is decrypted per lookup.
- Encoding: UTF-16LE with `0x0010` control sequences, XOR-encrypted per line with a rotating 16-bit key (`0x2983 * (line + 3)`, rotate left 3 per char). No other obfuscation or compression.
- **Species names are resident for the whole session** (statically): the pml library creates a full-load MsgData for file 60 at init and keeps it in the global `0x671088`.
- **Item names (file 42) are resident** (statically): a global 8-slot message cache at `0x6A7368` is filled at init with only slot 0 = file 42.
- **Move names are not resident globally.** The game's WordSet builds a temporary part-load MsgData for each lookup unless the current app preloaded the file (one main-image app preloads items + moves). Battle and Bag live in CRO modules that were not analysed.
- Proposed plugin API: read the resident MsgData objects directly and decrypt in the plugin (no calls into game code). Moves need one of three fallbacks; see "Proposed plugin API".

## Files and IDs

### Arc IDs and paths

`0x671C14` is the arc-ID → path table (`char*[]` in .data): entry `N` is `"a/X/Y/Z"` for the digits of `N` (`0` → `a/0/0/0`, `32` → `a/0/3/2`). `0x3D2488 GetMessageArcId(lang)` returns `0x5D6120[lang]` = `30 + lang`; `lang == 10` means "current", read from the byte at `0x671A10` (setter `0x3D24D4`).

| lang | arc ID | GARC | Language (checked by decoding species / move 1) |
|---|---|---|---|
| 0 | 30 | `a/0/3/0` | Japanese (species names identical to lang 1; kana/kanji split not checked) |
| 1 | 31 | `a/0/3/1` | Japanese |
| 2 | 32 | `a/0/3/2` | **English** |
| 3 | 33 | `a/0/3/3` | French |
| 4 | 34 | `a/0/3/4` | Italian (move 1 "Botta") |
| 5 | 35 | `a/0/3/5` | German |
| 6 | 36 | `a/0/3/6` | Spanish (move 1 "Destructor") |
| 7 | 37 | `a/0/3/7` | Korean |
| 8 | 38 | `a/0/3/8` | Simplified Chinese (private-use glyphs `U+E8xx`) |
| 9 | 39 | `a/0/3/9` | Traditional Chinese (private-use glyphs `U+EBxx`) |

The v1.2 update RomFS ships its own `a/0/3/0`–`a/0/3/9`. For `a/0/3/2` it changes only files 55 and 99; files 40, 42, 60, 101 and 118 are byte-identical in base and update. The binary has `"rom:"` and `"rom2:"` mount names (`0x5DBA68`, `0x5DBA5C`); how the update archive overrides the base one was not traced.

### Message files used for names (English, `a/0/3/2`)

| File | Content | Lines | Section bytes | Notes |
|---|---|---|---|---|
| 60 | Species names | 808 (0 = "Egg", 1–807) | 20956 | `Nidoran♀/♂` use `U+2640`/`U+2642`; `é` in Flabébé |
| 117 | Move descriptions | 730 | | Indexed by move ID; decoded offline from local English update GARC for battle extras; runtime unverified |
| 118 | Move names | 729 (0 = "———", 1–728) | 22896 | |
| 40 | Item names | 960 (0 = "None") | 28816 | plain |
| 42 | Item names for WordSet | 960 | 40648 | every line ends in a plural tag, e.g. `Master Ball` + `[VAR 1101 00FE 0100]` + `s`; cut at the first `0x0010` for the singular |
| 101 | Ability names | 234 | 7060 | |
| 112 | Type names | 18 | | |
| 92 | Nature names | 26 | | |
| 72 | Place names | 234 | | |
| 110 / 111 | Trainer names / classes | 653 / 223 | | |
| 2 | Bag pocket names | 8 | | |

The file numbers are confirmed twice: by decoding the files, and by the game's own callers (the pml settings pass 60, WordSet passes 42 / 118 / 101 / 112 / 92 / 72 / 110 / 111 / 2; see below). Non-ASCII characters in the English name lists: species `♀ ♂ ’ é`, moves `— ’`, items `é ’`.

## Formats

### GARC (container)

Version `0x0600`. `CRAG` header (size at `+4`, data offset at `+0x10`), then `OTAF` (offsets), `BTAF` (per entry: `u32` variant bitmask, then `{start, end, length}` per set bit), `BMIF`. The text GARCs have one variant per entry (bit 0).

### Message file (`gfl2::str::MsgData` data)

```
0x00 u16 sectionCount      (1 in every file checked)
0x02 u16 lineCount
0x04 u32 totalSize
0x08 u32 initialKey        (0)
0x0C u32 sectionOffset[sectionCount]
section:
0x00 u32 sectionSize
0x04 { u32 offset (from section start), u16 lengthInChars (includes the 0 terminator), u16 extra } [lineCount]
...  encrypted UTF-16LE text
```

Decryption of line `N` (game: `0x4B877C`, loop at `0x4B8810`: `key = (line + 3) * 0x2983`, `char ^= key`, `key = rol16(key, 3)`; each decrypted char is appended to the StrBuf by `0x35DA18`):

```
key = (0x2983 * (N + 3)) & 0xFFFF     # 0x7C89 for line 0
for each u16 c: out = c ^ key; key = ((key << 3) | (key >> 13)) & 0xFFFF
```

`GetString` returns the line's `extra` u16 (`0x35F71C`). The `sectionCount > 1` case picks section `*(0x6710D4)` (clamped to 0 if out of range); all name files have one section.

### Control codes

- `0x0000`: end of line.
- `0x0010, n, a1..an`: a tag of `n` argument words (variables, grammar, plural, colour). `0x35DCB4` (StrBuf length) and `0x35FB88` skip tags by `n + 2`. In name files they only appear in file 42 (plural tags `0x1101 …`).
- `0xE07F` (narrow space), `0xE08D` (`…`), `0xE08E` / `0xE08F` (`♂` / `♀` in dialogue): private-use glyphs of the game font. Species names use the real `U+2640` / `U+2642`.

## The game's text API (addresses, v1.2)

### `gfl2::str::MsgData` (0x30 bytes)

RTTI name `N4gfl23str7MsgDataE` at `0x6396D2`, typeinfo `0x628180`, **vtable `0x64B104`** (`[0]` dtor `0x35FA28`, `[1]` deleting dtor `0x35F95C`). Methods are not virtual.

| Offset | Field |
|---|---|
| `+0x00` | vptr = `0x64B104` |
| `+0x04` | heap (`gfl2::heap` object) |
| `+0x08` | u8 load type: `0` part load (header only; each lookup reads the line from the file), `1` full load |
| `+0x0C` | file (dat) ID within the arc |
| `+0x10` | current section index |
| `+0x14` | header buffer (`u16 sections, u16 lines, …, sectionOffset[]`) |
| `+0x18` | full load: section buffer (`u32 size`, line table, encrypted text); 0 for part load |
| `+0x1C` | part load: 8-byte scratch for one line entry |
| `+0x24` | file reader (null for the in-memory constructor) |
| `+0x28` | arc path string (path constructor) or 0 |
| `+0x2C` | arc ID (arc-ID constructor) or `-1` |

| Address | Function |
|---|---|
| `0x35F8F8` | ctor `(this, arcId, datId, heap, loadType, ...)` (23 callers in the main image) |
| `0x35F7F0` | ctor `(this, const char* arcPath, datId, heap, loadType, ...)` (copies the path; used by pml) |
| `0x35F88C` | ctor `(this, const void* fileInMemory, heap)`: load type 1, section points into the caller's buffer |
| `0x35F2BC` | opens the arc file (`+0x24`) |
| `0x35F384` | loads header; full load also allocates and reads the section into `+0x18` |
| `0x35F4F0` | read `(this, datId, offset, dst, size)` from the arc file |
| **`0x35F5D4`** | **`u16 GetString(this, u32 line, StrBuf* out)`**: clears `out` and returns 0 if `line >= lineCount`; otherwise reads the entry (from `+0x18`, or from the file for a part load into a temporary heap buffer), decrypts into `out` via `0x4B877C`, returns the entry's `extra` |
| `0x4B877C` | decrypt `(this, encrypted, length, line, StrBuf*)` |

### `gfl2::str::StrBuf` (0x10 bytes)

RTTI `N4gfl23str6StrBufE` at `0x6396AC`, vtable `0x64B0BC`. `+0x04` `u16*` buffer, `+0x08` u16 capacity, `+0x0A` u16 length, `+0x0C` u8 valid. Ctor `0x35DBB4(this, capacity, heap)`, dtor `0x35DC8C`, append char `0x35DA18`, clear `0x35D864`, length-with-tags `0x35DCB4`.

### Who keeps which names loaded

**Species (pml library, resident).**
- `0x3FDEB8` (called from the language/system setup `0x3F9064` and from `0x3287F0`) builds a 0x70-byte pml settings struct on the stack. `+0x4C` = the message arc's path (`0x4B4D1C(…, GetMessageArcId(10))`; the MsgData path ctor `strlen`s it, so it is a string), **`+0x50 = 0x3C` (file 60)**. It then calls the pml library ctor `0x31C958`, which copies the settings to a heap block (pointer at `0x692760`) and runs `0x31CA58`.
- `0x31CA58` creates a `StrBuf(0x100)` at **`*(0x671084)`** and a **full-load MsgData** `0x35F7F0(path = settings+0x4C, dat = settings+0x50, loadType 1)` at **`*(0x671088)`**. The other main-image functions that load `0x671084` (`0x31CECC`…`0x31E7F4`) were not all decompiled, so whether anything frees or replaces it is unverified.
- **`0x31CE8C GetMonsName(StrBuf* out, u32 monsno)`**: rejects `monsno > 809` (`0x31CEE0`), maps `(monsno, form 0)` to a line with `0x31D290` (form 0 → `monsno`; 0 for invalid numbers), then `GetString(*(0x671088), line, out)`. Callers include WordSet (`0x3D4664`, `0x3D49B8`, `0x3D4D40`).

**Global message cache (items, resident).**
- `0x15F460 MsgCacheInit(heap, flags)`: for each set bit `i` of `flags`, creates a full-load MsgData (arc `GetMessageArcId(10)`, dat `0x5D61C8[i]`) into slot `i` of an 8×16-byte table at **`0x6A7368`** (slot: `+0x08` u16 dat ID, `+0x0C` MsgData*). Flags and heap are kept at `0x671A24`/`0x671A28`.
- Dat IDs per bit (`0x5D61C8`): `42` items, `118` moves, `112` types, `101` abilities, `92` natures, `72` places, `110` trainer names, `2` bag pockets.
- Its only caller (`0x3F9228` in `0x3F9064`) passes **flags = 1**: only items (file 42) are cached globally.
- `0x15F5E4 MsgCacheFind(datId)` looks a dat ID up in that table.

**WordSetLoader (per app, temporary).** `0x15F958` ctor `(this, heap, flags)` (same bit → dat table, skips files already in the global cache), `0x15F7C4` starts async loads, `0x15F6F8` waits and wraps each file in a MsgData (`0x35F88C`), `0x15FA7C` frees them, `0x15F34C` finds a dat ID (global cache first, then the loader). Main-image users: `0x2E9C60` (flags 3 = items + moves) and `0x393F6C` (flags `0x20` = places). CRO users were not analysed.

**WordSet lookup.** `0x3D4794 GetWordFromFile(wordset, StrBuf* out, datId, line)`: global cache → the wordset's loader (`wordset+0x14`) → otherwise a temporary **part-load** MsgData (`0x35F8F8(GetMessageArcId(10), datId, heap, 0)`), `GetString`, delete. Its 12 callers pass dat 42, 43, 118, 72, 86, 111, 92, 101, 110, 112, 2, 54. So for moves, outside an app that preloaded them, the game reads the move name from the RomFS on every lookup.

### Residency (static conclusion, unverified)

| Names | Resident? | Where | Load |
|---|---|---|---|
| Species | Yes, from pml init on | `*(0x671088)` | full, file 60 |
| Items | Yes, from `0x3F9064` on | `*(0x6A7368 + 0x0C)` (slot 0, dat 42) | full, file 42 (plural tags) |
| Moves | Only while an app's WordSetLoader holds them | heap, per app | file 118 |
| Abilities, types, natures | Same as moves | | |

## Proposed plugin API

```cpp
namespace OneScreen::Text {
// Pure memory reads: never calls game code, safe from the plugin thread.
// Writes at most `cap` UTF-16 units (no tags, 0-terminated). False if anything fails validation.
bool GetSpeciesName(u16 species, u16* out, u32 cap);   // MsgData *(0x671088), line = species (1..807)
bool GetItemName(u16 item, u16* out, u32 cap);         // MsgData *(0x6A7368 + 0x0C), cut at first 0x0010
bool GetMoveName(u16 move, u16* out, u32 cap);         // see options below
// Shared helper: validate a MsgData and decrypt one line.
bool ReadMsgLine(u32 msgData, u16 expectDatId, u16 line, u16* out, u32 cap);
}
```

`ReadMsgLine` checks, in order: `*(msgData) == 0x64B104`; `+0x08 == 1`; `+0x0C == expectDatId`; `+0x18 != 0`; `line < *(u16*)(*(msgData+0x14) + 2)`; entry `= +0x18 + 4 + line*8` lies inside `sectionSize`; `length <= cap` and the text lies inside the section. Then it decrypts with the key above and stops at `0` or `0x0010`. Every pointer read goes through `Process::Read32`/`Read16`-style checked reads. The plugin also checks the language byte `*(u8*)0x671A10 == 2` and keeps a small cache of decoded names.

Drawing: the plugin's font must map `é ’ — ♀ ♂` (and `…`), or substitute ASCII (`e ' - F M`).

Options for moves, in order of preference:
1. **Find a resident copy** (Wave 1 check 5): if Battle/Bag/Party keep a full-load MsgData for file 118 reachable from a stable global, read it like species.
2. **Call the game's own lookup on the game thread**: from a hook that already runs on the game thread (e.g. the existing pad/touch reader hooks), build a temporary part-load MsgData exactly like `0x3D4794` does (`0x35F8F8` with a game heap, `GetString`, delete) or once create a full-load MsgData for file 118 (~23 KB from a game heap) and keep it. Needs a heap pointer (`0x107374(heapId)` returns one; which heap ID is safe is unknown) and must never run from the plugin thread.
3. **User-generated name file**: `tools/romfs_text.py names` writes the lists from the user's own dump; the user copies them to the SD card. The repo still ships no game text. Weakest option (manual step, can drift from the game's language).

### Risks

- All addresses are for USA v1.2 only; the plugin already refuses other versions. Ultra Moon has no local dump.
- Runtime state is unverified: the globals could be reset during soft reset, save load or language changes (`0x3F9064` reruns the cache init; `0x3FDEB8` only builds pml once, guarded by `*piVar9 == 0`).
- Reading game memory from the plugin thread while the game frees a MsgData could read freed memory. Species/items are built once, so this is low risk for them. It is higher for option 1 (per-app objects).
- Option 2 calls game code: wrong thread, heap exhaustion or a file read at a bad moment could crash or stall the game.
- Text is decrypted per line, so decoding costs about one XOR per char: negligible. Cache decoded names anyway.
- File 42 item names carry plural tags; file 40 (plain) is not resident.

## Wave 1 runtime checks

Use the plugin's `peek ADDR [COUNT]` (`tools/cmd.py`), compare against `analysis/private/names-en/*.txt` from `tools/romfs_text.py names`, and decode RAM text with `tools/romfs_text.py decrypt LINE WORDS…`.

| # | Scene | Command | Expect |
|---|---|---|---|
| 1 | Title, field, battle | `peek 671A10 1` | low byte `02` (English) |
| 2 | Field (Route 2) | `peek 671084 2` | two heap pointers (StrBuf, MsgData) |
| 3 | Field | `peek <MsgData> 12` | `+00 = 0064B104`, `+08` low byte `01`, `+0C = 0000003C`, `+18` non-zero, `+2C = FFFFFFFF` |
| 4 | Field | `peek <+18 value> 4`, then the line entry at `+18 + 4 + 25*8`, then the text at `+18 + offset` | decrypts to `Pikachu` (line 25). Static reference: the encrypted words are `52CD8A04 A9759549 454248C6 4A912927` and `py -3 -I tools/romfs_text.py decrypt 25 52CD8A04 A9759549 454248C6 4A912927` prints `Pikachu` |
| 5 | Field, X menu, Bag, Party, Battle | `vtscan NAME`, then search the records for vtable `0x64B104`; `peek` each hit's `+08`/`+0C`/`+2C` | live MsgData objects reachable from a global, per scene (does any hold dat `0x76` = moves, full load?). `vtscan` only follows global → object, so objects deeper in an app need a pointer chain |
| 6 | Field and Battle | `peek 6A7368 8` | slot 0: `+08` u16 = `002A`, `+0C` MsgData pointer; slots 1–7 empty |
| 7 | Battle | repeat 2–4 and 6 | same pointers as in the field (species and items survive scene changes) |
| 8 | After soft reset / reloading the save | repeat 2 and 6 | pointers valid (may differ) |
| 9 | Field | `peek 692760 1`, then `peek <that pointer + 0x4C> 2` | first word a pointer to the arc path, second word `0000003C` |

## How this was found

1. `tools/romfs_text.py list` on both dumps showed `a/0/3/0`–`9` (10 GARCs, 127 entries each). Decoding file 60 in each gave the language order; the key schedule was taken as a lead from community docs and confirmed by readable output in all 10 languages.
2. `find_refs.py … 2983` found the key literal at `0x4B8878`, loaded at `0x4B8810` inside the decrypt routine `0x4B877C`.
3. RTTI strings `N4gfl23str7MsgDataE` / `StrBufE` → typeinfo → vtables → constructors (refs to `0x64B104`) and the callers of `GetString` (`0x35F5D4`, 110 call sites).
4. The constructors take an arc ID from `0x3D2488`; its table `0x5D6120` gives `30 + lang`, and the path table `0x671C14` maps arc IDs to `a/x/y/z`.
5. Following constructor callers led to the global cache `0x15F460` (dat table `0x5D61C8`), the WordSetLoader (`0x15F958`…), WordSet's `0x3D4794`, and the path constructor's single caller `0x31CA58` (pml init). The species dat ID `0x3C` comes from the settings struct built in `0x3FDEB8` (`+0x50`), and `0x31CE8C` reads `*(0x671088)`.

Decompiler output is in `analysis/private/re2-msg*.txt` (local only).

## Tool: `tools/romfs_text.py`

Python stdlib only, run with `py -3 -I`. Reads NoCrypto CCI/CIA only and refuses anything else. Output is game data: write it under `analysis/private/` (gitignored), never into the repo.

```bash
py -3 -I tools/romfs_text.py list "<update>-decrypted.cia" a/0/3
py -3 -I tools/romfs_text.py names "<base>-decrypted.cci" "<update>-decrypted.cia" analysis/private/names-en
py -3 -I tools/romfs_text.py text analysis/private/romfs-upd/a/0/3/2 60
py -3 -I tools/romfs_text.py decrypt 25 52CD8A04 A9759549 454248C6 4A912927
```

`list`/`extract` walk the RomFS (IVFC level 3); `garc` lists entries; `text` decodes one message file (`--raw` shows decrypted code units); `names` writes species, moves, items (40 and 42), abilities, types and natures as `index<TAB>name` UTF-8 files; `decrypt` decodes words copied from a `peek` log line.
