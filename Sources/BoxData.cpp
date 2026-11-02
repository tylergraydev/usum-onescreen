#include "BoxData.hpp"

#include <cstdio>

namespace OneScreen
{
    BoxCursorKind BoxCursorKindOf(u32 cursor)
    {
        if (cursor < BoxSlots)
            return BoxCursorKind::Slot;
        if (cursor < BoxCursorParty + BoxPartySlots)
            return BoxCursorKind::Party;
        if (cursor == BoxCursorName)
            return BoxCursorKind::Name;
        if (cursor == BoxCursorBoxList)
            return BoxCursorKind::BoxList;
        return BoxCursorKind::Other;
    }

    BoxMode BoxModeFromListener(u32 offset)
    {
        switch (offset)
        {
        case BoxCroIdle:     return BoxMode::Idle;
        case BoxCroModal:    return BoxMode::Modal;
        case BoxCroCarrying: return BoxMode::Carrying;
        default:             return BoxMode::Unknown;
        }
    }

    const char *BoxModeName(BoxMode mode)
    {
        static const char *const names[] = {"unknown", "idle", "modal", "carrying"};
        return (u32)mode < 4 ? names[(u32)mode] : "?";
    }

    std::string BoxNameText(const u16 *units, u32 tray)
    {
        char utf8[3 * BoxNameUnits + 1];
        Utf16ToUtf8(units, BoxNameUnits, utf8, sizeof(utf8));
        if (utf8[0] != '\0')
            return utf8;
        char fallback[16];
        std::snprintf(fallback, sizeof(fallback), "Box %u", (unsigned)(tray + 1));
        return fallback;
    }

    std::string BoxMonLabel(const Pk7 &mon)
    {
        if (mon.form == Pk7Form::Invalid || mon.species == 0)
            return "";
        if (mon.isEgg)
            return "Egg";
        char utf8[3 * 13 + 1];
        Utf16ToUtf8(mon.nickname, 13, utf8, sizeof(utf8));
        return utf8;
    }

    u32 BoxUnlockedCount(u8 stored)
    {
        return stored == 0 ? 1 : stored > BoxCount ? BoxCount : stored;
    }
}
