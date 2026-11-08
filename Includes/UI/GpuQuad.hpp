#pragma once

#include <string>

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
namespace UI
{
    // Feasibility spike (docs/GPUQUAD.md): draws one textured, alpha-blended
    // quad on the top screen with the 3DS GPU (PICA200), so Azahar renders it
    // at its internal resolution.
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
