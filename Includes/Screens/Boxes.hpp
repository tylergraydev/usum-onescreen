#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native PC boxes (P2-6): while the game's Box screen is up on the
    // hidden bottom screen, the top screen shows the box on screen (its
    // name, the Pokémon in each slot) and the party, with the game's own
    // cursor. The D-pad and B stay with the game (move its cursor, leave);
    // A picks up / drops through the action layer, L / R turn the box, Y
    // steps aside so the game's own top screen shows the Pokémon at the
    // cursor. While it shows, the panel is held off; if the player turns the
    // panel on by hand (L+R), it steps aside until the PC closes. See
    // docs/BOXES.md.

    // Registers the OSD callback that draws the screen. Call before
    // StartPanel (screenshots are taken in the panel's callback).
    void StartBoxes(void);

    // Once per frame, from the plugin thread, after AutoPanelUpdate. Log
    // lines are appended to `log`.
    void BoxesUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // "boxes on|off|status|details|pick|drop|prev|next|icons on|icons off" (the part after
    // "boxes "); pick/drop/prev/next act as A / L / R would. False if it
    // isn't one of those.
    bool BoxesRunCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string BoxesStatus(void);
}
