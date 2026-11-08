# GPU quad spike (`gpuquad`)

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
