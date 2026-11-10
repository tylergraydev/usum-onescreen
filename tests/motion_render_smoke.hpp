#pragma once
#include <algorithm>
#include <vector>
#include "UI/Motion.hpp"

// Exercise real renderer call sites, not just the interpolation helper.
template<class Draw>
bool MotionRenderSmoke(OneScreen::UI::Motion::Screen screen, Draw draw)
{
    using namespace OneScreen;
    namespace M = UI::Motion;
    const Settings saved = GetSettings();
    Settings settings = saved;
    settings.uiMotion = false;
    SetSettings(settings);
    std::vector<UI::u8> pixels(UI::ScreenW * UI::ScreenH * 4);
    UI::Surface surface{pixels.data(), 4, UI::ScreenW * 4, UI::PixelFormat::RGBA8, UI::ScreenW, UI::ScreenH};
    UI::Canvas canvas(surface);
    const auto render = [&](bool target)
    {
        std::fill(pixels.begin(), pixels.end(), 0);
        draw(canvas, target);
    };
    M::Begin(screen, true); render(true); M::End();
    const auto expected = pixels;
    settings.uiMotion = true; SetSettings(settings);
    M::Begin(screen, true); render(false); M::End();
    M::Begin(screen); render(true); M::End();
    bool ok = pixels != expected; // first changed frame still at old position/value
    for (unsigned i = 0; i < M::GaugeFrames; ++i)
    {
        M::Begin(screen); render(true); M::End();
    }
    ok = ok && pixels == expected;
    M::Begin(screen); ok = ok && !M::Active(screen); M::End();
    SetSettings(saved);
    M::Begin(screen, true); // restore the motion-off preview context
    return ok;
}
