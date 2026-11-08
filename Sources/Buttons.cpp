#include "Buttons.hpp"

#include <CTRPluginFramework.hpp>

#include "InputCapture.hpp"
#include "PcAnywhereRules.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert(CaptureStickBits == (u32)Key::CPad, "stick capture bits");
    static_assert(CaptureTouchBit == (u32)Key::Touchpad, "touch capture bit");

    static Hook s_padHook;

    static volatile u32 s_held = 0;         // keys currently injected
    static volatile u32 s_frames[32] = {};  // per-key countdown, 0 = hold until released
    static u32 s_lastSeen = 0;              // injected keys as of the previous frame's first read
    static volatile bool s_newFrame = true; // set once per frame by ButtonsUpdate
    static volatile u32 s_reads = 0, s_injected = 0, s_gameSaw = 0;

    // Injected circle pad position (about -156..156 each way) and countdown.
    static volatile s16 s_stickX = 0, s_stickY = 0;
    static volatile u32 s_stickFrames = 0;

    // Capture request, written by the plugin thread with one store. Only the
    // hook touches s_capture, so ending capture can't race with its update.
    static volatile u32 s_captureRequest = 0;
    static PadCaptureState s_capture;

    // The latest read before masking (real input plus injected), for plugin menus.
    static volatile u32 s_realHeld = 0, s_realPrevHeld = 0, s_realPressed = 0;
    static volatile s16 s_realStickX = 0, s_realStickY = 0;

    // Keys from the action layer, added after capture, with their own edges.
    static volatile u32 s_actionKeys = 0;
    static u32 s_actionLastSeen = 0;
    static volatile bool s_actionNewFrame = true;

    // A key combo the plugin takes for itself (pc_key, PcAnywhere.cpp): the
    // hook counts its presses and hides its keys from the game until released.
    static volatile u32 s_combo = 0;
    static volatile u32 s_comboHits = 0;
    static PcComboState s_comboState;   // only the hook touches it

    // Circle pad direction bits the HID module sets alongside the raw position.
    static u32 StickBits(s32 x, s32 y)
    {
        const s32 dead = 40;
        u32 bits = 0;
        if (x >  dead) bits |= Key::CPadRight;
        if (x < -dead) bits |= Key::CPadLeft;
        if (y >  dead) bits |= Key::CPadUp;
        if (y < -dead) bits |= Key::CPadDown;
        return bits;
    }

    // Replaces the reader at 0x282548 (USUM v1.2):
    // (pad shm, samples out, max samples, int *count out, int *, int *).
    static u32 PadReadHook(u32 shm, PadSample *out, s32 max, s32 *count, s32 *a, s32 *b)
    {
        const u32 result = HookContext::GetCurrent().OriginalFunction<u32>(shm, out, max, count, a, b);

        s_reads++;
        const bool stick = s_stickFrames > 0;
        const u32 keys = s_held | (stick ? StickBits(s_stickX, s_stickY) : 0);
        if ((keys != 0 || s_lastSeen != 0) && out != nullptr && max > 0)
        {
            const s32 n = (count != nullptr && *count > 0) ? *count : 1;
            for (s32 i = 0; i < n && i < max; i++)
            {
                out[i].held |= keys;
                if (stick)
                {
                    out[i].stickX = s_stickX;
                    out[i].stickY = s_stickY;
                }
            }

            // PadReader::Read takes press/release edges from the samples, so
            // report them once per frame, on the first read after the change.
            if (s_newFrame)
            {
                out[0].pressed |= keys & ~s_lastSeen;
                out[0].released |= s_lastSeen & ~keys;
                s_lastSeen = keys;
                s_newFrame = false;
            }
            if (count != nullptr && *count < 1)
                *count = 1;
            if (keys)
                s_injected++;
        }

        const s32 n = (out != nullptr && count != nullptr) ? (*count < max ? *count : max) : 0;
        if (n > 0)
        {
            u32 held = 0;
            for (s32 i = 0; i < n; i++)
                held |= out[i].held;
            s_realHeld = held;
            s_realStickX = out[0].stickX;
            s_realStickY = out[0].stickY;
        }
        const u32 combo = s_combo;
        if ((combo != 0 || s_comboState.suppressed != 0) && n > 0)
        {
            u32 held = 0;
            for (s32 i = 0; i < n; i++)
                held |= out[i].held;
            if (PcComboRead(s_comboState, combo, held))
                s_comboHits = s_comboHits + 1;
            const u32 hide = s_comboState.suppressed;
            if (hide != 0)
                for (s32 i = 0; i < n; i++)
                {
                    out[i].held &= ~hide;
                    out[i].pressed &= ~hide;
                }
        }
        MaskPadSamples(s_capture, s_captureRequest, out, n);

        const u32 action = s_actionKeys;
        if ((action != 0 || s_actionLastSeen != 0) && out != nullptr && max > 0 && count != nullptr)
        {
            if (*count < 1)
            {
                out[0] = PadSample();
                *count = 1;
            }
            const s32 m = *count < max ? *count : max;
            for (s32 i = 0; i < m; i++)
                out[i].held |= action;
            if (s_actionNewFrame)
            {
                out[0].pressed |= action & ~s_actionLastSeen;
                out[0].released |= s_actionLastSeen & ~action;
                s_actionLastSeen = action;
                s_actionNewFrame = false;
            }
        }

        if (out != nullptr && count != nullptr && *count > 0)
            s_gameSaw = out[0].held;
        return result;
    }

    bool StartButtons(u32 padReadAddress, std::string &report)
    {
        s_padHook.InitializeForMitm(padReadAddress, (u32)PadReadHook);
        const HookResult result = s_padHook.Enable();
        report = Utils::Format("buttons: pad hook at %08lX -> %d", padReadAddress, (int)result);
        return result == HookResult::Success;
    }

    void ButtonsUpdate(void)
    {
        u32 held = s_held;
        for (u32 bit = 0; bit < 32; bit++)
        {
            if ((held & (1u << bit)) && s_frames[bit] > 0 && --s_frames[bit] == 0)
                held &= ~(1u << bit);
        }
        s_held = held;
        if (s_stickFrames > 0)
            s_stickFrames--;
        s_newFrame = true;
        s_actionNewFrame = true;

        const u32 real = s_realHeld;
        s_realPressed = real & ~s_realPrevHeld;
        s_realPrevHeld = real;
    }

    void ButtonsStick(s32 x, s32 y, u32 frames)
    {
        s_stickX = (s16)x;
        s_stickY = (s16)y;
        s_stickFrames = frames;
    }

    void ButtonsPress(u32 keys, u32 frames)
    {
        for (u32 bit = 0; bit < 32; bit++)
            if (keys & (1u << bit))
                s_frames[bit] = frames;
        s_held = s_held | keys;
    }

    void ButtonsRelease(u32 keys)
    {
        s_held = s_held & ~keys;
    }

    u32 ButtonFromName(const std::string &name)
    {
        static const struct { const char *name; u32 key; } keys[] =
        {
            { "a", Key::A }, { "b", Key::B }, { "x", Key::X }, { "y", Key::Y },
            { "l", Key::L }, { "r", Key::R }, { "zl", Key::ZL }, { "zr", Key::ZR },
            { "start", Key::Start }, { "select", Key::Select },
            { "up", Key::DPadUp }, { "down", Key::DPadDown },
            { "left", Key::DPadLeft }, { "right", Key::DPadRight },
        };
        for (const auto &k : keys)
            if (name == k.name)
                return k.key;
        return 0;
    }

    void ButtonsActionSet(u32 keys)
    {
        s_actionKeys = keys;
    }

    void ButtonsSetCombo(u32 keys)
    {
        s_combo = keys;
    }

    u32 ButtonsComboHits(void)
    {
        return s_comboHits;
    }

    u32 ButtonsGameHeld(void)
    {
        return s_gameSaw;
    }

    void InputCaptureBegin(u32 keys, bool captureStick, bool captureTouch)
    {
        s_captureRequest = (keys & ~(CaptureStickBits | CaptureTouchBit))
                           | (captureStick ? CaptureStickBits : 0)
                           | (captureTouch ? CaptureTouchBit : 0);
    }

    void InputCaptureEnd(void)
    {
        s_captureRequest = 0;
    }

    u32 InputCaptureRequest(void)
    {
        return s_captureRequest;
    }

    u32 CaptureRealHeld(void)
    {
        return s_realHeld;
    }

    u32 CaptureRealPressed(void)
    {
        return s_realPressed;
    }

    void CaptureRealStick(s32 &x, s32 &y)
    {
        x = s_realStickX;
        y = s_realStickY;
    }

    std::string ButtonsStatus(void)
    {
        return Utils::Format("buttons: injecting %08lX | game reads %lu, injected %lu, last held seen %08lX"
                             " | capture %08lX, suppressing %08lX, real held %08lX",
                             s_held, s_reads, s_injected, s_gameSaw,
                             s_captureRequest, s_capture.suppressed, s_realHeld);
    }
}
