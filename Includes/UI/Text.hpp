#pragma once

#include <string>

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    enum class Font : u8
    {
        System, // the 3DS system font, drawn through CTRPF (16 px line)
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

    // How the System font is measured and turned into a coverage mask. The
    // 3DS build installs one that goes through CTRPF (UI/Device.cpp). With
    // none set, System text uses the Small font at twice the size so layouts
    // keep roughly the same metrics on the PC.
    struct FontBackend
    {
        s32 lineHeight;
        s32 (*measure)(const std::string &text);
        // Fills mask (w x h, stride w) with coverage, 0 = empty. Returns false
        // if it could not, and the text is then skipped.
        bool (*rasterize)(const std::string &text, u8 *mask, s32 w, s32 h);
    };

    void SetSystemFont(const FontBackend *backend);

    s32 LineHeight(const TextStyle &style);
    s32 MeasureText(const std::string &text, const TextStyle &style);

    // Cuts text to fit maxWidth, ending in "..." if anything was removed.
    // Cuts on UTF-8 character boundaries.
    std::string Ellipsize(const std::string &text, s32 maxWidth, const TextStyle &style);

    // Draws text in box: aligned horizontally per style.align, centered
    // vertically, ellipsized to the box width. Returns the drawn width.
    s32 DrawText(Canvas &canvas, const std::string &text, const Rect &box, const TextStyle &style);

    // Renders the Small font into a coverage mask at the given scale. Used by
    // the fallback System font and by the host test's fake backend.
    void RenderSmallMask(const std::string &text, u8 *mask, s32 w, s32 h, s32 scale);
}
}
