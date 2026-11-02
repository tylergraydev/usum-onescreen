#include "Cursor.hpp"

#include "Framebuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // One touch sample as the game's reader writes it.
    struct TouchSample
    {
        u16 x;
        u16 y;
        u8  touching;
        u8  pad[3];
    };

    static Hook s_touchHook;

    static volatile s32 s_x = BottomWidth / 2;
    static volatile s32 s_y = ScreenHeight / 2;
    static volatile bool s_zlTouch = false;     // ZL held
    static volatile bool s_cmdTouch = false;    // "touch on" from the command file
    static volatile u32 s_tapFrames = 0;        // "tap N" countdown

    // What the game's reader last returned, and how many reads we overrode.
    static volatile u16 s_seenX = 0, s_seenY = 0;
    static volatile u8 s_seenTouching = 0;
    static volatile u32 s_reads = 0, s_injected = 0;

    static bool IsTouching(void)
    {
        return s_zlTouch || s_cmdTouch || s_tapFrames > 0;
    }

    // Replaces the reader at 0x2826F0 (USUM v1.2):
    // (touch shm, samples out, max samples, int *count out, int *, int *).
    static u32 TouchReadHook(u32 shm, TouchSample *out, s32 max, s32 *count, s32 *a, s32 *b)
    {
        const u32 result = HookContext::GetCurrent().OriginalFunction<u32>(shm, out, max, count, a, b);

        s_reads++;
        if (IsTouching() && out != nullptr && max > 0)
        {
            const s32 n = (count != nullptr && *count > 0) ? *count : 1;
            for (s32 i = 0; i < n && i < max; i++)
            {
                out[i].x = (u16)s_x;
                out[i].y = (u16)s_y;
                out[i].touching = 1;
            }
            if (count != nullptr && *count < 1)
                *count = 1;
            s_injected++;
        }

        if (out != nullptr && count != nullptr && *count > 0)
        {
            s_seenX = out[0].x;
            s_seenY = out[0].y;
            s_seenTouching = out[0].touching;
        }
        return result;
    }

    bool StartCursor(u32 touchReadAddress, std::string &report)
    {
        s_touchHook.InitializeForMitm(touchReadAddress, (u32)TouchReadHook);
        const HookResult result = s_touchHook.Enable();
        report = Utils::Format("cursor: touch hook at %08lX -> %d", touchReadAddress, (int)result);
        return result == HookResult::Success;
    }

    static s32 Clamp(s32 v, s32 lo, s32 hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    void CursorUpdate(void)
    {
        static u32 held = 0;

        const u32 keys = Controller::GetKeysDown();
        s32 dx = 0, dy = 0;
        if (keys & Key::CStickLeft)  dx--;
        if (keys & Key::CStickRight) dx++;
        if (keys & Key::CStickUp)    dy--;
        if (keys & Key::CStickDown)  dy++;

        // Start slow for precise taps, speed up while the stick stays held.
        if (dx || dy)
        {
            held++;
            const s32 speed = held < 10 ? 1 : (held < 30 ? 3 : 6);
            s_x = Clamp(s_x + dx * speed, 0, BottomWidth - 1);
            s_y = Clamp(s_y + dy * speed, 0, ScreenHeight - 1);
        }
        else
            held = 0;

        s_zlTouch = (keys & Key::ZL) != 0;

        if (s_tapFrames > 0)
            s_tapFrames--;
    }

    void CursorMoveTo(u32 x, u32 y)
    {
        s_x = Clamp((s32)x, 0, BottomWidth - 1);
        s_y = Clamp((s32)y, 0, ScreenHeight - 1);
    }

    void CursorSetTouching(bool touching)
    {
        s_cmdTouch = touching;
    }

    void CursorTap(u32 frames)
    {
        s_tapFrames = frames;
    }

    void CursorDraw(const Screen &screen, u32 left, u32 top, u32 divisor)
    {
        const u32 cx = left + (u32)s_x / divisor;
        const u32 cy = top + (u32)s_y / divisor;
        const Color &fill = IsTouching() ? Color::Red : Color::White;

        // Crosshair with a black outline so it shows on any background.
        const u32 arm = 5;
        const u32 x0 = cx >= arm + 1 ? cx - arm : 1, x1 = cx + arm < TopWidth - 1 ? cx + arm : TopWidth - 2;
        const u32 y0 = cy >= arm + 1 ? cy - arm : 1, y1 = cy + arm < ScreenHeight - 1 ? cy + arm : ScreenHeight - 2;
        screen.DrawRect(x0 - 1, cy - 1, x1 - x0 + 3, 3, Color::Black);
        screen.DrawRect(cx - 1, y0 - 1, 3, y1 - y0 + 3, Color::Black);
        screen.DrawRect(x0, cy, x1 - x0 + 1, 1, fill);
        screen.DrawRect(cx, y0, 1, y1 - y0 + 1, fill);
    }

    std::string CursorStatus(void)
    {
        return Utils::Format("cursor: at %ld,%ld touching %d | game reads %lu, injected %lu, last seen %u,%u touching %u",
                             s_x, s_y, IsTouching() ? 1 : 0, s_reads, s_injected,
                             s_seenX, s_seenY, s_seenTouching);
    }
}
