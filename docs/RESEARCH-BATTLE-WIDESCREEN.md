# Research: battle widescreen edges (backlog #27)

Research only, 2026-11-09. No plugin code changed, nothing built, no emulator run.
Game: Ultra Sun / Ultra Moon USA v1.2 (`0x0820`).

Tags used below:

- **[V-code]** seen in the disassembly/decompile of the files named.
- **[V-shot]** seen in an existing screenshot under `analysis/private/shots/` (no new run).
- **[U]** unverified: inferred, or needs a live check.

Files read: `analysis/private/usun-v1.2/code.bin` (same bytes as
`usun-v1.2-0820dec/code.bin`), `analysis/private/umoon-v1.2/cro-sun/Battle.cro`
(byte-identical to `cro-moon/Battle.cro` [V-code]: whole-file compare),
`cro-sun/Object.cro`, `cro-sun/SequencePlayer.cro`, and
`analysis/private/research/battle_rel1.bin` (Battle.cro with internal
relocations applied at base `0x100000`, from the instant-text research).

**Addresses.** `Battle+0xNNNNN` is an offset from the loaded Battle.cro header,
the same convention as `docs/FAST-TEXT.md` (it equals the file offset, and
`battle_rel1.bin` address minus `0x100000`). Imports were named by parsing the
CRO import tables: each named or anonymous import patches one
`ldr pc,[pc,#-4]` stub in segment 0; a stub sits at `Battle+0x7C + offset`.

## 1. Summary

| # | Cause | Evidence | Fix size |
|---|---|---|---|
| A | **Battle builds its own projection matrices inside Battle.cro**, with its own copy of the perspective builder (`Battle+0x736D0`). The plugin's widescreen hook covers only the main image builder `0x3631CC`, so battle 3D is never widened. The aspect comes from `CTRGL::GetDisplayWidth_ / GetDisplayHeight_` (or from the camera node's aspect field), not from anything we patch. | [V-code] call chain below; [V-shot] a 21:9 battle frame has the same 3D as a stock frame | Small to medium: one more MITM hook on CRO code, installed per battle |
| B | **Game 2D layouts are squeezed about x=200** (the existing ortho template patch). Any battle layout that is meant to cover the whole screen now covers only the middle 5:3 part. | [V-code] for the mechanism (`docs/WIDESCREEN.md`); [U] which battle layouts are full-screen | Per layout or per pane; not started |
| C | **The plugin's own pane cull** (`CalculateMtx` hook at `0x2A22EC`) hides any pane with less than 30% of its width inside x ±200. 2D effects that slide in from the edge stay hidden until they are 30% inside. | [V-code] `Sources/Widescreen.cpp` and `docs/WIDESCREEN.md`; [U] which battle panes hit it | Small: a name or screen filter |
| D | **Once A is fixed**, battle stage models, backdrops and effect planes were authored for a 30° fovy at 5:3. At 21:9 the camera sees about 40% more width, so finite models can end inside the view. | [U] needs a live run after A | Unknown; possibly "accept", possibly a narrower widescreen in battle |

**Verdict.** Feasible, and A is the root cause. Without A, "battle widescreen"
does not exist yet: at 21:9 Azahar just stretches the 5:3 battle 3D picture
1.4 times. Fixing A is about 1 day of plugin work plus a test pass. The
plugin already finds Battle.cro per battle (`CroModuleOf`) and patches it
(`FastText.cpp`). Hooking dynamically loaded CRO code is the only new risk.
D can only be judged after A runs. B and C are separate, smaller follow-ups.

## 2. What is already patched (for context)

All from `docs/WIDESCREEN.md` and `Sources/Widescreen.cpp` [V-code]:

- A MITM hook on the main-image perspective builder `0x3631CC`
  (`Perspective(out r0, pivot r1, fovy s0, aspect s1, near s2, far s3)`) swaps
  an incoming aspect of 400/240 ±0.001 for the active aspect. Its only callers
  are `0x4C1FF0/0x4C2094/0x4C20E4` in the static camera update/blend code
  (camera vtable `0x64E308`: near `+0xA4`, far `+0xA8`, fovy `+0xAC`, aspect `+0xB0`).
- Ortho templates `0x5C3180` and `0x5E8468` get left/right ±200·k.
- `nw::lyt::Pane::CalculateMtx` (`0x2A22EC`) hook: the 30% off-screen pane cull.

## 3. Cause A: battle projection bypasses the hook

