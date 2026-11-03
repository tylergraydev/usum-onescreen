#pragma once

// Move data (type, category, power, accuracy, PP) from the game's own RomFS,
// so the plugin ships none of it: GARC `a/0/1/1` (Ultra Sun; the v1.2 update
// doesn't replace it) has one entry, a "WD" pack of 729 records of 0x28
// bytes, one per move ID (docs/MOVE-LEARN.md). Pure, with no CTRPF
// dependency, so tests/ can build it on the PC.

#include <string>
#include <vector>

#include "RomfsText.hpp"

namespace OneScreen
{
    // GARC path and entry of the move data.
    extern const char *const MoveDataGarc;  // "a/0/1/1"

    struct MoveInfo
    {
        bool known = false;
        u8 type = 0xFF;      // 0 Normal ... 17 Fairy
        u8 category = 0xFF;  // 0 status, 1 physical, 2 special
        u8 power = 0;        // 0 for status moves (and a few with varying power)
        u8 accuracy = 0;     // 101 = never misses
        u8 pp = 0;           // base PP
    };

    // Splits a "WD" pack (the GARC entry) into one MoveInfo per record.
    bool ParseMovePack(const std::vector<u8> &pack, std::vector<MoveInfo> &moves, std::string &error);

    // Reads and parses the pack from a RomFS image.
    bool ReadMoveData(const RomfsRead &read, std::vector<MoveInfo> &moves, std::string &error);

    // "Physical", "Special", "Status", or "?".
    const char *MoveCategoryName(u8 category);
}
