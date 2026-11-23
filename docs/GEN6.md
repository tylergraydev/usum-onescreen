# Gen 6 (X/Y, Omega Ruby/Alpha Sapphire) support

Status: Omega Ruby base v0 is in the game table as panel-only support (bottom-screen panel, touch cursor, button injection and GPU quad). No scene manager or USUM engine features. Title version reported in Azahar: **0**. Other Gen 6 titles remain unsupported.

Boot test (2026-11-23, Azahar, instance created with `instance.py create NAME --game oruby`, ROM passed with `--rom`): the log reads `Pokemon Omega Ruby 000400000011C400 v0 (0x0000)`, every hook `ok` (gspSetFramebuffer `already hooked`, as on USUM), `hooks: all ok`, and the gpuquad, cursor and buttons hooks install. On the language-select screen: `mode panel` draws the bottom screen in the top screen's corner with the cursor (readback ok, GPU scratch allocated); an injected Down moves the game's selection from English to Spanish; an injected touch is read by the game (`last seen 160,60 touching 1`; that screen has no touch target). Not checked yet: the overworld, battles, and the L+R / ZL keys with a real pad.

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

`code.bin` size `0x530000`, SHA-256 `d587c98a…bc1a37`. Image end = `0x5EC000 + 0x439F4 + 0x7EC2C` = `0x6AE620` → `CroArea` would be `0x6AF000` (USUM: `0x6D6000`). These differ from USUM; CRO lookup, snapshot ranges and the vtscan rodata range now use the per-game image layout.

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
| gxProcessList | `0x164988` | Unique match of USUM's prologue (`E92D43F0 E1A05000 E3A00000 E3520000`). Function body identical to USUM `0x279D5C` except BL offsets and one literal. |
| gxState | `0x62AB70` | Five getters agree, matching USUM's getters for `0x69EA68`: `0x148820` ldr +`0x1C` / ldrsb +`0x75`; `0x164F64` ldr +`0x1C` (the gspGetManager called by gspSetFramebuffer); `0x125EB0` ldrsb +1; `0x1744A0` ldrsb +2; `0x148884` ldr +`0x0C`. |

So every SDK-level address the panel and touch cursor need exists in Omega Ruby with the same code. The boot test above confirmed the prologues at runtime.

## Scene detection

Omega Ruby loads CRO modules: `code.bin` names 94 `rom:/Dll*.cro` files (`DllField`, `DllBattle`, `DllBag`, `DllBox`, `DllContest`, …) plus `rom:/.crr/static.crr`. The plugin names scenes by the CRO that owns the current scene's vtable, so that approach may carry over, with `Dll…` names in place of USUM's. The Gen 6 scene/proc manager global still has to be found (Ghidra), and Party/Bag may live in CROs here (`DllBag.cro` exists) rather than in the main image.

## Next steps

1. Pick the target builds (latest update per game) and get dumps of X, Y and Alpha Sapphire plus updates.
2. Port additional game-specific constants only as their features gain Gen 6 support. Image layout and feature flags are already per-game; Omega Ruby enables only the core features.
3. Play-check Omega Ruby in the overworld and a battle with the panel, and with a real pad (L+R, ZL).
4. Find the Gen 6 scene manager for the auto panel.