### 3.1 Battle has its own perspective builder [V-code]

`Battle+0x736D0` (`battle_rel1` `0x1736D0`) is an inlined copy of the same
math as `0x3631CC`. It calls a Battle-local identity setup at `+0x735E8`, takes
fovy in `s0` and aspect in `s1` (kept in `d9`), and calls
`nn::math::SinCosFIdx`. It computes `cot(fovy/2)` and divides it by aspect at
`+0x73774`
(`EEC01AA9 vdiv.f32 s3, s1, s19`). It then has the same pivot switch
(`cmp r5,#0; cmpne r5,#4` at `+0x73768`). Same register signature as the hooked
function: `r0` out matrix, `r1` pivot, `s0..s3` fovy/aspect/near/far.
The first two words match `0x3631CC`'s:

| | word 0 | word 1 |
|---|---|---|
| `0x3631CC` (main image, hooked) | `E92D4030` push {r4,r5,lr} | `E1A05001` mov r5,r1 |
| `Battle+0x736D0` | `E92D4030` | `E1A05001` |

It has 7 callers, all inside Battle.cro (`find_calls.py` on `battle_rel1.bin`):
`+0x4218C/+0x421B8/+0x421E4` (init), `+0x426B0` (init), and
`+0x74374/+0x743A0/+0x743CC` (inside the setter below).

Searching every CRO and code.bin for the pivot pattern `E3550000 13550004`
found only two other inlined copies. One is `SequencePlayer.cro+0xE860`
(callers `+0x901C`, `+0x92AC`, both computing aspect = display
width/height). The others are in Kawaigari and FinderStudioViewer [V-code].
The SequencePlayer copy matters if battle sequences or demos render with
SequencePlayer's own camera [U].

### 3.2 Battle camera holds three projections [V-code]

The render-side camera object (call it `BtlCam`):

- **Init** `Battle+0x4211C`, called from `+0x7AB7C`: `aspect = (float)GetDisplayWidth_(s_Gp, handle) / (float)GetDisplayHeight_(...)`
  (`vdiv` at `+0x42180`). fovy `0.5235988` (30°), near `1.0`. It builds
  three matrices with far `12000.0`, then `Object.cro` rodata `seg1+0x98` and
  `seg1+0x94`, copied to `BtlCam+0x000`, `+0x150` and `+0x270`. View
  (look-at, `+0x7443C`) goes to `BtlCam+0x40`.
- **Setter** `Battle+0x7434C` `SetProjection(this, fovy s0, aspect s1, near s2, far s3)`
  rebuilds all three (same far rule) and sets a dirty byte `+0x3AC`.
  `Battle+0x742DC` sets the view.
- **Per frame** `Battle+0x3F410` (the battle camera controller, `this+0x20`→`+0xB0` = `BtlCam`):
  - Manual mode (`this+0x70 == 0`): `aspect = GetDisplayWidth_/GetDisplayHeight_` again (`vdiv` at `+0x40184`, call at `+0x40198`), fovy = `this+0x60` × π/180.
  - Animated mode (`this+0x70 != 0`): `poke_3d::model::BaseCamera::UpdateAnimation` on `this+0x34`, then reads the camera node at `this+0x28`: near `+0xC0`, far `+0xC4`, fovy `+0xC8`, **aspect `+0xCC`** (`+0x3F45C..+0x3F470`), and passes them to the setter at `+0x3FD68`.
- **Submit** `Battle+0x41E98` (called from `+0x3D430`): `gfl2::gfx::ctr::CTRGL::GetStereoMatrix`
  for each of the three projections (depth 4.0, factor 1.0). Left/right
  results go to `+0x70/+0xB0` (views `+0xF0/+0x120`), `+0x190/+0x1D0`
  and `+0x2B0/+0x2F0`. Projections 2 and 3 go to the render pipeline in
  Object.cro through anonymous imports `Object seg0+0x2020` / `+0x2120` (they
  copy proj+view into the pipeline at `+0x50/+0x90` and `+0xC0/+0x100` and call
  `System::ModelRenderPath::AddSheneCamera`). Then the module calls
  `AddSheneCamera` for another render path.

None of these calls reach `0x3631CC`, so the plugin never changes them. The
static `BaseCamera` update may still go through the hook. But battle only reads
the camera node's raw aspect field (`+0xCC`), and the hook does not change
that field, only the matrix it builds [V-code for the read; U for the node
type].

