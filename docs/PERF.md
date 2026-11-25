# Panel performance

## Field baseline: the action probe (2026-11-23)

With the panel off, the plugin still cost Azahar ~20% speed in the overworld.
Measured from Azahar's own status bar (Speed, read through UI Automation),
Route 2 Pokémon Center standing still, Ultra Sun v1.2, 5x, Vulkan, frame
limit 250%, same input movie each run, runs interleaved, 60 one-second samples.

A `[debug] disable = ...` setting (boot only, see the default ini) switches
subsystems off one at a time. Bisecting found `ActionsOnScreen`
(`Sources/Actions.cpp`): an OSD callback that read bottom-framebuffer pixels
every frame to detect the X menu and the Party list. Each CPU read of that
GPU-drawn memory makes Azahar download the surface (see P1-6 below). Disabling
`actions` alone gave no-plugin speed; disabling `panel` changed nothing.

The probe now runs only while an action runs, in Party, or in the field with
the X menu open (field state 4); `action status` reports `probe on|off, N taken`.
Standing in the field: 0 probes taken. X menu open: probe on, native X menu
shown/hidden as before.

These measurements used the old build; current main also probes during Battle
for power/Z controls, and keeps scene-gated Pelago/Festival observers.

Old build vs fixed vs no plugin, interleaved (PC load drifted between rounds,
so compare within a round):

| round | old | fixed | no plugin |
|---|--:|--:|--:|
| 1 | 184% | 203% | 208% |
| 2 | 170% | 174% | 176% |
| 3 | 144% | 159% | 165% |

Ported onto main (dad0939, which also gates the battle Mega/Z probe to
battles), same setup, 2026-11-24:

| round | main | probe gate | no plugin |
|---|--:|--:|--:|
| 1 | 181% | 189% | 191% |
| 2 | 179% | 203% | 221% |
| 3 | 185% | 203% | 225% |

The gate gains 4-13% over main. A 1-10% gap to no plugin remains on main,
which does more per frame than the build bisected above; not bisected yet.

`disable = framesync` (or `all`) freezes the game at boot: CTRPF's menu loop
without frame sync starves it. Not useful for bisecting.

### Current probe audit

The shared action probe is wanted while an action runs, in Party or Battle,
or in FieldRo with `FieldStateMenu`. Otherwise its flags are zero. Resuming
starts with no results, and battle power still needs three matching samples.
Idle scene refreshes run every five frames; the existing independent Pelago
and Festival watchers retain their fifteen-frame idle refresh cadence.
`N taken` counts shared bottom-screen samples, excluding those independent observers.

| Probe/read | Before this port | Consumers | Current gate / decision |
|---|---|---|---|
| XMenuProbe, XMenuBackground, XMenuPage2Probe | Every bottom frame | ActionQueue, XMenu/Grid | Shared action gate above |
| PartyListProbe | Every bottom frame | ActionQueue, PartyScreen/Menu | Shared action gate above |
| Battle power / Z slots | Every bottom frame | ActionQueue, BattleScreen (Mega/Z and Z row controls) | Shared gate includes Battle even without an action |
| Actions SamplePelago (top and bottom) | PelagoDetected only | Action close/leave guards, WorldPelago flags | Existing watcher retained, independent of native overlay visibility |
| Actions SampleFestival | FestivalPlazaDetected only | Action close/exit guards, WorldFestival flags | Existing watcher retained, independent of native overlay visibility |
| XMenu ScanGameCursor | s_scanWanted and s_shown | Native X-menu cursor/page sync | Already limited to shown X menu |
| Party ScanRing | s_ringWanted in status/summary | Native party summary selection | Already limited to summary |
| SubScreens SampleBottom | s_sampling >= 0 for detected screen | Native screen state, including Pelago/Festival | Already limited to matching scene/state |
| Panel copies/readback | Panel/full active; readback at diagnostic interval | Bottom-on-top display and verification | Required while displayed; bottom callback stores metadata only |
| Shot / PhotoMode copies | Screenshot requested / entering or leaving photo mode | Capture and frozen clean picture | Already request-limited |

All other registered screen OSD callbacks draw overlays; their framebuffer
layout helpers do not sample bottom pixels. UI layer backing copies operate
only while the corresponding overlay is presented. SidePanels draws top-screen
strips while visible; GpuQuad uses GPU transfers.

### Debug switches on current main

