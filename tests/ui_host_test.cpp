// Host test for the UI drawing layer (Sources/UI). Builds with any C++17
// compiler on the PC; run with `make -C tests`.
//
// Renders the demo menu over a fake game scene into a 400x240 buffer laid out
// like the real top screen (rotated, BGR8), checks some pixels, and writes a
// PNG preview to the path given as the first argument. The system font is a
// synthetic one built here in the CFNT sheet format (no font data in git).

#include "hires_screen_smoke.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "UI/Demo.hpp"
#include "UI/ColumnMapping.hpp"
#include <limits>
#include "UI/Font.hpp"
#include "UI/Icons.hpp"
#include "UI/Layer.hpp"
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

// Synthetic system font with the geometry of the real one (docs/UI.md): A4
// sheets of 128x32 holding five 24x30 cells, baseline at row 25. Glyphs are
// the 5x7 Small font at 3x, inked from cell column 1 and sitting on the
// baseline; g j p q y get a stem below it, so descenders can be checked. The
// width table is deliberately wrong (as in Azahar's font), so spacing must
// come from the ink.
static constexpr s32 FakeCellW = 24, FakeCellH = 30, FakeBase = 25, FakeSheetW = 128, FakeSheetH = 32;
static constexpr s32 FakeGlyphs = 95, FakePerSheet = 5;
static constexpr u32 FakeSheetSize = FakeSheetW * FakeSheetH / 2;
static std::vector<u8> s_fakeSheets;
static FontSource s_fakeFont;

static int FakeIndexOf(u32 cp)
{
    return cp >= 0x20 && cp < 0x7F ? (int)(cp - 0x20) : '?' - 0x20;
}

static void FakeWidths(int index, s32 &left, s32 &glyphW, s32 &advance)
{
    left = 1;
    glyphW = index == 0 ? 0 : 20;
    advance = 9 + (index % 7);
}

// Writes a 4-bit value at sheet pixel (x, y): tiles row-major, Morton order
// inside a tile, low nibble first (the inverse of DecodeGlyphCell).
static void PutNibble(u8 *sheet, s32 x, s32 y, u8 v)
{
    u32 i = 0;
    for (u32 bit = 0; bit < 3; bit++)
        i |= (((x >> bit) & 1) << (2 * bit)) | (((y >> bit) & 1) << (2 * bit + 1));
    u8 &byte = sheet[((y / 8) * (FakeSheetW / 8) + x / 8) * 32 + i / 2];
    byte = (i & 1) ? (u8)((byte & 0x0F) | (v << 4)) : (u8)((byte & 0xF0) | v);
}

static void BuildFakeFont(void)
{
    const s32 sheets = (FakeGlyphs + FakePerSheet - 1) / FakePerSheet;
    s_fakeSheets.assign(sheets * FakeSheetSize, 0);
    std::vector<u8> mask(15 * 21);
    for (s32 g = 0; g < FakeGlyphs; g++)
    {
        u8 *sheet = s_fakeSheets.data() + (g / FakePerSheet) * FakeSheetSize;
        const s32 cellX = 1 + (g % FakePerSheet) * (FakeCellW + 1), cellY = 1;
        const char ch = (char)(0x20 + g);
        RenderSmallMask(std::string(1, ch), mask.data(), 15, 21, 3);
        for (s32 y = 0; y < 21; y++)
            for (s32 x = 0; x < 15; x++)
                if (mask[y * 15 + x])
                    PutNibble(sheet, cellX + 1 + x, cellY + FakeBase - 21 + y, 15);
        if (std::strchr("gjpqy", ch) != nullptr)
            for (s32 y = FakeBase; y < FakeCellH; y++)
                for (s32 x = 10; x < 13; x++)
                    PutNibble(sheet, cellX + 1 + x, cellY + y, 15);
    }
    s_fakeFont.sheets = s_fakeSheets.data();
    s_fakeFont.sheetSize = FakeSheetSize;
    s_fakeFont.sheetCount = (u16)sheets;
    s_fakeFont.sheetFormat = SheetFormatA4;
    s_fakeFont.sheetW = FakeSheetW;
    s_fakeFont.sheetH = FakeSheetH;
    s_fakeFont.cellsPerRow = FakePerSheet;
    s_fakeFont.cellRows = 1;
    s_fakeFont.cellW = FakeCellW;
    s_fakeFont.cellH = FakeCellH;
    s_fakeFont.baseline = FakeBase;
    s_fakeFont.lineFeed = 30;
    s_fakeFont.indexOf = FakeIndexOf;
    s_fakeFont.widths = FakeWidths;
}

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

    // A canvas with an origin draws in screen coordinates into a smaller
    // surface, clipped to it.
    {
        Buffer small(PixelFormat::BGR8, true);
        Surface part = small.surface;
        part.width = 50;
        part.height = 30;
        Canvas offset(part, 100, 60);
        offset.FillRect(Rect{ 90, 50, 30, 30 }, Rgb(1, 2, 3));
        Canvas whole(small.surface);
        CHECK(Same(At(whole, 0, 0), Rgb(1, 2, 3)));
        CHECK(Same(At(whole, 19, 19), Rgb(1, 2, 3)));
        CHECK(Same(At(whole, 20, 20), Rgb(0, 0, 0)));
        CHECK(Same(At(offset, 100, 60), Rgb(1, 2, 3)));
        CHECK(small.GuardsIntact());
    }

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

    // Without a system font, System text is the Small font at 2x.
    TextStyle sys;
    sys.color = Rgb(10, 200, 30);
    CHECK(LineHeight(sys) == 18);
    CHECK(MeasureText("Hi", sys) == 24);
    canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, Rgb(0, 0, 0));
    DrawText(canvas, "H", Rect{ 0, 0, 100, 18 }, sys);
    // Small 'H' column 0 is solid; at 2x it covers x 0..1.
    CHECK(Same(At(canvas, 1, 2), Rgb(10, 200, 30)));
    CHECK(Same(At(canvas, 4, 2), Rgb(0, 0, 0)));
}

