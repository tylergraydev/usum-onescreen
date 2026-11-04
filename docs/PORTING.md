# Porting to another build (Ultra Moon, other updates)

The plugin is tied to exact addresses in one build of the game. `tools/sigscan.py` finds them again in another build's `code.bin` from byte signatures (`signatures/usum.json`), so a new build is mostly: extract, scan, paste, boot, check the log.

## What is build-specific

| What | Where | Found by |
|---|---|---|
| Title ID, title version | `Sources/Game.cpp` (`s_games`) | `tools/extract_code.py` (`info.json`) and the boot log |
| `gspSetFramebuffer`, `hidTouchRead`, `hidPadRead` + 4 prologue words each | `Sources/Game.cpp` | `sigscan.py --emit-cpp` |
| `sceneManager` | `Sources/Game.cpp` | `sigscan.py --emit-cpp` |
| Party and Bag scene IDs (`main:<vtable>`) | `Includes/Settings.hpp`, `Sources/Settings.cpp` (defaults text), `README.md`, `tests/settings_test.cpp` | `sigscan.py` (static), confirmed by the `scene:` log line (runtime) |
| End of the main image (`CroArea`, CRO modules load above it) | `Sources/Scene.cpp` | exheader: data address + data size + bss size, rounded up to 0x1000 |
| Global data range for `snap` (`SnapStart`/`SnapEnd`) and the rodata range for `vtscan` | `Sources/main.cpp` | exheader: data address, rodata address, the same end as above |

CRO scene names (`FieldRo`, `Zukan`, `Battle`, `TitleMenu`) come from the module headers at runtime, so they need no porting, though they should be confirmed in the log. Everything else in `docs/OFFSETS.md` (GSP/HID internals, RE2 text addresses) is reference material that the plugin does not use.

For Ultra Sun v1.2 the build-specific values are: text `0x100000`, rodata `0x5BC000`, data `0x669000` (size `0x3CD88`), bss `0x301BC`, image end `0x6D5F44` → `0x6D6000`.

## Update version 0x0820 and Ultra Moon (checked 2026-11-03)

**Genuine v1.2 is `0x0820`.** Three Ultra Sun v1.2 update files were compared:

