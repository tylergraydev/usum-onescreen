#pragma once

#include <string>
#include <vector>

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // The native battle menu in the plugin (P2-1). Screens/BattleMenu.hpp is
    // the menu itself; this feeds it the battle (RE1 `ReadBattle`, names from
    // RE2), starts the actions it chooses (Actions.hpp), keeps its keys from
    // the game while it is up (InputCaptureBegin) and draws it on the top
    // screen in a UI::Layer. On by default; while on, AutoPanel keeps the
    // panel off in battle.

    // Once per frame from the plugin thread. `scene` is the latest
    // SceneModule() result. Log lines (page changes, choices, actions) are
    // appended to `log`.
    void BattleScreenUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // From the OSD callback: draws the menu on the top screen if it is up.
    // Returns true if it drew.
    bool BattleScreenOnScreen(const CTRPluginFramework::Screen &screen);

    // `battleui on|off`: off hides the menu and gives the battle back to the
    // panel (the [scenes] Battle setting).
    void BattleScreenSetEnabled(bool enabled);
    bool BattleScreenEnabled(void);

    // `battleui`: on/off, the page, and what the menu sees.
    std::string BattleScreenStatus(void);
}
