#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
namespace UI
{
    // Presents UI::Layer overlays with GPU transfers instead of CPU writes
    // (docs/PERF.md): in Azahar a CPU write to the game's framebuffer makes it
    // show that frame at 1x, so every overlay turned the upscaled picture back
    // into 400x240. The game's columns under the layer are copied out by the
    // GPU, the layer is copied into that copy, and it goes back by GPU, so the
    // frame keeps its internal resolution. Falls back to the CPU copy when it
    // can't (not BGR8, no linear memory, a skip rect, pillarbox bands).
    void StartLayerGpu(u32 displayTransfer, u32 textureCopy);
    void SetLayerGpu(bool on);
    std::string LayerGpuStatus(void);
}
}
