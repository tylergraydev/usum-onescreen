#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // How the bottom screen is shown on the top screen.
    enum class PanelMode : u8
    {
        Off,    // top screen untouched
        Panel,  // bottom screen at half size in a corner of the top screen (settings: corner)
        Full,   // bottom screen at full size, centered over the top screen
    };

    // Registers the OSD callback that copies the bottom screen onto the top.
    void StartPanel(PanelMode initial);

    PanelMode GetPanelMode(void);
    void      SetPanelMode(PanelMode mode);
    void      CyclePanelMode(void);

    // One-line summary of what the panel has seen so far (frame counts and
    // framebuffer formats), for the boot log.
    std::string PanelStatus(void);
}
