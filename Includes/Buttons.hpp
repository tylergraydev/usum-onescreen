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
    // injected, after capture. Used to follow what the game itself reacts to.
    u32 ButtonsGameHeld(void);

    // Input capture for plugin menus: while begun, the game's pad and touch
    // readers no longer see `keys`, the circle pad (zeroed, direction bits
    // cleared) and/or touch. Injected test input is captured the same way.
    // Calling Begin again replaces the set. A key still held when it leaves
    // the set stays hidden until it is released, so the game never sees a
    // fresh press of it; touch likewise until lifted.
    void InputCaptureBegin(u32 keys, bool captureStick, bool captureTouch);
    void InputCaptureEnd(void);

    // The current request as one word: keys, plus CaptureStickBits and
    // CaptureTouchBit (InputCapture.hpp). Read by the touch hook.
    u32 InputCaptureRequest(void);

    // Real input for plugin menus while captured: the game's latest pad read
    // before masking, so injected `press`/`stick` commands drive menus too.
    // Pressed is the edge since the previous ButtonsUpdate. These follow the
    // game's own reads; if the game stops reading the pad they go stale, and
    // Controller (CTRPF, reads HID directly) is the fallback.
    u32 CaptureRealHeld(void);
    u32 CaptureRealPressed(void);
    void CaptureRealStick(s32 &x, s32 &y);

    // A combo the plugin takes for itself (pc_key): the pad hook counts each
    // time all of `keys` become held together, and hides those keys from the
    // game from then until each is released. 0 turns it off. The count only
    // grows; compare it with the last value seen.
    void ButtonsSetSaveCombo(u32 keys);
    u32 ButtonsSaveComboHits(void);
    void ButtonsSetCombo(u32 keys);
    u32 ButtonsComboHits(void);

    // Keys held by the action layer (Actions.hpp), 0 = none. Applied after
    // capture, so an action can press keys while the player's are captured.
    void ButtonsActionSet(u32 keys);
}
