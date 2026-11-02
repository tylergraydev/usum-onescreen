// Host test for the UI drawing layer (Sources/UI). Builds with any C++17
// compiler on the PC; run with `make -C tests`.
//
// Renders the demo menu over a fake game scene into a 400x240 buffer laid out
// like the real top screen (rotated, BGR8), checks some pixels, and writes a
// PNG preview to the path given as the first argument.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "UI/Demo.hpp"
#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Raster.hpp"
#include "UI/Text.hpp"

using namespace OneScreen::UI;

static int s_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            s_failures++;                                                        \
        }                                                                        \
    } while (0)

// A framebuffer with guard bytes on both sides, so clipping bugs show up as
// writes outside the screen.
struct Buffer
{
    static constexpr size_t Guard = 4096;
    static constexpr u8 GuardByte = 0xA5;
    std::vector<u8> bytes;
    Surface surface;

    // rotated = the 3DS top-screen layout: stored as 400 columns of 240
    // pixels, each column bottom to top, so x steps by a column and y steps
    // backwards by one pixel.
    Buffer(PixelFormat format, bool rotated)
    {
        const s32 bpp = (s32)BytesPerPixel(format);
        bytes.assign(Guard * 2 + ScreenW * ScreenH * bpp, GuardByte);
        u8 *base = bytes.data() + Guard;
        surface.format = format;
        surface.width = ScreenW;
        surface.height = ScreenH;
        if (rotated)
        {
            surface.origin = base + (ScreenH - 1) * bpp;
            surface.stepX = ScreenH * bpp;
            surface.stepY = -bpp;
        }
        else
        {
            surface.origin = base;
            surface.stepX = bpp;
            surface.stepY = ScreenW * bpp;
        }
        std::memset(base, 0, ScreenW * ScreenH * bpp);
    }

    bool GuardsIntact(void) const
    {
        const size_t end = bytes.size() - Guard;
        for (size_t i = 0; i < Guard; i++)
            if (bytes[i] != GuardByte || bytes[end + i] != GuardByte)
                return false;
        return true;
    }
};

static Color At(const Canvas &canvas, s32 x, s32 y)
{
    Color c{ 0, 0, 0, 0 };
    canvas.GetPixel(x, y, c);
    return c;
}

