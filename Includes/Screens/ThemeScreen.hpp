#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The USUM theme demo (W6-3): `uitheme on` draws a sample screen with
    // the theme (UI/ThemeDemo.hpp) over the top screen, filled with the lead
    // Pokémon's real moves and the party. The game's font, icons, move table
    // and type names come from the shared asset service (UI/Assets.hpp);
    // `uitheme on` starts its load if the field hasn't yet. See
    // docs/VISUAL-STYLE.md and docs/GAME-ASSETS.md.

    // Registers the OSD callback. Call before StartPanel (screenshots are
    // taken in the panel's callback).
    void StartThemeScreen(void);

    // Once per frame, from the plugin thread.
    void ThemeScreenUpdate(u32 sceneManager, std::vector<std::string> &log);

    // "uitheme on|off|load|status|page N|select N" (the part after
    // "uitheme "). False if it isn't one of those.
    bool ThemeScreenCommand(const std::string &args, std::vector<std::string> &log);
}
