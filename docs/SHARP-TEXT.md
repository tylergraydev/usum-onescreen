# Sharp text

## Code research (before implementation)

- `Sources/UI/Device.cpp::UiInitFont` reads the shared runtime CFNT's TGLP
  cell dimensions, baseline and FINF line feed; it does not select another font
  size. `docs/UI.md` records the measured Azahar font: **24 x 30** cells,
  baseline 25, line feed 30. These are observations, not constants guaranteed
  on every console. `uifont` reports the actual runtime values.
- `Sources/UI/Text.cpp` owns two 256-slot `GlyphCache`s: System at 0.6 and
  Large at 0.8 of a CFNT cell (18/24 pixel lines for the observed font).
  `SetSource` allocates ceil(cell width * scale) * ceil(cell height * scale)
  bytes per slot. `Get` decodes A4 Morton tiles, searches the glyph's own
  columns for ink, crops horizontally and area-filters into that cache.
  Advance is rounded (ink width + 3) * cache scale; whitespace uses CFNT metrics.
- `DrawText` measures, wraps, aligns and advances in logical coordinates.
  At canvas scale 1 it draws the cached mask directly. Otherwise it calls
  `GlyphCache::Draw`, which decodes the original CFNT cell again and sends
  that mask to `Canvas::DrawMaskSized`. The latter samples once per physical
  canvas pixel with bilinear interpolation. Canvas scaling thus reaches the
  original cell, but cannot create new font detail: 0.6 * 2.5 = 1.5 (System)
  and 0.8 * 2.5 = 2 (Large) physical pixels per original source pixel.
  System reaches that limit at 1/0.6 = 1.67x, Large at 1.25x.

## Approach

Use a build-time conversion of an openly licensed outline font, keeping the
runtime CFNT as the sole authority for layout and ink placement. There is no
larger runtime source established by the code: the shared font exposes one
TGLP cell size, and assuming an undocumented game font address would add
version and asset-lifetime dependencies. No Nintendo/game assets are included.

### Source and licence

The source is **Roboto Regular 2.137 (2017)**, Copyright 2011 Google Inc.,
under **Apache-2.0**, compatible with the project's GPL-3.0. The unchanged
outline is vendored at `tools/sharp-text/Roboto-Regular.ttf`; full terms and
attribution are in `LICENSE.txt` and `NOTICE.txt` alongside it. Include those
two texts in binary release distributions containing this font data.

