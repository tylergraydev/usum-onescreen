#include "InputCapture.hpp"

namespace OneScreen
{
    void MaskPadSamples(PadCaptureState &state, u32 request, PadSample *samples, s32 count)
    {
        // Keys dropped from the request since the last read may still be held.
        state.suppressed |= state.applied & ~request;
        state.applied = request;

        const u32 hide = request | state.suppressed;
        if (hide == 0 || samples == nullptr || count <= 0)
            return;

        // A key stops being suppressed once any sample of a read shows it up.
        // The whole read stays hidden, so a release and re-press inside one
        // read is seen from the next read on (by held, not by a press bit).
        u32 heldAll = ~0u;
        for (s32 i = 0; i < count; i++)
        {
            PadSample &s = samples[i];
            heldAll &= s.held;
            s.held &= ~hide;
            s.pressed &= ~hide;
            s.released &= ~hide;
            if (hide & CaptureStickBits)
            {
                s.stickX = 0;
                s.stickY = 0;
            }
        }
        state.suppressed &= heldAll;
    }

    void MaskTouchSamples(TouchCaptureState &state, u32 request, TouchSample *samples, s32 count)
    {
        const bool capture = (request & CaptureTouchBit) != 0;
        if (state.applied && !capture)
            state.suppressed = true;
        state.applied = capture;

        if (!(capture || state.suppressed) || samples == nullptr || count <= 0)
            return;

        // Same rule as the pad: touch held when capture ended stays hidden until lifted.
        bool lifted = false;
        for (s32 i = 0; i < count; i++)
        {
            TouchSample &s = samples[i];
            if (!s.touching)
                lifted = true;
            s.x = 0;
            s.y = 0;
            s.touching = 0;
        }
        if (lifted)
            state.suppressed = false;
    }
}