static bool Same(Color a, Color b, int tolerance = 0)
{
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

// Something game-like to draw over: a sky gradient, ground and a few blocks.
static void DrawFakeScene(Canvas &canvas)
{
    for (s32 y = 0; y < ScreenH; y++)
        canvas.HLine(0, y, ScreenW, Rgb((u8)(60 + y / 3), (u8)(120 + y / 4), (u8)(200 - y / 4)));
    canvas.FillRect(Rect{ 0, 170, ScreenW, 70 }, Rgb(70, 140, 60));
    for (s32 i = 0; i < 6; i++)
        canvas.FillRoundRect(Rect{ 20 + i * 64, 120 + (i % 2) * 14, 40, 50 }, 6, Rgb(150, 90, 50));
}

// Fake System font: the Small font at 2x, through the same mask path the 3DS
// uses for CTRPF's system font.
static s32 FakeMeasure(const std::string &text)
{
    s32 n = 0;
    for (unsigned char ch : text)
        if ((ch & 0xC0) != 0x80)
            n++;
    return n * 12;
}

static bool FakeRasterize(const std::string &text, u8 *mask, s32 w, s32 h)
{
    RenderSmallMask(text, mask, w, h, 2);
    return true;
}

static const FontBackend s_fakeFont = { 16, FakeMeasure, FakeRasterize };

static void TestPrimitives(void)
{
    for (PixelFormat format : { PixelFormat::BGR8, PixelFormat::RGBA8, PixelFormat::RGB565 })
    {
        for (bool rotated : { true, false })
        {
            Buffer buf(format, rotated);
            Canvas canvas(buf.surface);
            const int tol = format == PixelFormat::RGB565 ? 8 : 0;

            // Opaque fill: exact color inside, untouched outside.
            canvas.FillRect(Rect{ 10, 20, 30, 40 }, Rgb(200, 100, 50));
            CHECK(Same(At(canvas, 10, 20), Rgb(200, 100, 50), tol));
            CHECK(Same(At(canvas, 39, 59), Rgb(200, 100, 50), tol));
            CHECK(Same(At(canvas, 40, 59), Rgb(0, 0, 0)));
            CHECK(Same(At(canvas, 39, 60), Rgb(0, 0, 0)));

            // Half-transparent white over black is mid gray.
            canvas.FillRect(Rect{ 100, 100, 4, 4 }, Rgb(255, 255, 255, 128));
            CHECK(Same(At(canvas, 101, 101), Rgb(128, 128, 128), tol + 1));

            // Shapes hanging off every edge must not write outside the screen.
            canvas.FillRect(Rect{ -50, -50, 600, 60 }, Rgb(255, 0, 0));
            canvas.FillRoundRect(Rect{ 380, 220, 100, 100 }, 20, Rgb(0, 255, 0));
            canvas.StrokeRoundRect(Rect{ -30, 200, 80, 80 }, 12, Rgb(0, 0, 255), 3);
            CHECK(Same(At(canvas, 0, 0), Rgb(255, 0, 0), tol));
            CHECK(Same(At(canvas, 399, 239), Rgb(0, 255, 0), tol));
            canvas.Line(-100.f, -100.f, 600.f, 400.f, 5.f, Rgb(255, 255, 0));
            canvas.FillTriangle(-50.f, 300.f, 450.f, 300.f, 200.f, -40.f, Rgb(0, 255, 255, 60));
            CHECK(buf.GuardsIntact());
        }
    }

    Buffer buf(PixelFormat::BGR8, true);
    Canvas canvas(buf.surface);

    // Rounded rect: corner pixel stays background, the middle and edge
    // centers are filled, and the corner arc is anti-aliased.
    canvas.FillRoundRect(Rect{ 50, 50, 60, 40 }, 10, Rgb(255, 255, 255));
    CHECK(Same(At(canvas, 50, 50), Rgb(0, 0, 0)));
    CHECK(Same(At(canvas, 80, 70), Rgb(255, 255, 255)));
    CHECK(Same(At(canvas, 50, 70), Rgb(255, 255, 255)));
    CHECK(Same(At(canvas, 80, 50), Rgb(255, 255, 255)));
    const Color arc = At(canvas, 52, 53);
    CHECK(arc.r > 0 && arc.r < 255);

    // Stroked rounded rect leaves its inside alone.
    canvas.StrokeRoundRect(Rect{ 150, 50, 60, 40 }, 10, Rgb(255, 0, 0), 2);
    CHECK(Same(At(canvas, 180, 50), Rgb(255, 0, 0)));
    CHECK(Same(At(canvas, 180, 70), Rgb(0, 0, 0)));
    CHECK(Same(At(canvas, 150, 50), Rgb(0, 0, 0)));

    // Lines: HLine is exactly one row long, StrokeRect is hollow.
    canvas.HLine(10, 200, 20, Rgb(0, 255, 0));
    CHECK(Same(At(canvas, 10, 200), Rgb(0, 255, 0)));
    CHECK(Same(At(canvas, 29, 200), Rgb(0, 255, 0)));
    CHECK(Same(At(canvas, 30, 200), Rgb(0, 0, 0)));
    CHECK(Same(At(canvas, 10, 201), Rgb(0, 0, 0)));
    canvas.StrokeRect(Rect{ 250, 150, 20, 20 }, Rgb(0, 0, 255), 2);
    CHECK(Same(At(canvas, 251, 151), Rgb(0, 0, 255)));
    CHECK(Same(At(canvas, 260, 160), Rgb(0, 0, 0)));

    // Clip rect limits drawing.
    canvas.SetClip(Rect{ 300, 10, 10, 10 });
    canvas.FillRect(Rect{ 290, 0, 40, 40 }, Rgb(9, 9, 9));
    canvas.ResetClip();
    CHECK(Same(At(canvas, 305, 15), Rgb(9, 9, 9)));
    CHECK(Same(At(canvas, 295, 15), Rgb(0, 0, 0)));

    // Icons fill their middle stroke.
    canvas.FillRect(Rect{ 0, 100, 40, 40 }, Rgb(0, 0, 0));
    DrawIcon(canvas, Icon::Close, Rect{ 0, 100, 40, 40 }, Rgb(255, 255, 255));
    CHECK(Same(At(canvas, 20, 120), Rgb(255, 255, 255)));
    CHECK(Same(At(canvas, 20, 102), Rgb(0, 0, 0)));
}

static void TestLayout(void)
{
    Stack col(Rect{ 0, 0, 100, 100 }, Stack::Column, 4);
    const Rect a = col.Next(10), b = col.Next(20), z = col.NextFromEnd(15);
    CHECK(a.y == 0 && a.h == 10 && a.w == 100);
    CHECK(b.y == 14 && b.h == 20);
    CHECK(z.y == 85 && z.h == 15);
    CHECK(col.Rest().y == 38 && col.Rest().h == 43);

    Stack row(Inset(Rect{ 0, 0, 100, 20 }, 5), Stack::Row, 2);
    const Rect r = row.NextFromEnd(10);
    CHECK(r.x == 85 && r.w == 10 && r.y == 5 && r.h == 10);
    CHECK(row.Rest().w == 78);
}

static void TestText(void)
{
    TextStyle small;
    small.font = Font::Small;
    CHECK(MeasureText("ABC", small) == 18);
    small.scale = 2;
    CHECK(MeasureText("ABC", small) == 36);
    CHECK(LineHeight(small) == 16);
    small.scale = 1;

    // Ellipsis: fits, ends in "...", and leaves short text alone.
    const std::string longText = "The quick brown fox jumps over the lazy dog";
    const std::string cut = Ellipsize(longText, 100, small);
    CHECK(cut.size() > 3 && cut.compare(cut.size() - 3, 3, "...") == 0);
    CHECK(MeasureText(cut, small) <= 100);
    CHECK(Ellipsize("Bag", 100, small) == "Bag");
    // UTF-8: never cuts inside a character.
    const std::string accented = Ellipsize("Pok\xC3\xA9mon Pok\xC3\xA9mon Pok\xC3\xA9mon", 60, small);
    CHECK(accented.size() >= 3 && (accented[accented.size() - 4] & 0xC0) != 0xC0);

    // Alignment: right-aligned text ends at the box's right edge, centered
    // text has equal margins.
    Buffer buf(PixelFormat::BGR8, true);
    Canvas canvas(buf.surface);
    const Rect box{ 100, 100, 200, 16 };
    auto litColumns = [&](s32 &first, s32 &last) {
        first = -1; last = -1;
        for (s32 x = box.x; x < box.Right(); x++)
            for (s32 y = box.y; y < box.Bottom(); y++)
                if (At(canvas, x, y).r > 0) { if (first < 0) first = x; last = x; break; }
    };
    s32 first, last;

    small.align = Align::Right;
    DrawText(canvas, "END", box, small);
    litColumns(first, last);
    // The last glyph column is 1 px gap short of the box edge.
    CHECK(last == box.Right() - 2);

    canvas.FillRect(box, Rgb(0, 0, 0));
    small.align = Align::Center;
    DrawText(canvas, "MID", box, small);
    litColumns(first, last);
    CHECK(std::abs((first - box.x) - (box.Right() - 1 - last)) <= 2);

    // Scaled color text through the System font path (fake backend).
    SetSystemFont(&s_fakeFont);
    TextStyle sys;
    sys.color = Rgb(10, 200, 30);
    sys.scale = 2;
    CHECK(LineHeight(sys) == 32);
    CHECK(MeasureText("Hi", sys) == 48);
    canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, Rgb(0, 0, 0));
    DrawText(canvas, "H", Rect{ 0, 0, 100, 32 }, sys);
    // Small 'H' column 0 is solid; at 2x font and 2x scale it covers x 0..3.
    CHECK(Same(At(canvas, 1, 10), Rgb(10, 200, 30)));
    CHECK(Same(At(canvas, 8, 10), Rgb(0, 0, 0)));
    SetSystemFont(nullptr);
}

