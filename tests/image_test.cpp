#include "UI/Raster.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace OneScreen::UI;

static void NativeBytes()
{
    const u8 rgba[] = { 22, 63, 192, 255, 255, 8, 77, 126, 11, 200, 19, 0,
                        100, 4, 255, 12, 90, 80, 70, 200, 33, 66, 99, 255 };
    const ImageView image{ rgba, 3, 2, 12 };
    const Color tint = Rgb(111, 222, 39, 177);
    for (int format = 0; format <= 4; format++)
        for (bool rotate : { false, true })
        {
            const PixelFormat f = (PixelFormat)format;
            const s32 bpp = (s32)BytesPerPixel(f), w = rotate ? 2 : 3, h = rotate ? 3 : 2;
            std::vector<u8> expected(8 * 8 * bpp, 91), actual(expected);
            Surface a{ expected.data(), bpp, 8 * bpp, f, 8, 8 };
            Surface b{ actual.data(), bpp, 8 * bpp, f, 8, 8 };
            Canvas reference(a, 10, 20), result(b, 10, 20);
            reference.SetClip(Rect{ 12, 21, 3, 3 }); result.SetClip(reference.GetClip());
            for (s32 y = 0; y < h; y++)
                for (s32 x = 0; x < w; x++)
                {
                    const s32 sx = rotate ? 2 - y : x, sy = rotate ? x : y;
                    const u8 *p = rgba + (sy * 3 + sx) * 4;
                    reference.BlendPixel(11 + x, 21 + y,
                        Rgb((u8)(p[0] * tint.r / 255), (u8)(p[1] * tint.g / 255),
                            (u8)(p[2] * tint.b / 255)), (u8)(p[3] * tint.a / 255));
                }
            result.DrawImage(image, Rect{ 0, 0, 3, 2 }, Rect{ 11, 21, w, h },
                             ImageFilter::Bilinear, tint, rotate);
            assert(expected == actual);
        }
}

static void ScaledPixels()
{
    constexpr s32 guard = 32, w = 15, h = 10;
    std::vector<u8> storage(guard + w * h * 4 + guard, 0xCD);
    std::fill(storage.begin() + guard, storage.end() - guard, (u8)0);
    Surface surface{ storage.data() + guard, 4, w * 4, PixelFormat::RGBA8, w, h };
    surface.transparent = true;
    Canvas canvas(surface, 10, 20, 2.5f);
    // Pixel art is sampled at destination centres: every pixel belongs to
    // exactly one texel, including the half-physical-pixel logical boundary.
    const u8 rgba[] = { 255, 0, 0, 255, 0, 255, 0, 255 };
    canvas.DrawImage(ImageView{ rgba, 2, 1, 8 }, Rect{ 0, 0, 2, 1 }, Rect{ 10, 20, 2, 2 },
                     ImageFilter::Nearest);
    for (s32 y = 0; y < 5; y++)
        for (s32 x = 0; x < 5; x++)
        {
            const u8 *p = surface.origin + y * surface.stepY + x * 4;
            assert(p[0] == 255);
            assert((p[3] == 255 && p[2] == 0) || (p[3] == 0 && p[2] == 255));
        }
    // A transparent black texel must not darken a translucent red edge.
    const u8 edge[] = { 255, 0, 0, 255, 0, 0, 0, 0 };
    canvas.DrawImage(ImageView{ edge, 2, 1, 8 }, Rect{ 0, 0, 2, 1 }, Rect{ 12, 20, 2, 2 });
    bool translucent = false;
    for (s32 x = 5; x < 10; x++)
    {
        const u8 *p = surface.origin + x * 4;
        if (p[0] != 0) assert(p[3] == 255 && p[2] == 0 && p[1] == 0);
        translucent = translucent || (p[0] > 0 && p[0] < 255);
    }
    assert(translucent);
    // Clamp filtering to a crop instead of borrowing the neighbouring icon.
    canvas.DrawImage(ImageView{ rgba, 2, 1, 8 }, Rect{ 1, 0, 1, 1 }, Rect{ 14, 20, 2, 2 });
    for (s32 x = 10; x < 15; x++) assert(surface.origin[x * 4 + 2] == 255);
    Color c{};
    assert(!canvas.GetPixel(16, 20, c));
    for (s32 i = 0; i < guard; i++)
    {
        assert(storage[i] == 0xCD);
        assert(storage[storage.size() - 1 - i] == 0xCD);
    }
}

static void PackedPixels()
{
    // Exact 31 -> 255 expansion, one transparent packed texel.
    const u8 packed[] = { 1, 0xF8, 0xC0, 7 };
    std::vector<u8> pixels(5 * 5 * 4);
    Surface surface{ pixels.data(), 4, 20, PixelFormat::RGBA8, 5, 5 };
    surface.transparent = true;
    Canvas canvas(surface, 0, 0, 2.5f);
    canvas.DrawImage(ImageView{ packed, 2, 1, 4, ImageFormat::RGB5A1 },
                     Rect{ 0, 0, 2, 1 }, Rect{ 0, 0, 2, 2 }, ImageFilter::Nearest,
                     Rgb(255, 255, 255, 123));
    assert(pixels[0] == 123 && pixels[3] == 255);
    assert(pixels[4 * 4] == 0);
}

int main()
{
    NativeBytes(); ScaledPixels(); PackedPixels();
    std::puts("image_test: ok");
}
