#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native X menu (P2-2): while the game's X menu is up on the hidden
    // bottom screen, the top screen shows the same entries as a grid
    // (Screens/XMenuGrid.hpp). D-pad / circle pad move, L / R turn the page,
    // A opens the entry through the action layer (taps on the bottom screen).
    // B, X and Y are left to the game: B and X close its menu, Y opens its
    // save dialog. While the native menu shows, the panel is held off; if the
    // player turns the panel on by hand, the native menu steps aside until
    // the X menu closes. See docs/XMENU.md.

    // Registers the OSD callback that draws the menu and reads the game's
    // cursor. Call before StartPanel: screenshots are taken in the panel's
    // callback, which then runs after this one and includes the menu.
    void StartXMenu(void);

    // Once per frame, from the plugin thread, after AutoPanelUpdate. `scene`
    // is the latest SceneModule() result and `fieldState` FieldState() (-1
    // outside FieldRo or if unknown). Log lines are appended to `log`.
    void XMenuUpdate(u32 sceneManager, const std::string &scene, int fieldState, std::vector<std::string> &log);

    // "xmenu on|off|status|select NAME|open NAME" (the part after "xmenu ").
    // False if it isn't one of those.
    bool XMenuCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string XMenuStatus(void);
}
