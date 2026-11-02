#pragma once

#include "UI/Raster.hpp"

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

    // Draws the icon centered in box, sized to its shorter side.
    void DrawIcon(Canvas &canvas, Icon icon, const Rect &box, Color c);
}
}
