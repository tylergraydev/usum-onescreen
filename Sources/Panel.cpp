#include "Panel.hpp"

#include <CTRPluginFramework.hpp>

#include "Cursor.hpp"
#include "Framebuffer.hpp"
#include "Settings.hpp"
#include "Shot.hpp"
#include "UI/Device.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Last captured bottom screen, as packed RGB888, row-major.
    static u8 s_capture[BottomWidth * ScreenHeight * 3];
    static bool s_haveCapture = false;

    static volatile PanelMode s_mode = PanelMode::Off;

    static u32 s_bottomFrames = 0;
    static u32 s_topFrames = 0;
    static u32 s_drawnFrames = 0;
    static u32 s_readbackOk = 0;
    static int s_bottomFormat = -1;
    static int s_topFormat = -1;

    // Timing for the `perf` command (off by default): emulated CPU ticks spent
    // copying the bottom screen and drawing the panel, per report window.
    struct PerfStat
    {
        u32 count;
        u64 sum;
        u64 max;

        void Add(u64 ticks)
        {
            count++;
            sum += ticks;
            if (ticks > max)
                max = ticks;
        }
    };

    static volatile bool s_perf = false;
    static PerfStat s_captureStat;
    static PerfStat s_drawStat;
    static u64 s_perfStart = 0;
    static u32 s_perfTopFrames = 0;
    static u32 s_perfBottomFrames = 0;

    static void CaptureBottom(const Screen &screen)
    {
        ReadScreen(screen, BottomWidth, s_capture);
        s_haveCapture = true;
    }

    // Draws the capture at (left, top) on the top screen, scaled by 1/divisor.
    static void DrawCapture(const Screen &screen, u32 left, u32 top, u32 divisor)
    {
        const FbLayout fb = GetLayout(screen);
        const u32 width = BottomWidth / divisor;
        const u32 height = ScreenHeight / divisor;

        for (u32 y = 0; y < height; y++)
        {
            const u8 *src = s_capture + (y * divisor * BottomWidth) * 3;
            u8 *p = fb.origin + (s32)(top + y) * fb.stepY + (s32)left * fb.stepX;
            for (u32 x = 0; x < width; x++, p += fb.stepX, src += 3 * divisor)
                EncodePixel(p, fb.format, src[0], src[1], src[2]);
        }
    }

    // Reads back the top-screen pixel at the panel's center and checks it
    // matches the captured bottom-screen pixel it was drawn from, so the log can
    // show the panel is really being written without anyone watching.
    static void CheckReadback(const Screen &screen, u32 left, u32 top, u32 divisor)
    {
        const u32 cx = BottomWidth / divisor / 2, cy = ScreenHeight / divisor / 2;
        const FbLayout fb = GetLayout(screen);
        const u8 *p = fb.origin + (s32)(top + cy) * fb.stepY + (s32)(left + cx) * fb.stepX;
        const u8 *src = s_capture + ((cy * divisor) * BottomWidth + cx * divisor) * 3;

        u8 want[4] = {}, got[4] = {};
        EncodePixel(want, fb.format, src[0], src[1], src[2]);
        for (u32 i = 0; i < screen.BytesPerPixel && i < 4; i++)
            got[i] = p[i];
        if (want[0] == got[0] && want[1] == got[1] && want[2] == got[2] && want[3] == got[3])
            s_readbackOk++;
    }

    static bool DrawPanel(const Screen &screen)
    {
        if (!screen.IsTop)
        {
            s_bottomFrames++;
            s_bottomFormat = screen.Format;
            if (s_mode != PanelMode::Off)
            {
                const u64 start = s_perf ? svcGetSystemTick() : 0;
                CaptureBottom(screen);
                if (s_perf)
                    s_captureStat.Add(svcGetSystemTick() - start);
            }
            return false;
        }

        s_topFrames++;
        s_topFormat = screen.Format;

        if (s_mode == PanelMode::Off || !s_haveCapture)
            return false;

        const u64 start = s_perf ? svcGetSystemTick() : 0;
        if (s_mode == PanelMode::Full)
        {
            const u32 left = (TopWidth - BottomWidth) / 2;
            DrawCapture(screen, left, 0, 1);
            CheckReadback(screen, left, 0, 1);
            CursorDraw(screen, left, 0, 1);
        }
        else
        {
            // Half size, 4 px from the corner chosen in settings. A 2/3 size
            // setting is drawn as 1/2 until scaling supports it.
            const u32 w = BottomWidth / 2, h = ScreenHeight / 2;
            const PanelCorner corner = GetSettings().panelCorner;
            const bool right = corner == PanelCorner::BottomRight || corner == PanelCorner::TopRight;
            const bool bottom = corner == PanelCorner::BottomRight || corner == PanelCorner::BottomLeft;
            const u32 left = right ? TopWidth - w - 4 : 4, top = bottom ? ScreenHeight - h - 4 : 4;
            screen.DrawRect(left - 1, top - 1, w + 2, h + 2, Color::White, false);
            DrawCapture(screen, left, top, 2);
            CheckReadback(screen, left, top, 2);
            CursorDraw(screen, left, top, 2);
        }
        if (s_perf)
            s_drawStat.Add(svcGetSystemTick() - start);
        s_drawnFrames++;
        return true;
    }

    static bool PanelCallback(const Screen &screen)
    {
        bool drew = DrawPanel(screen);
        // Drawn after the panel so test menus sit on top, and before the
        // screenshot hook so shots include them.
        drew |= UiDemoOnScreen(screen);
        ShotOnScreen(screen);
        return drew;
    }

    void StartPanel(PanelMode initial)
    {
        s_mode = initial;
        OSD::Run(PanelCallback);
    }

    PanelMode GetPanelMode(void)
    {
        return s_mode;
    }

    void SetPanelMode(PanelMode mode)
    {
        s_mode = mode;
        if (mode == PanelMode::Off)
            s_haveCapture = false;
    }

    void CyclePanelMode(void)
    {
        switch (s_mode)
        {
        case PanelMode::Off:   SetPanelMode(PanelMode::Panel); break;
        case PanelMode::Panel: SetPanelMode(PanelMode::Full);  break;
        case PanelMode::Full:  SetPanelMode(PanelMode::Off);   break;
        }
    }

    std::string PanelStatus(void)
    {
        static const char *names[] = { "off", "panel", "full" };
        return Utils::Format("panel: mode %s, top frames %lu (format %d), bottom frames %lu (format %d), "
                             "drawn %lu, readback ok %lu",
                             names[(int)s_mode], s_topFrames, s_topFormat, s_bottomFrames, s_bottomFormat,
                             s_drawnFrames, s_readbackOk);
    }

    // Starts a new timing window. Not synchronized with the OSD callback, so a
    // frame at the window edge may land in either window.
    static void PerfResetWindow(void)
    {
        s_captureStat = PerfStat();
        s_drawStat = PerfStat();
        s_perfTopFrames = s_topFrames;
        s_perfBottomFrames = s_bottomFrames;
        s_perfStart = svcGetSystemTick();
    }

    void PanelPerfSetEnabled(bool enabled)
    {
        PerfResetWindow();
        s_perf = enabled;
    }

    std::string PanelPerfReport(void)
    {
        static const char *names[] = { "off", "panel", "full" };
        const auto us = [](u64 ticks) { return (unsigned long)(ticks * 1000000 / SYSCLOCK_ARM11); };
        const auto mean = [](const PerfStat &s) { return s.count ? s.sum / s.count : 0; };

        const u64 window = svcGetSystemTick() - s_perfStart;
        const std::string line = Utils::Format(
            "perf: %s, mode %s, window %lu us, top frames %lu, bottom frames %lu, "
            "capture n %lu mean %lu us max %lu us, draw n %lu mean %lu us max %lu us",
            s_perf ? "on" : "off", names[(int)s_mode], us(window),
            s_topFrames - s_perfTopFrames, s_bottomFrames - s_perfBottomFrames,
            s_captureStat.count, us(mean(s_captureStat)), us(s_captureStat.max),
            s_drawStat.count, us(mean(s_drawStat)), us(s_drawStat.max));
        PerfResetWindow();
        return line;
    }
}
