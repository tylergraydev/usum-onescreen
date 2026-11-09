#include "Screens/BagList.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using UI::Rgb;

    // HID key bits (same values as CTRPF's Key::).
    static const u32 KeyA = 1u << 0, KeyUp = 1u << 6, KeyDown = 1u << 7;

    const BagTab &BagTabAt(int tab)
    {
        // The game's tab index (Bag param +0x54) runs left to right over the
        // tabs at the bottom of its screen. The icons are the ones the tab
        // animations Bag_BTN_LOW_001_icon_NN put on the field tab (docs/BAG.md).
        static const BagTab tabs[BagTabCount] =
        {
            { "rotom_powers", "Rotom Powers", "bag.pocket_rotom_powers", "Bag_Icon_06f" },
            { "medicine",     "Medicine",     "bag.pocket_medicine",     "Bag_Icon_01b" },
            { "items",        "Items",        "bag.pocket_items",        "Bag_Icon_01c" },
            { "tms",          "TMs",          "bag.pocket_tms",          "Bag_Icon_01d" },
            { "berries",      "Berries",      "bag.pocket_berries",      "Bag_Icon_01e" },
            { "key_items",    "Key Items",    "bag.pocket_key_items",    "Bag_Icon_01f" },
            { "free_space",   "Free Space",   "bag.pocket_free_space",   "Bag_Icon_01g" },
        };
        return tabs[tab < 0 || tab >= BagTabCount ? 0 : tab];
    }

    int BagTabFind(const std::string &name)
    {
        for (int i = 0; i < BagTabCount; i++)
            if (name == BagTabAt(i).name)
                return i;
        return -1;
    }

    const char *BagCommandLabel(u8 command)
    {
        switch (command)
        {
        case BagCommand::Use:       return "Use";
        case BagCommand::On:        return "On";
        case BagCommand::Off:       return "Off";
        case BagCommand::Give:      return "Give";
        case BagCommand::FreeSpace: return "Move to Free Space";
        case BagCommand::ReturnToPocket: return "Return to Pocket";
        case BagCommand::Cancel:    return "Cancel";
        case BagCommand::Yes:       return "Yes";
        case BagCommand::No:        return "No";
        }
        static char other[16];
        std::snprintf(other, sizeof(other), "Option 0x%02X", command);
        return other;
    }

    u32 BagGame::Signature(void) const
    {
        // FNV-1a over what the driver looks at.
        u32 h = 2166136261u;
        auto mix = [&h](u32 v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 16777619u; } };
        mix((u32)mode);
        mix((u32)tab);
        mix((u32)scroll);
        mix((u32)cursorRow);
        mix((u32)items.size());
        mix((u32)partyCursor);
        mix(item);
        for (u8 c : commands)
            mix(c);
        return h;
    }

    // --- Native list cursor ---

    int BagMoveCursor(int cursor, int delta, int count)
    {
        if (count <= 0)
            return 0;
        int c = (cursor + delta) % count;
        return c < 0 ? c + count : c;
    }

    int BagListTop(int top, int cursor, int count, int rows)
    {
        if (count <= rows)
            return 0;
        if (cursor < top)
            top = cursor;
        if (cursor >= top + rows)
            top = cursor - rows + 1;
        if (top > count - rows)
            top = count - rows;
        return top < 0 ? 0 : top;
    }

    // --- Actions ---

    // The game's item menu grows upwards from Cancel; Layouts.hpp has its
    // entries by position from the bottom.
    static const int MenuPositions = 5;

    struct BagActionTable
    {
        std::vector<std::string> names, summaries;
        std::vector<ActionDef> defs;
        int tab, row, up, down, menu, party, yes, no, a, b, back;
    };

    static BagActionTable MakeBagActions(void)
    {
        BagActionTable t;
        t.names.reserve(64);
        t.summaries.reserve(64);
        t.defs.reserve(64);
        const ActionCond bag = InScene(SceneBag);
        auto add = [&](const std::string &name, std::vector<ActionStep> steps) {
            t.names.push_back(name);
            t.summaries.push_back(steps[0].Describe());
            t.defs.push_back(ActionDef{ nullptr, nullptr, bag, steps });
            return (int)t.defs.size() - 1;
        };
        // Button names must outlive the definitions too.
        static std::vector<std::string> buttons;
        buttons.reserve(32);
        auto button = [](const std::string &name) { buttons.push_back(name); return buttons.back().c_str(); };

        t.tab = (int)t.defs.size();
        for (int i = 0; i < BagTabCount; i++)
            add(std::string("bag-tab-") + BagTabAt(i).name, { StepTap(BagTabAt(i).button) });
        t.row = (int)t.defs.size();
        for (int r = 1; r <= 7; r++)
            add("bag-row" + std::to_string(r), { StepTap(button("bag.item_row" + std::to_string(r))) });
        t.up = add("bag-up", { StepPress(KeyUp) });
        t.down = add("bag-down", { StepPress(KeyDown) });
        t.menu = (int)t.defs.size();
        for (int p = 1; p <= MenuPositions; p++)
            add("bag-menu-" + std::to_string(p), { StepTap(button("bag_item_menu.from_bottom" + std::to_string(p))) });
        t.party = (int)t.defs.size();
        for (int s = 1; s <= 6; s++)
            add("bag-party" + std::to_string(s), { StepTap(button("bag_party.slot" + std::to_string(s))) });
        t.yes = add("bag-yes", { StepTap("bag_yes_no.yes") });
        t.no = add("bag-no", { StepTap("bag_yes_no.no") });
        t.a = add("bag-a", { StepPress(KeyA) });
        t.b = add("bag-b", { StepPress(ActionKeyB) });
        // Back returns to the X menu (or wherever the Bag was opened from).
        t.back = add("bag-back", { StepTap("bag.back"), StepWaitFor(NotInScene(SceneBag), 180) });

        for (size_t i = 0; i < t.defs.size(); i++)
        {
            t.defs[i].name = t.names[i].c_str();
            t.defs[i].summary = t.summaries[i].c_str();
        }
        return t;
    }

    const ActionDef *BagAction(const BagCmd &cmd)
    {
        static const BagActionTable t = MakeBagActions();
        const int a = cmd.arg;
        switch (cmd.step)
        {
        case BagStep::TapTab:    return a >= 0 && a < BagTabCount ? &t.defs[t.tab + a] : nullptr;
        case BagStep::TapRow:    return a >= 1 && a <= 7 ? &t.defs[t.row + a - 1] : nullptr;
        case BagStep::PressUp:   return &t.defs[t.up];
        case BagStep::PressDown: return &t.defs[t.down];
        case BagStep::TapMenu:   return a >= 1 && a <= MenuPositions ? &t.defs[t.menu + a - 1] : nullptr;
        case BagStep::TapParty:  return a >= 0 && a < 6 ? &t.defs[t.party + a] : nullptr;
        case BagStep::TapYes:    return &t.defs[t.yes];
        case BagStep::TapNo:     return &t.defs[t.no];
        case BagStep::PressA:    return &t.defs[t.a];
        case BagStep::PressB:    return &t.defs[t.b];
        case BagStep::TapBack:   return &t.defs[t.back];
        case BagStep::None:      break;
        }
        return nullptr;
    }

    bool BagEntryEnabled(const std::vector<u8> &commands, int entry)
    {
        if (entry < 0 || entry >= (int)commands.size())
            return false;
        // Yes / No have their own buttons.
        if (commands.size() == 2 && commands[0] == BagCommand::Yes)
            return true;
        const ActionDef *def = BagAction(BagCmd{ BagStep::TapMenu, (int)commands.size() - entry });
        return def != nullptr && CheckAction(*def).empty();
    }

    // --- Items with a follow-up ---

    BagItemKind BagItemKindOf(u16 item)
    {
        switch (item)
        {
        case 38: case 39: case 51: case 53:     return BagItemKind::PpOne;      // Ether, Max Ether, PP Up, PP Max
        case 40: case 41:                       return BagItemKind::PpAll;      // Elixir, Max Elixir
        case 76: case 77: case 79:              return BagItemKind::Repel;      // Super, Max, Repel
        case 50:                                return BagItemKind::RareCandy;
        case 80: case 81: case 82: case 83: case 84: case 85: case 107: case 108: case 109: case 849:
            return BagItemKind::Stone;          // Sun ... Leaf, Shiny, Dusk, Dawn, Ice Stone
        }
        if ((item >= 328 && item <= 419) || (item >= 618 && item <= 620) || (item >= 690 && item <= 694))
            return BagItemKind::Tm;             // TM01-92, TM93-95, TM96-100
        return BagItemKind::Other;
    }

    const char *BagItemKindName(BagItemKind kind)
    {
        static const char *const names[] = { "other", "pp-one", "pp-all", "repel", "stone", "rare-candy", "tm" };
        return names[(int)kind];
    }

    const char *BagPickQuestion(u16 item)
    {
        return BagItemKindOf(item) == BagItemKind::Tm ? "Which Pokémon do you want to teach it to?"
                                                      : "Which Pokémon do you want to use this item on?";
    }

    BagFollow BagFollowGame(BagMode mode, u32 busyFrames, bool usingItem, bool aside)
    {
        if (aside)
            // Back once the game waits in its list again (or asks for a
            // Pokémon, as after a Potion).
            return mode == BagMode::List || mode == BagMode::Pick ? BagFollow::Native : BagFollow::Aside;
        return usingItem && mode == BagMode::Busy && busyFrames >= BagAsideFrames ? BagFollow::Aside : BagFollow::Native;
    }

    std::string BagResultText(const BagResult &r)
    {
        const std::string &item = r.item;
        switch (r.command)
        {
        case BagCommand::FreeSpace:      return item + " moved to the Free Space";
        case BagCommand::ReturnToPocket: return item + " returned to its pocket";
        case BagCommand::On:             return item + " turned on";
        case BagCommand::Off:            return item + " turned off";
        case BagCommand::Give:
            if (!r.picked)
                return "";
            if (r.holdsItem)
                return r.mon + " now holds " + item + (r.heldBefore.empty() ? "" : " (" + r.heldBefore + " back in the Bag)");
            return r.mon + " still holds " + (r.heldAfter.empty() ? "nothing" : r.heldAfter);
        case BagCommand::Use:
            break;
        default:
            return "";
        }
        const bool used = r.countAfter < r.countBefore;
        if (!r.picked)
            return used ? "Used " + item : item + " wasn't used";
        if (r.speciesAfter != r.speciesBefore)
            return r.mon + " evolved" + (r.speciesName.empty() ? "" : " into " + r.speciesName);
        std::string text;
        auto add = [&text](const std::string &part) { text += (text.empty() ? "" : ", ") + part; };
        if (r.levelAfter > r.levelBefore)
            add(r.mon + " grew to Lv. " + std::to_string(r.levelAfter));
        for (const std::string &move : r.learned)
            add(r.mon + " learned " + move);
        if (r.hpAfter != r.hpBefore)
            add(r.mon + ": HP " + std::to_string(r.hpBefore) + " -> " + std::to_string(r.hpAfter) + "/" + std::to_string(r.maxHp));
        if (r.ppAfter > r.ppBefore)
            add(r.mon + "'s PP restored");
        if (!text.empty())
            return text;
        if (r.kind == BagItemKind::Tm)
            return r.mon + " did not learn the move";
        if (!used)
            return item + " had no effect on " + r.mon;
        return "Used " + item + " on " + r.mon;
    }

    // --- Pocket icons and descriptions ---

    const UI::Image *BagAssets::Pocket(int tab, bool selected) const
    {
        if (tab < 0 || tab >= BagTabCount)
            return nullptr;
        const UI::Image &image = pockets[tab][selected ? 0 : 1];
        return image.Empty() ? nullptr : &image;
    }

    int BagAssets::PocketCount(void) const
    {
        int n = 0;
        for (const auto &pair : pockets)
            for (const UI::Image &image : pair)
                n += image.Empty() ? 0 : 1;
        return n;
    }

    // Message file (docs/RE2-text.md): header u16 sections, u16 lines, u32
    // size, u32 key, u32 section offset; the section: u32 size, then per line
    // {u32 offset from the section, u16 length in chars, u16 extra}.
    bool ReadMessageLine(const RomfsRead &read, uint64_t fileAt, u32 fileSize, u32 line, std::string &out,
                         std::string &error)
    {
        out.clear();
        u8 head[0x10], entry[8];
        if (fileSize < sizeof(head) || !read(fileAt, head, sizeof(head)))
        {
            error = "can't read the message file";
            return false;
        }
        const u32 count = head[2] | (head[3] << 8);
        const u32 section = head[0x0C] | (head[0x0D] << 8) | (head[0x0E] << 16) | ((u32)head[0x0F] << 24);
        if (line >= count || section + 4 + (uint64_t)(line + 1) * 8 > fileSize
            || !read(fileAt + section + 4 + line * 8, entry, sizeof(entry)))
        {
            error = "no line " + std::to_string(line);
            return false;
        }
        const u32 at = section + (entry[0] | (entry[1] << 8) | (entry[2] << 16) | ((u32)entry[3] << 24));
        u32 length = entry[4] | (entry[5] << 8);
        if (length > MsgLineMax)
            length = MsgLineMax;
        u8 bytes[MsgLineMax * 2];
        if (at + (uint64_t)length * 2 > fileSize || (length != 0 && !read(fileAt + at, bytes, length * 2)))
        {
            error = "line " + std::to_string(line) + " out of range";
            return false;
        }
        u16 encrypted[MsgLineMax], plain[MsgLineMax];
        for (u32 i = 0; i < length; i++)
            encrypted[i] = (u16)(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
        DecryptMsgLine(encrypted, length, line, plain, MsgLineMax);
        char utf8[MsgLineMax * 3 + 1];
        Utf16ToUtf8(plain, MsgLineMax, utf8, sizeof(utf8));
        out = utf8;
        return true;
    }

    std::string BagReadDescription(const RomfsRead &read, const BagAssets &assets, u16 item)
    {
        std::string line, error, text;
        if (assets.descriptionsSize == 0
            || !ReadMessageLine(read, assets.descriptionsAt, assets.descriptionsSize, item, line, error))
            return "";
        // Line breaks (and runs of spaces) become one space.
        bool space = false;
        for (char c : line)
        {
            if (c == '\n' || c == '\r' || c == ' ')
            {
                space = true;
                continue;
            }
            if (space && !text.empty())
                text += ' ';
            space = false;
            text += c;
        }
        return text;
    }

    // LZ11 decoding as a stream: compressed bytes come from `read` in 4 KB
    // chunks, output goes through a ring the size of the largest copy
    // distance (4096) to `put(offset, byte)`.
    class Lz11Stream
    {
    public:
        // The buffers are on the heap: this runs in a Task with a small stack.
        Lz11Stream(const RomfsRead &read, uint64_t at, u32 size)
            : m_read(read), m_at(at), m_size(size), m_chunk(Chunk), m_ring(Ring) {}

        template <class Put>
        bool Run(Put put, std::string &error)
        {
            u8 head[4];
            if (!Next(head[0]) || head[0] != 0x11 || !Next(head[1]) || !Next(head[2]) || !Next(head[3]))
            {
                error = "not LZ11 data";
                return false;
            }
            u32 outSize = head[1] | (head[2] << 8) | (head[3] << 16);
            if (outSize == 0)
            {
                u8 b[4];
                for (u8 &v : b)
                    if (!Next(v))
                    {
                        error = "LZ11 data cut short";
                        return false;
                    }
                outSize = b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24);
            }
            if (outSize > (16u << 20))
            {
                error = "LZ11 output too big";
                return false;
            }
            u32 o = 0;
            auto emit = [&](u8 v) { m_ring[o & (Ring - 1)] = v; put(o, v); o++; };
            bool cut = false;
            while (o < outSize && !cut)
            {
                u8 flags;
                if (!Next(flags))
                    break;
                for (u32 bit = 0; bit < 8 && o < outSize; bit++)
                {
                    u8 b0, b1, b2, b3;
                    if (!(flags & (0x80 >> bit)))
                    {
                        if (!Next(b0))
                        {
                            cut = true;
                            break;
                        }
                        emit(b0);
                        continue;
                    }
                    if (!Next(b0) || !Next(b1))
                    {
                        cut = true;
                        break;
                    }
                    u32 length, disp;
                    const u32 kind = b0 >> 4;
                    if (kind == 0)
                    {
                        if (!Next(b2))
                        {
                            cut = true;
                            break;
                        }
                        length = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11;
                        disp = (((b1 & 0xF) << 8) | b2) + 1;
                    }
                    else if (kind == 1)
                    {
                        if (!Next(b2) || !Next(b3))
                        {
                            cut = true;
                            break;
                        }
                        length = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111;
                        disp = (((b2 & 0xF) << 8) | b3) + 1;
                    }
                    else
                    {
                        length = kind + 1;
                        disp = (((b0 & 0xF) << 8) | b1) + 1;
                    }
                    if (disp > o)
                    {
                        error = "bad LZ11 data";
                        return false;
                    }
                    for (u32 i = 0; i < length && o < outSize; i++)
                        emit(m_ring[(o - disp) & (Ring - 1)]);
                }
            }
            if (o < outSize)
            {
                error = m_failed ? "can't read the entry" : "LZ11 data cut short";
                return false;
            }
            return true;
        }

    private:
        static const u32 Ring = 4096, Chunk = 4096;

        bool Next(u8 &v)
        {
            if (m_pos == m_have)
            {
                const u32 left = m_size - m_done;
                const u32 n = left < Chunk ? left : Chunk;
                if (n == 0 || !m_read(m_at + m_done, m_chunk.data(), n))
                {
                    m_failed = n != 0;
                    return false;
                }
                m_done += n;
                m_have = n;
                m_pos = 0;
            }
            v = m_chunk[m_pos++];
            return true;
        }

        const RomfsRead &m_read;
        uint64_t m_at;
        u32 m_size, m_done = 0, m_have = 0, m_pos = 0;
        bool m_failed = false;
        std::vector<u8> m_chunk, m_ring;
    };

    static u32 Le16(const std::vector<u8> &b, size_t at) { return b[at] | (b[at + 1] << 8); }
    static u32 Le32(const std::vector<u8> &b, size_t at) { return Le16(b, at) | (Le16(b, at + 2) << 16); }

    bool Lz11ArchiveFiles(const RomfsRead &read, uint64_t at, u32 size, const std::vector<std::string> &names,
                          std::vector<std::vector<u8>> &files, std::string &error)
    {
        files.assign(names.size(), std::vector<u8>());
        // The header is kept until the SARC's file data starts (its node
        // table and names are all before that); then the wanted files'
        // ranges are known, sorted by start.
        static const u32 MaxHeader = 128 * 1024;
        std::vector<u8> header;
        long sarc = -1;
        u32 dataAt = 0;
        bool keeping = true, bad = false;
        struct Range { u32 start, end; size_t file; };
        std::vector<Range> ranges;
        size_t next = 0;

        auto parse = [&](void) {
            const size_t s = (size_t)sarc;
            if (header.size() < s + 0x20 || Le16(header, s + 4) != 0x14 || std::memcmp(&header[s + 0x14], "SFAT", 4) != 0)
                return false;
            const u32 nodes = Le16(header, s + 0x14 + 6);
            const size_t sfnt = s + 0x14 + 0x0C + (size_t)nodes * 16;
            if (sfnt + 8 > header.size() || std::memcmp(&header[sfnt], "SFNT", 4) != 0)
                return false;
            for (u32 n = 0; n < nodes; n++)
            {
                const size_t node = s + 0x14 + 0x0C + n * 16;
                const u32 attr = Le32(header, node + 4);
                if ((attr >> 24) == 0)
                    continue;
                const size_t nameAt = sfnt + 8 + (size_t)(attr & 0xFFFF) * 4;
                for (size_t f = 0; f < names.size(); f++)
                    if (nameAt + names[f].size() + 1 <= header.size()
                        && std::memcmp(&header[nameAt], names[f].c_str(), names[f].size() + 1) == 0)
                    {
                        const u32 start = Le32(header, node + 8), end = Le32(header, node + 12);
                        if (end < start || end - start > (1u << 20))
                            return false;
                        ranges.push_back(Range{ (u32)(s + dataAt + start), (u32)(s + dataAt + end), f });
                    }
            }
            std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start < b.start; });
            for (size_t i = 1; i < ranges.size(); i++)
                if (ranges[i].start < ranges[i - 1].end)
                    return false;
            for (const Range &r : ranges)
                files[r.file].reserve(r.end - r.start);
            return true;
        };

        auto put = [&](u32 o, u8 v) {
            if (keeping)
            {
                header.push_back(v);
                const size_t n = header.size();
                if (sarc < 0 && n >= 4 && (n - 4) % 4 == 0 && std::memcmp(&header[n - 4], "SARC", 4) == 0)
                    sarc = (long)(n - 4);
                if (sarc >= 0 && n == (size_t)sarc + 0x10)
                    dataAt = Le32(header, (size_t)sarc + 0x0C);
                if (sarc >= 0 && dataAt != 0 && n == (size_t)sarc + dataAt)
                {
                    keeping = false;
                    bad = !parse();
                    header = std::vector<u8>();
                }
                else if (n >= MaxHeader)
                {
                    keeping = false;
                    bad = true;
                    header = std::vector<u8>();
                }
            }
            while (next < ranges.size() && o >= ranges[next].end)
                next++;
            if (next < ranges.size() && o >= ranges[next].start)
                files[ranges[next].file].push_back(v);
        };

        Lz11Stream stream(read, at, size);
        if (!stream.Run(put, error))
            return false;
        if (bad || keeping)
        {
            error = "not a layout archive";
            return false;
        }
        return true;
    }

    void LoadBagAssets(const RomfsRead &read, u32 lang, BagAssets &out, std::vector<std::string> &log)
    {
        std::string error;
        // Pocket icons, out of the Bag layout archive.
        {
            std::vector<std::string> names;
            for (int tab = 0; tab < BagTabCount; tab++)
                for (int state = 0; state < 2; state++)
                    names.push_back(std::string("timg/") + BagTabAt(tab).icon + (state ? "2" : "") + ".bflim");
            uint64_t garc = 0, garcSize = 0, offset = 0;
            u32 size = 0;
            std::vector<std::vector<u8>> files;
            const bool found = RomfsFind(read, BagLayoutGarc, garc, garcSize, error)
                               && GarcEntry(read, garc, 0, offset, size, error)
                               && Lz11ArchiveFiles(read, offset, size, names, files, error);
            std::string missing;
            for (size_t i = 0; found && i < names.size(); i++)
            {
                std::string why;
                if (files[i].empty()
                    || !UI::DecodeFlim(files[i].data(), files[i].size(), out.pockets[i / 2][i % 2], why))
                    missing += (missing.empty() ? "" : ", ") + names[i] + (why.empty() ? "" : " (" + why + ")");
            }
            if (!found)
                log.push_back("bag assets: pocket icons not loaded (" + error + ")");
            else
                log.push_back("bag assets: pocket icons " + std::to_string(out.PocketCount()) + " of "
                              + std::to_string(BagTabCount * 2) + " from " + BagLayoutGarc
                              + (missing.empty() ? "" : ", missing " + missing));
        }
        // Descriptions.
        if (lang <= 9)
        {
            error.clear();
            const std::string garc = "a/0/3/" + std::to_string(lang);
            uint64_t garcAt = 0, garcSize = 0, at = 0;
            u32 size = 0;
            u8 head[4] = {};
            if (RomfsFind(read, garc, garcAt, garcSize, error) && GarcEntry(read, garcAt, BagDescriptionFile, at, size, error)
                && size >= 0x10 && read(at, head, sizeof(head)))
            {
                out.descriptionsAt = at;
                out.descriptionsSize = size;
                log.push_back("bag assets: item descriptions in " + garc + " file " + std::to_string(BagDescriptionFile)
                              + " (" + std::to_string(head[2] | (head[3] << 8)) + " lines, read when shown)");
            }
            else
                log.push_back("bag assets: descriptions not loaded (" + (error.empty() ? "file too short" : error) + ")");
        }
        else
            log.push_back("bag assets: language byte " + std::to_string(lang) + " not 0-9, no descriptions");
    }

    // --- Driver ---

    const char *BagGoalName(BagGoal goal)
    {
        static const char *const names[] = { "none", "tab", "open", "choose", "pick", "answer", "advance", "cancel", "close" };
        return names[(int)goal];
    }

    void BagDriver::Begin(BagGoal goal)
    {
        m_goal = goal;
        m_frames = 0;
        m_stable = 0;
        m_signature = 0;
        m_sent = false;
        m_commands = 0;
        m_startSignature = 0;
        m_failure.clear();
    }

    void BagDriver::Tab(int tab)          { Begin(BagGoal::Tab); m_arg = tab; }
    void BagDriver::Open(int index, u16 id) { Begin(BagGoal::Open); m_arg = index; m_id = id; }
    void BagDriver::Choose(int entry)     { Begin(BagGoal::Choose); m_arg = entry; }
    void BagDriver::Pick(int slot)        { Begin(BagGoal::Pick); m_arg = slot; }
    void BagDriver::Answer(bool yes)      { Begin(BagGoal::Answer); m_arg = yes ? 1 : 0; }
    void BagDriver::Advance(void)         { Begin(BagGoal::Advance); }
    void BagDriver::Cancel(void)          { Begin(BagGoal::Cancel); }
    void BagDriver::Close(void)           { Begin(BagGoal::Close); }

    void BagDriver::Done(std::vector<std::string> &log)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "bag: %s done in %lu frames, %lu commands", BagGoalName(m_goal),
                      (unsigned long)m_frames, (unsigned long)m_commands);
        log.push_back(line);
        m_goal = BagGoal::None;
        m_finished++;
    }

    void BagDriver::Fail(const std::string &why, std::vector<std::string> &log)
    {
        m_failure = std::string(BagGoalName(m_goal)) + ": " + why;
        log.push_back("bag: " + m_failure + " FAILED");
        m_goal = BagGoal::None;
        m_finished++;
    }

    void BagDriver::Refused(const std::string &why, std::vector<std::string> &log)
    {
        if (m_goal != BagGoal::None)
            Fail("action refused: " + why, log);
    }

    BagCmd BagDriver::Send(BagStep step, int arg)
    {
        m_commands++;
        m_stable = 0;
        m_sent = true;
        BagCmd cmd;
        cmd.step = step;
        cmd.arg = arg;
        return cmd;
    }

    BagCmd BagDriver::Update(const BagGame &g, bool actionRunning, std::vector<std::string> &log)
    {
        if (m_goal == BagGoal::None)
            return BagCmd();
        // While an action runs (ours or not) the screen is changing anyway.
        if (actionRunning)
        {
            m_stable = 0;
            return BagCmd();
        }
        m_sent = false;
        m_frames++;
        const u32 sig = g.Signature();
        if (sig != m_signature)
        {
            m_signature = sig;
            m_stable = 0;
        }
        else
            m_stable++;

        if (m_goal == BagGoal::Close && !g.Valid())
        {
            Done(log);
            return BagCmd();
        }
        // A Use can hand over to another of the game's screens (a TM's
        // move-forget screen, an evolution, a Key Item's own screen): after
        // a command of ours, that is where the goal ended.
        if (!g.Valid() && m_commands > 0 && (m_goal == BagGoal::Choose || m_goal == BagGoal::Pick
                                             || m_goal == BagGoal::Answer || m_goal == BagGoal::Advance))
        {
            log.push_back(std::string("bag: ") + BagGoalName(m_goal) + ": the game left the Bag screen");
            Done(log);
            return BagCmd();
        }
        if (!g.Valid())
        {
            Fail("the Bag is gone", log);
            return BagCmd();
        }
        if (m_frames > Timeout)
        {
            Fail("timed out", log);
            return BagCmd();
        }
        if (m_commands >= MaxCommands)
        {
            Fail("too many taps and presses", log);
            return BagCmd();
        }
        const u32 settle = (g.mode == BagMode::Message || m_goal == BagGoal::Advance) ? MessageSettleFrames : SettleFrames;
        if (m_stable < settle || g.mode == BagMode::Busy)
            return BagCmd();

        switch (m_goal)
        {
        case BagGoal::Tab:
            if (g.mode != BagMode::List)
                break;
            if (g.tab == m_arg)
            {
                if (g.scroll >= 0)
                    Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("the game stays on another pocket", log);
                return BagCmd();
            }
            return Send(BagStep::TapTab, m_arg);

        case BagGoal::Open:
            if (g.mode == BagMode::Menu && !g.YesNo() && g.item == m_id)
            {
                Done(log);
                return BagCmd();
            }
            if (g.mode != BagMode::List)
                break;
            if (m_arg < 0 || m_arg >= (int)g.items.size() || g.items[m_arg].id != m_id)
            {
                Fail("the item isn't where it was", log);
                return BagCmd();
            }
            if (g.scroll < 0)
                return BagCmd();
            if (m_arg >= g.scroll && m_arg < g.scroll + g.visible && m_arg - g.scroll < 7)
                return Send(BagStep::TapRow, m_arg - g.scroll + 1);
            return Send(m_arg > g.Index() ? BagStep::PressDown : BagStep::PressUp);

        case BagGoal::Choose:
            if (m_commands == 0)
            {
                if (g.mode != BagMode::Menu || m_arg < 0 || m_arg >= (int)g.commands.size())
                    break;
                m_startSignature = sig;
                return Send(BagStep::TapMenu, (int)g.commands.size() - m_arg);
            }
            if (g.mode != BagMode::Menu || sig != m_startSignature)
            {
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("the menu doesn't take the tap", log);
                return BagCmd();
            }
            return Send(BagStep::TapMenu, (int)g.commands.size() - m_arg);

        case BagGoal::Pick:
            if (g.mode != BagMode::Pick)
            {
                if (m_commands == 0)
                    break;
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 4)
            {
                Fail("the party slot doesn't take the tap", log);
                return BagCmd();
            }
            // The first tap on a slot moves the cursor there, a tap on the
            // slot under the cursor chooses it.
            return Send(BagStep::TapParty, m_arg);

        case BagGoal::Answer:
            if (!g.YesNo())
            {
                if (m_commands == 0)
                    break;
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("Yes / No doesn't take the tap", log);
                return BagCmd();
            }
            return Send(m_arg ? BagStep::TapYes : BagStep::TapNo);

        case BagGoal::Advance:
            if (g.mode == BagMode::Message)
                return Send(BagStep::PressA);
            Done(log);
            return BagCmd();

        case BagGoal::Cancel:
            if (g.mode == BagMode::List)
            {
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 6)
            {
                Fail("the game doesn't go back to the list", log);
                return BagCmd();
            }
            if (g.mode == BagMode::Message)
                return Send(BagStep::PressA);
            if (g.mode == BagMode::Menu && !g.YesNo() && !g.commands.empty() && g.commands.back() == BagCommand::Cancel)
                return Send(BagStep::TapMenu, 1);
            return Send(BagStep::PressB);

        case BagGoal::Close:
            if (g.mode != BagMode::List)
                break;
            if (m_commands >= 2)
            {
                Fail("the Bag doesn't close", log);
                return BagCmd();
            }
            return Send(BagStep::TapBack);

        case BagGoal::None:
            break;
        }

        static const char *const modes[] = { "none", "list", "menu", "pick", "message", "busy" };
        Fail(std::string("unexpected screen: ") + modes[(int)g.mode], log);
        return BagCmd();
    }

    std::string BagDriver::Status(void) const
    {
        if (m_goal == BagGoal::None)
            return m_failure.empty() ? "idle" : "idle, last failed: " + m_failure;
        char line[96];
        std::snprintf(line, sizeof(line), "%s (%lu frames, %lu commands)", BagGoalName(m_goal),
                      (unsigned long)m_frames, (unsigned long)m_commands);
        return line;
    }

    // --- Drawing ---

    // The game's Bag look (docs/VISUAL-STYLE.md): an orange background, a
    // dark window with the item's description under a green pocket tab, the
    // party on green plates, the items on the theme's green list rows with
    // the red cursor, the item menu as option pills, and the pocket icons
    // along the bottom. Shapes and text come from UI::Theme, which uses the
    // game's font and icons once they are loaded.
    namespace T = UI::Theme;
    using UI::s32;

    static constexpr UI::Color BgTop      = Rgb(0xFF, 0xD8, 0x4A);   // the Bag's orange (#FAB813), lighter at the top
    static constexpr UI::Color BgBottom   = Rgb(0xFA, 0xB8, 0x13);
    static constexpr UI::Color TabFill    = Rgb(0xC6, 0xF8, 0x8D);   // the pocket name's tab
    static constexpr UI::Color TabLine    = Rgb(0x14, 0x6E, 0x1C);
    static constexpr UI::Color PlateTop   = Rgb(0x8C, 0xE0, 0x5A);   // party plates, green as on the bottom screen
    static constexpr UI::Color PlateBottom = Rgb(0x2E, 0x9E, 0x3A);
    static constexpr UI::Color PlateLine  = Rgb(0x14, 0x5A, 0x1C);
    static constexpr UI::Color PlateText  = Rgb(0xFF, 0xFF, 0xFF);
    static constexpr UI::Color DimText    = Rgb(0x6A, 0x6A, 0x6A);
    static constexpr UI::Color NewBadge   = Rgb(0xEE, 0x22, 0x11);
    static constexpr UI::Color HeldDot    = Rgb(0xFF, 0x9E, 0x37);
    static constexpr UI::Color IconSlotOn = Rgb(0xFF, 0xFF, 0xFF, 0xE0);
    static constexpr UI::Color Shade      = Rgb(0x00, 0x00, 0x00, 0x30);
    static constexpr UI::Color ArrowColor = Rgb(0x8A, 0x4A, 0x00);

    // Geometry (top screen, 400x240).
    static const int Margin = 6;
    static const UI::Rect Window = { Margin, 14, 400 - 2 * Margin, 58 };
    static const UI::Rect PocketTab = { 140, 2, 120, 20 };
    static const int MidY = 76;
    static const int PartyX = Margin, PartyW = 144, PlatePitch = 23, PlateH = 21;
    static const int ListX = 156, ListW = 232;
    static const int MenuW = 164, MenuX = 400 - Margin - MenuW, MenuBottom = MidY + BagListRows * T::RowPitch;
    static const int FootY = 218;
    static const int IconSlot = 24, IconsX = 200;

    static std::string Count(const BagItem &item)
    {
        // "× 9" as the game; the system font has no "×".
        return (T::UsingGameFont() ? "\xC3\x97 " : "x") + std::to_string(item.count);
    }

    static std::string ItemLabel(const BagView &view, int i)
    {
        return i < (int)view.names.size() && !view.names[i].empty() ? view.names[i] : "#" + std::to_string(view.items[i].id);
    }

    // Splits text into lines that fit `width` in the given size (at word
    // boundaries; a word longer than a line is cut by DrawText).
    static std::vector<std::string> Wrap(const std::string &text, s32 width, T::TextSize size, int maxLines)
    {
        std::vector<std::string> lines;
        std::string line, word;
        size_t i = 0;
        while (i <= text.size() && (int)lines.size() < maxLines)
        {
            if (i == text.size() || text[i] == ' ')
            {
                const std::string tryLine = line.empty() ? word : line + " " + word;
                if (!line.empty() && T::MeasureText(tryLine, size) > width)
                {
                    lines.push_back(line);
                    line = word;
                }
                else
                    line = tryLine;
                word.clear();
            }
            else
                word += text[i];
            i++;
        }
        if (!line.empty() && (int)lines.size() < maxLines)
            lines.push_back(line);
        return lines;
    }

    static void DrawBackground(UI::Canvas &canvas)
    {
        for (int y = 0; y < BagArea.h; y++)
        {
            const int t = y * 255 / (BagArea.h - 1);
            const UI::Color c = Rgb((u8)(BgTop.r + (BgBottom.r - BgTop.r) * t / 255),
                                    (u8)(BgTop.g + (BgBottom.g - BgTop.g) * t / 255),
                                    (u8)(BgTop.b + (BgBottom.b - BgTop.b) * t / 255));
            canvas.HLine(BagArea.x, BagArea.y + y, BagArea.w, c);
        }
    }

    // The window at the top: a heading line (the item, the question, or
    // the last result) and the item's description under it.
    static void DrawInfo(UI::Canvas &canvas, const BagView &view)
    {
        T::DrawWindow(canvas, Window);
        const UI::Rect inner = UI::Inset(Window, T::WindowPad, 4, T::WindowPad, 4);
        T::TextStyle head;
        head.color = T::WindowText;
        T::TextStyle body = head;
        body.size = T::TextSize::Small;
        const bool haveItem = view.cursor >= 0 && view.cursor < (int)view.items.size();

        std::string heading, detail = view.description;
        bool showCount = false;
        if (!view.status.empty())
        {
            heading = T::FormatPanelHint(view.status);
            head.color = view.busy ? T::WindowText : T::AccentText;
        }
        else if (view.mode == BagMode::Menu && !view.menuTitle.empty())
            heading = view.menuTitle;
        else if (view.mode == BagMode::Pick && !view.pickQuestion.empty())
            heading = view.pickQuestion;
        else if (haveItem)
        {
            heading = ItemLabel(view, view.cursor);
            showCount = true;
        }
        else
        {
            heading = view.tab >= 0 ? std::string("Nothing in ") + BagTabAt(view.tab).label : "";
            detail.clear();
        }
        if (detail.empty() && haveItem && !showCount)
            detail = ItemLabel(view, view.cursor);

        const s32 lineH = T::LineHeight(T::TextSize::Body), smallH = T::LineHeight(T::TextSize::Small);
        s32 right = inner.Right();
        if (showCount)
        {
            T::TextStyle count = head;
            count.align = UI::Align::Right;
            const std::string text = Count(view.items[view.cursor]);
            T::DrawText(canvas, text, UI::Rect{ inner.x, inner.y, inner.w, lineH }, count);
            right -= T::MeasureText(text, T::TextSize::Body) + 12;
        }
        T::DrawText(canvas, heading, UI::Rect{ inner.x, inner.y, right - inner.x, lineH }, head);
        const std::vector<std::string> lines = Wrap(detail, inner.w, T::TextSize::Small, 2);
        for (size_t i = 0; i < lines.size(); i++)
            T::DrawText(canvas, lines[i], UI::Rect{ inner.x, inner.y + lineH + (s32)i * smallH, inner.w, smallH }, body);

        // The pocket's name on its tab over the window.
        canvas.FillRoundRect(PocketTab, 8, TabLine);
        canvas.FillRoundRect(UI::Inset(PocketTab, 1), 7, TabFill);
        T::TextStyle tab;
        tab.size = T::TextSize::Small;
        tab.color = TabLine;
        tab.align = UI::Align::Center;
        const int shown = view.wantTab >= 0 ? view.wantTab : view.tab;
        if (shown >= 0)
            T::DrawText(canvas, BagTabAt(shown).label, UI::Inset(PocketTab, 4, 1, 4, 1), tab);
    }

    static void DrawList(UI::Canvas &canvas, const BagView &view)
    {
        const bool listing = view.mode == BagMode::List && (view.wantTab < 0 || view.wantTab == view.tab);
        for (int row = 0; row < BagListRows; row++)
        {
            const int i = view.top + row;
            if (i >= (int)view.items.size())
                break;
            const UI::Rect r = { ListX, MidY + row * T::RowPitch, ListW, T::RowHeight };
            T::DrawRowBackground(canvas, r, T::RowKind::List);
            T::TextStyle name;
            name.color = listing || i == view.cursor ? T::ListText : DimText;
            T::TextStyle count = name;
            count.align = UI::Align::Right;
            const UI::Rect text = UI::Inset(r, T::RowTextInset, 2, 12, 4);
            s32 right = text.Right();
            if (view.items[i].isNew)
            {
                canvas.FillRoundRect(UI::Rect{ right - 8, r.y + (r.h - 2) / 2 - 4, 8, 8 }, 4, NewBadge);
                right -= 14;
            }
            const std::string countText = Count(view.items[i]);
            T::DrawText(canvas, countText, UI::Rect{ text.x, text.y, right - text.x, text.h }, count);
            right -= T::MeasureText(countText, T::TextSize::Body) + 8;
            T::DrawText(canvas, ItemLabel(view, i), UI::Rect{ text.x, text.y, right - text.x, text.h }, name);
            if (i == view.cursor && listing)
                T::DrawCursor(canvas, r.x + 14, r.y + (r.h - 2) / 2);
        }
        // Scroll marks when the list goes on.
        const int arrowX = ListX + ListW + 1;
        if (view.top > 0)
            UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ arrowX, MidY + 4, 8, 8 }, ArrowColor);
        if (view.top + BagListRows < (int)view.items.size())
            UI::DrawIcon(canvas, UI::Icon::ArrowDown, UI::Rect{ arrowX, MenuBottom - 14, 8, 8 }, ArrowColor);
    }

    static void DrawParty(UI::Canvas &canvas, const BagView &view)
    {
        const bool picking = view.mode == BagMode::Pick;
        for (int i = 0; i < (int)view.party.size() && i < 6; i++)
        {
            const BagMon &mon = view.party[i];
            const UI::Rect r = { PartyX, MidY + i * PlatePitch, PartyW, PlateH };
            canvas.FillRoundRect(r, 6, PlateLine);
            const UI::Rect in = UI::Inset(r, 1);
            for (int y = 0; y < in.h; y++)
            {
                const int t = y * 255 / (in.h > 1 ? in.h - 1 : 1);
                const UI::Color c = Rgb((u8)(PlateTop.r + (PlateBottom.r - PlateTop.r) * t / 255),
                                        (u8)(PlateTop.g + (PlateBottom.g - PlateTop.g) * t / 255),
                                        (u8)(PlateTop.b + (PlateBottom.b - PlateTop.b) * t / 255));
                const int inset = y < 4 ? 4 - y : y >= in.h - 4 ? y - (in.h - 5) : 0;   // rounded ends
                canvas.HLine(in.x + inset, in.y + y, in.w - 2 * inset, c);
            }
            T::TextStyle name;
            name.size = T::TextSize::Small;
            name.color = PlateText;
            name.outline = PlateLine;
            if (!mon.held.empty())
                canvas.FillRoundRect(UI::Rect{ r.x + 4, r.y + 3, 5, 5 }, 2, HeldDot);
            if (mon.egg)
                T::DrawText(canvas, "Egg", UI::Inset(r, 12, 1, 4, 1), name);
            else
            {
                const UI::Rect bar = { r.Right() - 54, r.y + (r.h - T::GaugeHeight - 3) / 2, 48, T::GaugeHeight + 3 };
                T::DrawHpBar(canvas, bar, (u32)(mon.hp < 0 ? 0 : mon.hp), (u32)(mon.maxHp < 0 ? 0 : mon.maxHp));
                // The name first; the level after it when it fits.
                const UI::Rect text = { r.x + 12, r.y + 1, bar.x - 6 - (r.x + 12), r.h - 2 };
                const s32 nameW = T::DrawText(canvas, mon.name, text, name);
                const std::string lv = "Lv." + std::to_string(mon.level);
                if (nameW + 6 + T::MeasureText(lv, T::TextSize::Small) <= text.w)
                {
                    T::TextStyle level = name;
                    level.align = UI::Align::Right;
                    T::DrawText(canvas, lv, text, level);
                }
            }
            if (picking && i == view.partyCursor)
            {
                T::DrawHighlightFrame(canvas, UI::Inset(r, -2), 8);
                T::DrawCursor(canvas, r.x + 10, r.y + r.h / 2);
            }
            else if (!picking && view.mode != BagMode::List)
                canvas.FillRoundRect(r, 6, Shade);
        }
    }

    static void DrawMenu(UI::Canvas &canvas, const BagView &view)
    {
        const int n = (int)view.commands.size();
        for (int i = 0; i < n; i++)
        {
            const UI::Rect r = { MenuX, MenuBottom - (n - i) * T::RowPitch + 2, MenuW, T::OptionHeight };
            T::DrawRowBackground(canvas, r, T::RowKind::Option);
            T::TextStyle style;
            style.color = BagEntryEnabled(view.commands, i) ? T::OptionText : DimText;
            style.align = UI::Align::Center;
            T::DrawText(canvas, BagCommandLabel(view.commands[i]), UI::Inset(r, T::RowTextInset, 2, 10, 4), style);
            if (i == view.menuCursor)
                T::DrawCursor(canvas, r.x + 14, r.y + (r.h - 2) / 2);
        }
    }

    // The pocket icons along the bottom, the open one on a white slot; L
    // and R on either side.
    static void DrawPockets(UI::Canvas &canvas, const BagView &view)
    {
        const int shown = view.wantTab >= 0 ? view.wantTab : view.tab;
        const int y = FootY + (BagArea.Bottom() - FootY - IconSlot) / 2;
        T::DrawButtonPrompt(canvas, IconsX - T::PromptWidth(T::Button::L, "") - 4, y + 2, T::Button::L, "");
        for (int i = 0; i < BagTabCount; i++)
        {
            const UI::Rect slot = { IconsX + i * (IconSlot + 2), y, IconSlot, IconSlot };
            const bool on = i == shown;
            if (on)
                canvas.FillRoundRect(UI::Inset(slot, -1), 7, IconSlotOn);
            const UI::Image *icon = view.assets != nullptr ? view.assets->Pocket(i, on) : nullptr;
            if (icon != nullptr)
                UI::DrawImage(canvas, *icon, slot.x + (slot.w - icon->w) / 2, slot.y + (slot.h - icon->h) / 2);
            else
            {
                // No game icon: a dot with the pocket's first letter.
                canvas.FillRoundRect(UI::Inset(slot, 2), 10, on ? TabLine : PlateBottom);
                T::TextStyle letter;
                letter.size = T::TextSize::Small;
                letter.color = PlateText;
                letter.align = UI::Align::Center;
                T::DrawText(canvas, std::string(1, BagTabAt(i).label[0]), slot, letter);
            }
        }
        T::DrawButtonPrompt(canvas, IconsX + BagTabCount * (IconSlot + 2) + 2, y + 2, T::Button::R, "");
    }

    static void DrawPrompts(UI::Canvas &canvas, const BagView &view)
    {
        const int y = FootY + (BagArea.Bottom() - FootY - T::PromptHeight) / 2;
        int x = Margin;
        auto prompt = [&](T::Button button, const char *label) { x += T::DrawButtonPrompt(canvas, x, y, button, label) + 6; };
        if (view.busy)
            return;
        switch (view.mode)
        {
        case BagMode::List:
            prompt(T::Button::A, "Choose");
            prompt(T::Button::B, "Close");
            break;
        case BagMode::Menu:
        case BagMode::Pick:
            prompt(T::Button::A, "OK");
            prompt(T::Button::B, "Back");
            break;
        default:
            break;
        }
    }

    void DrawBag(UI::Canvas &canvas, const BagView &view)
    {
        DrawBackground(canvas);
        DrawParty(canvas, view);
        DrawList(canvas, view);
        if (view.mode == BagMode::Menu && !view.commands.empty())
            DrawMenu(canvas, view);
        DrawInfo(canvas, view);
        DrawPrompts(canvas, view);
        DrawPockets(canvas, view);
    }
}
