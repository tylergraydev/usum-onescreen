# Offsets: Pokémon Ultra Sun (USA) v1.2

Title `00040000001B5000`, update title version `0x0800` (2048). Code layout from the update's exheader: text `0x100000` (size `0x4BB818`), rodata `0x5BC000`, data `0x669000`, bss `0x301BC`. The code is not compressed.

Found by static analysis of the local `code.bin` (`tools/extract_code.py`, `tools/find_refs.py`, `tools/find_calls.py`, `tools/ghidra/decompile.sh`). The plugin checks the first four instruction words of each hook target at boot and writes the result to `usum-onescreen.log`. `tools/sigscan.py` with `signatures/usum.json` finds the hook targets, `sceneManager` and the Party/Bag vtables again in another build (see `docs/PORTING.md`).

## Hook targets (verified in Azahar 2026-11-02)

| Name | Address | What it is |
|---|---|---|
| `gspSetFramebuffer` | `0x279BB4` | gx library "set framebuffer" `(screen 0=top/1=bottom, left, right, stride, format, select, attribute)`. Writes the 0x1C-byte framebuffer info into GSP shared memory and sets the screen's update flag. At runtime its first word is already a branch to a `ldr pc,[pc,#-4]` stub at `0x01E80000` (an existing hook, consistent with CTRPF's OSD; owner not yet confirmed). |
| `hidTouchRead` | `0x2826F0` | Touch sample reader used by both `TouchPanelReader::Read` (`0x1790B0`) and `ReadLatest` (`0x17902C`): `(touch shm, status* out, count, int* outCount, ...)`. Each status is 8 bytes: `u16 x, u16 y, u8 touching`. |
| `hidPadRead` | `0x282548` | Pad sample reader used by both `PadReader::Read` (`0x17A108`) and `ReadLatest` (`0x179FD8`): `(pad shm, status* out, count, int* outCount, ...)`. Each status is 16 bytes: `u32 held, u32 pressed, u32 released, s16 stick x, s16 stick y`. `ReadLatest` derives pressed/released from `held` itself; `Read` uses the sample's own edge bits. |
| `sceneManager` | `0x6A599C` | Global pointer to the scene manager (`0x34021048` at runtime). Manager `+0` points to the current scene object; that object's vtable names the scene (see "Scene detection"). |
| `gxDisplayTransfer` | `0x27A5BC` | Called, not hooked (panel, P1-6). gx DisplayTransfer: `(in, in width, in height, out, out width, out height, flags)`; builds GX command 3 and pushes it with `0x279A2C` onto the game's queue (shared `+0x800`; lock-free `ldrex`/`strex`, triggers `TriggerCmdReqQueue` when the queue was empty). Verified: decompiled (Ghidra), prologue checked at boot, and bottom-to-top copies at 1x scale and 2x2 down drawn correctly in Azahar (screenshots, readback check). |
| `gxTextureCopy` | `0x279E28` | Called, not hooked (panel). gx TextureCopy: `(in, out, size, in width, in gap, out width, out gap, flags)`, widths and gaps in 16-byte units; GX command 4, same queue push. Verified the same way. |

## Scene detection (runtime, 2026-11-02)

**Current scene** = `*(*(0x6A599C) + 0)` → its vtable. Many scenes are separate CRO modules loaded at runtime (at `0x6DF000` and up); a loaded CRO has `FIXD` at `+0x80` and an absolute name pointer at `+0x84` (`+0x88` next module, `+0x8C` previous). `Sources/Scene.cpp` walks back from the vtable to the module header and returns its name, or `main:<vtable>` when the class is in the main image. The plugin logs every change as `scene: …`.

