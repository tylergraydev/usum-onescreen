#pragma once
#include "QuickSaveRules.hpp"
namespace OneScreen
{
    // Plugin thread only. Request is explicit authorization to answer Yes.
    void QuickSaveRequest(u32 sceneManager, std::vector<std::string> &log);
    void QuickSaveUpdate(u32 sceneManager, std::vector<std::string> &log);
    bool QuickSaveRunning(void);
    // Sampled immediately before action input, not from cached scene polling.
    u32 QuickSaveWorldFlags(u32 sceneManager);
}
