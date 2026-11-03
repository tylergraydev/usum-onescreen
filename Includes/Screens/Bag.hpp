#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

#include "Screens/BagList.hpp"

namespace OneScreen
{
    // The native Bag (P2-4): while the game's field Bag is open on the hidden
    // bottom screen, the top screen shows its pockets, the open pocket's
    // items, the party, and the game's item menu, party pick and Yes/No
    // (Screens/BagList.hpp). D-pad / circle pad move, L / R and left / right
    // change pocket, A chooses, B goes back; every choice is carried out on
    // the bottom screen through the action layer, one tap or press at a time,
    // so the game's own Bag does the work. While it shows, the panel is held
    // off; if the player turns the panel on by hand, the native Bag steps
    // aside until the Bag closes. See docs/BAG.md.

    // Registers the OSD callback that draws the Bag. Call before StartPanel
    // (screenshots are taken in the panel's callback).
    void StartBag(void);

    // Once per frame, from the plugin thread, after XMenuUpdate. `scene` is
    // the latest SceneModule() result. Log lines are appended to `log`.
    void BagUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // The X menu is opening the Bag: hold the panel off until it shows.
    void BagExpectOpen(void);

    // "bagui on|off|status|read|select N" (the part after "bagui "). False
    // if it isn't one of those.
    bool BagCommandLine(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string BagStatus(void);

    // What the plugin reads of the game's field Bag this frame (docs/BAG.md).
    // Mode None outside the Bag scene or if its objects don't check out.
    BagGame ReadBagGame(u32 sceneManager);
}
