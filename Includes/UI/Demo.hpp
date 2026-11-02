#pragma once

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    static constexpr s32 DemoRows = 5;

    // A sample menu that uses every drawing feature, for the `uidemo` test
    // command and the host test's preview image. selected is the highlighted
    // row (0 .. DemoRows - 1).
    void DrawDemoMenu(Canvas &canvas, s32 selected);
}
}