`[debug] disable = name,...` is case-insensitive, ignores empty tokens, and
warns about unknown names. Empty preserves normal behavior. Switches are
latched at boot; reloading settings does not change them. `all` covers every
name, using a 64-bit mask (33 subsystems).

Original names: `xmenu, keyboard, pokedex, subscreens, bag, map, rotom,
party, boxes, theme, battlescreen, panel, actions, cursor, buttons, pcanywhere,
widescreen, autopanel, scene, commands, framesync, fasttext, assets, shot,
uidemo, fieldstate, status, framecallback`.

Added for current main: `battlewidescreen, quicksave, photomode, sidepanels,
gpuquad`. Native Pelago/Festival screens belong to `subscreens`; their
independent action observers belong to `actions`. Shared ScreenBuffer allocation
and layer GPU setup remain infrastructure owned by the display paths. `panel`
also skips automatic panel driving and screenshot polling, whose capture
callback would otherwise be absent. As before, disabling dependencies can
make dependent features unavailable; these are diagnostic switches.

## Widescreen panel safety fallback (2026-11-10)

Panel/full views use CPU composition only when `WidescreenSqueeze() > 1`.
At squeeze <= 1 (including aspect 0), the original GPU preparation, cursor,
filtering, two-thirds refresh cadence and flush behavior are retained. Only
the old squeezed final-copy loops in `CopyTopColumns` and `GpuFull` are
removed. Unsupported formats or scratch allocation failure still select the
CPU fallback. `PanelCopyBottomRect` continues to copy the game's already-
squeezed native map with one TextureCopy, independent of panel scratch.

The reported Ultra Sun X-menu crash at 21:9 (`auto on`, `pokedex off`)
corresponds to those two per-column GX submission loops. The CPU cursor
squeeze loop was safe. The user's emulator check of the widescreen CPU fix
now reaches scene Zukan with the panel shown and **0 unmapped reads**, versus
approximately 5,800 before. The stock-aspect build previously passed the full
regression suite without faults. The exact corruption/interrupt mechanism
behind the command-count failures remains unproven.

| Panel path | Stock total GX/present (unchanged) | Old widescreen total | New widescreen total |
|---|---:|---:|---:|
| Full | 8 | 7 + N | 0 |
| Half | 5 | 4 + N | 0 |
| Two-thirds | 3-4 | 2-3 + N | 0 |
| Native map rectangle (per call, any aspect) | 1 | 1 | 1 |

N is the number of covered destination columns. Full view previously issued
307 commands at 16:9 and 235 at 21:9; half issued 162 and 124. Two-thirds
adds one command on refresh frames. The stock final framebuffer copy is one
command, but its preparation and cursor work bring the **total** to the
counts above; the restored stock chains do not claim a two-command budget.
These counts cover Panel.cpp, not the separate LayerGpu/GpuQuad presenters.

Widescreen CPU composition uses the inverse column mapping for picture,
border and cursor. Half-size averages 2x2 pixels; two-thirds retains its 3:2
area filter and 229x176 box. All CPU panel draws request a CTRPF flush;
stock GPU paths retain their original flush returns. Widescreen readback
compares against the CPU filter; stock GPU readback remains unchanged.

This is a conservative widescreen fallback. Arbitrary 16:9/21:9 squeezes
cannot use DisplayTransfer's fixed 2x scaling; new quad texture/upload/state
integration is deferred. CPU reads and small writes cause Azahar framebuffer
downloads and may reduce sharpness at higher internal resolutions. Expect a
potentially substantial slowdown **while a widescreen panel is visible**.
Aspect 0 retains its GPU performance path. The historical ~40% CPU-copy loss
below is context, not a measurement of this implementation. Re-measure
panel/full at 0, 16:9 and 21:9 with `perf` / `tools/perf.py`.

Host verification (no game data, WSL or make required):

```powershell
python -I tests/panel_command_test.py --generate tests/build/panel_host.hpp
# From a Visual Studio x64 Native Tools prompt:
cl /nologo /std:c++17 /EHsc /W4 /WX /wd4244 /wd4505 /I Includes tests/panel_command_test.cpp /Fe:tests/build/panel_command_test.exe /Fo:tests/build/panel_command_test.obj
tests/build/panel_command_test.exe
```

