#pragma once

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // Headless screenshots for testing. Creating /usum-shot.req on the SD card
    // asks for one; the plugin then writes /usum-shot-top.ppm and
    // /usum-shot-bottom.ppm and removes the request file.

    // Called from the OSD callback with each screen, after the plugin has drawn
    // on it, so a top-screen shot shows exactly what the player sees.
    // True when a capture read the framebuffer (retain CTRPF flush reporting).
    bool ShotOnScreen(const CTRPluginFramework::Screen &screen);

    // Called every frame from the plugin thread: picks up requests and writes
    // finished captures to the SD card.
    void ShotPoll(void);
}
