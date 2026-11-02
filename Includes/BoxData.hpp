#pragma once

// PC boxes (docs/RE6-boxes.md): where the box data sits in the save data,
// what the open Box screen's cursor indices and states mean, and decoding of
// box names. Pure functions and constants with no CTRPF dependency, so tests/
// can build them on the PC; Box.cpp reads the game's memory with them.

#include <string>

#include "PartyData.hpp"

namespace OneScreen
{
    static const u32 BoxCount     = 32;     // trays in the save, unlocked or not
    static const u32 BoxSlots     = 30;     // per tray: 6 columns x 5 rows
    static const u32 BoxColumns   = 6;
    static const u32 BoxRows      = 5;
    static const u32 BoxPartySlots = 6;
    static const u32 BoxNameUnits = 17;     // UTF-16 units per name, 0-terminated (0x22 bytes)

    // Savedata::BOX (box names, team names, wallpapers, unlocked count) and
    // Savedata::BoxPokemon (32 x 30 PK7 cores of 0xE8 bytes, stored format)
    // live inside Savedata::SaveData; offsets from the SaveData object.
    static const u32 SaveDataBoxLayout = 0x3B98;   // vtable 0x64F364 (N8Savedata3BOXE)
    static const u32 SaveDataBoxPokemon = 0x4184;  // vtable 0x64F044 (N8Savedata10BoxPokemonE)
    // Inside Savedata::BOX (offsets from its object, data starts at +4).
    static const u32 BoxLayoutNames = 0x004;       // 32 x 0x22 bytes
    static const u32 BoxLayoutUnlocked = 0x5E5;    // u8: boxes the player can use
    // Inside Savedata::BoxPokemon.
    static const u32 BoxPokemonData = 0x004;

    // Byte offset of (tray, slot) in the BoxPokemon data.
    inline u32 BoxSlotOffset(u32 tray, u32 slot)
    {
        return (tray * BoxSlots + slot) * Pk7CoreSize;
    }

    // The open Box screen's cursor (App::Box::BoxLowBgDraw +0xDC): box slots
    // 0-29 row by row, the party 30-35 (left column 30, 32, 34, right 31,
    // 33, 35, as the game compacts it), the box name 36, the box list button
    // 38. Other values were not seen.
    static const u32 BoxCursorParty   = 30;
    static const u32 BoxCursorName    = 36;
    static const u32 BoxCursorBoxList = 38;

    enum class BoxCursorKind : u8 { Slot, Party, Name, BoxList, Other };
    BoxCursorKind BoxCursorKindOf(u32 cursor);

    // What the Box screen is doing, from the listener BoxLowBgDraw +0x8C
    // points to (its vtable, as an offset into the Box CRO module).
    enum class BoxMode : u8
    {
        Unknown,    // not the Box screen, or a listener not seen
        Idle,       // BoxLowBgFrame::DInnerNormalListener: browsing
        Modal,      // BoxLowBgFrame::DInnerNoneListener: the slot menu or a message is up
        Carrying,   // BoxNormalState::DInnerPokeMoveListener: a Pokémon is held
    };
    static const u32 BoxCroLowBgDraw   = 0x2F4D8;  // App::Box::BoxLowBgDraw vtable
    static const u32 BoxCroIdle        = 0x2F6F0;
    static const u32 BoxCroModal       = 0x2F6B0;
    static const u32 BoxCroCarrying    = 0x2FA80;
    BoxMode BoxModeFromListener(u32 listenerVtableOffset);
    const char *BoxModeName(BoxMode mode);

    // A box name (UTF-16 as stored, up to BoxNameUnits units) as UTF-8. An
    // empty name comes back as "Box N" (tray + 1), as the game shows it.
    std::string BoxNameText(const u16 *units, u32 tray);

    // The name to show for a Pokémon: its nickname, "Egg" for eggs, empty if
    // the slot is empty (species 0) or unreadable.
    std::string BoxMonLabel(const Pk7 &mon);

    // Unlocked box count from the save byte, kept to 1..BoxCount.
    u32 BoxUnlockedCount(u8 stored);
}