The host build compiles the actual Panel.cpp and Framebuffer.hpp bodies with
device includes replaced by stubs and transfer mocks. Host addresses use
pointer-sized `u32`; MSVC warning 4244 is suppressed for that host widening,
and 4505 for unused stub functions. The 2,400 cases count **all** GX calls:
8/5/4 then 3 on stock BGR8 paths, and 0 at 16:9/21:9. They also exercise CPU
fallback formats, framebuffer guards, flush returns, cursor/filter pixels
and returning from widescreen to stock. A source call-graph guard rejects
GX submissions inside loops, including via Transfer/CopyRuns; its regression
checks explicitly reject both original squeezed-loop shapes. Tests are
wired into `tests/Makefile` too.

The user's emulator result validates the widescreen CPU approach; the final
stock-GPU restoration still needs a plugin build and regression rerun. Host
transfer mocks cannot prove native GX execution, game timing or sharpness.

## GPU-copy performance measurements (2026-11-02)

The GPU panel measurements below remain relevant at stock aspect: 0-4% of
Azahar speed for half/full and ~8% for two-thirds. They do not describe the
new widescreen CPU path. Sections after P1-6 contain H5's original CPU-copy
measurements, retained as historical evidence.

## Map at widescreen: one GX command (2026-11-08)

On `main` (`b559b40`) the native map crashed the game at 16:9 and 21:9 within
~15-30 s (plugin stops answering; Azahar logs unmapped reads at PC `0x402690`,
`0x3596C4`, `0x33FA3C`, `0x3531BC`, then undefined instructions and a jump to
0); `aspect off` ran fine. `PanelCopyBottomRect` squeezed the picture with one
TextureCopy per destination column. Tests (instance `mapfix`, Ultra Sun, 5x,
frame limit 250%, Pokémon Center, map open; throwaway builds):

| copy | GX commands per frame | result |
|---|--:|---|
| `main`, 16:9, one per column | 244 | crashed in ~15 s |
| `main`, 21:9, one per column | 186 | crashed within 30 s (instance `lrtoggle`, 2026-11-07) |
| 21:9, consecutive columns merged into runs | 75 | crashed in < 15 s |
| 21:9, the same rectangle copied 75 times | 75 | crashed in < 30 s |
| 21:9, runs merged, 100 µs sleep between commands | 75 | crashed in < 30 s |
| 21:9, one copy (centre columns, unsqueezed) | 1 | ran 2 min |
| **this fix**, 21:9 / 16:9 | **1** | ran 3.7 / 3.5 min; `native-map` scenario passes at both |

So the number of commands is what matters, not what they copy, and giving the
game's threads time between them doesn't help. The game's GX push
(`0x279A2C`) is the standard GSP shared-memory command queue (15 entries; a
push into an empty queue triggers `TriggerCmdReqQueue`), so each command is
one GSP request and one PPF interrupt for the game's handlers; why many of them
break the game was not found. Merging runs can't get far enough: Azahar keeps
a TextureCopy on the GPU only when its gaps match the surfaces' stride, so a
nearest-neighbour squeeze needs one command per run (75 at 21:9, 17 at 16:9),
and DisplayTransfer only scales by exactly 2.

The fix makes the game draw the bottom map squeezed (`docs/WIDESCREEN.md`)
and copies it with one TextureCopy at every aspect; the picture stays at the
internal resolution.

Speed (`tools/perf.py --modes map:closed,map:off,map:16:9,map:21:9`, 2 rounds
× 4 windows of 8 s, 2 Azahar processes; "before" from the `main` build, 5
windows each):

| build | map | GX per frame | speed (mean ± sd) |
|---|---|--:|---|
| `main` | closed | 0 | 1.77 ± 0.02 |
| `main` | open, aspect off | 1 | 1.73 ± 0.04 |
| fix | closed | 0 | 1.80 ± 0.01 |
| fix | open, aspect off | 1 | 1.77 ± 0.01 |
| fix | open, 16:9 | 1 | 1.74 ± 0.02 |
| fix | open, 21:9 | 1 | 1.73 ± 0.01 |

The map costs ~2-4% either way.

