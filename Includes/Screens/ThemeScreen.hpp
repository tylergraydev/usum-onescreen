#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The USUM theme demo (W6-3): `uitheme on` loads the game's own font,
    // icons, move table and type names from its RomFS (UI/GameAssets.hpp)
    // and draws a sample screen with the theme (UI/ThemeDemo.hpp) over the
    // top screen, filled with the lead Pokémon's real moves and the party.
    // Nothing else uses the loaded assets yet. See docs/VISUAL-STYLE.md and
    // docs/GAME-ASSETS.md.

    // Registers the OSD callback. Call before StartPanel (screenshots are
    // taken in the panel's callback).
    void StartThemeScreen(void);

    // Once per frame, from the plugin thread.
    void ThemeScreenUpdate(u32 sceneManager, std::vector<std::string> &log);

    // "uitheme on|off|load|status|page N|select N" (the part after
    // "uitheme "). False if it isn't one of those.
    bool ThemeScreenCommand(const std::string &args, std::vector<std::string> &log);
}
