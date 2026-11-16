# GPU overlay backend (phase 2)

Phase 1 (`53a16c6`) was visually verified sharp at 5x in Azahar, with one
GX command/frame and three-minute X menu/Party soaks. Phase 2 expands that
backend to all native layers. **Phase 2 still needs the plugin build and emulator
checks below**; historical results do not validate the new screens.

## Optional screen filters (backlog #24)

`[display] filter = off|warm|cool|vignette` defaults to **off**. Live commands:
`filter <name>` changes the session setting; bare `filter` reports selection and
backend readiness. Reloading the INI replaces the live choice. Invalid names
warn and preserve the previous selection. `vivid`, sharpen and bloom are not
implemented or accepted.

There is a per-frame injection point: `DisplayTransferHook` runs our list
**before** forwarding the game's top copy (`256x512 -> 240x400`, `0x1004`).
The pass draws directly into that tiled render target, including the field and
battle picture when no plugin menus are visible. It uses the existing rotated
viewport (y=112..511), with no widescreen squeeze on filter geometry. Filters
run after hi-res bands and menus, before state restoration and the game's copy.
OSD/CPU/legacy UI and Map's later picture copy are drawn afterwards and are not
filtered. This is backend-independent PICA work in Azahar, rather than an
OpenGL post-processing shader; Vulkan output still needs emulator validation.

The safe point does **not** provide a separate texture of the game picture.
Sampling the render target while writing it is a feedback hazard. TEV cannot
read the destination framebuffer; blending can use it without texture feedback.
Warm/cool therefore use TEV REPLACE constant RGB/alpha and source-over blending:
`out = (tint * 24 + destination * 231) / 255`. Warm RGB is (255,180,96), cool
(128,208,255). These are gentle washes, not saturation/contrast or LUT grading.
Vignette draws five disjoint rectangular rings, each 10 pixels horizontally and
6 vertically, alpha 25,20,15,10,5 from outside inward. The central 300x180 is
untouched, and corners are not blended twice. This intentionally subtle stepped
vignette is not a smooth radial mask. Texture units are disabled for filter
quads; target alpha is preserved. The original shader and shadow restore suffice.

**GX per recognised top present:** off adds **0**; enabled adds **0** when the
hi-res overlay already submits its list, otherwise **1** (one total GpuQuad
list, no transfers). Map retains its existing separate picture copy (two plugin
commands total with the target list). No extra render pass or copy is added.
The legacy LayerGpu presenter can use 6-8 commands alone, so any selected filter
conservatively makes it take its existing CPU fallback, even if the filter
backend is unavailable. This prevents stacking those transfers with the filter;
it may reduce a fallback frame to 1x. Off preserves the original legacy path.

**New persistent pixel/linear/heap buffer bytes: 0.** Tint uses 96 vertex bytes;
vignette uses 1,920, both inside the existing 8-KiB VBO. Each filter quad adds
120 command bytes, plus at most one 8-byte sampler write and existing alignment
padding, inside the existing 16-KiB list. These are reused capacities, not new
allocations. The original vertex overflow guard refuses a whole list rather
than publish partial geometry. Filter bookkeeping adds six static bytes before
alignment (quad index, active enum, atomic selection), plus one Settings enum
field; executable/constant growth still reduces the plugin heap. No textures,
shader binaries, or large static tables are added.

Sharpen, true vivid/saturation grading and soft bloom need a separate source
picture and a sampling/combine design. A full RGBA8 256x512 source is **524,288
bytes**, plus **at least one copy GX command** per present before the drawing
list (at least two plugin GX total; Map adds another). The existing 384,000-byte
FB diagnostic target cannot hold that source. A cropped 240x400 source is
384,000 bytes but needs a validated crop/tiling/high-resolution-preserving copy.
Do not assume the current atlas blur can read the game's picture: it only reads
plugin menu artwork. Neither copy approach is implemented or validated here.

Host verification (native Windows, no make/WSL): after `vcvars64.bat`, directly
compiled and ran the following with MSVC `/nologo /std:c++17 /EHsc /W4 /WX
/I Includes`, executables/objects under `tests/build/`. For example, from the
repository root in a VS developer command prompt:

```bat
cl /nologo /std:c++17 /EHsc /W4 /WX /I Includes tests/filter_test.cpp Sources/Settings.cpp Sources/SettingsFile.cpp /Fe:tests/build/filter_test.exe /Fo:tests/build/
tests\build\filter_test.exe
```

