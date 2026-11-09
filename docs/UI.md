# UI layer: speed and text (UI4)

How plugin-drawn top-screen menus are drawn (`Sources/UI`, `Includes/UI`),
measured 2026-11-02 in Azahar. Short answer: draw a menu into an off-screen
**Layer** only when it changes and copy it to the framebuffer every frame
without reading the framebuffer; text comes from the console's **shared
system font**, decoded and cached by the plugin instead of CTRPF's
`DrawSysfont`. This closes open issues 1 and 2 of `docs/WAVE0-CHECKS.md`.

## How to draw a menu

```cpp
static UI::Layer layer;
static std::vector<u8> pixels;        // Layer::BytesFor(screen, area) bytes

// In the OSD callback, top screen:
UiInitFont();                          // once; cheap afterwards
const UI::Surface fb = SurfaceFromScreen(screen);
if (!layer.Matches(fb))
{
    pixels.resize(UI::Layer::BytesFor(fb, area));
    layer.Setup(fb, area, radius, pixels.data(), pixels.size());
    dirty = true;
}
if (dirty) { UI::Canvas c = layer.Begin(); DrawMyMenu(c); dirty = false; }
layer.Present(fb);
```

- `Canvas` draws in screen coordinates; a layer's canvas is clipped to its
  area and starts cleared to black.
- `Present` copies the layer's rounded rect (corner pixels less than half
  covered are skipped) and **only writes** the framebuffer. The buffer has the
  framebuffer's own format and pixel order (the 3DS stores screens rotated, a
  screen column is one run of memory), so the copy is one `memcpy` per column.
- Anything translucent over the game (a full-screen shade, a panel showing
  the game through it) has to read the framebuffer and costs host speed, see
  below. Draw it on the framebuffer canvas (`Canvas(fb)`) only if it is worth
  that.
- Fonts: `Font::System` (18 px line, baseline at 15), `Font::Large` (24 px,
  baseline at 20), `Font::Small` (5x7, 8 px). Without the system font (PC, or
  if it is missing) System and Large use the 5x7 font at 2x and 3x with the
  same line heights.

## Speed

Method as in `docs/PERF.md`: the plugin reads `svcGetSystemTick` around the
demo (`UiDemoOnScreen`) and `perf` logs the mean and max emulated time per
frame; `tools/perf.py` pairs those windows with wall-clock time. Modes:

- `off`: no menu.
- `ui:on`: the demo in a Layer: re-rendered when the highlight moves (once a
  second), copied every frame.
- `ui:shade`: `ui:on` plus a translucent full-screen shade drawn on the
  framebuffer every frame (reads it).