static void TestFont(void)
{
    BuildFakeFont();

    // UTF-8 decoding.
    {
        const std::string t = "A\xC3\xA9\xE2\x80\xA6\xFFz";
        size_t i = 0;
        CHECK(NextCodePoint(t.data(), t.size(), i) == 'A');
        CHECK(NextCodePoint(t.data(), t.size(), i) == 0xE9);
        CHECK(NextCodePoint(t.data(), t.size(), i) == 0x2026);
        CHECK(NextCodePoint(t.data(), t.size(), i) == 0xFFFD);
        CHECK(NextCodePoint(t.data(), t.size(), i) == 'z' && i == t.size());
    }

    // Sheet decoding: a cell comes back exactly as it was written, from its
    // place in the grid; bad indexes are refused.
    {
        std::vector<u8> cell(FakeCellW * FakeCellH);
        CHECK(DecodeGlyphCell(s_fakeFont, 'H' - 0x20, cell.data()));
        std::vector<u8> mask(15 * 21);
        RenderSmallMask("H", mask.data(), 15, 21, 3);
        bool same = true;
        for (s32 y = 0; y < FakeCellH; y++)
            for (s32 x = 0; x < FakeCellW; x++)
            {
                const s32 mx = x - 1, my = y - (FakeBase - 21);
                const bool ink = mx >= 0 && mx < 15 && my >= 0 && my < 21 && mask[my * 15 + mx];
                same &= cell[y * FakeCellW + x] == (ink ? 255 : 0);
            }
        CHECK(same);
        CHECK(DecodeGlyphCell(s_fakeFont, 4, cell.data()));    // '$', last cell of sheet 0
        CHECK(!DecodeGlyphCell(s_fakeFont, 100000, cell.data()));
    }

    // Area filter: solid stays solid, a 1 px checkerboard halves.
    {
        std::vector<u8> solid(10 * 10, 255), checker(10 * 10), out(5 * 5);
        for (s32 i = 0; i < 100; i++)
            checker[i] = ((i % 10 + i / 10) & 1) ? 255 : 0;
        ScaleMask(solid.data(), 10, 10, 0.5f, out.data(), 5, 5);
        CHECK(out[0] == 255 && out[24] == 255);
        ScaleMask(checker.data(), 10, 10, 0.5f, out.data(), 5, 5);
        CHECK(out[12] >= 126 && out[12] <= 129);
    }

    // Glyph cache: ink-based spacing, hits on reuse, empty space glyph.
    {
        static GlyphCache cache(0.6f);
        CHECK(cache.Get('H') == nullptr);
        cache.SetSource(&s_fakeFont);
        CHECK(cache.LineHeight() == 18 && cache.Ascent() == 15);
        const Glyph *h = cache.Get('H');
        // Ink 15 px + gap 3 = 18 font px -> 10.8 -> 11 screen px; the width
        // table (glyphW 20) is ignored.
        CHECK(h->advance == 11 && h->w == 9 && h->h == 18);
        CHECK(cache.misses == 1 && cache.hits == 0);
        cache.Get('H');
        CHECK(cache.hits == 1);
        const Glyph *space = cache.Get(' ');
        CHECK(space->w == 0 && space->advance == 5);   // the table's 9 -> 5
        CHECK(cache.Advance(0x263A) == cache.Advance('?'));   // missing -> replacement
    }

    // Drawing through the font: descenders below the baseline are drawn,
    // spacing is even, and the lines are 18 and 24 px.
    SetSystemFont(&s_fakeFont);
    TextStyle sys;
    sys.color = Rgb(10, 200, 30);
    CHECK(LineHeight(sys) == 18);
    TextStyle large = sys;
    large.font = Font::Large;
    CHECK(LineHeight(large) == 24);
    CHECK(MeasureText("HH", sys) == 22);

    Buffer buf(PixelFormat::BGR8, true);
    Canvas canvas(buf.surface);
    DrawText(canvas, "gH", Rect{ 0, 0, 100, 18 }, sys);
    // Baseline at row 15: the 'g' stem reaches rows 15..17, 'H' stops above.
    bool gBelow = false, hBelow = false;
    for (s32 x = 0; x < 30; x++)
    {
        if (x < 11)
            gBelow |= At(canvas, x, 16).g > 100;
        else
            hBelow |= At(canvas, x, 16).g > 0;
    }
    CHECK(gBelow && !hBelow);

    // Gaps between letters are the same wherever they are.
    canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, Rgb(0, 0, 0));
    DrawText(canvas, "IIII", Rect{ 0, 20, 200, 18 }, sys);
    std::vector<s32> starts;
    bool lit = false;
    for (s32 x = 0; x < 100; x++)
    {
        bool col = false;
        for (s32 y = 20; y < 38; y++)
            col |= At(canvas, x, y).g > 60;
        if (col && !lit)
            starts.push_back(x);
        lit = col;
    }
    CHECK(starts.size() == 4);
    if (starts.size() == 4)
        CHECK(starts[1] - starts[0] == starts[2] - starts[1] && starts[3] - starts[2] == starts[2] - starts[1]);
    SetSystemFont(nullptr);
}

