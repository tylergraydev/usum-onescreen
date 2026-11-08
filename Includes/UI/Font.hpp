#pragma once

#include <cstddef>
#include <vector>

#include "UI/Raster.hpp"

// Glyphs from a CFNT font (the 3DS system font) turned into coverage masks.
// Pure code: the 3DS build fills a FontSource from libctru's shared font
// (UI/Device.cpp), the host test from a synthetic sheet.
namespace OneScreen
{
namespace UI
{
    // The parts of a CFNT font's TGLP (texture sheets) and FINF this code
    // uses, copied out of libctru's structs.
    struct FontSource
    {
        const u8 *sheets;       // all sheets, sheetSize bytes apart
        u32       sheetSize;
        u16       sheetCount;
        u16       sheetFormat;  // GPU_TEXCOLOR; only A4 (0xB) is supported
        u16       sheetW, sheetH;
        u16       cellsPerRow, cellRows;
        u8        cellW, cellH;
        u8        baseline;     // baseline row inside a cell
        u8        lineFeed;

        // Glyph index of a code point (the font's replacement glyph if it has
        // none), and that glyph's horizontal metrics.
        int  (*indexOf)(u32 codePoint);
        void (*widths)(int index, s32 &left, s32 &glyphW, s32 &advance);
    };

    static constexpr u16 SheetFormatA4 = 0xB;

    // Decodes one glyph cell (cellW x cellH) into out as 8-bit coverage.
    // Sheets are GPU textures: 8x8 tiles left to right, top to bottom, pixels
    // in Morton order inside a tile, 4 bits each, low nibble first. Cells sit
    // in a grid with a 1 px gap: cell (c, r) starts at (1 + c * (cellW + 1),
    // 1 + r * (cellH + 1)). Returns false for a bad index or format.
    bool DecodeGlyphCell(const FontSource &font, int index, u8 *out);

    // Area-filtered resize of a coverage mask (srcW x srcH, stride srcW) by
    // factor scale (<= 1). Output is outW x outH, stride outW; output pixel
    // (x, y) covers source [x / scale, (x + 1) / scale).
    void ScaleMask(const u8 *src, s32 srcW, s32 srcH, float scale, u8 *out, s32 outW, s32 outH);

    struct Glyph
    {
        const u8 *mask;     // w x h coverage, stride w
        s32       w, h;
        s32       left;     // from the pen position to the mask's left edge
        s32       advance;  // pen step to the next glyph
    };

    // A font at one size: renders glyphs on first use and keeps them in a
    // small direct-mapped cache, so a redraw of the same text only copies
    // masks. Sizes are a fraction of the font's own cell (scale <= 1).
    class GlyphCache
    {
    public:
        static constexpr s32 Slots = 256;

        GlyphCache(float scale) : m_scale(scale) {}

        void SetSource(const FontSource *font);
        const FontSource *Source(void) const { return m_font; }

        s32 LineHeight(void) const;
        s32 Ascent(void) const;
        // Pen step for a code point (renders it into the cache if needed:
        // spacing comes from the glyph's ink).
        s32 Advance(u32 codePoint);
        // Space between the ink of two glyphs, in font pixels.
        static constexpr s32 InkGap = 3;
        // Valid until the next Get call on this cache. nullptr if no font.
        const Glyph *Get(u32 codePoint);
        void Draw(Canvas &canvas, u32 codePoint, s32 x, s32 y, s32 scale, Color color);

        // Counters for the host test and the `uifont` log line.
        u32 hits = 0, misses = 0;

    private:
        struct Slot
        {
            u32   codePoint;
            bool  used;
            Glyph glyph;
        };

        s32 Scaled(s32 v) const;

        const FontSource *m_font = nullptr;
        float             m_scale;
        Slot              m_slots[Slots] = {};
        // Glyph masks, one scaled cell (m_maskW x m_maskH) per slot, allocated
        // when a font is set: ~69 KB at 3/5, ~123 KB at 4/5 for 24x30 cells.
        std::vector<u8>   m_masks;
        s32               m_maskW = 0, m_maskH = 0;
    };

    // Decodes the next UTF-8 character at text[i] and advances i. Invalid
    // bytes come back as U+FFFD, one byte each.
    u32 NextCodePoint(const char *text, size_t size, size_t &i);
}
}
