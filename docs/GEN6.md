# Gen 6 (X/Y, Omega Ruby/Alpha Sapphire) support

Status: Omega Ruby base v0 is in the game table as panel-only support (bottom-screen panel, touch cursor, button injection and GPU quad). No scene manager or USUM engine features. Title version reported in Azahar: **0**. Other Gen 6 titles remain unsupported.

Boot test (2026-11-23, Azahar, instance created with `instance.py create NAME --game oruby`, ROM passed with `--rom`): the log reads `Pokemon Omega Ruby 000400000011C400 v0 (0x0000)`, every hook `ok` (gspSetFramebuffer `already hooked`, as on USUM), `hooks: all ok`, and the gpuquad, cursor and buttons hooks install. On the language-select screen: `mode panel` draws the bottom screen in the top screen's corner with the cursor (readback ok, GPU scratch allocated); an injected Down moves the game's selection from English to Spanish; an injected touch is read by the game (`last seen 160,60 touching 1`; that screen has no touch target).

Play-check (2026-11-23, new game, nothing saved): the intro runs with injected A presses; the name keyboard on the bottom screen takes injected taps (Delete, letters and Enter all register on the key under the cursor). In the overworld (moving-truck room, PokéNav Plus AreaNav on the bottom screen): `mode panel` and `mode full` both draw the bottom screen correctly; a tap on the PokéNav's second app opens the Trainer Card; B closes it; `stick 150 0` walks the player out of the truck into Littleroot Town. The panel log showed `readback ok 0/1` right after switching mode and `8/9` later, so the first readback after a mode switch failed but later ones succeeded; not looked into. Not checked yet: the L+R / ZL keys with a real pad.