- `ui:direct`: the shade and the whole menu drawn on the framebuffer every
  frame (how UI1 drew it, with this branch's faster text).

Conditions as in `docs/PERF.md` (same PC, Azahar 2126.2, 5x resolution,
frame limit 250%), field inside the Route 2 Pokémon Center (start point
`pokecenter`), standing still, instance `ui4`, panel off. Modes interleaved
round by round; other Wave 2 instances running (count per run below), so
wall-clock numbers are noisy. Mean ± standard deviation over 10 s windows.

**Final build** (4 rounds × 4 windows, 3-5 Azahar processes):

| mode     |  n | speed       | fps wall   | fps emu | demo emu µs/frame (mean / max) |
|----------|---:|-------------|------------|--------:|-------------------------------:|
| off      | 16 | 1.83 ± 0.15 | 54.8 ± 4.4 | 29.92   | –                              |
| ui:on    | 16 | 1.60 ± 0.11 | 47.7 ± 3.3 | 29.92   | 981 / 8 400                    |
| ui:shade | 16 | 1.54 ± 0.11 | 45.8 ± 3.2 | 29.67   | 12 702 / 20 200                |

In `ui:on` a frame that only copies takes 0.85 ms and a layer re-render
9.0 ms on average (20 ms the first time, while the glyph cache fills); one
`perf` window: `ui: demo on, frames 425, redraws 8, mean 1027 us, max 20390
us, layer redraw mean 9008 us, copy mean 853 us`.

**Before** (UI1 as merged, same scene, 2 rounds × 3 windows, 3-7 processes):
demo on 43.8 ms emulated per frame, top fps 22.0 ± 3.0 vs 49.3 ± 4.4 off;
fps emu fell to about 15. An intermediate build (3 rounds, 5-6 processes)
had `ui:direct` (everything every frame, new text) at 27.9 ms and 19.9 fps
emu, and `ui:on` at 1.26 ms, 25 ms max: its glyph cache thrashed (50 510
misses; a hash put ASCII letters in the same slots), fixed since by indexing
by code point.

What the numbers say:

1. **The game no longer loses frames.** With the layer, fps emu stays at
   29.92 (same as off): 0.98 ms of a 33 ms frame, about 3%. Drawing directly
   took 28-44 ms per frame, so the game ran at 15-20 fps.
2. **Writing the framebuffer has its own host cost.** Speed drops 1.83 →
   1.60 (-13%) with `ui:on`, much more than its 3% of emulated CPU. Likely
   Azahar re-uploading the CPU-written part of the framebuffer each frame
   (not profiled, unverified). It only eats into Azahar's headroom: the game
   stays at full speed while speed is above 1.00. Smaller menus should cost
   less (H5's 160x120 panel drew for -1.5%, not measured here).
3. **Reading costs more.** The full-screen shade (`ui:shade`) adds 11.7 ms
   emulated per frame (fps emu 29.67, a dropped frame every few seconds) and
   another few percent of speed. In an early test, reading a single pixel
   per frame cost about as much speed as reading the whole screen (one
   sync per frame, as for the panel in `docs/PERF.md`), so avoid any read
   of the top framebuffer in menus that stay up.
4. A layer re-render (9 ms) still fits in a frame; for menus that change
   every frame (scrolling), re-render only the rows that changed.

## Text

CTRPF's `DrawSysfont` drew with cut-off descenders, wide uneven spacing and
stray dots (WAVE0 open issue 1), and needed a draw-and-read-back trick for
colour and alpha. The plugin now renders glyphs itself from the shared system
font that CTRPF already maps (libctru's `g_sharedFont`; `uifont` logs it):

- Font (Azahar, this PC): CFNT at `1BC00080`, cells 24x30, baseline row 25,
  line feed 30, 1501 A4 sheets of 128x32 (5 cells each) at `1BC02080`.
- Sheets are GPU textures: 8x8 tiles row-major, pixels in Morton order inside
  a tile, 4 bits each, low nibble first; cell (c, r) starts at
  (1 + c·25, 1 + r·31). Checked by dumping 21 sheets (`dump 1BC02080 A800`)
  and decoding them on the PC: upright, clean glyphs (dump kept in
  `analysis/private/ui4/`, not in git).
- Glyphs are cut from their cell, area-filtered down to 3/5 (System) or 4/5
  (Large) and cached (256 per size, indexed by code point, so Latin-1 never collides). The scales put the
  baseline on a whole pixel, so all glyphs share it; the whole cell height is
  kept, so descenders are never cut.
- **Spacing comes from each glyph's ink, not from the font's width table.**
  In this font `glyphWidth`/`charWidth` are 0-4 px wider than the ink, by a
  different amount per glyph (e.g. `s`: ink 8 columns, glyphWidth 12,
  charWidth 13; `l`: ink 2, 4, 7; `a`: ink 10, 13, 14), which is what made
  CTRPF's text look uneven. The renderer uses ink width + 3 font px
  (2 screen px at 3/5) per glyph; blank glyphs (space) keep the table's
  advance. Unverified whether the real console font (if a dumped one is used
  instead of Azahar's) has the same mismatch; ink spacing works either way.
- Known limit: a glyph whose ink reaches into the previous cell (this font's
  `j` hook) loses that sliver.

## Evidence

Local only (game imagery, not in git): `analysis/private/shots/ui4-*` and
`analysis/private/ui4/` (zooms, perf runs, font dump) in the
`claude/ui4-fast-ui-a523de` worktree.

## Controller badges

`Settings::glyphs` selects `GlyphFamily` for native prompts. `Theme::Button`
always names the logical 3DS input; do not swap A/B at call sites.
`Theme::ButtonName` and `Theme::FormatPromptText` in `Sources/UI/Glyphs.cpp`
share the positional mapping documented in [PLAYING.md](PLAYING.md#controller-prompt-icons).
Use the formatter only for known control hints, not arbitrary game dialogue.
`FormatPanelHint` converts only the fixed `(L+R` hint in mixed messages,
preserving user names and applying the selection at draw time.
`glyphs status` and `glyphs 3ds|xbox|playstation|nintendo` inspect/change the
active setting without saving. INI reload applies the setting too.

`Theme::DrawButtonPrompt` preserves the original 3DS badge dimensions, chip
height, label position and `PromptWidth` for every family. The 3DS branch keeps
the original game-asset/fallback renderer. Other families call
`DrawControllerBadge` in `Sources/UI/Icons.cpp`: Canvas rounded circles/pills,
lines, and rectangle outlines draw Xbox coloured letter circles, PlayStation
coloured symbols, Nintendo letter circles, and shoulder/trigger/system labels.
Even the lettering uses stroke paths, with no texture or font-mask dependency.
Canvas scales these vectors from the existing 400x240 coordinates to 2.5x.
Long system-button labels fit inside the original Start/Select badge slots.
Custom map-key chips use vector badges for single buttons and mapped text for
combos. Compact hints retain their original bounds and use mapped text; long names can be ellipsized by the usual text renderer.
D-pad hints retain direction names; there are no existing stick-button badges.

`Layer::Matches` rejects a cached layer when its glyph family changes, forcing
idle menus and badges to redraw on the next frame. Host targets that link
`Layer.cpp` or `Glyphs.cpp` must also link `Settings.cpp` and `SettingsFile.cpp`.

`tests/settings_test.cpp` covers defaults, all values, auto, case tolerance,
invalid/empty values and the warning consumed by the existing settings logger.
`tests/theme_test.cpp` checks the mapping, constant widths, 1x/2.5x rendering
with/without synthetic assets, live layer invalidation, and byte equality
against the pre-change 3DS prompt renderer at 1x. With a preview directory it
writes `glyphs-<family>-1x.ppm` and `glyphs-<family>-2.5x.ppm` for visual review;
existing screen previews should remain byte-identical with the default `3ds`.