The targets/checks run were:

- `tests/filter_test.cpp Sources/Settings.cpp Sources/SettingsFile.cpp`: defaults,
  parse/reload/live parser, rejected names, constant TEV/source-over samples,
  every vignette pixel's coverage/alpha and zero-alpha identity.
- `tests/settings_test.cpp Sources/Settings.cpp Sources/SettingsFile.cpp`:
  **224 checks, zero failures**, including default-file round trip.
- `tests/blur_test.cpp`: existing production-UV blur reference passed.
- `python -I tests/filter_command_test.py` and `python -I
  tests/blur_command_test.py`: real submission-site/source guards passed. These
  prove the static one-list/no-copy structure, not GPU execution or queue timing.
- devkitARM `arm-none-eabi-g++ -fsyntax-only` with the repository's ARM/3DS flags
  on GpuQuad.cpp, LayerGpu.cpp, Settings.cpp and main.cpp passed.

Off emits no filter vertices/register writes and leaves the old command stream
and target bytes unchanged by construction. No emulator pixel capture or linked
plugin build was performed. Runtime checks still required: field/battle, Party,
X menu, Bag and Map at 1x/5x, stock/16:9/21:9, Vulkan and OpenGL; toggle all
filters/off, hires and settings reload, inspect ring seams/orientation/tint,
unchanged target alpha and game state, verify GX/frame=1 (Map=2), and soak for
crashes. Check CPU fallback resolution and menu ordering too. Existing Azahar
synchronous execution, shadow coverage, stereo and real-hardware limits apply.

## Controls and fallback

`hires on|off|status` switches all native Layer users live. Readiness requires
installed hooks, allocated linear memory, target mode, shadow and restore, and
no checkerboard test. `gpuquad test on|off`, `mode fb|target`, `restore on|off`,
`shadow on|off`, and `on|off|status|reset` retain their diagnostic meanings.
`hires on` re-enables the backend. Test transitions increment the generation,
forcing all visible layers to redraw. FB mode remains an explicit diagnostic
with three GX commands; normal hi-res uses target mode and one command list.

SetupHiRes retains each caller's original 1x fallback buffer and layout. Backend,
slot, allocation or setup-lock failure draws the 1x Layer/LayerGpu path for that
frame, and retries hi-res on the next callback. Upload contention retains dirty
content for retry, reusing the last complete GPU snapshot. A newly set-up layer
cannot publish until Begin has cleared and drawn its staging surface. Invisible
screens Hide; leases also expire after more than two recognised top transfers.

## Slots, atlas and ownership

Four named slots composite in this order, in **one GX command list**, followed
by one shadow-state restore:

| Slot | Users | Concurrent use |
|---|---|---|
| 0 MenuSlot | BattleScreen, XMenu, Party/summary, Bag, Boxes menu, Pokedex, SubScreens (Options/Passport/Video), Keyboard, Map, ThemeScreen, UI demo | Primary pages share ScreenBuffer's exclusive owner; a new owner invalidates the old Layer |
| 1 RotomSlot | Rotom field prompt or ready badge | Independent of primary menu and Map |
| 2 BadgeSlot | Boxes details badge / Battle foe info | Separate from primary menu; Boxes and Battle scenes are mutually exclusive |
| 3 MoveLearnSlot | MoveLearn Choose/Confirm/Working/Done | Separate from battle menu, drawn last |

Battle foe info uses BadgeSlot only on the Command page; Boxes and Battle are
mutually exclusive scenes. Its 154x112 logical rectangle is 385x280 at 2.5x,
with a separate 68,992-byte static fallback buffer. The primary menu keeps its
existing rectangle and slot. Both layers redraw on content changes only.

Layer tracks its owning object per slot: an inactive screen cannot Hide another
screen's quad, and an old layer cannot Present staging overwritten by a new owner.
The backend additionally checks the staging pointer/dimensions against its live
allocation, so an expired/reclaimed region forces setup and redraw.

The 1024x1024 RGBA8 atlas (GPU bytes A,B,G,R) is laid out as follows:

- x=0..1010, y=0..1023: dynamically packed live layer rectangles, no compaction.
  QuadAllocator searches fixed rectangle-edge candidates, with one transparent
  texel between allocations. Staging uses the same rectangles and 4096-byte stride.
