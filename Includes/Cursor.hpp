#pragma once

#include <string>

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // A cursor on the top screen that stands in for the touch screen. Its
    // position is kept in bottom-screen coordinates (0-319, 0-239); while it is
    // "touching", the game's touch reader returns that point.

    // Hooks the game's touch sample reader at `touchReadAddress`. Returns false
    // (and leaves the game untouched) if the hook can't be applied.
    bool StartCursor(u32 touchReadAddress, std::string &report);

    // Per-frame input from the plugin thread: C-stick moves, ZL touches.
    void CursorUpdate(void);

    // Test controls (from the command file).
    void CursorMoveTo(u32 x, u32 y);
    void CursorSetTouching(bool touching);
    void CursorTap(u32 frames);

    // Draws the cursor on the top screen where the bottom screen is shown:
    // inside the panel (left, top, scaled 1/divisor).
    void CursorDraw(const CTRPluginFramework::Screen &screen, u32 left, u32 top, u32 divisor);

    // The game's latest touch read before capture masking (real or injected
    // touch); false if not touching. See InputCaptureBegin.
    bool CaptureRealTouch(u32 &x, u32 &y);

    // Touch from the action layer (Actions.hpp): while `touching`, the game
    // reads (x, y). Applied after capture, so it works while touch is captured.
    void CursorActionTouch(bool touching, u32 x, u32 y);

    // One line for the log: cursor position, touch state, and what the game's
    // touch reader last returned.
    std::string CursorStatus(void);
}
