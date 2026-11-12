// Generate build/panel_host.hpp with panel_command_test.py before compiling.
#include "build/panel_host.hpp"

using namespace OneScreen;
using namespace CTRPluginFramework;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

void Screen::DrawRect(s32 x, s32 y, s32 w, s32 h, Color color) const
{
    for (s32 dx = std::max(0, x); dx < std::min(400, x + w); dx++)
        for (s32 dy = std::max(0, y); dy < std::min(240, y + h); dy++)
            EncodePixel(GetFramebuffer(dx, dy), (GSPFormat)(Format & 7), color.r, color.g, color.b);
}

static void Copy(u32 in, u32 out, u32 size, u32 inWidth, u32 inGap,
                 u32 outWidth, u32 outGap, u32)
{
    gxCommands++;
    // TextureCopy widths/gaps are in 16-byte units, not pixels.
    for (u32 i = 0; i < size; i++)
    {
        const u32 src = i / (inWidth * 16) * (inWidth + inGap) * 16 + i % (inWidth * 16);
        const u32 dst = i / (outWidth * 16) * (outWidth + outGap) * 16 + i % (outWidth * 16);
        ((u8 *)out)[dst] = ((const u8 *)in)[src];
    }
}

static void Display(u32 in, u32 inWidth, u32, u32 out, u32 outWidth, u32 outHeight, u32 flags)
{
    gxCommands++;
    const auto inFormat = (GSPFormat)((flags >> 8) & 7);
    const auto outFormat = (GSPFormat)((flags >> 12) & 7);
    const u32 inBpp = inFormat == GSP_BGR8_OES ? 3 : 4;
    const u32 outBpp = outFormat == GSP_BGR8_OES ? 3 : 4;
    const u32 scale = (flags >> 24) == 2 ? 2 : 1;
    const u32 width = outWidth / scale, height = outHeight / scale;
    for (u32 x = 0; x < height; x++)
        for (u32 y = 0; y < width; y++)
        {
            u32 rgb[3] = {};
            for (u32 dx = 0; dx < scale; dx++)
                for (u32 dy = 0; dy < scale; dy++)
                {
                    u8 r, g, b;
                    DecodePixel((const u8 *)in + ((x * scale + dx) * inWidth + y * scale + dy) * inBpp,
                                inFormat, r, g, b);
                    rgb[0] += r; rgb[1] += g; rgb[2] += b;
                }
            EncodePixel((u8 *)out + (x * width + y) * outBpp, outFormat,
                        (u8)(rgb[0] / (scale * scale)), (u8)(rgb[1] / (scale * scale)),
                        (u8)(rgb[2] / (scale * scale)));
        }
}

static void TestFilteringAndCursor()
{
    reverseY = false;
    std::vector<u8> bottom(320 * 720), top(400 * 720);
    Screen bs = {false, 1, 720, (u32)bottom.data()};
    Screen ts = {true, 1, 720, (u32)top.data()};
    for (u32 x = 0; x < 320; x++)
        for (u32 y = 0; y < 240; y++)
            EncodePixel(bs.GetFramebuffer(x, y), GSP_BGR8_OES, (x % 32) * 8, (y % 32) * 8, 32);
    DrawPanel(bs);
    for (float k : {1.0f, 16.0f / 15, 7.0f / 5})
    for (PanelSize size : {PanelSize::Half, PanelSize::TwoThirds})
    {
        testSqueeze = k;
        settings.panelSize = size;
        const Placement p = size == PanelSize::Half ? HalfPlacement(GetLayout(ts)) : TwoThirdsPlacement(GetLayout(ts));
        CpuCopy(ts, p);
        // Independent 1D 2x and 3:2 reference: even/odd filter phases and
        // discontinuities in a repeating ramp, including the last column.
        const auto sample = [&](int output) {
            const int source = size == PanelSize::Half ? output * 2 : output * 3 / 2;
            const int a = (source % 32) * 8, b = ((source + 1) % 32) * 8;
            return size == PanelSize::Half ? (a + b) / 2
                : (output & 1) ? (a + b * 2 + 1) / 3 : (a * 2 + b + 1) / 3;
        };
        for (int dx = 0; dx < 400; dx++)
        {
            const int x = UI::SourceColumn(dx, testSqueeze) - (int)(p.left + p.border);
            if (x < 0 || x >= (int)(320 * p.num / p.den)) continue;
            for (int y : {0, 1, 20, 21, 40})
            {
                u8 r, g, b;
                DecodePixel(ts.GetFramebuffer(dx, p.top + p.border + y), GSP_BGR8_OES, r, g, b);
                CHECK(r == sample(x) && g == sample(y) && b == 32);
            }
        }
        cursorTouching = true;
        CpuCopy(ts, p);
        const int cx = (int)(p.left + p.border + cursorX * p.num / p.den);
        const int dx = (int)std::lround(200 + (cx - 200) / testSqueeze);
        u8 r, g, b;
        DecodePixel(ts.GetFramebuffer(dx, p.top + p.border + cursorY * p.num / p.den), GSP_BGR8_OES, r, g, b);
        CHECK(r == 255 && g == 0 && b == 0);
    }
}

