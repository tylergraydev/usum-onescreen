// Host test for the USUM theme (Sources/UI/Theme.cpp, ThemeDemo.cpp) and the
// game asset decoders (Sources/UI/GameAssets.cpp). Every input is synthetic:
// LZ11 streams, SARC archives, BFLIM textures and an FFNT font built here, so
// no game data is needed (or in git). Writes the demo screen with and without
// (synthetic) game assets as PPM previews under the given directory.

#include "hires_screen_smoke.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "UI/GameAssets.hpp"
#include "UI/Theme.hpp"
#include "UI/ThemeDemo.hpp"

using namespace OneScreen::UI;

static int s_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            s_failures++;                                                        \
        }                                                                        \
    } while (0)

// --- Byte builders ---

static void Put16(std::vector<u8> &b, size_t at, u32 v)
{
    if (b.size() < at + 2)
        b.resize(at + 2);
    b[at] = (u8)v;
    b[at + 1] = (u8)(v >> 8);
}

static void Put32(std::vector<u8> &b, size_t at, u32 v)
{
    Put16(b, at, v & 0xFFFF);
    Put16(b, at + 2, v >> 16);
}

static void PutStr(std::vector<u8> &b, size_t at, const char *s)
{
    if (b.size() < at + std::strlen(s))
        b.resize(at + std::strlen(s));
    std::memcpy(&b[at], s, std::strlen(s));
}

// Position of texel (x, y) in an 8x8-tiled, Morton-ordered texture.
static u32 TexelIndex(u32 x, u32 y, u32 texW)
{
    const u32 tile = (y / 8) * (texW / 8) + x / 8;
    const u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return tile * 64 + m;
}

// --- LZ11 ---

static void TestLz11(void)
{
    std::vector<u8> out;
    // "ABC" then a 9-byte copy 3 back.
    const u8 a[] = { 0x11, 0x0C, 0, 0, 0x10, 'A', 'B', 'C', 0x80, 0x02 };
    CHECK(Lz11Decompress(a, sizeof(a), out));
    CHECK(std::string(out.begin(), out.end()) == "ABCABCABCABC");
    // The long form (kind 0): 'x' then 20 copies 1 back.
    const u8 b[] = { 0x11, 21, 0, 0, 0x40, 'x', 0x00, 0x30, 0x00 };
    CHECK(Lz11Decompress(b, sizeof(b), out));
    CHECK(out.size() == 21 && out[0] == 'x' && out[20] == 'x');
    // A copy from before the start, a cut stream, the wrong type.
    const u8 c[] = { 0x11, 3, 0, 0, 0x80, 0x20, 0x05 };
    CHECK(!Lz11Decompress(c, sizeof(c), out));
    CHECK(!Lz11Decompress(a, sizeof(a) - 1, out));
    const u8 d[] = { 0x10, 1, 0, 0, 0, 'z' };
    CHECK(!Lz11Decompress(d, sizeof(d), out));
}

// --- SARC and packs ---

