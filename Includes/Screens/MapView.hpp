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
        bool hasIsland = false; // the zone's map area (or the Rotom map's index) is known
        u32  island = 0;        // map index: 0-3 the islands, 4 Aether Paradise, 5-20 small maps
        u16  zone = 0;
        int  x = 0, z = 0;      // world position, rounded
        int  yaw = 0;           // facing, 0 south, 90 east
        char place[64] = {};    // place name, "" if the names aren't loaded
        char sub[64] = {};      // its second line ("Shopping District"), often ""
        char key[16] = {};      // the open/close key, for the hint ("ZR")
    };

    bool operator==(const MapView &a, const MapView &b);
    inline bool operator!=(const MapView &a, const MapView &b) { return !(a == b); }

    // The island for a map index: 0 Melemele ... 3 Poni; "" for other maps.
    // Only 0 was seen at runtime; 1-3 follow the zone table (docs/MAP.md).
    const char *MapIslandName(u32 index);
    // 8 compass points from a facing in degrees (0 south, 90 east).
    const char *MapCompass(int yaw);

    // The view for a field reading. `names` are the lines of the place-name
    // message file (empty if they couldn't be read): the zone's name line and
    // the line after it. `key` names the map key.
    MapView MakeMapView(const WhereState &where, const std::vector<std::string> &names, const std::string &key);

    // Decides when a new zone counts. Right after a door the field reads a
    // wrong zone for a few frames (79 -> 0 -> 7, with the old position) while
    // the entry zone already says 7; settled readings always had zone ==
    // entry zone (docs/MAP.md). A new zone counts after `stable` readings in
    // a row that agree with the entry zone, or after `patient` readings of it
    // whatever the entry zone says.
    class MapZoneFilter
    {
    public:
        MapZoneFilter(u32 stable = 3, u32 patient = 30) : m_stable(stable), m_patient(patient) {}
        void Reset(void) { m_current = m_pending = -1; m_count = m_agree = 0; }
        // One reading. True if `zone` is now the zone to show (the first
        // reading always is). `ignored` is set to a zone given up on, else -1.
        bool Feed(u16 zone, u16 entryZone, int &ignored);
        int  Current(void) const { return m_current; }

    private:
        u32 m_stable, m_patient;
        int m_current = -1, m_pending = -1;
        u32 m_count = 0, m_agree = 0;
    };

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
    // Around it: the header window (place name, two text lines), the
    // recessed frame (MapRecess px), the compass window to its left.
    static constexpr UI::Rect MapHeader = { 20, 14, 360, 50 };
    static constexpr int MapRecess = 6;
    static constexpr UI::Rect MapCompassBox = { 18, 92, 44, 66 };

    void DrawMap(UI::Canvas &canvas, const MapView &view);
}
