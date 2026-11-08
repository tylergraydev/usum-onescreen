# Panel performance

What the bottom-screen panel costs Azahar, measured 2026-11-02 with
`tools/perf.py`. **Now (P1-6):** the panel is copied by GPU transfers and costs
Azahar 0-4% of its speed (1/2 and full), 8% for 2/3; it was ~40% (H5, below).
The sections after "P1-6" are H5's original measurements of the old CPU copy.

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

### What the panel does now

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

**Map at 21:9 crashes on main too** (`b559b40`, `layergpu` not involved;
fine with `aspect off`). `PanelCopyBottomRect` copies its picture with one
TextureCopy per destination column when the screen is squeezed, a few
hundred a frame, the same pattern as the crashes above. Not fixed here.

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
