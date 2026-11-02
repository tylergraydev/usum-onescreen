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

    // The cursor's position in bottom-screen coordinates; true while it is
    // touching. The panel draws it.
    bool CursorGet(u32 &x, u32 &y);

    // The game's latest touch read before capture masking (real or injected
    // touch); false if not touching. See InputCaptureBegin.
    bool CaptureRealTouch(u32 &x, u32 &y);

    // One line for the log: cursor position, touch state, and what the game's
    // touch reader last returned.
    std::string CursorStatus(void);
}
