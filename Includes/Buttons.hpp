#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // Button presses injected into the game's pad reader, for testing without a
    // controller. Uses the HID key bits (A = 1, B = 2, Select = 4, Start = 8, ...).

    // Hooks the game's pad sample reader at `padReadAddress`.
    bool StartButtons(u32 padReadAddress, std::string &report);

    // Per-frame countdown for timed presses, from the plugin thread.
    void ButtonsUpdate(void);

    // Holds `keys` for `frames` frames (0 = until released).
    void ButtonsPress(u32 keys, u32 frames);
    void ButtonsRelease(u32 keys);

    // Holds the circle pad at (x, y) for `frames` frames. Up is +y; full tilt
    // is about 156 each way.
    void ButtonsStick(s32 x, s32 y, u32 frames);

    // Parses a key name ("a", "start", "up", "zl", ...); 0 if unknown.
    u32 ButtonFromName(const std::string &name);

    std::string ButtonsStatus(void);

    // Keys held in the game's latest pad read: real input plus anything
    // injected. Used to follow what the game itself reacts to.
    u32 ButtonsGameHeld(void);
}
