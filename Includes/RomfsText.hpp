#pragma once

// Reading game text straight from the game's own RomFS at runtime, so the
// plugin ships no game text (docs/MAP.md, "Place names"): the RomFS file
// tree (IVFC level 3), a GARC archive inside it, and one message file of that
// archive decoded into lines. Pure functions over a read callback, with no
// CTRPF dependency, so tests/ can build them on the PC; Screens/Map.cpp gives
// them the game's RomFS (ARCHIVE_ROMFS).

#include <functional>
#include <string>
#include <vector>

#include "PartyData.hpp"

namespace OneScreen
{
    // Reads `size` bytes at `offset` of the RomFS image; false on failure.
    using RomfsRead = std::function<bool(uint64_t offset, void *dst, u32 size)>;

    // Finds `path` ("a/0/3/2") in a RomFS image that starts either at its
    // IVFC header or at level 3 (what ARCHIVE_ROMFS gives). On success,
    // `offset` and `size` are the file's bytes in the image.
    bool RomfsFind(const RomfsRead &read, const std::string &path, uint64_t &offset, uint64_t &size,
                   std::string &error);

    // Entry `index` (its first variant) of the GARC at `garc` in the image.
    bool GarcEntry(const RomfsRead &read, uint64_t garc, u32 index, uint64_t &offset, u32 &size,
                   std::string &error);

    // Every line of a message file (gfl2::str::MsgData data, docs/RE2-text.md)
    // as UTF-8, control tags removed (a line that is only a tag is empty).
    bool MsgFileLines(const std::vector<u8> &file, std::vector<std::string> &lines, std::string &error);

    // All of the above: lines of message file `dat` in the GARC at `garcPath`.
    bool ReadMessageFile(const RomfsRead &read, const std::string &garcPath, u32 dat,
                         std::vector<std::string> &lines, std::string &error);
}