- x=1011: separator; x=1012..1023, y=0..239: twelve legacy band phases.
- A 400x240 Bag uses 1000x600 at (0,0); a 376x224 menu uses 940x560.
  With the former, MoveLearn's largest 600x395 fits at (0,601), ending y=996.
  A 250x55 Boxes badge can fit to its right at (601,601).
  A Rotom prompt up to 940x400 fits below the full menu instead.
  The tallest 940x490 Rotom choices cannot fit beside a full menu: the
  later allocation uses 1x until space is released (short prompts/badges fit).
- Two full menus cannot fit. Arbitrary combinations can exhaust or fragment the
  atlas: the refused layer uses 1x for that frame, with no partial publication.
  No slot borrows an overlapping region. Generic UpdateGpuQuad publishers must
  also avoid live overlaps and the reserved band columns; first publish is dirty.

Dirty updates seed the back atlas from the front once after a swap, tile only
the changed rectangle, clear its one-texel border, restore band texels and flush.
Texture address and all quad records snapshot together under a nonblocking mutex.
The hook never waits for an uploader. OSD staging is serial; it must not be edited
concurrently by another producer. The shader-shadow RecursiveLock remains separate.

## Bands and Map window

### Opt-in blurred-picture bands

`[display] pillarbox = blur` now low-passes the current menu atlas through a
64x64 -> 32x32 -> 16x16 -> 8x8 render-to-texture pyramid. The old nine sparse
full-resolution taps built and passed the host suite, but Ultra Sun at 21:9/5x
showed noisy horizontal streaks in Party/Bag: the taps aliased text and borders.
That emulator result does not validate this replacement, which needs a new run.

The first level averages a dense 4x4 bilinear grid per pixel, using running
source-over weights `1/(tap+1)` rounded to eight bits. Each later level explicitly
averages four neighbours at half-input-texel offsets. These explicit offsets keep
the kernel's logical footprint when Azahar renders intermediate targets at 5x;
relying only on bilinear 2:1 resizing would shrink the effective blur radius.
At 1x the final texel integrates 1024 bilinear source samples, rather than nine.
A source guard inset contains all shifted footprints inside the owning atlas
allocation, including at higher raster scales. Each level uses a separate aligned
scratch range, framebuffer flush/invalidate and texture-cache clear before reuse.

The final 8x8 texture is bilinear-filtered, stretched across the full 400x240
picture and cropped into the two side bands. Only then is RGB multiplied by
128/255; alpha is forced opaque so the game scene cannot bleed through. Sharp
menu textures, rounded corners and skip alpha keep their original sampling.
If generic publishers overlap, the last eligible slot owns the blurred background;
native primary menus already have exclusive ownership.

**Five render passes: four reductions (16+4+4+4 draws), then the scene (two band
draws plus visible menus). One GX command list and one state restore total.**
**Added GX commands per present: zero (1 before, 1 after)**; Map retains its
separate picture copy (2 plugin commands before and after). Counts exclude the
game's own lists/transfers. No DisplayTransfer, TextureCopy, CPU image processing
or readback is added. Tint remains the default; `hires off`, unavailable backend
and CPU/legacy LayerGpu presentation use tint for Blur.

**Working pixel buffer: 21,760 bytes, all reused from the existing 384,000-byte
FB diagnostic target. New allocated buffer bytes: 0.** The four disjoint RGBA8
levels occupy 16,384 + 4,096 + 1,024 + 256 bytes. Diagnostic FB mode cannot run
hi-res publishing concurrently. The scratch allocation, plugin heap, 8-KiB VBO
and 16-KiB list allocation are unchanged. The old 85-byte alpha array is removed;
blur metadata is now two 4-byte counters (8 bytes before alignment). Normal blur
plus four visible menus needs 34 quads/3264 VBO bytes. Arbitrary mixed publishers
still fail the existing overflow guard rather than submitting a partial frame.

`blur_test` now renders high-detail alternating rows, checkerboard, thin stripes
and noise with a large brightness shape through the **production reduction UVs**.
The reference models bilinear sampling, RGBA8 quantization, all blend weights,
clamping and final darkening, with intermediate raster scales 1 and 5. At both
16:9 and 21:9, across Party/Bag and an offset atlas region, it asserts a maximum
adjacent row/column difference <=5 over the bands, retained large shapes, exact
constant darkening and no neighbour contamination. Measured maxima: **1-2 levels**;
the rejected sparse kernel gives **15-48 levels** on the same fixture and fails
the same bound. White becomes 128, black stays 0. No external pictures are needed.
`blur_command_test.py` guards the real submission sites and in-list reduction loop.
Settings and multi-format/corner/skip fallback tests remain in place.

