// Synthetic CFNT only: never load a Nintendo/game font in host tests.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <vector>
#include "UI/Font.hpp"
#include "UI/SharpFont.hpp"
#include "UI/Text.hpp"

using namespace OneScreen::UI;

static size_t allocations = 0;
void *operator new(size_t n)
{
    allocations++;
    if (void *p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

static std::array<u8, 1024> sheets{};
static int Index(u32 cp) { return cp == ' ' ? 1 : 0; }
static void Widths(int index, s32 &left, s32 &width, s32 &advance)
{ left = 0; width = index ? 0 : 14; advance = index ? 9 : 17; }

static FontSource Synthetic()
{
    // A solid 12x20 rectangle, deliberately unlike an outline letter A.
    for (int y = 6; y <= 25; y++)
        for (int x = 2; x <= 13; x++)
        {
            const int px = x + 1, py = y + 1;
            unsigned morton = 0;
            for (unsigned b = 0; b < 3; b++)
            {
                morton |= ((px >> b) & 1) << (b * 2);
                morton |= ((py >> b) & 1) << (b * 2 + 1);
            }
            const unsigned i = ((py / 8) * 4 + px / 8) * 64 + morton;
            sheets[i / 2] |= (u8)(15 << ((i & 1) * 4));
        }
    return FontSource{ sheets.data(), 512, 2, SheetFormatA4, 32, 32, 1, 1,
                       24, 30, 25, 30, Index, Widths };
}

struct Image
{
    std::vector<u8> bytes;
    Surface surface;
    Image(PixelFormat format, bool rotated = false)
        : bytes(320 * 180 * BytesPerPixel(format) + 64, 0xA5), surface{}
    {
        const int bpp = (int)BytesPerPixel(format);
        surface = Surface{ bytes.data() + 32 + (rotated ? 179 * bpp : 0),
            rotated ? 180 * bpp : bpp, rotated ? -bpp : 320 * bpp,
            format, 320, 180 };
        std::memset(bytes.data() + 32, 0, bytes.size() - 64);
        surface.transparent = format == PixelFormat::RGBA8;
    }
    void Guards() const
    {
        for (size_t i = 0; i < 32; i++)
            assert(bytes[i] == 0xA5 && bytes[bytes.size() - 1 - i] == 0xA5);
    }
};

int main()
{
    FontSource source = Synthetic();
    SetSystemFont(&source);
    std::array<u8, SharpMaskBytes + 32> sharp;
    sharp.fill(0xA5);
    s32 sw, sh;
    unsigned glyphs = 0;
    // Validate the entire packed subset, coverage quantization and crop bounds.
    for (u32 cp = 0x20; cp <= 0xFFFF; cp++)
    {
        s32 w, h;
        sharp.fill(0xA5);
        if (DecodeSharpGlyph(cp, sharp.data(), SharpMaskBytes, w, h))
        {
            glyphs++;
            assert(w > 0 && h > 0 && (size_t)w * h <= SharpMaskBytes);
            for (size_t i = 0; i < (size_t)w * h; i++) assert(sharp[i] % 17 == 0);
            const auto rowHasInk = [&](int y) {
                return std::any_of(sharp.data() + y * w, sharp.data() + (y + 1) * w,
                                   [](u8 a) { return a != 0; });
            };
            assert(rowHasInk(0) && rowHasInk(h - 1));
            for (int x : { 0, w - 1 })
            {
                bool ink = false;
                for (int y = 0; y < h; y++) ink |= sharp[y * w + x] != 0;
                assert(ink);
            }
            for (size_t i = (size_t)w * h; i < sharp.size(); i++) assert(sharp[i] == 0xA5);
        }
    }
    assert(glyphs == 101);
    for (u32 cp = '!'; cp <= '~'; cp++)
        assert(DecodeSharpGlyph(cp, sharp.data(), SharpMaskBytes, sw, sh));
    assert(DecodeSharpGlyph('A', sharp.data(), SharpMaskBytes, sw, sh));
    assert(sw > 24 && sh > 30); // actual detail exceeds the shared cell
    for (size_t i = SharpMaskBytes; i < sharp.size(); i++) assert(sharp[i] == 0xA5);
    s32 w, h;
    assert(!DecodeSharpGlyph('A', sharp.data(), 1, w, h));
    assert(w == 0 && h == 0);
    assert(!DecodeSharpGlyph(0x3042, sharp.data(), SharpMaskBytes, w, h));
    assert(!DecodeSharpGlyph(' ', sharp.data(), SharpMaskBytes, w, h));
    for (u32 cp : { 0xC9u, 0xE9u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2026u })
        assert(DecodeSharpGlyph(cp, sharp.data(), SharpMaskBytes, w, h));
    for (u32 cp : { 0x3A9u, 0x416u, 0xE0u, 0x10041u })
        assert(!DecodeSharpGlyph(cp, sharp.data(), SharpMaskBytes, w, h));
    assert(DecodeSharpGlyph('A', sharp.data(), SharpMaskBytes, sw, sh));

    for (float size : { .6f, .8f })
    {
        GlyphCache cache(size);
        cache.SetSource(&source);
        const Glyph original = *cache.Get('A');
        const std::vector<u8> originalMask(original.mask, original.mask + original.w * original.h);
        assert(original.advance == (s32)std::lround(15 * size));
        assert(cache.LineHeight() == (s32)std::lround(30 * size));
        assert(cache.Ascent() == (s32)std::lround(25 * size));
        // A warm high glyph survives without consulting the CFNT cell again.
        // Cache fills, hits and subsequent 1x recovery must allocate nothing.
        Image warm(PixelFormat::RGBA8), warmReference(PixelFormat::RGBA8);
        Canvas warmCanvas(warm.surface, 0, 0, 2.5f);
        Canvas warmRef(warmReference.surface, 0, 0, 2.5f);
        size_t before = allocations;
        cache.Draw(warmCanvas, 'A', 4, 3, 1, Rgb(255, 255, 255));
        assert(allocations == before);
        const auto saved = sheets;
        sheets.fill(0);
        std::fill(warm.bytes.begin() + 32, warm.bytes.end() - 32, 0);
        cache.Draw(warmCanvas, 'A', 4, 3, 1, Rgb(255, 255, 255));
        warmRef.DrawMaskSized((float)(4 + original.left), 3 + 6 * size,
            sharp.data(), sw, sh, sw, 12 * size, 20 * size, Rgb(255, 255, 255));
        assert(warm.bytes == warmReference.bytes);
        assert(allocations == before);
        sheets = saved;
        // High-source collisions and normal slots over the shared tail must
        // evict each other without changing any recovered legacy mask bytes.
        for (u32 cp : { 0xC9u, 0xE9u, 0x3A9u, 0x416u, 0x2019u, 65u, 69u, 73u })
        {
            cache.Draw(warmCanvas, cp, 4, 3, 1, Rgb(255, 255, 255));
            const Glyph *recovered = cache.Get(cp);
            assert(recovered->w == original.w && recovered->h == original.h);
            assert(std::equal(originalMask.begin(), originalMask.end(), recovered->mask));
        }
        assert(allocations == before);
        for (int scale : { 1, 2 })
            for (PixelFormat format : { PixelFormat::RGBA8, PixelFormat::BGR8,
                PixelFormat::RGB565, PixelFormat::RGB5A1, PixelFormat::RGBA4 })
                for (bool rotated : { false, true })
                {
                    Image actual(format, rotated), expected(format, rotated), legacy(format, rotated);
                    Canvas a(actual.surface), e(expected.surface);
                    const Color color = Rgb(180, 230, 150, 210);
                    a.SetClip(Rect{ 7, 5, 140, 120 }); e.SetClip(a.GetClip());
                    cache.Draw(a, 'A', 4, 3, scale, color);
                    e.DrawMask(4 + original.left * scale, 3, originalMask.data(),
                        original.w, original.h, original.w, scale, color);
                    assert(actual.bytes == expected.bytes); // exact legacy 1x branch

                    // Real 2.5x canvas, independent expected high-source draw.
                    std::fill(actual.bytes.begin() + 32, actual.bytes.end() - 32, 0);
                    std::fill(expected.bytes.begin() + 32, expected.bytes.end() - 32, 0);
                    Canvas high(actual.surface, 0, 0, 2.5f), ref(expected.surface, 0, 0, 2.5f);
                    cache.Draw(high, 'A', 4, 3, scale, color);
                    ref.DrawMaskSized((float)(4 + original.left * scale), 3 + 6 * size * scale,
                        sharp.data(), sw, sh, sw, 12 * size * scale, 20 * size * scale, color);
                    assert(actual.bytes == expected.bytes);
                    Canvas old(legacy.surface, 0, 0, 2.5f);
                    std::array<u8, 24 * 30> cell;
                    assert(DecodeGlyphCell(source, 0, cell.data()));
                    old.DrawMaskSized((float)(4 + original.left * scale), 3, cell.data() + 2,
                        12, 30, 24, 12 * size * scale, 30 * size * scale, color);
                    assert(actual.bytes != legacy.bytes); // not CFNT enlargement

                    // Missing glyph retains exactly the old high-canvas draw.
                    std::fill(actual.bytes.begin() + 32, actual.bytes.end() - 32, 0);
                    cache.Draw(high, 0x3042, 4, 3, scale, color);
                    assert(actual.bytes == legacy.bytes);
                    actual.Guards(); expected.Guards(); legacy.Guards();
                }
        // Repeated high draws and colliding codepoints cannot change 1x masks.
        const Glyph *after = cache.Get('A');
        assert(after->advance == original.advance && after->left == original.left);
        assert(std::equal(originalMask.begin(), originalMask.end(), after->mask));
        // Source changes must invalidate decoded masks as well as 1x entries.
        FontSource blank = source;
        const std::array<u8, 1024> blankSheets{};
        blank.sheets = blankSheets.data();
        std::fill(warm.bytes.begin() + 32, warm.bytes.end() - 32, 0);
        before = allocations;
        cache.SetSource(&blank);
        cache.Draw(warmCanvas, 'A', 4, 3, 1, Rgb(255, 255, 255));
        assert(std::all_of(warm.bytes.begin() + 32, warm.bytes.end() - 32, [](u8 a) { return a == 0; }));
        cache.SetSource(&source);
        cache.Draw(warmCanvas, 'A', 4, 3, 1, Rgb(255, 255, 255));
        assert(warm.bytes == warmReference.bytes && allocations == before);
    }

    // Integration: Text.cpp must route both styles to the new source and
    // retain logical advances (including space), height and alignment.
    for (Font font : { Font::System, Font::Large })
    {
        TextStyle style;
        style.font = font;
        const float size = font == Font::System ? .6f : .8f;
        const int advance = (int)std::lround(15 * size);
        assert(MeasureText("A A", style) == advance * 2 + (int)std::lround(9 * size));
        Image actual(PixelFormat::RGBA8), expected(PixelFormat::RGBA8);
        Canvas a(actual.surface, 0, 0, 2.5f), e(expected.surface, 0, 0, 2.5f);
        const Rect box{ 8, 6, advance, (int)std::lround(30 * size) };
        assert(DrawText(a, "A", box, style) == advance);
        const int bearing = (int)std::lround(3 * size) / 2;
        e.DrawMaskSized((float)(8 + bearing), 6 + 6 * size, sharp.data(), sw, sh, sw,
                       12 * size, 20 * size, style.color);
        assert(actual.bytes == expected.bytes);
        a.SetClip(Rect{ 8, 6, 2, 2 });
        DrawText(a, "A", box, style);
        actual.Guards();
    }
    SetSystemFont(nullptr);
    std::puts("sharp_text_test: passed (101 A4 glyphs, 1x bytes, 2.5x source, cache reuse/eviction, no allocations, metrics, fallback, guards)");
}
