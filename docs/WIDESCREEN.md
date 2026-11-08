# Experimental widescreen (3D and 2D)

Ultra Sun / Ultra Moon USA v1.2 (`0x0820`) have byte-identical perspective
builder code at `0x3631CC`:

```cpp
void Perspective(Matrix34or44 *out, u32 pivot,
                 float fovy, float aspect, float near, float far);
```

The game uses hard-float: `out` / `pivot` arrive in `r0` / `r1`, and the four
floats in `s0`â€“`s3`. Pivot is screen rotation (0â€“4). The builder computes
cot(fovy/2) and divides by aspect at `0x3632C4`. Its only known callers are
`0x4C1FF0`, `0x4C2094`, and `0x4C20E4` in camera update/blend code.

The plugin installs two CTRPF MITM hooks at startup, alongside the input hooks,
after game detection and hook verification. It logs
`widescreen: perspective hook at 003631CC -> N` (`0` = success).
With a nonzero active aspect, incoming aspects within 0.001 of 400/240
(`0x3FD55555`, the top screen's 5:3) are replaced. All other aspects, including
the bottom screen's 4:3, pass through. Off passes every call through.
Changes take effect on subsequent perspective calls, without recreating cameras.

The hook is a plain C++ function compiled with the repo's `-mfloat-abi=hard`.
Like `Buttons.cpp`, it uses `Hook::InitializeForMitm` and `Enable`, then calls
`HookContext::GetCurrent().OriginalFunction<void>(out, pivot, fovy, aspect, near, far)`.
The installed CTRPF header's void overload obtains `GetCallCode()` and invokes
it through a typed function pointer, preserving float argument types. Runtime
validation of the MITM path with these VFP arguments is still needed.

## Camera findings

The camera object has vtable `0x64E308` and constructor `0x4217C0`:

| Offset | Field |
| --- | --- |
| `+0xA4` | near plane |
| `+0xA8` | far plane |
| `+0xAC` | fovy |
| `+0xB0` | aspect |

A live test setting camera aspect fields to 2.3333 produced the correct wider
21:9 view. The earlier literal patches at `0x35ABD4` / `0x310B38` had no
effect: those are unused defaults. The plugin no longer reads or patches them.
Hooking the shared builder instead covers the live camera update/blend path.

## Settings and live commands

At boot the plugin reads the SD-root `usum-onescreen.ini` (generated with
defaults if missing), then installs the hooks:

```ini
[display]
aspect = 16:9
```

Missing / `0` / `off` disables rewriting. Positive finite floats such as
`1.7778` or `2.3333`, and `16:9` / `21:9`, are accepted. Reloading settings
also applies this key. Bad INI values are logged and leave the default off.

Through the existing `usum-cmd.txt` handler (or `tools/cmd.py`):

- `aspect 16:9`, `aspect 21:9`, `aspect 1.7778`: set the active aspect.
- `aspect off` (or `aspect 0`): disable rewriting.
- `aspect`: log the active value (0 = off), the u32 count of perspective
  calls rewritten, and `culled N panes since boot`. Changing the aspect or
  turning it off does not reset these counters. Culls count hook calls, not
  unique panes over the lifetime of the game.
- `cullnames`: capture culled panes for the next frame only (the interval until
  the next plugin frame callback). Log each pane's name and global x once per
  pane address, capped at 32 lines. Names are bounded to their 24-byte field.
  The hook copies into a fixed buffer; formatting and file logging happen in
  the frame callback. Off or a frame with no culls produces no pane lines.

Set commands log `widescreen: aspect X` or `widescreen: off`. Invalid live
values leave the active setting unchanged. Live commands do not rewrite the
INI. `poke` / `pokef` remain available unchanged for data-memory experiments.

On the Azahar side, enable stretching in its configuration:

```ini
[Layout]
screen_top_stretch=true
```

Desktop Azahar 2126.2 ignores `aspect_ratio` (Android-only). Match the camera
aspect to the stretched top-screen viewport (16:9 or 21:9) to counter the
otherwise widened world.

## Launcher

The launcher's experimental Widescreen setting writes both `[display] aspect`
in the plugin INI and Azahar's `[Layout] screen_top_stretch`. Auto selects a
concrete aspect from the primary monitor; Original (5:3) turns stretch off.
The CLI also accepts `set aspect=16:9`, `21:9`, `0`, `off`, `auto` or a positive float.

21:9 looks best with a higher Azahar internal resolution scale (Emulation >
Configure > Graphics > Internal Resolution), since the 400x240 render is
stretched wider.

## Game layout templates

For active aspect A, k = A / (400/240). Setting the aspect at boot from the
INI, reloading settings, or running a live set command patches the top-screen
orthographic templates' left/right bounds to -200*k / +200*k. Off restores
-200 / +200. This makes game layouts squeeze about x=200 before Azahar stretches
the top screen. The two templates are:

| Address | Caller | Stock floats (left, right, bottom, top, near, far) |
| --- | --- | --- |
| `0x5C3180` | `0x317B84` | -200, 200, -120, 120, 1, 1000 |
| `0x5E8468` | `0x35A168` | -200, 200, -120, 120, 0.1, 800 |

Both have type 2 in the preceding word. `Process::Patch` writes exactly the
eight bytes of horizontal bounds per template, including on read-only pages.
Before either write, both templates' type and all six floats are checked by
bits: only a stock horizontal pair or the exact pair last successfully written
to that template is accepted; all other fields must be stock. A mismatch or
unreadable word logs `widescreen: 2d templates MISMATCH ...; no writes`.
Success logs `widescreen: 2d templates 0x5C3180 0x5E8468 left/right ±N`.
A failed Patch logs `PATCH FAILED` and may leave a partial update; the aspect
and native presentation still change. Successful writes are tracked per template.

Only layouts **created after the change** copy the new projection. Setting the
INI at boot covers layouts created during normal startup; a live change becomes
visible on the next newly opened screen/menu. Existing environments retain their
projection, including after switching off, until recreated.

Compared the supplied Moon `analysis/private/umoon-v1.2/code.bin` and Sun
`analysis/private/usun-v1.2-0820dec/code.bin`, subtracting base `0x100000`.
The 60 bytes at each of `0x5C317C` and `0x5E8464` are identical across games:
type, top projection, intervening word, bottom type, and bottom projection.
The top left/right words are `0xC3480000` / `0x43480000`. Near/far are
`0x3F800000` / `0x447A0000` and `0x3DCCCCCD` / `0x44480000`, respectively.
The bottom copies at `0x5C31A0` and `0x5E8488` have -160/+160 bounds and are
never patched. The runtime stock check remains required on both games.

## Off-screen game pane culling

A second CTRPF MITM hook replaces `nw::lyt::Pane::CalculateMtx` at `0x2A22EC`
(`Pane *` in r0, `const DrawInfo *` in r1, u32 dirty in r2, void return).
Startup logs `widescreen: pane CalculateMtx hook at 002A22EC -> N`
(`0` = success). It uses the same `InitializeForMitm` / `Enable` pattern as
the perspective hook and calls the typed `OriginalFunction<void>` first.
The installed CTRPF void overload calls a `GetCallCode()` function pointer
normally and returns to our hook, so post-original code is supported; the
input hooks also use post-original code. In-game validation remains required.

When `WidescreenSqueeze() > 1`, the hook reads the freshly calculated global
matrix and pane dimensions with plain loads:

```text
gx = *(float *)(pane + 0x54)  // global matrix m03
sx = *(float *)(pane + 0x48)  // m00
sy = *(float *)(pane + 0x4C)  // m01
W = width at +0x3C * abs(sx)
skip if abs(sy) > 0.01 * abs(sx) or W < 1
anchor = byte at +0x47 & 3   // horizontal base position
L = gx - W/2 (centre: 0), gx (left: 1), gx - W (right: 2)
R = L + W
inside = max(0, min(R, 200) - max(L, -200))
cull if inside < 0.3 * W
```

A pane with less than 30% of its anchored width inside the original layout
x range [-200, 200] is treated as parked off-screen. This catches battle timer
panes whose boxes extend beyond what they draw. The hook sets
only its effective alpha byte at +0x46 to zero and increments a u32 counter.
The original runs even when off and remains responsible for alpha/matrix
calculation on subsequent calls. The hot path allocates nothing and uses no
`Process::Read` or file I/O.

Rotated panes and null or zero-size panes are skipped by the guards above;
exactly 30% overlap is retained. The anchor uses only the low two bits, so
base position 9 means left-aligned. At unit scale, the four observed timer
panes have overlap/width 43/188, 0/76, 3/80, and 0/64 and are culled.
The two HP gauges, TextBox_00, Gauge_Exp, and IconBall_00 have 86/86, 86/86,
198/370, 38/38, and 10/10 and are retained (also checked in the hook comment).
This is a matrix/size heuristic, not clipping against a parent or a 3D camera.
All panes reaching this shared function are tested while squeeze > 1;
there is no screen/name filter.

Compared 0x40 bytes at `0x2A22EC - 0x100000` (file offset `0x1A22EC`) in
`analysis/private/umoon-v1.2/code.bin` and
`analysis/private/usun-v1.2-0820dec/code.bin`: all 64 bytes match.
The first ARM word is `0xE92D43F0` (`push {r4-r8,sb,lr}`).

## Native UI and panel presentation

`WidescreenSqueeze()` exposes k (1 when off). `SurfaceFromScreen` supplies it
as presentation metadata on top surfaces only. `Layer::Present` uses it for
400-pixel top output when k > 1: each destination column d samples the nearest
source column `200 + (d - 200)*k` (ties toward increasing x). Out-of-area sources
leave the destination untouched except for eligible pillarbox bands (below).
The source column's original `m_top` range preserves rounded corners. The CPU
fallback writes one memcpy per copied column on rotated screens; the GPU
path is described below. No redraw is needed for a live change. Bottom surfaces remain unchanged. Positive aspects at or below 5:3
retain native presentation at 1:1; native expansion is outside this change.

The skip rectangle remains in unsqueezed coordinates and is tested against the
same sampled source column. Map's skipped `MapWindow` is filled by
`PanelCopyBottomRect` from the hidden bottom framebuffer (`MapSource`).

**The map picture is squeezed by the game, not by the copy.** A copy that
squeezes on the GPU needs one TextureCopy per run of consecutive source
columns (Azahar keeps only copies whose strides match the framebuffer on the
GPU), and many GX commands in a frame crash the game (`docs/PERF.md`, "Map at
widescreen"). So while the map shows with k > 1, `Map.cpp` finds the Rotom
map layout's `All` pane (every tile, the player icon and the goal flag; its
origin is the map window's centre, bottom x 160) and `CalculateMtxHook` sets
its scale x to 1/k right before the game calculates its matrix (the game resets
it to 1 every frame, so it has to be applied in the hook; after the override
stops, the game puts it back by itself). The bottom screen then shows the map
squeezed about x 160, and one 1:1 TextureCopy puts the columns the squeezed
window covers (`UI::SqueezedColumns`) onto the top screen: top column d shows
bottom column d - 40, the same offset as unsqueezed. `MapWindow` is centred on
top x 200 and `MapSource` on bottom x 160, so the two line up
(`tests/map_test.cpp`). The override is cleared when the map hides or closes,
the picture is off, the pane isn't found, or k <= 1. Pane chain and offsets:
`docs/OFFSETS.md`. If the pane is not found, the copy stays one command and
the picture shows unsqueezed (wider than the rest of the game's 2D);
`map status` says `bottom pane not found`.

Audit of native top output:

- Layer presentation covers X menu, party, bag, boxes and its badge, keyboard,
  Pokédex, battle menu, move learning, map, Rotom prompts/badge, theme preview,
  and SubScreens (including options and the other native replacement screens).
  Their selection cursors are drawn into the layers and squeeze with them.
- The UI demo also uses Layer; `uidemo direct` uses the layer while k > 1 so
  its menu receives the same correction. Its direct-mode performance comparison
  is therefore meaningful only with widescreen off.
- Panel full, half, and two-thirds GPU paths squeeze their final top copy;
  source framebuffers and scratch drawing stay in original coordinates. Full
  mode converts one selected RGBA8 column at a time with DisplayTransfer;
  half/two-thirds use aligned vertical TextureCopy runs (one command per
  column: likely to hit the same crash as the map did, not yet tested). Map
  uses one TextureCopy of the game-squeezed picture (above). The CPU panel
  fallback samples the same columns. The half/fallback CPU cursor squeezes its
  bars; full/two-thirds scratch cursors squeeze with the final image. Panel
  readback checks sample the corrected destination and corresponding source.
- Left alone: the demo's full-screen dimming shade (a screen-wide effect),
  bottom framebuffer sampling/probes, screenshot capture, game 3D, and CTRPF's
  own plugin menu/notifications (external framework UI). Game 3D continues to
  use the existing perspective hook. No bottom-screen output is transformed.

Input audit: Cursor stores 320x240 bottom-screen coordinates and injects those
into the game's touch reader. Pad navigation and action taps likewise remain
in original game/bottom coordinates. `CaptureRealTouch` has no call sites;
there is no native top-screen pointer hit-test or top-to-UI touch conversion to
undo. Only the cursor's rendered top image changes. Any future top-screen
hit-testing must map destination x back through the same source-column rule.

## Native menu pillarbox art

```ini
[display]
aspect = 21:9
pillarbox = tint
```

`pillarbox` accepts `tint` (default), `pattern`, or `off`, case-insensitively.
It applies at boot and on settings reload, including menus already open.
Tint is an opaque dark olive, half the native `Theme::WindowFill` colour
(`#2B2A19`), with a three-destination-column gradient towards the menu.
It never samples or blends with the game's framebuffer. Pattern adds subtle
diagonal stripes (eight-column/row repeat, only three RGB levels brighter)
on the same base. Off leaves the game's scene visible in the side bands.

Only top-screen layers containing the standard full-screen inset
`{12, 8, 376, 224}` qualify, and only with squeeze greater than one.
This includes Party, Bag, Boxes, X menu, keyboard, Map and full-size native
pages; compact Options, battle command bars, prompts and badges do not.
The bottom screen is unchanged. The bands cover the full 240-pixel height,
but only columns outside the layer's mapped horizontal extent. Rounded
corners and the top/bottom inset inside that extent still show the game.

`PillarboxColumns` binary-searches the same `SourceColumn` mapping used by
`Layer::Present`: the menu covers `[begin, end)`, and bands cover
`[0, begin)` and `[end, 400)`. For the 376x224 inset these are `[24, 376)`
at 16:9 and `[66, 334)` at 21:9. No band overlaps a sampled layer column;
skipped Map pixels and rounded corners keep their existing behaviour.

The CPU fallback caches all twelve band phases (11.25 KiB), encoded for the
output format and vertical direction. Each rotated BGR8 band column costs
one 720-byte memcpy: 48 copies / 33.75 KiB at 16:9, or 132 copies /
92.81 KiB at 21:9 for the standard inset. Row-major host surfaces use a
pixel-copy fallback. The BGR8 GPU path instead stages these same phases as
RGBA8 and copies whole columns on the GPU; see below.

`tests/pillarbox_test.cpp` checks exact boundaries at 16:9 and 21:9,
unit squeeze, off, compact areas, bottom surfaces, all five pixel formats,
both column directions and row-major storage, corner/skip preservation and
guard bytes. It also renders the actual native X menu at 21:9 to
`analysis/private/pillarbox-tint.ppm` and `pillarbox-pattern.ppm`; the matching
`*-stretched.ppm` files resample the destination frame to 560x240.

## Overlays and upscaling

In Azahar, a CPU write to the game's framebuffer or an OSD callback returning
true drops the whole frame from the internal resolution (e.g. 5x) to 1x
400x240. The battle-menu prototype at `fff0411` was verified to stay sharp
using GPU transfers and reporting false from the callback.

`LayerGpu` copies game columns to RGBA8 background scratch, stages layer
pixels in CPU-only memory, then transfers them through a GPU twin seeded
from that background. TextureCopy inserts the layer's rectangle before
converting back to BGR8 and copying whole columns to the screen. Corners are
square on this path (the layer's black shows outside its rounded shape):
rounded corners need a copy per corner column, and with ~15 GX commands a
present the game's Party screen crashed (PERF.md). Pillarbox band
columns are written into the same stage by the CPU fallback's pixel helper
and copied with the layer, one copy per side (one copy per column crashed
the game; see PERF.md). With bands active the transfers cover all 400
columns. Pattern selects phase `x & 7`, tint phase 8, and edge phases 9-11.

Skip rectangles split runs before alignment, using the unsqueezed source
column. Map copies its game picture after the layer present: whole-column
copy-back also restores the saved background under the skipped window.
`Layer::Present` returns true only for a CPU present; callbacks propagate
that value and retain true for direct CPU drawing. Panel readback checks
and requested screenshots also retain true reporting. Non-BGR8 layouts,
linear-memory failure and `layergpu off` keep the existing CPU fallback.
The overlays themselves stay 1x pictures that Azahar scales up, so they look
soft at high internal resolutions (texture filters such as MMPX did not help);
only drawing them with the 3DS GPU would make them sharp.

The native map used to crash the game at 16:9/21:9 (one GPU copy per squeezed
column); it now copies a game-squeezed picture in one command (above).

## Verification and open work

After building, confirm hook result 0 in `usum-onescreen.log`. Try `aspect 21:9`
then query `aspect` twice while the game renders: the rewrite count should grow
and the world should widen. Try `aspect off`: the view should return to stock
and the count should stop growing. Check bottom-screen 4:3 views, scene changes,
camera blends, and battles; also check boot and reload with the INI key.

Confirm both perspective and CalculateMtx hook results are 0. With widescreen
active, inspect the battle timer and other panes parked near the edges; query
`aspect` to see the cull count grow and run `cullnames` to inspect up to 32
unique culled panes from one frame. Check the log stops adding pane lines
after that frame, until another `cullnames` command. With `aspect off` or
squeeze <= 1, the cull count should stop growing. Reopen layouts after aspect
changes and check visible edge panes and alpha restoration on moving panes.

Also check INI boot at 16:9 and 21:9, live changes followed by reopening game
menus, and off followed by reopening. Expected half-widths are approximately
213.3333, 280, and 200. Verify top bounds change while adjacent bottom data
stays stock. Inspect every native menu, rounded corners, badges, and Map's
picture edges; test panel full/half/two-thirds, all corners, and cursor edges.
Check that changing aspect while a native menu is already open takes effect
immediately, and that the bottom screen and injected touch targets stay correct.

The expanded `tests/ui_host_test.cpp` checks identity and squeezed Layer copies
against a stock reference, including rounded corners, skip edges, both rotated
and row-major layouts, three formats, 16:9/21:9 and extreme factors, sentinel
clipping, and unchanged 320-pixel bottom surfaces. It passed using MSVC C++17
on the host; no plugin build or emulator was run for this change.

GPU runtime correctness and transfer-call overhead remain unverified for the
panel. Widescreen panel copies submit up to one transfer per copied destination
column, which crashed the map (fixed there, see above); measure with the
existing perf commands. Full-mode one-column DisplayTransfer
and scene reloads especially need Azahar validation given the existing sprite
transfer caveat in Panel.cpp. CPU fallback/cursor and periodic panel readback
retain their existing framebuffer costs. Existing game layout environments may
need closing/reopening after a live change; templates do not rewrite them.
Pane culling and one-frame debug capture remain to be validated in-game.
The aspect tolerance identifies top-screen calls by value rather
than camera identity; any other camera using that same aspect will also match.