| Scene | ID | Verified |
|---|---|---|
| Title screen | `TitleMenu` | boot |
| Overworld (incl. X menu, which is an overlay) | `FieldRo` | Pokémon Center, Route 2 |
| Pokédex (top level, lists, entry pages) | `Zukan` | via Rotom tap and X menu |
| Party | `main:00647FCC` | via X menu |
| Bag | `main:006488C8` | via X menu |
| Battle | `Battle` | wild Ekans on Route 2 |
| Pokémon summary | `Status` | X menu → Pokémon → slot → Summary |
| PC boxes | `Box` | A at the Pokémon Center PC (start point `pc`) |
| Town Map | `TownMap` | tap the map on Rotom's screen |
| Trainer Passport | `TrainerPass` | X menu → Trainer Passport |
| Options | `Config` | X menu page 2 → Options |
| Pokémon Refresh | `Kawaigari` | X menu page 2 → Pokémon Refresh |
| Battle Video (Vs. Recorder) | `BattleVideoPlayer` | X menu page 2 → Battle Video |
| QR Scanner | `QRReader` | X menu page 2 → QR Scanner; logged, then Azahar hung (no camera), no screenshot |
| Quick Link, save dialog | `FieldRo` (field state 4) | X menu → Quick Link; X menu → Y |
| Festival Plaza | `FieldRo` (field state 4) | X menu → Festival Plaza; a second FieldRo scene object, `JoinFesta` CRO loaded |
| Poké Mart (Buy list), talking to people | `FieldRo` (field state 0x11) | the clerk in the Route 2 Pokémon Center; `Shop` CRO loaded |
| Nickname question and keyboard after a catch | `Zukan` (no scene change; the keyboard is the `StrInput` CRO, loaded with Zukan) | wild catches on Route 2; see "Keyboard" below |
| Loading / transition | (none) | between every scene |

Found by: `vtscan` (plugin: every global heap pointer → first 64 words → objects with a rodata vtable), diffed with `tools/vtdiff.py` across scenes, then `peek` on the manager showed vtables outside the main image, and Azahar's log (`CRO "…" loaded at …`) named the modules.

Default views (`Includes/Settings.hpp`): full where the bottom screen is the whole scene and the top shows little (Party, Box, TownMap, Config, Kawaigari); panel where the top still says something (Status: stats on top, moves below; TrainerPass; BattleVideoPlayer; Zukan; Bag; Battle) and for the X menu; off where the bottom screen is unused (TitleMenu, FieldRo walking, talking, shopping).

Not reached on this save: other uses of the keyboard (Box names, Name Rater, the player's name), trainer and double battles, Rotom's own menus (Roto Loto, Rotom chat: they only come up when Rotom offers them; not reached), the QR Scanner's screen (Azahar hangs).

**Field state** (`Sources/Scene.cpp` `FieldState()`): the FieldRo scene object (`*(*(0x6A599C))`, vtable `0x7B4F44`) holds an object whose vtable is FieldRo `+0xD735C` (`0x7B635C` with FieldRo at `0x6DF000`). Its offset depends on the map: `+0x618` in the Route 2 Pokémon Center, `+0x3DC` on Route 2 (same scene class; the members before it differ), so the plugin finds it by that vtable in the scene object's first `0x800` bytes (one match in both dumps). Its word at `+4` is 0 while walking, 4 while a bottom-screen menu is up (X menu, Quick Link, save dialog, Festival Plaza's menu; also for a moment while an app Rotom opened starts or closes) and 0x11 while talking or in a shop. Found with `dump` of the scene object, X menu open ×3 vs closed ×3 (only `+0x61C` and `+0x720` differed; `+0x720` stayed 0 after returning from Party with the menu open); checked with the menu closed by B and by touch, after Party/Summary/Options/Pokémon Refresh, in Festival Plaza and after leaving it, at the Poké Mart, and at the PC. Auto panel uses it for `FieldRo.xmenu` and falls back to X/B presses only if it never reads (scenarios `xmenu-touch-close`, and `wild-battle-run` for Route 2). After entering the field it waits until the state holds for 60 frames (`Includes/FieldSettle.hpp`), counting from the first read in the new field: coming back from the Town Map it reads 4 for 22 to 30 frames from the first FieldRo read before 0 (logged per frame, W6-4), and tapping the map on Rotom's screen reads 4 in the field just before, so a count that kept the old value let a 30-frame 4 through. If the state stops reading, it waits at most 60 frames before falling back to keys. During a wild encounter the state is 30 (0x1E).