static std::vector<u8> MakeAlyt(void)
{
    // A 16-byte stand-in for the ALYT header, then a SARC with two files.
    std::vector<u8> b;
    PutStr(b, 0, "ALYT");
    const size_t s = 16;
    const char *names[2] = { "timg/one.bflim", "blyt/two.bflyt" };
    const char *data[2] = { "ONE!", "second" };
    PutStr(b, s, "SARC");
    Put16(b, s + 4, 0x14);
    Put16(b, s + 6, 0xFEFF);
    const size_t sfat = 0x14;
    PutStr(b, s + sfat, "SFAT");
    Put16(b, s + sfat + 4, 0x0C);
    Put16(b, s + sfat + 6, 2);
    Put32(b, s + sfat + 8, 0x65);
    const size_t sfnt = sfat + 0x0C + 2 * 16;
    PutStr(b, s + sfnt, "SFNT");
    Put16(b, s + sfnt + 4, 8);
    size_t nameAt = 0;
    const size_t dataAt = 0x100;
    u32 dataOff = 0;
    for (int i = 0; i < 2; i++)
    {
        const size_t node = s + sfat + 0x0C + i * 16;
        Put32(b, node, 0x1234 + i);
        Put32(b, node + 4, 0x01000000 | (u32)(nameAt / 4));
        Put32(b, node + 8, dataOff);
        Put32(b, node + 12, dataOff + (u32)std::strlen(data[i]));
        PutStr(b, s + sfnt + 8 + nameAt, names[i]);
        b.resize(std::max(b.size(), s + sfnt + 8 + nameAt + std::strlen(names[i]) + 1));
        nameAt += (std::strlen(names[i]) + 1 + 3) / 4 * 4;
        PutStr(b, s + dataAt + dataOff, data[i]);
        dataOff += 8;
    }
    Put32(b, s + 0x0C, (u32)dataAt);
    Put32(b, s + 8, (u32)(b.size() - s));
    return b;
}

static void TestSarc(void)
{
    const std::vector<u8> alyt = MakeAlyt();
    ArchiveFile f;
    CHECK(SarcFind(alyt.data(), alyt.size(), "timg/one.bflim", f));
    CHECK(f.size == 4 && std::memcmp(&alyt[f.offset], "ONE!", 4) == 0);
    CHECK(SarcFind(alyt.data(), alyt.size(), "blyt/two.bflyt", f));
    CHECK(f.size == 6 && std::memcmp(&alyt[f.offset], "second", 6) == 0);
    CHECK(!SarcFind(alyt.data(), alyt.size(), "timg/one", f));
    CHECK(!SarcFind(alyt.data(), alyt.size(), "timg/three.bflim", f));
    CHECK(!SarcFind(alyt.data(), 12, "timg/one.bflim", f));

    // A "WD" move pack with two moves.
    std::vector<u8> wd;
    PutStr(wd, 0, "WD");
    Put16(wd, 2, 2);
    Put32(wd, 4, 16);
    Put32(wd, 8, 22);
    Put32(wd, 12, 28);
    const u8 fireFang[6] = { 9, 0, 1, 65, 95, 15 };
    wd.insert(wd.end(), fireFang, fireFang + 6);
    const u8 shortEntry[6] = { 7, 0, 2, 0, 0, 0 };
    wd.insert(wd.end(), shortEntry, shortEntry + 6);
    CHECK(MiniPackEntry(wd.data(), wd.size(), 0, f) && f.offset == 16 && f.size == 6);
    const MoveInfo m = DecodeMoveEntry(&wd[f.offset], f.size);
    CHECK(m.valid && m.type == 9 && m.category == 1 && m.power == 65 && m.accuracy == 95 && m.pp == 15);
    CHECK(MiniPackEntry(wd.data(), wd.size(), 1, f) && f.offset == 22);
    CHECK(!MiniPackEntry(wd.data(), wd.size(), 2, f));
    CHECK(!MiniPackEntry(wd.data(), 20, 1, f));
    CHECK(!DecodeMoveEntry(fireFang, 5).valid);
}

// --- BFLIM ---

static std::vector<u8> MakeFlim(const std::vector<u8> &data, u32 w, u32 h, u8 fmt, u8 flags)
{
    std::vector<u8> b = data;
    const size_t f = b.size();
    PutStr(b, f, "FLIM");
    Put16(b, f + 4, 0xFEFF);
    Put16(b, f + 6, 0x14);
    Put32(b, f + 8, 0x07020100);
    Put32(b, f + 0x0C, (u32)(f + 0x28));
    Put16(b, f + 0x10, 1);
    Put16(b, f + 0x12, 0);
    PutStr(b, f + 0x14, "imag");
    Put32(b, f + 0x18, 0x10);
    Put16(b, f + 0x1C, w);
    Put16(b, f + 0x1E, h);
    Put16(b, f + 0x20, 0x80);
    b.resize(f + 0x28);
    b[f + 0x22] = fmt;
    b[f + 0x23] = flags;
    Put32(b, f + 0x24, (u32)f);
    return b;
}

