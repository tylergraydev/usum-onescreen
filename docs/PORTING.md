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

## Ultra Moon checklist

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
