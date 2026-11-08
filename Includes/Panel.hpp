#pragma once

#include <string>

#include <3ds/types.h>

namespace CTRPluginFramework { class Screen; }

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

    // Copies the rect (x, y, w, h) of the bottom screen's last frame to
    // (dx, dy) on the top screen with GPU TextureCopy, for a native
    // screen to show part of the hidden bottom screen (Screens/Map.cpp). The
    // destination uses the same widescreen column squeeze as UI::Layer. Call
    // from an OSD callback on the top screen. Framebuffer lines are screen
    // columns, so each column's run must be whole 16-byte units at 8-byte
    // aligned addresses: h a multiple of 16, and y and dy multiples of 8 from
    // the end of the line (with 240-px lines: y + h and dy + h multiples of
    // 8). False, with nothing copied, if that doesn't hold or the GPU path
    // isn't available.
    // With squeeze k > 1, the caller must make the game draw the bottom
    // picture squeezed by k about the bottom column that lands on top x=200.
    // Copies 1:1 with exactly one TextureCopy.
    bool PanelCopyBottomRect(const CTRPluginFramework::Screen &top, u32 x, u32 y, u32 w, u32 h, u32 dx, u32 dy);

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