static void Pixel(const Image &im, s32 x, s32 y, u8 *out)
{
    std::memcpy(out, &im.rgba[(y * im.w + x) * 4], 4);
}

static void TestFlim(void)
{
    std::string error;
    Image im;
    u8 p[4];

    // RGBA8 (stored A, B, G, R), 5x3 in an 8x8 texture.
    std::vector<u8> tex(8 * 8 * 4, 0);
    for (u32 y = 0; y < 8; y++)
        for (u32 x = 0; x < 8; x++)
        {
            u8 *t = &tex[TexelIndex(x, y, 8) * 4];
            t[0] = 255; t[1] = 7; t[2] = (u8)(y * 40); t[3] = (u8)(x * 30);
        }
    std::vector<u8> f = MakeFlim(tex, 5, 3, 9, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    CHECK(im.w == 5 && im.h == 3);
    Pixel(im, 4, 2, p);
    CHECK(p[0] == 120 && p[1] == 80 && p[2] == 7 && p[3] == 255);
    // Transposed (flag 4): image (x, y) is texel (texW - 1 - y, x).
    for (u32 y = 0; y < 8; y++)
        for (u32 x = 0; x < 8; x++)
        {
            u8 *t = &tex[TexelIndex(x, y, 8) * 4];
            t[0] = 255; t[1] = 9; t[2] = (u8)(y * 20); t[3] = (u8)(x * 30);
        }
    f = MakeFlim(tex, 5, 3, 9, 4);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 4, 2, p);
    CHECK(p[0] == 150 && p[1] == 80 && p[2] == 9);
    Pixel(im, 0, 0, p);
    CHECK(p[0] == 210 && p[1] == 0);

    // LA4, RGBA5551, RGBA4, A4.
    std::vector<u8> la4(64, 0x5A);
    f = MakeFlim(la4, 8, 8, 2, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 3, 3, p);
    CHECK(p[0] == 0x55 && p[1] == 0x55 && p[3] == 0xAA);
    std::vector<u8> c16(128, 0);
    for (int i = 0; i < 64; i++)
        Put16(c16, i * 2, (31 << 11) | (31 << 1) | 1);
    f = MakeFlim(c16, 8, 8, 7, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 7, 7, p);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 255 && p[3] == 255);
    for (int i = 0; i < 64; i++)
        Put16(c16, i * 2, 0x1234);
    f = MakeFlim(c16, 8, 8, 8, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 1, 6, p);
    CHECK(p[0] == 0x11 && p[1] == 0x22 && p[2] == 0x33 && p[3] == 0x44);
    std::vector<u8> a4(32, 0x0F);
    f = MakeFlim(a4, 8, 8, 13, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 0, 0, p);
    CHECK(p[3] == 255);
    Pixel(im, 1, 0, p);
    CHECK(p[3] == 0);

    // ETC1: individual mode, both halves (0x88, 0x44, 0x22), table 0,
    // every pixel modifier +2. ETC1A4: the same with alpha 7.
    const uint64_t block = (8ull << 60) | (8ull << 56) | (4ull << 52) | (4ull << 48) | (2ull << 44) | (2ull << 40);
    std::vector<u8> etc;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 8; k++)
            etc.push_back((u8)(block >> (k * 8)));
    f = MakeFlim(etc, 8, 8, 10, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 5, 6, p);
    CHECK(p[0] == 0x8A && p[1] == 0x46 && p[2] == 0x24 && p[3] == 255);
    std::vector<u8> etca;
    for (int i = 0; i < 4; i++)
    {
        for (int k = 0; k < 8; k++)
            etca.push_back(0x77);
        for (int k = 0; k < 8; k++)
            etca.push_back((u8)(block >> (k * 8)));
    }
    f = MakeFlim(etca, 8, 8, 11, 0);
    CHECK(DecodeFlim(f.data(), f.size(), im, error));
    Pixel(im, 2, 1, p);
    CHECK(p[0] == 0x8A && p[3] == 0x77);

    // Broken files.
    f = MakeFlim(tex, 5, 3, 9, 0);
    f[f.size() - 0x28] = 'X';
    CHECK(!DecodeFlim(f.data(), f.size(), im, error));
    f = MakeFlim(std::vector<u8>(16, 0), 8, 8, 9, 0);
    CHECK(!DecodeFlim(f.data(), f.size(), im, error));
    f = MakeFlim(tex, 8, 8, 4, 0);   // HILO8: not supported
    CHECK(!DecodeFlim(f.data(), f.size(), im, error));
    CHECK(!DecodeFlim(f.data(), 0x20, im, error));
}

