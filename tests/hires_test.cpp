// Pure overlay tests; no CTRPF, libctru, emulator or game assets.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "UI/Layer.hpp"
#include "UI/Font.hpp"
#include "UI/Text.hpp"
#include "UI/QuadLayout.hpp"
using namespace OneScreen::UI;

struct Image
{
    std::vector<u8> data;
    Surface surface;
    Image(s32 w, s32 h, bool alpha = true) : data((size_t)w * h * 4 + 64, 0xA5),
        surface{ data.data() + 32, 4, w * 4, PixelFormat::RGBA8, w, h }
    {
        surface.transparent = alpha;
        Clear();
    }
    void Clear() { std::memset(surface.origin, 0, (size_t)surface.width * surface.height * 4); }
    u8 Alpha(s32 x, s32 y) const { return surface.origin[y * surface.stepY + x * 4]; }
    void Guards() const
    {
        for (size_t i = 0; i < 32; i++) assert(data[i] == 0xA5 && data[data.size() - 1 - i] == 0xA5);
    }
};

static void Scene(Canvas &c)
{
    c.FillRect(Rect{ -1, -1, 28, 24 }, Rgb(30, 50, 70));
    c.FillRoundRect(Rect{ 1, 2, 20, 16 }, 5, Rgb(210, 40, 80, 130));
    c.StrokeRoundRect(Rect{ 2, 3, 18, 14 }, 4, Rgb(60, 240, 100), 2);
    c.StrokeRect(Rect{ 3, 3, 10, 7 }, Rgb(255, 255, 255, 80), 2);
    c.HLine(0, 4, 25, Rgb(20, 40, 80));
    c.VLine(5, 0, 22, Rgb(100, 10, 50));
    c.Line(-2, 3, 24, 17, 1.5f, Rgb(210, 180, 90));
    c.FillTriangle(1, 20, 10, 5, 23, 20, Rgb(30, 220, 50, 180));
    const u8 mask[] = { 0, 64, 255, 128, 255, 0 };
    c.DrawMask(7, 9, mask, 3, 2, 3, 2, Rgb(50, 40, 255, 170));
    c.BlendPixel(8, 8, Rgb(150, 70, 200, 190), 123);
}

