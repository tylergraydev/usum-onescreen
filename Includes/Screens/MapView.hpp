#pragma once

// The native map (P2-5): what it shows and how it is laid out, which keys
// open and close it, and drawing. Pure C++ (no CTRPF), so tests/ can build it
// on the PC; Screens/Map.cpp runs it in the plugin. See docs/MAP.md.

#include <string>
#include <vector>

#include "UI/Raster.hpp"
#include "Where.hpp"

namespace OneScreen
{
    // What the map shows. Plain data (fixed-size text), so the plugin thread
    // can hand a copy to the OSD callback.
    struct MapView
    {
        bool known = false;     // a field reading has come in
        bool picture = false;   // the game's Rotom map is up: its window is copied in
        bool indoor = false;
        bool hasIsland = false; // the Rotom map view was found (its map index is the island)
        u32  island = 0;
        u16  zone = 0;
        int  x = 0, z = 0;      // world position, rounded
        int  yaw = 0;           // facing, 0 south, 90 east
        char place[64] = {};    // place name, "" if the names aren't loaded
        char sub[64] = {};      // its second line ("Shopping District"), often ""
        char key[16] = {};      // the open/close key, for the hint ("ZR")
    };

    bool operator==(const MapView &a, const MapView &b);
    inline bool operator!=(const MapView &a, const MapView &b) { return !(a == b); }

    // The island for a Rotom map index. Only index 0 (Melemele) has been seen
    // in the game; 1-3 follow the islands' order (docs/MAP.md).
    const char *MapIslandName(u32 index);
    // 8 compass points from a facing in degrees (0 south, 90 east).
    const char *MapCompass(int yaw);

    // The view for a field reading. `names` are the lines of the place-name
    // message file (empty if they couldn't be read): the zone's name line and
    // the line after it. `key` names the map key.
    MapView MakeMapView(const WhereState &where, const std::vector<std::string> &names, const std::string &key);

    // One log line: zone, names, inside/outside, island, position, picture.
    std::string DescribeMapView(const MapView &view);

    // What the keys pressed this frame do. `mapKey` (0: none) toggles the
    // map; B closes it. Opening also needs `canOpen` (the field, no menu).
    enum class MapKeyResult { None, Open, Close };
    MapKeyResult MapKeys(bool open, bool canOpen, u32 pressed, u32 mapKey);
    static const u32 MapKeyB = 1u << 1;

    // Top-screen layout. The map window is the game's Rotom map, copied from
    // the bottom screen 1:1 by Panel's GPU copy: MapSource there, MapWindow
    // here. Both keep the copy's alignment (y + h and y multiples of 8, h a
    // multiple of 16; see PanelCopyBottomRect). MapSource is the Rotom map's
    // window (layouts: field.map, 29,84 - 290,231) below Rotom's eyes, which
    // reach down to y 97 at its sides.
    static constexpr UI::Rect MapArea = { 12, 8, 376, 224 };
    static constexpr int MapRadius = 12;
    static constexpr UI::Rect MapSource = { 30, 104, 260, 128 };
    static constexpr UI::Rect MapWindow = { 70, 72, 260, 128 };

    void DrawMap(UI::Canvas &canvas, const MapView &view);
}
