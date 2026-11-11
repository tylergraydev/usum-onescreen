#pragma once

#include <cstdint>
#include <string>

namespace OneScreen
{
    struct PhotoSafety
    {
        bool saving = false, action = false, pcPending = false;
        bool supportedScene = true, fieldBusy = false, alreadyPaused = false;
        bool gpuTest = false;
    };

    struct PhotoState
    {
        bool active = false;
        uint64_t enteredMs = 0;
    };

    const char *PhotoRefusal(const PhotoSafety &safety);
    bool PhotoEnter(PhotoState &state, const PhotoSafety &safety, uint64_t nowMs);
    bool PhotoExit(PhotoState &state);
    bool PhotoHintVisible(const PhotoState &state, uint64_t nowMs);

#ifdef __3DS__
    // Requests are consumed from PanelEntry on the plugin stack, after the
    // CTRPF menu has closed. The paused loop only polls photo commands/HID.
    void PhotoModeRequest(bool on);
    bool PhotoModeActive(void);
    bool PhotoModeCanOpenMenu(void);
    std::string PhotoModeStatus(void);
    // True when a photo request consumed this plugin iteration.
    bool PhotoModeUpdate(uint32_t sceneManager, void (*pollCommands)());
#endif
}
