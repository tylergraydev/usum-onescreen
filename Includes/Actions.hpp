#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

#include "ActionQueue.hpp"

namespace OneScreen
{
    // The action layer in the plugin: runs ActionQueue.hpp's actions against
    // the game. While one runs, the player's keys, circle pad and touch are
    // captured (InputCaptureBegin) and the action's own presses and taps go
    // in after capture; the earlier capture request is restored when it ends.

    // Registers the OSD callback that samples the bottom screen (X menu probe).
    void StartActions(void);

    // Once per frame, from the plugin thread. Log lines are appended to `log`.
    void ActionsUpdate(u32 sceneManager, std::vector<std::string> &log);

    // "action NAME" | "action status" | "action cancel" | "action list" (the
    // part after "action "). False if it isn't one of those.
    bool ActionCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    bool ActionRunning(void);

    // Starts an action that isn't one of the built-in ones (a plugin screen's
    // own, e.g. Screens/XMenu.cpp). `def` must outlive the run. Same checks
    // and capture as `action NAME`; false (reason logged) if refused.
    bool ActionStart(const ActionDef &def, u32 sceneManager, std::vector<std::string> &log);

    // The bottom-screen probes' latest result (WorldXMenu, WorldXMenuPage2, WorldPartyList),
    // sampled every frame whether or not an action runs.
    u32 ActionProbeFlags(void);
}