// --- PNG writer: stored (uncompressed) deflate, so no zlib is needed. ---

static u32 Crc(const u8 *data, size_t n, u32 crc = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return crc;
}

static void Put32(std::vector<u8> &v, u32 x)
{
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void Chunk(std::vector<u8> &png, const char *type, const std::vector<u8> &data)
{
    Put32(png, (u32)data.size());
    std::vector<u8> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    Put32(png, Crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

static bool WritePng(const char *path, const Canvas &canvas)
{
    std::vector<u8> raw;
    for (s32 y = 0; y < ScreenH; y++)
    {
        raw.push_back(0);
        for (s32 x = 0; x < ScreenW; x++)
        {
            const Color c = At(canvas, x, y);
            raw.push_back(c.r); raw.push_back(c.g); raw.push_back(c.b);
        }
    }

    std::vector<u8> z = { 0x78, 0x01 };
    for (size_t pos = 0; pos < raw.size(); pos += 65535)
    {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    }
    u32 a = 1, b = 0;
    for (u8 byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    Put32(z, (b << 16) | a);

    std::vector<u8> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<u8> ihdr;
    Put32(ihdr, ScreenW); Put32(ihdr, ScreenH);
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });   // 8-bit RGB
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});

    FILE *f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

int main(int argc, char **argv)
{
    TestPrimitives();
    TestLayout();
    TestText();

    // The preview: demo menu over a fake scene on a 3DS-layout BGR8 buffer.
    Buffer buf(PixelFormat::BGR8, true);
    Canvas canvas(buf.surface);
    DrawFakeScene(canvas);
    const Color before = At(canvas, 10, 10);
    SetSystemFont(&s_fakeFont);
    DrawDemoMenu(canvas, 2);
    SetSystemFont(nullptr);

    CHECK(buf.GuardsIntact());
    // The shade darkens the game outside the panel without hiding it.
    const Color after = At(canvas, 10, 10);
    CHECK(after.r < before.r && after.b < before.b && after.b > 0);
    // Panel background inside the panel (below the title, left margin).
    CHECK(Same(At(canvas, 45, 120), Rgb(22, 30, 52), 12));
    // Border on the panel's left edge, mid height.
    CHECK(Same(At(canvas, 40, 120), Rgb(120, 150, 220), 1));
    // Panel corner is rounded: the very corner pixel shows the shaded game.
    CHECK(Same(At(canvas, 40, 16), At(canvas, 38, 16), 3));

    if (argc > 1)
    {
        if (WritePng(argv[1], canvas))
            std::printf("wrote %s\n", argv[1]);
        else
        {
            std::printf("FAIL could not write %s\n", argv[1]);
            s_failures++;
        }
    }

    if (s_failures != 0)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }
    std::printf("all UI host checks passed\n");
    return 0;
}
