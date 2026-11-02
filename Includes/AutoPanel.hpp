#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // Picks the panel mode from the current scene, using the [scenes]
    // settings (ModeForScene). Defaults:
    //   TitleMenu: off. FieldRo: off, or panel while the X menu is open.
    //   Battle, Zukan (Pokédex), Bag: panel. Party: full view.
    //   Any other scene: panel. Transitions ("" scene): unchanged.
    // A manual change (ZR) overrides this until the scene changes.

    // Call every frame. `scene` is the latest SceneModule() result and
    // `gameHeld` the keys the game last read. Returns true if it changed the
    // panel mode.
    bool AutoPanelUpdate(const std::string &scene, u32 gameHeld);

    // The player picked a mode by hand; keep it until the next scene change.
    void AutoPanelManualOverride(void);

    void AutoPanelSetEnabled(bool enabled);
    bool AutoPanelEnabled(void);

    std::string AutoPanelStatus(void);
}
