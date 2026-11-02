#include "Party.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2, main image. Found and checked as described in
    // docs/RE3-party-bag.md.
    static const u32 GameManagerGlobal = 0x6A59C4;  // GameSys::GameManager*
    static const u32 ManagerGameData   = 0x24;      // -> GameSys::GameData
    static const u32 GameDataSaveData  = 0x04;      // -> Savedata::SaveData
    static const u32 GameDataParty     = 0x0C;      // -> pml::PokeParty (player)
    static const u32 VtSaveData        = 0x64F4A0;
    static const u32 VtMyItem          = 0x64F3C4;
    static const u32 VtPokemonParam    = 0x649374;

    // Party screen (scene vtable 0x647FCC): App::PokeList::StaticWork at
    // scene +0xA8 (vtable 0x647F9C; also the battle Pokémon list's param,
    // docs/RE1-battle.md). +8 -> {pml::PokeParty*, ...}; display order u8[6]
    // at +0x53.
    static const u32 VtPartyScene = 0x647FCC, VtStaticWork = 0x647F9C;
    static const u32 SceneStaticWork = 0xA8, StaticWorkPartyRef = 0x08, StaticWorkOrder = 0x53;

    // pml::PokeParty: 6 PokemonParam pointers, u8 count at +0x18.
    static const u32 PartyCount = 0x18;
    // PokemonParam: +4 core data (0xE8), +8 party stats (0x1C).
    static const u32 ParamCore = 0x04, ParamCalc = 0x08;

    // Savedata::MyItem at SaveData +8: pocket pointer table at +0xE2C.
    static const u32 SaveDataMyItem = 0x08, MyItemPockets = 0xE2C;
    static const u16 PocketCapacity[(int)Pocket::Count] = {60, 427, 108, 67, 198, 35, 11};

    // Resident message files (docs/RE2-text.md).
    static const u32 SpeciesMsgGlobal = 0x671088;   // MsgData* (file 60)
    static const u32 MsgCacheTable = 0x6A7368;      // 8 x {.., +8 u16 dat, +0xC MsgData*}
    static const u32 VtMsgData = 0x64B104;
    static const u32 SpeciesDat = 60, ItemDat = 42;

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static bool Readable(u32 address, u32 size)
    {
        return address != 0 && Process::CheckAddress(address, MEMPERM_READ)
               && Process::CheckAddress(address + size - 1, MEMPERM_READ);
    }

    static bool ReadBytes(u32 address, u8 *out, u32 size)
    {
        if (!Readable(address, size))
            return false;
        for (u32 i = 0; i < size; i += 4)
        {
            const u32 v = Read32(address + i);
            for (u32 k = 0; k < 4 && i + k < size; k++)
                out[i + k] = (u8)(v >> (k * 8));
        }
        return true;
    }

    static u32 GameData(void)
    {
        const u32 manager = Read32(GameManagerGlobal);
        const u32 data = Readable(manager, ManagerGameData + 4) ? Read32(manager + ManagerGameData) : 0;
        return Readable(data, 0x14) ? data : 0;
    }

    // Fills state.order from the Party screen's work object if that screen
    // is up and the object refers to this party.
    static void ReadPartyScreen(u32 sceneManager, PartyState &state)
    {
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        if (scene == 0 || Read32(scene) != VtPartyScene)
            return;
        const u32 work = Read32(scene + SceneStaticWork);
        if (!Readable(work, 0x60) || Read32(work) != VtStaticWork
            || Read32(Read32(work + StaticWorkPartyRef)) != state.party)
            return;
        u8 order[6];
        u32 seen = 0;
        for (u32 i = 0; i < state.count; i++)
        {
            order[i] = Read8(work + StaticWorkOrder + i);
            if (order[i] >= state.count || (seen & (1u << order[i])))
                return;
            seen |= 1u << order[i];
        }
        for (u32 i = 0; i < state.count; i++)
            state.order[i] = order[i];
        state.partyScreen = true;
    }

    PartyState ReadParty(u32 sceneManager)
    {
        PartyState state;
        const u32 data = GameData();
        if (data == 0 || Read32(Read32(data + GameDataSaveData)) != VtSaveData)
            return state;
        const u32 party = Read32(data + GameDataParty);
        if (!Readable(party, 0x1C))
            return state;
        state.party = party;
        state.count = Read8(party + PartyCount);
        if (state.count > 6)
            return state;

        for (u32 i = 0; i < state.count; i++)
        {
            PartySlot &slot = state.slots[i];
            const u32 param = Read32(party + i * 4);
            if (!Readable(param, 0x10) || Read32(param) != VtPokemonParam)
                continue;
            slot.address = param;
            u8 core[Pk7CoreSize], calc[Pk7CalcSize];
            if (!ReadBytes(Read32(param + ParamCore), core, Pk7CoreSize))
                continue;
            const bool haveCalc = ReadBytes(Read32(param + ParamCalc), calc, Pk7CalcSize);
            slot.mon = DecodePk7(core, haveCalc ? calc : nullptr);
        }
        state.valid = true;
        ReadPartyScreen(sceneManager, state);
        return state;
    }

    BagState ReadBag(void)
    {
        BagState state;
        const u32 data = GameData();
        const u32 save = data ? Read32(data + GameDataSaveData) : 0;
        if (!Readable(save, SaveDataMyItem + MyItemPockets + 0x20) || Read32(save) != VtSaveData)
            return state;
        const u32 myItem = save + SaveDataMyItem;
        if (Read32(myItem) != VtMyItem)
            return state;
        state.myItem = myItem;

        // Each pocket must lie inside MyItem's item area (+4 .. +0xE2C).
        for (u32 p = 0; p < (u32)Pocket::Count; p++)
        {
            const u32 first = Read32(myItem + MyItemPockets + p * 4);
            if (first < myItem + 4 || first + PocketCapacity[p] * 4 > myItem + MyItemPockets)
                return state;
            state.pocket[p] = first;
            state.capacity[p] = PocketCapacity[p];
        }
        state.valid = true;
        return state;
    }

    BagSlot ReadBagSlot(const BagState &bag, Pocket pocket, u32 index)
    {
        const u32 p = (u32)pocket;
        if (!bag.valid || p >= (u32)Pocket::Count || index >= bag.capacity[p])
            return BagSlot();
        return DecodeBagSlot(Read32(bag.pocket[p] + index * 4));
    }

    const char *PocketName(Pocket pocket)
    {
        static const char *const names[] = {"Medicine", "Items", "TMs", "Berries", "Key Items", "Z-Crystals",
                                            "Rotom Powers"};
        return (u32)pocket < (u32)Pocket::Count ? names[(u32)pocket] : "?";
    }

    // One line of a full-load MsgData, checked as docs/RE2-text.md proposes.
    std::string MsgLine(u32 msg, u32 dat, u32 line)
    {
        if (!Readable(msg, 0x30) || Read32(msg) != VtMsgData || Read8(msg + 0x08) != 1 || Read32(msg + 0x0C) != dat)
            return "";
        const u32 header = Read32(msg + 0x14), section = Read32(msg + 0x18);
        if (!Readable(header, 4) || !Readable(section, 4) || line >= Read16(header + 2))
            return "";
        const u32 size = Read32(section);
        const u32 entry = section + 4 + line * 8;
        if (4 + line * 8 + 8 > size)
            return "";
        const u32 offset = Read32(entry);
        const u32 length = Read16(entry + 4);
        if (length == 0 || length > 96 || offset + length * 2 > size || !Readable(section + offset, length * 2))
            return "";

        u16 encrypted[96], text[96];
        for (u32 i = 0; i < length; i++)
            encrypted[i] = Read16(section + offset + i * 2);
        DecryptMsgLine(encrypted, length, line, text, 96);
        char utf8[3 * 96 + 1];
        Utf16ToUtf8(text, 96, utf8, sizeof(utf8));
        return utf8;
    }

    std::string SpeciesName(u16 species)
    {
        // Form 0 of a species is on line `species` (0x31D290).
        return MsgLine(Read32(SpeciesMsgGlobal), SpeciesDat, species);
    }

    std::string ItemName(u16 item)
    {
        for (u32 i = 0; i < 8; i++)
        {
            const u32 slot = MsgCacheTable + i * 0x10;
            if (Read16(slot + 0x08) == ItemDat)
                return MsgLine(Read32(slot + 0x0C), ItemDat, item);
        }
        return "";
    }

    static std::string Named(const std::string &name, u32 id)
    {
        return name.empty() ? Utils::Format("#%lu", id) : Utils::Format("%s(%lu)", name.c_str(), id);
    }

    std::string DescribeParty(const PartyState &state)
    {
        if (!state.valid)
            return "party: not found (no save loaded?)";
        static const char *const forms[] = {"invalid", "enc", "dec"};
        std::string out = Utils::Format("party: %u (%08lX)", state.count, state.party);
        for (u32 i = 0; i < state.count; i++)
        {
            const PartySlot &slot = state.slots[i];
            const Pk7 &mon = slot.mon;
            out += Utils::Format("\n  %lu %08lX", i + 1, slot.address);
            if (mon.form == Pk7Form::Invalid)
            {
                out += " unreadable";
                continue;
            }
            char nick[3 * 13 + 1];
            Utf16ToUtf8(mon.nickname, 13, nick, sizeof(nick));
            out += " " + Named(SpeciesName(mon.species), mon.species) + " \"" + nick + "\"";
            if (mon.isEgg)
                out += " egg";
            if (mon.calcValid)
                out += Utils::Format(" L%u hp %u/%u status %lX", mon.level, mon.hp, mon.maxHp, mon.status);
            else
                out += " stats?";
            out += " item " + (mon.heldItem ? Named(ItemName(mon.heldItem), mon.heldItem) : std::string("-"));
            out += " moves";
            for (u32 m = 0; m < 4; m++)
                out += Utils::Format(" %u:%u", mon.moves[m], mon.pp[m]);
            out += Utils::Format(" [%s]", forms[(int)mon.form]);
        }
        if (state.partyScreen)
        {
            out += "\n  party screen order (slot per position):";
            for (u32 i = 0; i < state.count; i++)
                out += Utils::Format(" %u", state.order[i] + 1);
        }
        return out;
    }

    std::string DescribeBag(const BagState &state)
    {
        if (!state.valid)
            return "bag: not found (no save loaded?)";
        std::string out = Utils::Format("bag: MyItem %08lX", state.myItem);
        for (u32 p = 0; p < (u32)Pocket::Count; p++)
        {
            // Slots left at count 0 (an item used up) keep their ID; the Bag
            // does not show them.
            std::string items, hidden;
            u32 used = 0;
            for (u32 i = 0; i < state.capacity[p]; i++)
            {
                const BagSlot slot = ReadBagSlot(state, (Pocket)p, i);
                if (slot.item == 0)
                    continue;
                if (slot.count == 0)
                {
                    hidden += " " + Named(ItemName(slot.item), slot.item);
                    continue;
                }
                used++;
                items += (used > 1 ? ", " : " ") + Named(ItemName(slot.item), slot.item)
                         + Utils::Format(" x%u", slot.count);
                if (slot.isNew)
                    items += " new";
                if (slot.freeSpace)
                    items += Utils::Format(" free%u", slot.freeSpace);
            }
            out += Utils::Format("\n  %s %lu/%u:", PocketName((Pocket)p), used, state.capacity[p]) + items;
            if (!hidden.empty())
                out += " | count 0:" + hidden;
        }
        return out;
    }
}
