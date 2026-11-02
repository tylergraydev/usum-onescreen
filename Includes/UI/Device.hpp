#pragma once

#include <string>

#include <CTRPluginFramework.hpp>

#include "UI/Raster.hpp"

// The CTRPF side of the UI drawing layer: turns an OSD Screen into a
// UI::Surface and provides the system font. Only built for the 3DS.
namespace OneScreen
{
    UI::Surface SurfaceFromScreen(const CTRPluginFramework::Screen &screen);

    // Points UI::Font::System and Large at the console's shared system font
    // (libctru's g_sharedFont, which CTRPF maps). Cheap after the first call;
    // returns false if the font isn't there (text then uses the 5x7 font).
    bool UiInitFont(void);
    // `uifont`: the font's metrics and the glyph cache counters.
    std::string UiFontInfo(void);

    // The `uidemo on|off|shade|direct` test command: a sample menu over the
    // top screen. on: drawn into a Layer when it changes, copied every frame.
    // shade: the same plus a translucent full-screen shade (reads the
    // framebuffer). direct: the whole menu and shade drawn onto the
    // framebuffer every frame (the old way, for comparison).
    enum class UiDemoMode : u8 { Off, On, Shade, Direct };
    void       UiDemoSetMode(UiDemoMode mode);
    UiDemoMode UiDemoGetMode(void);
    // Draws the demo if it is on and screen is the top screen. Called from the
    // OSD callback; returns true if it drew.
    bool UiDemoOnScreen(const CTRPluginFramework::Screen &screen);
    // Emulated time the demo took per frame since the last report (logged by
    // `perf` next to the panel's line).
    std::string UiPerfReport(void);
}
