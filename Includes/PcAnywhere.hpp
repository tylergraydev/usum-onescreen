#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // PC anywhere (docs/PC-ANYWHERE.md): opens the PC boxes from the
    // overworld the way the game opens its own apps from the X menu. A hook
    // on the game's event manager (game thread) starts the game's
    // EventAppCall (field closes, the player's place is kept), whose
    // between-app callback starts the game's BoxEvent (the Box app the PC
    // script starts); when the Box closes, EventAppCall reopens the field
    // where the player stood.

    // Checks the game functions and installs the event manager hook once,
    // after DetectGame/VerifyHooks. False (and PC anywhere stays off) if a
    // check fails; `report` says why.
    bool StartPcAnywhere(std::string &report);

    // Asks for the boxes from the plugin thread. `source` goes in the log
    // ("cmd", "key", "menu"). Appends "pc: refused (reason)" when it can't
    // open now; otherwise the request goes to the game thread, and
    // PcAnywhereUpdate logs "pc: opened" (or a late refusal).
    void PcAnywhereRequest(u32 sceneManager, const char *source, std::vector<std::string> &log);

    // Once per frame from the plugin thread: pc_key presses, results from
    // the game thread, and the return to the field.
    void PcAnywhereUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // "pc" (open) | "pc status". False if it isn't one of those.
    bool PcAnywhereCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string PcAnywhereStatus(void);
}
