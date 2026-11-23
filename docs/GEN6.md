# Gen 6 (X/Y, Omega Ruby/Alpha Sapphire) support

Status: static probe only (2026-11-22). Nothing in the plugin supports Gen 6 yet; `Sources/Game.cpp` still refuses these titles.

## Dumps on hand

| Game | File | Notes |
|---|---|---|
| Omega Ruby (USA) | Cartridge dump (`.3ds`, Rev 2) | NCSD, already decrypted (NCCH flags[7] = `0x04`, NoCrypto). Title ID `000400000011C400`, NCCH version 2. Base game only, no update applied. |
| X, Y, Alpha Sapphire | none | |
| Any Gen 6 update | none | Which update the plugin should target is not decided. |

`tools/extract_code.py` reads CIAs only. The Omega Ruby `code.bin` was extracted from the `.3ds` partition 0 with a one-off script (same ExeFS + BLZ steps, reusing `blz_decompress`) into `analysis/private/oruby-base/` (gitignored).

## Omega Ruby base: exheader

Exheader name `sango-1`, code compressed.

| Segment | Address | Size |
|---|---|---|
| text | `0x100000` | `0x479604` |
| rodata | `0x57A000` | `0x71A14` |
| data | `0x5EC000` | `0x439F4` |
| bss | | `0x7EC2C` |

`code.bin` size `0x530000`, SHA-256 `d587c98a…bc1a37`. Image end = `0x5EC000 + 0x439F4 + 0x7EC2C` = `0x6AE620` → `CroArea` would be `0x6AF000` (USUM: `0x6D6000`). These differ from USUM, so `CroArea`, `SnapStart`/`SnapEnd` and the vtscan rodata range have to move into the per-game table.

## sigscan with `signatures/usum.json`

`py -3 -I tools/sigscan.py analysis/private/oruby-base/code.bin signatures/usum.json` fails overall, but per signature:

| Target | Sig 1 (function body) | Sig 2 (caller) | Sig 3 (caller) | Result |
|---|---|---|---|---|
| gspSetFramebuffer | 1 match, `0x148720` | 1 match, but its `bl` goes to `0x164D2C` (gxDisplayTransfer) | — | **Found at `0x148720`.** Body identical to USUM's for at least the first 28 words. Its only caller is a stack-argument thunk at `0x1426C0` (`bl` at `0x1426D4`); the 5-word thunk before it differs from USUM, so sig 2 needs rewriting. |
| hidTouchRead | 1 match, `0x2E1DE0` | 1 match → `0x2E1DE0` | 0 matches | **Found at `0x2E1DE0`** (two signatures agree). |
| hidPadRead | 1 match, `0x2E1C0C` | 1 match → `0x2E1C0C` | 0 matches | **Found at `0x2E1C0C`** (two signatures agree). |
| sceneManager | 0 | 0 | — | Not found. Gen 6 game code, needs Ghidra. |
| Party, Bag | 0 | — | — | Not found. Gen 6 game code. |

GX calls (not in the signature file; found by USUM's prologue words):

| Target | Omega Ruby | Notes |
|---|---|---|
| gxDisplayTransfer | `0x164D2C` | Unique match for `E92D43F0 E24DD024 E28D4040 E1811802`; one caller, thunk `bl` at `0x158438`. |
| gxTextureCopy | `0x164A54` | First 15 words identical to USUM's `0x279E28` (GX command 4). The other prologue match, `0x164914`, is GX command 2, not TextureCopy. |

So every SDK-level address the panel and touch cursor need exists in Omega Ruby with the same code. **Not checked yet:** prologue words at runtime, and whether the hooks behave the same in game (no Omega Ruby boot has been tried).

## Scene detection

Omega Ruby loads CRO modules: `code.bin` names 94 `rom:/Dll*.cro` files (`DllField`, `DllBattle`, `DllBag`, `DllBox`, `DllContest`, …) plus `rom:/.crr/static.crr`. The plugin names scenes by the CRO that owns the current scene's vtable, so that approach may carry over, with `Dll…` names in place of USUM's. The Gen 6 scene/proc manager global still has to be found (Ghidra), and Party/Bag may live in CROs here (`DllBag.cro` exists) rather than in the main image.

## Next steps

1. Pick the target builds (latest update per game) and get dumps of X, Y and Alpha Sapphire plus updates.
2. Move every game-specific constant (the ~25 files with hardcoded addresses, `CroArea`, snap ranges) into the per-game table, with per-feature flags, so a Gen 6 entry can enable panel + cursor only.
3. Add an Omega Ruby entry with the five addresses above and boot it in a test instance; check the hook log.
4. Find the Gen 6 scene manager for the auto panel.