[U] Which of the three projections draws what (stage, Pokémon, effects, skybox).
The far values suggest main scene / near layer / far layer, but this was not
traced into Object.cro's pipeline.

### 3.3 Screenshot cross-check [V-shot]

`shots/20261107-033852-top.png` was taken with 2D widened: the battle timer
panes parked at x≈-255 are visible, and the HUD is squeezed toward the
centre ("Torracat" at x≈60 instead of ≈5). `shots/20261107-041608-top.png`
has the same scene with the stock HUD. In both, the trainer, Torracat and the
background trees are at the **same x positions and widths**. So in the 21:9
frame, battle 3D was rendered at 5:3 and only stretched by Azahar.
[U] The aspect setting during `041608` is not recorded. This contradicts the
handoff note "3D in battle verified at 21:9" (`.claude/handoffs/HANDOFF-2026-11-07-1018.md`),
so check it live.

### 3.4 Candidate fixes for A

1. **MITM hook on `Battle+0x736D0` (recommended).** Reuse `PerspectiveHook`
   unchanged: same signature and the same 400/240 test. Install it when
   `CroModuleOf` first sees the Battle module (as `FastText.cpp` does).
   Before installing, verify the stock words `E92D4030 E1A05001` and the
   divide word `EEC01AA9` at `+0x73774`. All seven callers are top-screen
   battle camera work, and their aspect is 400/240 [U: depends on
   `GetDisplayWidth_/Height_` returning 400/240, which the 5:3 picture implies].
   - Risk: a CTRPF `Hook` on memory that is unmapped when Battle.cro unloads.
     You must not call `Disable()` after unload, or it writes into freed or
     reused memory. You need a lifetime plan. Option 1: disable on the
     first frame the scene leaves Battle, while the module header still
     checks out. Option 2: never disable; re-`Initialize` per battle and
     track how many CTRPF hook slots this uses [U: the CTRPF hook-slot limit].
     The Battle load address was the same (`0x6DF000`) in earlier runs
     (`docs/OFFSETS.md`), but do not rely on that.
   - Pivot `r1` is 0 at every Battle call site [V-code], same as the static callers.
2. **Instruction patch instead of a hook.** The aspect enters the setter
   `+0x7434C` in `s1` and is copied into `s17` by `vmov.f64 d8,d0` at
   `+0x74358`. The setter has no free instruction to scale it, so this
   needs a literal word near the code. That is fragile; not recommended.
3. **Patch the two per-frame aspect sources**: the `vdiv` at `+0x40184`
   (manual mode) and the camera node aspect `+0xCC` (animated mode). Init at
   `+0x42180` would also be needed. That is three or more sites, and the node
   write must happen every frame. Worse than 1.
4. **Do not touch `GetDisplayWidth_`.** It is shared by framebuffer setup [U].

Repeat the same hook for `SequencePlayer.cro+0xE860` if battle sequences or
demos are seen to stretch after fix 1 [U].

## 4. Cause B: full-screen battle 2D is squeezed

The template patch makes every layout created after boot use x ±200·k.
Layouts authored to fill 400 px now fill only the middle 400/k px of the
stretched screen, and the 3D shows at the sides [V-code: mechanism].
Candidates in battle: fades/flashes drawn as layouts, intro/encounter
layouts, Z-move and trainer cut-ins, weather overlays [U: not enumerated].
`gfl2::Fade::FadeManager` is imported by Battle [V-code], but whether it draws
through a widened template was not checked [U].

Fixes:
- Scale the specific full-screen panes' x by k in the existing `CalculateMtxHook`.
  This is the same method the map uses (`Map.cpp`, scale x override). It needs
  pane names from `cullnames`-style logging in battle.
- Or leave battle layouts unsqueezed (template off while in battle). The HUD
  would then stretch 1.4 times, which is worse.

## 5. Cause C: the plugin's 30% pane cull

`CalculateMtxHook` zeroes the alpha of any unrotated pane with less than 30%
of its width inside [-200, 200], with no screen or name filter [V-code].
In battle, panes that animate in from off-screen are hidden until 30%
visible, and panes that live past the edges by design are hidden entirely.
This could read as "culled at the edges" [U: no pane list yet].
Fix: restrict the cull to the known timer panes (by name, or by Battle
layout), or log candidates with `cullnames` in a battle with effects first.

## 6. Cause D: content edges after A (expected, unverified)

