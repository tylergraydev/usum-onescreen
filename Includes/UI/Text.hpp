#pragma once

#include <string>
#include <vector>

#include "UI/Font.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    enum class Font : u8
    {
        System, // the 3DS system font at 3/5 size (18 px line)
        Large,  // the 3DS system font at 4/5 size (24 px line), for titles
        Small,  // built-in 5x7 ASCII font (8 px line), also used on the PC
    };

    enum class Align : u8
    {
        Left,
        Center,
        Right,
    };

    struct TextStyle
    {
        Font  font  = Font::System;
        s32   scale = 1;        // whole-pixel scale; 2 doubles the glyphs
        Color color = Rgb(255, 255, 255);
        Align align = Align::Left;
    };

    // Where System and Large glyphs come from. The 3DS build passes the
    // shared system font (UI/Device.cpp); nullptr (the default, and on the
    // PC) draws them with the Small font at 2x and 3x instead, with the same
    // line heights.
    void SetSystemFont(const FontSource *font);

    // Glyph cache counters (hits, misses) of the System and Large fonts.
    void TextCacheStats(u32 &hits, u32 &misses);

    s32 LineHeight(const TextStyle &style);
    s32 MeasureText(const std::string &text, const TextStyle &style);

    // Cuts text to fit maxWidth, ending in "..." if anything was removed.
    // Cuts on UTF-8 character boundaries.
    std::string Ellipsize(const std::string &text, s32 maxWidth, const TextStyle &style);

    // Draws text in box: aligned horizontally per style.align, centered
    // vertically, ellipsized to the box width. Returns the drawn width.
    s32 DrawText(Canvas &canvas, const std::string &text, const Rect &box, const TextStyle &style);

    // Measured word wrapping, preserving newlines and UTF-8 boundaries.
    // Overlong words split by codepoint. No height truncation.
    std::vector<std::string> WrapText(const std::string &text, s32 width, const TextStyle &style);

    // Renders the Small font into a coverage mask at the given scale. Used by
    // the fallback System font.
    void RenderSmallMask(const std::string &text, u8 *mask, s32 w, s32 h, s32 scale);
}
}