Earlier, weaker signals (kept for reference):

| Address | Field | X menu | Festival Plaza | Pokédex | Bag | Party |
|---|---|---|---|---|---|---|
| `0x6A5DFC` | 0 | 0 | 0 | ptr | ptr | ptr |
| `0x69EB2C` | ptr | ptr | ? | 0 | ptr | ptr |

- `0x6A5DFC`: non-null while a full-screen app (Pokédex, Bag, Party) is open. It is slot 1 of an 8-slot font registry at `0x6A5DF8` (names at `+0x68 + slot*0x40`, set by `0x35BB0C`): `num_font.bffnt`. A side effect of opening an app, not a scene ID.
- `0x69EB2C`: object with vtable `0x646A5C` in field, Bag and Party (a different instance per scene), null in the Pokédex. Not a scene ID.
- `0x6A1D04`: changes per scene, but its target starts with a heap address, not a vtable.
- Ruled out as scene IDs: `0x66EB54` (Bag and Party both 1), `0x66EA24` and `0x670D68` (drift like counters), `0x6B67D4` (non-zero in some X-menu snapshots, 0 in others).
- `0x6A60xx`-`0x6A63xx` is a block of graphics floats/matrices that changes with anything on screen; ignore it when diffing.

## Related addresses

GSP (display):
- `0x27A118`: GSP client init. Calls `RegisterInterruptRelayQueue`, and stores the top/bottom framebuffer-info pointers (shared `+0x200 + thread*0x80`, `+0x40` for bottom) at manager `+0x5C` / `+0x60`.
- `0x66FC40`: GSP manager object. `0x69EA68`: its parent (`+0x1C` -> manager).
- `0x279A2C`: GX command queue push (shared `+0x800`).
- GX commands: `0x279C48` MemoryFill, `0x27A5BC` DisplayTransfer, `0x279E28` TextureCopy, `0x279D5C` FlushCacheRegions + ProcessCommandList.
- IPC wrappers: `0x27A7F4` AcquireRight, `0x27A860` FlushDataCache, `0x27AA80` RegisterInterruptRelayQueue.

HID (input):
- `0x179D88`: `HID::GetIPCHandles`. `0x1775A0`: HID init (maps shared memory).
- `0x6BB2E0`: HID object. `+0x04` pad section (shm `+0x00`), `+0x0C` touch section (shm `+0xA8`), `+0x14` accelerometer (`+0x108`), `+0x20` gyro (`+0x158`).
- `0x6BB2E8`: touch device `{event handle, touch shm}`; getter `0x177894`.
- `0x34FDD4`: the game's input device constructor; `0x34FB44`: its per-frame update (reads pad, then touch into object `+0x110`/`+0x118`).
- `0x4B6790`: game accessor that copies the latest `{x, y, touching}`.

## Battle state (runtime, 2026-11-02)

Full write-up: [RE1-battle.md](RE1-battle.md). Read by the `battle` command (`Sources/Battle.cpp`). Battle CRO offsets are relative to the module header found from the scene's vtable.

- Path: scene `*(*(0x6A599C)+0)` → `+0x94` view (vtable Battle+`0x10A528`) → `+0xD0` poke container → `+0x04 + client*0x1C`: 6 battler record pointers + u8 count at `+0x18`; record 0 is the active battler (client 0 player, 1 opponent in wild battles).
- Record: species u16 `+0x0C`, max HP `+0x0E`, HP `+0x10`, ability `+0x16`, level u8 `+0x18`, 4 moves at `+0x1FC` (stride `0xE`: u16 move, u8 PP, u8 max PP).
- Menu: top of the scene manager's stack (`mgr+0..+0x1C`). Command Battle+`0x10B598` (cursor u8 `+0x278`), moves Battle+`0x10B724` (cursor `+0x114`, entries `+0xB0` stride `0x18`), Pokémon list main `0x648394`, Bag Bag+`0x16E24`, none Battle+`0x10B43C`.
- Verified against screenshots in 6 wild battles (Drowzee, Smeargle, 2× Yungoos, Makuhita), a PP drop for both sides, a faint and a switch.