Host math does not prove PICA output. Rebuild and soak Party, Bag and Boxes at
21:9/5x (also 1x and 16:9), inspecting gradient smoothness, correct orientation,
no stale/corrupted bands, centre sharpness, heap and `gpuquad status` GX/frame=1.
Toggle hires, reload settings, close/reopen and change pages. Four new offscreen
targets introduce cache/target-switching risk; measure performance in Azahar.
Map's later picture copy is absent from the menu atlas, so transparent window
pixels contribute their stored RGB (normally black), rather than the map picture.

Targeted Windows verification for this change used the Makefile's expanded
source lists directly with native MSVC (no host g++/clang++ was installed),
without make or WSL. After `vcvars64.bat` from VS 2022 Community, these exact
successful compile/run commands ran from `tests/`:

```bat
cl /nologo /std:c++17 /EHsc /W4 /WX /I ../Includes blur_test.cpp /Fe:build/blur_test.exe /Fo:build/
build\blur_test.exe
cl /nologo /std:c++17 /EHsc /W4 /WX /I ../Includes settings_test.cpp ../Sources/Settings.cpp ../Sources/SettingsFile.cpp /Fe:build/settings_test.exe /Fo:build/
build\settings_test.exe
cl /nologo /std:c++17 /EHsc /W4 /WX /wd4244 /I ../Includes hires_test.cpp ../Sources/UI/Raster.cpp ../Sources/UI/Text.cpp ../Sources/UI/Font.cpp ../Sources/UI/Layer.cpp ../Sources/Settings.cpp ../Sources/SettingsFile.cpp /Fe:build/hires_test.exe /Fo:build/
build\hires_test.exe
cl /nologo /std:c++17 /EHsc /W4 /WX /wd4244 /wd4701 /D_CRT_SECURE_NO_WARNINGS /FI<worktree>/tests/build/msvc_gnu.hpp /I ../Includes pillarbox_test.cpp ../Sources/Screens/XMenuGrid.cpp ../Sources/ActionQueue.cpp ../Sources/UI/GameAssets.cpp ../Sources/UI/Theme.cpp ../Sources/UI/ThemeDemo.cpp ../Sources/RomfsText.cpp ../Sources/PartyData.cpp ../Sources/Settings.cpp ../Sources/SettingsFile.cpp ../Sources/UI/Raster.cpp ../Sources/UI/Text.cpp ../Sources/UI/Font.cpp ../Sources/UI/Layer.cpp ../Sources/UI/Icons.cpp ../Sources/UI/Glyphs.cpp ../Sources/UI/Demo.cpp /Fe:build/pillarbox_test.exe /Fo:build/
build\pillarbox_test.exe
```

The ignored `build/msvc_gnu.hpp` contains only `#define __attribute__(x)` for
ActionQueue's existing GCC format annotation. The warning exceptions cover
existing numeric conversions, GameAssets uninitialized-variable diagnostics and
CRT deprecation; no production sources were changed to accommodate MSVC.
The four targeted tests passed again after the low-pass correction (settings: 224 checks; pillarbox: zero failures). Initial strict
MSVC UI builds failed on those existing compiler incompatibilities before these
flags/shim were applied. From the repository root, these also passed:

```powershell
python -I tests/blur_command_test.py
& C:/devkitPro/devkitARM/bin/arm-none-eabi-g++.exe -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -Os -mword-relocations -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-strict-aliasing -Wall -D__3DS__ -fno-rtti -fno-exceptions -std=gnu++17 -I Includes -I C:/devkitPro/libctrpf/include -I C:/devkitPro/libctru/include -fsyntax-only Sources/UI/GpuQuad.cpp
git diff --check
```

No plugin link/build, full host suite or emulator check was run here.

PillarboxColumns retains the existing eligibility and SourceColumn boundaries:
top full-menu insets, squeeze >1, Tint/Pattern/Blur enabled. BandRuns emits destination
coordinate strips **without applying squeeze again**. Pattern uses eight-column
phase runs; Tint stretches phase 8. The three columns touching the menu are phases
11,10,9 outward, as in WriteBandColumn. Bands use nearest texture sampling and
cover full screen height; menu textures switch back to bilinear sampling. Off emits
no band quads. All band and layer strips share the same list and state restore;
there are no hi-res CPU framebuffer writes or band TextureCopies.