static void ScaleOne()
{
    // Both scale-1 entry points must remain byte identical, every format/order.
    for (PixelFormat format : { PixelFormat::RGBA8, PixelFormat::BGR8, PixelFormat::RGB565,
                                PixelFormat::RGB5A1, PixelFormat::RGBA4 })
        for (bool reverse : { false, true })
        {
            const s32 bpp = (s32)BytesPerPixel(format);
            std::vector<u8> a((size_t)24 * 24 * bpp, 0x39), b = a;
            Surface sa{ a.data() + (reverse ? 23 * bpp : 0), reverse ? 24 * bpp : bpp,
                        reverse ? -bpp : 24 * bpp, format, 24, 24 };
            Surface sb = sa; sb.origin = b.data() + (sa.origin - a.data());
            Canvas ca(sa), cb(sb, 0, 0, 1.f);
            Scene(ca); Scene(cb);
            assert(a == b);
        }
    // Independent legacy pixel reference, including the old opaque RGBA alpha.
    // This checks bytes rather than comparing two instances of the new renderer.
    for (PixelFormat format : { PixelFormat::RGBA8, PixelFormat::BGR8, PixelFormat::RGB565,
                                PixelFormat::RGB5A1, PixelFormat::RGBA4 })
    {
        const s32 bpp = (s32)BytesPerPixel(format);
        std::vector<u8> pixels((size_t)8 * 8 * bpp, 0), reference = pixels;
        Surface surface{ pixels.data(), bpp, 8 * bpp, format, 8, 8 };
        Canvas canvas(surface);
        const auto apply = [&](s32 x, s32 y, Color color, u8 cov) {
            if (x < 0 || y < 0 || x >= 8 || y >= 8) return;
            const u32 a = (color.a * cov + 127u) / 255u;
            if (a == 0) return;
            u8 *p = reference.data() + (y * 8 + x) * bpp;
            u8 r, g, b; ReadPixel(p, format, r, g, b);
            WritePixel(p, format, (u8)((color.r * a + r * (255 - a) + 127) / 255),
                (u8)((color.g * a + g * (255 - a) + 127) / 255),
                (u8)((color.b * a + b * (255 - a) + 127) / 255));
        };
        const Color fill = Rgb(230, 70, 10, 173), line = Rgb(40, 220, 190);
        canvas.FillRect(Rect{ -1, 1, 6, 5 }, fill);
        for (s32 y = 1; y < 6; y++) for (s32 x = -1; x < 5; x++) apply(x, y, fill, 255);
        canvas.HLine(2, 4, 8, line);
        for (s32 x = 2; x < 10; x++) apply(x, 4, line, 255);
        canvas.VLine(6, -1, 6, fill);
        for (s32 y = -1; y < 5; y++) apply(6, y, fill, 255);
        const u8 mask[] = { 0, 64, 128, 255 };
        canvas.DrawMask(3, 3, mask, 2, 2, 2, 2, fill);
        for (s32 y = 0; y < 4; y++) for (s32 x = 0; x < 4; x++)
            apply(3 + x, 3 + y, fill, mask[(y / 2) * 2 + x / 2]);
        canvas.BlendPixel(5, 6, line, 123); apply(5, 6, line, 123);
        assert(pixels == reference);
    }
    // Frozen legacy RGBA byte result: partial coverage blends RGB against black
    // and still writes opaque alpha, unlike the opt-in transparent surface.
    Image legacy(2, 2, false);
    Canvas c(legacy.surface);
    c.BlendPixel(0, 0, Rgb(200, 100, 50, 128));
    const u8 expected[] = { 255, 25, 50, 100 };
    assert(std::memcmp(legacy.surface.origin, expected, sizeof(expected)) == 0);
}

static void Primitives()
{
    Image image(60, 60);
    Canvas c(image.surface, 10, 20, HiResScale);
    assert(c.GetClip().x == 10 && c.GetClip().w == 24);
    c.FillRect(Rect{ 11, 21, 2, 2 }, Rgb(255, 0, 0));
    // Screen edges 11..13 become physical edges 2.5..7.5.
    assert(image.Alpha(1, 3) == 0 && image.Alpha(2, 3) == 128);
    assert(image.Alpha(3, 3) == 255 && image.Alpha(7, 3) == 128 && image.Alpha(8, 3) == 0);
    assert(image.Alpha(2, 2) == 64);
    image.Clear();
    c.FillRoundRect(Rect{ 10, 20, 24, 24 }, 8, Rgb(40, 80, 160));
    assert(image.Alpha(0, 0) == 0 && image.Alpha(59, 59) == 0 && image.Alpha(30, 30) == 255);
    assert(std::any_of(image.data.begin() + 32, image.data.end() - 32,
                      [](u8 value) { return value > 0 && value < 255; }));
    image.Clear();
    c.StrokeRoundRect(Rect{ 10, 20, 24, 24 }, 8, Rgb(255, 255, 255), 2);
    assert(image.Alpha(30, 30) == 0 && image.Alpha(30, 1) == 255);
    image.Clear();
    c.HLine(12, 22, 4, Rgb(255, 255, 255));
    assert(image.Alpha(5, 5) == 255 && image.Alpha(5, 7) == 128 && image.Alpha(5, 8) == 0);
    image.Clear();
    c.VLine(12, 22, 4, Rgb(255, 255, 255));
    assert(image.Alpha(5, 5) == 255 && image.Alpha(7, 5) == 128 && image.Alpha(8, 5) == 0);
    image.Clear();
    c.BlendPixel(12, 22, Rgb(200, 100, 50, 128));
    assert(image.Alpha(5, 5) == 128 && image.surface.origin[5 * 240 + 5 * 4 + 3] == 200);
    image.Clear();
    Canvas physical(image.surface);
    physical.BlendPixel(0, 0, Rgb(200, 0, 0, 128));
    physical.BlendPixel(0, 0, Rgb(0, 0, 200, 128));
    assert(image.Alpha(0, 0) == 192);
    assert(image.surface.origin[1] == 133 && image.surface.origin[3] == 67);
    image.Clear();
    const u8 mask[] = { 0, 255 };
    c.DrawMask(12, 22, mask, 2, 1, 2, 1, Rgb(255, 255, 255));
    assert(image.Alpha(6, 6) > 0 && image.Alpha(6, 6) < image.Alpha(8, 6));
    image.Clear();
    c.Line(12, 22, 30, 22, 1, Rgb(255, 255, 255));
    assert(image.Alpha(25, 5) > 0 && image.Alpha(25, 10) == 0);
    image.Clear();
    c.FillTriangle(12, 22, 30, 22, 12, 40, Rgb(255, 255, 255));
    assert(image.Alpha(8, 8) == 255 && image.Alpha(55, 55) == 0);
    c.SetClip(Rect{ 12, 22, 2, 2 });
    c.FillRect(Rect{ -100, -100, 200, 200 }, Rgb(255, 255, 255));
    image.Guards();
}