With a 30° vertical fovy, 5:3 gives a horizontal half-FOV of about 24.0°.
At 21:9 (2.333) it is about 32.0° [U: computed]. The battle stage
(`System::Skybox` is imported, so the sky is a dome and should be fine) and
ground or backdrop models may not extend that far. Effects that use
camera-facing planes sized for 5:3 would also end early.
GameFreak's battle effect camera also animates (animated mode above), so
some shots will look wider than others. Options if this happens: accept it,
or use a smaller battle-only aspect (for example 16:9 in battle and pillarbox
the rest). That would need Azahar's stretch to stay at the global aspect, so
it means drawing bands, as the native menu pillarbox does.

Frustum culling: Battle imports `gfl2::renderingengine::scenegraph::instance::CullingObjectNode::SetMoved`
[V-code]. Whether gfl2's culling uses the same projection that is submitted
(which fix 1 would widen) or a separate copy was not traced [U].
Watch for models popping at the new edges.

## 7. How to test (after a build)

1. Boot at `aspect = 21:9`, enter a wild battle (start point `grass`).
   The log must show the new hook result `0` and the per-battle install line.
   `aspect` should show the rewrite count growing during the battle.
2. Compare top screenshots at `aspect off` and `21:9` in the same camera pose:
   at 21:9 the 3D (not the HUD) must be narrower in the raw 400x240 frame and
   show more scenery at the sides.
3. Play moves with big effects (a Z-move, weather, a stat-change flash) and the
   intro. Look for D (ends) and for popping models (culling).
4. Leave the battle and enter another 3 to 5 times, including running from
   a battle and a trainer battle. There must be no crash at unload (the hook lifetime).
5. Repeat on Moon (same CRO bytes, so the same offsets).
6. For B/C: run `cullnames` during the intro and effects, and list full-screen panes.

## 8. Web research

No public widescreen hack or cheat for USUM, SM, ORAS or X/Y battles was found.
General references found:

- GameTechWiki, Widescreen hacks: says there is no ultrawide hack for 3DS.
  https://emulation.gametechwiki.com/index.php/Widescreen_hacks (HTTP 403 to the fetcher; seen only in search snippets)
- GameTechWiki, Aspect ratio hacks: https://emulation.gametechwiki.com/index.php/Aspect_ratio_hacks
- Dolphin issue: a GPU-level widescreen hack that patches projection matrices
  breaks effects and 2D overlays (same class of risk as B/D):
  https://bugs.dolphin-emu.org/issues/2500
- USUM cheat lists checked; no aspect or widescreen codes:
  https://www.pokemoncoders.com/pokemon-ultra-sun-cheats/ ,
  https://www.pokemoncoders.com/pokemon-ultra-moon-cheats/
- DS-only widescreen (TWiLight Menu++ / TWPatch), not applicable to 3DS titles:
  https://wiki.ds-homebrew.com/ds-index/rtcom

All search conclusions are [U]: absence in search results is not proof.

## 9. Open questions

1. Live check: is battle 3D really unwidened today? (3.3 says yes, from screenshots.)
2. What draws through each of the three battle projections (`BtlCam+0x000/+0x150/+0x270`)?
3. A safe lifetime for a CTRPF hook on CRO code; is there a CTRPF hook-slot limit?
4. Does gfl2 culling use the submitted projection?
5. Which battle layouts are full-screen (B), and which panes the 30% cull hits (C)?
6. Is the `SequencePlayer.cro` copy used during battles?

## 10. Implementation (2026-11-09)

- Added `BattleWidescreen.cpp/.hpp`; frame discovery uses the same
  scene/vtable -> CroModuleOf -> Battle-name path as FastText, with a fresh
  FIXD-header check. FastText itself is unchanged. The entry at +0x736D0
  and divide at +0x73774 are verified before installing another Hook with
  the existing PerspectiveHook callback. Aspect/filter/counter/ABI are shared;
  aspect off or stock 5:3 installs no battle hook. No 2D code changed.
- Lifetime uses a static pre-unload MITM at code.bin 0x17376C, not a guessed
  scene-exit frame. Offline disassembly found its sole direct caller at
  0x1732C8, passing the module base in r1 (`mov r1,r4` at 0x1732C0).
  The wrapper submits IPC command 0x000500C2. All 17 words through the handle
  literal at 0x1737AC match both local Sun/Moon v1.2 code.bin files and are
  checked at startup. An unavailable guard disables battle hooking entirely.
