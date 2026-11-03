#pragma once

// The native PC boxes (P2-6): the actions that drive the game's Box screen,
// what A / L / R do where, and drawing. Pure C++ (no CTRPF), so tests/ can
// build it on the PC; Screens/Boxes.cpp runs it in the plugin. See
// docs/BOXES.md.

#include <string>

#include "ActionQueue.hpp"
#include "BoxData.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // Layout button tapped for a cursor index: "box.slot_N" (0-29) or
    // "box.party_N" (30-35); nullptr for anything else.
    const char *BoxesSlotButton(u32 cursor);

    // Pick up the Pokémon at `cursor`: tap its slot (the game opens its slot
    // menu), tap Move; done when the game holds it. Starts with the Box
    // screen idle.
    const ActionDef &BoxesPickAction(u32 cursor);
    // Drop the held Pokémon at `cursor` (a Pokémon already there swaps
    // places); done when the Box screen is idle again.
    const ActionDef &BoxesDropAction(u32 cursor);
    // Previous / next box: tap the arrow next to the box name. Works with or
    // without a held Pokémon.
    const ActionDef &BoxesTurnAction(bool next);

    // What a key does on the Box screen.
    enum class BoxesCommand : u8 { None, Pick, Drop, PrevBox, NextBox, Details };
    // `occupied`: a Pokémon is at the cursor (box or party).
    BoxesCommand BoxesCommandFor(u32 key, BoxMode mode, u32 cursor, bool occupied);
    // HID key bits (same values as CTRPF's Key::) the screen takes for itself.
    static const u32 BoxesKeyA = 1u << 0;
    static const u32 BoxesKeyR = 1u << 8;
    static const u32 BoxesKeyL = 1u << 9;
    static const u32 BoxesKeyX = 1u << 10;
    static const u32 BoxesKeyY = 1u << 11;

    // What the native screen shows.
    struct BoxesView
    {
        std::string boxName;
        u32 tray = 0, unlocked = 1;
        std::string slots[BoxSlots];        // labels; empty = no Pokémon
        std::string party[BoxPartySlots];
        u32 cursor = 0;                     // the game's cursor (BoxData.hpp)
        BoxMode mode = BoxMode::Idle;
        std::string held;                   // carrying: what (empty: unknown)
        int heldTray = -1, heldIndex = -1;  // carrying: where it came from (cursor index)
        std::string busy;                   // an action is running: what it does
    };

    // Screen area (top screen) and corner radius; cells by cursor index
    // (0-35 slots and party, 36 the box name).
    static constexpr UI::Rect BoxesArea = { 8, 6, 384, 228 };
    static constexpr int BoxesRadius = 12;
    UI::Rect BoxesCellRect(u32 index);
    // The footer line: hints, or what is happening.
    std::string BoxesFooter(const BoxesView &view);
    void DrawBoxes(UI::Canvas &canvas, const BoxesView &view);

    // Details: the native screen steps aside so the game's own top screen
    // (the Pokémon at its cursor) shows; only this badge stays.
    static constexpr UI::Rect BoxesBadgeArea = { 296, 4, 100, 22 };
    void DrawBoxesBadge(UI::Canvas &canvas);
}
