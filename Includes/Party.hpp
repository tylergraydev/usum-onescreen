#pragma once

#include <string>

#include <3ds/types.h>

#include "PartyData.hpp"

namespace OneScreen
{
    // The player's party as the game keeps it (pml::PokeParty), see
    // docs/RE3-party-bag.md. Valid in every scene after the save is loaded.
    struct PartySlot
    {
        u32 address = 0;    // pml::pokepara::PokemonParam
        Pk7 mon;
    };

    struct PartyState
    {
        bool valid = false;
        u32 party = 0;      // pml::PokeParty
        u8  count = 0;
        PartySlot slots[6];
        // While the Party screen (X menu -> Pokémon) is open it shows the
        // party through its own order: screen position i shows slots[order[i]].
        // Moves made there reach slots[] only when the screen closes.
        bool partyScreen = false;
        u8  order[6] = {0, 1, 2, 3, 4, 5};
        // The Party screen's cursor (screen position), -1 if not up.
        s8  screenCursor = -1;
    };

    // Bag pockets in the order of the game's pocket table (MyItem +0xE2C).
    // The Bag's tabs run Medicine, Items, TMs, Berries, Key Items, Free Space
    // (a view of items with a free-space position), Rotom Powers; the
    // Z-Crystals tab is hidden while that pocket is empty, so where it sits
    // was not seen.
    enum class Pocket : u8 { Medicine, Items, TMs, Berries, KeyItems, ZCrystals, RotomPowers, Count };

    struct BagState
    {
        bool valid = false;
        u32 myItem = 0;                         // Savedata::MyItem
        u32 pocket[(int)Pocket::Count] = {};    // first slot of each pocket
        u16 capacity[(int)Pocket::Count] = {};  // slots per pocket
    };

    PartyState ReadParty(u32 sceneManager);
    BagState ReadBag(void);

    // Slot `index` of a pocket (index < capacity). Empty slots have item 0.
    BagSlot ReadBagSlot(const BagState &bag, Pocket pocket, u32 index);

    const char *PocketName(Pocket pocket);

    // Names from the game's resident message files (docs/RE2-text.md), UTF-8.
    // Empty if the text is not there or does not check out.
    std::string SpeciesName(u16 species);
    std::string ItemName(u16 item);

    // The item list of the Bag scene (field Bag, and the Party screen's
    // Restore): the shown pocket's items (count-0 slots left out) and where
    // its cursor is. The Bag keeps cursor and scroll per pocket, also across
    // tab changes. See docs/OFFSETS.md.
    struct BagListState
    {
        bool valid = false;
        u32 count = 0;      // items in the list
        u32 rows = 0;       // rows shown (7)
        u32 row = 0;        // cursor row within the shown rows
        float scroll = 0;   // index of the first shown row (animates between whole numbers)
    };

    BagListState ReadBagList(u32 sceneManager);

    // The summary (Status scene) keeps a WordSet loader with the move (dat
    // 118), ability (101) and nature (92) names. 0 if the current scene is
    // not the summary or the loader doesn't check out.
    u32 SummaryWordLoader(u32 sceneManager);
    static const u32 MoveNameDat = 118, AbilityNameDat = 101, NatureNameDat = 92;
    // Line `line` of the loader's file `dat` (UTF-8), empty if not there.
    std::string LoaderText(u32 loader, u32 dat, u32 line);

    // Log lines for the `party` and `bag` commands.
    std::string DescribeParty(const PartyState &party);
    std::string DescribeBag(const BagState &bag);
}
