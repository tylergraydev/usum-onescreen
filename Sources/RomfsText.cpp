#include "RomfsText.hpp"

#include <cstring>

namespace OneScreen
{
    static const u32 None = 0xFFFFFFFF;

    static u16 Get16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
    static u32 Get32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
    static uint64_t Get64(const u8 *p) { return (uint64_t)Get32(p) | ((uint64_t)Get32(p + 4) << 32); }

    static bool ReadBytes(const RomfsRead &read, uint64_t offset, std::vector<u8> &out, u32 size)
    {
        out.resize(size);
        return size == 0 || read(offset, out.data(), size);
    }

    // A directory or file entry's name (UTF-16) compared with an ASCII name.
    static bool NameIs(const std::vector<u8> &meta, u32 nameAt, const std::string &name)
    {
        if (nameAt + 4 > meta.size())
            return false;
        const u32 length = Get32(&meta[nameAt]);
        if (length != name.size() * 2 || nameAt + 4 + length > meta.size())
            return false;
        for (size_t i = 0; i < name.size(); i++)
            if (Get16(&meta[nameAt + 4 + i * 2]) != (u8)name[i])
                return false;
        return true;
    }

    // Level 3 of the RomFS: header (0x28), then the directory and file
    // metadata tables. Directory entry: parent, sibling, first child dir,
    // first file, hash link, name length, name. File entry: parent, sibling,
    // u64 data offset, u64 size, hash link, name length, name.
    bool RomfsFind(const RomfsRead &read, const std::string &path, uint64_t &offset, uint64_t &size,
                   std::string &error)
    {
        u8 head[0x60];
        if (!read(0, head, sizeof(head)))
        {
            error = "can't read the RomFS header";
            return false;
        }
        uint64_t level3 = 0;
        if (std::memcmp(head, "IVFC", 4) == 0)
        {
            const u32 masterSize = Get32(head + 0x08), blockLog = Get32(head + 0x4C);
            if (blockLog > 20)
            {
                error = "bad IVFC header";
                return false;
            }
            const uint64_t block = 1ull << blockLog;
            level3 = (0x60 + masterSize + block - 1) / block * block;
            if (!read(level3, head, 0x28))
            {
                error = "can't read level 3";
                return false;
            }
        }
        if (Get32(head) != 0x28)
        {
            error = "not a RomFS level 3 header";
            return false;
        }
        const u32 dirMetaAt = Get32(head + 0x0C), dirMetaSize = Get32(head + 0x10);
        const u32 fileMetaAt = Get32(head + 0x1C), fileMetaSize = Get32(head + 0x20);
        const u32 dataAt = Get32(head + 0x24);
        if (dirMetaSize > 0x100000 || fileMetaSize > 0x400000)
        {
            error = "RomFS tables too big";
            return false;
        }
        std::vector<u8> dirs, files;
        if (!ReadBytes(read, level3 + dirMetaAt, dirs, dirMetaSize)
            || !ReadBytes(read, level3 + fileMetaAt, files, fileMetaSize))
        {
            error = "can't read the RomFS tables";
            return false;
        }

        u32 dir = 0;
        size_t start = 0;
        while (true)
        {
            const size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            const bool last = slash == std::string::npos;
            if (dir + 0x18 > dirs.size())
            {
                error = "bad RomFS directory entry";
                return false;
            }
            // Walk the siblings; the step limit guards against loops.
            u32 entry = Get32(&dirs[dir + (last ? 0x0C : 0x08)]);
            const std::vector<u8> &table = last ? files : dirs;
            const u32 nameAt = last ? 0x1C : 0x14;
            u32 steps = 0;
            while (entry != None && entry + nameAt <= table.size() && !NameIs(table, entry + nameAt, part)
                   && ++steps < 100000)
                entry = Get32(&table[entry + 0x04]);
            if (entry == None || entry + nameAt > table.size() || steps >= 100000)
            {
                error = "no " + path + " in the RomFS";
                return false;
            }
            if (last)
            {
                offset = level3 + dataAt + Get64(&files[entry + 0x08]);
                size = Get64(&files[entry + 0x10]);
                return true;
            }
            dir = entry;
            start = slash + 1;
        }
    }