// --- FFNT ---

struct FontSpec
{
    u8  cellW = 6, cellH = 8;
    u16 cols = 16, rows = 4;
};

// Glyph g: columns [0, glyphW), pixel (c, r) set unless (r + c + g) % 3 == 0.
static u32 GlyphW(u32 g) { return g == 27 ? 0 : 2 + g % 4; }
static bool GlyphPixel(u32 g, u32 c, u32 r) { return c < GlyphW(g) && (r + c + g) % 3 != 0; }

// A font file: header, FINF, TGLP, one A4 sheet, CWDH, then three CMAPs:
// direct ('A'-'Z' -> 1-26), scan (' ' -> 27, 'é' -> 28, '?' -> 0, 'Z'+1 ->
// last glyph), table ('a' -> 29, 'b' none, 'c' -> 30).
static std::vector<u8> MakeFont(const FontSpec &s)
{
    std::vector<u8> b(0x80, 0);
    const u32 glyphs = (u32)s.cols * s.rows;
    const u32 sheetW = ((s.cellW + 1) * s.cols + 7) / 8 * 8;
    const u32 sheetH = (1 + s.rows * (s.cellH + 1) + 7) / 8 * 8;
    const u32 sheetSize = sheetW * sheetH / 2;
    PutStr(b, 0, "FFNT");
    Put16(b, 4, 0xFEFF);
    Put16(b, 6, 0x14);
    Put32(b, 8, 0x04000000);
    Put16(b, 0x10, 4);
    PutStr(b, 0x14, "FINF");
    Put32(b, 0x18, 0x20);
    b[0x1C] = 1; b[0x1D] = s.cellH; b[0x1E] = s.cellW; b[0x1F] = (u8)(s.cellH - 1);
    Put16(b, 0x20, s.cellH + 2);    // line feed
    Put16(b, 0x22, 0);              // replacement glyph
    b[0x24] = 0; b[0x25] = s.cellW; b[0x26] = (u8)(s.cellW + 1); b[0x27] = 1;
    PutStr(b, 0x34, "TGLP");
    Put32(b, 0x38, 0x20);
    Put32(b, 0x28, 0x3C);           // glyph block data
    b[0x3C] = s.cellW; b[0x3D] = s.cellH; b[0x3E] = 1; b[0x3F] = s.cellW;
    Put32(b, 0x40, sheetSize);
    Put16(b, 0x44, (u32)(s.cellH - 1));
    Put16(b, 0x46, SheetFormatA4);
    Put16(b, 0x48, s.cols);
    Put16(b, 0x4A, s.rows);
    Put16(b, 0x4C, sheetW);
    Put16(b, 0x4E, sheetH);
    Put32(b, 0x50, 0x80);

    std::vector<u8> sheet(sheetSize, 0);
    for (u32 g = 0; g < glyphs; g++)
    {
        const u32 cx = 1 + (g % s.cols) * (s.cellW + 1), cy = 1 + (g / s.cols) * (s.cellH + 1);
        for (u32 r = 0; r < s.cellH; r++)
            for (u32 c = 0; c < s.cellW; c++)
                if (GlyphPixel(g, c, r))
                {
                    const u32 i = TexelIndex(cx + c, cy + r, sheetW);
                    sheet[i / 2] |= (u8)(0xF << ((i & 1) * 4));
                }
    }
    b.insert(b.end(), sheet.begin(), sheet.end());

    const size_t cwdh = b.size();
    PutStr(b, cwdh, "CWDH");
    Put32(b, cwdh + 4, 0x10 + glyphs * 3);
    Put16(b, cwdh + 8, 0);
    Put16(b, cwdh + 10, glyphs - 1);
    Put32(b, cwdh + 12, 0);
    for (u32 g = 0; g < glyphs; g++)
    {
        b.push_back((u8)(g % 2));
        b.push_back((u8)GlyphW(g));
        b.push_back((u8)(g == 27 ? 4 : GlyphW(g) + 2));
    }
    b.resize((b.size() + 3) / 4 * 4);
    Put32(b, 0x2C, (u32)cwdh + 8);

    const size_t direct = b.size();
    const size_t scan = direct + 0x18;
    const size_t table = scan + 0x2C;
    PutStr(b, direct, "CMAP");
    Put32(b, direct + 4, 0x18);
    Put16(b, direct + 8, 'A');
    Put16(b, direct + 10, 'Z');
    Put16(b, direct + 12, 0);
    Put32(b, direct + 16, (u32)scan + 8);
    Put16(b, direct + 20, 1);
    PutStr(b, scan, "CMAP");
    Put32(b, scan + 4, 0x2C);
    Put16(b, scan + 8, 0);
    Put16(b, scan + 10, 0xFFFF);
    Put16(b, scan + 12, 2);
    Put32(b, scan + 16, (u32)table + 8);
    Put16(b, scan + 20, 4);
    const u32 pairs[4][2] = { { ' ', 27 }, { 0xE9, 28 }, { '?', 0 }, { '[', glyphs - 1 } };
    for (int k = 0; k < 4; k++)
    {
        Put16(b, scan + 22 + k * 4, pairs[k][0]);
        Put16(b, scan + 24 + k * 4, pairs[k][1]);
    }
    PutStr(b, table, "CMAP");
    Put32(b, table + 4, 0x1C);
    Put16(b, table + 8, 'a');
    Put16(b, table + 10, 'c');
    Put16(b, table + 12, 1);
    Put32(b, table + 16, 0);
    Put16(b, table + 20, 29);
    Put16(b, table + 22, 0xFFFF);
    Put16(b, table + 24, 30);
    b.resize(table + 0x1C);
    Put32(b, 0x30, (u32)direct + 8);
    Put32(b, 0x0C, (u32)b.size());
    return b;
}

