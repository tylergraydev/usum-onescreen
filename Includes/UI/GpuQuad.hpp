#pragma once

#include <string>
#include "UI/Raster.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
namespace UI
{
    // Overlay backend evolved from the verified spike (docs/GPUQUAD.md).
    // Up to two textured quads share one PICA list and one state restore.
    //
    // Both modes run one PICA command list (our state, the draw, then the
    // game's state restored). `mode fb`, from the OSD callback:
    // DisplayTransfer the top framebuffer (linear BGR8) into a tiled RGBA8
    // target in our linear scratch, the list, and DisplayTransfer it back
    // (three GX commands). `mode target`: a hook on the game's DisplayTransfer
    // draws into the game's own top-screen render target just before the
    // game copies it to the framebuffer (one GX command).
    //
    // The game's GPU state is restored from a shadow kept by a hook on the
    // game's gx ProcessCommandList wrapper (0x279D5C), which parses every
    // command list the game submits (following CMDBUF jumps).

    // Two independently visible atlas regions, composited in slot order in one GX list.
    // Source is row-major RGBA8 (ABGR bytes); region is in a shared 1024x1024 atlas.
    // First publish must be dirty. Source must stay stable for this call; callers
    // own staging storage and slot/region ownership. Renew every visible callback.
    // Upload only on dirty content. Nonblocking: false means retry next callback.
    bool HiResReady(void);
    bool UpdateGpuQuad(u32 slot, const Surface &source, const Rect &region, const Rect &destination, bool dirty);
    void HideGpuQuad(u32 slot);
    std::string HiResCommand(const std::string &args);

    // Installs the ProcessCommandList hook. displayTransfer is the game's gx
    // DisplayTransfer (0x27A5BC). Returns a log line.
    std::string StartGpuQuad(u32 displayTransfer);

    // `gpuquad on|off|status|reset`. Returns the log line.
    std::string GpuQuadCommand(const std::string &args);

    // From the OSD callback: draws on the top screen when on. Never writes
    // the framebuffer with the CPU, so it always returns false.
    bool GpuQuadOnScreen(const CTRPluginFramework::Screen &screen);
}
}
