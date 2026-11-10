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

    // Current Battle scene only; enables bounded fitting of top layout roots.
    void SetBattlePanelsHud(bool enabled);
    bool BattlePanelsHudActive(void);

    // Applied before matrix calculation, guarded by the vtable and name word.
    void SetGamePaneScaleX(u32 pane, u32 vtable, u32 nameWord, float scaleX);
    // Restore on this pane's next matching matrix calculation, if it still exists.
    void ClearGamePaneScaleX(void);
    std::string DescribeGamePaneScaleX(void);

    // Capture unique culled panes until the next plugin frame callback (max 32).
    void RequestWidescreenCullNames(void);
    // Call once per frame before polling commands; returns captured log lines.
    std::string WidescreenFrame(void);

    // Active value (0 = off), perspective rewrites and pane culls since boot.
    std::string DescribeWidescreenAspect(void);
}
