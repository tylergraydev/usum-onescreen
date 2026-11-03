#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native Pokédex (W5-2): while the game's Zukan app runs on the
    // hidden bottom screen, the top screen shows what it lists, next to what
    // the game already shows there (Screens/PokedexList.hpp). The game's own
    // app does the work and takes the keys as it does without the plugin:
    // D-pad moves its cursor, A opens, B backs out, Up/Down on an entry page
    // go to the next seen one. The native screen follows its frame, cursor
    // and data in memory. While it shows, the panel is held off; if the
    // player turns the panel on by hand, it steps aside until the Pokédex
    // closes. See docs/RE7-pokedex.md.

    // Registers the OSD callback that draws it. Call before StartPanel (the
    // panel's callback takes the screenshots).
    void StartPokedex(void);

    // Once per frame, from the plugin thread, after XMenuUpdate. `scene` is
    // the latest SceneModule() result. Log lines are appended to `log`.
    void PokedexUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log);

    // "pokedex on|off|status|rows" (the part after "pokedex "). False if it
    // isn't one of those.
    bool PokedexCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    // `pokedex on|off` (default on).
    bool PokedexEnabled(void);
    std::string PokedexStatus(void);
}
