#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "UI/ColumnMapping.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"
#include "Screens/XMenuGrid.hpp"

using namespace OneScreen;
using namespace OneScreen::UI;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct Buffer
{
    std::vector<u8> bytes;
    Surface surface{};
    Buffer(PixelFormat format, int layout, int width = 400) : bytes(64 + 400 * 240 * BytesPerPixel(format), 0xA5)
    {
        const int bpp = (int)BytesPerPixel(format);
        surface = { bytes.data() + 32, layout == 0 ? bpp : 240 * bpp,
                    layout == 0 ? 400 * bpp : layout == 1 ? bpp : -bpp, format, width, 240 };
        if (layout == 2) surface.origin += 239 * bpp;
    }
    bool Guards() const
    {
        return std::all_of(bytes.begin(), bytes.begin() + 32, [](u8 v) { return v == 0xA5; })
            && std::all_of(bytes.end() - 32, bytes.end(), [](u8 v) { return v == 0xA5; });
    }
    u8 *At(int x, int y) { return surface.origin + x * surface.stepX + y * surface.stepY; }
};

static void TestRanges()
{
    Buffer b(PixelFormat::BGR8, 2);
    for (const Rect area : { XMenuArea, Rect{0, 0, 400, 240}, Rect{8, 6, 384, 228} })
        for (float squeeze : { 16.f / 15.f, 1.4f, 2.f, std::numeric_limits<float>::max() })
        {
            b.surface.squeeze = squeeze;
            const auto r = PillarboxColumns(b.surface, area);
            CHECK(r.active);
            for (int x = 0; x < 400; x++)
            {
                const int source = SourceColumn(x, squeeze);
                CHECK((x >= r.begin && x < r.end) == (source >= area.x && source < area.Right()));
            }
        }
    b.surface.squeeze = 16.f / 15.f;
    auto r = PillarboxColumns(b.surface, XMenuArea);
    CHECK(r.begin == 24 && r.end == 376);
    b.surface.squeeze = 1.4f;
    r = PillarboxColumns(b.surface, XMenuArea);
    CHECK(r.begin == 66 && r.end == 334);
}

static void TestPixels()
{
    for (const auto format : { PixelFormat::BGR8, PixelFormat::RGBA8, PixelFormat::RGB565, PixelFormat::RGB5A1, PixelFormat::RGBA4 })
        for (int layout : { 0, 1, 2 })
            for (int width : { 400, 320 })
                for (float squeeze : { 1.f, 16.f / 15.f, 1.4f })
                    for (const auto mode : { Pillarbox::Tint, Pillarbox::Pattern, Pillarbox::Off })
                        for (const Rect area : { XMenuArea, Rect{0, 0, 400, 240}, Rect{170, 182, 226, 54}, Rect{12, 6, 376, 182}, Rect{296, 4, 100, 22}, Rect{0, 0, 320, 240} })
                        {
                            if (area.Right() > width) continue;
                            Buffer b(format, layout, width), reference(format, layout, width);
                            b.surface.squeeze = reference.surface.squeeze = squeeze;
                            b.surface.pillarbox = mode;
                            reference.surface.pillarbox = Pillarbox::Off;
                            Layer layer;
                            std::vector<u8> pixels(Layer::BytesFor(b.surface, area));
                            CHECK(layer.Setup(b.surface, area, 12, pixels.data(), pixels.size()));
                            auto canvas = layer.Begin();
                            canvas.FillRect(area, Rgb(90, 170, 210));
                            const Rect skip{180, 50, 40, 100};
                            layer.Present(reference.surface, skip);
                            layer.Present(b.surface, skip);
                            const auto r = PillarboxColumns(b.surface, area);
                            const int bpp = (int)BytesPerPixel(format);
                            // Test the eligibility independently of the range helper.
                            const bool enabled = width == 400 && squeeze > 1 && mode != Pillarbox::Off
                                && area.x <= 12 && area.y <= 8 && area.Right() >= 388 && area.Bottom() >= 232;
                            CHECK(r.active == enabled);
                            for (int x = 0; x < width; x++)
                                for (int y = 0; y < 240; y++)
                                {
                                    const bool band = enabled && (SourceColumn(x, squeeze) < area.x || SourceColumn(x, squeeze) >= area.Right());
                                    if (!band) CHECK(std::memcmp(b.At(x,y), reference.At(x,y), bpp) == 0);
                                    else
                                    {
                                        const int distance = x < r.begin ? r.begin - 1 - x : x - r.end;
                                        const int extra = distance < 3 ? (3 - distance) * 4
                                            : mode == Pillarbox::Pattern && ((x + y) & 7) < 2 ? 3 : 0;
                                        u8 expected[4];
                                        const auto fill = Theme::WindowFill;
                                        WritePixel(expected, format, (u8)(fill.r / 2 + extra), (u8)(fill.g / 2 + extra), (u8)(fill.b / 2 + extra));
                                        CHECK(std::memcmp(b.At(x,y), expected, bpp) == 0);
                                    }
                                }
                            CHECK(b.Guards() && reference.Guards());
                        }
}

static void Preview(const std::string &directory, Pillarbox mode, const char *name)
{
    Buffer b(PixelFormat::BGR8, 2);
    b.surface.squeeze = 1.4f;
    b.surface.pillarbox = mode;
    Canvas scene(b.surface);
    scene.FillRect({0, 0, 400, 240}, Rgb(70, 95, 85));
    Layer layer;
    std::vector<u8> pixels(Layer::BytesFor(b.surface, XMenuArea));
    CHECK(layer.Setup(b.surface, XMenuArea, XMenuRadius, pixels.data(), pixels.size()));
    auto canvas = layer.Begin();
    DrawXMenu(canvas, XMenuView{});
    layer.Present(b.surface);
    for (int width : {400, 560})
    {
        const std::string path = directory + "/pillarbox-" + name + (width == 560 ? "-stretched.ppm" : ".ppm");
        FILE *file = std::fopen(path.c_str(), "wb");
        CHECK(file != nullptr);
        if (!file) continue;
        std::fprintf(file, "P6\n%d 240\n255\n", width);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < width; x++)
            {
                u8 rgb[3];
                ReadPixel(b.At(x * 400 / width, y), b.surface.format, rgb[0], rgb[1], rgb[2]);
                CHECK(std::fwrite(rgb, 1, 3, file) == 3);
            }
        CHECK(std::fclose(file) == 0);
        std::printf("wrote %s\n", path.c_str());
    }
}

int main(int argc, char **argv)
{
    TestRanges();
    TestPixels();
    if (argc > 1)
    {
        Preview(argv[1], Pillarbox::Tint, "tint");
        Preview(argv[1], Pillarbox::Pattern, "pattern");
    }
    std::printf("pillarbox: %d failures\n", failures);
    return failures ? 1 : 0;
}