    // GARC (version 6): "CRAG" header (its size at +4, the data offset at
    // +0x10), then "OTAF" (size at +4) and "BTAF" (size at +4, count at +8;
    // per entry a variant bitmask, then start, end, length per set bit).
    bool GarcEntry(const RomfsRead &read, uint64_t garc, u32 index, uint64_t &offset, u32 &size,
                   std::string &error)
    {
        u8 head[0x1C];
        if (!read(garc, head, sizeof(head)) || std::memcmp(head, "CRAG", 4) != 0)
        {
            error = "not a GARC";
            return false;
        }
        const u32 headerSize = Get32(head + 4), dataAt = Get32(head + 0x10);
        u8 chunk[12];
        if (!read(garc + headerSize, chunk, 8) || std::memcmp(chunk, "OTAF", 4) != 0)
        {
            error = "GARC: no OTAF";
            return false;
        }
        const uint64_t btafAt = garc + headerSize + Get32(chunk + 4);
        if (!read(btafAt, chunk, 12) || std::memcmp(chunk, "BTAF", 4) != 0)
        {
            error = "GARC: no BTAF";
            return false;
        }
        const u32 btafSize = Get32(chunk + 4), count = Get32(chunk + 8);
        if (index >= count || btafSize > 0x100000 || btafSize < 12)
        {
            error = "GARC: no entry " + std::to_string(index);
            return false;
        }
        std::vector<u8> btaf;
        if (!ReadBytes(read, btafAt, btaf, btafSize))
        {
            error = "GARC: can't read BTAF";
            return false;
        }
        size_t at = 12;
        for (u32 i = 0; i <= index; i++)
        {
            if (at + 4 > btaf.size())
            {
                error = "GARC: BTAF cut short";
                return false;
            }
            const u32 bits = Get32(&btaf[at]);
            at += 4;
            const size_t first = at;
            for (u32 bit = 0; bit < 32; bit++)
                if ((bits >> bit) & 1)
                    at += 12;
            if (i == index)
            {
                if (bits == 0 || at > btaf.size())
                {
                    error = "GARC: entry " + std::to_string(index) + " is empty";
                    return false;
                }
                offset = garc + dataAt + Get32(&btaf[first]);
                size = Get32(&btaf[first + 8]);
                return true;
            }
        }
        return false;
    }

    // Header: u16 sections, u16 lines, u32 size, u32 key, u32 section offsets.
    // Section: u32 size, per line {u32 offset from the section, u16 length in
    // chars, u16 extra}, then the encrypted text.
    bool MsgFileLines(const std::vector<u8> &file, std::vector<std::string> &lines, std::string &error)
    {
        lines.clear();
        if (file.size() < 0x10 || Get16(&file[0]) == 0)
        {
            error = "message file too short";
            return false;
        }
        const u32 count = Get16(&file[2]);
        const u32 section = Get32(&file[0x0C]);
        if (section + 4 + (uint64_t)count * 8 > file.size())
        {
            error = "message file: line table out of range";
            return false;
        }
        for (u32 line = 0; line < count; line++)
        {
            const u8 *entry = &file[section + 4 + line * 8];
            const u32 at = section + Get32(entry);
            u32 length = Get16(entry + 4);
            if (length > MsgLineMax)
                length = MsgLineMax;
            if (at + (uint64_t)length * 2 > file.size())
            {
                error = "message file: line " + std::to_string(line) + " out of range";
                return false;
            }
            u16 encrypted[MsgLineMax], plain[MsgLineMax];
            for (u32 i = 0; i < length; i++)
                encrypted[i] = Get16(&file[at + i * 2]);
            DecryptMsgLine(encrypted, length, line, plain, MsgLineMax);
            char utf8[MsgLineMax * 3 + 1];
            Utf16ToUtf8(plain, MsgLineMax, utf8, sizeof(utf8));
            lines.push_back(utf8);
        }
        return true;
    }

    bool ReadMessageFile(const RomfsRead &read, const std::string &garcPath, u32 dat,
                         std::vector<std::string> &lines, std::string &error)
    {
        uint64_t garc = 0, garcSize = 0, at = 0;
        u32 size = 0;
        if (!RomfsFind(read, garcPath, garc, garcSize, error) || !GarcEntry(read, garc, dat, at, size, error))
            return false;
        if (size > 0x100000 || at + size > garc + garcSize)
        {
            error = "message file " + std::to_string(dat) + " out of range";
            return false;
        }
        std::vector<u8> file;
        if (!ReadBytes(read, at, file, size))
        {
            error = "can't read message file " + std::to_string(dat);
            return false;
        }
        return MsgFileLines(file, lines, error);
    }
}