| File | TMD version | `code.bin` SHA-256 |
|---|---|---|
| `ultra-sun-update-v1.2-0x0800-decrypted.cia` (the one the plugin was built and tested on) | `0x0800` (2048) | `421774f2…ce16200f` |
| `ultra-sun-update-v1.2-encrypted.cia` (Tyler's encrypted original) | `0x0820` (2080) | `421774f2…ce16200f` |
| `ultra-sun-update-v1.2-0x0820-decrypted.cia` (another decrypted dump) | `0x0820` (2080) | `421774f2…ce16200f` |

The code is byte-identical, so every hook target sits at the same address and the same offset table serves both. Only the patched copy's TMD says `0x0800`. `Game.cpp` therefore lists Ultra Sun twice: `0x0820` (genuine) and `0x0800` (that patched copy, still used by the test instances and start points).

In Azahar (instance `us20`, genuine update installed): the plugin before this change logged `USUM One Screen: unsupported game 00040000001B5000 v2080 (0x0820)` and did nothing. With the `0x0820` entry it logs `Pokemon Ultra Sun 00040000001B5000 v2080 (0x0820)` and `hooks: all ok`.

**The new-game stall was the patched update.** With the genuine `0x0820` update and no save, a new game passes the language screen (`LangSelect` → "Your settings have been saved." → Professor Kukui's intro, `scene: main:0064787C`). With the patched `0x0800` update it stalls there (docs/PLAYER-APP.md). Since the code is identical, the patched file's RomFS is the likely cause; it was not diffed further.

**Azahar 2126.2 will not install encrypted CIAs.** Even with `aes_keys.txt` in `sysdata`, `azahar.exe -i` exits with code 7 and logs `… is encrypted! Aborting...`. `tools/extract_code.py` and `tools/romfs_text.py` also need NoCrypto content. For this check the encrypted CIAs were decrypted locally with the keys in `Y:\3ds\aes_keys.txt` (title key with the common key, then NCCH AES-CTR; the exheader SHA-256 in the NCCH header was checked). The script is not in the repo, and its output is under `analysis/private/`.

**Ultra Moon USA v1.2:** base `00040000001B5100` (v0), update `0004000E001B5100` TMD version `0x0820`. The exheader layout is identical to Ultra Sun's (text `0x100000` size `0x4BB818`, rodata `0x5BC000`, data `0x669000` size `0x3CD88`, bss `0x301BC`, not compressed), so `CroArea`, `SnapStart`/`SnapEnd` and the vtscan rodata range need no change. `code.bin` SHA-256 is `8854abc9…6949084a`.

- `sigscan.py` resolves every target uniquely, at the same address as Ultra Sun: gspSetFramebuffer `0x279BB4`, hidTouchRead `0x2826F0`, hidPadRead `0x282548`, sceneManager `0x6A599C`, Party `main:00647FCC`, Bag `main:006488C8`, with the same prologue words. `"umoon-1.2"` is now in each target's `known`.
- A byte diff of the two `code.bin`s: `0x279000`–`0x283000` (all hook targets, gxDisplayTransfer `0x27A5BC`, gxTextureCopy `0x279E28`, the GX queue push) is identical. Elsewhere in text some functions moved by 4 bytes. In rodata and data, 3125 of the differing words are code pointers 4 higher and the rest are 58 small constants. No vtable, global or table moved. So every main-image address the plugin hardcodes (Party, Bag, Box, Pokédex, Rotom, sub-screen vtables, `GameManager` `0x6A59C4`, the message globals, `LanguageByte` `0x671A10`, `FormTable` `0x5D63DC`) holds on Ultra Moon. FormTable's neighbour at `0x5D6250`–`0x5D6268` differs, which looks like version-exclusive data; the plugin does not read it.
- CROs (update RomFS, 132 modules, same file list): 123 are byte-identical, including `Battle`, `Bag`, `Box`, `WazaOshie` and `Kawaigari`. `FieldRo`, `FieldMenu`, `Zukan`, `TrainerPass`, `Live` and `SequencePlayer` differ only in the header hash (`0x20`–`0x3F`) and 1–10 single-byte immediates. `KisekaeData`, `MovieLib` and `Title` differ more. The plugin reads none of those three. So the CRO-relative offsets (Battle, Bag, FieldRo `+0xD735C`, WazaOshie) hold.
- Scene IDs: CRO names come from the modules (same names). Party/Bag `main:` IDs are the same as Ultra Sun's, so the settings defaults need no Moon entries.

`Game.cpp` has an Ultra Moon `0x0820` entry using the shared table.

**Seed.** The Ultra Moon base CIA uses seed crypto (NCCH flag `0x20`). The seed came from Nintendo's public seed server (`kagiya-ctr.cdn.nintendo.net/title/0x00040000001B5100/ext_key?country=US`, 16 bytes, fetched by hand). It passed the NCCH seed check (`0x114`), every ExeFS file SHA-256 and the full RomFS IVFC hash tree (896160 level-3 blocks, 0 bad). The seed is kept in `analysis/private/seeddb/`.

**In Azahar (instance `um1`, Moon base and update installed from local decrypted copies):**
- Boot log: `USUM One Screen: Pokemon Ultra Moon 00040000001B5100 v2080 (0x0820)`, every hook `ok`, `hooks: all ok`. The game assets load (font, icons, moves, names from `a/0/3/2`). `instance.py start` needs `--rom` with the installed `.app`'s **absolute** path; a relative one fails, because Azahar runs from the instance folder.
- **New game: reaches `FieldRo`, then stalls.** It goes LangSelect → Kukui intro → `HeroSelect` → name (game keyboard) → opening cutscene → `FieldRo`, but the opening bedroom scene never moves on. Meowth stays at the room's edge, field state 17, while frames keep running. This happened twice, once with the panel used and once without it. The same steps on Ultra Sun (genuine `0x0820`, also with the plugin) pass that scene (Meowth's "Mwaaawwwrrrrr!!!"). The RomFS checks out, so this isn't the decrypt. Not settled: whether it happens without the plugin. Driving the intro without the plugin needs a touch-capable input movie, and `tools/movie.py` has no touch input.
- **Field through a Sun save:** USUM saves share one format, and Moon loaded a copy of the `pokecenter` start point's Ultra Sun save (title menu: Continue, Route 2). From there:
  - Panel (`mode panel`): draws Moon's bottom screen (the Rotom map) with the cursor, GPU readback ok. The L+R key itself can't be tested with injected input (it reads the real pad).
  - **Native X menu: needed a fix.** Ultra Moon's X-menu background is light blue (animated; measured 137..161 / 219..252 / 244..255 at (10,120)), not Sun's yellow, so the pixel probe never matched (`xmenu: … the native menu isn't waiting for input`). `Sources/Actions.cpp` now accepts either background. The Save bar, the Y corner, the page-2 arrow and the red game cursor are the same colours in both games. After the fix: `xmenu: shown (game page 1)`, the cursor moves, and Pokémon → `action: xmenu-pokemon ok`, `party-ui: shown`; B returns to the native menu, B closes it.
  - Native Party (`main:00647FCC`): `party-ui: shown`, party read correctly (Torracat L17 … Buneary L12); the Party list probe colours match Sun's.
  - Native Bag (`main:006488C8`): pocket icons, item descriptions and the party sidebar all shown.
- Not checked on Moon: Pokédex, map, boxes, battles, keyboard, the remaining X-menu entries, and any other screen that checks bottom-screen colours. Any colour probe could hit a Moon theme difference like the X menu's.

## Ultra Moon checklist

(Done for Ultra Moon v1.2; see the section above. Step 6's full play-check is not.)

1. **Dump and extract.** Dump the base game and the v1.2 update as decrypted CIAs (no Ultra Moon dump exists yet). Extract the update's code:
   ```
   py -3 -I tools/extract_code.py <ultra-moon-update-decrypted.cia> analysis/private/umoon-v1.2
   ```
   `info.json` gives the title ID of the CIA you passed. For Ultra Sun's update it reads `0004000E001B5000`; the game's own ID (the one `Process::GetTitleID()` returns and `Game.cpp` stores) is `00040000001B5000`. For Ultra Moon expect `0004000E001B5100` / `00040000001B5100`. **Unverified:** that ID is a guess from Ultra Sun's pattern; `Game.cpp` already has a placeholder entry for it. Also note `title_version` (Ultra Sun v1.2: 2048 = `0x0800`), `data`, `bss_size`, and check that `text.address` is still `0x100000`.

2. **Scan.**
   ```
   py -3 -I tools/sigscan.py analysis/private/umoon-v1.2/code.bin signatures/usum.json --emit-cpp --name "Pokemon Ultra Moon" --title-id 00040000001B5100 --version 0800
   ```
   Every target must print `ok` with one match per signature; the tool exits non-zero and prints `FAIL …` if a signature matches zero or several times, or if a target's signatures disagree. On success it prints a `GameInfo` entry (addresses plus the four prologue words read from that code.bin) and the `main:` scene IDs.

   If a signature fails, nothing is pasted. Open the Ultra Sun address from the signature's `comment` next to the Ultra Moon candidates (`arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x100000 --start-address=… --stop-address=… code.bin`), see which instruction differs, and wildcard it or pick different words. Then re-run against Ultra Sun to keep it unique there:
   ```
   py -3 -I tools/sigscan.py analysis/private/usun-v1.2/code.bin signatures/usum.json --expect usun-1.2
   ```
   When Ultra Moon resolves, add `"umoon-1.2": "0x…"` to each target's `known` so later edits can be checked against both builds.

3. **Update the code.**
   - `Sources/Game.cpp`: replace the Ultra Moon placeholder with the emitted entry. Set `confirmed` to `true` only once the boot log shows the title ID and version (step 4).
   - `Sources/Scene.cpp` `CroArea`, `Sources/main.cpp` `SnapStart`/`SnapEnd` and the rodata range in the `vtscan` check: today these are single constants for Ultra Sun. If Ultra Moon's exheader layout differs, move them into `Offsets`/`GameInfo` rather than changing them in place.
   - Party/Bag scene IDs: if Ultra Moon's vtables differ from `main:00647FCC` / `main:006488C8`, add the Ultra Moon IDs to the defaults beside the Ultra Sun ones. Scene IDs are looked up by name (`ModeForScene`), so both games' IDs can live in one list.
   - `docs/OFFSETS.md`: add an Ultra Moon column or section with the addresses and how they were found (the sigscan output).

4. **Boot-log checks** (Azahar, Ultra Moon v1.2, plugin enabled). `usum-onescreen.log` at the SD root is rewritten each boot:
   - First line: `USUM One Screen: Pokemon Ultra Moon 00040000001B5100 v2048 (0x0800)`. If it says `unsupported game …`, the title ID or version in `Game.cpp` is wrong; the line shows the real ones.
   - One line per hook: `gspSetFramebuffer: ok at …` (on Ultra Sun it says `already hooked (b …)`: another hook, probably CTRPF's OSD, is already there; see `docs/OFFSETS.md`), `hidTouchRead: ok at …`, `hidPadRead: ok at …`, then `hooks: all ok`. Any `MISMATCH` means the address or prologue is wrong, and nothing is patched.
   - Scene lines while playing: `scene: TitleMenu` at the title, `FieldRo` in the overworld, `Zukan` (Pokédex), `Battle`. Each is a CRO module name; an empty or wrong name here means `sceneManager` or `CroArea` is wrong.

5. **Re-find the `main:` scene vtables at runtime.** Party and Bag are classes in the main image, so their scene ID is `main:<vtable address>`, which moves between builds. Sigscan already prints a static candidate for each; confirm it in the game:
   1. In the overworld, open the X menu and then Party. The log gets `scene: main:XXXXXXXX`. That is the Party ID.
   2. Back out, open Bag, and note its `scene: main:…` line the same way.
   3. Both IDs should equal sigscan's `Party`/`Bag` values. If they differ, trust the log: the constructor signature found a different class. Fix the signature (step 2).
   4. You can also put `scene` in `/usum-cmd.txt` to log the current scene on demand.
   5. To identify any other unknown `main:` scene, check which constructor stores that vtable with `py -3 -I tools/find_refs.py code.bin 100000 <text end> <vtable>`.

6. **Play-check** the per-scene behaviour from `docs/PLAYING.md` (panel modes in field, Party, Bag, Pokédex, battle) before marking Ultra Moon supported in `README.md` and `CHANGELOG.md`.

## How sigscan works

Each target in `signatures/usum.json` has one or more signatures. Each one must match exactly once (4-byte aligned, whole file), and all of a target's signatures must agree on the address:

- `"at"`: the signature is the function itself; the address is the match plus `at`.
- `"bl"`: the signature is a known caller; the address is the destination of the `BL` at that byte offset. The hook functions are pinned both by their own prologue and by their callers (`TouchPanelReader::Read`/`ReadLatest`, `PadReader::Read`/`ReadLatest`, the gsp thunk). Two bodies that differ only in register allocation (`hidTouchRead` and a sibling reader at `0x2829D8` in Ultra Sun) cannot both pass both checks.
- `"ldr"`: the signature contains `LDR Rt, [PC, #imm]` at that byte offset; the address is the literal-pool word it loads. `sceneManager` comes from the scene manager constructor and its getter, and Party/Bag come from their scene constructors, which store the vtable at `+0`.

Patterns are ARM words written as a disassembler shows them, with `?` per wildcard nibble. Wildcard what moves when code is relinked: `BL`/`B` offsets (`EB??????`, `0A??????`), PC-relative literal offsets (`E59F5???`). Keep the fixed parts that describe the function: register use, stack frame size, struct field offsets.

`tests/sigscan_test.py` (run by `make -C tests`, so in CI) builds a synthetic image from the signature file and checks placement, duplicate and missing cases. Given an Ultra Sun v1.2 code.bin (`make -C tests CODE_BIN=…`, or `SUNNYMOON_CODE_BIN`), it also checks every known address and scans a shifted copy: padding inserted before a caller, before `gspSetFramebuffer` and before `hidPadRead`, with every crossing `B`/`BL` relinked and the data/vtable literals moved. Every target must follow.
