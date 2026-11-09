#pragma once

#include "UI/Raster.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
namespace UI
{
    enum class Icon : u8
    {
        ArrowLeft,
        ArrowRight,
        ArrowUp,
        ArrowDown,
        Check,
        Close,
    };

    // Controller badges use only Canvas vector primitives, including lettering.
    void DrawControllerBadge(Canvas &canvas, const Rect &box, Theme::Button button, GlyphFamily family, const char *name);

    // Draws the icon centered in box, sized to its shorter side.
    void DrawIcon(Canvas &canvas, Icon icon, const Rect &box, Color c);
}
}