Map::Present(fb, MapWindow) zeros the staging alpha throughout the physical skip
rectangle on dirty content. The hook draws this transparent menu into the game
target before the game's top DisplayTransfer. MapOnScreen then calls
PanelCopyBottomRect (GPU TextureCopy) in the OSD callback, after Present, so the
current game picture lands on top of the framebuffer. A copy failure dirties the
next redraw and uses the existing unavailable-picture view. Rounded corners also
retain alpha zero. Picture copying is an existing separate GPU operation, so
Map's total GX work includes that copy in addition to the one overlay command.

## Images, text and unchanged 1x output

Canvas::DrawImage accepts RGBA or packed RGB5A1, crop/destination rectangles,
tint and rotation. Native-size 1x copies keep the exact original integer tint,
alpha and source-over calculations. At 2.5x, nearest sampling assigns every
physical pixel to one source texel for PokeIcons (box/party/dex sprite consumers).
Premultiplied-alpha bilinear sampling handles decoded GameAssets images (including
type plates/balls) and Theme's tinted/rotated cursor sprites without dark fringes.
Crop clamping prevents neighbouring sheet icons leaking into the result.

Converted direct loops: PokeIcons::DrawPokeIcon, GameAssets::DrawImage,
Theme::DrawTinted. UI/Icons already uses scaled lines/triangles/masks; Theme
background helpers use Canvas HLine/rectangles. Font/native-cell decoding and
DrawMaskSized remain scaled from phase 1. Canvas::GetPixel now checks logical
bounds before mapping a scaled read. Remaining framebuffer probes in XMenu,
PartyScreen and SubScreens detect game state; they do not draw art and are unchanged.
Legacy CPU/LayerGpu band helpers remain exclusively for the fallback path.

Font layout stays in 1x coordinates: advances, wrapping, ellipses and alignment
are unchanged. Native system/game glyph masks are decoded and sampled on redraw;
system Body detail tops out near 1.67x before upsampling. No glyph cache growth.

## Redraw and memory cost

A full layer clears/rasterizes/uploads about 6.25x its 1x pixels: 940x560 =
526,400 or Bag's 1000x600 = 600,000 physical pixels (2.4 MB staging content).
A dirty upload still seeds/copies and flushes the 4-MiB atlas as in phase 1;
CPU time is not measured for phase 2. Unchanged layers only renew metadata, with
no clear, rasterization, tiling or texture flush. Corner masking now visits only
four radius-sized squares, rather than computing sqrt over the full surface.
Tint/Pattern bands add at most 58 strips, no GX submissions; Blur uses the fixed
four reduction passes and two band draws described above. Vertex capacity is 8 KiB and
cache flush covers the actual vertices. Typical 16:9/21:9 uses far fewer strips.

Normal screens gate Begin on version/DrawKey or data equality. Party/summary,
Map, Bag, Dex, Battle and MoveLearn can poll without repainting unchanged data.
Boxes' 30-frame refresh now compares fields instead of repainting every poll;
new icon arrivals are batched every eight frames while icons load. Cursor moves
and text/result/progress changes can still redraw the whole layer. The UI demo
changes selection every 60 frames. Explicit `uidemo direct` and `shade` diagnostics
retain their per-frame CPU shade drawing; they are not representative idle menus.
Rotom/Loto redraws on published text/state changes. No new every-frame animation
redraw is introduced. Measure upload counters during real battle/party timers.

Persistent linear allocation: **12,992,512 bytes (12.39 MiB)**, phase 1 plus
4 KiB for the larger VBO. Front/back tiled atlases and staging remain 12 MiB;
FB test target, list and shader resources otherwise unchanged. Band CPU scratch
is 11.25 KiB static plus a 960-byte temporary column. Debug test uses an extra
4 MiB temporarily. Allocation failure preserves 1x output.

## Host and emulator verification

