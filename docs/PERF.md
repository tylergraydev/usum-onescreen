# Panel performance (H5)

What the bottom-screen panel costs in frame rate, measured 2026-11-02 on Azahar.
Short answer: with the panel shown (either size) Azahar's speed drops by about
40%, and the cost is **reading** the bottom framebuffer, not drawing on the top
one. On the test PC that takes the game from above full speed to slightly below
it (~27-31 fps instead of 30). P1-6 is needed, and it should remove the
per-frame CPU read rather than only skip unchanged frames.

## Method

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

## Results

Mean ± standard deviation over windows. Speed 1.00 = real time.

### Field: Pokémon Center on Route 2 (`FieldRo`), standing still, 5x resolution

3 rounds × 5 windows of 10 s per mode; 4-6 Azahar processes running (mostly 6).

| mode  |  n | speed       | fps wall     | fps emu | copy (emu µs/frame) | draw (emu µs/frame) | panel share of emu CPU |
|-------|---:|-------------|--------------|--------:|--------------------:|--------------------:|-----------------------:|
| off   | 15 | 1.59 ± 0.10 | 47.5 ± 3.0   | 29.92   | –                   | –                   | –                      |
| panel | 15 | 0.90 ± 0.03 | 27.0 ± 0.9   | 29.91   | 6722 (max 7117)     | 2270 (max 2615)     | 26.9%                  |
| full  | 15 | 0.93 ± 0.07 | 27.7 ± 2.1   | 29.89   | 6688 (max 7004)     | 8783 (max 9135)     | 46.2%                  |

### Pokédex (`Zukan`, a full-screen app; list page on the bottom), 5x resolution

2 rounds × 5 windows; 5-6 Azahar processes (mostly 5).

| mode  |  n | speed       | fps wall     | fps emu | copy µs | draw µs | panel share |
|-------|---:|-------------|--------------|--------:|--------:|--------:|------------:|
| off   | 10 | 1.68 ± 0.07 | 50.2 ± 2.1   | 29.92   | –       | –       | –           |
| panel | 10 | 1.01 ± 0.05 | 30.1 ± 1.4   | 29.92   | 6684    | 2271    | 26.8%       |
| full  | 10 | 1.04 ± 0.03 | 31.0 ± 1.0   | 29.90   | 6686    | 8746    | 46.1%       |

### Field at 1x internal resolution

Same spot, 2 rounds × 4 windows; 4-5 Azahar processes (mostly 5).

| mode  | n | speed       | fps wall   |
|-------|--:|-------------|------------|
| off   | 8 | 1.68 ± 0.04 | 50.3 ± 1.1 |
| panel | 8 | 1.06 ± 0.04 | 31.8 ± 1.1 |
| full  | 8 | 0.97 ± 0.07 | 29.0 ± 2.1 |

### Which half costs: copy only vs draw only (temporary build, not committed)

A throwaway build changed the modes so that `panel` drew the half-size panel
from a capture taken once (draw only, no reads of the bottom screen) and
`full` copied the bottom screen every frame but drew nothing (copy only).
Field, 1x resolution, 2 rounds × 4 windows; 2-3 Azahar processes.

| variant           | n | speed       | fps wall   | copy µs | draw µs |
|-------------------|--:|-------------|------------|--------:|--------:|
| off               | 8 | 1.95 ± 0.05 | 58.3 ± 1.4 | –       | –       |
| draw only (1/2)   | 8 | 1.92 ± 0.05 | 57.5 ± 1.6 | –       | 2271    |
| copy only         | 8 | 1.24 ± 0.10 | 37.1 ± 3.1 | 6723    | –       |

## What the numbers say

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

## Recommendation: P1-6 is needed

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

## Reproducing

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
