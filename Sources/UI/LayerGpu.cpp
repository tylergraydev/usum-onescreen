#include "UI/LayerGpu.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "UI/ColumnMapping.hpp"
#include "UI/Layer.hpp"

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

    static bool GpuPresent(const Layer &layer, const Surface &screen, const Rect &skip)
    {
        if (!s_on || s_displayTransfer == nullptr || s_textureCopy == nullptr)
            return false;
        if (screen.format != PixelFormat::BGR8 || screen.height != (s32)Len || screen.width > (s32)MaxLines
            || screen.stepX != (s32)Len * 3 || (screen.stepY != 3 && screen.stepY != -3))
            return Fallback("screen not BGR8 columns");
        if (!skip.Empty())
            return Fallback("skip rect");
        const Rect area = layer.Area();
        if (PillarboxColumns(screen, area).active)
            return Fallback("pillarbox bands");
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
        s_displayTransfer(fbLines, Len, lines, bg, Len, lines, Flags(Rgb8, Rgba8));

        // 2. The layer into stage (CPU, memory the GPU never writes), as RGBA8
        //    (bytes A, B, G, R), only the rows each column copies.
        const Surface &px = layer.Pixels();
        struct Run { u32 a, b; };
        static Run runs[MaxLines];
        for (u32 l = 0; l < lines; l++)
        {
            runs[l] = { 0, 0 };
            const s32 x = SourceColumn(d0 + (s32)l, squeeze);
            if (x < area.x || x >= area.Right())
                continue;
            const s32 i = x - area.x;
            const s32 y0 = layer.TopRows(i), y1 = area.h - layer.TopRows(i);
            if (y1 <= y0)
                continue;
            u8 *dst = (u8 *)(stage + l * Len * 4);
            for (s32 y = y0; y < y1; y++)
            {
                const u8 *s = px.origin + i * px.stepX + y * px.stepY;
                u8 *d = dst + index(area.y + y) * 4;
                d[0] = 0xFF; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
            }
            // Rows in memory order, rounded inward to the copy's steps.
            u32 a = index(area.y + (reverse ? y1 - 1 : y0));
            u32 b = index(area.y + (reverse ? y0 : y1 - 1)) + 1;
            a = (a + 1) & ~1u;
            if (b > a)
                b = a + ((b - a) & ~3u);
            runs[l] = { a, b > a ? b : a };
        }
        // stage's twin at the game's scale: made from bg, then overwritten.
        s_displayTransfer(bg, Len, lines, twin, Len, lines, Flags(Rgba8, Rgba8));
        s_displayTransfer(stage, Len, lines, twin, Len, lines, Flags(Rgba8, Rgba8));

        // 3. The layer's runs into bg; neighbouring columns with the same run
        //    go in one copy.
        const u32 lineBytes = Len * 4;
        for (u32 l = 0; l < lines;)
        {
            const Run r = runs[l];
            u32 n = 1;
            while (l + n < lines && runs[l + n].a == r.a && runs[l + n].b == r.b)
                n++;
            if (r.b > r.a)
            {
                const u32 run = (r.b - r.a) * 4, gap = lineBytes - run;
                const u32 at = l * lineBytes + r.a * 4;
                s_textureCopy(twin + at, bg + at, run * n, run / 16, gap / 16, run / 16, gap / 16, FlagTextureCopy);
            }
            l += n;
        }

        // 4. Back to BGR8, then over the game's columns.
        s_displayTransfer(bg, Len, lines, out, Len, lines, Flags(Rgba8, Rgb8));
        s_textureCopy(out, fbLines, lines * Len * 3, Len * 3 / 16, 0, Len * 3 / 16, 0, FlagTextureCopy);
        s_presents++;
        return true;
    }

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
        return CTRPluginFramework::Utils::Format("layergpu: %s, scratch %08lX, presents %lu, fallbacks %lu (last: %s)",
                                                 s_on ? "on" : "off", s_scratch, s_presents, s_fallbacks,
                                                 s_why.c_str());
    }
}
}
