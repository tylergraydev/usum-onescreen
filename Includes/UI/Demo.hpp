#pragma once

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    static constexpr s32  DemoRows   = 5;
    static constexpr Rect DemoPanel  = { 40, 8, 320, 224 };
    static constexpr s32  DemoRadius = 10;

    // A sample menu that uses every drawing feature, for the `uidemo` test
    // command and the host test's preview image. Draws only inside DemoPanel
    // (so it can go into a Layer); selected is the highlighted row
    // (0 .. DemoRows - 1).
    void DrawDemoMenu(Canvas &canvas, s32 selected);

    // The translucent full-screen shade drawn under the menu by `uidemo shade`.
    void DrawDemoShade(Canvas &canvas);
}
}