First battle (2026-11-24, from the save, auto on): the story from Littleroot (Mom, the clock, the neighbour's house, May) to Route 101 played with injected A presses and circle-pad walking. Scenes: `DllPoke3Select` (starter choice, panel) → `DllBattle` (panel) → `DllField` (off) → `DllStrInput` (nickname, full) → `DllField`. In the battle (Torchic Lv. 5 vs Poochyena Lv. 3) the panel showed the bottom-screen battle menu; FIGHT and Ember were tapped through the touch cursor and the battle was won in three turns by touch alone. The nickname keyboard took 13 Delete taps and Enter.

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

Omega Ruby loads CRO modules: `code.bin` names 94 `rom:/Dll*.cro` files (`DllField`, `DllBattle`, `DllBag`, `DllBox`, `DllContest`, …) plus `rom:/.crr/static.crr`. The plugin names scenes by the CRO that owns the current scene's vtable, and that carries over, with `Dll…` names in place of USUM's.

**Current scene (Omega Ruby v0):** `*(*(*(*(0x62F7C4) + 0x14) + 0x10) + 0x0C)` → its vtable → the CRO that owns it.

| Step | What | How it was found |
|---|---|---|
| `0x62F7C4` | Global → `gamesystem::GameManager` (`0x6C` bytes) | Its constructor `0x106ECC` asserts the global is null, then stores `this` there (singleton). Allocated and constructed at `0x1061EC`–`0x106204`. |
| GameManager `+0x14` | `gamesystem::GameProcManager*` (`0x1C` bytes, vtable `0x5D4570`) | Created inline at `0x106B38`–`0x106B6C` (in `0x106B10`, called right after the constructor) and stored to `+0x14`. Class name from the RTTI before the vtable (`N10gamesystem15GameProcManagerE`, base `N3gfl4proc7ManagerE`). |
| ProcManager `+0x10` | Current entry: u8 state `+0`, scene object `+0x0C` | The manager's per-frame main (`0x3AB3A0`, vtable slot 0) reads `+0x10`, switches on its state byte and calls the vtable of `+0x0C`. |
| ProcManager `+0x0C` | Next scene (set by `0x3AB384`, flag u8 `+8`) | Every scene launcher (87 callers, e.g. `0x367D44` for `DllField`) loads its CRO (`0x36F28C`), allocates and constructs the scene, stores the manager at scene `+0x1C` and the CRO at scene `+0x10`, then tail-calls `0x3AB384(manager, scene)`. |

Checked at runtime (2026-11-23, `peek` from a script, nothing saved): language screen → `DllLangSelect`, title screen → `DllTitle`, name keyboard → `DllStrInput`, overworld (truck) → `DllField` (scene vtable `+0x1072C4` in a module at `0x6F3000`), Trainer Card opened from the PokéNav → `DllTrainerCase`. While the Trainer Card is open its CRO sits at `0x6F3000`, where `DllField` was, so the field module is unloaded during apps. The GameManager was at `0x08C650B4` and the ProcManager at `0x08C6A180` in both runs. The scene object address stayed `0x08C65224` across scenes (the allocator reuses the slot), so the plugin must compare vtables, not object addresses.

**Wired:** `GameInfo::scene` (`ScenePath`) describes the pointer chain, and `CurrentScene()` uses it for scene commands, logging and automatic panel selection. Omega Ruby now has `FeatureScenes`, without the USUM engine; `Offsets.sceneManager` stays zero. Its default scene modes are `DllTitle = off`, `DllField = off` (overworld; PokeNav Plus on the bottom screen), and `DllStrInput = full` (name keyboard needs touch). Party/Bag may live in CROs here (`DllBag.cro` exists) rather than in the main image.

Boot test (2026-11-23, new game, `auto on`, nothing saved). Scene log and the mode the auto panel chose:

| `scene:` | Mode | What's on screen |
|---|---|---|
| `DllLangSelect` | panel (default) | Language select |
| `DllFieldDemo` | (no switch logged) | Opening demo |
| `DllTitle` | off | Title screen |
| `DllSangoIntro` | panel (default) | Birch's intro; the bottom screen is black here |
| `DllStrInput` | full | Name keyboard; the taps type on it |
| `(none)` → `DllField` | off | Overworld (truck) |
| `DllTrainerCase` | panel (default) | Trainer Card from the PokéNav |
| `DllField` | off | Back in the overworld after B |

The USUM X-menu handling did not run (the `auto:` status line has no X-menu or field-state text on this game). On Ultra Sun with the same build, `native-map` passes, and `xmenu-touch-close` passes (2 of 4 runs; the 2 failures were a random Rotom chat on the bottom screen catching the tap meant for the map). The previous build passed 3 of 3. The `scene:` sequence in the plugin log (TitleMenu, FieldRo, TownMap, FieldRo, Party `main:00647FCC`, FieldRo) is identical on both builds.

**Gap: the X menu.** X in the overworld opens the menu on the **bottom screen** (Bag, the trainer's name, Save, Options), but the scene stays `DllField`, so the auto panel keeps the panel off and the menu can't be seen on the top screen until the player presses L+R. USUM solved the same case (`FieldRo.xmenu`) with a field-state word read from the field scene. Omega Ruby needs its own signal.

**X menu signal (found 2026-11-23, not wired yet): u32 `0x66F9CC` is 1 while the X menu is open, 0 otherwise.** It's in the main image's bss, so no pointer chain is needed. Its neighbour `0x66F9D0` moves with it (`0x00010101` open, `0x00060101` closed).

How it was found:
- **Not in the scene structures:** 16 KB of the `DllField` scene object, the GameManager (`0x100`), the GameProcManager (`0x40`) and the current entry were byte-identical with the menu open and closed (3 dumps each).
- **No module of its own:** the menu loads no CRO; the module list is just `DllField` either way, although a `DllStartMenu.cro` exists.
- **Global data diff:** `snap` of the global data (`0x5EC000`–`0x6AF000`) ×3 open vs ×3 closed gave 45 consistent words, mostly bottom-screen layout floats and double-buffer swaps. Four looked like flags: `0x61741C`, `0x66E154`, `0x66E158` and `0x66F9CC`. Only `0x66F9CC` kept its meaning when re-read; the other three flip for other reasons (e.g. `0x61741C` read 0 and later 1 with the menu closed both times).

`0x66F9CC`, each reading paired with a screenshot of the bottom screen:

| State | `0x66F9CC` |
|---|---|
| Overworld | 0 |
| X menu open (pressed X) | 1 |
| Closed with B | 0 |
| Closed by tapping the menu's ✕ | 0 |
| After walking | 0 |
| In the Bag (scene `DllBag`) | 1 |
| Back from the Bag, menu open again | 1 |
| Save prompt (on the bottom screen) | 1 |
| After saving, back in the overworld | 0 |

Not checked: dialogue and shops, the PokéNav's other apps, and whether it is also 1 for other bottom-screen overlays in the field.

Now wired: `fieldMenuFlag` in the game table supplies the address, AutoPanel reads it on non-USUM games, and `DllField.xmenu = panel` is the default; the reviewer will boot-test it.

With the flag wired in (2026-11-24, from the save, auto on): X turns the panel on (`auto: …, menu open`), B and the menu's ✕ turn it off, and a Bag round trip (`DllBag` panel → back to the open menu, panel) works. **Flicker:** once, while the field was loading after Continue, the flag read 1 for about two frames (log: panel on at top frame 1995, off at 1996). USUM needed `FieldSettle` for the same kind of blip; Omega Ruby may need a short hold too.

The `oruby1` test instance has an in-game save, last made 2026-11-24 in Birch's lab (1:34 play time) with Torchic in the party, so later tests can Continue instead of replaying the intro. `tools/startpoint.py` waits for `FieldRo` after loading, so it doesn't support Omega Ruby start points yet.

## Next steps

1. Pick the target builds (latest update per game) and get dumps of X, Y and Alpha Sapphire plus updates.
2. Port additional game-specific constants only as their features gain Gen 6 support. Image layout and feature flags are already per-game; Omega Ruby enables only the core features.
3. Check L+R and ZL with a real pad on Omega Ruby.
4. Hold the X menu flag for a few frames before the auto panel acts on it (the load-time flicker above). Consider `DllSangoIntro = off` (its bottom screen is black). Add defaults for more `Dll…` scenes as they are met (boxes, summary).