int main()
{
    // Guard bytes, every framebuffer format, both column directions, all modes
    // and corners, normal/readback frames, cursor states and live aspect changes.
    int cases = 0;
    for (int format = 0; format < 5; format++)
    for (bool reverse : {false, true})
    {
        reverseY = reverse;
        const u32 bpp = format == 0 ? 4 : format == 1 ? 3 : 2;
        const u32 stride = 240 * bpp;
        std::vector<u8> bottom(320 * stride + 32, 0xA5), top(400 * stride + 32, 0xA5);
        Screen bs = {false, format, stride, (u32)(bottom.data() + 16)};
        Screen ts = {true, format, stride, (u32)(top.data() + 16)};
        // Constant per-channel values survive all supported format round trips.
        for (u32 x = 0; x < 320; x++)
            for (u32 y = 0; y < 240; y++)
                EncodePixel(bs.GetFramebuffer(x, y), (GSPFormat)format, 160, 96, 32);
        for (float k : {1.0f, (16.0f / 9) / (400.0f / 240), (21.0f / 9) / (400.0f / 240), 4.0f, 1.0f})
        for (PanelMode mode : {PanelMode::Off, PanelMode::Panel, PanelMode::Full})
        for (PanelSize size : {PanelSize::Half, PanelSize::TwoThirds})
        for (PanelCorner corner : {PanelCorner::BottomRight, PanelCorner::BottomLeft, PanelCorner::TopRight, PanelCorner::TopLeft})
        for (bool touching : {false, true})
        {
            testSqueeze = k; cursorTouching = touching;
            settings.panelSize = size; settings.panelCorner = corner;
            SetPanelMode(mode);
            s_textureCopy = Copy;
            s_displayTransfer = Display;
            CHECK(!DrawPanel(bs));
            std::fill(top.begin(), top.end(), 0xA5);
            gxCommands = 0;
            s_drawnFrames = 0; // exercise periodic diagnostic and normal frame
            const bool stockGpu = k <= 1.0f && format == 1 && mode != PanelMode::Off;
            const int firstCommands = !stockGpu ? 0 : mode == PanelMode::Full ? 8 : size == PanelSize::Half ? 5 : 4;
            const int nextCommands = !stockGpu ? 0 : mode == PanelMode::Full ? 8 : size == PanelSize::Half ? 5 : 3;
            CHECK(DrawPanel(ts) == (mode != PanelMode::Off));
            CHECK(gxCommands == firstCommands);
            gxCommands = 0;
            const bool cpuFlush = mode != PanelMode::Off && (!stockGpu || (mode == PanelMode::Panel && size == PanelSize::Half));
            CHECK(DrawPanel(ts) == cpuFlush);
            CHECK(gxCommands == nextCommands);
            if (mode == PanelMode::Off)
                CHECK(std::all_of(top.begin(), top.end(), [](u8 v) { return v == 0xA5; }));
            else
            {
                const bool twoThirds = size == PanelSize::TwoThirds && (stockGpu || k > 1.0f);
                const Placement p = mode == PanelMode::Full ? FullPlacement()
                    : twoThirds ? TwoThirdsPlacement(GetLayout(ts)) : HalfPlacement(GetLayout(ts));
                const int sourceX = (int)(p.left + p.border + 20);
                const int dx = (int)std::lround(200 + (sourceX - 200) / testSqueeze);
                u8 r, g, b;
                DecodePixel(ts.GetFramebuffer(dx, p.top + p.border + 20), (GSPFormat)format, r, g, b);
                CHECK(r == 160 && g == 96 && b == 32);
                if (p.border)
                {
                    DecodePixel(ts.GetFramebuffer(dx, p.top), (GSPFormat)format, r, g, b);
                    CHECK(r == 64 && g == 64 && b == 64);
                }
                CHECK(s_readbackOk == s_readbackChecks);
            }
            CHECK(std::all_of(top.begin(), top.begin() + 16, [](u8 v) { return v == 0xA5; }));
            CHECK(std::all_of(top.end() - 16, top.end(), [](u8 v) { return v == 0xA5; }));
            gxCommands = 0; // native map is a separate one-command presenter
            const bool copied = PanelCopyBottomRect(ts, 0, 0, 320, 240, 40, 0);
            CHECK(copied == (format == 1));
            CHECK(gxCommands == (copied ? 1 : 0));
            CHECK(gxCommands <= 2);
            // Rejected rectangles must submit nothing.
            CHECK(!PanelCopyBottomRect(ts, 0, 0, 320, 241, 40, 0));
            CHECK(gxCommands <= 1);
            cases++;
        }
    }
    TestFilteringAndCursor();
    std::printf("panel commands/composition: %d cases plus filter/cursor checks, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