`make -C tests` uses C++17 `-Wall -Wextra -Werror`. image_test covers independent
legacy 1x bytes in five formats, tint/rotation, crop isolation, nearest no-gap
scaling, premultiplied alpha and guards. hires_test covers packing, refusal,
reclaim, border clearing, all band runs against legacy phase selection, first-Begin
publication, ownership changes, live fallback/retry, rounded and skip alpha.
Every existing screen test adds guarded 2.5x Layer smoke draws through real
SetupHiRes/Begin/Present; original 1x assertions remain. Windows host verification used clang C++17 with -Wall -Wextra -Werror:
26 of 27 C++ targets pass; screen_buffer_test is blocked by its existing
Linux-only unistd.h/mallinfo dependencies. All 38 generated 1x preview artifacts
from 14 targets match HEAD 53a16c6 byte for byte. Layout generation and synthetic
signature checks pass; real game-data checks were skipped. Generated logs and
baseline artifacts remain ignored under tests/build/.

Claude should build the plugin and inspect **every page/state** at 1x and 5x,
stock aspect, 16:9 and 21:9, and toggle hires live:

- XMenu; Party list and summary tabs; every Bag pocket; Boxes slots, held icons,
  loading icons and details badge; Dex top/list/grid/entry; keyboard pages.
- Battle commands, moves, targets, party/bag transitions; MoveLearn choose,
  confirm, working, skip and done, especially concurrent battle controls.
- Options, Passport, Battle Video, Theme preview pages, and UI demo.
- Map with picture visible/unavailable, pan/location changes and Rotom badge or
  prompt; all Rotom choices/replies/Loto states alongside native menus.
- Check type plates, balls, cursors and sprites, alpha corners/window, band phase
  edges/Off, layout and colors. Close/reopen/switch screens rapidly to expose stale
  ownership. Force allocation refusal in a review harness; fallback must draw.
- Idle uploads must settle; cursor/text/icon changes upload. Confirm GX/frame=1
  for the overlay (Map picture adds its separate copy). Soak XMenu, Party, Boxes,
  battle+MoveLearn and Map; verify game render state survives hide/test transitions.

Real hardware fences/FINALIZE, Ultra Moon and stereo/right-eye output remain
unverified. Azahar's synchronous GX execution remains a requirement.

---

## Historical verified spike (`a5e717e`)

Can the plugin draw a textured rectangle on the top screen with the 3DS GPU
(PICA200), so that Azahar renders it at its internal resolution? Spike code:
`Sources/UI/GpuQuad.cpp`, shader `shaders/gpuquad.v.pica` (assembled with
picasso into `Includes/UI/GpuQuadShader.inc`). Tested in Azahar only (instance
`lrtoggle`: 21:9, 5x internal resolution, frame limit 250%), Ultra Sun v1.2.

**Result: yes, in Azahar.** One 1024x512 RGBA8 texture is drawn as an
alpha-blended quad in the right place, the game keeps rendering correctly, and
the game survived 3 minutes in every scene tried. Whether it *looks* sharp at
5x can't be read back by the plugin (screenshots read the framebuffer at 1x);
it needs a person looking at the Azahar window.

## Commands