// The demo in a Layer, presented onto a screen, matches drawing it directly
// inside the panel's rounded rect and leaves everything outside untouched.
static void TestLayer(void)
{
    SetSystemFont(&s_fakeFont);
    for (bool rotated : { true, false })
    {
        Buffer direct(PixelFormat::BGR8, rotated), screen(PixelFormat::BGR8, rotated), scene(PixelFormat::BGR8, rotated);
        Canvas directCanvas(direct.surface), screenCanvas(screen.surface), sceneCanvas(scene.surface);
        DrawDemoMenu(directCanvas, 1);
        DrawFakeScene(screenCanvas);
        DrawFakeScene(sceneCanvas);

        Layer layer;
        std::vector<u8> pixels(Layer::BytesFor(screen.surface, DemoPanel));
        CHECK(!layer.Matches(screen.surface));
        CHECK(!layer.Setup(screen.surface, Rect{ 300, 0, 200, 10 }, 0, pixels.data(), pixels.size()));
        CHECK(layer.Setup(screen.surface, DemoPanel, DemoRadius, pixels.data(), pixels.size()));
        CHECK(layer.Matches(screen.surface));
        Canvas layerCanvas = layer.Begin();
        DrawDemoMenu(layerCanvas, 1);
        layer.Present(screen.surface);
        CHECK(screen.GuardsIntact());

        s32 same = 0, outsideChanged = 0, mismatched = 0;
        for (s32 y = 0; y < ScreenH; y++)
            for (s32 x = 0; x < ScreenW; x++)
            {
                const Color got = At(screenCanvas, x, y);
                if (x < DemoPanel.x || x >= DemoPanel.Right() || y < DemoPanel.y || y >= DemoPanel.Bottom())
                {
                    outsideChanged += !Same(got, At(sceneCanvas, x, y));
                    continue;
                }
                // Corners are either not copied or antialiased against black.
                const s32 cx = std::min(x - DemoPanel.x, DemoPanel.Right() - 1 - x);
                const s32 cy = std::min(y - DemoPanel.y, DemoPanel.Bottom() - 1 - y);
                if (Same(got, At(directCanvas, x, y)))
                    same++;
                else if (cx >= DemoRadius || cy >= DemoRadius)
                    mismatched++;
            }
        CHECK(outsideChanged == 0 && mismatched == 0);
        CHECK(same > DemoPanel.w * DemoPanel.h - 4 * DemoRadius * DemoRadius);
        // The very corner pixel is the game, the left edge is the border.
        CHECK(Same(At(screenCanvas, DemoPanel.x, DemoPanel.y), At(sceneCanvas, DemoPanel.x, DemoPanel.y)));
        CHECK(Same(At(screenCanvas, DemoPanel.x, 120), Rgb(120, 150, 220), 1));
    }
    SetSystemFont(nullptr);
}