After merging PR #75 (`LayerGpu`, map frame presented by the GPU, picture copy
after it), same method: closed 1.86 ± 0.00, open off 1.80 ± 0.02, 16:9
1.79 ± 0.01, 21:9 1.78 ± 0.01. `layergpu status` with the map open: most GX
commands per present 11 (plus the map's one copy), queue depth 0, fallbacks 0;
ran 3.2 min at 21:9 and 16:9, and 1 min at 21:9 with `layergpu off`. A window
capture of Azahar at 21:9 shows the map picture at the internal resolution
with `layergpu on` and at 1x with `layergpu off`. The panel's own half/two-thirds/full paths
still sent one command per column at widescreen in that build and were not
tested there; the CPU fallback above now removes those paths.

## P1-6: GPU copy

### Why the old panel was slow

From Azahar's source (master, `video_core/rasterizer_cache/rasterizer_cache.h`,
`core/memory.cpp`; read, not profiled): framebuffers live on the host GPU.

- A CPU **read** of GPU-written memory makes Azahar download that surface from
  the host GPU first (`FlushRegion`).
- A CPU **write** of 8 bytes or less to GPU-written memory also downloads and
  drops the whole surface (`InvalidateRegion`: "If the CPU is invalidating this
  region we want to remove it"). So drawing even a cursor or a 1-px border on
  the game's frame costs a download per frame.

### What the stock-aspect GPU panel does

`Sources/Panel.cpp` queues transfers with the game's own gx library
(DisplayTransfer `0x27A5BC`, TextureCopy `0x279E28`, see `docs/OFFSETS.md`);
Azahar runs them on the host GPU before the queue trigger returns.

- **Full:** bottom framebuffer → RGBA8 scratch, cursor sprites copied in
  (TextureCopy), → top framebuffer as BGR8 (DisplayTransfer). No CPU access.
- **1/2:** bottom → RGBA8 → into a 256-px-line buffer pre-filled with the
  border color → 2x2 scaled → TextureCopy into the top screen's lines. The
  extra 8 px per line are needed because TextureCopy writes runs of 16-byte
  units: 120 BGR8 pixels (360 bytes) is not one, 128 is. The **cursor** is
  still drawn by the CPU on the top screen (one download per frame): copying
  the sprites into this chain made Azahar fail an assertion (`GetFormatType`,
  invalid pixel format) every time the field reloaded after the Pokédex or a
  battle (5 of 5 runs, several orderings tried; full size with the same
  sprites never did). Cause not found.
- **2/3:** transfers only scale by 1/2, so the CPU scales. The bottom screen is
  copied to plain memory with a TextureCopy that Azahar can't accelerate (16-byte
  gaps between BGR8 lines), so it runs in software: **one** bulk download per
  copy instead of piecemeal ones. The CPU scales it (3x3 → 2x2, weighted) into
  a CPU-only buffer with border and cursor, which transfers copy to the top
  screen. Redrawn every 2nd frame (15 fps); the cursor moves every frame.
- Sprites and the 2/3 picture are CPU-written (1x) while the game's surfaces
  are at the internal resolution (5x here). A TextureCopy between surfaces of
  different scales falls back to software (a download), so each CPU buffer's
  GPU twin is first made from a corner of a GPU buffer (`ToGpu`).
- The readback self-check now runs every 128 frames (~4 s) instead of every
  frame (it reads both framebuffers). The screenshot tool is unchanged (reads
  on request only).
- Needs linear memory (`svcControlMemory` LINEAR, 1.5 MB, allocated without
  trouble) and BGR8 framebuffers; otherwise it falls back to the old CPU copy.

### Results (field, Pokémon Center, 5x, frame limit 250%)

Mean ± sd of 10 s windows, modes interleaved per round. Speed 1.00 = real time.
The game's own rate stayed 29.9 fps (emulated) in every row below.

Final build (merged with main 82137a8), 4 rounds × 3 windows, 2-3 Azahar running:

| mode | n | speed | emu µs/frame (copy) |
|---|--:|---|--:|
| off | 12 | 1.82 ± 0.12 | – |
| panel 1/2 | 12 | 1.74 ± 0.14 | 358 |
| full | 12 | 1.83 ± 0.13 | 521 |

2/3 (same code), 4 rounds × 3 windows, 1-2 Azahar running:

| mode | n | speed | emu µs/frame |
|---|--:|---|--:|
| off | 12 | 1.95 ± 0.11 | – |
| panel 2/3 | 12 | 1.80 ± 0.13 | 3690 |

How it got there (each a separate run, so compare within a row group only):