`gpuquad on|off|status|reset`, `gpuquad mode fb|target` (target is the default), and two debug switches:
`gpuquad restore off` (skip the state restore) and `gpuquad shadow off` (stop
parsing the game's command lists). `status` prints the counters below.

## How it draws

Two pipelines, same command list:

- **`mode fb`** (the pipeline the spike was asked to test), from the OSD
  callback: DisplayTransfer the top framebuffer (linear BGR8, 240x400) into a
  tiled RGBA8 240x400 target in our linear scratch, run one PICA command list
  that draws the quad into it, DisplayTransfer it back. **3 GX commands a
  frame.** Works and stays on the GPU (Azahar's DisplayTransfer accelerates
  linear<->tiled with format conversion), but see Stability: with the native
  Party menu on top (LayerGpu, 8 commands) the game crashed.
- **`mode target`** (recommended): a hook on the game's gx DisplayTransfer
  (`0x27A5BC`, called only by nngx's request runner) recognises the game's own
  top-screen copy, `256x512 -> 240x400, flags 0x1004` (tiled RGBA8 render
  target -> BGR8 framebuffer, bit 2 crops the input), and pushes our command
  list drawing into the game's render target just before it. **1 GX command a
  frame, no extra transfers.** The visible 240x400 are the first memory rows of
  the 256x512 target, which is window y 112..511 (viewport xy `0x00700000`).
  Overlays drawn later from OSD callbacks (LayerGpu menus) cover the quad.

The command list (2112 bytes): framebuffer flush/invalidate, our colour buffer
(no depth buffer), viewport/scissor off/culling off/clip plane off/early depth
off, alpha blending `src*a + dst*(1-a)` with no alpha/stencil/depth test,
texture unit 0 (RGBA8 1024x512, bilinear, clamp), six texture combiners (stage
0 = texture, 1-5 pass-through), fog/gas off, lighting enable 0, the vertex
shader (libctru `shaderProgramUse`: 3 code words and 2 operand descriptors at
offset 0, entry point, output map), one attribute buffer (position float4 in
clip space, texcoord float2, stride 24; buffers 1-11 zeroed), a 4-vertex
triangle strip with `DRAWARRAYS`, flush/invalidate, then the restore. It has no
`GPUREG_FINALIZE`: in Azahar only that write raises the P3D interrupt, so the
game's nngx never sees an interrupt for our list (on hardware GSP needs it; see
Risks).

The game's GPU registers can't be read back, so **the restore comes from a
shadow**: a hook on the game's gx ProcessCommandList (`0x279D5C`) walks every
command list the game submits, following `CMDBUF_JUMP` like the GPU, and keeps
the last value of each register our draw touches (138 registers, found at boot
by a dry run of our own list) and the vertex shader code/operand memory. After
the draw the list writes those values back (142 writes plus 3 code words and
2 operand descriptors re-uploaded). A `RecursiveLock` keeps the game's lists
and ours in order (Azahar runs each GX command when it is pushed).

### Does the game re-set its state every frame?

No. `status` watches, after each of our draws, which registers we changed are
still not written again by the game when it issues its next draw:

| Scene | Registers the game relied on keeping |
|---|---|
| Field (`mode fb`) | `0E0 100 104 112 229 22A 242 253 254 25E 2B1-2B4` |
| Party (`mode fb`) | `081 0C8-0CC 0D0-0D4` (texture combiners 1 and 2) `229 254 25E 289 2B2-2B4` |
| Field, Party, Battle (`mode target`) | `047 062 080 081 0C8-0CC 0D0-0D4 118 11B 229 254 25E 289 2B2-2B4` |

Every frame had stale state, so the restore is required (`restore off` showed no
visible damage in the field, which proves nothing). Shader memory was never
stale: the game re-uploads code before its next draw. Registers the game never
writes at all (`254`, integer uniforms `2B2-2B4`) are restored as 0.

## Numbers

GX commands per frame: `mode fb` 3, `mode target` 1 (plus LayerGpu's 6-8 when a
native menu is open).

Emulated CPU time (svcGetSystemTick):

| | Field | Party | Battle |
|---|---|---|---|
| Game lists per top frame | 20 | 6 | 35 |
| Words walked (incl. jumps) | ~67-70k | ~15k | ~87k |
| CMDBUF jumps | ~1340 | 0 | ~1140 |
| Shadow parse per top frame | 6.6 ms | 1.5 ms | 6.6 ms |
| Our draw (build list + push) | 0.6-0.9 ms | 0.6-1.8 ms | 0.6 ms |

The parse was 11 ms before filtering by register (skipping commands for
untracked registers by header) and -O2. It runs whether or not the quad is on.

Speed (emulated / wall time, `tools/perf.py` method, 3 alternating rounds of 8 s,
21:9, 5x, frame limit 250%):

| Scene | base (no shadow, no quad) | shadow only | quad on |
|---|---|---|---|
| Field, `mode fb` (run 1) | 1.497 (sd 0.032) | 1.481 (0.039) | 1.523 (0.065) |
| Field, `mode fb` (run 2) | 1.461 (0.008) | 1.442 (0.068) | 1.505 (0.115) |
| Field, `mode target` (run 1) | 1.551 (0.114) | 1.476 (0.047) | 1.373 (0.011) |
| Field, `mode target` (run 2) | 1.419 (0.003) | 1.478 (0.130) | 1.404 (0.118) |
| Battle, `mode target` | 1.549 (0.033) | 1.515 (0.036) | 1.488 (0.042) |

The game stayed at 30 frames per emulated second in every window. Window to
window noise is about +-0.1, so no cost is clearly measurable; the worst
reading (`mode target`, field run 1) is ~10% under its base, and the repeat
was within noise.

Texture: drawing the 1024x512 test picture with `UI::Canvas` took 205 ms
(per-pixel FillRect, not representative); tiling it (CPU Morton swizzle) 34 ms
(emulated, -O2).

## Stability (3 minutes each, quad on)

| Scene | `mode fb` | `mode target` |
|---|---|---|
| Field (Route 2) | ok, 8515 frames | ok, 8624 frames |
| X menu (native, LayerGpu 8 cmds) | ok, 9236 frames (11 GX/frame) | ok, 8096 frames (9 GX/frame) |
| Party (native, LayerGpu 8 cmds) | **crash within ~1 s** (11 GX/frame): `unmapped Read32 @ 0x20 at PC 0x00402690`, the same PC as the old 15-command Party crash in PERF.md | ok, 8800 frames (9 GX/frame) |
| Party, `layergpu off` | ok, 10115 frames (3 GX/frame) | |
| Wild battle (native battle menu on) | not run | ok, 8077 frames |

`0x402690` is in the game's per-screen render loop (`0x402648`): it reads
`+0x20/+0x24` of a null render-target pointer for one of the three screens.
That fits the earlier finding that extra GX transfers per frame break the
Party screen; it is consistent with extra PPF interrupts from our
DisplayTransfers/TextureCopies confusing the game's frame pacing (our command
list raises none), but that is not proven.

