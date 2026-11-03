#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The native map (P2-5): in the field, map_key (ZR by default) or the
    // native X menu's Map entry shows a map on the top screen: the place name
    // (read from the game's own RomFS), the island, inside or outside, the
    // player's position and facing, and the game's Rotom Dex map window
    // copied from the hidden bottom screen. map_key or B closes it. The
    // player can keep walking while it is up. While it shows, the panel is
    // held off; turning the panel on by hand (L+R) closes it. See docs/MAP.md.

    // Registers the OSD callback that draws the map. Call before StartPanel
    // (screenshots are taken in the panel's callback).
    void StartMap(void);

    // Once per frame, from the plugin thread, after XMenuUpdate. `scene` is
    // the latest SceneModule() result and `fieldState` FieldState().
    void MapUpdate(u32 sceneManager, const std::string &scene, int fieldState, std::vector<std::string> &log);

    // Opens the map once the field allows it (within about a second), as
    // map_key would. For the native X menu's Map entry; `how` goes in the
    // log line ("map: open (X menu)").
    void MapRequestOpen(const char *how = "X menu");
    bool MapEnabled(void);

    // Whether the map is open, and closing it from another screen (the
    // Rotom prompt, Screens/RotomPrompt.hpp); `why` goes in the log line.
    bool MapIsOpen(void);
    void MapClose(const char *why, std::vector<std::string> &log);

    // "map on|off|open|close|status" (the part after "map "). False if it
    // isn't one of those.
    bool MapCommand(const std::string &args, std::vector<std::string> &log);

    std::string MapStatus(void);
}