static GameFont::FileRead ReaderFor(const std::vector<u8> &file, u32 *reads = nullptr)
{
    return [&file, reads](u32 at, void *dst, u32 n) {
        if (reads != nullptr)
            (*reads)++;
        if ((size_t)at + n > file.size())
            return false;
        std::memcpy(dst, &file[at], n);
        return true;
    };
}

static void TestFont(void)
{
    const std::vector<u8> file = MakeFont(FontSpec());
    std::string error;
    FfntInfo info;
    CHECK(ParseFfntHeader(file.data(), 0x60, info, error));
    CHECK(info.cellW == 6 && info.cellH == 8 && info.cellsPerRow == 16 && info.cellRows == 4 && info.sheetData == 0x80);

    GameFont font;
    CHECK(font.Load(ReaderFor(file), (u32)file.size(), error));
    CHECK(font.Ready());
    CHECK(font.GlyphCount() == 64);
    CHECK(font.IndexOf('A') == 1 && font.IndexOf('Z') == 26);
    CHECK(font.IndexOf(' ') == 27 && font.IndexOf(0xE9) == 28 && font.IndexOf('[') == 63);
    CHECK(font.IndexOf('a') == 29 && font.IndexOf('c') == 30);
    CHECK(font.IndexOf('b') == 0);          // table says none: replacement glyph
    CHECK(font.IndexOf(0x263A) == 0);       // not mapped
    s32 left, glyphW, advance;
    font.Widths(5, left, glyphW, advance);
    CHECK(left == 1 && glyphW == 3 && advance == 5);
    font.Widths(27, left, glyphW, advance);
    CHECK(glyphW == 0 && advance == 4);
    font.Widths(999, left, glyphW, advance);
    CHECK(glyphW == 6 && advance == 7);     // defaults

    // Every glyph decodes to its pattern, including ones in the last row.
    u8 cell[6 * 8];
    bool all = true;
    for (int g : { 1, 17, 28, 50, 63 })
    {
        CHECK(font.DecodeCell(g, cell));
        for (u32 r = 0; r < 8; r++)
            for (u32 c = 0; c < 6; c++)
                all &= (cell[r * 6 + c] != 0) == GlyphPixel((u32)g, c, r);
    }
    CHECK(all);
    CHECK(!font.DecodeCell(64, cell));

    // Text: advances from the width table, scaled.
    GameTextCache full(1.0f), half(0.5f);
    full.SetFont(&font);
    half.SetFont(&font);
    CHECK(full.LineHeight() == 10 && full.Ascent() == 7);
    CHECK(full.Advance('A') == (s32)GlyphW(1) + 2);
    CHECK(full.Advance(' ') == 4);
    CHECK(half.LineHeight() == 5 && half.Advance(' ') == 2);
    const Glyph *g = full.Get('B');
    CHECK(g != nullptr && g->w == (s32)GlyphW(2) && g->left == 0 && g->h == 8);
    full.Get('B');
    CHECK(full.hits == 1);

    // Scaled game-font drawing uses the original cell, while both sizes keep
    // their existing bearings/advances. Scale 1 still uses exactly its cache mask.
    for (GameTextCache *cache : { &full, &half })
        for (float scale : { 1.f, 2.5f })
        {
            const s32 w = (s32)(64 * scale), h = (s32)(24 * scale);
            std::vector<u8> actual((size_t)w * h * 4, 0), expected = actual;
            Surface sa{ actual.data(), 4, w * 4, PixelFormat::RGBA8, w, h };
            sa.transparent = scale != 1.f;
            Surface se = sa; se.origin = expected.data();
            Canvas a(sa, 0, 0, scale), e(se, 0, 0, scale);
            const s32 step = cache->Advance('A');
            const Glyph metrics = *cache->Get('A');
            for (int i = 0; i < 2; i++)
            {
                const s32 x = 4 + i * step;
                cache->Draw(a, 'A', x, 3, Rgb(255, 255, 255));
                if (scale == 1.f)
                    e.DrawMask(x + metrics.left, 3, metrics.mask, metrics.w, metrics.h, metrics.w, 1, Rgb(255, 255, 255));
                else
                {
                    u8 native[6 * 8];
                    CHECK(font.DecodeCell(font.IndexOf('A'), native));
                    const float size = cache == &full ? 1.f : .5f;
                    e.DrawMaskSized((float)(x + metrics.left), 3.f, native, (s32)GlyphW(1), 8, 6,
                                    GlyphW(1) * size, 8 * size, Rgb(255, 255, 255));
                }
            }
            CHECK(actual == expected && step == cache->Advance('A'));
        }

    // A font with more rows than the plugin keeps: only the first MaxRows
    // rows are read; characters beyond them fall back to the replacement.
    FontSpec tall;
    tall.cols = 4;
    tall.rows = 20;
    const std::vector<u8> tallFile = MakeFont(tall);
    GameFont tallFont;
    CHECK(tallFont.Load(ReaderFor(tallFile), (u32)tallFile.size(), error));
    // The highest kept index is 30 ('c'): rows 0-7 are read.
    CHECK(tallFont.GlyphCount() == 32);
    CHECK(tallFont.IndexOf('[') == 0);
    CHECK(tallFont.IndexOf('A') == 1);
    CHECK(tallFont.Bytes() < tallFile.size());

    // Broken fonts fail with a reason and leave the font empty.
    std::vector<u8> bad = file;
    bad[0] = 'X';
    CHECK(!font.Load(ReaderFor(bad), (u32)bad.size(), error) && !font.Ready() && !error.empty());
    bad = file;
    Put16(bad, 0x46, 0x8);    // A8 sheets
    CHECK(!font.Load(ReaderFor(bad), (u32)bad.size(), error));
    bad = file;
    Put32(bad, 0x30, 0x40);   // CMAP pointer into the header
    CHECK(!font.Load(ReaderFor(bad), (u32)bad.size(), error));
    CHECK(!font.Load(ReaderFor(file), 0x200, error));  // file cut short
}

