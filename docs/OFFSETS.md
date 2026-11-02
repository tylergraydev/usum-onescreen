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
| Loading / transition | (none) | between every scene |

Found by: `vtscan` (plugin: every global heap pointer → first 64 words → objects with a rodata vtable), diffed with `tools/vtdiff.py` across scenes, then `peek` on the manager showed vtables outside the main image, and Azahar's log (`CRO "…" loaded at …`) named the modules.

Default views (`Includes/Settings.hpp`): full where the bottom screen is the whole scene and the top shows little (Party, Box, TownMap, Config, Kawaigari); panel where the top still says something (Status: stats on top, moves below; TrainerPass; BattleVideoPlayer; Zukan; Bag; Battle) and for the X menu; off where the bottom screen is unused (TitleMenu, FieldRo walking, talking, shopping).

Not reached on this save: the nickname keyboard (`StrInput`; no rename without a permanent change), trainer and double battles, Rotom's own menus (Roto Loto, Rotom chat: they only come up when Rotom offers them; not reached), the QR Scanner's screen (Azahar hangs).

**Field state** (`Sources/Scene.cpp` `FieldState()`): the FieldRo scene object (`*(*(0x6A599C))`, vtable `0x7B4F44`) holds an object whose vtable is FieldRo `+0xD735C` (`0x7B635C` with FieldRo at `0x6DF000`). Its offset depends on the map: `+0x618` in the Route 2 Pokémon Center, `+0x3DC` on Route 2 (same scene class; the members before it differ), so the plugin finds it by that vtable in the scene object's first `0x800` bytes (one match in both dumps). Its word at `+4` is 0 while walking, 4 while a bottom-screen menu is up (X menu, Quick Link, save dialog, Festival Plaza's menu; also for a moment while an app Rotom opened starts or closes) and 0x11 while talking or in a shop. Found with `dump` of the scene object, X menu open ×3 vs closed ×3 (only `+0x61C` and `+0x720` differed; `+0x720` stayed 0 after returning from Party with the menu open); checked with the menu closed by B and by touch, after Party/Summary/Options/Pokémon Refresh, in Festival Plaza and after leaving it, at the Poké Mart, and at the PC. Auto panel uses it for `FieldRo.xmenu` and falls back to X/B presses only if it never reads (scenarios `xmenu-touch-close`, and `wild-battle-run` for Route 2). After entering the field it waits until the state holds for 30 frames: coming back from the Town Map it reads 4 for about 20 frames before 0. If the state stops reading, it waits at most 60 frames before falling back to keys. During a wild encounter the state is 30 (0x1E).

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

## Party and bag (runtime, 2026-11-02)

Full write-up: [RE3-party-bag.md](RE3-party-bag.md). Read by the `party` and `bag` commands (`Sources/Party.cpp`). Verified against the Party and Bag screens before and after battle damage, a Potion, two party reorders and giving a held item.

- Path: GameManager `*(0x6A59C4)` → `+0x24` GameData → `+0x04` Savedata::SaveData (vtable `0x64F4A0`) and `+0x0C` pml::PokeParty (6 PokemonParam pointers, u8 count `+0x18`).
- PokemonParam (vtable `0x649374`): `+4` core (`0xE8`, PK7 format, encrypted + shuffled in RAM), `+8` party stats (`0x1C`, encrypted with the same EC). Game crypt `0x320BBC`; accessor decrypt `0x322CB0`, re-encrypt `0x322BE0`.
- Bag: MyItem = SaveData `+8` (vtable `0x64F3C4`); pocket pointer table at MyItem `+0xE2C`: Medicine `+0xB78` (60), Items `+0x4` (427), TMs `+0x9C8` (108), Berries `+0xC68` (67), Key Items `+0x6B0` (198), Z-Crystals `+0xD74` (35), Rotom Powers `+0xE00` (11). Slot u32: item bits 0-9, count 10-19, Free Space 20-29, NEW bit 30.
- Party screen order while it is open: scene (vtable `0x647FCC`) `+0xA8` StaticWork (vtable `0x647F9C`) `+0x53` u8[6]; the party itself is reordered when the screen closes.

## Player location (runtime, 2026-11-02)

Full write-up: [RE4-map.md](RE4-map.md). Read by the `where` command (`Sources/Where.cpp`).

- `0x6A59C4`: global → game manager (`+0x1C` = the scene manager, checked). `gm+0x24` field object → `+0xCC` move model manager (vtable `0x64C628`) → `+0x0C` player model (vtable `0x64C538`): position floats `+0x3B0` (x east, y up, z south), yaw quaternion `+0x424`.
- Zone: `*(*(gm+0x2C)+0x88)+0x1C` (u32; 0 in battle). Zone data: `field+0x54` loader (vtable `0x64BDD0`) → `+0x14` table of 0x54-byte entries (`+0x18` size): `+0x1C` place-name line in message file 72, `+0x44/+0x48` fixed map point (interiors only).
- Rotom map view (vtable `0x64786C`): FieldMenu frame (FieldMenu+`0x183A0`) on the scene stack → `+0xD0`; `+0x170` map index, `+0x174/+0x178` centre (world x/z), scales `0x2CF230`/`0x2CF234` (0.0270139, 0.0270536 map px per unit).
- Verified walking the Route 2 Pokémon Center (zone 79), Route 2 (7) and Hau'oli City (13) against screenshots and the Rotom map.
