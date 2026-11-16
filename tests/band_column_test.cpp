#include <cstdio>
#include <vector>
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"

using namespace OneScreen::UI;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

// Independent reference to the original PresentBands pixel loop, including
// format packing, vertical order and the byte immediately after the column.
static void TestBandColumn()
{
    for (const auto format : { PixelFormat::BGR8, PixelFormat::RGBA8, PixelFormat::RGB565, PixelFormat::RGB5A1, PixelFormat::RGBA4 })
        for (bool reverse : { false, true })
            for (int phase = 0; phase < 12; phase++)
            {
                const int bpp = (int)BytesPerPixel(format);
                std::vector<u8> actual(240 * bpp + 32, 0xA5), expected = actual;
                WriteBandColumn(actual.data() + 16, phase, format, reverse);
                const auto fill = Theme::WindowFill;
                for (int y = 0; y < 240; y++)
                {
                    const int extra = phase < 8 ? (((phase + y) & 7) < 2 ? 3 : 0)
                                    : phase == 8 ? 0 : (phase - 8) * 4;
                    WritePixel(expected.data() + 16 + (reverse ? 239 - y : y) * bpp, format,
                               (u8)(fill.r / 2 + extra), (u8)(fill.g / 2 + extra), (u8)(fill.b / 2 + extra));
                }
                CHECK(actual == expected);
            }
}

int main()
{
    TestBandColumn();
    std::printf("band column: %d failures\n", failures);
    return failures ? 1 : 0;
}