Added for the native battle menu (P2-1, `Sources/Battle.cpp`), runtime 2026-11-02, instance `p21`, wild Growlithe, Ekans, Drowzee, Makuhita on Route 2:

- **Move names.** The busy object (stack entry with vtable Battle+`0x10B43C`, always on the stack in battle) `+0x94` → the battle's WordSet; WordSet `+0x14` → its loader; loader `+0x0C` u32 count (2), `+0x10` → entries of `0x10` bytes `{buffer, size, u32 dat, MsgData*}`. The entry with dat `118` (`0x76`) holds the move-name file (a full-load MsgData, vtable `0x64B104`, its own dat field `+0x0C` = 0 because it was loaded from the loader's buffer; 729 lines); the other is dat `101` (abilities). Found by searching a full heap dump taken in battle for MsgData objects with 729 lines and section size `0x5970` (RE2's move-file size), decoding lines 10/424/24/122 to Scratch/Fire Fang/Double Kick/Lick, and following pointers back to the busy object (command menu `+0x244` and two other battle objects hold the same WordSet). Checked live: the `battle` log line prints `player move names: Scratch, Fire Fang, Double Kick, Lick`, matching the game's move list, in every battle since. The Bag CRO keeps a second copy (heap `0x30F0BC00`), not used.
- **Bag item page.** Bag frame (Bag+`0x16E24`) `+0x6E` u8: 1 while the item list shows, 0 on an item's page (the one with Use). Found by diffing the frame and its child in both states (child `+0x72` flips the same way); checked on the Poké Balls and Status Restore tabs, list → page → list.
- **Battle party.** `party[0].record[0..count-1]` (same container as above) is the battle's party in battle order, which is what the battle Pokémon list shows; its cursor (`p+0x52`) indexes it.
- Seen, not an address: the battle Bag's Poké Balls tab lists the Items pocket's balls in pocket order, skipping count-0 slots (Poké Ball, Great Ball, Heal Ball with Premier Ball ×0 left out). The menu names the highlighted ball from that only when the counts agree.

## Keyboard (StrInput, runtime, 2026-11-02)

Full write-up: [KEYBOARD.md](KEYBOARD.md). Read by `Sources/Screens/Keyboard.cpp` (native keyboard) and the `keyboard read` command. StrInput offsets are relative to its module header (loaded at `0x73A000` in these runs, after Zukan `0x6DF000` and TownMapParts `0x72F000`).

- Path after a catch: scene manager stack (`*(0x6A599C) + 0..0x1C`) entry 4 or 5 (Zukan+`0x35114` / Zukan+`0x34E34`) → `+0xB0` Zukan object (Zukan+`0x351F4`) → `+0xB4` keyboard object (StrInput+`0xBFE4`; 0 at the Yes/No question) → `+0x1C` text object (StrInput+`0xBFC0`). The plugin finds it by scanning two levels from the stack entries, not by these offsets.
- Text object: u16[16] text `+0x30` (♂ `U+E08E`, ♀ `U+E08F`), page `+0x54` (1 QWERTY, 0 grid), shift `+0x5C`, length `+0x60`, limit `+0x64` (12), active `+0x90` (1 up, 0 after OK).
- Found by searching a heap dump for typed text (UTF-16) and walking pointers back; fields by `peek` after typing, Delete, Shift, the page arrow and OK; the path checked on two catches (the same heap addresses both times). Every QWERTY key tapped with shift off and on and the typed character read back.
- CRO module links checked: header `+0x88` next, `+0x8C` previous (Zukan → TownMapParts → StrInput; the head's previous is the tail, the tail's next is 0).

## Party and bag (runtime, 2026-11-02)

Full write-up: [RE3-party-bag.md](RE3-party-bag.md). Read by the `party` and `bag` commands (`Sources/Party.cpp`). Verified against the Party and Bag screens before and after battle damage, a Potion, two party reorders and giving a held item.

- Path: GameManager `*(0x6A59C4)` → `+0x24` GameData → `+0x04` Savedata::SaveData (vtable `0x64F4A0`) and `+0x0C` pml::PokeParty (6 PokemonParam pointers, u8 count `+0x18`).
- PokemonParam (vtable `0x649374`): `+4` core (`0xE8`, PK7 format, encrypted + shuffled in RAM), `+8` party stats (`0x1C`, encrypted with the same EC). Game crypt `0x320BBC`; accessor decrypt `0x322CB0`, re-encrypt `0x322BE0`.
- Bag: MyItem = SaveData `+8` (vtable `0x64F3C4`); pocket pointer table at MyItem `+0xE2C`: Medicine `+0xB78` (60), Items `+0x4` (427), TMs `+0x9C8` (108), Berries `+0xC68` (67), Key Items `+0x6B0` (198), Z-Crystals `+0xD74` (35), Rotom Powers `+0xE00` (11). Slot u32: item bits 0-9, count 10-19, Free Space 20-29, NEW bit 30.
- Party screen order while it is open: scene (vtable `0x647FCC`) `+0xA8` StaticWork (vtable `0x647F9C`) `+0x53` u8[6]; the party itself is reordered when the screen closes.
- **Party screen cursor** (P2-3): StaticWork `+0x52` u8, the screen position the game's cursor is on (0-5). `peek` read 0, then 5 after tapping slot 6 (Buneary), still 5 after closing its slot menu (instance p23, 2026-11-02). `ReadParty` fills `PartyState::screenCursor`; the native Party screen starts on it, and the swap action skips the cursor tap when it is already on the first Pokémon.
- **PK7 ability and nature** (P2-3): core `+0x14` u8 ability, `+0x1C` u8 nature (PK7 layout). Checked by name against the game's summary: Pikipek Skill Link / Timid (the memo page says Timid), Zorua Illusion, Pichu Static, Ledyba Early Bird, Buneary Klutz, Torracat Blaze (abilities on the summary's bottom page; only Pikipek's nature was seen on screen).
- **Status condition** (P2-3): party stats `+0` is a number, not the older bit field. Seen: 5 while the game's Party screen showed POISONED (Pichu after a wild Ekans, scenario `native-party` run 1). 1 paralysis, 2 sleep, 3 freeze, 4 burn are the usual Gen 7 values, **not seen**; `PartyStatusName` shows anything else as `?`.
- **Bag item list** (P2-3): Bag scene (vtable `0x6488C8`, the field Bag and the Party screen's Restore) `+0x88` → its list: `+0x0C` u32 items listed (the shown pocket's non-zero slots), `+0x14` u32 rows shown (7), `+0x18` u32 cursor row within the shown rows, `+0x1C` float first shown index (scroll; whole numbers once the list settles). The cursor index is scroll + row. Found by diffing three heap dumps (`dump 30000000 4184000`) in the Restore bag's Medicine pocket with the cursor on Revive / Ice Heal / Potion (row 4 / 3 / 0, scroll 2.0 / 2.0 / 0.0), then a pointer search back to the scene object. The Bag keeps cursor and scroll per pocket, also across tab taps. Read by `ReadBagList`; used live by the native Party screen's Use item (Fresh Water, 9th of 9: four Down steps, then a tap on row 7, `party-ui: bag scroll 2 row 4, tapping row 7`).
- **Summary names** (P2-3): the summary (scene `Status`, a CRO; vtable Status-module `0x6FE0A4` in the dump) `+0x58` → a WordSet loader: `+0x10` → entries of `0x10` bytes `{buffer, size, u32 dat, MsgData*}` (as the battle's, above): dat 118 moves (729 lines), 101 abilities (234), 92 natures (26). The MsgData's own dat field is 0 (loaded from the loader's buffer). Found by searching a heap dump taken in the summary for MsgData objects (vtable `0x64B104`) with 729 lines and following pointers back from the current scene. Read by `SummaryWordLoader` / `LoaderText`; the names printed (`party-ui: summary ...`) matched the game's move list for all six Pokémon.

## Field Bag screen (runtime, 2026-11-02)

Full write-up: [BAG.md](BAG.md). Read by the native Bag (`Sources/Screens/Bag.cpp`, `bagui read`). Found in instance `p24` with heap dumps diffed across pocket taps, cursor moves, the item menu, a Use and two Gives; then checked live with `bagui read` against screenshots and through `scenarios/native-bag.txt`. Bag CRO offsets are module-relative (the module's name is `Bag`; it loaded at `0x6DF000` in the field).

- Scene: top of the scene stack, vtable `0x6488C8` (main). `+0x88` item list window: `+0x14` rows shown (7, fewer for short pockets), `+0x18` cursor row, `+0x1C` first row as a float (animates while it scrolls; the item under the cursor is first row + cursor row). `+0x90` the Bag frame.
- Frame (vtable Bag+`0x16F9C`): `+0x24` the open pocket as an array of pointers to MyItem slots, in screen order, `+0x28` their count; `+0x2C` the Bag param; `+0x4C` u8[4], byte 3 = 0 while a message waits; `+0x54` byte 0 = a menu or message is up, byte 1 = the party pick ("Which Pokémon...?").
- Param (`+0x24` = MyItem, checked by its vtable): `+0x4C` item the menu / pick is about, `+0x54` u8 tab (0 Rotom Powers, 1 Medicine, 2 Items, 3 TMs, 4 Berries, 5 Key Items, 6 Free Space: left to right), `+0x64` party column (vtable Bag+`0x16D98`, u8 cursor `+0x1C6`, 6 = none), `+0x80` menu object (vtable `0x6487C0`, u16 entry count `+0x158`), `+0x84` u8 entries (Use 0x00, Give 0x03, Move to Free Space 0x04, Cancel 0x11, Yes 0x12, No 0x13), `+0xEC` u8 state (0 in the list; 2 Use, 3 Give while they run).

## PC boxes (runtime, 2026-11-02)

Full write-up: [RE6-boxes.md](RE6-boxes.md). Read by the `box [N]` command (`Sources/Box.cpp`) and the native boxes (`Sources/Screens/Boxes.cpp`). Verified against the Box screen (screenshots) through cursor moves over every slot, box changes, picking up, dropping, swapping, party ↔ box and box → box moves, in instance `p26` (start point `pc`).

- Box data: SaveData (RE3) `+0x3B98` Savedata::BOX (vtable `0x64F364`; data at +4 = the save's block `0x4C00`: names `0x22` × 32, unlocked count u8 at data `+0x5E1`), `+0x4184` Savedata::BoxPokemon (vtable `0x64F044`; data at +4 = the save's block `0x5200`: 32 × 30 PK7 cores of `0xE8`, stored format). Compared byte for byte with the save file; a drop writes it at once.
- Box screen (Box CRO; offsets from its load address): `App::Box::BoxLowBgDraw` (vtable Box+`0x2F4D8`) is one of the scene manager's first eight words. `+0x8C` listener: Box+`0x2F6F0` idle, Box+`0x2F6B0` slot menu / message, Box+`0x2FA80` holding a Pokémon. `+0xDC` u32 cursor (0-29 slots, 30-35 party, 36 box name, 38 box list). `+0xC0` → parameter block (`+0` BOX*, `+4` BoxPokemon*, `+0x60` u32 box on screen). `+0xC4` → → work: `+0x1C` 6 PokemonParam* = the screen's own copy of the party (the real party changes when the PC closes).

## Player location (runtime, 2026-11-02)

Full write-up: [RE4-map.md](RE4-map.md). Read by the `where` command (`Sources/Where.cpp`).

- `0x6A59C4`: global → game manager (`+0x1C` = the scene manager, checked). `gm+0x24` field object → `+0xCC` move model manager (vtable `0x64C628`) → `+0x0C` player model (vtable `0x64C538`): position floats `+0x3B0` (x east, y up, z south), yaw quaternion `+0x424`.
- Zone: `*(*(gm+0x2C)+0x88)+0x1C` (u32; 0 in battle). Zone data: `field+0x54` loader (vtable `0x64BDD0`) → `+0x14` table of 0x54-byte entries (`+0x18` size): `+0x1C` place-name line in message file 72, `+0x44/+0x48` fixed map point (interiors only).
- Rotom map view (vtable `0x64786C`): FieldMenu frame (FieldMenu+`0x183A0`) on the scene stack → `+0xD0`; `+0x170` map index, `+0x174/+0x178` centre (world x/z), scales `0x2CF230`/`0x2CF234` (0.0270139, 0.0270536 map px per unit).
- Verified walking the Route 2 Pokémon Center (zone 79), Route 2 (7) and Hau'oli City (13) against screenshots and the Rotom map.

## Pokédex (runtime, 2026-11-02)

Full write-up: [RE7-pokedex.md](RE7-pokedex.md). Read by the native Pokédex (`Sources/Screens/Pokedex.cpp`, `pokedex rows`). Zukan offsets are from the CRO's module header (loaded at `0x6DF000` here).

- Flags: SaveData `+0x23DC` Savedata::ZukanData (vtable `0x64F550`, `+4` magic `0x2F120F17`): caught bits `+0x8C` (bit species-1), seen `+0xF4` / `+0x180` / `+0x20C` / `+0x298` (male, female, shiny male, shiny female; 0x8C each). Extra forms' bits follow bit 806 in the order of the form table `0x5D63DC` (123 × {u16 species, u16 forms}). Game functions: caught `0x4C7A7C`, seen `0x4C7B7C` (form count `0x4C7664`, form bit `0x4C7FD0`, test `0x4C76C8`). Verified: 22 caught / 38 seen, as the game's top level shows; Alolan Rattata / Meowth / Grimer seen through form bits 813 / 830 / 837.
- Open dexes: event flags (SaveData `+0x15D8` event work, bits at `+0x7D4`; `0x4BCEE0`): `0x101F` + island - 1 (Melemele .. Poni), `0xFE7` Evaluation. Verified: Melemele and Evaluation set, the three later islands clear, as on screen.
- App frame: scene manager `+0x08`; vtable Zukan `+0x34CD4` top level (`+0x134` u32 cursor row 0-5, `+0x19D` u8 state, 7 = menu), `+0x34DD8` Alola list, `+0x34F68` island grid, `+0x34C24` entry page. Verified on each screen, by screenshot.
- App data: frame `+0xA4` (vtable Zukan `+0x34C14`): `+0x11` u8 dex (0 Alola, 1 Melemele), `+0xA0` u16 record count (403), `+0xA4` records (0x1F0 each: u16 species, u16 Alola / Melemele / Akala / Ula'ula / Poni number), `+0xAC` u32 Alola list rows (126), `+0xB4` u32 cursor index (list, grid, entry). Verified by heap dump diffs at several cursor positions and live against screenshots (`scenarios/native-pokedex.txt`).

## Options, Trainer Passport, Battle Video (runtime, 2026-11-02)

Read by the native screens (`Sources/Screens/SubScreens.cpp`, [NATIVE-SCREENS.md](NATIVE-SCREENS.md)). RTTI vtables found by the `N8Savedata…E` type names in `code.bin` (typeinfo → vtable), objects found by searching a heap dump (`dump 30000000 4184000`) for them.

- **ConfigSave** (`Savedata::ConfigSave`, vtable `0x64F064`) = SaveData `+0x3B30` (SaveData `0x33011928`, ConfigSave `0x33015458` in the dump). Its word at `+4` holds the Options: text speed bits 0-1 (0 Slow, 1 Normal, 2 Fast), battle effects bit 2 (0 On, 1 Off), battle style bit 3 (0 Switch, 1 Set), button mode bits 13-14 (0 Default, 1 L=A), Party/Box bit 15 (0 Manual, 1 Automatic). Verified: this save reads `0x0002A026` (Fast, Off, Switch, L=A, Automatic, as the screen showed); all five changed on the Options screen and confirmed gave `0x00020028`. The word only changes on Confirm (or Yes to "make these changes?"); while the screen is open it keeps a working copy that was not found as a clean value (only in its layout animation data), so the native screen reads the values from the bottom screen instead. Bit 5 (set) and bit 17 (set) were not identified.
- **Trainer Passport** (scene `TrainerPass`): scene object `*(*(0x6A599C))` (`*(*sceneManager)`) `+0x14` = the passport's work object (vtable in its CRO), which holds: `+0x10` the stamp book (its `+0x1C` is the scene object, `+0x20` the page, 0-based: 2, 3, 4 on pages 3/15, 4/15, 5/15 in three dumps; the game remembers it between visits), `+0x14` MyStatus, `+0x20` play time (vtable `0x64F480`: u16 hours `+4`, u8 minutes `+6`, u8 seconds `+7`; read 14:44:30 while the card showed 14:44), `+0x24` Misc (`Savedata::Misc`, vtable `0x64F384`: money u32 `+8`, read 27068 = ₽27,068 on the card), `+0x2C` a full-load MsgData of the passport's message file (52 lines, file 107 of `a/0/3/2`: labels on lines 0-21, stamp n's name and text on lines 22+2n and 23+2n; `+0x0C` dat ID reads 0, loaded from memory). The objects are filled in a few frames after the scene shows.
- **MyStatus** (`Savedata::MyStatus`, vtable `0x64F460`) = SaveData `+0xEE8`: `+8` u32 TID | SID << 16 (`0x2C422D1C`; the card's ID No. is it mod 1000000, 534428), `+0x40` the name, UTF-16, 12 characters and a 0.
- Battle Video (scene `BattleVideoPlayer`): nothing read from memory; its states come from bottom-screen pixels (NATIVE-SCREENS.md). The save has no Battle Videos, so a list was never seen.

## Native map (runtime, 2026-11-02)

Full write-up: [MAP.md](MAP.md). Used by `Sources/Screens/Map.cpp`.

- `0x671A10` (u8): current text language, 0-9 (RE2, static). Read 2 in the field (instance p25); the map then read `a/0/3/2` from the game's RomFS and its file 72 line 12 / 20 / 21 decoded to "Route 2" / "Hau’oli City" / "Shopping District", the in-game banners (RE4). Picks the place-name GARC `a/0/3/<lang>`.
- Not an address but relied on: `ARCHIVE_ROMFS` (`FSUSER_OpenFileDirectly`, binary path {0, 0, 0}) opens the running title's RomFS at IVFC level 3 from the plugin in Azahar 2126.2 (`map: place names: 234 lines from a/0/3/2 file 72 (17 ms)`).
- Field work zone (`*(*(gm+0x2C)+0x88)+0x1C`) right after a door: reads 0 for ~3 readings (4 frames apart) with the old position while the entry zone (`location+0x0A`) already has the new zone (79 → 0 → 7, logged); settled readings had zone == entry zone.
- Zone data `+0x4E` (u8): the zone's map, Rotom map index + 1 (1-4 the islands, 5 Aether Paradise, 6-21 small maps). From a runtime dump of all 380 entries read against the place names; zones 7, 13, 79 = 1 with Rotom map index 0. Only the 56 outdoor island zones have no fixed map point (`+0x44/+0x48`). docs/RE4-map.md.
- Bottom screen in the field: Rotom's eyes reach y 97 at the sides of the map window (29,84 – 290,231); the map copies (30,104) 260 × 128.
