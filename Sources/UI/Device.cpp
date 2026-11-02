#include "UI/Device.hpp"

#include <algorithm>
#include <cmath>

#include "Framebuffer.hpp"
#include "UI/Demo.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    UI::Surface SurfaceFromScreen(const Screen &screen)
    {
        const FbLayout fb = GetLayout(screen);
        UI::Surface surface;
        surface.origin = fb.origin;
        surface.stepX = fb.stepX;
        surface.stepY = fb.stepY;
        surface.format = (UI::PixelFormat)fb.format;
        surface.width = screen.IsTop ? (s32)TopWidth : (s32)BottomWidth;
        surface.height = ScreenHeight;
        return surface;
    }

    static const Screen *s_screen = nullptr;

    // DrawSysfont writes straight into the framebuffer and has no size or
    // alpha, so text is drawn white-on-black into a corner of the screen, read
    // back as a coverage mask, and the corner is restored. The mask is then
    // drawn wherever it is wanted, scaled and in any color. The margin covers
    // glyph pixels that may land outside the measured box (not yet checked
    // in-game).
    static constexpr s32 SysLine = 16, Margin = 8;
    static constexpr s32 SaveW = UI::ScreenW, SaveH = SysLine + Margin;
    static u8 s_saved[SaveW * SaveH * 4];

    static s32 SysMeasure(const std::string &text)
    {
        return (s32)std::ceil(OSD::GetTextWidth(true, text));
    }

    static bool SysRasterize(const std::string &text, u8 *mask, s32 w, s32 h)
    {
        if (s_screen == nullptr || w > SaveW || h > SysLine)
            return false;

        const UI::Surface surface = SurfaceFromScreen(*s_screen);
        const s32 bpp = (s32)UI::BytesPerPixel(surface.format);
        const s32 saveW = std::min(w + Margin, surface.width), saveH = SaveH;
        auto pixel = [&](s32 x, s32 y) { return surface.origin + x * surface.stepX + y * surface.stepY; };

        for (s32 y = 0; y < saveH; y++)
            for (s32 x = 0; x < saveW; x++)
                std::copy(pixel(x, y), pixel(x, y) + bpp, s_saved + (y * saveW + x) * bpp);

        UI::Canvas canvas(surface);
        canvas.FillRect(UI::Rect{ 0, 0, saveW, saveH }, UI::Rgb(0, 0, 0));
        s_screen->DrawSysfont(text, 0, 0, Color::White);

        for (s32 y = 0; y < h; y++)
            for (s32 x = 0; x < w; x++)
            {
                u8 r, g, b;
                UI::ReadPixel(pixel(x, y), surface.format, r, g, b);
                mask[y * w + x] = std::max(r, std::max(g, b));
            }

        for (s32 y = 0; y < saveH; y++)
            for (s32 x = 0; x < saveW; x++)
                std::copy(s_saved + (y * saveW + x) * bpp, s_saved + (y * saveW + x + 1) * bpp, pixel(x, y));
        return true;
    }

    static const UI::FontBackend s_sysfont = { SysLine, SysMeasure, SysRasterize };

    void UiBindScreen(const Screen *screen)
    {
        s_screen = screen;
        UI::SetSystemFont(screen != nullptr ? &s_sysfont : nullptr);
    }

    static volatile bool s_demo = false;
    static u32 s_demoFrames = 0;

    void UiDemoSetEnabled(bool enabled)
    {
        s_demo = enabled;
        s_demoFrames = 0;
    }

    bool UiDemoEnabled(void)
    {
        return s_demo;
    }

    bool UiDemoOnScreen(const Screen &screen)
    {
        if (!s_demo || !screen.IsTop)
            return false;

        UiBindScreen(&screen);
        UI::Canvas canvas(SurfaceFromScreen(screen));
        // Move the highlight about once a second so it is clear the menu is
        // redrawn every frame.
        UI::DrawDemoMenu(canvas, (s32)((s_demoFrames++ / 60) % UI::DemoRows));
        UiBindScreen(nullptr);
        return true;
    }
}
