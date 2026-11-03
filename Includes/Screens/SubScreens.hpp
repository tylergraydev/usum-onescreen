#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The simple native screens (W5-3): Options, Trainer Passport and Battle
    // Video (Screens/Options.hpp, TrainerPassport.hpp, BattleVideo.hpp).
    // While the game shows one of their scenes, the top screen shows the
    // native screen, the panel is held off, and the keys the screen uses
    // are captured and turned into presses on the game's own screen. B is
    // left to the game. If the player turns the panel on by hand (L+R), the
    // native screen steps aside until the scene ends. docs/NATIVE-SCREENS.md.

    // Registers the OSD callback (samples the bottom screen, draws). Call
    // before StartPanel, as for the X menu, so screenshots include it.
    void StartSubScreens(void);

    // Once per frame, from the plugin thread, after XMenuUpdate. `scene` is
    // the latest SceneModule() result.
    void SubScreensUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // "screens on|off|status" (the part after "screens "). False if it isn't
    // one of those.
    bool SubScreensCommand(const std::string &args, std::vector<std::string> &log);

    std::string SubScreensStatus(void);
}
