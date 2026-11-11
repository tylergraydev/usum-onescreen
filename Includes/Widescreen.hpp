#pragma once

#include <string>
#include <cstdint>
#include <cmath>
#include "PcAnywhereRules.hpp"

namespace OneScreen
{
    // Pure policy/math shared with host tests. Unknown/busy fields fail closed.
    inline bool FieldFovAllowed(const PcWorld &world, bool mapOpen)
    {
        return world.scene == "FieldRo" && world.fieldState == 0 && !mapOpen
            && !world.eventRunning && !world.festivalPlaza
            && !world.actionRunning && !world.pending;
    }

    inline bool TopPerspective(float aspect)
    {
        return std::fabs(aspect - 400.0f / 240.0f) <= 0.001f;
    }

    inline float FieldFovY(float fovy, float scale, float incomingAspect)
    {
        const float result = fovy * scale;
        // Builder uses radians. Reject degenerate projections rather than clamp.
        return TopPerspective(incomingAspect) && std::isfinite(scale)
            && scale >= 0.8f && scale <= 1.5f && std::isfinite(fovy)
            && fovy > 0.0f && std::isfinite(result) && result > 0.0f
            && result < 3.14159265358979323846f ? result : fovy;
    }

    inline float PerspectiveAspect(float incoming, float active)
    {
        return active != 0.0f && TopPerspective(incoming) ? active : incoming;
    }

    // Live setters update session settings; reload restores the INI value.
    std::string SetFieldFov(float scale);
    std::string DescribeFieldFov(void);

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
    void SetGamePaneScaleX(uint32_t pane, uint32_t vtable, uint32_t nameWord, float scaleX);
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