// --- Theme ---

static void TestThemeHelpers(void)
{
    using namespace Theme;
    CHECK(HpLevel(51, 51) == GaugeLevel::High);
    CHECK(HpLevel(26, 51) == GaugeLevel::High);
    CHECK(HpLevel(25, 51) == GaugeLevel::Half);
    CHECK(HpLevel(11, 51) == GaugeLevel::Half);
    CHECK(HpLevel(10, 51) == GaugeLevel::Low);
    CHECK(HpLevel(0, 51) == GaugeLevel::Empty);
    CHECK(HpLevel(5, 0) == GaugeLevel::Empty);
    CHECK(PpLevel(16, 30) == GaugeLevel::High);
    CHECK(PpLevel(15, 30) == GaugeLevel::Half);
    CHECK(PpLevel(7, 30) == GaugeLevel::Low);
    CHECK(PpLevel(0, 30) == GaugeLevel::Empty);
    CHECK(GaugeFill(0, 100, 48) == 0);
    CHECK(GaugeFill(1, 1000, 48) == 1);
    CHECK(GaugeFill(999, 1000, 48) == 47);
    CHECK(GaugeFill(1000, 1000, 48) == 48);
    CHECK(GaugeFill(1200, 1000, 48) == 48);
    CHECK(GaugeFill(50, 100, 48) == 24);
    CHECK(Upper("Fighting") == "FIGHTING");
    CHECK(Upper("Pok\xC3\xA9mon \xC3\xB7") == "POK\xC3\x89MON \xC3\xB7");
    CHECK(TypeColor(9).light.r == 0xE4 && TypeColor(99).light.r == TypeColor(0).light.r);
    const Color pp0 = PpTextColor(0, 30), ppFull = PpTextColor(30, 30);
    CHECK(pp0.g != ppFull.g);
    CHECK(CategoryIconRect(2).y == 40 && CategoryIconRect(7).y == 0);
}