- [Upstream font project](https://github.com/googlefonts/roboto-2)
- [Exact distribution used (CustomTkinter 5.2.2)](https://github.com/TomSchimansky/CustomTkinter/blob/v5.2.2/customtkinter/assets/fonts/Roboto/Roboto-Regular.ttf)
- [Upstream licence](https://github.com/googlefonts/roboto-2/blob/main/LICENSE)
- [Apache's GPLv3 compatibility explanation](https://www.apache.org/licenses/GPL-compatibility.html)

SHA-256: `319cff6e7a31f0f2a41c475dca42890aa5d19fe16017e2290f8c1d4e14f76481`.
The converter rejects other inputs. The sandbox blocked downloading a font;
this file was copied from the installed CustomTkinter 5.2.2 package and its
version, copyright and Apache licence were checked in its TrueType name table.

### Conversion and selection

`python tools/gen_sharp_font.py` generates `Sources/UI/SharpFont.inc` from
the outline at 64 px, using Pillow 11.3.0 / FreeType 2.13.3 and fonttools
4.61.1. `--check` compares with the checked-in include. These are developer
tools only: ordinary plugin builds need neither Python nor a font library,
and work offline. The source stays at **64 px**: Large is 30 * 0.8 * 2.5 =
60 physical pixels tall, so reducing resolution would give back sharpness.
The size reduction comes from the English subset and tightly packed A4,
without separate lower-resolution System/Small assets or lossy 2-bit coverage.

The explicit subset is printable ASCII U+0020..007E plus É/é (Pokémon,
Flabébé and uppercased headings), ‘/’ (English names such as Farfetch’d and
Hau’oli), “/” and … (English dialogue). Space is blank and retains the CFNT
path. There are **101 nonblank glyphs**, instead of the original 878. UI
literal searches found é; `Theme.cpp` also uppercases Latin-1. Game text can
contain the punctuation above. Other characters deliberately fall back.
The converter quantizes coverage to 4 bits **before cropping**, so all four
edges of each stored box contain nonzero ink. Two pixels share a byte,
low nibble first; only the final nibble of an odd-area glyph is padding.
The compact sorted index has 16-bit codepoints/byte offsets and 8-bit sizes.
Generation rejects offset overflow and glyphs exceeding the cache slice.

For `canvas.Scale() > 1`, `GlyphCache::Draw` decodes the generated glyph on
first use into its **existing `m_masks` allocation**, then calls
`DrawMaskSized` using the *runtime CFNT's*
horizontal ink width, vertical ink bounds and existing left bearing. Thus
baseline/ink placement stays with the console font; advances, height,
wrapping, alignment and ellipses still use the unmodified cache. Changing
the runtime font automatically changes these bounds. No guessed 24x30
constants enter the selection or placement code.

At 2.5x, System and Large cells are about 36x45 and 48x60 physical pixels;
64 px outline glyphs supply real extra detail rather than magnified CFNT
pixels. Typeface shapes differ, and fitting each glyph to the old ink box can
change its proportions; emulator comparison is needed. The canvas samples
the source once per destination pixel, retaining clipping and alpha blending.

The scale-1 cache generation and drawing branches are unchanged, including
`Text.cpp`'s direct mask draw. `hires off`, allocation fallback, and toggling
back to 1x use the same legacy mask bytes, rebuilding an evicted slot when
needed. Sub-1x canvases also retain the original CFNT path.

### Bounded memory, heap regression and redraw work

Coordinator's emulator measurement (Ultra Sun, 21:9, 5x, X menu + Party):
the original sharp font linked above 2 MiB with the warning "Only 3 MiB are
left for heap memory". Largest free block fell from **563 KB on main to
249 KB**, with LOW set. As `docs/MEMORY.md` explains, executable sections
and BSS are taken from the same fixed 3GX block as the heap: static font
data consumes the headroom that native screens need. Avoiding malloc alone
did not make the original 323,026-byte font payload acceptable.

| Added component | Bytes |
| --- | ---: |
| Read-only packed A4 coverage, 101 glyphs | 57,697 |
| Read-only sorted index, 101 records x 6 bytes | 606 |
| Four decoded-mask descriptors per existing cache, 2 x 4 x 12 | 96 |
| Extra decoded static scratch | 0 |
| **Total added static data payload** | **58,399 (57.03 KiB)** |

This removes **264,627 bytes** of font payload relative to the first version.
Generated compile-time assertions enforce record size and an 80 KiB total
budget including both caches' descriptors. ARM symbol sizes confirm the
57,697-byte data and 606-byte index and a 48-byte increase per cache instance.

This is ordinary plugin memory, plus linker alignment and new executable
code; **zero extra heap or linear/GPU allocation**. Existing cache size and
capacity do not change (69,120 System / 122,880 Large bytes for 24x30 cells).
Each cache borrows four 4,096-byte slices from the tail of that allocation,
indexed by codepoint modulo four. A sharp miss invalidates normal mask slots
overlapping its slice before decoding; a normal miss invalidates overlapping
sharp descriptors before rebuilding the original CFNT mask. Hits reuse decoded
coverage and its CFNT placement bounds without decoding either source again.
`SetSource` invalidates both kinds of entry. Missing/blank source glyphs and
unusually small caches unable to hold the four slices use the original path.
Direct-map collisions, including accents whose normal slots overlap the tail,
may cause redecodes; no extra memory is allocated to avoid those evictions.
This retains the existing single OSD rendering-thread assumption. The
168,260-byte TTF remains a build input, not linked into the plugin.

### ARM relink and 3GX size

Without WSL or make, native devkitARM compiled `Font.cpp` and `Text.cpp`
(the latter owns the changed cache instances) to `tests/build/Font-sharp.o`
and `Text-sharp.o`. Both used the Makefile's flags:

```text
-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -Os
-mword-relocations -fomit-frame-pointer -ffunction-sections -fdata-sections
-fno-strict-aliasing -Wall -D__3DS__ -fno-rtti -fno-exceptions -std=gnu++17 -I Includes
```

All 67 existing `Build/*.o` files were linked in filename order, substituting
those two new objects, with `arm-none-eabi-g++`, `-T 3gx.ld`, the architecture
flags above, `-Os -Wl,--gc-sections,--strip-discarded,--strip-debug`, library
paths `C:/devkitPro/{libctrpf,libctru,portlibs/3ds}/lib`, and `-lctrpf -lctru`.
The output is `tests/build/usum-onescreen-sharp.elf`. Then:

```powershell
& C:/devkitPro/tools/bin/3gxtool.exe -s tests/build/usum-onescreen-sharp.elf usum-onescreen.plgInfo tests/build/usum-onescreen-sharp.3gx
& C:/devkitPro/devkitARM/bin/arm-none-eabi-size.exe -A tests/build/usum-onescreen-sharp.elf
```

**3GX: 1,487,149 bytes**, versus the existing 1,741,960-byte artifact.
The ELF section-size total reported by `size -A` is 1,858,816 bytes,
264,048 below the existing 2,122,864-byte artifact. `.rodata` is 240,852
and `.bss` 696,028 bytes. The 2 MiB warning did not recur. This is an
incremental relink using the coordinator's existing objects, not a clean
full build or emulator heap measurement; loader page rounding and runtime
fragmentation determine the actual largest free block. Existing `Build/`
files and root plugin artifacts were not changed.

### Host verification

No WSL or make was used. From `tests/`, the actual compiler was:
`C:/Program Files/Unity/Hub/Editor/6000.6.6f1/Editor/Data/PlaybackEngines/WebGLSupport/BuildTools/Emscripten/llvm/clang++.exe`
(Clang 22, native `x86_64-pc-windows-msvc`, not the Emscripten driver).
These are the expanded Makefile compile lines; executable suffixes are for Windows:

```powershell
$cxx = 'C:/Program Files/Unity/Hub/Editor/6000.6.6f1/Editor/Data/PlaybackEngines/WebGLSupport/BuildTools/Emscripten/llvm/clang++.exe'
& $cxx -std=c++17 -Wall -Wextra -Werror -I ../Includes sharp_text_test.cpp ../Sources/UI/Font.cpp ../Sources/UI/Raster.cpp ../Sources/UI/Text.cpp -o build/sharp_text_test.exe
& $cxx -std=c++17 -Wall -Wextra -Werror -I ../Includes hires_test.cpp ../Sources/UI/Raster.cpp ../Sources/UI/Text.cpp ../Sources/UI/Font.cpp ../Sources/UI/Layer.cpp ../Sources/Settings.cpp ../Sources/SettingsFile.cpp -o build/hires_test.exe
& $cxx -std=c++17 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I ../Includes ui_host_test.cpp ../Sources/Settings.cpp ../Sources/SettingsFile.cpp ../Sources/UI/Raster.cpp ../Sources/UI/Text.cpp ../Sources/UI/Font.cpp ../Sources/UI/Layer.cpp ../Sources/UI/Icons.cpp ../Sources/UI/Glyphs.cpp ../Sources/UI/Demo.cpp -o build/ui_host_test.exe
./build/sharp_text_test.exe
./build/hires_test.exe
./build/ui_host_test.exe build/sharp-current.png
```

All three passed. The Windows-only `_CRT_SECURE_NO_WARNINGS` addition is
needed for the existing UI test's `fopen`: its first compilation failed on
Microsoft's deprecation annotation with `-Werror`. No warning flags were removed.

For independent 1x regression proof, `git show d7b635a:Sources/UI/Font.cpp`
was saved under ignored `tests/build/Font-baseline.cpp`; the same UI compile
line was run with that file replacing `../Sources/UI/Font.cpp` and output
`build/ui_host_baseline.exe`. Running it with `build/sharp-baseline.png`
also passed. Python compared both complete PNG byte arrays: **identical**,
288,328 bytes, SHA-256
`cf2babe0cc9515e5f3d04369f83360fb9366d8013a3e4e2defb067620be05bc2`.

`sharp_text_test` validates all 101 generated masks, quantization, tight
crop edges and output guards,
compares 2.5x output against the independent outline-mask draw and checks it
differs from the enlarged synthetic CFNT. It checks both cache sizes,
style scales 1/2, all five framebuffer formats and rotated storage, original
1x mask bytes, metrics, fallback bytes, text routing and clipping guards.
It proves warm reuse by clearing the synthetic CFNT sheet between high draws,
then checks sharp/normal overlap eviction and source changes. Overridden host
`operator new` counters verify zero allocations for fills, hits, evictions,
normal-mask recovery and same-size source switching after cache initialization.
`python tools/gen_sharp_font.py --check` and `git diff --check` also passed.

### Remaining verification and coverage limits

- This is **not sharp for every Unicode glyph**. Only the English subset
  listed above is embedded. Other accents, Greek/Cyrillic, CJK/kana, arrows,
  gender symbols and private-use console glyphs retain the
  old runtime source, without changing their codepoints or spacing. A
  separately licensed high-resolution source for these is still needed to
  satisfy the literal "everywhere" goal. Whitespace remains unchanged.
- The explicit Small 5x7 pixel font, no-system-font fallback and separate
  game-asset text renderers are unchanged; this patch targets the System/Large
  CFNT path described in item #22.
- Claude should perform a clean 3GX build and run the full host suite in WSL.
  In Azahar, inspect both sizes at 2.5x, mixed accents/descenders/punctuation,
  clipped/wrapped/ellipsized text and narrow/widescreen layouts; toggle
  `hires off`/`on` live and compare. Check missing-source glyphs remain legible.
  Review visual weight/proportions and measure dirty redraw time. Repeat
  the same X menu + Party heap measurements; confirm LOW clears and headroom
  recovers before accepting this revision.
- Console/emulator sharpness and actual runtime CFNT variations are not
  proven by synthetic host tests. No emulator
  or Computer Use was invoked, and no commit/push/branch switch was made.
