#pragma once

#include <string>
#include <3ds/types.h>

namespace OneScreen
{
    // Install once after DetectGame/VerifyHooks succeed; reports both hook results.
    bool StartWidescreen(std::string &report);

    // Zero disables rewriting; positive finite values select the active aspect.
    // Returns aspect and template status log lines. May be set before the hook is installed.
    std::string SetWidescreenAspect(float aspect);
    // Horizontal stretch relative to stock 5:3; 1 when off.
    float WidescreenSqueeze(void);

    // Applied before the game calculates this pane's matrix; no restore needed.
    void SetGamePaneScaleX(u32 pane, u32 vtable, float scaleX);
    void ClearGamePaneScaleX(void);
    std::string DescribeGamePaneScaleX(void);

    // Capture unique culled panes until the next plugin frame callback (max 32).
    void RequestWidescreenCullNames(void);
    // Call once per frame before polling commands; returns captured log lines.
    std::string WidescreenFrame(void);

    // Active value (0 = off), perspective rewrites and pane culls since boot.
    std::string DescribeWidescreenAspect(void);
}
