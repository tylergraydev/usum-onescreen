#include "UI/LayerGpu.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "UI/ColumnMapping.hpp"
#include "UI/Layer.hpp"
#include "UI/LayerRuns.hpp"

namespace OneScreen
{
namespace UI
{
    using DisplayTransferFn = void (*)(u32 in, u32 inWidth, u32 inHeight, u32 out, u32 outWidth,
                                       u32 outHeight, u32 flags);
    using TextureCopyFn = void (*)(u32 in, u32 out, u32 size, u32 inWidth, u32 inGap, u32 outWidth,
                                   u32 outGap, u32 flags);

    static DisplayTransferFn s_displayTransfer = nullptr;
    static TextureCopyFn s_textureCopy = nullptr;

    // Transfer engine flags and formats (Panel.cpp has the same).
    static constexpr u32 FlagInputLinear = 1 << 1;
    static constexpr u32 FlagTextureCopy = 1 << 3;
    static constexpr u32 FlagNoSwizzle   = 1 << 5;
    static constexpr u32 Rgba8 = GSP_RGBA8_OES;
    static constexpr u32 Rgb8  = GSP_BGR8_OES;

    // Framebuffer lines are screen columns of 240 px. Work is done in RGBA8
    // (4 bytes a pixel) so a TextureCopy's 8-byte start and 16-byte length
    // steps are 2 and 4 px; in BGR8 they would be 8 and 16 px, too coarse
    // for rounded corners.
    static constexpr u32 Len = 240;
    static constexpr u32 MaxLines = 400;
    static constexpr u32 PageAlign(u32 size) { return (size + 0xFFF) & ~0xFFFu; }
    static constexpr u32 OffBg    = 0;                                        // game columns, RGBA8, GPU-written
    static constexpr u32 OffStage = OffBg + PageAlign(Len * MaxLines * 4);    // layer, RGBA8, CPU-written
    static constexpr u32 OffTwin  = OffStage + PageAlign(Len * MaxLines * 4); // stage's GPU-side twin
    static constexpr u32 OffOut   = OffTwin + PageAlign(Len * MaxLines * 4);  // result, BGR8
    static constexpr u32 ScratchSize = OffOut + PageAlign(Len * MaxLines * 3);

    static u32 s_scratch = 0;
    static bool s_failed = false;
    static volatile bool s_on = true;
    static u32 s_presents = 0, s_fallbacks = 0;
    static u32 s_copies = 0, s_maxCopies = 0;   // GX commands in one present, most seen

    // The game's GX command queue (gx functions 0x27A5BC / 0x279E28 push
    // onto it with 0x279A2C): *(*(0x69EA68 + 0x1C) + 0x58), its command
    // count in byte +1. The push refuses a command once 15 are queued; a
    // refused command of the game's own could break it. In Azahar the queue
    // was always empty before our pushes (it runs each one at once), so this
    // is a safety net, not a fix for anything seen. Same address on Ultra Moon.
    static constexpr u32 GxState = 0x69EA68;
    static constexpr u32 QueueRoom = 12;        // wait while this many are queued
    static u32 s_maxDepth = 0, s_waits = 0, s_gaveUp = 0;

    static volatile u8 *QueueCount(void)
    {
        const u32 gx = *(volatile u32 *)(GxState + 0x1C);
        if (gx == 0)
            return nullptr;
        const u32 queue = *(volatile u32 *)(gx + 0x58);
        return queue == 0 ? nullptr : (volatile u8 *)(queue + 1);
    }

    // Room for one more command: waits up to ~2 ms for the GPU to drain the
    // queue. False if it stays full (the caller drops this frame's present).
    static bool Room(void)
    {
        volatile u8 *count = QueueCount();
        if (count == nullptr)
            return false;
        for (int i = 0; ; i++)
        {
            const u32 depth = *count;
            if (depth > s_maxDepth)
                s_maxDepth = depth;
            if (depth < QueueRoom)
                return true;
            if (i == 20)
            {
                s_gaveUp++;
                return false;
            }
            s_waits++;
            svcSleepThread(100000);
        }
    }
    static std::string s_why = "not used yet";

    static u32 Flags(u32 in, u32 out)
    {
        return FlagInputLinear | FlagNoSwizzle | (in << 8) | (out << 12);
    }