## Turning the menus into this

- Texture: a menu drawn by the CPU at 2.5x is 1000x600, so a 1024x1024 RGBA8
  texture (4 MB of linear memory; 37 MB of the application region is free) or
  RGBA4/RGB5A1 at 2 MB. 2x (800x480) fits 1024x512 RGBA8 (2 MB). The test
  texture was 1024x512 shown on 400x200 (2.56 texels a pixel).
- Fonts: the shared system font's glyph sheets are drawn at 3/5 size today;
  their native size is ~1.67x that, so text is crisp up to ~1.67x and
  bilinear-soft above it (still sharper than 1x upscaled).
- Upload when a menu changes: CPU drawing grows with the pixel count (6.25x at
  2.5x). Tiling a 1024x512 RGBA8 picture took 34 ms; only dirty rectangles need
  re-tiling, or a DisplayTransfer can tile linear -> tiled on the GPU (one extra
  GX command, on change only). Textures are CPU-written once per change; Azahar
  re-reads them when the memory changes.
- Drawing: one quad per menu (or a few for layers), per frame, through
  `mode target`: one GX command. The LayerGpu transfers (6-8 commands) and the
  pillarbox bands could become part of the same list, so a native menu would
  cost one GX command a frame instead of 8.
- Widescreen: the quad is squeezed by `WidescreenSqueeze()` in its vertex
  positions (400 / 1.4 = 286 px at 21:9), like the menus.

## Risks

- **Real hardware is untested and not expected to work as is**: no
  `GPUREG_FINALIZE` (GSP waits for P3D to finish a list), the game's nngx
  would then see an extra P3D interrupt, and the GPU runs asynchronously, so
  the shadow (taken at submit time) and our list may not match the GPU's
  order. Azahar runs each command at once, which is what makes this work.
- The shadow parse costs ~6.6 ms of emulated CPU per frame in the field and in
  battle (on the game's thread that runs nngx), even with the quad off. No fps
  loss was seen, but a heavier scene could drop frames.
- Restore correctness depends on the shadow: registers the game never wrote are
  restored as 0; registers written before the hook (boot) were never seen in
  testing. A missed `CMDBUF_JUMP` or list submitted another way (only
  `0x279D5C` was found) would leave stale state.
- `mode fb` adds 2 DisplayTransfers a frame and crashed the Party screen with
  the native menu on. `mode target` depends on the game's top-screen transfer
  keeping its shape (`256x512 -> 240x400`, `0x1004`); 3D (right eye) is not
  handled.
- Shader memory offsets 0-2 / descriptors 0-1 are overwritten and restored every
  frame; the game re-uploads before its draws anyway.

## Not verified

- **Sharpness at 5x.** The plugin's screenshots read the framebuffer at 1x, so
  they show placement, blending and the game picture, not resolution. Look at
  the Azahar window: at 1x the 1-texel checkerboard is grey and the 5x7 text on
  the top line is unreadable; at 5x both should resolve.
- `mode fb` in battle (not soaked; `mode target` was).
- Ultra Moon (same addresses and prologue statically; not run).
- The cause of the Party crash.
