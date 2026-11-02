#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // Where the player is in the overworld (see docs/RE4-map.md).
    struct WhereState
    {
        bool valid = false;     // the objects checked out (field only)
        u32 player = 0;         // the player's move model
        u16 zone = 0;           // current zone ID
        u16 entryZone = 0;      // zone of the last map entry / load point
        // Zone table entry (the game's zone data, one 0x54-byte entry per zone).
        bool zoneData = false;
        u16 parentZone = 0;     // shared by sub-zones of one area
        u16 nameLine = 0;       // place name: line in message file 72 (a/0/3/<lang>); line + 1 is the sub-name
        bool indoor = false;    // the zone has a fixed Rotom map point (interiors); outdoor zones use world coordinates
        s32 mapPointX = 0, mapPointZ = 0;   // that point, in world units of the island map
        float x = 0, y = 0, z = 0;  // world position; +x east, +z south (Rotom map right / down)
        float qy = 0, qw = 1;   // facing quaternion (rotation about y)
        int yaw = 0;            // facing in degrees: 0 south, 90 east, 180 north, -90 west
        // Rotom Dex map (FieldMenu's bottom-screen map view), if it is up.
        u32 mapView = 0;
        u32 mapIndex = 0;       // map table entry; 0-3 the islands (0 on Melemele)
        float mapX = 0, mapZ = 0;   // map centre in world units (the player, clamped)
        bool mapFixed = false;  // the centre is not the player clamped to the map bounds (cross-check for indoor)
        bool flag = false;      // goal flag shown
        float flagX = 0, flagZ = 0;
    };

    WhereState ReadWhere(void);
    std::string DescribeWhere(const WhereState &state);
}
