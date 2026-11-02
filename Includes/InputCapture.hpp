#pragma once

// Input capture for plugin-owned menus: while a menu is open, the keys it uses
// (and optionally the circle pad and touch) are removed from what the game's
// pad and touch readers return. This header holds the pure masking logic, with
// no CTRPF dependency, so tests/ can build it on the PC.

#ifdef __3DS__
#include <3ds/types.h>
#else
#include <cstdint>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int32_t  s32;
#endif

namespace OneScreen
{
    // A capture request is one word of HID key bits, so the hooks can read it
    // with a single load. The circle pad direction bits stand for the stick and
    // the touchpad bit for touch (same values as CTRPF's Key::CPad / Key::Touchpad).
    const u32 CaptureStickBits = 0xF0000000;
    const u32 CaptureTouchBit  = 1u << 20;

    // One pad sample as the game's reader at 0x282548 writes it.
    struct PadSample
    {
        u32 held;
        u32 pressed;
        u32 released;
        s16 stickX;
        s16 stickY;
    };

    // One touch sample as the game's reader at 0x2826F0 writes it.
    struct TouchSample
    {
        u16 x;
        u16 y;
        u8  touching;
        u8  pad[3];
    };

    // Kept by the pad hook between reads. `suppressed` holds keys that were
    // captured and still held when capture ended: they stay hidden until a
    // read shows them released, so the game never sees a fresh press.
    struct PadCaptureState
    {
        u32 applied = 0;
        u32 suppressed = 0;
    };

    struct TouchCaptureState
    {
        bool applied = false;
        bool suppressed = false;
    };

    // Applies `request` to the `count` samples of one read, in place.
    void MaskPadSamples(PadCaptureState &state, u32 request, PadSample *samples, s32 count);
    void MaskTouchSamples(TouchCaptureState &state, u32 request, TouchSample *samples, s32 count);
}