| variant | speed | off in the same run |
|---|---|---|
| transfers only, 1/2 / full (no CPU drawing at all) | 1.46 / 1.55 | 1.50 |
| transfers + CPU border, cursor, readback on top (first GPU version) | 1.31 / 1.37 | 1.50 |
| transfers + CPU border only / CPU cursor only | 1.59 / 1.59 | 1.74 |
| all on GPU incl. cursor sprites (before the 1/2 assertion was found) | 2.01 / 2.05 | 2.03 |
| 2/3, CPU reading the framebuffer directly | 1.00 | 1.50 |
| 2/3, one software bulk copy, scaled every frame (after the scaler went from 42 to 7 ms emulated) | 1.95 | 2.24 |

Not measured: 1x internal resolution, other scenes, real hardware (the
transfers are hardware features, but the flags used, e.g. scaling linear input,
were only checked against Azahar).

## Native overlays and internal resolution

Azahar drops the entire frame to 1x (400x240) when the CPU writes into the
game's framebuffer, or when a CTRPF OSD callback returns true ("I drew").
The battle-menu prototype at `fff0411` was verified at the internal resolution
with GPU-only presentation and a false callback result. Avoiding downloads
alone is not enough: callbacks must also report CPU drawing accurately.

`Sources/UI/LayerGpu.cpp` transfers the game's columns into GPU-written
RGBA8 background scratch. The CPU writes layer pixels only into a separate
stage that the GPU never writes. A GPU twin is seeded from the background
then overwritten from the stage, keeping its internal scale for TextureCopy.
The layer's rectangle is copied into the background, converted to BGR8 and
copied back as whole 240-pixel columns. RGBA8 gives 2-pixel aligned starts
and 4-pixel lengths rather than BGR8's coarse 8/16-pixel steps.

**Keep the GX command count per present small.** Every column of the layer
copies the whole rectangle (square corners: the layer's black shows outside
its rounded shape), so a present is 4 DisplayTransfers, one TextureCopy for
the layer (two around a skip window), one per pillarbox side and the copy
back: 6-8 commands. Rounded corners need one copy per distinct column run,
~15 commands for the 376x224 menus. With those, the game's Party screen
(scene `main:00647FCC`) crashed within ~10 s every time (bad reads at PC
`0x402690` / `0x3596C4`, then a jump to garbage). A bisect of the GPU steps
put it on the TextureCopies; one copy for the whole rectangle ran for
minutes. The X menu, same rectangle and count, never crashed. The cause was
not found (not memory: 37 MB of the application region stayed free; not the
GX queue, see below; not writes past the scratch: 1 MB guards changed
nothing).

Pillarbox tint/pattern band columns are written into the same CPU stage as
the layer (whole 240-row columns, by the same pure helper as the CPU
fallback), and the destination range expands to the full screen. Their runs
are identical, so each side's band goes in one TextureCopy with the layer's
runs. A first version copied each band column separately (~114 copies a
frame at 21:9): the game crashed in the X menu within seconds (PC 0, from
game code at `0x41DB8C`). The cause was not found: the game's GX queue
(`*(*(0x69EA68 + 0x1C) + 0x58)`, count at `+1`, refuses commands at 15)
was empty before every push. `LayerGpu` still checks it before each push
(waits at 12, gives up after ~2 ms) and reports the most commands in one
present and the deepest queue seen in `layergpu status`. Measured at 21:9:
X menu, Party, Bag and Pokédex 8 commands, battle menu 6.

Speed (21:9, 5x, frame limit 250%, X menu open, `layergpu on`/`off`
alternated, 3 windows of 8 s each): 1.51 / 1.51. No measurable cost.

**The map at 16:9/21:9 crashed on main too** (`b559b40`, `layergpu` not
involved; fine with `aspect off`): `PanelCopyBottomRect` copied its picture with
one TextureCopy per destination column, a few hundred a frame. Fixed by PR #76:
one copy of a picture the game draws squeezed ("Map at widescreen", above).

Skip rectangles split each run around excluded rows before rounding each
piece inward. Map's picture transfer runs after the layer present so the
whole-column copy-back cannot overwrite it with the saved background.
Non-BGR8 layouts, unavailable linear memory and `layergpu off` retain the
CPU fallback. Layer callbacks return `Present`'s CPU-write result, combining
results without short-circuiting. Direct demo shading/drawing, panel CPU
fallbacks/cursor, readback checks and requested screenshots retain true
reporting and their existing framebuffer costs. Actions only probes and
returns false; inactive/empty callbacks also return false.

