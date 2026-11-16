#include "UI/PokeIcons.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "UI/GameAssets.hpp"

namespace OneScreen
{
namespace UI
{
    static u16 Get16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
    static u32 Get32(const u8 *p) { return (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24); }

    // --- Which icon ---

    // The lookup function's first words (`stmdb sp!, {r4-r8, lr}`,
    // `cmp r0, #0x328`, `cpy r4, r1`, `ldr r6, [sp, #0x18]`); its literal
    // pool follows at +0xCC (egg icon) and +0xD0 (table).
    static const u32 LookupWords[4] = { 0xE92D41F0, 0xE3500FCA, 0xE1A04001, 0xE59D6018 };
    static const u32 LookupEggAt = 0xCC, LookupTableAt = 0xD0;
    static const u32 MaxSpecies = 807;          // the table's last entry
    static const u32 EntryBytes = 16;

    bool FindPokeIconTable(const MemRead &read, PokeIconTable &out, std::string &error)
    {
        out = PokeIconTable();
        u32 words[4] = {};
        if (!read(PokeIconLookupFunc, words, sizeof(words)) || std::memcmp(words, LookupWords, sizeof(words)) != 0)
        {
            error = "icon lookup not found (other game version?)";
            return false;
        }
        u32 egg = 0, table = 0;
        if (!read(PokeIconLookupFunc + LookupEggAt, &egg, 4) || !read(PokeIconLookupFunc + LookupTableAt, &table, 4))
        {
            error = "can't read the icon lookup's literals";
            return false;
        }
        // Species 1 has one icon and no forms: {1, 1, 0, 0, 0}.
        u8 first[EntryBytes] = {};
        if (table == 0 || egg == 0 || egg > 0xFFFF || !read(table + EntryBytes, first, sizeof(first))
            || Get16(first) != 1 || Get16(first + 2) != 1 || Get16(first + 12) != 0)
        {
            error = "icon table doesn't look right";
            return false;
        }
        out.table = table;
        out.egg = egg;
        return true;
    }

    u32 PokeIconIndex(const MemRead &read, const PokeIconTable &table, u16 species, u8 form, u8 gender, bool egg)
    {
        if (!table.Valid())
            return PokeIconNone;
        if (egg || species == 808 || species == 809)
            return table.egg;
        if (species > MaxSpecies)
            species = 0;
        u8 e[EntryBytes];
        if (!read(table.table + species * EntryBytes, e, sizeof(e)))
            return PokeIconNone;
        const u32 forms = Get32(e + 4), count = Get16(e + 12);
        if (forms != 0 && form < count)
        {
            u8 v[2];
            if (!read(forms + form * 2u, v, 2))
                return PokeIconNone;
            return Get16(v);
        }
        return gender == 1 ? Get16(e + 2) : Get16(e);
    }

    // --- The pictures ---

    // Where the picture sits in the 64x32 texture (every icon of the game
    // fits in it; checked on all 1154).
    static const s32 PictureX = 12, PictureY = 1;

    bool DecodePokeIcon(const u8 *entry, size_t size, PokeIcon &out, std::string &error)
    {
        std::vector<u8> plain;
        const u8 *file = entry;
        size_t fileSize = size;
        if (size > 4 && entry[0] == 0x11)
        {
            if (!Lz11Decompress(entry, size, plain))
            {
                error = "icon: bad LZ11 data";
                return false;
            }
            file = plain.data();
            fileSize = plain.size();
        }
        Image image;
        if (!DecodeFlim(file, fileSize, image, error))
            return false;
        if (image.w < PictureX + PokeIconW || image.h < PictureY + PokeIconH)
        {
            error = "icon: texture too small";
            return false;
        }
        for (s32 y = 0; y < PokeIconH; y++)
            for (s32 x = 0; x < PokeIconW; x++)
            {
                const u8 *p = &image.rgba[((size_t)(y + PictureY) * image.w + x + PictureX) * 4];
                out.px[y * PokeIconW + x] = (u16)((p[0] >> 3) << 11 | (p[1] >> 3) << 6 | (p[2] >> 3) << 1
                                                  | (p[3] >= 128 ? 1 : 0));
            }
        return true;
    }

    void DrawPokeIcon(Canvas &canvas, const PokeIcon &icon, s32 x, s32 y, u8 alpha)
    {
        const ImageView view{ reinterpret_cast<const u8 *>(icon.px), PokeIconW, PokeIconH,
                              PokeIconW * 2, ImageFormat::RGB5A1 };
        canvas.DrawImage(view, Rect{ 0, 0, PokeIconW, PokeIconH }, Rect{ x, y, PokeIconW, PokeIconH },
                         ImageFilter::Nearest, Rgb(255, 255, 255, alpha));
    }

    // --- The archive ---

