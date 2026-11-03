#include "MoveData.hpp"

namespace OneScreen
{
    const char *const MoveDataGarc = "a/0/1/1";

    static u16 Get16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
    static u32 Get32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

    bool ParseMovePack(const std::vector<u8> &pack, std::vector<MoveInfo> &moves, std::string &error)
    {
        moves.clear();
        if (pack.size() < 4 || pack[0] != 'W' || pack[1] != 'D')
        {
            error = "move data: not a WD pack";
            return false;
        }
        const u32 count = Get16(&pack[2]);
        if (count == 0 || 4 + (count + 1) * 4 > pack.size())
        {
            error = "move data: bad record count";
            return false;
        }
        moves.resize(count);
        for (u32 i = 0; i < count; i++)
        {
            const u32 start = Get32(&pack[4 + i * 4]), end = Get32(&pack[8 + i * 4]);
            if (start > end || end > pack.size() || end - start < 6)
            {
                moves.clear();
                error = "move data: bad record offsets";
                return false;
            }
            const u8 *r = &pack[start];
            MoveInfo &m = moves[i];
            m.known = true;
            m.type = r[0];
            m.category = r[2];
            m.power = r[3];
            m.accuracy = r[4];
            m.pp = r[5];
        }
        return true;
    }

    bool ReadMoveData(const RomfsRead &read, std::vector<MoveInfo> &moves, std::string &error)
    {
        uint64_t garc = 0, garcSize = 0;
        if (!RomfsFind(read, MoveDataGarc, garc, garcSize, error))
            return false;
        uint64_t offset = 0;
        u32 size = 0;
        if (!GarcEntry(read, garc, 0, offset, size, error))
            return false;
        if (size > 0x20000)
        {
            error = "move data: entry too large";
            return false;
        }
        std::vector<u8> pack(size);
        if (size == 0 || !read(offset, pack.data(), size))
        {
            error = "move data: read failed";
            return false;
        }
        return ParseMovePack(pack, moves, error);
    }

    const char *MoveCategoryName(u8 category)
    {
        switch (category)
        {
        case 0:  return "Status";
        case 1:  return "Physical";
        case 2:  return "Special";
        default: return "?";
        }
    }
}