static Color At(const Canvas &canvas, int x, int y)
{
    Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void WritePpm(const Canvas &canvas, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        std::printf("can't write %s\n", path.c_str());
        s_failures++;
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", ScreenW, ScreenH);
    for (int y = 0; y < ScreenH; y++)
        for (int x = 0; x < ScreenW; x++)
        {
            const Color c = At(canvas, x, y);
            const u8 rgb[3] = { c.r, c.g, c.b };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("theme_test: preview written to %s\n", path.c_str());
}

// Counts pixels in r that differ from `bg`.
static int Inked(const Canvas &canvas, const Rect &r, Color bg)
{
    int n = 0;
    for (int y = r.y; y < r.Bottom(); y++)
        for (int x = r.x; x < r.Right(); x++)
        {
            const Color c = At(canvas, x, y);
            n += c.r != bg.r || c.g != bg.g || c.b != bg.b;
        }
    return n;
}

static void TestDraw(const char *previewDir)
{
    const int bpp = 4;
    std::vector<u8> pixels(ScreenW * ScreenH * bpp, 0);
    const Surface surface = { pixels.data(), bpp, ScreenW * bpp, PixelFormat::RGBA8, ScreenW, ScreenH };
    Canvas canvas(surface);
    const ThemeDemoData data = ThemeDemoSample();

    // Without game assets (the fallback look), then with synthetic ones.
    PublishGameAssets(nullptr);
    CHECK(!Theme::UsingGameFont());
    static GameAssets assets;
    const std::vector<u8> fontFile = MakeFont(FontSpec());
    std::string error;
    CHECK(assets.font.Load(ReaderFor(fontFile), (u32)fontFile.size(), error));
    for (Image &icon : assets.icons)
    {
        icon.w = icon.h = 16;
        icon.rgba.assign(16 * 16 * 4, 200);
    }
    assets.balls.assign(17, Image());
    assets.balls[4].w = assets.balls[4].h = 32;
    assets.balls[4].rgba.assign(32 * 32 * 4, 255);

    for (int withAssets = 0; withAssets < 2; withAssets++)
    {
        PublishGameAssets(withAssets ? &assets : nullptr);
        CHECK(Theme::UsingGameFont() == (withAssets == 1));
        for (s32 page = 0; page < ThemeDemoPages; page++)
        {
            const Color bg = Rgb(10, 200, 10);
            canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, bg);
            DrawThemeDemo(canvas, data, page, 1);
            CHECK(HiResScreenSmoke::Run(ThemeDemoArea, 0, [&](Canvas &hi) { DrawThemeDemo(hi, data, page, 1); }));
            // Only the demo area is touched, and all of it.
            CHECK(At(canvas, ThemeDemoArea.x - 1, 100).g == 200);
            CHECK(At(canvas, ThemeDemoArea.Right(), 100).g == 200);
            CHECK(At(canvas, ThemeDemoArea.x, 120).g != 200);
            if (previewDir != nullptr)
                WritePpm(canvas, std::string(previewDir) + "/theme-preview-" + (withAssets ? "assets" : "fallback") + "-"
                                     + std::to_string(page) + ".ppm");
        }
    }

    // A row's label is drawn in both fonts, and the selected row gets the
    // cursor at its left end.
    for (int withAssets = 0; withAssets < 2; withAssets++)
    {
        PublishGameAssets(withAssets ? &assets : nullptr);
        const Color bg = Rgb(0, 0, 0);
        canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, bg);
        const Rect row{ 20, 20, 200, Theme::RowHeight };
        Theme::DrawRowBackground(canvas, row, Theme::RowKind::List);
        const int plain = Inked(canvas, Rect{ row.x + Theme::RowTextInset, row.y + 6, 100, 16 }, At(canvas, row.x + 150, row.y + 10));
        Theme::DrawRow(canvas, row, Theme::RowKind::List, "ABC", true);
        const int text = Inked(canvas, Rect{ row.x + Theme::RowTextInset, row.y + 6, 100, 16 }, At(canvas, row.x + 150, row.y + 10));
        CHECK(text > plain);
        CHECK(Theme::MeasureText("ABC", Theme::TextSize::Body) > 0);
        const s32 w = Theme::PromptWidth(Theme::Button::A, "OK");
        CHECK(Theme::DrawButtonPrompt(canvas, 10, 100, Theme::Button::A, "OK") == w);
        CHECK(At(canvas, 10 + w + 1, 110).r == 0);
    }
    PublishGameAssets(nullptr);
}

int main(int argc, char **argv)
{
    TestLz11();
    TestSarc();
    TestFlim();
    TestFont();
    TestThemeHelpers();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("theme_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("theme_test: all passed\n");
    return 0;
}
