#include "PartyData.hpp"

#include <cstring>

namespace OneScreen
{
    static const u32 BlockStart = 8, BlockSize = 0x38;

    // Block order per shuffle value: plain block b is stored at position
    // BlockPosition[sv * 4 + b].
    static const u8 BlockPosition[24 * 4] = {
        0, 1, 2, 3,  0, 1, 3, 2,  0, 2, 1, 3,  0, 3, 1, 2,  0, 2, 3, 1,  0, 3, 2, 1,
        1, 0, 2, 3,  1, 0, 3, 2,  2, 0, 1, 3,  3, 0, 1, 2,  2, 0, 3, 1,  3, 0, 2, 1,
        1, 2, 0, 3,  1, 3, 0, 2,  2, 1, 0, 3,  3, 1, 0, 2,  2, 3, 0, 1,  3, 2, 0, 1,
        1, 2, 3, 0,  1, 3, 2, 0,  2, 1, 3, 0,  3, 1, 2, 0,  2, 3, 1, 0,  3, 2, 1, 0,
    };

    static u16 Get16(const u8 *p, u32 offset)
    {
        return (u16)(p[offset] | (p[offset + 1] << 8));
    }

    static u32 Get32(const u8 *p, u32 offset)
    {
        return (u32)Get16(p, offset) | ((u32)Get16(p, offset + 2) << 16);
    }

    static void Put16(u8 *p, u32 offset, u16 v)
    {
        p[offset] = (u8)v;
        p[offset + 1] = (u8)(v >> 8);
    }

    static u32 ShuffleValue(u32 encryptionConstant)
    {
        return ((encryptionConstant >> 13) & 31) % 24;
    }

    void Pk7Crypt(u8 *data, u32 size, u32 seed)
    {
        // Same as the game's 0x320BBC.
        for (u32 i = 0; i + 1 < size; i += 2)
        {
            seed = seed * 0x41C64E6D + 0x6073;
            Put16(data, i, Get16(data, i) ^ (u16)(seed >> 16));
        }
    }

    u16 Pk7Checksum(const u8 *core)
    {
        u16 sum = 0;
        for (u32 i = BlockStart; i < Pk7CoreSize; i += 2)
            sum = (u16)(sum + Get16(core, i));
        return sum;
    }

    static void Unshuffle(const u8 *in, u8 *out, u32 sv)
    {
        std::memcpy(out, in, BlockStart);
        for (u32 b = 0; b < 4; b++)
            std::memcpy(out + BlockStart + b * BlockSize, in + BlockStart + BlockPosition[sv * 4 + b] * BlockSize,
                        BlockSize);
    }

    static bool Plausible(const u8 *core)
    {
        const u16 species = Get16(core, 0x08);
        return species != 0 && species <= 807 && Pk7Checksum(core) == Get16(core, 0x06);
    }

    Pk7 DecodePk7(const u8 *core, const u8 *calc)
    {
        Pk7 mon;
        const u32 ec = Get32(core, 0);
        const u32 sv = ShuffleValue(ec);
        u8 work[Pk7CoreSize], plain[Pk7CoreSize];

        // Resting state first: XOR-encrypted, then the accessor's decrypted state.
        std::memcpy(work, core, Pk7CoreSize);
        Pk7Crypt(work + BlockStart, Pk7CoreSize - BlockStart, ec);
        Unshuffle(work, plain, sv);
        if (Plausible(plain))
            mon.form = Pk7Form::Encrypted;
        else
        {
            Unshuffle(core, plain, sv);
            if (!Plausible(plain))
                return mon;
            mon.form = Pk7Form::Decrypted;
        }

        mon.encryptionConstant = ec;
        mon.species = Get16(plain, 0x08);
        mon.heldItem = Get16(plain, 0x0A);
        mon.ability = plain[0x14];
        mon.nature = plain[0x1C];
        mon.gender = (plain[0x1D] >> 1) & 3;
        for (u32 i = 0; i < 12; i++)
            mon.nickname[i] = Get16(plain, 0x40 + i * 2);
        mon.nickname[12] = 0;
        for (u32 i = 0; i < 4; i++)
        {
            mon.moves[i] = Get16(plain, 0x5A + i * 2);
            mon.pp[i] = plain[0x62 + i];
        }
        mon.isEgg = (Get32(plain, 0x74) >> 30) & 1;
        mon.ball = plain[0xDC];

        if (calc != nullptr)
        {
            u8 stats[Pk7CalcSize];
            std::memcpy(stats, calc, Pk7CalcSize);
            if (mon.form == Pk7Form::Encrypted)
                Pk7Crypt(stats, Pk7CalcSize, ec);
            mon.status = Get32(stats, 0x00);
            mon.level = stats[0x04];
            mon.hp = Get16(stats, 0x08);
            mon.maxHp = Get16(stats, 0x0A);
            mon.calcValid = mon.level >= 1 && mon.level <= 100 && mon.maxHp != 0 && mon.hp <= mon.maxHp;
        }
        return mon;
    }

