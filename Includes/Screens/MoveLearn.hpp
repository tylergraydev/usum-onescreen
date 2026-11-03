#pragma once

#include <string>
#include <vector>

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    // Native move learning in the plugin (W6-1). Screens/MoveLearnMenu.hpp is
    // the screen itself; this reads the game's question and forget screen
    // (docs/MOVE-LEARN.md), names from the battle's move-name file (RE1),
    // type / power / PP from the game's move data in its RomFS, and starts
    // the screen's actions. The native battle menu (BattleScreen.cpp) runs it
    // and gives way to it while the game asks.

    // What the battle menu needs back each frame.
    struct MoveLearnFrame
    {
        bool asked = false;     // the game is asking (its question or forget screen is up)
        u32 capture = 0;        // keys to keep from the game
    };

    // Once per frame from BattleScreenUpdate. `pressed`: the player's newly
    // pressed keys (before capture). Log lines go to `log`.
    MoveLearnFrame MoveLearnUpdate(u32 sceneManager, bool inBattle, u32 pressed, std::vector<std::string> &log);

    // From the OSD callback (via BattleScreenOnScreen): draws the screen on
    // the top screen if it is up. Returns true if it drew.
    bool MoveLearnOnScreen(const CTRPluginFramework::Screen &screen);

    // The World* bits for the action layer (Actions.cpp): WorldLearnAsk,
    // WorldLearnList or WorldLearnConfirm while the game shows that screen.
    u32 MoveLearnWorldFlags(u32 sceneManager);

    // For `battleui`: the page and what the game shows.
    std::string MoveLearnStatus(void);
}