    bool PokeIconArchive::Open(const RomfsRead &read, std::string &error)
    {
        Close();
        uint64_t garc = 0, garcSize = 0;
        if (!RomfsFind(read, "a/0/6/2", garc, garcSize, error))
            return false;
        u8 head[0x1C];
        if (!read(garc, head, sizeof(head)) || std::memcmp(head, "CRAG", 4) != 0)
        {
            error = "a/0/6/2 is not a GARC";
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
        if (count == 0 || count > 4096 || btafSize < 12 || btafSize > 0x20000)
        {
            error = "GARC: odd entry table";
            return false;
        }
        // The entry table in one read (18 KB here), only while it is parsed.
        std::vector<u8> btaf;
        btaf.resize(btafSize);
        if (btaf.size() != btafSize || !read(btafAt, btaf.data(), btafSize))
        {
            error = "GARC: can't read the entry table";
            return false;
        }
        std::vector<u32> starts;
        std::vector<u16> sizes;
        starts.reserve(count);
        sizes.reserve(count);
        // Entries one by one (bit mask, then start, end, length per
        // variant); the first variant is the icon.
        size_t at = 12;
        for (u32 i = 0; i < count; i++)
        {
            if (at + 4 > btaf.size())
            {
                error = "GARC: entry table cut short";
                return false;
            }
            const u32 bits = Get32(&btaf[at]);
            u32 variants = 0;
            for (u32 b = bits; b != 0; b >>= 1)
                variants += b & 1;
            if (variants == 0 || at + 4 + 12 * variants > btaf.size())
            {
                error = "GARC: entry without data";
                return false;
            }
            const u32 start = Get32(&btaf[at + 4]), length = Get32(&btaf[at + 12]);
            if (length == 0 || length > 0xFFFF || (uint64_t)dataAt + start + length > garcSize)
            {
                error = "GARC: entry out of range";
                return false;
            }
            starts.push_back(start);
            sizes.push_back((u16)length);
            at += 4 + 12 * variants;
        }
        m_read = read;
        m_data = garc + dataAt;
        m_starts.swap(starts);
        m_sizes.swap(sizes);
        return true;
    }

    void PokeIconArchive::Close(void)
    {
        std::vector<u32>().swap(m_starts);
        std::vector<u16>().swap(m_sizes);
        m_read = nullptr;
        m_data = 0;
    }

    bool PokeIconArchive::Load(u32 index, PokeIcon &out, std::string &error) const
    {
        if (index >= m_starts.size())
        {
            error = "icon " + std::to_string(index) + " out of range";
            return false;
        }
        std::vector<u8> packed(m_sizes[index]);
        if (!m_read(m_data + m_starts[index], packed.data(), (u32)packed.size()))
        {
            error = "can't read icon " + std::to_string(index);
            return false;
        }
        if (!DecodePokeIcon(packed.data(), packed.size(), out, error))
            return false;
        out.index = index;
        return true;
    }

    // --- The cache ---

    const PokeIcon *PokeIconCache::Find(u32 index) const
    {
        if (index == PokeIconNone)
            return nullptr;
        for (const auto &icon : m_icons)
            if (icon->index == index)
                return icon.get();
        return nullptr;
    }

    bool PokeIconCache::Failed(u32 index) const
    {
        return std::find(m_failed.begin(), m_failed.end(), index) != m_failed.end();
    }

    u32 PokeIconCache::Next(const u32 *wanted, u32 count)
    {
        m_clock++;
        u32 next = PokeIconNone;
        for (u32 i = 0; i < count; i++)
        {
            const u32 index = wanted[i];
            if (index == PokeIconNone)
                continue;
            bool have = false;
            for (size_t k = 0; k < m_icons.size(); k++)
                if (m_icons[k]->index == index)
                {
                    m_used[k] = m_clock;
                    have = true;
                }
            if (!have && next == PokeIconNone && !Failed(index))
                next = index;
        }
        return next;
    }

    bool PokeIconCache::Store(const PokeIcon &icon)
    {
        if (m_icons.capacity() < m_capacity)
            m_icons.reserve(m_capacity);
        if (m_used.capacity() < m_capacity)
            m_used.reserve(m_capacity);
        size_t slot = m_icons.size();
        if (slot >= m_capacity)
        {
            // The least recently wanted one, if it isn't wanted now.
            slot = 0;
            for (size_t k = 1; k < m_icons.size(); k++)
                if (m_used[k] < m_used[slot])
                    slot = k;
            if (m_used[slot] == m_clock)
                return false;
        }
        else
        {
            PokeIcon *fresh = new (std::nothrow) PokeIcon();
            if (fresh == nullptr)
                return false;
            m_icons.emplace_back(fresh);
            m_used.push_back(0);
        }
        *m_icons[slot] = icon;
        m_used[slot] = m_clock;
        return true;
    }

    void PokeIconCache::Fail(u32 index)
    {
        if (Failed(index) || m_failed.size() >= 64)
            return;
        if (m_failed.capacity() == 0)
            m_failed.reserve(64);
        m_failed.push_back(index);
    }

    u32 PokeIconCache::Fill(const PokeIconArchive &archive, const u32 *wanted, u32 count, u32 budget,
                            std::string &error)
    {
        u32 tried = 0;
        PokeIcon icon;
        while (tried < budget)
        {
            const u32 index = Next(wanted, count);
            if (index == PokeIconNone)
                break;
            tried++;
            std::string why;
            if (!archive.Load(index, icon, why))
            {
                Fail(index);
                if (error.empty())
                    error = why;
            }
            else if (!Store(icon))
            {
                // No room: more icons wanted than fit, or no memory.
                Fail(index);
                if (error.empty())
                    error = "no room for icon " + std::to_string(index);
            }
        }
        return tried;
    }

    void PokeIconCache::Clear(void)
    {
        std::vector<std::unique_ptr<PokeIcon>>().swap(m_icons);
        std::vector<u32>().swap(m_used);
        std::vector<u32>().swap(m_failed);
        m_clock = 0;
    }
}
}
