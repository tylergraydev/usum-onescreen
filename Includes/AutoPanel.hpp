#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // Picks the panel mode from the current scene, using the [scenes]
    // settings (ModeForScene). Defaults:
    //   TitleMenu: off. FieldRo: off, or panel while the X menu is open.
    //   Party, Box, TownMap, Config, Kawaigari: full view. Others: panel.
    //   Transitions ("" scene): unchanged.
    // A manual change (ZR) overrides this until the scene changes.

    // Call every frame. `scene` is the latest SceneModule() result,
    // `gameHeld` the keys the game last read and `fieldState` FieldState()
    // (-1 if unknown; the X menu is then tracked from X/B presses). Returns
    // true if it changed the panel mode.
    bool AutoPanelUpdate(const std::string &scene, u32 gameHeld, int fieldState);

    // The player picked a mode by hand; keep it until the next scene change.
    void AutoPanelManualOverride(void);

    // While the native battle menu (Screens/BattleScreen.hpp) is on, Battle,
    // and the Pokédex scene a catch goes to (nickname question), get no
    // panel whatever [scenes] says.
    void AutoPanelSetNativeBattle(bool on);

    void AutoPanelSetEnabled(bool enabled);
    bool AutoPanelEnabled(void);

    std::string AutoPanelStatus(void);
}
