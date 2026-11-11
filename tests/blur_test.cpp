// Software PICA sampling reference: actual production UVs, bilinear sampling,
// RGBA8 quantization, tap blending and final darkening. No device/assets needed.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include "UI/ColumnMapping.hpp"
#include "UI/QuadLayout.hpp"
using namespace OneScreen::UI;
using OneScreen::Pillarbox;

struct Picture
{
    int w, h;
    std::vector<u8> pixels;
    Picture(int width, int height, u8 fill = 0) : w(width), h(height), pixels((size_t)w * h, fill) {}
    u8 &At(int x, int y) { return pixels[(size_t)y * w + x]; }
    float Sample(float u, float v) const
    {
        const float x = std::clamp(u * w - .5f, 0.f, (float)w - 1);
        const float y = std::clamp((1 - v) * h - .5f, 0.f, (float)h - 1);
        const int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float fx = x - x0, fy = y - y0;
        const float a = pixels[(size_t)y0 * w + x0] * (1 - fx) + pixels[(size_t)y0 * w + x1] * fx;
        const float b = pixels[(size_t)y1 * w + x0] * (1 - fx) + pixels[(size_t)y1 * w + x1] * fx;
        return a * (1 - fy) + b * fy;
    }
};

static Picture Pyramid(Picture source, const Rect &region, int scale)
{
    for (u32 level = 0; level < BlurLevels; level++)
    {
        Picture target((int)BlurLevelSize(level) * scale, (int)BlurLevelSize(level) * scale);
        for (u32 tap = 0; tap < BlurLevelTaps(level); tap++)
        {
            float v[24];
            BlurPassVertices(v, region, level, tap);
            const float alpha = BlurTapAlpha(tap) / 255.f;
            for (int y = 0; y < target.h; y++) for (int x = 0; x < target.w; x++)
            {
                const float u = v[4] + (v[16] - v[4]) * (x + .5f) / target.w;
                const float t = v[5] + (v[11] - v[5]) * (y + .5f) / target.h;
                // Pixel-centre footprints, unlike quad edges, stay in this slot.
                if (level == 0)
                {
                    assert(u * 1024 > region.x + .5f && u * 1024 < region.Right() - .5f);
                    assert((1 - t) * 1024 > region.y + .5f && (1 - t) * 1024 < region.Bottom() - .5f);
                }
                target.At(x, y) = (u8)std::lround(source.Sample(u, t) * alpha + target.At(x, y) * (1 - alpha));
            }
        }
        source = std::move(target);
    }
    return source;
}

static Picture Bands(const Picture &texture, ColumnRange range)
{
    Picture result(400, 240);
    BlurBandRuns(range, [&](int x, int width) {
        for (int y = 0; y < 240; y++) for (int d = x; d < x + width; d++)
            result.At(d, y) = (u8)std::lround(texture.Sample((d + .5f) / 400, 1 - (y + .5f) / 240) * 128 / 255);
    });
    return result;
}

static int Difference(const Picture &p, ColumnRange range)
{
    int maximum = 0;
    BlurBandRuns(range, [&](int x, int width) {
        for (int y = 0; y < 240; y++) for (int d = x; d < x + width; d++)
        {
            const int value = p.pixels[(size_t)y * 400 + d];
            if (y > 0) maximum = std::max(maximum, std::abs(value - p.pixels[(size_t)(y - 1) * 400 + d]));
            if (d > x) maximum = std::max(maximum, std::abs(value - p.pixels[(size_t)y * 400 + d - 1]));
        }
    });
    return maximum;
}

// Regression control: the rejected sparse 3x3 full-resolution kernel must fail
// the same smoothness assertion on this input, otherwise the fixture is weak.
static Picture Sparse(const Picture &source, const Rect &r, ColumnRange range)
{
    Picture result(400, 240);
    const int dx = r.w / 40, dy = r.h / 24;
    BlurBandRuns(range, [&](int x, int width) {
        for (int y = 0; y < 240; y++) for (int d = x; d < x + width; d++)
        {
            float value = 0;
            for (int tap = 0; tap < 9; tap++)
            {
                const float sx = r.x + 2 * dx + (tap % 3 - 1) * dx + (r.w - 4 * dx) * (d + .5f) / 400;
                const float sy = r.y + 2 * dy + (tap / 3 - 1) * dy + (r.h - 4 * dy) * (y + .5f) / 240;
                const float alpha = BlurTapAlpha(tap) / 255.f;
                value = std::round(source.Sample(sx / 1024, 1 - sy / 1024) * 128 / 255 * alpha + value * (1 - alpha));
            }
            result.At(d, y) = (u8)value;
        }
    });
    return result;
}