static int Index(u32) { return 0; }
static void Widths(int, s32 &left, s32 &width, s32 &advance) { left = 0; width = 4; advance = 6; }
static void TextLayout()
{
    std::array<u8, 32 * 16 / 2> sheet{};
    // Solid 3x6 ink within an 8x8 native cell at (1,1).
    for (u32 y = 2; y < 8; y++) for (u32 x = 2; x < 5; x++)
    {
        const u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
                    | ((x & 4) << 2) | ((y & 4) << 3);
        const u32 at = ((y / 8) * 4 + x / 8) * 64 + m;
        sheet[at / 2] |= (u8)(15 << ((at & 1) * 4));
    }
    const FontSource font{ sheet.data(), (u32)sheet.size(), 1, SheetFormatA4, 32, 16, 2, 1, 8, 8, 7, 10, Index, Widths };
    SetSystemFont(&font);
    for (Font face : { Font::Small, Font::System, Font::Large })
        for (Align align : { Align::Left, Align::Center, Align::Right })
        {
            TextStyle style; style.font = face; style.align = align; style.color = Rgb(255, 255, 255);
            s32 widths[2] = {};
            int n = 0;
            for (float scale : { 1.f, HiResScale })
            {
                Image whole((s32)(80 * scale), (s32)(24 * scale), scale != 1.f);
                Image separate((s32)(80 * scale), (s32)(24 * scale), scale != 1.f);
                Canvas a(whole.surface, 0, 0, scale), b(separate.surface, 0, 0, scale);
                const Rect box{ 4, 2, 70, 20 };
                widths[n++] = DrawText(a, "AA", box, style);
                const s32 advance = MeasureText("A", style), height = LineHeight(style);
                const s32 x = align == Align::Left ? box.x : align == Align::Center
                    ? box.x + (box.w - 2 * advance) / 2 : box.Right() - 2 * advance;
                TextStyle left = style; left.align = Align::Left;
                for (int i = 0; i < 2; i++)
                    DrawText(b, "A", Rect{ x + i * advance, box.y + (box.h - height) / 2, advance, height }, left);
                if (face != Font::Small) assert(whole.data == separate.data);
                whole.Guards(); separate.Guards();
            }
            assert(widths[0] == widths[1] && widths[0] == MeasureText("AA", style));
        }
    SetSystemFont(nullptr);
}

