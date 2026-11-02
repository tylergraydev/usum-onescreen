#pragma once

#include <CTRPluginFramework.hpp>

#include "UI/Raster.hpp"

// The CTRPF side of the UI drawing layer: turns an OSD Screen into a
// UI::Surface and provides the System font. Only built for the 3DS.
namespace OneScreen
{
    UI::Surface SurfaceFromScreen(const CTRPluginFramework::Screen &screen);

    // Makes UI::Font::System draw through CTRPF on this screen. Call at the
    // start of each OSD frame that draws text; nullptr unbinds it.
    void UiBindScreen(const CTRPluginFramework::Screen *screen);

    // The `uidemo on|off` test command: a sample menu over the top screen.
    void UiDemoSetEnabled(bool enabled);
    bool UiDemoEnabled(void);
    // Draws the demo if it is on and screen is the top screen. Called from the
    // OSD callback; returns true if it drew.
    bool UiDemoOnScreen(const CTRPluginFramework::Screen &screen);
}