int main()
{
    static_assert(OverlayGxCommands == 1, "one overlay submission including all reductions");
    static_assert(BlurPyramidBytes == 21760 && BlurPyramidBytes <= 32768, "scratch budget");
    static_assert((2 + 4 + 16 + 3 * 4) * 24 * sizeof(float) <= 0x2000, "VBO budget");
    for (u32 level = 0; level < BlurLevels; level++)
    {
        assert(BlurLevelOffset(level) % 256 == 0);
        assert(BlurLevelSize(level) >= 8);
    }
    // A single channel is sufficient: sampling/blending is identical for RGB,
    // and therefore bounds the luminance of arbitrary coloured detail too.
    for (const Rect r : { Rect{ 0, 0, 940, 560 }, Rect{ 0, 0, 1000, 600 }, Rect{ 20, 601, 600, 395 } })
    {
        Picture source(1024, 1024, 255); // bright unrelated allocations outside r
        for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++)
        {
            const u32 hash = (u32)x * 1664525u + (u32)y * 1013904223u;
            const int detail = (y & 1) * 130 + ((x ^ y) & 1) * 20
                + ((x % 19) < 3 ? 60 : 0) + (int)((hash >> 24) & 31);
            source.At(r.x + x, r.y + y) = (u8)std::min(255, (y < r.h / 2 ? 65 : 5) + detail);
        }
        for (float squeeze : { 16.f / 15.f, 1.4f })
        {
            Surface screen{}; screen.width = 400; screen.height = 240;
            screen.squeeze = squeeze; screen.pillarbox = Pillarbox::Blur;
            const auto range = PillarboxColumns(screen, Rect{ 12, 8, 376, 224 });
            assert(range.begin == (squeeze < 1.1f ? 24 : 66));
            assert(range.end == 400 - range.begin);
            const int old = Difference(Sparse(source, r, range), range);
            assert(old > 5);
            for (int scale : { 1, 5 })
            {
                const Picture filtered = Pyramid(source, r, scale);
                const Picture output = Bands(filtered, range);
                const int maximum = Difference(output, range);
                std::printf("blur %ldx%ld scale %d squeeze %.3f: max adjacent %d (old %d)\n",
                    (long)r.w, (long)r.h, scale, (double)squeeze, maximum, old);
                assert(maximum <= 5);
                assert(output.pixels[20 * 400 + 10] > output.pixels[220 * 400 + 10] + 20);
                for (u8 pixel : output.pixels) assert(pixel <= 128);
            }
        }
    }
    // Exact darkening; first taps overwrite stale target data, and source alpha
    // is ignored by the combiner. Region guards above prevent neighbour leaks.
    for (u8 value : { (u8)0, (u8)100, (u8)255 })
    {
        const Picture output = Bands(Pyramid(Picture(1024, 1024, value), Rect{ 0, 0, 940, 560 }, 1), { 66, 334, true });
        const int expected = (int)std::lround(value * 128.f / 255);
        for (int y = 0; y < 240; y++) assert(output.pixels[(size_t)y * 400 + 10] == expected);
    }
    // A black menu cannot pick up the bright neighbouring atlas allocation.
    Picture isolated(1024, 1024, 255);
    const Rect r{ 20, 601, 600, 395 };
    for (int y = r.y; y < r.Bottom(); y++) for (int x = r.x; x < r.Right(); x++) isolated.At(x, y) = 0;
    for (u8 pixel : Pyramid(isolated, r, 5).pixels) assert(pixel == 0);
    int runs = 0;
    BlurBandRuns({ 0, 376, true }, [&](int x, int width) { assert(x == 376 && width == 24); runs++; });
    assert(runs == 1);
    BlurBandRuns({ 66, 334, false }, [&](int, int) { runs++; }); assert(runs == 1);
    std::puts("blur: smoothness <=5, darkening, atlas isolation and fixed budgets passed");
}