static void AtlasAndGeometry()
{
    Image first(8, 8), second(8, 8);
    Canvas(first.surface).FillRect(Rect{ 0, 0, 8, 8 }, Rgb(255, 0, 0));
    Canvas(second.surface).FillRect(Rect{ 0, 0, 8, 8 }, Rgb(0, 255, 0));
    std::array<u8, 16 * 16 * 4> atlas{};
    assert(TileQuadRegion(first.surface, Rect{ 0, 0, 8, 8 }, atlas.data(), 16, 16));
    assert(TileQuadRegion(second.surface, Rect{ 8, 8, 8, 8 }, atlas.data(), 16, 16));
    for (size_t i = 0; i < 64; i++)
    {
        assert(atlas[i * 4 + 3] == 255 && atlas[i * 4 + 2] == 0);
        assert(atlas[(192 + i) * 4 + 2] == 255 && atlas[(192 + i) * 4 + 3] == 0);
    }
    assert(!TileQuadRegion(first.surface, Rect{ 9, 9, 8, 8 }, atlas.data(), 16, 16));
    float vertices[48] = {};
    QuadVertices(vertices, Rect{ 12, 8, 376, 224 }, Rect{ 0, 0, 940, 560 }, 1024, 1024, 1.4f, true);
    QuadVertices(vertices + 24, Rect{ 196, 8, 196, 26 }, Rect{ 0, 600, 490, 65 }, 1024, 1024, 1.4f, true);
    const float x = 200.f * (1.f - vertices[1]);
    assert(std::fabs(x - (200.f + (12.f - 200.f) / 1.4f)) < .001f);
    assert(vertices[4] == 0.f && vertices[5] == 1.f);
    assert(vertices[29] == 1.f - 600.f / 1024.f);
}

static bool ready = true, uploaded = false, dirtySeen = false, accept = true;
static unsigned hidden = 0;
static u32 generation = 0;
static u32 Generation() { return generation; }
static Image staging(50, 50);
static bool Ready() { return ready; }
static bool Setup(u32, s32 w, s32 h, Surface &s) { assert(w == 50 && h == 50); s = staging.surface; return true; }
static bool Present(u32, const Surface &, const Rect &, bool dirty)
{ uploaded = true; dirtySeen = dirty; return accept; }
static void Hide(u32) { hidden++; }
static void LayerLifecycle()
{
    Layer::s_hiRes = HiResBackend{ Ready, Setup, Present, Hide, Generation };
    std::vector<u8> screenBytes(400 * 240 * 3, 0x37), fallback(20 * 20 * 3);
    Surface screen{ screenBytes.data(), 3, 400 * 3, PixelFormat::BGR8, 400, 240 };
    Layer layer;
    const Rect area{ 10, 20, 20, 20 };
    assert(layer.SetupHiRes(screen, area, 6, 0, fallback.data(), fallback.size()));
    Canvas canvas = layer.Begin(); assert(canvas.Scale() == HiResScale);
    canvas.FillRect(area, Rgb(255, 255, 255));
    accept = false;
    assert(!layer.Present(screen) && uploaded && dirtySeen);
    assert(staging.Alpha(0, 0) == 0 && staging.Alpha(25, 25) == 255);
    assert(!layer.Present(screen) && dirtySeen); // failed upload must retry
    accept = true;
    assert(!layer.Present(screen) && dirtySeen);
    assert(!layer.Present(screen) && !dirtySeen); // unchanged content doesn't upload
    assert(std::all_of(screenBytes.begin(), screenBytes.end(), [](u8 v) { return v == 0x37; }));
    generation++; assert(!layer.Matches(screen)); // test on/off between callbacks must redraw
    assert(layer.SetupHiRes(screen, area, 6, 0, fallback.data(), fallback.size()));
    layer.Begin().FillRect(area, Rgb(255, 255, 255));
    assert(!layer.Present(screen) && dirtySeen);
    ready = false; assert(!layer.Matches(screen));
    assert(layer.SetupHiRes(screen, area, 6, 0, fallback.data(), fallback.size()));
    assert(hidden > 0 && layer.Begin().Scale() == 1.f);
    assert(layer.Present(screen));
    ready = true; assert(!layer.Matches(screen));
    Layer::s_hiRes = {};
}

int main()
{
    ScaleOne(); Primitives(); TextLayout(); AtlasAndGeometry(); LayerLifecycle();
    std::puts("hires: scale-1, primitives, masks, layout, atlas and live fallback OK");
}