- A matching Battle hook is disabled before forwarding the unload IPC;
  its slot is freed while its code is mapped. One mutex serializes unload
  and frame operations, held through the original SDK call. Frames TryLock
  and skip if busy. The loader callback allocates/logs nothing. Disable failure
  vetoes unload with an error Result. Unrelated module unloads do not remove
  Battle. No hook is reinitialized while enabled or abandoned at an old base.
- Enable/Disable leave the CRO page permissions alone. A first version
  wrapped them in `Process::ProtectMemory` (add write, then restore) and hung
  every wild battle: Azahar's ldr:ro then rejected the next LoadCRO (Bag.cro,
  "CRO original buffer is in invalid state") and the main thread spun in the
  game's fatal handler at 0x3FD518 (gdb). Bisect: no ProtectMemory and no hook
  = fine; ProtectMemory without the hook = same hang; hook without
  ProtectMemory = fine, Enable -> 0 on the RX (AliasCode) page.
- Suppress the retiring base until a frame observes another module/no Battle,
  preventing a stale scene pointer from reinstalling during SDK cleanup.
  Same- and changed-base reloads then get fresh checks/hooks. Missing an entire
  out/in interval at the same base safely suppresses widening until another
  observed transition. Installation is frame-driven, so initial matrices may
  precede it; subsequent per-frame camera updates rebuild the projections.
- Hook audit: seven existing plugin-owned hooks, now at most nine. CTRPF adds
  internal hooks and optional tools/AR hooks. Installed Hook.hpp says 91 in a
  stale comment; HookManager source uses 0x1000/12 = 341, confirmed by the
  installed libctrpf.a DisableHook disassembly (0x155 limit, 12-byte indexing).
  Disable frees reusable slots; repeated battles do not consume new slots.
- Host regression tests exercise signature/address checks, each unreadable or
  mismatched word, off/stock, unload-before-unmap, stale scenes, unrelated
  unloads, base changes, same-base reload and failures. Fake removal asserts
  the target is still mapped. Integrated in tests/Makefile.
- SequencePlayer +0xE860 remains untouched. Battle full-screen 2D squeeze,
  the 30% pane cull, finite scene/effect edges and frustum pops are follow-ups.
  See WIDESCREEN.md for lifetime details and the runtime log/test recipe.

Validation performed here (no make, WSL, plugin link, emulator or screenshots):

1. From `tests/`, after the VS 2022 Community `vcvars64.bat` environment:
   `cl /nologo /std:c++17 /EHsc /W4 /WX /I ../Includes battle_widescreen_test.cpp /Fe:build/battle_widescreen_test.exe /Fo:build/battle_widescreen_test.obj`
   then `.\build\battle_widescreen_test.exe`: **passed**. This is the MSVC
   equivalent compile line recorded beside the Makefile target. Environment
   setup printed a vswhere.exe warning, but compiler and test exited 0.
2. ARM syntax checks, using
   `C:/devkitPro/devkitARM/bin/arm-none-eabi-g++.exe`, with exactly:
   `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -Os -mword-relocations -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-strict-aliasing -Wall -fno-rtti -fno-exceptions -std=gnu++17 -D__3DS__ -I Includes -I C:/devkitPro/libctrpf/include -I C:/devkitPro/libctru/include -fsyntax-only Sources/BattleWidescreen.cpp Sources/Widescreen.cpp`
   and a second invocation with the same flags for `Sources/main.cpp`: **passed**.
   After the permission handling change, the same command was rerun with
   all three files (`Sources/BattleWidescreen.cpp Sources/Widescreen.cpp Sources/main.cpp`): **passed**.
   The MSVC compile and host test above were also rerun: **passed**.
3. Python `struct.unpack_from` assertions checked all 17 unload words against
   local Sun/Moon code.bin and all three perspective words against both
   local Battle.cro files: **passed**. ARM objdump inspected the unload wrapper,
   its direct caller and installed CTRPF HookManager. `git diff --check` passed.

Claude still needs to build/link the plugin, run the full suite, compare stock
and 21:9 wild-battle screenshots, and enter/leave twice in succession. Confirm
both per-battle installs and pre-unload removals, growing rewrite counts,
mid-battle aspect changes, no unload crash, effects/intro/culling behavior,
and Moon. Runtime assumes all game CRO unloads use the checked SDK wrapper;
the pre-unload log is part of that proof. VFP forwarding and new 3D edges
remain unverified until those runs.
