#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // How the bottom screen is shown on the top screen.
    enum class PanelMode : u8
    {
        Off,    // top screen untouched
        Panel,  // bottom screen at 1/2 or 2/3 size in a corner of the top screen (settings: size, corner)
        Full,   // bottom screen at full size, centered over the top screen
    };

    // Registers the OSD callback that copies the bottom screen onto the top.
    // `displayTransfer` and `textureCopy` are the game's gx library calls
    // (Offsets::gxDisplayTransfer / gxTextureCopy) used for the copy.
    void StartPanel(PanelMode initial, u32 displayTransfer, u32 textureCopy);

    PanelMode GetPanelMode(void);
    void      SetPanelMode(PanelMode mode);
    void      CyclePanelMode(void);

    // One-line summary of what the panel has seen so far (frame counts and
    // framebuffer formats), for the boot log.
    std::string PanelStatus(void);

    // Frame timing for the `perf` command (off by default). Enabling starts a
    // new window; each report covers the window since the last report or
    // enable: emulated time (svcGetSystemTick), top and bottom frames seen,
    // and the mean and max time spent copying the bottom screen and drawing
    // the panel.
    void        PanelPerfSetEnabled(bool enabled);
    std::string PanelPerfReport(void);
}