// Sample a fully presented stock reference: this verifies rounded corners,
// source-column selection, and skip edges together, including untouched pixels.
static void TestLayerSqueeze(void)
{
    CHECK(SourceColumn(200, 1.4f) == 200);
    CHECK(SourceColumn(100, 1.4f) == 60);
    CHECK(SourceColumn(0, 2.0f) == -1);
    CHECK(SourceColumn(399, 2.0f) == 400);
    CHECK(SourceColumn(200, std::numeric_limits<float>::max()) == 200);
    CHECK(SourceColumn(199, std::numeric_limits<float>::max()) == -1);
    CHECK(SourceColumn(201, std::numeric_limits<float>::max()) == 400);
    for (bool rotated : { true, false })
        for (PixelFormat format : { PixelFormat::BGR8, PixelFormat::RGBA8, PixelFormat::RGB565 })
            for (s32 width : { 400, 320 })
                for (float factor : { 1.0f, 16.0f / 15.0f, 1.4f, 2.0f, 1000.0f })
                    for (const Rect area : { Rect{ 8, 6, 73, 34 }, Rect{ 140, 17, 170, 61 } })
                        for (const Rect skip : { Rect{}, Rect{ area.x + 11, area.y + 3, 27, 20 },
                                                Rect{ area.x - 1, area.y - 5, 20, 200 } })
                        {
                            Buffer reference(format, rotated), output(format, rotated);
                            reference.surface.width = output.surface.width = width;
                            output.surface.squeeze = factor;
                            Canvas ref(reference.surface), got(output.surface);
                            ref.FillRect(Rect{ 0, 0, width, ScreenH }, Rgb(9, 17, 25));
                            got.FillRect(Rect{ 0, 0, width, ScreenH }, Rgb(9, 17, 25));
                            Layer layer;
                            std::vector<u8> pixels(Layer::BytesFor(reference.surface, area));
                            CHECK(layer.Setup(reference.surface, area, 9, pixels.data(), pixels.size()));
                            Canvas source = layer.Begin();
                            for (s32 x = area.x; x < area.Right(); x++)
                                source.VLine(x, area.y, area.h, Rgb((u8)x, (u8)(x / 2), 201));
                            layer.Present(reference.surface, skip);
                            layer.Present(output.surface, skip);
                            for (s32 dx = 0; dx < width; dx++)
                            {
                                const s32 sx = width == 400 ? SourceColumn(dx, factor) : dx;
                                for (s32 y = 0; y < ScreenH; y++)
                                {
                                    const s32 sample = sx >= 0 && sx < width ? sx : 0;
                                    CHECK(Same(At(got, dx, y), At(ref, sample, y)));
                                }
                            }
                            CHECK(reference.GuardsIntact() && output.GuardsIntact());
                        }
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
    TestFont();
    TestLayer();
    TestLayerSqueeze();

    // The preview, as `uidemo shade` draws it: a fake scene on a 3DS-layout
    // BGR8 buffer, the shade drawn on it, the menu copied in from a Layer.
    Buffer buf(PixelFormat::BGR8, true);
    Canvas canvas(buf.surface);
    DrawFakeScene(canvas);
    const Color before = At(canvas, 10, 10);
    SetSystemFont(&s_fakeFont);
    DrawDemoShade(canvas);
    Layer layer;
    std::vector<u8> pixels(Layer::BytesFor(buf.surface, DemoPanel));
    layer.Setup(buf.surface, DemoPanel, DemoRadius, pixels.data(), pixels.size());
    Canvas layerCanvas = layer.Begin();
    DrawDemoMenu(layerCanvas, 2);
    CHECK(HiResScreenSmoke::Run(DemoPanel, DemoRadius, [&](Canvas &hi) { DrawDemoMenu(hi, 2); }));
    layer.Present(buf.surface);
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
    CHECK(Same(At(canvas, DemoPanel.x, DemoPanel.y), At(canvas, DemoPanel.x - 2, DemoPanel.y), 3));

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