Host regression tests cover band bytes in all five formats and both vertical
directions, plus split-run alignment and skip preservation. The new band,
skip and callback paths need a devkitARM build and Azahar verification;
no new emulator speed measurements are claimed. See docs/WIDESCREEN.md.

## H5: the old CPU copy

### Method

- **Plugin side** (`perf on`, `perf`; see `Sources/Panel.cpp`). Off by default.
  When on, the OSD callback reads `svcGetSystemTick` around the bottom-screen
  copy (`CaptureBottom`) and around the top-screen draw (border, scaled copy,
  readback check, cursor). Each `perf` logs the window since the previous one:
  emulated time, top and bottom frames seen, and mean and max copy and draw
  time, then starts a new window. Times are emulated ARM11 time
  (ticks / 268,111,856 Hz): what Azahar charges the game's CPU for the work,
  not host time and not real-3DS time.
- **PC side** (`tools/perf.py`): turns auto panel off, then for each round and
  mode sets the mode, waits 3 s, and takes windows of ~10 s. The wall-clock
  length of each window is measured between the moments the plugin removed the
  command file (polled every 2 ms; the plugin logs the window in the same
  call). The plugin only checks for commands every 30 frames, so windows end
  on those polls (hence emulated lengths in 1 s steps). Per window:
  - **speed** = emulated time / wall time. This is the same quantity as
    Azahar's on-screen speed %; Azahar's log does not record it (checked
    `user/log/azahar_log.txt`: only settings and HLE messages).
  - **fps wall** = top-screen frames / wall time; **fps emu** = per emulated
    second (the game's own rate).
  - the number of `azahar.exe` processes running (other Wave 1 sessions).
- Modes are interleaved round by round (off, panel, full, off, ...) so changes
  in the other instances' load hit every mode.
- Raw window lines: `analysis/private/perf/*.txt` and `.csv` on the test PC
  (not in git).

Conditions: AMD Ryzen 7 9800X3D (8 cores / 16 threads), RTX 5070 Ti. Azahar
2126.2, Vulkan, CPU JIT, CPU clock 100%, New 3DS mode, async presentation,
vsync on, **frame limit 250%** (copied from the user's own config, so with
spare host time the game runs faster than real time; that headroom is what the
"off" rows show). Internal resolution 5x unless noted. Instance `h5`, plugin
built from this branch. Panel size 1/2, bottom-right.

### Results

Mean ± standard deviation over windows. Speed 1.00 = real time.

#### Field: Pokémon Center on Route 2 (`FieldRo`), standing still, 5x resolution

3 rounds × 5 windows of 10 s per mode; 4-6 Azahar processes running (mostly 6).

| mode  |  n | speed       | fps wall     | fps emu | copy (emu µs/frame) | draw (emu µs/frame) | panel share of emu CPU |
|-------|---:|-------------|--------------|--------:|--------------------:|--------------------:|-----------------------:|
| off   | 15 | 1.59 ± 0.10 | 47.5 ± 3.0   | 29.92   | –                   | –                   | –                      |
| panel | 15 | 0.90 ± 0.03 | 27.0 ± 0.9   | 29.91   | 6722 (max 7117)     | 2270 (max 2615)     | 26.9%                  |
| full  | 15 | 0.93 ± 0.07 | 27.7 ± 2.1   | 29.89   | 6688 (max 7004)     | 8783 (max 9135)     | 46.2%                  |

#### Pokédex (`Zukan`, a full-screen app; list page on the bottom), 5x resolution

2 rounds × 5 windows; 5-6 Azahar processes (mostly 5).

| mode  |  n | speed       | fps wall     | fps emu | copy µs | draw µs | panel share |
|-------|---:|-------------|--------------|--------:|--------:|--------:|------------:|
| off   | 10 | 1.68 ± 0.07 | 50.2 ± 2.1   | 29.92   | –       | –       | –           |
| panel | 10 | 1.01 ± 0.05 | 30.1 ± 1.4   | 29.92   | 6684    | 2271    | 26.8%       |
| full  | 10 | 1.04 ± 0.03 | 31.0 ± 1.0   | 29.90   | 6686    | 8746    | 46.1%       |

#### Field at 1x internal resolution

Same spot, 2 rounds × 4 windows; 4-5 Azahar processes (mostly 5).

| mode  | n | speed       | fps wall   |
|-------|--:|-------------|------------|
| off   | 8 | 1.68 ± 0.04 | 50.3 ± 1.1 |
| panel | 8 | 1.06 ± 0.04 | 31.8 ± 1.1 |
| full  | 8 | 0.97 ± 0.07 | 29.0 ± 2.1 |

#### Which half costs: copy only vs draw only (temporary build, not committed)

A throwaway build changed the modes so that `panel` drew the half-size panel
from a capture taken once (draw only, no reads of the bottom screen) and
`full` copied the bottom screen every frame but drew nothing (copy only).
Field, 1x resolution, 2 rounds × 4 windows; 2-3 Azahar processes.

| variant           | n | speed       | fps wall   | copy µs | draw µs |
|-------------------|--:|-------------|------------|--------:|--------:|
| off               | 8 | 1.95 ± 0.05 | 58.3 ± 1.4 | –       | –       |
| draw only (1/2)   | 8 | 1.92 ± 0.05 | 57.5 ± 1.6 | –       | 2271    |
| copy only         | 8 | 1.24 ± 0.10 | 37.1 ± 3.1 | 6723    | –       |

### What the numbers say

1. **The game itself never slows down in emulated time.** fps emu stays at
   29.9 in every mode: the game runs at 30 fps and the panel's 9.0 ms (panel)
   or 15.5 ms (full) of emulated CPU per frame fits in its 33 ms frame.
2. **Azahar slows down on the host.** Speed drops to 57-63% of the panel-off
   speed in both scenes and at both resolutions. Under the test load (5-6
   instances on 8 cores) that is below real time in the field (0.90-0.93) and
   about real time in the Pokédex (1.01-1.04). With one instance the headroom
   is larger (off reached 1.95 with 2-3 instances), so a single player may
   stay at full speed, but the panel uses up most of the margin.
3. **Panel and full cost the same**, although full spends almost twice the
   emulated CPU time (46% vs 27%). So the emulated instruction count is not
   what costs host time.
4. **Reading the bottom framebuffer is the cost; drawing is almost free.** Draw
   only: 1.95 → 1.92. Copy only: 1.95 → 1.24, about the same drop as the real
   panel modes.
5. **Not the size of the readback.** 1x vs 5x resolution changes panel/off from
   0.57 to 0.63 (field), within the spread between runs.

Why reading is expensive is **not verified** (I did not profile Azahar). The
likely cause: the bottom framebuffer is rendered by the emulated GPU on the
host GPU, and a CPU read of that memory makes Azahar's rasterizer cache flush
(download) the surface and wait for the GPU, every frame. Finding 5 suggests
the wait, not the copy size, dominates.

### Recommendation: P1-6 is needed

- **GPU DisplayTransfer (game's `0x27A5BC`, see `docs/OFFSETS.md`) is the
  promising route**: a GX transfer from the bottom framebuffer straight into
  the top framebuffer (DisplayTransfer can scale down 2x for the half-size
  panel) keeps the data on the GPU side in an emulator, so no CPU read and no
  flush. The cursor and border would still be CPU-drawn on top, which finding
  4 shows is cheap. Unverified: that Azahar handles a framebuffer-to-framebuffer
  DisplayTransfer without a flush; measure it with `tools/perf.py` (copy time
  should drop to ~0 and speed should match "draw only").
- **"Copy only changed frames" alone will not help**: deciding whether a frame
  changed still needs reading it, and the read is the cost. A cheaper variant
  of the same idea that would help: **copy every Nth frame** (e.g. every 2nd
  or 3rd, i.e. 15 or 10 fps for the panel; most bottom screens are menus).
  If cost is per read, every 2nd frame should roughly halve the drop. Easy
  stopgap while DisplayTransfer is explored; also unmeasured.
- Lower priority: the draw loop (2.3 ms / 8.8 ms emulated) only matters on
  real hardware or with emulated CPU clock limits; not a problem here.

### Reproducing

```bash
py -3 -I tools/instance.py start NAME --wait 90
# load the save, go to the scene, then:
py -3 -I tools/perf.py --instance NAME --rounds 3 --windows 5 --seconds 10 --csv out.csv
py -3 -I tools/instance.py stop NAME
```

`perf.py` leaves auto panel off and `perf` on; `cmd.py --instance NAME "auto on" "perf off"`
restores them. Note: Azahar rewrites the instance's `qt-config.ini` on exit, so
to change the resolution for a run set both `resolution_factor` and
`resolution_factor\default=false` while it is stopped.
