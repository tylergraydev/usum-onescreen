#include "UI/Text.hpp"

#include <algorithm>
#include <cstring>

namespace OneScreen
{
namespace UI
{
    // 5x7 glyphs for ASCII 0x20-0x7E, one byte per column, bit 0 = top row.
    static const u8 s_small[95][5] =
    {
        { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x5F, 0x00, 0x00 }, { 0x00, 0x07, 0x00, 0x07, 0x00 }, // space ! "
        { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, { 0x23, 0x13, 0x08, 0x64, 0x62 }, // # $ %
        { 0x36, 0x49, 0x56, 0x20, 0x50 }, { 0x00, 0x00, 0x07, 0x00, 0x00 }, { 0x00, 0x1C, 0x22, 0x41, 0x00 }, // & ' (
        { 0x00, 0x41, 0x22, 0x1C, 0x00 }, { 0x2A, 0x1C, 0x7F, 0x1C, 0x2A }, { 0x08, 0x08, 0x3E, 0x08, 0x08 }, // ) * +
        { 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x08, 0x08, 0x08, 0x08, 0x08 }, { 0x00, 0x60, 0x60, 0x00, 0x00 }, // , - .
        { 0x20, 0x10, 0x08, 0x04, 0x02 }, { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 }, // / 0 1
        { 0x72, 0x49, 0x49, 0x49, 0x46 }, { 0x21, 0x41, 0x49, 0x4D, 0x33 }, { 0x18, 0x14, 0x12, 0x7F, 0x10 }, // 2 3 4
        { 0x27, 0x45, 0x45, 0x45, 0x39 }, { 0x3C, 0x4A, 0x49, 0x49, 0x31 }, { 0x41, 0x21, 0x11, 0x09, 0x07 }, // 5 6 7
        { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x46, 0x49, 0x49, 0x29, 0x1E }, { 0x00, 0x36, 0x36, 0x00, 0x00 }, // 8 9 :
        { 0x00, 0x56, 0x36, 0x00, 0x00 }, { 0x08, 0x14, 0x22, 0x41, 0x00 }, { 0x14, 0x14, 0x14, 0x14, 0x14 }, // ; < =
        { 0x00, 0x41, 0x22, 0x14, 0x08 }, { 0x02, 0x01, 0x59, 0x09, 0x06 }, { 0x3E, 0x41, 0x5D, 0x59, 0x4E }, // > ? @
        { 0x7C, 0x12, 0x11, 0x12, 0x7C }, { 0x7F, 0x49, 0x49, 0x49, 0x36 }, { 0x3E, 0x41, 0x41, 0x41, 0x22 }, // A B C
        { 0x7F, 0x41, 0x41, 0x41, 0x3E }, { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 }, // D E F
        { 0x3E, 0x41, 0x41, 0x51, 0x73 }, { 0x7F, 0x08, 0x08, 0x08, 0x7F }, { 0x00, 0x41, 0x7F, 0x41, 0x00 }, // G H I
        { 0x20, 0x40, 0x41, 0x3F, 0x01 }, { 0x7F, 0x08, 0x14, 0x22, 0x41 }, { 0x7F, 0x40, 0x40, 0x40, 0x40 }, // J K L
        { 0x7F, 0x02, 0x1C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F }, { 0x3E, 0x41, 0x41, 0x41, 0x3E }, // M N O
        { 0x7F, 0x09, 0x09, 0x09, 0x06 }, { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 }, // P Q R
        { 0x26, 0x49, 0x49, 0x49, 0x32 }, { 0x03, 0x01, 0x7F, 0x01, 0x03 }, { 0x3F, 0x40, 0x40, 0x40, 0x3F }, // S T U
        { 0x1F, 0x20, 0x40, 0x20, 0x1F }, { 0x3F, 0x40, 0x38, 0x40, 0x3F }, { 0x63, 0x14, 0x08, 0x14, 0x63 }, // V W X
        { 0x03, 0x04, 0x78, 0x04, 0x03 }, { 0x61, 0x59, 0x49, 0x4D, 0x43 }, { 0x00, 0x7F, 0x41, 0x41, 0x41 }, // Y Z [
        { 0x02, 0x04, 0x08, 0x10, 0x20 }, { 0x41, 0x41, 0x41, 0x7F, 0x00 }, { 0x04, 0x02, 0x01, 0x02, 0x04 }, // \ ] ^
        { 0x40, 0x40, 0x40, 0x40, 0x40 }, { 0x00, 0x01, 0x02, 0x04, 0x00 }, { 0x20, 0x54, 0x54, 0x78, 0x40 }, // _ ` a
        { 0x7F, 0x28, 0x44, 0x44, 0x38 }, { 0x38, 0x44, 0x44, 0x44, 0x28 }, { 0x38, 0x44, 0x44, 0x28, 0x7F }, // b c d
        { 0x38, 0x54, 0x54, 0x54, 0x18 }, { 0x00, 0x08, 0x7E, 0x09, 0x02 }, { 0x0C, 0x52, 0x52, 0x52, 0x3E }, // e f g
        { 0x7F, 0x08, 0x04, 0x04, 0x78 }, { 0x00, 0x44, 0x7D, 0x40, 0x00 }, { 0x20, 0x40, 0x40, 0x3D, 0x00 }, // h i j
        { 0x7F, 0x10, 0x28, 0x44, 0x00 }, { 0x00, 0x41, 0x7F, 0x40, 0x00 }, { 0x7C, 0x04, 0x78, 0x04, 0x78 }, // k l m
        { 0x7C, 0x08, 0x04, 0x04, 0x78 }, { 0x38, 0x44, 0x44, 0x44, 0x38 }, { 0x7C, 0x14, 0x14, 0x14, 0x08 }, // n o p
        { 0x08, 0x14, 0x14, 0x18, 0x7C }, { 0x7C, 0x08, 0x04, 0x04, 0x08 }, { 0x48, 0x54, 0x54, 0x54, 0x20 }, // q r s
        { 0x04, 0x3F, 0x44, 0x40, 0x20 }, { 0x3C, 0x40, 0x40, 0x20, 0x7C }, { 0x1C, 0x20, 0x40, 0x20, 0x1C }, // t u v
        { 0x3C, 0x40, 0x30, 0x40, 0x3C }, { 0x44, 0x28, 0x10, 0x28, 0x44 }, { 0x0C, 0x50, 0x50, 0x50, 0x3C }, // w x y
        { 0x44, 0x64, 0x54, 0x4C, 0x44 }, { 0x00, 0x08, 0x36, 0x41, 0x00 }, { 0x00, 0x00, 0x7F, 0x00, 0x00 }, // z { |
        { 0x00, 0x41, 0x36, 0x08, 0x00 }, { 0x02, 0x01, 0x02, 0x04, 0x02 },                                   // } ~
    };

    static constexpr s32 SmallAdvance = 6;   // 5 px glyph + 1 px gap
    static constexpr s32 SmallLine    = 8;

    // Scratch mask for one line of Small text. Only the OSD thread draws, so
    // one static buffer is enough.
    static constexpr s32 MaskW = ScreenW, MaskH = 32;
    static u8 s_mask[MaskW * MaskH];

    // System: 18 px line (font baseline 25 -> 15, so the baseline lands on a
    // whole pixel); Large: 24 px line (baseline 20).
    static GlyphCache s_system(0.6f);
    static GlyphCache s_large(0.8f);

    void SetSystemFont(const FontSource *font)
    {
        s_system.SetSource(font);
        s_large.SetSource(font);
    }

    void TextCacheStats(u32 &hits, u32 &misses)
    {
        hits = s_system.hits + s_large.hits;
        misses = s_system.misses + s_large.misses;
    }

    // The glyph cache for a system-font style, or nullptr if it draws with
    // the Small font (Small itself, or no system font set).
    static GlyphCache *CacheFor(Font font)
    {
        GlyphCache *cache = font == Font::System ? &s_system : font == Font::Large ? &s_large : nullptr;
        return cache != nullptr && cache->Source() != nullptr ? cache : nullptr;
    }

    // Scale the Small font is drawn at for a font without a system font.
    static s32 SmallScale(Font font)
    {
        return font == Font::System ? 2 : font == Font::Large ? 3 : 1;
    }

    // Number of characters (UTF-8 lead bytes), which is what the Small font draws.
    static s32 CountChars(const std::string &text)
    {
        s32 n = 0;
        for (unsigned char ch : text)
            if ((ch & 0xC0) != 0x80)
                n++;
        return n;
    }

    // Width of text at scale 1 in its font's own pixels.
    static s32 NativeWidth(const std::string &text, Font font)
    {
        if (GlyphCache *cache = CacheFor(font))
        {
            s32 w = 0;
            for (size_t i = 0; i < text.size();)
                w += cache->Advance(NextCodePoint(text.data(), text.size(), i));
            return w;
        }
        return CountChars(text) * SmallAdvance * SmallScale(font);
    }

    static s32 NativeHeight(Font font)
    {
        if (const GlyphCache *cache = CacheFor(font))
            return cache->LineHeight();
        return font == Font::System ? 18 : font == Font::Large ? 24 : SmallLine;
    }

    s32 LineHeight(const TextStyle &style)
    {
        return NativeHeight(style.font) * std::max<s32>(1, style.scale);
    }

    s32 MeasureText(const std::string &text, const TextStyle &style)
    {
        return NativeWidth(text, style.font) * std::max<s32>(1, style.scale);
    }

    std::string Ellipsize(const std::string &text, s32 maxWidth, const TextStyle &style)
    {
        if (MeasureText(text, style) <= maxWidth)
            return text;

        static const char dots[] = "...";
        std::string cut = text;
        while (!cut.empty())
        {
            // Drop the last UTF-8 character: continuation bytes, then its lead byte.
            while (!cut.empty() && (cut.back() & 0xC0) == 0x80)
                cut.pop_back();
            if (!cut.empty())
                cut.pop_back();
            while (!cut.empty() && cut.back() == ' ')
                cut.pop_back();
            if (MeasureText(cut + dots, style) <= maxWidth)
                return cut + dots;
        }
        return MeasureText(dots, style) <= maxWidth ? std::string(dots) : std::string();
    }

    std::vector<std::string> WrapText(const std::string &text, s32 width, const TextStyle &style)
    {
        std::vector<std::string> lines;
        if (width <= 0) return lines;
        std::string line, word;
        const auto append = [&]() {
            if (word.empty()) return;
            if (!line.empty() && MeasureText(line + " " + word, style) > width)
            {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += " ";
            for (size_t i = 0; i < word.size();)
            {
                const size_t start = i++;
                while (i < word.size() && ((unsigned char)word[i] & 0xC0) == 0x80) ++i;
                const std::string cp = word.substr(start, i - start);
                if (!line.empty() && MeasureText(line + cp, style) > width)
                {
                    lines.push_back(line);
                    line.clear();
                }
                line += cp;
            }
            word.clear();
        };
        for (char c : text)
        {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            {
                append();
                if (c == '\n') { lines.push_back(line); line.clear(); }
            }
            else word += c;
        }
        append();
        if (!line.empty()) lines.push_back(line);
        return lines;
    }

    void RenderSmallMask(const std::string &text, u8 *mask, s32 w, s32 h, s32 scale)
    {
        std::memset(mask, 0, (size_t)(w * h));
        scale = std::max<s32>(1, scale);
        s32 penX = 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            const unsigned char ch = text[i];
            if ((ch & 0xC0) == 0x80)
                continue;
            const u8 *glyph = s_small[(ch >= 0x20 && ch < 0x7F ? ch : '?') - 0x20];
            for (s32 col = 0; col < 5; col++)
                for (s32 row = 0; row < 7; row++)
                {
                    if (!(glyph[col] & (1 << row)))
                        continue;
                    for (s32 sy = 0; sy < scale; sy++)
                        for (s32 sx = 0; sx < scale; sx++)
                        {
                            const s32 x = penX + col * scale + sx, y = row * scale + sy;
                            if (x < w && y < h)
                                mask[y * w + x] = 255;
                        }
                }
            penX += SmallAdvance * scale;
            if (penX >= w)
                break;
        }
    }

    s32 DrawText(Canvas &canvas, const std::string &text, const Rect &box, const TextStyle &style)
    {
        const s32 scale = std::max<s32>(1, style.scale);
        const std::string fit = Ellipsize(text, box.w, style);
        if (fit.empty())
            return 0;

        const s32 width = NativeWidth(fit, style.font) * scale;
        const s32 height = NativeHeight(style.font) * scale;
        s32 x = box.x;
        if (style.align == Align::Center)
            x = box.x + (box.w - width) / 2;
        else if (style.align == Align::Right)
            x = box.Right() - width;
        const s32 y = box.y + (box.h - height) / 2;

        if (GlyphCache *cache = CacheFor(style.font))
        {
            // Glyph masks start at the top of the line, so each one is drawn
            // at the pen position plus its left bearing.
            s32 pen = x;
            for (size_t i = 0; i < fit.size();)
            {
                const u32 cp = NextCodePoint(fit.data(), fit.size(), i);
                const Glyph *g = cache->Get(cp);
                if (g->w > 0)
                {
                    if (canvas.Scale() == 1.f)
                        canvas.DrawMask(pen + g->left * scale, y, g->mask, g->w, g->h, g->w, scale, style.color);
                    else cache->Draw(canvas, cp, pen, y, scale, style.color);
                }
                pen += g->advance * scale;
            }
            return width;
        }

        const s32 small = SmallScale(style.font);
        const s32 w = std::min(width / scale, MaskW), h = std::min(height / scale, MaskH);
        if (w <= 0 || h <= 0)
            return 0;
        RenderSmallMask(fit, s_mask, w, h, small);
        canvas.DrawMask(x, y, s_mask, w, h, w, scale, style.color);
        return width;
    }
}
}
