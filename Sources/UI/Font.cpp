#include "UI/Font.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OneScreen
{
namespace UI
{
    // Position of pixel i (0..63) inside an 8x8 Morton-ordered tile.
    static inline void MortonXY(u32 i, s32 &x, s32 &y)
    {
        x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4);
        y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4);
    }

    bool DecodeGlyphCell(const FontSource &font, int index, u8 *out)
    {
        const s32 perSheet = font.cellsPerRow * font.cellRows;
        if (font.sheetFormat != SheetFormatA4 || perSheet <= 0 || index < 0 || index / perSheet >= font.sheetCount)
            return false;
        const s32 inSheet = index % perSheet;
        const s32 cellX = 1 + (inSheet % font.cellsPerRow) * (font.cellW + 1);
        const s32 cellY = 1 + (inSheet / font.cellsPerRow) * (font.cellH + 1);
        if (cellX + font.cellW > font.sheetW || cellY + font.cellH > font.sheetH)
            return false;

        const u8 *sheet = font.sheets + (size_t)(index / perSheet) * font.sheetSize;
        const s32 tilesPerRow = font.sheetW / 8;
        // Walk the tiles the cell touches and keep the pixels inside it.
        for (s32 ty = cellY / 8; ty * 8 < cellY + font.cellH; ty++)
            for (s32 tx = cellX / 8; tx * 8 < cellX + font.cellW; tx++)
            {
                const u8 *tile = sheet + (ty * tilesPerRow + tx) * 32;
                for (u32 i = 0; i < 64; i++)
                {
                    s32 px, py;
                    MortonXY(i, px, py);
                    const s32 x = tx * 8 + px - cellX, y = ty * 8 + py - cellY;
                    if (x < 0 || y < 0 || x >= font.cellW || y >= font.cellH)
                        continue;
                    const u8 nibble = (i & 1) ? tile[i / 2] >> 4 : tile[i / 2] & 0xF;
                    out[y * font.cellW + x] = nibble * 17;
                }
            }
        return true;
    }

    void ScaleMask(const u8 *src, s32 srcW, s32 srcH, float scale, u8 *out, s32 outW, s32 outH)
    {
        const float step = 1.f / scale;
        // Source pixel weight in an output pixel = overlap length on each axis,
        // normalized by the output pixel's area in source pixels.
        const float norm = scale * scale;
        for (s32 oy = 0; oy < outH; oy++)
        {
            const float y0 = oy * step, y1 = y0 + step;
            for (s32 ox = 0; ox < outW; ox++)
            {
                const float x0 = ox * step, x1 = x0 + step;
                float sum = 0.f;
                for (s32 sy = (s32)y0; sy < srcH && sy < y1; sy++)
                {
                    const float wy = std::min(y1, sy + 1.f) - std::max(y0, (float)sy);
                    for (s32 sx = (s32)x0; sx < srcW && sx < x1; sx++)
                    {
                        const float wx = std::min(x1, sx + 1.f) - std::max(x0, (float)sx);
                        sum += wx * wy * src[sy * srcW + sx];
                    }
                }
                out[oy * outW + ox] = (u8)std::min(255.f, sum * norm + 0.5f);
            }
        }
    }

    void GlyphCache::SetSource(const FontSource *font)
    {
        if (font == m_font)
            return;
        m_font = font;
        for (Slot &slot : m_slots)
            slot.used = false;
        m_maskW = font != nullptr ? (s32)std::ceil(font->cellW * m_scale) : 0;
        m_maskH = font != nullptr ? (s32)std::ceil(font->cellH * m_scale) : 0;
        m_masks.assign((size_t)(Slots * m_maskW * m_maskH), 0);
    }

    s32 GlyphCache::Scaled(s32 v) const
    {
        return (s32)std::lround(v * m_scale);
    }

    s32 GlyphCache::LineHeight(void) const
    {
        return m_font != nullptr ? Scaled(m_font->lineFeed) : 0;
    }

    s32 GlyphCache::Ascent(void) const
    {
        return m_font != nullptr ? Scaled(m_font->baseline) : 0;
    }

    s32 GlyphCache::Advance(u32 codePoint)
    {
        const Glyph *g = Get(codePoint);
        return g != nullptr ? g->advance : 0;
    }

    const Glyph *GlyphCache::Get(u32 codePoint)
    {
        if (m_font == nullptr)
            return nullptr;
        // Latin-1 never collides; other scripts share slots.
        const u32 index = codePoint % Slots;
        Slot &slot = m_slots[index];
        u8 *mask = m_masks.data() + (size_t)index * m_maskW * m_maskH;
        if (slot.used && slot.codePoint == codePoint)
        {
            hits++;
            return &slot.glyph;
        }
        misses++;

        const FontSource &f = *m_font;
        const int glyph = f.indexOf(codePoint);
        s32 left, glyphW, advance;
        f.widths(glyph, left, glyphW, advance);

        // Scratch for one decoded cell; only the OSD thread renders glyphs.
        static u8 s_cell[64 * 64];
        static u8 s_crop[64 * 64];
        Glyph &g = slot.glyph;
        g.mask = mask;
        g.left = 0;
        g.advance = Scaled(advance);
        g.w = 0;
        g.h = m_maskH;

        // Spacing comes from the ink, not the width table: in the font Azahar
        // provides, glyphWidth and charWidth are 0-4 px wider than the ink,
        // by a different amount per glyph, so text spaced by the table looks
        // uneven (docs/UI.md). The ink is searched in the glyph's own columns
        // plus one, so a neighbour's overhang into this cell is ignored.
        s32 ink0 = -1, ink1 = -1;
        if ((s32)f.cellW * f.cellH <= (s32)sizeof(s_cell) && DecodeGlyphCell(f, glyph, s_cell))
        {
            const s32 search = std::min<s32>(f.cellW, std::max<s32>(0, glyphW) + 1);
            for (s32 x = 0; x < search; x++)
                for (s32 y = 0; y < f.cellH; y++)
                    if (s_cell[y * f.cellW + x] != 0)
                    {
                        if (ink0 < 0)
                            ink0 = x;
                        ink1 = x;
                        break;
                    }
        }
        if (ink0 >= 0)
        {
            const s32 inkW = ink1 - ink0 + 1;
            g.w = std::min<s32>(m_maskW, (s32)std::ceil(inkW * m_scale));
            g.left = Scaled(InkGap) / 2;
            g.advance = Scaled(inkW + InkGap);
            for (s32 y = 0; y < f.cellH; y++)
                std::memcpy(s_crop + y * inkW, s_cell + y * f.cellW + ink0, (size_t)inkW);
            ScaleMask(s_crop, inkW, f.cellH, m_scale, mask, g.w, g.h);
        }
        slot.codePoint = codePoint;
        slot.used = true;
        return &g;
    }

    void GlyphCache::Draw(Canvas &canvas, u32 codePoint, s32 x, s32 y, s32 scale, Color color)
    {
        const Glyph *g = Get(codePoint);
        if (g == nullptr || g->w <= 0) return;
        if (canvas.Scale() == 1.f)
        {
            canvas.DrawMask(x + g->left * scale, y, g->mask, g->w, g->h, g->w, scale, color);
            return;
        }
        u8 cell[64 * 64];
        if ((u32)m_font->cellW * m_font->cellH > sizeof(cell)
            || !DecodeGlyphCell(*m_font, m_font->indexOf(codePoint), cell)) return;
        s32 left, glyphW, advance;
        m_font->widths(m_font->indexOf(codePoint), left, glyphW, advance);
        s32 ink0 = -1, ink1 = -1;
        const s32 search = std::min<s32>(m_font->cellW, std::max<s32>(0, glyphW) + 1);
        for (s32 sx = 0; sx < search; sx++)
            for (s32 sy = 0; sy < m_font->cellH; sy++)
                if (cell[sy * m_font->cellW + sx] != 0)
                {
                    if (ink0 < 0) ink0 = sx;
                    ink1 = sx;
                    break;
                }
        if (ink0 < 0) return;
        const s32 inkW = ink1 - ink0 + 1;
        canvas.DrawMaskSized((float)(x + g->left * scale), (float)y, cell + ink0,
            inkW, m_font->cellH, m_font->cellW, inkW * m_scale * scale,
            m_font->cellH * m_scale * scale, color);
    }

    u32 NextCodePoint(const char *text, size_t size, size_t &i)
    {
        const u8 lead = (u8)text[i++];
        if (lead < 0x80)
            return lead;
        s32 extra;
        u32 cp;
        if ((lead & 0xE0) == 0xC0)      { extra = 1; cp = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { extra = 2; cp = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { extra = 3; cp = lead & 0x07; }
        else
            return 0xFFFD;
        size_t j = i;
        for (s32 k = 0; k < extra; k++, j++)
        {
            if (j >= size || ((u8)text[j] & 0xC0) != 0x80)
                return 0xFFFD;
            cp = (cp << 6) | ((u8)text[j] & 0x3F);
        }
        i = j;
        return cp;
    }
}
}
