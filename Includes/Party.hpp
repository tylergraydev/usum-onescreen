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

    // Line `line` of the full-load MsgData object at `msg` whose dat ID field
    // (+0xC) is `dat` (0 for files an app loaded from its own buffer), UTF-8.
    // Empty if the object or the line does not check out.
    std::string MsgLine(u32 msg, u32 dat, u32 line);

    // Log lines for the `party` and `bag` commands.
    std::string DescribeParty(const PartyState &party);
    std::string DescribeBag(const BagState &bag);
}
