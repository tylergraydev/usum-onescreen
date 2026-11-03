#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native keyboard (W5-1): while the game's keyboard (the StrInput
    // CRO: nicknames after a catch) is up on the hidden bottom screen, the
    // top screen shows the same QWERTY keys and the name typed so far
    // (Screens/KeyboardGrid.hpp). D-pad / circle pad move, A types the key
    // under the cursor, B deletes, Y is Shift, X types a space, START moves
    // the cursor to OK. Each key is one tap on the game's key through the
    // action layer, so the game types it and keeps its own rules (shift, the
    // length limit); the name shown is read back from the game. While the
    // native keyboard shows, the panel is held off; if the player turns the
    // panel on by hand, the native keyboard steps aside until the game's
    // keyboard closes. See docs/KEYBOARD.md.

    // Registers the OSD callback that draws the keyboard. Call before
    // StartPanel (screenshots are taken in the panel's callback).
    void StartKeyboard(void);

    // Once per frame, from the plugin thread, after AutoPanelUpdate. Log
    // lines are appended to `log`.
    void KeyboardUpdate(u32 sceneManager, std::vector<std::string> &log);

    // "keyboard on|off|status|read" (the part after "keyboard "). False if
    // it isn't one of those.
    bool KeyboardCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string KeyboardStatus(void);
}
