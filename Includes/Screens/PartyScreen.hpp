#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native Party screen and summary (P2-3): while the game's Party
    // screen (X menu -> Pokémon) is up on the hidden bottom screen, the top
    // screen shows the party (Screens/PartyMenu.hpp). D-pad / circle pad move,
    // A opens Summary / Use item / Move / Cancel, Y starts a move, B is left
    // to the game (it closes its Party screen). Choices are carried out on
    // the game's own screen through the action layer. In the summary (Status
    // scene, opened from here) the top screen shows that Pokémon's details;
    // the game keeps its own keys there (Up/Down change Pokémon, B back), Y
    // shows the game's own top page instead. The panel is held off meanwhile;
    // if the player turns it on by hand, the native screen steps aside until
    // the Party screen closes. See docs/PARTY-UI.md.

    // Registers the OSD callback (top: the screen; bottom: the summary's
    // party-ball probe). Call before StartPanel, so screenshots include it.
    void StartPartyScreen(void);

    // Once per frame, from the plugin thread, after XMenuUpdate. `scene` is
    // the latest SceneModule() result.
    void PartyScreenUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // The native X menu is opening the Party screen: hold the panel off
    // through the transition, so it doesn't flash on before the list shows.
    void PartyScreenExpect(void);

    // "partyui on|off|status" (the part after "partyui "). False if it isn't one.
    bool PartyScreenCommand(const std::string &args, std::vector<std::string> &log);

    std::string PartyScreenStatus(void);
}