    void EncodePk7(u8 *core, u8 *calc)
    {
        const u32 ec = Get32(core, 0);
        const u32 sv = ShuffleValue(ec);
        Put16(core, 0x06, Pk7Checksum(core));
        u8 shuffled[Pk7CoreSize];
        std::memcpy(shuffled, core, BlockStart);
        for (u32 b = 0; b < 4; b++)
            std::memcpy(shuffled + BlockStart + BlockPosition[sv * 4 + b] * BlockSize, core + BlockStart + b * BlockSize,
                        BlockSize);
        Pk7Crypt(shuffled + BlockStart, Pk7CoreSize - BlockStart, ec);
        std::memcpy(core, shuffled, Pk7CoreSize);
        if (calc != nullptr)
            Pk7Crypt(calc, Pk7CalcSize, ec);
    }

    BagSlot DecodeBagSlot(u32 word)
    {
        BagSlot slot;
        slot.item = word & 0x3FF;
        slot.count = (word >> 10) & 0x3FF;
        slot.freeSpace = (word >> 20) & 0x3FF;
        slot.isNew = (word >> 30) & 1;
        return slot;
    }

    u32 DecryptMsgLine(const u16 *encrypted, u32 length, u32 line, u16 *out, u32 cap)
    {
        if (cap == 0)
            return 0;
        // Same as the game's 0x4B877C: key starts at 0x2983 * (line + 3) and
        // rotates left by 3 per character.
        u16 key = (u16)(0x2983 * (line + 3));
        u16 plain[MsgLineMax];
        if (length > MsgLineMax)
            length = MsgLineMax;
        for (u32 i = 0; i < length; i++)
        {
            plain[i] = encrypted[i] ^ key;
            key = (u16)((key << 3) | (key >> 13));
        }

        // Tags are 0x0010, word count, then that many words (tag ID first).
        // Plural tag 0x1101 (item names, file 42): the last argument's low
        // byte is the length of the singular ending that follows the tag, the
        // high byte the length of the plural ending after that
        // ("Oran Berr" [1101 00FE 0301] "y" "ies"). Keep the singular.
        u32 n = 0;
        for (u32 i = 0; i < length && plain[i] != 0 && n + 1 < cap;)
        {
            if (plain[i] != 0x0010)
            {
                out[n++] = plain[i++];
                continue;
            }
            const u32 words = i + 1 < length ? plain[i + 1] : 0;
            const u32 end = i + 2 + words;
            if (words == 0 || end > length)
                break;
            i = end;
            if (plain[end - words] == 0x1101)
            {
                const u32 singular = plain[end - 1] & 0xFF, plural = plain[end - 1] >> 8;
                for (u32 k = 0; k < singular && i < length && n + 1 < cap; k++)
                    out[n++] = plain[i++];
                i += plural;
            }
        }
        out[n] = 0;
        return n;
    }

    u32 Utf16ToUtf8(const u16 *text, u32 cap, char *out, u32 outSize)
    {
        if (outSize == 0)
            return 0;
        u32 n = 0;
        for (u32 i = 0; i < cap && text[i] != 0; i++)
        {
            const u16 c = text[i];
            char buf[3];
            u32 len;
            if (c < 0x80)
            {
                buf[0] = (char)c;
                len = 1;
            }
            else if (c < 0x800)
            {
                buf[0] = (char)(0xC0 | (c >> 6));
                buf[1] = (char)(0x80 | (c & 0x3F));
                len = 2;
            }
            else
            {
                buf[0] = (char)(0xE0 | (c >> 12));
                buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (c & 0x3F));
                len = 3;
            }
            if (n + len + 1 > outSize)
                break;
            std::memcpy(out + n, buf, len);
            n += len;
        }
        out[n] = 0;
        return n;
    }
}