    static bool Scratch(void)
    {
        if (s_scratch != 0)
            return true;
        if (s_failed)
            return false;
        u32 addr = 0;
        const Result res = svcControlMemory(&addr, 0, 0, ScratchSize, MEMOP_ALLOC_LINEAR,
                                            (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
        if (R_FAILED(res) || addr == 0)
        {
            s_failed = true;
            s_why = CTRPluginFramework::Utils::Format("no linear memory (0x%08lX)", (u32)res);
            return false;
        }
        s_scratch = addr;
        return true;
    }

    static bool Fallback(const char *why)
    {
        s_fallbacks++;
        s_why = why;
        return false;
    }

    // Checked pushes: false when the queue stays full.
#define DT(...) do { if (!Room()) return Fallback("GX queue full"); s_displayTransfer(__VA_ARGS__); } while (0)
#define TC(...) do { if (!Room()) return Fallback("GX queue full"); s_textureCopy(__VA_ARGS__); } while (0)

    static bool GpuPresent(const Layer &layer, const Surface &screen, const Rect &skip)
    {
        if (!s_on || s_displayTransfer == nullptr || s_textureCopy == nullptr)
            return false;
        // Reserve the filter's one target list. Legacy presentation can use
        // eight GX commands by itself; use the existing CPU fallback rather
        // than add that work to a filtered frame. Off retains the original path.
        if (GetScreenFilter() != ScreenFilter::Off)
            return Fallback("screen filter reserves GX budget");
        if (screen.format != PixelFormat::BGR8 || screen.height != (s32)Len || screen.width > (s32)MaxLines
            || screen.stepX != (s32)Len * 3 || (screen.stepY != 3 && screen.stepY != -3))
            return Fallback("screen not BGR8 columns");
        const Rect area = layer.Area();
        const ColumnRange bands = PillarboxColumns(screen, area);
        if (!Scratch())
            return false;

        // Destination columns [d0, d1) the layer lands on.
        const float squeeze = screen.width == ScreenW ? screen.squeeze : 1.0f;
        s32 d0 = -1, d1 = -1;
        for (s32 d = 0; d < screen.width; d++)
        {
            const s32 x = SourceColumn(d, squeeze);
            if (x < area.x || x >= area.Right())
                continue;
            if (d0 < 0)
                d0 = d;
            d1 = d + 1;
        }
        if (bands.active)
        {
            d0 = 0;
            d1 = screen.width;
        }
        if (d0 < 0)
            return true;    // nothing of it on screen
        const u32 lines = (u32)(d1 - d0);
        const bool reverse = screen.stepY < 0;
        // Memory index (0..239) of screen row y within a column.
        const auto index = [&](s32 y) { return (u32)(reverse ? (s32)Len - 1 - y : y); };
        const u32 fbLines = (u32)(screen.origin + d0 * screen.stepX + (reverse ? ((s32)Len - 1) * screen.stepY : 0));

        const u32 bg = s_scratch + OffBg, stage = s_scratch + OffStage;
        const u32 twin = s_scratch + OffTwin, out = s_scratch + OffOut;

        // 1. The game's columns, by GPU, into bg (keeps the internal resolution).
        DT(fbLines, Len, lines, bg, Len, lines, Flags(Rgb8, Rgba8));

        // 2. The layer into stage (CPU, memory the GPU never writes), as RGBA8
        //    (bytes A, B, G, R), only the rows each column copies.
        const Surface &px = layer.Pixels();
        static LayerRuns runs[MaxLines];
        for (u32 l = 0; l < lines; l++)
        {
            runs[l] = {};
            const s32 dx = d0 + (s32)l;
            if (bands.active && (dx < bands.begin || dx >= bands.end))
            {
                // A pillarbox band column: whole 240 rows, so all band
                // columns merge into one copy per side in step 3. One copy
                // per band column (~114 a frame at 21:9) crashed the game
                // in the X menu (PC 0 from 0x41DB8C); cause not found, the
                // queue was not full (see Room).
                const s32 distance = dx < bands.begin ? bands.begin - 1 - dx : dx - bands.end;
                const s32 phase = distance < 3 ? 11 - distance
                                : screen.pillarbox == Pillarbox::Pattern ? dx & 7 : 8;
                WriteBandColumn((u8 *)(stage + l * Len * 4), phase, PixelFormat::RGBA8, reverse);
                runs[l].pieces[0] = { 0, Len };
                continue;
            }
            const s32 x = SourceColumn(dx, squeeze);
            if (x < area.x || x >= area.Right())
                continue;
            const s32 i = x - area.x;
            // Square corners: every column copies the whole rectangle, so
            // the layer goes in one copy (two around a skip window), plus
            // one per band. Per-column rounded runs were ~10 more GX
            // commands a present: with 15 the game's Party screen
            // (main:00647FCC) crashed within seconds every time, and the
            // map with its own copies too, while 6-8 ran for minutes. The X
            // menu, same rectangle, did not crash. Cause not found. The
            // corners show the layer's black outside the rounded shape.
            const s32 y0 = 0, y1 = area.h;
            u8 *dst = (u8 *)(stage + l * Len * 4);
            for (s32 y = 0; y < area.h; y++)
            {
                const u8 *s = px.origin + i * px.stepX + y * px.stepY;
                u8 *d = dst + index(area.y + y) * 4;
                d[0] = 0xFF; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
            }
            // Rows in memory order, rounded inward to the copy's steps.
            u32 a = index(area.y + (reverse ? y1 - 1 : y0));
            u32 b = index(area.y + (reverse ? y0 : y1 - 1)) + 1;
            u32 skipA = 0, skipB = 0;
            if (!skip.Empty() && x >= skip.x && x < skip.Right())
            {
                // Clip in screen rows before converting to memory order.
                const s32 lo = skip.y < 0 ? 0 : skip.y > (s32)Len ? (s32)Len : skip.y;
                const s32 hi = skip.Bottom() < 0 ? 0 : skip.Bottom() > (s32)Len ? (s32)Len : skip.Bottom();
                skipA = (u32)(reverse ? (s32)Len - hi : lo);
                skipB = (u32)(reverse ? (s32)Len - lo : hi);
            }
            runs[l] = SplitLayerRun(a, b, skipA, skipB);
        }
        // stage's twin at the game's scale: made from bg, then overwritten.
        DT(bg, Len, lines, twin, Len, lines, Flags(Rgba8, Rgba8));
        DT(stage, Len, lines, twin, Len, lines, Flags(Rgba8, Rgba8));

        const u32 lineBytes = Len * 4;
        s_copies = 0;

        // 3. Each half of the layer's runs into bg; neighbouring columns
        //    with the same piece go in one copy. Both pieces avoid skip.
        for (unsigned piece = 0; piece < 2; piece++)
            for (u32 l = 0; l < lines;)
            {
                const LayerRun r = runs[l].pieces[piece];
                u32 n = 1;
                while (l + n < lines && runs[l + n].pieces[piece].a == r.a
                       && runs[l + n].pieces[piece].b == r.b)
                    n++;
                if (r.b > r.a)
                {
                    const u32 run = (r.b - r.a) * 4, gap = lineBytes - run;
                    const u32 at = l * lineBytes + r.a * 4;
                    TC(twin + at, bg + at, run * n, run / 16, gap / 16, run / 16, gap / 16, FlagTextureCopy);
                    s_copies++;
                }
                l += n;
            }

        // 4. Back to BGR8, then over the game's columns. This also restores
        // the saved bg under skip; Map must copy its picture AFTER Present.
        DT(bg, Len, lines, out, Len, lines, Flags(Rgba8, Rgb8));
        TC(out, fbLines, lines * Len * 3, Len * 3 / 16, 0, Len * 3 / 16, 0, FlagTextureCopy);
        s_presents++;
        s_copies += 5;  // the four DisplayTransfers and the copy back
        if (s_copies > s_maxCopies)
            s_maxCopies = s_copies;
        return true;
    }

#undef DT
#undef TC

    void StartLayerGpu(u32 displayTransfer, u32 textureCopy)
    {
        s_displayTransfer = (DisplayTransferFn)displayTransfer;
        s_textureCopy = (TextureCopyFn)textureCopy;
        Layer::s_gpuPresent = GpuPresent;
    }

    void SetLayerGpu(bool on)
    {
        s_on = on;
    }

    std::string LayerGpuStatus(void)
    {
        return CTRPluginFramework::Utils::Format("layergpu: %s, app free %lu KB, scratch %08lX, presents %lu, most GX commands %lu, deepest queue %lu, waits %lu, gave up %lu, fallbacks %lu (last: %s)",
                                                 s_on ? "on" : "off", (u32)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024), s_scratch, s_presents, s_maxCopies, s_maxDepth, s_waits, s_gaveUp, s_fallbacks,
                                                 s_why.c_str());
    }
}
}
