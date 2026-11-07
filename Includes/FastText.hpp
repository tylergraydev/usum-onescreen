#pragma once

#include <cstdint>
#include <string>

namespace OneScreen
{
    // Apply the current Settings to the main image and any active Battle CRO.
    // Called at boot, reload, and live changes. Returns log lines.
    std::string ApplyFastTextSettings(uint32_t sceneManager);
    // Called EVERY frame, independently of the slower scene/UI polling.
    // Resolves the module once per battle; retries discovery until known.
    std::string FastTextFrame(uint32_t sceneManager);
    std::string FastTextStatus();
}
