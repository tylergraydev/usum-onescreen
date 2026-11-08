#include "Panel.hpp"

#include <cstdlib>

#include <CTRPluginFramework.hpp>

#include "Cursor.hpp"
#include "Framebuffer.hpp"
#include "Screens/BattleScreen.hpp"
#include "Settings.hpp"
#include "Shot.hpp"
#include "UI/Device.hpp"
#include "UI/ColumnMapping.hpp"
#include "UI/LayerGpu.hpp"
#include "UI/GpuQuad.hpp"
#include "Widescreen.hpp"
#include <cmath>

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // How the bottom screen gets onto the top one without costing emulator
    // speed (docs/PERF.md). In Azahar, framebuffers live on the host GPU, and
    // the CPU touching one is what costs:
    //   - a CPU read of a GPU-written buffer downloads it from the host GPU;
    //   - a CPU write of 8 bytes or less to a GPU-written buffer downloads the
    //     whole buffer too (RasterizerCache::InvalidateRegion), so even drawing
    //     a cursor or a border onto the game's frame costs a download a frame.
    // So the panel is put together by GPU transfers queued with the game's own
    // gx library (DisplayTransfer and TextureCopy), and the CPU mostly writes
    // only to scratch buffers the GPU never writes (the border color, the
    // cursor sprites, the 2/3 picture), which the transfers then copy. The
    // exception is the cursor at half size (see below).
    //
    // The game's GX queue push (0x279A2C) is lock-free and triggers the queue
    // itself, and Azahar runs queued transfers before the trigger returns, so
    // each step is done before the next one is queued.

    using DisplayTransferFn = void (*)(u32 in, u32 inWidth, u32 inHeight, u32 out, u32 outWidth,
                                       u32 outHeight, u32 flags);
    using TextureCopyFn = void (*)(u32 in, u32 out, u32 size, u32 inWidth, u32 inGap, u32 outWidth,
                                   u32 outGap, u32 flags);

    static DisplayTransferFn s_displayTransfer = nullptr;
    static TextureCopyFn s_textureCopy = nullptr;

    // Transfer engine flags (Azahar's DisplayTransferConfig).
    static constexpr u32 FlagInputLinear = 1 << 1;
    static constexpr u32 FlagTextureCopy = 1 << 3;
    static constexpr u32 FlagNoSwizzle   = 1 << 5;     // with FlagInputLinear: linear to linear
    static constexpr u32 FlagScaleXY     = 2 << 24;    // 2x2 down

    // Pixel formats (GSP and transfer engine use the same numbers). The game
    // uses BGR8 for both screens; the GPU path needs that (see GpuReady).
    static constexpr u32 Rgba8 = GSP_RGBA8_OES;
    static constexpr u32 Rgb8  = GSP_BGR8_OES;

    // Framebuffers are stored rotated: each framebuffer line is one screen
    // column (x), 240 pixels of y. TextureCopy writes runs of 16-byte units
    // at 8-byte aligned addresses into lines, which is how the panel gets into
    // a part of the top screen's lines. So every run is a multiple of 16
    // bytes: 16 BGR8 pixels, or 4 RGBA8 pixels where the cursor is placed.
    //
    // Full size: `full` is the bottom screen in RGBA8 with the cursor over
    // it, then copied (as BGR8) over whole top-screen lines.
    // Half size (box 168 x 128 with a 4 px border):
    //   full:  the bottom screen in RGBA8, without the cursor.
    //   fill:  border color, 256 x 336 RGBA8, written once by the CPU.
    //   wide:  fill, with `full` copied into its middle (8 px and 8 lines in),
    //          so the 2x2 scaled lines are 128 px (384 bytes as BGR8); 120
    //          (360 bytes) would not be whole units.
    //   out:   wide scaled 2x2 into BGR8, copied into the top screen's lines.
    // The cursor is then drawn by the CPU on the top screen, which costs one
    // download a frame (~7%, docs/PERF.md). Copying the cursor sprites into
    // any buffer of this chain (full before the gap copy, or the scaled
    // picture) made Azahar fail an assertion (GetFormatType, invalid pixel
    // format) every time the field reloaded after the Pokédex or a battle;
    // the same sprites at full size never did. Cause not found.
    // 2/3 size (box 229 x 176 with an 8 px border): transfers only scale by
    // 1/2, so the CPU scales it:
    //   plain: the bottom screen's lines, each followed by a 16-byte gap.
    //          Azahar can't do that TextureCopy on the host GPU (the gap is
    //          not a whole number of BGR8 pixels), so it does it in software:
    //          one download of the whole framebuffer a frame, into memory
    //          the GPU never writes, so the CPU's reads cost nothing more.
    //   two:   the CPU's 2/3 picture with border and cursor; copied by a
    //          DisplayTransfer into twoGpu (a TextureCopy needs a GPU-side
    //          source, or Azahar falls back to software and downloads the
    //          top screen), then into the top screen's lines.
    // Cursor sprites (full size): spriteH and spriteV in RGBA8, the
    // crosshair's two bars, copied to GPU-side twins the same way, then into
    // full.
    static constexpr u32 WideLen    = 256;
    static constexpr u32 WideLines  = BottomWidth + 16;
    static constexpr u32 WideEdge   = 8;
    static constexpr u32 HalfLen    = WideLen / 2;
    static constexpr u32 HalfLines  = WideLines / 2;
    static constexpr u32 HalfBorder = WideEdge / 2;
    static constexpr u32 PlainGap   = 16;
    static constexpr u32 PlainLine  = ScreenHeight * 3 + PlainGap;
    static constexpr u32 TwoW       = BottomWidth * 2 / 3;     // 213
    static constexpr u32 TwoH       = ScreenHeight * 2 / 3;    // 160
    static constexpr u32 TwoBorder  = 8;
    static constexpr u32 TwoLen     = TwoH + 2 * TwoBorder;    // 176: 528 bytes, whole units
    static constexpr u32 TwoLines   = TwoW + 2 * TwoBorder;    // 229

    static constexpr u32 PageAlign(u32 size) { return (size + 0xFFF) & ~0xFFFu; }

    // Scratch layout in one linear-memory block (GPU-visible), each buffer on
    // its own pages.
    static constexpr u32 OffFill    = 0;
    static constexpr u32 OffWide    = OffFill + PageAlign(WideLen * WideLines * 4);
    static constexpr u32 OffOut     = OffWide + PageAlign(WideLen * WideLines * 4);
    static constexpr u32 OffFull    = OffOut + PageAlign(HalfLen * HalfLines * 3);
    static constexpr u32 OffPlain   = OffFull + PageAlign(ScreenHeight * BottomWidth * 4);
    static constexpr u32 OffTwo     = OffPlain + PageAlign(PlainLine * BottomWidth);
    static constexpr u32 OffTwoGpu  = OffTwo + PageAlign(TwoLen * TwoLines * 3);
    static constexpr u32 OffSprites = OffTwoGpu + PageAlign(TwoLen * TwoLines * 3);
    static constexpr u32 SpriteSlot = 0x400;   // spriteH, spriteV, then their GPU twins
    static constexpr u32 ScratchSize = OffSprites + PageAlign(4 * SpriteSlot);

    static u32 s_scratch = 0;

    // The 2/3 picture is redrawn every TwoRefresh frames (the download and
    // the CPU's scaling are its cost); the cursor moves every frame, over
    // saved pixels.
    static constexpr u32 TwoRefresh = 2;
    static u32 s_twoFrames = 0;
    struct Under
    {
        u32 offset;
        u8 rgb[3];
    };
    static constexpr u32 UnderMax = 2 * 13 * 3;
    static Under s_under[UnderMax];
    static u32 s_underCount = 0;
    static bool s_fillReady = false;
    static u32 s_spriteKey = 0xFFFFFFFF;

    static const u8 BorderGray = 0x40;

    // Last bottom framebuffer the game showed (set by the OSD callback).
    static volatile u32 s_bottomFb = 0;
    static volatile u32 s_bottomStride = 0;

    static volatile PanelMode s_mode = PanelMode::Off;

    static u32 s_bottomFrames = 0;
    static u32 s_topFrames = 0;
    static u32 s_drawnFrames = 0;
    static u32 s_readbackChecks = 0;
    static u32 s_readbackOk = 0;
    static int s_bottomFormat = -1;
    static int s_topFormat = -1;
    static std::string s_gpuStatus = "gpu: not started";

    // The readback check reads the top framebuffer (and for 1/2 and full the
    // bottom one), a download each, so it only runs every this many drawn
    // frames (~4 s).
    static constexpr u32 ReadbackInterval = 128;

    // Timing for the `perf` command (off by default): emulated CPU ticks spent
    // copying the bottom screen and drawing the panel, per report window.
    struct PerfStat
    {
        u32 count;
        u64 sum;
        u64 max;

        void Add(u64 ticks)
        {
            count++;
            sum += ticks;
            if (ticks > max)
                max = ticks;
        }
    };

    static volatile bool s_perf = false;
    static PerfStat s_captureStat;
    static PerfStat s_drawStat;
    static u64 s_perfStart = 0;
    static u32 s_perfTopFrames = 0;
    static u32 s_perfBottomFrames = 0;

    // Where the bottom screen goes on the top screen: the panel's box (left,
    // top, width, height), its border, and the picture's scale num/den.
    struct Placement
    {
        u32 left, top, width, height;
        u32 border;
        u32 num, den;
    };

    static u32 LineAddress(const FbLayout &fb, u32 x, u32 y)
    {
        return (u32)(fb.origin + (s32)x * fb.stepX + (s32)y * fb.stepY);
    }

    // Address of the box's first byte in screen column x: framebuffer lines
    // run toward smaller y when stepY < 0.
    static u32 BoxLineStart(const FbLayout &fb, const Placement &p, u32 x)
    {
        return LineAddress(fb, x, fb.stepY < 0 ? p.top + p.height - 1 : p.top);
    }

    // Puts the box 4 px from the corner chosen in settings. TextureCopy
    // addresses are in 8-byte units, so the box's first byte in each line must
    // be 8-byte aligned: it moves toward the middle of the screen until it is.
    static void PlaceInCorner(const FbLayout &fb, Placement &p)
    {
        const PanelCorner corner = GetSettings().panelCorner;
        const bool right = corner == PanelCorner::BottomRight || corner == PanelCorner::TopRight;
        const bool bottom = corner == PanelCorner::BottomRight || corner == PanelCorner::BottomLeft;
        p.left = right ? TopWidth - p.width - 4 : 4;
        p.top = bottom ? ScreenHeight - p.height - 4 : 4;
        for (u32 i = 0; i < 8 && BoxLineStart(fb, p, p.left) % 8 != 0; i++)
            p.top = bottom ? p.top - 1 : p.top + 1;
    }

    static Placement HalfPlacement(const FbLayout &fb)
    {
        Placement p = { 0, 0, HalfLines, HalfLen, HalfBorder, 1, 2 };
        PlaceInCorner(fb, p);
        return p;
    }

    static Placement TwoThirdsPlacement(const FbLayout &fb)
    {
        Placement p = { 0, 0, TwoLines, TwoLen, TwoBorder, 2, 3 };
        PlaceInCorner(fb, p);
        return p;
    }

    static Placement FullPlacement(void)
    {
        return { (TopWidth - BottomWidth) / 2, 0, BottomWidth, ScreenHeight, 0, 1, 1 };
    }

    static u32 Flags(u32 inFormat, u32 outFormat, u32 extra = 0)
    {
        return FlagInputLinear | FlagNoSwizzle | (inFormat << 8) | (outFormat << 12) | extra;
    }

    // Linear-to-linear DisplayTransfer of `lines` lines of `len` pixels (the
    // output is half of each with FlagScaleXY).
    static void Transfer(u32 in, u32 out, u32 len, u32 lines, u32 flags)
    {
        s_displayTransfer(in, len, lines, out, len, lines, flags);
    }

    // Copies a CPU-written buffer (BGR8 or RGBA8, `len` x `lines`) to a
    // GPU-side twin that a TextureCopy can then read. Azahar gives a new
    // surface its source's resolution scale, and a TextureCopy between
    // surfaces of different scales falls back to software (a download). A
    // CPU-written buffer is uploaded at 1x while the game's surfaces are at
    // the emulator's internal resolution, so the twin is first made from a
    // corner of `scaled` (a GPU-written buffer at the game's scale, lines of
    // `scaledLen` px in `scaledFormat`), then overwritten with the CPU's
    // buffer, which keeps it at that scale.
    static void ToGpu(u32 cpu, u32 gpu, u32 len, u32 lines, u32 format,
                      u32 scaled, u32 scaledLen, u32 scaledFormat)
    {
        s_displayTransfer(scaled, scaledLen, lines, gpu, len, lines, Flags(scaledFormat, format));
        Transfer(cpu, gpu, len, lines, Flags(format, format));
    }

    // TextureCopy of `runs` runs of `run` bytes: contiguous at `in`, at
    // `outStride` apart from `out`.
    static void CopyRuns(u32 in, u32 out, u32 run, u32 runs, u32 outStride)
    {
        s_textureCopy(in, out, run * runs, run / 16, 0, run / 16, (outStride - run) / 16, FlagTextureCopy);
    }

    // Final top-screen copy only: source columns and cursor sprites remain in
    // unsqueezed coordinates. One aligned vertical run per destination column
    // handles arbitrary aspects without reading either framebuffer on the CPU.
    static void CopyTopColumns(u32 in, u32 inStride, u32 run, u32 columns,
                               const FbLayout &fb, const Placement &p)
    {
        const float squeeze = WidescreenSqueeze();
        if (!(squeeze > 1.0f))
        {
            s_textureCopy(in, BoxLineStart(fb, p, p.left), run * columns,
                          run / 16, (inStride - run) / 16,
                          run / 16, ((u32)fb.stepX - run) / 16, FlagTextureCopy);
            return;
        }
        for (u32 dx = 0; dx < TopWidth; dx++)
        {
            const s32 sx = UI::SourceColumn(dx, squeeze) - (s32)p.left;
            if (sx < 0 || sx >= (s32)columns)
                continue;
            CopyRuns(in + sx * inStride, BoxLineStart(fb, p, dx), run, 1, fb.stepX);
        }
    }

    // Allocates the scratch block once. False (the panel then reads and
    // draws with the CPU, the slow way) if the game's framebuffers aren't
    // BGR8 or linear memory is short.
    static bool GpuReady(const Screen &top)
    {
        if (s_displayTransfer == nullptr || s_textureCopy == nullptr)
            return false;
        if ((top.Format & 7) != Rgb8 || (s_bottomFormat & 7) != (int)Rgb8
            || top.Stride != ScreenHeight * 3 || s_bottomStride != ScreenHeight * 3)
        {
            s_gpuStatus = "gpu: framebuffers not BGR8, drawing with the CPU";
            return false;
        }
        if (s_scratch != 0)
            return true;
        static bool failed = false;
        if (failed)
            return false;

        u32 addr = 0;
        const Result res = svcControlMemory(&addr, 0, 0, ScratchSize, MEMOP_ALLOC_LINEAR,
                                            (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
        if (R_FAILED(res) || addr == 0)
        {
            failed = true;
            s_gpuStatus = Utils::Format("gpu: no linear memory (0x%08lX), drawing with the CPU", (u32)res);
            return false;
        }
        s_scratch = addr;
        s_gpuStatus = Utils::Format("gpu: scratch %08lX (+%lX)", addr, ScratchSize);
        return true;
    }

    static void PutRgb(u8 *p, u8 r, u8 g, u8 b)   // BGR8
    {
        p[0] = b; p[1] = g; p[2] = r;
    }

    static void PutRgba(u8 *p, u8 v, bool red)    // RGBA8, gray or red
    {
        p[0] = 0xFF; p[1] = red ? 0 : v; p[2] = red ? 0 : v; p[3] = red ? 0xFF : v;
    }

    // The crosshair's two bars, for a cursor centered on line x, pixel j:
    // white arms of 5 px each way with a 1 px black outline. spriteH holds
    // the horizontal arm's 13 lines, each a run of 4 px from the even pixel
    // (j - 1) & ~1; spriteV the vertical arm's 3 lines, runs of 16 px from
    // (j - 7) & ~1. The runs' spare pixels are black. So the sprites depend
    // only on j's parity and the touch state (white, red while touching).
    static constexpr u32 SpriteHLines = 13, SpriteHLen = 4;
    static constexpr u32 SpriteVLines = 3,  SpriteVLen = 16;

    static u32 SpriteHStart(u32 j) { return (j - 1) & ~1u; }
    static u32 SpriteVStart(u32 j) { return (j - 7) & ~1u; }

    static void PrepareSprites(u32 j, bool touching)
    {
        const u32 key = (j & 1) | (touching ? 2 : 0);
        if (key == s_spriteKey)
            return;
        s_spriteKey = key;

        // Pixel (line dx, pixel dj) relative to the cursor's center.
        const auto white = [](s32 dx, s32 dj) {
            return (dj == 0 && std::abs(dx) <= 5) || (dx == 0 && std::abs(dj) <= 5);
        };
        u8 *h = (u8 *)(s_scratch + OffSprites);
        u8 *v = h + SpriteSlot;
        const s32 hj = (s32)SpriteHStart(j) - (s32)j, vj = (s32)SpriteVStart(j) - (s32)j;
        for (u32 line = 0; line < SpriteHLines; line++)
            for (u32 i = 0; i < SpriteHLen; i++)
            {
                const bool lit = white((s32)line - 6, hj + (s32)i);
                PutRgba(h + (line * SpriteHLen + i) * 4, lit ? 0xFF : 0, lit && touching);
            }
        for (u32 line = 0; line < SpriteVLines; line++)
            for (u32 i = 0; i < SpriteVLen; i++)
            {
                const bool lit = white((s32)line - 1, vj + (s32)i);
                PutRgba(v + (line * SpriteVLen + i) * 4, lit ? 0xFF : 0, lit && touching);
            }
    }

    // Copies the cursor sprites into an RGBA8 buffer of `lines` lines of
    // `len` px, centered on line x, pixel j (kept far enough from the edges
    // for both sprites to fit).
    static void GpuCursor(u32 buf, u32 len, u32 lines, u32 x, u32 j, bool touching)
    {
        x = x < 6 ? 6 : (x > lines - 7 ? lines - 7 : x);
        j = j < 8 ? 8 : (j > len - 10 ? len - 10 : j);
        PrepareSprites(j, touching);

        const u32 h = s_scratch + OffSprites, v = h + SpriteSlot;
        const u32 hGpu = v + SpriteSlot, vGpu = hGpu + SpriteSlot;
        ToGpu(h, hGpu, SpriteHLen, SpriteHLines, Rgba8, buf, len, Rgba8);
        ToGpu(v, vGpu, SpriteVLen, SpriteVLines, Rgba8, buf, len, Rgba8);
        CopyRuns(hGpu, buf + ((x - 6) * len + SpriteHStart(j)) * 4, SpriteHLen * 4, SpriteHLines, len * 4);
        CopyRuns(vGpu, buf + ((x - 1) * len + SpriteVStart(j)) * 4, SpriteVLen * 4, SpriteVLines, len * 4);
    }

    // Position of bottom-screen pixel row y (0..h-1) within a framebuffer-style
    // line of a picture h px high, as stored in memory.
    static u32 LineIndex(const FbLayout &fb, u32 y, u32 h)
    {
        return fb.stepY < 0 ? h - 1 - y : y;
    }

    // Cursor drawn by the CPU straight onto the top screen (its center at
    // x, y): costs a download of the top framebuffer a frame (see above).
    static void CpuCursor(const Screen &screen, u32 x, u32 y, bool touching)
    {
        const float squeeze = WidescreenSqueeze();
        const auto rect = [&](s32 left, s32 top, s32 width, s32 height, Color color) {
            if (!(squeeze > 1.0f))
            {
                screen.DrawRect(left, top, width, height, color);
                return;
            }
            for (s32 dx = 0; dx < (s32)TopWidth; dx++)
            {
                const s32 sx = UI::SourceColumn(dx, squeeze);
                if (sx >= left && sx < left + width)
                    screen.DrawRect(dx, top, 1, height, color);
            }
        };
        rect((s32)x - 6, (s32)y - 1, 13, 3, Color::Black);
        rect((s32)x - 1, (s32)y - 6, 3, 13, Color::Black);
        rect((s32)x - 5, y, 11, 1, touching ? Color::Red : Color::White);
        rect(x, (s32)y - 5, 1, 11, touching ? Color::Red : Color::White);
    }

    static void GpuFull(const Screen &screen, const Placement &p)
    {
        const FbLayout fb = GetLayout(screen);
        const u32 full = s_scratch + OffFull;
        u32 cx, cy;
        const bool touching = CursorGet(cx, cy);
        Transfer(s_bottomFb, full, ScreenHeight, BottomWidth, Flags(Rgb8, Rgba8));
        GpuCursor(full, ScreenHeight, BottomWidth, cx, LineIndex(fb, cy, ScreenHeight), touching);
        const float squeeze = WidescreenSqueeze();
        if (!(squeeze > 1.0f))
            Transfer(full, BoxLineStart(fb, p, p.left), ScreenHeight, BottomWidth, Flags(Rgba8, Rgb8));
        else
            for (u32 dx = 0; dx < TopWidth; dx++)
            {
                const s32 sx = UI::SourceColumn(dx, squeeze) - (s32)p.left;
                if (sx >= 0 && sx < (s32)BottomWidth)
                    Transfer(full + sx * ScreenHeight * 4, BoxLineStart(fb, p, dx),
                             ScreenHeight, 1, Flags(Rgba8, Rgb8));
            }
    }

    static void GpuHalf(const Screen &screen, const Placement &p)
    {
        const FbLayout fb = GetLayout(screen);
        const u32 fill = s_scratch + OffFill, wide = s_scratch + OffWide, out = s_scratch + OffOut;
        const u32 full = s_scratch + OffFull;

        if (!s_fillReady)
        {
            u8 *f = (u8 *)fill;
            for (u32 i = 0; i < WideLen * WideLines; i++, f += 4)
                PutRgba(f, BorderGray, false);
            s_fillReady = true;
        }

        Transfer(s_bottomFb, full, ScreenHeight, BottomWidth, Flags(Rgb8, Rgba8));
        // Border color everywhere, then the picture into the middle.
        Transfer(fill, wide, WideLen, WideLines, Flags(Rgba8, Rgba8));
        CopyRuns(full, wide + (WideEdge * WideLen + WideEdge) * 4, ScreenHeight * 4, BottomWidth,
                 WideLen * 4);
        Transfer(wide, out, WideLen, WideLines, Flags(Rgba8, Rgb8, FlagScaleXY));
        CopyTopColumns(out, HalfLen * 3, HalfLen * 3, HalfLines, fb, p);

        u32 cx, cy;
        const bool touching = CursorGet(cx, cy);
        CpuCursor(screen, p.left + p.border + cx / 2, p.top + p.border + cy / 2, touching);
    }

    // A readable view of a bottom-screen frame: pixel (x, y) is at
    // origin + x * stride + y * stepY, in `format`.
    struct Source
    {
        const u8 *origin;
        u32 stride;
        s32 stepY;
        GSPFormat format;

        void Pixel(u32 x, u32 y, u8 &r, u8 &g, u8 &b) const
        {
            DecodePixel(origin + x * stride + (s32)y * stepY, format, r, g, b);
        }
    };

    // The bottom framebuffer itself; reading it costs downloads in Azahar.
    // Its lines run the same way as the top screen's.
    static Source FramebufferSource(const FbLayout &top)
    {
        const u32 format = s_bottomFormat & 7;
        const u32 bpp = format == Rgba8 ? 4 : format == Rgb8 ? 3 : 2;
        const bool down = top.stepY < 0;
        return { (const u8 *)s_bottomFb + (down ? (ScreenHeight - 1) * bpp : 0), s_bottomStride,
                 down ? -(s32)bpp : (s32)bpp, (GSPFormat)format };
    }

    // 2/3: the bottom screen into `plain` (one download, see above), then
    // scaled by the CPU into `two`, inside the border. Every 3 x 3 source
    // block gives 2 x 2 pixels, each weighing its source pixels by how much
    // of them it covers (2/3 and 1/3 along each axis), which keeps text
    // legible.
    static void RefreshTwoThirds(u8 *base, u32 plain)
    {
        CopyRuns(s_bottomFb, plain, ScreenHeight * 3, BottomWidth, PlainLine);
        for (u32 x = 0; x < TwoLines; x++)
        {
            u8 *l = base + x * TwoLen * 3;
            const bool edge = x < TwoBorder || x >= TwoLines - TwoBorder;
            for (u32 i = 0; i < TwoLen; i = (edge || i != TwoBorder - 1) ? i + 1 : TwoLen - TwoBorder)
                PutRgb(l + i * 3, BorderGray, BorderGray, BorderGray);
        }
        // Works in memory order (lines of 240 bytes-triplets): 240 and 160
        // are multiples of 3 and 2, so the blocks line up the same whichever
        // way the lines run on screen. 320 lines are 106 blocks of 3 plus 2,
        // which give the last output line (2/3 and 1/3 again).
        const u8 *in = (const u8 *)plain;
        u8 *out = base + (TwoBorder * TwoLen + TwoBorder) * 3;
        for (u32 x = 0; x < BottomWidth; x += 3, in += 3 * PlainLine, out += 2 * TwoLen * 3)
        {
            const bool last = x + 3 > BottomWidth;     // only 2 lines left: one output line
            const u8 *l0 = in, *l1 = in + PlainLine, *l2 = last ? l1 : l1 + PlainLine;
            u8 *o0 = out, *o1 = out + TwoLen * 3;
            for (u32 j = 0; j < ScreenHeight * 3; j += 9, l0 += 9, l1 += 9, l2 += 9, o0 += 6, o1 += 6)
                for (u32 c = 0; c < 3; c++)
                {
                    // Along each line: 2 p0 + p1 and p1 + 2 p2 (x3).
                    const u32 a0 = 2 * l0[c] + l0[c + 3], b0 = l0[c + 3] + 2 * l0[c + 6];
                    const u32 a1 = 2 * l1[c] + l1[c + 3], b1 = l1[c + 3] + 2 * l1[c + 6];
                    // Across lines: 2 L0 + L1, then L1 + 2 L2 (x3 again).
                    o0[c]     = (u8)((2 * a0 + a1 + 4) / 9);
                    o0[c + 3] = (u8)((2 * b0 + b1 + 4) / 9);
                    if (!last)
                    {
                        const u32 a2 = 2 * l2[c] + l2[c + 3], b2 = l2[c + 3] + 2 * l2[c + 6];
                        o1[c]     = (u8)((a1 + 2 * a2 + 4) / 9);
                        o1[c + 3] = (u8)((b1 + 2 * b2 + 4) / 9);
                    }
                }
        }
    }

    // 2/3 on the top screen: the picture (redrawn every TwoRefresh frames)
    // with the cursor over it, copied on the GPU.
    static void GpuTwoThirds(const Screen &screen, const Placement &p)
    {
        const FbLayout fb = GetLayout(screen);
        const u32 plain = s_scratch + OffPlain;
        const u32 two = s_scratch + OffTwo, twoGpu = s_scratch + OffTwoGpu;
        u8 *base = (u8 *)two;
        if (s_twoFrames++ % TwoRefresh == 0)
        {
            s_underCount = 0;
            RefreshTwoThirds(base, plain);
        }
        else
        {
            // Take the last frame's cursor off again.
            for (u32 i = s_underCount; i-- > 0;)
                PutRgb(base + s_under[i].offset, s_under[i].rgb[2], s_under[i].rgb[1], s_under[i].rgb[0]);
            s_underCount = 0;
        }

        // Cursor: arms of 5 px, black outline; what it covers is saved.
        u32 cx, cy;
        const bool touching = CursorGet(cx, cy);
        const s32 x = TwoBorder + cx * 2 / 3, j = TwoBorder + LineIndex(fb, cy * 2 / 3, TwoH);
        const auto put = [&](s32 px, s32 pj, u8 r, u8 g, u8 b) {
            if (px < 0 || px >= (s32)TwoLines || pj < 0 || pj >= (s32)TwoLen || s_underCount >= UnderMax)
                return;
            Under &u = s_under[s_underCount++];
            u.offset = (px * TwoLen + pj) * 3;
            u.rgb[0] = base[u.offset]; u.rgb[1] = base[u.offset + 1]; u.rgb[2] = base[u.offset + 2];
            PutRgb(base + u.offset, r, g, b);
        };
        for (s32 d = -6; d <= 6; d++)
            for (s32 t = -1; t <= 1; t++)
            {
                const bool lit = t == 0 && d >= -5 && d <= 5;
                const u8 v = lit ? 0xFF : 0, g = lit && touching ? 0 : v;
                put(x + d, j + t, v, g, g);
                put(x + t, j + d, v, g, g);
            }

        ToGpu(two, twoGpu, TwoLen, TwoLines, Rgb8, s_bottomFb, ScreenHeight, Rgb8);
        CopyTopColumns(twoGpu, TwoLen * 3, TwoLen * 3, TwoLines, fb, p);
    }

    // Fallback without the GPU path: reads the framebuffer pixel by pixel
    // every frame and draws with the CPU (what the panel did before), at 1/2
    // for both panel sizes. Costs about 40% of Azahar's speed.
    static void CpuCopy(const Screen &screen, const Placement &p)
    {
        const FbLayout fb = GetLayout(screen);
        const Source src = FramebufferSource(fb);
        const u32 den = p.den == 1 ? 1 : 2;
        const u32 h = ScreenHeight / den, w = BottomWidth / den;
        const float squeeze = WidescreenSqueeze();
        for (u32 y = 0; y < h; y++)
        {
            for (u32 dx = 0; dx < TopWidth; dx++)
            {
                const s32 x = UI::SourceColumn(dx, squeeze) - (s32)(p.left + p.border);
                if (x < 0 || x >= (s32)w)
                    continue;
                u8 r, g, b;
                src.Pixel(x * den, y * den, r, g, b);
                u8 *out = fb.origin + (s32)(p.top + p.border + y) * fb.stepY + (s32)dx * fb.stepX;
                EncodePixel(out, fb.format, r, g, b);
            }
        }
        u32 cx, cy;
        const bool touching = CursorGet(cx, cy);
        CpuCursor(screen, p.left + p.border + cx / den, p.top + p.border + cy / den, touching);
    }

    // Every ReadbackInterval drawn frames, reads the top-screen pixel a
    // quarter into the picture (away from the cursor's start in the middle)
    // and checks it against what it was made from: the bottom-screen pixels
    // for 1/2 and full, the CPU's picture for 2/3, within scaling/filtering
    // error. So the log shows the panel is really on screen without anyone
    // watching.
    static void CheckReadback(const Screen &screen, const Placement &p, bool gpu)
    {
        const FbLayout fb = GetLayout(screen);
        const u32 den = gpu ? p.den : (p.den == 1 ? 1 : 2), num = gpu ? p.num : 1;
        const u32 w = BottomWidth * num / den, h = ScreenHeight * num / den;
        const float squeeze = WidescreenSqueeze();
        const s32 sourceX = p.left + p.border + w / 4;
        const s32 destinationX = squeeze > 1.0f
            ? (s32)std::floor(200.0f + (sourceX - 200) / squeeze + 0.5f) : sourceX;
        const s32 sampledX = UI::SourceColumn(destinationX, squeeze) - (s32)(p.left + p.border);
        if (sampledX < 0 || sampledX >= (s32)w)
            return; // Extremely narrow output may have no sample inside this picture.
        const u32 qx = sampledX, qy = h / 4;
        u8 r, g, b;
        DecodePixel(fb.origin + (s32)(p.top + p.border + qy) * fb.stepY + destinationX * fb.stepX,
                    fb.format, r, g, b);

        u32 er = 0, eg = 0, eb = 0;
        if (num == 2)
        {
            const u8 *s = (const u8 *)(s_scratch + OffTwo)
                        + ((TwoBorder + qx) * TwoLen + TwoBorder + LineIndex(fb, qy, TwoH)) * 3;
            er = s[2]; eg = s[1]; eb = s[0];
        }
        else
        {
            // Average of the den x den source block (the CPU fallback samples
            // its top-left pixel, which is within tolerance on flat areas).
            const Source src = FramebufferSource(fb);
            for (u32 dy = 0; dy < den; dy++)
                for (u32 dx = 0; dx < den; dx++)
                {
                    u8 ir, ig, ib;
                    src.Pixel(qx * den + dx, qy * den + dy, ir, ig, ib);
                    er += ir; eg += ig; eb += ib;
                }
            er /= den * den; eg /= den * den; eb /= den * den;
        }
        const s32 tolerance = 24;
        s_readbackChecks++;
        if (std::abs((s32)r - (s32)er) <= tolerance && std::abs((s32)g - (s32)eg) <= tolerance
            && std::abs((s32)b - (s32)eb) <= tolerance)
            s_readbackOk++;
    }

    static bool DrawPanel(const Screen &screen)
    {
        if (!screen.IsTop)
        {
            s_bottomFrames++;
            s_bottomFormat = screen.Format;
            s_bottomStride = screen.Stride;
            s_bottomFb = screen.LeftFramebuffer;
            return false;
        }

        s_topFrames++;
        s_topFormat = screen.Format;

        if (s_mode == PanelMode::Off || s_bottomFb == 0)
            return false;

        const u64 start = s_perf ? svcGetSystemTick() : 0;
        const FbLayout fb = GetLayout(screen);
        const bool twoThirds = s_mode == PanelMode::Panel && GetSettings().panelSize == PanelSize::TwoThirds;
        const bool gpu = GpuReady(screen);
        const Placement p = s_mode == PanelMode::Full ? FullPlacement()
                          : twoThirds && gpu ? TwoThirdsPlacement(fb) : HalfPlacement(fb);
        if (!gpu)
            CpuCopy(screen, p);
        else if (s_mode == PanelMode::Full)
            GpuFull(screen, p);
        else if (twoThirds)
            GpuTwoThirds(screen, p);
        else
            GpuHalf(screen, p);
        const u64 copied = s_perf ? svcGetSystemTick() : 0;
        if (s_perf)
            s_captureStat.Add(copied - start);

        const bool readback = s_drawnFrames % ReadbackInterval == 0;
        if (readback)
            CheckReadback(screen, p, gpu);

        if (s_perf)
            s_drawStat.Add(svcGetSystemTick() - copied);
        s_drawnFrames++;
        // True when the CPU wrote to the top screen (CTRPF then flushes it):
        // the fallback, and the half-size cursor. Keep the
        // readback diagnostic on CTRPF's flush path too.
        return !gpu || (s_mode == PanelMode::Panel && !twoThirds) || readback;
    }

    bool PanelCopyBottomRect(const Screen &top, u32 x, u32 y, u32 w, u32 h, u32 dx, u32 dy)
    {
        if (!top.IsTop || s_bottomFb == 0 || w == 0 || x + w > BottomWidth || dx + w > TopWidth
            || y + h > ScreenHeight || dy + h > ScreenHeight || !GpuReady(top))
            return false;
        const FbLayout fb = GetLayout(top);
        const u32 run = h * 3;
        // The run starts at the rect's lowest pixel in memory: its bottom row
        // when lines run toward smaller y.
        const u32 first = fb.stepY < 0 ? ScreenHeight - (y + h) : y;
        const UI::ColumnRange columns = UI::SqueezedColumns(dx, dx + w, WidescreenSqueeze());
        const s32 sourceX = (s32)x + columns.begin - (s32)dx;
        const u32 count = columns.end - columns.begin;
        if (count == 0 || sourceX < (s32)x || sourceX < 0
            || sourceX + (s32)count > (s32)(x + w)
            || sourceX + (s32)count > (s32)BottomWidth)
            return false;
        const u32 in = s_bottomFb + sourceX * s_bottomStride + first * 3;
        const u32 out = BoxLineStart(fb, { dx, dy, w, h, 0, 1, 1 }, columns.begin);
        if (run % 16 != 0 || in % 8 != 0 || out % 8 != 0 || (u32)fb.stepX != s_bottomStride)
            return false;
        // The game already squeezed the picture; one GX command avoids queue overload.
        s_textureCopy(in, out, run * count, run / 16, (s_bottomStride - run) / 16,
                      run / 16, ((u32)fb.stepX - run) / 16, FlagTextureCopy);
        return true;
    }

    static bool PanelCallback(const Screen &screen)
    {
        bool drew = DrawPanel(screen);
        // Drawn after the panel so test menus sit on top, and before the
        // screenshot hook so shots include them.
        drew |= UiDemoOnScreen(screen);
        drew |= BattleScreenOnScreen(screen);
        drew |= UI::GpuQuadOnScreen(screen);   // always false: GPU only
        drew |= ShotOnScreen(screen);
        return drew;
    }

    void StartPanel(PanelMode initial, u32 displayTransfer, u32 textureCopy)
    {
        s_displayTransfer = (DisplayTransferFn)displayTransfer;
        s_textureCopy = (TextureCopyFn)textureCopy;
        UI::StartLayerGpu(displayTransfer, textureCopy);
        s_mode = initial;
        OSD::Run(PanelCallback);
    }

    PanelMode GetPanelMode(void)
    {
        return s_mode;
    }

    void SetPanelMode(PanelMode mode)
    {
        s_mode = mode;
        s_twoFrames = 0;
    }

    void CyclePanelMode(void)
    {
        switch (s_mode)
        {
        case PanelMode::Off:   SetPanelMode(PanelMode::Panel); break;
        case PanelMode::Panel: SetPanelMode(PanelMode::Full);  break;
        case PanelMode::Full:  SetPanelMode(PanelMode::Off);   break;
        }
    }

    std::string PanelStatus(void)
    {
        static const char *names[] = { "off", "panel", "full" };
        return Utils::Format("panel: mode %s, top frames %lu (format %d), bottom frames %lu (format %d), "
                             "drawn %lu, readback ok %lu/%lu, %s",
                             names[(int)s_mode], s_topFrames, s_topFormat, s_bottomFrames, s_bottomFormat,
                             s_drawnFrames, s_readbackOk, s_readbackChecks, s_gpuStatus.c_str());
    }

    // Starts a new timing window. Not synchronized with the OSD callback, so a
    // frame at the window edge may land in either window.
    static void PerfResetWindow(void)
    {
        s_captureStat = PerfStat();
        s_drawStat = PerfStat();
        s_perfTopFrames = s_topFrames;
        s_perfBottomFrames = s_bottomFrames;
        s_perfStart = svcGetSystemTick();
    }

    void PanelPerfSetEnabled(bool enabled)
    {
        PerfResetWindow();
        s_perf = enabled;
    }

    std::string PanelPerfReport(void)
    {
        static const char *names[] = { "off", "panel", "full" };
        const auto us = [](u64 ticks) { return (unsigned long)(ticks * 1000000 / SYSCLOCK_ARM11); };
        const auto mean = [](const PerfStat &s) { return s.count ? s.sum / s.count : 0; };

        const u64 window = svcGetSystemTick() - s_perfStart;
        const std::string line = Utils::Format(
            "perf: %s, mode %s, window %lu us, top frames %lu, bottom frames %lu, "
            "capture n %lu mean %lu us max %lu us, draw n %lu mean %lu us max %lu us",
            s_perf ? "on" : "off", names[(int)s_mode], us(window),
            s_topFrames - s_perfTopFrames, s_bottomFrames - s_perfBottomFrames,
            s_captureStat.count, us(mean(s_captureStat)), us(s_captureStat.max),
            s_drawStat.count, us(mean(s_drawStat)), us(s_drawStat.max));
        PerfResetWindow();
        return line;
    }
}
