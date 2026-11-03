#include "Where.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cmath>

#include "Scene.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2, main image. Found and checked as described in docs/RE4-map.md.
    static const u32 GameManagerPtr = 0x6A59C4;     // global -> game manager
    static const u32 SceneManagerPtr = 0x6A599C;    // global -> scene manager (docs/OFFSETS.md)
    static const u32 GmSceneManager = 0x1C;         // game manager -> scene manager (sanity check)
    static const u32 GmField = 0x24;                // game manager -> field object
    static const u32 GmLocation = 0x2C;             // game manager -> location object
    static const u32 FieldMoveModels = 0xCC;        // field object -> move model manager
    static const u32 VtMoveModelManager = 0x64C628;
    static const u32 ManagerModels = 0x0C;          // 35 model pointers; [0] is the player
    static const u32 VtPlayerModel = 0x64C538;
    static const u32 ModelPos = 0x3B0;              // float x, y, z
    static const u32 ModelRot = 0x424;              // quaternion x, y, z, w
    static const u32 LocEntryZone = 0x0A;           // u16, followed by the entry position
    static const u32 LocWork = 0x88;                // location -> field work
    static const u32 WorkPlayer = 0x18, WorkZone = 0x1C;

    // Zone data: field object -> loader (vtable 0x64BDD0) -> table of 0x54-byte
    // entries indexed by zone, size in bytes after the pointer.
    static const u32 FieldZoneLoader = 0x54;
    static const u32 VtZoneLoader = 0x64BDD0;
    static const u32 LoaderTable = 0x14, LoaderSize = 0x18;
    static const u32 ZoneEntrySize = 0x54;
    static const u32 ZoneParent = 0x0A, ZoneName = 0x1C, ZoneMapPoint = 0x44;   // s32 x, z
    static const u32 ZoneMapArea = 0x4E;            // u8, docs/MAP.md "Island"

    // Rotom Dex map: a FieldMenu CRO frame on the scene manager's stack holds
    // the map view (a main-image class).
    static const u32 ManagerSlots = 8;
    static const u32 VtFieldMenuFrame = 0x183A0;    // FieldMenu-relative
    static const u32 FrameMapView = 0xD0;
    static const u32 VtMapView = 0x64786C;
    static const u32 MapIndex = 0x170, MapCentre = 0x174, MapBounds = 0x17C;  // bounds: min x, min z, max x, max z
    static const u32 MapFlag = 0x19C, MapFlagShown = 0x1A4;

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static float ReadFloat(u32 address)
    {
        float v = 0;
        Process::ReadFloat(address, v);
        return v;
    }

    WhereState ReadWhere(void)
    {
        WhereState state;
        // Outside the field (battle, apps) the work's zone reads 0, a real zone.
        if (SceneModule(SceneManagerPtr) != "FieldRo")
            return state;
        const u32 gm = Read32(GameManagerPtr);
        if (gm == 0 || Read32(gm + GmSceneManager) != Read32(SceneManagerPtr))
            return state;
        const u32 field = Read32(gm + GmField);
        const u32 manager = field ? Read32(field + FieldMoveModels) : 0;
        if (manager == 0 || Read32(manager) != VtMoveModelManager)
            return state;
        const u32 player = Read32(manager + ManagerModels);
        if (player == 0 || Read32(player) != VtPlayerModel)
            return state;
        state.player = player;

        const u32 location = Read32(gm + GmLocation);
        const u32 work = location ? Read32(location + LocWork) : 0;
        if (work == 0 || Read32(work + WorkPlayer) != player)
            return state;
        state.entryZone = Read16(location + LocEntryZone);
        state.zone = Read32(work + WorkZone);

        const u32 loader = Read32(field + FieldZoneLoader);
        if (loader != 0 && Read32(loader) == VtZoneLoader)
        {
            const u32 table = Read32(loader + LoaderTable);
            const u32 count = Read32(loader + LoaderSize) / ZoneEntrySize;
            if (table != 0 && state.zone < count)
            {
                const u32 entry = table + state.zone * ZoneEntrySize;
                state.zoneData = true;
                state.parentZone = Read16(entry + ZoneParent);
                state.nameLine = Read16(entry + ZoneName);
                state.mapPointX = (s32)Read32(entry + ZoneMapPoint);
                state.mapPointZ = (s32)Read32(entry + ZoneMapPoint + 4);
                state.indoor = state.mapPointX != 0 || state.mapPointZ != 0;
                state.mapArea = Read8(entry + ZoneMapArea);
            }
        }

        state.x = ReadFloat(player + ModelPos);
        state.y = ReadFloat(player + ModelPos + 4);
        state.z = ReadFloat(player + ModelPos + 8);
        state.qy = ReadFloat(player + ModelRot + 4);
        state.qw = ReadFloat(player + ModelRot + 12);
        state.yaw = (int)std::lround(2.0f * std::atan2(state.qy, state.qw) * 180.0f / (float)M_PI);
        if (state.yaw > 180)
            state.yaw -= 360;
        if (state.yaw <= -180)
            state.yaw += 360;
        state.valid = true;

        const u32 sceneManager = Read32(SceneManagerPtr);
        for (u32 i = 0; sceneManager != 0 && i < ManagerSlots; i++)
        {
            const u32 object = Read32(sceneManager + i * 4);
            if (object == 0)
                break;
            const u32 vtable = Read32(object);
            const u32 module = CroModuleOf(vtable);
            if (module == 0 || vtable != module + VtFieldMenuFrame || CroModuleName(module) != "FieldMenu")
                continue;
            const u32 view = Read32(object + FrameMapView);
            if (view == 0 || Read32(view) != VtMapView)
                break;
            state.mapView = view;
            state.mapIndex = Read32(view + MapIndex);
            state.mapX = ReadFloat(view + MapCentre);
            state.mapZ = ReadFloat(view + MapCentre + 4);
            const float minX = ReadFloat(view + MapBounds), minZ = ReadFloat(view + MapBounds + 4);
            const float maxX = ReadFloat(view + MapBounds + 8), maxZ = ReadFloat(view + MapBounds + 12);
            const float followX = state.x < minX ? minX : state.x > maxX ? maxX : state.x;
            const float followZ = state.z < minZ ? minZ : state.z > maxZ ? maxZ : state.z;
            state.mapFixed = std::fabs(state.mapX - followX) > 1.0f || std::fabs(state.mapZ - followZ) > 1.0f;
            state.flag = Read8(view + MapFlagShown) != 0;
            state.flagX = ReadFloat(view + MapFlag);
            state.flagZ = ReadFloat(view + MapFlag + 4);
            break;
        }
        return state;
    }

    // "-12.5" style, one decimal (avoids relying on float printf support).
    static std::string Fixed1(float v)
    {
        const long tenths = std::lround(v * 10.0f);
        const long a = tenths < 0 ? -tenths : tenths;
        return Utils::Format("%s%ld.%ld", tenths < 0 ? "-" : "", a / 10, a % 10);
    }

    static const char *Compass(int yaw)
    {
        static const char *const names[] = {"S", "SE", "E", "NE", "N", "NW", "W", "SW"};
        const int sector = ((yaw + 360 + 22) % 360) / 45;
        return names[sector];
    }

    std::string DescribeWhere(const WhereState &state)
    {
        if (!state.valid)
            return state.player ? Utils::Format("where: player %08lX, zone not found", state.player)
                                : "where: not in the field";
        std::string out = Utils::Format("where: zone %u (entry %u", state.zone, state.entryZone);
        if (state.zoneData)
        {
            out += Utils::Format(", parent %u) name line %u area %u %s", state.parentZone, state.nameLine,
                                 state.mapArea, state.indoor ? "indoor" : "outdoor");
            if (state.indoor)
                out += Utils::Format(" at %ld %ld", state.mapPointX, state.mapPointZ);
        }
        else
            out += ") no zone data";
        out += Utils::Format(" pos %s %s %s facing %d %s player %08lX", Fixed1(state.x).c_str(),
                             Fixed1(state.y).c_str(), Fixed1(state.z).c_str(), state.yaw, Compass(state.yaw),
                             state.player);
        if (state.mapView == 0)
            return out + " | map: none";
        out += Utils::Format(" | map %lu centre %s %s %s", state.mapIndex, Fixed1(state.mapX).c_str(),
                             Fixed1(state.mapZ).c_str(), state.mapFixed ? "fixed" : "follows");
        if (state.flag)
            out += Utils::Format(" flag %s %s", Fixed1(state.flagX).c_str(), Fixed1(state.flagZ).c_str());
        return out;
    }
}
